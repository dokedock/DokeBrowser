#include "FramedJsonSocket.h"

#include <QJsonDocument>
#include <QLocalSocket>
#include <QtEndian>

namespace {
constexpr qsizetype kHeaderBytes = 4;
}

FramedJsonSocket::FramedJsonSocket(QLocalSocket* socket, QObject* parent)
    : QObject(parent), m_socket(socket) {
  if (!m_socket) {
    return;
  }

  m_socket->setParent(this);
  QObject::connect(m_socket, &QLocalSocket::readyRead, this, &FramedJsonSocket::onReadyRead);
  QObject::connect(
      m_socket, &QLocalSocket::disconnected, this, &FramedJsonSocket::onDisconnected);

  QObject::connect(m_socket, &QLocalSocket::errorOccurred, this, &FramedJsonSocket::onErrorOccurred);
}

FramedJsonSocket::~FramedJsonSocket() = default;

QLocalSocket* FramedJsonSocket::socket() const {
  return m_socket;
}

bool FramedJsonSocket::send(const QJsonObject& obj) {
  if (!m_socket || m_socket->state() != QLocalSocket::ConnectedState) {
    return false;
  }

  const QByteArray payload = QJsonDocument(obj).toJson(QJsonDocument::Compact);
  if (payload.size() <= 0) {
    return false;
  }

  QByteArray frame;
  frame.resize(kHeaderBytes);
  qToBigEndian<quint32>(static_cast<quint32>(payload.size()),
                       reinterpret_cast<uchar*>(frame.data()));
  frame.append(payload);

  const qint64 written = m_socket->write(frame);
  return written == frame.size();
}

void FramedJsonSocket::onReadyRead() {
  if (!m_socket) {
    return;
  }

  m_buffer.append(m_socket->readAll());

  while (m_buffer.size() >= kHeaderBytes) {
    const quint32 len = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(m_buffer.constData()));
    const qsizetype frameBytes = static_cast<qsizetype>(kHeaderBytes + len);

    if (len == 0) {
      m_buffer.remove(0, kHeaderBytes);
      continue;
    }

    if (m_buffer.size() < frameBytes) {
      return;
    }

    const QByteArray payload = m_buffer.mid(kHeaderBytes, static_cast<qsizetype>(len));
    m_buffer.remove(0, frameBytes);

    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
      emit ioError(QStringLiteral("invalid_json_frame"));
      continue;
    }

    emit jsonReceived(doc.object());
  }
}

void FramedJsonSocket::onDisconnected() {
  emit disconnected();
}

void FramedJsonSocket::onErrorOccurred() {
  if (!m_socket) {
    emit ioError(QStringLiteral("unknown_socket_error"));
    return;
  }
  emit ioError(m_socket->errorString());
}

