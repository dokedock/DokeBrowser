#pragma once

#include "BrowserEngine.h"

class BrowserEngineFactory {
public:
  static QString normalizeId(const QString& id);
  static QVector<BrowserEngineDescriptor> listEngines();
  static BrowserEngineDescriptor describe(const QString& id);
  static QString executableFor(const QString& id);
  static QString notFoundErrorFor(const QString& id);
};
