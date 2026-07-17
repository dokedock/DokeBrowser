#include "agent/core/ProfileRuntimeManager.h"

#include <QCoreApplication>
#include <QJsonObject>
#include <QStringList>
#include <QtGlobal>

namespace {
struct CapturedStatus {
  QString profileId;
  QString status;
  QString error;
};

bool expect(bool condition, const char* message) {
  if (!condition) {
    qCritical("%s", message);
    return false;
  }
  return true;
}

bool testMissingProfileId() {
  ProfileRuntimeManager manager;
  QVector<CapturedStatus> statuses;
  QStringList logs;

  QJsonObject obj;
  obj.insert(QStringLiteral("type"), QStringLiteral("profile.stop"));

  manager.handleMessage(
      obj,
      [&statuses](const QString& profileId, const QString& status, const QString& error) {
        statuses.push_back({profileId, status, error});
      },
      [&logs](const QString& message) {
        logs.push_back(message);
      });

  bool ok = true;
  ok &= expect(statuses.size() == 1, "missing profile id should emit one status");
  ok &= expect(statuses.first().status == QStringLiteral("error"), "missing profile id should emit error status");
  ok &= expect(statuses.first().error == QStringLiteral("missing_profile_id"), "missing profile id error should match");
  ok &= expect(logs.isEmpty(), "missing profile id should not log runtime line");
  return ok;
}

bool testUnsupportedEngine() {
  ProfileRuntimeManager manager;
  QVector<CapturedStatus> statuses;
  QStringList logs;

  QJsonObject obj;
  obj.insert(QStringLiteral("type"), QStringLiteral("profile.start"));
  obj.insert(QStringLiteral("profile_id"), QStringLiteral("p1"));
  obj.insert(QStringLiteral("profile_name"), QStringLiteral("Profile 1"));
  obj.insert(QStringLiteral("browser_engine"), QStringLiteral("unknown_engine"));

  manager.handleMessage(
      obj,
      [&statuses](const QString& profileId, const QString& status, const QString& error) {
        statuses.push_back({profileId, status, error});
      },
      [&logs](const QString& message) {
        logs.push_back(message);
      });

  bool ok = true;
  ok &= expect(statuses.size() == 2, "unsupported engine should emit starting then error");
  ok &= expect(statuses.at(0).profileId == QStringLiteral("p1"), "profile id should be forwarded");
  ok &= expect(statuses.at(0).status == QStringLiteral("starting"), "first status should be starting");
  ok &= expect(statuses.at(1).status == QStringLiteral("error"), "second status should be error");
  ok &= expect(statuses.at(1).error == QStringLiteral("unsupported_browser_engine:unknown_engine"),
               "unsupported engine error should include engine id");
  ok &= expect(logs.size() == 1 && logs.first().contains(QStringLiteral("profile.start engine=unknown_engine")),
               "runtime should log profile start line");
  return ok;
}

bool testDokeInvalidExecutable() {
  ProfileRuntimeManager manager;
  QVector<CapturedStatus> statuses;
  QStringList logs;

  QJsonObject obj;
  obj.insert(QStringLiteral("type"), QStringLiteral("profile.start"));
  obj.insert(QStringLiteral("profile_id"), QStringLiteral("p2"));
  obj.insert(QStringLiteral("profile_name"), QStringLiteral("Profile 2"));
  obj.insert(QStringLiteral("browser_engine"), QStringLiteral("doke_chromium"));
  obj.insert(QStringLiteral("engine_config_json"),
             QStringLiteral("{\"executable\":\"/tmp/dokebrowser-missing-doke-chromium\"}"));

  manager.handleMessage(
      obj,
      [&statuses](const QString& profileId, const QString& status, const QString& error) {
        statuses.push_back({profileId, status, error});
      },
      [&logs](const QString& message) {
        logs.push_back(message);
      });

  bool ok = true;
  ok &= expect(statuses.size() == 2, "invalid doke path should emit starting then error");
  ok &= expect(statuses.at(0).profileId == QStringLiteral("p2"), "doke profile id should be forwarded");
  ok &= expect(statuses.at(0).status == QStringLiteral("starting"), "invalid doke first status should be starting");
  ok &= expect(statuses.at(1).status == QStringLiteral("error"), "invalid doke second status should be error");
  ok &= expect(statuses.at(1).error == QStringLiteral("doke_chromium_path_missing"),
               "invalid doke path should return precise missing-path error");
  ok &= expect(logs.size() == 1 && logs.first().contains(QStringLiteral("profile.start engine=doke_chromium")),
               "invalid doke runtime should log profile start line");
  return ok;
}
} // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  bool ok = true;
  ok &= testMissingProfileId();
  ok &= testUnsupportedEngine();
  ok &= testDokeInvalidExecutable();
  if (!ok) {
    return 1;
  }

  qInfo("profile_runtime_manager_ok");
  return 0;
}
