#include "IpcServer.h"

#include "shared/ipc/FramedJsonSocket.h"
#include "shared/ipc/IpcNames.h"

#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QProcess>
#include <QTemporaryFile>
#include <QTimer>
#include <QUrl>

IpcServer::IpcServer(QObject* parent) : QObject(parent) {
  m_server = new QLocalServer(this);
  QObject::connect(m_server, &QLocalServer::newConnection, this, &IpcServer::onNewConnection);
}

IpcServer::~IpcServer() {
  const auto keys = m_openvpnByProfileId.keys();
  for (const auto& k : keys) {
    QProcess* p = m_openvpnByProfileId.take(k);
    if (p) {
      p->kill();
      p->deleteLater();
    }
  }

  const auto authKeys = m_openvpnSocksAuthFileByProfileId.keys();
  for (const auto& k : authKeys) {
    const QString path = m_openvpnSocksAuthFileByProfileId.take(k);
    if (!path.isEmpty()) {
      QFile::remove(path);
    }
  }
}

bool IpcServer::start() {
  QLocalServer::removeServer(QString::fromUtf8(IpcNames::kAgentServerName));
  return m_server->listen(QString::fromUtf8(IpcNames::kAgentServerName));
}

void IpcServer::onNewConnection() {
  while (m_server->hasPendingConnections()) {
    QLocalSocket* sock = m_server->nextPendingConnection();
    if (!sock) {
      continue;
    }

    if (m_peer) {
      m_peer->deleteLater();
      m_peer = nullptr;
    }

    m_peer = new FramedJsonSocket(sock, this);
    QObject::connect(m_peer, &FramedJsonSocket::disconnected, this, &IpcServer::onPeerDisconnected);
    QObject::connect(m_peer, &FramedJsonSocket::ioError, this, &IpcServer::onPeerError);
    QObject::connect(m_peer, &FramedJsonSocket::jsonReceived, this, &IpcServer::onPeerJson);

    QJsonObject msg;
    msg.insert(QStringLiteral("type"), QStringLiteral("log.line"));
    msg.insert(QStringLiteral("message"), QStringLiteral("agent_connected"));
    m_peer->send(msg);
  }
}

void IpcServer::onPeerDisconnected() {
  emit logLine(QStringLiteral("peer_disconnected"));
}

void IpcServer::onPeerError(const QString& message) {
  emit logLine(QStringLiteral("peer_error: %1").arg(message));
}

