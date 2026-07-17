#include "SystemChromeEngine.h"

#include "FingerprintMetadata.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <memory>

namespace {
QString sanitizeKey(const QString& s) {
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
}

QString jsEscape(const QString& s) {
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
} // namespace

QString SystemChromeEngine::id() const {
  return QStringLiteral("system_chrome");
}

QString SystemChromeEngine::executablePath() const {
  return resolveExecutable();
}

bool SystemChromeEngine::isAvailable() const {
  return !resolveExecutable().isEmpty();
}

QStringList SystemChromeEngine::buildArguments(const LaunchOptions& options, bool compat) {
  QStringList args;
  args << QStringLiteral("--user-data-dir=%1").arg(options.userDataDir);
  args << QStringLiteral("--no-first-run");
  args << QStringLiteral("--no-default-browser-check");
  args << QStringLiteral("--disable-sync");
  args << QStringLiteral("--new-window");
  args << QStringLiteral("--disable-session-crashed-bubble");
  args << QStringLiteral("--force-webrtc-ip-handling-policy=disable_non_proxied_udp");
  if (options.debugPort > 0) {
    args << QStringLiteral("--remote-debugging-address=127.0.0.1");
    args << QStringLiteral("--remote-debugging-port=%1").arg(QString::number(options.debugPort));
  }
  if (!options.language.isEmpty()) {
    args << QStringLiteral("--lang=%1").arg(options.language);
  }
  if (!options.userAgent.isEmpty()) {
    args << QStringLiteral("--user-agent=%1").arg(options.userAgent);
  }
  if (!options.timezone.isEmpty()) {
    args << QStringLiteral("--blink-settings=timezoneOverride=%1").arg(options.timezone);
  }
  if (!options.windowSizeArg.isEmpty()) {
    args << options.windowSizeArg;
  }
  if (options.touchEnabled) {
    args << QStringLiteral("--touch-events=enabled");
  }
  if (!options.extensionDir.isEmpty()) {
    args << QStringLiteral("--disable-features=DisableLoadExtensionCommandLineSwitch");
    args << QStringLiteral("--load-extension=%1").arg(options.extensionDir);
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
  if (!options.proxyArg.isEmpty()) {
    args << options.proxyArg;
  }
  if (!options.url.isEmpty()) {
    args << options.url;
  } else {
    args << QStringLiteral("about:blank");
  }
  return args;
}

QString SystemChromeEngine::createProfileExtension(const ExtensionOptions& options) {
  const QString base = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
  if (base.isEmpty()) {
    return {};
  }
  const QString dir = QDir(base).filePath(QStringLiteral("dokebrowser_ext/%1_%2")
                                              .arg(sanitizeKey(options.profileId),
                                                   QString::number(QDateTime::currentMSecsSinceEpoch())));
  if (!QDir().mkpath(dir)) {
    return {};
  }

  QFile mf(QDir(dir).filePath(QStringLiteral("manifest.json")));
  if (!mf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    QDir(dir).removeRecursively();
    return {};
  }
  QByteArray manifest;
  if (options.enableProxyAuth) {
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
  if (options.enableProxyAuth) {
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
               .arg(jsEscape(options.proxyScheme), jsEscape(options.proxyHost), QString::number(options.proxyPort),
                    jsEscape(options.proxyUsername), jsEscape(options.proxyPassword));
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
                                .arg(jsEscape(options.language.trimmed()), jsEscape(options.timezone.trimmed()),
                                     options.touchEnabled ? QStringLiteral("1") : QStringLiteral("0"),
                                     jsEscape(options.resolution.trimmed()), jsEscape(options.fingerprintMode.trimmed()),
                                     jsEscape(options.platform.trimmed()));
  content.write(contentJs.toUtf8());
  content.close();
  QFile::setPermissions(content.fileName(), QFile::ReadOwner | QFile::WriteOwner);

  QFile inject(QDir(dir).filePath(QStringLiteral("inject.js")));
  if (!inject.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    QDir(dir).removeRecursively();
    return {};
  }

  QString inj;
  const QString lang = options.language.trimmed();
  const QString tz = options.timezone.trimmed();
  const QString plat = options.platform.trimmed();
  int resW = 0;
  int resH = 0;
  if (!options.resolution.trimmed().isEmpty()) {
    const auto parts = options.resolution.trimmed().split('x');
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
  const double dpr = options.deviceScaleFactor > 0 ? options.deviceScaleFactor : 0;
  const auto uaHints = FingerprintMetadata::buildUaClientHints(options.userAgent, plat);
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
             .arg(QString::number(options.fingerprintSeed));

  if (!plat.isEmpty()) {
    inj += QStringLiteral(
               "(function(){try{"
               "const v=\"%1\";"
               "try{Object.defineProperty(Navigator.prototype,'platform',{get:()=>v,configurable:true});}catch(e){}"
               "try{Object.defineProperty(navigator,'platform',{get:()=>v,configurable:true});}catch(e){}"
               "}catch(e){}})();")
               .arg(jsEscape(plat));
  }
  if (options.hardwareConcurrency > 0) {
    inj += QStringLiteral(
               "(function(){try{"
               "const v=%1;"
               "try{Object.defineProperty(Navigator.prototype,'hardwareConcurrency',{get:()=>v,configurable:true});}catch(e){}"
               "try{Object.defineProperty(navigator,'hardwareConcurrency',{get:()=>v,configurable:true});}catch(e){}"
               "}catch(e){}})();")
               .arg(QString::number(options.hardwareConcurrency));
  }
  if (options.deviceMemoryGb > 0) {
    inj += QStringLiteral(
               "(function(){try{"
               "const v=%1;"
               "try{Object.defineProperty(Navigator.prototype,'deviceMemory',{get:()=>v,configurable:true});}catch(e){}"
               "try{Object.defineProperty(navigator,'deviceMemory',{get:()=>v,configurable:true});}catch(e){}"
               "}catch(e){}})();")
               .arg(QString::number(options.deviceMemoryGb));
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
  if (options.touchEnabled) {
    inj += QStringLiteral(
        "try{Object.defineProperty(navigator,'maxTouchPoints',{get:()=>5,configurable:true});}catch(e){}"
        "try{Object.defineProperty(navigator,'msMaxTouchPoints',{get:()=>5,configurable:true});}catch(e){}"
        "try{if(!('ontouchstart'in window)){Object.defineProperty(window,'ontouchstart',{value:null,configurable:true,writable:true});}}catch(e){}");
  }
  if (options.geoEnabled) {
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
               .arg(QString::number(options.geoLatitude, 'f', 6), QString::number(options.geoLongitude, 'f', 6),
                    QString::number(options.geoAccuracy > 0 ? options.geoAccuracy : 1000, 'f', 0));
  }
  const QString geoObj =
      options.geoEnabled ? QStringLiteral("{enabled:true,lat:%1,lon:%2,acc:%3}")
                               .arg(QString::number(options.geoLatitude, 'f', 6),
                                    QString::number(options.geoLongitude, 'f', 6),
                                    QString::number(options.geoAccuracy > 0 ? options.geoAccuracy : 1000, 'f', 0))
                         : QStringLiteral("{enabled:false}");
  inj += QStringLiteral(
             "try{window.__DOKE_INJECTED={v:3,seed:%10,mode:\"%4\",lang:\"%1\",tz:\"%2\",plat:\"%5\",hc:%6,mem:%7,dpr:%8,res:\"%9\",touch:%3,geo:%11,ts:Date.now()};}catch(e){}")
             .arg(jsEscape(lang))
             .arg(jsEscape(tz))
             .arg(options.touchEnabled ? QStringLiteral("true") : QStringLiteral("false"))
             .arg(jsEscape(options.fingerprintMode.trimmed()))
             .arg(jsEscape(plat))
             .arg(QString::number(options.hardwareConcurrency))
             .arg(QString::number(options.deviceMemoryGb))
             .arg(QString::number(dpr, 'f', 2))
             .arg(jsEscape(options.resolution.trimmed()))
             .arg(QString::number(options.fingerprintSeed))
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
      "  add('navigator.userAgentData', navigator.userAgentData ? JSON.stringify(navigator.userAgentData) : '');\n"
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
      "    userAgentData:navigator.userAgentData||null,\n"
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

  return dir;
}

void SystemChromeEngine::attachCdpWhenReady(const CdpAttachOptions& options, QObject* owner, CdpAttachCallbacks callbacks) {
  if (!owner || options.debugPort <= 0) {
    return;
  }

  auto* nam = new QNetworkAccessManager(owner);
  auto attempts = std::make_shared<int>(0);
  auto doPoll = std::make_shared<std::function<void()>>();
  QPointer<QObject> ownerPtr(owner);
  const QString shortId = options.profileId.left(8);

  *doPoll = [ownerPtr, nam, attempts, doPoll, options, callbacks, shortId]() mutable {
    if (!ownerPtr) {
      nam->deleteLater();
      return;
    }
    if (callbacks.isActive && !callbacks.isActive()) {
      nam->deleteLater();
      return;
    }
    if (*attempts >= options.maxAttempts) {
      nam->deleteLater();
      return;
    }
    (*attempts)++;

    QNetworkRequest req(QUrl(QStringLiteral("http://127.0.0.1:%1/json/version").arg(QString::number(options.debugPort))));
    auto* reply = nam->get(req);
    QObject::connect(reply, &QNetworkReply::finished, ownerPtr,
                     [ownerPtr, reply, nam, attempts, doPoll, options, callbacks, shortId]() mutable {
      reply->deleteLater();
      if (!ownerPtr) {
        nam->deleteLater();
        return;
      }
      if (callbacks.isActive && !callbacks.isActive()) {
        nam->deleteLater();
        return;
      }
      if (reply->error() != QNetworkReply::NoError) {
        QTimer::singleShot(options.retryDelayMs, ownerPtr, *doPoll);
        return;
      }
      const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
      if (!doc.isObject()) {
        QTimer::singleShot(options.retryDelayMs, ownerPtr, *doPoll);
        return;
      }
      const QString ws = doc.object().value(QStringLiteral("webSocketDebuggerUrl")).toString().trimmed();
      if (ws.isEmpty()) {
        QTimer::singleShot(options.retryDelayMs, ownerPtr, *doPoll);
        return;
      }

      auto* cdp = new CdpClient(QUrl(ws), options.fingerprint, options.initialUrl, ownerPtr);
      if (callbacks.replaceClient) {
        callbacks.replaceClient(cdp);
      }
      QObject::connect(cdp, &CdpClient::ready, ownerPtr, [callbacks, shortId]() {
        if (callbacks.logLine) {
          callbacks.logLine(QStringLiteral("cdp[%1] ready").arg(shortId));
        }
      });
      QObject::connect(cdp, &CdpClient::error, ownerPtr, [callbacks, shortId](const QString& message) {
        if (callbacks.logLine) {
          callbacks.logLine(QStringLiteral("cdp[%1] error %2").arg(shortId, message));
        }
      });
      cdp->start();
      nam->deleteLater();
    });
  };

  QTimer::singleShot(options.retryDelayMs, owner, *doPoll);
}

QProcess* SystemChromeEngine::launchProcess(const ProcessLaunchOptions& options, QObject* owner, ProcessCallbacks callbacks) {
  if (!owner || options.executable.isEmpty()) {
    return nullptr;
  }

  auto* process = new QProcess(owner);
  const QString shortId = options.profileId.left(8);
  const QString processLabel = options.processLabel.trimmed().isEmpty() ? QStringLiteral("chrome") : options.processLabel.trimmed();

  auto sendStatus = [callbacks](const QString& status, const QString& error) {
    if (callbacks.status) {
      callbacks.status(status, error);
    }
  };
  auto sendLog = [callbacks](const QString& message) {
    if (callbacks.logLine) {
      callbacks.logLine(message);
    }
  };
  auto scheduleRunningCheck = [owner, process, options, callbacks, sendStatus]() {
    QPointer<QProcess> pp(process);
    QTimer::singleShot(900, owner, [pp, options, callbacks, sendStatus]() mutable {
      if (!pp) {
        return;
      }
      if (callbacks.isCurrentProcess && !callbacks.isCurrentProcess(pp)) {
        return;
      }
      if (pp->state() == QProcess::Running) {
        sendStatus(QStringLiteral("running"), QString());
      }
    });
  };

  QObject::connect(process, &QProcess::readyReadStandardOutput, owner, [process, shortId, processLabel, sendLog]() {
    const auto lines = QString::fromUtf8(process->readAllStandardOutput()).split('\n', Qt::SkipEmptyParts);
    for (const auto& line : lines) {
      sendLog(QStringLiteral("%1[%2] %3").arg(processLabel, shortId, line));
    }
  });
  QObject::connect(process, &QProcess::readyReadStandardError, owner, [process, shortId, processLabel, sendLog]() {
    const auto lines = QString::fromUtf8(process->readAllStandardError()).split('\n', Qt::SkipEmptyParts);
    for (const auto& line : lines) {
      sendLog(QStringLiteral("%1[%2] %3").arg(processLabel, shortId, line));
    }
  });
  QObject::connect(process, &QProcess::errorOccurred, owner,
                   [shortId, processLabel, callbacks, sendStatus, sendLog](QProcess::ProcessError) {
    if (callbacks.isStopRequested && callbacks.isStopRequested()) {
      return;
    }
    sendStatus(QStringLiteral("error"), QStringLiteral("process_error"));
    sendLog(QStringLiteral("%1[%2] error").arg(processLabel, shortId));
  });
  QObject::connect(process, &QProcess::finished, owner,
                   [process, options, callbacks, sendStatus, scheduleRunningCheck](int exitCode, QProcess::ExitStatus st) mutable {
    const bool expectedStop = callbacks.consumeExpectedStop ? callbacks.consumeExpectedStop() : false;
    if (expectedStop) {
      if (callbacks.clearCurrentProcess) {
        callbacks.clearCurrentProcess();
      }
      if (callbacks.cleanup) {
        callbacks.cleanup();
      }
      sendStatus(QStringLiteral("stopped"), QString());
      return;
    }

    if (callbacks.isCurrentProcess && !callbacks.isCurrentProcess(process)) {
      return;
    }

    const bool compatRequested = process->property("compat_requested").toBool();
    const bool compatTried = process->property("compat_tried").toBool();
    const bool compatRetried = process->property("compat_retried").toBool();
    if (!compatRequested && !compatTried && !compatRetried) {
      process->setProperty("compat_retried", true);
      process->setProperty("compat_tried", true);
      sendStatus(QStringLiteral("starting"), QString());
      process->setProgram(options.executable);
      process->setArguments(options.compatArguments);
      process->start();
      scheduleRunningCheck();
      return;
    }

    if (callbacks.clearCurrentProcess) {
      callbacks.clearCurrentProcess();
    }
    if (callbacks.cleanup) {
      callbacks.cleanup();
    }
    if (st == QProcess::CrashExit) {
      sendStatus(QStringLiteral("crashed"), QStringLiteral("crash_exit"));
    } else if (exitCode == 0) {
      sendStatus(QStringLiteral("stopped"), QString());
    } else {
      sendStatus(QStringLiteral("crashed"), QStringLiteral("exit_code_%1").arg(exitCode));
    }
  });

  process->setProperty("compat_requested", options.compatRequested);
  process->setProperty("compat_tried", options.compatInitial);
  process->setProperty("compat_retried", false);
  if (!options.timezone.isEmpty()) {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("TZ"), options.timezone);
    process->setProcessEnvironment(env);
  }

  process->setProgram(options.executable);
  process->setArguments(options.arguments);
  process->start();
  scheduleRunningCheck();
  return process;
}

QString SystemChromeEngine::resolveExecutable() {
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
}
