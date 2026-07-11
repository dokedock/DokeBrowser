#include "AppController.h"

#include "IpcClient.h"
#include "LogListModel.h"
#include "ProfileListModel.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTimeZone>
#include <QUuid>

namespace {
QString ts() {
  return QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
}
}

AppController::AppController(QObject* parent) : QObject(parent) {
  m_profiles = new ProfileListModel(this);
  m_logs = new LogListModel(this);
  m_ipc = new IpcClient(this);

  QObject::connect(m_ipc, &IpcClient::logLineReceived, this, [this](const QString& line) {
    m_logs->appendLine(QStringLiteral("[%1] %2").arg(ts(), line));
  });

  QObject::connect(m_ipc, &IpcClient::connectionError, this, [this](const QString& msg) {
    m_logs->appendLine(QStringLiteral("[%1] ipc_error: %2").arg(ts(), msg));
  });

  QObject::connect(m_ipc, &IpcClient::connectedChanged, this, [this]() {
    const bool now = m_ipc->isConnected();
    if (m_ipcConnected != now) {
      m_ipcConnected = now;
      emit ipcConnectedChanged();
    }
    m_logs->appendLine(QStringLiteral("[%1] ipc_connected=%2").arg(ts(), now ? "true" : "false"));
  });

  QObject::connect(m_ipc, &IpcClient::proxyTestResultReceived, this, [this](const QJsonObject& obj) {
    const bool ok = obj.value(QStringLiteral("ok")).toBool(false);
    const QString observedIp = obj.value(QStringLiteral("observed_ip")).toString();
    const QString error = obj.value(QStringLiteral("error")).toString();
    const int statusCode = obj.value(QStringLiteral("status_code")).toInt(0);
    const int durationMs = obj.value(QStringLiteral("duration_ms")).toInt(0);

    if (ok) {
      m_proxyLastTestSummary =
          QStringLiteral("OK ip=%1 status=%2 duration=%3ms").arg(observedIp).arg(statusCode).arg(durationMs);
    } else {
      m_proxyLastTestSummary =
          QStringLiteral("FAIL status=%1 duration=%2ms error=%3").arg(statusCode).arg(durationMs).arg(error);
    }
    emit proxyLastTestChanged();
    m_logs->appendLine(QStringLiteral("[%1] proxy_test: %2").arg(ts(), m_proxyLastTestSummary));
  });

  QObject::connect(m_ipc, &IpcClient::vpnStatusReceived, this, [this](const QJsonObject& obj) {
    const QString profileId = obj.value(QStringLiteral("profile_id")).toString();
    const QString status = obj.value(QStringLiteral("status")).toString();
    if (profileId.isEmpty()) {
      return;
    }
    m_vpnStatusByProfileId.insert(profileId, status);
    if (profileId == selectedProfileId()) {
      emit selectedVpnStatusChanged();
    }
  });

  loadProfiles();
  if (m_profiles->rowCount() > 0) {
    setSelectedProfileIndex(0);
  }

  startAgent();
}

AppController::~AppController() = default;

ProfileListModel* AppController::profiles() {
  return m_profiles;
}

LogListModel* AppController::logs() {
  return m_logs;
}

int AppController::selectedProfileIndex() const {
  return m_selectedProfileIndex;
}

void AppController::setSelectedProfileIndex(int index) {
  if (m_selectedProfileIndex == index) {
    return;
  }
  m_selectedProfileIndex = index;
  emit selectedProfileIndexChanged();
  emit selectedProfileChanged();
  emit selectedVpnStatusChanged();
}

bool AppController::hasSelectedProfile() const {
  return m_selectedProfileIndex >= 0 && m_selectedProfileIndex < m_profiles->items().size();
}

ProfileListModel::ProfileItem AppController::selectedProfileItem() const {
  if (!hasSelectedProfile()) {
    return {};
  }
  return m_profiles->items().at(m_selectedProfileIndex);
}

void AppController::updateSelectedProfileItem(const ProfileListModel::ProfileItem& item) {
  if (!hasSelectedProfile()) {
    return;
  }
  m_profiles->updateAt(m_selectedProfileIndex, item);
  saveProfiles();
  emit selectedProfileChanged();
}

