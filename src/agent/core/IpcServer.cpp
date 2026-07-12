#include "IpcServer.h"

#include "CdpClient.h"
#include "HttpProxyMapper.h"
#include "shared/ipc/FramedJsonSocket.h"
#include "shared/ipc/IpcNames.h"

#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTimer>
#include <QUrl>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHostAddress>
#include <QTcpServer>
#include <functional>
#include <memory>

IpcServer::IpcServer(QObject* parent) : QObject(parent) {
  m_server = new QLocalServer(this);
  QObject::connect(m_server, &QLocalServer::newConnection, this, &IpcServer::onNewConnection);
}

IpcServer::~IpcServer() {
  const auto profileKeys = m_profileProcByProfileId.keys();
  for (const auto& k : profileKeys) {
    QProcess* p = m_profileProcByProfileId.take(k);
    if (p) {
      p->kill();
      p->deleteLater();
    }
  }

  const auto cdpKeys = m_cdpByProfileId.keys();
  for (const auto& k : cdpKeys) {
    CdpClient* c = m_cdpByProfileId.take(k);
    if (c) {
      c->stop();
      c->deleteLater();
    }
  }

  const auto mapKeys = m_proxyMapperByProfileId.keys();
  for (const auto& k : mapKeys) {
    HttpProxyMapper* m = m_proxyMapperByProfileId.take(k);
    if (m) {
      m->stop();
      m->deleteLater();
    }
  }

  const auto keys = m_openvpnByProfileId.keys();
  for (const auto& k : keys) {
    QProcess* p = m_openvpnByProfileId.take(k);
    if (p) {
      p->kill();
      p->deleteLater();
    }
  }

  const auto extKeys = m_chromeProxyAuthExtDirByProfileId.keys();
  for (const auto& k : extKeys) {
    const QString dir = m_chromeProxyAuthExtDirByProfileId.take(k);
    if (!dir.isEmpty()) {
      QDir(dir).removeRecursively();
    }
  }

  const auto authKeys = m_openvpnSocksAuthFileByProfileId.keys();
  for (const auto& k : authKeys) {
    const QString path = m_openvpnSocksAuthFileByProfileId.take(k);
    if (!path.isEmpty()) {
      QFile::remove(path);
    }
  }
}

bool IpcServer::start() {
  QLocalServer::removeServer(QString::fromUtf8(IpcNames::kAgentServerName));
  return m_server->listen(QString::fromUtf8(IpcNames::kAgentServerName));
}

void IpcServer::onNewConnection() {
  while (m_server->hasPendingConnections()) {
    QLocalSocket* sock = m_server->nextPendingConnection();
    if (!sock) {
      continue;
    }

    if (m_peer) {
      m_peer->deleteLater();
      m_peer = nullptr;
    }

    m_peer = new FramedJsonSocket(sock, this);
    QObject::connect(m_peer, &FramedJsonSocket::disconnected, this, &IpcServer::onPeerDisconnected);
    QObject::connect(m_peer, &FramedJsonSocket::ioError, this, &IpcServer::onPeerError);
    QObject::connect(m_peer, &FramedJsonSocket::jsonReceived, this, &IpcServer::onPeerJson);

    QJsonObject msg;
    msg.insert(QStringLiteral("type"), QStringLiteral("log.line"));
    msg.insert(QStringLiteral("message"), QStringLiteral("agent_connected"));
    m_peer->send(msg);
  }
}

void IpcServer::onPeerDisconnected() {
  emit logLine(QStringLiteral("peer_disconnected"));
}

void IpcServer::onPeerError(const QString& message) {
  emit logLine(QStringLiteral("peer_error: %1").arg(message));
}

