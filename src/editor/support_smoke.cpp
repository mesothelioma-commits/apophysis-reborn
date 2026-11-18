#include "support_smoke.hpp"
#include "support_dialogs.hpp"
#include "main_window.hpp"
#include "build_info.hpp"
#include "output_file.hpp"
#include <QApplication>
#include <QFile>
#include <QJsonDocument>
#include <QTextBrowser>
#include <QTimer>
#include <iostream>

namespace apo::editor {
void supportSmoke(MainWindow &window, const QString &directory) {
  auto manual = new UserManualDialog(&window);
  manual->setAttribute(Qt::WA_DeleteOnClose);
  manual->show();
  QTimer::singleShot(120, manual, [&window, manual, directory] {
    try {
      const auto browser = manual->findChild<QTextBrowser *>("manualBrowser");
      if (!browser || !browser->toPlainText().contains("checkpoint", Qt::CaseInsensitive) ||
          browser->openLinks() || browser->openExternalLinks() ||
          !manual->grab().save(directory + "/user-manual.png"))
        throw std::runtime_error("Embedded manual smoke verification failed");
      manual->close();
      auto information = new SystemInformationDialog(window.systemInformation(), &window);
      information->setAttribute(Qt::WA_DeleteOnClose);
      information->show();
      QTimer::singleShot(120, information, [information, directory] {
        try {
          information->saveReport(directory + "/system-information.json", true);
          information->saveReport(directory + "/system-information.txt", false);
          QFile saved(directory + "/system-information.json");
          if (!saved.open(QIODevice::ReadOnly) ||
              QJsonDocument::fromJson(saved.readAll()).object() != information->report() ||
              !information->grab().save(directory + "/system-information.png"))
            throw std::runtime_error("System information did not save or capture correctly");
          QJsonObject report{{"version", QString::fromLatin1(application_version())},
                             {"qt_platform", QGuiApplication::platformName()},
                             {"source_revision", QString::fromStdString(build_info().source_revision)},
                             {"embedded_manual_verified", true},
                             {"diagnostics_roundtrip_verified", true},
                             {"diagnostics", information->report()}};
          const auto bytes = QJsonDocument(report).toJson(QJsonDocument::Indented);
          write_atomic(filesystemPath(directory + "/support-report.json"),
                       [&](std::ostream &out) { out.write(bytes.constData(), bytes.size()); });
          information->close();
          QCoreApplication::exit(0);
        } catch (const std::exception &error) {
          std::cerr << "Support verification: " << error.what() << '\n';
          QCoreApplication::exit(1);
        }
      });
    } catch (const std::exception &error) {
      std::cerr << "Support verification: " << error.what() << '\n';
      QCoreApplication::exit(1);
    }
  });
}
}
