#include "editor/variation_explorer.hpp"
#include "flame_io.hpp"
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSemaphore>
#include <QTest>
#include <QToolButton>
#include <algorithm>
#include <mutex>

using namespace apo;
using namespace apo::editor;

namespace {
template<class T> T *widget(QObject &dialog, const char *name) {
    auto *result = dialog.findChild<T *>(QString::fromLatin1(name));
    if (!result) qFatal("Missing explorer control %s", name);
    return result;
}
const std::string xml =
    "<flame name='Original' size='200 150' scale='45' version='Apophysis 7X'>"
    "<xform weight='1' linear='1' color='.5' coefs='.5 .2 -.1 .5 -.2 .3'/>"
    "<finalxform linear='.5' julian='.7' julian_power='3' julian_dist='1' "
    "blob_low='.123' coefs='1 0 0 1 0 0'/>"
    "<palette count='256' format='RGB'>" + std::string(256 * 6, '9') + "</palette>"
    "<!-- retained -->"
    "</flame>";
PreviewSettings quick() {
    PreviewSettings settings;
    settings.seed = 18446744073709551609ULL;
    settings.sample_limit = 3000;
    return settings;
}
struct Calls {
    std::mutex mutex;
    std::vector<std::string> models;
    std::vector<std::uint64_t> seeds;
};
RenderResult colorPatch() {
    RenderResult result;
    result.image = {2, 1, {254, 18, 12, 45, 50, 150}};
    return result;
}
} // namespace

class VariationExplorerTests : public QObject {
    Q_OBJECT
  private slots:
    void catalogAndSingleParameterRoundTrip() {
        auto source = LegacyDocument::parse(xml);
        const auto calls = std::make_shared<Calls>();
        VariationExplorerDialog dialog(source, 0, 0, true, Variation::julian, nullptr, quick(),
            [calls](const Flame &flame, const RenderOptions &options) {
                std::lock_guard lock(calls->mutex);
                calls->models.push_back(serialize_flame(flame));
                calls->seeds.push_back(options.seed);
                return colorPatch();
            });
        auto *browser = widget<QListWidget>(dialog, "variationBrowser");
        QCOMPARE(std::size_t(browser->count()), variation_registry().size());
        for (const auto &variation : variation_registry()) {
            const auto entries = browser->findItems(QString::fromStdString(std::string(variation.name)), Qt::MatchExactly);
            QCOMPARE(entries.size(), 1);
            QVERIFY(!entries.front()->icon().isNull());
            QVERIFY(!entries.front()->toolTip().isEmpty());
        }
        auto *control = widget<QComboBox>(dialog, "sweepControl");
        const auto power = control->findData("julian_power");
        QVERIFY(power > 0);
        control->setCurrentIndex(power);
        widget<QLineEdit>(dialog, "sweepFrom")->setText("2");
        widget<QLineEdit>(dialog, "sweepTo")->setText("4");
        auto *tile = widget<QToolButton>(dialog, "sweepTile3");
        QTRY_VERIFY_WITH_TIMEOUT(tile->isEnabled(), 5000);
        QCOMPARE(tile->text(), "4");
        QVERIFY(widget<QToolButton>(dialog, "sweepTile4")->isHidden());
        QCOMPARE(widget<QToolButton>(dialog, "sweepTile2")->toolTip(), "Already set to this value");
        tile->click();
        widget<QPushButton>(dialog, "sweepApply")->click();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(source.bytes(), xml);
        const auto edit = dialog.selectedEdit();
        QCOMPARE(edit.variation_parameters.size(), std::size_t(1));
        QVERIFY(!edit.variations);
        source.edit_transform(0, 0, edit, true);
        QVERIFY(source.bytes().find("<!-- retained -->") != std::string::npos);
        QCOMPARE(source.render_model(0).final_transform->parameters.julian_power, 4);
        const auto expected = serialize_flame(source.render_model(0));
        std::lock_guard lock(calls->mutex);
        QVERIFY(!calls->models.empty());
        // Source dimensions match the thumbnail viewport, so even camera and
        // oversampling stay identical to the model that the renderer received.
        QCOMPARE(calls->models.back(), expected);
        QVERIFY(std::all_of(calls->seeds.begin(), calls->seeds.end(),
                            [](auto seed) { return seed == quick().seed; }));
    }

