#include "ProxyTestRunner.h"

#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <memory>

namespace ProxyTestRunner {
namespace {
constexpr int kRequestTimeoutMs = 4000;
constexpr int kAbortTimeoutMs = 4500;

bool isPool(const Request& request) {
  return request.scope == Scope::Pool;
}

void populateIdentity(QJsonObject& obj, const Request& request) {
  if (isPool(request)) {
    obj.insert(QStringLiteral("proxy_id"), request.proxyId);
  } else {
    obj.insert(QStringLiteral("profile_id"), request.profileId);
  }
  if (!request.requestId.isEmpty()) {
    obj.insert(QStringLiteral("request_id"), request.requestId);
  }
  if (!request.batchId.isEmpty()) {
    obj.insert(QStringLiteral("batch_id"), request.batchId);
  }
}

QString observedIpFromBody(const QByteArray& body) {
  QJsonParseError parseErr{};
  const QJsonDocument doc = QJsonDocument::fromJson(body, &parseErr);
  if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
    return {};
  }

  QString observedIp = doc.object().value(QStringLiteral("ip")).toString();
  if (observedIp.isEmpty()) {
    observedIp = doc.object().value(QStringLiteral("origin")).toString();
  }
  if (observedIp.contains(',')) {
    observedIp = observedIp.split(',').value(0).trimmed();
  }
  return observedIp;
}
} // namespace

Request parseRequest(const QJsonObject& obj, Scope scope) {
  Request request;
  request.scope = scope;
  request.resultType = scope == Scope::Pool ? QStringLiteral("proxy_pool.test.result") : QStringLiteral("proxy.test.result");
  request.profileId = obj.value(QStringLiteral("profile_id")).toString();
  request.proxyId = obj.value(QStringLiteral("proxy_id")).toString();
  request.requestId = obj.value(QStringLiteral("request_id")).toString();
  request.batchId = obj.value(QStringLiteral("batch_id")).toString();
  request.url = obj.value(QStringLiteral("url")).toString(QStringLiteral("https://httpbin.org/ip"));

  const QJsonObject proxy = obj.value(QStringLiteral("proxy")).toObject();
  request.enabled =
      proxy.value(QStringLiteral("enabled")).toBool(scope == Scope::Pool ? true : false);
  request.proxyType =
      proxy.value(QStringLiteral("type")).toString(scope == Scope::Pool ? QStringLiteral("http") : QStringLiteral("direct")).toLower();
  request.host = proxy.value(QStringLiteral("host")).toString();
  request.port = proxy.value(QStringLiteral("port")).toInt(0);
  request.username = proxy.value(QStringLiteral("username")).toString();
  request.password = proxy.value(QStringLiteral("password")).toString();
  return request;
}

QString validationError(const Request& request) {
  if (isPool(request) && request.proxyId.isEmpty()) {
    return QStringLiteral("missing_proxy_id");
  }
  if (request.enabled && request.proxyType != QStringLiteral("direct") && (request.host.isEmpty() || request.port <= 0)) {
    return QStringLiteral("invalid_proxy_config");
  }
  return {};
}

QJsonObject baseResult(const Request& request) {
  QJsonObject result;
  result.insert(QStringLiteral("type"), request.resultType);
  populateIdentity(result, request);
  return result;
}

QStringList candidateUrls(const QString& requestedUrl) {
  QStringList urls;
  if (!requestedUrl.trimmed().isEmpty()) {
    urls.push_back(requestedUrl.trimmed());
  }
  urls.push_back(QStringLiteral("https://httpbin.org/ip"));
  urls.push_back(QStringLiteral("https://api.ipify.org?format=json"));
  urls.removeDuplicates();
  return urls;
}