void IpcServer::onPeerJson(const QJsonObject& obj) {
  if (!m_peer) {
    return;
  }

  const QString type = obj.value(QStringLiteral("type")).toString();
  if (type == QStringLiteral("hello")) {
    QJsonObject ack;
    ack.insert(QStringLiteral("type"), QStringLiteral("hello.ack"));
    ack.insert(QStringLiteral("agent"), QStringLiteral("dokebrowser_agent"));
    ack.insert(QStringLiteral("version"), 1);
    m_peer->send(ack);

    QJsonObject log;
    log.insert(QStringLiteral("type"), QStringLiteral("log.line"));
    log.insert(QStringLiteral("message"), QStringLiteral("hello_ok"));
    m_peer->send(log);
    return;
  }

  if (type == QStringLiteral("profile.start") || type == QStringLiteral("profile.stop")) {
    const QString profileId = obj.value(QStringLiteral("profile_id")).toString();
    const QString profileName = obj.value(QStringLiteral("profile_name")).toString();

    QJsonObject log;
    log.insert(QStringLiteral("type"), QStringLiteral("log.line"));
    log.insert(QStringLiteral("message"),
               QStringLiteral("%1 profile=%2 (%3)")
                   .arg(type, profileName.isEmpty() ? QStringLiteral("-") : profileName,
                        profileId.isEmpty() ? QStringLiteral("-") : profileId));
    m_peer->send(log);
    return;
  }

  if (type == QStringLiteral("proxy.test")) {
    const QJsonObject proxy = obj.value(QStringLiteral("proxy")).toObject();
    const bool enabled = proxy.value(QStringLiteral("enabled")).toBool(false);
    const QString proxyType = proxy.value(QStringLiteral("type")).toString(QStringLiteral("direct")).toLower();
    const QString host = proxy.value(QStringLiteral("host")).toString();
    const int port = proxy.value(QStringLiteral("port")).toInt(0);
    const QString username = proxy.value(QStringLiteral("username")).toString();
    const QString password = proxy.value(QStringLiteral("password")).toString();
    const QString urlStr = obj.value(QStringLiteral("url")).toString(QStringLiteral("https://api.ipify.org?format=json"));

    QJsonObject result;
    result.insert(QStringLiteral("type"), QStringLiteral("proxy.test.result"));

    if (enabled && proxyType != QStringLiteral("direct") && (host.isEmpty() || port <= 0)) {
      result.insert(QStringLiteral("ok"), false);
      result.insert(QStringLiteral("error"), QStringLiteral("invalid_proxy_config"));
      m_peer->send(result);
      return;
    }

    QPointer<FramedJsonSocket> peerPtr(m_peer);
    QElapsedTimer timer;
    timer.start();

    auto* nam = new QNetworkAccessManager(this);
    if (!enabled || proxyType == QStringLiteral("direct")) {
      nam->setProxy(QNetworkProxy::NoProxy);
    } else {
      QNetworkProxy px;
      if (proxyType == QStringLiteral("socks5")) {
        px.setType(QNetworkProxy::Socks5Proxy);
      } else {
        px.setType(QNetworkProxy::HttpProxy);
      }
      px.setHostName(host);
      px.setPort(static_cast<quint16>(port));
      px.setUser(username);
      px.setPassword(password);
      nam->setProxy(px);
    }

    QNetworkRequest req{QUrl(urlStr)};
    QNetworkReply* reply = nam->get(req);

    QObject::connect(reply, &QNetworkReply::finished, this, [peerPtr, reply, nam, timer]() mutable {
      const int durationMs = static_cast<int>(timer.elapsed());

      if (!peerPtr) {
        reply->deleteLater();
        nam->deleteLater();
        return;
      }

      const QVariant sc = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
      const int statusCode = sc.isValid() ? sc.toInt() : 0;
      const int qtError = static_cast<int>(reply->error());
      const QString errStr = reply->errorString();
      const QByteArray body = reply->readAll();

      QString observedIp;
      QJsonParseError parseErr{};
      const QJsonDocument doc = QJsonDocument::fromJson(body, &parseErr);
      if (parseErr.error == QJsonParseError::NoError && doc.isObject()) {
        observedIp = doc.object().value(QStringLiteral("ip")).toString();
      }

      const bool ok = (reply->error() == QNetworkReply::NoError) && (statusCode >= 200 && statusCode < 300);

      QJsonObject result;
      result.insert(QStringLiteral("type"), QStringLiteral("proxy.test.result"));
      result.insert(QStringLiteral("ok"), ok);
      result.insert(QStringLiteral("status_code"), statusCode);
      result.insert(QStringLiteral("duration_ms"), durationMs);
      result.insert(QStringLiteral("qt_error"), qtError);
      result.insert(QStringLiteral("error"), ok ? QString() : errStr);
      result.insert(QStringLiteral("observed_ip"), observedIp);

      peerPtr->send(result);

      reply->deleteLater();
      nam->deleteLater();
    });

    return;
  }

  if (type == QStringLiteral("vpn.openvpn.start")) {
    const QString profileId = obj.value(QStringLiteral("profile_id")).toString();
    const QString exe = obj.value(QStringLiteral("exe")).toString(QStringLiteral("openvpn"));
    const QString config = obj.value(QStringLiteral("config")).toString();
    const QJsonObject socks = obj.value(QStringLiteral("socks")).toObject();
    const bool socksEnabled = socks.value(QStringLiteral("enabled")).toBool(false);
    const QString socksHost = socks.value(QStringLiteral("host")).toString();
    const int socksPort = socks.value(QStringLiteral("port")).toInt(0);
    const QString socksUser = socks.value(QStringLiteral("username")).toString();
    const QString socksPass = socks.value(QStringLiteral("password")).toString();

    auto sendStatus = [this, profileId](const QString& status, const QString& error) {
      if (!m_peer) {
        return;
      }
      QJsonObject msg;
      msg.insert(QStringLiteral("type"), QStringLiteral("vpn.status"));
      msg.insert(QStringLiteral("profile_id"), profileId);
      msg.insert(QStringLiteral("status"), status);
      msg.insert(QStringLiteral("error"), error);
      m_peer->send(msg);
    };

    if (profileId.isEmpty()) {
      sendStatus(QStringLiteral("error"), QStringLiteral("missing_profile_id"));
      return;
    }

    QProcess* existing = m_openvpnByProfileId.value(profileId);
    if (existing && existing->state() != QProcess::NotRunning) {
      sendStatus(QStringLiteral("running"), QString());
      return;
    }

    if (config.isEmpty()) {
      sendStatus(QStringLiteral("error"), QStringLiteral("missing_config"));
      return;
    }

    QString socksAuthFile;
    if (socksEnabled) {
      if (socksHost.isEmpty() || socksPort <= 0) {
        sendStatus(QStringLiteral("error"), QStringLiteral("invalid_socks_proxy"));
        return;
      }

      if (!socksUser.isEmpty() || !socksPass.isEmpty()) {
        QTemporaryFile tf;
        tf.setAutoRemove(false);
        if (!tf.open()) {
          sendStatus(QStringLiteral("error"), QStringLiteral("socks_authfile_open_failed"));
          return;
        }
        tf.write(socksUser.toUtf8());
        tf.write("\n");
        tf.write(socksPass.toUtf8());
        tf.write("\n");
        tf.flush();
        tf.close();
        socksAuthFile = tf.fileName();
        m_openvpnSocksAuthFileByProfileId.insert(profileId, socksAuthFile);
      }
    }

    auto* p = new QProcess(this);
    m_openvpnByProfileId.insert(profileId, p);

    QStringList args;
    args << QStringLiteral("--config") << config;
    if (socksEnabled) {
      args << QStringLiteral("--socks-proxy") << socksHost << QString::number(socksPort);
      if (!socksAuthFile.isEmpty()) {
        args << socksAuthFile;
      }
    }

    p->setProgram(exe.isEmpty() ? QStringLiteral("openvpn") : exe);
    p->setArguments(args);

    const QString shortId = profileId.left(8);
    QObject::connect(p, &QProcess::started, this, [this, profileId, shortId, sendStatus]() mutable {
      sendStatus(QStringLiteral("running"), QString());
      if (m_peer) {
        QJsonObject log;
        log.insert(QStringLiteral("type"), QStringLiteral("log.line"));
        log.insert(QStringLiteral("message"), QStringLiteral("openvpn[%1] started").arg(shortId));
        m_peer->send(log);
      }
    });

    QObject::connect(p, &QProcess::readyReadStandardOutput, this, [this, p, shortId]() {
      if (!m_peer) {
        return;
      }
      const auto lines = QString::fromUtf8(p->readAllStandardOutput()).split('\n', Qt::SkipEmptyParts);
      for (const auto& line : lines) {
        QJsonObject log;
        log.insert(QStringLiteral("type"), QStringLiteral("log.line"));
        log.insert(QStringLiteral("message"), QStringLiteral("openvpn[%1] %2").arg(shortId, line));
        m_peer->send(log);
      }
    });

    QObject::connect(p, &QProcess::readyReadStandardError, this, [this, p, shortId]() {
      if (!m_peer) {
        return;
      }
      const auto lines = QString::fromUtf8(p->readAllStandardError()).split('\n', Qt::SkipEmptyParts);
      for (const auto& line : lines) {
        QJsonObject log;
        log.insert(QStringLiteral("type"), QStringLiteral("log.line"));
        log.insert(QStringLiteral("message"), QStringLiteral("openvpn[%1] %2").arg(shortId, line));
        m_peer->send(log);
      }
    });

    QObject::connect(p, &QProcess::errorOccurred, this, [this, profileId, shortId, sendStatus](QProcess::ProcessError) mutable {
      sendStatus(QStringLiteral("error"), QStringLiteral("openvpn_process_error"));
      if (m_peer) {
        QJsonObject log;
        log.insert(QStringLiteral("type"), QStringLiteral("log.line"));
        log.insert(QStringLiteral("message"), QStringLiteral("openvpn[%1] error").arg(shortId));
        m_peer->send(log);
      }
    });

    QObject::connect(p, &QProcess::finished, this, [this, profileId, shortId, sendStatus](int exitCode, QProcess::ExitStatus st) mutable {
      m_openvpnByProfileId.remove(profileId);
      sendStatus(st == QProcess::NormalExit ? QStringLiteral("stopped") : QStringLiteral("crashed"),
                 QStringLiteral("exitCode=%1").arg(exitCode));

      const QString authFile = m_openvpnSocksAuthFileByProfileId.take(profileId);
      if (!authFile.isEmpty()) {
        QFile::remove(authFile);
      }

      if (m_peer) {
        QJsonObject log;
        log.insert(QStringLiteral("type"), QStringLiteral("log.line"));
        log.insert(QStringLiteral("message"), QStringLiteral("openvpn[%1] finished").arg(shortId));
        m_peer->send(log);
      }
    });

    p->start();
    return;
  }

  if (type == QStringLiteral("vpn.openvpn.stop")) {
    const QString profileId = obj.value(QStringLiteral("profile_id")).toString();
    auto sendStatus = [this, profileId](const QString& status, const QString& error) {
      if (!m_peer) {
        return;
      }
      QJsonObject msg;
      msg.insert(QStringLiteral("type"), QStringLiteral("vpn.status"));
      msg.insert(QStringLiteral("profile_id"), profileId);
      msg.insert(QStringLiteral("status"), status);
      msg.insert(QStringLiteral("error"), error);
      m_peer->send(msg);
    };

    if (profileId.isEmpty()) {
      sendStatus(QStringLiteral("error"), QStringLiteral("missing_profile_id"));
      return;
    }

    QProcess* p = m_openvpnByProfileId.value(profileId);
    if (!p) {
      sendStatus(QStringLiteral("stopped"), QString());
      return;
    }

    p->terminate();
    QPointer<QProcess> pp(p);
    QTimer::singleShot(1500, this, [pp]() {
      if (pp && pp->state() != QProcess::NotRunning) {
        pp->kill();
      }
    });

    sendStatus(QStringLiteral("stopping"), QString());
    return;
  }
}
