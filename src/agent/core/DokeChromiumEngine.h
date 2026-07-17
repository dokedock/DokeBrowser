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
    bool humanize = false;
    bool geoip = false;
  };

  QString id() const override;
  QString executablePath() const override;
  bool isAvailable() const override;

  static QString resolveExecutable();
  static QString resolveExecutable(const QString& engineConfigJson);
  static Config parseConfig(const QString& engineConfigJson);
  static QStringList buildArguments(const LaunchOptions& options, bool compat);
  static QProcess* launchProcess(const SystemChromeEngine::ProcessLaunchOptions& options, QObject* owner,
                                 SystemChromeEngine::ProcessCallbacks callbacks);
};
