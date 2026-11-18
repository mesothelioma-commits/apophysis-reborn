#include "editor/main_window.hpp"
#include "editor/script_dialog.hpp"
#include "build_info.hpp"
#include "flame_io.hpp"
#include "project_io.hpp"
#include <QAction>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

using namespace apo;
using namespace apo::editor;
namespace {
std::string fixture() {
  auto f = demo_flame();
  f.width = 80;
  f.height = 60;
  f.scale = 20;
  return serialize_flame(f);
}
void write(const QString &path, const std::string &bytes) {
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly) ||
      f.write(bytes.data(), qsizetype(bytes.size())) != qsizetype(bytes.size()))
    throw std::runtime_error("fixture write");
}
std::string read(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly))
    throw std::runtime_error("fixture read");
  return f.readAll().toStdString();
}
template <class T> T *widget(QObject &parent, const char *name) {
  auto w = parent.findChild<T *>(name);
  if (!w)
    qFatal("Missing %s", name);
  return w;
}
void fast(MainWindow &window) {
  auto p = window.previewController().settings();
  p.sample_limit = 1000;
  p.threads = 1;
  p.debounce_ms = 1;
  window.previewController().setSettings(p);
}
} // namespace
class ExtensionsEditorTests : public QObject {
  Q_OBJECT
private slots:
  void initTestCase() {
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName("ApophysisRebornTests");
    QCoreApplication::setApplicationName(
        "extensions-" + QUuid::createUuid().toString(QUuid::WithoutBraces));
  }
  void cleanupTestCase() {
    QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .removeRecursively();
  }
  void nativeProjectUndoConflictAndRecovery() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ProjectFile p;
    p.library_bytes = fixture();
    p.metadata["vendor"] = "unchanged";
    p.render_settings["outputSeed"] = "18446744073709551615";
    p.animation_data = "opaque future timeline";
    const auto path = dir.filePath("native.apoproject");
    write(path, serialize_project(p));
    DocumentSession session;
    session.load(path);
    QVERIFY(session.isProject());
    QVERIFY(!session.isModified());
    const auto original = session.document().bytes();
    session.change("Rename", [](auto &doc) {
      LegacyEdits e;
      e.name = "Script edit";
      doc.edit(0, e);
    });
    QVERIFY(session.isModified());
    QCOMPARE(session.projectSnapshot().metadata.at("vendor"),
             std::string("unchanged"));
    session.undoStack().undo();
    QCOMPARE(session.document().bytes(), original);
    QVERIFY(!session.isModified());
    session.undoStack().redo();
    const auto recovery = session.writeRecovery();
    QVERIFY(recovery.endsWith(".apoproject"));
    const auto copy = dir.filePath("recover.apoproject");
    QVERIFY(QFile::copy(recovery, copy));
    DocumentSession restored;
    restored.recover(copy);
    QVERIFY(restored.isProject());
    QVERIFY(restored.isModified());
    QCOMPARE(restored.projectSnapshot().animation_data, p.animation_data);
    auto foreign = p;
    foreign.metadata["external"] = "changed";
    write(path, serialize_project(foreign));
    QVERIFY_EXCEPTION_THROWN(session.save(path), FileConflict);
    QCOMPARE(parse_project(read(path)).metadata.at("external"),
             std::string("changed"));
    session.save(path, true);
    QVERIFY(!session.isModified());
    const auto saved = load_project(filesystemPath(path));
    QCOMPARE(saved.render_settings.at("outputSeed"),
             std::string("18446744073709551615"));
    QCOMPARE(saved.animation_data, p.animation_data);
    const auto before = session.document().bytes();
    auto invalid = p;
    invalid.selected_index = 99;
    QVERIFY_EXCEPTION_THROWN(session.loadBytes(serialize_project(invalid)),
                             std::invalid_argument);
    QCOMPARE(session.document().bytes(), before);
    session.discardRecovery();
    restored.discardRecovery();
    // Discard is asynchronous; release both worker-owned leases before the
    // temporary directory is removed by this test's fixture teardown.
    QTRY_VERIFY_WITH_TIMEOUT(!session.isRecoveryBusy() && !restored.isRecoveryBusy(), 5000);
  }
  void privateScriptRunApplyAndUndo() {
    MainWindow window(false);
    fast(window);
    window.session().loadBytes(fixture());
    const auto before = window.session().document().bytes();
    ScriptDialog dialog(window.session().document(), 0, &window);
    dialog.setSource(
        "apo.set_flame{name='Edited by Lua',gamma=2.4}\nprint('done')");
    QSignalSpy finished(&dialog, &ScriptDialog::executionFinished);
    dialog.runScript();
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 15000);
    QVERIFY(dialog.scriptResult().has_value());
    QCOMPARE(window.session().document().bytes(), before);
    window.applyScriptResult(*dialog.scriptResult(), {});
    QCOMPARE(window.session().document().entries()[0].name,
             std::string("Edited by Lua"));
    QCOMPARE(window.session().undoStack().count(), 1);
    window.session().undoStack().undo();
    QCOMPARE(window.session().document().bytes(), before);
    window.session().undoStack().redo();
    QCOMPARE(window.session().document().render_model(0).gamma, 2.4);
    window.session().undoStack().setClean();
  }
  void failureStopAndClosingDiscardPrivateChanges() {
    auto doc = LegacyDocument::parse(fixture());
    ScriptDialog dialog(doc, 0);
    dialog.show();
    QSignalSpy finished(&dialog, &ScriptDialog::executionFinished);
    dialog.setSource(
        "apo.set_flame{name='never applied'}\nerror('deliberate failure')");
    dialog.runScript();
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 15000);
    QVERIFY(!dialog.scriptResult());
    QVERIFY(widget<QPlainTextEdit>(dialog, "scriptConsole")
                ->toPlainText()
                .contains("deliberate failure"));
    widget<QSpinBox>(dialog, "scriptInstructionLimit")->setValue(100000000);
    dialog.setSource("while true do end");
    dialog.runScript();
    dialog.stopScript();
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 2, 15000);
    QVERIFY(!dialog.scriptResult());
    QVERIFY(widget<QPlainTextEdit>(dialog, "scriptConsole")
                ->toPlainText()
                .contains("stopped", Qt::CaseInsensitive));
    dialog.setSource("while true do end");
    dialog.runScript();
    dialog.close();
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 3, 15000);
    QTRY_VERIFY(!dialog.isVisible());
    QVERIFY(!dialog.scriptResult());
  }
  void requestsArePreflightedAndBatchQueued() {
    QTemporaryDir dir;
    MainWindow window(false);
    fast(window);
    window.session().loadBytes(fixture());
    const auto before = window.session().document().bytes();
    ScriptOptions options;
    options.allow_export_requests = true;
    options.allow_render_requests = true;
    const auto result = run_script(
        window.session().document(), 0,
        "apo.set_flame{name='batch'}\napo.export_flame('capture.flame')\napo."
        "render('preview.png',{width=40,height=30,samples=1000})",
        options);
    write(dir.filePath("capture.flame"), "existing");
    QVERIFY_EXCEPTION_THROWN(window.applyScriptResult(result, dir.path()),
                             std::invalid_argument);
    QCOMPARE(window.session().document().bytes(), before);
    QVERIFY(window.renderQueue().jobs().empty());
    QCOMPARE(read(dir.filePath("capture.flame")), std::string("existing"));
    window.applyScriptResult(result, dir.path(), true);
    QCOMPARE(window.renderQueue().jobs().size(), std::size_t(1));
    QVERIFY(!window.renderQueue().isRunning());
    QCOMPARE(window.renderQueue().jobs()[0].flame.name, std::string("batch"));
    QCOMPARE(LegacyDocument::load(filesystemPath(dir.filePath("capture.flame")))
                 .entries()[0]
                 .name,
             std::string("batch"));
    window.session().undoStack().undo();
    QCOMPARE(window.session().document().bytes(), before);
    QCOMPARE(window.renderQueue().jobs().size(), std::size_t(1));
    RenderQueue queue;
    RenderJob valid;
    valid.flame = demo_flame();
    valid.render.samples = 1000;
    valid.destination = dir.filePath("valid.png");
    RenderJob invalid = valid;
    invalid.destination = dir.filePath("wrong.jpg");
    QVERIFY_EXCEPTION_THROWN(queue.addBatch({valid, invalid}),
                             std::invalid_argument);
    QVERIFY(queue.jobs().empty());
    window.session().undoStack().setClean();
  }
  void projectWorkspaceSaveUsesFullEnvelope() {
    QTemporaryDir dir;
    auto p = ProjectFile{};
    p.library_bytes = fixture();
    p.preview_settings["seed"] = "18446744073709551615";
    p.preview_settings["samples"] = "1000";
    p.metadata["foreign"] = "preserved";
    p.render_presets.push_back(
        {"Share", {{"outputWidth", "640"}, {"outputHeight", "480"}}});
    const auto path = dir.filePath("workspace.apoproject");
    write(path, serialize_project(p));
    MainWindow window(false);
    fast(window);
    QVERIFY(window.openFile(path));
    QCOMPARE(window.previewController().settings().seed, UINT64_MAX);
    widget<QAction>(window, "actionSave")->trigger();
    const auto saved = load_project(filesystemPath(path));
    QCOMPARE(saved.metadata.at("foreign"), std::string("preserved"));
    QCOMPARE(saved.metadata.at("application-version"), std::string(application_version()));
    QCOMPARE(saved.render_presets.size(), std::size_t(1));
    QVERIFY(saved.workspace.contains("qt-layout"));
    QCOMPARE(saved.preview_settings.at("seed"),
             std::string("18446744073709551615"));
  }
};
QTEST_MAIN(ExtensionsEditorTests)
#include "extensions_editor_tests.moc"
