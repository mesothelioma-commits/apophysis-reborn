#include "editor/preview_controller.hpp"

#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

using namespace apo;
using namespace apo::editor;

namespace {
Flame coloredPoint(RGB color) {
    Flame flame;
    flame.width = 80;
    flame.height = 40;
    flame.scale = 6;
    flame.palette.fill(color);
    Transform transform;
    transform.pre = {0, 0, 0, 0, 0, 0};
    flame.transforms.push_back(transform);
    return flame;
}
PreviewSettings quickSettings() {
    PreviewSettings result;
    result.sample_limit = 160000;
    result.debounce_ms = 1;
    result.threads = 2;
    return result;
}
} // namespace

class PreviewTests : public QObject {
    Q_OBJECT
  private slots:
    void reentrantStopDoesNotLeaveBusyState() {
        PreviewController controller;
        controller.setSettings(quickSettings());
        QSignalSpy images(&controller, &PreviewController::imageReady);
        QSignalSpy statuses(&controller, &PreviewController::statusChanged);
        bool stopped = false;
        connect(&controller, &PreviewController::progressChanged, &controller, [&](int value) {
            if (value == 0 && !stopped) {
                stopped = true;
                controller.stop();
            }
        });
        controller.request(coloredPoint({1, 0, 0}), QSize(40, 20));
        QVERIFY(stopped);
        QVERIFY(!controller.isBusy());
        QCOMPARE(statuses.back().at(0).toString(), QString("Stopped"));
        QTest::qWait(30);
        QCOMPARE(images.count(), 0);
        QVERIFY(!controller.isBusy());
    }

    void reentrantCompletion_data() {
        QTest::addColumn<QString>("signal");
        QTest::addColumn<bool>("replace");
        for (const auto &signal : {QString("busy"), QString("backend"), QString("status")}) {
            QTest::newRow(qPrintable(signal + "-replace")) << signal << true;
            QTest::newRow(qPrintable(signal + "-stop")) << signal << false;
        }
    }

    void reentrantCompletion() {
        QFETCH(QString, signal);
        QFETCH(bool, replace);
        PreviewController controller;
        auto settings = quickSettings();
        settings.sample_limit = 5000; // One pass makes the completion handoff unambiguous.
        controller.setSettings(settings);
        QSignalSpy images(&controller, &PreviewController::imageReady);
        QSignalSpy failures(&controller, &PreviewController::failed);
        bool changed = false;
        const auto change = [&] {
            if (changed)
                return;
            changed = true;
            if (replace)
                controller.request(coloredPoint({0, 0, 1}), QSize(60, 30));
            else
                controller.stop();
        };
        connect(&controller, &PreviewController::busyChanged, &controller,
                [&](bool busy) { if (signal == "busy" && !busy) change(); });
        connect(&controller, &PreviewController::backendResolved, &controller,
                [&] { if (signal == "backend") change(); });
        connect(&controller, &PreviewController::statusChanged, &controller,
                [&](const QString &status) {
                    if (signal == "status" && status.startsWith("Preview ready"))
                        change();
                });
        controller.request(coloredPoint({1, 0, 0}), QSize(40, 20));
        QTRY_VERIFY_WITH_TIMEOUT(changed && !controller.isBusy(), 10000);
        QCOMPARE(failures.count(), 0);
        QCOMPARE(images.count(), replace ? 1 : 0);
        if (replace) {
            const auto image = qvariant_cast<QImage>(images.front().at(0));
            QCOMPARE(image.size(), QSize(60, 30));
            QCOMPARE(image.pixelColor(30, 15).red(), 0);
            QVERIFY(image.pixelColor(30, 15).blue() > 0);
        }
    }