    void initializesOnlyNewTermsAndRetainsDormantFields() {
        auto source = LegacyDocument::parse(xml);
        auto calls = std::make_shared<Calls>();
        VariationExplorerDialog dialog(source, 0, 0, true, Variation::blob, nullptr, quick(),
            [calls](const Flame &flame, const RenderOptions &) {
                std::lock_guard lock(calls->mutex);
                calls->models.push_back(serialize_flame(flame));
                return colorPatch();
            });
        auto control = widget<QComboBox>(dialog, "sweepControl");
        QCOMPARE(control->count(), 1); // first enable the variation by its weight
        auto *tile = widget<QToolButton>(dialog, "sweepTile5");
        QTRY_VERIFY_WITH_TIMEOUT(tile->isEnabled(), 5000);
        tile->click();
        source.edit_transform(0, 0, dialog.selectedEdit(), true);
        const auto expected = source.render_model(0);
        QCOMPARE(expected.final_transform->parameters.blob_low, .123);
        QCOMPARE(expected.final_transform->parameters.blob_waves, 5.0);
        std::lock_guard lock(calls->mutex);
        QCOMPARE(calls->models.back(), serialize_flame(expected));
    }

    void boundedRangesClearStaleSelections() {
        auto source = LegacyDocument::parse(xml);
        VariationExplorerDialog dialog(source, 0, 0, true, Variation::julian, nullptr, quick());
        auto control = widget<QComboBox>(dialog, "sweepControl");
        control->setCurrentIndex(control->findData("julian_power"));
        auto *from = widget<QLineEdit>(dialog, "sweepFrom");
        auto *to = widget<QLineEdit>(dialog, "sweepTo");
        from->setText("-2");
        to->setText("2");
        auto *first = widget<QToolButton>(dialog, "sweepTile1");
        auto *invalid = widget<QToolButton>(dialog, "sweepTile3");
        QTRY_VERIFY_WITH_TIMEOUT(widget<QToolButton>(dialog, "sweepTile5")->isEnabled(), 5000);
        QVERIFY(!invalid->isEnabled()); // the power may not be zero
        QVERIFY(invalid->toolTip().contains("nonzero"));
        first->click();
        QVERIFY(widget<QPushButton>(dialog, "sweepApply")->isEnabled());
        from->setText("-1.25");
        QVERIFY(!widget<QPushButton>(dialog, "sweepApply")->isEnabled());
        QTRY_VERIFY_WITH_TIMEOUT(widget<QLabel>(dialog, "sweepStatus")->text().contains("whole"), 3000);
        auto *search = widget<QLineEdit>(dialog, "variationExplorerSearch");
        search->setText("none matches");
        QVERIFY(!widget<QListWidget>(dialog, "variationBrowser")->currentItem());
        search->clear();
        auto *category = widget<QComboBox>(dialog, "variationExplorerCategory");
        category->setCurrentText("Scatter");
        auto *browser = widget<QListWidget>(dialog, "variationBrowser");
        QVERIFY(browser->currentItem());
        QCOMPARE(browser->currentItem()->data(Qt::UserRole + 1).toString(), "Scatter");
        QCOMPARE(source.bytes(), xml);
    }

    void closeWithRendererInFlight() {
        struct Gate { QSemaphore entered, release, returned; };
        const auto gate = std::make_shared<Gate>();
        const auto source = LegacyDocument::parse(xml);
        auto dialog = std::make_unique<VariationExplorerDialog>(source, 0, 0, false,
            Variation::swirl, nullptr, quick(), [gate](const Flame &, const RenderOptions &options) {
                gate->entered.release();
                // Bound the wait even on a failing test.
                gate->release.tryAcquire(1, 5000);
                gate->returned.release(options.stop_token.stop_requested() ? 1 : 0);
                return colorPatch();
            });
        QTRY_VERIFY_WITH_TIMEOUT(gate->entered.available() > 0, 5000);
        QElapsedTimer timer;
        timer.start();
        dialog->reject();
        dialog.reset();
        const auto elapsed = timer.elapsed();
        gate->release.release();
        QVERIFY2(elapsed < 750, "Closing waited for the background render");
        QTRY_VERIFY_WITH_TIMEOUT(gate->returned.available() > 0, 5000);
    }
};
QTEST_MAIN(VariationExplorerTests)
#include "variation_explorer_tests.moc"
