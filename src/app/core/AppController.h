#pragma once

#include <QObject>
#include <QHash>

#include "LogListModel.h"
#include "ProfileListModel.h"

class IpcClient;

class QProcess;

class AppController : public QObject {
  Q_OBJECT

  Q_PROPERTY(ProfileListModel* profiles READ profiles CONSTANT)
  Q_PROPERTY(LogListModel* logs READ logs CONSTANT)

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
  Q_PROPERTY(QString selectedProfileLanguage READ selectedProfileLanguage WRITE setSelectedProfileLanguage NOTIFY
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
  LogListModel* logs();

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
  QString selectedProfileLanguage() const;
  void setSelectedProfileLanguage(const QString& value);
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
  Q_INVOKABLE void startSelectedVpn();
  Q_INVOKABLE void stopSelectedVpn();

  Q_INVOKABLE void startAgent();
  Q_INVOKABLE void stopAgent();
  Q_INVOKABLE void clearLogs();

signals:
  void selectedProfileIndexChanged();
  void selectedProfileChanged();
  void proxyLastTestChanged();
  void selectedVpnStatusChanged();
  void agentRunningChanged();
  void ipcConnectedChanged();

private:
  QString profilesFilePath() const;
  void loadProfiles();
  void saveProfiles();
  QString newProfileName() const;
  QString agentProgramPath() const;
  bool hasSelectedProfile() const;
  ProfileListModel::ProfileItem selectedProfileItem() const;
  void updateSelectedProfileItem(const ProfileListModel::ProfileItem& item);

  ProfileListModel* m_profiles = nullptr;
  LogListModel* m_logs = nullptr;

  int m_selectedProfileIndex = -1;

  QProcess* m_agent = nullptr;
  IpcClient* m_ipc = nullptr;
  bool m_ipcConnected = false;
  QString m_proxyLastTestSummary;
  QHash<QString, QString> m_vpnStatusByProfileId;
};
