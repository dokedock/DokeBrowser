#pragma once

#include <QAbstractItemModel>
#include <QObject>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <QUrl>

#include "LogListModel.h"
#include "ProfileListModel.h"
#include "ProxyPoolModel.h"

class IpcClient;
class ProfileFilterModel;
class ProfileRepository;

class QProcess;
class QTimer;
class QNetworkAccessManager;

class AppController : public QObject {
  Q_OBJECT

  Q_PROPERTY(ProfileListModel* profiles READ profiles CONSTANT)
  Q_PROPERTY(QAbstractItemModel* filteredProfiles READ filteredProfiles CONSTANT)
  Q_PROPERTY(LogListModel* logs READ logs CONSTANT)
  Q_PROPERTY(ProxyPoolModel* proxyPool READ proxyPool CONSTANT)
  Q_PROPERTY(QStringList groups READ groups NOTIFY groupsChanged)
  Q_PROPERTY(QStringList checkedProfileIds READ checkedProfileIds NOTIFY checkedProfileIdsChanged)
  Q_PROPERTY(bool showOnlyChecked READ showOnlyChecked NOTIFY showOnlyCheckedChanged)

  Q_PROPERTY(int selectedProfileIndex READ selectedProfileIndex WRITE setSelectedProfileIndex NOTIFY
                 selectedProfileIndexChanged)
  Q_PROPERTY(QString selectedProfileId READ selectedProfileId NOTIFY selectedProfileChanged)
  Q_PROPERTY(QString selectedProfileName READ selectedProfileName WRITE setSelectedProfileName NOTIFY
                 selectedProfileChanged)
  Q_PROPERTY(QString selectedProfileGroup READ selectedProfileGroup WRITE setSelectedProfileGroup NOTIFY
                 selectedProfileChanged)
  Q_PROPERTY(QString selectedProfileRemark READ selectedProfileRemark WRITE setSelectedProfileRemark NOTIFY
                 selectedProfileChanged)
  Q_PROPERTY(QString selectedProfileStatus READ selectedProfileStatus NOTIFY selectedProfileChanged)
  Q_PROPERTY(qint64 selectedProfileCreatedAtMs READ selectedProfileCreatedAtMs NOTIFY selectedProfileChanged)
  Q_PROPERTY(qint64 selectedProfileLastOpenAtMs READ selectedProfileLastOpenAtMs NOTIFY selectedProfileChanged)

