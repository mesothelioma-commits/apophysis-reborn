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
#include <fstream>

namespace apo::editor {

std::filesystem::path filesystemPath(const QString &path) {
#ifdef _WIN32
  return std::filesystem::path(path.toStdWString());
#else
  return std::filesystem::path(path.toUtf8().constData());
#endif
}

namespace {
QByteArray digest(const QString &path) {
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

DocumentSession::DocumentSession(QObject *parent)
    : QObject(parent), undo_(this) {
  undo_.setUndoLimit(100);
  connect(&undo_, &QUndoStack::cleanChanged, this, [this](bool clean) {
    if (clean && !projectDirty_)
      discardRecovery();
    emit modifiedChanged(isModified());
  });
}
DocumentSession::~DocumentSession() {
  disconnect(&undo_, nullptr, this, nullptr);
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

void DocumentSession::save(const QString &path, bool replaceExternalChange) {
  if (!document_)
    throw std::runtime_error("Open a flame before saving");
  const auto absolute = QFileInfo(path).absoluteFilePath();
  const bool sameFile =
      !path_.isEmpty() &&
      (absolute == path_ ||
       (QFileInfo(path).exists() && QFileInfo(path).canonicalFilePath() ==
                                        QFileInfo(path_).canonicalFilePath()));
  const auto checkConflict = [&] {
    if (sameFile && !replaceExternalChange &&
        (!QFileInfo(path).exists() || digest(path) != diskDigest_))
      throw FileConflict();
  };
  checkConflict();
  const bool native = is_project_path(filesystemPath(path));
  auto project = projectSnapshot();
  const auto bytes = native ? serialize_project(project) : document_->bytes();
  write_atomic(
      filesystemPath(absolute), [&](std::ostream &out) { out << bytes; },
      checkConflict);
  path_ = absolute;
  project_ = native ? std::optional(std::move(project)) : std::nullopt;
  projectDirty_ = false;
  diskDigest_ = QCryptographicHash::hash(
      QByteArray::fromRawData(bytes.data(), qsizetype(bytes.size())),
      QCryptographicHash::Sha256);
  undo_.setClean();
  discardRecovery();
  emit changed();
  emit modifiedChanged(isModified());
}

ProjectFile DocumentSession::projectSnapshot() const {
  if (!document_)
    throw std::runtime_error("Open a document before creating a project");
  auto state = project_.value_or(ProjectFile{});
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
  project_ = std::move(state);
  projectDirty_ = true;
  emit modifiedChanged(true);
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
  emit changed();
}

QString DocumentSession::writeRecovery() {
  if (!document_ || !isModified()) {
    discardRecovery();
    return {};
  }
  const auto directory =
      QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
      "/recovery";
  if (!QDir().mkpath(directory))
    throw std::runtime_error("Could not create the recovery folder");
  if (recoveryPath_.isEmpty()) {
    const auto path = directory + '/' +
                      QUuid::createUuid().toString(QUuid::WithoutBraces) +
                      (project_ ? ".apoproject" : ".flame");
    auto lock = std::make_unique<QLockFile>(path + ".lock");
    lock->setStaleLockTime(0);
    if (!lock->tryLock())
      throw std::runtime_error("Could not reserve the recovery file");
    recoveryPath_ = path;
    recoveryLock_ = std::move(lock);
  }
  const auto bytes =
      project_ ? serialize_project(projectSnapshot()) : document_->bytes();
  write_atomic(filesystemPath(recoveryPath_),
               [&](std::ostream &out) { out << bytes; });
  const auto info =
      QJsonDocument(QJsonObject{{"source", path_},
                                {"original_sha256",
                                 QString::fromLatin1(diskDigest_.toHex())}})
          .toJson();
  write_atomic(filesystemPath(recoveryPath_ + ".json"),
               [&](std::ostream &out) { out.write(info.data(), info.size()); });
  return recoveryPath_;
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
  recoveryLock_ = std::move(lock);
}

void DocumentSession::discardRecovery() {
  if (recoveryPath_.isEmpty())
    return;
  QFile::remove(recoveryPath_);
  QFile::remove(recoveryPath_ + ".json");
  recoveryPath_.clear();
  recoveryLock_.reset();
}
} // namespace apo::editor
