#include "ProfileRuntimeManager.h"

#include "BrowserEngineFactory.h"
#include "CdpClient.h"
#include "DokeChromiumEngine.h"
#include "FingerprintMetadata.h"
#include "HttpProxyMapper.h"
#include "ProfileLaunchConfig.h"
#include "SystemChromeEngine.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTimer>

namespace {
QJsonArray jsonArrayFromStringList(const QStringList& values) {
  QJsonArray out;
  for (const auto& value : values) {
    if (!value.isEmpty()) {
      out.push_back(value);
    }
  }
  return out;
}

QString hexSeed(quint32 seed) {
  return QString::number(seed, 16).rightJustified(8, QLatin1Char('0'));
}

QJsonObject renderingNoiseSection(quint32 profileSeed, quint32 salt) {
  QJsonObject out;
  out.insert(QStringLiteral("enabled"), true);
  out.insert(QStringLiteral("strategy"), QStringLiteral("stable_noise"));
  out.insert(QStringLiteral("seed"), hexSeed(profileSeed ^ salt));
  return out;
}

QString surfacePresetForPlatform(const QString& platform) {
  const QString p = platform.trimmed().toLower();
  if (p.contains(QStringLiteral("mac"))) {
    return QStringLiteral("macos");
  }
  if (p.contains(QStringLiteral("win"))) {
    return QStringLiteral("windows");
  }
  if (p.contains(QStringLiteral("android"))) {
    return QStringLiteral("android");
  }
  if (p.contains(QStringLiteral("iphone")) || p.contains(QStringLiteral("ipad"))) {
    return QStringLiteral("ios");
  }
  return QStringLiteral("linux");
}

QJsonObject presetSurfaceSection(const QString& preset) {
  QJsonObject out;
  out.insert(QStringLiteral("enabled"), true);
  out.insert(QStringLiteral("strategy"), QStringLiteral("platform_preset"));
  out.insert(QStringLiteral("preset"), preset);
  return out;
}

QJsonObject seededPresetSurfaceSection(const QString& preset, quint32 profileSeed, quint32 salt) {
  QJsonObject out = presetSurfaceSection(preset);
  out.insert(QStringLiteral("seed"), hexSeed(profileSeed ^ salt));
  return out;
}

QString writeDokeRuntimeConfig(const QString& userDataDir, const QJsonObject& root, QString* error) {
  const QString dokeDir = QDir(userDataDir).filePath(QStringLiteral("Doke"));
  if (!QDir().mkpath(dokeDir)) {
    if (error) {
      *error = QStringLiteral("doke_runtime_config_mkdir_failed");
    }
    return {};
  }

  const QString path = QDir(dokeDir).filePath(QStringLiteral("runtime.json"));
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    if (error) {
      *error = QStringLiteral("doke_runtime_config_write_failed");
    }
    return {};
  }
  file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
  file.write("\n");
  file.close();
  return path;
}
} // namespace

ProfileRuntimeManager::ProfileRuntimeManager(QObject* parent) : QObject(parent) {}

ProfileRuntimeManager::~ProfileRuntimeManager() {
  const auto profileKeys = m_processByProfileId.keys();
  for (const auto& k : profileKeys) {
    QProcess* p = m_processByProfileId.take(k);
    if (p) {
      p->kill();
      p->deleteLater();
    }
  }

  const auto cdpKeys = m_cdpByProfileId.keys();
  for (const auto& k : cdpKeys) {
    cleanupCdp(k);
  }

  const auto mapKeys = m_proxyMapperByProfileId.keys();
  for (const auto& k : mapKeys) {
    cleanupProxyMapping(k);
  }

  const auto extKeys = m_proxyAuthExtDirByProfileId.keys();
  for (const auto& k : extKeys) {
    cleanupProxyAuthExtension(k);
  }

  const auto runtimeConfigKeys = m_dokeRuntimeConfigByProfileId.keys();
  for (const auto& k : runtimeConfigKeys) {
    cleanupDokeRuntimeConfig(k);
  }
}

