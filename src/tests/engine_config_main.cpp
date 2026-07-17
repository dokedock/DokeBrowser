#include "agent/core/DokeChromiumEngine.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QTemporaryDir>
#include <QtGlobal>

namespace {
bool expect(bool condition, const char* message) {
  if (!condition) {
    qCritical("%s", message);
    return false;
  }
  return true;
}

QString writeExecutable(QTemporaryDir& tempDir, const QString& name) {
  const QString path = QDir(tempDir.path()).filePath(name);
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return {};
  }
  file.write("#!/bin/sh\nexit 0\n");
  file.close();
  QFile::setPermissions(path,
                        QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadGroup | QFile::ExeGroup);
  return path;
}

QString configJson(const QString& executable) {
  QJsonObject features;
  features.insert(QStringLiteral("native_fingerprint"), true);
  features.insert(QStringLiteral("native_proxy"), true);
  features.insert(QStringLiteral("native_geoip"), true);
  features.insert(QStringLiteral("native_humanize"), true);

  QJsonObject root;
  root.insert(QStringLiteral("executable"), executable);
  root.insert(QStringLiteral("extra_args"),
              QJsonArray{QStringLiteral("--doke-a=1"), QStringLiteral("--doke-b"), QStringLiteral("   ")});
  root.insert(QStringLiteral("features"), features);
  return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

bool testParseConfig(const QString& executable, const QString& json) {
  const DokeChromiumEngine::Config config = DokeChromiumEngine::parseConfig(json);
  bool ok = true;
  ok &= expect(config.executable == executable, "config executable was not parsed");
  ok &= expect(config.extraArgs == QStringList({QStringLiteral("--doke-a=1"), QStringLiteral("--doke-b")}),
               "config extra args were not parsed or trimmed");
  ok &= expect(config.nativeFingerprint, "native_fingerprint was not parsed");
  ok &= expect(config.nativeProxy, "native_proxy was not parsed");
  ok &= expect(config.nativeGeoip, "native_geoip was not parsed");
  ok &= expect(config.nativeHumanize, "native_humanize was not parsed");
  return ok;
}

bool testExecutableResolution(const QString& executable, const QString& json) {
  bool ok = true;
  ok &= expect(DokeChromiumEngine::resolveExecutable(json) == executable,
               "per-profile executable did not win resolution");

  QJsonObject fallbackRoot;
  fallbackRoot.insert(QStringLiteral("binary_path"), executable);
  const QString fallbackJson = QString::fromUtf8(QJsonDocument(fallbackRoot).toJson(QJsonDocument::Compact));
  const DokeChromiumEngine::Config fallbackConfig = DokeChromiumEngine::parseConfig(fallbackJson);
  ok &= expect(fallbackConfig.executable == executable, "binary_path fallback was not parsed");
  ok &= expect(DokeChromiumEngine::resolveExecutable(fallbackJson) == executable,
               "binary_path fallback did not resolve");
  return ok;
}

bool testArgumentOrdering(const QString& json, const QString& profileDir) {
  DokeChromiumEngine::LaunchOptions options;
  options.engineConfigJson = json;
  options.chromium.userDataDir = profileDir;
  options.chromium.url = QStringLiteral("https://example.test/");
  options.chromium.proxyArg = QStringLiteral("--proxy-server=http://127.0.0.1:18080");
  options.chromium.debugPort = 9222;
  options.chromium.language = QStringLiteral("en-US");

  const QStringList args = DokeChromiumEngine::buildArguments(options, false);
  const int extraA = args.indexOf(QStringLiteral("--doke-a=1"));
  const int extraB = args.indexOf(QStringLiteral("--doke-b"));
  const int url = args.indexOf(QStringLiteral("https://example.test/"));

  bool ok = true;
  ok &= expect(extraA >= 0 && extraB >= 0, "extra args were not added");
  ok &= expect(url == args.size() - 1, "final URL is not the final argument");
  ok &= expect(extraA < url && extraB < url, "extra args must be inserted before the final URL");
  ok &= expect(args.contains(QStringLiteral("--proxy-server=http://127.0.0.1:18080")),
               "system chrome args were not preserved");
  ok &= expect(args.contains(QStringLiteral("--remote-debugging-port=9222")), "debug port arg was not preserved");

  DokeChromiumEngine::LaunchOptions blankOptions;
  blankOptions.chromium.userDataDir = profileDir;
  const QStringList blankArgs = DokeChromiumEngine::buildArguments(blankOptions, false);
  ok &= expect(!blankArgs.isEmpty() && blankArgs.last() == QStringLiteral("about:blank"),
               "empty start URL should resolve to about:blank");
  return ok;
}
} // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  QTemporaryDir tempDir;
  if (!tempDir.isValid()) {
    qCritical("failed to create temporary directory");
    return 1;
  }

  const QString executable = writeExecutable(tempDir, QStringLiteral("doke_chromium"));
  if (executable.isEmpty()) {
    qCritical("failed to create temporary executable");
    return 1;
  }

  const QString json = configJson(executable);
  bool ok = true;
  ok &= testParseConfig(executable, json);
  ok &= testExecutableResolution(executable, json);
  ok &= testArgumentOrdering(json, QDir(tempDir.path()).filePath(QStringLiteral("profile")));

  if (!ok) {
    return 1;
  }

  qInfo("engine_config_ok");
  return 0;
}