  Q_PROPERTY(QString selectedProfileDataDir READ selectedProfileDataDir WRITE setSelectedProfileDataDir NOTIFY
                 selectedProfileChanged)
  Q_PROPERTY(QString selectedProfileBrowserEngine READ selectedProfileBrowserEngine WRITE setSelectedProfileBrowserEngine NOTIFY
                 selectedProfileChanged)
  Q_PROPERTY(QString selectedProfileBrowserEngineStatus READ selectedProfileBrowserEngineStatus NOTIFY selectedProfileChanged)
  Q_PROPERTY(QString selectedProfileEngineConfigJson READ selectedProfileEngineConfigJson WRITE setSelectedProfileEngineConfigJson NOTIFY
                 selectedProfileChanged)
  Q_PROPERTY(QString selectedProfileDokeExecutable READ selectedProfileDokeExecutable WRITE setSelectedProfileDokeExecutable NOTIFY
                 selectedProfileChanged)
  Q_PROPERTY(QString selectedProfileDokeExtraArgs READ selectedProfileDokeExtraArgs WRITE setSelectedProfileDokeExtraArgs NOTIFY
                 selectedProfileChanged)
  Q_PROPERTY(bool selectedProfileDokeNativeFingerprint READ selectedProfileDokeNativeFingerprint WRITE
                 setSelectedProfileDokeNativeFingerprint NOTIFY selectedProfileChanged)
  Q_PROPERTY(bool selectedProfileDokeNativeProxy READ selectedProfileDokeNativeProxy WRITE setSelectedProfileDokeNativeProxy NOTIFY
                 selectedProfileChanged)
  Q_PROPERTY(bool selectedProfileDokeNativeGeoip READ selectedProfileDokeNativeGeoip WRITE setSelectedProfileDokeNativeGeoip NOTIFY
                 selectedProfileChanged)
  Q_PROPERTY(bool selectedProfileDokeNativeHumanize READ selectedProfileDokeNativeHumanize WRITE setSelectedProfileDokeNativeHumanize NOTIFY
                 selectedProfileChanged)
  Q_PROPERTY(QString selectedProfileFingerprintSeed READ selectedProfileFingerprintSeed WRITE setSelectedProfileFingerprintSeed NOTIFY
                 selectedProfileChanged)
  Q_PROPERTY(QString selectedProfileStartUrl READ selectedProfileStartUrl WRITE setSelectedProfileStartUrl NOTIFY
                 selectedProfileChanged)
  Q_PROPERTY(bool selectedProfileHumanizeEnabled READ selectedProfileHumanizeEnabled WRITE setSelectedProfileHumanizeEnabled NOTIFY
                 selectedProfileChanged)
  Q_PROPERTY(bool selectedProfileGeoipEnabled READ selectedProfileGeoipEnabled WRITE setSelectedProfileGeoipEnabled NOTIFY
                 selectedProfileChanged)
  Q_PROPERTY(QString selectedProfileFingerprintMode READ selectedProfileFingerprintMode WRITE setSelectedProfileFingerprintMode NOTIFY
                 selectedProfileChanged)
  Q_PROPERTY(QString selectedProfileLanguage READ selectedProfileLanguage WRITE setSelectedProfileLanguage NOTIFY
                 selectedProfileChanged)
  Q_PROPERTY(QString selectedProfileUserAgent READ selectedProfileUserAgent WRITE setSelectedProfileUserAgent NOTIFY
                 selectedProfileChanged)
  Q_PROPERTY(QString selectedProfilePlatform READ selectedProfilePlatform WRITE setSelectedProfilePlatform NOTIFY
                 selectedProfileChanged)
  Q_PROPERTY(int selectedProfileHardwareConcurrency READ selectedProfileHardwareConcurrency WRITE setSelectedProfileHardwareConcurrency NOTIFY
                 selectedProfileChanged)
  Q_PROPERTY(int selectedProfileDeviceMemoryGb READ selectedProfileDeviceMemoryGb WRITE setSelectedProfileDeviceMemoryGb NOTIFY
                 selectedProfileChanged)
  Q_PROPERTY(double selectedProfileDeviceScaleFactor READ selectedProfileDeviceScaleFactor WRITE setSelectedProfileDeviceScaleFactor NOTIFY
                 selectedProfileChanged)
  Q_PROPERTY(QString selectedProfileTimezone READ selectedProfileTimezone WRITE setSelectedProfileTimezone NOTIFY
                 selectedProfileChanged)
  Q_PROPERTY(QString selectedProfileResolution READ selectedProfileResolution WRITE setSelectedProfileResolution NOTIFY
                 selectedProfileChanged)
  Q_PROPERTY(bool selectedProfileTouchEnabled READ selectedProfileTouchEnabled WRITE setSelectedProfileTouchEnabled NOTIFY
                 selectedProfileChanged)

