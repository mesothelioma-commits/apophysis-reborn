#include "creative_widgets.hpp"
#include "color_curve_dialog.hpp"
#include "document_session.hpp"
#include "flame_io.hpp"
#include "legacy_formats.hpp"
#include "plugin.hpp"
#include "output_file.hpp"
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QScreen>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QUuid>
#include <QVBoxLayout>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>

namespace apo::editor {
namespace {
QString text(std::string_view s) {
    return QString::fromUtf8(s.data(), qsizetype(s.size()));
}
QString number(double value) {
    char buffer[128];
    const auto result = std::to_chars(std::begin(buffer), std::end(buffer), value);
    return QString::fromLatin1(buffer, qsizetype(result.ptr - buffer));
}
bool equal(RGB a, RGB b) {
    return a.r == b.r && a.g == b.g && a.b == b.b;
}
bool equal(const Palette &a, const Palette &b) {
    return std::equal(a.begin(), a.end(), b.begin(), [](RGB x, RGB y) { return equal(x, y); });
}
bool equal(const Affine &a, const Affine &b) {
    return a.a == b.a && a.b == b.b && a.c == b.c && a.d == b.d && a.e == b.e && a.f == b.f;
}
bool equal(const std::vector<Term> &a, const std::vector<Term> &b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](const Term &x, const Term &y) {
               if (x.id != y.id || x.weight != y.weight || term_name(x) != term_name(y) ||
                   x.plugin_parameters != y.plugin_parameters || bool(x.plugin) != bool(y.plugin))
                   return false;
               if (!x.plugin)
                   return true;
               const auto &left = x.plugin->info();
               const auto &right = y.plugin->info();
               return left.id == right.id && left.version == right.version &&
                      left.fingerprint == right.fingerprint;
           });
}
QColor color(RGB value) {
    return QColor::fromRgbF(value.r, value.g, value.b);
}
RGB rgb(const QColor &value) {
    return {value.redF(), value.greenF(), value.blueF()};
}
QSpinBox *integer(const char *name, int low, int high, int value) {
    auto field = new QSpinBox;
    field->setObjectName(name);
    field->setRange(low, high);
    field->setValue(value);
    field->setKeyboardTracking(false);
    return field;
}
QDoubleSpinBox *decimal(const char *name, double low, double high, double value, double step = .05) {
    auto field = new QDoubleSpinBox;
    field->setObjectName(name);
    field->setRange(low, high);
    field->setDecimals(4);
    field->setSingleStep(step);
    field->setValue(value);
    field->setKeyboardTracking(false);
    return field;
}
QCheckBox *check(const QString &title, const char *name, bool value) {
    auto result = new QCheckBox(title);
    result->setObjectName(name);
    result->setChecked(value);
    return result;
}
QPushButton *button(const QString &title, const char *name) {
    auto result = new QPushButton(title);
    result->setObjectName(name);
    result->setAutoDefault(false);
    return result;
}
std::uint64_t seedValue(const QLineEdit *field) {
    const auto s = field->text().trimmed().toLatin1();
    std::uint64_t seed = 0;
    const auto converted = std::from_chars(s.data(), s.data() + s.size(), seed);
    if (s.isEmpty() || converted.ec != std::errc{} || converted.ptr != s.data() + s.size())
        throw std::invalid_argument("Seed must be an integer from 0 to 18446744073709551615");
    return seed;
}
double fieldValue(const QLineEdit *field) {
    bool ok = false;
    const double value = QLocale::c().toDouble(field->text().trimmed(), &ok);
    if (!ok || !std::isfinite(value))
        throw std::invalid_argument("Enter a finite number for " +
                                    field->accessibleName().toStdString());
    return value;
}
void fitDialog(QDialog *dialog, QSize desired) {
    const auto available =
        dialog->screen() ? dialog->screen()->availableGeometry().size() - QSize(40, 70) : desired;
    dialog->resize(desired.boundedTo(available));
}
void configurePreview(PreviewController &preview, PreviewSettings settings,
                      std::uint64_t samples = 400000) {
    settings.sample_limit = std::min(settings.sample_limit, samples);
    if (!settings.threads)
        settings.threads = 2;
    settings.debounce_ms = 80;
    preview.setSettings(settings);
}
QLabel *rendererCaption(PreviewController &preview, QWidget *parent) {
    auto label = new QLabel("Preparing preview…", parent);
    label->setObjectName("previewRenderer");
    label->setWordWrap(true);
    QObject::connect(&preview, &PreviewController::backendResolved, label,
                     [label](const QString &backend, const QString &device, const QString &fallback) {
                         label->setText(!fallback.isEmpty()   ? "CPU fallback · " + fallback
                                        : backend == "Vulkan" ? "Vulkan FP32 · " + device
                                                              : "CPU reference preview");
                         label->setToolTip(device + (fallback.isEmpty() ? QString{} : "\n" + fallback));
                     });
    return label;
}
QString paletteFolder() {
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/palettes";
}
std::string readSmallFile(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error(file.errorString().toStdString());
    if (file.size() > 2 * 1024 * 1024)
        throw std::runtime_error("Palette files must be no larger than 2 MiB");
    const auto bytes = file.readAll();
    if (file.error() != QFile::NoError)
        throw std::runtime_error(file.errorString().toStdString());
    return bytes.toStdString();
}
QWidget *scrollPanel(QWidget *body) {
    auto area = new QScrollArea;
    area->setWidget(body);
    area->setWidgetResizable(true);
    area->setFrameShape(QFrame::NoFrame);
    return area;
}
} // namespace

PaletteStrip::PaletteStrip(QWidget *parent) : QWidget(parent) {
    setMinimumHeight(68);
    setMinimumWidth(256);
    setCursor(Qt::CrossCursor);
    setToolTip("Click or drag to choose an individual palette index from 0 to 255.");
}
void PaletteStrip::setColors(const Palette &colors) {
    colors_ = colors;
    update();
}
void PaletteStrip::setIndex(int index) {
    index_ = std::clamp(index, 0, 255);
    update();
}
void PaletteStrip::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    QImage image(256, 1, QImage::Format_RGB32);
    for (int i = 0; i < 256; ++i)
        image.setPixelColor(i, 0, color(colors_[i]));
    painter.drawImage(QRect(0, 4, width(), 40), image);
    const int x = qRound(index_ * (width() - 1) / 255.0);
    painter.setPen(QPen(Qt::black, 3));
    painter.drawLine(x, 2, x, 46);
    painter.setPen(Qt::white);
    painter.drawLine(x, 2, x, 46);
    painter.drawText(QRect(0, 47, width(), 20), Qt::AlignCenter, QString("Index %1 / 255").arg(index_));
}
void PaletteStrip::choose(double x) {
    const int value = std::clamp(qRound(x * 255 / std::max(1, width() - 1)), 0, 255);
    setIndex(value);
    emit indexSelected(value);
}
void PaletteStrip::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton)
        choose(event->position().x());
}
void PaletteStrip::mouseMoveEvent(QMouseEvent *event) {
    if (event->buttons() & Qt::LeftButton)
        choose(event->position().x());
}

