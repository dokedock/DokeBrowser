#pragma once

#include <QHash>
#include <QObject>
#include <QJsonObject>

class FramedJsonSocket;
class QLocalServer;
class QProcess;

class IpcServer : public QObject {
  Q_OBJECT

public:
  explicit IpcServer(QObject* parent = nullptr);
  ~IpcServer() override;

  bool start();

signals:
  void logLine(const QString& line);

private:
  void onNewConnection();
  void onPeerDisconnected();
  void onPeerError(const QString& message);
  void onPeerJson(const QJsonObject& obj);

  QLocalServer* m_server = nullptr;
  FramedJsonSocket* m_peer = nullptr;
  QHash<QString, QProcess*> m_openvpnByProfileId;
  QHash<QString, QString> m_openvpnSocksAuthFileByProfileId;
  QHash<QString, QProcess*> m_profileProcByProfileId;
};