    void reentrantFailureDoesNotPublishAnOldError() {
        PreviewController controller;
        controller.setSettings(quickSettings());
        QSignalSpy images(&controller, &PreviewController::imageReady);
        QSignalSpy failures(&controller, &PreviewController::failed);
        bool replaced = false;
        connect(&controller, &PreviewController::busyChanged, &controller, [&](bool busy) {
            if (!busy && !replaced) {
                replaced = true;
                controller.request(coloredPoint({0, 0, 1}), QSize(40, 20));
            }
        });
        auto invalid = coloredPoint({1, 0, 0});
        invalid.width = 0;
        controller.request(invalid, QSize(40, 20));
        QTRY_VERIFY_WITH_TIMEOUT(replaced && !controller.isBusy(), 10000);
        QCOMPARE(failures.count(), 0);
        QVERIFY(!images.isEmpty());
        for (const auto &signal : images) {
            const auto image = qvariant_cast<QImage>(signal.at(0));
            QVERIFY(image.pixelColor(20, 10).blue() > 0);
            QCOMPARE(image.pixelColor(20, 10).red(), 0);
        }
    }

    void completionCanDeleteController() {
        auto *controller = new PreviewController;
        const QPointer<PreviewController> alive(controller);
        auto settings = quickSettings();
        settings.sample_limit = 5000;
        controller->setSettings(settings);
        connect(controller, &PreviewController::busyChanged, this, [controller](bool busy) {
            if (!busy)
                delete controller;
        });
        controller->request(coloredPoint({0, 1, 0}), QSize(40, 20));
        QTRY_VERIFY_WITH_TIMEOUT(alive.isNull(), 10000);
        QCoreApplication::processEvents();
    }

    void progressBacklogIsCoalesced() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const auto path = folder.filePath("many strips.ppm");
        auto flame = coloredPoint({0, 1, 0});
        flame.height = 1024;
        PreviewController controller;
        QSignalSpy progress(&controller, &PreviewController::progressChanged);
        QSignalSpy complete(&controller, &PreviewController::fileReady);
        controller.requestFileRender(flame, path, {200000, 13, 2});

