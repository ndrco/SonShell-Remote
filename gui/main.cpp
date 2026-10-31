#include <QCoreApplication>
#include "main_window.h"
#include "language_support.h"
#include "settings_store.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  app.setApplicationName("SonShell Remote");
  app.setApplicationVersion(SONSHELL_REMOTE_VERSION);
  app.setOrganizationName("SonShellRemote");
  app.setWindowIcon(QIcon::fromTheme("sonshell-remote"));
  LanguageSupport language;
  if (!language.install(SettingsStore().load().language)) return 1;
  QCommandLineParser parser;
  parser.setApplicationDescription(QCoreApplication::translate("App", "Sony camera control · SonShell Remote"));
  parser.addHelpOption(); parser.addVersionOption();
  parser.addOption({"sonshell", QCoreApplication::translate("App", "SonShell backend path"), "path"});
  parser.process(app);
  const QDir directory(QCoreApplication::applicationDirPath());
  QString binary = parser.value("sonshell");
  if (binary.isEmpty()) {
    // Build tree, normal prefix install, then Debian multiarch install.
    for (const auto& candidate : {directory.filePath("../sonshell"), directory.filePath("sonshell"),
        directory.filePath("../lib/sonshell-remote/sonshell"),
        directory.filePath("../lib/x86_64-linux-gnu/sonshell-remote/sonshell"),
        directory.filePath("../lib/aarch64-linux-gnu/sonshell-remote/sonshell")}) {
      if (QFileInfo(candidate).isExecutable()) { binary = candidate; break; }
    }
  }
  MainWindow window(binary);
  window.show();
  return app.exec();
}
