#include "render_proof_dialog.hpp"
#include "background_jobs.hpp"
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>
#include <charconv>
#include <cmath>

namespace apo::editor {
namespace {
Q_APPLICATION_STATIC(ApplicationJobs, proofJobs)
constexpr int maxCropSide = 1024;

QString number(double value) {
    char buffer[64];
    auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    return QString::fromLatin1(buffer, result.ptr - buffer);
}

QSpinBox *coordinate(const char *name, int max, int min = 0) {
    auto *box = new QSpinBox;
    box->setObjectName(name);
    box->setRange(min, max);
    box->setKeyboardTracking(false);
    return box;
}

QLineEdit *toneField(const char *name, double value) {
    auto *field = new QLineEdit(number(value));
    field->setObjectName(name);
    field->setMaxLength(40);
    return field;
}

QHBoxLayout *pair(QWidget *first, const QString &separator, QWidget *second) {
    auto *row = new QHBoxLayout;
    row->addWidget(first);
    row->addWidget(new QLabel(separator));
    row->addWidget(second);
    return row;
}
} // namespace

// Paint one image sample per physical display pixel, including on high-DPI monitors.
class ProofCanvas : public QWidget {
public:
    explicit ProofCanvas(QWidget *parent = nullptr) : QWidget(parent) {
        setObjectName("proofCanvas");
        setAccessibleName(tr("Crop at actual output pixels"));
        setFixedSize(1, 1);
    }
    void display(QImage image, bool checkerboard) {
        image_ = std::move(image);
        checkerboard_ = checkerboard;
        resizeToPixels();
        update();
    }
protected:
    bool event(QEvent *event) override {
        const bool processed = QWidget::event(event);
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
        if (event->type() == QEvent::DevicePixelRatioChange) resizeToPixels();
#else
        if (event->type() == QEvent::ScreenChangeInternal) resizeToPixels();
#endif
        return processed;
    }
    void paintEvent(QPaintEvent *) override {
        if (image_.isNull()) return;
        QPainter painter(this);
        const QRectF target(QPointF(0, 0), QSizeF(image_.size()) / devicePixelRatioF());
        if (checkerboard_) {
            constexpr int tile = 12;
            painter.fillRect(target, QColor(185, 185, 185));
            for (int y = 0; y < height(); y += tile)
                for (int x = (y / tile % 2) * tile; x < width(); x += tile * 2)
                    painter.fillRect(QRect(x, y, tile, tile), QColor(132, 132, 132));
        }
        painter.drawImage(target, image_, image_.rect());
    }
private:
    QImage image_;
    bool checkerboard_ = false;
    void resizeToPixels() {
        const qreal ratio = devicePixelRatioF();
        setFixedSize(std::max(1, int(std::ceil(image_.width() / ratio))),
                     std::max(1, int(std::ceil(image_.height() / ratio))));
    }
};

RenderProofDialog::RenderProofDialog(const Flame &outputFlame, const RenderOptions &outputOptions,
        bool transparent, QWidget *parent, bool sixteenBit, Sampler sampler)
    : QDialog(parent), flame_(std::make_shared<const Flame>(outputFlame)), options_(outputOptions),
      sampler_(std::move(sampler)), jobs_(proofJobs->jobs),
      receiver_(std::make_shared<BackgroundReceiver<RenderProofDialog>>(this)), sixteenBit_(sixteenBit) {
    setObjectName("renderProofDialog");
    setWindowTitle(tr("Render proof — %1").arg(QString::fromStdString(flame_->name)));
    options_.progress = {};
    options_.stop_token = {};
    auto *layout = new QVBoxLayout(this);
    auto *columns = new QHBoxLayout;
    layout->addLayout(columns, 1);
    auto *controls = new QVBoxLayout;
    columns->addLayout(controls);
    const auto &f = *flame_;
    auto *crop = new QGroupBox(tr("Crop in output pixels"));
    auto *cropForm = new QFormLayout(crop);
    cropForm->addRow(tr("Full image"), new QLabel(tr("%1 × %2").arg(f.width).arg(f.height)));
    x_ = coordinate("proofX", f.width - 1);
    y_ = coordinate("proofY", f.height - 1);
    width_ = coordinate("proofWidth", std::min(f.width, maxCropSide), 1);
    height_ = coordinate("proofHeight", std::min(f.height, maxCropSide), 1);
    width_->setValue(std::min(f.width, 256));
    height_->setValue(std::min(f.height, 256));
    x_->setAccessibleName(tr("Left edge"));
    y_->setAccessibleName(tr("Top edge"));
    width_->setAccessibleName(tr("Crop width"));
    height_->setAccessibleName(tr("Crop height"));
    cropForm->addRow(tr("Left, top"), pair(x_, ",", y_));
    cropForm->addRow(tr("Width × height"), pair(width_, "×", height_));
    auto *center = new QPushButton(tr("Center crop"));
    center->setObjectName("centerProofCrop");
    cropForm->addRow(QString{}, center);
    controls->addWidget(crop);
    auto centerCrop = [this] {
        x_->setValue((flame_->width - width_->value()) / 2);
        y_->setValue((flame_->height - height_->value()) / 2);
    };
    updateCropBounds();
    centerCrop();
    connect(center, &QPushButton::clicked, this, centerCrop);
    auto *tone = new QGroupBox(tr("Tone"));
    auto *toneForm = new QFormLayout(tone);
    brightness_ = toneField("proofBrightness", f.brightness);
    gamma_ = toneField("proofGamma", f.gamma);
    vibrancy_ = toneField("proofVibrancy", f.vibrancy);
    toneForm->addRow(tr("Brightness"), brightness_);
    toneForm->addRow(tr("Gamma"), gamma_);
    toneForm->addRow(tr("Vibrancy"), vibrancy_);
    auto *reset = new QPushButton(tr("Reset tone"));
    reset->setObjectName("resetProofTone");
    toneForm->addRow(QString{}, reset);
    controls->addWidget(tone);
    connect(reset, &QPushButton::clicked, this, [this] {
        brightness_->setText(number(flame_->brightness));
        gamma_->setText(number(flame_->gamma));
        vibrancy_->setText(number(flame_->vibrancy));
    });
    controls->addStretch();
    auto *viewColumn = new QVBoxLayout;
    columns->addLayout(viewColumn, 1);
    auto *displayRow = new QHBoxLayout;
    displayRow->addWidget(new QLabel(tr("Actual pixels · 1:1")));
    displayRow->addStretch();
    transparent_ = new QCheckBox(tr("Transparency checkerboard"));
    transparent_->setObjectName("proofTransparency");
    transparent_->setChecked(transparent);
    transparent_->setToolTip(tr("Inspect straight alpha. With this off, the proof uses the flame background."));
    displayRow->addWidget(transparent_);
    viewColumn->addLayout(displayRow);
    auto *scroll = new QScrollArea;
    scroll->setAlignment(Qt::AlignCenter);
    scroll->setMinimumSize(200, 180);
    canvas_ = new ProofCanvas;
    scroll->setWidget(canvas_);
    viewColumn->addWidget(scroll, 1);
    auto *sampling = new QLabel(tr("%1 samples  ·  Seed %2")
        .arg(QLocale().toString(qulonglong(options_.samples))).arg(options_.seed));
    sampling->setObjectName("proofSamplingSettings");
    viewColumn->addWidget(sampling);
    device_ = new QLabel;
    device_->setObjectName("proofDevice");
    device_->setWordWrap(true);
    device_->setTextFormat(Qt::PlainText);
    device_->hide();
    viewColumn->addWidget(device_);
    progress_ = new QProgressBar;
    progress_->setRange(0, 100);
    progress_->hide();
    layout->addWidget(progress_);
    status_ = new QLabel(tr("Render a crop to inspect it at the final image resolution."));
    status_->setObjectName("proofStatus");
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    layout->addWidget(status_);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    start_ = buttons->addButton(tr("Render crop"), QDialogButtonBox::ActionRole);
    start_->setObjectName("renderProofCrop");
    stop_ = buttons->addButton(tr("Stop"), QDialogButtonBox::ActionRole);
    stop_->setObjectName("stopRenderProof");
    apply_ = buttons->addButton(tr("Use tone for render"), QDialogButtonBox::AcceptRole);
    apply_->setObjectName("useProofTone");
    layout->addWidget(buttons);
    for (auto *button : {start_, stop_, apply_, center, reset}) button->setAutoDefault(false);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(start_, &QPushButton::clicked, this, &RenderProofDialog::sample);
    connect(stop_, &QPushButton::clicked, this, [this] {
        invalidateCrop();
        status_->setText(tr("Proof stopped. Render the crop to try again."));
    });
    for (auto *field : {width_, height_})
        connect(field, &QSpinBox::valueChanged, this, [this] {
            updateCropBounds();
            invalidateCrop();
        });
    for (auto *field : {x_, y_})
        connect(field, &QSpinBox::valueChanged, this, &RenderProofDialog::invalidateCrop);
    for (auto *field : {brightness_, gamma_, vibrancy_})
        connect(field, &QLineEdit::textChanged, this, &RenderProofDialog::toneChanged);
    connect(transparent_, &QCheckBox::toggled, this, &RenderProofDialog::toneChanged);
    toneDebounce_.setSingleShot(true);
    toneDebounce_.setInterval(45);
    connect(&toneDebounce_, &QTimer::timeout, this, &RenderProofDialog::develop);
    updateButtons();
    resize(850, 510);
}

RenderProofDialog::~RenderProofDialog() {
    receiver_->close();
    cancelJobs();
}

void RenderProofDialog::done(int result) {
    // Invalidate callbacks that may already be in the UI event queue.
    invalidateCrop();
    QDialog::done(result);
}

void RenderProofDialog::cancelJobs() {
    toneDebounce_.stop();
    jobs_->cancel(this);
}

double RenderProofDialog::brightness() const { return brightness_->text().toDouble(); }
double RenderProofDialog::gamma() const { return gamma_->text().toDouble(); }
double RenderProofDialog::vibrancy() const { return vibrancy_->text().toDouble(); }

void RenderProofDialog::updateCropBounds() {
    const QSignalBlocker bx(x_), by(y_);
    x_->setMaximum(flame_->width - width_->value());
    y_->setMaximum(flame_->height - height_->value());
}

void RenderProofDialog::updateButtons() {
    start_->setEnabled(!sampling_);
    stop_->setEnabled(sampling_ || developing_);
    apply_->setEnabled(retained_ && !developing_ && !toneDebounce_.isActive() && readTone().has_value());
}

void RenderProofDialog::invalidateCrop() {
    ++cropGeneration_;
    ++toneGeneration_;
    cancelJobs();
    retained_.reset();
    sampling_ = developing_ = false;
    progress_->hide();
    device_->hide();
    canvas_->display({}, false);
    status_->setText(tr("Render the selected crop to inspect it."));
    updateButtons();
}

void RenderProofDialog::sample() {
    invalidateCrop();
    sampling_ = true;
    updateButtons();
    progress_->setValue(0);
    progress_->show();
    status_->setText(tr("Sampling the final image for this crop…"));
    const ProofRegion region{x_->value(), y_->value(), width_->value(), height_->value()};
    jobs_->submit(this, [flame = flame_, options = options_, sample = sampler_, region,
                         receiver = receiver_, generation = cropGeneration_](std::stop_token stop) mutable {
        try {
            options.stop_token = stop;
            // The renderer reports committed batches serially. Post at most 100
            // UI events, even if a final render contains billions of samples.
            options.progress = [receiver, generation, last = 0](std::uint64_t n, std::uint64_t total) mutable {
                if (!total) return;
                const auto percent = int(std::min(100.0, 100.0 * double(n) / double(total)));
                if (percent <= last) return;
                last = percent;
                receiver->post([generation, percent](RenderProofDialog *dialog) {
                    if (generation == dialog->cropGeneration_ && dialog->sampling_)
                        dialog->progress_->setValue(percent);
                });
            };
            auto proof = sample(*flame, options, region);
            if (stop.stop_requested()) return;
            receiver->post([generation, proof = std::move(proof)](RenderProofDialog *dialog) {
                if (generation != dialog->cropGeneration_) return;
                dialog->sampling_ = false;
                dialog->progress_->hide();
                dialog->retained_ = proof;
                QString device = QString::fromStdString(proof->device_name);
                if (!proof->fallback_reason.empty())
                    device += tr(" · %1").arg(QString::fromStdString(proof->fallback_reason));
                dialog->device_->setText(device);
                dialog->device_->setVisible(!device.isEmpty());
                dialog->toneChanged();
            });
        } catch (const std::exception &error) {
            if (stop.stop_requested()) return;
            const auto message = QString::fromUtf8(error.what());
            receiver->post([generation, message](RenderProofDialog *dialog) {
                if (generation != dialog->cropGeneration_) return;
                dialog->invalidateCrop();
                dialog->status_->setText(message);
            });
        }
    });
}

std::optional<ProofTone> RenderProofDialog::readTone() const {
    auto tone = proof_tone(*flame_);
    bool a = false, b = false, c = false;
    tone.brightness = brightness_->text().toDouble(&a);
    tone.gamma = gamma_->text().toDouble(&b);
    tone.vibrancy = vibrancy_->text().toDouble(&c);
    if (!a || !std::isfinite(tone.brightness) || tone.brightness < 0 ||
        !b || !std::isfinite(tone.gamma) || tone.gamma <= 0 ||
        !c || !std::isfinite(tone.vibrancy) || tone.vibrancy < 0 || tone.vibrancy > 1) return {};
    return tone;
}

void RenderProofDialog::toneChanged() {
    ++toneGeneration_;
    // A tone edit while sampling is picked up when the histogram arrives.
    if (sampling_) return;
    toneDebounce_.stop();
    if (retained_) jobs_->cancel(this);
    if (!readTone()) {
        developing_ = false;
        status_->setText(tr("Use a nonnegative brightness, a positive gamma and a vibrancy from 0 to 1."));
    } else if (retained_) {
        developing_ = true;
        status_->setText(tr("Updating tone from saved samples…"));
        toneDebounce_.start();
    } else {
        status_->setText(tr("Render the selected crop to inspect it."));
    }
    updateButtons();
}

void RenderProofDialog::develop() {
    auto tone = readTone();
    if (!tone || !retained_) return;
    const bool alpha = transparent_->isChecked();
    jobs_->submit(this, [proof = retained_, tone = *tone, alpha, sixteenBit = sixteenBit_,
                         receiver = receiver_, generation = toneGeneration_](std::stop_token stop) {
        try {
            auto rgba = develop_render_proof(*proof, tone, alpha, stop, sixteenBit);
            if (stop.stop_requested()) return;
            const auto &r = proof->region;
            QImage image = QImage(rgba.data(), r.width, r.height, r.width * 4, QImage::Format_RGBA8888).copy();
            if (image.isNull()) throw std::runtime_error("Could not allocate the proof display image");
            receiver->post([generation, image = std::move(image), alpha](RenderProofDialog *dialog) {
                if (generation != dialog->toneGeneration_) return;
                dialog->developing_ = false;
                dialog->canvas_->display(image, alpha);
                dialog->status_->setText(tr("Crop ready. Tone changes reuse these samples."));
                dialog->updateButtons();
            });
        } catch (const std::exception &error) {
            if (stop.stop_requested()) return;
            const auto message = QString::fromUtf8(error.what());
            receiver->post([generation, message](RenderProofDialog *dialog) {
                if (generation != dialog->toneGeneration_) return;
                dialog->developing_ = false;
                dialog->updateButtons();
                dialog->apply_->setEnabled(false);
                dialog->status_->setText(message);
            });
        }
    });
}
} // namespace apo::editor