  Q_PROPERTY(bool selectedProxyEnabled READ selectedProxyEnabled WRITE setSelectedProxyEnabled NOTIFY
                 selectedProfileChanged)
  Q_PROPERTY(QString selectedProxyType READ selectedProxyType WRITE setSelectedProxyType NOTIFY
                 selectedProfileChanged)
  Q_PROPERTY(QString selectedProxyHost READ selectedProxyHost WRITE setSelectedProxyHost NOTIFY
                 selectedProfileChanged)
  Q_PROPERTY(int selectedProxyPort READ selectedProxyPort WRITE setSelectedProxyPort NOTIFY selectedProfileChanged)
  Q_PROPERTY(QString selectedProxyUsername READ selectedProxyUsername WRITE setSelectedProxyUsername NOTIFY
                 selectedProfileChanged)
  Q_PROPERTY(QString selectedProxyPassword READ selectedProxyPassword WRITE setSelectedProxyPassword NOTIFY
                 selectedProfileChanged)
  Q_PROPERTY(QString proxyLastTestSummary READ proxyLastTestSummary NOTIFY proxyLastTestChanged)

  Q_PROPERTY(bool selectedVpnEnabled READ selectedVpnEnabled WRITE setSelectedVpnEnabled NOTIFY selectedProfileChanged)
  Q_PROPERTY(QString selectedOpenvpnExe READ selectedOpenvpnExe WRITE setSelectedOpenvpnExe NOTIFY selectedProfileChanged)
  Q_PROPERTY(QString selectedOpenvpnConfig READ selectedOpenvpnConfig WRITE setSelectedOpenvpnConfig NOTIFY selectedProfileChanged)
  Q_PROPERTY(bool selectedOpenvpnUseSocks READ selectedOpenvpnUseSocks WRITE setSelectedOpenvpnUseSocks NOTIFY selectedProfileChanged)
  Q_PROPERTY(QString selectedOpenvpnSocksHost READ selectedOpenvpnSocksHost WRITE setSelectedOpenvpnSocksHost NOTIFY selectedProfileChanged)
  Q_PROPERTY(int selectedOpenvpnSocksPort READ selectedOpenvpnSocksPort WRITE setSelectedOpenvpnSocksPort NOTIFY selectedProfileChanged)
  Q_PROPERTY(QString selectedOpenvpnSocksUsername READ selectedOpenvpnSocksUsername WRITE setSelectedOpenvpnSocksUsername NOTIFY selectedProfileChanged)
  Q_PROPERTY(QString selectedOpenvpnSocksPassword READ selectedOpenvpnSocksPassword WRITE setSelectedOpenvpnSocksPassword NOTIFY selectedProfileChanged)
  Q_PROPERTY(QString selectedVpnStatus READ selectedVpnStatus NOTIFY selectedVpnStatusChanged)

  Q_PROPERTY(bool agentRunning READ agentRunning NOTIFY agentRunningChanged)
  Q_PROPERTY(bool ipcConnected READ ipcConnected NOTIFY ipcConnectedChanged)

public:
  explicit AppController(QObject* parent = nullptr);
  ~AppController() override;

  ProfileListModel* profiles();
  QAbstractItemModel* filteredProfiles();
  LogListModel* logs();
  ProxyPoolModel* proxyPool();
  QStringList groups() const;
  QStringList checkedProfileIds() const;
  bool showOnlyChecked() const;

  int selectedProfileIndex() const;
  void setSelectedProfileIndex(int index);
  QString selectedProfileId() const;
  QString selectedProfileName() const;
  void setSelectedProfileName(const QString& value);
  QString selectedProfileGroup() const;
  void setSelectedProfileGroup(const QString& value);
  QString selectedProfileRemark() const;
  void setSelectedProfileRemark(const QString& value);
  QString selectedProfileStatus() const;
  qint64 selectedProfileCreatedAtMs() const;
  qint64 selectedProfileLastOpenAtMs() const;

