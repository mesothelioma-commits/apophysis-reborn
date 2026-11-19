#include "render_queue.hpp"
#include "../gpu.hpp"
#include "../sha256.hpp"
#include "../plugin.hpp"
#include "document_session.hpp"
#include "flame_io.hpp"
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLockFile>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <set>
#include <thread>

namespace apo::editor {
namespace {
QString text(std::string_view s) {
  return QString::fromUtf8(s.data(), qsizetype(s.size()));
}
QString absolute(const QString &path) {
  return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}
QString duration(double seconds) {
  if (!std::isfinite(seconds) || seconds < 0)
    return QStringLiteral("—");
  const auto n = qint64(seconds);
  if (n < 60)
    return QString("%1 s").arg(n);
  if (n < 3600)
    return QString("%1 m %2 s").arg(n / 60).arg(n % 60);
  return QString("%1 h %2 m").arg(n / 3600).arg(n / 60 % 60);
}
QString stateName(RenderJobState state) {
  switch (state) {
  case RenderJobState::queued:
    return "queued";
  case RenderJobState::running:
    return "running";
  case RenderJobState::pausing:
    return "pausing";
  case RenderJobState::paused:
    return "paused";
  case RenderJobState::cancelled:
    return "cancelled";
  case RenderJobState::failed:
    return "failed";
  case RenderJobState::complete:
    return "complete";
  }
  throw std::invalid_argument("Unknown render job state");
}
RenderJobState readState(const QString &value) {
  for (auto state : {RenderJobState::queued, RenderJobState::running,
                     RenderJobState::pausing, RenderJobState::paused,
                     RenderJobState::cancelled, RenderJobState::failed,
                     RenderJobState::complete})
    if (stateName(state) == value)
      return state;
  throw std::invalid_argument("Invalid render queue job state");
}
QString formatName(ImageFormat format) {
  switch (format) {
  case ImageFormat::bmp8:
    return "bmp8";
  case ImageFormat::ppm8:
    return "ppm8";
  case ImageFormat::ppm16:
    return "ppm16";
  case ImageFormat::png8:
    return "png8";
  case ImageFormat::png16:
    return "png16";
  case ImageFormat::tiff8:
    return "tiff8";
  case ImageFormat::tiff16:
    return "tiff16";
  }
  throw std::invalid_argument("Unknown image format");
}
ImageFormat readFormat(const QString &value) {
  for (auto format : {ImageFormat::bmp8, ImageFormat::ppm8, ImageFormat::ppm16,
                      ImageFormat::png8, ImageFormat::png16, ImageFormat::tiff8,
                      ImageFormat::tiff16})
    if (formatName(format) == value)
      return format;
  throw std::invalid_argument("Invalid render queue image format");
}
QString requiredString(const QJsonObject &o, const char *key,
                       bool allowEmpty = false) {
  const auto value = o.value(QLatin1String(key));
  if (!value.isString() || (!allowEmpty && value.toString().isEmpty()))
    throw std::invalid_argument(std::string("Missing render queue field: ") +
                                key);
  return value.toString();
}
std::uint64_t integer(const QJsonObject &o, const char *key,
                      std::uint64_t maximum = UINT64_MAX) {
  const auto string = requiredString(o, key);
  bool ok = false;
  const auto result = string.toULongLong(&ok);
  if (!ok || string.startsWith('-') || result > maximum)
    throw std::invalid_argument(std::string("Invalid render queue integer: ") +
                                key);
  return result;
}
bool boolean(const QJsonObject &o, const char *key) {
  const auto value = o.value(QLatin1String(key));
  if (!value.isBool())
    throw std::invalid_argument(std::string("Invalid render queue flag: ") +
                                key);
  return value.toBool();
}
double seconds(const QJsonObject &o, const char *key) {
  const auto v = o.value(QLatin1String(key));
  if (!v.isDouble() || !std::isfinite(v.toDouble()) || v.toDouble() < 0)
    throw std::invalid_argument(std::string("Invalid render queue duration: ") +
                                key);
  return v.toDouble();
}
QJsonObject jobJson(const RenderJob &j) {
  QJsonArray conventions;
  const auto flags = [](const Transform &t) {
    return QJsonObject{
        {"apophysis_math", t.parameters.apophysis_math},
        {"separate_blur_angle", t.parameters.separate_blur_angle}};
  };
  for (const auto &t : j.flame.transforms)
    conventions.append(flags(t));
  return {
      {"id", j.id},
      {"name", j.name},
      {"source", j.source},
      {"destination", j.destination},
      {"destination_sha256", j.destination_sha256 ? QJsonValue(text(*j.destination_sha256)) : QJsonValue(QJsonValue::Null)},
      {"checkpoint", j.checkpoint},
      {"flame", text(serialize_flame(j.flame, j.file.embed_flame ? FlameXmlMode::render_snapshot : FlameXmlMode::canonical))},
      {"plugin_identity", text(plugin_identity(j.flame))},
      {"transform_math", conventions},
      {"final_math", j.flame.final_transform
                         ? QJsonValue(flags(*j.flame.final_transform))
                         : QJsonValue(QJsonValue::Null)},
      {"samples", QString::number(j.render.samples)},
      {"seed", QString::number(j.render.seed)},
      {"threads", QString::number(j.render.threads)},
      {"memory_mib", QString::number(j.render.memory_mib)},
      {"backend", text(backend_name(j.render.backend))},
      {"device", text(j.render.device)},
      {"gpu_memory_mib", QString::number(j.render.gpu_memory_mib)},
      {"cpu_fallback", j.render.allow_cpu_fallback},
      {"format", formatName(j.file.image.format)},
      {"alpha", j.file.image.alpha},
      {"embed_flame", j.file.embed_flame},
      {"disk_mib", QString::number(j.file.disk_mib)},
      {"strip_rows", QString::number(j.file.strip_rows)},
      {"state", stateName(j.state)},
      {"phase", j.phase},
      {"actual_backend", j.backend},
      {"actual_device", j.device},
      {"fallback", j.fallback},
      {"error", j.error},
      {"completed_samples", QString::number(j.completed_samples)},
      {"plotted", QString::number(j.plotted)},
      {"invalid", QString::number(j.invalid)},
      {"completed_strips", QString::number(j.completed_strips)},
      {"total_strips", QString::number(j.total_strips)},
      {"workers", QString::number(j.workers)},
      {"progress", QString::number(j.progress)},
      {"elapsed_seconds", j.elapsed_seconds},
      {"phase_remaining_seconds", j.phase_remaining_seconds < 0
                                      ? QJsonValue(QJsonValue::Null)
                                      : QJsonValue(j.phase_remaining_seconds)},
      {"checkpointed", j.checkpointed},
      {"resumed", j.resumed}};
}
RenderJob readJob(const QJsonObject &o, int version) {
  RenderJob j;
  j.id = requiredString(o, "id");
  if (QUuid(j.id).isNull() ||
      QUuid(j.id).toString(QUuid::WithoutBraces) != j.id)
    throw std::invalid_argument("Invalid render queue job identifier");
  j.name = requiredString(o, "name", true);
  j.source = requiredString(o, "source", true);
  j.destination = requiredString(o, "destination");
  if (o.contains("destination_sha256") && !o.value("destination_sha256").isNull()) {
    const auto hash = requiredString(o, "destination_sha256", true).toStdString();
    if (!hash.empty() && (hash.size() != 64 ||
        !std::all_of(hash.begin(), hash.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); })))
      throw std::invalid_argument("Invalid render queue destination fingerprint");
    j.destination_sha256 = hash;
  }
  j.checkpoint = requiredString(o, "checkpoint");
  if (!QFileInfo(j.destination).isAbsolute() ||
      !QFileInfo(j.checkpoint).isAbsolute())
    throw std::invalid_argument("Render queue paths must be absolute");
  const auto flame = requiredString(o, "flame").toUtf8();
  if (flame.size() > 2 * 1024 * 1024)
    throw std::invalid_argument("Render queue flame snapshot is too large");
  auto flames = parse_flames(
      std::string_view(flame.constData(), std::size_t(flame.size())));
  if (flames.size() != 1)
    throw std::invalid_argument(
        "Each render queue job needs one flame snapshot");
  j.flame = std::move(flames.front());
  // Queue v1 predates active estimation. Its snapshots could omit an explicit disabled
  // radius, including on FLAM3 releases whose original producer defaults to an active filter.
  if (version == 1)
    j.flame.density_estimator.reset();
  const auto identity = text(plugin_identity(j.flame));
  if ((!identity.isEmpty() && !o.value("plugin_identity").isString()) ||
      (o.contains("plugin_identity") &&
       (!o.value("plugin_identity").isString() ||
        o.value("plugin_identity").toString() != identity)))
    throw std::invalid_argument(
        "A queued plugin module changed or is missing. Restore the approved "
        "module before loading this queue");
  if (!o.value("transform_math").isArray() ||
      o.value("transform_math").toArray().size() !=
          qsizetype(j.flame.transforms.size()))
    throw std::invalid_argument(
        "Render queue transform conventions do not match the snapshot");
  const auto applyFlags = [](Transform &t, const QJsonValue &value) {
    if (!value.isObject())
      throw std::invalid_argument("Invalid render queue transform conventions");
    t.parameters.apophysis_math = boolean(value.toObject(), "apophysis_math");
    t.parameters.separate_blur_angle =
        boolean(value.toObject(), "separate_blur_angle");
  };
  const auto conventions = o.value("transform_math").toArray();
  for (std::size_t i = 0; i < j.flame.transforms.size(); ++i)
    applyFlags(j.flame.transforms[i], conventions[qsizetype(i)]);
  if (j.flame.final_transform)
    applyFlags(*j.flame.final_transform, o.value("final_math"));
  else if (!o.value("final_math").isNull())
    throw std::invalid_argument(
        "Unexpected final-transform conventions in render queue");
  j.render.samples = integer(o, "samples");
  j.render.seed = integer(o, "seed");
  j.render.threads = unsigned(integer(o, "threads", 1024));
  j.render.memory_mib = integer(o, "memory_mib");
  j.render.gpu_memory_mib = integer(o, "gpu_memory_mib");
  const auto backend = requiredString(o, "backend");
  if (backend == "cpu")
    j.render.backend = RenderBackend::cpu;
  else if (backend == "auto")
    j.render.backend = RenderBackend::automatic;
  else if (backend == "vulkan")
    j.render.backend = RenderBackend::vulkan;
  else
    throw std::invalid_argument("Invalid render queue backend");
  j.render.device = requiredString(o, "device", true).toStdString();
  j.render.allow_cpu_fallback = boolean(o, "cpu_fallback");
  j.file.image.format = readFormat(requiredString(o, "format"));
  j.file.image.alpha = boolean(o, "alpha");
  j.file.embed_flame = o.contains("embed_flame") && boolean(o, "embed_flame");
  j.file.disk_mib = integer(o, "disk_mib");
  j.file.strip_rows = unsigned(integer(o, "strip_rows", 16384));
  j.file.checkpoint_directory = filesystemPath(j.checkpoint);
  if (image_format_for_path(filesystemPath(j.destination),
                            image_format_bits(j.file.image.format)) !=
      j.file.image.format)
    throw std::invalid_argument(
        "Render queue destination extension does not match its image format");
  if (!j.source.isEmpty() &&
      (absolute(j.source) == absolute(j.destination) ||
       (QFileInfo(j.source).exists() && QFileInfo(j.destination).exists() &&
        std::filesystem::equivalent(filesystemPath(j.source),
                                    filesystemPath(j.destination)))))
    throw std::invalid_argument(
        "Render queue destination refers to its source flame");
  j.state = readState(requiredString(o, "state"));
  j.phase = requiredString(o, "phase", true);
  j.backend = requiredString(o, "actual_backend", true);
  j.device = requiredString(o, "actual_device", true);
  j.fallback = requiredString(o, "fallback", true);
  j.error = requiredString(o, "error", true);
  j.completed_samples = integer(o, "completed_samples", j.render.samples);
  j.plotted = integer(o, "plotted");
  j.invalid = integer(o, "invalid");
  j.completed_strips = unsigned(integer(o, "completed_strips", 16384));
  j.total_strips = unsigned(integer(o, "total_strips", 16384));
  if (o.contains("workers"))
    j.workers = unsigned(integer(o, "workers", 1024));
  j.elapsed_seconds = seconds(o, "elapsed_seconds");
  if (o.contains("phase_remaining_seconds") &&
      !o.value("phase_remaining_seconds").isNull())
    j.phase_remaining_seconds = seconds(o, "phase_remaining_seconds");
  j.resumed = boolean(o, "resumed");
  (void)boolean(o, "checkpointed");
  plan_file_render(j.flame, j.render, j.file);
  j.checkpointed = has_render_checkpoint(j.file.checkpoint_directory);
  if (j.state == RenderJobState::running || j.state == RenderJobState::pausing)
    j.state = RenderJobState::paused;
  j.progress =
      o.contains("progress") ? int(integer(o, "progress", 100))
      : j.state == RenderJobState::complete
          ? 100
          : std::min(99, int(80.0 * double(j.completed_samples) /
                             double(j.render.samples)) +
                             (j.total_strips ? int(20.0 * j.completed_strips /
                                                   j.total_strips)
                                             : 0));
  return j;
}
void writeQueue(const QString &path, const std::vector<RenderJob> &jobs) {
  QJsonArray array;
  for (const auto &j : jobs)
    array.append(jobJson(j));
  const auto bytes =
      QJsonDocument(QJsonObject{{"format", "apophysis-render-queue"},
                                {"version", 2},
                                {"jobs", array}})
          .toJson();
  QSaveFile out(path);
  if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() ||
      !out.commit())
    throw std::runtime_error(
        ("Cannot save render queue: " + out.errorString()).toStdString());
}
} // namespace

