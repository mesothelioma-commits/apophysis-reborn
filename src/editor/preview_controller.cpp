#include "preview_controller.hpp"
#include "../render_file.hpp"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>

#include <algorithm>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>

namespace apo::editor {
namespace {
std::filesystem::path nativePath(const QString &value) {
#ifdef _WIN32
    return std::filesystem::path(value.toStdWString());
#else
    return std::filesystem::path(value.toUtf8().constData());
#endif
}

Flame fittedPreview(Flame flame, QSize viewport) {
    const QSize original(flame.width, flame.height);
    const QSize fitted = original.scaled(viewport.expandedTo(QSize(1, 1)), Qt::KeepAspectRatio);
    const int width = std::max(1, fitted.width());
    const int height = std::max(1, fitted.height());
    flame.scale *= double(width) / flame.width;
    flame.width = width;
    flame.height = height;
    // A preview uses a single histogram sample per screen pixel. The file render
    // retains the document's declared oversampling and output dimensions.
    flame.oversample = 1;
    return flame;
}

QImage copiedImage(const Image &image) {
    return QImage(image.rgb.data(), image.width, image.height, image.width * 3, QImage::Format_RGB888)
        .copy();
}
} // namespace

struct PreviewController::Worker {
    struct Job {
        std::shared_ptr<const Flame> source;
        RenderOptions options;
        quint64 generation;
        QString output;
        QSize viewport;
    };
    struct ProgressMailbox {
        std::mutex mutex;
        quint64 generation = 0;
        int percent = 0;
        bool queued = false;
    };

    PreviewController *const owner;
    std::mutex mutex;
    std::condition_variable wake;
    std::optional<Job> pending;
    std::stop_source active;
    bool closing = false;
    std::shared_ptr<ProgressMailbox> progress = std::make_shared<ProgressMailbox>();
    std::jthread thread;

    explicit Worker(PreviewController *controller) : owner(controller), thread([this] { run(); }) {}
    ~Worker() {
        {
            std::lock_guard lock(mutex);
            closing = true;
            pending.reset();
            active.request_stop();
        }
        wake.notify_one();
        thread.join();
    }

    void cancel() {
        std::lock_guard lock(mutex);
        pending.reset();
        active.request_stop();
    }

    void submit(Job job) {
        {
            std::lock_guard lock(mutex);
            active.request_stop();
            pending = std::move(job);
        }
        wake.notify_one();
    }

    // Qt owns the event callback. Destruction removes queued callbacks after the
    // worker has joined, and generation filtering rejects an already queued old image.
    void post(quint64 generation, std::function<void()> callback) {
        QMetaObject::invokeMethod(
            owner,
            [controller = owner, generation, callback = std::move(callback)] {
                if (controller->generation_ == generation)
                    callback();
            },
            Qt::QueuedConnection);
    }

    void postProgress(quint64 generation, int percent) {
        // Replace an undelivered update instead of adding an event per batch.
        // The state outlives the worker if its Qt callback is still queued.
        const auto mailbox = progress;
        {
            std::lock_guard lock(mailbox->mutex);
            mailbox->generation = generation;
            mailbox->percent = percent;
            if (mailbox->queued)
                return;
            mailbox->queued = true;
        }
        QMetaObject::invokeMethod(
            owner,
            [controller = owner, mailbox] {
                quint64 generation;
                int percent;
                {
                    std::lock_guard lock(mailbox->mutex);
                    generation = mailbox->generation;
                    percent = mailbox->percent;
                    mailbox->queued = false;
                }
                if (controller->generation_ == generation)
                    emit controller->progressChanged(percent);
            },
            Qt::QueuedConnection);
    }