GradientDialog::GradientDialog(const Flame &flame, QWidget *parent, const PreviewSettings &settings)
    : QDialog(parent), flame_(flame), preview_(this), history_{flame.palette} {
    setObjectName("gradientDialog");
    setWindowTitle("Gradient editor");
    fitDialog(this, {1080, 730});
    configurePreview(preview_, settings);
    auto layout = new QVBoxLayout(this);
    auto top = new QHBoxLayout;
    presets_ = new QComboBox;
    presets_->setObjectName("palettePresets");
    presets_->setMinimumWidth(190);
    top->addWidget(new QLabel("Palette"));
    top->addWidget(presets_, 1);
    auto usePreset = button("Use preset", "usePalettePreset");
    auto import = button("Import…", "importPalette");
    auto exportPalette = button("Export…", "exportPalette");
    auto savePreset = button("Save to library…", "savePalettePreset");
    for (auto control : {usePreset, import, exportPalette, savePreset})
        top->addWidget(control);
    layout->addLayout(top);
    auto splitter = new QSplitter;
    canvas_ = new PreviewCanvas;
    canvas_->setObjectName("gradientPreview");
    canvas_->setMessage("Rendering gradient preview…");
    canvas_->setMinimumSize(300, 240);
    splitter->addWidget(canvas_);
    auto tabs = new QTabWidget;
    tabs->setObjectName("gradientTabs");
    splitter->addWidget(tabs);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    layout->addWidget(splitter, 1);
    auto individual = new QWidget;
    auto form = new QFormLayout(individual);
    index_ = integer("paletteIndex", 0, 255, 0);
    red_ = integer("paletteRed", 0, 255, 0);
    green_ = integer("paletteGreen", 0, 255, 0);
    blue_ = integer("paletteBlue", 0, 255, 0);
    hue_ = integer("paletteHue", 0, 359, 0);
    saturation_ = integer("paletteSaturation", 0, 100, 0);
    value_ = integer("paletteValue", 0, 100, 0);
    hslHue_ = integer("paletteHslHue", 0, 359, 0);
    hslSaturation_ = integer("paletteHslSaturation", 0, 100, 0);
    lightness_ = integer("paletteLightness", 0, 100, 0);
    form->addRow("Palette index", index_);
    form->addRow("Red", red_);
    form->addRow("Green", green_);
    form->addRow("Blue", blue_);
    form->addRow("HSV hue °", hue_);
    form->addRow("HSV saturation %", saturation_);
    form->addRow("HSV value %", value_);
    form->addRow("HSL hue °", hslHue_);
    form->addRow("HSL saturation %", hslSaturation_);
    form->addRow("HSL lightness %", lightness_);
    auto pick = button("Choose color…", "choosePaletteColor");
    form->addRow(pick);
    paletteMode_ = new QComboBox;
    paletteMode_->setObjectName("paletteInterpolation");
    paletteMode_->addItems({"Step / indexed", "Linear between indices"});
    paletteMode_->setCurrentIndex(flame.palette_mode == PaletteMode::linear ? 1 : 0);
    paletteMode_->setToolTip("Controls how a rendered color coordinate samples the stored 256 colors.");
    form->addRow("Render interpolation", paletteMode_);
    tabs->addTab(scrollPanel(individual), "Individual color");
    auto stopsPanel = new QWidget;
    auto stopsLayout = new QVBoxLayout(stopsPanel);
    auto stopsHelp = new QLabel("Choose control colors and indices, then build a gradient. Existing "
                                "palette colors stay unchanged until Build.");
    stopsHelp->setWordWrap(true);
    stopsLayout->addWidget(stopsHelp);
    stops_ = new QTableWidget(0, 4);
    stops_->setObjectName("paletteStops");
    stops_->setHorizontalHeaderLabels({"Index", "Red", "Green", "Blue"});
    stops_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    stops_->verticalHeader()->hide();
    stops_->setSelectionBehavior(QAbstractItemView::SelectRows);
    stopsLayout->addWidget(stops_, 1);
    auto stopActions = new QHBoxLayout;
    auto addStop = button("Add index", "addPaletteStop");
    auto deleteStop = button("Remove", "removePaletteStop");
    auto sampleStops = button("Sample palette", "samplePaletteStops");
    for (auto control : {addStop, deleteStop, sampleStops})
        stopActions->addWidget(control);
    stopsLayout->addLayout(stopActions);
    space_ = new QComboBox;
    space_->setObjectName("gradientColorSpace");
    space_->addItems({"RGB interpolation", "HSV shortest hue path", "Linear-light sRGB", "HSL shortest hue path"});
    stopsLayout->addWidget(space_);
    auto build = button("Build gradient", "buildPaletteStops");
    stopsLayout->addWidget(build);
    tabs->addTab(stopsPanel, "Color stops");
    auto adjustments = new QWidget;
    auto adjustmentsForm = new QFormLayout(adjustments);
    auto hue = decimal("gradientHueShift", -360, 360, 0, 5);
    auto saturation = decimal("gradientSaturation", 0, 4, 1);
    auto brightness = decimal("gradientBrightness", 0, 4, 1);
    auto redGamma = decimal("gradientRedGamma", .05, 20, 1);
    auto greenGamma = decimal("gradientGreenGamma", .05, 20, 1);
    auto blueGamma = decimal("gradientBlueGamma", .05, 20, 1);
    auto contrast = decimal("gradientContrast", 0, 4, 1);
    adjustmentsForm->addRow("Hue rotation °", hue);
    adjustmentsForm->addRow("Saturation multiplier", saturation);
    adjustmentsForm->addRow("Value multiplier", brightness);
    adjustmentsForm->addRow("Red curve gamma", redGamma);
    adjustmentsForm->addRow("Green curve gamma", greenGamma);
    adjustmentsForm->addRow("Blue curve gamma", blueGamma);
    adjustmentsForm->addRow("Contrast", contrast);
    auto adjust = button("Apply color adjustment", "applyPaletteAdjustment");
    adjustmentsForm->addRow(adjust);
    auto paletteCurves = button("Palette curves…", "editPaletteCurves");
    adjustmentsForm->addRow(paletteCurves);
    auto curveHelp = new QLabel("These controls change the stored palette colors. Flame gamma and "
                                "brightness are in the Adjustment editor.");
    curveHelp->setWordWrap(true);
    adjustmentsForm->addRow(curveHelp);
    tabs->addTab(scrollPanel(adjustments), "Color adjustments");
    strip_ = new PaletteStrip;
    strip_->setObjectName("gradientStrip");
    layout->addWidget(strip_);
    auto operations = new QHBoxLayout;
    rotate_ = integer("paletteRotation", -255, 255, 16);
    rotate_->setMaximumWidth(90);
    auto rotate = button("Rotate", "rotatePalette");
    auto reverse = button("Reverse", "reversePalette");
    auto smooth = button("Smooth", "smoothPalette");
    auto random = button("Randomize", "randomizePalette");
    seed_ = new QLineEdit("123456");
    seed_->setObjectName("paletteSeed");
    seed_->setMaximumWidth(160);
    operations->addWidget(rotate_);
    for (auto control : {rotate, reverse, smooth})
        operations->addWidget(control);
    operations->addStretch();
    operations->addWidget(new QLabel("Seed"));
    operations->addWidget(seed_);
    operations->addWidget(random);
    layout->addLayout(operations);
    status_ = new QLabel;
    status_->setObjectName("gradientStatus");
    status_->setWordWrap(true);
    layout->addWidget(status_);
    auto bottom = new QHBoxLayout;
    undo_ = button("Undo palette edit", "undoPalette");
    redo_ = button("Redo", "redoPalette");
    auto reset = button("Reset to original", "resetPalette");
    bottom->addWidget(undo_);
    bottom->addWidget(redo_);
    bottom->addWidget(reset);
    bottom->addStretch();
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText("Apply gradient");
    buttons->button(QDialogButtonBox::Ok)->setObjectName("acceptGradient");
    bottom->addWidget(buttons);
    layout->addLayout(bottom);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(&preview_, &PreviewController::imageReady, this,
            [this](const QImage &image) { canvas_->setImage(image); });
    connect(&preview_, &PreviewController::statusChanged, status_, &QLabel::setText);
    connect(&preview_, &PreviewController::backendResolved, status_,
            [this](const QString &, const QString &device, const QString &fallback) {
                status_->setToolTip(device + (fallback.isEmpty() ? QString{} : "\n" + fallback));
            });
    connect(canvas_, &PreviewCanvas::viewportChanged, this, [this] { requestPreview(); });
    connect(strip_, &PaletteStrip::indexSelected, index_, &QSpinBox::setValue);
    connect(index_, &QSpinBox::valueChanged, this, [this](int index) {
        strip_->setIndex(index);
        refreshColor();
    });
    for (int channel = 0; auto field : {red_, green_, blue_}) {
        connect(field, &QSpinBox::valueChanged, this, [this, channel](int value) {
            if (syncing_)
                return;
            auto next = flame_.palette;
            auto &entry = next[index_->value()];
            if (channel == 0)
                entry.r = value / 255.0;
            else if (channel == 1)
                entry.g = value / 255.0;
            else
                entry.b = value / 255.0;
            setColors(next);
        });
        ++channel;
    }
    for (int channel = 0; auto field : {hue_, saturation_, value_}) {
        connect(field, &QSpinBox::valueChanged, this, [this, channel](int value) {
            if (syncing_)
                return;
            auto next = flame_.palette;
            const auto previous = color(next[index_->value()]);
            const auto hue = channel == 0 ? value / 360.0 : std::max(0.0, double(previous.hsvHueF()));
            const auto saturation = channel == 1 ? value / 100.0 : previous.hsvSaturationF();
            const auto brightness = channel == 2 ? value / 100.0 : previous.valueF();
            next[index_->value()] = rgb(QColor::fromHsvF(hue, saturation, brightness));
            setColors(next);
        });
        ++channel;
    }
    for (int channel = 0; auto field : {hslHue_, hslSaturation_, lightness_}) {
        connect(field, &QSpinBox::valueChanged, this, [this, channel](int value) {
            if (syncing_) return;
            auto next = flame_.palette;
            const auto previous = color(next[index_->value()]);
            const double hue = channel == 0 ? value / 360.0 : std::max(0.0, double(previous.hslHueF()));
            const double saturation = channel == 1 ? value / 100.0 : previous.hslSaturationF();
            const double lightness = channel == 2 ? value / 100.0 : previous.lightnessF();
            next[index_->value()] = rgb(QColor::fromHslF(hue, saturation, lightness));
            setColors(next);
        });
        ++channel;
    }
    connect(paletteCurves, &QPushButton::clicked, this, [this] {
        ColorCurves initial;
        initial.mode = ColorCurveMode::continuous;
        ColorCurveDialog dialog(initial, this);
        dialog.setWindowTitle("Palette curves — change stored colors");
        if (dialog.exec() == QDialog::Accepted)
            run([&] { setColors(creative::curve_palette(flame_.palette, dialog.resultCurves())); });
    });
    connect(pick, &QPushButton::clicked, this, [this] {
        const auto chosen =
            QColorDialog::getColor(color(flame_.palette[index_->value()]), this, "Palette color");
        if (chosen.isValid()) {
            auto next = flame_.palette;
            next[index_->value()] = rgb(chosen);
            setColors(next);
        }
    });
    connect(paletteMode_, &QComboBox::currentIndexChanged, this, [this](int mode) {
        flame_.palette_mode = mode ? PaletteMode::linear : PaletteMode::step;
        requestPreview();
    });
    connect(rotate, &QPushButton::clicked, this,
            [this] { setColors(creative::rotate_palette(flame_.palette, rotate_->value())); });
    connect(reverse, &QPushButton::clicked, this,
            [this] { setColors(creative::reverse_palette(flame_.palette)); });
    connect(smooth, &QPushButton::clicked, this,
            [this] { setColors(creative::smooth_palette(flame_.palette)); });
    connect(random, &QPushButton::clicked, this,
            [this] { run([&] { setColors(creative::random_palette(seedValue(seed_))); }); });
    connect(reset, &QPushButton::clicked, this, [this, flame] {
        setColors(flame.palette);
        paletteMode_->setCurrentIndex(flame.palette_mode == PaletteMode::linear ? 1 : 0);
    });
    connect(undo_, &QPushButton::clicked, this, [this] {
        if (historyIndex_)
            setColors(history_[--historyIndex_], false);
    });
    connect(redo_, &QPushButton::clicked, this, [this] {
        if (historyIndex_ + 1 < history_.size())
            setColors(history_[++historyIndex_], false);
    });
    connect(adjust, &QPushButton::clicked, this, [=, this] {
        run([&] {
            setColors(creative::adjust_palette(
                flame_.palette, {hue->value(),
                                 saturation->value(),
                                 brightness->value(),
                                 {redGamma->value(), greenGamma->value(), blueGamma->value()},
                                 contrast->value()}));
        });
    });
    connect(addStop, &QPushButton::clicked, this, [this] {
        const int row = stops_->rowCount();
        stops_->insertRow(row);
        const auto selected = color(flame_.palette[index_->value()]);
        const std::array values{index_->value(), selected.red(), selected.green(), selected.blue()};
        for (int col = 0; col < 4; ++col)
            stops_->setItem(row, col, new QTableWidgetItem(QString::number(values[col])));
        stops_->setCurrentCell(row, 0);
    });
    connect(deleteStop, &QPushButton::clicked, this, [this] {
        if (stops_->currentRow() >= 0)
            stops_->removeRow(stops_->currentRow());
    });
    connect(sampleStops, &QPushButton::clicked, this, &GradientDialog::fillStops);
    connect(build, &QPushButton::clicked, this, [this] { run([&] { buildStops(); }); });
    connect(usePreset, &QPushButton::clicked, this, [this] {
        run([&] {
            const auto path = presets_->currentData().toString();
            if (!path.isEmpty())
                importPaletteFile(path);
            else if (presets_->currentIndex() >= 0 &&
                     presets_->currentIndex() < int(presetsData_.size()))
                setColors(presetsData_[presets_->currentIndex()].colors);
        });
    });
    connect(import, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(
            this, "Import palette", {},
            "Palettes and flames (*.gpl *.ugr *.map *.flame *.xml *.temp);;GIMP palettes (*.gpl);;Ultra Fractal gradients (*.ugr);;Fractint palettes (*.map);;All files (*)");
        if (!path.isEmpty())
            run([&] { importPaletteFile(path); });
    });
    connect(exportPalette, &QPushButton::clicked, this, [this] {
        QString selectedFilter;
        auto path = QFileDialog::getSaveFileName(
            this, "Export palette", "palette",
            "GIMP palettes (*.gpl);;Ultra Fractal gradients (*.ugr);;Fractint palettes (*.map)",
            &selectedFilter);
        if (!path.isEmpty())
            run([&] {
                if (QFileInfo(path).suffix().isEmpty())
                    path += selectedFilter.contains("*.ugr") ? ".ugr"
                            : selectedFilter.contains("*.map") ? ".map" : ".gpl";
                exportPaletteFile(path);
            });
    });
    connect(savePreset, &QPushButton::clicked, this, [this] {
        bool ok = false;
        auto name = QInputDialog::getText(this, "Save palette to library", "Palette name",
                                          QLineEdit::Normal, "My palette", &ok)
                        .trimmed();
        if (!ok || name.isEmpty())
            return;
        run([&] {
            if (!QDir().mkpath(paletteFolder()))
                throw std::runtime_error("Cannot create the palette library folder");
            name.replace(QRegularExpression("[^A-Za-z0-9 _-]"), "_");
            name = name.left(80);
            const auto path = paletteFolder() + '/' + name + '-' +
                              QUuid::createUuid().toString(QUuid::WithoutBraces).left(8) + ".gpl";
            exportPaletteFile(path);
            loadPresets();
            presets_->setCurrentIndex(presets_->findData(path));
            status_->setText("Palette saved to your local palette library.");
        });
    });
    loadPresets();
    setColors(flame.palette, false);
    fillStops();
}

