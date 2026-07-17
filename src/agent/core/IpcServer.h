#pragma once

#include <QObject>
#include <QJsonObject>

class FramedJsonSocket;
class QLocalServer;
class OpenVpnManager;
class ProfileRuntimeManager;

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
  OpenVpnManager* m_openVpnManager = nullptr;
  ProfileRuntimeManager* m_profileRuntimeManager = nullptr;
};