    void run() {
        for (;;) {
            std::optional<Job> job;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock, [this] { return closing || pending.has_value(); });
                if (closing)
                    return;
                job = std::move(pending);
                pending.reset();
                active = std::stop_source();
                job->options.stop_token = active.get_token();
            }
            try {
                renderJob(*job);
            } catch (const RenderCancelled &) {
                // The GUI already published the replacement, pause or stop state.
            } catch (const std::exception &error) {
                const auto message = QString::fromUtf8(error.what());
                post(job->generation, [controller = owner, generation = job->generation, message] {
                    const QPointer<PreviewController> alive(controller);
                    controller->setBusy(false);
                    if (!alive || controller->generation_ != generation)
                        return;
                    emit controller->statusChanged(message);
                    if (!alive || controller->generation_ != generation)
                        return;
                    emit controller->failed(message);
                });
            }
        }
    }

    void renderJob(Job &job) {
        const bool file = !job.output.isEmpty();
        if (job.options.stop_token.stop_requested())
            throw RenderCancelled();
        std::optional<Flame> preview;
        if (!file) {
            // Validation can traverse a large imported model. Neither it nor
            // the second, fitted model copy belongs in the GUI's timer callback.
            validate(*job.source);
            if (job.options.stop_token.stop_requested())
                throw RenderCancelled();
            preview = fittedPreview(*job.source, job.viewport);
        }
        const auto &flame = file ? *job.source : *preview;
        std::vector<std::uint64_t> passes =
            file ? std::vector<std::uint64_t>{job.options.samples}
                 : std::vector<std::uint64_t>{std::min(UINT64_C(80000), job.options.samples),
                                              std::min(UINT64_C(400000), job.options.samples),
                                              job.options.samples};
        passes.erase(std::unique(passes.begin(), passes.end()), passes.end());
        QElapsedTimer elapsed;
        elapsed.start();
        for (std::size_t pass = 0; pass < passes.size(); ++pass) {
            auto options = job.options;
            options.samples = passes[pass];
            int lastPercent = -1;
            // The core invokes progress serially, even with several sampling workers.
            options.progress = [this, &job, pass, count = passes.size(),
                                &lastPercent](std::uint64_t completed, std::uint64_t total) {
                const int percent = int((100 * pass + completed * 100 / total) / count);
                if (percent == lastPercent)
                    return;
                lastPercent = percent;
                postProgress(job.generation, percent);
            };
            RenderResult result;
            if (file) {
                FileRenderOptions fileOptions;
                fileOptions.image.format = image_format_for_path(nativePath(job.output));
                options.progress = {};
                fileOptions.status = [this, &job, &lastPercent](const FileRenderProgress& state) {
                    const int percent = state.phase == "Sampling"
                        ? int(state.completed_samples * 70 / state.total_samples)
                        : 70 + int(std::uint64_t(state.completed_strips) * 29 / state.total_strips);
                    if (lastPercent == percent) return;
                    lastPercent = percent;
                    postProgress(job.generation, percent);
                };
                const auto saved = render_to_file(flame, options, nativePath(job.output), fileOptions);
                result.plotted = saved.plotted;
                result.invalid = saved.invalid;
                result.workers = saved.workers;
                result.backend = saved.backend;
                result.device_name = saved.device_name;
                result.fallback_reason = saved.fallback_reason;
            } else {
                result = render(flame, options);
            }
            if (options.stop_token.stop_requested())
                throw RenderCancelled();
            const QString backend = result.backend == RenderBackend::vulkan ? QStringLiteral("Vulkan")
                                                                            : QStringLiteral("CPU");
            const QString device = QString::fromStdString(result.device_name);
            const QString fallback = QString::fromStdString(result.fallback_reason);
            const QString execution = !fallback.isEmpty() ? QStringLiteral("CPU fallback") : backend;
            const bool finalPass = pass + 1 == passes.size();
            const qint64 duration = elapsed.elapsed();
            if (file) {
                post(job.generation,
                     [controller = owner, generation = job.generation, path = job.output,
                      backend, device, fallback, execution] {
                         const QPointer<PreviewController> alive(controller);
                         controller->setBusy(false);
                         if (!alive || controller->generation_ != generation)
                             return;
                         emit controller->progressChanged(100);
                         if (!alive || controller->generation_ != generation)
                             return;
                         emit controller->backendResolved(backend, device, fallback);
                         if (!alive || controller->generation_ != generation)
                             return;
                         emit controller->statusChanged(QStringLiteral("Render saved · ") + execution);
                         if (!alive || controller->generation_ != generation)
                             return;
                         emit controller->fileReady(path);
                     });
            } else {
                auto image = copiedImage(result.image);
                post(job.generation, [controller = owner, generation = job.generation, image = std::move(image),
                                      samples = options.samples, plotted = result.plotted,
                                      invalid = result.invalid, workers = result.workers, duration,
                                      finalPass, backend, device, fallback, execution] {
                    const QPointer<PreviewController> alive(controller);
                    if (finalPass)
                        controller->setBusy(false);
                    if (!alive || controller->generation_ != generation)
                        return;
                    emit controller->backendResolved(backend, device, fallback);
                    if (!alive || controller->generation_ != generation)
                        return;
                    emit controller->statusChanged((finalPass ? QStringLiteral("Preview ready · ")
                                                              : QStringLiteral("Refining preview · ")) +
                                                   execution);
                    if (!alive || controller->generation_ != generation)
                        return;
                    emit controller->imageReady(image, samples, plotted, invalid, workers, duration,
                                                finalPass);
                });
            }
        }
    }
};

