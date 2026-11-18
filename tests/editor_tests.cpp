#include "editor/creative_widgets.hpp"
#include "editor/main_window.hpp"
#include "flame_io.hpp"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QInputDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QElapsedTimer>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <cmath>

using namespace apo;
using namespace apo::editor;

namespace {
template <class T> T *widget(QObject &window, const char *name) {
    auto result = window.findChild<T *>(name);
    if (!result)
        qFatal("Missing editor control %s", name);
    return result;
}
void enter(QLineEdit *field, const QString &value) {
    field->setFocus();
    field->selectAll();
    QTest::keyClicks(field, value);
    QTest::keyClick(field, Qt::Key_Return);
}
void fastPreview(MainWindow &window) {
    auto settings = window.previewController().settings();
    settings.sample_limit = 100000;
    settings.threads = 2;
    settings.debounce_ms = 1;
    window.previewController().setSettings(settings);
}
void enterDialogField(QLineEdit *field, const QString &value) {
    field->setFocus();
    field->selectAll();
    QTest::keyClicks(field, value);
    // Tab commits the edit without invoking the dialog's default Apply button.
    QTest::keyClick(field, Qt::Key_Tab);
}
template <class Dialog, class Interaction>
bool throughAction(MainWindow &window, const char *action, Interaction interaction) {
    bool entered = false, expired = false;
    QTimer driver, deadline;
    driver.setSingleShot(true);
    deadline.setSingleShot(true);
    QObject::connect(&driver, &QTimer::timeout, &window, [&] {
        auto dialog = qobject_cast<Dialog *>(QApplication::activeModalWidget());
        if (!dialog)
            return;
        entered = true;
        interaction(*dialog);
        // A failed assertion inside the callback must not leave exec() waiting.
        if (dialog->isVisible())
            dialog->reject();
    });
    QObject::connect(&deadline, &QTimer::timeout, &window, [&] {
        expired = true;
        if (auto dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
            dialog->reject();
    });
    driver.start(0);
    deadline.start(30000);
    widget<QAction>(window, action)->trigger();
    deadline.stop();
    return entered && !expired;
}
} // namespace

class EditorTests : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("ApophysisRebornTests");
        QCoreApplication::setApplicationName("EditorWorkflow");
        applyDarkTheme();
    }
    void deviceDiscoveryDoesNotBlockDialogsOrDestruction() {
        struct Gate {
            std::mutex mutex;
            std::condition_variable changed;
            bool release = false;
            std::atomic<unsigned> calls = 0;
            std::atomic<bool> returned = false;
        };
        auto gate = std::make_shared<Gate>();
        struct ReleaseGate {
            std::shared_ptr<Gate> gate;
            ~ReleaseGate() {
                { std::lock_guard lock(gate->mutex); gate->release = true; }
                gate->changed.notify_all();
            }
        } release{gate};
        auto discovery = std::make_shared<DeviceDiscovery>([gate] {
            ++gate->calls;
            std::unique_lock lock(gate->mutex);
            gate->changed.wait_for(lock, std::chrono::seconds(10), [&] { return gate->release; });
            ComputeInfo result;
            result.compiled = result.loader_available = true;
            result.diagnostic = "Injected delayed driver completed";
            gate->returned = true;
            return result;
        });
        auto window = std::make_unique<MainWindow>(false, nullptr, QString{}, discovery);
        window->previewController().setPaused(true);
        window->show();
        // CPU-only preferences do not enter a graphics driver at all.
        QVERIFY(throughAction<QDialog>(*window, "actionPreferences", [&](QDialog &dialog) {
            QCOMPARE(gate->calls.load(), 0U);
            dialog.reject();
        }));
        QVERIFY(throughAction<QDialog>(*window, "actionPreferences", [&](QDialog &dialog) {
            auto backend = widget<QComboBox>(dialog, "renderBackend");
            backend->setCurrentIndex(backend->findData(int(RenderBackend::vulkan)));
            QTRY_COMPARE_WITH_TIMEOUT(gate->calls.load(), 1U, 3000);
            QVERIFY(discovery->snapshot().pending);
            QVERIFY(widget<QLabel>(dialog, "computeDetails")->text().contains("background"));
            unsigned heartbeats = 0;
            QTimer heartbeat;
            heartbeat.setInterval(5);
            connect(&heartbeat, &QTimer::timeout, &dialog, [&] { ++heartbeats; });
            heartbeat.start();
            QTRY_VERIFY_WITH_TIMEOUT(heartbeats >= 8, 1000);
            for (int i = 0; i < 8; ++i) {
                backend->setCurrentIndex(backend->findData(int(RenderBackend::cpu)));
                backend->setCurrentIndex(backend->findData(int(RenderBackend::vulkan)));
                QVERIFY(!discovery->request(true));
            }
            QCOMPARE(gate->calls.load(), 1U); // No background-probe backlog.
            dialog.reject();
        }));
        QElapsedTimer elapsed;
        elapsed.start();
        window.reset();
        discovery.reset();
        QVERIFY2(elapsed.elapsed() < 500, "Closing the UI waited for a blocked driver probe");
        QVERIFY(!gate->returned.load());
        { std::lock_guard lock(gate->mutex); gate->release = true; }
        gate->changed.notify_all();
        QTRY_VERIFY_WITH_TIMEOUT(gate->returned.load(), 3000);
    }
    void changeSeedThenSaveKeepsPreviewAndNewerEdits() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("seed.apoproject");
        ProjectFile original;
        original.library_bytes = serialize_flame(demo_flame());
        original.library_bytes += "<!--" + std::string(2 * 1024 * 1024, 'x') + "-->\n";
        original.metadata["vendor"] = "preserved";
        save_project(original, filesystemPath(path));
        MainWindow window(false);
        fastPreview(window);
        window.show();
        QVERIFY(window.openFile(path));
        QVERIFY(throughAction<QDialog>(window, "actionPreferences", [&](QDialog &dialog) {
            widget<QLineEdit>(dialog, "previewSeed")->setText("18446744073709551614");
            dialog.accept();
        }));
        QCOMPARE(window.previewController().settings().seed, UINT64_C(18446744073709551614));
        QTRY_VERIFY_WITH_TIMEOUT(!window.previewController().isBusy(), 10000);
        QSignalSpy busy(&window.previewController(), &PreviewController::busyChanged);
        QSignalSpy changed(&window.session(), &DocumentSession::changed);
        bool dispatched = false;
        QMetaObject::invokeMethod(&window, [&] {
            const auto dialog = QApplication::activeModalWidget();
            dispatched = dialog && dialog->objectName() == "savingDocumentDialog";
        }, Qt::QueuedConnection);
        int heartbeats = 0;
        qint64 previous = 0, longestGap = 0;
        QElapsedTimer elapsed;
        QTimer heartbeat;
        heartbeat.setTimerType(Qt::PreciseTimer);
        connect(&heartbeat, &QTimer::timeout, &window, [&] {
            const auto now = elapsed.elapsed();
            longestGap = std::max(longestGap, now - previous);
            previous = now;
            ++heartbeats;
        });
        elapsed.start();
        heartbeat.start(1);
        widget<QAction>(window, "actionSave")->trigger();
        heartbeat.stop();
        const auto duration = elapsed.elapsed();
        longestGap = std::max(longestGap, duration - previous);
        qInfo("Explicit save: %llu library bytes in %lld ms, %d GUI heartbeats, longest gap %lld ms",
              static_cast<unsigned long long>(original.library_bytes.size()),
              static_cast<long long>(duration), heartbeats, static_cast<long long>(longestGap));
        QVERIFY(dispatched);
        QVERIFY2(heartbeats >= 2, "The UI timer stalled while saving the project");
        QCOMPARE(changed.size(), 0);
        QCOMPARE(busy.size(), 0);
        QVERIFY(!window.session().isModified());
        auto saved = load_project(filesystemPath(path));
        QCOMPARE(saved.preview_settings.at("seed"), std::string("18446744073709551614"));
        QCOMPARE(saved.metadata.at("vendor"), std::string("preserved"));
        QCOMPARE(saved.library_bytes, original.library_bytes);

        // Model work already queued before Save may arrive while its snapshot
        // is being written. It must survive both the write and a close request.
        QMetaObject::invokeMethod(&window, [&] {
            window.close();
            window.session().change("New edit during save", [](LegacyDocument &document) {
                LegacyEdits edit;
                edit.name = "Edited while saving";
                document.edit(0, edit);
            });
        }, Qt::QueuedConnection);
        widget<QAction>(window, "actionSave")->trigger();
        QVERIFY(window.isVisible());
        QVERIFY(window.session().isModified());
        QCOMPARE(window.session().document().entries()[0].name, std::string("Edited while saving"));
        QCOMPARE(load_project(filesystemPath(path)).library_bytes, original.library_bytes);
        widget<QAction>(window, "actionSave")->trigger();
        QVERIFY(!window.session().isModified());
        QCOMPARE(load_project(filesystemPath(path)).library_bytes, window.session().document().bytes());
    }
    void saveAsCancelAndConflictCopyPreserveBothDocuments() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto source = directory.filePath("original.apoproject");
        const auto copy = directory.filePath("copy.apoproject");
        ProjectFile project;
        project.library_bytes = serialize_flame(demo_flame());
        save_project(project, filesystemPath(source));
        MainWindow window(false);
        window.previewController().setPaused(true);
        window.show();
        QVERIFY(window.openFile(source));
        window.session().change("Local edit", [](LegacyDocument &document) {
            LegacyEdits edit;
            edit.name = "Local edit";
            document.edit(0, edit);
        });
        const auto local = window.session().document().bytes();
        project.metadata["external"] = "external edit";
        save_project(project, filesystemPath(source));

        struct NativeDialogPolicy {
            bool previous = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
            NativeDialogPolicy() { QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true); }
            ~NativeDialogPolicy() { QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, previous); }
        } policy;
        QVERIFY(throughAction<QFileDialog>(window, "actionSaveAs", [&](QFileDialog &dialog) {
            dialog.reject();
        }));
        QVERIFY(window.session().isModified());
        QCOMPARE(window.session().path(), source);

        QTimer drive;
        drive.setInterval(5);
        bool chooseCopy = false;
        int conflicts = 0, filenames = 0;
        connect(&drive, &QTimer::timeout, &window, [&] {
            auto modal = QApplication::activeModalWidget();
            if (auto box = qobject_cast<QMessageBox *>(modal)) {
                if (box->windowTitle() == "File changed on disk") {
                    ++conflicts;
                    if (chooseCopy) {
                        for (auto button : box->buttons())
                            if (box->buttonRole(button) == QMessageBox::AcceptRole) {
                                button->click();
                                return;
                            }
                    }
                }
                box->reject();
            } else if (auto file = qobject_cast<QFileDialog *>(modal)) {
                ++filenames;
                file->selectFile(copy);
                static_cast<QDialog *>(file)->accept();
            }
        });
        drive.start();
        widget<QAction>(window, "actionSave")->trigger();
        QCOMPARE(conflicts, 1);
        QVERIFY(window.session().isModified());
        QCOMPARE(window.session().path(), source);
        chooseCopy = true;
        widget<QAction>(window, "actionSave")->trigger();
        drive.stop();
        QCOMPARE(conflicts, 2);
        QCOMPARE(filenames, 1);
        QCOMPARE(window.session().path(), copy);
        QVERIFY(!window.session().isModified());
        QCOMPARE(load_project(filesystemPath(copy)).library_bytes, local);
        const auto external = load_project(filesystemPath(source));
        QCOMPARE(external.library_bytes, project.library_bytes);
        QCOMPARE(external.metadata.at("external"), std::string("external edit"));
    }
    void savingRenderPresetCapturesSeedWithoutRebuildingProject() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("render-preset.apoproject");
        ProjectFile original;
        original.library_bytes = serialize_flame(demo_flame());
        save_project(original, filesystemPath(path));
        MainWindow window(false);
        window.previewController().setPaused(true);
        window.show();
        QVERIFY(window.openFile(path));
        QImage image(32, 32, QImage::Format_RGB32);
        image.fill(Qt::blue);
        window.findChild<PreviewCanvas *>()->setImage(image);
        QVERIFY(throughAction<QDialog>(window, "actionRender", [&](QDialog &dialog) {
            widget<QLineEdit>(dialog, "outputSeed")->setText("1729");
            QTimer::singleShot(0, &dialog, [] {
                if (auto name = qobject_cast<QInputDialog *>(QApplication::activeModalWidget())) {
                    name->setTextValue("Seed 1729");
                    name->accept();
                }
            });
            widget<QPushButton>(dialog, "saveRenderPreset")->click();
            dialog.reject();
        }));
        auto project = window.session().projectSnapshot();
        QVERIFY(window.session().isModified());
        QCOMPARE(project.render_presets.size(), std::size_t(1));
        QCOMPARE(project.render_presets[0].settings.at("outputSeed"), std::string("1729"));
        QCOMPARE(project.preview_settings.count("thumbnail-png"), std::size_t(0));
        widget<QAction>(window, "actionSave")->trigger();
        const auto saved = load_project(filesystemPath(path));
        QCOMPARE(saved.render_presets, project.render_presets);
        QCOMPARE(saved.preview_settings.count("thumbnail-png"), std::size_t(1));
        QVERIFY(!window.session().isModified());
    }
    void periodicRecoveryCapturesViewAndThumbnailWithoutChangingDocument() {
        MainWindow window(false);
        window.previewController().setPaused(true);
        ProjectFile project;
        project.library_bytes = serialize_flame(demo_flame());
        project.metadata["vendor"] = "retain native envelope";
        project.animation_data = "opaque future timeline";
        project.plugin_references.push_back({"missing-plugin", "3", "vendor.apoplugin", "hash",
                                             {{"vendor-setting", "retain"}}});
        window.session().loadBytes(serialize_project(project), {}, true);
        const auto original = window.session().document().bytes();
        auto settings = window.previewController().settings();
        settings.seed = 123789;
        settings.threads = 1;
        window.previewController().setSettings(settings);
        QImage image(768, 384, QImage::Format_RGB32);
        image.fill(QColor(50, 80, 140));
        const auto canvas = window.findChild<PreviewCanvas *>();
        QVERIFY(canvas);
        canvas->setImage(image);
        QSignalSpy saved(&window.session(), &DocumentSession::recoverySaved);
        QSignalSpy failed(&window.session(), &DocumentSession::recoveryFailed);
        auto timer = widget<QTimer>(window, "recoveryTimer");
        QCOMPARE(timer->interval(), 45000);
        timer->setSingleShot(true);
        timer->start(1);
        QTRY_COMPARE_WITH_TIMEOUT(saved.size(), 1, 10000);
        QCOMPARE(failed.size(), 0);
        const auto recovery = window.session().recoveryPath();
        QFile file(recovery);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto snapshot = parse_project(file.readAll().toStdString());
        file.close();
        QCOMPARE(snapshot.library_bytes, original);
        QCOMPARE(snapshot.metadata.at("vendor"), project.metadata.at("vendor"));
        QCOMPARE(snapshot.animation_data, project.animation_data);
        QCOMPARE(snapshot.plugin_references, project.plugin_references);
        QCOMPARE(snapshot.preview_settings.at("seed"), std::string("123789"));
        QVERIFY(!snapshot.workspace.at("qt-layout").empty());
        const auto encoded = QByteArray::fromStdString(snapshot.preview_settings.at("thumbnail-png"));
        const auto thumbnail = QImage::fromData(QByteArray::fromBase64(encoded), "PNG");
        QCOMPARE(thumbnail.size(), QSize(384, 192));
        QCOMPARE(thumbnail.pixelColor(0, 0), QColor(50, 80, 140));
        QCOMPARE(window.session().document().bytes(), original);
        QCOMPARE(window.session().undoStack().count(), 0);
        QCOMPARE(window.session().projectSnapshot().preview_settings.count("thumbnail-png"), std::size_t(0));
        QVERIFY(window.session().isModified());
        window.session().undoStack().setClean();
        QTRY_VERIFY_WITH_TIMEOUT(!window.session().isRecoveryBusy(), 5000);
        QVERIFY(!QFileInfo::exists(recovery));
    }
    void closeWaitsResponsivelyForDiscardedRecoveryAndConfirmsOnce() {
        struct QuitPolicy {
            bool previous = QApplication::quitOnLastWindowClosed();
            QuitPolicy() { QApplication::setQuitOnLastWindowClosed(false); }
            ~QuitPolicy() { QApplication::setQuitOnLastWindowClosed(previous); }
        } policy;
        MainWindow window(false);
        window.previewController().setPaused(true);
        ProjectFile project;
        project.library_bytes = serialize_flame(demo_flame());
        window.session().loadBytes(serialize_project(project), {}, true);
        window.show();
        window.showRenderQueue();
        const auto queueView = window.findChild<RenderQueueDialog *>();
        QVERIFY(queueView && queueView->isVisible());
        const auto recovery = window.session().writeRecovery();
        QVERIFY(QFileInfo::exists(recovery));
        struct Gate {
            std::mutex mutex;
            std::condition_variable changed;
            bool release = false;
            std::atomic<bool> entered = false, timedOut = false;
        };
        auto gate = std::make_shared<Gate>();
        struct ReleaseGate {
            std::shared_ptr<Gate> gate;
            ~ReleaseGate() {
                { std::lock_guard lock(gate->mutex); gate->release = true; }
                gate->changed.notify_all();
            }
        } release{gate};
        window.session().requestRecovery(window.session().projectSnapshot(), [gate](ProjectFile &) {
            gate->entered = true;
            std::unique_lock lock(gate->mutex);
            if (!gate->changed.wait_for(lock, std::chrono::seconds(10), [&] { return gate->release; }))
                gate->timedOut = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(gate->entered.load(), 3000);
        unsigned prompts = 0, heartbeats = 0;
        QTimer driver, heartbeat;
        driver.setInterval(5);
        heartbeat.setInterval(5);
        connect(&driver, &QTimer::timeout, this, [&] {
            const auto box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (box && box->windowTitle() == "Unsaved flame edits") {
                ++prompts;
                box->button(QMessageBox::Discard)->click();
            }
        });
        connect(&heartbeat, &QTimer::timeout, this, [&] { ++heartbeats; });
        driver.start();
        heartbeat.start();
        QVERIFY(!window.close()); // The worker is still blocked; close is deferred.
        QCOMPARE(prompts, 1U);
        QVERIFY(window.isVisible());
        QVERIFY(!queueView->isVisible());
        QVERIFY(!window.isEnabled());
        QVERIFY(!widget<QLineEdit>(window, "flameName")->isEnabled());
        QVERIFY(!widget<QAction>(window, "actionNew")->isEnabled());
        QVERIFY(!widget<QTimer>(window, "recoveryTimer")->isActive());
        const auto before = heartbeats;
        QTRY_VERIFY_WITH_TIMEOUT(heartbeats >= before + 8, 1000);
        for (unsigned i = 0; i < 4; ++i)
            QVERIFY(!window.close());
        QCOMPARE(window.findChildren<QTimer *>("pendingCloseTimer").size(), 1);
        QCOMPARE(prompts, 1U);
        QVERIFY(QFileInfo::exists(recovery));
        { std::lock_guard lock(gate->mutex); gate->release = true; }
        gate->changed.notify_all();
        QTRY_VERIFY_WITH_TIMEOUT(!window.isVisible(), 5000);
        QVERIFY(!window.session().isRecoveryBusy());
        QVERIFY(!widget<QTimer>(window, "pendingCloseTimer")->isActive());
        QCOMPARE(prompts, 1U);
        QVERIFY(!QFileInfo::exists(recovery));
        QVERIFY(!QFileInfo::exists(recovery + ".json"));
        QVERIFY(!QFileInfo::exists(recovery + ".lock"));
        QVERIFY(!gate->timedOut.load());
        QCOMPARE(window.session().document().bytes(), project.library_bytes);
    }
    void rendererPreferencesPersistAndCancel() {
        QTemporaryDir identity;
        QVERIFY(identity.isValid());
        struct IdentityGuard {
            QString organization = QCoreApplication::organizationName();
            QString application = QCoreApplication::applicationName();
            ~IdentityGuard() {
                QSettings temporary;
                temporary.clear();
                temporary.sync();
                QCoreApplication::setOrganizationName(organization);
                QCoreApplication::setApplicationName(application);
            }
        } restore;
        QCoreApplication::setOrganizationName("ApophysisRebornTests");
        QCoreApplication::setApplicationName("RendererPreferences-" +
                                             QFileInfo(identity.path()).fileName());
        {
            MainWindow window(true);
            QCOMPARE(window.previewController().settings().backend, RenderBackend::automatic);
            auto initial = window.previewController().settings();
            initial.device = "retired-device-for-settings-test";
            window.previewController().setSettings(initial);
            window.show();
            QVERIFY(throughAction<QDialog>(window, "actionPreferences", [&](QDialog &dialog) {
                QCOMPARE(dialog.objectName(), QString("preferencesDialog"));
                auto backend = widget<QComboBox>(dialog, "renderBackend");
                backend->setCurrentIndex(backend->findData(int(RenderBackend::vulkan)));
                widget<QSpinBox>(dialog, "gpuMemory")->setValue(96);
                widget<QCheckBox>(dialog, "cpuFallback")->setChecked(false);
                QTRY_VERIFY_WITH_TIMEOUT(widget<QLabel>(dialog, "computeDetails")->text().contains("unavailable"), 10000);
                dialog.reject();
            }));
            QCOMPARE(window.previewController().settings().backend, RenderBackend::automatic);
            QCOMPARE(window.previewController().settings().gpu_memory_mib, UINT64_C(512));
            QVERIFY(window.previewController().settings().allow_cpu_fallback);

            QVERIFY(throughAction<QDialog>(window, "actionPreferences", [&](QDialog &dialog) {
                auto backend = widget<QComboBox>(dialog, "renderBackend");
                backend->setCurrentIndex(backend->findData(int(RenderBackend::vulkan)));
                widget<QSpinBox>(dialog, "gpuMemory")->setValue(96);
                widget<QCheckBox>(dialog, "cpuFallback")->setChecked(false);
                dialog.accept();
            }));
            const auto &settings = window.previewController().settings();
            QCOMPARE(settings.backend, RenderBackend::vulkan);
            QCOMPARE(settings.device, std::string("retired-device-for-settings-test"));
            QCOMPARE(settings.gpu_memory_mib, UINT64_C(96));
            QVERIFY(!settings.allow_cpu_fallback);
        }
        {
            MainWindow restored(true);
            const auto &settings = restored.previewController().settings();
            QCOMPARE(settings.backend, RenderBackend::vulkan);
            QCOMPARE(settings.device, std::string("retired-device-for-settings-test"));
            QCOMPARE(settings.gpu_memory_mib, UINT64_C(96));
            QVERIFY(!settings.allow_cpu_fallback);
        }
    }
    void creativeAndExportInheritRendererSelection() {
        MainWindow window(false);
        fastPreview(window);
        auto settings = window.previewController().settings();
        settings.backend = RenderBackend::vulkan;
        settings.device = "__missing_creative_test_device__";
        settings.gpu_memory_mib = 48;
        settings.allow_cpu_fallback = true;
        window.previewController().setSettings(settings);
        window.openExample();
        window.show();
        QTRY_VERIFY_WITH_TIMEOUT(!window.previewController().isBusy(), 10000);
        QVERIFY(widget<QLabel>(window, "previewStatistics")->text().contains("fallback"));
        const auto check = [&](QDialog &dialog) {
            const auto controllers = dialog.findChildren<PreviewController *>();
            QVERIFY(!controllers.isEmpty());
            for (const auto controller : controllers) {
                QCOMPARE(controller->settings().backend, RenderBackend::vulkan);
                QCOMPARE(controller->settings().device, settings.device);
                QCOMPARE(controller->settings().gpu_memory_mib, UINT64_C(48));
                QVERIFY(controller->settings().allow_cpu_fallback);
            }
            dialog.reject();
        };
        QVERIFY(throughAction<GradientDialog>(window, "actionGradient", check));
        QVERIFY(throughAction<AdjustmentDialog>(window, "actionAdjustments", check));
        QVERIFY(throughAction<ExploreDialog>(window, "actionMutations", check));
        QVERIFY(throughAction<ExploreDialog>(window, "actionRandomFlames", check));
        QVERIFY(throughAction<QDialog>(window, "actionRender", [&](QDialog &dialog) {
            QCOMPARE(dialog.objectName(), QString("renderSettingsDialog"));
            QCOMPARE(widget<QComboBox>(dialog, "renderBackend")->currentData().toInt(),
                     int(RenderBackend::vulkan));
            QCOMPARE(widget<QComboBox>(dialog, "renderDevice")->currentData().toString(),
                     QString::fromStdString(settings.device));
            QCOMPARE(widget<QSpinBox>(dialog, "gpuMemory")->value(), 48);
            QVERIFY(widget<QCheckBox>(dialog, "cpuFallback")->isChecked());
            dialog.reject();
        }));
    }
    void rendererDetailsGrowAfterBackendChange() {
        MainWindow window(false);
        fastPreview(window);
        window.previewController().setPaused(true);
        window.openExample();
        window.show();
        const auto checkLayout = [&](QDialog &dialog) {
            auto backend = widget<QComboBox>(dialog, "renderBackend");
            auto device = widget<QComboBox>(dialog, "renderDevice");
            auto details = widget<QLabel>(dialog, "computeDetails");
            QCOMPARE(backend->currentData().toInt(), int(RenderBackend::cpu));
            QCoreApplication::processEvents();
            const int originalHeight = dialog.height();
            backend->setCurrentIndex(backend->findData(int(RenderBackend::vulkan)));
            if (device->count() > 1)
                device->setCurrentIndex(1);
            QVERIFY(details->text().contains(
                "identical pixels across backends or devices are not guaranteed"));
            QTRY_VERIFY_WITH_TIMEOUT(details->height() >= details->heightForWidth(details->width()),
                                     3000);
            QVERIFY(dialog.height() >= originalHeight);
            auto buttons = dialog.findChild<QDialogButtonBox *>();
            QVERIFY(buttons);
            QVERIFY(details->mapTo(&dialog, QPoint(0, details->height())).y() <=
                    buttons->mapTo(&dialog, QPoint(0, 0)).y());
            QVERIFY(buttons->mapTo(&dialog, QPoint(0, buttons->height())).y() <= dialog.height());
            const int expandedHeight = dialog.height();
            backend->setCurrentIndex(backend->findData(int(RenderBackend::cpu)));
            QCoreApplication::processEvents();
            QCOMPARE(dialog.height(), expandedHeight);
            dialog.reject();
        };
        QVERIFY(throughAction<QDialog>(window, "actionPreferences", checkLayout));
        QVERIFY(throughAction<QDialog>(window, "actionRender", checkLayout));
    }
    void editUndoSaveAndReopen() {
        MainWindow window(false);
        fastPreview(window);
        window.openExample();
        window.show();
        auto canvas = widget<PreviewCanvas>(window, "previewCanvas");
        QTRY_VERIFY_WITH_TIMEOUT(!canvas->image().isNull(), 10000);
        const auto original = window.session().document().bytes();
        const auto originalModel = window.session().document().render_model(0);
        enter(widget<QLineEdit>(window, "flameName"), "Edited & saved");
        QCOMPARE(window.session().document().entries()[0].name, std::string("Edited & saved"));
        enter(widget<QLineEdit>(window, "coefficient_a"), "0.125");
        QCOMPARE(window.session().document().render_model(0).transforms[0].pre.a, .125);
        QCOMPARE(window.session().undoStack().count(), 2);
        widget<QAction>(window, "actionUndo")->trigger();
        QCOMPARE(window.session().document().render_model(0).transforms[0].pre.a,
                 originalModel.transforms[0].pre.a);
        widget<QAction>(window, "actionUndo")->trigger();
        QCOMPARE(window.session().document().bytes(), original);
        widget<QAction>(window, "actionRedo")->trigger();
        widget<QAction>(window, "actionRedo")->trigger();
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const auto file = folder.filePath("edited with spaces.flame");
        window.session().save(file);
        QVERIFY(!window.session().isModified());
        QVERIFY(window.openFile(file));
        QCOMPARE(window.session().document().entries()[0].name, std::string("Edited & saved"));
        QCOMPARE(window.session().document().render_model(0).transforms[0].pre.a, .125);
        QTRY_VERIFY_WITH_TIMEOUT(!window.previewController().isBusy(), 10000);
        QVERIFY(!canvas->image().isNull());
    }
    void transformCommandsAndDrag() {
        MainWindow window(false);
        fastPreview(window);
        window.openExample();
        window.show();
        QCoreApplication::processEvents();
        const auto first = window.session().document().render_model(0);
        const auto count = first.transforms.size();
        widget<QAction>(window, "actionDuplicateTransform")->trigger();
        auto model = window.session().document().render_model(0);
        QCOMPARE(model.transforms.size(), count + 1);
        QCOMPARE(window.session().selectedTransform(), int(count));
        QCOMPARE(model.transforms[0].weight + model.transforms.back().weight,
                 first.transforms[0].weight);
        widget<QAction>(window, "actionDeleteTransform")->trigger();
        QCOMPARE(window.session().document().render_model(0).transforms.size(), count);
        widget<QAction>(window, "actionUndo")->trigger();
        widget<QAction>(window, "actionUndo")->trigger();
        auto mode = widget<QComboBox>(window, "matrixMode");
        mode->setCurrentIndex(1);
        enter(widget<QLineEdit>(window, "coefficient_e"), "0.25");
        model = window.session().document().render_model(0);
        QCOMPARE(model.transforms[0].pre.e, first.transforms[0].pre.e);
        QCOMPARE(model.transforms[0].post.e, .25);
        widget<QAction>(window, "actionAddFinal")->trigger();
        QVERIFY(window.session().isFinalTransform());
        enter(widget<QLineEdit>(window, "coefficient_d"), "0.8");
        QCOMPARE(window.session().document().render_model(0).final_transform->post.d, .8);
        widget<QAction>(window, "actionDeleteTransform")->trigger();
        QVERIFY(!window.session().document().render_model(0).final_transform);
        mode->setCurrentIndex(0);
        auto triangles = widget<TriangleCanvas>(window, "triangleCanvas");
        triangles->fit();
        model = window.session().document().render_model(0);
        const auto a = model.transforms[0].pre;
        const auto start = triangles->toScreen({a.e, a.f}).toPoint();
        const auto end = start + QPoint(20, 8);
        const auto edits = window.session().undoStack().index();
        QTest::mousePress(triangles, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(triangles, end);
        QTest::mouseRelease(triangles, Qt::LeftButton, Qt::NoModifier, end);
        QCOMPARE(window.session().undoStack().index(), edits + 1);
        QVERIFY(window.session().document().render_model(0).transforms[0].pre.e != a.e);
        widget<QAction>(window, "actionUndo")->trigger();
        QCOMPARE(window.session().document().render_model(0).transforms[0].pre.e, a.e);
        // Avoid a close prompt. Destruction tests run without invoking window-close actions.
    }
    void unsupportedSiblingAndPause() {
        const std::string xml =
            "<flames><!-- original --><flame name='Unsupported' size='80 60'><xform weight='1' "
            "vendor_plugin='1'/></flame>"
            "<flame name='Supported' size='80 60' scale='6'><xform weight='1' linear='1' coefs='0 0 0 0 "
            "0 0'/><color index='0' rgb='100 200 50'/></flame></flames>";
        MainWindow window(false);
        fastPreview(window);
        window.session().loadBytes(xml);
        window.show();
        QVERIFY(!widget<QAction>(window, "actionAddTransform")->isEnabled());
        QVERIFY(widget<QLabel>(window, "renderDiagnostic")->isVisible());
        enter(widget<QLineEdit>(window, "flameName"), "Plugin preserved");
        QVERIFY(window.session().document().bytes().find("vendor_plugin='1'") != std::string::npos);
        auto library = widget<QListWidget>(window, "flameLibrary");
        library->setCurrentRow(1);
        QVERIFY(widget<QAction>(window, "actionAddTransform")->isEnabled());
        QTRY_VERIFY_WITH_TIMEOUT(!widget<PreviewCanvas>(window, "previewCanvas")->image().isNull(),
                                 10000);
        auto pause = widget<QAction>(window, "actionPause");
        pause->setChecked(true);
        QVERIFY(window.previewController().isPaused());
        enter(widget<QLineEdit>(window, "coefficient_e"), "0.1");
        QVERIFY(!window.previewController().isBusy());
        pause->setChecked(false);
        QTRY_VERIFY_WITH_TIMEOUT(!window.previewController().isBusy(), 10000);
        QVERIFY(window.session().document().bytes().find("<!-- original -->") != std::string::npos);
        QCOMPARE(window.session().document().entries()[0].name, std::string("Plugin preserved"));
    }
    void gradientAndAdjustmentActionsCommitOrCancel() {
        MainWindow window(false);
        fastPreview(window);
        window.openExample();
        window.show();
        const auto original = window.session().document().bytes();
        const auto palette = window.session().document().render_model(0).palette;
        QVERIFY(throughAction<GradientDialog>(window, "actionGradient", [&](GradientDialog &dialog) {
            QVERIFY(window.previewController().isPaused());
            QTest::mouseClick(widget<QPushButton>(dialog, "reversePalette"), Qt::LeftButton);
            QCOMPARE(window.session().document().bytes(), original);
            QTest::mouseClick(widget<QPushButton>(dialog, "acceptGradient"), Qt::LeftButton);
        }));
        QVERIFY(!window.previewController().isPaused());
        QCOMPARE(window.session().undoStack().index(), 1);
        QCOMPARE(window.session().undoStack().count(), 1);
        const auto reversed = window.session().document().render_model(0).palette;
        QVERIFY(std::abs(reversed.front().r - palette.back().r) < 1e-15);
        QVERIFY(std::abs(reversed.front().g - palette.back().g) < 1e-15);
        QVERIFY(std::abs(reversed.back().b - palette.front().b) < 1e-15);
        widget<QAction>(window, "actionUndo")->trigger();
        QCOMPARE(window.session().document().bytes(), original);
        QVERIFY(!window.session().isModified());

        QVERIFY(
            throughAction<AdjustmentDialog>(window, "actionAdjustments", [&](AdjustmentDialog &dialog) {
                enterDialogField(widget<QLineEdit>(dialog, "adjust_gamma"), "3.125");
                QCOMPARE(window.session().document().bytes(), original);
                auto buttons = dialog.findChild<QDialogButtonBox *>();
                QVERIFY(buttons);
                QTest::mouseClick(buttons->button(QDialogButtonBox::Cancel), Qt::LeftButton);
            }));
        QCOMPARE(window.session().document().bytes(), original);
        QCOMPARE(window.session().undoStack().index(), 0);
        QVERIFY(
            throughAction<AdjustmentDialog>(window, "actionAdjustments", [&](AdjustmentDialog &dialog) {
                enterDialogField(widget<QLineEdit>(dialog, "adjust_gamma"), "3.125");
                enterDialogField(widget<QLineEdit>(dialog, "adjust_brightness"), "1.75");
                QTest::mouseClick(widget<QPushButton>(dialog, "acceptAdjustments"), Qt::LeftButton);
            }));
        const auto adjusted = window.session().document().render_model(0);
        QCOMPARE(adjusted.gamma, 3.125);
        QCOMPARE(adjusted.brightness, 1.75);
        QCOMPARE(window.session().undoStack().index(), 1);
        QCOMPARE(window.session().undoStack().count(), 1);
        widget<QAction>(window, "actionUndo")->trigger();
        QCOMPARE(window.session().document().bytes(), original);
        QVERIFY(!window.previewController().isPaused());
    }
    void randomBatchAndFlameCommandsKeepSelectionHistory() {
        MainWindow window(false);
        fastPreview(window);
        window.openExample();
        window.show();
        window.session().selectTransform(1);
        const auto original = window.session().document().bytes();
        const auto firstEntry = std::string(window.session().document().element_bytes(0));
        std::vector<std::string> generatedNames;
        QVERIFY(throughAction<ExploreDialog>(window, "actionRandomFlames", [&](ExploreDialog &dialog) {
            widget<QSpinBox>(dialog, "exploreCount")->setValue(2);
            auto addBatch = widget<QPushButton>(dialog, "addAllCandidates");
            QTRY_VERIFY_WITH_TIMEOUT(addBatch->isEnabled(), 10000);
            if (dialog.candidates().size() != 2) {
                QTest::mouseClick(widget<QPushButton>(dialog, "generateCandidates"), Qt::LeftButton);
                QTRY_VERIFY_WITH_TIMEOUT(addBatch->isEnabled() && dialog.candidates().size() == 2,
                                         10000);
            }
            for (const auto &flame : dialog.candidates())
                generatedNames.push_back(flame.name);
            QTest::mouseClick(addBatch, Qt::LeftButton);
        }));
        QCOMPARE(generatedNames.size(), std::size_t(2));
        QCOMPARE(window.session().document().entries().size(), std::size_t(3));
        QCOMPARE(window.session().selectedFlame(), 1);
        QCOMPARE(window.session().selectedTransform(), 0);
        QCOMPARE(std::string(window.session().document().element_bytes(0)), firstEntry);
        QCOMPARE(window.session().document().entries()[1].name, generatedNames[0]);
        QCOMPARE(window.session().document().entries()[2].name, generatedNames[1]);
        QCOMPARE(window.session().undoStack().count(), 1);
        const auto added = window.session().document().bytes();
        widget<QAction>(window, "actionUndo")->trigger();
        QCOMPARE(window.session().document().bytes(), original);
        QCOMPARE(window.session().selectedFlame(), 0);
        QCOMPARE(window.session().selectedTransform(), 1);
        widget<QAction>(window, "actionRedo")->trigger();
        QCOMPARE(window.session().document().bytes(), added);
        QCOMPARE(window.session().selectedFlame(), 1);

        widget<QAction>(window, "actionDuplicateFlame")->trigger();
        QCOMPARE(window.session().document().entries().size(), std::size_t(4));
        QCOMPARE(window.session().selectedFlame(), 3);
        QCOMPARE(window.session().document().entries()[3].name, generatedNames[0] + " copy");
        widget<QAction>(window, "actionUndo")->trigger();
        QCOMPARE(window.session().document().bytes(), added);
        QCOMPARE(window.session().selectedFlame(), 1);
        widget<QAction>(window, "actionDeleteFlame")->trigger();
        QCOMPARE(window.session().document().entries().size(), std::size_t(2));
        QCOMPARE(window.session().selectedFlame(), 1);
        QCOMPARE(window.session().document().entries()[1].name, generatedNames[1]);
        widget<QAction>(window, "actionUndo")->trigger();
        QCOMPARE(window.session().document().bytes(), added);
        widget<QAction>(window, "actionUndo")->trigger();
        QCOMPARE(window.session().document().bytes(), original);
        QCOMPARE(window.session().selectedTransform(), 1);
    }
    void mutationApplyAndBranchEntriesPreserveSource() {
        MainWindow window(false);
        fastPreview(window);
        window.openExample();
        auto annotated = window.session().document().bytes();
        const std::string history =
            "<!-- creative provenance --><edit><flame name='Historical parent'/></edit>";
        annotated.insert(annotated.rfind("</flame>"), history);
        window.session().loadBytes(annotated);
        window.show();
        const auto name = window.session().document().entries()[0].name;
        const auto originalEntry = std::string(window.session().document().element_bytes(0));
        Affine chosen;
        QVERIFY(throughAction<ExploreDialog>(window, "actionMutations", [&](ExploreDialog &dialog) {
            auto apply = widget<QPushButton>(dialog, "applyMutation");
            QTRY_VERIFY_WITH_TIMEOUT(apply->isEnabled(), 10000);
            chosen = dialog.candidates().at(dialog.selectedIndex()).transforms[0].pre;
            QTest::mouseClick(apply, Qt::LeftButton);
        }));
        QCOMPARE(window.session().document().entries()[0].name, name);
        QCOMPARE(window.session().document().render_model(0).transforms[0].pre.a, chosen.a);
        QCOMPARE(window.session().document().render_model(0).transforms[0].pre.e, chosen.e);
        QVERIFY(window.session().document().bytes().find(history) != std::string::npos);
        QCOMPARE(window.session().undoStack().index(), 1);
        widget<QAction>(window, "actionUndo")->trigger();
        QCOMPARE(window.session().document().bytes(), annotated);

        std::string branchName, descendantName;
        QVERIFY(throughAction<ExploreDialog>(window, "actionMutations", [&](ExploreDialog &dialog) {
            auto useParent = widget<QPushButton>(dialog, "useCandidateAsParent");
            QTRY_VERIFY_WITH_TIMEOUT(useParent->isEnabled(), 10000);
            branchName = dialog.candidates().at(dialog.selectedIndex()).name;
            QTest::mouseClick(useParent, Qt::LeftButton);
            auto keepBranch = widget<QPushButton>(dialog, "addMutationBranch");
            QTRY_VERIFY_WITH_TIMEOUT(keepBranch->isEnabled() && dialog.branchCount() == 2, 10000);
            descendantName = dialog.candidates().at(dialog.selectedIndex()).name;
            QTest::mouseClick(keepBranch, Qt::LeftButton);
        }));
        QCOMPARE(window.session().document().entries().size(), std::size_t(3));
        QCOMPARE(window.session().selectedFlame(), 1);
        QCOMPARE(window.session().document().entries()[1].name, branchName);
        QCOMPARE(window.session().document().entries()[2].name, descendantName);
        QCOMPARE(std::string(window.session().document().element_bytes(0)), originalEntry);
        for (std::size_t i = 1; i < 3; ++i) {
            QVERIFY(!window.session().document().render_issue(i));
            QVERIFY(window.session().document().element_bytes(i).find(history) != std::string::npos);
        }
        QCOMPARE(window.session().undoStack().index(), 1);
        QCOMPARE(window.session().undoStack().count(), 1);
        const auto kept = window.session().document().bytes();
        widget<QAction>(window, "actionUndo")->trigger();
        QCOMPARE(window.session().document().bytes(), annotated);
        QCOMPARE(window.session().selectedFlame(), 0);
        widget<QAction>(window, "actionRedo")->trigger();
        QCOMPARE(window.session().document().bytes(), kept);
        QCOMPARE(window.session().selectedFlame(), 1);
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const auto path = folder.filePath("mutation branches.flame");
        window.session().save(path);
        QVERIFY(window.openFile(path));
        QCOMPARE(window.session().document().bytes(), kept);
        QCOMPARE(window.session().document().entries()[2].name, descendantName);
    }
};
QTEST_MAIN(EditorTests)
#include "editor_tests.moc"
