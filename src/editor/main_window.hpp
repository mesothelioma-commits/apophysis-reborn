#pragma once

#include "../script.hpp"
#include "../animation_export.hpp"
#include "canvases.hpp"
#include "document_session.hpp"
#include "device_discovery.hpp"
#include "preview_controller.hpp"
#include "render_queue.hpp"
#include <QMainWindow>
#include <QJsonObject>
#include <QPointer>
#include <QSettings>
#include <array>

class QAction;
class QCheckBox;
class QComboBox;
class QDockWidget;
class QLabel;
class QLineEdit;
class QFormLayout;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QSpinBox;
class QTabWidget;
class QTableWidget;
class QToolBar;
class QTimer;

namespace apo::editor {

class LocalAiConnection;
class ScriptDialog;

class MainWindow : public QMainWindow {
  Q_OBJECT
public:
  explicit MainWindow(bool useSettings = true, QWidget *parent = nullptr,
                      const QString &queueDirectory = {},
                      std::shared_ptr<DeviceDiscovery> deviceDiscovery = {});
  ~MainWindow() override;
  DocumentSession &session() { return session_; }
  PreviewController &previewController() { return preview_; }
  RenderQueue &renderQueue() { return *queue_; }
  QJsonObject systemInformation() const;
  void showRenderQueue();
  bool openFile(const QString &path);
  void openExample();
  bool openStarterLibrary();
  void recoverPreviousSession();
  void applyAnimation(const AnimationClip &);
  std::vector<QString> queueAnimation(const AnimationClip &,
                                      AnimationExportOptions options);
  void applyScriptResult(
      const ScriptResult &, const QString &outputDirectory,
      bool replaceExisting = false,
      const std::vector<std::filesystem::path> &protectedFiles = {});

protected:
  void closeEvent(QCloseEvent *) override;
  void dragEnterEvent(QDragEnterEvent *) override;
  void dropEvent(QDropEvent *) override;

private:
  DocumentSession session_;
  PreviewController preview_;
  std::unique_ptr<RenderQueue> queue_;
  std::shared_ptr<DeviceDiscovery> deviceDiscovery_;
  QPointer<RenderQueueDialog> queueDialog_;
  QSettings settings_;
  bool useSettings_, refreshing_ = false, closing_ = false, saving_ = false;
  QTimer *closeTimer_ = nullptr;
  std::optional<Flame> model_;
  std::optional<Transform> copiedTransform_;
  QDockWidget *libraryDock_, *transformDock_, *propertiesDock_, *logDock_;
  QListWidget *library_;
  QLineEdit *librarySearch_, *name_, *centerX_, *centerY_, *scale_;
  QSpinBox *width_, *height_;
  QComboBox *transforms_, *matrixMode_, *coordinates_, *quality_;
  QCheckBox *lock_, *snap_, *grid_;
  PreviewCanvas *previewCanvas_;
  TriangleCanvas *triangles_;
  QTabWidget *properties_;
  std::array<QLineEdit *, 6> coefficients_{};
  QLineEdit *weight_, *color_, *colorSpeed_, *opacity_;
  QFormLayout *variationForm_;
  std::map<std::string_view, QLineEdit *> variationFields_;
  void refreshVariationParameters();
  QLineEdit *pivotX_, *pivotY_, *rotation_, *factor_, *translateX_,
      *translateY_;
  QTableWidget *variations_, *chaos_;
  QLabel *producer_, *diagnostic_, *stats_, *coordinatesLabel_, *exportStatus_,
      *palette_;
  QProgressBar *progress_, *exportProgress_;
  QPlainTextEdit *log_;
  QAction *save_, *pause_, *render_, *duplicate_, *remove_, *add_, *addFinal_,
      *copy_, *paste_, *moveUp_, *moveDown_;
  QAction *gradient_, *adjustments_, *mutations_, *randomFlames_,
      *duplicateFlame_, *deleteFlame_;
  QAction *queueAction_, *animation_;
  std::unique_ptr<LocalAiConnection> localAi_;
  QPointer<ScriptDialog> aiReview_;
  QLabel *aiIndicator_ = nullptr;
  quint64 aiSelection_ = 0;
  bool aiPreviewCurrent_ = false;
  QByteArray aiPreviewPng_;
  QSize aiPreviewSize_;
  void showLocalAiSettings();
  QString localAiRevision() const;
  QJsonObject handleLocalAi(const QString &tool, const QJsonObject &arguments);

  QByteArray defaultLayout_;
  ProjectSettings renderSettings_;
  std::vector<ProjectPreset> renderPresets_;
  QString previewBackend_ = "CPU", previewDevice_ = "CPU", previewFallback_,
          lastBackendReport_;

  void makeActions();
  void showScriptEditor();
  void showAnimationTimeline();
  void exportFlameLibrary();
  void showPluginManager();
  void editPluginVariations();
  bool importUpr(const QString &path);
  void exportUpr();
  ProjectFile projectViewSnapshot() const;
  void requestRecovery();
  void restoreProjectState();
  void makeLibrary();
  void makePreview();
  void makeTransformEditor();
  void makeProperties();
  void makeLogs();
  void refresh();
  void refreshSelection();
  void refreshTransform();
  void requestPreview();
  void setCoefficients(const Affine &value);
  const Transform &selectedTransform() const;
  void editTransform(const QString &label, const LegacyTransformEdits &edit);
  void commitAffine(const Affine &value);
  void applyGeometry(const QString &label, const Affine &operation);
  void chooseOpen();
  bool confirmDiscard();
  bool saveDocument(bool saveAs = false);
  bool saveToPath(const QString &path, bool replaceExternalChange = false);
  void renderImage();
  void preferences();
  void newDocument();
  void editGradient();
  void editAdjustments();
  void exploreFlames(bool mutation);
  void filterLibrary();
  void record(const QString &message);
  void run(const std::function<void()> &operation);
  void updateTitle();
};
void applyDarkTheme();
} // namespace apo::editor