QString imageFormatText(ImageFormat format) {
  switch (format) {
  case ImageFormat::bmp8:
    return "BMP · RGB8";
  case ImageFormat::ppm8:
    return "PPM · RGB8";
  case ImageFormat::ppm16:
    return "PPM · RGB16";
  case ImageFormat::png8:
    return "PNG · 8 bit";
  case ImageFormat::png16:
    return "PNG · 16 bit";
  case ImageFormat::tiff8:
    return "TIFF · 8 bit";
  case ImageFormat::tiff16:
    return "TIFF · 16 bit";
  }
  return {};
}
QString renderJobStateText(const RenderJob &j) {
  switch (j.state) {
  case RenderJobState::queued:
    return "Queued";
  case RenderJobState::running:
    return j.phase.isEmpty() ? "Running" : j.phase;
  case RenderJobState::pausing:
    return "Stopping at checkpoint…";
  case RenderJobState::paused:
    return j.checkpointed ? "Paused · checkpoint saved"
                          : "Paused · ready to restart";
  case RenderJobState::cancelled:
    return "Cancelled";
  case RenderJobState::failed:
    return "Failed";
  case RenderJobState::complete:
    return "Complete";
  }
  return {};
}

struct RenderQueue::Impl {
  RenderQueue *owner;
  QString stateFile, checkpointRoot, active;
  std::unique_ptr<QTemporaryDir> temporary;
  std::unique_ptr<QLockFile> lock;
  std::vector<RenderJob> jobs;
  bool running = false, shuttingDown = false;
  std::uint64_t activeRun = 0;
  enum class StopIntent { none, pause, cancel } stopIntent = StopIntent::none;
  struct Finish {
    QString id, error;
    std::optional<FileRenderResult> result;
    std::optional<std::string> destination_sha256;
    bool cancelled = false, checkpointed = false;
    double elapsed = 0;
  };
  std::mutex mutex;
  std::optional<Finish> finished;
  struct Progress {
    QString id;
    FileRenderProgress value;
  };
  struct Work {
    RenderJob snapshot;
    std::stop_token stop;
  };
  std::optional<Progress> progress;
  std::optional<Work> pending;
  std::condition_variable wake, completed;
  std::stop_source activeStop;
  bool closing = false;
  QTimer progressTimer;
  std::jthread worker;
  double elapsedBefore = 0, phaseStartSeconds = 0;
  std::uint64_t phaseStartSamples = 0;
  unsigned phaseStartStrips = 0;
  QString measuredPhase;

