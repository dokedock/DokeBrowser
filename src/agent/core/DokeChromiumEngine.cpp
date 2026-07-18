#include "DokeChromiumEngine.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>

namespace {
struct CommandResult {
  bool started = false;
  bool finished = false;
  bool normalExit = false;
  int exitCode = -1;
  QByteArray stdoutData;
  QByteArray stderrData;
};

bool isUsableExecutable(const QString& path) {
  const QFileInfo info(path);
  return info.exists() && info.isFile() && info.isExecutable();
}

QString executableErrorFor(const QString& path) {
  const QFileInfo info(path);
  if (!info.exists()) {
    return QStringLiteral("doke_chromium_path_missing");
  }
  if (!info.isFile()) {
    return QStringLiteral("doke_chromium_path_not_file");
  }
  if (!info.isExecutable()) {
    return QStringLiteral("doke_chromium_path_not_executable");
  }
  return QStringLiteral("doke_chromium_not_found");
}

QStringList capabilitiesFor(const DokeChromiumEngine::Config& config) {
  QStringList out;
  if (config.nativeFingerprint) {
    out << QStringLiteral("native_fingerprint");
  }
  if (config.nativeProxy) {
    out << QStringLiteral("native_proxy");
  }
  if (config.nativeGeoip) {
    out << QStringLiteral("native_geoip");
  }
  if (config.nativeHumanize) {
    out << QStringLiteral("native_humanize");
  }
  return out;
}

QString firstOutputLine(const QByteArray& stdoutData, const QByteArray& stderrData) {
  QString text = QString::fromUtf8(stdoutData).trimmed();
  if (text.isEmpty()) {
    text = QString::fromUtf8(stderrData).trimmed();
  }
  const QStringList lines = text.split('\n', Qt::SkipEmptyParts);
  if (lines.isEmpty()) {
    return {};
  }
  QString line = lines.first().trimmed();
  if (line.size() > 240) {
    line = line.left(240);
  }
  return line;
}

QByteArray mergedOutput(const CommandResult& result) {
  QByteArray out = result.stdoutData.trimmed();
  if (out.isEmpty()) {
    out = result.stderrData.trimmed();
  }
  return out;
}

CommandResult runShortCommand(const QString& executable, const QStringList& arguments, int timeoutMs) {
  CommandResult result;
  QProcess process;
  process.setProgram(executable);
  process.setArguments(arguments);
  process.start();
  result.started = process.waitForStarted(1000);
  if (!result.started) {
    return result;
  }
  result.finished = process.waitForFinished(timeoutMs);
  if (!result.finished) {
    process.kill();
    process.waitForFinished(1000);
    result.stdoutData = process.readAllStandardOutput();
    result.stderrData = process.readAllStandardError();
    return result;
  }
  result.normalExit = process.exitStatus() == QProcess::NormalExit;
  result.exitCode = process.exitCode();
  result.stdoutData = process.readAllStandardOutput();
  result.stderrData = process.readAllStandardError();
  return result;
}

QString commandError(const CommandResult& result, const QString& prefix) {
  if (!result.started) {
    return QStringLiteral("%1_start_failed").arg(prefix);
  }
  if (!result.finished) {
    return QStringLiteral("%1_timeout").arg(prefix);
  }
  if (!result.normalExit) {
    return QStringLiteral("%1_crashed").arg(prefix);
  }
  if (result.exitCode != 0) {
    return QStringLiteral("%1_exit_%2").arg(prefix).arg(result.exitCode);
  }
  return {};
}

QStringList stringListFromJsonArray(const QJsonArray& values) {
  QStringList out;
  for (const auto& value : values) {
    const QString text = value.toString().trimmed();
    if (!text.isEmpty() && !out.contains(text)) {
      out << text;
    }
  }
  return out;
}

QStringList missingCapabilities(const QStringList& requested, const QStringList& supported) {
  QStringList out;
  for (const auto& capability : requested) {
    if (!supported.contains(capability) && !out.contains(capability)) {
      out << capability;
    }
  }
  return out;
}

QByteArray jsonObjectPayload(const QByteArray& raw) {
  const QByteArray trimmed = raw.trimmed();
  if (trimmed.startsWith('{') && trimmed.endsWith('}')) {
    return trimmed;
  }
  const int start = trimmed.indexOf('{');
  const int end = trimmed.lastIndexOf('}');
  if (start < 0 || end <= start) {
    return trimmed;
  }
  return trimmed.mid(start, end - start + 1);
}

void applyNativeProbeJson(DokeChromiumEngine::ProbeResult& out, const QByteArray& json) {
  QJsonParseError parseError;
  const QJsonDocument doc = QJsonDocument::fromJson(jsonObjectPayload(json), &parseError);
  if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
    out.nativeProbeError = QStringLiteral("native_probe_invalid_json");
    return;
  }

