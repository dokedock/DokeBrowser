#include "ProfileRepository.h"

#include <algorithm>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUrl>
#include <QVariant>
#include <QRegularExpression>
#include <QUuid>

namespace {
bool execSql(QSqlDatabase& db, const QString& sql, QString* error) {
  QSqlQuery q(db);
  if (!q.exec(sql)) {
    if (error) {
      *error = q.lastError().text();
    }
    return false;
  }
  return true;
}

QString dbLastError(QSqlDatabase& db) {
  return db.lastError().text();
}

int b(bool v) {
  return v ? 1 : 0;
}

QString nn(const QString& s) {
  return s.isNull() ? QStringLiteral("") : s;
}

constexpr int kKeepLogs = 500;
constexpr int kKeepRuns = 100;
constexpr int kKeepProxyTests = 100;
} // namespace

ProfileRepository::ProfileRepository(QObject* parent) : QObject(parent) {
  m_connectionName = QStringLiteral("dokebrowser_profiles_%1")
                         .arg(QString::number(QDateTime::currentMSecsSinceEpoch()));
}

ProfileRepository::~ProfileRepository() {
  if (!QSqlDatabase::contains(m_connectionName)) {
    return;
  }

  {
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    if (db.isOpen()) {
      db.close();
    }
  }

  QSqlDatabase::removeDatabase(m_connectionName);
}

QString ProfileRepository::dbFilePath() const {
  const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  QDir().mkpath(dir);
  return dir + QStringLiteral("/profiles.sqlite");
}

bool ProfileRepository::open(QString* error) {
  QSqlDatabase db;
  if (QSqlDatabase::contains(m_connectionName)) {
    db = QSqlDatabase::database(m_connectionName);
  } else {
    db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
  }

  db.setDatabaseName(dbFilePath());
  if (!db.open()) {
    if (error) {
      *error = dbLastError(db);
    }
    return false;
  }

  if (!execSql(db, QStringLiteral("PRAGMA foreign_keys = ON;"), error)) {
    return false;
  }
  return true;
}

