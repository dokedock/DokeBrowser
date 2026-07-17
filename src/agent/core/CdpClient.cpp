#include "CdpClient.h"

#include "FingerprintMetadata.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

static QString jsEscape(const QString& s) {
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
}

CdpClient::CdpClient(const QUrl& wsUrl, const Fingerprint& fp, const QString& initialUrl, QObject* parent)
    : QObject(parent), m_wsUrl(wsUrl), m_fp(fp), m_initialUrl(initialUrl) {
  connect(&m_ws, &QWebSocket::connected, this, &CdpClient::onConnected);
  connect(&m_ws, &QWebSocket::disconnected, this, &CdpClient::onClosed);
  connect(&m_ws, &QWebSocket::textMessageReceived, this, &CdpClient::onTextMessage);
}

void CdpClient::start() {
  m_ws.open(m_wsUrl);
}

void CdpClient::stop() {
  m_ws.close();
}

void CdpClient::send(const QJsonObject& msg) {
  m_ws.sendTextMessage(QString::fromUtf8(QJsonDocument(msg).toJson(QJsonDocument::Compact)));
}

void CdpClient::sendRoot(const QString& method, const QJsonObject& params) {
  QJsonObject msg;
  const int id = m_nextId++;
  msg.insert(QStringLiteral("id"), id);
  msg.insert(QStringLiteral("method"), method);
  if (!params.isEmpty()) {
    msg.insert(QStringLiteral("params"), params);
  }
  m_pending.insert(id, method);
  send(msg);
}

void CdpClient::sendToSession(const QString& sessionId, const QString& method, const QJsonObject& params) {
  QJsonObject msg;
  const int id = m_nextId++;
  msg.insert(QStringLiteral("id"), id);
  msg.insert(QStringLiteral("method"), method);
  if (!params.isEmpty()) {
    msg.insert(QStringLiteral("params"), params);
  }
  msg.insert(QStringLiteral("sessionId"), sessionId);
  m_pending.insert(id, method);
  send(msg);
}

QJsonObject CdpClient::buildExtraHeaders() const {
  QJsonObject headers;
  if (!m_fp.enabled) {
    return headers;
  }
  const QString lang = m_fp.language.trimmed();
  if (!lang.isEmpty()) {
    headers.insert(QStringLiteral("Accept-Language"), lang);
  }
  return headers;
}

