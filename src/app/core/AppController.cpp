#include "AppController.h"

#include "IpcClient.h"
#include "LogListModel.h"
#include "ProfileFilterModel.h"
#include "ProfileListModel.h"
#include "ProfileRepository.h"
#include "ProxyPoolModel.h"

#include <algorithm>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QTimeZone>
#include <QUuid>

namespace {
QString ts() {
  return QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
}

qint64 parseTsMs(const QString& s, bool endOfDay) {
  const QString v = s.trimmed();
  if (v.isEmpty()) {
    return 0;
  }

  QDateTime dt = QDateTime::fromString(v, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
  if (!dt.isValid()) {
    dt = QDateTime::fromString(v, QStringLiteral("yyyy-MM-dd"));
    if (dt.isValid()) {
      if (endOfDay) {
        dt = dt.addSecs(23 * 3600 + 59 * 60 + 59);
      }
    }
  }
  if (!dt.isValid()) {
    return 0;
  }
  dt.setTimeZone(QTimeZone::systemTimeZone());
  return dt.toMSecsSinceEpoch();
}
}

AppController::AppController(QObject* parent) : QObject(parent) {
  m_profiles = new ProfileListModel(this);
  m_filtered = new ProfileFilterModel(this);
  m_filtered->setSourceModel(m_profiles);
  m_logs = new LogListModel(this);
  m_proxyPool = new ProxyPoolModel(this);
  m_ipc = new IpcClient(this);
  m_repo = new ProfileRepository(this);

  QObject::connect(m_ipc, &IpcClient::logLineReceived, this, [this](const QString& line) {
    appendLogLine(line, QStringLiteral("agent"));
  });

  QObject::connect(m_ipc, &IpcClient::connectionError, this, [this](const QString& msg) {
    appendLogLine(QStringLiteral("ipc_error: %1").arg(msg), QStringLiteral("ipc"));
  });

  QObject::connect(m_ipc, &IpcClient::connectedChanged, this, [this]() {
    const bool now = m_ipc->isConnected();
    if (m_ipcConnected != now) {
      m_ipcConnected = now;
      emit ipcConnectedChanged();
    }
    appendLogLine(QStringLiteral("ipc_connected=%1").arg(now ? "true" : "false"), QStringLiteral("ipc"));
  });

  QObject::connect(m_ipc, &IpcClient::profileStatusReceived, this, [this](const QJsonObject& obj) {
    const QString profileId = obj.value(QStringLiteral("profile_id")).toString().trimmed();
    const QString status = obj.value(QStringLiteral("status")).toString().trimmed();
    const QString err = obj.value(QStringLiteral("error")).toString();
    if (profileId.isEmpty() || status.isEmpty()) {
      return;
    }

    for (int i = 0; i < m_profiles->items().size(); i++) {
      const auto& it = m_profiles->items().at(i);
      if (it.id != profileId) {
        continue;
      }
      auto updated = it;
      updated.status = status;
      if (status == QStringLiteral("running")) {
        updated.lastOpenAtMs = QDateTime::currentMSecsSinceEpoch();
      }
      m_profiles->updateAt(i, updated);
      persistProfile(updated);
      break;
    }

    persistRunEvent(profileId, QStringLiteral("profile.status"), status, err);
    appendLogLine(QStringLiteral("profile_status: %1 err=%2").arg(status, err), QStringLiteral("profile"), profileId);
    if (profileId == selectedProfileId()) {
      emit selectedProfileChanged();
    }
    applyFilters();
  });

  QObject::connect(m_ipc, &IpcClient::proxyTestResultReceived, this, [this](const QJsonObject& obj) {
    const QString profileId = obj.value(QStringLiteral("profile_id")).toString(selectedProfileId());
    const QString batchId = obj.value(QStringLiteral("batch_id")).toString();
    const QString requestId = obj.value(QStringLiteral("request_id")).toString();
    const bool isBatch = !batchId.isEmpty();
    if (isBatch) {
      if (batchId != m_proxyBatchId) {
        return;
      }
      if (!m_proxyBatchInFlightStartMs.contains(profileId)) {
        return;
      }
      const QString expected = m_proxyBatchInFlightRequestId.value(profileId);
      if (!expected.isEmpty() && !requestId.isEmpty() && requestId != expected) {
        return;
      }
    }
    const bool ok = obj.value(QStringLiteral("ok")).toBool(false);
    const QString observedIp = obj.value(QStringLiteral("observed_ip")).toString();
    const QString error = obj.value(QStringLiteral("error")).toString();
    const int statusCode = obj.value(QStringLiteral("status_code")).toInt(0);
    const int durationMs = obj.value(QStringLiteral("duration_ms")).toInt(0);
    const int qtError = obj.value(QStringLiteral("qt_error")).toInt(0);

    persistProxyTestRun(profileId, ok, observedIp, statusCode, durationMs, qtError, error);

    const QString summary = ok ? QStringLiteral("OK ip=%1 status=%2 duration=%3ms").arg(observedIp).arg(statusCode).arg(durationMs)
                               : QStringLiteral("FAIL status=%1 duration=%2ms error=%3").arg(statusCode).arg(durationMs).arg(error);

    for (int i = 0; i < m_profiles->items().size(); i++) {
      const auto& it = m_profiles->items().at(i);
      if (it.id != profileId) {
        continue;
      }
      auto updated = it;
      updated.proxyLastOk = ok;
      updated.proxyLastObservedIp = observedIp;
      updated.proxyLastError = ok ? QString() : error;
      updated.proxyLastAtMs = QDateTime::currentMSecsSinceEpoch();
      m_profiles->updateAt(i, updated);
      break;
    }

    if (profileId == selectedProfileId()) {
      m_proxyLastTestSummary = summary;
      emit proxyLastTestChanged();
    }
    appendLogLine(QStringLiteral("proxy_test: %1").arg(summary), QStringLiteral("proxy"), profileId);

    if (isBatch) {
      finishProxyTestSlot(profileId, QStringLiteral("result"));
      pumpProxyTestQueue();
    }
  });

  QObject::connect(m_ipc, &IpcClient::proxyPoolTestResultReceived, this, [this](const QJsonObject& obj) {
    const QString proxyId = obj.value(QStringLiteral("proxy_id")).toString();
    const QString batchId = obj.value(QStringLiteral("batch_id")).toString();
    const QString requestId = obj.value(QStringLiteral("request_id")).toString();
    const bool isBatch = !batchId.isEmpty();
    if (isBatch) {
      if (batchId != m_proxyPoolBatchId) {
        return;
      }
      if (!m_proxyPoolBatchInFlightStartMs.contains(proxyId)) {
        return;
      }
      const QString expected = m_proxyPoolBatchInFlightRequestId.value(proxyId);
      if (!expected.isEmpty() && !requestId.isEmpty() && requestId != expected) {
        return;
      }
    }

    const bool ok = obj.value(QStringLiteral("ok")).toBool(false);
    const QString observedIp = obj.value(QStringLiteral("observed_ip")).toString();
    const QString error = obj.value(QStringLiteral("error")).toString();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    if (m_repo && !proxyId.isEmpty()) {
      QString err;
      if (!m_repo->updateProxyHealth(proxyId, ok, observedIp, error, now, &err)) {
        appendLogLine(QStringLiteral("proxy_pool_update_error: %1").arg(err), QStringLiteral("db"));
      }
    }

    if (m_proxyPool) {
      for (int i = 0; i < m_proxyPool->items().size(); i++) {
        const auto& it = m_proxyPool->items().at(i);
        if (it.id != proxyId) {
          continue;
        }
        auto updated = it;
        updated.lastOk = ok;
        updated.lastIp = observedIp;
        updated.lastError = ok ? QString() : error;
        updated.lastAtMs = now;
        m_proxyPool->updateAt(i, updated);
        break;
      }
    }

    appendLogLine(QStringLiteral("proxy_pool_test ok=%1 ip=%2 error=%3")
                      .arg(ok ? "true" : "false")
                      .arg(observedIp)
                      .arg(error),
                  QStringLiteral("proxy"));

    if (isBatch) {
      finishProxyPoolTestSlot(proxyId, QStringLiteral("result"));
      pumpProxyPoolTestQueue();
    }
  });

  QObject::connect(m_ipc, &IpcClient::vpnStatusReceived, this, [this](const QJsonObject& obj) {
    const QString profileId = obj.value(QStringLiteral("profile_id")).toString();
    const QString status = obj.value(QStringLiteral("status")).toString();
    const QString err = obj.value(QStringLiteral("error")).toString();
    if (profileId.isEmpty()) {
      return;
    }
    m_vpnStatusByProfileId.insert(profileId, status);
    if (profileId == selectedProfileId()) {
      emit selectedVpnStatusChanged();
    }
    persistRunEvent(profileId, QStringLiteral("vpn.status"), status, err);
  });

  loadProfiles();
  refreshProxyPool();
  rebuildGroups();
  applyFilters();
  if (m_profiles->rowCount() > 0) {
    setSelectedProfileIndex(0);
  }

  startAgent();
}

AppController::~AppController() = default;

ProfileListModel* AppController::profiles() {
  return m_profiles;
}

QAbstractItemModel* AppController::filteredProfiles() {
  return m_filtered;
}

LogListModel* AppController::logs() {
  return m_logs;
}

ProxyPoolModel* AppController::proxyPool() {
  return m_proxyPool;
}

QStringList AppController::groups() const {
  return m_groups;
}

QStringList AppController::checkedProfileIds() const {
  return m_checkedIds;
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
  persistProfile(item);
  rebuildGroups();
  applyFilters();
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

int AppController::checkedCount() const {
  return m_checkedIds.size();
}

bool AppController::isProfileChecked(const QString& profileId) const {
  const QString pid = profileId.trimmed();
  if (pid.isEmpty()) {
    return false;
  }
  return m_checkedSet.contains(pid);
}

bool AppController::showOnlyChecked() const {
  return m_showOnlyChecked;
}

bool AppController::selectProfileById(const QString& profileId) {
  const QString pid = profileId.trimmed();
  if (pid.isEmpty()) {
    return false;
  }
  for (int i = 0; i < m_profiles->items().size(); i++) {
    const auto& it = m_profiles->items().at(i);
    if (it.id == pid) {
      setSelectedProfileIndex(i);
      return true;
    }
  }
  return false;
}

void AppController::runCheckedProfiles() {
  if (!m_ipcConnected) {
    appendLogLine(QStringLiteral("ipc_not_connected"), QStringLiteral("ipc"));
    return;
  }
  if (m_checkedIds.isEmpty()) {
    runSelectedProfile();
    return;
  }

  for (const auto& id : m_checkedIds) {
    for (int i = 0; i < m_profiles->items().size(); i++) {
      const auto& it = m_profiles->items().at(i);
      if (it.id != id) {
        continue;
      }

      QJsonObject msg;
      msg.insert(QStringLiteral("type"), QStringLiteral("profile.start"));
      msg.insert(QStringLiteral("profile_id"), it.id);
      msg.insert(QStringLiteral("profile_name"), it.name);
      msg.insert(QStringLiteral("data_dir"), it.dataDir);
      if (it.proxyEnabled) {
        QJsonObject proxy;
        proxy.insert(QStringLiteral("enabled"), true);
        proxy.insert(QStringLiteral("type"), it.proxyType);
        proxy.insert(QStringLiteral("host"), it.proxyHost);
        proxy.insert(QStringLiteral("port"), it.proxyPort);
        msg.insert(QStringLiteral("proxy"), proxy);
      }
      m_ipc->send(msg);
      persistRunEvent(it.id, QStringLiteral("profile.start"), QStringLiteral("requested"), QString());

      auto updated = it;
      updated.status = QStringLiteral("starting");
      updated.lastOpenAtMs = QDateTime::currentMSecsSinceEpoch();
      m_profiles->updateAt(i, updated);
      persistProfile(updated);
      persistRunEvent(it.id, QStringLiteral("profile.status"), QStringLiteral("starting"), QString());
      break;
    }
  }

  emit selectedProfileChanged();
  applyFilters();
}

void AppController::stopCheckedProfiles() {
  if (!m_ipcConnected) {
    appendLogLine(QStringLiteral("ipc_not_connected"), QStringLiteral("ipc"));
    return;
  }
  if (m_checkedIds.isEmpty()) {
    stopSelectedProfile();
    return;
  }

  for (const auto& id : m_checkedIds) {
    for (int i = 0; i < m_profiles->items().size(); i++) {
      const auto& it = m_profiles->items().at(i);
      if (it.id != id) {
        continue;
      }

      QJsonObject msg;
      msg.insert(QStringLiteral("type"), QStringLiteral("profile.stop"));
      msg.insert(QStringLiteral("profile_id"), it.id);
      msg.insert(QStringLiteral("profile_name"), it.name);
      m_ipc->send(msg);
      persistRunEvent(it.id, QStringLiteral("profile.stop"), QStringLiteral("requested"), QString());

      auto updated = it;
      updated.status = QStringLiteral("stopping");
      m_profiles->updateAt(i, updated);
      persistProfile(updated);
      persistRunEvent(it.id, QStringLiteral("profile.status"), QStringLiteral("stopping"), QString());
      break;
    }
  }

  emit selectedProfileChanged();
  applyFilters();
}

void AppController::deleteCheckedProfiles() {
  if (m_checkedIds.isEmpty()) {
    deleteSelectedProfile();
    return;
  }

  QVector<int> indices;
  for (const auto& id : m_checkedIds) {
    for (int i = 0; i < m_profiles->items().size(); i++) {
      if (m_profiles->items().at(i).id == id) {
        indices.push_back(i);
        break;
      }
    }
  }
  if (indices.isEmpty()) {
    return;
  }

  std::sort(indices.begin(), indices.end(), [](int a, int b) { return a > b; });
  for (const int idx : indices) {
    if (idx < 0 || idx >= m_profiles->rowCount()) {
      continue;
    }
    const QString id = m_profiles->items().at(idx).id;
    if (!deleteProfileFromStore(id)) {
      continue;
    }
    m_profiles->removeAt(idx);
    m_checkedSet.remove(id);
  }

  emitCheckedProfileIds();
  rebuildGroups();
  applyFilters();

  if (m_profiles->rowCount() > 0) {
    setSelectedProfileIndex(qMin(m_selectedProfileIndex, m_profiles->rowCount() - 1));
  } else {
    setSelectedProfileIndex(-1);
  }
  emit selectedProfileChanged();
}

void AppController::setGroupForCheckedProfiles(const QString& group) {
  const QString g = group.trimmed();
  if (g.isEmpty()) {
    return;
  }

  const QStringList targets = m_checkedIds.isEmpty() ? QStringList{selectedProfileId()} : m_checkedIds;
  for (const auto& id : targets) {
    if (id.isEmpty()) {
      continue;
    }
    for (int i = 0; i < m_profiles->items().size(); i++) {
      const auto& it = m_profiles->items().at(i);
      if (it.id != id) {
        continue;
      }
      auto updated = it;
      updated.group = g;
      m_profiles->updateAt(i, updated);
      persistProfile(updated);
      break;
    }
  }

  rebuildGroups();
  applyFilters();
  emit selectedProfileChanged();
}

void AppController::setGroupFilter(const QString& group) {
  const QString v = group.trimmed();
  m_groupFilter = v.isEmpty() ? QStringLiteral("所有分组") : v;
}

void AppController::checkAllVisibleProfiles() {
  if (!m_filtered) {
    return;
  }
  for (int i = 0; i < m_filtered->rowCount(); i++) {
    const QModelIndex idx = m_filtered->index(i, 0);
    const QString id = m_filtered->data(idx, ProfileListModel::IdRole).toString();
    if (!id.isEmpty()) {
      m_checkedSet.insert(id);
    }
  }
  emitCheckedProfileIds();
}

void AppController::uncheckAllVisibleProfiles() {
  if (!m_filtered) {
    return;
  }
  for (int i = 0; i < m_filtered->rowCount(); i++) {
    const QModelIndex idx = m_filtered->index(i, 0);
    const QString id = m_filtered->data(idx, ProfileListModel::IdRole).toString();
    if (!id.isEmpty()) {
      m_checkedSet.remove(id);
    }
  }
  emitCheckedProfileIds();
}

void AppController::invertCheckedVisibleProfiles() {
  if (!m_filtered) {
    return;
  }
  for (int i = 0; i < m_filtered->rowCount(); i++) {
    const QModelIndex idx = m_filtered->index(i, 0);
    const QString id = m_filtered->data(idx, ProfileListModel::IdRole).toString();
    if (id.isEmpty()) {
      continue;
    }
    if (m_checkedSet.contains(id)) {
      m_checkedSet.remove(id);
    } else {
      m_checkedSet.insert(id);
    }
  }
  emitCheckedProfileIds();
}

void AppController::checkGroupProfiles(const QString& group) {
  const QString g = group.trimmed();
  const bool all = g.isEmpty() || g == QStringLiteral("所有分组");
  for (const auto& it : m_profiles->items()) {
    if (it.id.isEmpty()) {
      continue;
    }
    if (all || it.group == g) {
      m_checkedSet.insert(it.id);
    }
  }
  emitCheckedProfileIds();
}

void AppController::uncheckGroupProfiles(const QString& group) {
  const QString g = group.trimmed();
  const bool all = g.isEmpty() || g == QStringLiteral("所有分组");
  for (const auto& it : m_profiles->items()) {
    if (it.id.isEmpty()) {
      continue;
    }
    if (all || it.group == g) {
      m_checkedSet.remove(it.id);
    }
  }
  emitCheckedProfileIds();
}

void AppController::invertCheckedGroupProfiles(const QString& group) {
  const QString g = group.trimmed();
  const bool all = g.isEmpty() || g == QStringLiteral("所有分组");
  for (const auto& it : m_profiles->items()) {
    if (it.id.isEmpty()) {
      continue;
    }
    if (!(all || it.group == g)) {
      continue;
    }
    if (m_checkedSet.contains(it.id)) {
      m_checkedSet.remove(it.id);
    } else {
      m_checkedSet.insert(it.id);
    }
  }
  emitCheckedProfileIds();
}

void AppController::setShowOnlyChecked(bool enabled) {
  if (m_showOnlyChecked == enabled) {
    return;
  }
  m_showOnlyChecked = enabled;
  if (m_filtered) {
    m_filtered->setOnlyChecked(m_showOnlyChecked);
    m_filtered->setCheckedIds(m_checkedIds);
  }
  emit showOnlyCheckedChanged();
}


void AppController::setSearchKeyword(const QString& keyword) {
  m_searchKeyword = keyword.trimmed();
}

void AppController::applyFilters() {
  if (!m_filtered) {
    return;
  }
  m_filtered->setGroupFilter(m_groupFilter);
  m_filtered->setKeyword(m_searchKeyword);
}

void AppController::resetFilters() {
  m_groupFilter = QStringLiteral("所有分组");
  m_searchKeyword.clear();
  applyFilters();
}

void AppController::refreshProxyPool() {
  if (!m_repo || !m_proxyPool) {
    return;
  }
  QString err;
  const auto items = m_repo->loadProxyPool(&err);
  if (!err.isEmpty()) {
    appendLogLine(QStringLiteral("proxy_pool_load_error: %1").arg(err), QStringLiteral("db"));
    return;
  }
  m_proxyPool->setItems(items);
}

int AppController::importProxyPool(const QString& text) {
  if (!m_repo) {
    return 0;
  }
  const QStringList lines = text.split('\n');
  QString err;
  const int n = m_repo->importProxyPool(lines, &err);
  if (!err.isEmpty()) {
    appendLogLine(QStringLiteral("proxy_pool_import_error: %1").arg(err), QStringLiteral("db"));
    return 0;
  }
  appendLogLine(QStringLiteral("proxy_pool_imported n=%1").arg(n), QStringLiteral("ui"));
  refreshProxyPool();
  return n;
}

void AppController::assignProxyPoolToCheckedProfiles() {
  const QStringList targets = m_checkedIds.isEmpty() ? QStringList{selectedProfileId()} : m_checkedIds;
  if (targets.isEmpty() || (targets.size() == 1 && targets.at(0).isEmpty())) {
    return;
  }
  assignProxyPoolToProfileIds(targets, true);
}

void AppController::releaseProxyPoolFromCheckedProfiles() {
  if (!m_repo) {
    return;
  }
  const QStringList targets = m_checkedIds.isEmpty() ? QStringList{selectedProfileId()} : m_checkedIds;
  if (targets.isEmpty() || (targets.size() == 1 && targets.at(0).isEmpty())) {
    return;
  }

  int okN = 0;
  int failN = 0;
  for (const auto& id : targets) {
    const QString pid = id.trimmed();
    if (pid.isEmpty()) {
      continue;
    }
    QString err;
    if (!m_repo->releaseProxyFromProfile(pid, &err)) {
      appendLogLine(QStringLiteral("proxy_pool_release_failed: %1").arg(err), QStringLiteral("proxy"), pid);
      failN++;
      continue;
    }
    okN++;
    for (int i = 0; i < m_profiles->items().size(); i++) {
      const auto& it = m_profiles->items().at(i);
      if (it.id != pid) {
        continue;
      }
      auto updated = it;
      updated.proxyEnabled = false;
      updated.proxyType = QStringLiteral("direct");
      updated.proxyHost.clear();
      updated.proxyPort = 0;
      updated.proxyUsername.clear();
      updated.proxyPassword.clear();
      m_profiles->updateAt(i, updated);
      if (pid == selectedProfileId()) {
        emit selectedProfileChanged();
      }
      break;
    }
  }
  appendLogLine(QStringLiteral("proxy_pool_release done ok=%1 fail=%2").arg(okN).arg(failN), QStringLiteral("proxy"));
  refreshProxyPool();
}

void AppController::rotateProxyForSelectedProfile() {
  if (!m_repo) {
    return;
  }
  const QString pid = selectedProfileId().trimmed();
  if (pid.isEmpty()) {
    return;
  }
  QString err;
  if (!m_repo->rotateProxyForProfile(pid, &err)) {
    appendLogLine(QStringLiteral("proxy_pool_rotate_failed: %1").arg(err), QStringLiteral("proxy"), pid);
    return;
  }

  QString loadErr;
  const auto all = m_repo->loadAll(&loadErr);
  if (!loadErr.isEmpty()) {
    appendLogLine(QStringLiteral("profiles_db_error: %1").arg(loadErr), QStringLiteral("db"));
  } else {
    ProfileListModel::ProfileItem fresh;
    bool found = false;
    for (const auto& it : all) {
      if (it.id == pid) {
        fresh = it;
        found = true;
        break;
      }
    }
    if (found) {
      for (int i = 0; i < m_profiles->items().size(); i++) {
        const auto& cur = m_profiles->items().at(i);
        if (cur.id != pid) {
          continue;
        }
        auto updated = cur;
        updated.proxyEnabled = fresh.proxyEnabled;
        updated.proxyType = fresh.proxyType;
        updated.proxyHost = fresh.proxyHost;
        updated.proxyPort = fresh.proxyPort;
        updated.proxyUsername = fresh.proxyUsername;
        updated.proxyPassword = fresh.proxyPassword;
        m_profiles->updateAt(i, updated);
        emit selectedProfileChanged();
        break;
      }
    }
  }
  appendLogLine(QStringLiteral("proxy_pool_rotate ok"), QStringLiteral("proxy"), pid);
  refreshProxyPool();
}

bool AppController::hasHealthyFreeProxy() const {
  if (!m_proxyPool) {
    return false;
  }
  for (const auto& it : m_proxyPool->items()) {
    if (it.disabled) {
      continue;
    }
    if (!it.assignedProfileId.trimmed().isEmpty()) {
      continue;
    }
    if (it.lastOk) {
      return true;
    }
  }
  return false;
}

void AppController::startProxyPoolTestBatch(const QStringList& proxyIds) {
  if (!m_ipcConnected) {
    appendLogLine(QStringLiteral("ipc_not_connected"), QStringLiteral("ipc"));
    return;
  }
  if (proxyIds.isEmpty()) {
    return;
  }
  if (!m_proxyPoolBatchTimer) {
    m_proxyPoolBatchTimer = new QTimer(this);
    m_proxyPoolBatchTimer->setInterval(200);
    QObject::connect(m_proxyPoolBatchTimer, &QTimer::timeout, this, [this]() { pumpProxyPoolTestQueue(); });
  }

  m_proxyPoolBatchId = QUuid::createUuid().toString(QUuid::WithoutBraces);
  m_proxyPoolBatchQueue = proxyIds;
  m_proxyPoolBatchInFlightStartMs.clear();
  m_proxyPoolBatchInFlightRequestId.clear();
  m_proxyPoolBatchRunning = true;
  appendLogLine(QStringLiteral("proxy_pool_test_batch start n=%1 max_concurrent=%2 timeout_ms=%3")
                    .arg(m_proxyPoolBatchQueue.size())
                    .arg(m_proxyPoolBatchMaxConcurrent)
                    .arg(m_proxyPoolBatchTimeoutMs),
                QStringLiteral("proxy"));

  if (!m_proxyPoolBatchTimer->isActive()) {
    m_proxyPoolBatchTimer->start();
  }
  pumpProxyPoolTestQueue();
}

void AppController::assignProxyPoolToProfileIds(const QStringList& profileIds, bool allowAutoTest) {
  if (!m_repo) {
    return;
  }
  if (profileIds.isEmpty()) {
    return;
  }

  if (allowAutoTest && !hasHealthyFreeProxy()) {
    if (m_proxyPoolBatchRunning) {
      m_proxyPoolAssignPending = true;
      m_proxyPoolAssignPendingProfileIds = profileIds;
      appendLogLine(QStringLiteral("proxy_pool_assign pending (batch_running) n=%1").arg(profileIds.size()), QStringLiteral("proxy"));
      return;
    }

    QStringList queue;
    if (m_proxyPool) {
      QSet<QString> set;
      for (const auto& it : m_proxyPool->items()) {
        const QString id = it.id.trimmed();
        if (id.isEmpty() || it.disabled || set.contains(id)) {
          continue;
        }
        if (!it.assignedProfileId.trimmed().isEmpty()) {
          continue;
        }
        set.insert(id);
        queue.push_back(id);
      }
    }
    if (!queue.isEmpty()) {
      m_proxyPoolAssignPending = true;
      m_proxyPoolAssignPendingProfileIds = profileIds;
      appendLogLine(QStringLiteral("proxy_pool_assign waiting_for_health_check n=%1").arg(profileIds.size()), QStringLiteral("proxy"));
      startProxyPoolTestBatch(queue);
      return;
    }
  }

  int okN = 0;
  int failN = 0;
  QSet<QString> updatedIds;
  for (const auto& id : profileIds) {
    const QString pid = id.trimmed();
    if (pid.isEmpty()) {
      continue;
    }
    QString err;
    if (!m_repo->assignNextAvailableProxyToProfile(pid, &err)) {
      appendLogLine(QStringLiteral("proxy_pool_assign_failed: %1").arg(err), QStringLiteral("proxy"), pid);
      failN++;
      continue;
    }
    okN++;
    updatedIds.insert(pid);
  }

  if (!updatedIds.isEmpty()) {
    QString loadErr;
    const auto all = m_repo->loadAll(&loadErr);
    if (!loadErr.isEmpty()) {
      appendLogLine(QStringLiteral("profiles_db_error: %1").arg(loadErr), QStringLiteral("db"));
    } else {
      QHash<QString, ProfileListModel::ProfileItem> map;
      map.reserve(all.size());
      for (const auto& it : all) {
        map.insert(it.id, it);
      }
      for (int i = 0; i < m_profiles->items().size(); i++) {
        const auto& cur = m_profiles->items().at(i);
        if (!updatedIds.contains(cur.id)) {
          continue;
        }
        const auto fresh = map.value(cur.id);
        auto updated = cur;
        updated.proxyEnabled = fresh.proxyEnabled;
        updated.proxyType = fresh.proxyType;
        updated.proxyHost = fresh.proxyHost;
        updated.proxyPort = fresh.proxyPort;
        updated.proxyUsername = fresh.proxyUsername;
        updated.proxyPassword = fresh.proxyPassword;
        m_profiles->updateAt(i, updated);
      }
      if (updatedIds.contains(selectedProfileId())) {
        emit selectedProfileChanged();
      }
    }
  }

  appendLogLine(QStringLiteral("proxy_pool_assign done ok=%1 fail=%2").arg(okN).arg(failN), QStringLiteral("proxy"));
  refreshProxyPool();
}

void AppController::testProxyPoolAll() {
  if (!m_proxyPool) {
    return;
  }

  QSet<QString> set;
  QStringList queue;
  for (const auto& it : m_proxyPool->items()) {
    const QString id = it.id.trimmed();
    if (id.isEmpty() || it.disabled || set.contains(id)) {
      continue;
    }
    set.insert(id);
    queue.push_back(id);
  }
  if (queue.isEmpty()) {
    return;
  }
  startProxyPoolTestBatch(queue);
}

void AppController::cancelProxyPoolTestBatch() {
  if (!m_proxyPoolBatchRunning) {
    return;
  }
  m_proxyPoolBatchRunning = false;
  m_proxyPoolBatchId.clear();
  m_proxyPoolBatchQueue.clear();
  m_proxyPoolBatchInFlightStartMs.clear();
  m_proxyPoolBatchInFlightRequestId.clear();
  m_proxyPoolAssignPending = false;
  m_proxyPoolAssignPendingProfileIds.clear();
  if (m_proxyPoolBatchTimer) {
    m_proxyPoolBatchTimer->stop();
  }
  appendLogLine(QStringLiteral("proxy_pool_test_batch cancelled"), QStringLiteral("proxy"));
}

void AppController::setProfileChecked(const QString& profileId, bool checked) {
  const QString pid = profileId.trimmed();
  if (pid.isEmpty()) {
    return;
  }
  const bool had = m_checkedSet.contains(pid);
  if (checked) {
    m_checkedSet.insert(pid);
  } else {
    m_checkedSet.remove(pid);
  }
  if (had == checked) {
    return;
  }
  emitCheckedProfileIds();
}

void AppController::clearCheckedProfiles() {
  if (m_checkedSet.isEmpty()) {
    return;
  }
  m_checkedSet.clear();
  emitCheckedProfileIds();
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

  if (!persistProfile(item)) {
    return;
  }
  const int row = m_profiles->addProfile(item);
  setSelectedProfileIndex(row);
  rebuildGroups();
  applyFilters();
  appendLogLine(QStringLiteral("profile_created id=%1 name=%2").arg(item.id, item.name), QStringLiteral("ui"), item.id);
}

void AppController::deleteSelectedProfile() {
  if (m_selectedProfileIndex < 0 || m_selectedProfileIndex >= m_profiles->rowCount()) {
    return;
  }

  const int removed = m_selectedProfileIndex;
  const QString profileId = m_profiles->items().at(removed).id;
  if (!deleteProfileFromStore(profileId)) {
    return;
  }
  m_profiles->removeAt(removed);
  m_checkedSet.remove(profileId);
  emitCheckedProfileIds();
  rebuildGroups();
  applyFilters();

  const int newIndex = qMin(removed, m_profiles->rowCount() - 1);
  setSelectedProfileIndex(newIndex);
  emit selectedProfileChanged();
}

void AppController::runSelectedProfile() {
  if (!m_ipcConnected) {
    appendLogLine(QStringLiteral("ipc_not_connected"), QStringLiteral("ipc"));
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
  msg.insert(QStringLiteral("data_dir"), it.dataDir);
  if (it.proxyEnabled) {
    QJsonObject proxy;
    proxy.insert(QStringLiteral("enabled"), true);
    proxy.insert(QStringLiteral("type"), it.proxyType);
    proxy.insert(QStringLiteral("host"), it.proxyHost);
    proxy.insert(QStringLiteral("port"), it.proxyPort);
    msg.insert(QStringLiteral("proxy"), proxy);
  }
  m_ipc->send(msg);
  persistRunEvent(it.id, QStringLiteral("profile.start"), QStringLiteral("requested"), QString());

  auto updated = it;
  updated.status = QStringLiteral("starting");
  updated.lastOpenAtMs = QDateTime::currentMSecsSinceEpoch();
  updateSelectedProfileItem(updated);
  persistRunEvent(it.id, QStringLiteral("profile.status"), QStringLiteral("starting"), QString());
}

void AppController::stopSelectedProfile() {
  if (!m_ipcConnected) {
    appendLogLine(QStringLiteral("ipc_not_connected"), QStringLiteral("ipc"));
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
  persistRunEvent(it.id, QStringLiteral("profile.stop"), QStringLiteral("requested"), QString());

  auto updated = it;
  updated.status = QStringLiteral("stopping");
  updateSelectedProfileItem(updated);
  persistRunEvent(it.id, QStringLiteral("profile.status"), QStringLiteral("stopping"), QString());
}

void AppController::testSelectedProxy() {
  if (!m_ipcConnected) {
    appendLogLine(QStringLiteral("ipc_not_connected"), QStringLiteral("ipc"));
    return;
  }
  if (!hasSelectedProfile()) {
    return;
  }

  const QString id = selectedProfileId().trimmed();
  if (id.isEmpty()) {
    return;
  }

  m_proxyLastTestSummary = QStringLiteral("testing...");
  emit proxyLastTestChanged();

  if (m_proxyBatchRunning) {
    if (m_proxyBatchInFlightStartMs.contains(id)) {
      return;
    }
    if (m_proxyBatchQueue.contains(id)) {
      return;
    }
    m_proxyBatchQueue.push_front(id);
    pumpProxyTestQueue();
    return;
  }

  if (!m_proxyBatchTimer) {
    m_proxyBatchTimer = new QTimer(this);
    m_proxyBatchTimer->setInterval(200);
    QObject::connect(m_proxyBatchTimer, &QTimer::timeout, this, [this]() { pumpProxyTestQueue(); });
  }

  m_proxyBatchId = QUuid::createUuid().toString(QUuid::WithoutBraces);
  m_proxyBatchQueue = {id};
  m_proxyBatchInFlightStartMs.clear();
  m_proxyBatchInFlightRequestId.clear();
  m_proxyBatchRunning = true;
  appendLogLine(QStringLiteral("proxy_test_batch start n=1 max_concurrent=%1 timeout_ms=%2")
                    .arg(m_proxyBatchMaxConcurrent)
                    .arg(m_proxyBatchTimeoutMs),
                QStringLiteral("proxy"));

  if (!m_proxyBatchTimer->isActive()) {
    m_proxyBatchTimer->start();
  }
  pumpProxyTestQueue();
}

void AppController::testCheckedProxies() {
  if (!m_ipcConnected) {
    appendLogLine(QStringLiteral("ipc_not_connected"), QStringLiteral("ipc"));
    return;
  }

  const QStringList targets = m_checkedIds.isEmpty() ? QStringList{selectedProfileId()} : m_checkedIds;
  if (targets.isEmpty() || (targets.size() == 1 && targets.at(0).isEmpty())) {
    return;
  }

  QSet<QString> set;
  QStringList queue;
  for (const auto& id : targets) {
    const QString v = id.trimmed();
    if (v.isEmpty() || set.contains(v)) {
      continue;
    }
    set.insert(v);
    queue.push_back(v);
  }
  if (queue.isEmpty()) {
    return;
  }

  if (!m_proxyBatchTimer) {
    m_proxyBatchTimer = new QTimer(this);
    m_proxyBatchTimer->setInterval(200);
    QObject::connect(m_proxyBatchTimer, &QTimer::timeout, this, [this]() { pumpProxyTestQueue(); });
  }

  m_proxyBatchId = QUuid::createUuid().toString(QUuid::WithoutBraces);
  m_proxyBatchQueue = queue;
  m_proxyBatchInFlightStartMs.clear();
  m_proxyBatchInFlightRequestId.clear();
  m_proxyBatchRunning = true;
  appendLogLine(QStringLiteral("proxy_test_batch start n=%1 max_concurrent=%2 timeout_ms=%3")
                    .arg(m_proxyBatchQueue.size())
                    .arg(m_proxyBatchMaxConcurrent)
                    .arg(m_proxyBatchTimeoutMs),
                QStringLiteral("proxy"));

  if (!m_proxyBatchTimer->isActive()) {
    m_proxyBatchTimer->start();
  }
  pumpProxyTestQueue();
}

void AppController::cancelProxyTestBatch() {
  if (!m_proxyBatchRunning) {
    return;
  }

  m_proxyBatchRunning = false;
  m_proxyBatchId.clear();
  m_proxyBatchQueue.clear();
  m_proxyBatchInFlightStartMs.clear();
  m_proxyBatchInFlightRequestId.clear();
  if (m_proxyBatchTimer) {
    m_proxyBatchTimer->stop();
  }
  appendLogLine(QStringLiteral("proxy_test_batch cancelled"), QStringLiteral("proxy"));
}

void AppController::startSelectedVpn() {
  if (!m_ipcConnected) {
    appendLogLine(QStringLiteral("ipc_not_connected"), QStringLiteral("ipc"));
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
  persistRunEvent(it.id, QStringLiteral("vpn.openvpn.start"), QStringLiteral("requested"), QString());
}

void AppController::stopSelectedVpn() {
  if (!m_ipcConnected) {
    appendLogLine(QStringLiteral("ipc_not_connected"), QStringLiteral("ipc"));
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
  persistRunEvent(it.id, QStringLiteral("vpn.openvpn.stop"), QStringLiteral("requested"), QString());
}

void AppController::startCheckedVpns() {
  if (!m_ipcConnected) {
    appendLogLine(QStringLiteral("ipc_not_connected"), QStringLiteral("ipc"));
    return;
  }

  const QStringList targets = m_checkedIds.isEmpty() ? QStringList{selectedProfileId()} : m_checkedIds;
  if (targets.isEmpty() || (targets.size() == 1 && targets.at(0).isEmpty())) {
    return;
  }

  appendLogLine(QStringLiteral("vpn_openvpn_start_batch requested n=%1").arg(targets.size()), QStringLiteral("vpn"));

  for (const auto& id : targets) {
    if (id.isEmpty()) {
      continue;
    }
    for (int i = 0; i < m_profiles->items().size(); i++) {
      const auto& it = m_profiles->items().at(i);
      if (it.id != id) {
        continue;
      }

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
      persistRunEvent(it.id, QStringLiteral("vpn.openvpn.start"), QStringLiteral("requested"), QString());
      break;
    }
  }
}

void AppController::stopCheckedVpns() {
  if (!m_ipcConnected) {
    appendLogLine(QStringLiteral("ipc_not_connected"), QStringLiteral("ipc"));
    return;
  }

  const QStringList targets = m_checkedIds.isEmpty() ? QStringList{selectedProfileId()} : m_checkedIds;
  if (targets.isEmpty() || (targets.size() == 1 && targets.at(0).isEmpty())) {
    return;
  }

  appendLogLine(QStringLiteral("vpn_openvpn_stop_batch requested n=%1").arg(targets.size()), QStringLiteral("vpn"));

  for (const auto& id : targets) {
    if (id.isEmpty()) {
      continue;
    }
    QJsonObject msg;
    msg.insert(QStringLiteral("type"), QStringLiteral("vpn.openvpn.stop"));
    msg.insert(QStringLiteral("profile_id"), id);
    m_ipc->send(msg);
    persistRunEvent(id, QStringLiteral("vpn.openvpn.stop"), QStringLiteral("requested"), QString());
  }
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
      appendLogLine(QStringLiteral("agent_started"), QStringLiteral("agent"));
      m_ipc->connectToAgent();
    });

    QObject::connect(m_agent, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus st) {
      emit agentRunningChanged();
      appendLogLine(QStringLiteral("agent_finished exitCode=%1 exitStatus=%2")
                        .arg(exitCode)
                        .arg(st == QProcess::NormalExit ? "normal" : "crash"),
                    QStringLiteral("agent"));
    });

    QObject::connect(m_agent, &QProcess::readyReadStandardOutput, this, [this]() {
      const QByteArray bytes = m_agent->readAllStandardOutput();
      const auto lines = QString::fromUtf8(bytes).split('\n', Qt::SkipEmptyParts);
      for (const auto& line : lines) {
        appendLogLine(QStringLiteral("agent_out: %1").arg(line), QStringLiteral("agent_out"));
      }
    });

    QObject::connect(m_agent, &QProcess::readyReadStandardError, this, [this]() {
      const QByteArray bytes = m_agent->readAllStandardError();
      const auto lines = QString::fromUtf8(bytes).split('\n', Qt::SkipEmptyParts);
      for (const auto& line : lines) {
        appendLogLine(QStringLiteral("agent_err: %1").arg(line), QStringLiteral("agent_err"));
      }
    });
  }

  const QString program = agentProgramPath();
  if (program.isEmpty()) {
    appendLogLine(QStringLiteral("agent_not_found"), QStringLiteral("agent"));
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

void AppController::setLogViewMode(const QString& mode) {
  const QString v = mode.trimmed();
  if (v.isEmpty()) {
    return;
  }
  m_logViewMode = v;
  m_liveLogsEnabled = (v == QStringLiteral("实时日志"));
  if (m_liveLogsEnabled) {
    m_logs->clear();
    appendLogLine(QStringLiteral("切换到实时日志"), QStringLiteral("ui"));
  }
}

void AppController::loadHistory(const QString& mode, const QString& keyword, const QString& from, const QString& to) {
  loadHistory(mode, keyword, from, to, QStringLiteral("当前Profile"));
}

void AppController::loadHistory(const QString& mode,
                               const QString& keyword,
                               const QString& from,
                               const QString& to,
                               const QString& scope) {
  if (!m_repo) {
    return;
  }

  const QString v = mode.trimmed();
  const qint64 fromMs = parseTsMs(from, false);
  const qint64 toMs = parseTsMs(to, true);

  QString pid;
  if (v == QStringLiteral("历史日志")) {
    const QString sc = scope.trimmed();
    if (sc == QStringLiteral("全局")) {
      pid.clear();
    } else {
      pid = selectedProfileId();
    }
  } else if (v == QStringLiteral("运行记录") || v == QStringLiteral("代理自检")) {
    pid = selectedProfileId();
    if (pid.isEmpty()) {
      appendLogLine(QStringLiteral("未选择 Profile"), QStringLiteral("ui"));
      return;
    }
  }

  QString err;
  QStringList lines;
  if (v == QStringLiteral("历史日志")) {
    const QString sc = scope.trimmed();
    if (sc == QStringLiteral("全局")) {
      lines = m_repo->queryLogsAllProfiles(keyword, fromMs, toMs, 500, &err);
    } else {
      lines = m_repo->queryLogs(pid, keyword, fromMs, toMs, 500, &err);
    }
  } else if (v == QStringLiteral("运行记录")) {
    lines = m_repo->queryRuns(pid, keyword, fromMs, toMs, 100, &err);
  } else if (v == QStringLiteral("代理自检")) {
    lines = m_repo->queryProxyTests(pid, keyword, fromMs, toMs, 100, &err);
  } else {
    setLogViewMode(QStringLiteral("实时日志"));
    return;
  }

  if (!err.isEmpty()) {
    appendLogLine(QStringLiteral("history_load_error: %1").arg(err), QStringLiteral("db"), pid);
    return;
  }

  m_liveLogsEnabled = false;
  m_logViewMode = v;
  m_logs->clear();
  for (const auto& line : lines) {
    m_logs->appendLine(line);
  }
  appendLogLine(QStringLiteral("history_loaded type=%1 rows=%2").arg(v).arg(lines.size()), QStringLiteral("ui"), pid);
}

QString AppController::exportHistory(const QString& mode, const QString& keyword, const QString& from, const QString& to) {
  return exportHistory(mode, keyword, from, to, QStringLiteral("当前Profile"));
}

QString AppController::exportHistory(const QString& mode,
                                    const QString& keyword,
                                    const QString& from,
                                    const QString& to,
                                    const QString& scope) {
  if (!m_repo) {
    return {};
  }

  const QString v = mode.trimmed();
  const qint64 fromMs = parseTsMs(from, false);
  const qint64 toMs = parseTsMs(to, true);

  QString pid;
  if (v == QStringLiteral("历史日志")) {
    const QString sc = scope.trimmed();
    if (sc == QStringLiteral("全局")) {
      pid.clear();
    } else {
      pid = selectedProfileId();
    }
  } else if (v == QStringLiteral("运行记录") || v == QStringLiteral("代理自检")) {
    pid = selectedProfileId();
    if (pid.isEmpty()) {
      appendLogLine(QStringLiteral("未选择 Profile"), QStringLiteral("ui"));
      return {};
    }
  }

  QString err;
  QStringList lines;
  int limit = 500;
  if (v == QStringLiteral("历史日志")) {
    const QString sc = scope.trimmed();
    if (sc == QStringLiteral("全局")) {
      lines = m_repo->queryLogsAllProfiles(keyword, fromMs, toMs, 500, &err);
    } else {
      lines = m_repo->queryLogs(pid, keyword, fromMs, toMs, 500, &err);
    }
    limit = 500;
  } else if (v == QStringLiteral("运行记录")) {
    lines = m_repo->queryRuns(pid, keyword, fromMs, toMs, 100, &err);
    limit = 100;
  } else if (v == QStringLiteral("代理自检")) {
    lines = m_repo->queryProxyTests(pid, keyword, fromMs, toMs, 100, &err);
    limit = 100;
  } else {
    appendLogLine(QStringLiteral("仅支持导出 历史日志/运行记录/代理自检"), QStringLiteral("ui"));
    return {};
  }

  if (!err.isEmpty()) {
    appendLogLine(QStringLiteral("history_export_query_error: %1").arg(err), QStringLiteral("db"), pid);
    return {};
  }

  const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/exports");
  QDir().mkpath(dir);
  const QString fileName =
      QStringLiteral("%1/%2_%3_%4_%5.log")
          .arg(dir)
          .arg(v)
          .arg(pid.isEmpty() ? QStringLiteral("global") : pid.left(8))
          .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")))
          .arg(limit);

  QSaveFile f(fileName);
  if (!f.open(QIODevice::WriteOnly)) {
    appendLogLine(QStringLiteral("history_export_open_failed: %1").arg(fileName), QStringLiteral("ui"));
    return {};
  }
  for (const auto& line : lines) {
    f.write(line.toUtf8());
    f.write("\n");
  }
  if (!f.commit()) {
    appendLogLine(QStringLiteral("history_export_commit_failed: %1").arg(fileName), QStringLiteral("ui"));
    return {};
  }

  appendLogLine(QStringLiteral("history_exported: %1").arg(fileName), QStringLiteral("ui"), pid);
  return fileName;
}

bool AppController::selectProfileByIdPrefix(const QString& prefix) {
  const QString p = prefix.trimmed();
  if (p.isEmpty()) {
    return false;
  }
  for (int i = 0; i < m_profiles->items().size(); i++) {
    const auto& it = m_profiles->items().at(i);
    if (it.id.startsWith(p)) {
      setSelectedProfileIndex(i);
      return true;
    }
  }
  return false;
}

QString AppController::legacyProfilesJsonPath() const {
  const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  QDir().mkpath(dir);
  return dir + QStringLiteral("/profiles.json");
}

void AppController::loadProfiles() {
  m_profiles->clear();

  auto makeDefault = []() {
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
    return item;
  };

  if (!m_repo) {
    return;
  }

  const QString dbPath = m_repo->dbFilePath();
  const bool hadDbFile = QFile::exists(dbPath);

  QString dbError;
  if (!m_repo->ensureSchema(&dbError)) {
    appendLogLine(QStringLiteral("profiles_db_error: %1").arg(dbError), QStringLiteral("db"));
    return;
  }

  if (!hadDbFile) {
    QVector<ProfileListModel::ProfileItem> imported;
    QFile f(legacyProfilesJsonPath());
    if (f.exists() && f.open(QIODevice::ReadOnly)) {
      QJsonParseError err{};
      const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
      if (err.error == QJsonParseError::NoError && doc.isArray()) {
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
          item.timezone =
              o.value(QStringLiteral("timezone")).toString(QString::fromUtf8(QTimeZone::systemTimeZoneId()));
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
          imported.push_back(item);
        }
      }
    }

    if (imported.isEmpty()) {
      imported.push_back(makeDefault());
    }

    QString importErr;
    if (!m_repo->upsertMany(imported, &importErr)) {
      appendLogLine(QStringLiteral("profiles_db_import_error: %1").arg(importErr), QStringLiteral("db"));
    }
  }

  QString loadErr;
  const auto items = m_repo->loadAll(&loadErr);
  if (!loadErr.isEmpty()) {
    appendLogLine(QStringLiteral("profiles_db_load_error: %1").arg(loadErr), QStringLiteral("db"));
  }

  if (items.isEmpty()) {
    const auto fallback = makeDefault();
    persistProfile(fallback);
    m_profiles->addProfile(fallback);
    return;
  }

  for (const auto& it : items) {
    m_profiles->addProfile(it);
  }
}

bool AppController::persistProfile(const ProfileListModel::ProfileItem& item) {
  if (!m_repo) {
    return false;
  }
  QString err;
  if (!m_repo->upsert(item, &err)) {
    appendLogLine(QStringLiteral("profiles_db_upsert_error: %1").arg(err), QStringLiteral("db"), item.id);
    return false;
  }
  return true;
}

bool AppController::deleteProfileFromStore(const QString& profileId) {
  if (!m_repo) {
    return false;
  }
  QString err;
  if (!m_repo->remove(profileId, &err)) {
    appendLogLine(QStringLiteral("profiles_db_delete_error: %1").arg(err), QStringLiteral("db"), profileId);
    return false;
  }
  return true;
}

void AppController::appendLogLine(const QString& message, const QString& source, const QString& profileId) {
  appendLogRaw(QStringLiteral("[%1] %2").arg(ts(), message), source, profileId);
}

void AppController::appendLogRaw(const QString& line, const QString& source, const QString& profileId) {
  if (m_liveLogsEnabled) {
    m_logs->appendLine(line);
  }
  if (!m_repo) {
    return;
  }
  QString err;
  m_repo->insertLogLine(profileId, source, line, QDateTime::currentMSecsSinceEpoch(), &err);
}

void AppController::persistRunEvent(const QString& profileId,
                                   const QString& event,
                                   const QString& status,
                                   const QString& detail) {
  if (!m_repo || profileId.isEmpty()) {
    return;
  }
  QString err;
  if (!m_repo->insertRunEvent(profileId, event, status, detail, QDateTime::currentMSecsSinceEpoch(), &err)) {
    m_logs->appendLine(QStringLiteral("[%1] runs_db_error: %2").arg(ts(), err));
  }
}

void AppController::persistProxyTestRun(const QString& profileId,
                                       bool ok,
                                       const QString& observedIp,
                                       int statusCode,
                                       int durationMs,
                                       int qtError,
                                       const QString& errorText) {
  if (!m_repo || profileId.isEmpty()) {
    return;
  }
  QString err;
  if (!m_repo->insertProxyTestRun(profileId, ok, observedIp, statusCode, durationMs, qtError, errorText,
                                  QDateTime::currentMSecsSinceEpoch(), &err)) {
    m_logs->appendLine(QStringLiteral("[%1] proxy_test_db_error: %2").arg(ts(), err));
  }
}

void AppController::rebuildGroups() {
  QSet<QString> set;
  for (const auto& it : m_profiles->items()) {
    const QString g = it.group.trimmed();
    if (!g.isEmpty()) {
      set.insert(g);
    }
  }

  QStringList groups;
  groups.push_back(QStringLiteral("所有分组"));
  auto rest = set.values();
  std::sort(rest.begin(), rest.end(), [](const QString& a, const QString& b) { return a.localeAwareCompare(b) < 0; });
  groups.append(rest);

  if (groups == m_groups) {
    return;
  }
  m_groups = groups;
  emit groupsChanged();
}

void AppController::emitCheckedProfileIds() {
  QStringList ids = m_checkedSet.values();
  std::sort(ids.begin(), ids.end(), [](const QString& a, const QString& b) { return a.localeAwareCompare(b) < 0; });
  if (ids == m_checkedIds) {
    return;
  }
  m_checkedIds = ids;
  if (m_filtered) {
    m_filtered->setCheckedIds(m_checkedIds);
  }
  if (m_showOnlyChecked && m_checkedIds.isEmpty()) {
    m_showOnlyChecked = false;
    if (m_filtered) {
      m_filtered->setOnlyChecked(false);
    }
    emit showOnlyCheckedChanged();
  }
  emit checkedProfileIdsChanged();
}

QString AppController::sendProxyTestRequest(const QString& profileId) {
  if (!m_ipcConnected) {
    return {};
  }

  const QString id = profileId.trimmed();
  if (id.isEmpty()) {
    return {};
  }

  for (int i = 0; i < m_profiles->items().size(); i++) {
    const auto& it = m_profiles->items().at(i);
    if (it.id != id) {
      continue;
    }

    const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);

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
    msg.insert(QStringLiteral("url"), QStringLiteral("https://httpbin.org/ip"));
    msg.insert(QStringLiteral("request_id"), requestId);
    if (!m_proxyBatchId.isEmpty()) {
      msg.insert(QStringLiteral("batch_id"), m_proxyBatchId);
    }
    m_ipc->send(msg);

    if (it.id == selectedProfileId()) {
      m_proxyLastTestSummary = QStringLiteral("testing...");
      emit proxyLastTestChanged();
    }
    return requestId;
  }
  return {};
}

void AppController::pumpProxyTestQueue() {
  if (!m_proxyBatchRunning) {
    return;
  }

  if (!m_ipcConnected) {
    m_proxyBatchRunning = false;
    m_proxyBatchId.clear();
    m_proxyBatchQueue.clear();
    m_proxyBatchInFlightStartMs.clear();
    m_proxyBatchInFlightRequestId.clear();
    if (m_proxyBatchTimer) {
      m_proxyBatchTimer->stop();
    }
    appendLogLine(QStringLiteral("proxy_test_batch aborted ipc_not_connected"), QStringLiteral("proxy"));
    return;
  }

  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  const qint64 timeoutMs = qMax<qint64>(1000, m_proxyBatchTimeoutMs);

  QStringList timedOut;
  timedOut.reserve(m_proxyBatchInFlightStartMs.size());
  for (auto it = m_proxyBatchInFlightStartMs.constBegin(); it != m_proxyBatchInFlightStartMs.constEnd(); ++it) {
    if (now - it.value() > timeoutMs) {
      timedOut.push_back(it.key());
    }
  }
  for (const auto& id : timedOut) {
    finishProxyTestSlot(id, QStringLiteral("timeout"));

    for (int i = 0; i < m_profiles->items().size(); i++) {
      const auto& it = m_profiles->items().at(i);
      if (it.id != id) {
        continue;
      }
      auto updated = it;
      updated.proxyLastOk = false;
      updated.proxyLastObservedIp.clear();
      updated.proxyLastError = QStringLiteral("timeout");
      updated.proxyLastAtMs = now;
      m_profiles->updateAt(i, updated);
      break;
    }

    if (id == selectedProfileId()) {
      m_proxyLastTestSummary = QStringLiteral("timeout");
      emit proxyLastTestChanged();
    }
    appendLogLine(QStringLiteral("proxy_test: TIMEOUT"), QStringLiteral("proxy"), id);
  }

  const int maxConc = qMax(1, m_proxyBatchMaxConcurrent);
  while (m_proxyBatchInFlightStartMs.size() < maxConc && !m_proxyBatchQueue.isEmpty()) {
    const QString id = m_proxyBatchQueue.takeFirst().trimmed();
    if (id.isEmpty() || m_proxyBatchInFlightStartMs.contains(id)) {
      continue;
    }

    const QString requestId = sendProxyTestRequest(id);
    if (requestId.isEmpty()) {
      appendLogLine(QStringLiteral("proxy_test_skip profile_not_found id=%1").arg(id), QStringLiteral("proxy"));
      continue;
    }
    m_proxyBatchInFlightStartMs.insert(id, now);
    m_proxyBatchInFlightRequestId.insert(id, requestId);
  }

  if (m_proxyBatchQueue.isEmpty() && m_proxyBatchInFlightStartMs.isEmpty()) {
    m_proxyBatchRunning = false;
    m_proxyBatchId.clear();
    if (m_proxyBatchTimer) {
      m_proxyBatchTimer->stop();
    }
    appendLogLine(QStringLiteral("proxy_test_batch done"), QStringLiteral("proxy"));
  }
}

void AppController::finishProxyTestSlot(const QString& profileId, const QString& reason) {
  Q_UNUSED(reason);
  if (profileId.isEmpty()) {
    return;
  }
  m_proxyBatchInFlightStartMs.remove(profileId);
  m_proxyBatchInFlightRequestId.remove(profileId);
}

QString AppController::sendProxyPoolTestRequest(const QString& proxyId) {
  if (!m_ipcConnected) {
    return {};
  }
  if (!m_proxyPool) {
    return {};
  }

  const QString id = proxyId.trimmed();
  if (id.isEmpty()) {
    return {};
  }

  for (const auto& it : m_proxyPool->items()) {
    if (it.id != id) {
      continue;
    }
    if (it.disabled) {
      return {};
    }

    const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);

    QJsonObject proxy;
    proxy.insert(QStringLiteral("enabled"), true);
    proxy.insert(QStringLiteral("type"), it.type);
    proxy.insert(QStringLiteral("host"), it.host);
    proxy.insert(QStringLiteral("port"), it.port);
    proxy.insert(QStringLiteral("username"), it.username);
    proxy.insert(QStringLiteral("password"), it.password);

    QJsonObject msg;
    msg.insert(QStringLiteral("type"), QStringLiteral("proxy_pool.test"));
    msg.insert(QStringLiteral("proxy_id"), it.id);
    msg.insert(QStringLiteral("proxy"), proxy);
    msg.insert(QStringLiteral("url"), QStringLiteral("https://httpbin.org/ip"));
    msg.insert(QStringLiteral("request_id"), requestId);
    if (!m_proxyPoolBatchId.isEmpty()) {
      msg.insert(QStringLiteral("batch_id"), m_proxyPoolBatchId);
    }
    m_ipc->send(msg);
    return requestId;
  }
  return {};
}

void AppController::pumpProxyPoolTestQueue() {
  if (!m_proxyPoolBatchRunning) {
    return;
  }

  if (!m_ipcConnected) {
    m_proxyPoolBatchRunning = false;
    m_proxyPoolBatchId.clear();
    m_proxyPoolBatchQueue.clear();
    m_proxyPoolBatchInFlightStartMs.clear();
    m_proxyPoolBatchInFlightRequestId.clear();
    if (m_proxyPoolBatchTimer) {
      m_proxyPoolBatchTimer->stop();
    }
    appendLogLine(QStringLiteral("proxy_pool_test_batch aborted ipc_not_connected"), QStringLiteral("proxy"));
    return;
  }

  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  const qint64 timeoutMs = qMax<qint64>(1000, m_proxyPoolBatchTimeoutMs);

  QStringList timedOut;
  timedOut.reserve(m_proxyPoolBatchInFlightStartMs.size());
  for (auto it = m_proxyPoolBatchInFlightStartMs.constBegin(); it != m_proxyPoolBatchInFlightStartMs.constEnd(); ++it) {
    if (now - it.value() > timeoutMs) {
      timedOut.push_back(it.key());
    }
  }
  for (const auto& id : timedOut) {
    finishProxyPoolTestSlot(id, QStringLiteral("timeout"));

    if (m_repo) {
      QString err;
      m_repo->updateProxyHealth(id, false, QString(), QStringLiteral("timeout"), now, &err);
    }

    if (m_proxyPool) {
      for (int i = 0; i < m_proxyPool->items().size(); i++) {
        const auto& it = m_proxyPool->items().at(i);
        if (it.id != id) {
          continue;
        }
        auto updated = it;
        updated.lastOk = false;
        updated.lastIp.clear();
        updated.lastError = QStringLiteral("timeout");
        updated.lastAtMs = now;
        m_proxyPool->updateAt(i, updated);
        break;
      }
    }
    appendLogLine(QStringLiteral("proxy_pool_test TIMEOUT id=%1").arg(id), QStringLiteral("proxy"));
  }

  const int maxConc = qMax(1, m_proxyPoolBatchMaxConcurrent);
  while (m_proxyPoolBatchInFlightStartMs.size() < maxConc && !m_proxyPoolBatchQueue.isEmpty()) {
    const QString id = m_proxyPoolBatchQueue.takeFirst().trimmed();
    if (id.isEmpty() || m_proxyPoolBatchInFlightStartMs.contains(id)) {
      continue;
    }
    const QString requestId = sendProxyPoolTestRequest(id);
    if (requestId.isEmpty()) {
      continue;
    }
    m_proxyPoolBatchInFlightStartMs.insert(id, now);
    m_proxyPoolBatchInFlightRequestId.insert(id, requestId);
  }

  if (m_proxyPoolBatchQueue.isEmpty() && m_proxyPoolBatchInFlightStartMs.isEmpty()) {
    m_proxyPoolBatchRunning = false;
    m_proxyPoolBatchId.clear();
    if (m_proxyPoolBatchTimer) {
      m_proxyPoolBatchTimer->stop();
    }
    appendLogLine(QStringLiteral("proxy_pool_test_batch done"), QStringLiteral("proxy"));

    if (m_proxyPoolAssignPending && !m_proxyPoolAssignPendingProfileIds.isEmpty()) {
      const auto pending = m_proxyPoolAssignPendingProfileIds;
      m_proxyPoolAssignPending = false;
      m_proxyPoolAssignPendingProfileIds.clear();
      assignProxyPoolToProfileIds(pending, false);
    }
  }
}

void AppController::finishProxyPoolTestSlot(const QString& proxyId, const QString& reason) {
  Q_UNUSED(reason);
  if (proxyId.isEmpty()) {
    return;
  }
  m_proxyPoolBatchInFlightStartMs.remove(proxyId);
  m_proxyPoolBatchInFlightRequestId.remove(proxyId);
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
