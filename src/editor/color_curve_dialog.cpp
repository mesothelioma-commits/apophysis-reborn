#include "color_curve_dialog.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <stdexcept>

namespace apo::editor {
namespace {
QString numeric(double value) {
    std::array<char, 64> bytes{};
    const auto result = std::to_chars(bytes.data(), bytes.data() + bytes.size(), value);
    return QString::fromLatin1(bytes.data(), int(result.ptr - bytes.data()));
}
double number(const QLineEdit* field) {
    auto bytes = field->text().trimmed().toLatin1();
    if (bytes.startsWith('+')) bytes.remove(0, 1);
    double value = 0;
    const auto result = std::from_chars(bytes.constData(), bytes.constData() + bytes.size(), value);
    if (result.ec != std::errc{} || result.ptr != bytes.constData() + bytes.size() || !std::isfinite(value))
        throw std::invalid_argument("Enter finite numbers for all curve coordinates and weights.");
    return value;
}
QPushButton* action(const QString& title, const char* name) {
    auto button = new QPushButton(title);
    button->setObjectName(name);
    return button;
}
} // namespace

CurvePlot::CurvePlot(QWidget* parent) : QWidget(parent) {
    setObjectName("colorCurvePlot");
    setMinimumSize(250, 240);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAccessibleName("Color transfer curve. Drag the control points or edit their numeric coordinates.");
}
QRectF CurvePlot::graphRect() const {
    return QRectF(38, 12, std::max(1, width() - 54), std::max(1, height() - 49));
}
QPointF CurvePlot::pointPosition(unsigned index) const {
    const auto bounds = graphRect();
    const auto& point = curves_.channels[channel_][index];
    const double x = curves_.mode == ColorCurveMode::legacy_7x ? double(index) / 3 : point.x;
    return {bounds.left() + x * bounds.width(), bounds.bottom() - point.y * bounds.height()};
}
void CurvePlot::setCurves(const ColorCurves& curves, unsigned channel) {
    curves_ = curves;
    channel_ = std::min(channel, 3u);
    update();
}
void CurvePlot::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const auto bounds = graphRect();
    painter.fillRect(bounds, QColor(24, 28, 35));
    painter.setPen(QColor(57, 63, 73));
    for (int i = 0; i <= 4; ++i) {
        const double x = bounds.left() + bounds.width() * i / 4;
        const double y = bounds.top() + bounds.height() * i / 4;
        painter.drawLine(QPointF(x, bounds.top()), QPointF(x, bounds.bottom()));
        painter.drawLine(QPointF(bounds.left(), y), QPointF(bounds.right(), y));
    }
    painter.setPen(QPen(QColor(123, 131, 144), 1, Qt::DashLine));
    painter.drawLine(bounds.bottomLeft(), bounds.topRight());
    QPainterPath path;
    const bool bypass = color_curves_are_identity(curves_);
    for (unsigned i = 0; i <= 256; ++i) {
        const double x = double(i) / 256;
        const double y = bypass ? x : evaluate_color_curve(curves_.channels[channel_], x, curves_.mode);
        const QPointF point(bounds.left() + x * bounds.width(), bounds.bottom() - y * bounds.height());
        if (!i) path.moveTo(point);
        else path.lineTo(point);
    }
    const std::array<QColor, 4> colors{QColor(233, 239, 247), QColor(255, 112, 112),
                                      QColor(116, 221, 156), QColor(125, 174, 255)};
    painter.setPen(QPen(colors[channel_], 2.5));
    painter.drawPath(path);
    painter.setPen(QPen(QColor(118, 133, 154), 1, Qt::DotLine));
    painter.drawLine(pointPosition(0), pointPosition(1));
    painter.drawLine(pointPosition(2), pointPosition(3));
    for (unsigned i = 0; i < 4; ++i) {
        painter.setPen(QPen(colors[channel_], 2));
        painter.setBrush(i == 1 || i == 2 ? colors[channel_] : QColor(24, 28, 35));
        painter.drawEllipse(pointPosition(i), 5, 5);
    }
    painter.setPen(palette().color(QPalette::Text));
    painter.drawText(QRectF(bounds.left(), bounds.bottom() + 12, bounds.width(), 22), Qt::AlignCenter,
                     curves_.mode == ColorCurveMode::legacy_7x ? "Input / parameter t" : "Input");
    painter.drawText(QRectF(0, bounds.top() - 4, 30, 20), Qt::AlignRight, "1");
    painter.drawText(QRectF(0, bounds.bottom() - 12, 30, 20), Qt::AlignRight, "0");
}
void CurvePlot::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    double nearest = 18;
    for (unsigned i : {0u, 3u, 1u, 2u}) {
        const double distance = QLineF(event->position(), pointPosition(i)).length();
        if (distance <= nearest) { nearest = distance; dragged_ = int(i); }
    }
    if (dragged_ >= 0) dragPoint(event->position());
}
void CurvePlot::mouseMoveEvent(QMouseEvent* event) {
    if (dragged_ >= 0 && (event->buttons() & Qt::LeftButton)) dragPoint(event->position());
}
void CurvePlot::mouseReleaseEvent(QMouseEvent* event) {
    if (dragged_ >= 0 && event->button() == Qt::LeftButton) dragPoint(event->position());
    dragged_ = -1;
}
void CurvePlot::dragPoint(QPointF position) {
    const auto bounds = graphRect();
    const auto& curve = curves_.channels[channel_];
    double x = curve[unsigned(dragged_)].x;
    if (curves_.mode == ColorCurveMode::continuous && dragged_ > 0 && dragged_ < 3)
        x = std::clamp((position.x() - bounds.left()) / bounds.width(),
                        curve[unsigned(dragged_ - 1)].x, curve[unsigned(dragged_ + 1)].x);
    const double y = std::clamp((bounds.bottom() - position.y()) / bounds.height(), 0.0, 1.0);
    emit pointEdited(unsigned(dragged_), x, y);
}

