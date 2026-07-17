#include "shared/ipc/FramedJsonSocket.h"
#include "shared/ipc/IpcNames.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>

namespace {
struct WaitResult {
  bool ok = false;
  QJsonObject obj;
};

WaitResult waitForType(FramedJsonSocket* framed, const QString& type, int timeoutMs) {
  WaitResult out;
  QEventLoop loop;
  QTimer timer;
  timer.setSingleShot(true);
  QObject::connect(&timer, &QTimer::timeout, &loop, [&]() { loop.quit(); });

  QMetaObject::Connection c = QObject::connect(framed, &FramedJsonSocket::jsonReceived, &loop, [&](const QJsonObject& obj) {
    if (obj.value(QStringLiteral("type")).toString() == type) {
      out.ok = true;
      out.obj = obj;
      loop.quit();
    }
  });

  timer.start(timeoutMs);
  loop.exec();
  QObject::disconnect(c);
  return out;
}

WaitResult waitForProfileStatus(FramedJsonSocket* framed, const QString& profileId, const QStringList& statuses, int timeoutMs) {
  WaitResult out;
  QEventLoop loop;
  QTimer timer;
  timer.setSingleShot(true);
  QObject::connect(&timer, &QTimer::timeout, &loop, [&]() { loop.quit(); });

  QMetaObject::Connection c = QObject::connect(framed, &FramedJsonSocket::jsonReceived, &loop, [&](const QJsonObject& obj) {
    if (obj.value(QStringLiteral("type")).toString() != QStringLiteral("profile.status")) {
      return;
    }
    if (obj.value(QStringLiteral("profile_id")).toString() != profileId) {
      return;
    }
    if (!statuses.contains(obj.value(QStringLiteral("status")).toString())) {
      return;
    }
    out.ok = true;
    out.obj = obj;
    loop.quit();
  });

  timer.start(timeoutMs);
  loop.exec();
  QObject::disconnect(c);
  return out;
}

QString agentPathFromTestExe() {
  const QDir d(QCoreApplication::applicationDirPath());
#if defined(Q_OS_WIN)
  const QString exe = QStringLiteral("dokebrowser_agent.exe");
#else
  const QString exe = QStringLiteral("dokebrowser_agent");
#endif
  const QString candidate1 = d.filePath(QStringLiteral("../agent/%1").arg(exe));
  if (QFile::exists(candidate1)) {
    return candidate1;
  }
  const QString candidate2 = d.filePath(exe);
  if (QFile::exists(candidate2)) {
    return candidate2;
  }
  return {};
}

QString writeFakeDokeExecutable(QTemporaryDir& tempDir) {
#if defined(Q_OS_WIN)
  const QString path = QDir(tempDir.path()).filePath(QStringLiteral("doke_chromium.bat"));
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return {};
  }
  file.write("@echo off\r\ntimeout /t 30 /nobreak >nul\r\nexit /b 0\r\n");
  file.close();
  return path;
#else
  const QString path = QDir(tempDir.path()).filePath(QStringLiteral("doke_chromium"));
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return {};
  }
  file.write("#!/bin/sh\nsleep 30\nexit 0\n");
  file.close();
  QFile::setPermissions(path,
                        QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadGroup | QFile::ExeGroup);
  return path;
#endif
}
}

