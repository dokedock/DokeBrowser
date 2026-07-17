#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>
#include <functional>

class CdpClient;
class HttpProxyMapper;
class QProcess;

class ProfileRuntimeManager : public QObject {
  Q_OBJECT

public:
  using StatusCallback = std::function<void(const QString& profileId, const QString& status, const QString& error)>;
  using LogCallback = std::function<void(const QString& message)>;

  explicit ProfileRuntimeManager(QObject* parent = nullptr);
  ~ProfileRuntimeManager() override;

  void handleMessage(const QJsonObject& obj, StatusCallback status, LogCallback log);

private:
  void cleanupProxyAuthExtension(const QString& profileId);
  void cleanupProxyMapping(const QString& profileId);
  void cleanupCdp(const QString& profileId);
  void cleanupProfileResources(const QString& profileId);

  QHash<QString, QProcess*> m_processByProfileId;
  QHash<QString, QString> m_proxyAuthExtDirByProfileId;
  QHash<QString, HttpProxyMapper*> m_proxyMapperByProfileId;
  QHash<QString, CdpClient*> m_cdpByProfileId;
  QSet<QString> m_stopRequested;
};