  explicit Impl(RenderQueue *q) : owner(q) {}
  ~Impl() {
    progressTimer.stop();
    activeStop.request_stop();
    {
      std::lock_guard guard(mutex);
      closing = true;
      pending.reset();
    }
    wake.notify_one();
    if (worker.joinable())
      worker.join();
  }
  void execute(const RenderJob &snapshot, std::stop_token stop);
  void ensureWorker() {
    if (worker.joinable())
      return;
    worker = std::jthread([this] {
      gpu::ContextScope contextScope;
      for (;;) {
        std::optional<Work> work;
        {
          std::unique_lock guard(mutex);
          wake.wait(guard, [this] { return closing || pending.has_value(); });
          if (closing)
            return;
          work = std::move(pending);
          pending.reset();
        }
        execute(work->snapshot, work->stop);
      }
    });
  }
  auto find(const QString &id) {
    return std::find_if(jobs.begin(), jobs.end(),
                        [&](const auto &j) { return j.id == id; });
  }
  void persist(const std::vector<RenderJob> &candidate) const {
    if (!stateFile.isEmpty())
      writeQueue(stateFile, candidate);
  }
  void commit(std::vector<RenderJob> next) {
    persist(next);
    jobs = std::move(next);
    emit owner->changed();
  }
  bool persistResult() {
    try {
      persist(jobs);
      return true;
    } catch (const std::exception &error) {
      running = false;
      stopIntent = StopIntent::pause;
      activeStop.request_stop();
      const QPointer<RenderQueue> alive(owner);
      emit owner->runningChanged(false);
      if (!alive)
        return false;
      emit owner->problem(
          text(error.what()) +
          " The queue has stopped; save a copy before continuing.");
      return false;
    }
  }
};

