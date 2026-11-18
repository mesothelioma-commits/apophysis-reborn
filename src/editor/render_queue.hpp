#pragma once

#include "../render_file.hpp"
#include <QDialog>
#include <QObject>
#include <QString>
#include <memory>
#include <vector>

class QLabel;
class QPushButton;
class QTableWidget;

namespace apo::editor {

enum class RenderJobState {
  queued,
  running,
  pausing,
  paused,
  cancelled,
  failed,
  complete
};

struct RenderJob {
  QString id, name, source, destination, checkpoint;
  Flame flame;
  RenderOptions render;
  FileRenderOptions file;
  RenderJobState state = RenderJobState::queued;
  QString phase, backend, device, fallback, error;
  std::uint64_t completed_samples = 0, plotted = 0, invalid = 0;
  unsigned completed_strips = 0, total_strips = 0;
  unsigned workers = 0;
  int progress = 0;
  double elapsed_seconds = 0, phase_remaining_seconds = -1;
  bool checkpointed = false, resumed = false;
};

QString renderJobStateText(const RenderJob &job);
QString imageFormatText(ImageFormat format);

// Methods and state inspection belong to the Qt thread. Every worker receives
// an immutable Flame/options snapshot and owns its checkpoint transaction.
class RenderQueue : public QObject {
  Q_OBJECT
public:
  explicit RenderQueue(QString stateFile = {}, QString checkpointRoot = {},
                       QObject *parent = nullptr);
  ~RenderQueue() override;
  const std::vector<RenderJob> &jobs() const;
  const RenderJob *job(const QString &id) const;
  QString activeId() const;
  bool isBusy() const;
  bool isRunning() const;
  QString stateFile() const;
  QString add(const Flame &, const RenderOptions &, const FileRenderOptions &,
              const QString &destination, const QString &source = {});
  void remove(const QString &id);
  void move(const QString &id, int offset);
  void retry(const QString &id);
  void restart(const QString &id);
  void start();
  void pause();
  void cancel(const QString &id);
  void saveTo(const QString &path) const;
  void loadFrom(const QString &path);
  void disableAutoSave();
  void pauseAndWait();

signals:
  void changed();
  void busyChanged(bool busy);
  void runningChanged(bool running);
  void jobFinished(const QString &id, bool succeeded);
  void problem(const QString &message);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  void launchNext();
  void finishActive();
  void updateProgress(const QString &id, const FileRenderProgress &progress);
};

class RenderQueueDialog : public QDialog {
  Q_OBJECT
public:
  explicit RenderQueueDialog(RenderQueue &, QWidget *parent = nullptr);

private:
  RenderQueue &queue_;
  QTableWidget *table_;
  QLabel *details_, *summary_;
  QPushButton *start_, *pause_, *cancel_, *retry_, *remove_, *up_, *down_,
      *restart_;
  QString selectedId() const;
  void refresh();
  void perform(const std::function<void()> &operation);
};
} // namespace apo::editor
