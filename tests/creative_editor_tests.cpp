#include "editor/creative_widgets.hpp"
#include "editor/document_session.hpp"
#include "flame_io.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <memory>

using namespace apo;
using namespace apo::editor;

namespace {
template <class T> T *control(QObject &owner, const char *name) {
    auto result = owner.findChild<T *>(QString::fromLatin1(name));
    if (!result) qFatal("Missing creative editor control %s", name);
    return result;
}
void click(QObject &owner, const char *name) {
    QTest::mouseClick(control<QPushButton>(owner, name), Qt::LeftButton);
}
void enter(QLineEdit *field, const QString &value) {
    field->window()->activateWindow();
    field->setFocus();
    QCoreApplication::processEvents();
    field->selectAll();
    QTest::keyClicks(field, value);
    QTest::keyClick(field, Qt::Key_Tab);
    QCoreApplication::processEvents();
}
void enter(QSpinBox *field, const QString &value) {
    field->window()->activateWindow();
    field->setFocus();
    QCoreApplication::processEvents();
    field->selectAll();
    QTest::keyClicks(field, value);
    QTest::keyClick(field, Qt::Key_Tab);
    QCoreApplication::processEvents();
}
void showDialog(QDialog &dialog) {
    dialog.show();
    if (!QTest::qWaitForWindowExposed(&dialog)) qFatal("Creative editor dialog was not exposed");
    dialog.activateWindow();
    QCoreApplication::processEvents();
}
void quickPreviews(QObject &owner) {
    for (auto preview : owner.findChildren<PreviewController *>()) {
        auto settings = preview->settings();
        settings.sample_limit = 20000;
        settings.threads = 1;
        settings.debounce_ms = 1;
        preview->setSettings(settings);
    }
}
bool samePalette(const Palette &left, const Palette &right, double tolerance = 0) {
    return std::equal(left.begin(), left.end(), right.begin(), [&](RGB a, RGB b) {
        return std::abs(a.r - b.r) <= tolerance && std::abs(a.g - b.g) <= tolerance &&
               std::abs(a.b - b.b) <= tolerance;
    });
}
Flame sample() {
    auto result = demo_flame();
    result.width = 160;
    result.height = 120;
    result.scale = 40;
    result.palette = default_palette();
    result.palette[11] = {.123456789012345, .314159265358979, .876543210987654};
    result.background = {.1, .2, .3};
    return result;
}
std::string source() {
    return "<?xml version='1.0' encoding='ISO-8859-1'?>\r\n"
           "<Flames name='Creative library'>\r\n<!-- caf\xe9 provenance -->\r\n"
           "<flame name='Editable' size='160 120' scale='40' gamma='2.2' zoom='0' rotate='0'>"
           "<xform name='retain metadata' linear='1' weight='1' color='.2' coefs='.5 .2 -.2 .5 -.4 0'/>"
           "<xform linear='1' weight='1' color='.8' coefs='.5 -.2 .2 .5 .4 .2'/>"
           "<!-- original color note --><color index='0' rgb='255 64 0'/>"
           "<color index='128' rgb='0 64 255'/>"
           "<edit><vendor stamp='keep &amp; remember'/></edit></flame>\r\n"
           "<flame name='Unsupported sibling' vendor='unchanged'><unknown payload='keep'/></flame>\r\n"
           "</Flames>\r\n";
}
void writeFile(const QString &path, const QByteArray &contents) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size())
        throw std::runtime_error("Cannot write creative editor test file");
}
} // namespace

