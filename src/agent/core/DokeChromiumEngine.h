#pragma once

#include "BrowserEngine.h"
#include "SystemChromeEngine.h"

class DokeChromiumEngine final : public BrowserEngine {
public:
  struct Config {
    QString executable;
    QStringList extraArgs;
    bool nativeFingerprint = false;
    bool nativeProxy = false;
    bool nativeGeoip = false;
    bool nativeHumanize = false;
  };

  struct LaunchOptions {
    SystemChromeEngine::LaunchOptions chromium;
    QString engineConfigJson;
    QString runtimeConfigPath;
    bool humanize = false;
    bool geoip = false;
  };

  struct ResolveResult {
    QString executable;
    QString error;
  };

  struct ProbeResult {
    ResolveResult resolution;
    QString version;
    QString versionError;
    QString nativeProbeError;
    QString probeProtocol;
    QStringList capabilities;
    QStringList nativeCapabilities;
    QStringList missingNativeCapabilities;
  };

  QString id() const override;
  QString executablePath() const override;
  bool isAvailable() const override;

  static ResolveResult resolve();
  static ResolveResult resolve(const QString& engineConfigJson);
  static ProbeResult probe(const QString& engineConfigJson, int probeTimeoutMs = 1500);
  static QString resolveExecutable();
  static QString resolveExecutable(const QString& engineConfigJson);
  static Config parseConfig(const QString& engineConfigJson);
  static QStringList buildArguments(const LaunchOptions& options, bool compat);
  static QProcess* launchProcess(const SystemChromeEngine::ProcessLaunchOptions& options, QObject* owner,
                                 SystemChromeEngine::ProcessCallbacks callbacks);
};