  QString selectedProfileDataDir() const;
  void setSelectedProfileDataDir(const QString& value);
  QString selectedProfileBrowserEngine() const;
  void setSelectedProfileBrowserEngine(const QString& value);
  QString selectedProfileBrowserEngineStatus() const;
  QString selectedProfileEngineConfigJson() const;
  void setSelectedProfileEngineConfigJson(const QString& value);
  QString selectedProfileDokeExecutable() const;
  void setSelectedProfileDokeExecutable(const QString& value);
  QString selectedProfileDokeExtraArgs() const;
  void setSelectedProfileDokeExtraArgs(const QString& value);
  bool selectedProfileDokeNativeFingerprint() const;
  void setSelectedProfileDokeNativeFingerprint(bool value);
  bool selectedProfileDokeNativeProxy() const;
  void setSelectedProfileDokeNativeProxy(bool value);
  bool selectedProfileDokeNativeGeoip() const;
  void setSelectedProfileDokeNativeGeoip(bool value);
  bool selectedProfileDokeNativeHumanize() const;
  void setSelectedProfileDokeNativeHumanize(bool value);
  QString selectedProfileFingerprintSeed() const;
  void setSelectedProfileFingerprintSeed(const QString& value);
  QString selectedProfileStartUrl() const;
  void setSelectedProfileStartUrl(const QString& value);
  bool selectedProfileHumanizeEnabled() const;
  void setSelectedProfileHumanizeEnabled(bool value);
  bool selectedProfileGeoipEnabled() const;
  void setSelectedProfileGeoipEnabled(bool value);
  QString selectedProfileFingerprintMode() const;
  void setSelectedProfileFingerprintMode(const QString& value);
  QString selectedProfileLanguage() const;
  void setSelectedProfileLanguage(const QString& value);
  QString selectedProfileUserAgent() const;
  void setSelectedProfileUserAgent(const QString& value);
  QString selectedProfilePlatform() const;
  void setSelectedProfilePlatform(const QString& value);
  int selectedProfileHardwareConcurrency() const;
  void setSelectedProfileHardwareConcurrency(int value);
  int selectedProfileDeviceMemoryGb() const;
  void setSelectedProfileDeviceMemoryGb(int value);
  double selectedProfileDeviceScaleFactor() const;
  void setSelectedProfileDeviceScaleFactor(double value);
  QString selectedProfileTimezone() const;
  void setSelectedProfileTimezone(const QString& value);
  QString selectedProfileResolution() const;
  void setSelectedProfileResolution(const QString& value);
  bool selectedProfileTouchEnabled() const;
  void setSelectedProfileTouchEnabled(bool value);

  bool selectedProxyEnabled() const;
  void setSelectedProxyEnabled(bool value);
  QString selectedProxyType() const;
  void setSelectedProxyType(const QString& value);
  QString selectedProxyHost() const;
  void setSelectedProxyHost(const QString& value);
  int selectedProxyPort() const;
  void setSelectedProxyPort(int value);
  QString selectedProxyUsername() const;
  void setSelectedProxyUsername(const QString& value);
  QString selectedProxyPassword() const;
  void setSelectedProxyPassword(const QString& value);
  QString proxyLastTestSummary() const;

  bool selectedVpnEnabled() const;
  void setSelectedVpnEnabled(bool value);
  QString selectedOpenvpnExe() const;
  void setSelectedOpenvpnExe(const QString& value);
  QString selectedOpenvpnConfig() const;
  void setSelectedOpenvpnConfig(const QString& value);
  bool selectedOpenvpnUseSocks() const;
  void setSelectedOpenvpnUseSocks(bool value);
  QString selectedOpenvpnSocksHost() const;
  void setSelectedOpenvpnSocksHost(const QString& value);
  int selectedOpenvpnSocksPort() const;
  void setSelectedOpenvpnSocksPort(int value);
  QString selectedOpenvpnSocksUsername() const;
  void setSelectedOpenvpnSocksUsername(const QString& value);
  QString selectedOpenvpnSocksPassword() const;
  void setSelectedOpenvpnSocksPassword(const QString& value);
  QString selectedVpnStatus() const;

  bool agentRunning() const;
  bool ipcConnected() const;