QString AppController::selectedProfileId() const {
  if (!hasSelectedProfile()) {
    return {};
  }
  return m_profiles->items().at(m_selectedProfileIndex).id;
}

QString AppController::selectedProfileName() const {
  if (!hasSelectedProfile()) {
    return {};
  }
  return m_profiles->items().at(m_selectedProfileIndex).name;
}

void AppController::setSelectedProfileName(const QString& value) {
  if (!hasSelectedProfile()) {
    return;
  }
  auto it = selectedProfileItem();
  const QString v = value.trimmed();
  if (it.name == v || v.isEmpty()) {
    return;
  }
  it.name = v;
  updateSelectedProfileItem(it);
}

QString AppController::selectedProfileGroup() const {
  if (!hasSelectedProfile()) {
    return {};
  }
  return m_profiles->items().at(m_selectedProfileIndex).group;
}

void AppController::setSelectedProfileGroup(const QString& value) {
  if (!hasSelectedProfile()) {
    return;
  }
  auto it = selectedProfileItem();
  const QString v = value.trimmed();
  if (it.group == v) {
    return;
  }
  it.group = v;
  updateSelectedProfileItem(it);
}

QString AppController::selectedProfileRemark() const {
  if (!hasSelectedProfile()) {
    return {};
  }
  return m_profiles->items().at(m_selectedProfileIndex).remark;
}

void AppController::setSelectedProfileRemark(const QString& value) {
  if (!hasSelectedProfile()) {
    return;
  }
  auto it = selectedProfileItem();
  if (it.remark == value) {
    return;
  }
  it.remark = value;
  updateSelectedProfileItem(it);
}

QString AppController::selectedProfileStatus() const {
  if (!hasSelectedProfile()) {
    return {};
  }
  return m_profiles->items().at(m_selectedProfileIndex).status;
}

qint64 AppController::selectedProfileCreatedAtMs() const {
  if (!hasSelectedProfile()) {
    return 0;
  }
  return m_profiles->items().at(m_selectedProfileIndex).createdAtMs;
}

qint64 AppController::selectedProfileLastOpenAtMs() const {
  if (!hasSelectedProfile()) {
    return 0;
  }
  return m_profiles->items().at(m_selectedProfileIndex).lastOpenAtMs;
}

QString AppController::selectedProfileDataDir() const {
  if (!hasSelectedProfile()) {
    return {};
  }
  return m_profiles->items().at(m_selectedProfileIndex).dataDir;
}

void AppController::setSelectedProfileDataDir(const QString& value) {
  if (!hasSelectedProfile()) {
    return;
  }
  auto it = selectedProfileItem();
  const QString v = value.trimmed();
  if (it.dataDir == v) {
    return;
  }
  it.dataDir = v;
  updateSelectedProfileItem(it);
}

QString AppController::selectedProfileLanguage() const {
  if (!hasSelectedProfile()) {
    return {};
  }
  return m_profiles->items().at(m_selectedProfileIndex).language;
}

void AppController::setSelectedProfileLanguage(const QString& value) {
  if (!hasSelectedProfile()) {
    return;
  }
  auto it = selectedProfileItem();
  const QString v = value.trimmed();
  if (it.language == v) {
    return;
  }
  it.language = v;
  updateSelectedProfileItem(it);
}

QString AppController::selectedProfileTimezone() const {
  if (!hasSelectedProfile()) {
    return {};
  }
  return m_profiles->items().at(m_selectedProfileIndex).timezone;
}

void AppController::setSelectedProfileTimezone(const QString& value) {
  if (!hasSelectedProfile()) {
    return;
  }
  auto it = selectedProfileItem();
  const QString v = value.trimmed();
  if (it.timezone == v) {
    return;
  }
  it.timezone = v;
  updateSelectedProfileItem(it);
}

QString AppController::selectedProfileResolution() const {
  if (!hasSelectedProfile()) {
    return {};
  }
  return m_profiles->items().at(m_selectedProfileIndex).resolution;
}

void AppController::setSelectedProfileResolution(const QString& value) {
  if (!hasSelectedProfile()) {
    return;
  }
  auto it = selectedProfileItem();
  const QString v = value.trimmed();
  if (it.resolution == v) {
    return;
  }
  it.resolution = v;
  updateSelectedProfileItem(it);
}

