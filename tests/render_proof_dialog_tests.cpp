#include "editor/render_proof_dialog.hpp"
#include <QCheckBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSemaphore>
#include <QSpinBox>
#include <QTest>
#include <atomic>
#include <cmath>

using namespace apo;
using namespace apo::editor;

namespace {
template<class T> T *field(QObject &dialog, const char *name) {
    auto *widget = dialog.findChild<T *>(QString::fromLatin1(name));
    if (!widget) qFatal("Missing proof control %s", name);
    return widget;
}

QSize logicalSizeForPixels(QWidget *widget, int width, int height) {
    const qreal ratio = widget->devicePixelRatioF();
    return {int(std::ceil(width / ratio)), int(std::ceil(height / ratio))};
}

Flame smallFlame() {
    auto flame = demo_flame();
    flame.width = 80;
    flame.height = 64;
    flame.scale = 20;
    return flame;
}

RenderOptions smallRender() {
    RenderOptions options;
    options.samples = 35000;
    options.seed = 18446744073709551609ULL;
    options.threads = 1;
    return options;
}
} // namespace

class RenderProofDialogTests : public QObject {
    Q_OBJECT
private slots:
    void samplesOnceForToneAndTransparency() {
        const auto flame = smallFlame();
        const auto options = smallRender();
        auto calls = std::make_shared<std::atomic_int>();
        RenderProofDialog dialog(flame, options, true, nullptr, false,
            [calls](const Flame &f, const RenderOptions &o, ProofRegion region) {
                ++*calls;
                return sample_render_proof(f, o, region);
            });
        dialog.show();
        auto *render = field<QPushButton>(dialog, "renderProofCrop");
        auto *apply = field<QPushButton>(dialog, "useProofTone");
        auto *view = field<QWidget>(dialog, "proofCanvas");
        QVERIFY(field<QLabel>(dialog, "proofSamplingSettings")->text().contains(QString::number(options.seed)));
        QVERIFY(!apply->isEnabled());
        render->click();
        QTRY_VERIFY_WITH_TIMEOUT(apply->isEnabled(), 8000);
        QCOMPARE(calls->load(), 1);
        const auto image = view->grab().toImage();
        QCOMPARE(view->size(), logicalSizeForPixels(view, flame.width, flame.height));
        auto *gamma = field<QLineEdit>(dialog, "proofGamma");
        auto *brightness = field<QLineEdit>(dialog, "proofBrightness");
        gamma->setText("0");
        QVERIFY(!apply->isEnabled());
        gamma->setText("3.75");
        brightness->setText("12");
        QTRY_VERIFY_WITH_TIMEOUT(apply->isEnabled(), 5000);
        const auto retuned = view->grab().toImage();
        QVERIFY(image != retuned);
        QCOMPARE(calls->load(), 1);
        field<QCheckBox>(dialog, "proofTransparency")->setChecked(false);
        QTRY_VERIFY_WITH_TIMEOUT(apply->isEnabled(), 5000);
        QVERIFY(retuned != view->grab().toImage());
        QCOMPARE(calls->load(), 1);
        apply->click();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(dialog.gamma(), 3.75);
        QCOMPARE(dialog.brightness(), 12.0);
        QCOMPARE(dialog.vibrancy(), flame.vibrancy);
    }

