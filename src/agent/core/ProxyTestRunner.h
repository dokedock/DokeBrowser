#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <functional>

class QObject;

namespace ProxyTestRunner {

enum class Scope {
  Profile,
  Pool,
};

struct Request {
  Scope scope = Scope::Profile;
  QString resultType;
  QString profileId;
  QString proxyId;
  QString requestId;
  QString batchId;
  bool enabled = false;
  QString proxyType;
  QString host;
  int port = 0;
  QString username;
  QString password;
  QString url;
};

using SendResult = std::function<void(const QJsonObject&)>;

Request parseRequest(const QJsonObject& obj, Scope scope);
QString validationError(const Request& request);
QJsonObject baseResult(const Request& request);
QStringList candidateUrls(const QString& requestedUrl);
void start(const Request& request, QObject* owner, SendResult sendResult);

} // namespace ProxyTestRunner
