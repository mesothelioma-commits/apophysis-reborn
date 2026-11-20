#include "animation_dialog.hpp"
#include "creative_widgets.hpp"
#include "document_session.hpp"
#include "../project_document.hpp"
#include <QCheckBox>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTabWidget>
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
std::uint64_t unsignedValue(const QLineEdit *field, const char *label) {
  const auto s = field->text().trimmed().toStdString();
  std::uint64_t value{};
  const auto [end, error] = std::from_chars(s.data(), s.data() + s.size(), value);
  if (error != std::errc{} || end != s.data() + s.size() || s.empty())
    throw std::invalid_argument(std::string(label) + " must be a whole number from 0 through 18446744073709551615");
  return value;
}
QSpinBox *spin(int minimum, int maximum, int value, const char *name) {
  auto w = new QSpinBox;
  w->setRange(minimum, maximum);
  w->setValue(value);
  w->setObjectName(name);
  return w;
}
QComboBox *combo(std::initializer_list<const char *> names, const char *name) {
  auto w = new QComboBox;
  w->setObjectName(name);
  for (const auto s : names) w->addItem(QString::fromUtf8(s));
  return w;
}
QWidget *scrollPanel(QWidget *child) {
  auto area = new QScrollArea;
  area->setWidgetResizable(true);
  area->setFrameShape(QFrame::NoFrame);
  area->setWidget(child);
  return area;
}
QTableWidgetItem *readOnly(const QString &value) {
  auto item = new QTableWidgetItem(value);
  item->setFlags(item->flags() & ~Qt::ItemIsEditable);
  return item;
}
} // namespace

TimelineRuler::TimelineRuler(QWidget *parent) : QWidget(parent) {
  setObjectName("timelineRuler");
  setMinimumHeight(62);
  setMaximumHeight(72);
  setCursor(Qt::PointingHandCursor);
  setToolTip("Click or drag to scrub. Diamonds mark keyframes.");
}
void TimelineRuler::setTimeline(const AnimationClip &clip, std::uint32_t frame) {
  count_ = clip.frame_count;
  frame_ = frame;
  keys_.clear();
  for (const auto &key : clip.keyframes) keys_.push_back(key.frame);
  for (const auto &track : clip.tracks)
    for (const auto &key : track) keys_.push_back(key.frame);
  std::sort(keys_.begin(), keys_.end());
  keys_.erase(std::unique(keys_.begin(), keys_.end()), keys_.end());
  update();
}
void TimelineRuler::paintEvent(QPaintEvent *) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  p.fillRect(rect(), QColor("#171d25"));
  const auto x = [&](std::uint32_t f) {
    return 12.0 + (width() - 24.0) * double(f) / std::max(1u, count_ - 1);
  };
  const int divisions = std::max(1, std::min(10, width() / 75));
  p.setPen(QColor("#596b7b"));
  p.drawLine(QPointF(12, 33), QPointF(width() - 12, 33));
  for (int i = 0; i <= divisions; ++i) {
    auto f = std::uint32_t(std::llround(double(count_ - 1) * i / divisions));
    p.drawLine(QPointF(x(f), 29), QPointF(x(f), 39));
    p.drawText(QRectF(std::clamp(x(f) - 24, 0.0, std::max(0.0, width() - 48.0)), 42, 48, 18),
               Qt::AlignCenter, QString::number(f));
  }
  p.setPen(Qt::NoPen);
  p.setBrush(QColor("#e7bb74"));
  for (auto f : keys_) {
    const auto c = x(f);
    p.drawPolygon(QPolygonF{QPointF(c, 14), QPointF(c + 5, 21),
                            QPointF(c, 28), QPointF(c - 5, 21)});
  }
  p.setPen(QPen(QColor("#71d8e6"), 2));
  p.drawLine(QPointF(x(frame_), 4), QPointF(x(frame_), 40));
}
void TimelineRuler::choose(double x) {
  const auto t = std::clamp((x - 12) / std::max(1.0, width() - 24.0), 0.0, 1.0);
  emit frameSelected(int(std::llround(t * (count_ - 1))));
}
void TimelineRuler::mousePressEvent(QMouseEvent *e) {
  if (e->button() == Qt::LeftButton) choose(e->position().x());
}
void TimelineRuler::mouseMoveEvent(QMouseEvent *e) {
  if (e->buttons() & Qt::LeftButton) choose(e->position().x());
}

