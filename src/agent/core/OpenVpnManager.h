#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>

class QProcess;

class OpenVpnManager : public QObject {
  Q_OBJECT

public:
  struct StartRequest {
    QString profileId;
    QString exe;
    QString config;
    bool socksEnabled = false;
    QString socksHost;
    int socksPort = 0;
    QString socksUser;
    QString socksPass;
  };

  using StatusCallback = std::function<void(const QString& profileId, const QString& status, const QString& error)>;
  using LogCallback = std::function<void(const QString& message)>;

  explicit OpenVpnManager(QObject* parent = nullptr);
  ~OpenVpnManager() override;

  void startOpenVpn(const QJsonObject& obj, StatusCallback status, LogCallback log);
  void stopOpenVpn(const QJsonObject& obj, StatusCallback status);

  static StartRequest parseStartRequest(const QJsonObject& obj);
  static QString validationError(const StartRequest& request);
  static QStringList buildArguments(const StartRequest& request, const QString& socksAuthFile);

private:
  void cleanupAuthFile(const QString& profileId);

  QHash<QString, QProcess*> m_processByProfileId;
  QHash<QString, QString> m_socksAuthFileByProfileId;
};
