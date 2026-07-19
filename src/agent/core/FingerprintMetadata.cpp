#include "FingerprintMetadata.h"

#include <QJsonArray>
#include <QRegularExpression>

namespace {

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

QString jsString(const QString& s) {
  return QStringLiteral("\"%1\"").arg(jsEscape(s));
}

QString normalizeFullVersion(QString version) {
  version = version.trimmed();
  if (version.isEmpty()) {
    return {};
  }
  QStringList parts = version.split(QLatin1Char('.'));
  while (parts.size() < 4) {
    parts.push_back(QStringLiteral("0"));
  }
  while (parts.size() > 4) {
    parts.removeLast();
  }
  return parts.join(QLatin1Char('.'));
}

QString majorVersion(const QString& fullVersion) {
  return fullVersion.section(QLatin1Char('.'), 0, 0);
}

QString chromeFullVersion(const QString& ua) {
  static const QRegularExpression re(QStringLiteral("(?:Chrome|CriOS)/(\\d+(?:\\.\\d+){0,3})"));
  const auto m = re.match(ua);
  if (!m.hasMatch()) {
    return {};
  }
  return normalizeFullVersion(m.captured(1));
}

QString platformFromUa(const QString& ua, const QString& navigatorPlatform) {
  const QString p = navigatorPlatform.trimmed();
  if (ua.contains(QStringLiteral("Android"), Qt::CaseInsensitive)) {
    return QStringLiteral("Android");
  }
  if (ua.contains(QStringLiteral("iPhone"), Qt::CaseInsensitive) || ua.contains(QStringLiteral("iPad"), Qt::CaseInsensitive)
      || ua.contains(QStringLiteral("iPod"), Qt::CaseInsensitive)) {
    return QStringLiteral("iOS");
  }
  if (p.startsWith(QStringLiteral("Win"), Qt::CaseInsensitive) || ua.contains(QStringLiteral("Windows"), Qt::CaseInsensitive)) {
    return QStringLiteral("Windows");
  }
  if (p.startsWith(QStringLiteral("Mac"), Qt::CaseInsensitive) || ua.contains(QStringLiteral("Mac OS X"), Qt::CaseInsensitive)) {
    return QStringLiteral("macOS");
  }
  if (p.startsWith(QStringLiteral("Linux"), Qt::CaseInsensitive) || ua.contains(QStringLiteral("Linux"), Qt::CaseInsensitive)
      || ua.contains(QStringLiteral("X11"), Qt::CaseInsensitive)) {
    return QStringLiteral("Linux");
  }
  return {};
}

QString platformVersionFromUa(const QString& ua, const QString& platform) {
  if (platform == QStringLiteral("macOS")) {
    static const QRegularExpression re(QStringLiteral("Mac OS X (\\d+)[_\\.](\\d+)(?:[_\\.](\\d+))?"));
    const auto m = re.match(ua);
    if (m.hasMatch()) {
      return QStringLiteral("%1.%2.%3")
          .arg(m.captured(1), m.captured(2), m.captured(3).isEmpty() ? QStringLiteral("0") : m.captured(3));
    }
  }
  if (platform == QStringLiteral("Windows")) {
    static const QRegularExpression re(QStringLiteral("Windows NT (\\d+)\\.(\\d+)"));
    const auto m = re.match(ua);
    if (m.hasMatch()) {
      return QStringLiteral("%1.%2.0").arg(m.captured(1), m.captured(2));
    }
  }
  if (platform == QStringLiteral("Android")) {
    static const QRegularExpression re(QStringLiteral("Android (\\d+(?:\\.\\d+)*)"));
    const auto m = re.match(ua);
    if (m.hasMatch()) {
      return m.captured(1);
    }
  }
  return {};
}

QString architectureFromUa(const QString& ua, const QString& navigatorPlatform) {
  const QString combo = ua + QLatin1Char(' ') + navigatorPlatform;
  if (combo.contains(QStringLiteral("arm64"), Qt::CaseInsensitive) || combo.contains(QStringLiteral("aarch64"), Qt::CaseInsensitive)) {
    return QStringLiteral("arm");
  }
  if (combo.contains(QStringLiteral("x86_64"), Qt::CaseInsensitive) || combo.contains(QStringLiteral("Win64"), Qt::CaseInsensitive)
      || combo.contains(QStringLiteral("x64"), Qt::CaseInsensitive) || combo.contains(QStringLiteral("Intel"), Qt::CaseInsensitive)) {
    return QStringLiteral("x86");
  }
  return {};
}

QJsonArray toBrandsJson(const QVector<FingerprintMetadata::UaBrandVersion>& brands, bool full) {
  QJsonArray out;
  for (const auto& brand : brands) {
    QJsonObject obj;
    obj.insert(QStringLiteral("brand"), brand.brand);
    obj.insert(QStringLiteral("version"), full ? brand.fullVersion : brand.version);
    out.push_back(obj);
  }
  return out;
}

QString brandsJs(const QVector<FingerprintMetadata::UaBrandVersion>& brands, bool full) {
  QStringList parts;
  for (const auto& brand : brands) {
    parts.push_back(QStringLiteral("{brand:%1,version:%2}").arg(jsString(brand.brand), jsString(full ? brand.fullVersion : brand.version)));
  }
  return QStringLiteral("[%1]").arg(parts.join(QLatin1Char(',')));
}

} // namespace