int main(int argc, char* argv[]) {
  QCoreApplication app(argc, argv);

  const QString agentPath = agentPathFromTestExe();
  if (agentPath.isEmpty()) {
    qCritical("agent_not_found");
    return 2;
  }

  QProcess agent;
  agent.setProgram(agentPath);
  agent.setArguments({});
  agent.start();
  if (!agent.waitForStarted(2000)) {
    qCritical("agent_start_failed");
    return 3;
  }

  auto* sock = new QLocalSocket(&app);
  const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + 5000;
  while (QDateTime::currentMSecsSinceEpoch() < deadline) {
    sock->connectToServer(QString::fromUtf8(IpcNames::kAgentServerName));
    if (sock->waitForConnected(200)) {
      break;
    }
    sock->abort();
  }

  if (sock->state() != QLocalSocket::ConnectedState) {
    agent.kill();
    agent.waitForFinished(2000);
    qCritical("ipc_connect_failed");
    return 4;
  }

  FramedJsonSocket framed(sock);

  QObject::connect(&framed, &FramedJsonSocket::ioError, &app, [&](const QString& msg) {
    qWarning() << "ipc_error" << msg;
  });

  QJsonObject hello;
  hello.insert(QStringLiteral("type"), QStringLiteral("hello"));
  hello.insert(QStringLiteral("client"), QStringLiteral("smoke"));
  framed.send(hello);

  const auto ack = waitForType(&framed, QStringLiteral("hello.ack"), 3000);
  if (!ack.ok) {
    agent.kill();
    agent.waitForFinished(2000);
    qCritical("hello_ack_timeout");
    return 5;
  }

  QJsonObject engineList;
  engineList.insert(QStringLiteral("type"), QStringLiteral("engine.list"));
  framed.send(engineList);

  const auto engines = waitForType(&framed, QStringLiteral("engine.list.result"), 3000);
  if (!engines.ok || !engines.obj.value(QStringLiteral("engines")).isArray()) {
    agent.kill();
    agent.waitForFinished(2000);
    qCritical("engine_list_timeout");
    return 7;
  }
  const QJsonArray engineArray = engines.obj.value(QStringLiteral("engines")).toArray();
  bool hasSystemChrome = false;
  bool hasDokeChromium = false;
  for (const auto& v : engineArray) {
    const QJsonObject e = v.toObject();
    const QString id = e.value(QStringLiteral("id")).toString();
    hasSystemChrome = hasSystemChrome || id == QStringLiteral("system_chrome");
    hasDokeChromium = hasDokeChromium || id == QStringLiteral("doke_chromium");
  }
  if (!hasSystemChrome || !hasDokeChromium) {
    agent.kill();
    agent.waitForFinished(2000);
    qCritical("engine_list_missing_expected_ids");
    return 8;
  }

  QTemporaryDir fakeDokeDir;
  const QString fakeDokePath = writeFakeDokeExecutable(fakeDokeDir);
  if (!fakeDokeDir.isValid() || fakeDokePath.isEmpty()) {
    agent.kill();
    agent.waitForFinished(2000);
    qCritical("fake_doke_create_failed");
    return 9;
  }

  QJsonObject availableDokeConfig;
  availableDokeConfig.insert(QStringLiteral("executable"), fakeDokePath);
  QJsonObject availableProbe;
  availableProbe.insert(QStringLiteral("type"), QStringLiteral("engine.probe"));
  availableProbe.insert(QStringLiteral("profile_id"), QStringLiteral("smoke-available"));
  availableProbe.insert(QStringLiteral("browser_engine"), QStringLiteral("doke_chromium"));
  availableProbe.insert(QStringLiteral("engine_config_json"),
                        QString::fromUtf8(QJsonDocument(availableDokeConfig).toJson(QJsonDocument::Compact)));
  framed.send(availableProbe);

  const auto availableProbeResult = waitForType(&framed, QStringLiteral("engine.probe.result"), 3000);
  if (!availableProbeResult.ok || availableProbeResult.obj.value(QStringLiteral("id")).toString() != QStringLiteral("doke_chromium")) {
    agent.kill();
    agent.waitForFinished(2000);
    qCritical("engine_probe_available_timeout");
    return 9;
  }
  if (!availableProbeResult.obj.value(QStringLiteral("available")).toBool(false)
      || availableProbeResult.obj.value(QStringLiteral("executable")).toString() != fakeDokePath) {
    agent.kill();
    agent.waitForFinished(2000);
    qCritical("engine_probe_expected_available");
    return 10;
  }

  const QString fakeDokeProfileId = QStringLiteral("smoke-doke-start");
  QJsonObject dokeStart;
  dokeStart.insert(QStringLiteral("type"), QStringLiteral("profile.start"));
  dokeStart.insert(QStringLiteral("profile_id"), fakeDokeProfileId);
  dokeStart.insert(QStringLiteral("profile_name"), QStringLiteral("Smoke Doke"));
  dokeStart.insert(QStringLiteral("data_dir"), QDir(fakeDokeDir.path()).filePath(QStringLiteral("profile-data")));
  dokeStart.insert(QStringLiteral("browser_engine"), QStringLiteral("doke_chromium"));
  dokeStart.insert(QStringLiteral("engine_config_json"),
                   QString::fromUtf8(QJsonDocument(availableDokeConfig).toJson(QJsonDocument::Compact)));
  dokeStart.insert(QStringLiteral("url"), QStringLiteral("about:blank"));
  framed.send(dokeStart);

  const auto dokeRunning =
      waitForProfileStatus(&framed, fakeDokeProfileId, QStringList{QStringLiteral("running")}, 5000);
  if (!dokeRunning.ok) {
    agent.kill();
    agent.waitForFinished(2000);
    qCritical("doke_profile_start_running_timeout");
    return 14;
  }

  QJsonObject dokeStop;
  dokeStop.insert(QStringLiteral("type"), QStringLiteral("profile.stop"));
  dokeStop.insert(QStringLiteral("profile_id"), fakeDokeProfileId);
  framed.send(dokeStop);

  const auto dokeStopping = waitForProfileStatus(
      &framed, fakeDokeProfileId, QStringList{QStringLiteral("stopping"), QStringLiteral("stopped")}, 3000);
  if (!dokeStopping.ok) {
    agent.kill();
    agent.waitForFinished(2000);
    qCritical("doke_profile_stop_timeout");
    return 15;
  }

  QJsonObject dokeConfig;
  dokeConfig.insert(QStringLiteral("executable"), QStringLiteral("/tmp/dokebrowser-missing-doke-chromium"));
  QJsonObject engineProbe;
  engineProbe.insert(QStringLiteral("type"), QStringLiteral("engine.probe"));
  engineProbe.insert(QStringLiteral("profile_id"), QStringLiteral("smoke"));
  engineProbe.insert(QStringLiteral("browser_engine"), QStringLiteral("doke_chromium"));
  engineProbe.insert(QStringLiteral("engine_config_json"),
                     QString::fromUtf8(QJsonDocument(dokeConfig).toJson(QJsonDocument::Compact)));
  framed.send(engineProbe);

  const auto probe = waitForType(&framed, QStringLiteral("engine.probe.result"), 3000);
  if (!probe.ok || probe.obj.value(QStringLiteral("id")).toString() != QStringLiteral("doke_chromium")) {
    agent.kill();
    agent.waitForFinished(2000);
    qCritical("engine_probe_timeout");
    return 11;
  }
  if (probe.obj.value(QStringLiteral("profile_id")).toString() != QStringLiteral("smoke")) {
    agent.kill();
    agent.waitForFinished(2000);
    qCritical("engine_probe_profile_id_mismatch");
    return 12;
  }
  if (probe.obj.value(QStringLiteral("available")).toBool(true)) {
    agent.kill();
    agent.waitForFinished(2000);
    qCritical("engine_probe_expected_unavailable");
    return 13;
  }

  QJsonObject proxy;
  proxy.insert(QStringLiteral("enabled"), false);
  proxy.insert(QStringLiteral("type"), QStringLiteral("direct"));

  QJsonObject proxyTest;
  proxyTest.insert(QStringLiteral("type"), QStringLiteral("proxy.test"));
  proxyTest.insert(QStringLiteral("profile_id"), QStringLiteral("smoke"));
  proxyTest.insert(QStringLiteral("proxy"), proxy);
  proxyTest.insert(QStringLiteral("url"), QStringLiteral("https://httpbin.org/ip"));
  framed.send(proxyTest);

  const auto res = waitForType(&framed, QStringLiteral("proxy.test.result"), 20000);
  if (!res.ok) {
    agent.kill();
    agent.waitForFinished(2000);
    qCritical("proxy_test_timeout");
    return 6;
  }

  const bool ok = res.obj.value(QStringLiteral("ok")).toBool(false);
  const QString ip = res.obj.value(QStringLiteral("observed_ip")).toString();
  if (!ok || ip.isEmpty()) {
    qWarning().noquote() << QJsonDocument(res.obj).toJson(QJsonDocument::Compact);
  }

  agent.terminate();
  if (!agent.waitForFinished(2000)) {
    agent.kill();
    agent.waitForFinished(2000);
  }

  qInfo("smoke_ok");
  return 0;
}