RenderQueue::RenderQueue(QString stateFile, QString checkpointRoot,
                         QObject *parent)
    : QObject(parent), impl_(std::make_unique<Impl>(this)) {
  if (!stateFile.isEmpty()) {
    impl_->stateFile = absolute(stateFile);
    if (!QDir().mkpath(QFileInfo(impl_->stateFile).absolutePath()))
      throw std::runtime_error("Cannot create the render queue directory");
    impl_->lock = std::make_unique<QLockFile>(impl_->stateFile + ".lock");
    impl_->lock->setStaleLockTime(0);
    if (!impl_->lock->tryLock())
      throw std::runtime_error(
          "Another application session owns this render queue");
  }
  if (checkpointRoot.isEmpty()) {
    impl_->temporary = std::make_unique<QTemporaryDir>();
    if (!impl_->temporary->isValid())
      throw std::runtime_error("Cannot create render working storage");
    checkpointRoot = impl_->temporary->path();
  }
  impl_->checkpointRoot = absolute(checkpointRoot);
  if (!QDir().mkpath(impl_->checkpointRoot))
    throw std::runtime_error("Cannot create checkpoint storage");
  // Only the latest worker update matters. A GUI timer bounds delivery even
  // when a render produces thousands of strip/checkpoint notifications.
  impl_->progressTimer.setInterval(50);
  connect(&impl_->progressTimer, &QTimer::timeout, this, [this] {
    std::optional<Impl::Progress> progress;
    {
      std::lock_guard guard(impl_->mutex);
      progress = std::move(impl_->progress);
      impl_->progress.reset();
    }
    if (progress)
      updateProgress(progress->id, progress->value);
  });
}
RenderQueue::~RenderQueue() {
  impl_->shuttingDown = true;
  pauseAndWait();
}
const std::vector<RenderJob> &RenderQueue::jobs() const { return impl_->jobs; }
const RenderJob *RenderQueue::job(const QString &id) const {
  const auto it = impl_->find(id);
  return it == impl_->jobs.end() ? nullptr : &*it;
}
QString RenderQueue::activeId() const { return impl_->active; }
bool RenderQueue::isBusy() const { return !impl_->active.isEmpty(); }
bool RenderQueue::isRunning() const { return impl_->running; }
QString RenderQueue::stateFile() const { return impl_->stateFile; }
QStringList RenderQueue::protectedPaths() const {
  QStringList paths{impl_->checkpointRoot};
  if (!impl_->stateFile.isEmpty())
    paths << impl_->stateFile << impl_->stateFile + ".lock";
  for (const auto &job : impl_->jobs) {
    paths << job.checkpoint;
    auto pluginFiles = [&](const Transform &t) {
      for (const auto &term : t.variations)
        if (term.plugin) {
          const auto &info = term.plugin->info();
#ifdef _WIN32
          paths << QString::fromStdWString(info.manifest_path.wstring())
                << QString::fromStdWString(info.library_path.wstring());
#else
          paths << text(info.manifest_path.native())
                << text(info.library_path.native());
#endif
        }
    };
    for (const auto &t : job.flame.transforms)
      pluginFiles(t);
    if (job.flame.final_transform)
      pluginFiles(*job.flame.final_transform);
  }
  paths.removeAll(QString{});
  paths.removeDuplicates();
  return paths;
}

QString RenderQueue::add(const Flame &flame, const RenderOptions &render,
                         const FileRenderOptions &file,
                         const QString &destination, const QString &source) {
  RenderJob request;
  request.flame = flame;
  request.render = render;
  request.file = file;
  request.destination = destination;
  request.source = source;
  return addBatch({request}).front();
}
std::vector<QString>
RenderQueue::addBatch(const std::vector<RenderJob> &requests) {
  if (requests.empty())
    return {};
  if (impl_->jobs.size() + requests.size() > 1000)
    throw std::invalid_argument("The render queue is limited to 1,000 jobs");
  auto next = impl_->jobs;
  std::vector<QString> ids;
  for (const auto &request : requests) {
    const auto &flame = request.flame;
    const auto &render = request.render;
    const auto &file = request.file;
    const auto &destination = request.destination;
    const auto &source = request.source;
    if (destination.isEmpty())
      throw std::invalid_argument("Choose an output file");
    RenderJob j;
    j.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    j.name = request.name.isEmpty() ? text(flame.name) : request.name;
    j.source = source.isEmpty() ? QString{} : absolute(source);
    j.destination = absolute(destination);
    j.destination_sha256 = request.destination_sha256;
    if (!j.source.isEmpty() &&
        (j.destination == j.source ||
         (QFileInfo(j.source).exists() && QFileInfo(j.destination).exists() &&
          std::filesystem::equivalent(filesystemPath(j.source),
                                      filesystemPath(j.destination)))))
      throw std::invalid_argument(
          "Choose a different file for the image and source flame");
    j.checkpoint = QDir(impl_->checkpointRoot).filePath(j.id);
    j.flame = flame;
    j.render = render;
    j.render.stop_token = {};
    j.render.progress = {};
    j.file = file;
    j.file.status = {};
    j.file.before_publish = {};
    j.file.resume = false;
    j.file.checkpoint_directory = filesystemPath(j.checkpoint);
    if (image_format_for_path(filesystemPath(j.destination),
                              image_format_bits(j.file.image.format)) !=
        j.file.image.format)
      throw std::invalid_argument(
          "The filename extension must match the selected image format");
    const auto plan = plan_file_render(j.flame, j.render, j.file);
    j.total_strips = plan.strip_count;
    next.push_back(j);
    ids.push_back(j.id);
  }
  impl_->commit(std::move(next));
  if (impl_->running && !isBusy())
    launchNext();
  return ids;
}

