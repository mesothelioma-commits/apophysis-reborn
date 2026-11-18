#include "document_session.hpp"
#include "output_file.hpp"
#include "project_document.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUuid>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>
#include <utility>

namespace apo::editor {

std::filesystem::path filesystemPath(const QString &path) {
#ifdef _WIN32
  return std::filesystem::path(path.toStdWString());
#else
  return std::filesystem::path(path.toUtf8().constData());
#endif
}

namespace {
QByteArray fileDigest(const QString &path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    throw std::runtime_error(file.errorString().toStdString());
  QCryptographicHash hash(QCryptographicHash::Sha256);
  if (!hash.addData(&file))
    throw std::runtime_error(
        "Could not read the complete file for conflict detection");
  return hash.result();
}
} // namespace

struct DocumentSession::RecoveryWorker {
  struct Job {
    std::uint64_t serial = 0, generation = 0;
    QString destination, source;
    QByteArray digest;
    std::optional<ProjectFile> project;
    std::string library;
    RecoveryPreparation prepare;
  };
  struct Completion {
    std::uint64_t serial = 0, generation = 0;
    QString destination, error;
    bool cancelled = false;
  };
  struct Lease {
    QString path;
    std::unique_ptr<QLockFile> lock;
  };
  struct State {
    std::mutex mutex;
    std::condition_variable wake;
    std::optional<Job> pending;
    std::optional<Completion> completed;
    std::vector<QString> retired;
    std::vector<Lease> adopted;
    std::atomic<std::uint64_t> generation{0};
    std::uint64_t serial = 0;
    bool active = false, closing = false;
  };
  std::shared_ptr<State> state;