  const QJsonObject obj = doc.object();
  out.version = obj.value(QStringLiteral("version")).toString().trimmed();
  out.nativeCapabilities = stringListFromJsonArray(obj.value(QStringLiteral("capabilities")).toArray());
  if (obj.value(QStringLiteral("probe_protocol")).isDouble()) {
    out.probeProtocol = QString::number(obj.value(QStringLiteral("probe_protocol")).toInt());
  } else {
    out.probeProtocol = obj.value(QStringLiteral("probe_protocol")).toString().trimmed();
  }
}
} // namespace

QString DokeChromiumEngine::id() const {
  return QStringLiteral("doke_chromium");
}

QString DokeChromiumEngine::executablePath() const {
  return resolveExecutable();
}

bool DokeChromiumEngine::isAvailable() const {
  return !resolveExecutable().isEmpty();
}

DokeChromiumEngine::ResolveResult DokeChromiumEngine::resolve() {
  const QString envPath = qEnvironmentVariable("DOKE_CHROMIUM_PATH").trimmed();
  if (!envPath.isEmpty()) {
    return isUsableExecutable(envPath) ? ResolveResult{envPath, QString()}
                                      : ResolveResult{QString(), executableErrorFor(envPath)};
  }

  const QStringList names = {
      QStringLiteral("doke-chromium"),
      QStringLiteral("doke_chromium"),
      QStringLiteral("dokebrowser-chromium"),
  };
  for (const auto& n : names) {
    const QString p = QStandardPaths::findExecutable(n);
    if (!p.isEmpty()) {
      return {p, QString()};
    }
  }
  return {QString(), QStringLiteral("doke_chromium_not_found")};
}

DokeChromiumEngine::ResolveResult DokeChromiumEngine::resolve(const QString& engineConfigJson) {
  const Config config = parseConfig(engineConfigJson);
  if (!config.executable.isEmpty()) {
    return isUsableExecutable(config.executable) ? ResolveResult{config.executable, QString()}
                                                : ResolveResult{QString(), executableErrorFor(config.executable)};
  }
  return resolve();
}

QString DokeChromiumEngine::resolveExecutable() {
  return resolve().executable;
}

QString DokeChromiumEngine::resolveExecutable(const QString& engineConfigJson) {
  const ResolveResult result = resolve(engineConfigJson);
  return result.executable;
}

DokeChromiumEngine::ProbeResult DokeChromiumEngine::probe(const QString& engineConfigJson, int probeTimeoutMs) {
  ProbeResult out;
  const Config config = parseConfig(engineConfigJson);
  out.capabilities = capabilitiesFor(config);
  out.resolution = resolve(engineConfigJson);
  if (out.resolution.executable.isEmpty()) {
    return out;
  }

  const CommandResult nativeProbe =
      runShortCommand(out.resolution.executable, QStringList{QStringLiteral("--doke-probe")}, probeTimeoutMs);
  out.nativeProbeError = commandError(nativeProbe, QStringLiteral("native_probe"));
  if (out.nativeProbeError.isEmpty()) {
    const QByteArray nativeJson = mergedOutput(nativeProbe);
    if (nativeJson.isEmpty()) {
      out.nativeProbeError = QStringLiteral("native_probe_empty");
    } else {
      applyNativeProbeJson(out, nativeJson);
    }
  }
  if (out.nativeProbeError.isEmpty()) {
    out.missingNativeCapabilities = missingCapabilities(out.capabilities, out.nativeCapabilities);
  }

  if (!out.version.isEmpty()) {
    return out;
  }

  const CommandResult versionProbe =
      runShortCommand(out.resolution.executable, QStringList{QStringLiteral("--version")}, probeTimeoutMs);
  out.versionError = commandError(versionProbe, QStringLiteral("version"));
  if (!out.versionError.isEmpty()) {
    return out;
  }
  out.version = firstOutputLine(versionProbe.stdoutData, versionProbe.stderrData);
  if (out.version.isEmpty()) {
    out.versionError = QStringLiteral("version_empty");
  }
  return out;
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

  if (!options.runtimeConfigPath.isEmpty() || !config.extraArgs.isEmpty()) {
    const QString startUrl = args.isEmpty() ? QString() : args.takeLast();
    if (!options.runtimeConfigPath.isEmpty()) {
      args << QStringLiteral("--doke-runtime-config=%1").arg(options.runtimeConfigPath);
    }
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
