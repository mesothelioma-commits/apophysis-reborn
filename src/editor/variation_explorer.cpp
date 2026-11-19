#include "variation_explorer.hpp"
#include "background_jobs.hpp"
#include "creative.hpp"

#include <QButtonGroup>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>

namespace apo::editor {
namespace {
Q_APPLICATION_STATIC(ApplicationJobs, sweepPreparation)

struct CatalogEntry {
    Variation id;
    const char *category;
    const char *description;
};
using enum Variation;
constexpr CatalogEntry catalog[] = {
    {linear, "Geometry", "Passes points through the affine transform."},
    {sinusoidal, "Waves and folds", "Bends each coordinate with a sine wave."},
    {spherical, "Radial", "Inverts distance from the center, exchanging near and far points."},
    {swirl, "Radial", "Twists points through an angle that depends on their distance from the center."},
    {horseshoe, "Radial", "Folds the plane into a horseshoe around the origin."},
    {polar, "Radial", "Turns angle and distance into horizontal and vertical coordinates."},
    {handkerchief, "Waves and folds", "Folds the radius and angle into rippling fabric-like bands."},
    {heart, "Radial", "Bends radial lines into a heart-like shape."},
    {disc, "Radial", "Winds points around a disc using their radius and angle."},
    {spiral, "Radial", "Combines angle and inverse radius into winding arms."},
    {hyperbolic, "Radial", "Stretches one angular component by the radius and divides the other by it."},
    {diamond, "Radial", "Combines radial waves and angle to form diamond-like folds."},
    {ex, "Waves and folds", "Mixes cubed sine and cosine waves into curved lobes."},
    {julia, "Branches", "Picks between two square-root branches and rotates the result."},
    {bent, "Geometry", "Stretches negative X and compresses negative Y."},
    {waves, "Waves and folds", "Warps each axis with sine waves tied to the affine coefficients."},
    {fisheye, "Geometry", "Swaps axes and bends points through a circular lens."},
    {popcorn, "Waves and folds", "Adds cross-axis sine and tangent ripples tied to the affine offsets."},
    {exponential, "Radial", "Wraps Y around a circle with an exponential scale along X."},
    {power, "Radial", "Raises the radius to an angle-dependent power."},
    {cosine, "Waves and folds", "Combines circular waves in X with hyperbolic stretching in Y."},
    {rings, "Radial", "Repeats radial bands with spacing tied to the affine offset."},
    {fan, "Radial", "Alternately rotates angular sectors according to the affine offsets."},
    {blob, "Radial", "Modulates the radius between low and high scales around a series of lobes."},
    {pdj, "Waves and folds", "Combines four adjustable sine and cosine waves across the two axes."},
    {fan2, "Radial", "Splits the plane into fan sectors with adjustable angle and offset."},
    {rings2, "Radial", "Repeats circular bands with adjustable spacing."},
    {eyefish, "Geometry", "Compresses distant points toward a circular boundary."},
    {bubble, "Geometry", "Bends radial distance into a bounded bubble."},
    {cylinder, "Geometry", "Wraps X through a sine curve while retaining Y."},
    {perspective, "Geometry", "Tilts the plane using a projection angle and distance."},
    {noise, "Scatter", "Scales the input by a random radius and angle."},
    {julian, "Branches", "Chooses a Julia branch using adjustable power and radial distance."},
    {juliascope, "Branches", "Alternates reflected Julia branches using power and distance."},
    {blur, "Scatter", "Scatters points inside a disc independently of their input position."},
    {gaussian_blur, "Scatter", "Scatters points with a distribution concentrated near the center."},
    {radial_blur, "Scatter", "Combines random angular and radial displacement."},
    {pie, "Scatter", "Distributes points into slices with adjustable rotation and thickness."},
    {ngon, "Geometry", "Shapes radial distances around polygon sides and corners."},
    {curl, "Waves and folds", "Bends the plane with a two-coefficient complex mapping."},
    {rectangles, "Waves and folds", "Reflects points within rectangular cells."},
    {foci, "Geometry", "Maps points around focal regions using exponential and periodic terms."},
    {pre_blur, "Scatter", "Scatters the shared input before the other variations run."}
};

QString text(std::string_view value) {
    return QString::fromUtf8(value.data(), qsizetype(value.size()));
}
QString number(double value) {
    char buffer[64];
    const auto result = std::to_chars(std::begin(buffer), std::end(buffer), value);
    return QString::fromLatin1(buffer, qsizetype(result.ptr - buffer));
}

// Each icon plots the same colored lattice through one built-in mapping. A fixed
// sample affine exposes the effects that use its coefficients (waves, rings, fan).
QIcon mappingIcon(Variation id) {
    QPixmap icon(82, 58);
    icon.fill(QColor("#14212b"));
    QPainter painter(&icon);
    Rng rng(140);
    auto parameters = creative::authoring_parameters();
    parameters.julian_power = 3;
    const Affine affine{.85, .25, -.4, .7, .5, .32};
    for (int y = -11; y <= 11; ++y)
        for (int x = -16; x <= 16; ++x) {
            const Point point{x * .085, y * .095};
            Point mapped = apply_variation(id, point, rng, parameters, affine);
            if (id == Variation::pre_blur) {
                mapped.x += point.x;
                mapped.y += point.y;
            }
            if (!(std::abs(mapped.x) < 2.1 && std::abs(mapped.y) < 1.5))
                continue;
            painter.setPen(QColor::fromHsv(160 + (x + 16) * 3, 140 + (y + 11) * 3, 225));
            painter.drawPoint(QPointF(41 + mapped.x * 20, 29 - mapped.y * 20));
        }
    return QIcon(icon);
}

double weightOf(const Transform &transform, Variation id) {
    for (const auto &term : transform.variations)
        if (term.id == id)
            return term.weight;
    return 0;
}
} // namespace

VariationExplorerDialog::VariationExplorerDialog(const LegacyDocument &document, std::size_t flameIndex,
        std::size_t transformIndex, bool final, Variation initial, QWidget *parent,
        const PreviewSettings &settings, PreviewController::Renderer renderer)
    : QDialog(parent),
      source_(std::make_shared<const LegacyDocument>(LegacyDocument::parse(document.standalone_flame(flameIndex)))),
      transformIndex_(transformIndex), final_(final),
      previews_(this, PreviewController::Purpose::thumbnail, std::move(renderer)),
      preparation_(sweepPreparation->jobs),
      receiver_(std::make_shared<BackgroundReceiver<VariationExplorerDialog>>(this)) {
    const auto flame = source_->render_model(0);
    original_ = final ? flame.final_transform.value() : flame.transforms.at(transformIndex);
    auto options = settings;
    options.sample_limit = std::min(options.sample_limit, UINT64_C(180000));
    options.threads = std::min(options.threads ? options.threads : 2u, 2u);
    options.debounce_ms = 0;
    previews_.setSettings(options);
    debounce_.setSingleShot(true);
    debounce_.setInterval(120);
    connect(&debounce_, &QTimer::timeout, this, &VariationExplorerDialog::startSweep);
    connect(&previews_, &PreviewController::imageReady, this,
            [this](const QImage &image, quint64, quint64, quint64, unsigned, qint64, bool complete) {
                if (complete) finishTile(image);
            });
    connect(&previews_, &PreviewController::failed, this,
            [this](const QString &message) { finishTile({}, message); });

    setWindowTitle(tr("Variation explorer"));
    setObjectName("variationExplorerDialog");
    auto layout = new QVBoxLayout(this);
    auto panels = new QHBoxLayout;
    layout->addLayout(panels, 1);
    auto browse = new QVBoxLayout;
    panels->addLayout(browse, 1);
    search_ = new QLineEdit;
    search_->setObjectName("variationExplorerSearch");
    search_->setPlaceholderText(tr("Find a variation…"));
    browse->addWidget(search_);
    category_ = new QComboBox;
    category_->setObjectName("variationExplorerCategory");
    category_->addItems({tr("All categories"), "Geometry", "Radial", "Waves and folds", "Branches", "Scatter"});
    browse->addWidget(category_);
    browser_ = new QListWidget;
    browser_->setObjectName("variationBrowser");
    browser_->setIconSize({64, 44});
    browser_->setAccessibleName(tr("Visual variation catalog"));
    browse->addWidget(browser_, 1);
    int firstRow = 0;
    for (const auto &entry : catalog) {
        const auto name = text(variation_name(entry.id));
        auto item = new QListWidgetItem(mappingIcon(entry.id), name, browser_);
        item->setData(Qt::UserRole, unsigned(entry.id));
        item->setData(Qt::UserRole + 1, entry.category);
        item->setToolTip(entry.description);
        if (entry.id == initial) firstRow = browser_->row(item);
    }

    auto compare = new QVBoxLayout;
    panels->addLayout(compare, 3);
    auto title = new QLabel(QStringLiteral("%1   ·   %2").arg(text(flame.name),
        final ? tr("Final transform") : tr("Transform %1").arg(transformIndex + 1)));
    title->setTextFormat(Qt::PlainText);
    title->setWordWrap(true);
    compare->addWidget(title);
    description_ = new QLabel;
    description_->setWordWrap(true);
    compare->addWidget(description_);
    auto form = new QGridLayout;
    compare->addLayout(form);
    control_ = new QComboBox;
    control_->setObjectName("sweepControl");
    low_ = new QLineEdit;
    low_->setObjectName("sweepFrom");
    high_ = new QLineEdit;
    high_->setObjectName("sweepTo");
    form->addWidget(new QLabel(tr("Compare")), 0, 0);
    form->addWidget(new QLabel(tr("From")), 0, 1);
    form->addWidget(new QLabel(tr("To")), 0, 2);
    form->addWidget(control_, 1, 0);
    form->addWidget(low_, 1, 1);
    form->addWidget(high_, 1, 2);
    form->setColumnStretch(0, 2);
    form->setColumnStretch(1, 1);
    form->setColumnStretch(2, 1);

    auto grid = new QGridLayout;
    compare->addLayout(grid, 1);
    choices_ = new QButtonGroup(this);
    for (int i = 0; i < int(tiles_.size()); ++i) {
        auto tile = new QToolButton;
        tile->setObjectName(QStringLiteral("sweepTile%1").arg(i));
        tile->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        tile->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        tile->setIconSize({165, 120});
        tile->setCheckable(i > 0);
        if (i > 0) choices_->addButton(tile, i);
        grid->addWidget(tile, i / 3, i % 3);
        tiles_[i] = tile;
    }
    status_ = new QLabel;
    status_->setObjectName("sweepStatus");
    status_->setWordWrap(true);
    compare->addWidget(status_);
    compare->addWidget(new QLabel(tr("All previews use seed %1 and the same camera.").arg(options.seed)));
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    apply_ = buttons->addButton(tr("Apply selected value"), QDialogButtonBox::AcceptRole);
    apply_->setObjectName("sweepApply");
    apply_->setAutoDefault(false);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(choices_, &QButtonGroup::idClicked, this, [this](int id) {
        selected_ = id;
        apply_->setEnabled(true);
    });
    connect(search_, &QLineEdit::textChanged, this, &VariationExplorerDialog::filterCatalog);
    connect(category_, &QComboBox::currentIndexChanged, this, &VariationExplorerDialog::filterCatalog);
    connect(browser_, &QListWidget::currentItemChanged, this, &VariationExplorerDialog::selectVariation);
    connect(control_, &QComboBox::currentIndexChanged, this, &VariationExplorerDialog::setDefaultRange);
    connect(low_, &QLineEdit::textChanged, this, &VariationExplorerDialog::schedule);
    connect(high_, &QLineEdit::textChanged, this, &VariationExplorerDialog::schedule);
    browser_->setCurrentRow(firstRow);
    const QSize preferred(950, 570);
    resize(screen() ? preferred.boundedTo(screen()->availableGeometry().size() - QSize(30, 60)) : preferred);
}

VariationExplorerDialog::~VariationExplorerDialog() {
    receiver_->close();
    preparation_->cancel(this);
}

void VariationExplorerDialog::done(int result) {
    if (result == Accepted && selected_ < 1) return;
    ++generation_;
    debounce_.stop();
    preparation_->cancel(this);
    previews_.stop();
    QDialog::done(result);
}

Variation VariationExplorerDialog::variation() const {
    return static_cast<Variation>(browser_->currentItem()->data(Qt::UserRole).toUInt());
}

const VariationParameterInfo *VariationExplorerDialog::parameter() const {
    return control_->currentIndex() > 0
        ? find_variation_parameter(control_->currentData().toString().toStdString()) : nullptr;
}

double VariationExplorerDialog::currentValue() const {
    if (const auto *field = parameter()) return field->get(original_.parameters);
    return weightOf(original_, variation());
}

LegacyTransformEdits VariationExplorerDialog::editFor(double value) const {
    LegacyTransformEdits edit;
    if (const auto *field = parameter()) {
        edit.variation_parameters.emplace(field->name, value);
    } else {
        edit.variations = original_.variations;
        auto &terms = *edit.variations;
        const auto id = variation();
        const auto term = std::find_if(terms.begin(), terms.end(), [id](const Term &t) { return t.id == id; });
        if (term != terms.end()) term->weight = value;
        else if (value != 0) terms.push_back({id, value});
        // Match the weight editor's deterministic defaults when authoring a new term.
        edit.initialize_new_variations = true;
    }
    return edit;
}

LegacyTransformEdits VariationExplorerDialog::selectedEdit() const {
    return editFor(values_.at(std::size_t(selected_)));
}

void VariationExplorerDialog::filterCatalog() {
    QListWidgetItem *first = nullptr;
    for (int i = 0; i < browser_->count(); ++i) {
        auto *item = browser_->item(i);
        const bool show = item->text().contains(search_->text().trimmed(), Qt::CaseInsensitive)
            && (!category_->currentIndex() || item->data(Qt::UserRole + 1).toString() == category_->currentText());
        item->setHidden(!show);
        if (show && !first) first = item;
    }
    if (!browser_->currentItem() || browser_->currentItem()->isHidden())
        browser_->setCurrentItem(first);
}

void VariationExplorerDialog::selectVariation() {
    invalidate();
    const QSignalBlocker blocker(control_);
    control_->clear();
    const auto *item = browser_->currentItem();
    control_->setEnabled(item != nullptr);
    low_->setEnabled(item != nullptr);
    high_->setEnabled(item != nullptr);
    if (!item) {
        description_->setText(tr("No matching variations."));
        status_->clear();
        return;
    }
    control_->addItem(tr("Weight"));
    const auto id = variation();
    const bool active = weightOf(original_, id) != 0;
    bool hasParameters = false;
    for (const auto &field : variation_parameter_registry()) {
        if (field.variation != id) continue;
        hasParameters = true;
        if (active) control_->addItem(text(field.name), text(field.name));
    }
    auto description = item->text() + QStringLiteral(" — ") + item->toolTip();
    if (hasParameters && !active)
        description += tr(" Apply a nonzero weight first to explore its parameters.");
    description_->setText(description);
    setDefaultRange();
}

void VariationExplorerDialog::setDefaultRange() {
    if (!browser_->currentItem()) return;
    const auto *field = parameter();
    const double value = currentValue();
    const bool integral = field && field->is_integer(original_.parameters);
    const double spread = integral ? 2 : std::max(.5, std::abs(value) * .5);
    double low = value - spread, high = value + spread;
    if (field) {
        try { field->validate(original_.parameters, 0); }
        catch (const std::invalid_argument &) {
            if (value > 0 && low <= 0) { low = value; high = value + (integral ? 4 : spread); }
            if (value < 0 && high >= 0) { high = value; low = value - (integral ? 4 : spread); }
        }
        if (integral) {
            low = std::max(low, double(std::numeric_limits<int>::min()));
            high = std::min(high, double(std::numeric_limits<int>::max()));
        }
    } else if (value == 0) {
        low = 0;
        high = 1;
    }
    const QSignalBlocker lowBlock(low_), highBlock(high_);
    low_->setText(number(low));
    high_->setText(number(high));
    schedule();
}

void VariationExplorerDialog::invalidate() {
    ++generation_;
    preparation_->cancel(this);
    previews_.stop();
    debounce_.stop();
    values_.clear();
    next_ = 0;
    selected_ = -1;
    choices_->setExclusive(false);
    for (auto tile : tiles_) {
        tile->setChecked(false);
        tile->setEnabled(false);
        tile->setIcon({});
        tile->setText({});
        tile->setToolTip({});
        tile->setAccessibleDescription({});
    }
    choices_->setExclusive(true);
    apply_->setEnabled(false);
}

void VariationExplorerDialog::schedule() {
    invalidate();
    if (browser_->currentItem()) debounce_.start();
}

void VariationExplorerDialog::startSweep() {
    bool validLow, validHigh;
    const double low = QLocale::c().toDouble(low_->text(), &validLow);
    const double high = QLocale::c().toDouble(high_->text(), &validHigh);
    const auto *field = parameter();
    const bool integral = field && field->is_integer(original_.parameters);
    if (!validLow || !validHigh || !std::isfinite(low) || !std::isfinite(high) || low >= high ||
        (integral && (std::round(low) != low || std::round(high) != high ||
              low < std::numeric_limits<int>::min() || high > std::numeric_limits<int>::max()))) {
        status_->setText(integral ? tr("Enter two increasing whole numbers in the supported integer range.")
                                  : tr("Enter two increasing, finite numbers."));
        return;
    }
    values_.push_back(currentValue());
    for (int i = 0; i < 5; ++i) {
        double value = std::lerp(low, high, i / 4.0);
        if (integral) {
            value = std::round(value);
        } else if (i > 0 && i < 4) {
            // Use the readable decimal for both the render and the actual edit.
            // Keep full precision when a narrow range needs the extra digits.
            const double rounded = QString::number(value, 'g', 15).toDouble();
            if (rounded > low && rounded < high)
                value = rounded;
        }
        if (values_.size() == 1 || value != values_.back()) values_.push_back(value);
    }
    for (std::size_t i = 0; i < tiles_.size(); ++i) {
        tiles_[i]->setVisible(i < values_.size());
        if (i < values_.size())
            tiles_[i]->setText(i == 0 ? tr("Current · %1").arg(number(values_[i])) : number(values_[i]));
    }
    status_->setText(tr("Rendering comparison…"));
    prepareNext();
}

void VariationExplorerDialog::prepareNext() {
    if (next_ >= values_.size()) {
        status_->setText(tr("Select a preview to apply its value."));
        return;
    }
    // Parse and patch in the background through the exact same producer-specific
    // path as Apply. At most one prepared candidate and one render are outstanding.
    const auto generation = generation_;
    const auto edit = next_ ? editFor(values_[next_]) : LegacyTransformEdits{};
    preparation_->submit(this, [source = source_, edit, index = transformIndex_, final = final_,
                                generation, receiver = receiver_](std::stop_token stop) {
        try {
            auto candidate = *source;
            if (stop.stop_requested()) return;
            if (edit.variations || !edit.variation_parameters.empty())
                candidate.edit_transform(0, index, edit, final);
            auto model = candidate.render_model(0);
            if (stop.stop_requested()) return;
            receiver->post([generation, model = std::move(model)](VariationExplorerDialog *dialog) {
                if (dialog->generation_ == generation) dialog->previews_.request(model, {200, 150});
            });
        } catch (const std::exception &error) {
            const auto message = QString::fromUtf8(error.what());
            receiver->post([generation, message](VariationExplorerDialog *dialog) {
                if (dialog->generation_ == generation) dialog->finishTile({}, message);
            });
        }
    });
}

void VariationExplorerDialog::finishTile(const QImage &image, const QString &error) {
    if (next_ >= values_.size()) return;
    auto *tile = tiles_[next_];
    if (error.isEmpty() && !image.isNull()) {
        tile->setIcon(QIcon(QPixmap::fromImage(image)));
        tile->setEnabled(true);
        if (next_ == 0) tile->setToolTip(tr("Current appearance"));
        if (next_ && values_[next_] == values_.front())
            tile->setToolTip(tr("Already set to this value"));
    } else {
        tile->setText(tr("%1 · unavailable").arg(number(values_[next_])));
        tile->setToolTip(error);
        tile->setAccessibleDescription(error);
    }
    ++next_;
    prepareNext();
}

} // namespace apo::editor
