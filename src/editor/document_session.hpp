#pragma once

#include "legacy_document.hpp"
#include <QObject>
#include <QString>
#include <QUndoStack>
#include <QLockFile>
#include <memory>
#include <functional>
#include <stdexcept>

namespace apo::editor {

std::filesystem::path filesystemPath(const QString &path);

class FileConflict : public std::runtime_error {
  public:
    FileConflict()
        : std::runtime_error("The file changed on disk after it was opened. Save a copy or explicitly "
                             "replace that version.") {}
};

class DocumentSession : public QObject {
    Q_OBJECT
  public:
    explicit DocumentSession(QObject *parent = nullptr);
    ~DocumentSession() override;
    bool hasDocument() const {
        return document_.has_value();
    }
    const LegacyDocument &document() const {
        return document_.value();
    }
    const QString &path() const {
        return path_;
    }
    bool isModified() const {
        return !undo_.isClean();
    }
    QUndoStack &undoStack() {
        return undo_;
    }
    int selectedFlame() const {
        return flame_;
    }
    int selectedTransform() const {
        return transform_;
    }
    bool isFinalTransform() const {
        return final_;
    }
    void load(const QString &path);
    void loadBytes(std::string bytes, const QString &path = {}, bool modified = false);
    void save(const QString &path, bool replaceExternalChange = false);
    void selectFlame(int index);
    void selectTransform(int index, bool final = false);
    void change(const QString &label, const std::function<void(LegacyDocument &)> &edit,
                int nextTransform = -1, bool nextFinal = false);
    QString writeRecovery();
    void recover(const QString &path);
    void discardRecovery();
    QString recoveryPath() const {
        return recoveryPath_;
    }
  signals:
    void changed();
    void selectionChanged();
    void modifiedChanged(bool modified);

  private:
    friend class DocumentChange;
    void restore(const std::string &bytes, int flame, int transform, bool final);
    std::optional<LegacyDocument> document_;
    QUndoStack undo_;
    QString path_, recoveryPath_;
    std::unique_ptr<QLockFile> recoveryLock_;
    QByteArray diskDigest_;
    int flame_ = 0, transform_ = 0;
    bool final_ = false;
};
} // namespace apo::editor