void RenderQueue::remove(const QString &id) {
  if (id == impl_->active)
    throw std::invalid_argument("Pause or cancel this job before removing it");
  auto next = impl_->jobs;
  std::erase_if(next, [&](const auto &j) { return j.id == id; });
  impl_->commit(std::move(next));
}
void RenderQueue::move(const QString &id, int offset) {
  auto next = impl_->jobs;
  const auto found = std::find_if(next.begin(), next.end(),
                                  [&](const auto &j) { return j.id == id; });
  if (found == next.end() || !offset)
    return;
  const auto from = int(found - next.begin()), to = from + offset;
  if (to < 0 || to >= int(next.size()))
    return;
  if (found->id == impl_->active || next[std::size_t(to)].id == impl_->active)
    throw std::invalid_argument("The active job cannot be reordered");
  std::swap(next[std::size_t(from)], next[std::size_t(to)]);
  impl_->commit(std::move(next));
}
void RenderQueue::retry(const QString &id) {
  if (id == impl_->active)
    throw std::invalid_argument("This job is already running");
  auto next = impl_->jobs;
  const auto found = std::find_if(next.begin(), next.end(),
                                  [&](const auto &j) { return j.id == id; });
  if (found == next.end())
    return;
  if (found->state == RenderJobState::complete)
    throw std::invalid_argument("Add a new job to render this image again");
  found->state = RenderJobState::queued;
  found->checkpointed =
      has_render_checkpoint(filesystemPath(found->checkpoint));
  if (!found->checkpointed) {
    found->checkpoint =
        QDir(impl_->checkpointRoot)
            .filePath(QUuid::createUuid().toString(QUuid::WithoutBraces));
    found->file.checkpoint_directory = filesystemPath(found->checkpoint);
    found->completed_samples = 0;
    found->completed_strips = 0;
    found->progress = 0;
  }
  found->error.clear();
  found->phase_remaining_seconds = -1;
  impl_->commit(std::move(next));
  if (impl_->running && !isBusy())
    launchNext();
}
void RenderQueue::restart(const QString &id) {
  if (id == impl_->active)
    throw std::invalid_argument(
        "Pause or cancel this job before starting over");
  auto next = impl_->jobs;
  const auto found = std::find_if(next.begin(), next.end(),
                                  [&](const auto &j) { return j.id == id; });
  if (found == next.end())
    return;
  found->checkpoint =
      QDir(impl_->checkpointRoot)
          .filePath(QUuid::createUuid().toString(QUuid::WithoutBraces));
  found->file.checkpoint_directory = filesystemPath(found->checkpoint);
  found->state = RenderJobState::queued;
  found->checkpointed = false;
  found->resumed = false;
  found->completed_samples = found->plotted = found->invalid = 0;
  found->completed_strips = 0;
  found->workers = 0;
  found->progress = 0;
  found->elapsed_seconds = 0;
  found->phase_remaining_seconds = -1;
  found->error.clear();
  found->phase.clear();
  found->backend.clear();
  found->device.clear();
  found->fallback.clear();
  impl_->commit(std::move(next));
  if (impl_->running && !isBusy())
    launchNext();
}
void RenderQueue::start() {
  impl_->persist(
      impl_->jobs); // Do not begin work if the queue cannot be saved.
  impl_->running = true;
  emit runningChanged(true);
  if (!isBusy())
    launchNext();
}
void RenderQueue::pause() {
  const QPointer<RenderQueue> alive(this);
  impl_->running = false;
  if (isBusy()) {
    impl_->stopIntent = Impl::StopIntent::pause;
    impl_->find(impl_->active)->state = RenderJobState::pausing;
    impl_->activeStop.request_stop();
  }
  impl_->persistResult();
  if (!alive)
    return;
  emit runningChanged(false);
  if (!alive)
    return;
  emit changed();
}
void RenderQueue::cancel(const QString &id) {
  auto found = impl_->find(id);
  if (found == impl_->jobs.end() || found->state == RenderJobState::complete)
    return;
  if (id == impl_->active) {
    impl_->stopIntent = Impl::StopIntent::cancel;
    found->state = RenderJobState::pausing;
    impl_->activeStop.request_stop();
    impl_->persistResult();
    emit changed();
  } else {
    auto next = impl_->jobs;
    next[std::size_t(found - impl_->jobs.begin())].state =
        RenderJobState::cancelled;
    impl_->commit(std::move(next));
  }
}
void RenderQueue::saveTo(const QString &path) const {
  if (path.isEmpty())
    throw std::invalid_argument("Choose a render queue file");
  writeQueue(path, impl_->jobs);
}
void RenderQueue::loadFrom(const QString &path) {
  if (isBusy())
    throw std::invalid_argument(
        "Pause the active job before restoring a queue");
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    throw std::runtime_error("Cannot open the render queue file");
  constexpr qint64 maxQueueBytes = 32 * 1024 * 1024;
  if (file.size() > maxQueueBytes)
    throw std::invalid_argument("Render queue file exceeds 32 MiB");
  const auto bytes = file.read(maxQueueBytes + 1);
  if (file.error() != QFileDevice::NoError)
    throw std::runtime_error("Cannot read the complete render queue file");
  if (bytes.size() > maxQueueBytes)
    throw std::invalid_argument("Render queue file exceeds 32 MiB");
  // Startup restores from the autosave path itself. Release the source read
  // handle before persist() replaces that file, including on Windows.
  file.close();
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(bytes, &error);
  if (error.error != QJsonParseError::NoError || !document.isObject())
    throw std::invalid_argument("Render queue is not valid JSON");
  const auto object = document.object();
  const int version = object.value("version").toInt(-1);
  if (object.value("format") != "apophysis-render-queue" ||
      (version != 1 && version != 2) || !object.value("jobs").isArray())
    throw std::invalid_argument("Unsupported render queue format or version");
  const auto array = object.value("jobs").toArray();
  if (array.size() > 1000)
    throw std::invalid_argument("The render queue is limited to 1,000 jobs");
  std::vector<RenderJob> candidate;
  std::set<QString> ids;
  for (const auto &value : array) {
    if (!value.isObject())
      throw std::invalid_argument("Invalid render queue entry");
    auto j = readJob(value.toObject(), version);
    if (!ids.insert(j.id).second)
      throw std::invalid_argument("Duplicate render queue job identifier");
    candidate.push_back(std::move(j));
  }
  // Full validation precedes the persistent replacement and the in-memory
  // change.
  impl_->persist(candidate);
  impl_->jobs = std::move(candidate);
  impl_->running = false;
  emit runningChanged(false);
  emit changed();
}
void RenderQueue::disableAutoSave() { impl_->stateFile.clear(); }
void RenderQueue::pauseAndWait() {
  if (!isBusy())
    return;
  const QPointer<RenderQueue> alive(this);
  const auto run = impl_->activeRun;
  pause();
  // pause() publishes signals. A direct slot or nested event loop can finish
  // this job (and even start another with the same ID) before it returns.
  if (!alive || !isBusy() || impl_->activeRun != run)
    return;
  {
    std::unique_lock guard(impl_->mutex);
    impl_->completed.wait(guard, [this] { return impl_->finished.has_value(); });
  }
  finishActive();
}

