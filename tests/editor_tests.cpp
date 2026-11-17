#include "editor/creative_widgets.hpp"
#include "editor/main_window.hpp"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
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
                QVERIFY(widget<QLabel>(dialog, "computeDetails")->text().contains("unavailable"));
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
