#pragma once

#include <QAbstractListModel>
#include <QVector>

class ProfileListModel : public QAbstractListModel {
  Q_OBJECT

public:
  struct ProfileItem {
    QString id;
    QString name;
    QString group;
    QString remark;
    QString status;
    qint64 createdAtMs = 0;
    qint64 lastOpenAtMs = 0;

    QString proxyLastObservedIp;
    bool proxyLastOk = false;
    QString proxyLastError;
    qint64 proxyLastAtMs = 0;

    QString dataDir;
    QString browserEngine;
    QString engineConfigJson;
    QString fingerprintSeed;
    QString startUrl;
    bool humanizeEnabled = false;
    bool geoipEnabled = false;
    QString fingerprintMode;
    QString language;
    QString userAgent;
    QString platform;
    int hardwareConcurrency = 0;
    int deviceMemoryGb = 0;
    double deviceScaleFactor = 0;
    QString timezone;
    QString resolution;
    bool touchEnabled = false;
    bool geoEnabled = false;
    double geoLatitude = 0;
    double geoLongitude = 0;
    double geoAccuracy = 0;

    bool proxyEnabled = false;
    QString proxyType;
    QString proxyHost;
    int proxyPort = 0;
    QString proxyUsername;
    QString proxyPassword;

    bool vpnEnabled = false;
    QString openvpnExe;
    QString openvpnConfig;
    bool openvpnUseSocks = false;
    QString openvpnSocksHost;
    int openvpnSocksPort = 0;
    QString openvpnSocksUsername;
    QString openvpnSocksPassword;
  };

  enum Roles {
    IdRole = Qt::UserRole + 1,
    NameRole,
    GroupRole,
    RemarkRole,
    StatusRole,
    CreatedAtMsRole,
    LastOpenAtMsRole,
    ProxyLastIpRole,
    ProxyLastOkRole,
  };

  explicit ProfileListModel(QObject* parent = nullptr);

  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
  QHash<int, QByteArray> roleNames() const override;

  const QVector<ProfileItem>& items() const;

  int addProfile(const ProfileItem& item);
  void removeAt(int index);
  void updateAt(int index, const ProfileItem& item);
  bool setStatus(int index, const QString& status);
  bool setLastOpenAtMs(int index, qint64 ms);
  void clear();

private:
  QVector<ProfileItem> m_items;
};