GradientDialog::~GradientDialog() {
    // QWidget may move focus while destroying child editors. Disconnect before
    // our palette history and worker members are destroyed.
    for (auto child : findChildren<QObject *>())
        QObject::disconnect(child, nullptr, this, nullptr);
    preview_.stop();
}
void GradientDialog::run(const std::function<void()> &operation) {
    try {
        operation();
    } catch (const std::exception &error) {
        preview_.stop();
        status_->setText(text(error.what()));
    }
}
void GradientDialog::setColors(const Palette &colors, bool remember) {
    if (remember && !equal(flame_.palette, colors)) {
        history_.resize(historyIndex_ + 1);
        history_.push_back(colors);
        if (history_.size() > 100)
            history_.erase(history_.begin());
        historyIndex_ = history_.size() - 1;
    }
    flame_.palette = colors;
    strip_->setColors(colors);
    undo_->setEnabled(historyIndex_ > 0);
    redo_->setEnabled(historyIndex_ + 1 < history_.size());
    refreshColor();
    requestPreview();
}
void GradientDialog::refreshColor() {
    syncing_ = true;
    const auto selected = color(flame_.palette[index_->value()]);
    red_->setValue(selected.red());
    green_->setValue(selected.green());
    blue_->setValue(selected.blue());
    hue_->setValue(std::max(0, selected.hsvHue()));
    saturation_->setValue(qRound(selected.hsvSaturationF() * 100));
    value_->setValue(qRound(selected.valueF() * 100));
    hslHue_->setValue(std::max(0, selected.hslHue()));
    hslSaturation_->setValue(qRound(selected.hslSaturationF() * 100));
    lightness_->setValue(qRound(selected.lightnessF() * 100));
    syncing_ = false;
}
void GradientDialog::requestPreview() {
    preview_.request(flame_, canvas_->size());
}
void GradientDialog::loadPresets() {
    presets_->clear();
    presetsData_ = creative::palette_presets();
    for (const auto &preset : presetsData_)
        presets_->addItem(text(preset.name));
    const QDir folder(paletteFolder());
    for (const auto &name : folder.entryList({"*.gpl"}, QDir::Files, QDir::Name))
        presets_->addItem("My library / " + QFileInfo(name).completeBaseName(), folder.filePath(name));
}
void GradientDialog::fillStops() {
    const std::array indices{0, 51, 102, 153, 204, 255};
    stops_->setRowCount(int(indices.size()));
    for (int row = 0; row < int(indices.size()); ++row) {
        const auto c = color(flame_.palette[indices[row]]);
        const std::array values{indices[row], c.red(), c.green(), c.blue()};
        for (int col = 0; col < 4; ++col)
            stops_->setItem(row, col, new QTableWidgetItem(QString::number(values[col])));
    }
}
void GradientDialog::buildStops() {
    std::vector<creative::ColorStop> stops;
    for (int row = 0; row < stops_->rowCount(); ++row) {
        std::array<int, 4> values;
        for (int col = 0; col < 4; ++col) {
            bool ok = false;
            values[col] =
                stops_->item(row, col) ? stops_->item(row, col)->text().trimmed().toInt(&ok) : -1;
            if (!ok || values[col] < 0 || values[col] > 255)
                throw std::runtime_error(
                    "Stop indices and RGB channels must be integers from 0 to 255.");
        }
        stops.push_back({values[0], {values[1] / 255.0, values[2] / 255.0, values[3] / 255.0}});
    }
    setColors(creative::palette_from_stops(stops, static_cast<creative::GradientSpace>(space_->currentIndex())));
}
void GradientDialog::importPaletteFile(const QString &path) {
    const auto suffix = QFileInfo(path).suffix().toLower();
    if (suffix == "gpl") {
        setColors(creative::import_gimp_palette(readSmallFile(path)));
        return;
    }
    if (suffix == "map") {
        setColors(import_fractint_map(readSmallFile(path)));
        return;
    }
    if (suffix == "ugr") {
        const auto entries = import_ugr(readSmallFile(path));
        QStringList names, issues;
        std::vector<std::size_t> indices;
        for (std::size_t i = 0; i < entries.size(); ++i) {
            const auto label = QString::number(i + 1) + " · " + text(entries[i].name);
            if (entries[i].report.supported()) {
                indices.push_back(i);
                names << label;
            } else {
                QStringList reasons;
                for (const auto &error : entries[i].report.errors)
                    reasons << text(error);
                issues << label + ": " + reasons.join("; ");
            }
        }
        if (indices.empty())
            throw std::invalid_argument(("No compatible UGR gradients. " + issues.join("\n")).toStdString());
        int chosen = 0;
        if (entries.size() > 1) {
            bool ok = false;
            const auto item = QInputDialog::getItem(
                this, "Choose UGR palette", "Compatible gradients", names, 0, false, &ok);
            if (!ok)
                return;
            chosen = names.indexOf(item);
        }
        const auto &entry = entries[indices.at(chosen)];
        setColors(entry.palette);
        QStringList details;
        details << "Imported " + text(entry.name) + ".";
        for (const auto &warning : entry.report.warnings)
            details << text(warning);
        if (!issues.isEmpty())
            details << "Skipped unsupported gradients:\n" + issues.join("\n");
        status_->setText(details.join("\n"));
        return;
    }
    const auto document = LegacyDocument::load(filesystemPath(path));
    QStringList names;
    std::vector<std::size_t> indices;
    for (std::size_t i = 0; i < document.entries().size(); ++i)
        if (!document.render_issue(i)) {
            indices.push_back(i);
            names << QString::number(i + 1) + " · " + text(document.entries()[i].name);
        }
    if (indices.empty())
        throw std::runtime_error("This file has no supported flame with an embedded palette.");
    int chosen = 0;
    if (indices.size() > 1) {
        bool ok = false;
        const auto item =
            QInputDialog::getItem(this, "Choose flame palette", "Flame", names, 0, false, &ok);
        if (!ok)
            return;
        chosen = names.indexOf(item);
    }
    setColors(document.render_model(indices.at(chosen)).palette);
}
void GradientDialog::exportPaletteFile(const QString &path) const {
    const auto suffix = QFileInfo(path).suffix().toLower();
    const auto name = QFileInfo(path).completeBaseName().toStdString();
    std::string bytes;
    if (suffix == "ugr")
        bytes = export_ugr(flame_.palette, name);
    else if (suffix == "map")
        bytes = export_fractint_map(flame_.palette, name);
    else if (suffix == "gpl" || suffix.isEmpty())
        bytes = creative::export_gimp_palette(flame_.palette, name);
    else
        throw std::invalid_argument("Choose a .gpl, .ugr or .map palette filename.");
    write_atomic(filesystemPath(path), [&](std::ostream &out) { out << bytes; });
}

