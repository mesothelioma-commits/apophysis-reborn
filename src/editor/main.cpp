#include "creative_widgets.hpp"
#include "flame_io.hpp"
#include "main_window.hpp"
#include "output_file.hpp"
#include <QApplication>
#include <QComboBox>
#include <QCommandLineParser>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <iostream>

namespace {
void creativeSmoke(apo::editor::MainWindow &window, const QString &path) {
    using namespace apo;
    using namespace apo::editor;
    struct State {
        int stage = 0;
        QPointer<QDialog> dialog;
    };
    auto state = std::make_shared<State>();
    const auto flame = window.session().document().render_model(0);
    auto gradient = new GradientDialog(flame, &window);
    gradient->resize(1080, 730);
    gradient->findChild<QComboBox *>("palettePresets")->setCurrentIndex(1);
    gradient->findChild<QPushButton *>("usePalettePreset")->click();
    gradient->show();
    state->dialog = gradient;
    auto timer = new QTimer(&window);
    QObject::connect(timer, &QTimer::timeout, &window, [state, timer, &window, path, flame] {
        auto dialog = state->dialog.data();
        if (!dialog) {
            QCoreApplication::exit(1);
            return;
        }
        const auto canvas = dialog->findChild<PreviewCanvas *>();
        if (!canvas || canvas->image().isNull())
            return;
        for (auto controller : dialog->findChildren<PreviewController *>())
            if (controller->isBusy())
                return;
        const auto explorer = qobject_cast<ExploreDialog *>(dialog);
        if (explorer) {
            if (explorer->candidates().empty())
                return;
            for (std::size_t i = 0; i < explorer->candidates().size(); ++i) {
                const auto tile =
                    explorer->findChild<QToolButton *>(QString("chooseCandidate%1").arg(i));
                if (!tile || tile->icon().isNull())
                    return;
            }
        }
        try {
            const std::array names{"gradient-editor.png", "adjustment-editor.png",
                                   "mutation-explorer.png", "random-explorer.png"};
            if (!dialog->grab().save(path + '/' + names.at(state->stage)))
                throw std::runtime_error("Could not save a creative editor screenshot");
            auto write = [&](const QString &name, const std::string &bytes) {
                write_atomic(filesystemPath(path + '/' + name),
                             [&](std::ostream &out) { out << bytes; });
            };
            if (auto gradient = qobject_cast<GradientDialog *>(dialog)) {
                gradient->exportPaletteFile(path + "/palette.gpl");
                write("gradient-example.flame", serialize_flame(gradient->resultFlame()));
            } else if (auto adjustment = qobject_cast<AdjustmentDialog *>(dialog)) {
                write("adjustment-example.flame", serialize_flame(adjustment->resultFlame()));
            } else if (state->stage == 2) {
                auto document = window.session().document();
                const auto &candidate = explorer->candidates().front();
                applyCreativeFlame(document, 0, candidate);
                LegacyEdits edit;
                edit.name = candidate.name;
                document.edit(0, edit);
                write("mutation-example.flame", document.bytes());
            } else {
                const auto &candidates = explorer->candidates();
                auto document = LegacyDocument::parse(serialize_flame(candidates.front()));
                for (std::size_t i = 1; i < candidates.size(); ++i)
                    document.append_flame(candidates[i]);
                write("random-batch.flame", document.bytes());
            }
            dialog->hide();
            dialog->deleteLater();
            ++state->stage;
            if (state->stage == 1) {
                auto next = new AdjustmentDialog(flame, &window);
                next->resize(980, 680);
                next->findChild<QTabWidget *>("adjustmentTabs")->setCurrentIndex(1);
                state->dialog = next;
            } else if (state->stage < 4) {
                state->dialog = new ExploreDialog(state->stage == 2 ? ExploreDialog::Mode::mutation
                                                                    : ExploreDialog::Mode::random,
                                                  flame, &window);
                state->dialog->resize(1280, 790);
            } else {
                timer->stop();
                QCoreApplication::exit(0);
                return;
            }
            state->dialog->show();
        } catch (const std::exception &error) {
            std::cerr << error.what() << '\n';
            timer->stop();
            QCoreApplication::exit(1);
        }
    });
    timer->start(100);
}
} // namespace

int main(int argc, char **argv) {
    QApplication application(argc, argv);
    application.setOrganizationName("ApophysisReborn");
    application.setApplicationName("Apophysis Reborn");
    application.setApplicationVersion("0.5.0");
    QCommandLineParser parser;
    parser.setApplicationDescription("Native flame library and transform editor");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("flame", "Optional .flame library to open");
    QCommandLineOption smoke(
        "smoke-test", "Open the editor, render the example, save a screenshot and exit.", "directory");
    parser.addOption(smoke);
    QCommandLineOption creativeSmokeOption(
        "creative-smoke-test",
        "Render the main editor and all creative dialogs, save examples and screenshots, then exit.",
        "directory");
    parser.addOption(creativeSmokeOption);
    parser.process(application);
    apo::editor::applyDarkTheme();
    try {
        const bool creative = parser.isSet(creativeSmokeOption);
        const bool testing = parser.isSet(smoke) || creative;
        apo::editor::MainWindow window(!testing);
        if (testing) {
            window.resize(1420, 900);
            const auto path = parser.value(creative ? creativeSmokeOption : smoke);
            if (!QDir().mkpath(path))
                throw std::runtime_error("Cannot create smoke-test output directory");
            auto started = std::make_shared<bool>(false);
            QObject::connect(&window.previewController(), &apo::editor::PreviewController::imageReady,
                             &window,
                             [&window, path, creative, started](const QImage &image, quint64, quint64,
                                                                quint64, unsigned, qint64, bool final) {
                                 if (!final || *started)
                                     return;
                                 *started = true;
                                 QTimer::singleShot(100, &window, [&window, path, image, creative] {
                                     bool ok = image.save(path + "/preview.png") &&
                                               window.grab().save(path + "/editor.png");
                                     try {
                                         window.session().save(path + "/saved-example.flame");
                                     } catch (const std::exception &) {
                                         ok = false;
                                     }
                                     if (ok && creative) {
                                         window.previewController().setPaused(true);
                                         creativeSmoke(window, path);
                                     } else
                                         QCoreApplication::exit(ok ? 0 : 1);
                                 });
                             });
            QTimer::singleShot(creative ? 120000 : 30000, &application,
                               [] { QCoreApplication::exit(2); });
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
        if (parser.isSet(smoke) || parser.isSet(creativeSmokeOption)) {
            std::cerr << error.what() << '\n';
            return 1;
        }
        QMessageBox::critical(nullptr, "Apophysis Reborn could not start",
                              QString::fromUtf8(error.what()));
        return 1;
    }
}