void RenderQueue::Impl::execute(const RenderJob &snapshot, std::stop_token stop) {
  auto options = snapshot.render;
  auto file = snapshot.file;
  options.stop_token = stop;
  options.progress = {};
  file.resume = snapshot.checkpointed;
  file.status = [this, id = snapshot.id](const FileRenderProgress &p) {
    std::lock_guard guard(mutex);
    progress = Progress{id, p};
  };
  if (snapshot.destination_sha256) {
    file.before_publish = [snapshot, stop] {
      const auto path = filesystemPath(snapshot.destination);
      const auto state = std::filesystem::symlink_status(path);
      const bool exists = std::filesystem::exists(state);
      if (snapshot.destination_sha256->empty()) {
        if (exists || std::filesystem::is_symlink(state))
          throw std::runtime_error("The frame destination appeared after queue admission. The new file was not replaced.");
      } else if (!exists || std::filesystem::is_symlink(state) ||
                 !std::filesystem::is_regular_file(state) ||
                 detail::sha256_file(path, stop) != *snapshot.destination_sha256) {
        throw std::runtime_error("The frame destination changed after queue admission. The changed file was not replaced.");
      }
    };
  }
  Finish result;
  result.id = snapshot.id;
  const auto began = std::chrono::steady_clock::now();
  try {
    result.result = render_to_file(
        snapshot.flame, options, filesystemPath(snapshot.destination), file);
    if (snapshot.destination_sha256)
      result.destination_sha256 = detail::sha256_file(filesystemPath(snapshot.destination));
  } catch (const RenderCancelled &) {
    result.cancelled = true;
  } catch (const std::exception &error) {
    result.result.reset();
    result.error = text(error.what());
  }
  result.elapsed =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - began)
          .count();
  result.checkpointed = has_render_checkpoint(file.checkpoint_directory);
  {
    std::lock_guard lock(mutex);
    finished = std::move(result);
  }
  completed.notify_one();
  QMetaObject::invokeMethod(owner, &RenderQueue::finishActive,
                            Qt::QueuedConnection);
}