        // A temporarily occupied GUI must not accumulate one callback per
        // reconstruction progress update. The file is published by the worker.
        QElapsedTimer wait;
        wait.start();
        while (!QFileInfo::exists(path) && wait.elapsed() < 10000)
            QTest::qSleep(1);
        QVERIFY(QFileInfo::exists(path));
        QTRY_COMPARE_WITH_TIMEOUT(complete.count(), 1, 10000);
        QVERIFY2(progress.count() <= 4, qPrintable(QString("Queued %1 progress updates").arg(progress.count())));
        QCOMPARE(progress.back().at(0).toInt(), 100);
        QVERIFY(!controller.isBusy());
    }

    void requestOwnsItsSnapshot() {
        PreviewController controller;
        auto settings = quickSettings();
        settings.sample_limit = 5000;
        controller.setSettings(settings);
        QSignalSpy images(&controller, &PreviewController::imageReady);
        auto source = coloredPoint({1, 0, 0});
        controller.request(source, QSize(40, 20));
        source.palette.fill({0, 0, 1});
        source.width = 1;
        source.transforms.clear();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.isBusy(), 10000);
        QCOMPARE(images.count(), 1);
        const auto image = qvariant_cast<QImage>(images.front().at(0));
        QCOMPARE(image.size(), QSize(40, 20));
        QVERIFY(image.pixelColor(20, 10).red() > 0);
        QCOMPARE(image.pixelColor(20, 10).blue(), 0);
    }

    void replacementStressKeepsEventLoopResponsive() {
        PreviewController controller;
        auto settings = quickSettings();
        settings.sample_limit = 20000000;
        settings.debounce_ms = 0;
        controller.setSettings(settings);
        QSignalSpy failures(&controller, &PreviewController::failed);
        QElapsedTimer clock;
        clock.start();
        qint64 previous = 0, worstGap = 0;
        int beats = 0, changes = 0;
        bool finalImage = false;
        QTimer heartbeat;
        heartbeat.setTimerType(Qt::PreciseTimer);
        heartbeat.setInterval(2);
        connect(&heartbeat, &QTimer::timeout, this, [&] {
            const auto now = clock.elapsed();
            worstGap = std::max(worstGap, now - previous);
            previous = now;
            ++beats;
        });
        QTimer edits;
        edits.setInterval(4);
        connect(&edits, &QTimer::timeout, this, [&] {
            ++changes;
            if (changes == 40) {
                edits.stop();
                controller.setSettings(quickSettings());
                controller.setPaused(false);
                controller.request(coloredPoint({0, 0, 1}), QSize(60, 30));
            } else if (changes % 7 == 0) {
                controller.setPaused(true);
            } else if (changes % 5 == 0) {
                controller.stop();
            } else {
                controller.setPaused(false);
                controller.request(coloredPoint({1, 0, 0}), QSize(160, 80));
                if (changes % 3 == 0)
                    controller.restart();
            }
        });
        connect(&controller, &PreviewController::imageReady, this,
                [&](const QImage &image, quint64, quint64, quint64, unsigned, qint64, bool finalPass) {
                    if (changes == 40 && finalPass) {
                        QCOMPARE(image.size(), QSize(60, 30));
                        QCOMPARE(image.pixelColor(30, 15).red(), 0);
                        QVERIFY(image.pixelColor(30, 15).blue() > 0);
                        finalImage = true;
                    }
                });
        heartbeat.start();
        edits.start();
        controller.request(coloredPoint({1, 0, 0}), QSize(160, 80));
        QTRY_VERIFY_WITH_TIMEOUT(finalImage && !controller.isBusy(), 10000);
        heartbeat.stop();
        QVERIFY(beats >= 10);
        QVERIFY2(worstGap < 500, qPrintable(QString("Worst GUI heartbeat gap: %1 ms").arg(worstGap)));
        QCOMPARE(failures.count(), 0);
        qInfo("Preview replacement stress: %d changes, %d GUI heartbeats, worst gap %lld ms",
              changes, beats, static_cast<long long>(worstGap));
    }

    void progressiveAndLatest() {
        PreviewController controller;
        controller.setSettings(quickSettings());
        QSignalSpy images(&controller, &PreviewController::imageReady);
        QSignalSpy failures(&controller, &PreviewController::failed);
        controller.request(coloredPoint({1, 0, 0}), QSize(40, 40));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.isBusy(), 10000);
        QCOMPARE(failures.count(), 0);
        QCOMPARE(images.count(), 2);
        QCOMPARE(images.front().at(1).toULongLong(), quint64(80000));
        QCOMPARE(images.back().at(1).toULongLong(), quint64(160000));
        QVERIFY(images.back().at(6).toBool());
        auto image = qvariant_cast<QImage>(images.back().at(0));
        QCOMPARE(image.size(), QSize(40, 20));
        QVERIFY(image.pixelColor(20, 10).red() > 0);
        QCOMPARE(image.pixelColor(20, 10).blue(), 0);

        auto settings = quickSettings();
        settings.sample_limit = 100000000;
        settings.debounce_ms = 0;
        controller.setSettings(settings);
        QTest::qWait(10);
        controller.setSettings(quickSettings());
        controller.request(coloredPoint({0, 0, 1}), QSize(60, 30));
        images.clear();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.isBusy(), 10000);
        QVERIFY(!images.isEmpty());
        for (const auto &signal : images) {
            image = qvariant_cast<QImage>(signal.at(0));
            QCOMPARE(image.size(), QSize(60, 30));
            QCOMPARE(image.pixelColor(30, 15).red(), 0);
            QVERIFY(image.pixelColor(30, 15).blue() > 0);
        }
        QCOMPARE(failures.count(), 0);
    }

    void pauseDebounceAndDestruction() {
        PreviewController controller;
        auto settings = quickSettings();
        settings.debounce_ms = 40;
        controller.setSettings(settings);
        QSignalSpy images(&controller, &PreviewController::imageReady);
        controller.request(coloredPoint({1, 0, 0}), QSize(50, 25));
        controller.request(coloredPoint({0, 1, 0}), QSize(50, 25));
        controller.setPaused(true);
        QTest::qWait(90);
        QCOMPARE(images.count(), 0);
        QVERIFY(!controller.isBusy());
        controller.setPaused(false);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.isBusy(), 10000);
        QVERIFY(!images.isEmpty());
        auto image = qvariant_cast<QImage>(images.back().at(0));
        QVERIFY(image.pixelColor(25, 12).green() > 0);
        QCOMPARE(image.pixelColor(25, 12).red(), 0);

        QElapsedTimer elapsed;
        elapsed.start();
        {
            PreviewController transient;
            settings.sample_limit = 1000000000;
            settings.debounce_ms = 0;
            transient.setSettings(settings);
            transient.request(coloredPoint({1, 0, 0}), QSize(100, 50));
            QTest::qWait(10);
        }
        QVERIFY(elapsed.elapsed() < 3000);
        // Let already queued worker callbacks run after their target's destruction.
        QCoreApplication::processEvents();
    }

    void renderFileAndCancel() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const QString path = folder.filePath(QStringLiteral("render with spaces.ppm"));
        PreviewController controller;
        QSignalSpy complete(&controller, &PreviewController::fileReady);
        QSignalSpy failures(&controller, &PreviewController::failed);
        controller.requestFileRender(coloredPoint({0, 1, 0}), path, {150000, 13, 2});
        QTRY_COMPARE_WITH_TIMEOUT(complete.count(), 1, 10000);
        QVERIFY(!controller.isBusy());
        QImage result(path);
        QCOMPARE(result.size(), QSize(80, 40));
        QVERIFY(result.pixelColor(40, 20).green() > 0);
        QFile saved(path);
        QVERIFY(saved.open(QIODevice::ReadOnly));
        const auto original = saved.readAll();
        saved.close();
        complete.clear();
        controller.requestFileRender(coloredPoint({1, 0, 0}), path, {1000000000, 13, 2});
        QTest::qWait(10);
        controller.stop();
        QTest::qWait(100);
        QCOMPARE(complete.count(), 0);
        QCOMPARE(failures.count(), 0);
        QVERIFY(saved.open(QIODevice::ReadOnly));
        QCOMPARE(saved.readAll(), original);
    }

    void backendFallbackAndStrictFailure() {
        PreviewController controller;
        auto settings = quickSettings();
        settings.backend = RenderBackend::vulkan;
        settings.device = "__missing_preview_test_device__";
        settings.gpu_memory_mib = 32;
        controller.setSettings(settings);
        QSignalSpy images(&controller, &PreviewController::imageReady);
        QSignalSpy backends(&controller, &PreviewController::backendResolved);
        QSignalSpy failures(&controller, &PreviewController::failed);
        controller.request(coloredPoint({0, 0, 1}), QSize(40, 20));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.isBusy(), 10000);
        QCOMPARE(failures.count(), 0);
        QCOMPARE(images.count(), 2);
        QCOMPARE(backends.count(), 2);
        for (const auto &signal : backends) {
            QCOMPARE(signal.at(0).toString(), QString("CPU"));
            QVERIFY(!signal.at(2).toString().isEmpty());
        }
        const auto image = qvariant_cast<QImage>(images.back().at(0));
        QVERIFY(image.pixelColor(20, 10).blue() > 0);

        images.clear();
        backends.clear();
        settings.allow_cpu_fallback = false;
        controller.setSettings(settings);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.isBusy(), 10000);
        QCOMPARE(failures.count(), 1);
        QCOMPARE(images.count(), 0);
        QCOMPARE(backends.count(), 0);
        auto invalid = settings;
        invalid.gpu_memory_mib = 0;
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, controller.setSettings(invalid));
        QCOMPARE(controller.settings().gpu_memory_mib, UINT64_C(32));
    }

    void strictBackendFailureKeepsExistingOutput() {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        const auto path = folder.filePath("keep existing.ppm");
        QFile original(path);
        QVERIFY(original.open(QIODevice::WriteOnly));
        QCOMPARE(original.write("keep these bytes"), qint64(16));
        original.close();
        PreviewController controller;
        QSignalSpy complete(&controller, &PreviewController::fileReady);
        QSignalSpy failures(&controller, &PreviewController::failed);
        RenderOptions options{10000, 13, 2};
        options.backend = RenderBackend::vulkan;
        options.device = "__missing_export_test_device__";
        options.allow_cpu_fallback = false;
        controller.requestFileRender(coloredPoint({1, 0, 0}), path, options);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.isBusy(), 10000);
        QCOMPARE(complete.count(), 0);
        QCOMPARE(failures.count(), 1);
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), QByteArray("keep these bytes"));
    }
};

QTEST_GUILESS_MAIN(PreviewTests)
#include "preview_tests.moc"
