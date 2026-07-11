#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>

int main(int argc, char* argv[]) {
  QGuiApplication app(argc, argv);
  QQuickStyle::setStyle(QStringLiteral("Fusion"));
  QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/assets/DKIcon.png")));

  QQmlApplicationEngine engine;
  const QUrl url(QStringLiteral("qrc:/qml/Main.qml"));
  QObject::connect(
      &engine,
      &QQmlApplicationEngine::objectCreated,
      &app,
      [url](QObject* obj, const QUrl& objUrl) {
        if (!obj && url == objUrl) {
          QCoreApplication::exit(1);
        }
      },
      Qt::QueuedConnection);
  engine.load(url);

  return app.exec();
}

