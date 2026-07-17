#pragma once

#include "BrowserEngine.h"
#include "CdpClient.h"

#include <QStringList>
#include <functional>

class QObject;
class QProcess;

class SystemChromeEngine final : public BrowserEngine {
public:
  struct LaunchOptions {
    QString userDataDir;
    QString proxyArg;
    QString extensionDir;
    QString url;
    QString language;
    QString userAgent;
    QString timezone;
    QString windowSizeArg;
    bool touchEnabled = false;
    int debugPort = 0;
  };

  struct ExtensionOptions {
    QString profileId;
    quint32 fingerprintSeed = 0;
    QString fingerprintMode;
    QString language;
    QString userAgent;
    QString platform;
    int hardwareConcurrency = 0;
    int deviceMemoryGb = 0;
    double deviceScaleFactor = 0;
    QString timezone;
    QString resolution;
    bool touchEnabled = false;
    bool geoEnabled = false;
    double geoLatitude = 0;
    double geoLongitude = 0;
    double geoAccuracy = 0;
    QString proxyScheme;
    QString proxyHost;
    int proxyPort = 0;
    QString proxyUsername;
    QString proxyPassword;
    bool enableProxyAuth = false;
  };

  struct CdpAttachOptions {
    QString profileId;
    QString initialUrl;
    int debugPort = 0;
    int maxAttempts = 25;
    int retryDelayMs = 200;
    CdpClient::Fingerprint fingerprint;
  };

  struct CdpAttachCallbacks {
    std::function<bool()> isActive;
    std::function<void(CdpClient*)> replaceClient;
    std::function<void(const QString&)> logLine;
  };

  struct ProcessLaunchOptions {
    QString profileId;
    QString processLabel = QStringLiteral("chrome");
    QString executable;
    QStringList arguments;
    QStringList compatArguments;
    QString timezone;
    bool compatRequested = false;
    bool compatInitial = false;
  };

  struct ProcessCallbacks {
    std::function<bool(QProcess*)> isCurrentProcess;
    std::function<bool()> isStopRequested;
    std::function<bool()> consumeExpectedStop;
    std::function<void()> clearCurrentProcess;
    std::function<void()> cleanup;
    std::function<void(const QString&, const QString&)> status;
    std::function<void(const QString&)> logLine;
  };

  QString id() const override;
  QString executablePath() const override;
  bool isAvailable() const override;

  static QString resolveExecutable();
  static QStringList buildArguments(const LaunchOptions& options, bool compat);
  static QString createProfileExtension(const ExtensionOptions& options);
  static void attachCdpWhenReady(const CdpAttachOptions& options, QObject* owner, CdpAttachCallbacks callbacks);
  static QProcess* launchProcess(const ProcessLaunchOptions& options, QObject* owner, ProcessCallbacks callbacks);
};