AdjustmentDialog::AdjustmentDialog(const Flame &flame, QWidget *parent, const PreviewSettings &settings)
    : QDialog(parent), original_(flame), flame_(flame), preview_(this),
      aspectRatio_(double(flame.width) / flame.height) {
    setObjectName("adjustmentDialog");
    setWindowTitle("Adjustment editor");
    fitDialog(this, {980, 680});
    configurePreview(preview_, settings);
    auto layout = new QVBoxLayout(this);
    auto split = new QSplitter;
    auto tabs = new QTabWidget;
    tabs->setObjectName("adjustmentTabs");
    split->addWidget(tabs);
    canvas_ = new PreviewCanvas;
    canvas_->setObjectName("adjustmentPreview");
    canvas_->setMessage("Rendering adjustment preview…");
    canvas_->setMinimumSize(300, 240);
    split->addWidget(canvas_);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 1);
    layout->addWidget(split, 1);
    auto camera = new QWidget;
    auto cameraForm = new QFormLayout(camera);
    auto tone = new QWidget;
    auto toneForm = new QFormLayout(tone);
    auto rendering = new QWidget;
    auto renderingForm = new QFormLayout(rendering);
    auto addField = [&](QFormLayout *form, const char *key, const QString &label, const QString &help) {
        auto field = new QLineEdit;
        field->setObjectName(QString("adjust_") + key);
        field->setAccessibleName(label);
        field->setToolTip(help);
        field->setAlignment(Qt::AlignRight);
        fields_[key] = field;
        form->addRow(label, field);
        connect(field, &QLineEdit::editingFinished, this, [this] { updateFlame(); });
    };
    width_ = integer("adjust_width", 1, std::numeric_limits<int>::max(), flame.width);
    height_ = integer("adjust_height", 1, std::numeric_limits<int>::max(), flame.height);
    aspect_ = check("Keep aspect ratio", "adjust_aspect", false);
    cameraForm->addRow("Image width", width_);
    cameraForm->addRow("Image height", height_);
    cameraForm->addRow(aspect_);
    addField(cameraForm, "center_x", "Center X", "Horizontal position of the saved camera.");
    addField(cameraForm, "center_y", "Center Y", "Vertical position of the saved camera.");
    addField(cameraForm, "scale", "Scale",
             "Pixels per world unit at the saved output size, before zoom.");
    addField(cameraForm, "zoom", "Zoom",
             "Logarithmic camera zoom. Each increase of 1 doubles the image scale.");
    addField(cameraForm, "rotate", "Rotation °",
             "Camera rotation in degrees. Historical angle fields retain their source convention.");
    auto fit = button("Frame visible orbit", "fitFlameCamera");
    fit->setToolTip("Estimate a camera from 98% of a deterministic orbit, excluding extreme outliers.");
    cameraForm->addRow(fit);
    auto presets = new QComboBox;
    presets->setObjectName("outputSizePreset");
    presets->addItems({"Choose output size…", "768 × 576", "1920 × 1080", "2560 × 1440", "3840 × 2160",
                       "4096 × 4096", "7680 × 4320", "16384 × 16384"});
    cameraForm->addRow("Size preset", presets);
    auto sizeHelp = new QLabel("Export checks the available buffer budget. Large output sizes may "
                               "require the later tiled renderer.");
    sizeHelp->setWordWrap(true);
    cameraForm->addRow(sizeHelp);
    addField(toneForm, "gamma", "Gamma",
             "Positive gamma used by the selected producer's image reconstruction.");
    addField(toneForm, "gamma_threshold", "Gamma threshold",
             "Nonnegative threshold for the renderer's low-density gamma transition.");
    addField(toneForm, "brightness", "Brightness", "Nonnegative density brightness multiplier.");
    addField(toneForm, "vibrancy", "Vibrancy",
             "Blend between independent-channel gamma and color-preserving gamma, from 0 to 1.");
    background_ = button("Choose background…", "adjust_background");
    toneForm->addRow("Background", background_);
    auto toneCurves = button("Tone curves…", "editToneCurves");
    toneForm->addRow(toneCurves);
    auto paletteHelp =
        new QLabel("Tone curves affect the reconstructed image. Use the Gradient editor to change stored palette colors.");
    paletteHelp->setWordWrap(true);
    toneForm->addRow(paletteHelp);
    addField(renderingForm, "quality", "Sample density",
             "Requested samples per nominal output pixel. Zoom contributes to the default image-export "
             "budget.");
    oversample_ = integer("adjust_oversample", 1, std::numeric_limits<int>::max(), flame.oversample);
    renderingForm->addRow("Oversample", oversample_);
    oversample_->setToolTip(
        "Histogram grid multiplier. The live preview uses 1; file rendering uses this saved value.");
    filter_ = new QComboBox;
    filter_->setObjectName("adjust_filter");
    filter_->addItems({"Source default", "Gaussian filter"});
    filter_->setCurrentIndex(flame.filter_radius.has_value() ? 1 : 0);
    renderingForm->addRow("Reconstruction", filter_);
    addField(renderingForm, "filter_radius", "Filter radius",
             "Nonnegative Gaussian filter radius in the supported renderer.");
    auto filterHelp =
        new QLabel("Source default removes the stored filter override. Apophysis sources use their "
                   "historical Gaussian default; generic sources use box reconstruction.");
    filterHelp->setWordWrap(true);
    renderingForm->addRow(filterHelp);
    tabs->addTab(scrollPanel(camera), "Camera");
    tabs->addTab(scrollPanel(tone), "Tone / color");
    tabs->addTab(scrollPanel(rendering), "Rendering");
    status_ = new QLabel;
    status_->setObjectName("adjustmentStatus");
    status_->setWordWrap(true);
    layout->addWidget(status_);
    layout->addWidget(rendererCaption(preview_, this));
    auto bottom = new QHBoxLayout;
    auto reset = button("Reset to original", "resetAdjustments");
    bottom->addWidget(reset);
    bottom->addStretch();
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    apply_ = buttons->button(QDialogButtonBox::Ok);
    apply_->setText("Apply adjustments");
    apply_->setObjectName("acceptAdjustments");
    bottom->addWidget(buttons);
    layout->addLayout(bottom);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (updateFlame())
            accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(&preview_, &PreviewController::imageReady, this,
            [this](const QImage &image) { canvas_->setImage(image); });
    connect(&preview_, &PreviewController::failed, this,
            [this](const QString &message) { status_->setText(message); });
    connect(canvas_, &PreviewCanvas::viewportChanged, this, [this] { requestPreview(); });
    auto sizeEdited = [this](bool horizontal) {
        if (syncing_)
            return;
        if (aspect_->isChecked()) {
            const double value =
                horizontal ? width_->value() / aspectRatio_ : height_->value() * aspectRatio_;
            auto other = horizontal ? height_ : width_;
            const QSignalBlocker block(other);
            other->setValue(
                int(std::clamp(std::round(value), 1.0, double(std::numeric_limits<int>::max()))));
        }
        updateFlame();
    };
    connect(width_, &QSpinBox::valueChanged, this, [sizeEdited] { sizeEdited(true); });
    connect(height_, &QSpinBox::valueChanged, this, [sizeEdited] { sizeEdited(false); });
    connect(aspect_, &QCheckBox::toggled, this, [this](bool enabled) {
        if (enabled)
            aspectRatio_ = double(width_->value()) / height_->value();
    });
    connect(oversample_, &QSpinBox::valueChanged, this, [this] { updateFlame(); });
    connect(filter_, &QComboBox::currentIndexChanged, this, [this] { updateFlame(); });
    connect(background_, &QPushButton::clicked, this, [this] {
        const auto chosen = QColorDialog::getColor(color(flame_.background), this, "Flame background");
        if (chosen.isValid()) {
            flame_.background = rgb(chosen);
            updateFlame();
        }
    });
    connect(toneCurves, &QPushButton::clicked, this, [this] {
        if (!updateFlame()) return;
        ColorCurveDialog dialog(flame_.curves, this);
        if (dialog.exec() == QDialog::Accepted) {
            flame_.curves = dialog.resultCurves();
            updateFlame();
        }
    });
    connect(reset, &QPushButton::clicked, this, [this] {
        flame_ = original_;
        aspectRatio_ = double(original_.width) / original_.height;
        const QSignalBlocker block(filter_);
        filter_->setCurrentIndex(flame_.filter_radius.has_value() ? 1 : 0);
        refreshFields();
        updateFlame();
    });
    connect(fit, &QPushButton::clicked, this, [this] {
        if (!updateFlame())
            return;
        try {
            auto next = flame_;
            if (!creative::fit_camera(next, 123456))
                throw std::runtime_error(
                    "This orbit has too little finite geometry to frame automatically.");
            flame_ = std::move(next);
            refreshFields();
            requestPreview();
        } catch (const std::exception &error) {
            status_->setText(text(error.what()));
        }
    });
    connect(presets, &QComboBox::currentIndexChanged, this, [this](int index) {
        constexpr std::array<QSize, 7> sizes{{{768, 576},
                                              {1920, 1080},
                                              {2560, 1440},
                                              {3840, 2160},
                                              {4096, 4096},
                                              {7680, 4320},
                                              {16384, 16384}}};
        if (index < 1)
            return;
        syncing_ = true;
        width_->setValue(sizes[index - 1].width());
        height_->setValue(sizes[index - 1].height());
        aspectRatio_ = double(width_->value()) / height_->value();
        syncing_ = false;
        updateFlame();
    });
    refreshFields();
    updateFlame();
}
AdjustmentDialog::~AdjustmentDialog() {
    for (auto child : findChildren<QObject *>())
        QObject::disconnect(child, nullptr, this, nullptr);
    preview_.stop();
}
void AdjustmentDialog::refreshFields() {
    syncing_ = true;
    const std::map<std::string, double> values{{"center_x", flame_.center.x},
                                               {"center_y", flame_.center.y},
                                               {"scale", flame_.scale},
                                               {"zoom", flame_.zoom},
                                               {"rotate", flame_.rotate},
                                               {"gamma", flame_.gamma},
                                               {"gamma_threshold", flame_.gamma_threshold},
                                               {"brightness", flame_.brightness},
                                               {"vibrancy", flame_.vibrancy},
                                               {"quality", flame_.quality},
                                               {"filter_radius", flame_.filter_radius.value_or(.5)}};
    for (const auto &[key, value] : values) {
        fields_.at(key)->setText(number(value));
        fields_.at(key)->setCursorPosition(0);
    }
    width_->setValue(flame_.width);
    height_->setValue(flame_.height);
    oversample_->setValue(flame_.oversample);
    syncing_ = false;
}
bool AdjustmentDialog::updateFlame() {
    if (syncing_)
        return true;
    try {
        auto next = flame_;
        auto value = [&](const char *key) { return fieldValue(fields_.at(key)); };
        next.width = width_->value();
        next.height = height_->value();
        next.center = {value("center_x"), value("center_y")};
        next.scale = value("scale");
        next.zoom = value("zoom");
        next.rotate = value("rotate");
        next.gamma = value("gamma");
        next.gamma_threshold = value("gamma_threshold");
        next.brightness = value("brightness");
        next.vibrancy = value("vibrancy");
        next.quality = value("quality");
        next.oversample = oversample_->value();
        fields_.at("filter_radius")->setEnabled(filter_->currentIndex() == 1);
        if (filter_->currentIndex())
            next.filter_radius = value("filter_radius");
        else
            next.filter_radius =
                is_apophysis(next.source_version) ? std::optional<double>(.5) : std::nullopt;
        validate(next);
        flame_ = std::move(next);
        background_->setText(color(flame_.background).name(QColor::HexRgb));
        background_->setStyleSheet("border-left: 24px solid " + color(flame_.background).name() + ";");
        apply_->setEnabled(true);
        status_->setText("Changes are previewed here. Apply creates one undo step in the library.");
        requestPreview();
        return true;
    } catch (const std::exception &error) {
        status_->setText(text(error.what()));
        apply_->setEnabled(false);
        return false;
    }
}
void AdjustmentDialog::requestPreview() {
    preview_.request(flame_, canvas_->size());
}
LegacyEdits AdjustmentDialog::edits() const {
    LegacyEdits result;
    if (flame_.width != original_.width || flame_.height != original_.height)
        result.size = std::array{flame_.width, flame_.height};
    if (flame_.center.x != original_.center.x || flame_.center.y != original_.center.y)
        result.center = flame_.center;
    if (flame_.scale != original_.scale)
        result.scale = flame_.scale;
    if (flame_.zoom != original_.zoom)
        result.zoom = flame_.zoom;
    if (flame_.rotate != original_.rotate)
        result.rotate = flame_.rotate;
    if (flame_.gamma != original_.gamma)
        result.gamma = flame_.gamma;
    if (flame_.gamma_threshold != original_.gamma_threshold)
        result.gamma_threshold = flame_.gamma_threshold;
    if (flame_.brightness != original_.brightness)
        result.brightness = flame_.brightness;
    if (flame_.vibrancy != original_.vibrancy)
        result.vibrancy = flame_.vibrancy;
    if (flame_.quality != original_.quality)
        result.quality = flame_.quality;
    if (flame_.oversample != original_.oversample)
        result.oversample = flame_.oversample;
    if (!equal(flame_.background, original_.background))
        result.background = flame_.background;
    if (flame_.curves != original_.curves)
        result.curves = flame_.curves;
    if (!filter_->currentIndex() && original_.filter_radius)
        result.clear_filter_radius = true;
    else if (flame_.filter_radius != original_.filter_radius && flame_.filter_radius)
        result.filter_radius = *flame_.filter_radius;
    return result;
}

