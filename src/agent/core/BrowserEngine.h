#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

struct BrowserEngineDescriptor {
  QString id;
  QString executable;
  QString error;

  bool available() const {
    return !executable.isEmpty();
  }
};

struct BrowserLaunchRequest {
  QString profileId;
  QString profileName;
  QString dataDir;
  QString browserEngine;
  QString startUrl;
  QJsonObject proxy;
  QJsonObject fingerprint;
  QJsonObject engineOptions;
};

struct BrowserInstance {
  QString profileId;
  QString engine;
  QString executable;
  int pid = 0;
  int debuggingPort = 0;
};

class BrowserEngine {
public:
  virtual ~BrowserEngine() = default;
  virtual QString id() const = 0;
  virtual QString executablePath() const = 0;
  virtual bool isAvailable() const = 0;
};
