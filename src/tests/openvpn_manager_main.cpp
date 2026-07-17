#include "agent/core/OpenVpnManager.h"

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

QJsonObject startRequestObject() {
  QJsonObject socks;
  socks.insert(QStringLiteral("enabled"), true);
  socks.insert(QStringLiteral("host"), QStringLiteral("127.0.0.1"));
  socks.insert(QStringLiteral("port"), 1080);
  socks.insert(QStringLiteral("username"), QStringLiteral("u"));
  socks.insert(QStringLiteral("password"), QStringLiteral("p"));

  QJsonObject obj;
  obj.insert(QStringLiteral("type"), QStringLiteral("vpn.openvpn.start"));
  obj.insert(QStringLiteral("profile_id"), QStringLiteral("profile-1"));
  obj.insert(QStringLiteral("exe"), QStringLiteral("/usr/local/bin/openvpn"));
  obj.insert(QStringLiteral("config"), QStringLiteral("/tmp/profile.ovpn"));
  obj.insert(QStringLiteral("socks"), socks);
  return obj;
}

bool testParseStartRequest() {
  const OpenVpnManager::StartRequest request = OpenVpnManager::parseStartRequest(startRequestObject());
  bool ok = true;
  ok &= expect(request.profileId == QStringLiteral("profile-1"), "profile id should parse");
  ok &= expect(request.exe == QStringLiteral("/usr/local/bin/openvpn"), "exe should parse");
  ok &= expect(request.config == QStringLiteral("/tmp/profile.ovpn"), "config should parse");
  ok &= expect(request.socksEnabled, "socks enabled should parse");
  ok &= expect(request.socksHost == QStringLiteral("127.0.0.1"), "socks host should parse");
  ok &= expect(request.socksPort == 1080, "socks port should parse");
  ok &= expect(request.socksUser == QStringLiteral("u"), "socks username should parse");
  ok &= expect(request.socksPass == QStringLiteral("p"), "socks password should parse");
  ok &= expect(OpenVpnManager::validationError(request).isEmpty(), "valid request should pass validation");
  return ok;
}

bool testValidation() {
  OpenVpnManager::StartRequest missingProfile;
  missingProfile.config = QStringLiteral("/tmp/profile.ovpn");

  OpenVpnManager::StartRequest missingConfig;
  missingConfig.profileId = QStringLiteral("profile-1");

  OpenVpnManager::StartRequest invalidSocks;
  invalidSocks.profileId = QStringLiteral("profile-1");
  invalidSocks.config = QStringLiteral("/tmp/profile.ovpn");
  invalidSocks.socksEnabled = true;
  invalidSocks.socksPort = 0;

  bool ok = true;
  ok &= expect(OpenVpnManager::validationError(missingProfile) == QStringLiteral("missing_profile_id"),
               "profile id should be required");
  ok &= expect(OpenVpnManager::validationError(missingConfig) == QStringLiteral("missing_config"),
               "config should be required");
  ok &= expect(OpenVpnManager::validationError(invalidSocks) == QStringLiteral("invalid_socks_proxy"),
               "enabled socks should require host and port");
  return ok;
}

bool testBuildArguments() {
  const OpenVpnManager::StartRequest request = OpenVpnManager::parseStartRequest(startRequestObject());
  const QStringList args = OpenVpnManager::buildArguments(request, QStringLiteral("/tmp/auth.txt"));
  const QStringList expected = {
      QStringLiteral("--config"),
      QStringLiteral("/tmp/profile.ovpn"),
      QStringLiteral("--socks-proxy"),
      QStringLiteral("127.0.0.1"),
      QStringLiteral("1080"),
      QStringLiteral("/tmp/auth.txt"),
  };

  OpenVpnManager::StartRequest noSocks;
  noSocks.profileId = QStringLiteral("profile-1");
  noSocks.config = QStringLiteral("/tmp/profile.ovpn");
  const QStringList noSocksArgs = OpenVpnManager::buildArguments(noSocks, QString());

  bool ok = true;
  ok &= expect(args == expected, "openvpn socks args should match expected order");
  ok &= expect(noSocksArgs == QStringList({QStringLiteral("--config"), QStringLiteral("/tmp/profile.ovpn")}),
               "openvpn args without socks should only include config");
  return ok;
}
} // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  bool ok = true;
  ok &= testParseStartRequest();
  ok &= testValidation();
  ok &= testBuildArguments();
  if (!ok) {
    return 1;
  }

  qInfo("openvpn_manager_ok");
  return 0;
}