QString CdpClient::buildInitScript() const {
  if (!m_fp.enabled) {
    return {};
  }
  QString inj;
  const QString lang = m_fp.language.trimmed();
  const QString tz = m_fp.timezone.trimmed();
  const QString plat = m_fp.platform.trimmed();
  int resW = 0;
  int resH = 0;
  if (!m_fp.resolution.trimmed().isEmpty()) {
    const auto parts = m_fp.resolution.trimmed().split('x');
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
  const double dpr = m_fp.deviceScaleFactor > 0 ? m_fp.deviceScaleFactor : 0;
  const auto uaHints = FingerprintMetadata::buildUaClientHints(m_fp.userAgent, plat);
  inj += FingerprintMetadata::buildUserAgentDataScript(uaHints);
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
             .arg(QString::number(m_fp.seed));

  if (!plat.isEmpty()) {
    inj += QStringLiteral(
               "(function(){try{"
               "const v=\"%1\";"
               "try{Object.defineProperty(Navigator.prototype,'platform',{get:()=>v,configurable:true});}catch(e){}"
               "try{Object.defineProperty(navigator,'platform',{get:()=>v,configurable:true});}catch(e){}"
               "}catch(e){}})();")
               .arg(jsEscape(plat));
  }
  if (m_fp.hardwareConcurrency > 0) {
    inj += QStringLiteral(
               "(function(){try{"
               "const v=%1;"
               "try{Object.defineProperty(Navigator.prototype,'hardwareConcurrency',{get:()=>v,configurable:true});}catch(e){}"
               "try{Object.defineProperty(navigator,'hardwareConcurrency',{get:()=>v,configurable:true});}catch(e){}"
               "}catch(e){}})();")
               .arg(QString::number(m_fp.hardwareConcurrency));
  }
  if (m_fp.deviceMemoryGb > 0) {
    inj += QStringLiteral(
               "(function(){try{"
               "const v=%1;"
               "try{Object.defineProperty(Navigator.prototype,'deviceMemory',{get:()=>v,configurable:true});}catch(e){}"
               "try{Object.defineProperty(navigator,'deviceMemory',{get:()=>v,configurable:true});}catch(e){}"
               "}catch(e){}})();")
               .arg(QString::number(m_fp.deviceMemoryGb));
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
  if (m_fp.touchEnabled) {
    inj += QStringLiteral(
        "try{Object.defineProperty(navigator,'maxTouchPoints',{get:()=>5,configurable:true});}catch(e){}"
        "try{Object.defineProperty(navigator,'msMaxTouchPoints',{get:()=>5,configurable:true});}catch(e){}"
        "try{if(!('ontouchstart'in window)){Object.defineProperty(window,'ontouchstart',{value:null,configurable:true,writable:true});}}catch(e){}");
  }
  const QString geoObj = m_fp.geoEnabled
                             ? QStringLiteral("{enabled:true,lat:%1,lon:%2,acc:%3}")
                                   .arg(QString::number(m_fp.geoLatitude, 'f', 6), QString::number(m_fp.geoLongitude, 'f', 6),
                                        QString::number(m_fp.geoAccuracy > 0 ? m_fp.geoAccuracy : 1000, 'f', 0))
                             : QStringLiteral("{enabled:false}");
  inj += QStringLiteral("try{window.__DOKE_CDP_INJECTED={v:3,seed:%1,geo:%2,ts:Date.now()};}catch(e){}")
             .arg(QString::number(m_fp.seed), geoObj);
  if (inj.isEmpty()) {
    inj = QStringLiteral(";");
  }
  return inj;
}

void CdpClient::applyToTarget(const QString& sessionId) {
  if (m_fp.enabled && !m_fp.timezone.trimmed().isEmpty()) {
    QJsonObject p;
    p.insert(QStringLiteral("timezoneId"), m_fp.timezone.trimmed());
    sendToSession(sessionId, QStringLiteral("Emulation.setTimezoneOverride"), p);
  }
  if (m_fp.geoEnabled) {
    QJsonObject p;
    p.insert(QStringLiteral("latitude"), m_fp.geoLatitude);
    p.insert(QStringLiteral("longitude"), m_fp.geoLongitude);
    p.insert(QStringLiteral("accuracy"), m_fp.geoAccuracy > 0 ? m_fp.geoAccuracy : 1000);
    sendToSession(sessionId, QStringLiteral("Emulation.setGeolocationOverride"), p);
  }

  const QString ua = m_fp.userAgent.trimmed();
  const QJsonObject extra = buildExtraHeaders();
  const bool needNetwork = m_fp.enabled && (!ua.isEmpty() || !extra.isEmpty());
  if (needNetwork) {
    sendToSession(sessionId, QStringLiteral("Network.enable"), QJsonObject{});
  }
  if (m_fp.enabled && !ua.isEmpty()) {
    QJsonObject p;
    p.insert(QStringLiteral("userAgent"), ua);
    const QJsonObject metadata =
        FingerprintMetadata::toCdpUserAgentMetadata(FingerprintMetadata::buildUaClientHints(ua, m_fp.platform));
    if (!metadata.isEmpty()) {
      p.insert(QStringLiteral("userAgentMetadata"), metadata);
    }
    sendToSession(sessionId, QStringLiteral("Network.setUserAgentOverride"), p);
  }

  if (m_fp.enabled && m_fp.touchEnabled) {
    QJsonObject p;
    p.insert(QStringLiteral("enabled"), true);
    sendToSession(sessionId, QStringLiteral("Emulation.setTouchEmulationEnabled"), p);
  }

  if (!extra.isEmpty()) {
    QJsonObject p;
    p.insert(QStringLiteral("headers"), extra);
    sendToSession(sessionId, QStringLiteral("Network.setExtraHTTPHeaders"), p);
  }

  const QString script = buildInitScript();
  if (!script.isEmpty()) {
    QJsonObject p;
    p.insert(QStringLiteral("source"), script);
    sendToSession(sessionId, QStringLiteral("Page.addScriptToEvaluateOnNewDocument"), p);
  }

  const QString res = m_fp.resolution.trimmed();
  int w = 0;
  int h = 0;
  if (!res.isEmpty()) {
    const auto parts = res.split('x');
    if (parts.size() == 2) {
      bool okW = false;
      bool okH = false;
      w = parts.at(0).trimmed().toInt(&okW);
      h = parts.at(1).trimmed().toInt(&okH);
      if (!okW || !okH) {
        w = 0;
        h = 0;
      }
    }
  }
  if (m_fp.enabled && w > 0 && h > 0) {
    QJsonObject p;
    p.insert(QStringLiteral("width"), w);
    p.insert(QStringLiteral("height"), h);
    p.insert(QStringLiteral("deviceScaleFactor"), m_fp.deviceScaleFactor > 0 ? m_fp.deviceScaleFactor : 1.0);
    p.insert(QStringLiteral("mobile"), false);
    sendToSession(sessionId, QStringLiteral("Emulation.setDeviceMetricsOverride"), p);
  }

  maybeReloadOrNavigate(sessionId);
}

void CdpClient::maybeReloadOrNavigate(const QString& sessionId) {
  if (m_initialUrl.trimmed().isEmpty()) {
    QJsonObject p;
    p.insert(QStringLiteral("ignoreCache"), true);
    sendToSession(sessionId, QStringLiteral("Page.reload"), p);
    return;
  }

  QJsonObject p;
  p.insert(QStringLiteral("url"), m_initialUrl.trimmed());
  sendToSession(sessionId, QStringLiteral("Page.navigate"), p);
}

void CdpClient::onConnected() {
  sendRoot(QStringLiteral("Target.setAutoAttach"),
           QJsonObject{{QStringLiteral("autoAttach"), true},
                       {QStringLiteral("flatten"), true},
                       {QStringLiteral("waitForDebuggerOnStart"), false}});
  sendRoot(QStringLiteral("Target.setDiscoverTargets"), QJsonObject{{QStringLiteral("discover"), true}});
}

void CdpClient::onClosed() {
  if (!m_readyEmitted) {
    emit error(QStringLiteral("cdp_ws_closed"));
  }
}

void CdpClient::onTextMessage(const QString& payload) {
  const QJsonDocument doc = QJsonDocument::fromJson(payload.toUtf8());
  if (!doc.isObject()) {
    return;
  }
  const QJsonObject obj = doc.object();

  const int id = obj.value(QStringLiteral("id")).toInt(0);
  if (id > 0) {
    m_pending.remove(id);
  }

  const QString method = obj.value(QStringLiteral("method")).toString();
  if (method == QStringLiteral("Target.attachedToTarget")) {
    const QJsonObject params = obj.value(QStringLiteral("params")).toObject();
    const QString sessionId = params.value(QStringLiteral("sessionId")).toString();
    const QJsonObject targetInfo = params.value(QStringLiteral("targetInfo")).toObject();
    const QString type = targetInfo.value(QStringLiteral("type")).toString();
    if (sessionId.isEmpty() || type != QStringLiteral("page")) {
      return;
    }

    sendToSession(sessionId, QStringLiteral("Page.enable"), QJsonObject{});
    applyToTarget(sessionId);

    if (!m_readyEmitted) {
      m_readyEmitted = true;
      emit ready();
    }
    return;
  }
}
