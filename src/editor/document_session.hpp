#pragma once

#include "legacy_document.hpp"
#include "project_io.hpp"
#include <QLockFile>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUndoStack>
#include <functional>
#include <memory>
#include <stdexcept>

namespace apo::editor {

std::filesystem::path filesystemPath(const QString &path);

class FileConflict : public std::runtime_error {
public:
  FileConflict()
      : std::runtime_error("The file changed on disk after it was opened. Save "
                           "a copy or explicitly "
                           "replace that version.") {}
};

class DocumentSession : public QObject {
  Q_OBJECT
public:
  explicit DocumentSession(QObject *parent = nullptr);
  ~DocumentSession() override;
  bool hasDocument() const { return document_.has_value(); }
  const LegacyDocument &document() const { return document_.value(); }
  const QString &path() const { return path_; }
  bool isModified() const { return !undo_.isClean() || projectDirty_; }
  bool isProject() const { return project_.has_value(); }
  ProjectFile projectSnapshot() const;
  // View/preset settings are kept outside model undo. The complete native
  // envelope is nevertheless included in saving, conflict checks and recovery.
  void setProjectState(ProjectFile state);
  // Animation is document content and participates in the ordinary undo stack.
  // Other project preferences remain outside content undo.
  void changeAnimation(const QString &label, std::optional<std::string> bytes);
  bool hasAnimation() const {
    return project_ && project_->animation_data.has_value();
  }
  QUndoStack &undoStack() { return undo_; }
  int selectedFlame() const { return flame_; }
  int selectedTransform() const { return transform_; }
  bool isFinalTransform() const { return final_; }
  void load(const QString &path);
  void loadBytes(std::string bytes, const QString &path = {},
                 bool modified = false);
  void save(const QString &path, bool replaceExternalChange = false);
  void selectFlame(int index);
  void selectTransform(int index, bool final = false);
  void change(const QString &label,
              const std::function<void(LegacyDocument &)> &edit,
              int nextTransform = -1, bool nextFinal = false,
              int nextFlame = -1);
  QString writeRecovery();
  // Normal autosave: captures immutable bytes on the GUI thread, then prepares,
  // serializes and writes them on a single background worker. Pending requests
  // coalesce. Preparation must own its inputs and must not access Qt widgets.
  using RecoveryPreparation = std::function<void(ProjectFile &)>;
  void requestRecovery(std::optional<ProjectFile> project = {},
                       RecoveryPreparation prepare = {});
  bool isRecoveryBusy() const;
  void recover(const QString &path);
  void discardRecovery();
  QString recoveryPath() const { return recoveryPath_; }
signals:
  void changed();
  void selectionChanged();
  void modifiedChanged(bool modified);
  void recoverySaved(const QString &path);
  void recoveryFailed(const QString &message);

private:
  friend class MainWindow;
  struct SaveSnapshot {
    QString destination, originalPath;
    QByteArray digest;
    ProjectFile project;
    std::uint64_t documentGeneration, revision;
    bool native;
    void write(bool replaceExternalChange, const RecoveryPreparation &prepare = {});
  };
  SaveSnapshot snapshotForSave(const QString &path, std::optional<ProjectFile> project = {}) const;
  bool finishSave(SaveSnapshot saved);
  void setProjectRenderSettings(ProjectSettings settings, std::vector<ProjectPreset> presets);
  std::uint64_t documentGeneration_ = 0, revision_ = 0;
  friend class DocumentChange;
  friend class AnimationChange;
  void restore(const std::string &bytes, int flame, int transform, bool final);
  void restoreAnimation(const std::optional<std::string> &bytes);
  struct RecoveryWorker;
  std::unique_ptr<RecoveryWorker> recoveryWorker_;
  QTimer recoveryPoll_;
  std::uint64_t recoveryGeneration_ = 0;
  std::uint64_t queueRecovery(std::optional<ProjectFile> project,
                              RecoveryPreparation prepare);
  void pollRecovery();
  std::optional<LegacyDocument> document_;
  std::optional<ProjectFile> project_;
  bool projectDirty_ = false;
  QUndoStack undo_;
  QString path_, recoveryPath_;
  QByteArray diskDigest_;
  int flame_ = 0, transform_ = 0;
  bool final_ = false;
};
} // namespace apo::editor