void start(const Request& request, QObject* owner, SendResult sendResult) {
  QJsonObject invalidResult = baseResult(request);
  const QString error = validationError(request);
  if (!error.isEmpty()) {
    invalidResult.insert(QStringLiteral("ok"), false);
    invalidResult.insert(QStringLiteral("error"), error);
    sendResult(invalidResult);
    return;
  }

  auto timerPtr = std::make_shared<QElapsedTimer>();
  timerPtr->start();

  auto* nam = new QNetworkAccessManager(owner);
  if (!request.enabled || request.proxyType == QStringLiteral("direct")) {
    nam->setProxy(QNetworkProxy::NoProxy);
  } else {
    QNetworkProxy px;
    if (request.proxyType == QStringLiteral("socks5")) {
      px.setType(QNetworkProxy::Socks5Proxy);
    } else {
      px.setType(QNetworkProxy::HttpProxy);
    }
    px.setHostName(request.host);
    px.setPort(static_cast<quint16>(request.port));
    px.setUser(request.username);
    px.setPassword(request.password);
    nam->setProxy(px);
  }

  const QStringList urls = candidateUrls(request.url);
  auto sent = std::make_shared<bool>(false);
  auto attempt = std::make_shared<int>(0);
  auto lastStatusCode = std::make_shared<int>(0);
  auto lastQtError = std::make_shared<int>(0);
  auto lastError = std::make_shared<QString>();
  auto lastObservedIp = std::make_shared<QString>();

  auto doAttempt = std::make_shared<std::function<void()>>();
  *doAttempt = [owner, nam, urls, timerPtr, sent, attempt, request, sendResult, doAttempt, lastStatusCode, lastQtError,
                lastError, lastObservedIp]() mutable {
    if (*sent) {
      nam->deleteLater();
      return;
    }
    if (*attempt >= urls.size()) {
      const int durationMs = static_cast<int>(timerPtr->elapsed());
      QJsonObject r = baseResult(request);
      r.insert(QStringLiteral("ok"), false);
      r.insert(QStringLiteral("status_code"), *lastStatusCode);
      r.insert(QStringLiteral("duration_ms"), durationMs);
      r.insert(QStringLiteral("qt_error"), *lastQtError);
      r.insert(QStringLiteral("error"), lastError->isEmpty() ? QStringLiteral("all_attempts_failed") : *lastError);
      r.insert(QStringLiteral("observed_ip"), *lastObservedIp);
      sendResult(r);
      *sent = true;
      nam->deleteLater();
      return;
    }

    const QString url = urls.at(*attempt);
    (*attempt)++;

    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("DokeBrowser/0.1"));
    req.setRawHeader("Accept", "application/json");
    req.setTransferTimeout(kRequestTimeoutMs);
    QNetworkReply* reply = nam->get(req);

    auto* timeout = new QTimer(nam);
    timeout->setSingleShot(true);
    timeout->start(kAbortTimeoutMs);

    QObject::connect(timeout, &QTimer::timeout, owner, [reply, timeout, doAttempt, sent]() mutable {
      if (*sent) {
        timeout->deleteLater();
        reply->deleteLater();
        return;
      }
      reply->abort();
      timeout->deleteLater();
      reply->deleteLater();
      (*doAttempt)();
    });

    QObject::connect(reply, &QNetworkReply::finished, owner,
                     [reply, timeout, timerPtr, sent, request, sendResult, doAttempt, lastStatusCode, lastQtError,
                      lastError, lastObservedIp]() mutable {
                       timeout->stop();
                       timeout->deleteLater();

                       if (*sent) {
                         reply->deleteLater();
                         return;
                       }

                       const int durationMs = static_cast<int>(timerPtr->elapsed());
                       const QVariant sc = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
                       const int statusCode = sc.isValid() ? sc.toInt() : 0;
                       const int qtError = static_cast<int>(reply->error());
                       const QString errStr = reply->errorString();
                       const QByteArray body = reply->readAll();
                       const QString observedIp = observedIpFromBody(body);

                       *lastStatusCode = statusCode;
                       *lastQtError = qtError;
                       *lastError = errStr;
                       *lastObservedIp = observedIp;

                       const bool ok = (reply->error() == QNetworkReply::NoError) && (statusCode >= 200 && statusCode < 300)
                                       && !observedIp.isEmpty();
                       if (!ok) {
                         reply->deleteLater();
                         (*doAttempt)();
                         return;
                       }

                       QJsonObject r = baseResult(request);
                       r.insert(QStringLiteral("ok"), true);
                       r.insert(QStringLiteral("status_code"), statusCode);
                       r.insert(QStringLiteral("duration_ms"), durationMs);
                       r.insert(QStringLiteral("qt_error"), qtError);
                       r.insert(QStringLiteral("error"), QString());
                       r.insert(QStringLiteral("observed_ip"), observedIp);
                       sendResult(r);
                       *sent = true;
                       reply->deleteLater();
                     });
  };

  (*doAttempt)();
}

} // namespace ProxyTestRunner
