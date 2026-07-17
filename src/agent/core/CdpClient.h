#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QWebSocket>

class CdpClient : public QObject {
  Q_OBJECT

public:
  struct Fingerprint {
    bool enabled = true;
    QString language;
    QString userAgent;
    QString platform;
    int hardwareConcurrency = 0;
    int deviceMemoryGb = 0;
    double deviceScaleFactor = 0;
    quint32 seed = 0;
    QString timezone;
    QString resolution;
    bool touchEnabled = false;
    bool geoEnabled = false;
    double geoLatitude = 0;
    double geoLongitude = 0;
    double geoAccuracy = 0;
  };

  explicit CdpClient(const QUrl& wsUrl, const Fingerprint& fp, const QString& initialUrl, QObject* parent = nullptr);
  void start();
  void stop();

signals:
  void ready();
  void error(const QString& message);

private:
  void send(const QJsonObject& msg);
  void sendToSession(const QString& sessionId, const QString& method, const QJsonObject& params);
  void sendRoot(const QString& method, const QJsonObject& params);
  void onTextMessage(const QString& payload);
  void onConnected();
  void onClosed();

  QJsonObject buildExtraHeaders() const;
  QString buildInitScript() const;
  void applyToTarget(const QString& sessionId);
  void maybeReloadOrNavigate(const QString& sessionId);

  QWebSocket m_ws;
  QUrl m_wsUrl;
  Fingerprint m_fp;
  QString m_initialUrl;
  int m_nextId = 1;
  QHash<int, QString> m_pending;
  bool m_readyEmitted = false;
};