  explicit RecoveryWorker(std::uint64_t generation) : state(std::make_shared<State>()) {
    state->generation = generation;
    // The worker owns only immutable snapshots and its private state. No queued
    // callback captures a DocumentSession; teardown cannot wait on slow storage.
    std::thread([shared = state] { run(shared); }).detach();
  }
  ~RecoveryWorker() {
    { std::lock_guard lock(state->mutex); state->closing = true; }
    state->wake.notify_one();
  }
  std::uint64_t submit(Job job) {
    std::lock_guard lock(state->mutex);
    job.serial = ++state->serial;
    const auto serial = job.serial;
    state->pending = std::move(job);
    state->wake.notify_one();
    return serial;
  }
  void invalidate(std::uint64_t generation, const QString &path) {
    state->generation = generation;
    {
      std::lock_guard lock(state->mutex);
      if (state->pending && state->pending->generation != generation)
        state->pending.reset();
      if (!path.isEmpty())
        state->retired.push_back(path);
    }
    state->wake.notify_one();
  }
  void adopt(QString path, std::unique_ptr<QLockFile> lock) {
    { std::lock_guard guard(state->mutex); state->adopted.push_back({std::move(path), std::move(lock)}); }
    state->wake.notify_one();
  }
  bool busy() const {
    std::lock_guard lock(state->mutex);
    return state->active || state->pending || !state->retired.empty() || !state->adopted.empty();
  }
  std::pair<std::optional<Completion>, bool> poll() {
    std::lock_guard lock(state->mutex);
    // Completion and idleness must be observed together. Separate reads can
    // miss a just-finished job and stop GUI polling before delivering its result.
    return {std::exchange(state->completed, {}),
            state->active || state->pending || !state->retired.empty() || !state->adopted.empty()};
  }
  Completion wait(std::uint64_t serial) {
    std::unique_lock lock(state->mutex);
    state->wake.wait(lock, [&] {
      return state->completed && state->completed->serial >= serial;
    });
    return *state->completed;
  }
  void waitIdle() {
    std::unique_lock lock(state->mutex);
    state->wake.wait(lock, [&] {
      return !state->active && !state->pending && state->retired.empty() && state->adopted.empty();
    });
  }
  static void run(const std::shared_ptr<State> &state) {
    std::map<QString, std::unique_ptr<QLockFile>> leases;
    for (;;) {
      std::optional<Job> job;
      std::vector<QString> retired;
      std::vector<Lease> adopted;
      {
        std::unique_lock lock(state->mutex);
        state->wake.wait(lock, [&] {
          return state->closing || state->pending || !state->retired.empty() || !state->adopted.empty();
        });
        job = std::exchange(state->pending, {});
        retired.swap(state->retired);
        adopted.swap(state->adopted);
        if (state->closing && !job && retired.empty() && adopted.empty())
          return;
        state->active = true;
      }
      for (auto &lease : adopted)
        leases[lease.path] = std::move(lease.lock);
      for (const auto &path : retired) {
        QFile::remove(path);
        QFile::remove(path + ".json");
        leases.erase(path);
      }
      std::optional<Completion> completed;
      if (job) {
        completed = Completion{job->serial, job->generation, job->destination, {}, false};
        const auto current = [&] { return state->generation.load() == job->generation; };
        struct Obsolete {};
        const auto check = [&] { if (!current()) throw Obsolete{}; };
        try {
          check();
          if (job->project && job->prepare)
            job->prepare(*job->project);
          check();
          const auto bytes = job->project ? serialize_project(*job->project) : std::move(job->library);
          check();
          if (!leases.contains(job->destination)) {
            if (!QDir().mkpath(QFileInfo(job->destination).absolutePath()))
              throw std::runtime_error("Could not create the recovery folder");
            auto lock = std::make_unique<QLockFile>(job->destination + ".lock");
            lock->setStaleLockTime(0);
            if (!lock->tryLock())
              throw std::runtime_error("Could not reserve the recovery file");
            leases.emplace(job->destination, std::move(lock));
          }
          const auto write = [&](const QString &path, std::string_view contents) {
            write_atomic(filesystemPath(path), [&](std::ostream &out) {
              for (std::size_t offset = 0; offset < contents.size();) {
                check();
                const auto count = std::min<std::size_t>(65536, contents.size() - offset);
                out.write(contents.data() + offset, std::streamsize(count));
                offset += count;
              }
            }, check);
          };
          write(job->destination, bytes);
          const auto info = QJsonDocument(QJsonObject{{"source", job->source},
              {"original_sha256", QString::fromLatin1(job->digest.toHex())}}).toJson();
          write(job->destination + ".json", std::string_view(info.constData(), std::size_t(info.size())));
          check();
        } catch (const Obsolete &) {
          completed->cancelled = true;
        } catch (const std::exception &error) {
          completed->error = QString::fromUtf8(error.what());
        } catch (...) {
          completed->error = "Recovery saving failed with an unknown error";
        }
        // Invalidation can race the final atomic rename, but this path is unique
        // to the retired document and remains locked until its cleanup below.
        if (!current()) {
          completed->cancelled = true;
          QFile::remove(job->destination);
          QFile::remove(job->destination + ".json");
          leases.erase(job->destination);
        }
      }
      // Idle is also a lifetime boundary for ordinary application close: release
      // the preparation closure and its owned QImage before advertising it.
      job.reset();
      {
        std::lock_guard lock(state->mutex);
        state->active = false;
        if (completed)
          state->completed = std::move(completed);
      }
      state->wake.notify_all();
    }
  }
};

class DocumentChange : public QUndoCommand {
public:
  DocumentChange(DocumentSession &session, QString label, std::string after,
                 int next, bool final, int nextFlame)
      : QUndoCommand(std::move(label)), session_(session),
        before_(session.document().bytes()), after_(std::move(after)),
        beforeFlame_(session.selectedFlame()),
        afterFlame_(nextFlame < 0 ? beforeFlame_ : nextFlame),
        beforeTransform_(session.selectedTransform()),
        afterTransform_(next < 0 ? beforeTransform_ : next),
        beforeFinal_(session.isFinalTransform()),
        afterFinal_(next < 0 ? beforeFinal_ : final) {}
  void undo() override {
    session_.restore(before_, beforeFlame_, beforeTransform_, beforeFinal_);
  }
  void redo() override {
    session_.restore(after_, afterFlame_, afterTransform_, afterFinal_);
  }

private:
  DocumentSession &session_;
  std::string before_, after_;
  int beforeFlame_, afterFlame_, beforeTransform_, afterTransform_;
  bool beforeFinal_, afterFinal_;
};

class AnimationChange : public QUndoCommand {
public:
  AnimationChange(DocumentSession &session, QString label,
                  std::optional<std::string> after)
      : QUndoCommand(std::move(label)), session_(session),
        before_(session.projectSnapshot().animation_data),
        after_(std::move(after)) {}
  void undo() override { session_.restoreAnimation(before_); }
  void redo() override { session_.restoreAnimation(after_); }

private:
  DocumentSession &session_;
  std::optional<std::string> before_, after_;
};

DocumentSession::DocumentSession(QObject *parent)
    : QObject(parent), undo_(this) {
  recoveryPoll_.setInterval(25);
  connect(&recoveryPoll_, &QTimer::timeout, this, &DocumentSession::pollRecovery);
  undo_.setUndoLimit(100);
  connect(&undo_, &QUndoStack::cleanChanged, this, [this](bool clean) {
    if (clean && !projectDirty_)
      discardRecovery();
    emit modifiedChanged(isModified());
  });
}
DocumentSession::~DocumentSession() {
  disconnect(&undo_, nullptr, this, nullptr);
  recoveryPoll_.stop();
  recoveryWorker_.reset();
}

void DocumentSession::load(const QString &path) {
  // Read once so the loaded document and conflict fingerprint describe
  // identical bytes.
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    throw std::runtime_error(file.errorString().toStdString());
  if (file.size() > qint64(project_file_limit))
    throw std::invalid_argument("The document exceeds the 96 MiB file limit");
  const auto bytes = file.read(qint64(project_file_limit) + 1);
  if (file.error() != QFileDevice::NoError ||
      bytes.size() > qint64(project_file_limit))
    throw std::runtime_error(
        "Could not read the complete document within its file limit");
  loadBytes(bytes.toStdString(), QFileInfo(path).absoluteFilePath());
}

void DocumentSession::loadBytes(std::string bytes, const QString &path,
                                bool modified) {
  auto next = parse_document_file(bytes, is_project_path(filesystemPath(path)));
  const auto &source = bytes;
  const auto fingerprint = QCryptographicHash::hash(
      QByteArray::fromRawData(source.data(), qsizetype(source.size())),
      QCryptographicHash::Sha256);
  discardRecovery();
  ++documentGeneration_;
  document_ = std::move(next.document);
  project_ = std::move(next.project);
  projectDirty_ = false;
  path_ = path.isEmpty() ? QString{} : QFileInfo(path).absoluteFilePath();
  diskDigest_ = path.isEmpty() ? QByteArray{} : fingerprint;
  flame_ = project_ ? int(project_->selected_index) : 0;
  transform_ = 0;
  final_ = false;
  undo_.clear();
  if (modified)
    undo_.resetClean();
  emit changed();
  emit modifiedChanged(isModified());
}

DocumentSession::SaveSnapshot DocumentSession::snapshotForSave(
    const QString &path, std::optional<ProjectFile> project) const {
  if (!document_)
    throw std::runtime_error("Open a flame before saving");
  if (path.isEmpty())
    throw std::invalid_argument("Choose a destination before saving");
  const bool native = is_project_path(filesystemPath(path));
  if (!native && hasAnimation())
    throw std::invalid_argument(
        "This document contains animation. Save it as .apoproject to retain "
        "the timeline, or export a separate flame library.");
  SaveSnapshot snapshot{QFileInfo(path).absoluteFilePath(), path_, diskDigest_,
                        {}, documentGeneration_, revision_, native};
  if (native)
    snapshot.project = project ? std::move(*project) : projectSnapshot();
  else
    snapshot.project.library_bytes = document_->bytes();
  return snapshot;
}

void DocumentSession::SaveSnapshot::write(bool replaceExternalChange,
                                          const RecoveryPreparation &prepare) {
  const bool sameFile = !originalPath.isEmpty() &&
      (destination == originalPath ||
       (QFileInfo::exists(destination) && QFileInfo(destination).canonicalFilePath() ==
                                              QFileInfo(originalPath).canonicalFilePath()));
  const auto checkConflict = [&] {
    if (sameFile && !replaceExternalChange &&
        (!QFileInfo::exists(destination) || fileDigest(destination) != digest))
      throw FileConflict();
  };
  checkConflict();
  if (native && prepare)
    prepare(project);
  const std::string serialized = native ? serialize_project(project) : std::string{};
  const auto &bytes = native ? serialized : project.library_bytes;
  const auto newDigest = QCryptographicHash::hash(
      QByteArray::fromRawData(bytes.data(), qsizetype(bytes.size())), QCryptographicHash::Sha256);
  write_atomic(filesystemPath(destination), [&](std::ostream &out) { out << bytes; }, checkConflict);
  digest = newDigest;
}

bool DocumentSession::finishSave(SaveSnapshot saved) {
  if (saved.documentGeneration != documentGeneration_)
    return false;
  path_ = std::move(saved.destination);
  diskDigest_ = std::move(saved.digest);
  if (saved.revision != revision_) {
    // A queued edit during the write belongs to the live document. Keep its
    // content and recovery, and never mark it clean against the older file.
    if (saved.native && !project_)
      project_ = std::move(saved.project);
    projectDirty_ = true;
    undo_.resetClean();
    emit modifiedChanged(true);
    return false;
  }
  project_ = saved.native ? std::optional(std::move(saved.project)) : std::nullopt;
  projectDirty_ = false;
  undo_.setClean();
  discardRecovery();
  // Saving changes the path and clean indicator. Rebuilding the entire flame
  // library here would parse every flame and restart the unchanged preview.
  emit modifiedChanged(false);
  return true;
}

void DocumentSession::save(const QString &path, bool replaceExternalChange) {
  auto snapshot = snapshotForSave(path);
  snapshot.write(replaceExternalChange);
  finishSave(std::move(snapshot));
}

ProjectFile DocumentSession::projectSnapshot() const {
  if (!document_)
    throw std::runtime_error("Open a document before creating a project");
  ProjectFile state;
  if (project_) {
    // The envelope's cached legacy bytes may already be large and are replaced
    // by the current document below. Copy each retained field once instead of
    // copying and immediately discarding an entire old library snapshot.
    state.version = project_->version;
    state.render_settings = project_->render_settings;
    state.preview_settings = project_->preview_settings;
    state.workspace = project_->workspace;
    state.metadata = project_->metadata;
    state.render_presets = project_->render_presets;
    state.plugin_references = project_->plugin_references;
    state.animation_data = project_->animation_data;
    state.retained_source = project_->retained_source;
  }
  state.library_bytes = document_->bytes();
  state.selected_index = std::size_t(flame_);
  return state;
}
void DocumentSession::setProjectState(ProjectFile state) {
  if (!document_)
    throw std::runtime_error(
        "Open a document before updating project settings");
  state.library_bytes = document_->bytes();
  state.selected_index = std::size_t(flame_);
  // Serialization also validates bounded UTF-8 settings and retained data.
  const auto bytes = serialize_project(state);
  if (project_ && bytes == serialize_project(projectSnapshot()))
    return;
  ++revision_;
  project_ = std::move(state);
  projectDirty_ = true;
  emit modifiedChanged(true);
}

void DocumentSession::setProjectRenderSettings(ProjectSettings settings,
                                             std::vector<ProjectPreset> presets) {
  if (project_ && project_->render_settings == settings && project_->render_presets == presets)
    return;
  validate_project_render_settings(settings, presets);
  if (!project_)
    return;
  project_->render_settings = std::move(settings);
  project_->render_presets = std::move(presets);
  ++revision_;
  projectDirty_ = true;
  emit modifiedChanged(true);
}

void DocumentSession::changeAnimation(const QString &label,
                                      std::optional<std::string> bytes) {
  auto state = projectSnapshot();
  if (state.animation_data == bytes)
    return;
  state.animation_data = bytes;
  // Validate all envelope limits before the undo command mutates anything.
  (void)serialize_project(state);
  undo_.push(new AnimationChange(*this, label, std::move(bytes)));
}

void DocumentSession::restoreAnimation(
    const std::optional<std::string> &bytes) {
  auto state = projectSnapshot();
  state.animation_data = bytes;
  ++revision_;
  project_ = std::move(state);
  emit changed();
}

void DocumentSession::selectFlame(int index) {
  if (!document_ || index < 0 || index >= int(document_->entries().size()) ||
      index == flame_)
    return;
  flame_ = index;
  transform_ = 0;
  final_ = false;
  emit selectionChanged();
}

void DocumentSession::selectTransform(int index, bool final) {
  if (index < 0 || (index == transform_ && final == final_))
    return;
  transform_ = index;
  final_ = final;
  emit selectionChanged();
}

void DocumentSession::change(const QString &label,
                             const std::function<void(LegacyDocument &)> &edit,
                             int nextTransform, bool nextFinal, int nextFlame) {
  if (!document_)
    throw std::runtime_error("Open a flame before editing");
  auto next = *document_;
  edit(next);
  if (next.bytes() == document_->bytes()) {
    if (nextFlame >= 0)
      selectFlame(nextFlame);
    return;
  }
  if (nextFlame >= int(next.entries().size()))
    throw std::invalid_argument("The next selected flame does not exist");
  undo_.push(new DocumentChange(*this, label, next.bytes(), nextTransform,
                                nextFinal, nextFlame));
}

void DocumentSession::restore(const std::string &bytes, int flame,
                              int transform, bool final) {
  auto next = LegacyDocument::parse(bytes);
  document_ = std::move(next);
  flame_ = std::clamp(flame, 0, int(document_->entries().size()) - 1);
  transform_ = transform;
  final_ = final;
  ++revision_;
  emit changed();
}

std::uint64_t DocumentSession::queueRecovery(std::optional<ProjectFile> project,
                                             RecoveryPreparation prepare) {
  if (!document_ || !isModified()) {
    discardRecovery();
    return 0;
  }
  const auto directory =
      QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
      "/recovery";
  if (!recoveryWorker_)
    recoveryWorker_ = std::make_unique<RecoveryWorker>(recoveryGeneration_);
  if (recoveryPath_.isEmpty()) {
    recoveryPath_ = directory + '/' +
                      QUuid::createUuid().toString(QUuid::WithoutBraces) +
                      (project_ ? ".apoproject" : ".flame");
  }
  RecoveryWorker::Job job;
  job.generation = recoveryGeneration_;
  job.destination = recoveryPath_;
  job.source = path_;
  job.digest = diskDigest_;
  if (project_) {
    job.project = project ? std::move(project) : std::optional(projectSnapshot());
    job.prepare = std::move(prepare);
  } else {
    job.library = document_->bytes();
  }
  recoveryPoll_.start();
  return recoveryWorker_->submit(std::move(job));
}

void DocumentSession::requestRecovery(std::optional<ProjectFile> project,
                                       RecoveryPreparation prepare) {
  (void)queueRecovery(std::move(project), std::move(prepare));
}

bool DocumentSession::isRecoveryBusy() const {
  return recoveryWorker_ && recoveryWorker_->busy();
}

void DocumentSession::pollRecovery() {
  if (!recoveryWorker_)
    return;
  const auto [completed, busy] = recoveryWorker_->poll();
  // Finish touching our state before signals. A direct slot may destroy this
  // session or enqueue another save (which must retain its restarted timer).
  if (!busy)
    recoveryPoll_.stop();
  if (completed &&
      completed->generation == recoveryGeneration_ &&
      completed->destination == recoveryPath_ && !completed->cancelled) {
    if (completed->error.isEmpty())
      emit recoverySaved(completed->destination);
    else
      emit recoveryFailed(completed->error);
  }
}

QString DocumentSession::writeRecovery() {
  const auto serial = queueRecovery({}, {});
  if (!serial) {
    if (recoveryWorker_)
      recoveryWorker_->waitIdle();
    return {};
  }
  const auto completed = recoveryWorker_->wait(serial);
  if (!completed.error.isEmpty())
    throw std::runtime_error(completed.error.toStdString());
  if (completed.cancelled)
    return {};
  return completed.destination;
}

void DocumentSession::recover(const QString &path) {
  auto lock = std::make_unique<QLockFile>(path + ".lock");
  lock->setStaleLockTime(0);
  if (!lock->tryLock())
    throw std::runtime_error(
        "That recovery belongs to an editor which is still open");
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly) ||
      file.size() > qint64(project_file_limit))
    throw std::runtime_error("Could not read the recovery document");
  const auto bytes = file.read(qint64(project_file_limit) + 1);
  if (file.error() != QFileDevice::NoError ||
      bytes.size() > qint64(project_file_limit))
    throw std::runtime_error("Could not read the complete recovery document");
  loadBytes(bytes.toStdString(), {}, true);
  recoveryPath_ = path;
  if (!recoveryWorker_)
    recoveryWorker_ = std::make_unique<RecoveryWorker>(recoveryGeneration_);
  recoveryWorker_->adopt(path, std::move(lock));
}

void DocumentSession::discardRecovery() {
  if (recoveryPath_.isEmpty())
    return;
  ++recoveryGeneration_;
  if (recoveryWorker_)
    recoveryWorker_->invalidate(recoveryGeneration_, recoveryPath_);
  recoveryPath_.clear();
  recoveryPoll_.start();
}
} // namespace apo::editor
