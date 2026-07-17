#include "IpcServer.h"

#include "BrowserEngineFactory.h"
#include "CdpClient.h"
#include "DokeChromiumEngine.h"
#include "HttpProxyMapper.h"
#include "ProfileLaunchConfig.h"
#include "ProxyTestRunner.h"
#include "SystemChromeEngine.h"
#include "shared/ipc/FramedJsonSocket.h"
#include "shared/ipc/IpcNames.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QProcess>
#include <QTemporaryFile>
#include <QTimer>
#include <QDir>
#include <functional>

namespace {
QJsonObject engineInfo(const BrowserEngineDescriptor& engine) {
  QJsonObject obj;
  obj.insert(QStringLiteral("id"), engine.id);
  obj.insert(QStringLiteral("available"), engine.available());
  if (engine.available()) {
    obj.insert(QStringLiteral("executable"), engine.executable);
  } else {
    obj.insert(QStringLiteral("error"), engine.error);
  }
  return obj;
}

} // namespace

IpcServer::IpcServer(QObject* parent) : QObject(parent) {
  m_server = new QLocalServer(this);
  QObject::connect(m_server, &QLocalServer::newConnection, this, &IpcServer::onNewConnection);
}

IpcServer::~IpcServer() {
  const auto profileKeys = m_profileProcByProfileId.keys();
  for (const auto& k : profileKeys) {
    QProcess* p = m_profileProcByProfileId.take(k);
    if (p) {
      p->kill();
      p->deleteLater();
    }
  }

  const auto cdpKeys = m_cdpByProfileId.keys();
  for (const auto& k : cdpKeys) {
    CdpClient* c = m_cdpByProfileId.take(k);
    if (c) {
      c->stop();
      c->deleteLater();
    }
  }

  const auto mapKeys = m_proxyMapperByProfileId.keys();
  for (const auto& k : mapKeys) {
    HttpProxyMapper* m = m_proxyMapperByProfileId.take(k);
    if (m) {
      m->stop();
      m->deleteLater();
    }
  }

  const auto keys = m_openvpnByProfileId.keys();
  for (const auto& k : keys) {
    QProcess* p = m_openvpnByProfileId.take(k);
    if (p) {
      p->kill();
      p->deleteLater();
    }
  }

  const auto extKeys = m_chromeProxyAuthExtDirByProfileId.keys();
  for (const auto& k : extKeys) {
    const QString dir = m_chromeProxyAuthExtDirByProfileId.take(k);
    if (!dir.isEmpty()) {
      QDir(dir).removeRecursively();
    }
  }

  const auto authKeys = m_openvpnSocksAuthFileByProfileId.keys();
  for (const auto& k : authKeys) {
    const QString path = m_openvpnSocksAuthFileByProfileId.take(k);
    if (!path.isEmpty()) {
      QFile::remove(path);
    }
  }
}

bool IpcServer::start() {
  QLocalServer::removeServer(QString::fromUtf8(IpcNames::kAgentServerName));
  if (m_server->listen(QString::fromUtf8(IpcNames::kAgentServerName))) {
    return true;
  }
  emit logLine(QStringLiteral("listen_failed: %1").arg(m_server->errorString()));
  return false;
}

void IpcServer::onNewConnection() {
  while (m_server->hasPendingConnections()) {
    QLocalSocket* sock = m_server->nextPendingConnection();
    if (!sock) {
      continue;
    }

    if (m_peer) {
      m_peer->deleteLater();
      m_peer = nullptr;
    }

    m_peer = new FramedJsonSocket(sock, this);
    QObject::connect(m_peer, &FramedJsonSocket::disconnected, this, &IpcServer::onPeerDisconnected);
    QObject::connect(m_peer, &FramedJsonSocket::ioError, this, &IpcServer::onPeerError);
    QObject::connect(m_peer, &FramedJsonSocket::jsonReceived, this, &IpcServer::onPeerJson);

    QJsonObject msg;
    msg.insert(QStringLiteral("type"), QStringLiteral("log.line"));
    msg.insert(QStringLiteral("message"), QStringLiteral("agent_connected"));
    m_peer->send(msg);
  }
}

