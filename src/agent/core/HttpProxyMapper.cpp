#include "HttpProxyMapper.h"

#include <QByteArray>
#include <QHostAddress>
#include <QTimer>

struct HttpProxyMapper::Conn {
  QPointer<QTcpSocket> client;
  QPointer<QAbstractSocket> upstream;
  QByteArray buffer;
  bool headerParsed = false;
  bool isConnect = false;
  QByteArray connectHostPort;
};

static QByteArray readUntilHeadersComplete(QIODevice* dev, QByteArray* buf) {
  if (!dev) {
    return {};
  }
  buf->append(dev->readAll());
  const int idx = buf->indexOf("\r\n\r\n");
  if (idx < 0) {
    return {};
  }
  const QByteArray headers = buf->left(idx + 4);
  buf->remove(0, idx + 4);
  return headers;
}

static QByteArray replaceOrAppendHeader(const QByteArray& headers, const QByteArray& keyLower, const QByteArray& line) {
  QList<QByteArray> outLines;
  const QList<QByteArray> lines = headers.split('\n');
  bool replaced = false;
  for (auto l : lines) {
    if (l.endsWith('\r')) {
      l.chop(1);
    }
    if (l.isEmpty()) {
      continue;
    }
    const int colon = l.indexOf(':');
    if (colon > 0) {
      const QByteArray lk = l.left(colon).trimmed().toLower();
      if (lk == keyLower) {
        if (!replaced) {
          outLines.push_back(line);
          replaced = true;
        }
        continue;
      }
    }
    outLines.push_back(l);
  }
  if (!replaced) {
    outLines.push_back(line);
  }
  QByteArray out;
  for (const auto& l : outLines) {
    out += l;
    out += "\r\n";
  }
  out += "\r\n";
  return out;
}

HttpProxyMapper::HttpProxyMapper(const Upstream& upstream, QObject* parent) : QObject(parent), m_upstream(upstream) {
  connect(&m_server, &QTcpServer::newConnection, this, &HttpProxyMapper::onNewConnection);
}

bool HttpProxyMapper::start(QString* error) {
  if (m_server.isListening()) {
    return true;
  }
  if (!m_server.listen(QHostAddress::LocalHost, 0)) {
    if (error) {
      *error = m_server.errorString();
    }
    return false;
  }
  return true;
}

void HttpProxyMapper::stop() {
  if (m_server.isListening()) {
    m_server.close();
  }
}

quint16 HttpProxyMapper::localPort() const {
  return m_server.serverPort();
}

QSslSocket* HttpProxyMapper::createUpstreamSocket(QObject* parent) const {
  auto* s = new QSslSocket(parent);
  s->setPeerVerifyMode(QSslSocket::VerifyNone);
  return s;
}

QByteArray HttpProxyMapper::basicAuthHeaderValue() const {
  const QByteArray creds = (m_upstream.username + QStringLiteral(":") + m_upstream.password).toUtf8();
  return QByteArrayLiteral("Basic ") + creds.toBase64();
}