ColorCurveDialog::ColorCurveDialog(const ColorCurves& curves, QWidget* parent)
    : QDialog(parent), original_(curves), curves_(curves) {
    validate_color_curves(curves);
    setObjectName("colorCurveDialog");
    setWindowTitle("Color curves");
    resize(790, 660);
    auto layout = new QVBoxLayout(this);
    auto selectors = new QHBoxLayout;
    channel_ = new QComboBox;
    channel_->setObjectName("curveChannel");
    channel_->addItems({"Master", "Red", "Green", "Blue"});
    mode_ = new QComboBox;
    mode_->setObjectName("curveMode");
    mode_->addItems({"Original 7X lookup", "Continuous transfer"});
    selectors->addWidget(new QLabel("Channel"));
    selectors->addWidget(channel_);
    selectors->addSpacing(20);
    selectors->addWidget(new QLabel("Evaluation"));
    selectors->addWidget(mode_, 1);
    layout->addLayout(selectors);
    plot_ = new CurvePlot;
    layout->addWidget(plot_, 1);
    help_ = new QLabel;
    help_->setWordWrap(true);
    layout->addWidget(help_);
    auto grid = new QGridLayout;
    grid->addWidget(new QLabel("Control point"), 0, 0);
    for (unsigned axis = 0; axis < 3; ++axis)
        grid->addWidget(new QLabel(std::array<QString, 3>{"X", "Y", "Weight"}[axis]), 0, int(axis + 1));
    for (unsigned i = 0; i < 4; ++i) {
        grid->addWidget(new QLabel(QString::number(i + 1)), int(i + 1), 0);
        for (unsigned axis = 0; axis < 3; ++axis) {
            auto field = new QLineEdit;
            field->setObjectName(QString("curvePoint_%1_%2").arg(i).arg(axis));
            field->setAccessibleName(QString("Point %1 %2").arg(i + 1).arg(
                std::array<QString, 3>{"X", "Y", "weight"}[axis]));
            field->setAlignment(Qt::AlignRight);
            fields_[i][axis] = field;
            grid->addWidget(field, int(i + 1), int(axis + 1));
            connect(field, &QLineEdit::textChanged, this, [this] { if (!syncing_) updateCurves(); });
        }
    }
    layout->addLayout(grid);
    auto presets = new QHBoxLayout;
    preset_ = new QComboBox;
    preset_->setObjectName("curvePreset");
    preset_->addItems({"Identity", "Soft contrast", "Lift shadows", "Lower highlights", "Warm balance"});
    preset_->setToolTip("Presets replace all channels. Nonidentity presets select Continuous transfer.");
    auto usePreset = action("Use preset", "useCurvePreset");
    auto resetChannel = action("Reset channel", "resetCurveChannel");
    auto resetAll = action("Reset all", "resetAllCurves");
    presets->addWidget(preset_, 1);
    presets->addWidget(usePreset);
    presets->addWidget(resetChannel);
    presets->addWidget(resetAll);
    layout->addLayout(presets);
    status_ = new QLabel;
    status_->setObjectName("curveStatus");
    status_->setWordWrap(true);
    layout->addWidget(status_);
    auto bottom = new QHBoxLayout;
    auto original = action("Restore original", "restoreOriginalCurves");
    bottom->addWidget(original);
    bottom->addStretch();
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    apply_ = buttons->button(QDialogButtonBox::Ok);
    apply_->setObjectName("acceptCurves");
    apply_->setText("Apply curves");
    bottom->addWidget(buttons);
    layout->addLayout(bottom);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] { if (updateCurves()) accept(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(channel_, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
    connect(mode_, &QComboBox::currentIndexChanged, this, [this] { if (!syncing_) updateCurves(); });
    connect(plot_, &CurvePlot::pointEdited, this, [this](unsigned index, double x, double y) {
        auto& point = curves_.channels[unsigned(channel_->currentIndex())][index];
        point.x = x;
        point.y = y;
        refresh();
    });
    connect(usePreset, &QPushButton::clicked, this, [this] { preset(preset_->currentIndex()); });
    connect(resetChannel, &QPushButton::clicked, this, [this] {
        curves_.channels[unsigned(channel_->currentIndex())] = identity_color_curve();
        refresh();
    });
    connect(resetAll, &QPushButton::clicked, this, [this] {
        const auto mode = curves_.mode;
        curves_ = {};
        curves_.mode = mode;
        refresh();
    });
    connect(original, &QPushButton::clicked, this, [this] { curves_ = original_; refresh(); });
    refresh();
    if (screen()) resize(size().boundedTo(screen()->availableGeometry().size() - QSize(36, 60)));
}

bool ColorCurveDialog::updateCurves() {
    if (syncing_) return true;
    try {
        auto next = curves_;
        next.mode = mode_->currentIndex() == 0 ? ColorCurveMode::legacy_7x : ColorCurveMode::continuous;
        auto& curve = next.channels[unsigned(channel_->currentIndex())];
        for (unsigned i = 0; i < 4; ++i)
            curve[i] = {number(fields_[i][0]), number(fields_[i][1]), number(fields_[i][2])};
        validate_color_curves(next);
        curves_ = next;
        apply_->setEnabled(true);
        status_->setText(color_curves_are_identity(curves_) ? "Identity: image values pass through unchanged." :
                         "Master is applied first, then the red, green and blue channels.");
        help_->setText(curves_.mode == ColorCurveMode::legacy_7x ?
            "Original 7X uses Y(parameter): X values are retained but do not shape the transfer. Drag handles vertically. "
            "Changing any point activates all four curves, including the default channels." :
            "Drag control points to shape the transfer. X values must stay ordered from 0 to 1. "
            "The graph shows the selected channel; master is applied before each RGB channel.");
        plot_->setCurves(curves_, unsigned(channel_->currentIndex()));
        return true;
    } catch (const std::exception& error) {
        status_->setText(QString::fromUtf8(error.what()));
        apply_->setEnabled(false);
        return false;
    }
}
void ColorCurveDialog::refresh() {
    syncing_ = true;
    mode_->setCurrentIndex(curves_.mode == ColorCurveMode::legacy_7x ? 0 : 1);
    const auto& curve = curves_.channels[unsigned(channel_->currentIndex())];
    for (unsigned i = 0; i < 4; ++i) {
        fields_[i][0]->setText(numeric(curve[i].x));
        fields_[i][1]->setText(numeric(curve[i].y));
        fields_[i][2]->setText(numeric(curve[i].weight));
    }
    syncing_ = false;
    updateCurves();
}
void ColorCurveDialog::preset(int index) {
    if (index == 0) {
        const auto mode = curves_.mode;
        curves_ = {};
        curves_.mode = mode;
    } else if (index == 4) {
        curves_ = {};
        curves_.mode = ColorCurveMode::continuous;
        curves_.channels[1] = {{{0, 0, 1}, {.25, .34, 1}, {.75, .85, 1}, {1, 1, 1}}};
        curves_.channels[3] = {{{0, 0, 1}, {.25, .18, 1}, {.75, .66, 1}, {1, 1, 1}}};
    } else {
        // Presets are modern transfer curves; avoid implying the same numbers
        // describe the historical parameter-space lookup.
        curves_ = {};
        curves_.mode = ColorCurveMode::continuous;
        auto& curve = curves_.channels[unsigned(channel_->currentIndex())];
        if (index == 1) curve = {{{0, 0, 1}, {.25, .1, 1}, {.75, .9, 1}, {1, 1, 1}}};
        if (index == 2) curve = {{{0, 0, 1}, {.2, .4, 1}, {.7, .85, 1}, {1, 1, 1}}};
        if (index == 3) curve = {{{0, 0, 1}, {.3, .15, 1}, {.8, .6, 1}, {1, 1, 1}}};
    }
    refresh();
}

} // namespace apo::editor
