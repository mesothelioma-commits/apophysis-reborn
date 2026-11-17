#include "editor/preview_controller.hpp"

#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

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
