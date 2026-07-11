#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include "ProfileListModel.h"

class ProfileRepository final : public QObject {
public:
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

private:
  bool enforceRetention(const QString& profileId, QString* error = nullptr);
  bool enforceRetentionOne(const QString& table,
                           const QString& profileId,
                           int keepN,
                           QString* error = nullptr);

  QString m_connectionName;
};
