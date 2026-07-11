#include "shared/ipc/FramedJsonSocket.h"
#include "shared/ipc/IpcNames.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QProcess>
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
