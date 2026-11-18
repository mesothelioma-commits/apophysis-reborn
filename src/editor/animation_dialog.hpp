#pragma once

#include "../animation_export.hpp"
#include "canvases.hpp"
#include "preview_controller.hpp"
#include <QDialog>
#include <QTimer>
#include <functional>
#include <optional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;
class QSpinBox;
class QTableWidget;

namespace apo::editor {

class TimelineRuler : public QWidget {
  Q_OBJECT
public:
  explicit TimelineRuler(QWidget *parent = nullptr);
  void setTimeline(const AnimationClip &, std::uint32_t frame);
signals:
  void frameSelected(int frame);
protected:
  void paintEvent(QPaintEvent *) override;
  void mousePressEvent(QMouseEvent *) override;
  void mouseMoveEvent(QMouseEvent *) override;
private:
  std::uint32_t count_ = 1, frame_ = 0;
  std::vector<std::uint32_t> keys_;
  void choose(double x);
};

// All changes remain private until the caller applies the accepted clip.
class AnimationDialog : public QDialog {
  Q_OBJECT
public:
  enum class Disposition { apply, use_frame, queue };
  AnimationDialog(const LegacyDocument &, std::size_t selected,
                  std::optional<AnimationClip> clip = {},
                  QWidget *parent = nullptr,
                  const PreviewSettings &settings = {});
  ~AnimationDialog() override;
  const AnimationClip &clip() const { return clip_; }
  std::uint32_t frame() const { return frame_; }
  bool isPlaying() const { return playing_; }
  Disposition disposition() const { return disposition_; }
  AnimationExportOptions exportOptions() const;
  const std::optional<Flame> &currentFlame() const { return current_; }
  const QImage &previewImage() const { return canvas_->image(); }
  void setClip(AnimationClip clip);
  void setFrame(int frame);
  void play();
  void stop();
  void captureKeyframe(const LegacyDocument &, std::size_t selected,
                       std::uint32_t frame, bool replace = false);
signals:
  void framePreviewed(int frame);
  void playbackChanged(bool playing);
protected:
  void reject() override;
private:
  struct History { AnimationClip clip; std::size_t bytes; };
  LegacyDocument source_;
  std::size_t selected_;
  AnimationClip clip_;
  std::optional<PreparedAnimation> prepared_;
  std::optional<Flame> current_;
  PreviewController preview_;
  PreviewSettings settings_;
  PreviewCanvas *canvas_;
  TimelineRuler *ruler_;
  QSlider *scrubber_;
  QSpinBox *frameSpin_, *frames_, *rateNumerator_, *rateDenominator_;
  QDoubleSpinBox *duration_;
  QLineEdit *name_, *seed_;
  QComboBox *loop_, *affine_, *palette_, *angle_, *topology_, *seedMode_;
  QComboBox *library_, *presets_;
  QComboBox *ratePreset_;
  QTableWidget *keys_;
  QLabel *status_, *time_, *diagnostics_;
  QPushButton *play_, *undo_, *redo_, *apply_, *use_, *queue_;
  QLineEdit *directory_, *prefix_, *samples_, *device_;
  QSpinBox *first_, *last_, *step_, *width_, *height_, *threads_, *memory_, *disk_, *gpuMemory_;
  QComboBox *format_, *backend_;
  QCheckBox *alpha_, *replace_, *fallback_;
  QTimer timer_;
  std::uint32_t frame_ = 0;
  std::int64_t playbackTick_ = 0;
  int renderingFrame_ = 0;
  bool syncing_ = false, playing_ = false, previewPending_ = false;
  Disposition disposition_ = Disposition::apply;
  std::vector<History> history_;
  std::size_t historyIndex_ = 0;
  std::vector<std::filesystem::path> protectedFiles_;
  void commit(AnimationClip clip);
  void sync();
  void requestPreview();
  void updateSettings();
  AnimationClip settingsClip() const;
  void updateKeyframe(int row, int column);
  void editCorrespondence();
  void editKeyframe(bool gradient);
  void importKeyframe();
  void importTimed();
  void choosePreset(bool morph);
  void run(const std::function<void()> &operation);
  void chooseDisposition(Disposition);
};
} // namespace apo::editor
