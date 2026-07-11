#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QObject>

class QLocalSocket;

class FramedJsonSocket : public QObject {
  Q_OBJECT

public:
  explicit FramedJsonSocket(QLocalSocket* socket, QObject* parent = nullptr);
  ~FramedJsonSocket() override;

  bool send(const QJsonObject& obj);
  QLocalSocket* socket() const;

signals:
  void jsonReceived(const QJsonObject& obj);
  void disconnected();
  void ioError(const QString& message);

private:
  void onReadyRead();
  void onDisconnected();
  void onErrorOccurred();

  QLocalSocket* m_socket = nullptr;
  QByteArray m_buffer;
};

