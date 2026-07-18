#include "agent/core/ProfileRuntimeManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>
#include <QtGlobal>

namespace {
struct CapturedStatus {
  QString profileId;
  QString status;
  QString error;
  int debugPort = 0;
};

bool expect(bool condition, const char* message) {
  if (!condition) {
    qCritical("%s", message);
    return false;
  }
  return true;
}

QString writeFakeDokeExecutable(QTemporaryDir& tempDir) {
  const QString path = QDir(tempDir.path()).filePath(QStringLiteral("doke_chromium"));
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return {};
  }
  file.write("#!/bin/sh\n"
             "if [ \"$1\" = \"--doke-probe\" ]; then printf '%s\\n' '{\"probe_protocol\":1,\"version\":\"Doke Chromium runtime-test\",\"capabilities\":[\"native_proxy\"]}'; exit 0; fi\n"
             "if [ \"$1\" = \"--version\" ]; then echo \"Doke Chromium runtime-test\"; exit 0; fi\n"
             "sleep 5\n"
             "exit 0\n");
  file.close();
  QFile::setPermissions(path,
                        QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadGroup | QFile::ExeGroup);
  return path;
}

bool testMissingProfileId() {
  ProfileRuntimeManager manager;
  QVector<CapturedStatus> statuses;
  QStringList logs;

  QJsonObject obj;
  obj.insert(QStringLiteral("type"), QStringLiteral("profile.stop"));

  manager.handleMessage(
      obj,
      [&statuses](const QString& profileId, const QString& status, const QString& error, int debugPort) {
        statuses.push_back({profileId, status, error, debugPort});
      },
      [&logs](const QString& message) {
        logs.push_back(message);
      });

  bool ok = true;
  ok &= expect(statuses.size() == 1, "missing profile id should emit one status");
  ok &= expect(statuses.first().status == QStringLiteral("error"), "missing profile id should emit error status");
  ok &= expect(statuses.first().error == QStringLiteral("missing_profile_id"), "missing profile id error should match");
  ok &= expect(logs.isEmpty(), "missing profile id should not log runtime line");
  return ok;
}

bool testUnsupportedEngine() {
  ProfileRuntimeManager manager;
  QVector<CapturedStatus> statuses;
  QStringList logs;

  QJsonObject obj;
  obj.insert(QStringLiteral("type"), QStringLiteral("profile.start"));
  obj.insert(QStringLiteral("profile_id"), QStringLiteral("p1"));
  obj.insert(QStringLiteral("profile_name"), QStringLiteral("Profile 1"));
  obj.insert(QStringLiteral("browser_engine"), QStringLiteral("unknown_engine"));

  manager.handleMessage(
      obj,
      [&statuses](const QString& profileId, const QString& status, const QString& error, int debugPort) {
        statuses.push_back({profileId, status, error, debugPort});
      },
      [&logs](const QString& message) {
        logs.push_back(message);
      });

  bool ok = true;
  ok &= expect(statuses.size() == 2, "unsupported engine should emit starting then error");
  ok &= expect(statuses.at(0).profileId == QStringLiteral("p1"), "profile id should be forwarded");
  ok &= expect(statuses.at(0).status == QStringLiteral("starting"), "first status should be starting");
  ok &= expect(statuses.at(1).status == QStringLiteral("error"), "second status should be error");
  ok &= expect(statuses.at(1).error == QStringLiteral("unsupported_browser_engine:unknown_engine"),
               "unsupported engine error should include engine id");
  ok &= expect(logs.size() == 1 && logs.first().contains(QStringLiteral("profile.start engine=unknown_engine")),
               "runtime should log profile start line");
  return ok;
}

