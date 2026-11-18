#pragma once
#include "../render.hpp"

#include <QImage>
#include <QObject>
#include <QSize>
#include <QString>
#include <QTimer>

#include <memory>
#include <optional>

namespace apo::editor {

struct PreviewSettings {
    std::uint64_t sample_limit = 2000000;
    std::uint64_t seed = 123456;
    std::uint64_t memory_mib = 256;
    unsigned threads = 0; // Automatic preview leaves one hardware thread for the interface.
    int debounce_ms = 100;
    RenderBackend backend = RenderBackend::cpu;
    std::string device;
    std::uint64_t gpu_memory_mib = 512;
    bool allow_cpu_fallback = true;
};

// Public methods run on the object's Qt thread. The worker owns every render snapshot.
class PreviewController : public QObject {
    Q_OBJECT
  public:
    explicit PreviewController(QObject *parent = nullptr);
    ~PreviewController() override;

    void setSettings(const PreviewSettings &settings);
    const PreviewSettings &settings() const {
        return settings_;
    }
    void request(const Flame &flame, QSize viewport);
    void requestFileRender(const Flame &flame, const QString &path, RenderOptions options);
    void setPaused(bool paused);
    bool isPaused() const {
        return paused_;
    }
    bool isBusy() const {
        return busy_;
    }
    void restart();
    void stop();

  signals:
    void imageReady(const QImage &image, quint64 samples, quint64 plotted, quint64 invalid,
                    unsigned workers, qint64 elapsedMs, bool finalPass);
    void fileReady(const QString &path);
    void statusChanged(const QString &status);
    void busyChanged(bool busy);
    void progressChanged(int percent);
    void backendResolved(const QString &backend, const QString &device, const QString &fallbackReason);
    void failed(const QString &message);

  private:
    struct Worker;
    std::unique_ptr<Worker> worker_;
    QTimer debounce_;
    PreviewSettings settings_;
    // request() takes one immutable copy on the Qt thread. Debouncing, settings
    // changes and restarts then share it; validation/fitting run on the worker.
    std::shared_ptr<const Flame> latest_;
    QSize viewport_;
    quint64 generation_ = 0;
    bool paused_ = false;
    bool busy_ = false;

    void queuePreview();
    void schedulePreview();
    quint64 invalidate();
    void setBusy(bool busy);
};
} // namespace apo::editor