void ProfileRuntimeManager::handleMessage(const QJsonObject& obj, StatusCallback status, LogCallback log) {
  const QString type = obj.value(QStringLiteral("type")).toString();
  const ProfileLaunch::StartRequest request = ProfileLaunch::parseStartRequest(obj);
  const QString& profileId = request.profileId;
  const QString& profileName = request.profileName;
  const QString& dataDirFromMsg = request.dataDir;
  const QString& browserEngine = request.browserEngine;
  const QString& engineConfigJson = request.engineConfigJson;
  const bool engineHumanizeEnabled = request.engineHumanizeEnabled;
  const bool engineGeoipEnabled = request.engineGeoipEnabled;
  const QString& fingerprintMode = request.fingerprintMode;
  const QString& language = request.language;
  const QString& userAgent = request.userAgent;
  const QString& platform = request.platform;
  const int hardwareConcurrency = request.hardwareConcurrency;
  const int deviceMemoryGb = request.deviceMemoryGb;
  const double deviceScaleFactor = request.deviceScaleFactor;
  const QString& timezone = request.timezone;
  const QString& resolution = request.resolution;
  const bool touchEnabled = request.touchEnabled;
  const bool geoEnabled = request.geoEnabled;
  const double geoLatitude = request.geoLatitude;
  const double geoLongitude = request.geoLongitude;
  const double geoAccuracy = request.geoAccuracy;
  const QJsonObject& proxyObj = request.proxy;
  const bool chromeCompatRequested = request.chromeCompatRequested;
  const bool chromeCompat = request.chromeCompat;

  auto sendStatus = [status, profileId](const QString& profileStatus, const QString& error, int debugPort = 0) {
    if (status) {
      status(profileId, profileStatus, error, debugPort);
    }
  };
  auto sendLogLine = [log](const QString& message) {
    if (log) {
      log(message);
    }
  };

  if (profileId.isEmpty()) {
    sendStatus(QStringLiteral("error"), QStringLiteral("missing_profile_id"));
    return;
  }

  sendLogLine(QStringLiteral("%1 engine=%4 profile=%2 (%3)")
                  .arg(type, profileName.isEmpty() ? QStringLiteral("-") : profileName,
                       profileId.isEmpty() ? QStringLiteral("-") : profileId, browserEngine));

  const quint32 fpSeed = qHash(profileId);

  if (type == QStringLiteral("profile.stop")) {
    QProcess* existing = m_processByProfileId.value(profileId);
    if (!existing || existing->state() == QProcess::NotRunning) {
      sendStatus(QStringLiteral("stopped"), QString());
      if (existing) {
        m_processByProfileId.remove(profileId);
        existing->deleteLater();
      }
      return;
    }

    if (!m_stopRequested.contains(profileId)) {
      m_stopRequested.insert(profileId);
    }
    sendStatus(QStringLiteral("stopping"), QString());
    existing->terminate();
    QTimer::singleShot(1200, this, [this, profileId]() {
      QProcess* p = m_processByProfileId.value(profileId);
      if (!p || p->state() == QProcess::NotRunning) {
        return;
      }
      p->kill();
    });
    return;
  }

  QProcess* existing = m_processByProfileId.value(profileId);
  if (existing && existing->state() != QProcess::NotRunning) {
    sendStatus(QStringLiteral("running"), QString(), m_debugPortByProfileId.value(profileId));
    return;
  }
  if (existing) {
    m_processByProfileId.remove(profileId);
    existing->deleteLater();
    existing = nullptr;
  }

  sendStatus(QStringLiteral("starting"), QString());

  const BrowserEngineDescriptor engine = BrowserEngineFactory::describe(browserEngine);
  if (engine.error.startsWith(QStringLiteral("unsupported_browser_engine:"))) {
    sendStatus(QStringLiteral("error"), engine.error);
    return;
  }
  QString browserExe = engine.executable;
  QString browserError = engine.error;
  DokeChromiumEngine::ProbeResult dokeProbe;
  if (browserEngine == QStringLiteral("doke_chromium")) {
    dokeProbe = DokeChromiumEngine::probe(engineConfigJson);
    browserExe = dokeProbe.resolution.executable;
    browserError = dokeProbe.resolution.error;
  }
  const DokeChromiumEngine::Config dokeConfig =
      browserEngine == QStringLiteral("doke_chromium") ? DokeChromiumEngine::parseConfig(engineConfigJson)
                                                       : DokeChromiumEngine::Config();
  if (browserExe.isEmpty()) {
    sendStatus(QStringLiteral("error"), browserError);
    return;
  }

  if (browserEngine == QStringLiteral("doke_chromium")) {
    if (!dokeProbe.nativeProbeError.isEmpty() && !dokeProbe.capabilities.isEmpty()) {
      sendLogLine(QStringLiteral("doke_chromium[%1] native_probe_unavailable error=%2 requested=%3; using agent fallback")
                      .arg(profileId, dokeProbe.nativeProbeError, dokeProbe.capabilities.join(QStringLiteral(","))));
    }
    if (!dokeProbe.missingNativeCapabilities.isEmpty()) {
      sendLogLine(QStringLiteral("doke_chromium[%1] missing_native_capabilities=%2; using agent fallback")
                      .arg(profileId, dokeProbe.missingNativeCapabilities.join(QStringLiteral(","))));
    }
  }

  const QString userDataDir = ProfileLaunch::resolveProfileDataDir(profileId, dataDirFromMsg);
  if (userDataDir.isEmpty()) {
    sendStatus(QStringLiteral("error"), QStringLiteral("invalid_data_dir"));
    return;
  }
  QDir().mkpath(userDataDir);

  cleanupProfileResources(profileId);
  const ProfileLaunch::ProxyConfig proxyConfig = ProfileLaunch::buildProxyConfig(proxyObj, this);
  if (proxyConfig.mapper) {
    m_proxyMapperByProfileId.insert(profileId, proxyConfig.mapper);
  }

  QString chromeExtDir;
  const bool nativeFingerprint = browserEngine == QStringLiteral("doke_chromium") && dokeConfig.nativeFingerprint
                                 && dokeProbe.nativeCapabilities.contains(QStringLiteral("native_fingerprint"));
  const bool nativeGeoip = browserEngine == QStringLiteral("doke_chromium") && dokeConfig.nativeGeoip
                           && dokeProbe.nativeCapabilities.contains(QStringLiteral("native_geoip"));
  const bool fingerprintFallbackNeeded =
      !nativeFingerprint
      && (!language.isEmpty() || !userAgent.isEmpty() || !platform.isEmpty() || hardwareConcurrency > 0 || deviceMemoryGb > 0
          || deviceScaleFactor > 0 || !timezone.isEmpty() || touchEnabled);
  const bool geoFallbackNeeded = geoEnabled && !nativeGeoip;
  const bool needInject = fingerprintFallbackNeeded || geoFallbackNeeded;
  const bool cdpEnabled = needInject;
  const QString fallbackLanguage = fingerprintFallbackNeeded ? language : QString();
  const QString fallbackUserAgent = fingerprintFallbackNeeded ? userAgent : QString();
  const QString fallbackPlatform = fingerprintFallbackNeeded ? platform : QString();
  const int fallbackHardwareConcurrency = fingerprintFallbackNeeded ? hardwareConcurrency : 0;
  const int fallbackDeviceMemoryGb = fingerprintFallbackNeeded ? deviceMemoryGb : 0;
  const double fallbackDeviceScaleFactor = fingerprintFallbackNeeded ? deviceScaleFactor : 0;
  const QString fallbackTimezone = fingerprintFallbackNeeded ? timezone : QString();
  const QString fallbackResolution = fingerprintFallbackNeeded ? resolution : QString();
  const bool fallbackTouchEnabled = fingerprintFallbackNeeded && touchEnabled;

  QString dokeRuntimeConfigPath;
  if (browserEngine == QStringLiteral("doke_chromium")) {
    const bool nativeProxy = dokeConfig.nativeProxy && dokeProbe.nativeCapabilities.contains(QStringLiteral("native_proxy"));
    const bool nativeHumanize =
        dokeConfig.nativeHumanize && dokeProbe.nativeCapabilities.contains(QStringLiteral("native_humanize"));

    QJsonObject fingerprint;
    fingerprint.insert(QStringLiteral("mode"), fingerprintMode);
    fingerprint.insert(QStringLiteral("seed"), static_cast<int>(fpSeed));
    fingerprint.insert(QStringLiteral("language"), language);
    fingerprint.insert(QStringLiteral("user_agent"), userAgent);
    fingerprint.insert(QStringLiteral("platform"), platform);
    fingerprint.insert(QStringLiteral("hardware_concurrency"), hardwareConcurrency);
    fingerprint.insert(QStringLiteral("device_memory_gb"), deviceMemoryGb);
    fingerprint.insert(QStringLiteral("device_scale_factor"), deviceScaleFactor);
    fingerprint.insert(QStringLiteral("timezone"), timezone);
    fingerprint.insert(QStringLiteral("resolution"), resolution);
    fingerprint.insert(QStringLiteral("touch_enabled"), touchEnabled);
    const QString runtimeWindowSizeArg = ProfileLaunch::windowSizeArgForResolution(resolution);
    if (!runtimeWindowSizeArg.isEmpty()) {
      fingerprint.insert(QStringLiteral("window_size"), runtimeWindowSizeArg.section(QLatin1Char('='), 1));
    }
    if (deviceScaleFactor > 0) {
      fingerprint.insert(QStringLiteral("device_scale_factor_arg"),
                         QString::number(deviceScaleFactor, 'g', 8));
    }
    if (hardwareConcurrency > 0) {
      fingerprint.insert(QStringLiteral("hardware_concurrency_arg"), QString::number(hardwareConcurrency));
    }
    if (deviceMemoryGb > 0) {
      fingerprint.insert(QStringLiteral("device_memory_gb_arg"), QString::number(deviceMemoryGb));
    }
    if (touchEnabled) {
      fingerprint.insert(QStringLiteral("touch_events"), QStringLiteral("enabled"));
    }
    const QJsonObject uaClientHints =
        FingerprintMetadata::toCdpUserAgentMetadata(FingerprintMetadata::buildUaClientHints(userAgent, platform));
    if (!uaClientHints.isEmpty()) {
      fingerprint.insert(QStringLiteral("ua_client_hints"), uaClientHints);
    }

    QJsonObject geo;
    geo.insert(QStringLiteral("enabled"), geoEnabled);
    geo.insert(QStringLiteral("latitude"), geoLatitude);
    geo.insert(QStringLiteral("longitude"), geoLongitude);
    geo.insert(QStringLiteral("accuracy"), geoAccuracy);

    QJsonObject native;
    native.insert(QStringLiteral("requested"), jsonArrayFromStringList(dokeProbe.capabilities));
    native.insert(QStringLiteral("supported"), jsonArrayFromStringList(dokeProbe.nativeCapabilities));
    native.insert(QStringLiteral("missing"), jsonArrayFromStringList(dokeProbe.missingNativeCapabilities));
    native.insert(QStringLiteral("fingerprint"), nativeFingerprint);
    native.insert(QStringLiteral("proxy"), nativeProxy);
    native.insert(QStringLiteral("geoip"), nativeGeoip);
    native.insert(QStringLiteral("humanize"), nativeHumanize);

    QJsonObject fallback;
    fallback.insert(QStringLiteral("fingerprint"), fingerprintFallbackNeeded);
    fallback.insert(QStringLiteral("geoip"), geoFallbackNeeded);
    fallback.insert(QStringLiteral("proxy_auth"), proxyConfig.enableProxyAuth);

    QJsonObject proxy;
    proxy.insert(QStringLiteral("enabled"), !proxyConfig.argument.isEmpty());
    proxy.insert(QStringLiteral("scheme"), proxyConfig.scheme);
    proxy.insert(QStringLiteral("host"), proxyConfig.host);
    proxy.insert(QStringLiteral("port"), proxyConfig.port);
    proxy.insert(QStringLiteral("auth_fallback"), proxyConfig.enableProxyAuth);

    QJsonObject webrtc;
    webrtc.insert(QStringLiteral("ip_handling_policy"), QStringLiteral("disable_non_proxied_udp"));
    webrtc.insert(QStringLiteral("proxy_only"), !proxyConfig.argument.isEmpty());

    QJsonObject rendering;
    rendering.insert(QStringLiteral("canvas"), renderingNoiseSection(fpSeed, 0xC0A551D));
    rendering.insert(QStringLiteral("webgl"), renderingNoiseSection(fpSeed, 0x0EBC1A55));
    rendering.insert(QStringLiteral("audio"), renderingNoiseSection(fpSeed, 0xA0D105E5));

    const QString surfacePreset = surfacePresetForPlatform(platform);
    QJsonObject surfaces;
    surfaces.insert(QStringLiteral("plugins"), presetSurfaceSection(surfacePreset));
    surfaces.insert(QStringLiteral("mime_types"), presetSurfaceSection(surfacePreset));
    surfaces.insert(QStringLiteral("fonts"), seededPresetSurfaceSection(surfacePreset, fpSeed, 0xF071FACE));
    surfaces.insert(QStringLiteral("client_rects"),
                    seededPresetSurfaceSection(surfacePreset, fpSeed, 0xC11E47EC));

    QJsonObject alignmentGeo;
    alignmentGeo.insert(QStringLiteral("enabled"), geoEnabled);
    alignmentGeo.insert(QStringLiteral("latitude"), geoLatitude);
    alignmentGeo.insert(QStringLiteral("longitude"), geoLongitude);
    alignmentGeo.insert(QStringLiteral("accuracy"), geoAccuracy);
    alignmentGeo.insert(QStringLiteral("latitude_arg"), QString::number(geoLatitude, 'f', 6));
    alignmentGeo.insert(QStringLiteral("longitude_arg"), QString::number(geoLongitude, 'f', 6));
    alignmentGeo.insert(QStringLiteral("accuracy_arg"),
                        QString::number(geoAccuracy > 0 ? geoAccuracy : 1000, 'f', 0));

    QJsonObject alignmentProxy;
    alignmentProxy.insert(QStringLiteral("enabled"), !proxyConfig.argument.isEmpty());
    alignmentProxy.insert(QStringLiteral("scheme"), proxyConfig.scheme);
    alignmentProxy.insert(QStringLiteral("host"), proxyConfig.host);
    alignmentProxy.insert(QStringLiteral("port"), proxyConfig.port);
    if (proxyConfig.port > 0) {
      alignmentProxy.insert(QStringLiteral("port_arg"), QString::number(proxyConfig.port));
    }

    QJsonObject alignment;
    alignment.insert(QStringLiteral("language"), language);
    alignment.insert(QStringLiteral("timezone"), timezone);
    alignment.insert(QStringLiteral("geo"), alignmentGeo);
    alignment.insert(QStringLiteral("proxy"), alignmentProxy);

    QJsonObject automation;
    automation.insert(QStringLiteral("webdriver_policy"), QStringLiteral("hide"));
    automation.insert(QStringLiteral("devtools_exposure"), cdpEnabled ? QStringLiteral("fallback_required")
                                                                      : QStringLiteral("minimize"));
    automation.insert(QStringLiteral("cdp_side_effect_guard"), true);
    automation.insert(QStringLiteral("debug_port_required"), cdpEnabled);
    automation.insert(QStringLiteral("startup_automation_controlled"), false);

    QJsonObject root;
    root.insert(QStringLiteral("schema"), QStringLiteral("doke_profile_runtime.v1"));
    root.insert(QStringLiteral("profile_id"), profileId);
    root.insert(QStringLiteral("profile_name"), profileName);
    root.insert(QStringLiteral("version"), dokeProbe.version);
    root.insert(QStringLiteral("probe_protocol"), dokeProbe.probeProtocol);
    root.insert(QStringLiteral("fingerprint"), fingerprint);
    root.insert(QStringLiteral("geo"), geo);
    root.insert(QStringLiteral("native"), native);
    root.insert(QStringLiteral("fallback"), fallback);
    root.insert(QStringLiteral("proxy"), proxy);
    root.insert(QStringLiteral("webrtc"), webrtc);
    root.insert(QStringLiteral("rendering"), rendering);
    root.insert(QStringLiteral("surfaces"), surfaces);
    root.insert(QStringLiteral("alignment"), alignment);
    root.insert(QStringLiteral("automation"), automation);

    QString runtimeConfigError;
    dokeRuntimeConfigPath = writeDokeRuntimeConfig(userDataDir, root, &runtimeConfigError);
    if (dokeRuntimeConfigPath.isEmpty()) {
      cleanupProfileResources(profileId);
      sendStatus(QStringLiteral("error"), runtimeConfigError);
      return;
    }
    m_dokeRuntimeConfigByProfileId.insert(profileId, dokeRuntimeConfigPath);
    sendLogLine(QStringLiteral("doke_chromium[%1] runtime_config=%2").arg(profileId, dokeRuntimeConfigPath));
  }

  if (needInject || proxyConfig.enableProxyAuth) {
    SystemChromeEngine::ExtensionOptions extensionOptions;
    extensionOptions.profileId = profileId;
    extensionOptions.fingerprintSeed = fpSeed;
    extensionOptions.fingerprintMode = fingerprintMode;
    extensionOptions.language = fallbackLanguage;
    extensionOptions.userAgent = fallbackUserAgent;
    extensionOptions.platform = fallbackPlatform;
    extensionOptions.hardwareConcurrency = fallbackHardwareConcurrency;
    extensionOptions.deviceMemoryGb = fallbackDeviceMemoryGb;
    extensionOptions.deviceScaleFactor = fallbackDeviceScaleFactor;
    extensionOptions.timezone = fallbackTimezone;
    extensionOptions.resolution = fallbackResolution;
    extensionOptions.touchEnabled = fallbackTouchEnabled;
    extensionOptions.geoEnabled = geoFallbackNeeded;
    extensionOptions.geoLatitude = geoLatitude;
    extensionOptions.geoLongitude = geoLongitude;
    extensionOptions.geoAccuracy = geoAccuracy;
    extensionOptions.proxyScheme = proxyConfig.scheme;
    extensionOptions.proxyHost = proxyConfig.host;
    extensionOptions.proxyPort = proxyConfig.port;
    extensionOptions.proxyUsername = proxyConfig.username;
    extensionOptions.proxyPassword = proxyConfig.password;
    extensionOptions.enableProxyAuth = proxyConfig.enableProxyAuth;
    chromeExtDir = SystemChromeEngine::createProfileExtension(extensionOptions);
    if (!chromeExtDir.isEmpty()) {
      m_proxyAuthExtDirByProfileId.insert(profileId, chromeExtDir);
    }
  }

  const QString url = obj.value(QStringLiteral("url")).toString().trimmed();
  const int debugPort = cdpEnabled ? ProfileLaunch::allocateLocalTcpPort() : 0;
  if (debugPort > 0) {
    m_debugPortByProfileId.insert(profileId, debugPort);
  } else {
    m_debugPortByProfileId.remove(profileId);
    if (cdpEnabled) {
      sendLogLine(QStringLiteral("cdp[%1] debug_port_allocation_failed; fallback cdp disabled").arg(profileId.left(8)));
    }
  }
  const QString windowSizeArg = ProfileLaunch::windowSizeArgForResolution(resolution);

  SystemChromeEngine::LaunchOptions launchOptions;
  launchOptions.userDataDir = userDataDir;
  launchOptions.proxyArg = proxyConfig.argument;
  launchOptions.extensionDir = chromeExtDir;
  launchOptions.url = url;
  launchOptions.language = language;
  launchOptions.userAgent = userAgent;
  launchOptions.timezone = timezone;
  launchOptions.windowSizeArg = windowSizeArg;
  launchOptions.touchEnabled = touchEnabled;
  launchOptions.debugPort = debugPort;

  SystemChromeEngine::ProcessLaunchOptions processOptions;
  processOptions.profileId = profileId;
  processOptions.executable = browserExe;
  processOptions.timezone = timezone;
  processOptions.compatRequested = chromeCompatRequested;
  processOptions.compatInitial = chromeCompat;
  if (browserEngine == QStringLiteral("doke_chromium")) {
    DokeChromiumEngine::LaunchOptions dokeOptions;
    dokeOptions.chromium = launchOptions;
    dokeOptions.engineConfigJson = engineConfigJson;
    dokeOptions.runtimeConfigPath = dokeRuntimeConfigPath;
    dokeOptions.humanize = engineHumanizeEnabled;
    dokeOptions.geoip = engineGeoipEnabled;
    processOptions.arguments = DokeChromiumEngine::buildArguments(dokeOptions, chromeCompat);
    processOptions.compatArguments = DokeChromiumEngine::buildArguments(dokeOptions, true);
    processOptions.processLabel = QStringLiteral("doke_chromium");
  } else {
    processOptions.arguments = SystemChromeEngine::buildArguments(launchOptions, chromeCompat);
    processOptions.compatArguments = SystemChromeEngine::buildArguments(launchOptions, true);
    processOptions.processLabel = QStringLiteral("chrome");
  }

  SystemChromeEngine::ProcessCallbacks processCallbacks;
  processCallbacks.isCurrentProcess = [this, profileId](QProcess* proc) {
    return m_processByProfileId.value(profileId) == proc;
  };
  processCallbacks.isStopRequested = [this, profileId]() {
    return m_stopRequested.contains(profileId);
  };
  processCallbacks.consumeExpectedStop = [this, profileId]() {
    return m_stopRequested.remove(profileId);
  };
  processCallbacks.clearCurrentProcess = [this, profileId]() {
    m_processByProfileId.remove(profileId);
  };
  processCallbacks.cleanup = [this, profileId]() {
    cleanupProfileResources(profileId);
  };
  processCallbacks.status = [sendStatus, debugPort](const QString& profileStatus, const QString& error) {
    const bool browserMayBeDebuggable =
        profileStatus == QStringLiteral("starting") || profileStatus == QStringLiteral("running");
    sendStatus(profileStatus, error, browserMayBeDebuggable ? debugPort : 0);
  };
  processCallbacks.logLine = sendLogLine;

  QProcess* p = nullptr;
  if (browserEngine == QStringLiteral("doke_chromium")) {
    p = DokeChromiumEngine::launchProcess(processOptions, this, processCallbacks);
  } else {
    p = SystemChromeEngine::launchProcess(processOptions, this, processCallbacks);
  }
  if (!p) {
    cleanupProfileResources(profileId);
    sendStatus(QStringLiteral("error"), QStringLiteral("process_create_failed"));
    return;
  }
  m_processByProfileId.insert(profileId, p);

  if (cdpEnabled && debugPort > 0) {
    SystemChromeEngine::CdpAttachOptions cdpOptions;
    cdpOptions.profileId = profileId;
    cdpOptions.initialUrl = url;
    cdpOptions.debugPort = debugPort;
    cdpOptions.fingerprint.enabled = fingerprintFallbackNeeded;
    cdpOptions.fingerprint.language = fallbackLanguage;
    cdpOptions.fingerprint.userAgent = fallbackUserAgent;
    cdpOptions.fingerprint.platform = fallbackPlatform;
    cdpOptions.fingerprint.hardwareConcurrency = fallbackHardwareConcurrency;
    cdpOptions.fingerprint.deviceMemoryGb = fallbackDeviceMemoryGb;
    cdpOptions.fingerprint.deviceScaleFactor = fallbackDeviceScaleFactor;
    cdpOptions.fingerprint.seed = fpSeed;
    cdpOptions.fingerprint.timezone = fallbackTimezone;
    cdpOptions.fingerprint.resolution = fallbackResolution;
    cdpOptions.fingerprint.touchEnabled = fallbackTouchEnabled;
    cdpOptions.fingerprint.geoEnabled = geoFallbackNeeded;
    cdpOptions.fingerprint.geoLatitude = geoLatitude;
    cdpOptions.fingerprint.geoLongitude = geoLongitude;
    cdpOptions.fingerprint.geoAccuracy = geoAccuracy;

    SystemChromeEngine::CdpAttachCallbacks cdpCallbacks;
    cdpCallbacks.isActive = [this, profileId]() {
      return m_processByProfileId.contains(profileId);
    };
    cdpCallbacks.replaceClient = [this, profileId](CdpClient* cdp) {
      cleanupCdp(profileId);
      m_cdpByProfileId.insert(profileId, cdp);
    };
    cdpCallbacks.logLine = sendLogLine;
    SystemChromeEngine::attachCdpWhenReady(cdpOptions, this, cdpCallbacks);
  }
}