bool AppController::selectedProfileTouchEnabled() const {
  if (!hasSelectedProfile()) {
    return false;
  }
  return m_profiles->items().at(m_selectedProfileIndex).touchEnabled;
}

void AppController::setSelectedProfileTouchEnabled(bool value) {
  if (!hasSelectedProfile()) {
    return;
  }
  auto it = selectedProfileItem();
  if (it.touchEnabled == value) {
    return;
  }
  it.touchEnabled = value;
  updateSelectedProfileItem(it);
}

bool AppController::selectedProxyEnabled() const {
  if (!hasSelectedProfile()) {
    return false;
  }
  return m_profiles->items().at(m_selectedProfileIndex).proxyEnabled;
}

void AppController::setSelectedProxyEnabled(bool value) {
  if (!hasSelectedProfile()) {
    return;
  }
  auto it = selectedProfileItem();
  if (it.proxyEnabled == value) {
    return;
  }
  it.proxyEnabled = value;
  updateSelectedProfileItem(it);
}

QString AppController::selectedProxyType() const {
  if (!hasSelectedProfile()) {
    return {};
  }
  return m_profiles->items().at(m_selectedProfileIndex).proxyType;
}

void AppController::setSelectedProxyType(const QString& value) {
  if (!hasSelectedProfile()) {
    return;
  }
  auto it = selectedProfileItem();
  const QString v = value.trimmed();
  if (it.proxyType == v) {
    return;
  }
  it.proxyType = v;
  updateSelectedProfileItem(it);
}

QString AppController::selectedProxyHost() const {
  if (!hasSelectedProfile()) {
    return {};
  }
  return m_profiles->items().at(m_selectedProfileIndex).proxyHost;
}

void AppController::setSelectedProxyHost(const QString& value) {
  if (!hasSelectedProfile()) {
    return;
  }
  auto it = selectedProfileItem();
  const QString v = value.trimmed();
  if (it.proxyHost == v) {
    return;
  }
  it.proxyHost = v;
  updateSelectedProfileItem(it);
}

int AppController::selectedProxyPort() const {
  if (!hasSelectedProfile()) {
    return 0;
  }
  return m_profiles->items().at(m_selectedProfileIndex).proxyPort;
}

void AppController::setSelectedProxyPort(int value) {
  if (!hasSelectedProfile()) {
    return;
  }
  auto it = selectedProfileItem();
  const int v = qMax(0, value);
  if (it.proxyPort == v) {
    return;
  }
  it.proxyPort = v;
  updateSelectedProfileItem(it);
}

QString AppController::selectedProxyUsername() const {
  if (!hasSelectedProfile()) {
    return {};
  }
  return m_profiles->items().at(m_selectedProfileIndex).proxyUsername;
}

void AppController::setSelectedProxyUsername(const QString& value) {
  if (!hasSelectedProfile()) {
    return;
  }
  auto it = selectedProfileItem();
  if (it.proxyUsername == value) {
    return;
  }
  it.proxyUsername = value;
  updateSelectedProfileItem(it);
}

QString AppController::selectedProxyPassword() const {
  if (!hasSelectedProfile()) {
    return {};
  }
  return m_profiles->items().at(m_selectedProfileIndex).proxyPassword;
}

void AppController::setSelectedProxyPassword(const QString& value) {
  if (!hasSelectedProfile()) {
    return;
  }
  auto it = selectedProfileItem();
  if (it.proxyPassword == value) {
    return;
  }
  it.proxyPassword = value;
  updateSelectedProfileItem(it);
}

QString AppController::proxyLastTestSummary() const {
  return m_proxyLastTestSummary;
}

bool AppController::selectedVpnEnabled() const {
  if (!hasSelectedProfile()) {
    return false;
  }
  return m_profiles->items().at(m_selectedProfileIndex).vpnEnabled;
}

void AppController::setSelectedVpnEnabled(bool value) {
  if (!hasSelectedProfile()) {
    return;
  }
  auto it = selectedProfileItem();
  if (it.vpnEnabled == value) {
    return;
  }
  it.vpnEnabled = value;
  updateSelectedProfileItem(it);
}

