#include "IpcClient.h"

#include "shared/ipc/FramedJsonSocket.h"
#include "shared/ipc/IpcNames.h"

#include <QJsonObject>
#include <QLocalSocket>
#include <QTimer>

IpcClient::IpcClient(QObject* parent) : QObject(parent) {
  m_socket = new QLocalSocket(this);
  m_retryTimer = new QTimer(this);
  m_retryTimer->setInterval(500);
  m_retryTimer->setSingleShot(false);
  QObject::connect(m_retryTimer, &QTimer::timeout, this, &IpcClient::tryConnectOnce);
  QObject::connect(m_socket, &QLocalSocket::connected, this, &IpcClient::attachSocket);
  QObject::connect(m_socket, &QLocalSocket::errorOccurred, this, [this]() {
    onIoError(m_socket ? m_socket->errorString() : QStringLiteral("unknown_socket_error"));
  });
}

IpcClient::~IpcClient() = default;

void IpcClient::connectToAgent() {
  if (m_socket->state() == QLocalSocket::ConnectedState ||
      m_socket->state() == QLocalSocket::ConnectingState) {
    return;
  }

  m_retryTimer->start();
  tryConnectOnce();
}

bool IpcClient::isConnected() const {
  return m_connected;
}

void IpcClient::tryConnectOnce() {
  if (m_socket->state() == QLocalSocket::ConnectedState) {
    if (!m_framed) {
      attachSocket();
    }
    m_retryTimer->stop();
    return;
  }

  if (m_socket->state() == QLocalSocket::ConnectingState) {
    return;
  }

  m_socket->abort();
  m_socket->connectToServer(QString::fromUtf8(IpcNames::kAgentServerName));
}

void IpcClient::attachSocket() {
  if (m_framed) {
    m_framed->deleteLater();
    m_framed = nullptr;
  }

  m_framed = new FramedJsonSocket(m_socket, this);
  QObject::connect(m_framed, &FramedJsonSocket::disconnected, this, &IpcClient::onDisconnected);
  QObject::connect(m_framed, &FramedJsonSocket::ioError, this, &IpcClient::onIoError);
  QObject::connect(m_framed, &FramedJsonSocket::jsonReceived, this, &IpcClient::onJsonReceived);

  m_retryTimer->stop();

  if (!m_connected) {
    m_connected = true;
    emit connectedChanged();
  }

  sendHello();
}

void IpcClient::onDisconnected() {
  if (m_connected) {
    m_connected = false;
    emit connectedChanged();
  }
  connectToAgent();
}

void IpcClient::onIoError(const QString& message) {
  emit connectionError(message);
}

void IpcClient::onJsonReceived(const QJsonObject& obj) {
  const QString type = obj.value(QStringLiteral("type")).toString();
  if (type == QStringLiteral("log.line")) {
    emit logLineReceived(obj.value(QStringLiteral("message")).toString());
    return;
  }
  if (type == QStringLiteral("profile.status")) {
    emit profileStatusReceived(obj);
    return;
  }
  if (type == QStringLiteral("proxy.test.result")) {
    emit proxyTestResultReceived(obj);
    return;
  }
  if (type == QStringLiteral("proxy_pool.test.result")) {
    emit proxyPoolTestResultReceived(obj);
    return;
  }
  if (type == QStringLiteral("vpn.status")) {
    emit vpnStatusReceived(obj);
    return;
  }
  if (type == QStringLiteral("engine.list.result")) {
    emit engineListReceived(obj);
    return;
  }
  if (type == QStringLiteral("engine.probe.result")) {
    emit engineProbeReceived(obj);
  }
}

bool IpcClient::sendHello() {
  QJsonObject msg;
  msg.insert(QStringLiteral("type"), QStringLiteral("hello"));
  msg.insert(QStringLiteral("client"), QStringLiteral("app"));
  return send(msg);
}

bool IpcClient::send(const QJsonObject& obj) {
  if (!m_framed) {
    return false;
  }
  return m_framed->send(obj);
}