namespace FingerprintMetadata {

UaClientHints buildUaClientHints(const QString& userAgent, const QString& navigatorPlatform) {
  UaClientHints hints;
  const QString ua = userAgent.trimmed();
  const QString full = chromeFullVersion(ua);
  if (ua.isEmpty() || full.isEmpty()) {
    return hints;
  }

  const QString major = majorVersion(full);
  hints.valid = true;
  hints.fullVersion = full;
  hints.platform = platformFromUa(ua, navigatorPlatform);
  hints.platformVersion = platformVersionFromUa(ua, hints.platform);
  hints.architecture = architectureFromUa(ua, navigatorPlatform);
  hints.bitness = hints.architecture.isEmpty() ? QString() : QStringLiteral("64");
  hints.mobile = ua.contains(QStringLiteral("Mobile"), Qt::CaseInsensitive);
  hints.brands = {
      {QStringLiteral("Not;A=Brand"), QStringLiteral("8"), QStringLiteral("8.0.0.0")},
      {QStringLiteral("Chromium"), major, full},
      {QStringLiteral("Google Chrome"), major, full},
  };

  return hints;
}

QJsonObject toCdpUserAgentMetadata(const UaClientHints& hints) {
  QJsonObject metadata;
  if (!hints.valid) {
    return metadata;
  }
  metadata.insert(QStringLiteral("brands"), toBrandsJson(hints.brands, false));
  metadata.insert(QStringLiteral("fullVersionList"), toBrandsJson(hints.brands, true));
  metadata.insert(QStringLiteral("fullVersion"), hints.fullVersion);
  metadata.insert(QStringLiteral("platform"), hints.platform);
  metadata.insert(QStringLiteral("platformVersion"), hints.platformVersion);
  metadata.insert(QStringLiteral("architecture"), hints.architecture);
  metadata.insert(QStringLiteral("bitness"), hints.bitness);
  metadata.insert(QStringLiteral("model"), hints.model);
  metadata.insert(QStringLiteral("mobile"), hints.mobile);
  return metadata;
}

QString buildUserAgentDataScript(const UaClientHints& hints) {
  if (!hints.valid) {
    return {};
  }

  return QStringLiteral(
             "(function(){try{"
             "const brands=%1;"
             "const fullVersionList=%2;"
             "const data={"
             "brands:brands.map(x=>Object.freeze({...x})),"
             "mobile:%3,"
             "platform:%4,"
             "getHighEntropyValues(keys){"
             "const all={architecture:%5,bitness:%6,brands:this.brands,fullVersion:%7,fullVersionList:fullVersionList.map(x=>Object.freeze({...x})),"
             "mobile:this.mobile,model:%8,platform:this.platform,platformVersion:%9,uaFullVersion:%7};"
             "const out={};"
             "(Array.isArray(keys)?keys:[]).forEach(k=>{if(Object.prototype.hasOwnProperty.call(all,k)){out[k]=all[k];}});"
             "return Promise.resolve(out);"
             "},"
             "toJSON(){return {brands:this.brands,mobile:this.mobile,platform:this.platform};}"
             "};"
             "Object.freeze(data.brands);"
             "try{Object.defineProperty(Navigator.prototype,'userAgentData',{get:()=>data,configurable:true});}catch(e){}"
             "try{Object.defineProperty(navigator,'userAgentData',{get:()=>data,configurable:true});}catch(e){}"
             "}catch(e){}})();")
      .arg(brandsJs(hints.brands, false))
      .arg(brandsJs(hints.brands, true))
      .arg(hints.mobile ? QStringLiteral("true") : QStringLiteral("false"))
      .arg(jsString(hints.platform))
      .arg(jsString(hints.architecture))
      .arg(jsString(hints.bitness))
      .arg(jsString(hints.fullVersion))
      .arg(jsString(hints.model))
      .arg(jsString(hints.platformVersion));
}

} // namespace FingerprintMetadata