void ProfileRuntimeManager::cleanupProxyAuthExtension(const QString& profileId) {
  const QString dir = m_proxyAuthExtDirByProfileId.take(profileId);
  if (!dir.isEmpty()) {
    QDir(dir).removeRecursively();
  }
}

void ProfileRuntimeManager::cleanupDokeRuntimeConfig(const QString& profileId) {
  const QString path = m_dokeRuntimeConfigByProfileId.take(profileId);
  if (!path.isEmpty()) {
    QFile::remove(path);
  }
}

void ProfileRuntimeManager::cleanupProxyMapping(const QString& profileId) {
  HttpProxyMapper* m = m_proxyMapperByProfileId.take(profileId);
  if (m) {
    m->stop();
    m->deleteLater();
  }
}

void ProfileRuntimeManager::cleanupCdp(const QString& profileId) {
  CdpClient* c = m_cdpByProfileId.take(profileId);
  if (c) {
    c->stop();
    c->deleteLater();
  }
}

void ProfileRuntimeManager::cleanupProfileResources(const QString& profileId) {
  m_debugPortByProfileId.remove(profileId);
  cleanupCdp(profileId);
  cleanupProxyMapping(profileId);
  cleanupProxyAuthExtension(profileId);
  cleanupDokeRuntimeConfig(profileId);
}
