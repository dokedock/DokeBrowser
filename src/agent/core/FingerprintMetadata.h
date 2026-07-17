#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace FingerprintMetadata {

struct UaBrandVersion {
  QString brand;
  QString version;
  QString fullVersion;
};

struct UaClientHints {
  bool valid = false;
  QVector<UaBrandVersion> brands;
  QString fullVersion;
  QString platform;
  QString platformVersion;
  QString architecture;
  QString bitness;
  QString model;
  bool mobile = false;
};

UaClientHints buildUaClientHints(const QString& userAgent, const QString& navigatorPlatform);
QJsonObject toCdpUserAgentMetadata(const UaClientHints& hints);
QString buildUserAgentDataScript(const UaClientHints& hints);

} // namespace FingerprintMetadata
