#pragma once

#include <QHash>
#include <QSet>
#include <QObject>
#include <QJsonObject>

class FramedJsonSocket;
class QLocalServer;
class QProcess;
class CdpClient;
class HttpProxyMapper;

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
  QHash<QString, QString> m_chromeProxyAuthExtDirByProfileId;
  QHash<QString, HttpProxyMapper*> m_proxyMapperByProfileId;
  QHash<QString, CdpClient*> m_cdpByProfileId;
  QSet<QString> m_profileStopRequested;
};