PreviewController::PreviewController(QObject *parent) : QObject(parent) {
    debounce_.setSingleShot(true);
    connect(&debounce_, &QTimer::timeout, this, &PreviewController::queuePreview);
    worker_ = std::make_unique<Worker>(this);
}

PreviewController::~PreviewController() {
    debounce_.stop();
    ++generation_;
    worker_.reset();
}

void PreviewController::setBusy(bool busy) {
    if (busy_ == busy)
        return;
    busy_ = busy;
    emit busyChanged(busy);
}

quint64 PreviewController::invalidate() {
    const auto generation = ++generation_;
    debounce_.stop();
    worker_->cancel();
    emit progressChanged(0);
    return generation;
}

void PreviewController::setSettings(const PreviewSettings &settings) {
    if (!settings.sample_limit || !settings.memory_mib || !settings.gpu_memory_mib ||
        settings.debounce_ms < 0)
        throw std::invalid_argument(
            "Preview samples and memory budgets must be positive; delay must be nonnegative");
    if (settings.backend != RenderBackend::cpu && settings.backend != RenderBackend::automatic &&
        settings.backend != RenderBackend::vulkan)
        throw std::invalid_argument("Choose CPU, Automatic or Vulkan rendering");
    settings_ = settings;
    if (latest_)
        schedulePreview();
}

void PreviewController::request(const Flame &flame, QSize viewport) {
    latest_ = std::make_shared<const Flame>(flame);
    viewport_ = viewport;
    schedulePreview();
}

void PreviewController::schedulePreview() {
    const QPointer<PreviewController> alive(this);
    const auto generation = invalidate();
    if (!alive || generation_ != generation)
        return;
    setBusy(!paused_);
    if (!alive || generation_ != generation)
        return;
    if (paused_) {
        emit statusChanged(QStringLiteral("Preview paused"));
        return;
    }
    emit statusChanged(QStringLiteral("Updating preview"));
    if (!alive || generation_ != generation)
        return;
    debounce_.start(settings_.debounce_ms);
}

void PreviewController::queuePreview() {
    if (!latest_ || paused_)
        return;
    RenderOptions options;
    options.samples = settings_.sample_limit;
    options.seed = settings_.seed;
    options.memory_mib = settings_.memory_mib;
    options.threads = settings_.threads;
    options.backend = settings_.backend;
    options.device = settings_.device;
    options.gpu_memory_mib = settings_.gpu_memory_mib;
    options.allow_cpu_fallback = settings_.allow_cpu_fallback;
    if (!options.threads) {
        const auto hardware = std::thread::hardware_concurrency();
        options.threads = hardware > 1 ? hardware - 1 : 1;
    }
    worker_->submit({latest_, options, generation_, {}, viewport_});
}

void PreviewController::requestFileRender(const Flame &flame, const QString &path,
                                          RenderOptions options) {
    auto snapshot = path.isEmpty() ? std::shared_ptr<const Flame>{}
                                  : std::make_shared<const Flame>(flame);
    const QPointer<PreviewController> alive(this);
    const auto generation = invalidate();
    if (!alive || generation_ != generation)
        return;
    latest_.reset();
    paused_ = false;
    if (path.isEmpty()) {
        setBusy(false);
        if (!alive || generation_ != generation)
            return;
        emit statusChanged(QStringLiteral("Choose an output file"));
        if (!alive || generation_ != generation)
            return;
        emit failed(QStringLiteral("Choose an output file"));
        return;
    }
    setBusy(true);
    if (!alive || generation_ != generation)
        return;
    emit statusChanged(QStringLiteral("Rendering image"));
    if (!alive || generation_ != generation)
        return;
    worker_->submit({std::move(snapshot), std::move(options), generation, path, {}});
}

void PreviewController::setPaused(bool paused) {
    if (paused_ == paused)
        return;
    paused_ = paused;
    if (!paused_ && latest_) {
        schedulePreview();
        return;
    }
    const QPointer<PreviewController> alive(this);
    const auto generation = invalidate();
    if (!alive || generation_ != generation)
        return;
    setBusy(false);
    if (!alive || generation_ != generation)
        return;
    emit statusChanged(paused_ ? QStringLiteral("Preview paused") : QStringLiteral("Preview ready"));
}

void PreviewController::restart() {
    paused_ = false;
    if (latest_)
        schedulePreview();
}

void PreviewController::stop() {
    const QPointer<PreviewController> alive(this);
    const auto generation = invalidate();
    if (!alive || generation_ != generation)
        return;
    latest_.reset();
    setBusy(false);
    if (!alive || generation_ != generation)
        return;
    emit statusChanged(QStringLiteral("Stopped"));
}
} // namespace apo::editor
