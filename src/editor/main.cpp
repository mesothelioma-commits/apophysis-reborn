#include "main_window.hpp"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QTimer>
#include <iostream>

int main(int argc, char **argv) {
    QApplication application(argc, argv);
    application.setOrganizationName("ApophysisReborn");
    application.setApplicationName("Apophysis Reborn");
    application.setApplicationVersion("0.4.0");
    QCommandLineParser parser;
    parser.setApplicationDescription("Native flame library and transform editor");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("flame", "Optional .flame library to open");
    QCommandLineOption smoke(
        "smoke-test", "Open the editor, render the example, save a screenshot and exit.", "directory");
    parser.addOption(smoke);
    parser.process(application);
    apo::editor::applyDarkTheme();
    try {
        const bool testing = parser.isSet(smoke);
        apo::editor::MainWindow window(!testing);
        if (testing) {
            window.resize(1420, 900);
            const auto path = parser.value(smoke);
            if (!QDir().mkpath(path))
                throw std::runtime_error("Cannot create smoke-test output directory");
            QObject::connect(&window.previewController(), &apo::editor::PreviewController::imageReady,
                             &window,
                             [&window, path](const QImage &image, quint64, quint64, quint64, unsigned,
                                             qint64, bool final) {
                                 if (!final)
                                     return;
                                 QTimer::singleShot(100, &window, [&window, path, image] {
                                     bool ok = image.save(path + "/preview.png") &&
                                               window.grab().save(path + "/editor.png");
                                     try {
                                         window.session().save(path + "/saved-example.flame");
                                     } catch (const std::exception &) {
                                         ok = false;
                                     }
                                     QCoreApplication::exit(ok ? 0 : 1);
                                 });
                             });
            QTimer::singleShot(30000, &application, [] { QCoreApplication::exit(2); });
            window.openExample();
            window.show();
        } else {
            window.show();
            const auto args = parser.positionalArguments();
            if (!args.isEmpty())
                window.openFile(args.front());
            else {
                window.openExample();
                QTimer::singleShot(0, &window, &apo::editor::MainWindow::recoverPreviousSession);
            }
        }
        return application.exec();
    } catch (const std::exception &error) {
        if (parser.isSet(smoke)) {
            std::cerr << error.what() << '\n';
            return 1;
        }
        QMessageBox::critical(nullptr, "Apophysis Reborn could not start",
                              QString::fromUtf8(error.what()));
        return 1;
    }
}