void IpcServer::onPeerDisconnected() {
  emit logLine(QStringLiteral("peer_disconnected"));
}

void IpcServer::onPeerError(const QString& message) {
  emit logLine(QStringLiteral("peer_error: %1").arg(message));
}

void IpcServer::onPeerJson(const QJsonObject& obj) {
  if (!m_peer) {
    return;
  }

  const QString type = obj.value(QStringLiteral("type")).toString();
  if (type == QStringLiteral("hello")) {
    QJsonObject ack;
    ack.insert(QStringLiteral("type"), QStringLiteral("hello.ack"));
    ack.insert(QStringLiteral("agent"), QStringLiteral("dokebrowser_agent"));
    ack.insert(QStringLiteral("version"), 1);
    m_peer->send(ack);

    QJsonObject log;
    log.insert(QStringLiteral("type"), QStringLiteral("log.line"));
    log.insert(QStringLiteral("message"), QStringLiteral("hello_ok"));
    m_peer->send(log);
    return;
  }

  if (type == QStringLiteral("engine.list")) {
    QJsonArray engines;
    for (const auto& engine : BrowserEngineFactory::listEngines()) {
      engines.push_back(engineInfo(engine));
    }

    QJsonObject result;
    result.insert(QStringLiteral("type"), QStringLiteral("engine.list.result"));
    result.insert(QStringLiteral("engines"), engines);
    m_peer->send(result);
    return;
  }

  if (type == QStringLiteral("profile.start") || type == QStringLiteral("profile.stop")) {
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

    auto sendStatus = [this, profileId](const QString& status, const QString& error) {
      if (!m_peer) {
        return;
      }
      QJsonObject msg;
      msg.insert(QStringLiteral("type"), QStringLiteral("profile.status"));
      msg.insert(QStringLiteral("profile_id"), profileId);
      msg.insert(QStringLiteral("status"), status);
      msg.insert(QStringLiteral("error"), error);
      m_peer->send(msg);
    };

    if (profileId.isEmpty()) {
      sendStatus(QStringLiteral("error"), QStringLiteral("missing_profile_id"));
      return;
    }

    QJsonObject log;
    log.insert(QStringLiteral("type"), QStringLiteral("log.line"));
    log.insert(QStringLiteral("message"),
               QStringLiteral("%1 engine=%4 profile=%2 (%3)")
                   .arg(type, profileName.isEmpty() ? QStringLiteral("-") : profileName,
                        profileId.isEmpty() ? QStringLiteral("-") : profileId, browserEngine));
    m_peer->send(log);

    auto cleanupProxyAuthExt = [this](const QString& pid) {
      const QString dir = m_chromeProxyAuthExtDirByProfileId.take(pid);
      if (!dir.isEmpty()) {
        QDir(dir).removeRecursively();
      }
    };

    auto cleanupProxyMapping = [this](const QString& pid) {
      HttpProxyMapper* m = m_proxyMapperByProfileId.take(pid);
      if (m) {
        m->stop();
        m->deleteLater();
      }
    };

    auto cleanupCdp = [this](const QString& pid) {
      CdpClient* c = m_cdpByProfileId.take(pid);
      if (c) {
        c->stop();
        c->deleteLater();
      }
    };

    const quint32 fpSeed = qHash(profileId);

    if (type == QStringLiteral("profile.stop")) {
      QProcess* existing = m_profileProcByProfileId.value(profileId);
      if (!existing || existing->state() == QProcess::NotRunning) {
        sendStatus(QStringLiteral("stopped"), QString());
        if (existing) {
          m_profileProcByProfileId.remove(profileId);
          existing->deleteLater();
        }
        return;
      }

      if (!m_profileStopRequested.contains(profileId)) {
        m_profileStopRequested.insert(profileId);
      }
      sendStatus(QStringLiteral("stopping"), QString());
      existing->terminate();
      QTimer::singleShot(1200, this, [this, profileId]() {
        QProcess* p = m_profileProcByProfileId.value(profileId);
        if (!p) {
          return;
        }
        if (p->state() == QProcess::NotRunning) {
          return;
        }
        p->kill();
      });
      return;
    }

    QProcess* existing = m_profileProcByProfileId.value(profileId);
    if (existing && existing->state() != QProcess::NotRunning) {
      sendStatus(QStringLiteral("running"), QString());
      return;
    }
    if (existing) {
      m_profileProcByProfileId.remove(profileId);
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

    cleanupCdp(profileId);
    cleanupProxyAuthExt(profileId);
    cleanupProxyMapping(profileId);
    const ProfileLaunch::ProxyConfig proxyConfig = ProfileLaunch::buildProxyConfig(proxyObj, this);
    if (proxyConfig.mapper) {
      m_proxyMapperByProfileId.insert(profileId, proxyConfig.mapper);
    }
    const QString proxyArg = proxyConfig.argument;
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
        m_chromeProxyAuthExtDirByProfileId.insert(profileId, chromeExtDir);
      }
    }
    const QString url = obj.value(QStringLiteral("url")).toString().trimmed();

    const int debugPort = cdpEnabled ? ProfileLaunch::allocateLocalTcpPort() : 0;
    const QString windowSizeArg = ProfileLaunch::windowSizeArgForResolution(resolution);

    SystemChromeEngine::LaunchOptions launchOptions;
    launchOptions.userDataDir = userDataDir;
    launchOptions.proxyArg = proxyArg;
    launchOptions.extensionDir = chromeExtDir;
    launchOptions.url = url;
    launchOptions.language = language;
    launchOptions.userAgent = userAgent;
    launchOptions.timezone = timezone;
    launchOptions.windowSizeArg = windowSizeArg;
    launchOptions.touchEnabled = touchEnabled;
    launchOptions.debugPort = debugPort;

    auto cleanupProfileResources = [cleanupCdp, cleanupProxyMapping, cleanupProxyAuthExt, profileId]() {
      cleanupCdp(profileId);
      cleanupProxyMapping(profileId);
      cleanupProxyAuthExt(profileId);
    };
    auto sendLogLine = [this](const QString& message) {
      if (!m_peer) {
        return;
      }
      QJsonObject log;
      log.insert(QStringLiteral("type"), QStringLiteral("log.line"));
      log.insert(QStringLiteral("message"), message);
      m_peer->send(log);
    };

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
      return m_profileProcByProfileId.value(profileId) == proc;
    };
    processCallbacks.isStopRequested = [this, profileId]() {
      return m_profileStopRequested.contains(profileId);
    };
    processCallbacks.consumeExpectedStop = [this, profileId]() {
      return m_profileStopRequested.remove(profileId);
    };
    processCallbacks.clearCurrentProcess = [this, profileId]() {
      m_profileProcByProfileId.remove(profileId);
    };
    processCallbacks.cleanup = cleanupProfileResources;
    processCallbacks.status = sendStatus;
    processCallbacks.logLine = sendLogLine;

    QProcess* p = nullptr;
    if (browserEngine == QStringLiteral("doke_chromium")) {
      p = DokeChromiumEngine::launchProcess(processOptions, this, processCallbacks);
    } else {
      p = SystemChromeEngine::launchProcess(processOptions, this, processCallbacks);
    }
    if (!p) {
      cleanupProfileResources();
      sendStatus(QStringLiteral("error"), QStringLiteral("process_create_failed"));
      return;
    }
    m_profileProcByProfileId.insert(profileId, p);

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
        return m_profileProcByProfileId.contains(profileId);
      };
      cdpCallbacks.replaceClient = [this, profileId, cleanupCdp](CdpClient* cdp) {
        cleanupCdp(profileId);
        m_cdpByProfileId.insert(profileId, cdp);
      };
      cdpCallbacks.logLine = sendLogLine;
      SystemChromeEngine::attachCdpWhenReady(cdpOptions, this, cdpCallbacks);
    }
    return;
  }

  if (type == QStringLiteral("proxy_pool.test")) {
    QPointer<FramedJsonSocket> peerPtr(m_peer);
    ProxyTestRunner::start(ProxyTestRunner::parseRequest(obj, ProxyTestRunner::Scope::Pool), this,
                           [peerPtr](const QJsonObject& result) {
                             if (peerPtr) {
                               peerPtr->send(result);
                             }
                           });
    return;
  }

  if (type == QStringLiteral("proxy.test")) {
    QPointer<FramedJsonSocket> peerPtr(m_peer);
    ProxyTestRunner::start(ProxyTestRunner::parseRequest(obj, ProxyTestRunner::Scope::Profile), this,
                           [peerPtr](const QJsonObject& result) {
                             if (peerPtr) {
                               peerPtr->send(result);
                             }
                           });
    return;
  }

  if (type == QStringLiteral("vpn.openvpn.start")) {
    const QString profileId = obj.value(QStringLiteral("profile_id")).toString();
    const QString exe = obj.value(QStringLiteral("exe")).toString(QStringLiteral("openvpn"));
    const QString config = obj.value(QStringLiteral("config")).toString();
    const QJsonObject socks = obj.value(QStringLiteral("socks")).toObject();
    const bool socksEnabled = socks.value(QStringLiteral("enabled")).toBool(false);
    const QString socksHost = socks.value(QStringLiteral("host")).toString();
    const int socksPort = socks.value(QStringLiteral("port")).toInt(0);
    const QString socksUser = socks.value(QStringLiteral("username")).toString();
    const QString socksPass = socks.value(QStringLiteral("password")).toString();

    auto sendStatus = [this, profileId](const QString& status, const QString& error) {
      if (!m_peer) {
        return;
      }
      QJsonObject msg;
      msg.insert(QStringLiteral("type"), QStringLiteral("vpn.status"));
      msg.insert(QStringLiteral("profile_id"), profileId);
      msg.insert(QStringLiteral("status"), status);
      msg.insert(QStringLiteral("error"), error);
      m_peer->send(msg);
    };

    if (profileId.isEmpty()) {
      sendStatus(QStringLiteral("error"), QStringLiteral("missing_profile_id"));
      return;
    }

    QProcess* existing = m_openvpnByProfileId.value(profileId);
    if (existing && existing->state() != QProcess::NotRunning) {
      sendStatus(QStringLiteral("running"), QString());
      return;
    }

    if (config.isEmpty()) {
      sendStatus(QStringLiteral("error"), QStringLiteral("missing_config"));
      return;
    }

    QString socksAuthFile;
    if (socksEnabled) {
      if (socksHost.isEmpty() || socksPort <= 0) {
        sendStatus(QStringLiteral("error"), QStringLiteral("invalid_socks_proxy"));
        return;
      }

      if (!socksUser.isEmpty() || !socksPass.isEmpty()) {
        QTemporaryFile tf;
        tf.setAutoRemove(false);
        if (!tf.open()) {
          sendStatus(QStringLiteral("error"), QStringLiteral("socks_authfile_open_failed"));
          return;
        }
        tf.write(socksUser.toUtf8());
        tf.write("\n");
        tf.write(socksPass.toUtf8());
        tf.write("\n");
        tf.flush();
        tf.close();
        socksAuthFile = tf.fileName();
        m_openvpnSocksAuthFileByProfileId.insert(profileId, socksAuthFile);
      }
    }

    auto* p = new QProcess(this);
    m_openvpnByProfileId.insert(profileId, p);

    QStringList args;
    args << QStringLiteral("--config") << config;
    if (socksEnabled) {
      args << QStringLiteral("--socks-proxy") << socksHost << QString::number(socksPort);
      if (!socksAuthFile.isEmpty()) {
        args << socksAuthFile;
      }
    }

    p->setProgram(exe.isEmpty() ? QStringLiteral("openvpn") : exe);
    p->setArguments(args);

    const QString shortId = profileId.left(8);
    QObject::connect(p, &QProcess::started, this, [this, profileId, shortId, sendStatus]() mutable {
      sendStatus(QStringLiteral("running"), QString());
      if (m_peer) {
        QJsonObject log;
        log.insert(QStringLiteral("type"), QStringLiteral("log.line"));
        log.insert(QStringLiteral("message"), QStringLiteral("openvpn[%1] started").arg(shortId));
        m_peer->send(log);
      }
    });

    QObject::connect(p, &QProcess::readyReadStandardOutput, this, [this, p, shortId]() {
      if (!m_peer) {
        return;
      }
      const auto lines = QString::fromUtf8(p->readAllStandardOutput()).split('\n', Qt::SkipEmptyParts);
      for (const auto& line : lines) {
        QJsonObject log;
        log.insert(QStringLiteral("type"), QStringLiteral("log.line"));
        log.insert(QStringLiteral("message"), QStringLiteral("openvpn[%1] %2").arg(shortId, line));
        m_peer->send(log);
      }
    });

    QObject::connect(p, &QProcess::readyReadStandardError, this, [this, p, shortId]() {
      if (!m_peer) {
        return;
      }
      const auto lines = QString::fromUtf8(p->readAllStandardError()).split('\n', Qt::SkipEmptyParts);
      for (const auto& line : lines) {
        QJsonObject log;
        log.insert(QStringLiteral("type"), QStringLiteral("log.line"));
        log.insert(QStringLiteral("message"), QStringLiteral("openvpn[%1] %2").arg(shortId, line));
        m_peer->send(log);
      }
    });

    QObject::connect(p, &QProcess::errorOccurred, this, [this, profileId, shortId, sendStatus](QProcess::ProcessError) mutable {
      sendStatus(QStringLiteral("error"), QStringLiteral("openvpn_process_error"));
      if (m_peer) {
        QJsonObject log;
        log.insert(QStringLiteral("type"), QStringLiteral("log.line"));
        log.insert(QStringLiteral("message"), QStringLiteral("openvpn[%1] error").arg(shortId));
        m_peer->send(log);
      }
    });

    QObject::connect(p, &QProcess::finished, this, [this, profileId, shortId, sendStatus](int exitCode, QProcess::ExitStatus st) mutable {
      m_openvpnByProfileId.remove(profileId);
      sendStatus(st == QProcess::NormalExit ? QStringLiteral("stopped") : QStringLiteral("crashed"),
                 QStringLiteral("exitCode=%1").arg(exitCode));

      const QString authFile = m_openvpnSocksAuthFileByProfileId.take(profileId);
      if (!authFile.isEmpty()) {
        QFile::remove(authFile);
      }

      if (m_peer) {
        QJsonObject log;
        log.insert(QStringLiteral("type"), QStringLiteral("log.line"));
        log.insert(QStringLiteral("message"), QStringLiteral("openvpn[%1] finished").arg(shortId));
        m_peer->send(log);
      }
    });

    p->start();
    return;
  }

  if (type == QStringLiteral("vpn.openvpn.stop")) {
    const QString profileId = obj.value(QStringLiteral("profile_id")).toString();
    auto sendStatus = [this, profileId](const QString& status, const QString& error) {
      if (!m_peer) {
        return;
      }
      QJsonObject msg;
      msg.insert(QStringLiteral("type"), QStringLiteral("vpn.status"));
      msg.insert(QStringLiteral("profile_id"), profileId);
      msg.insert(QStringLiteral("status"), status);
      msg.insert(QStringLiteral("error"), error);
      m_peer->send(msg);
    };

    if (profileId.isEmpty()) {
      sendStatus(QStringLiteral("error"), QStringLiteral("missing_profile_id"));
      return;
    }

    QProcess* p = m_openvpnByProfileId.value(profileId);
    if (!p) {
      sendStatus(QStringLiteral("stopped"), QString());
      return;
    }

    p->terminate();
    QPointer<QProcess> pp(p);
    QTimer::singleShot(1500, this, [pp]() {
      if (pp && pp->state() != QProcess::NotRunning) {
        pp->kill();
      }
    });

    sendStatus(QStringLiteral("stopping"), QString());
    return;
  }
}
