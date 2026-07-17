#include "OpenVpnManager.h"

#include <QFile>
#include <QPointer>
#include <QProcess>
#include <QTemporaryFile>
#include <QTimer>

OpenVpnManager::OpenVpnManager(QObject* parent) : QObject(parent) {}

OpenVpnManager::~OpenVpnManager() {
  const auto keys = m_processByProfileId.keys();
  for (const auto& k : keys) {
    QProcess* p = m_processByProfileId.take(k);
    if (p) {
      p->kill();
      p->deleteLater();
    }
  }

  const auto authKeys = m_socksAuthFileByProfileId.keys();
  for (const auto& k : authKeys) {
    cleanupAuthFile(k);
  }
}

OpenVpnManager::StartRequest OpenVpnManager::parseStartRequest(const QJsonObject& obj) {
  StartRequest request;
  request.profileId = obj.value(QStringLiteral("profile_id")).toString();
  request.exe = obj.value(QStringLiteral("exe")).toString(QStringLiteral("openvpn"));
  request.config = obj.value(QStringLiteral("config")).toString();

  const QJsonObject socks = obj.value(QStringLiteral("socks")).toObject();
  request.socksEnabled = socks.value(QStringLiteral("enabled")).toBool(false);
  request.socksHost = socks.value(QStringLiteral("host")).toString();
  request.socksPort = socks.value(QStringLiteral("port")).toInt(0);
  request.socksUser = socks.value(QStringLiteral("username")).toString();
  request.socksPass = socks.value(QStringLiteral("password")).toString();
  return request;
}

QString OpenVpnManager::validationError(const StartRequest& request) {
  if (request.profileId.isEmpty()) {
    return QStringLiteral("missing_profile_id");
  }
  if (request.config.isEmpty()) {
    return QStringLiteral("missing_config");
  }
  if (request.socksEnabled && (request.socksHost.isEmpty() || request.socksPort <= 0)) {
    return QStringLiteral("invalid_socks_proxy");
  }
  return {};
}

QStringList OpenVpnManager::buildArguments(const StartRequest& request, const QString& socksAuthFile) {
  QStringList args;
  args << QStringLiteral("--config") << request.config;
  if (request.socksEnabled) {
    args << QStringLiteral("--socks-proxy") << request.socksHost << QString::number(request.socksPort);
    if (!socksAuthFile.isEmpty()) {
      args << socksAuthFile;
    }
  }
  return args;
}

void OpenVpnManager::startOpenVpn(const QJsonObject& obj, StatusCallback status, LogCallback log) {
  const StartRequest request = parseStartRequest(obj);
  auto sendStatus = [status, request](const QString& vpnStatus, const QString& error) {
    if (status) {
      status(request.profileId, vpnStatus, error);
    }
  };

  const QString error = validationError(request);
  if (!error.isEmpty()) {
    sendStatus(QStringLiteral("error"), error);
    return;
  }

  QProcess* existing = m_processByProfileId.value(request.profileId);
  if (existing && existing->state() != QProcess::NotRunning) {
    sendStatus(QStringLiteral("running"), QString());
    return;
  }
  if (existing) {
    m_processByProfileId.remove(request.profileId);
    existing->deleteLater();
  }

  QString socksAuthFile;
  if (request.socksEnabled && (!request.socksUser.isEmpty() || !request.socksPass.isEmpty())) {
    QTemporaryFile tf;
    tf.setAutoRemove(false);
    if (!tf.open()) {
      sendStatus(QStringLiteral("error"), QStringLiteral("socks_authfile_open_failed"));
      return;
    }
    tf.write(request.socksUser.toUtf8());
    tf.write("\n");
    tf.write(request.socksPass.toUtf8());
    tf.write("\n");
    tf.flush();
    tf.close();
    socksAuthFile = tf.fileName();
    m_socksAuthFileByProfileId.insert(request.profileId, socksAuthFile);
  }

  auto* p = new QProcess(this);
  m_processByProfileId.insert(request.profileId, p);

  p->setProgram(request.exe.isEmpty() ? QStringLiteral("openvpn") : request.exe);
  p->setArguments(buildArguments(request, socksAuthFile));

  const QString shortId = request.profileId.left(8);
  QObject::connect(p, &QProcess::started, this, [sendStatus, log, shortId]() mutable {
    sendStatus(QStringLiteral("running"), QString());
    if (log) {
      log(QStringLiteral("openvpn[%1] started").arg(shortId));
    }
  });

  QObject::connect(p, &QProcess::readyReadStandardOutput, this, [p, log, shortId]() {
    if (!log) {
      return;
    }
    const auto lines = QString::fromUtf8(p->readAllStandardOutput()).split('\n', Qt::SkipEmptyParts);
    for (const auto& line : lines) {
      log(QStringLiteral("openvpn[%1] %2").arg(shortId, line));
    }
  });

  QObject::connect(p, &QProcess::readyReadStandardError, this, [p, log, shortId]() {
    if (!log) {
      return;
    }
    const auto lines = QString::fromUtf8(p->readAllStandardError()).split('\n', Qt::SkipEmptyParts);
    for (const auto& line : lines) {
      log(QStringLiteral("openvpn[%1] %2").arg(shortId, line));
    }
  });

  QObject::connect(p, &QProcess::errorOccurred, this, [sendStatus, log, shortId](QProcess::ProcessError) mutable {
    sendStatus(QStringLiteral("error"), QStringLiteral("openvpn_process_error"));
    if (log) {
      log(QStringLiteral("openvpn[%1] error").arg(shortId));
    }
  });

  QObject::connect(p, &QProcess::finished, this,
                   [this, request, shortId, sendStatus, log](int exitCode, QProcess::ExitStatus st) mutable {
                     m_processByProfileId.remove(request.profileId);
                     sendStatus(st == QProcess::NormalExit ? QStringLiteral("stopped") : QStringLiteral("crashed"),
                                QStringLiteral("exitCode=%1").arg(exitCode));
                     cleanupAuthFile(request.profileId);

                     if (log) {
                       log(QStringLiteral("openvpn[%1] finished").arg(shortId));
                     }
                   });

  p->start();
}

void OpenVpnManager::stopOpenVpn(const QJsonObject& obj, StatusCallback status) {
  const QString profileId = obj.value(QStringLiteral("profile_id")).toString();
  auto sendStatus = [status, profileId](const QString& vpnStatus, const QString& error) {
    if (status) {
      status(profileId, vpnStatus, error);
    }
  };

  if (profileId.isEmpty()) {
    sendStatus(QStringLiteral("error"), QStringLiteral("missing_profile_id"));
    return;
  }

  QProcess* p = m_processByProfileId.value(profileId);
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
}

void OpenVpnManager::cleanupAuthFile(const QString& profileId) {
  const QString path = m_socksAuthFileByProfileId.take(profileId);
  if (!path.isEmpty()) {
    QFile::remove(path);
  }
}