bool testDokeInvalidExecutable() {
  ProfileRuntimeManager manager;
  QVector<CapturedStatus> statuses;
  QStringList logs;

  QJsonObject obj;
  obj.insert(QStringLiteral("type"), QStringLiteral("profile.start"));
  obj.insert(QStringLiteral("profile_id"), QStringLiteral("p2"));
  obj.insert(QStringLiteral("profile_name"), QStringLiteral("Profile 2"));
  obj.insert(QStringLiteral("browser_engine"), QStringLiteral("doke_chromium"));
  obj.insert(QStringLiteral("engine_config_json"),
             QStringLiteral("{\"executable\":\"/tmp/dokebrowser-missing-doke-chromium\"}"));

  manager.handleMessage(
      obj,
      [&statuses](const QString& profileId, const QString& status, const QString& error, int debugPort) {
        statuses.push_back({profileId, status, error, debugPort});
      },
      [&logs](const QString& message) {
        logs.push_back(message);
      });

  bool ok = true;
  ok &= expect(statuses.size() == 2, "invalid doke path should emit starting then error");
  ok &= expect(statuses.at(0).profileId == QStringLiteral("p2"), "doke profile id should be forwarded");
  ok &= expect(statuses.at(0).status == QStringLiteral("starting"), "invalid doke first status should be starting");
  ok &= expect(statuses.at(1).status == QStringLiteral("error"), "invalid doke second status should be error");
  ok &= expect(statuses.at(1).error == QStringLiteral("doke_chromium_path_missing"),
               "invalid doke path should return precise missing-path error");
  ok &= expect(logs.size() == 1 && logs.first().contains(QStringLiteral("profile.start engine=doke_chromium")),
               "invalid doke runtime should log profile start line");
  return ok;
}