    void boundedCropAndRestart() {
        auto flame = smallFlame();
        flame.width = 2048;
        flame.height = 1400;
        const auto options = smallRender();
        RenderProofDialog dialog(flame, options, false);
        auto *width = field<QSpinBox>(dialog, "proofWidth");
        auto *x = field<QSpinBox>(dialog, "proofX");
        auto *y = field<QSpinBox>(dialog, "proofY");
        auto *height = field<QSpinBox>(dialog, "proofHeight");
        auto *apply = field<QPushButton>(dialog, "useProofTone");
        QCOMPARE(width->maximum(), 1024);
        QCOMPARE(height->maximum(), 1024);
        QCOMPARE(x->value(), (2048 - 256) / 2);
        width->setValue(28);
        height->setValue(24);
        x->setValue(2048);
        y->setValue(1400);
        QCOMPARE(x->value(), 2048 - 28);
        QCOMPARE(y->value(), 1400 - 24);
        auto *render = field<QPushButton>(dialog, "renderProofCrop");
        render->click();
        QTRY_VERIFY_WITH_TIMEOUT(apply->isEnabled(), 5000);
        field<QPushButton>(dialog, "centerProofCrop")->click();
        QCOMPARE(x->value(), (2048 - 28) / 2);
        QCOMPARE(y->value(), (1400 - 24) / 2);
        QVERIFY(!apply->isEnabled());
        render->click();
        QTRY_VERIFY_WITH_TIMEOUT(apply->isEnabled(), 5000);
        dialog.reject();
        QCOMPARE(dialog.result(), int(QDialog::Rejected));
    }

    void replacingAnActiveCropUsesTheLatestSettings() {
        struct Gate { QSemaphore entered, release; std::atomic_int calls{0}; std::atomic_bool cancelled{false}; };
        const auto gate = std::make_shared<Gate>();
        RenderProofDialog dialog(smallFlame(), smallRender(), false, nullptr, false,
            [gate](const Flame &flame, const RenderOptions &options, ProofRegion region) {
                if (++gate->calls == 1) {
                    gate->entered.release();
                    gate->release.tryAcquire(1, 8000);
                    gate->cancelled.store(options.stop_token.stop_requested());
                }
                return sample_render_proof(flame, options, region);
            });
        dialog.show();
        auto *render = field<QPushButton>(dialog, "renderProofCrop");
        auto *apply = field<QPushButton>(dialog, "useProofTone");
        render->click();
        QTRY_VERIFY_WITH_TIMEOUT(gate->entered.available() > 0, 5000);
        field<QSpinBox>(dialog, "proofWidth")->setValue(32);
        field<QSpinBox>(dialog, "proofHeight")->setValue(21);
        render->click();
        field<QLineEdit>(dialog, "proofGamma")->setText("1.8");
        QVERIFY(!apply->isEnabled());
        gate->release.release();
        QTRY_VERIFY_WITH_TIMEOUT(apply->isEnabled(), 8000);
        QCOMPARE(gate->calls.load(), 2);
        QVERIFY(gate->cancelled.load());
        auto *canvas = field<QWidget>(dialog, "proofCanvas");
        QCOMPARE(canvas->size(), logicalSizeForPixels(canvas, 32, 21));
        QCOMPARE(dialog.gamma(), 1.8);
    }

    void closeWhileSampling() {
        struct Gate { QSemaphore entered, release, cancelled; };
        const auto gate = std::make_shared<Gate>();
        auto dialog = std::make_unique<RenderProofDialog>(smallFlame(), smallRender(), true, nullptr, false,
            [gate](const Flame &, const RenderOptions &o, ProofRegion) -> std::shared_ptr<const RenderProof> {
                gate->entered.release();
                gate->release.tryAcquire(1, 8000);
                if (o.stop_token.stop_requested()) gate->cancelled.release();
                throw RenderCancelled();
            });
        field<QPushButton>(*dialog, "renderProofCrop")->click();
        QTRY_VERIFY_WITH_TIMEOUT(gate->entered.available() > 0, 5000);
        QElapsedTimer elapsed;
        elapsed.start();
        dialog->reject();
        dialog.reset();
        const auto closeMs = elapsed.elapsed();
        gate->release.release();
        QVERIFY2(closeMs < 750, "Closing waited for a sampling worker");
        QTRY_VERIFY_WITH_TIMEOUT(gate->cancelled.available() > 0, 5000);
    }
};
QTEST_MAIN(RenderProofDialogTests)
#include "render_proof_dialog_tests.moc"
