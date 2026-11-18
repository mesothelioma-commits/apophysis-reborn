#include "editor/document_session.hpp"
#include "editor/render_queue.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using namespace apo;
using namespace apo::editor;
namespace {
Flame fixture() {
  auto f = demo_flame();
  f.width = 96;
  f.height = 72;
  f.scale *= 96.0 / 640;
  f.oversample = 2;
  f.filter_radius = 0.5;
  f.name = "Queued flame";
  return f;
}
RenderOptions settings(std::uint64_t samples = 131075) {
  RenderOptions o;
  o.samples = samples;
  o.seed = UINT64_C(18446744073709551598);
  o.threads = 2;
  o.memory_mib = 32;
  return o;
}
FileRenderOptions fileSettings() {
  FileRenderOptions o;
  o.image.format = ImageFormat::ppm8;
  o.disk_mib = 512;
  o.strip_rows = 7;
  return o;
}
QByteArray bytes(const QString &path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    return {};
  return file.readAll();
}
void write(const QString &path, const QByteArray &value) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(value) != value.size())
    throw std::runtime_error("Test fixture could not be written");
}
} // namespace
class RenderQueueTests : public QObject {
  Q_OBJECT
private slots:
  void immutableSnapshotsOrderAndPersistence() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    RenderQueue queue(dir.filePath("queue.json"), dir.filePath("checkpoints"));
    auto f = fixture();
    f.transforms[0].parameters.apophysis_math = true;
    f.transforms[0].parameters.separate_blur_angle = true;
    auto o = settings();
    const auto first =
        queue.add(f, o, fileSettings(), dir.filePath("first.ppm"),
                  dir.filePath("source.flame"));
    f.name = "Second snapshot";
    f.gamma = 3.125;
    const auto second =
        queue.add(f, o, fileSettings(), dir.filePath("second.ppm"));
    f.gamma = 8;
    QCOMPARE(queue.job(first)->flame.name, std::string("Queued flame"));
    QCOMPARE(queue.job(second)->flame.gamma, 3.125);
    queue.move(second, -1);
    QCOMPARE(queue.jobs().front().id, second);
    queue.saveTo(dir.filePath("copy.json"));
    RenderQueue restored({}, dir.filePath("other-checkpoints"));
    restored.loadFrom(dir.filePath("copy.json"));
    QVERIFY(!restored.isRunning());
    QVERIFY(!restored.isBusy());
    QCOMPARE(restored.jobs().size(), std::size_t(2));
    QCOMPARE(restored.jobs().front().id, second);
    QCOMPARE(restored.jobs()[0].render.seed, o.seed);
    QCOMPARE(restored.jobs()[0].flame.gamma, 3.125);
    QVERIFY(restored.jobs()[0].flame.transforms[0].parameters.apophysis_math);
    QVERIFY(
        restored.jobs()[0].flame.transforms[0].parameters.separate_blur_angle);
    const auto before = bytes(dir.filePath("queue.json"));
    write(
        dir.filePath("bad.json"),
        "{\"format\":\"apophysis-render-queue\",\"version\":999,\"jobs\":[]}");
    QVERIFY_EXCEPTION_THROWN(queue.loadFrom(dir.filePath("bad.json")),
                             std::invalid_argument);
    QCOMPARE(queue.jobs().size(), std::size_t(2));
    QCOMPARE(bytes(dir.filePath("queue.json")), before);
    QSignalSpy completed(&queue, &RenderQueue::jobFinished);
    int heartbeats = 0;
    QTimer pulse;
    pulse.setInterval(1);
    connect(&pulse, &QTimer::timeout, this, [&] { ++heartbeats; });
    pulse.start();
    queue.start();
    QTRY_VERIFY_WITH_TIMEOUT(completed.size() == 2, 20000);
    QCOMPARE(completed[0][0].toString(), second);
    QCOMPARE(completed[1][0].toString(), first);
    QVERIFY(completed[0][1].toBool());
    QVERIFY(completed[1][1].toBool());
    QVERIFY(heartbeats > 0);
    QVERIFY(bytes(dir.filePath("first.ppm")).startsWith("P6\n96 72\n255\n"));
    QCOMPARE(queue.job(first)->backend, QString("CPU"));
    QCOMPARE(queue.job(first)->workers, 2u);
    QCOMPARE(queue.job(first)->progress, 100);
    queue.pause();
    const auto oldCheckpoint = queue.job(first)->checkpoint;
    const auto oldOutput = bytes(dir.filePath("first.ppm"));
    queue.restart(first);
    QCOMPARE(queue.job(first)->state, RenderJobState::queued);
    QCOMPARE(queue.job(first)->progress, 0);
    QVERIFY(!queue.job(first)->checkpointed);
    QVERIFY(queue.job(first)->checkpoint != oldCheckpoint);
    QVERIFY(QDir(oldCheckpoint).exists());
    QCOMPARE(bytes(dir.filePath("first.ppm")), oldOutput);
    queue.start();
    QTRY_VERIFY_WITH_TIMEOUT(completed.size() == 3, 20000);
    QVERIFY(completed[2][1].toBool());
    QVERIFY(!queue.job(first)->resumed);
    QCOMPARE(bytes(dir.filePath("first.ppm")), oldOutput);
    queue.remove(first);
    QCOMPARE(queue.jobs().size(), std::size_t(1));
  }
  void checkpointPauseRestoreResumeMatchesUninterrupted() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto destination = dir.filePath("resumed.ppm");
    const auto state = dir.filePath("queue.json");
    const auto root = dir.filePath("checkpoints");
    const auto f = fixture();
    const auto options = settings(1500003);
    const auto file = fileSettings();
    write(destination, "EXISTING OUTPUT");
    QString id;
    {
      RenderQueue queue(state, root);
      id = queue.add(f, options, file, destination);
      bool requested = false;
      connect(&queue, &RenderQueue::changed, &queue, [&] {
        const auto job = queue.job(id);
        if (!requested && job && job->completed_samples >= 32768 &&
            queue.isBusy()) {
          requested = true;
          queue.pause();
        }
      });
      queue.start();
      QTRY_VERIFY_WITH_TIMEOUT(requested && !queue.isBusy(), 20000);
      QCOMPARE(queue.job(id)->state, RenderJobState::paused);
      QCOMPARE(queue.job(id)->backend, QString("CPU"));
      QVERIFY(queue.job(id)->checkpointed);
      QVERIFY(queue.job(id)->completed_samples < options.samples);
      QCOMPARE(bytes(destination), QByteArray("EXISTING OUTPUT"));
      QVERIFY(!queue.isRunning());
    }
    RenderQueue restored(state, root);
    restored.loadFrom(state);
    QVERIFY(!restored.isBusy());
    QVERIFY(!restored.isRunning());
    QVERIFY(restored.job(id)->checkpointed);
    QSignalSpy done(&restored, &RenderQueue::jobFinished);
    restored.start();
    QTRY_VERIFY_WITH_TIMEOUT(done.size() == 1, 30000);
    QVERIFY(done[0][1].toBool());
    QCOMPARE(restored.job(id)->state, RenderJobState::complete);
    QVERIFY(restored.job(id)->resumed);
    render_to_file(f, options,
                   filesystemPath(dir.filePath("uninterrupted.ppm")), file);
    QCOMPARE(bytes(destination), bytes(dir.filePath("uninterrupted.ppm")));
  }
  void cancelledOutputAndFailedJobRetry() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    RenderQueue queue(dir.filePath("queue.json"), dir.filePath("checkpoints"));
    const auto output = dir.filePath("cancelled.ppm");
    write(output, "PRESERVED");
    const auto id =
        queue.add(fixture(), settings(1500003), fileSettings(), output);
    queue.start();
    queue.cancel(id);
    QTRY_VERIFY_WITH_TIMEOUT(!queue.isBusy(), 15000);
    QCOMPARE(queue.job(id)->state, RenderJobState::cancelled);
    QCOMPARE(bytes(output), QByteArray("PRESERVED"));
    const auto missing = dir.filePath("missing/failed.ppm");
    const auto failed =
        queue.add(fixture(), settings(32771), fileSettings(), missing);
    queue.start();
    QTRY_VERIFY_WITH_TIMEOUT(queue.job(failed)->state == RenderJobState::failed,
                             15000);
    QVERIFY(!queue.job(failed)->error.isEmpty());
    QVERIFY(!QFileInfo::exists(missing));
    QVERIFY(QDir().mkpath(dir.filePath("missing")));
    queue.retry(failed);
    queue.start();
    QTRY_VERIFY_WITH_TIMEOUT(
        queue.job(failed)->state == RenderJobState::complete, 15000);
    QVERIFY(bytes(missing).startsWith("P6\n96 72\n255\n"));
  }
  void pauseFromStartedSignalStopsTheActiveWorker() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    RenderQueue queue({}, dir.filePath("checkpoints"));
    const auto output = dir.filePath("stopped.ppm");
    write(output, "PRESERVED");
    const auto id =
        queue.add(fixture(), settings(20000003), fileSettings(), output);
    connect(&queue, &RenderQueue::busyChanged, &queue, [&](bool busy) {
      if (busy)
        queue.pause();
    });
    queue.start();
    QTRY_VERIFY_WITH_TIMEOUT(!queue.isBusy(), 15000);
    QCOMPARE(queue.job(id)->state, RenderJobState::paused);
    QVERIFY(!queue.isRunning());
    QCOMPARE(bytes(output), QByteArray("PRESERVED"));
  }
  void destructionLeavesStoppedResumableQueue() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto path = dir.filePath("queue.json");
    const auto root = dir.filePath("checkpoints");
    QString id;
    {
      RenderQueue queue(path, root);
      id = queue.add(fixture(), settings(20000003), fileSettings(),
                     dir.filePath("large.ppm"));
      queue.start();
      QTRY_VERIFY_WITH_TIMEOUT(queue.job(id)->completed_samples >= 32768,
                               15000);
    }
    RenderQueue restored(path, root);
    restored.loadFrom(path);
    QCOMPARE(restored.job(id)->state, RenderJobState::paused);
    QVERIFY(restored.job(id)->checkpointed);
    QVERIFY(!restored.isRunning());
    QVERIFY(!restored.isBusy());
    QVERIFY(!QFileInfo::exists(dir.filePath("large.ppm")));
  }
};
QTEST_MAIN(RenderQueueTests)
#include "render_queue_tests.moc"