void RenderQueue::launchNext() {
  if (!impl_->running || isBusy() || impl_->shuttingDown)
    return;
  const auto found =
      std::find_if(impl_->jobs.begin(), impl_->jobs.end(), [](const auto &j) {
        return j.state == RenderJobState::queued ||
               j.state == RenderJobState::paused;
      });
  if (found == impl_->jobs.end()) {
    impl_->running = false;
    const QPointer<RenderQueue> alive(this);
    emit runningChanged(false);
    if (!alive)
      return;
    emit changed();
    return;
  }
  const auto index = std::size_t(found - impl_->jobs.begin());
  auto next = impl_->jobs;
  auto &pending = next[index];
  pending.checkpointed =
      has_render_checkpoint(filesystemPath(pending.checkpoint));
  pending.state = RenderJobState::running;
  pending.phase = pending.checkpointed ? "Resuming checkpoint" : "Preparing";
  pending.error.clear();
  pending.phase_remaining_seconds = -1;
  auto snapshot = pending;
  impl_->ensureWorker();
  impl_->persist(next);
  impl_->jobs = std::move(next);
  impl_->active = snapshot.id;
  ++impl_->activeRun;
  impl_->stopIntent = Impl::StopIntent::none;
  impl_->elapsedBefore = snapshot.elapsed_seconds;
  impl_->measuredPhase.clear();
  impl_->activeStop = std::stop_source{};
  {
    std::lock_guard guard(impl_->mutex);
    impl_->progress.reset();
    impl_->pending = Impl::Work{std::move(snapshot), impl_->activeStop.get_token()};
  }
  impl_->progressTimer.start();
  impl_->wake.notify_one();
  const QPointer<RenderQueue> alive(this);
  emit busyChanged(true);
  if (!alive)
    return;
  emit changed();
}
void RenderQueue::updateProgress(const QString &id,
                                 const FileRenderProgress &p,
                                 bool notifyObservers) {
  if (id != impl_->active)
    return;
  auto &j = *impl_->find(id);
  j.completed_samples = p.completed_samples;
  j.completed_strips = p.completed_strips;
  j.total_strips = p.total_strips;
  j.elapsed_seconds = impl_->elapsedBefore + p.elapsed_seconds;
  j.phase = text(p.phase);
  if (!p.backend.empty()) {
    j.backend = p.backend == "cpu"      ? "CPU"
                : p.backend == "vulkan" ? "Vulkan"
                                        : text(p.backend);
    j.device = text(p.device_name);
    j.fallback = text(p.fallback_reason);
  }
  j.progress =
      p.total_samples
          ? int(80.0 * double(p.completed_samples) / double(p.total_samples))
          : 0;
  if (p.total_strips)
    j.progress += int(20.0 * p.completed_strips / p.total_strips);
  j.progress = std::clamp(j.progress, 0, 99);
  if (impl_->measuredPhase != j.phase) {
    impl_->measuredPhase = j.phase;
    impl_->phaseStartSamples = p.completed_samples;
    impl_->phaseStartStrips = p.completed_strips;
    impl_->phaseStartSeconds = p.elapsed_seconds;
    j.phase_remaining_seconds = -1;
  }
  const auto elapsed = p.elapsed_seconds - impl_->phaseStartSeconds;
  if (elapsed >= 0.5) {
    if (p.completed_samples > impl_->phaseStartSamples &&
        p.completed_samples < p.total_samples)
      j.phase_remaining_seconds =
          elapsed * double(p.total_samples - p.completed_samples) /
          double(p.completed_samples - impl_->phaseStartSamples);
    else if (p.completed_strips > impl_->phaseStartStrips &&
             p.completed_strips < p.total_strips)
      j.phase_remaining_seconds =
          elapsed * double(p.total_strips - p.completed_strips) /
          double(p.completed_strips - impl_->phaseStartStrips);
  }
  // Progress is cosmetic. Durable transitions and render checkpoints are
  // saved independently; serializing the whole queue here stalls the GUI.
  if (notifyObservers)
    emit changed();
}
void RenderQueue::finishActive() {
  std::optional<Impl::Finish> finish;
  std::optional<Impl::Progress> progress;
  {
    std::lock_guard lock(impl_->mutex);
    finish = std::move(impl_->finished);
    impl_->finished.reset();
    if (finish) {
      progress = std::move(impl_->progress);
      impl_->progress.reset();
    }
  }
  if (!finish || finish->id != impl_->active)
    return;
  impl_->progressTimer.stop();
  // Do not publish an intermediate signal while completion is being consumed:
  // a direct slot may synchronously pause and wait for this same completion.
  if (progress)
    updateProgress(progress->id, progress->value, false);
  auto &j = *impl_->find(finish->id);
  j.elapsed_seconds = impl_->elapsedBefore + finish->elapsed;
  j.phase_remaining_seconds = -1;
  j.checkpointed = finish->checkpointed;
  if (finish->result) {
    if (finish->destination_sha256)
      j.destination_sha256 = finish->destination_sha256;
    const auto &r = *finish->result;
    j.state = RenderJobState::complete;
    j.phase = "Complete";
    j.progress = 100;
    j.completed_samples = j.render.samples;
    j.completed_strips = j.total_strips;
    j.plotted = r.plotted;
    j.invalid = r.invalid;
    j.workers = r.workers;
    j.backend = r.backend == RenderBackend::vulkan ? "Vulkan" : "CPU";
    j.device = text(r.device_name);
    j.fallback = text(r.fallback_reason);
    j.resumed = r.resumed;
  } else if (finish->cancelled) {
    j.state = impl_->stopIntent == Impl::StopIntent::cancel
                  ? RenderJobState::cancelled
                  : RenderJobState::paused;
    j.phase = j.checkpointed ? "Checkpoint saved" : "Ready to restart";
  } else {
    j.state = RenderJobState::failed;
    j.phase = "Failed";
    j.error = finish->error;
  }
  const bool success = j.state == RenderJobState::complete;
  impl_->active.clear();
  const QPointer<RenderQueue> alive(this);
  const bool saved = impl_->persistResult();
  if (!alive)
    return;
  emit busyChanged(false);
  if (!alive)
    return;
  emit changed();
  if (!alive)
    return;
  emit jobFinished(finish->id, success);
  if (!alive)
    return;
  if (saved && impl_->running && !impl_->shuttingDown)
    QTimer::singleShot(0, this, [this] {
      try {
        launchNext();
      } catch (const std::exception &error) {
        impl_->running = false;
        const QPointer<RenderQueue> alive(this);
        emit runningChanged(false);
        if (!alive)
          return;
        emit problem(text(error.what()));
      }
    });
}

