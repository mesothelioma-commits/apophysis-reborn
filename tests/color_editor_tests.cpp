#include "editor/color_curve_dialog.hpp"
#include "editor/animation_dialog.hpp"
#include "editor/creative_widgets.hpp"
#include "editor/document_session.hpp"
#include "editor/render_queue.hpp"
#include "flame_io.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <cmath>

using namespace apo;
using namespace apo::editor;
namespace {
template<class T> T* control(QObject& parent, const char* name) {
    auto result = parent.findChild<T*>(QString::fromLatin1(name));
    if (!result) qFatal("Missing color editor control: %s", name);
    return result;
}
void click(QObject& parent, const char* name) { control<QPushButton>(parent, name)->click(); }
ColorCurveDialog* activeCurveDialog() {
    auto result = qobject_cast<ColorCurveDialog*>(QApplication::activeModalWidget());
    if (!result) qFatal("Expected the modal color curve dialog");
    return result;
}
void show(QDialog& dialog) {
    dialog.show();
    if (!QTest::qWaitForWindowExposed(&dialog)) qFatal("Color editor window was not exposed");
    QCoreApplication::processEvents();
}
Flame fixture() {
    auto flame = demo_flame();
    flame.width = 96;
    flame.height = 72;
    flame.scale = 24;
    flame.palette[0] = {1, 0, 0};
    return flame;
}
PreviewSettings quick() {
    PreviewSettings result;
    result.sample_limit = 1000;
    result.threads = 1;
    result.debounce_ms = 1;
    return result;
}
ColorCurves modern() {
    ColorCurves curves;
    curves.mode = ColorCurveMode::continuous;
    curves.channels[0][1] = {.12345678901234567, .34567890123456789, .87654321098765432};
    return curves;
}
} // namespace

class ColorEditorTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
    void exactNoOpAndValidation() {
        const auto original = modern();
        ColorCurveDialog dialog(original);
        show(dialog);
        QVERIFY(dialog.resultCurves() == original);
        auto field = control<QLineEdit>(dialog, "curvePoint_1_1");
        field->setText("not a number");
        QVERIFY(!control<QPushButton>(dialog, "acceptCurves")->isEnabled());
        QVERIFY(dialog.resultCurves() == original);
        click(dialog, "restoreOriginalCurves");
        QVERIFY(control<QPushButton>(dialog, "acceptCurves")->isEnabled());
        QVERIFY(dialog.resultCurves() == original);
        control<QComboBox>(dialog, "curveChannel")->setCurrentIndex(2);
        control<QLineEdit>(dialog, "curvePoint_0_2")->setText("0");
        QVERIFY(!control<QPushButton>(dialog, "acceptCurves")->isEnabled());
        click(dialog, "restoreOriginalCurves");
        click(dialog, "acceptCurves");
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QVERIFY(dialog.resultCurves() == original);
    }
    void curveHandlesPresetsAndReset() {
        ColorCurves original;
        original.mode = ColorCurveMode::continuous;
        ColorCurveDialog dialog(original);
        show(dialog);
        auto plot = control<CurvePlot>(dialog, "colorCurvePlot");
        QSignalSpy edits(plot, &CurvePlot::pointEdited);
        const auto from = plot->pointPosition(1).toPoint();
        const auto to = from + QPoint(65, -50);
        QTest::mousePress(plot, Qt::LeftButton, {}, from);
        QTest::mouseMove(plot, to);
        QTest::mouseRelease(plot, Qt::LeftButton, {}, to);
        QVERIFY(edits.size() >= 2);
        QVERIFY(dialog.resultCurves().channels[0][1].x > 0);
        QVERIFY(dialog.resultCurves().channels[0][1].y > 0);
        QVERIFY(original.channels[0][1].y == 0);
        click(dialog, "resetCurveChannel");
        QVERIFY(dialog.resultCurves() == original);
        control<QComboBox>(dialog, "curvePreset")->setCurrentIndex(4);
        click(dialog, "useCurvePreset");
        QVERIFY(dialog.resultCurves().channels[1] != identity_color_curve());
        QVERIFY(dialog.resultCurves().channels[3] != identity_color_curve());
        click(dialog, "resetAllCurves");
        QVERIFY(dialog.resultCurves() == original);
        dialog.reject();
    }
    void gradientHslAndLinearLight() {
        const auto original = fixture();
        GradientDialog dialog(original, nullptr, quick());
        show(dialog);
        control<QSpinBox>(dialog, "paletteLightness")->setValue(25);
        QVERIFY(std::abs(dialog.resultFlame().palette[0].r - .5) < .0001);
        QVERIFY(dialog.resultFlame().palette[0].g == 0);
        QVERIFY(dialog.resultFlame().palette[0].b == 0);
        QCOMPARE(original.palette[0].r, 1.0);
        auto stops = control<QTableWidget>(dialog, "paletteStops");
        stops->setRowCount(2);
        for (int row = 0; row < 2; ++row)
            for (int column = 0; column < 4; ++column)
                stops->setItem(row, column, new QTableWidgetItem(QString::number(row ? 255 : 0)));
        auto space = control<QComboBox>(dialog, "gradientColorSpace");
        QCOMPARE(space->count(), 4);
        space->setCurrentIndex(2);
        click(dialog, "buildPaletteStops");
        const auto expected = creative::blend_colors({}, {1, 1, 1}, 128. / 255,
                                                     creative::GradientSpace::linear_rgb);
        QVERIFY(std::abs(dialog.resultFlame().palette[128].r - expected.r) < 1e-14);
        QVERIFY(dialog.resultFlame().curves == original.curves);
        dialog.reject();
    }
    void nestedPaletteCurveCancelAndApply() {
        const auto original = fixture();
        GradientDialog dialog(original, nullptr, quick());
        show(dialog);
        bool opened = false;
        QTimer::singleShot(0, &dialog, [&] {
            auto child = activeCurveDialog();
            opened = true;
            control<QComboBox>(*child, "curvePreset")->setCurrentIndex(2);
            click(*child, "useCurvePreset");
            child->reject();
        });
        click(dialog, "editPaletteCurves");
        QVERIFY(opened);
        QCOMPARE(serialize_flame(dialog.resultFlame()), serialize_flame(original));
        QTimer::singleShot(0, &dialog, [&] {
            auto child = activeCurveDialog();
            control<QComboBox>(*child, "curvePreset")->setCurrentIndex(2);
            click(*child, "useCurvePreset");
            click(*child, "acceptCurves");
        });
        click(dialog, "editPaletteCurves");
        QVERIFY(serialize_flame(dialog.resultFlame()) != serialize_flame(original));
        QVERIFY(dialog.resultFlame().curves == original.curves);
        click(dialog, "undoPalette");
        QCOMPARE(serialize_flame(dialog.resultFlame()), serialize_flame(original));
        dialog.reject();
    }
    void toneCurvesAreOneSourcePreservingUndo() {
        const auto original = fixture();
        auto bytes = serialize_flame(original);
        bytes.insert(bytes.find("<xform"), "<!-- keep original curve provenance -->");
        DocumentSession session;
        session.loadBytes(bytes);
        AdjustmentDialog dialog(session.document().render_model(0), nullptr, quick());
        show(dialog);
        QTimer::singleShot(0, &dialog, [&] {
            auto child = activeCurveDialog();
            control<QComboBox>(*child, "curvePreset")->setCurrentIndex(2);
            click(*child, "useCurvePreset");
            child->reject();
        });
        click(dialog, "editToneCurves");
        QVERIFY(dialog.resultFlame().curves == original.curves);
        QTimer::singleShot(0, &dialog, [&] {
            auto child = activeCurveDialog();
            control<QComboBox>(*child, "curvePreset")->setCurrentIndex(1);
            click(*child, "useCurvePreset");
            click(*child, "acceptCurves");
        });
        click(dialog, "editToneCurves");
        QVERIFY(dialog.resultFlame().curves != original.curves);
        QCOMPARE(session.document().bytes(), bytes);
        QCOMPARE(session.undoStack().count(), 0);
        click(dialog, "acceptAdjustments");
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        const auto changed = dialog.resultFlame();
        const auto edits = dialog.edits();
        QVERIFY(edits.curves.has_value());
        session.change("Tone curves", [&](LegacyDocument& document) { document.edit(0, edits); });
        QCOMPARE(session.undoStack().count(), 1);
        QVERIFY(session.document().render_model(0).curves == changed.curves);
        QVERIFY(session.document().bytes().find("<!-- keep original curve provenance -->") != std::string::npos);
        session.undoStack().undo();
        QCOMPARE(session.document().bytes(), bytes);
        session.undoStack().redo();
        QVERIFY(session.document().render_model(0).curves == changed.curves);
        auto direct = LegacyDocument::parse(bytes);
        applyCreativeFlame(direct, 0, changed);
        QVERIFY(direct.render_model(0).curves == changed.curves);
        session.discardRecovery();
    }
    void queuedCurvesAreImmutableAndPersisted() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        RenderQueue queue({}, folder.filePath("checkpoints"));
        auto flame = fixture();
        flame.curves = modern();
        const auto curves = flame.curves;
        RenderOptions render;
        render.samples = 1000;
        render.memory_mib = 32;
        FileRenderOptions file;
        file.disk_mib = 32;
        const auto id = queue.add(flame, render, file, folder.filePath("curved.png"));
        flame.curves = {};
        QVERIFY(queue.job(id)->flame.curves == curves);
        const auto saved = folder.filePath("queue.json");
        queue.saveTo(saved);
        RenderQueue restored({}, folder.filePath("restored"));
        restored.loadFrom(saved);
        QVERIFY(restored.job(id));
        QVERIFY(restored.job(id)->flame.curves == curves);
        QVERIFY(!restored.isRunning());
        queue.disableAutoSave();
        restored.disableAutoSave();
    }
    void timelineAdjustmentsRetainGammaAndCurves() {
        auto flame = fixture();
        flame.curves = modern();
        auto bytes = serialize_flame(flame);
        bytes.insert(bytes.find("<xform"), "<!-- preserve keyframe metadata -->");
        const auto source = LegacyDocument::parse(bytes);
        AnimationDialog timeline(source, 0, {}, nullptr, quick());
        show(timeline);
        const auto original = timeline.clip();
        QTimer::singleShot(0, &timeline, [&] {
            auto adjustment = qobject_cast<AdjustmentDialog*>(QApplication::activeModalWidget());
            if (!adjustment) qFatal("Expected timeline Adjustment dialog");
            click(*adjustment, "acceptAdjustments");
        });
        click(timeline, "timelineAdjustments");
        QVERIFY(timeline.clip() == original);
        QTimer::singleShot(0, &timeline, [&] {
            auto adjustment = qobject_cast<AdjustmentDialog*>(QApplication::activeModalWidget());
            if (!adjustment) qFatal("Expected timeline Adjustment dialog");
            control<QLineEdit>(*adjustment, "adjust_gamma")->setText("3.125");
            QTimer::singleShot(0, adjustment, [&] {
                auto curves = activeCurveDialog();
                control<QComboBox>(*curves, "curveChannel")->setCurrentIndex(1);
                control<QLineEdit>(*curves, "curvePoint_1_0")->setText("0.25");
                control<QLineEdit>(*curves, "curvePoint_1_1")->setText("0.6");
                click(*curves, "acceptCurves");
            });
            click(*adjustment, "editToneCurves");
            click(*adjustment, "acceptAdjustments");
        });
        click(timeline, "timelineAdjustments");
        const auto changed = timeline.clip();
        const auto& key = changed.keyframes.at(0);
        const auto model = LegacyDocument::parse(key.library_bytes).render_model(key.selected_index);
        QCOMPARE(model.gamma, 3.125);
        QCOMPARE(model.curves.channels[1][1].x, .25);
        QCOMPARE(model.curves.channels[1][1].y, .6);
        QVERIFY(model.curves.channels[0] == flame.curves.channels[0]);
        QVERIFY(key.library_bytes.find("<!-- preserve keyframe metadata -->") != std::string::npos);
        for (std::size_t i = 1; i < changed.keyframes.size(); ++i)
            QCOMPARE(changed.keyframes[i].library_bytes, original.keyframes[i].library_bytes);
        QCOMPARE(source.bytes(), bytes);
        click(timeline, "timelineUndo");
        QVERIFY(timeline.clip() == original);
        click(timeline, "timelineRedo");
        QVERIFY(timeline.clip() == changed);
        timeline.done(QDialog::Rejected);
    }
};

QTEST_MAIN(ColorEditorTests)
#include "color_editor_tests.moc"