  Q_INVOKABLE void createProfile();
  Q_INVOKABLE void deleteSelectedProfile();
  Q_INVOKABLE void runSelectedProfile();
  Q_INVOKABLE void stopSelectedProfile();
  Q_INVOKABLE void testSelectedProxy();
  Q_INVOKABLE void testCheckedProxies();
  Q_INVOKABLE void cancelProxyTestBatch();
  Q_INVOKABLE void startSelectedVpn();
  Q_INVOKABLE void stopSelectedVpn();
  Q_INVOKABLE void startCheckedVpns();
  Q_INVOKABLE void stopCheckedVpns();
  Q_INVOKABLE bool selectProfileById(const QString& profileId);
  Q_INVOKABLE void runCheckedProfiles();
  Q_INVOKABLE void stopCheckedProfiles();
  Q_INVOKABLE void deleteCheckedProfiles();
  Q_INVOKABLE void setGroupForCheckedProfiles(const QString& group);
  Q_INVOKABLE int checkedCount() const;
  Q_INVOKABLE bool isProfileChecked(const QString& profileId) const;

  Q_INVOKABLE void startAgent();
  Q_INVOKABLE void stopAgent();
  Q_INVOKABLE void refreshEngineStatus();
  Q_INVOKABLE void probeSelectedBrowserEngine();
  Q_INVOKABLE void setSelectedProfileDokeExecutableFromUrl(const QUrl& url);
  Q_INVOKABLE void clearLogs();
  Q_INVOKABLE void copyLogsToClipboard();

  Q_INVOKABLE void setLogViewMode(const QString& mode);
  Q_INVOKABLE void loadHistory(const QString& mode, const QString& keyword, const QString& from, const QString& to);
  Q_INVOKABLE void loadHistory(const QString& mode,
                               const QString& keyword,
                               const QString& from,
                               const QString& to,
                               const QString& scope);
  Q_INVOKABLE QString exportHistory(const QString& mode, const QString& keyword, const QString& from, const QString& to);
  Q_INVOKABLE QString exportHistory(const QString& mode,
                                    const QString& keyword,
                                    const QString& from,
                                    const QString& to,
                                    const QString& scope);
  Q_INVOKABLE bool selectProfileByIdPrefix(const QString& prefix);

  Q_INVOKABLE void setGroupFilter(const QString& group);
  Q_INVOKABLE void setSearchKeyword(const QString& keyword);
  Q_INVOKABLE void applyFilters();
  Q_INVOKABLE void resetFilters();

  Q_INVOKABLE void setProfileChecked(const QString& profileId, bool checked);
  Q_INVOKABLE void clearCheckedProfiles();
  Q_INVOKABLE void checkAllVisibleProfiles();
  Q_INVOKABLE void uncheckAllVisibleProfiles();
  Q_INVOKABLE void invertCheckedVisibleProfiles();
  Q_INVOKABLE void checkGroupProfiles(const QString& group);
  Q_INVOKABLE void uncheckGroupProfiles(const QString& group);
  Q_INVOKABLE void invertCheckedGroupProfiles(const QString& group);
  Q_INVOKABLE void setShowOnlyChecked(bool enabled);

  Q_INVOKABLE void refreshProxyPool();
  Q_INVOKABLE int importProxyPool(const QString& text);
  Q_INVOKABLE void assignProxyPoolToCheckedProfiles();
  Q_INVOKABLE void releaseProxyPoolFromCheckedProfiles();
  Q_INVOKABLE void rotateProxyForSelectedProfile();
  Q_INVOKABLE void testProxyPoolAll();
  Q_INVOKABLE void cancelProxyPoolTestBatch();