class CreativeEditorTests : public QObject {
    Q_OBJECT
  private:
    QString dataDirectory_;
  private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        QCoreApplication::setOrganizationName("Apophysis Reborn Tests");
        QCoreApplication::setApplicationName("creative-" + QUuid::createUuid().toString(QUuid::WithoutBraces));
        dataDirectory_ = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    }
    void cleanupTestCase() { QDir(dataDirectory_).removeRecursively(); }

    void gradientReverseUndoAndIndividualColor() {
        const auto original = sample();
        const auto originalBytes = serialize_flame(original);
        GradientDialog dialog(original);
        quickPreviews(dialog);
        showDialog(dialog);
        QVERIFY(dialog.isVisible());
        click(dialog, "reversePalette");
        auto reversed = original.palette;
        std::reverse(reversed.begin(), reversed.end());
        QVERIFY(samePalette(dialog.resultFlame().palette, reversed));
        click(dialog, "undoPalette");
        QVERIFY(samePalette(dialog.resultFlame().palette, original.palette));
        click(dialog, "redoPalette");
        QVERIFY(samePalette(dialog.resultFlame().palette, reversed));
        click(dialog, "resetPalette");
        QVERIFY(samePalette(dialog.resultFlame().palette, original.palette));

        enter(control<QSpinBox>(dialog, "paletteIndex"), "37");
        enter(control<QSpinBox>(dialog, "paletteRed"), "91");
        enter(control<QSpinBox>(dialog, "paletteGreen"), "57");
        enter(control<QSpinBox>(dialog, "paletteBlue"), "203");
        auto expected = original.palette;
        expected[37] = {91 / 255.0, 57 / 255.0, 203 / 255.0};
        QVERIFY(samePalette(dialog.resultFlame().palette, expected));
        enter(control<QSpinBox>(dialog, "paletteIndex"), "11");
        enter(control<QSpinBox>(dialog, "paletteRed"), "17");
        QCOMPARE(dialog.resultFlame().palette[11].r, 17 / 255.0);
        QCOMPARE(dialog.resultFlame().palette[11].g, original.palette[11].g);
        QCOMPARE(dialog.resultFlame().palette[11].b, original.palette[11].b);
        click(dialog, "undoPalette");
        QVERIFY(samePalette(dialog.resultFlame().palette, expected));
        QCOMPARE(serialize_flame(original), originalBytes);
        dialog.reject();
        QCOMPARE(dialog.result(), int(QDialog::Rejected));
    }

    void gradientStopsBuildAndInvalidRollback() {
        const auto original = sample();
        GradientDialog dialog(original);
        quickPreviews(dialog);
        showDialog(dialog);
        control<QTabWidget>(dialog, "gradientTabs")->setCurrentIndex(1);
        auto table = control<QTableWidget>(dialog, "paletteStops");
        table->setRowCount(3);
        const int values[3][4] = {{0, 0, 0, 0}, {128, 255, 128, 0}, {255, 0, 0, 255}};
        for (int row = 0; row < 3; ++row)
            for (int column = 0; column < 4; ++column)
                table->setItem(row, column, new QTableWidgetItem(QString::number(values[row][column])));
        click(dialog, "buildPaletteStops");
        const auto built = dialog.resultFlame().palette;
        QCOMPARE(built[0].r, 0.);
        QCOMPARE(built[64].r, .5);
        QCOMPARE(built[64].g, 64 / 255.0);
        QCOMPARE(built[128].r, 1.);
        QCOMPARE(built[255].b, 1.);
        table->item(1, 0)->setText("0");
        click(dialog, "buildPaletteStops");
        QVERIFY(samePalette(dialog.resultFlame().palette, built));
        QVERIFY(!control<QLabel>(dialog, "gradientStatus")->text().isEmpty());
        click(dialog, "undoPalette");
        QVERIFY(samePalette(dialog.resultFlame().palette, original.palette));
        dialog.reject();
    }

    void paletteExportImportAndFailedImport() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const auto original = sample();
        GradientDialog exporter(original);
        quickPreviews(exporter);
        const auto path = folder.filePath(QString::fromUtf8("palette caf\xc3\xa9 with spaces.gpl"));
        exporter.exportPaletteFile(path);
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.readAll().startsWith("GIMP Palette\n"));
        GradientDialog importer(original);
        quickPreviews(importer);
        importer.importPaletteFile(path);
        auto quantized = original.palette;
        for (auto &entry : quantized)
            entry = {std::lround(entry.r * 255) / 255., std::lround(entry.g * 255) / 255.,
                     std::lround(entry.b * 255) / 255.};
        QVERIFY(samePalette(importer.resultFlame().palette, quantized));
        const auto invalid = folder.filePath("invalid.gpl");
        writeFile(invalid, "GIMP Palette\n300 0 0\n0 0 0\n");
        QVERIFY_EXCEPTION_THROWN(importer.importPaletteFile(invalid), std::invalid_argument);
        QVERIFY(samePalette(importer.resultFlame().palette, quantized));
        const auto flamePath = folder.filePath("one flame.flame");
        writeFile(flamePath, QByteArray::fromStdString(serialize_flame(original)));
        importer.importPaletteFile(flamePath);
        QVERIFY(samePalette(importer.resultFlame().palette, original.palette, 1e-14));
    }

    void legacyPaletteFormatsAndReports() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const auto original = sample();
        GradientDialog exporter(original);
        quickPreviews(exporter);
        GradientDialog importer(original);
        quickPreviews(importer);
        auto quantized = original.palette;
        for (auto &entry : quantized)
            entry = {std::lround(entry.r * 255) / 255., std::lround(entry.g * 255) / 255.,
                     std::lround(entry.b * 255) / 255.};
        for (const auto &suffix : {QString("ugr"), QString("map")}) {
            const auto path = folder.filePath("palette." + suffix);
            exporter.exportPaletteFile(path);
            importer.importPaletteFile(path);
            QVERIFY(samePalette(importer.resultFlame().palette, quantized));
        }
        const auto invalid = folder.filePath("unsupported.ugr");
        writeFile(invalid, "Unsupported { gradient: smooth=yes index=0 color=0 }\n");
        QVERIFY_EXCEPTION_THROWN(importer.importPaletteFile(invalid), std::invalid_argument);
        QVERIFY(samePalette(importer.resultFlame().palette, quantized));
        const auto wrongSuffix = folder.filePath("palette.unknown");
        QVERIFY_EXCEPTION_THROWN(exporter.exportPaletteFile(wrongSuffix), std::invalid_argument);
        QVERIFY(!QFile::exists(wrongSuffix));
        const auto multiple = folder.filePath("collection.ugr");
        writeFile(multiple, "Red { gradient: title=\"Red\" index=0 color=255 }\n"
                            "Green { gradient: title=\"Green\" index=0 color=65280 }\n"
                            "Unsupported { gradient: smooth=yes index=0 color=0 }\n");
        bool chose = false;
        QTimer::singleShot(30, [&] {
            if (auto dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget())) {
                dialog->setTextValue("2 · Green");
                chose = true;
                dialog->accept();
            }
        });
        importer.importPaletteFile(multiple);
        QVERIFY(chose);
        for (const auto &entry : importer.resultFlame().palette) {
            QCOMPARE(entry.r, 0.);
            QCOMPARE(entry.g, 1.);
            QCOMPARE(entry.b, 0.);
        }
    }

    void adjustmentsValidateResetAndAccept() {
        const auto original = sample();
        const auto originalBytes = serialize_flame(original);
        AdjustmentDialog dialog(original);
        quickPreviews(dialog);
        showDialog(dialog);
        auto tabs = control<QTabWidget>(dialog, "adjustmentTabs");
        auto gamma = control<QLineEdit>(dialog, "adjust_gamma");
        auto apply = control<QPushButton>(dialog, "acceptAdjustments");
        QCOMPARE(gamma->text().toDouble(), original.gamma);
        QCOMPARE(control<QLineEdit>(dialog, "adjust_zoom")->text().toDouble(), original.zoom);
        QCOMPARE(control<QLineEdit>(dialog, "adjust_rotate")->text().toDouble(), original.rotate);
        QCOMPARE(control<QPushButton>(dialog, "adjust_background")->text(), QColor::fromRgbF(.1, .2, .3).name());
        QSignalSpy accepted(&dialog, &QDialog::accepted);
        tabs->setCurrentIndex(1);
        enter(gamma, "0");
        QVERIFY(!apply->isEnabled());
        QTest::mouseClick(apply, Qt::LeftButton);
        QCOMPARE(accepted.count(), 0);
        QCOMPARE(dialog.resultFlame().gamma, original.gamma);
        QCOMPARE(serialize_flame(original), originalBytes);
        enter(gamma, "1.6");
        QVERIFY(apply->isEnabled());
        QCOMPARE(dialog.resultFlame().gamma, 1.6);
        tabs->setCurrentIndex(0);
        enter(control<QLineEdit>(dialog, "adjust_zoom"), "1.25");
        enter(control<QLineEdit>(dialog, "adjust_rotate"), "37.5");
        QCOMPARE(dialog.resultFlame().zoom, 1.25);
        QCOMPARE(dialog.resultFlame().rotate, 37.5);
        tabs->setCurrentIndex(1);
        bool picked = false;
        QTimer::singleShot(0, &dialog, [&] {
            if (auto picker = dialog.findChild<QColorDialog *>()) {
                picker->setCurrentColor(QColor(17, 34, 51));
                picked = true;
                picker->accept();
            }
        });
        click(dialog, "adjust_background");
        QVERIFY(picked);
        QVERIFY(std::abs(dialog.resultFlame().background.r - 17 / 255.0) < 1e-6);
        QVERIFY(std::abs(dialog.resultFlame().background.g - 34 / 255.0) < 1e-6);
        QVERIFY(dialog.edits().gamma.has_value());
        QVERIFY(dialog.edits().background.has_value());
        click(dialog, "resetAdjustments");
        QCOMPARE(serialize_flame(dialog.resultFlame()), originalBytes);
        QVERIFY(!dialog.edits().gamma.has_value());
        QVERIFY(!dialog.edits().zoom.has_value());
        QVERIFY(!dialog.edits().rotate.has_value());
        QVERIFY(!dialog.edits().background.has_value());
        enter(gamma, "1.8");
        QTest::mouseClick(apply, Qt::LeftButton);
        QCOMPARE(accepted.count(), 1);
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(dialog.resultFlame().gamma, 1.8);
    }

    void adjustmentAspectAndSourceFilterDefault() {
        auto original = sample();
        original.filter_radius = .8;
        AdjustmentDialog dialog(original);
        quickPreviews(dialog);
        showDialog(dialog);
        auto tabs = control<QTabWidget>(dialog, "adjustmentTabs");
        control<QCheckBox>(dialog, "adjust_aspect")->setChecked(true);
        enter(control<QSpinBox>(dialog, "adjust_width"), "320");
        QCOMPARE(dialog.resultFlame().width, 320);
        QCOMPARE(dialog.resultFlame().height, 240);
        tabs->setCurrentIndex(2);
        control<QComboBox>(dialog, "adjust_filter")->setCurrentIndex(0);
        QVERIFY(!dialog.resultFlame().filter_radius.has_value());
        QVERIFY(dialog.edits().clear_filter_radius);
        QVERIFY(!control<QLineEdit>(dialog, "adjust_filter_radius")->isEnabled());
        click(dialog, "resetAdjustments");
        QCOMPARE(dialog.resultFlame().filter_radius.value(), .8);
        original.source_version = "Apophysis 2.09";
        AdjustmentDialog historical(original);
        quickPreviews(historical);
        control<QComboBox>(historical, "adjust_filter")->setCurrentIndex(0);
        QCOMPARE(historical.resultFlame().filter_radius.value(), .5);
        QVERIFY(historical.edits().clear_filter_radius);
    }

    void applyOneUndoPreservesSourceAndSibling() {
        DocumentSession session;
        const auto original = source();
        session.loadBytes(original);
        const auto sibling = std::string(session.document().element_bytes(1));
        const auto model = session.document().render_model(0);
        GradientDialog gradient(model);
        quickPreviews(gradient);
        showDialog(gradient);
        click(gradient, "reversePalette");
        auto next = gradient.resultFlame();
        next.transforms[0].pre.e += .25;
        QSignalSpy changes(&session, &DocumentSession::changed);
        session.change("Apply creative edits", [&](LegacyDocument &document) {
            applyCreativeFlame(document, 0, next);
        });
        QCOMPARE(session.undoStack().count(), 1);
        QCOMPARE(changes.count(), 1);
        QVERIFY(session.isModified());
        const auto updated = session.document().render_model(0);
        QCOMPARE(updated.transforms[0].pre.e, next.transforms[0].pre.e);
        QCOMPARE(updated.gamma, model.gamma);
        QCOMPARE(updated.zoom, model.zoom);
        QVERIFY(samePalette(updated.palette, next.palette, 1e-14));
        QCOMPARE(std::string(session.document().element_bytes(1)), sibling);
        QVERIFY(session.document().bytes().find("<!-- caf\xe9 provenance -->\r\n") != std::string::npos);
        QVERIFY(session.document().bytes().find("<edit><vendor stamp='keep &amp; remember'/></edit>") != std::string::npos);
        QVERIFY(session.document().bytes().find("name='retain metadata'") != std::string::npos);
        const auto edited = session.document().bytes();
        session.undoStack().undo();
        QCOMPARE(session.document().bytes(), original);
        QVERIFY(!session.isModified());
        session.undoStack().redo();
        QCOMPARE(session.document().bytes(), edited);
        auto invalid = next;
        invalid.transforms.pop_back();
        QVERIFY_EXCEPTION_THROWN(session.change("Invalid topology", [&](LegacyDocument &document) {
            applyCreativeFlame(document, 0, invalid);
        }), std::exception);
        QCOMPARE(session.document().bytes(), edited);
        QCOMPARE(session.undoStack().count(), 1);
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const auto path = folder.filePath("creative edit with spaces.flame");
        session.save(path);
        DocumentSession reopened;
        reopened.load(path);
        QCOMPARE(reopened.document().bytes(), edited);
        gradient.reject();
    }

    void adjustmentApplicationIsOneUndo() {
        DocumentSession session;
        const auto original = source();
        session.loadBytes(original);
        const auto sibling = std::string(session.document().element_bytes(1));
        AdjustmentDialog dialog(session.document().render_model(0));
        quickPreviews(dialog);
        showDialog(dialog);
        auto tabs = control<QTabWidget>(dialog, "adjustmentTabs");
        tabs->setCurrentIndex(1);
        enter(control<QLineEdit>(dialog, "adjust_gamma"), "1.7");
        tabs->setCurrentIndex(0);
        enter(control<QLineEdit>(dialog, "adjust_zoom"), "0.5");
        enter(control<QLineEdit>(dialog, "adjust_rotate"), "-21");
        click(dialog, "acceptAdjustments");
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        session.change("Adjust flame", [&](LegacyDocument &document) { document.edit(0, dialog.edits()); });
        QCOMPARE(session.undoStack().count(), 1);
        const auto model = session.document().render_model(0);
        QCOMPARE(model.gamma, 1.7);
        QCOMPARE(model.zoom, .5);
        QCOMPARE(model.rotate, -21.);
        QCOMPARE(std::string(session.document().element_bytes(1)), sibling);
        session.undoStack().undo();
        QCOMPARE(session.document().bytes(), original);
        QVERIFY(!session.isModified());
    }

    void randomBatchSeedLockAndAppendUndo() {
        const auto original = sample();
        ExploreDialog dialog(ExploreDialog::Mode::random, original);
        quickPreviews(dialog);
        control<QSpinBox>(dialog, "exploreCount")->setValue(3);
        control<QLineEdit>(dialog, "exploreSeed")->setText("8589");
        dialog.resize(1280, 790);
        showDialog(dialog);
        auto generate = control<QPushButton>(dialog, "generateCandidates");
        QTRY_COMPARE_WITH_TIMEOUT(dialog.candidates().size(), std::size_t(3), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(generate->isEnabled(), 15000);
        std::vector<std::string> first;
        for (const auto &flame : dialog.candidates()) first.push_back(serialize_flame(flame));
        QTRY_VERIFY_WITH_TIMEOUT(!control<PreviewCanvas>(dialog, "candidatePreview")->image().isNull(), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(!control<QToolButton>(dialog, "chooseCandidate0")->icon().isNull(), 15000);
        for (int index = 0; index < 3; ++index) {
            const auto tile = dialog.findChild<QToolButton *>(QString("chooseCandidate%1").arg(index));
            QVERIFY(tile);
            // The first complete row must be visible without manually widening the candidate pane.
            QTRY_COMPARE(tile->visibleRegion().boundingRect().width(), tile->width());
        }
        QTest::mouseClick(generate, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(generate->isEnabled(), 15000);
        QCOMPARE(control<QLineEdit>(dialog, "exploreSeed")->text(), QString("8589"));
        for (std::size_t i = 0; i < first.size(); ++i)
            QCOMPARE(serialize_flame(dialog.candidates()[i]), first[i]);
        control<QCheckBox>(dialog, "lockExploreSeed")->setChecked(false);
        QTest::mouseClick(generate, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(generate->isEnabled(), 15000);
        QCOMPARE(control<QLineEdit>(dialog, "exploreSeed")->text(), QString("8590"));
        QVERIFY(serialize_flame(dialog.candidates()[0]) != first[0]);
        click(dialog, "addAllCandidates");
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(dialog.disposition(), ExploreDialog::Disposition::append);
        QCOMPARE(dialog.acceptedFlames().size(), std::size_t(3));

        DocumentSession session;
        const auto bytes = source();
        session.loadBytes(bytes);
        session.change("Add random batch", [&](LegacyDocument &document) {
            for (const auto &flame : dialog.acceptedFlames()) document.append_flame(flame);
        });
        QCOMPARE(session.document().entries().size(), std::size_t(5));
        QCOMPARE(session.undoStack().count(), 1);
        for (std::size_t i = 0; i < dialog.acceptedFlames().size(); ++i)
            QCOMPARE(session.document().entries()[i + 2].name, dialog.acceptedFlames()[i].name);
        session.undoStack().undo();
        QCOMPARE(session.document().bytes(), bytes);
    }

    void zeroStrengthMutationAppliesAsNoop() {
        DocumentSession session;
        const auto bytes = source();
        session.loadBytes(bytes);
        const auto original = session.document().render_model(0);
        ExploreDialog dialog(ExploreDialog::Mode::mutation, original);
        quickPreviews(dialog);
        control<QSpinBox>(dialog, "exploreCount")->setValue(1);
        control<QDoubleSpinBox>(dialog, "mutationStrength")->setValue(0);
        showDialog(dialog);
        QTRY_COMPARE_WITH_TIMEOUT(dialog.candidates().size(), std::size_t(1), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(control<QPushButton>(dialog, "generateCandidates")->isEnabled(), 15000);
        auto candidate = dialog.candidates()[0];
        QVERIFY(candidate.name != original.name);
        candidate.name = original.name;
        QCOMPARE(serialize_flame(candidate), serialize_flame(original));
        click(dialog, "applyMutation");
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(dialog.disposition(), ExploreDialog::Disposition::replace);
        QCOMPARE(dialog.acceptedFlames().size(), std::size_t(1));
        session.change("Apply unchanged mutation", [&](LegacyDocument &document) {
            applyCreativeFlame(document, 0, dialog.acceptedFlames().front());
        });
        QCOMPARE(session.undoStack().count(), 0);
        QCOMPARE(session.document().bytes(), bytes);
        QVERIFY(!session.isModified());
    }

    void mutationSelectionComparisonAndBranchHistory() {
        const auto original = sample();
        ExploreDialog dialog(ExploreDialog::Mode::mutation, original);
        quickPreviews(dialog);
        control<QSpinBox>(dialog, "exploreCount")->setValue(2);
        showDialog(dialog);
        auto generate = control<QPushButton>(dialog, "generateCandidates");
        auto history = control<QComboBox>(dialog, "mutationHistory");
        auto compare = control<QCheckBox>(dialog, "compareMutationParent");
        QTRY_COMPARE_WITH_TIMEOUT(dialog.candidates().size(), std::size_t(2), 15000);
        QTRY_VERIFY_WITH_TIMEOUT(generate->isEnabled(), 15000);
        QTest::mouseClick(control<QToolButton>(dialog, "chooseCandidate1"), Qt::LeftButton);
        QCOMPARE(dialog.selectedIndex(), 1);
        const auto promoted = serialize_flame(dialog.candidates()[1]);
        compare->setChecked(true);
        QCOMPARE(control<QLabel>(dialog, "selectedCandidateName")->text(), QString::fromStdString(original.name));
        compare->setChecked(false);
        click(dialog, "useCandidateAsParent");
        QCOMPARE(dialog.branchCount(), 2);
        QTRY_VERIFY_WITH_TIMEOUT(generate->isEnabled(), 15000);
        QCOMPARE(history->currentIndex(), 1);
        const auto child = serialize_flame(dialog.candidates()[0]);
        click(dialog, "useCandidateAsParent");
        QCOMPARE(dialog.branchCount(), 3);
        QTRY_VERIFY_WITH_TIMEOUT(generate->isEnabled(), 15000);
        QCOMPARE(history->currentIndex(), 2);
        history->setCurrentIndex(0);
        QTRY_VERIFY_WITH_TIMEOUT(generate->isEnabled(), 15000);
        compare->setChecked(true);
        QCOMPARE(control<QLabel>(dialog, "selectedCandidateName")->text(), QString::fromStdString(original.name));
        history->setCurrentIndex(1);
        QTRY_VERIFY_WITH_TIMEOUT(generate->isEnabled(), 15000);
        QCOMPARE(serialize_flame(dialog.candidates()[0]), child);
        click(dialog, "addMutationBranch");
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(dialog.disposition(), ExploreDialog::Disposition::append);
        QCOMPARE(dialog.acceptedFlames().size(), std::size_t(2));
        QCOMPARE(serialize_flame(dialog.acceptedFlames()[0]), promoted);
        QCOMPARE(serialize_flame(dialog.acceptedFlames()[1]), child);
    }

    void destructionWithPendingInputAndGeneration() {
        // QWidget teardown can commit a focused spin box after C++ members have
        // started destruction. These signals must already be disconnected.
        auto adjustment = std::make_unique<AdjustmentDialog>(sample());
        quickPreviews(*adjustment);
        showDialog(*adjustment);
        auto width = control<QSpinBox>(*adjustment, "adjust_width");
        width->setFocus();
        QCoreApplication::processEvents();
        width->selectAll();
        QTest::keyClicks(width, "320");
        QCOMPARE(width->text(), QString("320"));
        adjustment.reset();

        auto gradient = std::make_unique<GradientDialog>(sample());
        quickPreviews(*gradient);
        showDialog(*gradient);
        auto red = control<QSpinBox>(*gradient, "paletteRed");
        red->setFocus();
        QCoreApplication::processEvents();
        red->selectAll();
        QTest::keyClicks(red, "91");
        QCOMPARE(red->text(), QString("91"));
        gradient.reset();

        auto explorer = std::make_unique<ExploreDialog>(ExploreDialog::Mode::random, sample());
        quickPreviews(*explorer);
        control<QSpinBox>(*explorer, "exploreCount")->setValue(24);
        showDialog(*explorer);
        explorer.reset();
        QCoreApplication::processEvents();
    }
};

QTEST_MAIN(CreativeEditorTests)
#include "creative_editor_tests.moc"
