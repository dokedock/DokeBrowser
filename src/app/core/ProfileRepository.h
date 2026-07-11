#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include "ProfileListModel.h"

class ProfileRepository final : public QObject {
public:
  struct ProxyPoolItem {
    QString id;
    QString type;
    QString host;
    int port = 0;
    QString username;
    QString password;
    QString remark;
    bool disabled = false;
    qint64 createdAtMs = 0;
    bool lastOk = false;
    QString lastIp;
    QString lastError;
    qint64 lastAtMs = 0;
    QString assignedProfileId;
  };

  explicit ProfileRepository(QObject* parent = nullptr);
  ~ProfileRepository() override;

  QString dbFilePath() const;

  bool open(QString* error = nullptr);
  bool ensureSchema(QString* error = nullptr);

  QVector<ProfileListModel::ProfileItem> loadAll(QString* error = nullptr);
  bool upsert(const ProfileListModel::ProfileItem& item, QString* error = nullptr);
  bool upsertMany(const QVector<ProfileListModel::ProfileItem>& items, QString* error = nullptr);
  bool remove(const QString& profileId, QString* error = nullptr);

  bool insertRunEvent(const QString& profileId,
                      const QString& event,
                      const QString& status,
                      const QString& detail,
                      qint64 tsMs,
                      QString* error = nullptr);

  bool insertLogLine(const QString& profileId,
                     const QString& source,
                     const QString& message,
                     qint64 tsMs,
                     QString* error = nullptr);

  bool insertProxyTestRun(const QString& profileId,
                          bool ok,
                          const QString& observedIp,
                          int statusCode,
                          int durationMs,
                          int qtError,
                          const QString& errorText,
                          qint64 tsMs,
                          QString* error = nullptr);

  QStringList queryLogs(const QString& profileId,
                        const QString& keyword,
                        qint64 fromTsMs,
                        qint64 toTsMs,
                        int limit,
                        QString* error = nullptr);

  QStringList queryLogsAllProfiles(const QString& keyword,
                                   qint64 fromTsMs,
                                   qint64 toTsMs,
                                   int limit,
                                   QString* error = nullptr);

  QStringList queryRuns(const QString& profileId,
                        const QString& keyword,
                        qint64 fromTsMs,
                        qint64 toTsMs,
                        int limit,
                        QString* error = nullptr);

  QStringList queryProxyTests(const QString& profileId,
                             const QString& keyword,
                             qint64 fromTsMs,
                             qint64 toTsMs,
                             int limit,
                             QString* error = nullptr);

  QVector<ProxyPoolItem> loadProxyPool(QString* error = nullptr);
  int importProxyPool(const QStringList& lines, QString* error = nullptr);
  bool assignProxyToProfile(const QString& proxyId, const QString& profileId, QString* error = nullptr);
  bool assignNextAvailableProxyToProfile(const QString& profileId, QString* error = nullptr);
  bool releaseProxyFromProfile(const QString& profileId, QString* error = nullptr);
  bool rotateProxyForProfile(const QString& profileId, QString* error = nullptr);
  bool updateProxyHealth(const QString& proxyId,
                         bool ok,
                         const QString& observedIp,
                         const QString& errorText,
                         qint64 tsMs,
                         QString* error = nullptr);

private:
  bool enforceRetention(const QString& profileId, QString* error = nullptr);
  bool enforceRetentionOne(const QString& table,
                           const QString& profileId,
                           int keepN,
                           QString* error = nullptr);

  QString m_connectionName;
};