void HttpProxyMapper::onNewConnection() {
  while (m_server.hasPendingConnections()) {
    QTcpSocket* client = m_server.nextPendingConnection();
    if (!client) {
      continue;
    }

    auto* st = new Conn();
    st->client = client;

    connect(client, &QTcpSocket::disconnected, this, [st]() {
      if (st->upstream) {
        st->upstream->disconnectFromHost();
      }
      if (st->client) {
        st->client->deleteLater();
      }
      if (st->upstream) {
        st->upstream->deleteLater();
      }
      delete st;
    });

    connect(client, &QTcpSocket::readyRead, this, [this, st]() {
      if (!st->client) {
        return;
      }

      if (!st->headerParsed) {
        const QByteArray headers = readUntilHeadersComplete(st->client, &st->buffer);
        if (headers.isEmpty()) {
          return;
        }

        st->headerParsed = true;
        const int firstLineEnd = headers.indexOf("\r\n");
        const QByteArray requestLine = (firstLineEnd >= 0) ? headers.left(firstLineEnd) : headers;
        const QList<QByteArray> parts = requestLine.split(' ');
        if (parts.size() >= 2 && parts.at(0).toUpper() == "CONNECT") {
          st->isConnect = true;
          st->connectHostPort = parts.at(1).trimmed();
        }

        QAbstractSocket* upstream = nullptr;
        if (m_upstream.scheme.trimmed().toLower() == QStringLiteral("https")) {
          auto* ssl = createUpstreamSocket(this);
          upstream = ssl;
          connect(ssl, &QSslSocket::encrypted, this, [this, st, headers]() {
            if (!st->upstream || !st->client) {
              return;
            }
            QByteArray authLine = QByteArrayLiteral("Proxy-Authorization: ") + basicAuthHeaderValue();
            authLine += "\r\n";
            QByteArray outHeaders = headers;
            outHeaders = replaceOrAppendHeader(outHeaders, QByteArrayLiteral("proxy-authorization"), authLine.trimmed());
            if (st->isConnect) {
              const QByteArray hp = st->connectHostPort.isEmpty() ? QByteArray() : st->connectHostPort;
              QByteArray connectReq;
              connectReq += "CONNECT " + hp + " HTTP/1.1\r\n";
              connectReq += "Host: " + hp + "\r\n";
              connectReq += "Proxy-Connection: Keep-Alive\r\n";
              connectReq += "Proxy-Authorization: " + basicAuthHeaderValue() + "\r\n\r\n";
              st->upstream->write(connectReq);
            } else {
              st->upstream->write(outHeaders);
              if (!st->buffer.isEmpty()) {
                st->upstream->write(st->buffer);
                st->buffer.clear();
              }
            }
          });
        } else {
          auto* tcp = new QTcpSocket(this);
          upstream = tcp;
          connect(tcp, &QTcpSocket::connected, this, [this, st, headers]() {
            if (!st->upstream || !st->client) {
              return;
            }
            QByteArray authLine = QByteArrayLiteral("Proxy-Authorization: ") + basicAuthHeaderValue();
            authLine += "\r\n";
            QByteArray outHeaders = headers;
            outHeaders = replaceOrAppendHeader(outHeaders, QByteArrayLiteral("proxy-authorization"), authLine.trimmed());
            if (st->isConnect) {
              const QByteArray hp = st->connectHostPort.isEmpty() ? QByteArray() : st->connectHostPort;
              QByteArray connectReq;
              connectReq += "CONNECT " + hp + " HTTP/1.1\r\n";
              connectReq += "Host: " + hp + "\r\n";
              connectReq += "Proxy-Connection: Keep-Alive\r\n";
              connectReq += "Proxy-Authorization: " + basicAuthHeaderValue() + "\r\n\r\n";
              st->upstream->write(connectReq);
            } else {
              st->upstream->write(outHeaders);
              if (!st->buffer.isEmpty()) {
                st->upstream->write(st->buffer);
                st->buffer.clear();
              }
            }
          });
        }

        st->upstream = upstream;

        connect(upstream, &QAbstractSocket::readyRead, this, [st]() {
          if (!st->client || !st->upstream) {
            return;
          }

          if (st->isConnect) {
            st->buffer.append(st->upstream->readAll());
            const int idx = st->buffer.indexOf("\r\n\r\n");
            if (idx < 0) {
              return;
            }
            const QByteArray respHeaders = st->buffer.left(idx + 4);
            st->buffer.remove(0, idx + 4);
            st->client->write(respHeaders);
            if (!st->buffer.isEmpty()) {
              st->client->write(st->buffer);
              st->buffer.clear();
            }
            st->isConnect = false;
            QObject::connect(st->client, &QTcpSocket::readyRead, st->client, [st]() {
              if (st->client && st->upstream) {
                st->upstream->write(st->client->readAll());
              }
            });
            QObject::connect(st->upstream, &QAbstractSocket::readyRead, st->upstream, [st]() {
              if (st->client && st->upstream) {
                st->client->write(st->upstream->readAll());
              }
            });
            return;
          }

          st->client->write(st->upstream->readAll());
        });

        connect(upstream, &QAbstractSocket::disconnected, this, [st]() {
          if (st->client) {
            st->client->disconnectFromHost();
          }
        });

        if (auto* ssl = qobject_cast<QSslSocket*>(upstream)) {
          ssl->connectToHostEncrypted(m_upstream.host, static_cast<quint16>(m_upstream.port));
        } else if (auto* tcp = qobject_cast<QTcpSocket*>(upstream)) {
          tcp->connectToHost(m_upstream.host, static_cast<quint16>(m_upstream.port));
        }
        return;
      }

      if (st->upstream) {
        st->upstream->write(st->client->readAll());
      }
    });
  }
}