void IpcServer::onPeerJson(const QJsonObject& obj) {
  if (!m_peer) {
    return;
  }

  const QString type = obj.value(QStringLiteral("type")).toString();
  if (type == QStringLiteral("hello")) {
    QJsonObject ack;
    ack.insert(QStringLiteral("type"), QStringLiteral("hello.ack"));
    ack.insert(QStringLiteral("agent"), QStringLiteral("dokebrowser_agent"));
    ack.insert(QStringLiteral("version"), 1);
    m_peer->send(ack);

    QJsonObject log;
    log.insert(QStringLiteral("type"), QStringLiteral("log.line"));
    log.insert(QStringLiteral("message"), QStringLiteral("hello_ok"));
    m_peer->send(log);
    return;
  }

  if (type == QStringLiteral("profile.start") || type == QStringLiteral("profile.stop")) {
    const QString profileId = obj.value(QStringLiteral("profile_id")).toString().trimmed();
    const QString profileName = obj.value(QStringLiteral("profile_name")).toString();
    const QString dataDirFromMsg = obj.value(QStringLiteral("data_dir")).toString();
    const QString fingerprintMode = obj.value(QStringLiteral("fingerprint_mode")).toString().trimmed();
    const QString language = obj.value(QStringLiteral("language")).toString().trimmed();
    const QString userAgent = obj.value(QStringLiteral("user_agent")).toString().trimmed();
    const QString platform = obj.value(QStringLiteral("platform")).toString().trimmed();
    const int hardwareConcurrency = obj.value(QStringLiteral("hardware_concurrency")).toInt(0);
    const int deviceMemoryGb = obj.value(QStringLiteral("device_memory_gb")).toInt(0);
    const double deviceScaleFactor = obj.value(QStringLiteral("device_scale_factor")).toDouble(0);
    const QString timezone = obj.value(QStringLiteral("timezone")).toString().trimmed();
    const QString resolution = obj.value(QStringLiteral("resolution")).toString().trimmed();
    const bool touchEnabled = obj.value(QStringLiteral("touch_enabled")).toBool(false);
    const bool geoEnabled = obj.value(QStringLiteral("geo_enabled")).toBool(false);
    const double geoLatitude = obj.value(QStringLiteral("geo_latitude")).toDouble(0);
    const double geoLongitude = obj.value(QStringLiteral("geo_longitude")).toDouble(0);
    const double geoAccuracy = obj.value(QStringLiteral("geo_accuracy")).toDouble(0);
    const QJsonObject proxyObj = obj.value(QStringLiteral("proxy")).toObject();
    const bool chromeCompatRequested = obj.value(QStringLiteral("chrome_compat")).toBool(false);
    bool chromeCompat = chromeCompatRequested;
    if (!chromeCompatRequested && qEnvironmentVariableIsSet("TRAE_SANDBOX_STORAGE_PATH")) {
      chromeCompat = true;
    }

    auto sendStatus = [this, profileId](const QString& status, const QString& error) {
      if (!m_peer) {
        return;
      }
      QJsonObject msg;
      msg.insert(QStringLiteral("type"), QStringLiteral("profile.status"));
      msg.insert(QStringLiteral("profile_id"), profileId);
      msg.insert(QStringLiteral("status"), status);
      msg.insert(QStringLiteral("error"), error);
      m_peer->send(msg);
    };

    if (profileId.isEmpty()) {
      sendStatus(QStringLiteral("error"), QStringLiteral("missing_profile_id"));
      return;
    }

    QJsonObject log;
    log.insert(QStringLiteral("type"), QStringLiteral("log.line"));
    log.insert(QStringLiteral("message"),
               QStringLiteral("%1 profile=%2 (%3)")
                   .arg(type, profileName.isEmpty() ? QStringLiteral("-") : profileName,
                        profileId.isEmpty() ? QStringLiteral("-") : profileId));
    m_peer->send(log);

    auto resolveChrome = []() -> QString {
      const QStringList directCandidates = {
#if defined(Q_OS_MAC)
        QStringLiteral("/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"),
        QStringLiteral("/Applications/Chromium.app/Contents/MacOS/Chromium"),
#endif
#if defined(Q_OS_WIN)
        QString(),
#endif
      };

      for (const auto& c : directCandidates) {
        if (c.isEmpty()) {
          continue;
        }
        if (QFileInfo::exists(c)) {
          return c;
        }
      }

#if defined(Q_OS_WIN)
      const QString pf = qEnvironmentVariable("ProgramFiles");
      const QString pfx86 = qEnvironmentVariable("ProgramFiles(x86)");
      const QString local = qEnvironmentVariable("LocalAppData");
      const QStringList winCandidates = {
        pf.isEmpty() ? QString() : (pf + QStringLiteral("\\Google\\Chrome\\Application\\chrome.exe")),
        pfx86.isEmpty() ? QString() : (pfx86 + QStringLiteral("\\Google\\Chrome\\Application\\chrome.exe")),
        local.isEmpty() ? QString() : (local + QStringLiteral("\\Google\\Chrome\\Application\\chrome.exe")),
        pf.isEmpty() ? QString() : (pf + QStringLiteral("\\Chromium\\Application\\chrome.exe")),
        pfx86.isEmpty() ? QString() : (pfx86 + QStringLiteral("\\Chromium\\Application\\chrome.exe")),
      };
      for (const auto& c : winCandidates) {
        if (c.isEmpty()) {
          continue;
        }
        if (QFileInfo::exists(c)) {
          return c;
        }
      }
#endif

      const QStringList names = {
        QStringLiteral("google-chrome-stable"),
        QStringLiteral("google-chrome"),
        QStringLiteral("chrome"),
        QStringLiteral("chromium"),
        QStringLiteral("chromium-browser"),
      };
      for (const auto& n : names) {
        const QString p = QStandardPaths::findExecutable(n);
        if (!p.isEmpty()) {
          return p;
        }
      }
      return {};
    };

    auto resolveProfileDataDir = [profileId, dataDirFromMsg]() -> QString {
      const QString v = dataDirFromMsg.trimmed();
      if (!v.isEmpty()) {
        return v;
      }
      const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
      if (base.isEmpty()) {
        return {};
      }
      return QDir(base).filePath(QStringLiteral("profiles/%1/chrome").arg(profileId));
    };

    auto sanitizeKey = [](const QString& s) -> QString {
      QString out;
      out.reserve(s.size());
      for (const auto& ch : s) {
        if ((ch >= QLatin1Char('a') && ch <= QLatin1Char('z')) || (ch >= QLatin1Char('A') && ch <= QLatin1Char('Z'))
            || (ch >= QLatin1Char('0') && ch <= QLatin1Char('9')) || ch == QLatin1Char('_') || ch == QLatin1Char('-')) {
          out.push_back(ch);
        } else {
          out.push_back(QLatin1Char('_'));
        }
      }
      if (out.isEmpty()) {
        out = QStringLiteral("p");
      }
      return out;
    };

    auto jsEscape = [](const QString& s) -> QString {
      QString out;
      out.reserve(s.size() + 8);
      for (const auto& ch : s) {
        if (ch == QLatin1Char('\\')) {
          out += QStringLiteral("\\\\");
        } else if (ch == QLatin1Char('"')) {
          out += QStringLiteral("\\\"");
        } else if (ch == QLatin1Char('\n')) {
          out += QStringLiteral("\\n");
        } else if (ch == QLatin1Char('\r')) {
          out += QStringLiteral("\\r");
        } else if (ch == QLatin1Char('\t')) {
          out += QStringLiteral("\\t");
        } else {
          out += ch;
        }
      }
      return out;
    };

    auto cleanupProxyAuthExt = [this](const QString& pid) {
      const QString dir = m_chromeProxyAuthExtDirByProfileId.take(pid);
      if (!dir.isEmpty()) {
        QDir(dir).removeRecursively();
      }
    };

    auto cleanupProxyMapping = [this](const QString& pid) {
      HttpProxyMapper* m = m_proxyMapperByProfileId.take(pid);
      if (m) {
        m->stop();
        m->deleteLater();
      }
    };

    auto cleanupCdp = [this](const QString& pid) {
      CdpClient* c = m_cdpByProfileId.take(pid);
      if (c) {
        c->stop();
        c->deleteLater();
      }
    };

    const quint32 fpSeed = qHash(profileId);

    auto createChromeExt =
        [this, profileId, fpSeed, sanitizeKey, jsEscape, fingerprintMode, language, userAgent, platform, hardwareConcurrency,
         deviceMemoryGb, deviceScaleFactor, timezone, resolution, touchEnabled, geoEnabled, geoLatitude, geoLongitude, geoAccuracy](
            const QString& scheme, const QString& host, int port, const QString& username, const QString& password,
            bool enableProxyAuth) -> QString {
      const QString base = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
      if (base.isEmpty()) {
        return {};
      }
      const QString dir = QDir(base).filePath(
          QStringLiteral("dokebrowser_ext/%1_%2").arg(sanitizeKey(profileId), QString::number(QDateTime::currentMSecsSinceEpoch())));
      if (!QDir().mkpath(dir)) {
        return {};
      }

      QFile mf(QDir(dir).filePath(QStringLiteral("manifest.json")));
      if (!mf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QDir(dir).removeRecursively();
        return {};
      }
      QByteArray manifest;
      if (enableProxyAuth) {
        manifest = QByteArrayLiteral(
            "{\"name\":\"Doke Profile\",\"version\":\"1.0.0\",\"manifest_version\":3,"
            "\"permissions\":[\"proxy\",\"storage\",\"tabs\",\"webRequest\",\"webRequestAuthProvider\"],"
            "\"host_permissions\":[\"<all_urls>\"],"
            "\"background\":{\"service_worker\":\"background.js\"},"
            "\"content_scripts\":[{\"matches\":[\"<all_urls>\"],\"js\":[\"inject.js\",\"content.js\"],\"run_at\":\"document_start\",\"world\":\"MAIN\"}],"
            "\"web_accessible_resources\":[{\"resources\":[\"inject.js\"],\"matches\":[\"<all_urls>\"]}]"
            "}");
      } else {
        manifest = QByteArrayLiteral(
            "{\"name\":\"Doke Profile\",\"version\":\"1.0.0\",\"manifest_version\":3,"
            "\"permissions\":[\"storage\",\"tabs\"],"
            "\"host_permissions\":[\"<all_urls>\"],"
            "\"background\":{\"service_worker\":\"background.js\"},"
            "\"content_scripts\":[{\"matches\":[\"<all_urls>\"],\"js\":[\"inject.js\",\"content.js\"],\"run_at\":\"document_start\",\"world\":\"MAIN\"}],"
            "\"web_accessible_resources\":[{\"resources\":[\"inject.js\"],\"matches\":[\"<all_urls>\"]}]"
            "}");
      }
      mf.write(manifest);
      mf.close();
      QFile::setPermissions(mf.fileName(), QFile::ReadOwner | QFile::WriteOwner);

      QFile bg(QDir(dir).filePath(QStringLiteral("background.js")));
      if (!bg.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QDir(dir).removeRecursively();
        return {};
      }
      QString bgJs;
      if (enableProxyAuth) {
        bgJs = QStringLiteral(
                   "const openCheck=()=>{try{chrome.tabs.create({url:chrome.runtime.getURL('check.html')});}catch(e){}};"
                   "const applyProxy=()=>{"
                   "const config={mode:\"fixed_servers\",rules:{singleProxy:{scheme:\"%1\",host:\"%2\",port:parseInt(%3)},bypassList:[\"localhost\",\"127.0.0.1\"]}};"
                   "chrome.proxy.settings.set({value:config,scope:\"regular\"},()=>{});"
                   "};"
                   "chrome.runtime.onInstalled.addListener(()=>{applyProxy();openCheck();});"
                   "chrome.runtime.onStartup.addListener(()=>{applyProxy();});"
                   "chrome.webRequest.onAuthRequired.addListener((details)=>{"
                   "if(!details||!details.isProxy){return {};}"
                   "return {authCredentials:{username:\"%4\",password:\"%5\"}};"
                   "},{urls:[\"<all_urls>\"]},[\"blocking\"]);")
                   .arg(jsEscape(scheme), jsEscape(host), QString::number(port), jsEscape(username), jsEscape(password));
      } else {
        bgJs = QStringLiteral(
            "const openCheck=()=>{try{chrome.tabs.create({url:chrome.runtime.getURL('check.html')});}catch(e){}};"
            "chrome.runtime.onInstalled.addListener(()=>{openCheck();});");
      }
      bg.write(bgJs.toUtf8());
      bg.close();
      QFile::setPermissions(bg.fileName(), QFile::ReadOwner | QFile::WriteOwner);

      QFile content(QDir(dir).filePath(QStringLiteral("content.js")));
      if (!content.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QDir(dir).removeRecursively();
        return {};
      }
      const QString contentJs = QStringLiteral(
                                    "(function(){try{"
                                    "const root=(document.documentElement||document.documentElement||document.body);"
                                    "if(root){"
                                    "root.dataset.dokeInjected='1';"
                                    "root.dataset.dokeMode=\"%5\";"
                                    "root.dataset.dokeLang=\"%1\";"
                                    "root.dataset.dokeTz=\"%2\";"
                                    "root.dataset.dokeTouch=\"%3\";"
                                    "root.dataset.dokeRes=\"%4\";"
                                    "root.dataset.dokePlat=\"%6\";"
                                    "}"
                                    "}catch(e){}})();")
                                    .arg(jsEscape(language.trimmed()), jsEscape(timezone.trimmed()),
                                         touchEnabled ? QStringLiteral("1") : QStringLiteral("0"), jsEscape(resolution.trimmed()),
                                         jsEscape(fingerprintMode.trimmed()), jsEscape(platform.trimmed()));
      content.write(contentJs.toUtf8());
      content.close();
      QFile::setPermissions(content.fileName(), QFile::ReadOwner | QFile::WriteOwner);

      QFile inject(QDir(dir).filePath(QStringLiteral("inject.js")));
      if (!inject.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QDir(dir).removeRecursively();
        return {};
      }

      QString inj;
      const QString lang = language.trimmed();
      const QString tz = timezone.trimmed();
      const QString plat = platform.trimmed();
      int resW = 0;
      int resH = 0;
      if (!resolution.trimmed().isEmpty()) {
        const auto parts = resolution.trimmed().split('x');
        if (parts.size() == 2) {
          bool okW = false;
          bool okH = false;
          resW = parts.at(0).trimmed().toInt(&okW);
          resH = parts.at(1).trimmed().toInt(&okH);
          if (!okW || !okH) {
            resW = 0;
            resH = 0;
          }
        }
      }
      const double dpr = deviceScaleFactor > 0 ? deviceScaleFactor : 0;
      if (!lang.isEmpty()) {
        const QString primary = lang.contains('-') ? lang.left(lang.indexOf('-')) : lang;
        const QString langs = (primary.isEmpty() || primary == lang) ? QStringLiteral("['%1']").arg(jsEscape(lang))
                                                                     : QStringLiteral("['%1','%2']").arg(jsEscape(lang), jsEscape(primary));
        inj += QStringLiteral(
                   "(function(){try{"
                   "const define=(obj,key,getter)=>{try{Object.defineProperty(obj,key,{get:getter,configurable:true});}catch(e){}};"
                   "define(Navigator.prototype,'language',()=>\"%1\");"
                   "define(navigator,'language',()=>\"%1\");"
                   "define(Navigator.prototype,'languages',()=>%2);"
                   "define(navigator,'languages',()=>%2);"
                   "}catch(e){}})();")
                   .arg(jsEscape(lang), langs);
      }
      if (!tz.isEmpty()) {
        inj += QStringLiteral(
                   "(function(){try{"
                   "const tz=\"%1\";"
                   "try{"
                   "const Original=Intl.DateTimeFormat;"
                   "Intl.DateTimeFormat=function(){"
                   "const dtf=new Original(...arguments);"
                   "const ro=dtf.resolvedOptions?dtf.resolvedOptions.bind(dtf):null;"
                   "if(ro){dtf.resolvedOptions=function(){const o=ro();try{o.timeZone=tz;}catch(e){}return o;};}"
                   "return dtf;"
                   "};"
                   "Intl.DateTimeFormat.prototype=Original.prototype;"
                   "}catch(e){}"
                   "}catch(e){}})();")
                   .arg(jsEscape(tz));
      }
      inj += QStringLiteral(
          "(function(){try{"
          "try{Object.defineProperty(Navigator.prototype,'webdriver',{get:()=>undefined,configurable:true});}catch(e){}"
          "try{Object.defineProperty(navigator,'webdriver',{get:()=>undefined,configurable:true});}catch(e){}"
          "}catch(e){}})();");

      inj += QStringLiteral(
          "(function(){try{"
          "const RTCP=window.RTCPeerConnection||window.webkitRTCPeerConnection;"
          "if(!RTCP){return;}"
          "const proto=RTCP.prototype;"
          "const wrap=(fn)=>function(ev){"
          "try{"
          "if(ev&&ev.candidate&&ev.candidate.candidate){"
          "const s=String(ev.candidate.candidate||'');"
          "if(/\\btyp\\s+(host|srflx)\\b/i.test(s)){return;}"
          "}"
          "}catch(e){}"
          "return fn.call(this,ev);"
          "};"
          "try{"
          "const orig=proto.addEventListener;"
          "if(orig){"
          "proto.addEventListener=function(t,fn,opt){"
          "if(t==='icecandidate'&&typeof fn==='function'){return orig.call(this,t,wrap(fn),opt);}"
          "return orig.call(this,t,fn,opt);"
          "};"
          "}"
          "}catch(e){}"
          "try{"
          "let h=null;"
          "Object.defineProperty(proto,'onicecandidate',{"
          "configurable:true,enumerable:true,"
          "get(){return h;},"
          "set(fn){h=(typeof fn==='function')?wrap(fn):fn;}"
          "});"
          "}catch(e){}"
          "}catch(e){}})();");

      inj += QStringLiteral(
                 "(function(){try{"
                 "const seed=(%1>>>0);"
                 "function mulberry32(a){return function(){var t=a+=0x6D2B79F5;t=Math.imul(t^t>>>15,t|1);t^=t+Math.imul(t^t>>>7,t|61);return ((t^t>>>14)>>>0)/4294967296;};}"
                 "const rnd=mulberry32(seed);"
                 "const sgn=()=>rnd()<0.5?-1:1;"
                 "try{"
                 "const P=window.CanvasRenderingContext2D&&CanvasRenderingContext2D.prototype;"
                 "if(P&&P.getImageData){"
                 "const orig=P.getImageData;"
                 "P.getImageData=function(){"
                 "const d=orig.apply(this,arguments);"
                 "try{"
                 "const n=Math.min(10,Math.floor(d.data.length/4));"
                 "for(let i=0;i<n;i++){"
                 "const p=(Math.floor(rnd()*(d.data.length/4))|0)*4;"
                 "d.data[p]=(d.data[p]+sgn())&255;"
                 "}"
                 "}catch(e){}"
                 "return d;"
                 "};"
                 "}"
                 "}catch(e){}"
                 "try{"
                 "const patchGL=(GL)=>{if(!GL||!GL.prototype){return;}const proto=GL.prototype;"
                 "if(proto.readPixels){const orig=proto.readPixels;"
                 "proto.readPixels=function(){orig.apply(this,arguments);try{const a=arguments[6];if(a&&a.length){a[0]=(a[0]+1)&255;}}catch(e){}};}"
                 "};"
                 "patchGL(window.WebGLRenderingContext);"
                 "patchGL(window.WebGL2RenderingContext);"
                 "}catch(e){}"
                 "try{"
                 "const AB=window.AudioBuffer&&AudioBuffer.prototype;"
                 "if(AB&&AB.getChannelData){const orig=AB.getChannelData;"
                 "AB.getChannelData=function(){"
                 "const a=orig.apply(this,arguments);"
                 "try{"
                 "const out=new Float32Array(a.length);out.set(a);"
                 "const n=Math.min(20,out.length);"
                 "for(let i=0;i<n;i++){const p=(Math.floor(rnd()*out.length)|0);out[p]=out[p]+(sgn()*1e-7);}return out;"
                 "}catch(e){}"
                 "return a;"
                 "};}"
                 "}catch(e){}"
                 "}catch(e){}})();")
                 .arg(QString::number(fpSeed));

      if (!plat.isEmpty()) {
        inj += QStringLiteral(
                   "(function(){try{"
                   "const v=\"%1\";"
                   "try{Object.defineProperty(Navigator.prototype,'platform',{get:()=>v,configurable:true});}catch(e){}"
                   "try{Object.defineProperty(navigator,'platform',{get:()=>v,configurable:true});}catch(e){}"
                   "}catch(e){}})();")
                   .arg(jsEscape(plat));
      }
      if (hardwareConcurrency > 0) {
        inj += QStringLiteral(
                   "(function(){try{"
                   "const v=%1;"
                   "try{Object.defineProperty(Navigator.prototype,'hardwareConcurrency',{get:()=>v,configurable:true});}catch(e){}"
                   "try{Object.defineProperty(navigator,'hardwareConcurrency',{get:()=>v,configurable:true});}catch(e){}"
                   "}catch(e){}})();")
                   .arg(QString::number(hardwareConcurrency));
      }
      if (deviceMemoryGb > 0) {
        inj += QStringLiteral(
                   "(function(){try{"
                   "const v=%1;"
                   "try{Object.defineProperty(Navigator.prototype,'deviceMemory',{get:()=>v,configurable:true});}catch(e){}"
                   "try{Object.defineProperty(navigator,'deviceMemory',{get:()=>v,configurable:true});}catch(e){}"
                   "}catch(e){}})();")
                   .arg(QString::number(deviceMemoryGb));
      }
      if (resW > 0 && resH > 0) {
        inj += QStringLiteral(
                   "(function(){try{"
                   "const w=%1;const h=%2;"
                   "try{const S=(window.Screen&&Screen.prototype)?Screen.prototype:null;"
                   "if(S){"
                   "const def=(k,v)=>{try{Object.defineProperty(S,k,{get:()=>v,configurable:true});}catch(e){}};"
                   "def('width',w);def('height',h);def('availWidth',w);def('availHeight',h);"
                   "}"
                   "}catch(e){}"
                   "}catch(e){}})();")
                   .arg(QString::number(resW), QString::number(resH));
      }
      if (dpr > 0) {
        inj += QStringLiteral(
                   "(function(){try{"
                   "const v=%1;"
                   "try{Object.defineProperty(window,'devicePixelRatio',{get:()=>v,configurable:true});}catch(e){}"
                   "}catch(e){}})();")
                   .arg(QString::number(dpr, 'f', 2));
      }
      if (touchEnabled) {
        inj += QStringLiteral(
            "try{Object.defineProperty(navigator,'maxTouchPoints',{get:()=>5,configurable:true});}catch(e){}"
            "try{Object.defineProperty(navigator,'msMaxTouchPoints',{get:()=>5,configurable:true});}catch(e){}"
            "try{if(!('ontouchstart'in window)){Object.defineProperty(window,'ontouchstart',{value:null,configurable:true,writable:true});}}catch(e){}");
      }
      if (geoEnabled) {
        inj += QStringLiteral(
                   "(function(){try{"
                   "const lat=%1;const lon=%2;const acc=%3;"
                   "const geo=navigator.geolocation;"
                   "if(!geo){return;}"
                   "const makePos=()=>({coords:{latitude:lat,longitude:lon,accuracy:acc,altitude:null,altitudeAccuracy:null,heading:null,speed:null},timestamp:Date.now()});"
                   "try{geo.getCurrentPosition=function(s,e,o){try{s&&s(makePos());}catch(err){try{e&&e(err);}catch(_){} }};}catch(e){}"
                   "try{geo.watchPosition=function(s,e,o){const id=Math.floor(Math.random()*1e9);try{s&&s(makePos());}catch(err){try{e&&e(err);}catch(_){} } return id;};}catch(e){}"
                   "try{geo.clearWatch=function(id){};}catch(e){}"
                   "}catch(e){}})();")
                   .arg(QString::number(geoLatitude, 'f', 6), QString::number(geoLongitude, 'f', 6),
                        QString::number(geoAccuracy > 0 ? geoAccuracy : 1000, 'f', 0));
      }
      const QString geoObj =
          geoEnabled ? QStringLiteral("{enabled:true,lat:%1,lon:%2,acc:%3}")
                           .arg(QString::number(geoLatitude, 'f', 6), QString::number(geoLongitude, 'f', 6),
                                QString::number(geoAccuracy > 0 ? geoAccuracy : 1000, 'f', 0))
                     : QStringLiteral("{enabled:false}");
      inj += QStringLiteral(
                 "try{window.__DOKE_INJECTED={v:3,seed:%10,mode:\"%4\",lang:\"%1\",tz:\"%2\",plat:\"%5\",hc:%6,mem:%7,dpr:%8,res:\"%9\",touch:%3,geo:%11,ts:Date.now()};}catch(e){}")
                 .arg(jsEscape(lang))
                 .arg(jsEscape(tz))
                 .arg(touchEnabled ? QStringLiteral("true") : QStringLiteral("false"))
                 .arg(jsEscape(fingerprintMode.trimmed()))
                 .arg(jsEscape(plat))
                 .arg(QString::number(hardwareConcurrency))
                 .arg(QString::number(deviceMemoryGb))
                 .arg(QString::number(dpr, 'f', 2))
                 .arg(jsEscape(resolution.trimmed()))
                 .arg(QString::number(fpSeed))
                 .arg(geoObj);
      if (inj.isEmpty()) {
        inj = QStringLiteral(";");
      }
      inject.write(inj.toUtf8());
      inject.close();
      QFile::setPermissions(inject.fileName(), QFile::ReadOwner | QFile::WriteOwner);

      QFile checkHtml(QDir(dir).filePath(QStringLiteral("check.html")));
      if (!checkHtml.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QDir(dir).removeRecursively();
        return {};
      }
      checkHtml.write(QByteArrayLiteral(
          "<!doctype html><html><head><meta charset=\"utf-8\"><title>Doke 注入自检</title>"
          "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
          "<style>body{font-family:-apple-system,BlinkMacSystemFont,Segoe UI,Roboto,Helvetica,Arial,sans-serif;padding:16px;}"
          ".k{color:#666;width:180px;display:inline-block;}pre{background:#f6f8fa;padding:12px;border-radius:8px;overflow:auto;}"
          "</style></head><body><h2>Doke 注入自检</h2>"
          "<p>这个页面会在扩展上下文直接运行同一份 inject.js，并展示关键指纹字段。</p>"
          "<div id=\"rows\"></div><h3>Raw</h3><pre id=\"raw\"></pre>"
          "<script src=\"inject.js\"></script><script src=\"check.js\"></script></body></html>"));
      checkHtml.close();
      QFile::setPermissions(checkHtml.fileName(), QFile::ReadOwner | QFile::WriteOwner);

      QFile checkJs(QDir(dir).filePath(QStringLiteral("check.js")));
      if (!checkJs.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QDir(dir).removeRecursively();
        return {};
      }
      checkJs.write(QByteArrayLiteral(
          "(function(){\n"
          "  const rows=document.getElementById('rows');\n"
          "  const add=(k,v)=>{const d=document.createElement('div');d.innerHTML='<span class=\"k\">'+k+':</span> '+String(v);rows.appendChild(d);};\n"
          "  let tz='';\n"
          "  try{tz=Intl.DateTimeFormat().resolvedOptions().timeZone||'';}catch(e){}\n"
          "  add('navigator.userAgent', navigator.userAgent);\n"
          "  add('navigator.language', navigator.language);\n"
          "  add('navigator.languages', JSON.stringify(navigator.languages));\n"
          "  add('navigator.platform', navigator.platform);\n"
          "  add('navigator.hardwareConcurrency', navigator.hardwareConcurrency);\n"
          "  add('navigator.deviceMemory', navigator.deviceMemory);\n"
          "  add('devicePixelRatio', window.devicePixelRatio);\n"
          "  add('screen', (window.screen? (screen.width+'x'+screen.height) : ''));\n"
          "  add('Intl tz', tz);\n"
          "  add('navigator.webdriver', navigator.webdriver);\n"
          "  add('navigator.maxTouchPoints', navigator.maxTouchPoints);\n"
          "  add('ontouchstart in window', ('ontouchstart' in window));\n"
          "  add('__DOKE_INJECTED', window.__DOKE_INJECTED ? JSON.stringify(window.__DOKE_INJECTED) : '');\n"
          "  const raw={\n"
          "    userAgent:navigator.userAgent,\n"
          "    language:navigator.language,\n"
          "    languages:navigator.languages,\n"
          "    platform:navigator.platform,\n"
          "    hardwareConcurrency:navigator.hardwareConcurrency,\n"
          "    deviceMemory:navigator.deviceMemory,\n"
          "    devicePixelRatio:window.devicePixelRatio,\n"
          "    screen: window.screen? {w:screen.width,h:screen.height,aw:screen.availWidth,ah:screen.availHeight}:null,\n"
          "    tz,\n"
          "    webdriver:navigator.webdriver,\n"
          "    maxTouchPoints:navigator.maxTouchPoints,\n"
          "    ontouchstart:('ontouchstart' in window),\n"
          "    marker:window.__DOKE_INJECTED||null\n"
          "  };\n"
          "  document.getElementById('raw').textContent=JSON.stringify(raw,null,2);\n"
          "})();\n"));
      checkJs.close();
      QFile::setPermissions(checkJs.fileName(), QFile::ReadOwner | QFile::WriteOwner);

      m_chromeProxyAuthExtDirByProfileId.insert(profileId, dir);
      return dir;
    };

    QString proxyScheme;
    QString proxyHost;
    int proxyPort = 0;
    QString proxyUsername;
    QString proxyPassword;
    bool enableProxyAuth = false;

    auto buildProxyArg = [this, proxyObj, &proxyScheme, &proxyHost, &proxyPort, &proxyUsername, &proxyPassword, &enableProxyAuth,
                          cleanupProxyAuthExt, cleanupProxyMapping, profileId]() -> QString {
      cleanupProxyAuthExt(profileId);
      cleanupProxyMapping(profileId);
      enableProxyAuth = false;
      proxyScheme.clear();
      proxyHost.clear();
      proxyPort = 0;
      proxyUsername.clear();
      proxyPassword.clear();

      const bool enabled = proxyObj.value(QStringLiteral("enabled")).toBool(false);
      if (!enabled) {
        return {};
      }
      const QString type = proxyObj.value(QStringLiteral("type")).toString().trimmed().toLower();
      const QString host = proxyObj.value(QStringLiteral("host")).toString().trimmed();
      const int port = proxyObj.value(QStringLiteral("port")).toInt(0);
      const QString username = proxyObj.value(QStringLiteral("username")).toString();
      const QString password = proxyObj.value(QStringLiteral("password")).toString();
      const bool hasAuth = !username.isEmpty() || !password.isEmpty();

      if (type.isEmpty() || type == QStringLiteral("direct")) {
        return {};
      }
      if (host.isEmpty() || port <= 0) {
        return {};
      }

      QString scheme = QStringLiteral("http");
      if (type == QStringLiteral("socks5")) {
        scheme = QStringLiteral("socks5");
      } else if (type == QStringLiteral("https")) {
        scheme = QStringLiteral("https");
      }

      if (scheme == QStringLiteral("socks5") && hasAuth) {
        const QString u = QString::fromUtf8(QUrl::toPercentEncoding(username));
        const QString p = QString::fromUtf8(QUrl::toPercentEncoding(password));
        return QStringLiteral("--proxy-server=socks5://%1:%2@%3:%4").arg(u, p, host, QString::number(port));
      }

      proxyScheme = scheme;
      proxyHost = host;
      proxyPort = port;
      proxyUsername = username;
      proxyPassword = password;
      enableProxyAuth = hasAuth;

      if (hasAuth && (scheme == QStringLiteral("http") || scheme == QStringLiteral("https"))) {
        HttpProxyMapper::Upstream u;
        u.scheme = scheme;
        u.host = host;
        u.port = port;
        u.username = username;
        u.password = password;
        auto* mapper = new HttpProxyMapper(u, this);
        QString err;
        if (!mapper->start(&err)) {
          mapper->deleteLater();
          return QStringLiteral("--proxy-server=%1://%2:%3").arg(scheme, host, QString::number(port));
        }
        m_proxyMapperByProfileId.insert(profileId, mapper);
        enableProxyAuth = false;
        return QStringLiteral("--proxy-server=http://127.0.0.1:%1").arg(QString::number(mapper->localPort()));
      }

      return QStringLiteral("--proxy-server=%1://%2:%3").arg(scheme, host, QString::number(port));
    };

    if (type == QStringLiteral("profile.stop")) {
      QProcess* existing = m_profileProcByProfileId.value(profileId);
      if (!existing || existing->state() == QProcess::NotRunning) {
        sendStatus(QStringLiteral("stopped"), QString());
        if (existing) {
          m_profileProcByProfileId.remove(profileId);
          existing->deleteLater();
        }
        return;
      }

      if (!m_profileStopRequested.contains(profileId)) {
        m_profileStopRequested.insert(profileId);
      }
      sendStatus(QStringLiteral("stopping"), QString());
      existing->terminate();
      QTimer::singleShot(1200, this, [this, profileId]() {
        QProcess* p = m_profileProcByProfileId.value(profileId);
        if (!p) {
          return;
        }
        if (p->state() == QProcess::NotRunning) {
          return;
        }
        p->kill();
      });
      return;
    }

    QProcess* existing = m_profileProcByProfileId.value(profileId);
    if (existing && existing->state() != QProcess::NotRunning) {
      sendStatus(QStringLiteral("running"), QString());
      return;
    }
    if (existing) {
      m_profileProcByProfileId.remove(profileId);
      existing->deleteLater();
      existing = nullptr;
    }

    QProcess* p = new QProcess(this);
    QObject::connect(p, &QProcess::readyReadStandardOutput, this, [this, p, profileId]() {
      if (!m_peer) {
        return;
      }
      const QString shortId = profileId.left(8);
      const auto lines = QString::fromUtf8(p->readAllStandardOutput()).split('\n', Qt::SkipEmptyParts);
      for (const auto& line : lines) {
        QJsonObject log;
        log.insert(QStringLiteral("type"), QStringLiteral("log.line"));
        log.insert(QStringLiteral("message"), QStringLiteral("chrome[%1] %2").arg(shortId, line));
        m_peer->send(log);
      }
    });
    QObject::connect(p, &QProcess::readyReadStandardError, this, [this, p, profileId]() {
      if (!m_peer) {
        return;
      }
      const QString shortId = profileId.left(8);
      const auto lines = QString::fromUtf8(p->readAllStandardError()).split('\n', Qt::SkipEmptyParts);
      for (const auto& line : lines) {
        QJsonObject log;
        log.insert(QStringLiteral("type"), QStringLiteral("log.line"));
        log.insert(QStringLiteral("message"), QStringLiteral("chrome[%1] %2").arg(shortId, line));
        m_peer->send(log);
      }
    });
    QObject::connect(p, &QProcess::errorOccurred, this, [this, profileId, sendStatus](QProcess::ProcessError) mutable {
      if (m_profileStopRequested.contains(profileId)) {
        return;
      }
      sendStatus(QStringLiteral("error"), QStringLiteral("process_error"));
      if (m_peer) {
        QJsonObject log;
        log.insert(QStringLiteral("type"), QStringLiteral("log.line"));
        log.insert(QStringLiteral("message"), QStringLiteral("chrome[%1] error").arg(profileId.left(8)));
        m_peer->send(log);
      }
    });

    m_profileProcByProfileId.insert(profileId, p);
    sendStatus(QStringLiteral("starting"), QString());
    p->setProperty("compat_requested", chromeCompatRequested);
    p->setProperty("compat_tried", chromeCompat);
    p->setProperty("compat_retried", false);

    const QString chromeExe = resolveChrome();
    if (chromeExe.isEmpty()) {
      sendStatus(QStringLiteral("error"), QStringLiteral("chrome_not_found"));
      m_profileProcByProfileId.remove(profileId);
      p->deleteLater();
      return;
    }

    const QString userDataDir = resolveProfileDataDir();
    if (userDataDir.isEmpty()) {
      sendStatus(QStringLiteral("error"), QStringLiteral("invalid_data_dir"));
      m_profileProcByProfileId.remove(profileId);
      p->deleteLater();
      return;
    }
    QDir().mkpath(userDataDir);

    cleanupCdp(profileId);
    const QString proxyArg = buildProxyArg();
    QString chromeExtDir;
    const bool needInject = !language.isEmpty() || !userAgent.isEmpty() || !platform.isEmpty() || hardwareConcurrency > 0 ||
                            deviceMemoryGb > 0 || deviceScaleFactor > 0 || !timezone.isEmpty() || touchEnabled || geoEnabled;
    const bool cdpEnabled = needInject;
    if (needInject || enableProxyAuth) {
      chromeExtDir = createChromeExt(proxyScheme, proxyHost, proxyPort, proxyUsername, proxyPassword, enableProxyAuth);
    }
    const QString url = obj.value(QStringLiteral("url")).toString().trimmed();

    auto allocateDebugPort = []() -> int {
      QTcpServer s;
      if (!s.listen(QHostAddress::LocalHost, 0)) {
        return 0;
      }
      const int port = static_cast<int>(s.serverPort());
      s.close();
      return port;
    };
    const int debugPort = cdpEnabled ? allocateDebugPort() : 0;

    int winW = 0;
    int winH = 0;
    if (!resolution.isEmpty()) {
      const auto parts = resolution.split('x');
      if (parts.size() == 2) {
        bool okW = false;
        bool okH = false;
        winW = parts.at(0).trimmed().toInt(&okW);
        winH = parts.at(1).trimmed().toInt(&okH);
        if (!okW || !okH) {
          winW = 0;
          winH = 0;
        }
      }
    }

    const QString windowSizeArg =
        (winW > 0 && winH > 0) ? QStringLiteral("--window-size=%1,%2").arg(QString::number(winW), QString::number(winH))
                               : QString();

    auto buildArgs =
        [userDataDir, proxyArg, chromeExtDir, url, language, userAgent, timezone, windowSizeArg, touchEnabled, debugPort](bool compat)
            -> QStringList {
      QStringList args;
      args << QStringLiteral("--user-data-dir=%1").arg(userDataDir);
      args << QStringLiteral("--no-first-run");
      args << QStringLiteral("--no-default-browser-check");
      args << QStringLiteral("--disable-sync");
      args << QStringLiteral("--new-window");
      args << QStringLiteral("--disable-session-crashed-bubble");
      args << QStringLiteral("--force-webrtc-ip-handling-policy=disable_non_proxied_udp");
      if (debugPort > 0) {
        args << QStringLiteral("--remote-debugging-address=127.0.0.1");
        args << QStringLiteral("--remote-debugging-port=%1").arg(QString::number(debugPort));
      }
      if (!language.isEmpty()) {
        args << QStringLiteral("--lang=%1").arg(language);
      }
      if (!userAgent.isEmpty()) {
        args << QStringLiteral("--user-agent=%1").arg(userAgent);
      }
      if (!timezone.isEmpty()) {
        args << QStringLiteral("--blink-settings=timezoneOverride=%1").arg(timezone);
      }
      if (!windowSizeArg.isEmpty()) {
        args << windowSizeArg;
      }
      if (touchEnabled) {
        args << QStringLiteral("--touch-events=enabled");
      }
      if (!chromeExtDir.isEmpty()) {
        args << QStringLiteral("--disable-features=DisableLoadExtensionCommandLineSwitch");
        args << QStringLiteral("--load-extension=%1").arg(chromeExtDir);
      }
      if (compat) {
        args << QStringLiteral("--test-type");
        args << QStringLiteral("--no-sandbox");
        args << QStringLiteral("--disable-gpu");
        args << QStringLiteral("--disable-software-rasterizer");
        args << QStringLiteral("--disable-dev-shm-usage");
        args << QStringLiteral("--disable-breakpad");
        args << QStringLiteral("--disable-crash-reporter");
      }
      if (!proxyArg.isEmpty()) {
        args << proxyArg;
      }
      if (!url.isEmpty()) {
        args << url;
      } else {
        args << QStringLiteral("about:blank");
      }
      return args;
    };

    auto scheduleRunningCheck = [this, p, profileId, sendStatus]() {
      QPointer<QProcess> pp(p);
      QTimer::singleShot(900, this, [this, pp, profileId, sendStatus]() mutable {
        if (!pp) {
          return;
        }
        if (m_profileProcByProfileId.value(profileId) != pp) {
          return;
        }
        if (pp->state() == QProcess::Running) {
          sendStatus(QStringLiteral("running"), QString());
        }
      });
    };

    QObject::connect(
        p, &QProcess::finished, this,
        [this, profileId, sendStatus, chromeExe, buildArgs, scheduleRunningCheck, cleanupProxyAuthExt, cleanupProxyMapping,
         cleanupCdp](int exitCode, QProcess::ExitStatus st) mutable {
                       const bool expectedStop = m_profileStopRequested.remove(profileId);
                       if (expectedStop) {
                         m_profileProcByProfileId.remove(profileId);
                         cleanupCdp(profileId);
                         cleanupProxyMapping(profileId);
                         cleanupProxyAuthExt(profileId);
                         sendStatus(QStringLiteral("stopped"), QString());
                         return;
                       }

                       QProcess* cur = m_profileProcByProfileId.value(profileId);
                       if (!cur) {
                         return;
                       }

                       const bool compatRequested = cur->property("compat_requested").toBool();
                       const bool compatTried = cur->property("compat_tried").toBool();
                       const bool compatRetried = cur->property("compat_retried").toBool();
                       if (!compatRequested && !compatTried && !compatRetried) {
                         cur->setProperty("compat_retried", true);
                         cur->setProperty("compat_tried", true);
                         sendStatus(QStringLiteral("starting"), QString());
                         cur->setProgram(chromeExe);
                         cur->setArguments(buildArgs(true));
                         cur->start();
                         scheduleRunningCheck();
                         return;
                       }

                       m_profileProcByProfileId.remove(profileId);
                       cleanupCdp(profileId);
                       cleanupProxyMapping(profileId);
                       cleanupProxyAuthExt(profileId);
                       if (st == QProcess::CrashExit) {
                         sendStatus(QStringLiteral("crashed"), QStringLiteral("crash_exit"));
                       } else if (exitCode == 0) {
                         sendStatus(QStringLiteral("stopped"), QString());
                       } else {
                         sendStatus(QStringLiteral("crashed"), QStringLiteral("exit_code_%1").arg(exitCode));
                       }
                     });

    if (!timezone.isEmpty()) {
      QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
      env.insert(QStringLiteral("TZ"), timezone);
      p->setProcessEnvironment(env);
    }

    p->setProgram(chromeExe);
    p->setArguments(buildArgs(chromeCompat));
    p->start();

    if (cdpEnabled && debugPort > 0) {
      auto* nam = new QNetworkAccessManager(this);
      auto attempts = std::make_shared<int>(0);
      auto doPoll = std::make_shared<std::function<void()>>();
      const QString shortId = profileId.left(8);

      *doPoll = [this, nam, attempts, doPoll, profileId, shortId, debugPort, language, userAgent, platform, hardwareConcurrency,
                 deviceMemoryGb, deviceScaleFactor, fpSeed, timezone, resolution, touchEnabled, geoEnabled, geoLatitude, geoLongitude,
                 geoAccuracy, url, cleanupCdp]() mutable {
        if (!m_profileProcByProfileId.contains(profileId)) {
          nam->deleteLater();
          return;
        }
        if (*attempts >= 25) {
          nam->deleteLater();
          return;
        }
        (*attempts)++;

        QNetworkRequest req(QUrl(QStringLiteral("http://127.0.0.1:%1/json/version").arg(QString::number(debugPort))));
        auto* reply = nam->get(req);
        QObject::connect(reply, &QNetworkReply::finished, this,
                         [this, reply, nam, attempts, doPoll, profileId, shortId, language, userAgent, platform,
                          hardwareConcurrency, deviceMemoryGb, deviceScaleFactor, fpSeed, timezone, resolution, touchEnabled, geoEnabled,
                          geoLatitude, geoLongitude, geoAccuracy, url, cleanupCdp]() mutable {
          reply->deleteLater();
          if (!m_profileProcByProfileId.contains(profileId)) {
            nam->deleteLater();
            return;
          }
          if (reply->error() != QNetworkReply::NoError) {
            QTimer::singleShot(200, this, *doPoll);
            return;
          }
          const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
          if (!doc.isObject()) {
            QTimer::singleShot(200, this, *doPoll);
            return;
          }
          const QString ws = doc.object().value(QStringLiteral("webSocketDebuggerUrl")).toString().trimmed();
          if (ws.isEmpty()) {
            QTimer::singleShot(200, this, *doPoll);
            return;
          }

          CdpClient::Fingerprint fp;
          fp.language = language;
          fp.userAgent = userAgent;
          fp.platform = platform;
          fp.hardwareConcurrency = hardwareConcurrency;
          fp.deviceMemoryGb = deviceMemoryGb;
          fp.deviceScaleFactor = deviceScaleFactor;
          fp.seed = fpSeed;
          fp.timezone = timezone;
          fp.resolution = resolution;
          fp.touchEnabled = touchEnabled;
          fp.geoEnabled = geoEnabled;
          fp.geoLatitude = geoLatitude;
          fp.geoLongitude = geoLongitude;
          fp.geoAccuracy = geoAccuracy;

          cleanupCdp(profileId);
          auto* cdp = new CdpClient(QUrl(ws), fp, url, this);
          m_cdpByProfileId.insert(profileId, cdp);
          QObject::connect(cdp, &CdpClient::ready, this, [this, shortId]() {
            if (!m_peer) {
              return;
            }
            QJsonObject log;
            log.insert(QStringLiteral("type"), QStringLiteral("log.line"));
            log.insert(QStringLiteral("message"), QStringLiteral("cdp[%1] ready").arg(shortId));
            m_peer->send(log);
          });
          QObject::connect(cdp, &CdpClient::error, this, [this, shortId](const QString& message) {
            if (!m_peer) {
              return;
            }
            QJsonObject log;
            log.insert(QStringLiteral("type"), QStringLiteral("log.line"));
            log.insert(QStringLiteral("message"), QStringLiteral("cdp[%1] error %2").arg(shortId, message));
            m_peer->send(log);
          });
          cdp->start();
          nam->deleteLater();
        });
      };

      QTimer::singleShot(200, this, *doPoll);
    }

    scheduleRunningCheck();
    return;
  }

  if (type == QStringLiteral("proxy_pool.test")) {
    const QString proxyId = obj.value(QStringLiteral("proxy_id")).toString();
    const QJsonObject proxy = obj.value(QStringLiteral("proxy")).toObject();
    const bool enabled = proxy.value(QStringLiteral("enabled")).toBool(true);
    const QString proxyType = proxy.value(QStringLiteral("type")).toString(QStringLiteral("http")).toLower();
    const QString host = proxy.value(QStringLiteral("host")).toString();
    const int port = proxy.value(QStringLiteral("port")).toInt(0);
    const QString username = proxy.value(QStringLiteral("username")).toString();
    const QString password = proxy.value(QStringLiteral("password")).toString();
    const QString urlStr = obj.value(QStringLiteral("url")).toString(QStringLiteral("https://httpbin.org/ip"));
    const QString requestId = obj.value(QStringLiteral("request_id")).toString();
    const QString batchId = obj.value(QStringLiteral("batch_id")).toString();

    QJsonObject result;
    result.insert(QStringLiteral("type"), QStringLiteral("proxy_pool.test.result"));
    result.insert(QStringLiteral("proxy_id"), proxyId);
    if (!requestId.isEmpty()) {
      result.insert(QStringLiteral("request_id"), requestId);
    }
    if (!batchId.isEmpty()) {
      result.insert(QStringLiteral("batch_id"), batchId);
    }

    if (proxyId.isEmpty()) {
      result.insert(QStringLiteral("ok"), false);
      result.insert(QStringLiteral("error"), QStringLiteral("missing_proxy_id"));
      m_peer->send(result);
      return;
    }

    if (enabled && proxyType != QStringLiteral("direct") && (host.isEmpty() || port <= 0)) {
      result.insert(QStringLiteral("ok"), false);
      result.insert(QStringLiteral("error"), QStringLiteral("invalid_proxy_config"));
      m_peer->send(result);
      return;
    }

    QPointer<FramedJsonSocket> peerPtr(m_peer);
    auto timerPtr = std::make_shared<QElapsedTimer>();
    timerPtr->start();

    auto* nam = new QNetworkAccessManager(this);
    if (!enabled || proxyType == QStringLiteral("direct")) {
      nam->setProxy(QNetworkProxy::NoProxy);
    } else {
      QNetworkProxy px;
      if (proxyType == QStringLiteral("socks5")) {
        px.setType(QNetworkProxy::Socks5Proxy);
      } else {
        px.setType(QNetworkProxy::HttpProxy);
      }
      px.setHostName(host);
      px.setPort(static_cast<quint16>(port));
      px.setUser(username);
      px.setPassword(password);
      nam->setProxy(px);
    }

    QStringList urls;
    if (!urlStr.trimmed().isEmpty()) {
      urls.push_back(urlStr.trimmed());
    }
    urls.push_back(QStringLiteral("https://httpbin.org/ip"));
    urls.push_back(QStringLiteral("https://api.ipify.org?format=json"));
    urls.removeDuplicates();

    auto sent = std::make_shared<bool>(false);
    auto attempt = std::make_shared<int>(0);
    auto lastStatusCode = std::make_shared<int>(0);
    auto lastQtError = std::make_shared<int>(0);
    auto lastError = std::make_shared<QString>();
    auto lastObservedIp = std::make_shared<QString>();

    auto doAttempt = std::make_shared<std::function<void()>>();
    *doAttempt = [this, peerPtr, nam, urls, timerPtr, sent, attempt, proxyId, requestId, batchId, doAttempt, lastStatusCode,
                  lastQtError, lastError, lastObservedIp]() mutable {
      if (*sent) {
        nam->deleteLater();
        return;
      }
      if (!peerPtr) {
        nam->deleteLater();
        return;
      }
      if (*attempt >= urls.size()) {
        const int durationMs = static_cast<int>(timerPtr->elapsed());
        QJsonObject r;
        r.insert(QStringLiteral("type"), QStringLiteral("proxy_pool.test.result"));
        r.insert(QStringLiteral("proxy_id"), proxyId);
        if (!requestId.isEmpty()) {
          r.insert(QStringLiteral("request_id"), requestId);
        }
        if (!batchId.isEmpty()) {
          r.insert(QStringLiteral("batch_id"), batchId);
        }
        r.insert(QStringLiteral("ok"), false);
        r.insert(QStringLiteral("status_code"), *lastStatusCode);
        r.insert(QStringLiteral("duration_ms"), durationMs);
        r.insert(QStringLiteral("qt_error"), *lastQtError);
        r.insert(QStringLiteral("error"), lastError->isEmpty() ? QStringLiteral("all_attempts_failed") : *lastError);
        r.insert(QStringLiteral("observed_ip"), *lastObservedIp);
        peerPtr->send(r);
        *sent = true;
        nam->deleteLater();
        return;
      }

      const QString url = urls.at(*attempt);
      (*attempt)++;

      QNetworkRequest req{QUrl(url)};
      req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("DokeBrowser/0.1"));
      req.setRawHeader("Accept", "application/json");
      req.setTransferTimeout(4000);
      QNetworkReply* reply = nam->get(req);

      auto* timeout = new QTimer(nam);
      timeout->setSingleShot(true);
      timeout->start(4500);

      QObject::connect(timeout, &QTimer::timeout, this, [reply, timeout, doAttempt, sent]() mutable {
        if (*sent) {
          timeout->deleteLater();
          reply->deleteLater();
          return;
        }
        reply->abort();
        timeout->deleteLater();
        reply->deleteLater();
        (*doAttempt)();
      });

      QObject::connect(reply, &QNetworkReply::finished, this,
                       [peerPtr, reply, timeout, timerPtr, sent, proxyId, requestId, batchId, doAttempt, lastStatusCode,
                        lastQtError, lastError, lastObservedIp, url]() mutable {
                         timeout->stop();
                         timeout->deleteLater();

                         if (*sent) {
                           reply->deleteLater();
                           return;
                         }
                         if (!peerPtr) {
                           reply->deleteLater();
                           return;
                         }

                         const int durationMs = static_cast<int>(timerPtr->elapsed());
                         const QVariant sc = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
                         const int statusCode = sc.isValid() ? sc.toInt() : 0;
                         const int qtError = static_cast<int>(reply->error());
                         const QString errStr = reply->errorString();
                         const QByteArray body = reply->readAll();

                         QString observedIp;
                         QJsonParseError parseErr{};
                         const QJsonDocument doc = QJsonDocument::fromJson(body, &parseErr);
                         if (parseErr.error == QJsonParseError::NoError && doc.isObject()) {
                           observedIp = doc.object().value(QStringLiteral("ip")).toString();
                           if (observedIp.isEmpty()) {
                             observedIp = doc.object().value(QStringLiteral("origin")).toString();
                           }
                           if (observedIp.contains(',')) {
                             observedIp = observedIp.split(',').value(0).trimmed();
                           }
                         }

                         *lastStatusCode = statusCode;
                         *lastQtError = qtError;
                         *lastError = errStr;
                         *lastObservedIp = observedIp;

                         const bool ok = (reply->error() == QNetworkReply::NoError) && (statusCode >= 200 && statusCode < 300) &&
                                         !observedIp.isEmpty();
                         if (!ok) {
                           reply->deleteLater();
                           (*doAttempt)();
                           return;
                         }

                         QJsonObject r;
                         r.insert(QStringLiteral("type"), QStringLiteral("proxy_pool.test.result"));
                         r.insert(QStringLiteral("proxy_id"), proxyId);
                         if (!requestId.isEmpty()) {
                           r.insert(QStringLiteral("request_id"), requestId);
                         }
                         if (!batchId.isEmpty()) {
                           r.insert(QStringLiteral("batch_id"), batchId);
                         }
                         r.insert(QStringLiteral("ok"), true);
                         r.insert(QStringLiteral("status_code"), statusCode);
                         r.insert(QStringLiteral("duration_ms"), durationMs);
                         r.insert(QStringLiteral("qt_error"), qtError);
                         r.insert(QStringLiteral("error"), QString());
                         r.insert(QStringLiteral("observed_ip"), observedIp);
                         peerPtr->send(r);
                         *sent = true;
                         reply->deleteLater();
                       });
    };

    (*doAttempt)();
    return;
  }

  if (type == QStringLiteral("proxy.test")) {
    const QString profileId = obj.value(QStringLiteral("profile_id")).toString();
    const QJsonObject proxy = obj.value(QStringLiteral("proxy")).toObject();
    const bool enabled = proxy.value(QStringLiteral("enabled")).toBool(false);
    const QString proxyType = proxy.value(QStringLiteral("type")).toString(QStringLiteral("direct")).toLower();
    const QString host = proxy.value(QStringLiteral("host")).toString();
    const int port = proxy.value(QStringLiteral("port")).toInt(0);
    const QString username = proxy.value(QStringLiteral("username")).toString();
    const QString password = proxy.value(QStringLiteral("password")).toString();
    const QString urlStr = obj.value(QStringLiteral("url")).toString(QStringLiteral("https://httpbin.org/ip"));
    const QString requestId = obj.value(QStringLiteral("request_id")).toString();
    const QString batchId = obj.value(QStringLiteral("batch_id")).toString();

    QJsonObject result;
    result.insert(QStringLiteral("type"), QStringLiteral("proxy.test.result"));
    result.insert(QStringLiteral("profile_id"), profileId);
    if (!requestId.isEmpty()) {
      result.insert(QStringLiteral("request_id"), requestId);
    }
    if (!batchId.isEmpty()) {
      result.insert(QStringLiteral("batch_id"), batchId);
    }

    if (enabled && proxyType != QStringLiteral("direct") && (host.isEmpty() || port <= 0)) {
      result.insert(QStringLiteral("ok"), false);
      result.insert(QStringLiteral("error"), QStringLiteral("invalid_proxy_config"));
      m_peer->send(result);
      return;
    }

    QPointer<FramedJsonSocket> peerPtr(m_peer);
    auto timerPtr = std::make_shared<QElapsedTimer>();
    timerPtr->start();

    auto* nam = new QNetworkAccessManager(this);
    if (!enabled || proxyType == QStringLiteral("direct")) {
      nam->setProxy(QNetworkProxy::NoProxy);
    } else {
      QNetworkProxy px;
      if (proxyType == QStringLiteral("socks5")) {
        px.setType(QNetworkProxy::Socks5Proxy);
      } else {
        px.setType(QNetworkProxy::HttpProxy);
      }
      px.setHostName(host);
      px.setPort(static_cast<quint16>(port));
      px.setUser(username);
      px.setPassword(password);
      nam->setProxy(px);
    }

    QStringList urls;
    if (!urlStr.trimmed().isEmpty()) {
      urls.push_back(urlStr.trimmed());
    }
    urls.push_back(QStringLiteral("https://httpbin.org/ip"));
    urls.push_back(QStringLiteral("https://api.ipify.org?format=json"));
    urls.removeDuplicates();

    auto sent = std::make_shared<bool>(false);
    auto attempt = std::make_shared<int>(0);
    auto lastStatusCode = std::make_shared<int>(0);
    auto lastQtError = std::make_shared<int>(0);
    auto lastError = std::make_shared<QString>();
    auto lastObservedIp = std::make_shared<QString>();

    auto doAttempt = std::make_shared<std::function<void()>>();
    *doAttempt = [this, peerPtr, nam, urls, timerPtr, sent, attempt, profileId, requestId, batchId, doAttempt, lastStatusCode,
                  lastQtError, lastError, lastObservedIp]() mutable {
      if (*sent) {
        nam->deleteLater();
        return;
      }
      if (!peerPtr) {
        nam->deleteLater();
        return;
      }
      if (*attempt >= urls.size()) {
        const int durationMs = static_cast<int>(timerPtr->elapsed());
        QJsonObject r;
        r.insert(QStringLiteral("type"), QStringLiteral("proxy.test.result"));
        r.insert(QStringLiteral("profile_id"), profileId);
        if (!requestId.isEmpty()) {
          r.insert(QStringLiteral("request_id"), requestId);
        }
        if (!batchId.isEmpty()) {
          r.insert(QStringLiteral("batch_id"), batchId);
        }
        r.insert(QStringLiteral("ok"), false);
        r.insert(QStringLiteral("status_code"), *lastStatusCode);
        r.insert(QStringLiteral("duration_ms"), durationMs);
        r.insert(QStringLiteral("qt_error"), *lastQtError);
        r.insert(QStringLiteral("error"), lastError->isEmpty() ? QStringLiteral("all_attempts_failed") : *lastError);
        r.insert(QStringLiteral("observed_ip"), *lastObservedIp);
        peerPtr->send(r);
        *sent = true;
        nam->deleteLater();
        return;
      }

      const QString url = urls.at(*attempt);
      (*attempt)++;

      QNetworkRequest req{QUrl(url)};
      req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("DokeBrowser/0.1"));
      req.setRawHeader("Accept", "application/json");
      req.setTransferTimeout(4000);
      QNetworkReply* reply = nam->get(req);

      auto* timeout = new QTimer(nam);
      timeout->setSingleShot(true);
      timeout->start(4500);

      QObject::connect(timeout, &QTimer::timeout, this, [reply, timeout, doAttempt, sent]() mutable {
        if (*sent) {
          timeout->deleteLater();
          reply->deleteLater();
          return;
        }
        reply->abort();
        timeout->deleteLater();
        reply->deleteLater();
        (*doAttempt)();
      });

      QObject::connect(reply, &QNetworkReply::finished, this,
                       [peerPtr, reply, timeout, timerPtr, sent, profileId, requestId, batchId, doAttempt, lastStatusCode,
                        lastQtError, lastError, lastObservedIp, url]() mutable {
                         timeout->stop();
                         timeout->deleteLater();

                         if (*sent) {
                           reply->deleteLater();
                           return;
                         }
                         if (!peerPtr) {
                           reply->deleteLater();
                           return;
                         }

                         const int durationMs = static_cast<int>(timerPtr->elapsed());
                         const QVariant sc = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
                         const int statusCode = sc.isValid() ? sc.toInt() : 0;
                         const int qtError = static_cast<int>(reply->error());
                         const QString errStr = reply->errorString();
                         const QByteArray body = reply->readAll();

                         QString observedIp;
                         QJsonParseError parseErr{};
                         const QJsonDocument doc = QJsonDocument::fromJson(body, &parseErr);
                         if (parseErr.error == QJsonParseError::NoError && doc.isObject()) {
                           observedIp = doc.object().value(QStringLiteral("ip")).toString();
                           if (observedIp.isEmpty()) {
                             observedIp = doc.object().value(QStringLiteral("origin")).toString();
                           }
                           if (observedIp.contains(',')) {
                             observedIp = observedIp.split(',').value(0).trimmed();
                           }
                         }

                         *lastStatusCode = statusCode;
                         *lastQtError = qtError;
                         *lastError = errStr;
                         *lastObservedIp = observedIp;

                         const bool ok = (reply->error() == QNetworkReply::NoError) && (statusCode >= 200 && statusCode < 300) &&
                                         !observedIp.isEmpty();
                         if (!ok) {
                           reply->deleteLater();
                           (*doAttempt)();
                           return;
                         }

                         QJsonObject r;
                         r.insert(QStringLiteral("type"), QStringLiteral("proxy.test.result"));
                         r.insert(QStringLiteral("profile_id"), profileId);
                         if (!requestId.isEmpty()) {
                           r.insert(QStringLiteral("request_id"), requestId);
                         }
                         if (!batchId.isEmpty()) {
                           r.insert(QStringLiteral("batch_id"), batchId);
                         }
                         r.insert(QStringLiteral("ok"), true);
                         r.insert(QStringLiteral("status_code"), statusCode);
                         r.insert(QStringLiteral("duration_ms"), durationMs);
                         r.insert(QStringLiteral("qt_error"), qtError);
                         r.insert(QStringLiteral("error"), QString());
                         r.insert(QStringLiteral("observed_ip"), observedIp);
                         peerPtr->send(r);
                         *sent = true;
                         reply->deleteLater();
                       });
    };

    (*doAttempt)();

    return;
  }

  if (type == QStringLiteral("vpn.openvpn.start")) {
    const QString profileId = obj.value(QStringLiteral("profile_id")).toString();
    const QString exe = obj.value(QStringLiteral("exe")).toString(QStringLiteral("openvpn"));
    const QString config = obj.value(QStringLiteral("config")).toString();
    const QJsonObject socks = obj.value(QStringLiteral("socks")).toObject();
    const bool socksEnabled = socks.value(QStringLiteral("enabled")).toBool(false);
    const QString socksHost = socks.value(QStringLiteral("host")).toString();
    const int socksPort = socks.value(QStringLiteral("port")).toInt(0);
    const QString socksUser = socks.value(QStringLiteral("username")).toString();
    const QString socksPass = socks.value(QStringLiteral("password")).toString();

    auto sendStatus = [this, profileId](const QString& status, const QString& error) {
      if (!m_peer) {
        return;
      }
      QJsonObject msg;
      msg.insert(QStringLiteral("type"), QStringLiteral("vpn.status"));
      msg.insert(QStringLiteral("profile_id"), profileId);
      msg.insert(QStringLiteral("status"), status);
      msg.insert(QStringLiteral("error"), error);
      m_peer->send(msg);
    };

    if (profileId.isEmpty()) {
      sendStatus(QStringLiteral("error"), QStringLiteral("missing_profile_id"));
      return;
    }

    QProcess* existing = m_openvpnByProfileId.value(profileId);
    if (existing && existing->state() != QProcess::NotRunning) {
      sendStatus(QStringLiteral("running"), QString());
      return;
    }

    if (config.isEmpty()) {
      sendStatus(QStringLiteral("error"), QStringLiteral("missing_config"));
      return;
    }

    QString socksAuthFile;
    if (socksEnabled) {
      if (socksHost.isEmpty() || socksPort <= 0) {
        sendStatus(QStringLiteral("error"), QStringLiteral("invalid_socks_proxy"));
        return;
      }

      if (!socksUser.isEmpty() || !socksPass.isEmpty()) {
        QTemporaryFile tf;
        tf.setAutoRemove(false);
        if (!tf.open()) {
          sendStatus(QStringLiteral("error"), QStringLiteral("socks_authfile_open_failed"));
          return;
        }
        tf.write(socksUser.toUtf8());
        tf.write("\n");
        tf.write(socksPass.toUtf8());
        tf.write("\n");
        tf.flush();
        tf.close();
        socksAuthFile = tf.fileName();
        m_openvpnSocksAuthFileByProfileId.insert(profileId, socksAuthFile);
      }
    }

    auto* p = new QProcess(this);
    m_openvpnByProfileId.insert(profileId, p);

    QStringList args;
    args << QStringLiteral("--config") << config;
    if (socksEnabled) {
      args << QStringLiteral("--socks-proxy") << socksHost << QString::number(socksPort);
      if (!socksAuthFile.isEmpty()) {
        args << socksAuthFile;
      }
    }

    p->setProgram(exe.isEmpty() ? QStringLiteral("openvpn") : exe);
    p->setArguments(args);

    const QString shortId = profileId.left(8);
    QObject::connect(p, &QProcess::started, this, [this, profileId, shortId, sendStatus]() mutable {
      sendStatus(QStringLiteral("running"), QString());
      if (m_peer) {
        QJsonObject log;
        log.insert(QStringLiteral("type"), QStringLiteral("log.line"));
        log.insert(QStringLiteral("message"), QStringLiteral("openvpn[%1] started").arg(shortId));
        m_peer->send(log);
      }
    });

    QObject::connect(p, &QProcess::readyReadStandardOutput, this, [this, p, shortId]() {
      if (!m_peer) {
        return;
      }
      const auto lines = QString::fromUtf8(p->readAllStandardOutput()).split('\n', Qt::SkipEmptyParts);
      for (const auto& line : lines) {
        QJsonObject log;
        log.insert(QStringLiteral("type"), QStringLiteral("log.line"));
        log.insert(QStringLiteral("message"), QStringLiteral("openvpn[%1] %2").arg(shortId, line));
        m_peer->send(log);
      }
    });

    QObject::connect(p, &QProcess::readyReadStandardError, this, [this, p, shortId]() {
      if (!m_peer) {
        return;
      }
      const auto lines = QString::fromUtf8(p->readAllStandardError()).split('\n', Qt::SkipEmptyParts);
      for (const auto& line : lines) {
        QJsonObject log;
        log.insert(QStringLiteral("type"), QStringLiteral("log.line"));
        log.insert(QStringLiteral("message"), QStringLiteral("openvpn[%1] %2").arg(shortId, line));
        m_peer->send(log);
      }
    });

    QObject::connect(p, &QProcess::errorOccurred, this, [this, profileId, shortId, sendStatus](QProcess::ProcessError) mutable {
      sendStatus(QStringLiteral("error"), QStringLiteral("openvpn_process_error"));
      if (m_peer) {
        QJsonObject log;
        log.insert(QStringLiteral("type"), QStringLiteral("log.line"));
        log.insert(QStringLiteral("message"), QStringLiteral("openvpn[%1] error").arg(shortId));
        m_peer->send(log);
      }
    });

    QObject::connect(p, &QProcess::finished, this, [this, profileId, shortId, sendStatus](int exitCode, QProcess::ExitStatus st) mutable {
      m_openvpnByProfileId.remove(profileId);
      sendStatus(st == QProcess::NormalExit ? QStringLiteral("stopped") : QStringLiteral("crashed"),
                 QStringLiteral("exitCode=%1").arg(exitCode));

      const QString authFile = m_openvpnSocksAuthFileByProfileId.take(profileId);
      if (!authFile.isEmpty()) {
        QFile::remove(authFile);
      }

      if (m_peer) {
        QJsonObject log;
        log.insert(QStringLiteral("type"), QStringLiteral("log.line"));
        log.insert(QStringLiteral("message"), QStringLiteral("openvpn[%1] finished").arg(shortId));
        m_peer->send(log);
      }
    });

    p->start();
    return;
  }

  if (type == QStringLiteral("vpn.openvpn.stop")) {
    const QString profileId = obj.value(QStringLiteral("profile_id")).toString();
    auto sendStatus = [this, profileId](const QString& status, const QString& error) {
      if (!m_peer) {
        return;
      }
      QJsonObject msg;
      msg.insert(QStringLiteral("type"), QStringLiteral("vpn.status"));
      msg.insert(QStringLiteral("profile_id"), profileId);
      msg.insert(QStringLiteral("status"), status);
      msg.insert(QStringLiteral("error"), error);
      m_peer->send(msg);
    };

    if (profileId.isEmpty()) {
      sendStatus(QStringLiteral("error"), QStringLiteral("missing_profile_id"));
      return;
    }

    QProcess* p = m_openvpnByProfileId.value(profileId);
    if (!p) {
      sendStatus(QStringLiteral("stopped"), QString());
      return;
    }

    p->terminate();
    QPointer<QProcess> pp(p);
    QTimer::singleShot(1500, this, [pp]() {
      if (pp && pp->state() != QProcess::NotRunning) {
        pp->kill();
      }
    });

    sendStatus(QStringLiteral("stopping"), QString());
    return;
  }
}