QString AppController::selectedOpenvpnExe() const {
  if (!hasSelectedProfile()) {
    return {};
  }
  return m_profiles->items().at(m_selectedProfileIndex).openvpnExe;
}

void AppController::setSelectedOpenvpnExe(const QString& value) {
  if (!hasSelectedProfile()) {
    return;
  }
  auto it = selectedProfileItem();
  const QString v = value.trimmed();
  if (it.openvpnExe == v) {
    return;
  }
  it.openvpnExe = v;
  updateSelectedProfileItem(it);
}

QString AppController::selectedOpenvpnConfig() const {
  if (!hasSelectedProfile()) {
    return {};
  }
  return m_profiles->items().at(m_selectedProfileIndex).openvpnConfig;
}

void AppController::setSelectedOpenvpnConfig(const QString& value) {
  if (!hasSelectedProfile()) {
    return;
  }
  auto it = selectedProfileItem();
  const QString v = value.trimmed();
  if (it.openvpnConfig == v) {
    return;
  }
  it.openvpnConfig = v;
  updateSelectedProfileItem(it);
}

bool AppController::selectedOpenvpnUseSocks() const {
  if (!hasSelectedProfile()) {
    return false;
  }
  return m_profiles->items().at(m_selectedProfileIndex).openvpnUseSocks;
}

void AppController::setSelectedOpenvpnUseSocks(bool value) {
  if (!hasSelectedProfile()) {
    return;
  }
  auto it = selectedProfileItem();
  if (it.openvpnUseSocks == value) {
    return;
  }
  it.openvpnUseSocks = value;
  updateSelectedProfileItem(it);
}

QString AppController::selectedOpenvpnSocksHost() const {
  if (!hasSelectedProfile()) {
    return {};
  }
  return m_profiles->items().at(m_selectedProfileIndex).openvpnSocksHost;
}

void AppController::setSelectedOpenvpnSocksHost(const QString& value) {
  if (!hasSelectedProfile()) {
    return;
  }
  auto it = selectedProfileItem();
  const QString v = value.trimmed();
  if (it.openvpnSocksHost == v) {
    return;
  }
  it.openvpnSocksHost = v;
  updateSelectedProfileItem(it);
}

int AppController::selectedOpenvpnSocksPort() const {
  if (!hasSelectedProfile()) {
    return 0;
  }
  return m_profiles->items().at(m_selectedProfileIndex).openvpnSocksPort;
}

void AppController::setSelectedOpenvpnSocksPort(int value) {
  if (!hasSelectedProfile()) {
    return;
  }
  auto it = selectedProfileItem();
  const int v = qMax(0, value);
  if (it.openvpnSocksPort == v) {
    return;
  }
  it.openvpnSocksPort = v;
  updateSelectedProfileItem(it);
}

QString AppController::selectedOpenvpnSocksUsername() const {
  if (!hasSelectedProfile()) {
    return {};
  }
  return m_profiles->items().at(m_selectedProfileIndex).openvpnSocksUsername;
}

void AppController::setSelectedOpenvpnSocksUsername(const QString& value) {
  if (!hasSelectedProfile()) {
    return;
  }
  auto it = selectedProfileItem();
  if (it.openvpnSocksUsername == value) {
    return;
  }
  it.openvpnSocksUsername = value;
  updateSelectedProfileItem(it);
}

QString AppController::selectedOpenvpnSocksPassword() const {
  if (!hasSelectedProfile()) {
    return {};
  }
  return m_profiles->items().at(m_selectedProfileIndex).openvpnSocksPassword;
}

void AppController::setSelectedOpenvpnSocksPassword(const QString& value) {
  if (!hasSelectedProfile()) {
    return;
  }
  auto it = selectedProfileItem();
  if (it.openvpnSocksPassword == value) {
    return;
  }
  it.openvpnSocksPassword = value;
  updateSelectedProfileItem(it);
}

QString AppController::selectedVpnStatus() const {
  const QString pid = selectedProfileId();
  if (pid.isEmpty()) {
    return {};
  }
  return m_vpnStatusByProfileId.value(pid, QStringLiteral("stopped"));
}

