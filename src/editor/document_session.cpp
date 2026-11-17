#include "document_session.hpp"
#include "output_file.hpp"
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
        throw std::runtime_error("Could not read the complete file for conflict detection");
    return hash.result();
}
} // namespace

class DocumentChange : public QUndoCommand {
  public:
    DocumentChange(DocumentSession &session, QString label, std::string after, int next, bool final)
        : QUndoCommand(std::move(label)), session_(session), before_(session.document().bytes()),
          after_(std::move(after)), flame_(session.selectedFlame()),
          beforeTransform_(session.selectedTransform()),
          afterTransform_(next < 0 ? beforeTransform_ : next), beforeFinal_(session.isFinalTransform()),
          afterFinal_(next < 0 ? beforeFinal_ : final) {}
    void undo() override {
        session_.restore(before_, flame_, beforeTransform_, beforeFinal_);
    }
    void redo() override {
        session_.restore(after_, flame_, afterTransform_, afterFinal_);
    }

  private:
    DocumentSession &session_;
    std::string before_, after_;
    int flame_, beforeTransform_, afterTransform_;
    bool beforeFinal_, afterFinal_;
};

DocumentSession::DocumentSession(QObject *parent) : QObject(parent), undo_(this) {
    undo_.setUndoLimit(100);
    connect(&undo_, &QUndoStack::cleanChanged, this, [this](bool clean) {
        if (clean)
            discardRecovery();
        emit modifiedChanged(!clean);
    });
}
DocumentSession::~DocumentSession() {
    disconnect(&undo_, nullptr, this, nullptr);
}

void DocumentSession::load(const QString &path) {
    // Read once so the loaded document and conflict fingerprint describe identical bytes.
    auto next = LegacyDocument::load(filesystemPath(path));
    loadBytes(next.bytes(), QFileInfo(path).absoluteFilePath());
}

void DocumentSession::loadBytes(std::string bytes, const QString &path, bool modified) {
    auto next = LegacyDocument::parse(std::move(bytes));
    const auto &source = next.bytes();
    const auto fingerprint = QCryptographicHash::hash(
        QByteArray::fromRawData(source.data(), qsizetype(source.size())), QCryptographicHash::Sha256);
    discardRecovery();
    document_ = std::move(next);
    path_ = path.isEmpty() ? QString{} : QFileInfo(path).absoluteFilePath();
    diskDigest_ = path.isEmpty() ? QByteArray{} : fingerprint;
    flame_ = transform_ = 0;
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
        (absolute == path_ || (QFileInfo(path).exists() && QFileInfo(path).canonicalFilePath() ==
                                                               QFileInfo(path_).canonicalFilePath()));
    const auto checkConflict = [&] {
        if (sameFile && !replaceExternalChange &&
            (!QFileInfo(path).exists() || digest(path) != diskDigest_))
            throw FileConflict();
    };
    checkConflict();
    write_atomic(
        filesystemPath(absolute), [&](std::ostream &out) { out << document_->bytes(); }, checkConflict);
    path_ = absolute;
    const auto &bytes = document_->bytes();
    diskDigest_ = QCryptographicHash::hash(
        QByteArray::fromRawData(bytes.data(), qsizetype(bytes.size())), QCryptographicHash::Sha256);
    undo_.setClean();
    discardRecovery();
    emit changed();
}

void DocumentSession::selectFlame(int index) {
    if (!document_ || index < 0 || index >= int(document_->entries().size()) || index == flame_)
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

void DocumentSession::change(const QString &label, const std::function<void(LegacyDocument &)> &edit,
                             int nextTransform, bool nextFinal) {
    if (!document_)
        throw std::runtime_error("Open a flame before editing");
    auto next = *document_;
    edit(next);
    if (next.bytes() == document_->bytes())
        return;
    undo_.push(new DocumentChange(*this, label, next.bytes(), nextTransform, nextFinal));
}

void DocumentSession::restore(const std::string &bytes, int flame, int transform, bool final) {
    auto next = LegacyDocument::parse(bytes);
    document_ = std::move(next);
    flame_ = flame;
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
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/recovery";
    if (!QDir().mkpath(directory))
        throw std::runtime_error("Could not create the recovery folder");
    if (recoveryPath_.isEmpty()) {
        const auto path =
            directory + '/' + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".flame";
        auto lock = std::make_unique<QLockFile>(path + ".lock");
        lock->setStaleLockTime(0);
        if (!lock->tryLock())
            throw std::runtime_error("Could not reserve the recovery file");
        recoveryPath_ = path;
        recoveryLock_ = std::move(lock);
    }
    write_atomic(filesystemPath(recoveryPath_), [&](std::ostream &out) { out << document_->bytes(); });
    const auto info =
        QJsonDocument(QJsonObject{{"source", path_},
                                  {"original_sha256", QString::fromLatin1(diskDigest_.toHex())}})
            .toJson();
    write_atomic(filesystemPath(recoveryPath_ + ".json"),
                 [&](std::ostream &out) { out.write(info.data(), info.size()); });
    return recoveryPath_;
}

void DocumentSession::recover(const QString &path) {
    auto lock = std::make_unique<QLockFile>(path + ".lock");
    lock->setStaleLockTime(0);
    if (!lock->tryLock())
        throw std::runtime_error("That recovery belongs to an editor which is still open");
    const auto recovered = LegacyDocument::load(filesystemPath(path));
    loadBytes(recovered.bytes(), {}, true);
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
