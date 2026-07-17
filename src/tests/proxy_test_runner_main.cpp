#include "agent/core/ProxyTestRunner.h"

#include <QCoreApplication>
#include <QJsonObject>
#include <QtGlobal>

namespace {
bool expect(bool condition, const char* message) {
  if (!condition) {
    qCritical("%s", message);
    return false;
  }
  return true;
}

QJsonObject proxyObject(bool enabled, const QString& type, const QString& host, int port) {
  QJsonObject proxy;
  proxy.insert(QStringLiteral("enabled"), enabled);
  proxy.insert(QStringLiteral("type"), type);
  proxy.insert(QStringLiteral("host"), host);
  proxy.insert(QStringLiteral("port"), port);
  proxy.insert(QStringLiteral("username"), QStringLiteral("u"));
  proxy.insert(QStringLiteral("password"), QStringLiteral("p"));
  return proxy;
}

bool testProfileRequest() {
  QJsonObject obj;
  obj.insert(QStringLiteral("type"), QStringLiteral("proxy.test"));
  obj.insert(QStringLiteral("profile_id"), QStringLiteral("p1"));
  obj.insert(QStringLiteral("request_id"), QStringLiteral("r1"));
  obj.insert(QStringLiteral("batch_id"), QStringLiteral("b1"));
  obj.insert(QStringLiteral("url"), QStringLiteral(" https://example.test/ip "));
  obj.insert(QStringLiteral("proxy"), proxyObject(true, QStringLiteral("SOCKS5"), QStringLiteral("127.0.0.1"), 1080));

  const ProxyTestRunner::Request request = ProxyTestRunner::parseRequest(obj, ProxyTestRunner::Scope::Profile);
  bool ok = true;
  ok &= expect(request.resultType == QStringLiteral("proxy.test.result"), "profile result type should parse");
  ok &= expect(request.profileId == QStringLiteral("p1"), "profile id should parse");
  ok &= expect(request.requestId == QStringLiteral("r1"), "request id should parse");
  ok &= expect(request.batchId == QStringLiteral("b1"), "batch id should parse");
  ok &= expect(request.enabled, "profile enabled proxy should parse");
  ok &= expect(request.proxyType == QStringLiteral("socks5"), "proxy type should lowercase");
  ok &= expect(request.host == QStringLiteral("127.0.0.1"), "proxy host should parse");
  ok &= expect(request.port == 1080, "proxy port should parse");
  ok &= expect(ProxyTestRunner::validationError(request).isEmpty(), "valid profile proxy should pass validation");

  const QJsonObject base = ProxyTestRunner::baseResult(request);
  ok &= expect(base.value(QStringLiteral("type")).toString() == QStringLiteral("proxy.test.result"),
               "base profile result type should match");
  ok &= expect(base.value(QStringLiteral("profile_id")).toString() == QStringLiteral("p1"),
               "base profile result should include profile id");
  ok &= expect(base.value(QStringLiteral("request_id")).toString() == QStringLiteral("r1"),
               "base profile result should include request id");
  return ok;
}

bool testPoolRequestValidation() {
  QJsonObject missingId;
  missingId.insert(QStringLiteral("proxy"), proxyObject(true, QStringLiteral("http"), QStringLiteral("proxy.local"), 8080));
  const ProxyTestRunner::Request noId = ProxyTestRunner::parseRequest(missingId, ProxyTestRunner::Scope::Pool);

  bool ok = true;
  ok &= expect(noId.resultType == QStringLiteral("proxy_pool.test.result"), "pool result type should parse");
  ok &= expect(noId.enabled, "pool proxy should default enabled");
  ok &= expect(ProxyTestRunner::validationError(noId) == QStringLiteral("missing_proxy_id"),
               "pool request should require proxy id");

  QJsonObject invalidProxy;
  invalidProxy.insert(QStringLiteral("proxy_id"), QStringLiteral("px1"));
  invalidProxy.insert(QStringLiteral("proxy"), proxyObject(true, QStringLiteral("http"), QString(), 0));
  const ProxyTestRunner::Request invalid = ProxyTestRunner::parseRequest(invalidProxy, ProxyTestRunner::Scope::Pool);
  ok &= expect(ProxyTestRunner::validationError(invalid) == QStringLiteral("invalid_proxy_config"),
               "enabled non-direct proxy should require host and port");

  const QJsonObject base = ProxyTestRunner::baseResult(invalid);
  ok &= expect(base.value(QStringLiteral("type")).toString() == QStringLiteral("proxy_pool.test.result"),
               "base pool result type should match");
  ok &= expect(base.value(QStringLiteral("proxy_id")).toString() == QStringLiteral("px1"),
               "base pool result should include proxy id");
  return ok;
}

bool testCandidateUrls() {
  const QStringList urls = ProxyTestRunner::candidateUrls(QStringLiteral(" https://api.ipify.org?format=json "));
  bool ok = true;
  ok &= expect(urls.size() == 2, "duplicate fallback URL should be removed");
  ok &= expect(urls.first() == QStringLiteral("https://api.ipify.org?format=json"),
               "requested URL should be trimmed and stay first");

  const QStringList fallbackOnly = ProxyTestRunner::candidateUrls(QString());
  ok &= expect(fallbackOnly.size() == 2, "empty requested URL should use two fallback URLs");
  ok &= expect(fallbackOnly.first() == QStringLiteral("https://httpbin.org/ip"),
               "httpbin should be first fallback");
  return ok;
}
} // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  bool ok = true;
  ok &= testProfileRequest();
  ok &= testPoolRequestValidation();
  ok &= testCandidateUrls();
  if (!ok) {
    return 1;
  }

  qInfo("proxy_test_runner_ok");
  return 0;
}