bool testDokeMissingNativeCapabilitiesFallsBack() {
  QTemporaryDir tempDir;
  const QString executable = writeFakeDokeExecutable(tempDir);
  if (!tempDir.isValid() || executable.isEmpty()) {
    qCritical("failed to create fake doke executable");
    return false;
  }

  ProfileRuntimeManager manager;
  QVector<CapturedStatus> statuses;
  QStringList logs;

  QJsonObject features;
  features.insert(QStringLiteral("native_fingerprint"), true);
  features.insert(QStringLiteral("native_proxy"), true);
  features.insert(QStringLiteral("native_geoip"), true);
  features.insert(QStringLiteral("native_humanize"), true);
  QJsonObject config;
  config.insert(QStringLiteral("executable"), executable);
  config.insert(QStringLiteral("features"), features);

  QJsonObject obj;
  obj.insert(QStringLiteral("type"), QStringLiteral("profile.start"));
  obj.insert(QStringLiteral("profile_id"), QStringLiteral("p3"));
  obj.insert(QStringLiteral("profile_name"), QStringLiteral("Profile 3"));
  obj.insert(QStringLiteral("browser_engine"), QStringLiteral("doke_chromium"));
  obj.insert(QStringLiteral("engine_config_json"), QString::fromUtf8(QJsonDocument(config).toJson(QJsonDocument::Compact)));
  obj.insert(QStringLiteral("data_dir"), QDir(tempDir.path()).filePath(QStringLiteral("profile-data")));
  obj.insert(QStringLiteral("language"), QStringLiteral("en-US"));
  obj.insert(QStringLiteral("user_agent"),
             QStringLiteral("Mozilla/5.0 (Macintosh; Intel Mac OS X 13_6_0) AppleWebKit/537.36 "
                            "(KHTML, like Gecko) Chrome/120.0.6099.109 Safari/537.36"));
  obj.insert(QStringLiteral("platform"), QStringLiteral("MacIntel"));
  obj.insert(QStringLiteral("hardware_concurrency"), 8);
  obj.insert(QStringLiteral("device_memory_gb"), 16);
  obj.insert(QStringLiteral("device_scale_factor"), 2);
  obj.insert(QStringLiteral("timezone"), QStringLiteral("Asia/Tokyo"));
  obj.insert(QStringLiteral("resolution"), QStringLiteral("1440x900"));
  obj.insert(QStringLiteral("touch_enabled"), true);
  obj.insert(QStringLiteral("geo_enabled"), true);
  obj.insert(QStringLiteral("geo_latitude"), 35.0);
  obj.insert(QStringLiteral("geo_longitude"), 139.0);
  obj.insert(QStringLiteral("geo_accuracy"), 100);
  QJsonObject proxy;
  proxy.insert(QStringLiteral("enabled"), true);
  proxy.insert(QStringLiteral("type"), QStringLiteral("http"));
  proxy.insert(QStringLiteral("host"), QStringLiteral("127.0.0.1"));
  proxy.insert(QStringLiteral("port"), 18080);
  obj.insert(QStringLiteral("proxy"), proxy);

  manager.handleMessage(
      obj,
      [&statuses](const QString& profileId, const QString& status, const QString& error, int debugPort) {
        statuses.push_back({profileId, status, error, debugPort});
      },
      [&logs](const QString& message) {
        logs.push_back(message);
      });

  QEventLoop startedLoop;
  QTimer::singleShot(50, &startedLoop, &QEventLoop::quit);
  startedLoop.exec();

  manager.handleMessage(
      obj,
      [&statuses](const QString& profileId, const QString& status, const QString& error, int debugPort) {
        statuses.push_back({profileId, status, error, debugPort});
      },
      [&logs](const QString& message) {
        logs.push_back(message);
      });

  QString runtimeConfigPath;
  for (const auto& line : logs) {
    const QString marker = QStringLiteral("runtime_config=");
    const int idx = line.indexOf(marker);
    if (idx >= 0) {
      runtimeConfigPath = line.mid(idx + marker.size()).trimmed();
    }
  }

  QJsonObject runtimeConfig;
  if (!runtimeConfigPath.isEmpty()) {
    QFile runtimeFile(runtimeConfigPath);
    if (runtimeFile.open(QIODevice::ReadOnly)) {
      runtimeConfig = QJsonDocument::fromJson(runtimeFile.readAll()).object();
    }
  }

  QJsonObject stopObj;
  stopObj.insert(QStringLiteral("type"), QStringLiteral("profile.stop"));
  stopObj.insert(QStringLiteral("profile_id"), QStringLiteral("p3"));
  stopObj.insert(QStringLiteral("profile_name"), QStringLiteral("Profile 3"));
  manager.handleMessage(
      stopObj,
      [&statuses](const QString& profileId, const QString& status, const QString& error, int debugPort) {
        statuses.push_back({profileId, status, error, debugPort});
      },
      [&logs](const QString& message) {
        logs.push_back(message);
      });

  QEventLoop loop;
  QTimer::singleShot(200, &loop, &QEventLoop::quit);
  loop.exec();

  bool sawNativeFallbackLog = false;
  bool sawDebugPortAllocationFailure = false;
  for (const auto& line : logs) {
    sawNativeFallbackLog =
        sawNativeFallbackLog
        || (line.contains(QStringLiteral("using agent fallback"))
            && (line.contains(QStringLiteral("missing_native_capabilities"))
                || line.contains(QStringLiteral("native_probe_unavailable"))));
    sawDebugPortAllocationFailure =
        sawDebugPortAllocationFailure || line.contains(QStringLiteral("debug_port_allocation_failed"));
  }

  bool ok = true;
  const QJsonObject nativeRuntime = runtimeConfig.value(QStringLiteral("native")).toObject();
  const QJsonObject fallbackRuntime = runtimeConfig.value(QStringLiteral("fallback")).toObject();
  const QJsonObject fingerprintRuntime = runtimeConfig.value(QStringLiteral("fingerprint")).toObject();
  const QJsonObject uaClientHintsRuntime = fingerprintRuntime.value(QStringLiteral("ua_client_hints")).toObject();
  const QJsonObject webrtcRuntime = runtimeConfig.value(QStringLiteral("webrtc")).toObject();
  const QJsonObject renderingRuntime = runtimeConfig.value(QStringLiteral("rendering")).toObject();
  const QJsonObject canvasRuntime = renderingRuntime.value(QStringLiteral("canvas")).toObject();
  const QJsonObject webglRuntime = renderingRuntime.value(QStringLiteral("webgl")).toObject();
  const QJsonObject audioRuntime = renderingRuntime.value(QStringLiteral("audio")).toObject();
  const QJsonObject surfacesRuntime = runtimeConfig.value(QStringLiteral("surfaces")).toObject();
  const QJsonObject pluginsRuntime = surfacesRuntime.value(QStringLiteral("plugins")).toObject();
  const QJsonObject mimeTypesRuntime = surfacesRuntime.value(QStringLiteral("mime_types")).toObject();
  const QJsonObject fontsRuntime = surfacesRuntime.value(QStringLiteral("fonts")).toObject();
  const QJsonObject clientRectsRuntime = surfacesRuntime.value(QStringLiteral("client_rects")).toObject();
  const QJsonObject alignmentRuntime = runtimeConfig.value(QStringLiteral("alignment")).toObject();
  const QJsonObject alignmentGeoRuntime = alignmentRuntime.value(QStringLiteral("geo")).toObject();
  const QJsonObject alignmentProxyRuntime = alignmentRuntime.value(QStringLiteral("proxy")).toObject();
  const QJsonObject automationRuntime = runtimeConfig.value(QStringLiteral("automation")).toObject();
  const bool nativeProbeAvailable = !nativeRuntime.value(QStringLiteral("supported")).toArray().isEmpty();
  ok &= expect(statuses.size() >= 1, "doke fallback test should emit at least starting status");
  ok &= expect(statuses.first().status == QStringLiteral("starting"), "doke fallback first status should be starting");
  bool sawRunningDebugPort = false;
  for (const auto& item : statuses) {
    sawRunningDebugPort = sawRunningDebugPort || (item.status == QStringLiteral("running") && item.debugPort > 0);
  }
  if (!sawRunningDebugPort && !sawDebugPortAllocationFailure) {
    for (const auto& item : statuses) {
      qCritical("status profile=%s status=%s error=%s debug_port=%d",
                qPrintable(item.profileId),
                qPrintable(item.status),
                qPrintable(item.error),
                item.debugPort);
    }
  }
  ok &= expect(sawRunningDebugPort || sawDebugPortAllocationFailure,
               "running profile status should expose debug_port or log allocation failure");
  ok &= expect(sawNativeFallbackLog, "doke fallback should log native fallback reason");
  ok &= expect(!runtimeConfigPath.isEmpty(), "doke fallback should log runtime config path");
  ok &= expect(runtimeConfig.value(QStringLiteral("schema")).toString() == QStringLiteral("doke_profile_runtime.v1"),
               "runtime config schema should match");
  if (nativeProbeAvailable) {
    ok &= expect(!nativeRuntime.value(QStringLiteral("fingerprint")).toBool(true),
                 "runtime config should keep unsupported native fingerprint disabled");
    ok &= expect(nativeRuntime.value(QStringLiteral("proxy")).toBool(false),
                 "runtime config should mark supported native proxy");
    ok &= expect(!nativeRuntime.value(QStringLiteral("geoip")).toBool(true),
                 "runtime config should keep unsupported native geoip disabled");
  }
  ok &= expect(fallbackRuntime.value(QStringLiteral("fingerprint")).toBool(false),
               "runtime config should keep fingerprint fallback enabled when native fingerprint is missing");
  ok &= expect(fallbackRuntime.value(QStringLiteral("geoip")).toBool(false),
               "runtime config should keep geoip fallback enabled when native geoip is missing");
  ok &= expect(!uaClientHintsRuntime.isEmpty(), "runtime config should include UA client hints");
  ok &= expect(uaClientHintsRuntime.value(QStringLiteral("platform")).toString() == QStringLiteral("macOS"),
               "runtime config UA client hints should include platform");
  ok &= expect(uaClientHintsRuntime.value(QStringLiteral("brands")).toArray().size() >= 2,
               "runtime config UA client hints should include brands");
  ok &= expect(fingerprintRuntime.value(QStringLiteral("window_size")).toString() == QStringLiteral("1440,900"),
               "runtime config should include Chrome window-size value");
  ok &= expect(fingerprintRuntime.value(QStringLiteral("device_scale_factor_arg")).toString() == QStringLiteral("2"),
               "runtime config should include device scale factor arg");
  ok &= expect(fingerprintRuntime.value(QStringLiteral("hardware_concurrency_arg")).toString() == QStringLiteral("8"),
               "runtime config should include hardware concurrency arg");
  ok &= expect(fingerprintRuntime.value(QStringLiteral("device_memory_gb_arg")).toString() == QStringLiteral("16"),
               "runtime config should include device memory arg");
  ok &= expect(fingerprintRuntime.value(QStringLiteral("touch_events")).toString() == QStringLiteral("enabled"),
               "runtime config should include touch-events arg");
  ok &= expect(webrtcRuntime.value(QStringLiteral("ip_handling_policy")).toString()
                   == QStringLiteral("disable_non_proxied_udp"),
               "runtime config should include WebRTC IP handling policy");
  ok &= expect(canvasRuntime.value(QStringLiteral("strategy")).toString() == QStringLiteral("stable_noise"),
               "runtime config should include canvas stable noise strategy");
  ok &= expect(webglRuntime.value(QStringLiteral("strategy")).toString() == QStringLiteral("stable_noise"),
               "runtime config should include WebGL stable noise strategy");
  ok &= expect(audioRuntime.value(QStringLiteral("strategy")).toString() == QStringLiteral("stable_noise"),
               "runtime config should include audio stable noise strategy");
  ok &= expect(!canvasRuntime.value(QStringLiteral("seed")).toString().isEmpty(),
               "runtime config should include canvas noise seed");
  ok &= expect(canvasRuntime.value(QStringLiteral("seed")).toString()
                   != webglRuntime.value(QStringLiteral("seed")).toString(),
               "runtime config should use distinct rendering seeds");
  ok &= expect(pluginsRuntime.value(QStringLiteral("preset")).toString() == QStringLiteral("macos"),
               "runtime config should include plugin platform preset");
  ok &= expect(mimeTypesRuntime.value(QStringLiteral("preset")).toString() == QStringLiteral("macos"),
               "runtime config should include MIME type platform preset");
  ok &= expect(fontsRuntime.value(QStringLiteral("strategy")).toString() == QStringLiteral("platform_preset"),
               "runtime config should include font platform preset strategy");
  ok &= expect(!fontsRuntime.value(QStringLiteral("seed")).toString().isEmpty(),
               "runtime config should include font seed");
  ok &= expect(clientRectsRuntime.value(QStringLiteral("strategy")).toString() == QStringLiteral("platform_preset"),
               "runtime config should include client rect platform preset strategy");
  ok &= expect(!clientRectsRuntime.value(QStringLiteral("seed")).toString().isEmpty(),
               "runtime config should include client rect seed");
  ok &= expect(fontsRuntime.value(QStringLiteral("seed")).toString()
                   != clientRectsRuntime.value(QStringLiteral("seed")).toString(),
               "runtime config should use distinct surface seeds");
  ok &= expect(alignmentRuntime.value(QStringLiteral("language")).toString() == QStringLiteral("en-US"),
               "runtime config should include alignment language");
  ok &= expect(alignmentRuntime.value(QStringLiteral("timezone")).toString() == QStringLiteral("Asia/Tokyo"),
               "runtime config should include alignment timezone");
  ok &= expect(alignmentGeoRuntime.value(QStringLiteral("latitude_arg")).toString() == QStringLiteral("35.000000"),
               "runtime config should include alignment latitude arg");
  ok &= expect(alignmentGeoRuntime.value(QStringLiteral("longitude_arg")).toString() == QStringLiteral("139.000000"),
               "runtime config should include alignment longitude arg");
  ok &= expect(alignmentGeoRuntime.value(QStringLiteral("accuracy_arg")).toString() == QStringLiteral("100"),
               "runtime config should include alignment accuracy arg");
  ok &= expect(alignmentProxyRuntime.value(QStringLiteral("enabled")).toBool(false),
               "runtime config should include enabled alignment proxy");
  ok &= expect(alignmentProxyRuntime.value(QStringLiteral("scheme")).toString() == QStringLiteral("http"),
               "runtime config should include alignment proxy scheme");
  ok &= expect(alignmentProxyRuntime.value(QStringLiteral("host")).toString() == QStringLiteral("127.0.0.1"),
               "runtime config should include alignment proxy host");
  ok &= expect(alignmentProxyRuntime.value(QStringLiteral("port_arg")).toString() == QStringLiteral("18080"),
               "runtime config should include alignment proxy port arg");
  ok &= expect(automationRuntime.value(QStringLiteral("webdriver_policy")).toString() == QStringLiteral("hide"),
               "runtime config should include webdriver policy");
  ok &= expect(automationRuntime.value(QStringLiteral("devtools_exposure")).toString()
                   == QStringLiteral("fallback_required"),
               "runtime config should mark fallback-required DevTools exposure");
  ok &= expect(automationRuntime.value(QStringLiteral("cdp_side_effect_guard")).toBool(false),
               "runtime config should enable CDP side-effect guard");
  ok &= expect(automationRuntime.value(QStringLiteral("debug_port_required")).toBool(false),
               "runtime config should mark debug port required when fallback CDP is enabled");
  ok &= expect(!automationRuntime.value(QStringLiteral("startup_automation_controlled")).toBool(true),
               "runtime config should disable startup automation controlled flag");
  return ok;
}
} // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  bool ok = true;
  ok &= testMissingProfileId();
  ok &= testUnsupportedEngine();
  ok &= testDokeInvalidExecutable();
  ok &= testDokeMissingNativeCapabilitiesFallsBack();
  if (!ok) {
    return 1;
  }

  qInfo("profile_runtime_manager_ok");
  return 0;
}
