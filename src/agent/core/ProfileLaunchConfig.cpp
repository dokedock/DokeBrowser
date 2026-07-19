#include "ProfileLaunchConfig.h"

#include "BrowserEngineFactory.h"
#include "HttpProxyMapper.h"

#include <QDir>
#include <QHostAddress>
#include <QStandardPaths>
#include <QTcpServer>
#include <QUrl>

namespace ProfileLaunch {

StartRequest parseStartRequest(const QJsonObject& obj) {
  StartRequest request;
  request.profileId = obj.value(QStringLiteral("profile_id")).toString().trimmed();
  request.profileName = obj.value(QStringLiteral("profile_name")).toString();
  request.dataDir = obj.value(QStringLiteral("data_dir")).toString();
  request.browserEngine =
      BrowserEngineFactory::normalizeId(obj.value(QStringLiteral("browser_engine")).toString(QStringLiteral("system_chrome")));
  request.engineConfigJson = obj.value(QStringLiteral("engine_config_json")).toString();
  const QJsonObject engineOptions = obj.value(QStringLiteral("engine_options")).toObject();
  request.engineHumanizeEnabled = engineOptions.value(QStringLiteral("humanize")).toBool(false);
  request.engineGeoipEnabled = engineOptions.value(QStringLiteral("geoip")).toBool(false);
  request.fingerprintMode = obj.value(QStringLiteral("fingerprint_mode")).toString().trimmed();
  request.language = obj.value(QStringLiteral("language")).toString().trimmed();
  request.userAgent = obj.value(QStringLiteral("user_agent")).toString().trimmed();
  request.platform = obj.value(QStringLiteral("platform")).toString().trimmed();
  request.hardwareConcurrency = obj.value(QStringLiteral("hardware_concurrency")).toInt(0);
  request.deviceMemoryGb = obj.value(QStringLiteral("device_memory_gb")).toInt(0);
  request.deviceScaleFactor = obj.value(QStringLiteral("device_scale_factor")).toDouble(0);
  request.screenColorDepth = obj.value(QStringLiteral("screen_color_depth")).toInt(0);
  request.screenAvailWidth = obj.value(QStringLiteral("screen_avail_width")).toInt(0);
  request.screenAvailHeight = obj.value(QStringLiteral("screen_avail_height")).toInt(0);
  request.timezone = obj.value(QStringLiteral("timezone")).toString().trimmed();
  request.resolution = obj.value(QStringLiteral("resolution")).toString().trimmed();
  request.touchEnabled = obj.value(QStringLiteral("touch_enabled")).toBool(false);
  request.geoEnabled = obj.value(QStringLiteral("geo_enabled")).toBool(false);
  request.geoLatitude = obj.value(QStringLiteral("geo_latitude")).toDouble(0);
  request.geoLongitude = obj.value(QStringLiteral("geo_longitude")).toDouble(0);
  request.geoAccuracy = obj.value(QStringLiteral("geo_accuracy")).toDouble(0);
  request.proxy = obj.value(QStringLiteral("proxy")).toObject();
  request.chromeCompatRequested = obj.value(QStringLiteral("chrome_compat")).toBool(false);
  request.chromeCompat = request.chromeCompatRequested;
  if (!request.chromeCompatRequested && qEnvironmentVariableIsSet("TRAE_SANDBOX_STORAGE_PATH")) {
    request.chromeCompat = true;
  }
  return request;
}

QString resolveProfileDataDir(const QString& profileId, const QString& dataDirFromMsg) {
  const QString v = dataDirFromMsg.trimmed();
  if (!v.isEmpty()) {
    return v;
  }
  const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  if (base.isEmpty()) {
    return {};
  }
  return QDir(base).filePath(QStringLiteral("profiles/%1/chrome").arg(profileId));
}

ProxyConfig buildProxyConfig(const QJsonObject& proxyObj, QObject* owner) {
  ProxyConfig config;
  const bool enabled = proxyObj.value(QStringLiteral("enabled")).toBool(false);
  if (!enabled) {
    return config;
  }

  const QString type = proxyObj.value(QStringLiteral("type")).toString().trimmed().toLower();
  const QString host = proxyObj.value(QStringLiteral("host")).toString().trimmed();
  const int port = proxyObj.value(QStringLiteral("port")).toInt(0);
  const QString username = proxyObj.value(QStringLiteral("username")).toString();
  const QString password = proxyObj.value(QStringLiteral("password")).toString();
  const bool hasAuth = !username.isEmpty() || !password.isEmpty();

  if (type.isEmpty() || type == QStringLiteral("direct")) {
    return config;
  }
  if (host.isEmpty() || port <= 0) {
    return config;
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
    config.argument = QStringLiteral("--proxy-server=socks5://%1:%2@%3:%4").arg(u, p, host, QString::number(port));
    return config;
  }

  config.scheme = scheme;
  config.host = host;
  config.port = port;
  config.username = username;
  config.password = password;
  config.enableProxyAuth = hasAuth;

  if (hasAuth && (scheme == QStringLiteral("http") || scheme == QStringLiteral("https"))) {
    HttpProxyMapper::Upstream u;
    u.scheme = scheme;
    u.host = host;
    u.port = port;
    u.username = username;
    u.password = password;
    auto* mapper = new HttpProxyMapper(u, owner);
    QString err;
    if (!mapper->start(&err)) {
      mapper->deleteLater();
      config.argument = QStringLiteral("--proxy-server=%1://%2:%3").arg(scheme, host, QString::number(port));
      return config;
    }
    config.mapper = mapper;
    config.enableProxyAuth = false;
    config.argument = QStringLiteral("--proxy-server=http://127.0.0.1:%1").arg(QString::number(mapper->localPort()));
    return config;
  }

  config.argument = QStringLiteral("--proxy-server=%1://%2:%3").arg(scheme, host, QString::number(port));
  return config;
}

int allocateLocalTcpPort() {
  QTcpServer s;
  if (!s.listen(QHostAddress::LocalHost, 0)) {
    return 0;
  }
  const int port = static_cast<int>(s.serverPort());
  s.close();
  return port;
}

QString windowSizeArgForResolution(const QString& resolution) {
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

  return (winW > 0 && winH > 0) ? QStringLiteral("--window-size=%1,%2").arg(QString::number(winW), QString::number(winH))
                                : QString();
}

} // namespace ProfileLaunch