bool AppController::agentRunning() const {
  return m_agent && m_agent->state() != QProcess::NotRunning;
}

bool AppController::ipcConnected() const {
  return m_ipcConnected;
}

void AppController::createProfile() {
  ProfileListModel::ProfileItem item;
  item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  item.name = newProfileName();
  item.group = QStringLiteral("默认分组");
  item.status = QStringLiteral("stopped");
  item.createdAtMs = QDateTime::currentMSecsSinceEpoch();
  item.language = QStringLiteral("zh-CN");
  item.timezone = QString::fromUtf8(QTimeZone::systemTimeZoneId());
  item.resolution = QStringLiteral("1920x1080");
  item.touchEnabled = false;

  item.proxyEnabled = false;
  item.proxyType = QStringLiteral("direct");

  item.vpnEnabled = false;
  item.openvpnExe = QStringLiteral("openvpn");
  item.openvpnConfig = QString();
  item.openvpnUseSocks = false;

  const int row = m_profiles->addProfile(item);
  saveProfiles();
  setSelectedProfileIndex(row);
}

void AppController::deleteSelectedProfile() {
  if (m_selectedProfileIndex < 0 || m_selectedProfileIndex >= m_profiles->rowCount()) {
    return;
  }

  const int removed = m_selectedProfileIndex;
  m_profiles->removeAt(removed);
  saveProfiles();

  const int newIndex = qMin(removed, m_profiles->rowCount() - 1);
  setSelectedProfileIndex(newIndex);
  emit selectedProfileChanged();
}

void AppController::runSelectedProfile() {
  if (!m_ipcConnected) {
    m_logs->appendLine(QStringLiteral("[%1] ipc_not_connected").arg(ts()));
    return;
  }
  if (!hasSelectedProfile()) {
    return;
  }

  const auto it = selectedProfileItem();
  QJsonObject msg;
  msg.insert(QStringLiteral("type"), QStringLiteral("profile.start"));
  msg.insert(QStringLiteral("profile_id"), it.id);
  msg.insert(QStringLiteral("profile_name"), it.name);
  m_ipc->send(msg);

  m_profiles->setStatus(m_selectedProfileIndex, QStringLiteral("running"));
  m_profiles->setLastOpenAtMs(m_selectedProfileIndex, QDateTime::currentMSecsSinceEpoch());
  saveProfiles();
  emit selectedProfileChanged();
}

void AppController::stopSelectedProfile() {
  if (!m_ipcConnected) {
    m_logs->appendLine(QStringLiteral("[%1] ipc_not_connected").arg(ts()));
    return;
  }
  if (!hasSelectedProfile()) {
    return;
  }

  const auto it = selectedProfileItem();
  QJsonObject msg;
  msg.insert(QStringLiteral("type"), QStringLiteral("profile.stop"));
  msg.insert(QStringLiteral("profile_id"), it.id);
  msg.insert(QStringLiteral("profile_name"), it.name);
  m_ipc->send(msg);

  m_profiles->setStatus(m_selectedProfileIndex, QStringLiteral("stopped"));
  saveProfiles();
  emit selectedProfileChanged();
}

void AppController::testSelectedProxy() {
  if (!m_ipcConnected) {
    m_logs->appendLine(QStringLiteral("[%1] ipc_not_connected").arg(ts()));
    return;
  }
  if (!hasSelectedProfile()) {
    return;
  }

  const auto it = selectedProfileItem();
  QJsonObject proxy;
  proxy.insert(QStringLiteral("enabled"), it.proxyEnabled);
  proxy.insert(QStringLiteral("type"), it.proxyType);
  proxy.insert(QStringLiteral("host"), it.proxyHost);
  proxy.insert(QStringLiteral("port"), it.proxyPort);
  proxy.insert(QStringLiteral("username"), it.proxyUsername);
  proxy.insert(QStringLiteral("password"), it.proxyPassword);

  QJsonObject msg;
  msg.insert(QStringLiteral("type"), QStringLiteral("proxy.test"));
  msg.insert(QStringLiteral("profile_id"), it.id);
  msg.insert(QStringLiteral("proxy"), proxy);
  msg.insert(QStringLiteral("url"), QStringLiteral("https://api.ipify.org?format=json"));
  m_ipc->send(msg);

  m_proxyLastTestSummary = QStringLiteral("testing...");
  emit proxyLastTestChanged();
}

