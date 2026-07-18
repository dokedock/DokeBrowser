#include "IpcServer.h"

#include "BrowserEngineFactory.h"
#include "DokeChromiumEngine.h"
#include "OpenVpnManager.h"
#include "ProfileRuntimeManager.h"
#include "ProxyTestRunner.h"
#include "shared/ipc/FramedJsonSocket.h"
#include "shared/ipc/IpcNames.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>

namespace {
QJsonObject engineInfo(const BrowserEngineDescriptor& engine) {
  QJsonObject obj;
  obj.insert(QStringLiteral("id"), engine.id);
  obj.insert(QStringLiteral("available"), engine.available());
  if (engine.available()) {
    obj.insert(QStringLiteral("executable"), engine.executable);
  } else {
    obj.insert(QStringLiteral("error"), engine.error);
  }
  return obj;
}

} // namespace

IpcServer::IpcServer(QObject* parent) : QObject(parent) {
  m_server = new QLocalServer(this);
  m_openVpnManager = new OpenVpnManager(this);
  m_profileRuntimeManager = new ProfileRuntimeManager(this);
  QObject::connect(m_server, &QLocalServer::newConnection, this, &IpcServer::onNewConnection);
}

IpcServer::~IpcServer() = default;

bool IpcServer::start() {
  QLocalServer::removeServer(QString::fromUtf8(IpcNames::kAgentServerName));
  if (m_server->listen(QString::fromUtf8(IpcNames::kAgentServerName))) {
    return true;
  }
  emit logLine(QStringLiteral("listen_failed: %1").arg(m_server->errorString()));
  return false;
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

  if (type == QStringLiteral("engine.list")) {
    QJsonArray engines;
    for (const auto& engine : BrowserEngineFactory::listEngines()) {
      engines.push_back(engineInfo(engine));
    }

    QJsonObject result;
    result.insert(QStringLiteral("type"), QStringLiteral("engine.list.result"));
    result.insert(QStringLiteral("engines"), engines);
    m_peer->send(result);
    return;
  }

  if (type == QStringLiteral("engine.probe")) {
    const QString engineId =
        BrowserEngineFactory::normalizeId(obj.value(QStringLiteral("browser_engine")).toString(QStringLiteral("system_chrome")));
    const QString engineConfigJson = obj.value(QStringLiteral("engine_config_json")).toString();
    const QString profileId = obj.value(QStringLiteral("profile_id")).toString().trimmed();

    QJsonObject result;
    result.insert(QStringLiteral("type"), QStringLiteral("engine.probe.result"));
    result.insert(QStringLiteral("id"), engineId);
    if (!profileId.isEmpty()) {
      result.insert(QStringLiteral("profile_id"), profileId);
    }

    const QString unsupportedError = BrowserEngineFactory::notFoundErrorFor(engineId);
    if (unsupportedError.startsWith(QStringLiteral("unsupported_browser_engine:"))) {
      result.insert(QStringLiteral("available"), false);
      result.insert(QStringLiteral("error"), unsupportedError);
      m_peer->send(result);
      return;
    }

    const DokeChromiumEngine::ProbeResult dokeProbe =
        engineId == QStringLiteral("doke_chromium") ? DokeChromiumEngine::probe(engineConfigJson)
                                                    : DokeChromiumEngine::ProbeResult();
    const QString executable =
        engineId == QStringLiteral("doke_chromium") ? dokeProbe.resolution.executable : BrowserEngineFactory::executableFor(engineId);
    result.insert(QStringLiteral("available"), !executable.isEmpty());
    if (executable.isEmpty()) {
      result.insert(QStringLiteral("error"),
                    engineId == QStringLiteral("doke_chromium") ? dokeProbe.resolution.error
                                                                : BrowserEngineFactory::notFoundErrorFor(engineId));
    } else {
      result.insert(QStringLiteral("executable"), executable);
      if (engineId == QStringLiteral("doke_chromium")) {
        if (!dokeProbe.version.isEmpty()) {
          result.insert(QStringLiteral("version"), dokeProbe.version);
        }
        if (!dokeProbe.versionError.isEmpty()) {
          result.insert(QStringLiteral("version_error"), dokeProbe.versionError);
        }
        if (!dokeProbe.nativeProbeError.isEmpty()) {
          result.insert(QStringLiteral("native_probe_error"), dokeProbe.nativeProbeError);
        }
        if (!dokeProbe.probeProtocol.isEmpty()) {
          result.insert(QStringLiteral("probe_protocol"), dokeProbe.probeProtocol);
        }
        QJsonArray capabilities;
        for (const auto& capability : dokeProbe.capabilities) {
          capabilities.push_back(capability);
        }
        result.insert(QStringLiteral("capabilities"), capabilities);
        QJsonArray nativeCapabilities;
        for (const auto& capability : dokeProbe.nativeCapabilities) {
          nativeCapabilities.push_back(capability);
        }
        result.insert(QStringLiteral("native_capabilities"), nativeCapabilities);
        QJsonArray missingNativeCapabilities;
        for (const auto& capability : dokeProbe.missingNativeCapabilities) {
          missingNativeCapabilities.push_back(capability);
        }
        result.insert(QStringLiteral("missing_native_capabilities"), missingNativeCapabilities);
      }
    }
    m_peer->send(result);
    return;
  }

  if (type == QStringLiteral("profile.start") || type == QStringLiteral("profile.stop")) {
    m_profileRuntimeManager->handleMessage(
        obj,
        [this](const QString& profileId, const QString& status, const QString& error, int debugPort) {
          if (!m_peer) {
            return;
          }
          QJsonObject msg;
          msg.insert(QStringLiteral("type"), QStringLiteral("profile.status"));
          msg.insert(QStringLiteral("profile_id"), profileId);
          msg.insert(QStringLiteral("status"), status);
          msg.insert(QStringLiteral("error"), error);
          if (debugPort > 0) {
            msg.insert(QStringLiteral("debug_port"), debugPort);
          }
          m_peer->send(msg);
        },
        [this](const QString& message) {
          if (!m_peer) {
            return;
          }
          QJsonObject log;
          log.insert(QStringLiteral("type"), QStringLiteral("log.line"));
          log.insert(QStringLiteral("message"), message);
          m_peer->send(log);
        });
    return;
  }

  if (type == QStringLiteral("proxy_pool.test")) {
    QPointer<FramedJsonSocket> peerPtr(m_peer);
    ProxyTestRunner::start(ProxyTestRunner::parseRequest(obj, ProxyTestRunner::Scope::Pool), this,
                           [peerPtr](const QJsonObject& result) {
                             if (peerPtr) {
                               peerPtr->send(result);
                             }
                           });
    return;
  }

  if (type == QStringLiteral("proxy.test")) {
    QPointer<FramedJsonSocket> peerPtr(m_peer);
    ProxyTestRunner::start(ProxyTestRunner::parseRequest(obj, ProxyTestRunner::Scope::Profile), this,
                           [peerPtr](const QJsonObject& result) {
                             if (peerPtr) {
                               peerPtr->send(result);
                             }
                           });
    return;
  }

  if (type == QStringLiteral("vpn.openvpn.start")) {
    m_openVpnManager->startOpenVpn(
        obj,
        [this](const QString& profileId, const QString& status, const QString& error) {
          if (!m_peer) {
            return;
          }
          QJsonObject msg;
          msg.insert(QStringLiteral("type"), QStringLiteral("vpn.status"));
          msg.insert(QStringLiteral("profile_id"), profileId);
          msg.insert(QStringLiteral("status"), status);
          msg.insert(QStringLiteral("error"), error);
          m_peer->send(msg);
        },
        [this](const QString& message) {
          if (!m_peer) {
            return;
          }
          QJsonObject log;
          log.insert(QStringLiteral("type"), QStringLiteral("log.line"));
          log.insert(QStringLiteral("message"), message);
          m_peer->send(log);
        });
    return;
  }

  if (type == QStringLiteral("vpn.openvpn.stop")) {
    m_openVpnManager->stopOpenVpn(obj, [this](const QString& profileId, const QString& status, const QString& error) {
      if (!m_peer) {
        return;
      }
      QJsonObject msg;
      msg.insert(QStringLiteral("type"), QStringLiteral("vpn.status"));
      msg.insert(QStringLiteral("profile_id"), profileId);
      msg.insert(QStringLiteral("status"), status);
      msg.insert(QStringLiteral("error"), error);
      m_peer->send(msg);
    });
    return;
  }
}
