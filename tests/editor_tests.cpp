#include "editor/main_window.hpp"
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace apo;
using namespace apo::editor;

namespace {
template <class T> T *widget(MainWindow &window, const char *name) {
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
} // namespace

class EditorTests : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("ApophysisRebornTests");
        QCoreApplication::setApplicationName("EditorWorkflow");
        applyDarkTheme();
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
};
QTEST_MAIN(EditorTests)
#include "editor_tests.moc"
