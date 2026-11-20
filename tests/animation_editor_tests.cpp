#include "editor/animation_dialog.hpp"
#include "editor/sequence_composition.hpp"
#include <QProcess>
#include "editor/main_window.hpp"
#include "flame_io.hpp"
#include "sha256.hpp"
#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

using namespace apo;
using namespace apo::editor;
namespace {
Flame flame() {
  auto f = demo_flame(); f.width = 72; f.height = 54; f.scale = 18;
  return f;
}
LegacyDocument fixture() { return LegacyDocument::parse(serialize_flame(flame())); }
PreviewSettings quick() {
  PreviewSettings p; p.sample_limit = 1500; p.threads = 1; p.debounce_ms = 1;
  return p;
}
AnimationClip clip() {
  return make_animation_preset(fixture(), 0, AnimationPreset::zoom, 5, {24, 1}, UINT64_MAX);
}
void write(const QString &path, std::string_view bytes) {
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly) || f.write(bytes.data(), qsizetype(bytes.size())) != qsizetype(bytes.size()))
    throw std::runtime_error("test write failed");
}
std::string read(const QString &path) {
  QFile f(path); if (!f.open(QIODevice::ReadOnly)) throw std::runtime_error("test read failed");
  return f.readAll().toStdString();
}
template<class T> T *control(QObject &owner, const char *name) {
  auto result = owner.findChild<T *>(name);
  if (!result) qFatal("Missing animation control %s", name);
  return result;
}
AnimationExportOptions output(const QString &folder) {
  AnimationExportOptions o;
  o.output_directory = filesystemPath(folder); o.first = 0; o.last = 4; o.step = 2;
  o.width = 48; o.height = 36; o.render.samples = 1000; o.render.threads = 1;
  o.render.memory_mib = 32; o.file.disk_mib = 64; o.file.image.format = ImageFormat::png16;
  return o;
}
void clean(MainWindow &window) { window.previewController().stop(); window.session().undoStack().setClean(); }
}
class AnimationEditorTests : public QObject {
  Q_OBJECT
private slots:
  void composedLayersAndVideo() {
    QTemporaryDir dir;
    const auto bottom = dir.filePath("base_000000.png");
    const auto top = dir.filePath("top_000000.png");
    QImage img(13, 15, QImage::Format_RGBA8888);
    img.fill(Qt::blue); QVERIFY(img.save(bottom));
    img.fill(Qt::transparent); img.setPixelColor(0, 0, Qt::red); QVERIFY(img.save(top));
    const auto output = dir.filePath("composite");
    composeSequence({bottom, top}, output, false, 24, 1, 1000);
    QImage frame(output + "/frame_000000.png");
    QCOMPARE(frame.pixelColor(0,0), QColor(Qt::red));
    QCOMPARE(frame.pixelColor(1,1), QColor(Qt::blue));
    QVERIFY_EXCEPTION_THROWN(composeSequence({top}, output, false, 24, 1, 1000), std::exception);
    ProjectFile project;
    project.library_bytes = fixture().bytes();
    project.animation_data = serialize_animation(clip());
    const auto path = dir.filePath("layer.apoproject");
    save_project(project, filesystemPath(path));
    const auto projectOutput = dir.filePath("from-projects");
    composeSequence({path, path}, projectOutput, false, 24, 1, 1000);
    QCOMPARE(QDir(projectOutput).entryList({"*.png"}, QDir::Files).size(), 5);
    const auto probe = QStandardPaths::findExecutable("ffprobe");
    if (probe.isEmpty() || QStandardPaths::findExecutable("ffmpeg").isEmpty()) return;
    const auto mp4 = dir.filePath("sequence.mp4");
    composeSequence({projectOutput + "/frame_000000.png"}, mp4, true, 24000, 1001, 1000);
    QProcess decoder;
    decoder.start(probe, {"-v", "error", "-select_streams", "v:0", "-show_entries",
                          "stream=codec_name,r_frame_rate,nb_frames", "-of", "default=nw=1", mp4});
    QVERIFY(decoder.waitForFinished());
    auto fields = decoder.readAllStandardOutput();
    QVERIFY(fields.contains("codec_name=h264")); QVERIFY(fields.contains("nb_frames=5"));
    QVERIFY(fields.contains("r_frame_rate=24000/1001"));
  }
  void initTestCase() {
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName("ApophysisRebornTests");
    QCoreApplication::setApplicationName("animation-" + QUuid::createUuid().toString(QUuid::WithoutBraces));
  }
  void cleanupTestCase() {
    QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).removeRecursively();
  }
  void timelineUndoSaveRecoveryAndPlainSaveGuard() {
    QTemporaryDir dir;
    auto original = fixture().bytes();
    ProjectFile project; project.library_bytes = original; project.metadata["vendor"] = "keep";
    project.render_settings["seed"] = "18446744073709551615";
    DocumentSession session; session.loadBytes(serialize_project(project));
    const auto payload = serialize_animation(clip());
    session.changeAnimation("Timeline", payload);
    QVERIFY(session.hasAnimation()); QVERIFY(session.isModified());
    QCOMPARE(session.undoStack().count(), 1);
    session.changeAnimation("No-op", payload); QCOMPARE(session.undoStack().count(), 1);
    session.undoStack().undo(); QVERIFY(!session.hasAnimation()); QVERIFY(!session.isModified());
    session.undoStack().redo(); QVERIFY(session.hasAnimation());
    QCOMPARE(session.document().bytes(), original);
    QCOMPARE(session.projectSnapshot().metadata.at("vendor"), std::string("keep"));
    const auto plain = dir.filePath("original.flame"); write(plain, original);
    QVERIFY_EXCEPTION_THROWN(session.save(plain), std::invalid_argument);
    QCOMPARE(read(plain), original);
    const auto recovery = session.writeRecovery();
    QVERIFY(recovery.endsWith(".apoproject"));
    const auto recoveryCopy = dir.filePath("recovered.apoproject");
    QVERIFY(QFile::copy(recovery, recoveryCopy));
    DocumentSession recovered; recovered.recover(recoveryCopy);
    QCOMPARE(recovered.projectSnapshot().animation_data.value(), payload);
    QVERIFY(recovered.isModified());
    const auto native = dir.filePath("clip.apoproject"); session.save(native);
    QVERIFY(!session.isModified());
    session.undoStack().undo(); QVERIFY(session.isModified());
    QVERIFY(!session.hasAnimation()); session.undoStack().redo(); QVERIFY(!session.isModified());
    QCOMPARE(load_project(filesystemPath(native)).animation_data.value(), payload);
    QCOMPARE(load_project(filesystemPath(native)).render_settings.at("seed"), std::string("18446744073709551615"));
    recovered.discardRecovery(); session.discardRecovery();
  }
  void opaquePayloadPreservedAndInvalidChangeRollsBack() {
    MainWindow window(false); window.previewController().setSettings(quick());
    ProjectFile project; project.library_bytes = fixture().bytes();
    project.animation_data = std::string("\0opaque", 7) + char(0xff) + "future";
    window.session().loadBytes(serialize_project(project));
    const auto before = serialize_project(window.session().projectSnapshot());
    QVERIFY_EXCEPTION_THROWN(window.applyAnimation(clip()), std::invalid_argument);
    QCOMPARE(serialize_project(window.session().projectSnapshot()), before);
    QVERIFY_EXCEPTION_THROWN(window.session().changeAnimation("Too large", std::string(8 * 1024 * 1024 + 1, 'x')), std::exception);
    QCOMPARE(serialize_project(window.session().projectSnapshot()), before);
    QCOMPARE(window.session().undoStack().count(), 0);
    clean(window);
  }
  void propertyKeysAcrossFramesAndProjectReload() {
    AnimationDialog dialog(fixture(), 0, clip(), nullptr, quick());
    auto field = control<QLineEdit>(dialog, "timelinePropertyValue");
    auto set = control<QPushButton>(dialog, "timelineSetPropertyKey");
    dialog.setFrame(1);
    field->setText("1.25"); set->click();
    dialog.setFrame(3);
    field->setText("2.25"); set->click();
    auto reloaded = parse_animation(serialize_animation(dialog.clip()));
    PreparedAnimation result(reloaded);
    QCOMPARE(result.evaluate(0).flame.zoom, 1.25);
    QCOMPARE(result.evaluate(2).flame.zoom, 1.75);
    QCOMPARE(result.evaluate(4).flame.zoom, 2.25);
    control<QPushButton>(dialog, "timelineUndo")->click();
    QCOMPARE(dialog.clip().tracks[0].size(), std::size_t(1));
  }
  void timelineEditsRemainPrivateAndSupportLocalUndo() {
    auto document = fixture(); auto c = clip();
    const auto source = document.bytes();
    AnimationDialog dialog(document, 0, c, nullptr, quick());
    dialog.show(); QVERIFY(QTest::qWaitForWindowExposed(&dialog));
    QSignalSpy preview(&dialog, &AnimationDialog::framePreviewed);
    dialog.setFrame(2);
    QTRY_VERIFY_WITH_TIMEOUT(!preview.empty(), 15000);
    QVERIFY(!dialog.previewImage().isNull());
    auto changed = document; LegacyEdits edits; edits.gamma = 3.1; changed.edit(0, edits);
    dialog.captureKeyframe(changed, 0, 2);
    QCOMPARE(dialog.clip().keyframes.size(), std::size_t(3));
    QCOMPARE(dialog.currentFlame()->gamma, 3.1);
    QCOMPARE(document.bytes(), source);
    control<QPushButton>(dialog, "timelineUndo")->click();
    QCOMPARE(dialog.clip(), c);
    control<QPushButton>(dialog, "timelineRedo")->click();
    QCOMPARE(dialog.clip().keyframes.size(), std::size_t(3));
    const auto before = serialize_animation(dialog.clip());
    QVERIFY_EXCEPTION_THROWN(dialog.captureKeyframe(changed, 0, 2), std::invalid_argument);
    QCOMPARE(serialize_animation(dialog.clip()), before);
    control<QLineEdit>(dialog, "timelineSeed")->setText("18446744073709551616");
    control<QPushButton>(dialog, "timelineApply")->click();
    QVERIFY(dialog.isVisible());
    QCOMPARE(serialize_animation(dialog.clip()), before);
    QVERIFY(control<QLabel>(dialog, "timelineStatus")->text().contains("whole number"));
    dialog.close(); QCOMPARE(document.bytes(), source);
  }
  void playbackReachesFramesAndStopsWithoutChangingClip() {
    auto c = clip(); c.loop = AnimationLoop::once;
    AnimationDialog dialog(fixture(), 0, c, nullptr, quick()); dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));
    QSignalSpy rendered(&dialog, &AnimationDialog::framePreviewed);
    QSignalSpy playback(&dialog, &AnimationDialog::playbackChanged);
    dialog.play(); QVERIFY(dialog.isPlaying());
    QTRY_VERIFY_WITH_TIMEOUT(!dialog.isPlaying(), 20000);
    QCOMPARE(dialog.frame(), 4u); QVERIFY(rendered.size() >= 2); QCOMPARE(playback.size(), 2);
    QCOMPARE(dialog.clip(), c);
    dialog.setFrame(2); dialog.play(); dialog.close(); QVERIFY(!dialog.isPlaying());
  }
  void rationalFrameRatesCommitAsOneSetting() {
    AnimationDialog dialog(fixture(), 0, clip(), nullptr, quick());
    dialog.show(); QVERIFY(QTest::qWaitForWindowExposed(&dialog));
    auto preset = control<QComboBox>(dialog, "timelineRatePreset");
    preset->setCurrentIndex(preset->findData(QString("30000/1001")));
    QCOMPARE(dialog.clip().rate, (AnimationRate{30000, 1001}));
    auto numerator = control<QSpinBox>(dialog, "timelineRateNumerator");
    auto denominator = control<QSpinBox>(dialog, "timelineRateDenominator");
    numerator->setFocus(); numerator->setValue(24000);
    denominator->setFocus(); QCoreApplication::processEvents();
    QCOMPARE(dialog.clip().rate, (AnimationRate{30000, 1001}));
    denominator->setValue(1001);
    control<QLineEdit>(dialog, "timelineName")->setFocus();
    QCoreApplication::processEvents();
    QCOMPARE(dialog.clip().rate, (AnimationRate{24000, 1001}));
    control<QPushButton>(dialog, "timelineUndo")->click();
    QCOMPARE(dialog.clip().rate, (AnimationRate{30000, 1001}));
    dialog.close();
  }
  void batchAdmissionIsAtomicAndSnapshotsSurviveEdits() {
    QTemporaryDir dir;
    MainWindow window(false, nullptr, dir.filePath("queue-state"));
    window.previewController().setSettings(quick()); window.session().loadBytes(fixture().bytes());
    auto c = clip(); c.seed_mode = AnimationSeedMode::per_frame;
    auto o = output(dir.filePath("frames"));
    auto bad = o; bad.prefix = "../escape";
    QVERIFY_EXCEPTION_THROWN(window.queueAnimation(c, bad), std::invalid_argument);
    QVERIFY(window.renderQueue().jobs().empty()); QVERIFY(!window.session().hasAnimation());
    QVERIFY(!QFileInfo(dir.filePath("frames")).exists());
    QVERIFY(QDir().mkpath(dir.filePath("frames")));
    auto invalid = c; invalid.keyframes.back().transform_ids.front() = "unmatched";
    QVERIFY_EXCEPTION_THROWN(window.queueAnimation(invalid, o), std::invalid_argument);
    QVERIFY(window.renderQueue().jobs().empty());
    const auto ids = window.queueAnimation(c, o);
    QCOMPARE(ids.size(), std::size_t(3)); QCOMPARE(window.session().undoStack().count(), 1);
    const PreparedAnimation prepared(c);
    for (std::size_t i = 0; i < ids.size(); ++i) {
      const auto job = window.renderQueue().job(ids[i]); QVERIFY(job);
      QCOMPARE(job->render.seed, animation_seed(c, std::uint32_t(i * 2)));
      QCOMPARE(job->flame.width, 48); QCOMPARE(job->flame.height, 36);
      QCOMPARE(job->flame.zoom, prepared.evaluate(std::int64_t(i * 2)).flame.zoom);
      QVERIFY(job->destination.endsWith(QString("frame_%1.png").arg(i * 2, 6, 10, QChar('0'))));
      QVERIFY(job->destination_sha256.has_value()); QVERIFY(job->destination_sha256->empty());
      QVERIFY(job->name.contains(QString("frame %1").arg(i * 2)));
    }
    const auto jobBytes = serialize_flame(window.renderQueue().job(ids.back())->flame);
    auto changed = c; changed.seed = 77; window.applyAnimation(changed);
    window.session().change("Edit source", [](auto &doc) { LegacyEdits e; e.gamma = 4; doc.edit(0, e); });
    QCOMPARE(serialize_flame(window.renderQueue().job(ids.back())->flame), jobBytes);
    const auto saved = dir.filePath("queue-copy.json"); window.renderQueue().saveTo(saved);
    RenderQueue restored; restored.loadFrom(saved);
    QCOMPARE(restored.jobs().size(), std::size_t(3));
    QCOMPARE(restored.jobs().back().destination_sha256, std::optional<std::string>(""));
    QCOMPARE(serialize_flame(restored.jobs().back().flame), jobBytes);
    QSignalSpy finished(&window.renderQueue(), &RenderQueue::jobFinished);
    window.renderQueue().start(); QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 3, 20000);
    for (const auto &id : ids) {
      const auto job = window.renderQueue().job(id);
      QCOMPARE(job->state, RenderJobState::complete); QVERIFY(QFileInfo(job->destination).size() > 0);
      QCOMPARE(*job->destination_sha256, detail::sha256_file(filesystemPath(job->destination)));
    }
    clean(window);
  }
  void pendingFrameDoesNotOverwriteAFileThatAppearsLater() {
    QTemporaryDir dir; MainWindow window(false);
    window.previewController().setSettings(quick()); window.session().loadBytes(fixture().bytes());
    auto o = output(dir.path()); o.last = 0;
    const auto ids = window.queueAnimation(clip(), o);
    const auto destination = window.renderQueue().job(ids.front())->destination;
    write(destination, "external work");
    QSignalSpy finished(&window.renderQueue(), &RenderQueue::jobFinished);
    window.renderQueue().start(); QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 15000);
    QCOMPARE(window.renderQueue().job(ids.front())->state, RenderJobState::failed);
    QCOMPARE(read(destination), std::string("external work"));
    QVERIFY(window.renderQueue().job(ids.front())->error.contains("appeared"));
    clean(window);
  }
  void admittedReplacementRejectsChangedBytesAfterRestore() {
    QTemporaryDir dir; MainWindow window(false);
    window.previewController().setSettings(quick()); window.session().loadBytes(fixture().bytes());
    auto o = output(dir.path()); o.last = 0; o.replace_existing = true;
    const auto destination = dir.filePath("frame_000000.png"); write(destination, "previous frame");
    const auto ids = window.queueAnimation(clip(), o);
    const auto saved = dir.filePath("guarded-queue.json"); window.renderQueue().saveTo(saved);
    RenderQueue restored; restored.loadFrom(saved);
    QCOMPARE(restored.jobs().front().destination_sha256.value(), detail::sha256_bytes("previous frame"));
    write(destination, "changed after restore");
    QSignalSpy finished(&restored, &RenderQueue::jobFinished);
    restored.start(); QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 15000);
    QCOMPARE(restored.jobs().front().state, RenderJobState::failed);
    QCOMPARE(read(destination), std::string("changed after restore"));
    clean(window);
  }
};
QTEST_MAIN(AnimationEditorTests)
#include "animation_editor_tests.moc"
