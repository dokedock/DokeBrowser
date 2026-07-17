#include "agent/core/ProfileLaunchConfig.h"

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

bool testStartRequestParsing() {
  QJsonObject engineOptions;
  engineOptions.insert(QStringLiteral("humanize"), true);
  engineOptions.insert(QStringLiteral("geoip"), true);

  QJsonObject proxy;
  proxy.insert(QStringLiteral("enabled"), true);
  proxy.insert(QStringLiteral("type"), QStringLiteral("socks5"));
  proxy.insert(QStringLiteral("host"), QStringLiteral("127.0.0.1"));
  proxy.insert(QStringLiteral("port"), 1080);

  QJsonObject obj;
  obj.insert(QStringLiteral("profile_id"), QStringLiteral("  p1  "));
  obj.insert(QStringLiteral("profile_name"), QStringLiteral("Profile 1"));
  obj.insert(QStringLiteral("data_dir"), QStringLiteral(" /tmp/doke-profile "));
  obj.insert(QStringLiteral("browser_engine"), QStringLiteral("doke-chromium"));
  obj.insert(QStringLiteral("engine_config_json"), QStringLiteral("{\"extra_args\":[\"--flag\"]}"));
  obj.insert(QStringLiteral("engine_options"), engineOptions);
  obj.insert(QStringLiteral("fingerprint_mode"), QStringLiteral(" random "));
  obj.insert(QStringLiteral("language"), QStringLiteral(" en-US "));
  obj.insert(QStringLiteral("user_agent"), QStringLiteral(" UA "));
  obj.insert(QStringLiteral("platform"), QStringLiteral(" MacIntel "));
  obj.insert(QStringLiteral("hardware_concurrency"), 8);
  obj.insert(QStringLiteral("device_memory_gb"), 16);
  obj.insert(QStringLiteral("device_scale_factor"), 2);
  obj.insert(QStringLiteral("timezone"), QStringLiteral(" Asia/Shanghai "));
  obj.insert(QStringLiteral("resolution"), QStringLiteral(" 1440x900 "));
  obj.insert(QStringLiteral("touch_enabled"), true);
  obj.insert(QStringLiteral("geo_enabled"), true);
  obj.insert(QStringLiteral("geo_latitude"), 31.2304);
  obj.insert(QStringLiteral("geo_longitude"), 121.4737);
  obj.insert(QStringLiteral("geo_accuracy"), 50);
  obj.insert(QStringLiteral("proxy"), proxy);
  obj.insert(QStringLiteral("chrome_compat"), true);

  const ProfileLaunch::StartRequest request = ProfileLaunch::parseStartRequest(obj);
  bool ok = true;
  ok &= expect(request.profileId == QStringLiteral("p1"), "profile id should be trimmed");
  ok &= expect(request.browserEngine == QStringLiteral("doke_chromium"), "engine alias should normalize");
  ok &= expect(request.engineHumanizeEnabled, "humanize engine option should parse");
  ok &= expect(request.engineGeoipEnabled, "geoip engine option should parse");
  ok &= expect(request.fingerprintMode == QStringLiteral("random"), "fingerprint mode should be trimmed");
  ok &= expect(request.language == QStringLiteral("en-US"), "language should be trimmed");
  ok &= expect(request.userAgent == QStringLiteral("UA"), "user agent should be trimmed");
  ok &= expect(request.platform == QStringLiteral("MacIntel"), "platform should be trimmed");
  ok &= expect(request.hardwareConcurrency == 8, "hardware concurrency should parse");
  ok &= expect(request.deviceMemoryGb == 16, "device memory should parse");
  ok &= expect(request.deviceScaleFactor == 2, "device scale factor should parse");
  ok &= expect(request.timezone == QStringLiteral("Asia/Shanghai"), "timezone should be trimmed");
  ok &= expect(request.resolution == QStringLiteral("1440x900"), "resolution should be trimmed");
  ok &= expect(request.touchEnabled, "touch flag should parse");
  ok &= expect(request.geoEnabled, "geo flag should parse");
  ok &= expect(request.chromeCompatRequested && request.chromeCompat, "chrome compat should parse");
  ok &= expect(request.proxy.value(QStringLiteral("host")).toString() == QStringLiteral("127.0.0.1"),
               "proxy object should be preserved");
  return ok;
}

bool testProxyConfig() {
  QJsonObject direct;
  direct.insert(QStringLiteral("enabled"), true);
  direct.insert(QStringLiteral("type"), QStringLiteral("direct"));
  bool ok = expect(ProfileLaunch::buildProxyConfig(direct, nullptr).argument.isEmpty(), "direct proxy should produce no arg");

  QJsonObject http;
  http.insert(QStringLiteral("enabled"), true);
  http.insert(QStringLiteral("type"), QStringLiteral("http"));
  http.insert(QStringLiteral("host"), QStringLiteral("proxy.local"));
  http.insert(QStringLiteral("port"), 8080);
  const ProfileLaunch::ProxyConfig httpConfig = ProfileLaunch::buildProxyConfig(http, nullptr);
  ok &= expect(httpConfig.argument == QStringLiteral("--proxy-server=http://proxy.local:8080"),
               "http proxy arg should be generated");
  ok &= expect(httpConfig.scheme == QStringLiteral("http"), "http proxy scheme should be preserved");
  ok &= expect(!httpConfig.enableProxyAuth, "http proxy without credentials should not require auth extension");

  QJsonObject socks;
  socks.insert(QStringLiteral("enabled"), true);
  socks.insert(QStringLiteral("type"), QStringLiteral("socks5"));
  socks.insert(QStringLiteral("host"), QStringLiteral("127.0.0.1"));
  socks.insert(QStringLiteral("port"), 1080);
  socks.insert(QStringLiteral("username"), QStringLiteral("user name"));
  socks.insert(QStringLiteral("password"), QStringLiteral("p@ss"));
  const ProfileLaunch::ProxyConfig socksConfig = ProfileLaunch::buildProxyConfig(socks, nullptr);
  ok &= expect(socksConfig.argument == QStringLiteral("--proxy-server=socks5://user%20name:p%40ss@127.0.0.1:1080"),
               "socks5 proxy credentials should be percent encoded inline");
  ok &= expect(!socksConfig.enableProxyAuth, "socks5 inline auth should not require auth extension");
  return ok;
}

bool testWindowSize() {
  bool ok = true;
  ok &= expect(ProfileLaunch::windowSizeArgForResolution(QStringLiteral("1440x900"))
                   == QStringLiteral("--window-size=1440,900"),
               "valid resolution should produce window-size arg");
  ok &= expect(ProfileLaunch::windowSizeArgForResolution(QStringLiteral("bad")).isEmpty(),
               "invalid resolution should produce no window-size arg");
  ok &= expect(ProfileLaunch::windowSizeArgForResolution(QStringLiteral("0x900")).isEmpty(),
               "zero width should produce no window-size arg");
  return ok;
}
} // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  bool ok = true;
  ok &= testStartRequestParsing();
  ok &= testProxyConfig();
  ok &= testWindowSize();
  if (!ok) {
    return 1;
  }

  qInfo("profile_launch_config_ok");
  return 0;
}
