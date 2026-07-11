#include <QCoreApplication>
#include <QTextStream>

#include "core/IpcServer.h"

int main(int argc, char* argv[]) {
  QCoreApplication app(argc, argv);

  QTextStream out(stdout);
  out << "dokebrowser_agent starting\n";
  out.flush();

  IpcServer server;
  QObject::connect(&server, &IpcServer::logLine, &app, [&](const QString& line) {
    out << "agent: " << line << "\n";
    out.flush();
  });

  if (!server.start()) {
    out << "agent: listen_failed\n";
    out.flush();
    return 2;
  }

  out << "agent: listening\n";
  out.flush();

  return app.exec();
}