void AppController::startSelectedVpn() {
  if (!m_ipcConnected) {
    m_logs->appendLine(QStringLiteral("[%1] ipc_not_connected").arg(ts()));
    return;
  }
  if (!hasSelectedProfile()) {
    return;
  }

  const auto it = selectedProfileItem();
  QJsonObject socks;
  socks.insert(QStringLiteral("enabled"), it.openvpnUseSocks);
  socks.insert(QStringLiteral("host"), it.openvpnSocksHost);
  socks.insert(QStringLiteral("port"), it.openvpnSocksPort);
  socks.insert(QStringLiteral("username"), it.openvpnSocksUsername);
  socks.insert(QStringLiteral("password"), it.openvpnSocksPassword);

  QJsonObject msg;
  msg.insert(QStringLiteral("type"), QStringLiteral("vpn.openvpn.start"));
  msg.insert(QStringLiteral("profile_id"), it.id);
  msg.insert(QStringLiteral("exe"), it.openvpnExe);
  msg.insert(QStringLiteral("config"), it.openvpnConfig);
  msg.insert(QStringLiteral("socks"), socks);
  m_ipc->send(msg);
}

void AppController::stopSelectedVpn() {
  if (!m_ipcConnected) {
    m_logs->appendLine(QStringLiteral("[%1] ipc_not_connected").arg(ts()));
    return;
  }
  if (!hasSelectedProfile()) {
    return;
  }

  const auto it = selectedProfileItem();
  QJsonObject msg;
  msg.insert(QStringLiteral("type"), QStringLiteral("vpn.openvpn.stop"));
  msg.insert(QStringLiteral("profile_id"), it.id);
  m_ipc->send(msg);
}

void AppController::startAgent() {
  if (m_agent && m_agent->state() != QProcess::NotRunning) {
    m_ipc->connectToAgent();
    return;
  }

  if (!m_agent) {
    m_agent = new QProcess(this);
    QObject::connect(m_agent, &QProcess::started, this, [this]() {
      emit agentRunningChanged();
      m_logs->appendLine(QStringLiteral("[%1] agent_started").arg(ts()));
      m_ipc->connectToAgent();
    });

    QObject::connect(m_agent, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus st) {
      emit agentRunningChanged();
      m_logs->appendLine(QStringLiteral("[%1] agent_finished exitCode=%2 exitStatus=%3")
                             .arg(ts())
                             .arg(exitCode)
                             .arg(st == QProcess::NormalExit ? "normal" : "crash"));
    });

    QObject::connect(m_agent, &QProcess::readyReadStandardOutput, this, [this]() {
      const QByteArray bytes = m_agent->readAllStandardOutput();
      const auto lines = QString::fromUtf8(bytes).split('\n', Qt::SkipEmptyParts);
      for (const auto& line : lines) {
        m_logs->appendLine(QStringLiteral("[%1] agent_out: %2").arg(ts(), line));
      }
    });

    QObject::connect(m_agent, &QProcess::readyReadStandardError, this, [this]() {
      const QByteArray bytes = m_agent->readAllStandardError();
      const auto lines = QString::fromUtf8(bytes).split('\n', Qt::SkipEmptyParts);
      for (const auto& line : lines) {
        m_logs->appendLine(QStringLiteral("[%1] agent_err: %2").arg(ts(), line));
      }
    });
  }

  const QString program = agentProgramPath();
  if (program.isEmpty()) {
    m_logs->appendLine(QStringLiteral("[%1] agent_not_found").arg(ts()));
    return;
  }

  m_agent->setProgram(program);
  m_agent->setArguments({});
  m_agent->start();
}

void AppController::stopAgent() {
  if (!m_agent || m_agent->state() == QProcess::NotRunning) {
    return;
  }

  m_agent->terminate();
  if (!m_agent->waitForFinished(500)) {
    m_agent->kill();
  }
}

void AppController::clearLogs() {
  m_logs->clear();
}