bool ProfileRepository::ensureSchema(QString* error) {
  if (!open(error)) {
    return false;
  }

  QSqlDatabase db = QSqlDatabase::database(m_connectionName);

  if (!execSql(db,
               QStringLiteral("CREATE TABLE IF NOT EXISTS profiles ("
                              "id TEXT PRIMARY KEY NOT NULL,"
                              "name TEXT NOT NULL,"
                              "group_name TEXT NOT NULL DEFAULT '',"
                              "remark TEXT NOT NULL DEFAULT '',"
                              "status TEXT NOT NULL DEFAULT 'stopped',"
                              "created_at_ms INTEGER NOT NULL DEFAULT 0,"
                              "last_open_at_ms INTEGER NOT NULL DEFAULT 0,"
                              "data_dir TEXT NOT NULL DEFAULT '',"
                              "language TEXT NOT NULL DEFAULT 'zh-CN',"
                              "timezone TEXT NOT NULL DEFAULT '',"
                              "resolution TEXT NOT NULL DEFAULT '1920x1080',"
                              "touch_enabled INTEGER NOT NULL DEFAULT 0"
                              ");"),
               error)) {
    return false;
  }

  if (!execSql(db,
               QStringLiteral("CREATE TABLE IF NOT EXISTS proxy_configs ("
                              "profile_id TEXT PRIMARY KEY NOT NULL,"
                              "enabled INTEGER NOT NULL DEFAULT 0,"
                              "type TEXT NOT NULL DEFAULT 'direct',"
                              "host TEXT NOT NULL DEFAULT '',"
                              "port INTEGER NOT NULL DEFAULT 0,"
                              "username TEXT NOT NULL DEFAULT '',"
                              "password TEXT NOT NULL DEFAULT '',"
                              "FOREIGN KEY(profile_id) REFERENCES profiles(id) ON DELETE CASCADE"
                              ");"),
               error)) {
    return false;
  }

  if (!execSql(db,
               QStringLiteral("CREATE TABLE IF NOT EXISTS vpn_openvpn_configs ("
                              "profile_id TEXT PRIMARY KEY NOT NULL,"
                              "enabled INTEGER NOT NULL DEFAULT 0,"
                              "exe TEXT NOT NULL DEFAULT 'openvpn',"
                              "config TEXT NOT NULL DEFAULT '',"
                              "use_socks INTEGER NOT NULL DEFAULT 0,"
                              "socks_host TEXT NOT NULL DEFAULT '',"
                              "socks_port INTEGER NOT NULL DEFAULT 0,"
                              "socks_username TEXT NOT NULL DEFAULT '',"
                              "socks_password TEXT NOT NULL DEFAULT '',"
                              "FOREIGN KEY(profile_id) REFERENCES profiles(id) ON DELETE CASCADE"
                              ");"),
               error)) {
    return false;
  }

  if (!execSql(db,
               QStringLiteral("CREATE INDEX IF NOT EXISTS idx_profiles_group_name ON profiles(group_name);"),
               error)) {
    return false;
  }

  if (!execSql(db,
               QStringLiteral("CREATE INDEX IF NOT EXISTS idx_profiles_name ON profiles(name);"),
               error)) {
    return false;
  }

  if (!execSql(db,
               QStringLiteral("CREATE TABLE IF NOT EXISTS profile_runs ("
                              "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                              "profile_id TEXT NOT NULL,"
                              "event TEXT NOT NULL,"
                              "status TEXT NOT NULL DEFAULT '',"
                              "detail TEXT NOT NULL DEFAULT '',"
                              "ts_ms INTEGER NOT NULL,"
                              "FOREIGN KEY(profile_id) REFERENCES profiles(id) ON DELETE CASCADE"
                              ");"),
               error)) {
    return false;
  }

  if (!execSql(db,
               QStringLiteral("CREATE INDEX IF NOT EXISTS idx_profile_runs_profile_ts ON profile_runs(profile_id, ts_ms);"),
               error)) {
    return false;
  }

  if (!execSql(db,
               QStringLiteral("CREATE TABLE IF NOT EXISTS logs ("
                              "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                              "profile_id TEXT NULL,"
                              "source TEXT NOT NULL DEFAULT 'app',"
                              "message TEXT NOT NULL,"
                              "ts_ms INTEGER NOT NULL,"
                              "FOREIGN KEY(profile_id) REFERENCES profiles(id) ON DELETE CASCADE"
                              ");"),
               error)) {
    return false;
  }

  if (!execSql(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_logs_profile_ts ON logs(profile_id, ts_ms);"), error)) {
    return false;
  }

  if (!execSql(db,
               QStringLiteral("CREATE TABLE IF NOT EXISTS proxy_test_runs ("
                              "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                              "profile_id TEXT NOT NULL,"
                              "ok INTEGER NOT NULL DEFAULT 0,"
                              "observed_ip TEXT NOT NULL DEFAULT '',"
                              "status_code INTEGER NOT NULL DEFAULT 0,"
                              "duration_ms INTEGER NOT NULL DEFAULT 0,"
                              "qt_error INTEGER NOT NULL DEFAULT 0,"
                              "error TEXT NOT NULL DEFAULT '',"
                              "ts_ms INTEGER NOT NULL,"
                              "FOREIGN KEY(profile_id) REFERENCES profiles(id) ON DELETE CASCADE"
                              ");"),
               error)) {
    return false;
  }

  if (!execSql(db,
               QStringLiteral(
                   "CREATE INDEX IF NOT EXISTS idx_proxy_test_runs_profile_ts ON proxy_test_runs(profile_id, ts_ms);"),
               error)) {
    return false;
  }

  if (!execSql(db,
               QStringLiteral("CREATE TABLE IF NOT EXISTS proxies ("
                              "id TEXT PRIMARY KEY NOT NULL,"
                              "type TEXT NOT NULL DEFAULT 'http',"
                              "host TEXT NOT NULL,"
                              "port INTEGER NOT NULL,"
                              "username TEXT NOT NULL DEFAULT '',"
                              "password TEXT NOT NULL DEFAULT '',"
                              "remark TEXT NOT NULL DEFAULT '',"
                              "disabled INTEGER NOT NULL DEFAULT 0,"
                              "created_at_ms INTEGER NOT NULL DEFAULT 0,"
                              "last_ok INTEGER NOT NULL DEFAULT 0,"
                              "last_ip TEXT NOT NULL DEFAULT '',"
                              "last_error TEXT NOT NULL DEFAULT '',"
                              "last_at_ms INTEGER NOT NULL DEFAULT 0"
                              ");"),
               error)) {
    return false;
  }

  if (!execSql(db,
               QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idx_proxies_unique ON proxies(type, host, port, username);"),
               error)) {
    return false;
  }

  if (!execSql(db,
               QStringLiteral("CREATE TABLE IF NOT EXISTS proxy_assignments ("
                              "profile_id TEXT PRIMARY KEY NOT NULL,"
                              "proxy_id TEXT NOT NULL,"
                              "assigned_at_ms INTEGER NOT NULL DEFAULT 0,"
                              "FOREIGN KEY(profile_id) REFERENCES profiles(id) ON DELETE CASCADE,"
                              "FOREIGN KEY(proxy_id) REFERENCES proxies(id) ON DELETE RESTRICT"
                              ");"),
               error)) {
    return false;
  }

  if (!execSql(db,
               QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idx_proxy_assignments_proxy ON proxy_assignments(proxy_id);"),
               error)) {
    return false;
  }

  return true;
}

QVector<ProfileListModel::ProfileItem> ProfileRepository::loadAll(QString* error) {
  QVector<ProfileListModel::ProfileItem> out;
  if (!ensureSchema(error)) {
    return out;
  }

  QSqlDatabase db = QSqlDatabase::database(m_connectionName);
  QSqlQuery q(db);
  q.prepare(QStringLiteral(
      "SELECT "
      "p.id, p.name, p.group_name, p.remark, p.status, p.created_at_ms, p.last_open_at_ms, p.data_dir, "
      "p.language, p.timezone, p.resolution, p.touch_enabled, "
      "pc.enabled, pc.type, pc.host, pc.port, pc.username, pc.password, "
      "vc.enabled, vc.exe, vc.config, vc.use_socks, vc.socks_host, vc.socks_port, vc.socks_username, vc.socks_password "
      "FROM profiles p "
      "LEFT JOIN proxy_configs pc ON pc.profile_id = p.id "
      "LEFT JOIN vpn_openvpn_configs vc ON vc.profile_id = p.id "
      "ORDER BY p.created_at_ms ASC, p.rowid ASC"));

  if (!q.exec()) {
    if (error) {
      *error = q.lastError().text();
    }
    return out;
  }

  while (q.next()) {
    ProfileListModel::ProfileItem it;
    it.id = q.value(0).toString();
    it.name = q.value(1).toString();
    it.group = q.value(2).toString();
    it.remark = q.value(3).toString();
    it.status = q.value(4).toString();
    it.createdAtMs = q.value(5).toLongLong();
    it.lastOpenAtMs = q.value(6).toLongLong();
    it.dataDir = q.value(7).toString();
    it.language = q.value(8).toString();
    it.timezone = q.value(9).toString();
    it.resolution = q.value(10).toString();
    it.touchEnabled = q.value(11).toInt() != 0;

    it.proxyEnabled = q.value(12).toInt() != 0;
    it.proxyType = q.value(13).toString();
    it.proxyHost = q.value(14).toString();
    it.proxyPort = q.value(15).toInt();
    it.proxyUsername = q.value(16).toString();
    it.proxyPassword = q.value(17).toString();

    it.vpnEnabled = q.value(18).toInt() != 0;
    it.openvpnExe = q.value(19).toString();
    it.openvpnConfig = q.value(20).toString();
    it.openvpnUseSocks = q.value(21).toInt() != 0;
    it.openvpnSocksHost = q.value(22).toString();
    it.openvpnSocksPort = q.value(23).toInt();
    it.openvpnSocksUsername = q.value(24).toString();
    it.openvpnSocksPassword = q.value(25).toString();

    out.push_back(it);
  }

  return out;
}

bool ProfileRepository::upsert(const ProfileListModel::ProfileItem& item, QString* error) {
  if (!ensureSchema(error)) {
    return false;
  }

  QSqlDatabase db = QSqlDatabase::database(m_connectionName);
  if (!db.transaction()) {
    if (error) {
      *error = dbLastError(db);
    }
    return false;
  }

  QSqlQuery p(db);
  p.prepare(QStringLiteral(
      "INSERT INTO profiles (id, name, group_name, remark, status, created_at_ms, last_open_at_ms, data_dir, language, "
      "timezone, resolution, touch_enabled) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
      "ON CONFLICT(id) DO UPDATE SET "
      "name=excluded.name, group_name=excluded.group_name, remark=excluded.remark, status=excluded.status, "
      "created_at_ms=excluded.created_at_ms, last_open_at_ms=excluded.last_open_at_ms, data_dir=excluded.data_dir, "
      "language=excluded.language, timezone=excluded.timezone, resolution=excluded.resolution, "
      "touch_enabled=excluded.touch_enabled"));
  p.addBindValue(item.id);
  p.addBindValue(item.name);
  p.addBindValue(item.group);
  p.addBindValue(item.remark);
  p.addBindValue(item.status);
  p.addBindValue(item.createdAtMs);
  p.addBindValue(item.lastOpenAtMs);
  p.addBindValue(item.dataDir);
  p.addBindValue(item.language);
  p.addBindValue(item.timezone);
  p.addBindValue(item.resolution);
  p.addBindValue(b(item.touchEnabled));

  if (!p.exec()) {
    db.rollback();
    if (error) {
      *error = p.lastError().text();
    }
    return false;
  }

  QSqlQuery pc(db);
  pc.prepare(QStringLiteral(
      "INSERT INTO proxy_configs (profile_id, enabled, type, host, port, username, password) "
      "VALUES (?, ?, ?, ?, ?, ?, ?) "
      "ON CONFLICT(profile_id) DO UPDATE SET "
      "enabled=excluded.enabled, type=excluded.type, host=excluded.host, port=excluded.port, "
      "username=excluded.username, password=excluded.password"));
  pc.addBindValue(item.id);
  pc.addBindValue(b(item.proxyEnabled));
  pc.addBindValue(item.proxyType);
  pc.addBindValue(item.proxyHost);
  pc.addBindValue(item.proxyPort);
  pc.addBindValue(item.proxyUsername);
  pc.addBindValue(item.proxyPassword);

  if (!pc.exec()) {
    db.rollback();
    if (error) {
      *error = pc.lastError().text();
    }
    return false;
  }

  QSqlQuery vc(db);
  vc.prepare(QStringLiteral(
      "INSERT INTO vpn_openvpn_configs (profile_id, enabled, exe, config, use_socks, socks_host, socks_port, "
      "socks_username, socks_password) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?) "
      "ON CONFLICT(profile_id) DO UPDATE SET "
      "enabled=excluded.enabled, exe=excluded.exe, config=excluded.config, use_socks=excluded.use_socks, "
      "socks_host=excluded.socks_host, socks_port=excluded.socks_port, socks_username=excluded.socks_username, "
      "socks_password=excluded.socks_password"));
  vc.addBindValue(item.id);
  vc.addBindValue(b(item.vpnEnabled));
  vc.addBindValue(item.openvpnExe);
  vc.addBindValue(item.openvpnConfig);
  vc.addBindValue(b(item.openvpnUseSocks));
  vc.addBindValue(item.openvpnSocksHost);
  vc.addBindValue(item.openvpnSocksPort);
  vc.addBindValue(item.openvpnSocksUsername);
  vc.addBindValue(item.openvpnSocksPassword);

  if (!vc.exec()) {
    db.rollback();
    if (error) {
      *error = vc.lastError().text();
    }
    return false;
  }

  if (!db.commit()) {
    if (error) {
      *error = dbLastError(db);
    }
    return false;
  }

  return true;
}

bool ProfileRepository::upsertMany(const QVector<ProfileListModel::ProfileItem>& items, QString* error) {
  for (const auto& it : items) {
    QString e;
    if (!upsert(it, &e)) {
      if (error) {
        *error = e;
      }
      return false;
    }
  }
  return true;
}

bool ProfileRepository::remove(const QString& profileId, QString* error) {
  if (!ensureSchema(error)) {
    return false;
  }

  QSqlDatabase db = QSqlDatabase::database(m_connectionName);
  QSqlQuery q(db);
  q.prepare(QStringLiteral("DELETE FROM profiles WHERE id = ?"));
  q.addBindValue(profileId);
  if (!q.exec()) {
    if (error) {
      *error = q.lastError().text();
    }
    return false;
  }
  return true;
}

bool ProfileRepository::insertRunEvent(const QString& profileId,
                                      const QString& event,
                                      const QString& status,
                                      const QString& detail,
                                      qint64 tsMs,
                                      QString* error) {
  if (profileId.isEmpty()) {
    if (error) {
      *error = QStringLiteral("missing_profile_id");
    }
    return false;
  }
  if (!ensureSchema(error)) {
    return false;
  }

  QSqlDatabase db = QSqlDatabase::database(m_connectionName);
  if (!db.transaction()) {
    if (error) {
      *error = dbLastError(db);
    }
    return false;
  }

  QSqlQuery q(db);
  q.prepare(QStringLiteral("INSERT INTO profile_runs(profile_id, event, status, detail, ts_ms) VALUES(?, ?, ?, ?, ?)"));
  q.addBindValue(profileId);
  q.addBindValue(nn(event));
  q.addBindValue(nn(status));
  q.addBindValue(nn(detail));
  q.addBindValue(tsMs);
  if (!q.exec()) {
    db.rollback();
    if (error) {
      *error = q.lastError().text();
    }
    return false;
  }

  QString e;
  if (!enforceRetention(profileId, &e)) {
    db.rollback();
    if (error) {
      *error = e;
    }
    return false;
  }

  if (!db.commit()) {
    if (error) {
      *error = dbLastError(db);
    }
    return false;
  }
  return true;
}

bool ProfileRepository::insertLogLine(const QString& profileId,
                                     const QString& source,
                                     const QString& message,
                                     qint64 tsMs,
                                     QString* error) {
  if (!ensureSchema(error)) {
    return false;
  }

  QSqlDatabase db = QSqlDatabase::database(m_connectionName);
  if (!db.transaction()) {
    if (error) {
      *error = dbLastError(db);
    }
    return false;
  }

  QSqlQuery q(db);
  q.prepare(QStringLiteral("INSERT INTO logs(profile_id, source, message, ts_ms) VALUES(?, ?, ?, ?)"));
  if (profileId.isEmpty()) {
    q.addBindValue(QVariant());
  } else {
    q.addBindValue(profileId);
  }
  q.addBindValue(nn(source));
  q.addBindValue(nn(message));
  q.addBindValue(tsMs);
  if (!q.exec()) {
    db.rollback();
    if (error) {
      *error = q.lastError().text();
    }
    return false;
  }

  QString e;
  if (!enforceRetention(profileId, &e)) {
    db.rollback();
    if (error) {
      *error = e;
    }
    return false;
  }

  if (!db.commit()) {
    if (error) {
      *error = dbLastError(db);
    }
    return false;
  }
  return true;
}

bool ProfileRepository::insertProxyTestRun(const QString& profileId,
                                          bool ok,
                                          const QString& observedIp,
                                          int statusCode,
                                          int durationMs,
                                          int qtError,
                                          const QString& errorText,
                                          qint64 tsMs,
                                          QString* error) {
  if (profileId.isEmpty()) {
    if (error) {
      *error = QStringLiteral("missing_profile_id");
    }
    return false;
  }

  if (!ensureSchema(error)) {
    return false;
  }

  QSqlDatabase db = QSqlDatabase::database(m_connectionName);
  if (!db.transaction()) {
    if (error) {
      *error = dbLastError(db);
    }
    return false;
  }

  QSqlQuery q(db);
  q.prepare(QStringLiteral("INSERT INTO proxy_test_runs(profile_id, ok, observed_ip, status_code, duration_ms, qt_error, "
                           "error, ts_ms) VALUES(?, ?, ?, ?, ?, ?, ?, ?)"));
  q.addBindValue(profileId);
  q.addBindValue(b(ok));
  q.addBindValue(nn(observedIp));
  q.addBindValue(statusCode);
  q.addBindValue(durationMs);
  q.addBindValue(qtError);
  q.addBindValue(nn(errorText));
  q.addBindValue(tsMs);
  if (!q.exec()) {
    db.rollback();
    if (error) {
      *error = q.lastError().text();
    }
    return false;
  }

  QString e;
  if (!enforceRetention(profileId, &e)) {
    db.rollback();
    if (error) {
      *error = e;
    }
    return false;
  }

  if (!db.commit()) {
    if (error) {
      *error = dbLastError(db);
    }
    return false;
  }
  return true;
}

bool ProfileRepository::enforceRetention(const QString& profileId, QString* error) {
  if (!enforceRetentionOne(QStringLiteral("logs"), profileId, kKeepLogs, error)) {
    return false;
  }

  if (!profileId.isEmpty()) {
    if (!enforceRetentionOne(QStringLiteral("profile_runs"), profileId, kKeepRuns, error)) {
      return false;
    }
    if (!enforceRetentionOne(QStringLiteral("proxy_test_runs"), profileId, kKeepProxyTests, error)) {
      return false;
    }
  }

  return true;
}

bool ProfileRepository::enforceRetentionOne(const QString& table, const QString& profileId, int keepN, QString* error) {
  if (keepN <= 0) {
    return true;
  }
  QSqlDatabase db = QSqlDatabase::database(m_connectionName);
  QSqlQuery q(db);

  if (profileId.isEmpty()) {
    q.prepare(QStringLiteral("DELETE FROM %1 WHERE profile_id IS NULL AND id NOT IN ("
                             "SELECT id FROM %1 WHERE profile_id IS NULL ORDER BY ts_ms DESC, id DESC LIMIT ?"
                             ")")
                  .arg(table));
    q.addBindValue(keepN);
  } else {
    q.prepare(QStringLiteral("DELETE FROM %1 WHERE profile_id = ? AND id NOT IN ("
                             "SELECT id FROM %1 WHERE profile_id = ? ORDER BY ts_ms DESC, id DESC LIMIT ?"
                             ")")
                  .arg(table));
    q.addBindValue(profileId);
    q.addBindValue(profileId);
    q.addBindValue(keepN);
  }

  if (!q.exec()) {
    if (error) {
      *error = q.lastError().text();
    }
    return false;
  }
  return true;
}

QStringList ProfileRepository::queryLogs(const QString& profileId,
                                        const QString& keyword,
                                        qint64 fromTsMs,
                                        qint64 toTsMs,
                                        int limit,
                                        QString* error) {
  QStringList out;
  if (!ensureSchema(error)) {
    return out;
  }
  if (limit <= 0) {
    limit = kKeepLogs;
  }

  QSqlDatabase db = QSqlDatabase::database(m_connectionName);
  QString sql = QStringLiteral("SELECT message FROM logs WHERE 1=1");
  if (profileId.isEmpty()) {
    sql += QStringLiteral(" AND profile_id IS NULL");
  } else {
    sql += QStringLiteral(" AND profile_id = :pid");
  }
  if (!keyword.trimmed().isEmpty()) {
    sql += QStringLiteral(" AND message LIKE :kw");
  }
  if (fromTsMs > 0) {
    sql += QStringLiteral(" AND ts_ms >= :fromTs");
  }
  if (toTsMs > 0) {
    sql += QStringLiteral(" AND ts_ms <= :toTs");
  }
  sql += QStringLiteral(" ORDER BY ts_ms DESC, id DESC LIMIT :limit");

  QSqlQuery q(db);
  q.prepare(sql);
  if (!profileId.isEmpty()) {
    q.bindValue(QStringLiteral(":pid"), profileId);
  }
  if (!keyword.trimmed().isEmpty()) {
    q.bindValue(QStringLiteral(":kw"), QStringLiteral("%%%1%%").arg(keyword.trimmed()));
  }
  if (fromTsMs > 0) {
    q.bindValue(QStringLiteral(":fromTs"), fromTsMs);
  }
  if (toTsMs > 0) {
    q.bindValue(QStringLiteral(":toTs"), toTsMs);
  }
  q.bindValue(QStringLiteral(":limit"), limit);

  if (!q.exec()) {
    if (error) {
      *error = q.lastError().text();
    }
    return out;
  }

  while (q.next()) {
    out.push_back(q.value(0).toString());
  }

  std::reverse(out.begin(), out.end());
  return out;
}

QStringList ProfileRepository::queryLogsAllProfiles(const QString& keyword,
                                                   qint64 fromTsMs,
                                                   qint64 toTsMs,
                                                   int limit,
                                                   QString* error) {
  QStringList out;
  if (!ensureSchema(error)) {
    return out;
  }
  if (limit <= 0) {
    limit = kKeepLogs;
  }

  QSqlDatabase db = QSqlDatabase::database(m_connectionName);
  QString sql = QStringLiteral("SELECT profile_id, message FROM logs WHERE 1=1");
  if (!keyword.trimmed().isEmpty()) {
    sql += QStringLiteral(" AND message LIKE :kw");
  }
  if (fromTsMs > 0) {
    sql += QStringLiteral(" AND ts_ms >= :fromTs");
  }
  if (toTsMs > 0) {
    sql += QStringLiteral(" AND ts_ms <= :toTs");
  }
  sql += QStringLiteral(" ORDER BY ts_ms DESC, id DESC LIMIT :limit");

  QSqlQuery q(db);
  q.prepare(sql);
  if (!keyword.trimmed().isEmpty()) {
    q.bindValue(QStringLiteral(":kw"), QStringLiteral("%%%1%%").arg(keyword.trimmed()));
  }
  if (fromTsMs > 0) {
    q.bindValue(QStringLiteral(":fromTs"), fromTsMs);
  }
  if (toTsMs > 0) {
    q.bindValue(QStringLiteral(":toTs"), toTsMs);
  }
  q.bindValue(QStringLiteral(":limit"), limit);

  if (!q.exec()) {
    if (error) {
      *error = q.lastError().text();
    }
    return out;
  }

  while (q.next()) {
    const QString pid = q.value(0).toString();
    const QString msg = q.value(1).toString();
    out.push_back(QStringLiteral("[%1] %2").arg(pid.isEmpty() ? QStringLiteral("global") : pid.left(8), msg));
  }

  std::reverse(out.begin(), out.end());
  return out;
}

QStringList ProfileRepository::queryRuns(const QString& profileId,
                                        const QString& keyword,
                                        qint64 fromTsMs,
                                        qint64 toTsMs,
                                        int limit,
                                        QString* error) {
  QStringList out;
  if (profileId.isEmpty()) {
    if (error) {
      *error = QStringLiteral("missing_profile_id");
    }
    return out;
  }

  if (!ensureSchema(error)) {
    return out;
  }
  if (limit <= 0) {
    limit = kKeepRuns;
  }

  QSqlDatabase db = QSqlDatabase::database(m_connectionName);
  QString sql = QStringLiteral("SELECT ts_ms, event, status, detail FROM profile_runs WHERE profile_id = :pid");
  if (!keyword.trimmed().isEmpty()) {
    sql += QStringLiteral(" AND (event LIKE :kw OR status LIKE :kw OR detail LIKE :kw)");
  }
  if (fromTsMs > 0) {
    sql += QStringLiteral(" AND ts_ms >= :fromTs");
  }
  if (toTsMs > 0) {
    sql += QStringLiteral(" AND ts_ms <= :toTs");
  }
  sql += QStringLiteral(" ORDER BY ts_ms DESC, id DESC LIMIT :limit");

  QSqlQuery q(db);
  q.prepare(sql);
  q.bindValue(QStringLiteral(":pid"), profileId);
  if (!keyword.trimmed().isEmpty()) {
    q.bindValue(QStringLiteral(":kw"), QStringLiteral("%%%1%%").arg(keyword.trimmed()));
  }
  if (fromTsMs > 0) {
    q.bindValue(QStringLiteral(":fromTs"), fromTsMs);
  }
  if (toTsMs > 0) {
    q.bindValue(QStringLiteral(":toTs"), toTsMs);
  }
  q.bindValue(QStringLiteral(":limit"), limit);

  if (!q.exec()) {
    if (error) {
      *error = q.lastError().text();
    }
    return out;
  }

  while (q.next()) {
    const qint64 tsMs = q.value(0).toLongLong();
    const QString event = q.value(1).toString();
    const QString status = q.value(2).toString();
    const QString detail = q.value(3).toString();
    const QString line = QStringLiteral("[%1] run event=%2 status=%3 detail=%4")
                             .arg(QDateTime::fromMSecsSinceEpoch(tsMs).toString(QStringLiteral("HH:mm:ss")))
                             .arg(event)
                             .arg(status)
                             .arg(detail);
    out.push_back(line);
  }

  std::reverse(out.begin(), out.end());
  return out;
}

QStringList ProfileRepository::queryProxyTests(const QString& profileId,
                                              const QString& keyword,
                                              qint64 fromTsMs,
                                              qint64 toTsMs,
                                              int limit,
                                              QString* error) {
  QStringList out;
  if (profileId.isEmpty()) {
    if (error) {
      *error = QStringLiteral("missing_profile_id");
    }
    return out;
  }

  if (!ensureSchema(error)) {
    return out;
  }
  if (limit <= 0) {
    limit = kKeepProxyTests;
  }

  QSqlDatabase db = QSqlDatabase::database(m_connectionName);
  QString sql = QStringLiteral(
      "SELECT ts_ms, ok, observed_ip, status_code, duration_ms, qt_error, error FROM proxy_test_runs WHERE profile_id = :pid");
  if (!keyword.trimmed().isEmpty()) {
    sql += QStringLiteral(" AND (observed_ip LIKE :kw OR error LIKE :kw)");
  }
  if (fromTsMs > 0) {
    sql += QStringLiteral(" AND ts_ms >= :fromTs");
  }
  if (toTsMs > 0) {
    sql += QStringLiteral(" AND ts_ms <= :toTs");
  }
  sql += QStringLiteral(" ORDER BY ts_ms DESC, id DESC LIMIT :limit");

  QSqlQuery q(db);
  q.prepare(sql);
  q.bindValue(QStringLiteral(":pid"), profileId);
  if (!keyword.trimmed().isEmpty()) {
    q.bindValue(QStringLiteral(":kw"), QStringLiteral("%%%1%%").arg(keyword.trimmed()));
  }
  if (fromTsMs > 0) {
    q.bindValue(QStringLiteral(":fromTs"), fromTsMs);
  }
  if (toTsMs > 0) {
    q.bindValue(QStringLiteral(":toTs"), toTsMs);
  }
  q.bindValue(QStringLiteral(":limit"), limit);

  if (!q.exec()) {
    if (error) {
      *error = q.lastError().text();
    }
    return out;
  }

  while (q.next()) {
    const qint64 tsMs = q.value(0).toLongLong();
    const bool ok = q.value(1).toInt() != 0;
    const QString observedIp = q.value(2).toString();
    const int statusCode = q.value(3).toInt();
    const int durationMs = q.value(4).toInt();
    const int qtError = q.value(5).toInt();
    const QString errorText = q.value(6).toString();

    const QString line = QStringLiteral("[%1] proxy_test ok=%2 ip=%3 status=%4 duration=%5ms qt=%6 error=%7")
                             .arg(QDateTime::fromMSecsSinceEpoch(tsMs).toString(QStringLiteral("HH:mm:ss")))
                             .arg(ok ? QStringLiteral("true") : QStringLiteral("false"))
                             .arg(observedIp)
                             .arg(statusCode)
                             .arg(durationMs)
                             .arg(qtError)
                             .arg(errorText);
    out.push_back(line);
  }

  std::reverse(out.begin(), out.end());
  return out;
}

namespace {
struct ParsedProxy {
  QString type;
  QString host;
  int port = 0;
  QString username;
  QString password;
  QString remark;
};

bool parseProxyLine(const QString& line, ParsedProxy* out) {
  if (!out) {
    return false;
  }
  const QString v = line.trimmed();
  if (v.isEmpty()) {
    return false;
  }
  if (v.startsWith('#')) {
    return false;
  }

  if (v.contains(QStringLiteral("://"))) {
    const QUrl url(v);
    if (!url.isValid() || url.host().trimmed().isEmpty()) {
      return false;
    }
    QString scheme = url.scheme().trimmed().toLower();
    if (scheme == QStringLiteral("socks")) {
      scheme = QStringLiteral("socks5");
    }
    if (scheme != QStringLiteral("http") && scheme != QStringLiteral("https") && scheme != QStringLiteral("socks5")) {
      return false;
    }
    const int port = url.port(0);
    if (port <= 0) {
      return false;
    }
    out->type = scheme;
    out->host = url.host().trimmed();
    out->port = port;
    out->username = url.userName();
    out->password = url.password();
    out->remark.clear();
    return true;
  }

  static QRegularExpression re(
      QStringLiteral("^\\s*(?:(http|https|socks5)\\s+)?([^\\s:]+)\\s*:\\s*(\\d+)(?::([^:\\s]+))?(?::([^\\s]+))?(?:\\s+(.*))?$"),
      QRegularExpression::CaseInsensitiveOption);
  const QRegularExpressionMatch m = re.match(v);
  if (!m.hasMatch()) {
    return false;
  }
  const QString t = m.captured(1).trimmed().toLower();
  const QString host = m.captured(2).trimmed();
  const int port = m.captured(3).toInt();
  if (host.isEmpty() || port <= 0) {
    return false;
  }
  out->type = t.isEmpty() ? QStringLiteral("http") : t;
  out->host = host;
  out->port = port;
  out->username = m.captured(4);
  out->password = m.captured(5);
  out->remark = m.captured(6).trimmed();
  return true;
}

bool loadProxyById(QSqlDatabase& db,
                   const QString& proxyId,
                   ProfileRepository::ProxyPoolItem* out,
                   QString* error) {
  if (proxyId.trimmed().isEmpty()) {
    if (error) {
      *error = QStringLiteral("missing_proxy_id");
    }
    return false;
  }

  QSqlQuery q(db);
  q.prepare(QStringLiteral("SELECT id, type, host, port, username, password, remark, disabled, created_at_ms, last_ok, "
                           "last_ip, last_error, last_at_ms FROM proxies WHERE id = ?"));
  q.addBindValue(proxyId.trimmed());
  if (!q.exec()) {
    if (error) {
      *error = q.lastError().text();
    }
    return false;
  }
  if (!q.next()) {
    if (error) {
      *error = QStringLiteral("proxy_not_found");
    }
    return false;
  }

  ProfileRepository::ProxyPoolItem it;
  it.id = q.value(0).toString();
  it.type = q.value(1).toString();
  it.host = q.value(2).toString();
  it.port = q.value(3).toInt();
  it.username = q.value(4).toString();
  it.password = q.value(5).toString();
  it.remark = q.value(6).toString();
  it.disabled = q.value(7).toInt() != 0;
  it.createdAtMs = q.value(8).toLongLong();
  it.lastOk = q.value(9).toInt() != 0;
  it.lastIp = q.value(10).toString();
  it.lastError = q.value(11).toString();
  it.lastAtMs = q.value(12).toLongLong();
  if (out) {
    *out = it;
  }
  return true;
}

bool upsertProxyConfigForProfile(QSqlDatabase& db,
                                const QString& profileId,
                                bool enabled,
                                const QString& type,
                                const QString& host,
                                int port,
                                const QString& username,
                                const QString& password,
                                QString* error) {
  QSqlQuery q(db);
  q.prepare(QStringLiteral(
      "INSERT INTO proxy_configs (profile_id, enabled, type, host, port, username, password) "
      "VALUES (?, ?, ?, ?, ?, ?, ?) "
      "ON CONFLICT(profile_id) DO UPDATE SET enabled=excluded.enabled, type=excluded.type, host=excluded.host, port=excluded.port, "
      "username=excluded.username, password=excluded.password"));
  q.addBindValue(profileId);
  q.addBindValue(enabled ? 1 : 0);
  q.addBindValue(type);
  q.addBindValue(host);
  q.addBindValue(port);
  q.addBindValue(username);
  q.addBindValue(password);
  if (!q.exec()) {
    if (error) {
      *error = q.lastError().text();
    }
    return false;
  }
  return true;
}
} // namespace

QVector<ProfileRepository::ProxyPoolItem> ProfileRepository::loadProxyPool(QString* error) {
  QVector<ProxyPoolItem> out;
  if (!ensureSchema(error)) {
    return out;
  }
  QSqlDatabase db = QSqlDatabase::database(m_connectionName);
  QSqlQuery q(db);
  q.prepare(QStringLiteral(
      "SELECT p.id, p.type, p.host, p.port, p.username, p.password, p.remark, p.disabled, p.created_at_ms, "
      "p.last_ok, p.last_ip, p.last_error, p.last_at_ms, a.profile_id "
      "FROM proxies p "
      "LEFT JOIN proxy_assignments a ON a.proxy_id = p.id "
      "ORDER BY p.created_at_ms DESC, p.rowid DESC"));
  if (!q.exec()) {
    if (error) {
      *error = q.lastError().text();
    }
    return out;
  }

  while (q.next()) {
    ProxyPoolItem it;
    it.id = q.value(0).toString();
    it.type = q.value(1).toString();
    it.host = q.value(2).toString();
    it.port = q.value(3).toInt();
    it.username = q.value(4).toString();
    it.password = q.value(5).toString();
    it.remark = q.value(6).toString();
    it.disabled = q.value(7).toInt() != 0;
    it.createdAtMs = q.value(8).toLongLong();
    it.lastOk = q.value(9).toInt() != 0;
    it.lastIp = q.value(10).toString();
    it.lastError = q.value(11).toString();
    it.lastAtMs = q.value(12).toLongLong();
    it.assignedProfileId = q.value(13).toString();
    out.push_back(it);
  }
  return out;
}

int ProfileRepository::importProxyPool(const QStringList& lines, QString* error) {
  if (!ensureSchema(error)) {
    return 0;
  }
  QSqlDatabase db = QSqlDatabase::database(m_connectionName);
  if (!db.transaction()) {
    if (error) {
      *error = dbLastError(db);
    }
    return 0;
  }

  int n = 0;
  const qint64 now = QDateTime::currentMSecsSinceEpoch();

  QSqlQuery q(db);
  q.prepare(QStringLiteral(
      "INSERT INTO proxies (id, type, host, port, username, password, remark, disabled, created_at_ms) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, 0, ?) "
      "ON CONFLICT(type, host, port, username) DO UPDATE SET "
      "password=excluded.password, remark=excluded.remark, disabled=0"));

  for (const auto& line : lines) {
    ParsedProxy p;
    if (!parseProxyLine(line, &p)) {
      continue;
    }
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    q.addBindValue(id);
    q.addBindValue(p.type);
    q.addBindValue(p.host);
    q.addBindValue(p.port);
    q.addBindValue(nn(p.username));
    q.addBindValue(nn(p.password));
    q.addBindValue(nn(p.remark));
    q.addBindValue(now);
    if (!q.exec()) {
      db.rollback();
      if (error) {
        *error = q.lastError().text();
      }
      return 0;
    }
    n++;
  }

  if (!db.commit()) {
    if (error) {
      *error = dbLastError(db);
    }
    return 0;
  }
  return n;
}

bool ProfileRepository::assignProxyToProfile(const QString& proxyId, const QString& profileId, QString* error) {
  if (profileId.trimmed().isEmpty()) {
    if (error) {
      *error = QStringLiteral("missing_profile_id");
    }
    return false;
  }
  if (!ensureSchema(error)) {
    return false;
  }

  QSqlDatabase db = QSqlDatabase::database(m_connectionName);
  if (!db.transaction()) {
    if (error) {
      *error = dbLastError(db);
    }
    return false;
  }

  ProxyPoolItem px;
  QString e;
  if (!loadProxyById(db, proxyId, &px, &e)) {
    db.rollback();
    if (error) {
      *error = e;
    }
    return false;
  }
  if (px.disabled) {
    db.rollback();
    if (error) {
      *error = QStringLiteral("proxy_disabled");
    }
    return false;
  }

  {
    QSqlQuery chk(db);
    chk.prepare(QStringLiteral("SELECT profile_id FROM proxy_assignments WHERE proxy_id = ?"));
    chk.addBindValue(px.id);
    if (!chk.exec()) {
      db.rollback();
      if (error) {
        *error = chk.lastError().text();
      }
      return false;
    }
    if (chk.next()) {
      db.rollback();
      if (error) {
        *error = QStringLiteral("proxy_already_assigned");
      }
      return false;
    }
  }

  {
    QSqlQuery del(db);
    del.prepare(QStringLiteral("DELETE FROM proxy_assignments WHERE profile_id = ?"));
    del.addBindValue(profileId.trimmed());
    if (!del.exec()) {
      db.rollback();
      if (error) {
        *error = del.lastError().text();
      }
      return false;
    }
  }

  {
    QSqlQuery ins(db);
    ins.prepare(QStringLiteral("INSERT INTO proxy_assignments(profile_id, proxy_id, assigned_at_ms) VALUES(?, ?, ?)"));
    ins.addBindValue(profileId.trimmed());
    ins.addBindValue(px.id);
    ins.addBindValue(QDateTime::currentMSecsSinceEpoch());
    if (!ins.exec()) {
      db.rollback();
      if (error) {
        *error = ins.lastError().text();
      }
      return false;
    }
  }

  if (!upsertProxyConfigForProfile(db, profileId.trimmed(), true, px.type, px.host, px.port, px.username, px.password, &e)) {
    db.rollback();
    if (error) {
      *error = e;
    }
    return false;
  }

  if (!db.commit()) {
    if (error) {
      *error = dbLastError(db);
    }
    return false;
  }
  return true;
}

bool ProfileRepository::assignNextAvailableProxyToProfile(const QString& profileId, QString* error) {
  if (profileId.trimmed().isEmpty()) {
    if (error) {
      *error = QStringLiteral("missing_profile_id");
    }
    return false;
  }
  if (!ensureSchema(error)) {
    return false;
  }

  QSqlDatabase db = QSqlDatabase::database(m_connectionName);
  QSqlQuery q(db);
  q.prepare(QStringLiteral(
      "SELECT id FROM proxies "
      "WHERE disabled = 0 AND id NOT IN (SELECT proxy_id FROM proxy_assignments) "
      "ORDER BY last_ok DESC, last_at_ms DESC, created_at_ms ASC, rowid ASC LIMIT 1"));
  if (!q.exec()) {
    if (error) {
      *error = q.lastError().text();
    }
    return false;
  }
  if (!q.next()) {
    if (error) {
      *error = QStringLiteral("no_available_proxy");
    }
    return false;
  }
  const QString proxyId = q.value(0).toString();
  return assignProxyToProfile(proxyId, profileId, error);
}

bool ProfileRepository::releaseProxyFromProfile(const QString& profileId, QString* error) {
  if (profileId.trimmed().isEmpty()) {
    if (error) {
      *error = QStringLiteral("missing_profile_id");
    }
    return false;
  }
  if (!ensureSchema(error)) {
    return false;
  }

  QSqlDatabase db = QSqlDatabase::database(m_connectionName);
  if (!db.transaction()) {
    if (error) {
      *error = dbLastError(db);
    }
    return false;
  }

  {
    QSqlQuery del(db);
    del.prepare(QStringLiteral("DELETE FROM proxy_assignments WHERE profile_id = ?"));
    del.addBindValue(profileId.trimmed());
    if (!del.exec()) {
      db.rollback();
      if (error) {
        *error = del.lastError().text();
      }
      return false;
    }
  }

  QString e;
  if (!upsertProxyConfigForProfile(db, profileId.trimmed(), false, QStringLiteral("direct"), QString(), 0, QString(), QString(), &e)) {
    db.rollback();
    if (error) {
      *error = e;
    }
    return false;
  }

  if (!db.commit()) {
    if (error) {
      *error = dbLastError(db);
    }
    return false;
  }
  return true;
}

bool ProfileRepository::rotateProxyForProfile(const QString& profileId, QString* error) {
  if (profileId.trimmed().isEmpty()) {
    if (error) {
      *error = QStringLiteral("missing_profile_id");
    }
    return false;
  }
  if (!ensureSchema(error)) {
    return false;
  }

  QSqlDatabase db = QSqlDatabase::database(m_connectionName);
  if (!db.transaction()) {
    if (error) {
      *error = dbLastError(db);
    }
    return false;
  }

  QString current;
  {
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT proxy_id FROM proxy_assignments WHERE profile_id = ?"));
    q.addBindValue(profileId.trimmed());
    if (!q.exec()) {
      db.rollback();
      if (error) {
        *error = q.lastError().text();
      }
      return false;
    }
    if (q.next()) {
      current = q.value(0).toString();
    }
  }

  QSqlQuery pick(db);
  pick.prepare(QStringLiteral(
      "SELECT id FROM proxies "
      "WHERE disabled = 0 AND id != :cur AND id NOT IN (SELECT proxy_id FROM proxy_assignments) "
      "ORDER BY last_ok DESC, last_at_ms DESC, created_at_ms ASC, rowid ASC LIMIT 1"));
  pick.bindValue(QStringLiteral(":cur"), current);
  if (!pick.exec()) {
    db.rollback();
    if (error) {
      *error = pick.lastError().text();
    }
    return false;
  }
  if (!pick.next()) {
    db.rollback();
    if (error) {
      *error = QStringLiteral("no_available_proxy");
    }
    return false;
  }
  const QString nextId = pick.value(0).toString();

  ProxyPoolItem px;
  QString e;
  if (!loadProxyById(db, nextId, &px, &e)) {
    db.rollback();
    if (error) {
      *error = e;
    }
    return false;
  }

  {
    QSqlQuery del(db);
    del.prepare(QStringLiteral("DELETE FROM proxy_assignments WHERE profile_id = ?"));
    del.addBindValue(profileId.trimmed());
    if (!del.exec()) {
      db.rollback();
      if (error) {
        *error = del.lastError().text();
      }
      return false;
    }
  }

  {
    QSqlQuery ins(db);
    ins.prepare(QStringLiteral("INSERT INTO proxy_assignments(profile_id, proxy_id, assigned_at_ms) VALUES(?, ?, ?)"));
    ins.addBindValue(profileId.trimmed());
    ins.addBindValue(px.id);
    ins.addBindValue(QDateTime::currentMSecsSinceEpoch());
    if (!ins.exec()) {
      db.rollback();
      if (error) {
        *error = ins.lastError().text();
      }
      return false;
    }
  }

  if (!upsertProxyConfigForProfile(db, profileId.trimmed(), true, px.type, px.host, px.port, px.username, px.password, &e)) {
    db.rollback();
    if (error) {
      *error = e;
    }
    return false;
  }

  if (!db.commit()) {
    if (error) {
      *error = dbLastError(db);
    }
    return false;
  }
  return true;
}

bool ProfileRepository::updateProxyHealth(const QString& proxyId,
                                         bool ok,
                                         const QString& observedIp,
                                         const QString& errorText,
                                         qint64 tsMs,
                                         QString* error) {
  if (proxyId.trimmed().isEmpty()) {
    if (error) {
      *error = QStringLiteral("missing_proxy_id");
    }
    return false;
  }
  if (!ensureSchema(error)) {
    return false;
  }
  QSqlDatabase db = QSqlDatabase::database(m_connectionName);
  QSqlQuery q(db);
  q.prepare(QStringLiteral("UPDATE proxies SET last_ok=?, last_ip=?, last_error=?, last_at_ms=? WHERE id=?"));
  q.addBindValue(b(ok));
  q.addBindValue(nn(observedIp));
  q.addBindValue(nn(errorText));
  q.addBindValue(tsMs);
  q.addBindValue(proxyId.trimmed());
  if (!q.exec()) {
    if (error) {
      *error = q.lastError().text();
    }
    return false;
  }
  return true;
}