  Q_INVOKABLE void randomizeSelectedFingerprint();

signals:
  void selectedProfileIndexChanged();
  void selectedProfileChanged();
  void proxyLastTestChanged();
  void selectedVpnStatusChanged();
  void agentRunningChanged();
  void ipcConnectedChanged();
  void groupsChanged();
  void checkedProfileIdsChanged();
  void showOnlyCheckedChanged();

private:
  QString legacyProfilesJsonPath() const;
  void loadProfiles();
  bool persistProfile(const ProfileListModel::ProfileItem& item);
  bool deleteProfileFromStore(const QString& profileId);
  void appendLogLine(const QString& message, const QString& source = QStringLiteral("app"), const QString& profileId = {});
  void appendLogRaw(const QString& line, const QString& source = QStringLiteral("app"), const QString& profileId = {});
  void persistRunEvent(const QString& profileId, const QString& event, const QString& status, const QString& detail);
  void persistProxyTestRun(const QString& profileId,
                           bool ok,
                           const QString& observedIp,
                           int statusCode,
                           int durationMs,
                           int qtError,
                           const QString& errorText);
  void rebuildGroups();
  void emitCheckedProfileIds();
  void assignProxyPoolToProfileIds(const QStringList& profileIds, bool allowAutoTest);
  bool hasHealthyFreeProxy() const;
  QString sendProxyTestRequest(const QString& profileId);
  void pumpProxyTestQueue();
  void finishProxyTestSlot(const QString& profileId, const QString& reason);
  QString sendProxyPoolTestRequest(const QString& proxyId);
  void startProxyPoolTestBatch(const QStringList& proxyIds);
  void pumpProxyPoolTestQueue();
  void finishProxyPoolTestSlot(const QString& proxyId, const QString& reason);
  QString newProfileName() const;
  QString agentProgramPath() const;
  bool hasSelectedProfile() const;
  ProfileListModel::ProfileItem selectedProfileItem() const;
  void updateSelectedProfileItem(const ProfileListModel::ProfileItem& item);
  void applyProxyGeoToProfile(const QString& profileId, const QString& ip);

  ProfileListModel* m_profiles = nullptr;
  ProfileFilterModel* m_filtered = nullptr;
  LogListModel* m_logs = nullptr;
  ProxyPoolModel* m_proxyPool = nullptr;
  QStringList m_groups;
  QString m_groupFilter = QStringLiteral("所有分组");
  QString m_searchKeyword;
  QSet<QString> m_checkedSet;
  QStringList m_checkedIds;
  bool m_showOnlyChecked = false;

  int m_selectedProfileIndex = -1;

  QProcess* m_agent = nullptr;
  IpcClient* m_ipc = nullptr;
  ProfileRepository* m_repo = nullptr;
  QNetworkAccessManager* m_http = nullptr;
  bool m_ipcConnected = false;
  bool m_liveLogsEnabled = true;
  QString m_logViewMode = QStringLiteral("实时日志");
  QString m_proxyLastTestSummary;
  bool m_proxyBatchRunning = false;
  QString m_proxyBatchId;
  int m_proxyBatchMaxConcurrent = 3;
  qint64 m_proxyBatchTimeoutMs = 16000;
  QStringList m_proxyBatchQueue;
  QHash<QString, qint64> m_proxyBatchInFlightStartMs;
  QHash<QString, QString> m_proxyBatchInFlightRequestId;
  QTimer* m_proxyBatchTimer = nullptr;
  bool m_proxyPoolBatchRunning = false;
  QString m_proxyPoolBatchId;
  int m_proxyPoolBatchMaxConcurrent = 3;
  qint64 m_proxyPoolBatchTimeoutMs = 16000;
  QStringList m_proxyPoolBatchQueue;
  QHash<QString, qint64> m_proxyPoolBatchInFlightStartMs;
  QHash<QString, QString> m_proxyPoolBatchInFlightRequestId;
  QTimer* m_proxyPoolBatchTimer = nullptr;
  bool m_proxyPoolAssignPending = false;
  QStringList m_proxyPoolAssignPendingProfileIds;
  QHash<QString, QString> m_engineStatusById;
  QHash<QString, QString> m_engineStatusByProfileId;
  QHash<QString, QString> m_vpnStatusByProfileId;
};