QString AppController::profilesFilePath() const {
  const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  QDir().mkpath(dir);
  return dir + QStringLiteral("/profiles.json");
}

void AppController::loadProfiles() {
  m_profiles->clear();

  QFile f(profilesFilePath());
  if (!f.exists()) {
    ProfileListModel::ProfileItem item;
    item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    item.name = QStringLiteral("Profile 1");
    item.group = QStringLiteral("默认分组");
    item.status = QStringLiteral("stopped");
    item.createdAtMs = QDateTime::currentMSecsSinceEpoch();
    item.language = QStringLiteral("zh-CN");
    item.timezone = QString::fromUtf8(QTimeZone::systemTimeZoneId());
    item.resolution = QStringLiteral("1920x1080");
    item.touchEnabled = false;

    item.proxyEnabled = false;
    item.proxyType = QStringLiteral("direct");

    item.vpnEnabled = false;
    item.openvpnExe = QStringLiteral("openvpn");
    item.openvpnConfig = QString();
    item.openvpnUseSocks = false;
    m_profiles->addProfile(item);
    saveProfiles();
    return;
  }

  if (!f.open(QIODevice::ReadOnly)) {
    return;
  }

  QJsonParseError err{};
  const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
  if (err.error != QJsonParseError::NoError || !doc.isArray()) {
    return;
  }

  const QJsonArray arr = doc.array();
  for (const auto& v : arr) {
    if (!v.isObject()) {
      continue;
    }
    const QJsonObject o = v.toObject();
    const QString id = o.value(QStringLiteral("id")).toString();
    const QString name = o.value(QStringLiteral("name")).toString();
    if (id.isEmpty() || name.isEmpty()) {
      continue;
    }

    ProfileListModel::ProfileItem item;
    item.id = id;
    item.name = name;
    item.group = o.value(QStringLiteral("group")).toString(QStringLiteral("默认分组"));
    item.remark = o.value(QStringLiteral("remark")).toString();
    item.status = o.value(QStringLiteral("status")).toString(QStringLiteral("stopped"));
    item.createdAtMs = static_cast<qint64>(o.value(QStringLiteral("created_at_ms")).toDouble(0));
    item.lastOpenAtMs = static_cast<qint64>(o.value(QStringLiteral("last_open_at_ms")).toDouble(0));
    item.dataDir = o.value(QStringLiteral("data_dir")).toString();
    item.language = o.value(QStringLiteral("language")).toString(QStringLiteral("zh-CN"));
    item.timezone = o.value(QStringLiteral("timezone")).toString(QString::fromUtf8(QTimeZone::systemTimeZoneId()));
    item.resolution = o.value(QStringLiteral("resolution")).toString(QStringLiteral("1920x1080"));
    item.touchEnabled = o.value(QStringLiteral("touch_enabled")).toBool(false);

    item.proxyEnabled = o.value(QStringLiteral("proxy_enabled")).toBool(false);
    item.proxyType = o.value(QStringLiteral("proxy_type")).toString(QStringLiteral("direct"));
    item.proxyHost = o.value(QStringLiteral("proxy_host")).toString();
    item.proxyPort = o.value(QStringLiteral("proxy_port")).toInt(0);
    item.proxyUsername = o.value(QStringLiteral("proxy_username")).toString();
    item.proxyPassword = o.value(QStringLiteral("proxy_password")).toString();

    item.vpnEnabled = o.value(QStringLiteral("vpn_enabled")).toBool(false);
    item.openvpnExe = o.value(QStringLiteral("openvpn_exe")).toString(QStringLiteral("openvpn"));
    item.openvpnConfig = o.value(QStringLiteral("openvpn_config")).toString();
    item.openvpnUseSocks = o.value(QStringLiteral("openvpn_use_socks")).toBool(false);
    item.openvpnSocksHost = o.value(QStringLiteral("openvpn_socks_host")).toString();
    item.openvpnSocksPort = o.value(QStringLiteral("openvpn_socks_port")).toInt(0);
    item.openvpnSocksUsername = o.value(QStringLiteral("openvpn_socks_username")).toString();
    item.openvpnSocksPassword = o.value(QStringLiteral("openvpn_socks_password")).toString();

    if (item.createdAtMs <= 0) {
      item.createdAtMs = QDateTime::currentMSecsSinceEpoch();
    }

    m_profiles->addProfile(item);
  }

  if (m_profiles->rowCount() == 0) {
    ProfileListModel::ProfileItem item;
    item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    item.name = QStringLiteral("Profile 1");
    item.group = QStringLiteral("默认分组");
    item.status = QStringLiteral("stopped");
    item.createdAtMs = QDateTime::currentMSecsSinceEpoch();
    item.language = QStringLiteral("zh-CN");
    item.timezone = QString::fromUtf8(QTimeZone::systemTimeZoneId());
    item.resolution = QStringLiteral("1920x1080");
    item.touchEnabled = false;

    item.proxyEnabled = false;
    item.proxyType = QStringLiteral("direct");

    item.vpnEnabled = false;
    item.openvpnExe = QStringLiteral("openvpn");
    item.openvpnConfig = QString();
    item.openvpnUseSocks = false;
    m_profiles->addProfile(item);
    saveProfiles();
  }
}

