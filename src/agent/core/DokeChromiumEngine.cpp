#include "DokeChromiumEngine.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QStringList>

QString DokeChromiumEngine::id() const {
  return QStringLiteral("doke_chromium");
}

QString DokeChromiumEngine::executablePath() const {
  return resolveExecutable();
}

bool DokeChromiumEngine::isAvailable() const {
  return !resolveExecutable().isEmpty();
}

QString DokeChromiumEngine::resolveExecutable() {
  const QString envPath = qEnvironmentVariable("DOKE_CHROMIUM_PATH").trimmed();
  if (!envPath.isEmpty() && QFileInfo::exists(envPath)) {
    return envPath;
  }

  const QStringList names = {
      QStringLiteral("doke-chromium"),
      QStringLiteral("doke_chromium"),
      QStringLiteral("dokebrowser-chromium"),
  };
  for (const auto& n : names) {
    const QString p = QStandardPaths::findExecutable(n);
    if (!p.isEmpty()) {
      return p;
    }
  }
  return {};
}

QString DokeChromiumEngine::resolveExecutable(const QString& engineConfigJson) {
  const Config config = parseConfig(engineConfigJson);
  if (!config.executable.isEmpty() && QFileInfo::exists(config.executable)) {
    return config.executable;
  }
  return resolveExecutable();
}

DokeChromiumEngine::Config DokeChromiumEngine::parseConfig(const QString& engineConfigJson) {
  Config config;
  const QJsonDocument doc = QJsonDocument::fromJson(engineConfigJson.toUtf8());
  if (doc.isObject()) {
    const QJsonObject obj = doc.object();
    config.executable = obj.value(QStringLiteral("executable")).toString().trimmed();
    if (config.executable.isEmpty()) {
      config.executable = obj.value(QStringLiteral("binary_path")).toString().trimmed();
    }

    const QJsonArray extraArgValues = obj.value(QStringLiteral("extra_args")).toArray();
    for (const auto& value : extraArgValues) {
      const QString arg = value.toString().trimmed();
      if (!arg.isEmpty()) {
        config.extraArgs << arg;
      }
    }

    const QJsonObject features = obj.value(QStringLiteral("features")).toObject();
    config.nativeFingerprint = features.value(QStringLiteral("native_fingerprint")).toBool(false);
    config.nativeProxy = features.value(QStringLiteral("native_proxy")).toBool(false);
    config.nativeGeoip = features.value(QStringLiteral("native_geoip")).toBool(false);
    config.nativeHumanize = features.value(QStringLiteral("native_humanize")).toBool(false);
  }
  return config;
}

QStringList DokeChromiumEngine::buildArguments(const LaunchOptions& options, bool compat) {
  QStringList args = SystemChromeEngine::buildArguments(options.chromium, compat);
  const Config config = parseConfig(options.engineConfigJson);

  if (!config.extraArgs.isEmpty()) {
    const QString startUrl = args.isEmpty() ? QString() : args.takeLast();
    args << config.extraArgs;
    if (!startUrl.isEmpty()) {
      args << startUrl;
    }
  }

  return args;
}

QProcess* DokeChromiumEngine::launchProcess(const SystemChromeEngine::ProcessLaunchOptions& options, QObject* owner,
                                            SystemChromeEngine::ProcessCallbacks callbacks) {
  SystemChromeEngine::ProcessLaunchOptions dokeOptions = options;
  dokeOptions.processLabel = QStringLiteral("doke_chromium");
  return SystemChromeEngine::launchProcess(dokeOptions, owner, callbacks);
}