RenderQueueDialog::RenderQueueDialog(RenderQueue &queue, QWidget *parent)
    : QDialog(parent), queue_(queue) {
  setObjectName("renderQueueDialog");
  setWindowTitle("Render queue");
  resize(1120, 610);
  auto layout = new QVBoxLayout(this);
  summary_ = new QLabel;
  summary_->setObjectName("queueSummary");
  summary_->setWordWrap(true);
  layout->addWidget(summary_);
  table_ = new QTableWidget(0, 8);
  table_->setObjectName("renderQueueTable");
  table_->setHorizontalHeaderLabels({"Flame", "Resolution", "Renderer",
                                     "Status", "Progress", "Elapsed",
                                     "Phase ETA", "Destination"});
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::SingleSelection);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->verticalHeader()->hide();
  table_->horizontalHeader()->setSectionResizeMode(
      QHeaderView::ResizeToContents);
  table_->horizontalHeader()->setStretchLastSection(true);
  layout->addWidget(table_, 1);
  details_ = new QLabel;
  details_->setObjectName("queueDetails");
  details_->setWordWrap(true);
  details_->setTextFormat(Qt::PlainText);
  details_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  details_->setMinimumHeight(100);
  layout->addWidget(details_);
  auto controls = new QHBoxLayout;
  const auto button = [&](const QString &name, const QString &label,
                          auto operation) {
    auto b = new QPushButton(label);
    b->setObjectName(name);
    controls->addWidget(b);
    connect(b, &QPushButton::clicked, this,
            [this, operation] { perform(operation); });
    return b;
  };
  start_ = button("startQueue", "Start / resume", [this] { queue_.start(); });
  pause_ = button("pauseQueue", "Pause queue", [this] { queue_.pause(); });
  cancel_ = button("cancelQueueJob", "Cancel job",
                   [this] { queue_.cancel(selectedId()); });
  retry_ = button("retryQueueJob", "Retry / resume job",
                  [this] { queue_.retry(selectedId()); });
  up_ = button("moveQueueJobUp", "Move up",
               [this] { queue_.move(selectedId(), -1); });
  down_ = button("moveQueueJobDown", "Move down",
                 [this] { queue_.move(selectedId(), 1); });
  remove_ = button("removeQueueJob", "Remove",
                   [this] { queue_.remove(selectedId()); });
  layout->addLayout(controls);
  auto buttons = new QDialogButtonBox(QDialogButtonBox::Close);
  restart_ = buttons->addButton("Start selected job over",
                                QDialogButtonBox::ActionRole);
  restart_->setObjectName("restartQueueJob");
  connect(restart_, &QPushButton::clicked, this,
          [this] { perform([&] { queue_.restart(selectedId()); }); });
  auto save = buttons->addButton("Save queue…", QDialogButtonBox::ActionRole);
  save->setObjectName("saveRenderQueue");
  auto load =
      buttons->addButton("Restore queue…", QDialogButtonBox::ActionRole);
  load->setObjectName("restoreRenderQueue");
  connect(save, &QPushButton::clicked, this, [this] {
    perform([&] {
      const auto path = QFileDialog::getSaveFileName(this, "Save render queue",
                                                     "render-queue.json",
                                                     "Render queue (*.json)");
      if (!path.isEmpty())
        queue_.saveTo(path);
    });
  });
  connect(load, &QPushButton::clicked, this, [this] {
    perform([&] {
      const auto path = QFileDialog::getOpenFileName(
          this, "Restore render queue", {}, "Render queue (*.json)");
      if (!path.isEmpty())
        queue_.loadFrom(path);
    });
  });
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(buttons);
  connect(&queue_, &RenderQueue::changed, this, &RenderQueueDialog::refresh);
  connect(&queue_, &RenderQueue::runningChanged, this,
          &RenderQueueDialog::refresh);
  connect(table_, &QTableWidget::itemSelectionChanged, this,
          &RenderQueueDialog::refresh);
  refresh();
}
QString RenderQueueDialog::selectedId() const {
  const auto item = table_->item(table_->currentRow(), 0);
  return item ? item->data(Qt::UserRole).toString() : QString{};
}
void RenderQueueDialog::perform(const std::function<void()> &operation) {
  try {
    operation();
  } catch (const std::exception &error) {
    QMessageBox::warning(this, "Render queue", text(error.what()));
  }
}
void RenderQueueDialog::refresh() {
  const QSignalBlocker block(table_);
  const auto selected = selectedId();
  table_->setRowCount(int(queue_.jobs().size()));
  int selectedRow = -1;
  for (std::size_t i = 0; i < queue_.jobs().size(); ++i) {
    const auto &j = queue_.jobs()[i];
    QString renderer =
        j.backend.isEmpty() ? text(backend_name(j.render.backend)) : j.backend;
    if (!j.fallback.isEmpty())
      renderer += " fallback";
    const QStringList values{
        j.name,
        QString("%1 × %2").arg(j.flame.width).arg(j.flame.height),
        renderer,
        renderJobStateText(j),
        QString("%1%").arg(j.progress),
        duration(j.elapsed_seconds),
        duration(j.state == RenderJobState::running ? j.phase_remaining_seconds
                                                    : -1),
        j.destination};
    for (int c = 0; c < values.size(); ++c) {
      auto item = table_->item(int(i), c);
      if (!item) {
        item = new QTableWidgetItem;
        table_->setItem(int(i), c, item);
      }
      if (item->text() != values[c])
        item->setText(values[c]);
      if (item->toolTip() != values[c])
        item->setToolTip(values[c]);
      if (c == 0)
        item->setData(Qt::UserRole, j.id);
    }
    if (j.id == selected)
      selectedRow = int(i);
  }
  if (selectedRow < 0 && !queue_.jobs().empty())
    selectedRow = 0;
  if (selectedRow >= 0)
    table_->selectRow(selectedRow);
  const auto j = queue_.job(selectedId());
  const bool active = j && j->id == queue_.activeId();
  start_->setEnabled(!queue_.isRunning() &&
                     std::any_of(queue_.jobs().begin(), queue_.jobs().end(),
                                 [](const auto &job) {
                                   return job.state == RenderJobState::queued ||
                                          job.state == RenderJobState::paused;
                                 }));
  pause_->setEnabled(queue_.isRunning() || queue_.isBusy());
  cancel_->setEnabled(j && j->state != RenderJobState::complete &&
                      j->state != RenderJobState::cancelled);
  retry_->setEnabled(j && !active &&
                     (j->state == RenderJobState::failed ||
                      j->state == RenderJobState::cancelled ||
                      j->state == RenderJobState::paused));
  remove_->setEnabled(j && !active);
  restart_->setEnabled(j && !active);
  up_->setEnabled(j && !active && selectedRow > 0);
  down_->setEnabled(j && !active && selectedRow + 1 < table_->rowCount());
  summary_->setText(
      QString(
          "%1 job(s) · %2. Pausing commits finished sampling chunks before a "
          "job becomes resumable. Restored queues wait for Start / resume.")
          .arg(queue_.jobs().size())
          .arg(queue_.isRunning() ? "Queue running"
               : queue_.isBusy()  ? "Saving checkpoint"
                                  : "Queue stopped"));
  if (!j) {
    details_->setText("Add a job with File → Render image. You can keep "
                      "editing while the queue renders.");
    return;
  }
  auto description =
      QString("%1 · %2 · %3 / %4 samples · seed %5\nSource: %6\nCheckpoint: %7")
          .arg(imageFormatText(j->file.image.format),
               j->file.image.alpha ? "Transparent RGBA" : "Opaque RGB")
          .arg(j->completed_samples)
          .arg(j->render.samples)
          .arg(j->render.seed)
          .arg(j->source.isEmpty() ? "Embedded flame snapshot" : j->source,
               j->checkpointed
                   ? "Saved; committed sampling can resume after restart"
                   : "No committed sampling yet");
  if (!j->device.isEmpty())
    description += "\nDevice: " + j->device;
  if (j->workers)
    description += QString(" · CPU workers: %1").arg(j->workers);
  if (!j->fallback.isEmpty())
    description += "\nCPU fallback: " + j->fallback;
  if (!j->error.isEmpty())
    description += "\nError: " + j->error;
  details_->setText(description);
  details_->setToolTip(
      "Checkpoint directory: " + j->checkpoint +
      "\nPhase ETA uses measured progress in the current phase.");
}
} // namespace apo::editor