void AppController::saveProfiles() {
  QJsonArray arr;
  for (const auto& it : m_profiles->items()) {
    QJsonObject o;
    o.insert(QStringLiteral("id"), it.id);
    o.insert(QStringLiteral("name"), it.name);
    o.insert(QStringLiteral("group"), it.group);
    o.insert(QStringLiteral("remark"), it.remark);
    o.insert(QStringLiteral("status"), it.status);
    o.insert(QStringLiteral("created_at_ms"), static_cast<double>(it.createdAtMs));
    o.insert(QStringLiteral("last_open_at_ms"), static_cast<double>(it.lastOpenAtMs));

    o.insert(QStringLiteral("data_dir"), it.dataDir);
    o.insert(QStringLiteral("language"), it.language);
    o.insert(QStringLiteral("timezone"), it.timezone);
    o.insert(QStringLiteral("resolution"), it.resolution);
    o.insert(QStringLiteral("touch_enabled"), it.touchEnabled);

    o.insert(QStringLiteral("proxy_enabled"), it.proxyEnabled);
    o.insert(QStringLiteral("proxy_type"), it.proxyType);
    o.insert(QStringLiteral("proxy_host"), it.proxyHost);
    o.insert(QStringLiteral("proxy_port"), it.proxyPort);
    o.insert(QStringLiteral("proxy_username"), it.proxyUsername);
    o.insert(QStringLiteral("proxy_password"), it.proxyPassword);

    o.insert(QStringLiteral("vpn_enabled"), it.vpnEnabled);
    o.insert(QStringLiteral("openvpn_exe"), it.openvpnExe);
    o.insert(QStringLiteral("openvpn_config"), it.openvpnConfig);
    o.insert(QStringLiteral("openvpn_use_socks"), it.openvpnUseSocks);
    o.insert(QStringLiteral("openvpn_socks_host"), it.openvpnSocksHost);
    o.insert(QStringLiteral("openvpn_socks_port"), it.openvpnSocksPort);
    o.insert(QStringLiteral("openvpn_socks_username"), it.openvpnSocksUsername);
    o.insert(QStringLiteral("openvpn_socks_password"), it.openvpnSocksPassword);

    arr.push_back(o);
  }

  QFile f(profilesFilePath());
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return;
  }
  f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
}

QString AppController::newProfileName() const {
  const int base = m_profiles->rowCount() + 1;
  for (int i = 0; i < 10000; i++) {
    const QString candidate = QStringLiteral("Profile %1").arg(base + i);
    bool exists = false;
    for (const auto& it : m_profiles->items()) {
      if (it.name == candidate) {
        exists = true;
        break;
      }
    }
    if (!exists) {
      return candidate;
    }
  }
  return QStringLiteral("Profile");
}

QString AppController::agentProgramPath() const {
  const QString dir = QCoreApplication::applicationDirPath();
#if defined(Q_OS_WIN)
  const QString name = QStringLiteral("dokebrowser_agent.exe");
#else
  const QString name = QStringLiteral("dokebrowser_agent");
#endif
  const QString path = dir + QLatin1Char('/') + name;
  if (QFile::exists(path)) {
    return path;
  }
  return {};
}
