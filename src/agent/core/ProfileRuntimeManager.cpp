#include "ProfileRuntimeManager.h"

#include "BrowserEngineFactory.h"
#include "CdpClient.h"
#include "DokeChromiumEngine.h"
#include "HttpProxyMapper.h"
#include "ProfileLaunchConfig.h"
#include "SystemChromeEngine.h"

#include <QDir>
#include <QJsonObject>
#include <QProcess>
#include <QTimer>

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

  auto sendStatus = [status, profileId](const QString& profileStatus, const QString& error) {
    if (status) {
      status(profileId, profileStatus, error);
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
    sendStatus(QStringLiteral("running"), QString());
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
  const QString chromeExe = engine.executable;
  QString browserExe = chromeExe;
  if (browserEngine == QStringLiteral("doke_chromium")) {
    browserExe = DokeChromiumEngine::resolveExecutable(engineConfigJson);
  }
  const DokeChromiumEngine::Config dokeConfig =
      browserEngine == QStringLiteral("doke_chromium") ? DokeChromiumEngine::parseConfig(engineConfigJson)
                                                       : DokeChromiumEngine::Config();
  if (browserExe.isEmpty()) {
    sendStatus(QStringLiteral("error"), engine.error);
    return;
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
  const bool nativeFingerprint = browserEngine == QStringLiteral("doke_chromium") && dokeConfig.nativeFingerprint;
  const bool nativeGeoip = browserEngine == QStringLiteral("doke_chromium") && dokeConfig.nativeGeoip;
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
  processCallbacks.status = sendStatus;
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
  cleanupCdp(profileId);
  cleanupProxyMapping(profileId);
  cleanupProxyAuthExtension(profileId);
}