AnimationDialog::AnimationDialog(const LegacyDocument &source, std::size_t selected,
                                 std::optional<AnimationClip> initial,
                                 QWidget *parent, const PreviewSettings &settings)
    : QDialog(parent), source_(source), selected_(selected), preview_(this), settings_(settings) {
  if (selected >= source.entries().size()) throw std::invalid_argument("Select an existing flame");
  clip_ = initial ? std::move(*initial)
                  : make_animation_preset(source, selected, AnimationPreset::still);
  const auto bytes = serialize_animation(clip_).size();
  history_.push_back({clip_, bytes});
  settings_.debounce_ms = 15;
  preview_.setSettings(settings_);
  setObjectName("animationTimelineDialog");
  setWindowTitle("Animation timeline");
  if (const auto screen = QGuiApplication::primaryScreen())
    resize(QSize(1240, 820).boundedTo(screen->availableGeometry().size() - QSize(32, 64)));
  else resize(1240, 820);
  auto root = new QVBoxLayout(this);
  auto heading = new QLabel("<b>ANIMATION TIMELINE</b>  ·  Keyframes, morphing and image sequences");
  root->addWidget(heading);
  auto split = new QSplitter;
  root->addWidget(split, 1);
  auto left = new QWidget;
  auto view = new QVBoxLayout(left);
  view->setContentsMargins(0, 0, 6, 0);
  canvas_ = new PreviewCanvas;
  canvas_->setObjectName("timelinePreview");
  canvas_->setMinimumSize(250, 200);
  view->addWidget(canvas_, 1);
  time_ = new QLabel;
  time_->setObjectName("timelineTime");
  view->addWidget(time_);
  ruler_ = new TimelineRuler;
  view->addWidget(ruler_);
  scrubber_ = new QSlider(Qt::Horizontal);
  scrubber_->setObjectName("timelineScrubber");
  view->addWidget(scrubber_);
  auto transport = new QHBoxLayout;
  auto start = new QPushButton("First");
  auto previous = new QPushButton("−1");
  play_ = new QPushButton("Play");
  play_->setObjectName("timelinePlay");
  auto next = new QPushButton("+1");
  auto end = new QPushButton("Last");
  frameSpin_ = spin(0, 999999, 0, "timelineFrame");
  for (auto w : {start, previous, play_, next, end}) transport->addWidget(w);
  transport->addWidget(frameSpin_);
  view->addLayout(transport);
  diagnostics_ = new QLabel;
  diagnostics_->setWordWrap(true);
  diagnostics_->setObjectName("timelineDiagnostics");
  view->addWidget(diagnostics_);
  split->addWidget(left);

  auto tabs = new QTabWidget;
  tabs->setObjectName("timelineTabs");
  split->addWidget(tabs);
  auto keyPanel = new QWidget;
  auto keyLayout = new QVBoxLayout(keyPanel);
  auto keyNote = new QLabel("Capture flames at the playhead. Matching transform IDs define the morph; review Correspondence when combining different flames. Edits stay private until Apply.");
  keyNote->setWordWrap(true);
  keyLayout->addWidget(keyNote);
  keys_ = new QTableWidget(0, 5);
  keys_->setObjectName("timelineKeyframes");
  keys_->setHorizontalHeaderLabels({"Frame", "Seconds", "Name", "Transition", "Transforms"});
  keys_->setSelectionBehavior(QAbstractItemView::SelectRows);
  keys_->setSelectionMode(QAbstractItemView::SingleSelection);
  keys_->verticalHeader()->hide();
  keys_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
  keys_->setColumnWidth(0, 65);
  keys_->setColumnWidth(1, 73);
  keys_->setColumnWidth(3, 112);
  keys_->setColumnWidth(4, 83);
  keyLayout->addWidget(keys_, 1);
  auto captures = new QHBoxLayout;
  auto capture = new QPushButton("Capture editor flame");
  capture->setObjectName("timelineCapture");
  auto replaceKey = new QPushButton("Replace selected");
  replaceKey->setObjectName("timelineReplace");
  auto removeKey = new QPushButton("Remove");
  removeKey->setObjectName("timelineRemove");
  for (auto w : {capture, replaceKey, removeKey}) captures->addWidget(w);
  keyLayout->addLayout(captures);
  auto edits = new QHBoxLayout;
  auto correspondence = new QPushButton("Correspondence…");
  correspondence->setObjectName("timelineCorrespondence");
  auto gradient = new QPushButton("Gradient…");
  auto adjust = new QPushButton("Adjustments…");
  gradient->setObjectName("timelineGradient");
  adjust->setObjectName("timelineAdjustments");
  for (auto w : {correspondence, gradient, adjust}) edits->addWidget(w);
  keyLayout->addLayout(edits);
  auto imports = new QHBoxLayout;
  library_ = new QComboBox;
  library_->setObjectName("timelineLibrary");
  for (std::size_t i = 0; i < source_.entries().size(); ++i)
    library_->addItem(QString("%1 · %2").arg(i + 1).arg(text(source_.entries()[i].name)));
  library_->setCurrentIndex(int(selected_));
  imports->addWidget(library_, 1);
  auto addLibrary = new QPushButton("Add at frame");
  addLibrary->setObjectName("timelineAddLibrary");
  auto import = new QPushButton("Import…");
  imports->addWidget(addLibrary);
  imports->addWidget(import);
  keyLayout->addLayout(imports);
  auto presets = new QHBoxLayout;
  presets_ = combo({"Still", "Camera orbit", "Zoom", "Palette cycle"}, "timelinePreset");
  auto makePreset = new QPushButton("Apply preset");
  makePreset->setObjectName("timelineMakePreset");
  auto morph = new QPushButton("Morph to library selection");
  morph->setObjectName("timelineMorph");
  presets->addWidget(presets_);
  presets->addWidget(makePreset);
  presets->addWidget(morph);
  keyLayout->addLayout(presets);
  auto timed = new QPushButton("Import timed library…");
  timed->setToolTip("Choose whether legacy time values represent frames or seconds.");
  keyLayout->addWidget(timed);
  tabs->addTab(keyPanel, "Keyframes");

  auto settingsPanel = new QWidget;
  auto form = new QFormLayout(settingsPanel);
  name_ = new QLineEdit;
  name_->setObjectName("timelineName");
  form->addRow("Name", name_);
  frames_ = spin(1, int(animation_frame_limit), 121, "timelineFrameCount");
  form->addRow("Total frames", frames_);
  ratePreset_ = new QComboBox;
  ratePreset_->setObjectName("timelineRatePreset");
  ratePreset_->addItem("Custom rational rate", QString{});
  for (const auto rate : {"24/1", "25/1", "30/1", "50/1", "60/1", "24000/1001", "30000/1001", "60000/1001"})
    ratePreset_->addItem(QString::fromLatin1(rate), QString::fromLatin1(rate));
  form->addRow("Frame-rate preset", ratePreset_);
  rateNumerator_ = spin(1, 240000, 30, "timelineRateNumerator");
  rateDenominator_ = spin(1, 10000, 1, "timelineRateDenominator");
  auto rate = new QHBoxLayout;
  rate->addWidget(rateNumerator_);
  rate->addWidget(new QLabel("/"));
  rate->addWidget(rateDenominator_);
  form->addRow("Frames per second", rate);
  duration_ = new QDoubleSpinBox;
  duration_->setObjectName("timelineDuration");
  duration_->setDecimals(6);
  duration_->setRange(.000001, 1000000000);
  duration_->setSuffix(" s");
  form->addRow("Duration (frame count ÷ fps)", duration_);
  loop_ = combo({"Once", "Repeat", "Ping-pong"}, "timelineLoop");
  affine_ = combo({"Linear coefficients", "Polar rotation and stretch"}, "timelineAffine");
  palette_ = combo({"RGB", "HSV · shortest hue", "Linear-light RGB", "HSL · shortest hue"}, "timelinePalette");
  angle_ = combo({"Shortest rotation", "Unwrapped rotation"}, "timelineAngle");
  topology_ = combo({"Require matching transform IDs", "Fade unmatched transforms"}, "timelineTopology");
  seedMode_ = combo({"Fixed across frames", "Deterministic per frame"}, "timelineSeedMode");
  seed_ = new QLineEdit;
  seed_->setObjectName("timelineSeed");
  form->addRow("Playback loop", loop_);
  form->addRow("Affine interpolation", affine_);
  form->addRow("Palette interpolation", palette_);
  form->addRow("Camera rotation", angle_);
  form->addRow("Transform correspondence", topology_);
  form->addRow("Sampling seed", seed_);
  form->addRow("Seed behavior", seedMode_);
  auto settingsNote = new QLabel("Each keyframe stores its source flame. Continuous values interpolate; integer and discrete settings hold until the next keyframe. Hold creates an explicit cut. Playback may run below the chosen fps while previews render.");
  settingsNote->setWordWrap(true);
  form->addRow(settingsNote);
  tabs->addTab(scrollPanel(settingsPanel), "Timing & interpolation");

  auto outputPanel = new QWidget;
  auto output = new QFormLayout(outputPanel);
  directory_ = new QLineEdit;
  directory_->setObjectName("timelineOutputDirectory");
  auto browse = new QPushButton("Browse…");
  auto path = new QHBoxLayout;
  path->addWidget(directory_);
  path->addWidget(browse);
  output->addRow("Output folder", path);
  prefix_ = new QLineEdit("frame");
  prefix_->setObjectName("timelineOutputPrefix");
  output->addRow("Filename prefix", prefix_);
  first_ = spin(0, 999999, 0, "timelineExportFirst");
  last_ = spin(0, 999999, int(clip_.frame_count - 1), "timelineExportLast");
  step_ = spin(1, 1000000, 1, "timelineExportStep");
  auto range = new QHBoxLayout;
  range->addWidget(first_); range->addWidget(new QLabel("through"));
  range->addWidget(last_); range->addWidget(new QLabel("step")); range->addWidget(step_);
  output->addRow("Frames (inclusive)", range);
  width_ = spin(0, 1000000, 0, "timelineExportWidth");
  height_ = spin(0, 1000000, 0, "timelineExportHeight");
  width_->setSpecialValueText("First frame"); height_->setSpecialValueText("First frame");
  auto size = new QHBoxLayout;
  size->addWidget(width_); size->addWidget(new QLabel("×")); size->addWidget(height_);
  output->addRow("Fixed output size", size);
  format_ = new QComboBox;
  format_->setObjectName("timelineExportFormat");
  for (const auto f : {ImageFormat::png16, ImageFormat::png8, ImageFormat::tiff16,
                       ImageFormat::tiff8, ImageFormat::ppm16, ImageFormat::ppm8, ImageFormat::bmp8})
    format_->addItem(text(image_format_name(f)), int(f));
  output->addRow("Image format", format_);
  alpha_ = new QCheckBox("Transparent background (PNG/TIFF)");
  output->addRow(alpha_);
  samples_ = new QLineEdit("1000000"); samples_->setObjectName("timelineExportSamples");
  output->addRow("Samples per frame", samples_);
  threads_ = spin(0, 256, 0, "timelineExportThreads");
  threads_->setSpecialValueText("Automatic");
  output->addRow("CPU workers", threads_);
  memory_ = spin(16, 1048576, 512, "timelineExportMemory"); memory_->setSuffix(" MiB");
  disk_ = spin(1, 1048576, 16384, "timelineExportDisk"); disk_->setSuffix(" MiB");
  output->addRow("Working memory", memory_);
  output->addRow("Batch disk budget", disk_);
  backend_ = combo({"CPU", "Automatic", "Vulkan"}, "timelineExportBackend");
  backend_->setCurrentIndex(int(settings_.backend));
  device_ = new QLineEdit(text(settings_.device));
  device_->setPlaceholderText("Automatic device");
  gpuMemory_ = spin(16, 1048576, int(std::min<std::uint64_t>(settings_.gpu_memory_mib, 1048576)), "timelineExportGpuMemory");
  gpuMemory_->setSuffix(" MiB");
  fallback_ = new QCheckBox("Allow CPU fallback"); fallback_->setChecked(settings_.allow_cpu_fallback);
  output->addRow("Renderer", backend_);
  output->addRow("Vulkan device", device_);
  output->addRow("Vulkan memory", gpuMemory_);
  output->addRow(fallback_);
  replace_ = new QCheckBox("Replace existing frame images");
  replace_->setObjectName("timelineExportReplace");
  output->addRow(replace_);
  auto outputNote = new QLabel("Queue up to 1,000 frames per batch. Filenames use the original zero-based frame number. Jobs retain independent snapshots and checkpoints. Completed images remain available if a later job stops.");
  outputNote->setWordWrap(true);
  output->addRow(outputNote);
  tabs->addTab(scrollPanel(outputPanel), "Image sequence");
  auto tracks = new QWidget;
  auto trackLayout = new QVBoxLayout(tracks);
  auto trackForm = new QFormLayout;
  property_ = new QComboBox;
  property_->setObjectName("timelineProperty");
  for (const auto &property : animated_properties) property_->addItem(text(property.name));
  propertyValue_ = new QLineEdit;
  propertyValue_->setObjectName("timelinePropertyValue");
  propertyEasing_ = combo({"Hold", "Linear", "Smooth", "Smoother"}, "timelinePropertyEasing");
  propertyEasing_->setCurrentIndex(int(AnimationEasing::linear));
  trackForm->addRow("Property", property_);
  trackForm->addRow("Value", propertyValue_);
  trackForm->addRow("Outgoing transition", propertyEasing_);
  trackLayout->addLayout(trackForm);
  propertyKeys_ = new QTableWidget(0, 3);
  propertyKeys_->setHorizontalHeaderLabels({"Frame", "Value", "Transition"});
  propertyKeys_->setSelectionBehavior(QAbstractItemView::SelectRows);
  propertyKeys_->setSelectionMode(QAbstractItemView::SingleSelection);
  trackLayout->addWidget(propertyKeys_);
  auto setPropertyKey = new QPushButton("Set key at playhead");
  setPropertyKey->setObjectName("timelineSetPropertyKey");
  auto removePropertyKey = new QPushButton("Remove selected key");
  trackLayout->addWidget(setPropertyKey);
  trackLayout->addWidget(removePropertyKey);
  tabs->addTab(tracks, "Property keys");
  connect(property_, &QComboBox::currentIndexChanged, this, [this] { refreshPropertyKeys(); });
  connect(propertyKeys_, &QTableWidget::cellClicked, this, [this](int row, int) {
    const auto &key = clip_.tracks.at(property_->currentIndex()).at(row);
    setFrame(int(key.frame));
    propertyValue_->setText(QString::number(key.value, 'g', 17));
    propertyEasing_->setCurrentIndex(int(key.easing));
  });
  connect(setPropertyKey, &QPushButton::clicked, this, [this] {
    run([&] {
      const auto i = property_->currentIndex();
      bool valid = false;
      const auto value = propertyValue_->text().toDouble(&valid);
      if (!valid) throw std::invalid_argument("Enter a numeric property value");
      auto candidate = clip_;
      candidate.version = AnimationClip::current_version;
      auto &track = candidate.tracks.at(i);
      auto pos = std::lower_bound(track.begin(), track.end(), frame_,
          [](const PropertyKey &key, std::uint32_t f) { return key.frame < f; });
      PropertyKey key{frame_, value, AnimationEasing(propertyEasing_->currentIndex())};
      if (pos != track.end() && pos->frame == frame_) *pos = key;
      else track.insert(pos, key);
      commit(std::move(candidate));
    });
  });
  connect(removePropertyKey, &QPushButton::clicked, this, [this] {
    const int row = propertyKeys_->currentRow();
    if (row < 0) return;
    run([&] {
      auto candidate = clip_;
      auto &track = candidate.tracks.at(property_->currentIndex());
      track.erase(track.begin() + row);
      commit(std::move(candidate));
    });
  });
  split->setSizes({540, 640});
  status_ = new QLabel;
  status_->setWordWrap(true);
  status_->setObjectName("timelineStatus");
  root->addWidget(status_);
  auto buttons = new QHBoxLayout;
  undo_ = new QPushButton("Undo"); undo_->setObjectName("timelineUndo");
  redo_ = new QPushButton("Redo"); redo_->setObjectName("timelineRedo");
  buttons->addWidget(undo_); buttons->addWidget(redo_); buttons->addStretch();
  auto cancel = new QPushButton("Cancel");
  use_ = new QPushButton("Use frame in editor"); use_->setObjectName("timelineUseFrame");
  queue_ = new QPushButton("Apply && queue frames"); queue_->setObjectName("timelineQueue");
  apply_ = new QPushButton("Apply timeline"); apply_->setObjectName("timelineApply");
  apply_->setDefault(true);
  for (auto w : {cancel, use_, queue_, apply_}) buttons->addWidget(w);
  root->addLayout(buttons);

  connect(ruler_, &TimelineRuler::frameSelected, this, &AnimationDialog::setFrame);
  connect(scrubber_, &QSlider::valueChanged, this, &AnimationDialog::setFrame);
  connect(frameSpin_, &QSpinBox::valueChanged, this, &AnimationDialog::setFrame);
  connect(start, &QPushButton::clicked, this, [this] { stop(); setFrame(0); });
  connect(end, &QPushButton::clicked, this, [this] { stop(); setFrame(int(clip_.frame_count - 1)); });
  connect(previous, &QPushButton::clicked, this, [this] { stop(); setFrame(int(frame_) - 1); });
  connect(next, &QPushButton::clicked, this, [this] { stop(); setFrame(int(frame_) + 1); });
  connect(play_, &QPushButton::clicked, this, [this] { playing_ ? stop() : play(); });
  connect(&timer_, &QTimer::timeout, this, [this] {
    if (!playing_ || preview_.isBusy() || previewPending_) return;
    if (clip_.loop == AnimationLoop::once && frame_ == clip_.frame_count - 1) { stop(); return; }
    const auto tick = ++playbackTick_;
    setFrame(int(animation_frame_at(clip_, tick)));
    playbackTick_ = tick;
  });
  connect(&preview_, &PreviewController::imageReady, this,
          [this](const QImage &image, quint64, quint64, quint64, unsigned, qint64, bool final) {
    canvas_->setImage(image);
    if (final) { previewPending_ = false; emit framePreviewed(renderingFrame_); }
  });
  connect(&preview_, &PreviewController::failed, this, [this](const QString &error) {
    previewPending_ = false; stop(); status_->setText(error);
  });
  connect(canvas_, &PreviewCanvas::viewportChanged, this, [this] { if (!syncing_) requestPreview(); });
  connect(keys_, &QTableWidget::cellChanged, this, &AnimationDialog::updateKeyframe);
  connect(keys_, &QTableWidget::cellClicked, this, [this](int row, int) {
    if (row >= 0 && std::size_t(row) < clip_.keyframes.size()) { stop(); setFrame(int(clip_.keyframes[row].frame)); }
  });
  connect(capture, &QPushButton::clicked, this, [this] { run([&] { captureKeyframe(source_, selected_, frame_); }); });
  connect(replaceKey, &QPushButton::clicked, this, [this] { run([&] {
    const auto row = keys_->currentRow();
    if (row < 0) throw std::invalid_argument("Select a keyframe to replace");
    captureKeyframe(source_, selected_, clip_.keyframes.at(row).frame, true);
  }); });
  connect(removeKey, &QPushButton::clicked, this, [this] { run([&] {
    const auto row = keys_->currentRow();
    if (row < 0) throw std::invalid_argument("Select a keyframe to remove");
    auto c = clip_; c.keyframes.erase(c.keyframes.begin() + row); commit(std::move(c));
  }); });
  connect(addLibrary, &QPushButton::clicked, this, [this] { run([&] {
    captureKeyframe(source_, std::size_t(library_->currentIndex()), frame_);
  }); });
  connect(import, &QPushButton::clicked, this, &AnimationDialog::importKeyframe);
  connect(timed, &QPushButton::clicked, this, &AnimationDialog::importTimed);
  connect(correspondence, &QPushButton::clicked, this, &AnimationDialog::editCorrespondence);
  connect(gradient, &QPushButton::clicked, this, [this] { editKeyframe(true); });
  connect(adjust, &QPushButton::clicked, this, [this] { editKeyframe(false); });
  connect(makePreset, &QPushButton::clicked, this, [this] { choosePreset(false); });
  connect(morph, &QPushButton::clicked, this, [this] { choosePreset(true); });
  connect(frames_, &QSpinBox::editingFinished, this, &AnimationDialog::updateSettings);
  for (auto w : {rateNumerator_, rateDenominator_})
    connect(w, &QSpinBox::editingFinished, this, [this] {
      // A rational rate is one setting. Let focus move between its fields
      // before validating the pair, so 30/1 can become 30000/1001 directly.
      QTimer::singleShot(0, this, [this] {
        const auto focus = QApplication::focusWidget();
        if (focus != rateNumerator_ && focus != rateDenominator_ &&
            !rateNumerator_->isAncestorOf(focus) && !rateDenominator_->isAncestorOf(focus))
          updateSettings();
      });
    });
  connect(ratePreset_, &QComboBox::currentIndexChanged, this, [this] {
    if (syncing_ || ratePreset_->currentIndex() == 0) return;
    run([&] {
      const auto values = ratePreset_->currentData().toString().split('/');
      auto c = clip_; c.rate = {values[0].toUInt(), values[1].toUInt()}; commit(std::move(c));
    });
  });
  for (auto w : {name_, seed_}) connect(w, &QLineEdit::editingFinished, this, &AnimationDialog::updateSettings);
  for (auto w : {loop_, affine_, palette_, angle_, topology_, seedMode_})
    connect(w, &QComboBox::currentIndexChanged, this, [this] { if (!syncing_) updateSettings(); });
  connect(duration_, &QDoubleSpinBox::editingFinished, this, [this] { if (!syncing_) run([&] {
    auto c = clip_;
    const double count = std::round(duration_->value() * c.rate.numerator / c.rate.denominator);
    if (count < 1 || count > animation_frame_limit) throw std::invalid_argument("Duration exceeds the supported frame count");
    c.frame_count = std::uint32_t(count); commit(std::move(c));
  }); });
  connect(undo_, &QPushButton::clicked, this, [this] {
    if (historyIndex_) { --historyIndex_; clip_ = history_[historyIndex_].clip; stop(); sync(); }
  });
  connect(redo_, &QPushButton::clicked, this, [this] {
    if (historyIndex_ + 1 < history_.size()) { ++historyIndex_; clip_ = history_[historyIndex_].clip; stop(); sync(); }
  });
  connect(browse, &QPushButton::clicked, this, [this] {
    const auto path = QFileDialog::getExistingDirectory(this, "Sequence output folder", directory_->text());
    if (!path.isEmpty()) directory_->setText(path);
  });
  connect(format_, &QComboBox::currentIndexChanged, this, [this] {
    const bool supported = image_format_supports_alpha(ImageFormat(format_->currentData().toInt()));
    alpha_->setEnabled(supported); if (!supported) alpha_->setChecked(false);
  });
  connect(cancel, &QPushButton::clicked, this, &AnimationDialog::reject);
  connect(apply_, &QPushButton::clicked, this, [this] { chooseDisposition(Disposition::apply); });
  connect(use_, &QPushButton::clicked, this, [this] { chooseDisposition(Disposition::use_frame); });
  connect(queue_, &QPushButton::clicked, this, [this] { chooseDisposition(Disposition::queue); });
  sync();
}
AnimationDialog::~AnimationDialog() { preview_.stop(); }
void AnimationDialog::reject() { stop(); preview_.stop(); QDialog::reject(); }
void AnimationDialog::run(const std::function<void()> &operation) {
  try { operation(); }
  catch (const std::exception &e) {
    const auto message = text(e.what());
    // Rejected edits must not remain displayed as if they were committed.
    sync();
    status_->setText(message);
  }
}
void AnimationDialog::commit(AnimationClip candidate) {
  const auto bytes = serialize_animation(candidate).size();
  if (candidate == clip_) return;
  stop();
  history_.resize(historyIndex_ + 1);
  history_.push_back({candidate, bytes});
  std::size_t total = 0;
  for (const auto &entry : history_) total += entry.bytes;
  while (history_.size() > 2 && (history_.size() > 16 || total > 32 * 1024 * 1024)) {
    total -= history_.front().bytes; history_.erase(history_.begin());
  }
  historyIndex_ = history_.size() - 1;
  clip_ = std::move(candidate);
  sync();
}
void AnimationDialog::setClip(AnimationClip clip) { commit(std::move(clip)); }
void AnimationDialog::refreshPropertyKeys() {
  const auto index = property_->currentIndex();
  const auto &track = clip_.tracks.at(index);
  propertyKeys_->setRowCount(int(track.size()));
  for (int row = 0; row < int(track.size()); ++row) {
    const auto &key = track[row];
    propertyKeys_->setItem(row, 0, readOnly(QString::number(key.frame)));
    propertyKeys_->setItem(row, 1, readOnly(QString::number(key.value, 'g', 17)));
    propertyKeys_->setItem(row, 2, readOnly(text(animation_name(key.easing))));
  }
  propertyValue_->clear();
  if (current_) propertyValue_->setPlaceholderText(QString::number((*current_).*animated_properties[index].member));
}
void AnimationDialog::sync() {
  syncing_ = true;
  frame_ = std::min(frame_, clip_.frame_count - 1);
  name_->setText(text(clip_.name)); frames_->setValue(int(clip_.frame_count));
  rateNumerator_->setValue(int(clip_.rate.numerator)); rateDenominator_->setValue(int(clip_.rate.denominator));
  ratePreset_->setCurrentIndex(std::max(0, ratePreset_->findData(QString("%1/%2").arg(clip_.rate.numerator).arg(clip_.rate.denominator))));
  duration_->setValue(animation_duration(clip_));
  loop_->setCurrentIndex(int(clip_.loop)); affine_->setCurrentIndex(int(clip_.affine));
  palette_->setCurrentIndex(int(clip_.palette)); angle_->setCurrentIndex(int(clip_.camera_angle));
  topology_->setCurrentIndex(int(clip_.topology)); seedMode_->setCurrentIndex(int(clip_.seed_mode));
  seed_->setText(QString::number(qulonglong(clip_.seed)));
  scrubber_->setRange(0, int(clip_.frame_count - 1)); frameSpin_->setRange(0, int(clip_.frame_count - 1));
  const bool fullRange = last_->value() == last_->maximum();
  first_->setMaximum(int(clip_.frame_count - 1)); last_->setMaximum(int(clip_.frame_count - 1));
  if (fullRange) last_->setValue(int(clip_.frame_count - 1));
  const auto selection = keys_->currentRow();
  keys_->setRowCount(int(clip_.keyframes.size()));
  for (std::size_t i = 0; i < clip_.keyframes.size(); ++i) {
    const auto &key = clip_.keyframes[i];
    keys_->setItem(int(i), 0, new QTableWidgetItem(QString::number(key.frame)));
    keys_->setItem(int(i), 1, readOnly(QString::number(animation_seconds(clip_, key.frame), 'f', 3)));
    keys_->setItem(int(i), 2, new QTableWidgetItem(text(key.name)));
    auto easing = combo({"Hold / cut", "Linear", "Smooth", "Smoother"}, "timelineKeyEasing");
    easing->setCurrentIndex(int(key.easing));
    connect(easing, &QComboBox::currentIndexChanged, this, [this, i](int value) {
      if (syncing_) return;
      run([&] { auto c = clip_; c.keyframes.at(i).easing = AnimationEasing(value); commit(std::move(c)); });
    });
    keys_->setCellWidget(int(i), 3, easing);
    keys_->setItem(int(i), 4, readOnly(QString::number(key.transform_ids.size())));
  }
  if (!clip_.keyframes.empty()) keys_->selectRow(std::clamp(selection, 0, int(clip_.keyframes.size()) - 1));
  undo_->setEnabled(historyIndex_ > 0); redo_->setEnabled(historyIndex_ + 1 < history_.size());
  prepared_.reset();
  QStringList notes;
  try {
    prepared_.emplace(clip_);
    for (const auto &line : prepared_->diagnostics()) notes << text(line);
    status_->setText(QString("%1 keyframe(s) · %2 frames · %3 fps · private changes")
                         .arg(clip_.keyframes.size()).arg(clip_.frame_count)
                         .arg(double(clip_.rate.numerator) / clip_.rate.denominator, 0, 'g', 8));
  } catch (const std::exception &e) {
    notes << text(e.what());
    status_->setText("Timeline preserved. Resolve the diagnostic to preview or export frames.");
  }
  diagnostics_->setText(notes.join('\n'));
  diagnostics_->setVisible(!notes.isEmpty());
  use_->setEnabled(prepared_.has_value()); queue_->setEnabled(prepared_.has_value());
  play_->setEnabled(prepared_.has_value() && clip_.frame_count > 1);
  syncing_ = false;
  setFrame(int(frame_));
  refreshPropertyKeys();
}
void AnimationDialog::setFrame(int frame) {
  if (syncing_) return;
  frame_ = std::uint32_t(std::clamp(frame, 0, int(clip_.frame_count - 1)));
  playbackTick_ = frame_;
  syncing_ = true;
  frameSpin_->setValue(int(frame_)); scrubber_->setValue(int(frame_));
  ruler_->setTimeline(clip_, frame_);
  time_->setText(QString("Frame %1 / %2  ·  %3 s  ·  %4 s total")
                     .arg(frame_).arg(clip_.frame_count - 1)
                     .arg(animation_seconds(clip_, frame_), 0, 'f', 3)
                     .arg(animation_duration(clip_), 0, 'f', 3));
  syncing_ = false;
  requestPreview();
}
void AnimationDialog::requestPreview() {
  if (syncing_) return;
  current_.reset();
  if (!prepared_) { preview_.stop(); previewPending_ = false; canvas_->setMessage("Resolve the timeline diagnostic to preview"); return; }
  try {
    auto frame = prepared_->evaluate(frame_);
    current_ = std::move(frame.flame);
    auto settings = settings_; settings.seed = frame.seed;
    if (playing_)
      settings.sample_limit = std::min<std::uint64_t>(settings.sample_limit, 160000);
    preview_.setSettings(settings);
    renderingFrame_ = int(frame_); previewPending_ = true;
    preview_.request(*current_, canvas_->size());
    use_->setEnabled(true); queue_->setEnabled(true);
  } catch (const std::exception &e) {
    preview_.stop(); previewPending_ = false; stop();
    status_->setText(text(e.what())); canvas_->setMessage(text(e.what()));
    use_->setEnabled(false); queue_->setEnabled(false);
  }
}
void AnimationDialog::play() {
  if (!prepared_ || clip_.frame_count < 2 || playing_) return;
  if (clip_.loop == AnimationLoop::once && frame_ == clip_.frame_count - 1) setFrame(0);
  playing_ = true; play_->setText("Pause"); playbackTick_ = frame_;
  requestPreview();
  timer_.start(std::max(1, int(std::llround(1000.0 * clip_.rate.denominator / clip_.rate.numerator))));
  emit playbackChanged(true);
}
void AnimationDialog::stop() {
  timer_.stop();
  if (!playing_) return;
  playing_ = false; play_->setText("Play"); emit playbackChanged(false);
  // Refine the stopped playhead at the user's chosen preview quality.
  QTimer::singleShot(0, this, [this] { if (!playing_) requestPreview(); });
}
void AnimationDialog::updateSettings() {
  if (syncing_) return;
  run([&] { commit(settingsClip()); });
}
AnimationClip AnimationDialog::settingsClip() const {
    auto c = clip_;
    c.name = name_->text().toStdString(); c.frame_count = std::uint32_t(frames_->value());
    c.rate = {std::uint32_t(rateNumerator_->value()), std::uint32_t(rateDenominator_->value())};
    c.loop = AnimationLoop(loop_->currentIndex()); c.affine = AnimationAffine(affine_->currentIndex());
    c.palette = AnimationPalette(palette_->currentIndex()); c.camera_angle = AnimationAngle(angle_->currentIndex());
    c.topology = AnimationTopology(topology_->currentIndex()); c.seed_mode = AnimationSeedMode(seedMode_->currentIndex());
    c.seed = unsignedValue(seed_, "Sampling seed");
    return c;
}
void AnimationDialog::captureKeyframe(const LegacyDocument &document, std::size_t selected,
                                      std::uint32_t frame, bool replace) {
  auto c = clip_;
  const auto existing = std::find_if(c.keyframes.begin(), c.keyframes.end(), [&](const auto &k) { return k.frame == frame; });
  if (existing != c.keyframes.end() && !replace)
    throw std::invalid_argument("A keyframe already occupies this frame. Use Replace selected or move the playhead.");
  auto key = capture_animation_keyframe(document, selected, frame);
  if (existing != c.keyframes.end()) {
    if (key.transform_ids.size() == existing->transform_ids.size()) key.transform_ids = existing->transform_ids;
    key.easing = existing->easing; key.name = existing->name; *existing = std::move(key);
  } else c.keyframes.push_back(std::move(key));
  std::sort(c.keyframes.begin(), c.keyframes.end(), [](const auto &a, const auto &b) { return a.frame < b.frame; });
  commit(std::move(c));
}
void AnimationDialog::updateKeyframe(int row, int column) {
  if (syncing_ || row < 0 || std::size_t(row) >= clip_.keyframes.size()) return;
  run([&] {
    auto c = clip_;
    if (column == 0) {
      bool ok = false; const auto value = keys_->item(row, 0)->text().toUInt(&ok);
      if (!ok) throw std::invalid_argument("Keyframe position must be a whole frame number");
      c.keyframes[row].frame = value;
      std::sort(c.keyframes.begin(), c.keyframes.end(), [](const auto &a, const auto &b) { return a.frame < b.frame; });
    } else if (column == 2) c.keyframes[row].name = keys_->item(row, 2)->text().toStdString();
    else return;
    commit(std::move(c));
  });
}
void AnimationDialog::editCorrespondence() {
  run([&] {
    const auto row = keys_->currentRow();
    if (row < 0) throw std::invalid_argument("Select a keyframe first");
    QDialog dialog(this); dialog.setWindowTitle("Transform correspondence"); dialog.resize(510, 440);
    auto layout = new QVBoxLayout(&dialog);
    auto note = new QLabel("Give corresponding ordinary transforms the same ID in each keyframe. IDs must be unique within a keyframe. Different IDs require Fade unmatched transforms.");
    note->setWordWrap(true); layout->addWidget(note);
    auto table = new QTableWidget(int(clip_.keyframes[row].transform_ids.size()), 2);
    table->setObjectName("timelineCorrespondenceTable");
    table->setHorizontalHeaderLabels({"Source transform", "Correspondence ID"});
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    for (int i = 0; i < table->rowCount(); ++i) {
      table->setItem(i, 0, readOnly(QString::number(i + 1)));
      table->setItem(i, 1, new QTableWidgetItem(text(clip_.keyframes[row].transform_ids[i])));
    }
    layout->addWidget(table);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;
    auto c = clip_;
    for (int i = 0; i < table->rowCount(); ++i) c.keyframes[row].transform_ids[i] = table->item(i, 1)->text().trimmed().toStdString();
    commit(std::move(c));
  });
}
void AnimationDialog::editKeyframe(bool gradient) {
  run([&] {
    const auto row = keys_->currentRow();
    if (row < 0) throw std::invalid_argument("Select a keyframe first");
    stop();
    auto c = clip_; auto &key = c.keyframes[row];
    auto document = LegacyDocument::parse(key.library_bytes);
    auto flame = document.render_model(key.selected_index);
    if (gradient) {
      GradientDialog dialog(flame, this, settings_);
      if (dialog.exec() != QDialog::Accepted) return;
      applyCreativeFlame(document, key.selected_index, dialog.resultFlame());
    } else {
      AdjustmentDialog dialog(flame, this, settings_);
      if (dialog.exec() != QDialog::Accepted) return;
      document.edit(key.selected_index, dialog.edits());
    }
    key.library_bytes = document.bytes(); commit(std::move(c));
  });
}
void AnimationDialog::importKeyframe() {
  const auto path = QFileDialog::getOpenFileName(this, "Import a keyframe", {}, "Projects and flames (*.apoproject *.flame *.xml);;All files (*)");
  if (path.isEmpty()) return;
  run([&] {
    auto file = load_document_file(filesystemPath(path));
    const auto &document = file.document;
    QStringList choices;
    for (std::size_t i = 0; i < document.entries().size(); ++i)
      choices << QString("%1 · %2").arg(i + 1).arg(text(document.entries()[i].name));
    bool ok = true;
    int index = file.project ? int(file.project->selected_index) : 0;
    if (choices.size() > 1) {
      const auto chosen = QInputDialog::getItem(this, "Choose keyframe flame", "Flame", choices, index, false, &ok);
      index = choices.indexOf(chosen);
    }
    if (!ok || index < 0) return;
    captureKeyframe(document, std::size_t(index), frame_);
    protectedFiles_.push_back(filesystemPath(path));
  });
}
void AnimationDialog::importTimed() {
  const auto path = QFileDialog::getOpenFileName(this, "Import a timed flame library", {}, "Flame libraries (*.flame *.xml);;All files (*)");
  if (path.isEmpty()) return;
  run([&] {
    bool ok = false;
    const QStringList choices{"Frames", "Seconds"};
    const auto units = QInputDialog::getItem(this, "Interpret legacy time", "The source time values represent", choices, 0, false, &ok);
    if (!ok) return;
    const auto file = load_document_file(filesystemPath(path));
    commit(animation_from_timed_library(file.document, units == "Frames" ? AnimationTimeUnits::frames : AnimationTimeUnits::seconds, clip_.rate, clip_.seed));
    protectedFiles_.push_back(filesystemPath(path));
  });
}
void AnimationDialog::choosePreset(bool morph) {
  run([&] {
    if (morph) {
      auto from = capture_animation_keyframe(source_, selected_, 0);
      auto to = capture_animation_keyframe(source_, std::size_t(library_->currentIndex()), clip_.frame_count - 1);
      commit(make_morph_animation(source_, selected_, source_, std::size_t(library_->currentIndex()),
                                  clip_.frame_count, clip_.rate, from.transform_ids, to.transform_ids, clip_.topology, clip_.seed));
    } else commit(make_animation_preset(source_, selected_, AnimationPreset(presets_->currentIndex()), clip_.frame_count, clip_.rate, clip_.seed));
  });
}
AnimationExportOptions AnimationDialog::exportOptions() const {
  if (directory_->text().trimmed().isEmpty()) throw std::invalid_argument("Choose an output folder on the Image sequence tab");
  AnimationExportOptions o;
  o.output_directory = filesystemPath(directory_->text().trimmed());
  o.prefix = prefix_->text().toStdString(); o.first = std::uint32_t(first_->value());
  o.last = std::uint32_t(last_->value()); o.step = std::uint32_t(step_->value());
  o.width = width_->value(); o.height = height_->value();
  o.render.samples = unsignedValue(samples_, "Samples per frame");
  o.render.threads = unsigned(threads_->value()); o.render.memory_mib = std::uint64_t(memory_->value());
  o.render.backend = RenderBackend(backend_->currentIndex()); o.render.device = device_->text().toStdString();
  o.render.gpu_memory_mib = std::uint64_t(gpuMemory_->value()); o.render.allow_cpu_fallback = fallback_->isChecked();
  o.file.disk_mib = std::uint64_t(disk_->value());
  o.file.image = {ImageFormat(format_->currentData().toInt()), alpha_->isChecked()};
  o.replace_existing = replace_->isChecked(); o.protected_files = protectedFiles_;
  return o;
}
void AnimationDialog::chooseDisposition(Disposition value) {
  run([&] {
    // Commit any text field still being edited before accepting the dialog.
    commit(settingsClip());
    (void)serialize_animation(clip_);
    if (value != Disposition::apply && !current_) throw std::invalid_argument("The current frame cannot be evaluated");
    if (value == Disposition::queue) (void)exportOptions();
    disposition_ = value; stop(); preview_.stop(); accept();
  });
}
} // namespace apo::editor
