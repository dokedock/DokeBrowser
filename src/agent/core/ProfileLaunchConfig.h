#pragma once

#include <QJsonObject>
#include <QString>

class HttpProxyMapper;
class QObject;

namespace ProfileLaunch {

struct StartRequest {
  QString profileId;
  QString profileName;
  QString dataDir;
  QString browserEngine;
  QString engineConfigJson;
  bool engineHumanizeEnabled = false;
  bool engineGeoipEnabled = false;
  QString fingerprintMode;
  QString language;
  QString userAgent;
  QString platform;
  int hardwareConcurrency = 0;
  int deviceMemoryGb = 0;
  double deviceScaleFactor = 0;
  QString timezone;
  QString resolution;
  bool touchEnabled = false;
  bool geoEnabled = false;
  double geoLatitude = 0;
  double geoLongitude = 0;
  double geoAccuracy = 0;
  QJsonObject proxy;
  bool chromeCompatRequested = false;
  bool chromeCompat = false;
};

struct ProxyConfig {
  QString argument;
  QString scheme;
  QString host;
  int port = 0;
  QString username;
  QString password;
  bool enableProxyAuth = false;
  HttpProxyMapper* mapper = nullptr;
};

StartRequest parseStartRequest(const QJsonObject& obj);
QString resolveProfileDataDir(const QString& profileId, const QString& dataDirFromMsg);
ProxyConfig buildProxyConfig(const QJsonObject& proxyObj, QObject* owner);
int allocateLocalTcpPort();
QString windowSizeArgForResolution(const QString& resolution);

} // namespace ProfileLaunch