ExploreDialog::ExploreDialog(Mode mode, const Flame &parentFlame, QWidget *parent,
                             const PreviewSettings &settings)
    : QDialog(parent), mode_(mode), initial_(parentFlame), branches_{{parentFlame, -1}}, preview_(this),
      thumbnails_(this) {
    const bool mutation = mode == Mode::mutation;
    setObjectName(mutation ? "mutationDialog" : "randomFlameDialog");
    setWindowTitle(mutation ? "Mutation explorer" : "Random flame generator");
    fitDialog(this, {1280, 790});
    configurePreview(preview_, settings);
    configurePreview(thumbnails_, settings, 80000);
    auto thumbnailSettings = thumbnails_.settings();
    thumbnailSettings.debounce_ms = 0;
    thumbnails_.setSettings(thumbnailSettings);
    auto layout = new QVBoxLayout(this);
    auto explanation =
        new QLabel(mutation ? "Explore variations of your flame. Compare candidates, follow a branch, "
                              "then apply a result or keep it in the library."
                            : "Generate a reproducible collection. Choose the variations and geometry "
                              "limits, then keep one candidate or the complete batch.");
    explanation->setWordWrap(true);
    layout->addWidget(explanation);
    auto split = new QSplitter;
    auto controls = new QWidget;
    auto form = new QFormLayout(controls);
    seed_ = new QLineEdit("123456");
    seed_->setObjectName("exploreSeed");
    form->addRow("Seed", seed_);
    lockSeed_ = check("Lock seed when regenerating", "lockExploreSeed", true);
    form->addRow(lockSeed_);
    auto newSeed = button("Choose new seed", "newExploreSeed");
    form->addRow(newSeed);
    count_ = integer("exploreCount", 1, 24, 9);
    form->addRow("Candidates", count_);
    strength_ = decimal("mutationStrength", 0, 1, .25);
    trend_ = decimal("mutationTrend", -1, 1, 0);
    strength_->setToolTip(
        "0 reproduces the parent exactly; 1 uses the largest permitted perturbations.");
    trend_->setToolTip(
        "Bias numeric perturbations toward negative or positive changes. Zero is balanced.");
    geometry_ = check("Geometry", "mutateGeometry", true);
    variations_ = check("Variation weights / choices", "mutateVariations", false);
    colors_ = check("Transform color", "mutateColors", false);
    parameters_ = check("Variation parameters", "mutateParameters", false);
    palette_ = check(mutation ? "Palette" : "Random palettes", "mutatePalette", !mutation);
    final_ = check("Include final transform", "mutateFinal", false);
    minimum_ = integer("randomMinTransforms", 2, 12, 3);
    maximum_ = integer("randomMaxTransforms", 2, 12, 5);
    symmetry_ = integer("randomSymmetry", 1, 12, 1);
    reflection_ = check("Reflection symmetry", "randomReflection", false);
    minContraction_ = decimal("randomMinContraction", .01, .99, .35);
    maxContraction_ = decimal("randomMaxContraction", .01, .99, .78);
    translation_ = decimal("randomTranslation", .01, 10, 1.2);
    if (mutation) {
        form->addRow("Strength", strength_);
        form->addRow("Trend", trend_);
        for (auto toggle : {geometry_, variations_, colors_, parameters_, palette_, final_})
            form->addRow(toggle);
    } else {
        form->addRow("Minimum transforms", minimum_);
        form->addRow("Maximum transforms", maximum_);
        form->addRow("Rotation order", symmetry_);
        form->addRow(reflection_);
        form->addRow("Minimum contraction", minContraction_);
        form->addRow("Maximum contraction", maxContraction_);
        form->addRow("Translation extent", translation_);
        form->addRow(palette_);
    }
    // Controls unused by this mode are owned by the dialog without taking layout space.
    const std::vector<QWidget *> modeControls{
        strength_, trend_,   geometry_, variations_, colors_,         parameters_,     final_,
        minimum_,  maximum_, symmetry_, reflection_, minContraction_, maxContraction_, translation_};
    for (auto control : modeControls)
        if (!control->parent()) {
            control->setParent(this);
            control->hide();
        }
    allowlist_ = new QListWidget;
    allowlist_->setObjectName("generationVariations");
    allowlist_->setMinimumHeight(180);
    const std::vector<Variation> defaults{Variation::linear, Variation::sinusoidal, Variation::spherical,
                                          Variation::swirl,  Variation::horseshoe,  Variation::julia,
                                          Variation::bent,   Variation::bubble};
    for (const auto &v : variation_registry()) {
        auto item = new QListWidgetItem(text(v.name), allowlist_);
        item->setData(Qt::UserRole, unsigned(v.id));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(std::find(defaults.begin(), defaults.end(), v.id) != defaults.end()
                                ? Qt::Checked
                                : Qt::Unchecked);
    }
    form->addRow("Allowed variations", allowlist_);
    auto allVariations = new QHBoxLayout;
    auto selectAll = button("All", "allowAllVariations");
    auto selectNone = button("None", "allowNoVariations");
    allVariations->addWidget(selectAll);
    allVariations->addWidget(selectNone);
    form->addRow(allVariations);
    auto variationsHelp = new QLabel("Select at least one variation. Symmetry adds explicit linear "
                                     "branches outside the ordinary transform count.");
    variationsHelp->setWordWrap(true);
    form->addRow(variationsHelp);
    auto controlScroll = scrollPanel(controls);
    controlScroll->setMinimumWidth(235);
    controlScroll->setMaximumWidth(310);
    split->addWidget(controlScroll);
    auto candidates = new QWidget;
    grid_ = new QGridLayout(candidates);
    grid_->setAlignment(Qt::AlignTop);
    for (int column = 0; column < 3; ++column)
        grid_->setColumnStretch(column, 1);
    auto candidateScroll = scrollPanel(candidates);
    candidateScroll->setMinimumWidth(500);
    split->addWidget(candidateScroll);
    auto comparison = new QWidget;
    auto comparisonLayout = new QVBoxLayout(comparison);
    selectedName_ = new QLabel("Generating candidates…");
    selectedName_->setObjectName("selectedCandidateName");
    selectedName_->setWordWrap(true);
    comparisonLayout->addWidget(selectedName_);
    canvas_ = new PreviewCanvas;
    canvas_->setObjectName("candidatePreview");
    canvas_->setMessage("Choose a candidate to preview");
    canvas_->setMinimumSize(240, 220);
    comparisonLayout->addWidget(canvas_, 1);
    comparisonLayout->addWidget(rendererCaption(preview_, comparison));
    showParent_ = check("Compare with parent", "compareMutationParent", false);
    comparisonLayout->addWidget(showParent_);
    history_ = new QComboBox;
    history_->setObjectName("mutationHistory");
    history_->addItem("Original parent");
    comparisonLayout->addWidget(new QLabel("Mutation history"));
    comparisonLayout->addWidget(history_);
    useParent_ = button("Use candidate as parent", "useCandidateAsParent");
    comparisonLayout->addWidget(useParent_);
    if (!mutation) {
        showParent_->hide();
        history_->hide();
        useParent_->hide();
        // The comparison panel still shows the selected random flame at a larger size.
        for (auto label : comparison->findChildren<QLabel *>())
            if (label->text() == "Mutation history")
                label->hide();
    }
    split->addWidget(comparison);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 2);
    split->setStretchFactor(2, 1);
    split->setSizes({290, 520, 400});
    layout->addWidget(split, 1);
    status_ = new QLabel;
    status_->setObjectName("exploreStatus");
    status_->setWordWrap(true);
    layout->addWidget(status_);
    auto bottom = new QHBoxLayout;
    generate_ = button("Regenerate", "generateCandidates");
    bottom->addWidget(generate_);
    bottom->addStretch();
    apply_ = button("Apply to current", "applyMutation");
    addSelected_ = button("Add selected", "addSelectedCandidates");
    addAll_ = button("Add batch", "addAllCandidates");
    addBranch_ = button("Add branch history", "addMutationBranch");
    auto cancel = button("Cancel", "cancelExplore");
    for (auto control : {apply_, addSelected_, addAll_, addBranch_, cancel})
        bottom->addWidget(control);
    if (!mutation) {
        apply_->hide();
        addBranch_->hide();
    }
    layout->addLayout(bottom);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(generate_, &QPushButton::clicked, this, [this] { generate(true); });
    connect(newSeed, &QPushButton::clicked, this,
            [this] { seed_->setText(QString::number(QRandomGenerator::global()->generate64())); });
    connect(selectAll, &QPushButton::clicked, this, [this] {
        for (int i = 0; i < allowlist_->count(); ++i)
            allowlist_->item(i)->setCheckState(Qt::Checked);
    });
    connect(selectNone, &QPushButton::clicked, this, [this] {
        for (int i = 0; i < allowlist_->count(); ++i)
            allowlist_->item(i)->setCheckState(Qt::Unchecked);
    });
    connect(apply_, &QPushButton::clicked, this, [this] { chooseResult(Disposition::replace); });
    connect(addSelected_, &QPushButton::clicked, this, [this] { chooseResult(Disposition::append); });
    connect(addAll_, &QPushButton::clicked, this, [this] { chooseResult(Disposition::append, true); });
    connect(addBranch_, &QPushButton::clicked, this,
            [this] { chooseResult(Disposition::append, false, true); });
    connect(showParent_, &QCheckBox::toggled, this, [this] { showSelected(); });
    connect(useParent_, &QPushButton::clicked, this, [this] {
        if (selected_ < 0 || generating_)
            return;
        branches_.push_back({candidates_.at(selected_), parentBranch_});
        parentBranch_ = int(branches_.size()) - 1;
        {
            const QSignalBlocker block(history_);
            history_->addItem(QString("Branch %1 · %2")
                                  .arg(parentBranch_)
                                  .arg(text(branches_.back().flame.name).right(28)));
            history_->setCurrentIndex(parentBranch_);
        }
        showParent_->setChecked(false);
        generate(false);
    });
    connect(history_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index < 0 || index >= int(branches_.size()) || generating_)
            return;
        parentBranch_ = index;
        generate(false);
    });
    connect(&preview_, &PreviewController::imageReady, this,
            [this](const QImage &image) { canvas_->setImage(image); });
    connect(&preview_, &PreviewController::failed, status_, &QLabel::setText);
    connect(canvas_, &PreviewCanvas::viewportChanged, this, [this] { showSelected(); });
    connect(&thumbnails_, &PreviewController::imageReady, this,
            [this](const QImage &image, quint64, quint64 plotted, quint64, unsigned, qint64,
                   bool finalPass) {
                if (!finalPass || generating_ || thumbnail_ >= int(tiles_.size()))
                    return;
                tiles_[thumbnail_]->setIcon(QPixmap::fromImage(image));
                if (!plotted)
                    tiles_[thumbnail_]->setToolTip(
                        tiles_[thumbnail_]->toolTip() +
                        "\nNo samples reached this camera; adjust framing or choose another candidate.");
                ++thumbnail_;
                status_->setText(
                    QString("Rendered %1 of %2 thumbnails. Select a candidate to refine its preview.")
                        .arg(thumbnail_)
                        .arg(tiles_.size()));
                QTimer::singleShot(0, this, &ExploreDialog::nextThumbnail);
            });
    connect(&thumbnails_, &PreviewController::backendResolved, this,
            [this](const QString &backend, const QString &device, const QString &fallback) {
                if (generating_ || thumbnail_ >= int(tiles_.size()))
                    return;
                const auto details =
                    !fallback.isEmpty() ? "CPU fallback: " + fallback : backend + " · " + device;
                tiles_[thumbnail_]->setToolTip(tiles_[thumbnail_]->toolTip() + "\n" + details);
            });
    connect(&thumbnails_, &PreviewController::failed, this, [this](const QString &message) {
        if (generating_ || thumbnail_ >= int(tiles_.size()))
            return;
        tiles_[thumbnail_]->setToolTip(message);
        ++thumbnail_;
        QTimer::singleShot(0, this, &ExploreDialog::nextThumbnail);
    });
    updateActions();
    QTimer::singleShot(0, this, [this] { generate(false); });
}
ExploreDialog::~ExploreDialog() {
    for (auto child : findChildren<QObject *>())
        QObject::disconnect(child, nullptr, this, nullptr);
    if (generationThread_.joinable())
        generationThread_.join();
    preview_.stop();
    thumbnails_.stop();
}
void ExploreDialog::updateActions() {
    const bool selected = !generating_ && selected_ >= 0 && selected_ < int(candidates_.size());
    generate_->setEnabled(!generating_);
    history_->setEnabled(!generating_);
    useParent_->setEnabled(selected);
    apply_->setEnabled(selected);
    addSelected_->setEnabled(selected);
    addBranch_->setEnabled(selected);
    addAll_->setEnabled(!generating_ && !candidates_.empty());
}
void ExploreDialog::generate(bool advanceSeed) {
    if (generating_)
        return;
    try {
        auto seed = seedValue(seed_);
        if (advanceSeed && !lockSeed_->isChecked()) {
            ++seed;
            seed_->setText(QString::number(seed));
        }
        std::vector<Variation> allowed;
        for (int i = 0; i < allowlist_->count(); ++i)
            if (allowlist_->item(i)->checkState() == Qt::Checked)
                allowed.push_back(Variation(allowlist_->item(i)->data(Qt::UserRole).toUInt()));
        if (allowed.empty())
            throw std::runtime_error("Select at least one allowed variation.");
        creative::RandomOptions random;
        random.seed = seed;
        random.batch_size = std::size_t(count_->value());
        random.min_transforms = unsigned(minimum_->value());
        random.max_transforms = unsigned(maximum_->value());
        random.allowed_variations = allowed;
        random.rotational_symmetry = unsigned(symmetry_->value());
        random.reflection = reflection_->isChecked();
        random.randomize_palette = palette_->isChecked();
        random.min_contraction = minContraction_->value();
        random.max_contraction = maxContraction_->value();
        random.translation = translation_->value();
        random.width = initial_.width;
        random.height = initial_.height;
        creative::MutationOptions mutation;
        mutation.seed = seed;
        mutation.count = std::size_t(count_->value());
        mutation.strength = strength_->value();
        mutation.trend = trend_->value();
        mutation.geometry = geometry_->isChecked();
        mutation.variations = variations_->isChecked();
        mutation.colors = colors_->isChecked();
        mutation.parameters = parameters_->isChecked();
        mutation.palette = palette_->isChecked();
        mutation.include_final = final_->isChecked();
        mutation.allowed_variations = std::move(allowed);
        const auto parent = branches_.at(parentBranch_).flame;
        const auto initialPalette = initial_.palette;
        const auto mode = mode_;
        generating_ = true;
        preview_.stop();
        thumbnails_.stop();
        updateActions();
        status_->setText("Generating candidates…");
        generationThread_ = std::jthread([this, mode, random, mutation, parent, initialPalette] {
            try {
                auto candidates = mode == Mode::random ? creative::random_flames(random)
                                                       : creative::mutation_candidates(parent, mutation);
                for (std::size_t i = 0; i < candidates.size(); ++i) {
                    if (mode == Mode::mutation)
                        candidates[i].name = text(parent.name).left(100).toUtf8().toStdString() +
                                             " / M" + std::to_string(mutation.seed) + "." +
                                             std::to_string(i + 1);
                    else if (!random.randomize_palette)
                        candidates[i].palette = initialPalette;
                }
                QMetaObject::invokeMethod(
                    this,
                    [this, candidates = std::move(candidates)]() mutable {
                        generating_ = false;
                        showCandidates(std::move(candidates));
                    },
                    Qt::QueuedConnection);
            } catch (const std::exception &error) {
                const auto message = text(error.what());
                QMetaObject::invokeMethod(
                    this,
                    [this, message] {
                        generating_ = false;
                        status_->setText(message);
                        updateActions();
                    },
                    Qt::QueuedConnection);
            }
        });
    } catch (const std::exception &error) {
        generating_ = false;
        updateActions();
        status_->setText(text(error.what()));
    }
}
void ExploreDialog::showCandidates(std::vector<Flame> flames) {
    thumbnails_.stop();
    while (auto item = grid_->takeAt(0)) {
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    tiles_.clear();
    candidates_ = std::move(flames);
    for (int i = 0; i < int(candidates_.size()); ++i) {
        auto tile = new QToolButton;
        tile->setObjectName(QString("chooseCandidate%1").arg(i));
        tile->setText(QString("Candidate %1").arg(i + 1));
        tile->setToolTip(text(candidates_[i].name));
        tile->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        tile->setIconSize({144, 108});
        tile->setMinimumSize(148, 142);
        tile->setCheckable(true);
        tile->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        tile->setStyleSheet("QToolButton { border: 1px solid #435060; border-radius: 4px; padding: 4px; "
                            "} QToolButton:checked { border: 2px solid #62cddd; background: #203f4b; }");
        grid_->addWidget(tile, i / 3, i % 3);
        tiles_.push_back(tile);
        connect(tile, &QToolButton::clicked, this, [this, i] { select(i); });
    }
    thumbnail_ = 0;
    selected_ = candidates_.empty() ? -1 : 0;
    select(selected_);
    updateActions();
    nextThumbnail();
}
void ExploreDialog::select(int index) {
    if (index < 0 || index >= int(candidates_.size()))
        return;
    selected_ = index;
    for (int i = 0; i < int(tiles_.size()); ++i)
        tiles_[i]->setChecked(i == index);
    showParent_->setChecked(false);
    showSelected();
    updateActions();
}
void ExploreDialog::nextThumbnail() {
    if (!generating_ && thumbnail_ < int(candidates_.size()))
        thumbnails_.request(candidates_[thumbnail_], {160, 120});
}
void ExploreDialog::showSelected() {
    if (generating_ || selected_ < 0 || selected_ >= int(candidates_.size()))
        return;
    const auto &flame = mode_ == Mode::mutation && showParent_->isChecked()
                            ? branches_.at(parentBranch_).flame
                            : candidates_[selected_];
    selectedName_->setText(text(flame.name));
    preview_.request(flame, canvas_->size());
}
void ExploreDialog::chooseResult(Disposition disposition, bool all, bool branch) {
    if (generating_ || selected_ < 0 || selected_ >= int(candidates_.size()))
        return;
    disposition_ = disposition;
    accepted_.clear();
    if (all)
        accepted_ = candidates_;
    else {
        if (branch) {
            int node = parentBranch_;
            while (node > 0) {
                accepted_.push_back(branches_.at(node).flame);
                node = branches_.at(node).parent;
            }
            std::reverse(accepted_.begin(), accepted_.end());
        }
        accepted_.push_back(candidates_[selected_]);
    }
    accept();
}

void applyCreativeFlame(LegacyDocument &document, std::size_t index, const Flame &flame) {
    const auto before = document.render_model(index);
    if (before.transforms.size() != flame.transforms.size() ||
        bool(before.final_transform) != bool(flame.final_transform))
        throw std::invalid_argument("A mutation must retain its parent's transform structure");
    validate(flame);
    auto editTransform = [&](const Transform &old, const Transform &next, std::size_t transform,
                             bool final) {
        LegacyTransformEdits edit;
        if (!equal(old.pre, next.pre))
            edit.pre = next.pre;
        if (!equal(old.post, next.post))
            edit.post = next.post;
        if (!final && old.weight != next.weight)
            edit.weight = next.weight;
        if (old.color != next.color)
            edit.color = next.color;
        if (old.color_speed != next.color_speed)
            edit.color_speed = next.color_speed;
        if (old.opacity != next.opacity)
            edit.opacity = next.opacity;
        if (!equal(old.variations, next.variations))
            edit.variations = next.variations;
        if (old.parameters.julian_power != next.parameters.julian_power)
            edit.julian_power = next.parameters.julian_power;
        if (old.parameters.julian_dist != next.parameters.julian_dist)
            edit.julian_dist = next.parameters.julian_dist;
        if (old.chaos != next.chaos)
            edit.chaos = next.chaos;
        document.edit_transform(index, transform, edit, final);
    };
    for (std::size_t i = 0; i < before.transforms.size(); ++i)
        editTransform(before.transforms[i], flame.transforms[i], i, false);
    if (before.final_transform)
        editTransform(*before.final_transform, *flame.final_transform, 0, true);
    if (!equal(before.palette, flame.palette))
        document.edit_palette(index, flame.palette);
    if (before.palette_mode != flame.palette_mode || before.curves != flame.curves) {
        LegacyEdits edit;
        if (before.palette_mode != flame.palette_mode) edit.palette_mode = flame.palette_mode;
        if (before.curves != flame.curves) edit.curves = flame.curves;
        document.edit(index, edit);
    }
}

} // namespace apo::editor
