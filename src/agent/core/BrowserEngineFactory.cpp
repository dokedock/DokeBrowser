#include "BrowserEngineFactory.h"

#include "DokeChromiumEngine.h"
#include "SystemChromeEngine.h"

QString BrowserEngineFactory::normalizeId(const QString& id) {
  QString v = id.trimmed();
  if (v.isEmpty()) {
    return QStringLiteral("system_chrome");
  }
  if (v == QStringLiteral("chrome") || v == QStringLiteral("chromium")) {
    return QStringLiteral("system_chrome");
  }
  if (v == QStringLiteral("doke") || v == QStringLiteral("doke-chromium")) {
    return QStringLiteral("doke_chromium");
  }
  return v;
}

QVector<BrowserEngineDescriptor> BrowserEngineFactory::listEngines() {
  return {
      describe(QStringLiteral("system_chrome")),
      describe(QStringLiteral("doke_chromium")),
  };
}

BrowserEngineDescriptor BrowserEngineFactory::describe(const QString& id) {
  const QString normalized = normalizeId(id);
  BrowserEngineDescriptor out;
  out.id = normalized;
  out.executable = executableFor(normalized);
  if (out.executable.isEmpty()) {
    out.error = notFoundErrorFor(normalized);
  }
  return out;
}

QString BrowserEngineFactory::executableFor(const QString& id) {
  const QString normalized = normalizeId(id);
  if (normalized == QStringLiteral("system_chrome")) {
    return SystemChromeEngine::resolveExecutable();
  }
  if (normalized == QStringLiteral("doke_chromium")) {
    return DokeChromiumEngine::resolveExecutable();
  }
  return {};
}

QString BrowserEngineFactory::notFoundErrorFor(const QString& id) {
  const QString normalized = normalizeId(id);
  if (normalized == QStringLiteral("system_chrome")) {
    return QStringLiteral("chrome_not_found");
  }
  if (normalized == QStringLiteral("doke_chromium")) {
    return QStringLiteral("doke_chromium_not_found");
  }
  return QStringLiteral("unsupported_browser_engine:%1").arg(normalized);
}
