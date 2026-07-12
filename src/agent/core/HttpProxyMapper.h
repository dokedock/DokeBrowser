#pragma once

#include <QObject>
#include <QPointer>
#include <QTcpSocket>
#include <QSslSocket>
#include <QTcpServer>

class HttpProxyMapper : public QObject {
  Q_OBJECT

public:
  struct Upstream {
    QString scheme;
    QString host;
    int port = 0;
    QString username;
    QString password;
  };

  explicit HttpProxyMapper(const Upstream& upstream, QObject* parent = nullptr);

  bool start(QString* error);
  void stop();
  quint16 localPort() const;

private:
  struct Conn;

  void onNewConnection();
  QSslSocket* createUpstreamSocket(QObject* parent) const;
  QByteArray basicAuthHeaderValue() const;

  Upstream m_upstream;
  QTcpServer m_server;
};
