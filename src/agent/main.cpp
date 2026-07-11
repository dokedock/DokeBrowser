#include <QCoreApplication>
#include <QTimer>

int main(int argc, char* argv[]) {
  QCoreApplication app(argc, argv);
  QTimer::singleShot(0, []() {});
  return app.exec();
}

