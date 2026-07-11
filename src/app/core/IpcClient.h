#pragma once

#include <QObject>
#include <QJsonObject>

class QLocalSocket;
class QTimer;

class FramedJsonSocket;

class IpcClient : public QObject {
  Q_OBJECT

public:
  explicit IpcClient(QObject* parent = nullptr);
  ~IpcClient() override;

  void connectToAgent();
  bool isConnected() const;

  bool sendHello();
  bool send(const QJsonObject& obj);

signals:
  void connectedChanged();
  void logLineReceived(const QString& line);
  void connectionError(const QString& message);
  void proxyTestResultReceived(const QJsonObject& result);
  void vpnStatusReceived(const QJsonObject& result);

private:
  void tryConnectOnce();
  void attachSocket();
  void onDisconnected();
  void onIoError(const QString& message);
  void onJsonReceived(const QJsonObject& obj);

  QLocalSocket* m_socket = nullptr;
  FramedJsonSocket* m_framed = nullptr;
  QTimer* m_retryTimer = nullptr;
  bool m_connected = false;
};
