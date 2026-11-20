#include "main_window.hpp"
#include "sequence_composition.hpp"
#include "background_jobs.hpp"
#include "local_ai_connection.hpp"
#include "flame_favorites.hpp"
#include "variation_explorer.hpp"
#include "render_proof_dialog.hpp"
#include "build_info.hpp"
#include "support_dialogs.hpp"
#include "creative_widgets.hpp"
#include "flame_io.hpp"
#include "editable_png.hpp"
#include "gpu.hpp"
#include "output_file.hpp"
#include "plugin_dialog.hpp"
#include "project_document.hpp"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHash>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QProgressDialog>
#include <QScopedValueRollback>
#include <QScopeGuard>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStorageInfo>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>
#include <algorithm>
#include <charconv>
#include <climits>
#include <cmath>
#include <future>
#include <numbers>

namespace apo::editor {
namespace {
Q_APPLICATION_STATIC(ApplicationJobs, pngReaders)
QString text(std::string_view value) {
  return QString::fromUtf8(value.data(), qsizetype(value.size()));
}
// Narrow library docks put the artwork above the name. Wide docks use the
// standard Qt list layout. Both modes share the same items and selection.
class FlameLibraryDelegate final : public QStyledItemDelegate {
public:
  using QStyledItemDelegate::QStyledItemDelegate;

  QSize sizeHint(const QStyleOptionViewItem &option,
                 const QModelIndex &index) const override {
    auto size = QStyledItemDelegate::sizeHint(option, index);
    if (option.decorationPosition == QStyleOptionViewItem::Top &&
        option.rect.width() > 0) {
      const int textWidth = std::max(1, option.rect.width() - 22);
      const auto text = index.data(Qt::DisplayRole).toString();
      const auto bounds = option.fontMetrics.boundingRect(
          QRect(0, 0, textWidth, 0), Qt::TextWordWrap | Qt::AlignCenter, text);
      const int imageHeight = index.data(Qt::DecorationRole).isNull()
                                  ? 0 : option.decorationSize.height();
      size.setHeight(std::max(size.height(), imageHeight + bounds.height() + 18));
    }
    return size;
  }
};

class FlameLibraryList final : public QListWidget {
protected:
  void initViewItemOption(QStyleOptionViewItem *option) const override {
    QListWidget::initViewItemOption(option);
    if (viewport()->width() < iconSize().width() + 14 * option->fontMetrics.averageCharWidth() + 8) {
      option->decorationPosition = QStyleOptionViewItem::Top;
      option->decorationAlignment = Qt::AlignHCenter | Qt::AlignTop;
      option->displayAlignment = Qt::AlignCenter;
      option->decorationSize = iconSize().scaled(
          std::max(1, viewport()->width() - 8), 39, Qt::KeepAspectRatio);
    }
  }
};

constexpr auto starterPath = ":/examples/starter-library.flame";

QByteArray thumbnailKey(std::string_view entry) {
  return QCryptographicHash::hash(QByteArrayView(entry.data(), qsizetype(entry.size())),
                                  QCryptographicHash::Sha256);
}

QString starterThumbnail(const QByteArray &key) {
  // Matching source bytes also handles edited, reordered and saved collections.
  static const auto paths = [] {
    QHash<QByteArray, QString> result;
    QFile file(starterPath);
    if (file.open(QIODevice::ReadOnly)) {
      const auto document = LegacyDocument::parse(file.readAll().toStdString());
      for (std::size_t i = 0; i < document.entries().size(); ++i)
        result.insert(thumbnailKey(document.element_bytes(i)),
                      QString(":/examples/starter/%1.png").arg(i + 1, 2, 10, QChar('0')));
    }
    return result;
  }();
  return paths.value(key);
}

QString number(double value) {
  char buffer[128];
  const auto result =
      std::to_chars(std::begin(buffer), std::end(buffer), value);
  return QString::fromLatin1(buffer, qsizetype(result.ptr - buffer));
}
void setNumber(QLineEdit *field, double value) {
  const auto formatted = number(value);
  field->setText(formatted);
  field->setCursorPosition(0);
  field->setToolTip(formatted);
  field->setProperty("committedText", formatted);
}
double readNumber(const QLineEdit *field) {
  bool ok = false;
  const double value = QLocale::c().toDouble(field->text().trimmed(), &ok);
  if (!ok || !std::isfinite(value))
    throw std::invalid_argument("Enter a finite number in " +
                                field->accessibleName().toStdString());
  return value;
}
QLineEdit *numeric(const QString &name, double initial,
                   QWidget *parent = nullptr) {
  auto field = new QLineEdit(parent);
  field->setObjectName(name);
  field->setAccessibleName(name);
  field->setAlignment(Qt::AlignRight);
  field->setMinimumWidth(64);
  setNumber(field, initial);
  return field;
}
void numberEdited(QLineEdit *field, QObject *receiver,
                  const std::function<void()> &commit) {
  QObject::connect(
      field, &QLineEdit::editingFinished, receiver, [field, commit] {
        if (field->text() != field->property("committedText").toString())
          commit();
      });
}
QWidget *scrollPanel(QWidget *content) {
  auto area = new QScrollArea;
  area->setWidgetResizable(true);
  area->setFrameShape(QFrame::NoFrame);
  area->setWidget(content);
  return area;
}
// Both docked tool rows share the preview column's width. A form can wrap the
// two groups intact when that column narrows, while retaining normal tab order.
QHBoxLayout *dockControlRow(QVBoxLayout *layout, QHBoxLayout *leading) {
  auto trailing = new QHBoxLayout;
  auto row = new QFormLayout;
  row->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  row->setRowWrapPolicy(QFormLayout::WrapLongRows);
  row->setLayout(0, QFormLayout::LabelRole, leading);
  row->setLayout(0, QFormLayout::FieldRole, trailing);
  layout->addLayout(row);
  return trailing;
}
Affine compose(const Affine &left, const Affine &right) {
  return {left.a * right.a + left.c * right.b,
          left.b * right.a + left.d * right.b,
          left.a * right.c + left.c * right.d,
          left.b * right.c + left.d * right.d,
          left.a * right.e + left.c * right.f + left.e,
          left.b * right.e + left.d * right.f + left.f};
}

struct RenderControls {
  QComboBox *backend, *device;
  QSpinBox *gpuMemory;
  QCheckBox *fallback;

  template <class Settings> void apply(Settings &settings) const {
    settings.backend =
        static_cast<RenderBackend>(backend->currentData().toInt());
    settings.device = device->currentData().toString().toStdString();
    settings.gpu_memory_mib = std::uint64_t(gpuMemory->value());
    settings.allow_cpu_fallback = fallback->isChecked();
  }
};

RenderControls renderControls(QFormLayout *form, QDialog *dialog,
                              const PreviewSettings &settings,
                              std::shared_ptr<DeviceDiscovery> discovery) {
  RenderControls controls{new QComboBox, new QComboBox, new QSpinBox,
                          new QCheckBox};
  controls.backend->setObjectName("renderBackend");
  controls.backend->addItem("CPU (reference)", int(RenderBackend::cpu));
  controls.backend->addItem("Automatic (GPU when available)",
                            int(RenderBackend::automatic));
  controls.backend->addItem("Vulkan (FP32 sampling)",
                            int(RenderBackend::vulkan));
  controls.backend->setCurrentIndex(
      controls.backend->findData(int(settings.backend)));
  controls.device->setObjectName("renderDevice");
  controls.device->setSizeAdjustPolicy(
      QComboBox::AdjustToMinimumContentsLengthWithIcon);
  controls.device->setMinimumContentsLength(24);
  controls.device->addItem("Automatic selection", QString{});
  if (!settings.device.empty()) {
    controls.device->addItem("Saved device (not yet checked)", text(settings.device));
    controls.device->setCurrentIndex(1);
  }
  struct InventoryView {
    ComputeInfo report;
    bool ready = false;
    std::uint64_t revision = 0;
  };
  auto inventory = std::make_shared<InventoryView>();
  controls.gpuMemory->setObjectName("gpuMemory");
  controls.gpuMemory->setRange(1, 1048576);
  controls.gpuMemory->setValue(
      int(std::min(settings.gpu_memory_mib, UINT64_C(1048576))));
  controls.gpuMemory->setSuffix(" MiB");
  controls.fallback->setObjectName("cpuFallback");
  controls.fallback->setText("Use CPU if Vulkan cannot render this flame");
  controls.fallback->setChecked(settings.allow_cpu_fallback);
  form->addRow("Renderer", controls.backend);
  form->addRow("Preferred device", controls.device);
  auto refreshDevices = new QPushButton("Refresh devices");
  refreshDevices->setObjectName("refreshRenderDevices");
  form->addRow(QString{}, refreshDevices);
  form->addRow("GPU working budget", controls.gpuMemory);
  form->addRow(QString{}, controls.fallback);
  auto details = new QLabel;
  details->setObjectName("computeDetails");
  details->setWordWrap(true);
  details->setTextFormat(Qt::PlainText);
  details->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  details->setTextInteractionFlags(Qt::TextSelectableByMouse);
  details->setMinimumWidth(360);
  form->addRow(details);
  auto poll = new QTimer(dialog);
  poll->setObjectName("computeDiscoveryPoll");
  poll->setInterval(50);
  auto update = [controls, details, inventory, discovery, dialog, poll, refreshDevices] {
    const auto backend =
        static_cast<RenderBackend>(controls.backend->currentData().toInt());
    const bool usesGpu = backend != RenderBackend::cpu;
    if (usesGpu)
      discovery->request();
    const auto snapshot = discovery->snapshot();
    if (snapshot.report && (!inventory->ready || inventory->revision != snapshot.revision)) {
      const QSignalBlocker block(controls.device);
      auto savedDevice = controls.device->currentData().toString();
      inventory->report = *snapshot.report;
      inventory->ready = true;
      inventory->revision = snapshot.revision;
      controls.device->clear();
      controls.device->addItem("Automatic selection", QString{});
      for (const auto &device : inventory->report.devices) {
        const auto label = text(device.name) + (device.software ? " (software)" : "") +
                           (device.usable ? "" : " (unavailable)");
        controls.device->addItem(label, text(device.id));
        if (savedDevice == QString::number(device.index))
          savedDevice = text(device.id);
      }
      int selected = controls.device->findData(savedDevice);
      if (selected < 0) {
        controls.device->addItem("Saved device is currently unavailable", savedDevice);
        selected = controls.device->count() - 1;
      }
      controls.device->setCurrentIndex(selected);
    }
    if (snapshot.pending)
      poll->start();
    else
      poll->stop();
    controls.device->setEnabled(usesGpu);
    refreshDevices->setEnabled(usesGpu && !snapshot.pending);
    controls.gpuMemory->setEnabled(usesGpu);
    controls.fallback->setEnabled(usesGpu);
    QString description;
    const auto &report = inventory->report;
    const auto key = controls.device->currentData().toString();
    const auto selected = std::find_if(
        report.devices.begin(), report.devices.end(),
        [&](const auto &device) { return text(device.id) == key; });
    if (!usesGpu) {
      description = "CPU rendering keeps the existing 64-bit double-precision "
                    "reference path.";
    } else if (!inventory->ready) {
      description = "Checking Vulkan devices in the background… You can continue "
                    "using this dialog, choose CPU, or close it.";
    } else if (selected != report.devices.end()) {
      description =
          QString("%1 · Vulkan %2 · %3 MiB device memory\n"
                  "Maximum storage buffer: %4 MiB · FP64 capability: %5 · "
                  "Int64: %6")
              .arg(text(selected->type), text(selected->api_version))
              .arg(qulonglong(selected->device_memory_mib))
              .arg(double(selected->max_storage_buffer_bytes) / 1048576, 0, 'f',
                   0)
              .arg(selected->float64 ? "yes" : "no",
                   selected->int64 ? "yes" : "no");
      if (!selected->reason.empty())
        description += "\n" + text(selected->reason);
      if (selected->software)
        description += "\nThis is software Vulkan on the CPU. An explicit "
                       "device choice can use it; "
                       "automatic device selection skips it.";
    } else if (!key.isEmpty()) {
      description = "The saved device is unavailable. Choose another device or "
                    "enable CPU fallback.";
    } else {
      const auto usable = std::count_if(
          report.devices.begin(), report.devices.end(),
          [](const auto &device) { return device.usable && !device.software; });
      description = QString("%1 usable hardware Vulkan device(s) found. ")
                        .arg(qlonglong(usable)) +
                    "Automatic selection skips software devices.";
      if (!report.diagnostic.empty())
        description += "\n" + text(report.diagnostic);
    }
    if (usesGpu && inventory->ready && snapshot.pending)
      description += "\nRefreshing the device list in the background…";
    if (usesGpu)
      description += "\nVulkan uses 32-bit orbit calculations and CPU image "
                     "reconstruction; "
                     "the CPU reference uses 64-bit calculations. Random "
                     "streams and rounding "
                     "differ, so identical pixels across backends or devices "
                     "are not guaranteed.";
    controls.device->setToolTip(key);
    if (details->text() == description)
      return;
    details->setText(description);
    // A dialog first opened on the short CPU description keeps its initial
    // height when a word-wrapped QLabel later grows. Measure after layout
    // has assigned the real width, then expand to include every line. Keep
    // the largest measured height so toggling back does not shrink the UI.
    const auto fitDetails = [details, dialog] {
      auto layout = dialog->layout();
      if (!layout)
        return;
      for (int pass = 0; pass < 3; ++pass) {
        layout->invalidate();
        layout->activate();
        const int needed = details->heightForWidth(details->width());
        if (needed > details->minimumHeight())
          details->setMinimumHeight(needed);
        details->updateGeometry();
        layout->invalidate();
        const auto previousSize = dialog->size();
        dialog->adjustSize();
        dialog->resize(dialog->size().expandedTo(previousSize));
      }
    };
    // Once visible, settle the whole layout before returning from a backend
    // change. Deferring that resize can make a later CPU choice appear to
    // enlarge the dialog when an earlier Vulkan layout request arrives.
    if (dialog->isVisible())
      fitDetails();
    else
      QTimer::singleShot(0, dialog, fitDetails);
  };
  QObject::connect(controls.backend, &QComboBox::currentIndexChanged, dialog,
                   update);
  QObject::connect(controls.device, &QComboBox::currentIndexChanged, dialog,
                   update);
  QObject::connect(poll, &QTimer::timeout, dialog, update);
  QObject::connect(refreshDevices, &QPushButton::clicked, dialog,
                   [discovery, update] { discovery->request(true); update(); });
  update();
  return controls;
}
} // namespace

void applyDarkTheme() {
  QApplication::setStyle("Fusion");
  QPalette p;
  p.setColor(QPalette::Window, QColor("#202731"));
  p.setColor(QPalette::WindowText, QColor("#dde4ed"));
  p.setColor(QPalette::Base, QColor("#171d25"));
  p.setColor(QPalette::AlternateBase, QColor("#232c37"));
  p.setColor(QPalette::Text, QColor("#dde4ed"));
  p.setColor(QPalette::PlaceholderText, QColor("#8e9cae"));
  p.setColor(QPalette::Button, QColor("#2b3542"));
  p.setColor(QPalette::ButtonText, QColor("#dde4ed"));
  p.setColor(QPalette::Highlight, QColor("#316a7c"));
  p.setColor(QPalette::HighlightedText, Qt::white);
  p.setColor(QPalette::ToolTipBase, QColor("#e3e9ef"));
  p.setColor(QPalette::ToolTipText, QColor("#1c242d"));
  p.setColor(QPalette::Disabled, QPalette::Text, QColor("#74808d"));
  p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#74808d"));
  qApp->setPalette(p);
  QFont font("Inter");
  font.setPointSize(10);
  qApp->setFont(font);
  qApp->setStyleSheet(
      "QMainWindow::separator { width: 12px; height: 8px; background: #394553; }"
      "QMainWindow::separator:hover { background: #568b99; }"
      "QToolBar { spacing: 5px; padding: 5px; border: 0; border-bottom: 1px "
      "solid #39434e; }"
      "QToolButton { padding: 5px 7px; }"
      "QDockWidget::title { background: #252e39; padding: 7px; font-weight: "
      "600; }"
      "QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox { padding: 4px; border: "
      "1px solid #435060; "
      "border-radius: 3px; }"
      "QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus "
      "{ border-color: "
      "#56c8da; }"
      "QPushButton { padding: 5px 9px; }"
      "QListWidget { border: 0; outline: 0; }"
      "QListWidget::item { padding: 7px; border-bottom: 1px solid #2b3542; }"
      "QHeaderView::section { background: #2b3542; padding: 5px; border: 0; }"
      "QGroupBox { font-weight: 600; border: 1px solid #394553; border-radius: "
      "4px; margin-top: 12px; "
      "padding-top: 9px; }"
      "QGroupBox::title { subcontrol-origin: margin; left: 9px; padding: 0 "
      "4px; }"
      "QTabWidget::pane { border: 1px solid #394553; }"
      "QStatusBar { color: #aab8c8; }"
      "QProgressBar { border: 0; background: #171d25; text-align: center; "
      "max-height: 16px; }"
      "QProgressBar::chunk { background: #387f92; }");
}

MainWindow::MainWindow(bool useSettings, QWidget *parent,
                       const QString &queueDirectory,
                       std::shared_ptr<DeviceDiscovery> deviceDiscovery)
    : QMainWindow(parent), session_(this), preview_(this, PreviewController::Purpose::main),
      deviceDiscovery_(deviceDiscovery ? std::move(deviceDiscovery)
                                       : std::make_shared<DeviceDiscovery>()),
      useSettings_(useSettings) {
  const auto pluginWarnings =
      useSettings_ ? loadApprovedPlugins(settings_) : QStringList{};
  if (!queueDirectory.isEmpty())
    favoritesDirectory_ = QDir(queueDirectory).filePath("favorites");
  QString queueWarning;
  try {
    const auto data = queueDirectory.isEmpty()
                          ? QStandardPaths::writableLocation(
                                QStandardPaths::AppLocalDataLocation)
                          : queueDirectory;
    const bool persistent = useSettings || !queueDirectory.isEmpty();
    queue_ = std::make_unique<RenderQueue>(
        persistent ? data + "/render-queue.json" : QString{},
        persistent ? data + "/render-checkpoints" : QString{}, this);
  } catch (const std::exception &error) {
    if (!queueDirectory.isEmpty())
      throw;
    queueWarning = text(error.what()) +
                   ". This session's queue can be saved with Save queue.";
    const auto separateRoot =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
        "/render-checkpoints-session-" +
        QUuid::createUuid().toString(QUuid::WithoutBraces);
    queue_ = std::make_unique<RenderQueue>(QString{}, separateRoot, this);
  }
  setObjectName("mainWindow");
  const auto screen = QGuiApplication::primaryScreen();
  const auto available =
      screen ? screen->availableGeometry().size() - QSize(32, 64)
             : QSize(1420, 900);
  resize(QSize(1420, 900).boundedTo(available));
  setWindowIcon(QIcon(":/examples/ember-spiral.png"));
  setAcceptDrops(true);
  setDockOptions(QMainWindow::AllowNestedDocks | QMainWindow::AllowTabbedDocks |
                 QMainWindow::AnimatedDocks);
  setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
  setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
  localAi_ = std::make_unique<LocalAiConnection>(
      [this](const QString &tool, const QJsonObject &arguments) {
        return handleLocalAi(tool, arguments);
      }, this);
  makeActions();
  makeLibrary();
  makePreview();
  makeTransformEditor();
  makeProperties();
  makeLogs();
  aiIndicator_ = new QLabel("Local AI on");
  aiIndicator_->setToolTip("Manage sharing under Tools → Local AI connection");
  statusBar()->addPermanentWidget(aiIndicator_);
  aiIndicator_->hide();
  for (const auto &warning : pluginWarnings)
    record(warning);
  resizeDocks({libraryDock_, propertiesDock_}, {220, 370}, Qt::Horizontal);
  defaultLayout_ = saveState(1);
  auto view = menuBar()->addMenu("&View");
  for (auto dock : {libraryDock_, transformDock_, propertiesDock_, logDock_})
    view->addAction(dock->toggleViewAction());
  view->addSeparator();
  view->addAction("Reset workspace", this,
                  [this] { restoreState(defaultLayout_, 1); });
  auto full = view->addAction("Full screen", this, [this] {
    isFullScreen() ? showNormal() : showFullScreen();
  });
  full->setShortcut(Qt::Key_F11);
  auto help = menuBar()->addMenu("&Help");
  auto manual = help->addAction("User manual", this, [this] {
    run([this] { UserManualDialog dialog(this); dialog.exec(); });
  });
  manual->setObjectName("actionUserManual");
  manual->setShortcut(QKeySequence::HelpContents);
  auto information = help->addAction("System information…", this, [this] {
    SystemInformationDialog dialog(systemInformation(), this);
    dialog.exec();
  });
  information->setObjectName("actionSystemInformation");
  help->addSeparator();
  help->addAction("Editor controls", this, [this] {
    QMessageBox::information(
        this, "Editor controls",
        "Drag a triangle's O handle or interior to move it. Drag X or Y to "
        "change its basis. "
        "Middle-drag pans the grid and the wheel zooms. Esc cancels a drag.\n\n"
        "The matrix selector switches between pre-affine and post-affine "
        "editing. Rotate and scale "
        "use the chosen world or local pivot. Every edit supports undo.\n\n"
        "The preview refines automatically. Pause stops preview work. Render "
        "image writes the "
        "selected flame at its declared output size.\n\n"
        "Unsupported flames keep their original settings and can be renamed "
        "and saved. The "
        "diagnostics panel identifies their first render blocker.");
  });
  help->addAction("About Apophysis Reborn", this, [this] {
    QMessageBox::about(
        this, "Apophysis Reborn " + text(application_version()),
        QString("C++20 / Qt 6 / CPU "
        "and Vulkan compute\n\n"
        "%1 registered variations, gradient and adjustment editors, seeded "
        "generation and "
        "mutations. Global sampling, disk-backed reconstruction, persistent "
        "render queues, "
        "restart-safe sampling checkpoints and 8/16-bit image exports.\n"
        "Lua and legacy script editor, trusted native variation plugins, "
        "native projects and historical palette/UPR formats.\n"
        "Keyframe timeline, morphing, frame sequences, RGB/HSV/HSL gradients "
        "and original 7X color curves.\n"
        "See the packaged compatibility report for historical rendering "
        "coverage.\n\n"
        "Qt and other third-party license texts accompany the portable "
        "distribution.").arg(variation_registry().size()));
  });
  help->addAction("About Qt", qApp, &QApplication::aboutQt);
  connect(&session_, &DocumentSession::changed, this, &MainWindow::refresh);
  connect(&session_, &DocumentSession::selectionChanged, this, [this] {
    ++aiSelection_;
    refreshSelection();
  });
  connect(&session_, &DocumentSession::modifiedChanged, this,
          [this] { updateTitle(); });
  connect(&preview_, &PreviewController::backendResolved, this,
          [this](const QString &backend, const QString &device,
                 const QString &fallback) {
            previewBackend_ = backend;
            previewDevice_ = device;
            previewFallback_ = fallback;
            const auto report =
                backend + " · " + device +
                (fallback.isEmpty() ? QString{} : " · Fallback: " + fallback);
            if (report != lastBackendReport_) {
              record("Preview renderer: " + report);
              lastBackendReport_ = report;
            }
          });
  connect(
      &preview_, &PreviewController::imageReady, this,
      [this](const QImage &image, quint64 samples, quint64 plotted,
             quint64 invalid, unsigned workers, qint64 elapsed, bool complete) {
        previewCanvas_->setImage(image);
        aiPreviewCurrent_ = true;
        aiPreviewPng_.clear();
        const auto execution =
            previewBackend_ == "Vulkan"
                ? QStringLiteral("Vulkan FP32")
                : QString("%1 CPU workers%2")
                      .arg(workers)
                      .arg(previewFallback_.isEmpty() ? "" : " (fallback)");
        stats_->setText(QString("%1 samples  ·  %2  ·  %3 ms%4")
                            .arg(QLocale().toString(samples))
                            .arg(execution)
                            .arg(elapsed)
                            .arg(complete ? "  ·  Refined" : ""));
        stats_->setToolTip(
            stats_->text() + "\n" +
            QString("%1 plotted samples\n%2 invalid orbit steps\nDevice: %3%4")
                .arg(plotted)
                .arg(invalid)
                .arg(previewDevice_)
                .arg(previewFallback_.isEmpty()
                         ? QString{}
                         : "\nFallback: " + previewFallback_));
        if (auto item = library_->item(session_.selectedFlame()))
          item->setIcon(QPixmap::fromImage(image.scaled(
              72, 54, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
      });
  connect(
      &preview_, &PreviewController::statusChanged, this,
      [this](const QString &message) { statusBar()->showMessage(message); });
  connect(&preview_, &PreviewController::progressChanged, progress_,
          &QProgressBar::setValue);
  connect(&preview_, &PreviewController::busyChanged, progress_,
          &QWidget::setVisible);
  connect(&preview_, &PreviewController::failed, this,
          [this](const QString &error) {
            record(error);
            statusBar()->showMessage(error);
          });
  connect(queue_.get(), &RenderQueue::problem, this,
          [this](const QString &error) {
            record(error);
            exportStatus_->setText(error);
            exportStatus_->setToolTip(error);
          });
  connect(queue_.get(), &RenderQueue::changed, this, [this] {
    exportProgress_->setVisible(queue_->isBusy());
    if (const auto job = queue_->job(queue_->activeId())) {
      exportStatus_->setText(job->name + " · " + renderJobStateText(*job));
      exportProgress_->setValue(job->progress);
      exportStatus_->setToolTip(job->destination);
    }
  });
  connect(queue_.get(), &RenderQueue::jobFinished, this,
          [this](const QString &id, bool success) {
            const auto job = queue_->job(id);
            if (!job)
              return;
            if (success) {
              record("Rendered image " + job->destination + " · " +
                     job->backend + " · " + job->device +
                     (job->fallback.isEmpty()
                          ? QString{}
                          : " · Fallback: " + job->fallback));
              exportStatus_->setText("Saved " +
                                     QFileInfo(job->destination).fileName() +
                                     " · " + job->backend);
            } else {
              record(job->name + " · " + renderJobStateText(*job) +
                     (job->error.isEmpty() ? QString{} : " · " + job->error));
              exportStatus_->setText(renderJobStateText(*job));
            }
            exportStatus_->setToolTip(job->device + "\n" + job->fallback +
                                      "\n" + job->error);
          });
  connect(previewCanvas_, &PreviewCanvas::viewportChanged, &preview_,
          &PreviewController::setViewport);
  auto timer = new QTimer(this);
  timer->setObjectName("recoveryTimer");
  timer->setInterval(45000);
  connect(&session_, &DocumentSession::recoverySaved, this,
          [this] { record("Recovery copy updated"); });
  connect(&session_, &DocumentSession::recoveryFailed, this,
          [this](const QString &message) { record(message); });
  connect(timer, &QTimer::timeout, this, [this] {
    try {
      requestRecovery();
    } catch (const std::exception &e) {
      record(text(e.what()));
    }
  });
  timer->start();
  if (useSettings_) {
    restoreGeometry(settings_.value("window/geometry").toByteArray());
    restoreState(settings_.value("window/layout").toByteArray(), 1);
    auto configuration = preview_.settings();
    configuration.threads = settings_.value("preview/threads", 0).toUInt();
    configuration.memory_mib =
        settings_.value("preview/memory", 256).toULongLong();
    configuration.seed = settings_.value("preview/seed", 123456).toULongLong();
    configuration.sample_limit =
        settings_.value("preview/samples", 2000000).toULongLong();
    configuration.backend = static_cast<RenderBackend>(
        settings_.value("render/backend", int(RenderBackend::automatic))
            .toInt());
    configuration.device =
        settings_.value("render/device").toString().toStdString();
    configuration.gpu_memory_mib =
        settings_.value("render/gpuMemory", 512).toULongLong();
    configuration.allow_cpu_fallback =
        settings_.value("render/cpuFallback", true).toBool();
    try {
      preview_.setSettings(configuration);
    } catch (const std::exception &) {
      preview_.setSettings(PreviewSettings{});
    }
    const QSignalBlocker qualityBlock(quality_);
    int preset =
        quality_->findData(qulonglong(preview_.settings().sample_limit));
    if (preset < 0) {
      quality_->addItem("Custom", qulonglong(preview_.settings().sample_limit));
      preset = quality_->count() - 1;
    }
    quality_->setCurrentIndex(preset);
  }
  if (useSettings_ && !queue_->stateFile().isEmpty() &&
      QFileInfo::exists(queue_->stateFile())) {
    try {
      queue_->loadFrom(queue_->stateFile());
      if (!queue_->jobs().empty())
        record("Restored render queue. Start / resume continues saved jobs.");
    } catch (const std::exception &error) {
      queue_->disableAutoSave();
      queueWarning =
          "Could not restore the saved render queue: " + text(error.what()) +
          ". The file was retained; save this session's queue to a new file.";
    }
  }
  if (!queueWarning.isEmpty()) {
    record(queueWarning);
    exportStatus_->setText(queueWarning);
    exportStatus_->setToolTip(queueWarning);
  }
  refresh();
}

MainWindow::~MainWindow() {
  localAi_->stop();
  for (auto child : findChildren<QObject *>())
    QObject::disconnect(child, nullptr, this, nullptr);
  preview_.stop();
  queue_->pauseAndWait();
}
void MainWindow::makeActions() {
  auto file = menuBar()->addMenu("&File");
  auto create = file->addAction(style()->standardIcon(QStyle::SP_FileIcon),
                                "&New flame", this, &MainWindow::newDocument);
  create->setShortcut(QKeySequence::New);
  create->setObjectName("actionNew");
  auto open = file->addAction(style()->standardIcon(QStyle::SP_DirOpenIcon),
                              "&Open project or library…", this,
                              &MainWindow::chooseOpen);
  open->setShortcut(QKeySequence::Open);
  open->setObjectName("actionOpen");
  file->addAction("Open starter library", this, [this] {
    run([this] {
      if (openStarterLibrary()) {
        libraryDock_->show();
        libraryDock_->raise();
      }
    });
  })->setObjectName("actionOpenStarterLibrary");
  auto recent = file->addMenu("Recent files");
  connect(recent, &QMenu::aboutToShow, this, [this, recent] {
    recent->clear();
    for (const auto &path : settings_.value("files/recent").toStringList())
      recent->addAction(path, this, [this, path] { openFile(path); });
    if (recent->isEmpty())
      recent->addAction("No recent files")->setEnabled(false);
  });
  save_ = file->addAction(style()->standardIcon(QStyle::SP_DialogSaveButton),
                          "&Save", this, [this] { saveDocument(); });
  save_->setShortcut(QKeySequence::Save);
  save_->setObjectName("actionSave");
  auto saveAs =
      file->addAction("Save &as…", this, [this] { saveDocument(true); });
  saveAs->setShortcut(QKeySequence::SaveAs);
  saveAs->setObjectName("actionSaveAs");
  file->addSeparator();
  file->addAction("Export flame library…", this, &MainWindow::exportFlameLibrary)
      ->setObjectName("actionExportLibrary");
  auto standalone = file->addAction("Export selected flame…", this, [this] {
    run([&] {
      if (!model_)
        throw std::runtime_error("Select a renderable flame to export");
      const auto path = QFileDialog::getSaveFileName(
          this, "Export standalone flame", {}, "Flame files (*.flame)");
      if (path.isEmpty())
        return;
      if (!session_.path().isEmpty() &&
          (QFileInfo(path).absoluteFilePath() == session_.path() ||
           (QFileInfo(path).exists() &&
            std::filesystem::equivalent(filesystemPath(path),
                                        filesystemPath(session_.path())))))
        throw std::runtime_error(
            "Choose a different file for a standalone export");
      const auto bytes = serialize_flame(*model_);
      write_atomic(filesystemPath(path),
                   [&](std::ostream &out) { out << bytes; });
      record("Exported selected flame " + path);
    });
  });
  standalone->setToolTip("Creates a standalone supported model. Comments, "
                         "history and collection metadata are omitted.");
  file->addAction("Export Apophysis UPR…", this, &MainWindow::exportUpr)
      ->setObjectName("actionExportUpr");
  render_ = file->addAction(style()->standardIcon(QStyle::SP_DialogApplyButton),
                            "&Render image…", this, &MainWindow::renderImage);
  render_->setShortcut(QKeySequence("Ctrl+R"));
  render_->setObjectName("actionRender");
  queueAction_ =
      file->addAction("Render &queue…", this, &MainWindow::showRenderQueue);
  queueAction_->setObjectName("actionRenderQueue");
  file->addSeparator();
  file->addAction("E&xit", this, &QWidget::close)
      ->setShortcut(QKeySequence::Quit);
  auto edit = menuBar()->addMenu("&Edit");
  auto undo = session_.undoStack().createUndoAction(this, "&Undo");
  undo->setShortcut(QKeySequence::Undo);
  undo->setObjectName("actionUndo");
  auto redo = session_.undoStack().createRedoAction(this, "&Redo");
  redo->setShortcut(QKeySequence::Redo);
  redo->setObjectName("actionRedo");
  edit->addAction(undo);
  edit->addAction(redo);
  edit->addSeparator();
  copy_ = edit->addAction("Copy transform", this, [this] {
    if (model_) {
      copiedTransform_ = selectedTransform();
      paste_->setEnabled(true);
      record("Transform copied");
    }
  });
  copy_->setShortcut(QKeySequence("Ctrl+Shift+C"));
  paste_ = edit->addAction("Paste transform", this, [this] {
    run([&] {
      if (!copiedTransform_ || !model_)
        return;
      auto value = *copiedTransform_;
      value.chaos.clear();
      session_.change(
          "Paste transform",
          [&](auto &doc) {
            doc.add_transform(session_.selectedFlame(), value);
          },
          int(model_->transforms.size()), false);
    });
  });
  paste_->setShortcut(QKeySequence("Ctrl+Shift+V"));
  edit->addSeparator();
  edit->addAction("Preferences…", this, &MainWindow::preferences)
      ->setObjectName("actionPreferences");
  auto tools = menuBar()->addMenu("&Tools");
  tools->addAction("Local AI connection…", this, &MainWindow::showLocalAiSettings)
      ->setObjectName("actionLocalAiConnection");
  animation_ = tools->addAction("Animation timeline…", this,
                               &MainWindow::showAnimationTimeline);
  animation_->setObjectName("actionAnimation");
  tools->addAction("Compose image sequences…", this, [this] { showSequenceComposer(this); });
  animation_->setShortcut(QKeySequence("Ctrl+T"));
  auto script =
      tools->addAction("Script editor…", this, &MainWindow::showScriptEditor);
  script->setObjectName("actionScriptEditor");
  script->setShortcut(QKeySequence("Ctrl+E"));
  tools->addAction("Plugins…", this, &MainWindow::showPluginManager)
      ->setObjectName("actionPlugins");
  auto transform = menuBar()->addMenu("&Transform");
  add_ = transform->addAction("Add transform", this, [this] {
    run([&] {
      if (!model_)
        return;
      Transform value;
      value.pre = {.5, 0, 0, .5, 0, 0};
      value.color = .5;
      session_.change(
          "Add transform",
          [&](auto &doc) {
            doc.add_transform(session_.selectedFlame(), value);
          },
          int(model_->transforms.size()), false);
    });
  });
  add_->setObjectName("actionAddTransform");
  duplicate_ = transform->addAction("Duplicate transform", this, [this] {
    run([&] {
      session_.change(
          "Duplicate transform",
          [&](auto &doc) {
            doc.duplicate_transform(session_.selectedFlame(),
                                    session_.selectedTransform());
          },
          int(model_->transforms.size()), false);
    });
  });
  duplicate_->setShortcut(QKeySequence("Ctrl+D"));
  duplicate_->setObjectName("actionDuplicateTransform");
  remove_ = transform->addAction("Delete transform", this, [this] {
    run([&] {
      const int next = session_.isFinalTransform()
                           ? 0
                           : std::max(0, session_.selectedTransform() - 1);
      session_.change(
          "Delete transform",
          [&](auto &doc) {
            doc.remove_transform(session_.selectedFlame(),
                                 session_.selectedTransform(),
                                 session_.isFinalTransform());
          },
          next, false);
    });
  });
  remove_->setObjectName("actionDeleteTransform");
  moveUp_ = transform->addAction("Move transform up", this, [this] {
    run([&] {
      const auto i = session_.selectedTransform();
      session_.change(
          "Reorder transform",
          [&](auto &doc) {
            doc.move_transform(session_.selectedFlame(), i, i - 1);
          },
          i - 1, false);
    });
  });
  moveDown_ = transform->addAction("Move transform down", this, [this] {
    run([&] {
      const auto i = session_.selectedTransform();
      session_.change(
          "Reorder transform",
          [&](auto &doc) {
            doc.move_transform(session_.selectedFlame(), i, i + 1);
          },
          i + 1, false);
    });
  });
  addFinal_ = transform->addAction("Add final transform", this, [this] {
    run([&] {
      session_.change(
          "Add final transform",
          [&](auto &doc) {
            doc.add_transform(session_.selectedFlame(), Transform{}, true);
          },
          0, true);
    });
  });
  addFinal_->setObjectName("actionAddFinal");
  auto flame = menuBar()->addMenu("&Flame");
  saveFavorite_ = flame->addAction("Save to Favorites…", this, [this] {
    run([this] {
      QImage thumbnail;
      if (aiPreviewCurrent_)
        thumbnail = previewCanvas_->image();
      else if (const auto *item = library_->item(session_.selectedFlame()); item && !item->icon().isNull())
        thumbnail = item->icon().pixmap(144, 96).toImage();
      saveSelectedFavorite(session_, this, thumbnail, favoriteStorage());
    });
  });
  saveFavorite_->setObjectName("actionSaveFavorite");
  saveFavorite_->setToolTip("Keep an editable copy of the selected flame in your personal favorites");
  browseFavorites_ = flame->addAction("Favorites…", this, [this] {
    run([this] { browseFlameFavorites(session_, this, favoriteStorage()); });
  });
  browseFavorites_->setObjectName("actionBrowseFavorites");
  browseFavorites_->setToolTip("Find a favorite and add a copy to this project");
  flame->addSeparator();
  duplicateFlame_ = flame->addAction("Duplicate selected flame", this, [this] {
    run([&] {
      if (!session_.hasDocument())
        return;
      const auto index = std::size_t(session_.selectedFlame());
      const auto next = int(session_.document().entries().size());
      const auto name = session_.document().entries()[index].name + " copy";
      session_.change(
          "Duplicate flame",
          [&](auto &doc) { doc.duplicate_flame(index, name); }, 0, false, next);
    });
  });
  duplicateFlame_->setObjectName("actionDuplicateFlame");
  duplicateFlame_->setShortcut(QKeySequence("Ctrl+Shift+D"));
  deleteFlame_ = flame->addAction("Delete selected flame", this, [this] {
    run([&] {
      if (!session_.hasDocument() || session_.document().entries().size() < 2)
        return;
      const auto index = session_.selectedFlame();
      const int next =
          std::min(index, int(session_.document().entries().size()) - 2);
      session_.change(
          "Delete flame",
          [&](auto &doc) { doc.remove_flame(std::size_t(index)); }, 0, false,
          next);
    });
  });
  deleteFlame_->setObjectName("actionDeleteFlame");
  flame->addSeparator();
  gradient_ =
      flame->addAction("Gradient editor…", this, &MainWindow::editGradient);
  gradient_->setObjectName("actionGradient");
  gradient_->setShortcut(QKeySequence("Ctrl+G"));
  adjustments_ = flame->addAction("Adjustment editor…", this,
                                  &MainWindow::editAdjustments);
  adjustments_->setObjectName("actionAdjustments");
  adjustments_->setShortcut(QKeySequence("Ctrl+J"));
  variationExplorer_ = flame->addAction("Variation explorer…", this, &MainWindow::exploreVariations);
  variationExplorer_->setObjectName("actionVariationExplorer");
  mutations_ = flame->addAction("Mutation explorer…", this,
                                [this] { exploreFlames(true); });
  mutations_->setObjectName("actionMutations");
  mutations_->setShortcut(QKeySequence("Ctrl+M"));
  randomFlames_ = flame->addAction("Random flame generator…", this,
                                   [this] { exploreFlames(false); });
  randomFlames_->setObjectName("actionRandomFlames");
  randomFlames_->setShortcut(QKeySequence("Ctrl+Shift+N"));
  auto toolbar = addToolBar("Main");
  toolbar->setObjectName("mainToolbar");
  toolbar->setMovable(false);
  toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  toolbar->addAction(open);
  toolbar->addAction(save_);
  toolbar->addSeparator();
  auto toolbarUndo =
      toolbar->addAction("Undo", &session_.undoStack(), &QUndoStack::undo);
  auto toolbarRedo =
      toolbar->addAction("Redo", &session_.undoStack(), &QUndoStack::redo);
  toolbarUndo->setEnabled(false);
  toolbarRedo->setEnabled(false);
  connect(&session_.undoStack(), &QUndoStack::canUndoChanged, toolbarUndo,
          &QAction::setEnabled);
  connect(&session_.undoStack(), &QUndoStack::canRedoChanged, toolbarRedo,
          &QAction::setEnabled);
  connect(undo, &QAction::changed, toolbarUndo,
          [undo, toolbarUndo] { toolbarUndo->setToolTip(undo->text()); });
  connect(redo, &QAction::changed, toolbarRedo,
          [redo, toolbarRedo] { toolbarRedo->setToolTip(redo->text()); });
  toolbar->addSeparator();
  toolbar->addAction(render_);
  auto queueShortcut =
      toolbar->addAction("Queue", queueAction_, &QAction::trigger);
  queueShortcut->setObjectName("toolbarRenderQueue");
  toolbar->addSeparator();
  pause_ = toolbar->addAction(style()->standardIcon(QStyle::SP_MediaPause),
                              "Pause preview");
  pause_->setCheckable(true);
  pause_->setObjectName("actionPause");
  connect(pause_, &QAction::toggled, &preview_, &PreviewController::setPaused);
  toolbar->addAction(style()->standardIcon(QStyle::SP_BrowserReload), "Restart",
                     this, [this] {
                       pause_->setChecked(false);
                       preview_.restart();
                     });
  auto creativeTools = addToolBar("Creative tools");
  creativeTools->setObjectName("creativeToolbar");
  creativeTools->setToolButtonStyle(Qt::ToolButtonTextOnly);
  for (const auto &[label, action] :
       std::array<std::pair<QString, QAction *>, 5>{
           {{"Gradient", gradient_},
            {"Adjust", adjustments_},
            {"Mutate", mutations_},
            {"Random", randomFlames_},
            {"Timeline", animation_}}}) {
    auto shortcut = creativeTools->addAction(label, action, &QAction::trigger);
    connect(action, &QAction::changed, shortcut,
            [action, shortcut] { shortcut->setEnabled(action->isEnabled()); });
  }
}

void MainWindow::makeLibrary() {
  libraryDock_ = new QDockWidget("Flame library", this);
  libraryDock_->setObjectName("libraryDock");
  auto body = new QWidget;
  auto layout = new QVBoxLayout(body);
  layout->setContentsMargins(9, 9, 9, 9);
  librarySearch_ = new QLineEdit;
  librarySearch_->setObjectName("librarySearch");
  librarySearch_->setPlaceholderText("Find a flame…");
  librarySearch_->setClearButtonEnabled(true);
  layout->addWidget(librarySearch_);
  auto favoritesRow = new QHBoxLayout;
  favoritesRow->setSpacing(2);
  auto saveFavoriteButton = new QToolButton;
  saveFavoriteButton->setToolTip(saveFavorite_->toolTip());
  connect(saveFavoriteButton, &QToolButton::clicked, saveFavorite_, &QAction::trigger);
  connect(saveFavorite_, &QAction::changed, saveFavoriteButton, [this, saveFavoriteButton] {
    saveFavoriteButton->setEnabled(saveFavorite_->isEnabled());
  });
  saveFavoriteButton->setText(QStringLiteral("☆"));
  saveFavoriteButton->setAccessibleName("Save to Favorites");
  saveFavoriteButton->setObjectName("saveFavoriteButton");
  auto favoritesButton = new QToolButton;
  favoritesButton->setDefaultAction(browseFavorites_);
  favoritesButton->setObjectName("browseFavoritesButton");
  favoritesButton->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
  favoritesRow->addWidget(saveFavoriteButton);
  favoritesRow->addWidget(favoritesButton, 1);
  layout->addLayout(favoritesRow);
  library_ = new FlameLibraryList;
  library_->setItemDelegate(new FlameLibraryDelegate(library_));
  library_->setWordWrap(true);
  library_->setResizeMode(QListView::Adjust);
  library_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  library_->setObjectName("flameLibrary");
  library_->setIconSize({64, 48});
  layout->addWidget(library_);
  connect(librarySearch_, &QLineEdit::textChanged, this,
          &MainWindow::filterLibrary);
  connect(library_, &QListWidget::currentRowChanged, this, [this](int row) {
    if (!refreshing_)
      session_.selectFlame(row);
  });
  libraryDock_->setWidget(body);
  addDockWidget(Qt::LeftDockWidgetArea, libraryDock_);
}

void MainWindow::makePreview() {
  auto body = new QWidget;
  auto layout = new QVBoxLayout(body);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  auto controls = new QHBoxLayout;
  controls->setContentsMargins(12, 7, 12, 7);
  auto label = new QLabel("Live preview");
  auto font = label->font();
  font.setBold(true);
  label->setFont(font);
  controls->addWidget(label);
  controls->addStretch();
  quality_ = new QComboBox;
  quality_->setObjectName("previewQuality");
  quality_->addItem("Draft · 400k", qulonglong(400000));
  quality_->addItem("Standard · 2M", qulonglong(2000000));
  quality_->addItem("Fine · 8M", qulonglong(8000000));
  quality_->setCurrentIndex(1);
  controls->addWidget(quality_);
  auto fit = new QPushButton("Fit");
  fit->setObjectName("previewFit");
  controls->addWidget(fit);
  auto actual = new QPushButton("1:1");
  controls->addWidget(actual);
  layout->addLayout(controls);
  previewCanvas_ = new PreviewCanvas;
  layout->addWidget(previewCanvas_, 1);
  connect(fit, &QPushButton::clicked, previewCanvas_, &PreviewCanvas::fit);
  connect(actual, &QPushButton::clicked, previewCanvas_,
          &PreviewCanvas::actualPixels);
  connect(quality_, &QComboBox::currentIndexChanged, this, [this] {
    run([&] {
      auto s = preview_.settings();
      s.sample_limit = quality_->currentData().toULongLong();
      preview_.setSettings(s);
    });
  });
  auto footer = new QHBoxLayout;
  footer->setContentsMargins(12, 6, 12, 6);
  stats_ = new QLabel("CPU preview");
  stats_->setObjectName("previewStatistics");
  stats_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  footer->addWidget(stats_, 1);
  progress_ = new QProgressBar;
  progress_->setRange(0, 100);
  progress_->setFixedWidth(110);
  auto reserve = progress_->sizePolicy();
  reserve.setRetainSizeWhenHidden(true);
  progress_->setSizePolicy(reserve);
  progress_->setVisible(false);
  footer->addWidget(progress_);
  layout->addLayout(footer);
  setCentralWidget(body);
}

void MainWindow::makeTransformEditor() {
  transformDock_ = new QDockWidget("Transform editor", this);
  transformDock_->setObjectName("transformDock");
  auto body = new QWidget;
  auto layout = new QVBoxLayout(body);
  layout->setContentsMargins(8, 8, 8, 6);
  layout->setSpacing(6);
  auto tools = new QHBoxLayout;
  transforms_ = new QComboBox;
  transforms_->setObjectName("transformSelector");
  transforms_->setMinimumWidth(130);
  tools->addWidget(transforms_);
  matrixMode_ = new QComboBox;
  matrixMode_->setObjectName("matrixMode");
  matrixMode_->addItems({"Pre-affine", "Post-affine"});
  tools->addWidget(matrixMode_);
  auto add = new QPushButton("Add");
  auto duplicate = new QPushButton("Duplicate");
  auto remove = new QPushButton("Delete");
  auto buttons = dockControlRow(layout, tools);
  buttons->addWidget(add);
  buttons->addWidget(duplicate);
  buttons->addWidget(remove);
  buttons->addStretch();
  connect(add, &QPushButton::clicked, add_, &QAction::trigger);
  connect(duplicate, &QPushButton::clicked, duplicate_, &QAction::trigger);
  connect(remove, &QPushButton::clicked, remove_, &QAction::trigger);
  connect(add_, &QAction::changed, add,
          [this, add] { add->setEnabled(add_->isEnabled()); });
  connect(duplicate_, &QAction::changed, duplicate, [this, duplicate] {
    duplicate->setEnabled(duplicate_->isEnabled());
  });
  connect(remove_, &QAction::changed, remove,
          [this, remove] { remove->setEnabled(remove_->isEnabled()); });
  lock_ = new QCheckBox("Lock");
  lock_->setToolTip("Lock the selected transform while inspecting it");
  buttons->addWidget(lock_);
  triangles_ = new TriangleCanvas;
  layout->addWidget(triangles_, 1);
  auto footer = new QHBoxLayout;
  grid_ = new QCheckBox("Grid");
  grid_->setChecked(true);
  snap_ = new QCheckBox("Snap");
  footer->addWidget(grid_);
  footer->addWidget(snap_);
  auto spacing = new QDoubleSpinBox;
  spacing->setObjectName("gridSpacing");
  spacing->setDecimals(4);
  spacing->setRange(.0001, 100000);
  spacing->setValue(.25);
  spacing->setSingleStep(.05);
  spacing->setPrefix("Grid ");
  footer->addWidget(spacing);
  auto fit = new QPushButton("Fit triangles");
  auto viewTools = dockControlRow(layout, footer);
  viewTools->addWidget(fit);
  viewTools->addStretch();
  coordinatesLabel_ = new QLabel("x 0.0000    y 0.0000");
  viewTools->addWidget(coordinatesLabel_);
  connect(fit, &QPushButton::clicked, triangles_, &TriangleCanvas::fit);
  connect(grid_, &QCheckBox::toggled, triangles_, &TriangleCanvas::setGrid);
  connect(snap_, &QCheckBox::toggled, triangles_, &TriangleCanvas::setSnap);
  connect(spacing, &QDoubleSpinBox::valueChanged, triangles_,
          &TriangleCanvas::setGridSpacing);
  connect(lock_, &QCheckBox::toggled, this, [this](bool locked) {
    triangles_->setLocked(locked);
    properties_->setEnabled(model_.has_value() && !locked);
    refreshTransform();
  });
  connect(transforms_, &QComboBox::currentIndexChanged, this, [this](int row) {
    if (refreshing_ || !model_ || row < 0)
      return;
    const bool final = row == int(model_->transforms.size());
    session_.selectTransform(final ? 0 : row, final);
  });
  connect(matrixMode_, &QComboBox::currentIndexChanged, this,
          [this](int index) {
            triangles_->setPost(index == 1);
            refreshTransform();
          });
  connect(triangles_, &TriangleCanvas::selected, &session_,
          &DocumentSession::selectTransform);
  connect(triangles_, &TriangleCanvas::coordinateChanged, this,
          [this](double x, double y) {
            coordinatesLabel_->setText(
                QString("x %1    y %2").arg(x, 0, 'f', 4).arg(y, 0, 'f', 4));
          });
  connect(triangles_, &TriangleCanvas::affinePreview, this,
          [this](const Affine &affine) {
            if (!model_)
              return;
            setCoefficients(affine);
            auto draft = *model_;
            auto &transform =
                session_.isFinalTransform()
                    ? draft.final_transform.value()
                    : draft.transforms.at(session_.selectedTransform());
            (matrixMode_->currentIndex() == 1 ? transform.post
                                              : transform.pre) = affine;
            preview_.request(draft, previewCanvas_->size());
          });
  connect(triangles_, &TriangleCanvas::affineCommitted, this,
          &MainWindow::commitAffine);
  connect(triangles_, &TriangleCanvas::editCanceled, this, [this] {
    refreshTransform();
    requestPreview();
  });
  // If the tools wrap in a short dock, keep the canvas and controls reachable
  // without forcing the whole main window to grow taller.
  transformDock_->setWidget(scrollPanel(body));
  addDockWidget(Qt::BottomDockWidgetArea, transformDock_);
}

void MainWindow::makeProperties() {
  propertiesDock_ = new QDockWidget("Flame and transform", this);
  propertiesDock_->setObjectName("propertiesDock");
  auto body = new QWidget;
  auto layout = new QVBoxLayout(body);
  layout->setContentsMargins(10, 9, 10, 9);
  layout->setSpacing(8);
  name_ = new QLineEdit;
  name_->setObjectName("flameName");
  name_->setPlaceholderText("Flame name");
  layout->addWidget(name_);
  connect(name_, &QLineEdit::editingFinished, this, [this] {
    if (refreshing_ || !session_.hasDocument())
      return;
    run([&] {
      LegacyEdits edits;
      edits.name = name_->text().toUtf8().toStdString();
      session_.change("Rename flame", [&](auto &doc) {
        doc.edit(session_.selectedFlame(), edits);
      });
    });
  });
  producer_ = new QLabel;
  producer_->setWordWrap(true);
  producer_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  layout->addWidget(producer_);
  diagnostic_ = new QLabel;
  diagnostic_->setObjectName("renderDiagnostic");
  diagnostic_->setWordWrap(true);
  diagnostic_->setStyleSheet("color:#e8b783; padding:7px; background:#382f2a;");
  layout->addWidget(diagnostic_);
  auto camera = new QGroupBox("Output and camera");
  auto cameraLayout = new QGridLayout(camera);
  width_ = new QSpinBox;
  height_ = new QSpinBox;
  width_->setObjectName("outputWidth");
  height_->setObjectName("outputHeight");
  for (auto field : {width_, height_}) {
    field->setRange(1, 2147483647);
    field->setKeyboardTracking(false);
  }
  cameraLayout->addWidget(new QLabel("Size"), 0, 0);
  cameraLayout->addWidget(width_, 0, 1);
  cameraLayout->addWidget(height_, 0, 2);
  centerX_ = numeric("centerX", 0);
  centerY_ = numeric("centerY", 0);
  scale_ = numeric("cameraScale", 50);
  cameraLayout->addWidget(new QLabel("Center"), 1, 0);
  cameraLayout->addWidget(centerX_, 1, 1);
  cameraLayout->addWidget(centerY_, 1, 2);
  cameraLayout->addWidget(new QLabel("Scale"), 2, 0);
  cameraLayout->addWidget(scale_, 2, 1, 1, 2);
  layout->addWidget(camera);
  auto sizeEdited = [this] {
    if (refreshing_ || !session_.hasDocument())
      return;
    run([&] {
      LegacyEdits e;
      e.size = std::array{width_->value(), height_->value()};
      session_.change("Resize output",
                      [&](auto &d) { d.edit(session_.selectedFlame(), e); });
    });
  };
  connect(width_, &QSpinBox::editingFinished, this, sizeEdited);
  connect(height_, &QSpinBox::editingFinished, this, sizeEdited);
  auto centerEdited = [this] {
    if (refreshing_)
      return;
    run([&] {
      LegacyEdits e;
      e.center = Point{readNumber(centerX_), readNumber(centerY_)};
      session_.change("Move camera",
                      [&](auto &d) { d.edit(session_.selectedFlame(), e); });
    });
  };
  numberEdited(centerX_, this, centerEdited);
  numberEdited(centerY_, this, centerEdited);
  numberEdited(scale_, this, [this] {
    run([&] {
      LegacyEdits e;
      e.scale = readNumber(scale_);
      session_.change("Change camera scale",
                      [&](auto &d) { d.edit(session_.selectedFlame(), e); });
    });
  });
  properties_ = new QTabWidget;
  properties_->setObjectName("transformTabs");
  layout->addWidget(properties_, 1);

  auto geometry = new QWidget;
  auto geometryLayout = new QVBoxLayout(geometry);
  auto matrix = new QGroupBox("Affine coefficients");
  auto matrixLayout = new QGridLayout(matrix);
  static const char *names[]{"a", "b", "c", "d", "e", "f"};
  for (int i = 0; i < 6; ++i) {
    coefficients_[i] = numeric(QString("coefficient_%1").arg(names[i]),
                               i == 0 || i == 3 ? 1 : 0);
    const int column = i / 2, row = i % 2;
    auto cell = new QVBoxLayout;
    cell->setSpacing(2);
    cell->addWidget(new QLabel(names[i]));
    cell->addWidget(coefficients_[i]);
    matrixLayout->addLayout(cell, row, column);
    numberEdited(coefficients_[i], this, [this] {
      run([&] {
        commitAffine(
            {readNumber(coefficients_[0]), readNumber(coefficients_[1]),
             readNumber(coefficients_[2]), readNumber(coefficients_[3]),
             readNumber(coefficients_[4]), readNumber(coefficients_[5])});
      });
    });
  }
  matrix->setToolTip("x′ = a·x + c·y + e\ny′ = b·x + d·y + f");
  geometryLayout->addWidget(matrix);
  weight_ = numeric("transformWeight", 1);
  auto weightLayout = new QFormLayout;
  weightLayout->addRow("Selection weight", weight_);
  geometryLayout->addLayout(weightLayout);
  numberEdited(weight_, this, [this] {
    run([&] {
      LegacyTransformEdits e;
      e.weight = readNumber(weight_);
      editTransform("Change transform weight", e);
    });
  });
  auto operations = new QGroupBox("Geometry");
  auto op = new QGridLayout(operations);
  coordinates_ = new QComboBox;
  coordinates_->addItems({"World coordinates", "Local coordinates"});
  op->addWidget(coordinates_, 0, 0, 1, 3);
  pivotX_ = numeric("pivotX", 0);
  pivotY_ = numeric("pivotY", 0);
  op->addWidget(new QLabel("Pivot"), 1, 0);
  op->addWidget(pivotX_, 1, 1);
  op->addWidget(pivotY_, 1, 2);
  auto origin = new QPushButton("Pivot at triangle origin");
  op->addWidget(origin, 2, 0, 1, 3);
  connect(origin, &QPushButton::clicked, this, [this] {
    if (!model_)
      return;
    const auto &t = selectedTransform();
    const auto &a = matrixMode_->currentIndex() == 1 ? t.post : t.pre;
    setNumber(pivotX_, coordinates_->currentIndex() == 1 ? 0 : a.e);
    setNumber(pivotY_, coordinates_->currentIndex() == 1 ? 0 : a.f);
  });
  rotation_ = numeric("rotationDegrees", 15);
  auto rotate = new QPushButton("Rotate");
  op->addWidget(rotation_, 3, 0, 1, 2);
  op->addWidget(rotate, 3, 2);
  connect(rotate, &QPushButton::clicked, this, [this] {
    run([&] {
      const double angle = readNumber(rotation_) * std::numbers::pi / 180;
      const double c = std::cos(angle), s = std::sin(angle);
      applyGeometry("Rotate transform", {c, s, -s, c, 0, 0});
    });
  });
  factor_ = numeric("scaleFactor", 1.1);
  auto scale = new QPushButton("Scale");
  op->addWidget(factor_, 4, 0, 1, 2);
  op->addWidget(scale, 4, 2);
  connect(scale, &QPushButton::clicked, this, [this] {
    run([&] {
      const double factor = readNumber(factor_);
      applyGeometry("Scale transform", {factor, 0, 0, factor, 0, 0});
    });
  });
  translateX_ = numeric("translateX", .1);
  translateY_ = numeric("translateY", 0);
  auto move = new QPushButton("Move");
  op->addWidget(translateX_, 5, 0);
  op->addWidget(translateY_, 5, 1);
  op->addWidget(move, 5, 2);
  connect(move, &QPushButton::clicked, this, [this] {
    run([&] {
      applyGeometry("Move transform", {1, 0, 0, 1, readNumber(translateX_),
                                       readNumber(translateY_)});
    });
  });
  auto flipX = new QPushButton("Flip X");
  auto flipY = new QPushButton("Flip Y");
  op->addWidget(flipX, 6, 0);
  op->addWidget(flipY, 6, 1);
  connect(flipX, &QPushButton::clicked, this, [this] {
    run([&] {
      applyGeometry("Flip transform horizontally", {-1, 0, 0, 1, 0, 0});
    });
  });
  connect(flipY, &QPushButton::clicked, this, [this] {
    run([&] {
      applyGeometry("Flip transform vertically", {1, 0, 0, -1, 0, 0});
    });
  });
  geometryLayout->addWidget(operations);
  geometryLayout->addStretch();
  properties_->addTab(scrollPanel(geometry), "Geometry");

  auto variationPanel = new QWidget;
  auto variationLayout = new QVBoxLayout(variationPanel);
  auto search = new QLineEdit;
  search->setPlaceholderText("Find a variation…");
  search->setClearButtonEnabled(true);
  variationLayout->addWidget(search);
  variations_ = new QTableWidget(int(variation_registry().size()), 2);
  variations_->setObjectName("variationWeights");
  variations_->setHorizontalHeaderLabels({"Variation", "Weight"});
  variations_->verticalHeader()->hide();
  variations_->horizontalHeader()->setSectionResizeMode(0,
                                                        QHeaderView::Stretch);
  variations_->horizontalHeader()->setSectionResizeMode(
      1, QHeaderView::ResizeToContents);
  int row = 0;
  for (const auto &v : variation_registry()) {
    auto item = new QTableWidgetItem(text(v.name));
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    variations_->setItem(row, 0, item);
    variations_->setItem(row, 1, new QTableWidgetItem("0"));
    ++row;
  }
  variationLayout->addWidget(variations_, 1);
  auto explore = new QPushButton("Variation explorer…");
  explore->setObjectName("exploreVariationButton");
  connect(explore, &QPushButton::clicked, variationExplorer_, &QAction::trigger);
  connect(variationExplorer_, &QAction::changed, explore,
          [this, explore] { explore->setEnabled(variationExplorer_->isEnabled()); });
  variationLayout->addWidget(explore);
  auto plugins = new QPushButton("Plugin variations…");
  plugins->setObjectName("editPluginVariations");
  variationLayout->addWidget(plugins);
  connect(plugins, &QPushButton::clicked, this,
          &MainWindow::editPluginVariations);
  connect(search, &QLineEdit::textChanged, this, [this](const QString &query) {
    for (int i = 0; i < variations_->rowCount(); ++i)
      variations_->setRowHidden(i, !variations_->item(i, 0)->text().contains(
                                       query, Qt::CaseInsensitive));
  });
  connect(variations_, &QTableWidget::cellChanged, this,
          [this](int row, int column) {
            if (refreshing_ || column != 1 || !model_)
              return;
            run([&] {
              bool ok = false;
              const double value =
                  QLocale::c().toDouble(variations_->item(row, 1)->text(), &ok);
              if (!ok || !std::isfinite(value))
                throw std::runtime_error(
                    "Variation weight must be a finite number");
              auto terms = selectedTransform().variations;
              const auto id = variation_registry()[row].id;
              auto term =
                  std::find_if(terms.begin(), terms.end(),
                               [&](const Term &t) { return t.id == id; });
              if (term != terms.end())
                term->weight = value;
              else if (value != 0)
                terms.push_back({id, value});
              LegacyTransformEdits edit;
              edit.variations = std::move(terms);
              edit.initialize_new_variations = true;
              editTransform("Change variation weight", edit);
            });
          });
  variationForm_ = new QFormLayout;
  for (const auto &field : variation_parameter_registry()) {
    const auto key = text(field.name);
    const auto objectName = field.name == "julian_power" ? QString("julianPower") :
                            field.name == "julian_dist" ? QString("julianDistance") : key;
    auto input = numeric(objectName, field.get(VariationParameters{}));
    variationFields_[field.name] = input;
    variationForm_->addRow(key, input);
    variationForm_->setRowVisible(input, false);
    numberEdited(input, this, [this, input, name = field.name] {
      run([&] {
        LegacyTransformEdits edits;
        edits.variation_parameters.emplace(name, readNumber(input));
        editTransform("Change " + text(name), edits);
      });
    });
  }
  variationLayout->addLayout(variationForm_);
  connect(variations_, &QTableWidget::currentCellChanged, this,
          [this] { refreshVariationParameters(); });
  properties_->addTab(variationPanel, "Variations");

  auto colors = new QWidget;
  auto colorsLayout = new QFormLayout(colors);
  color_ = numeric("transformColor", 0);
  colorSpeed_ = numeric("colorSpeed", .5);
  opacity_ = numeric("opacity", 1);
  colorsLayout->addRow("Palette coordinate", color_);
  colorsLayout->addRow("Color speed", colorSpeed_);
  colorsLayout->addRow("Opacity", opacity_);
  palette_ = new QLabel;
  palette_->setFixedHeight(26);
  palette_->setScaledContents(true);
  colorsLayout->addRow(palette_);
  color_->setToolTip("Palette position from 0 to 1");
  colorSpeed_->setToolTip(
      "0 keeps the incoming color. 1 adopts this transform's color.");
  opacity_->setToolTip("Visibility from 0 to 1, interpreted using the original "
                       "producer's convention");
  numberEdited(color_, this, [this] {
    run([&] {
      LegacyTransformEdits e;
      e.color = readNumber(color_);
      editTransform("Change transform color", e);
    });
  });
  numberEdited(colorSpeed_, this, [this] {
    run([&] {
      LegacyTransformEdits e;
      e.color_speed = readNumber(colorSpeed_);
      editTransform("Change color speed", e);
    });
  });
  numberEdited(opacity_, this, [this] {
    run([&] {
      LegacyTransformEdits e;
      e.opacity = readNumber(opacity_);
      editTransform("Change opacity", e);
    });
  });
  properties_->addTab(colors, "Color");
  chaos_ = new QTableWidget(0, 2);
  chaos_->setObjectName("transitionWeights");
  chaos_->setHorizontalHeaderLabels({"Destination", "Multiplier"});
  chaos_->verticalHeader()->hide();
  chaos_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  properties_->addTab(chaos_, "Xaos");
  connect(chaos_, &QTableWidget::cellChanged, this, [this](int, int column) {
    if (refreshing_ || column != 1 || !model_)
      return;
    run([&] {
      std::vector<double> values;
      for (int i = 0; i < chaos_->rowCount(); ++i) {
        bool ok = false;
        double v = QLocale::c().toDouble(chaos_->item(i, 1)->text(), &ok);
        if (!ok || !std::isfinite(v) || v < 0)
          throw std::runtime_error(
              "Xaos multipliers must be finite and nonnegative");
        values.push_back(v);
      }
      LegacyTransformEdits e;
      e.chaos = std::move(values);
      editTransform("Change transition weights", e);
    });
  });
  propertiesDock_->setWidget(scrollPanel(body));
  addDockWidget(Qt::RightDockWidgetArea, propertiesDock_);
}

void MainWindow::makeLogs() {
  logDock_ = new QDockWidget("Activity", this);
  logDock_->setObjectName("activityDock");
  auto body = new QWidget;
  auto layout = new QVBoxLayout(body);
  auto row = new QHBoxLayout;
  exportStatus_ = new QLabel("Render to disk is idle");
  exportStatus_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  row->addWidget(exportStatus_, 1);
  exportProgress_ = new QProgressBar;
  exportProgress_->setRange(0, 100);
  exportProgress_->hide();
  row->addWidget(exportProgress_);
  auto cancel = new QPushButton("Cancel render");
  row->addWidget(cancel);
  connect(cancel, &QPushButton::clicked, this,
          [this] { queue_->cancel(queue_->activeId()); });
  connect(queue_.get(), &RenderQueue::busyChanged, cancel,
          &QPushButton::setEnabled);
  cancel->setEnabled(false);
  layout->addLayout(row);
  log_ = new QPlainTextEdit;
  log_->setReadOnly(true);
  log_->setMaximumBlockCount(500);
  layout->addWidget(log_);
  logDock_->setWidget(body);
  addDockWidget(Qt::BottomDockWidgetArea, logDock_);
  tabifyDockWidget(transformDock_, logDock_);
  transformDock_->raise();
}

void MainWindow::refresh() {
  refreshing_ = true;
  const QSignalBlocker block(library_);
  const int count =
      session_.hasDocument() ? int(session_.document().entries().size()) : 0;
  if (library_->count() != count) {
    library_->clear();
    for (int i = 0; i < count; ++i)
      library_->addItem(new QListWidgetItem);
  }
  for (int i = 0; i < count; ++i) {
    const auto &entry = session_.document().entries()[i];
    auto item = library_->item(i);
    item->setText(entry.name.empty() ? QString("Untitled %1").arg(i + 1)
                                     : text(entry.name));
    const auto issue = session_.document().render_issue(i);
    const auto key = issue ? QByteArray{} : thumbnailKey(session_.document().element_bytes(i));
    if (item->data(Qt::UserRole).toByteArray() != key) {
      item->setData(Qt::UserRole, key);
      const auto path = starterThumbnail(key);
      item->setIcon(path.isEmpty() ? QIcon{} : QIcon(path));
    }
    const auto version = entry.version.empty() ? QString{} : "\n" + text(entry.version);
    item->setToolTip(item->text() + version +
                     (issue ? "\nPreserved · " + text(*issue) : "\nPreview available"));
    item->setForeground(issue ? QColor("#d3af89")
                              : palette().color(QPalette::Text));
  }
  library_->setCurrentRow(count ? session_.selectedFlame() : -1);
  refreshing_ = false;
  filterLibrary();
  refreshSelection();
  updateTitle();
}

void MainWindow::refreshSelection() {
  refreshing_ = true;
  model_.reset();
  const bool available = session_.hasDocument();
  save_->setEnabled(available);
  animation_->setEnabled(available);
  duplicateFlame_->setEnabled(available);
  deleteFlame_->setEnabled(available &&
                           session_.document().entries().size() > 1);
  name_->setEnabled(available);
  for (auto field : {centerX_, centerY_, scale_})
    field->setEnabled(available);
  width_->setEnabled(available);
  height_->setEnabled(available);
  const QSignalBlocker lockBlock(lock_);
  lock_->setChecked(false);
  triangles_->setLocked(false);
  if (available) {
    const auto &entry =
        session_.document().entries().at(session_.selectedFlame());
    name_->setText(text(entry.name));
    producer_->setText(entry.version.empty() ? "Generic flame"
                                             : text(entry.version));
    const auto issue =
        session_.document().render_issue(session_.selectedFlame());
    diagnostic_->setVisible(issue.has_value());
    diagnostic_->setText(issue ? "Preservation only\n" + text(*issue)
                               : QString{});
    if (!issue)
      model_ = session_.document().render_model(session_.selectedFlame());
    if (model_) {
      width_->setValue(model_->width);
      height_->setValue(model_->height);
      setNumber(centerX_, model_->center.x);
      setNumber(centerY_, model_->center.y);
      setNumber(scale_, model_->scale);
    } else {
      // Header fields can still be read and edited while a plugin prevents
      // rendering.
      const auto &attributes = entry.attributes;
      auto source = [&](const char *key, const QString &fallback) {
        auto i = attributes.find(key);
        return i == attributes.end() ? fallback : text(i->second);
      };
      const auto size = source("size", "100 100").simplified().split(' ');
      width_->setValue(size.value(0).toInt());
      height_->setValue(size.value(1).toInt());
      const auto center = source("center", "0 0").simplified().split(' ');
      centerX_->setText(center.value(0));
      centerY_->setText(center.value(1));
      scale_->setText(source("scale", "50"));
      for (auto field : {centerX_, centerY_, scale_})
        field->setProperty("committedText", field->text());
    }
  } else {
    name_->clear();
    producer_->clear();
    diagnostic_->hide();
  }
  transforms_->clear();
  if (model_) {
    for (int i = 0; i < int(model_->transforms.size()); ++i)
      transforms_->addItem(QString("Transform %1").arg(i + 1));
    if (model_->final_transform)
      transforms_->addItem("Final transform");
    int row = session_.isFinalTransform() ? int(model_->transforms.size())
                                          : session_.selectedTransform();
    if (row >= transforms_->count() || row < 0)
      row = 0;
    transforms_->setCurrentIndex(row);
  }
  transforms_->setEnabled(model_.has_value());
  matrixMode_->setEnabled(model_.has_value());
  lock_->setEnabled(model_.has_value());
  render_->setEnabled(model_.has_value());
  properties_->setEnabled(model_.has_value());
  gradient_->setEnabled(model_.has_value());
  adjustments_->setEnabled(model_.has_value());
  mutations_->setEnabled(model_.has_value());
  saveFavorite_->setEnabled(session_.hasDocument());
  refreshing_ = false;
  refreshTransform();
  requestPreview();
}

const Transform &MainWindow::selectedTransform() const {
  return session_.isFinalTransform()
             ? model_->final_transform.value()
             : model_->transforms.at(session_.selectedTransform());
}
void MainWindow::setCoefficients(const Affine &a) {
  const std::array values{a.a, a.b, a.c, a.d, a.e, a.f};
  for (int i = 0; i < 6; ++i)
    setNumber(coefficients_[i], values[i]);
}
void MainWindow::refreshTransform() {
  refreshing_ = true;
  const bool editable = model_.has_value() && !lock_->isChecked();
  const bool final = session_.isFinalTransform();
  const int index = session_.selectedTransform();
  add_->setEnabled(model_.has_value());
  addFinal_->setEnabled(model_ && !model_->final_transform);
  duplicate_->setEnabled(editable && !final);
  variationExplorer_->setEnabled(editable);
  remove_->setEnabled(editable &&
                      (final || (model_ && model_->transforms.size() > 1)));
  moveUp_->setEnabled(editable && !final && index > 0);
  moveDown_->setEnabled(editable && !final &&
                        index + 1 <
                            int(model_ ? model_->transforms.size() : 0));
  copy_->setEnabled(model_.has_value());
  paste_->setEnabled(model_.has_value() && copiedTransform_.has_value());
  if (model_) {
    const auto &t = selectedTransform();
    setCoefficients(matrixMode_->currentIndex() == 1 ? t.post : t.pre);
    setNumber(weight_, t.weight);
    weight_->setEnabled(!final);
    setNumber(color_, t.color);
    setNumber(colorSpeed_, t.color_speed);
    setNumber(opacity_, t.opacity);
    int firstParameterized = -1;
    for (int row = 0; row < variations_->rowCount(); ++row) {
      const auto id = variation_registry()[row].id;
      auto term = std::find_if(t.variations.begin(), t.variations.end(),
                               [&](const Term &v) { return v.id == id; });
      const double weight = term == t.variations.end() ? 0 : term->weight;
      variations_->item(row, 1)->setText(number(weight));
      variations_->item(row, 0)->setForeground(
          weight ? QColor("#76d5dd") : palette().color(QPalette::Text));
      if (firstParameterized < 0 && weight != 0 &&
          std::any_of(variation_parameter_registry().begin(), variation_parameter_registry().end(),
                      [id](const auto &field) { return field.variation == id; }))
        firstParameterized = row;
    }
    if (variations_->currentRow() < 0 && firstParameterized >= 0)
      variations_->setCurrentCell(firstParameterized, 0);
    refreshVariationParameters();
    QImage strip(256, 1, QImage::Format_RGB32);
    for (int i = 0; i < 256; ++i) {
      const auto c = model_->palette[i];
      strip.setPixelColor(i, 0, QColor::fromRgbF(c.r, c.g, c.b));
    }
    palette_->setPixmap(QPixmap::fromImage(strip));
    chaos_->setEnabled(!final);
    chaos_->setRowCount(final ? 0 : int(model_->transforms.size()));
    for (int i = 0; i < chaos_->rowCount(); ++i) {
      auto label = new QTableWidgetItem(QString("Transform %1").arg(i + 1));
      label->setFlags(label->flags() & ~Qt::ItemIsEditable);
      chaos_->setItem(i, 0, label);
      chaos_->setItem(i, 1,
                      new QTableWidgetItem(number(
                          std::size_t(i) < t.chaos.size() ? t.chaos[i] : 1)));
    }
  }
  triangles_->setFlame(model_, index, final);
  refreshing_ = false;
}
void MainWindow::requestPreview() {
  aiPreviewCurrent_ = false;
  aiPreviewPng_.clear();
  if (closing_)
    return;
  if (model_) {
    preview_.request(*model_, previewCanvas_->size());
    return;
  }
  preview_.stop();
  stats_->setText("CPU preview unavailable");
  previewCanvas_->setMessage(
      session_.hasDocument()
          ? "This flame is preserved with its original settings.\n\n" +
                diagnostic_->text()
          : "Open a flame library to begin");
}
void MainWindow::refreshVariationParameters() {
  const int row = variations_->currentRow();
  if (!model_ || row < 0) return;
  const auto selected = variation_registry()[row].id;
  const auto &transform = selectedTransform();
  for (const auto &field : variation_parameter_registry()) {
    auto input = variationFields_.at(field.name);
    variationForm_->setRowVisible(input, field.variation == selected);
    if (field.variation == selected)
      setNumber(input, field.get(transform.parameters));
  }
}

void MainWindow::editTransform(const QString &label,
                               const LegacyTransformEdits &edit) {
  if (!model_ || lock_->isChecked())
    return;
  session_.change(label, [&](auto &document) {
    document.edit_transform(session_.selectedFlame(),
                            session_.selectedTransform(), edit,
                            session_.isFinalTransform());
  });
}
void MainWindow::editGradient() {
  if (!model_)
    return;
  const auto original = *model_;
  const bool paused = preview_.isPaused();
  preview_.setPaused(true);
  GradientDialog dialog(original, this, preview_.settings());
  const auto result = dialog.exec();
  preview_.setPaused(paused);
  if (result != QDialog::Accepted)
    return;
  run([&] {
    const auto &next = dialog.resultFlame();
    const bool samePalette = std::equal(
        original.palette.begin(), original.palette.end(), next.palette.begin(),
        [](RGB a, RGB b) { return a.r == b.r && a.g == b.g && a.b == b.b; });
    session_.change("Edit gradient", [&](auto &document) {
      if (!samePalette)
        document.edit_palette(std::size_t(session_.selectedFlame()),
                              next.palette);
      if (original.palette_mode != next.palette_mode) {
        LegacyEdits edit;
        edit.palette_mode = next.palette_mode;
        document.edit(std::size_t(session_.selectedFlame()), edit);
      }
    });
  });
}
void MainWindow::editAdjustments() {
  if (!model_)
    return;
  const bool paused = preview_.isPaused();
  preview_.setPaused(true);
  AdjustmentDialog dialog(*model_, this, preview_.settings());
  const auto result = dialog.exec();
  preview_.setPaused(paused);
  if (result != QDialog::Accepted)
    return;
  run([&] {
    session_.change("Adjust flame", [&](auto &document) {
      document.edit(std::size_t(session_.selectedFlame()), dialog.edits());
    });
  });
}
QString MainWindow::favoriteStorage() {
  if (useSettings_ || !favoritesDirectory_.isEmpty())
    return favoritesDirectory_;
  // An isolated editor session uses a disposable library unless a data
  // directory was supplied explicitly for persistence between sessions.
  if (!temporaryFavorites_)
    temporaryFavorites_.emplace();
  if (!temporaryFavorites_->isValid())
    throw std::runtime_error("Could not create a temporary favorites folder");
  return temporaryFavorites_->path();
}

void MainWindow::exploreVariations() {
  if (!model_ || lock_->isChecked())
    return;
  run([this] {
    const auto revision = session_.revision_;
    const auto generation = session_.documentGeneration_;
    const auto index = session_.selectedFlame();
    const auto transform = session_.selectedTransform();
    const bool final = session_.isFinalTransform();
    const int row = variations_->currentRow();
    const auto registry = variation_registry();
    const auto initial = row >= 0 && row < int(registry.size())
                             ? registry[std::size_t(row)].id : Variation::swirl;
    const bool paused = preview_.isPaused();
    preview_.setPaused(true);
    const auto restorePreview = qScopeGuard([this, paused] { preview_.setPaused(paused); });
    VariationExplorerDialog dialog(session_.document(), index, transform, final, initial,
                                   this, preview_.settings());
    if (dialog.exec() != QDialog::Accepted)
      return;
    if (session_.documentGeneration_ != generation || session_.revision_ != revision ||
        session_.selectedFlame() != index || session_.selectedTransform() != transform ||
        session_.isFinalTransform() != final)
      throw std::runtime_error("The selected flame changed. Open the variation explorer again.");
    editTransform("Apply variation preview", dialog.selectedEdit());
  });
}

void MainWindow::exploreFlames(bool mutation) {
  if (mutation && !model_)
    return;
  const bool paused = preview_.isPaused();
  preview_.setPaused(true);
  ExploreDialog dialog(
      mutation ? ExploreDialog::Mode::mutation : ExploreDialog::Mode::random,
      model_.value_or(demo_flame()), this, preview_.settings());
  const auto result = dialog.exec();
  preview_.setPaused(paused);
  if (result != QDialog::Accepted || dialog.acceptedFlames().empty())
    return;
  run([&] {
    const auto &flames = dialog.acceptedFlames();
    if (!session_.hasDocument()) {
      session_.loadBytes(serialize_flame(flames.front()), {}, true);
      if (flames.size() > 1)
        session_.change("Add generated batch", [&](auto &document) {
          for (std::size_t i = 1; i < flames.size(); ++i)
            document.append_flame(flames[i]);
        });
      return;
    }
    const auto source = std::size_t(session_.selectedFlame());
    if (mutation &&
        dialog.disposition() == ExploreDialog::Disposition::replace) {
      session_.change("Apply mutation", [&](auto &document) {
        applyCreativeFlame(document, source, flames.front());
      });
    } else {
      const auto next = int(session_.document().entries().size());
      session_.change(
          mutation ? "Keep mutation candidates" : "Add generated flames",
          [&](auto &document) {
            for (const auto &flame : flames) {
              if (mutation) {
                const auto index = document.duplicate_flame(source, flame.name);
                applyCreativeFlame(document, index, flame);
              } else
                document.append_flame(flame);
            }
          },
          0, false, next);
    }
  });
}
void MainWindow::commitAffine(const Affine &value) {
  run([&] {
    LegacyTransformEdits edit;
    if (matrixMode_->currentIndex() == 1)
      edit.post = value;
    else
      edit.pre = value;
    editTransform("Edit affine transform", edit);
  });
}
void MainWindow::applyGeometry(const QString &label, const Affine &operation) {
  if (!model_ || lock_->isChecked())
    return;
  const double px = readNumber(pivotX_), py = readNumber(pivotY_);
  const auto aboutPivot =
      compose(Affine{1, 0, 0, 1, px, py},
              compose(operation, Affine{1, 0, 0, 1, -px, -py}));
  const auto &t = selectedTransform();
  const auto &current = matrixMode_->currentIndex() == 1 ? t.post : t.pre;
  const auto result = coordinates_->currentIndex() == 0
                          ? compose(aboutPivot, current)
                          : compose(current, aboutPivot);
  LegacyTransformEdits edit;
  if (matrixMode_->currentIndex() == 1)
    edit.post = result;
  else
    edit.pre = result;
  editTransform(label, edit);
}
void MainWindow::filterLibrary() {
  const auto query = librarySearch_->text();
  for (int row = 0; row < library_->count(); ++row)
    library_->item(row)->setHidden(
        !library_->item(row)->text().contains(query, Qt::CaseInsensitive));
}
void MainWindow::updateTitle() {
  auto title = session_.path().isEmpty()
                   ? "Untitled library"
                   : QFileInfo(session_.path()).fileName();
  setWindowModified(session_.isModified());
  setWindowTitle(title + "[*] — Apophysis Reborn");
}
QJsonObject MainWindow::systemInformation() const {
  return system_information(preview_.settings(),
      lastBackendReport_.isEmpty() ? QString("Not yet reported") : previewBackend_,
      lastBackendReport_.isEmpty() ? QString("Not yet reported") : previewDevice_,
      previewFallback_);
}
void MainWindow::record(const QString &message) {
  if (!log_)
    return;
  log_->appendPlainText(QDateTime::currentDateTime().toString("HH:mm:ss") +
                        "  " + message);
  if (!useSettings_)
    return;
  const auto directory =
      QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
  QDir().mkpath(directory);
  const auto path = directory + "/editor.log";
  if (QFileInfo(path).size() > 2 * 1024 * 1024) {
    QFile::remove(path + ".1");
    QFile::rename(path, path + ".1");
  }
  QFile file(path);
  if (file.open(QIODevice::WriteOnly | QIODevice::Append))
    file.write((QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs) +
                "\t" + message + "\n")
                   .toUtf8());
}
void MainWindow::run(const std::function<void()> &operation) {
  if (closing_)
    return;
  try {
    operation();
  } catch (const std::exception &error) {
    record(text(error.what()));
    statusBar()->showMessage(text(error.what()), 12000);
    refresh();
    QMessageBox::warning(this, "Could not apply change", text(error.what()));
  }
}
bool MainWindow::confirmDiscard() {
  if (!session_.isModified())
    return true;
  const auto choice = QMessageBox::warning(
      this, "Unsaved flame edits", "Save your changes before continuing?",
      QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
      QMessageBox::Save);
  if (choice == QMessageBox::Cancel)
    return false;
  if (choice == QMessageBox::Save)
    return saveDocument();
  return true;
}
bool MainWindow::openFile(const QString &path) {
  if (closing_ || saving_)
    return false;
  try {
    const auto suffix = QFileInfo(path).suffix();
    std::optional<std::string> pngProject;
    if (suffix.compare("png", Qt::CaseInsensitive) == 0) {
      auto read = std::make_shared<std::packaged_task<std::string(std::stop_token)>>(
          [file = filesystemPath(path)](std::stop_token token) {
        auto png = read_editable_png(file, token);
        ProjectFile project;
        project.library_bytes = std::move(png.xml);
        project.render_settings["outputFormat"] = std::to_string(int(png.image.format));
        project.render_settings["outputAlpha"] = png.image.alpha ? "1" : "0";
        if (png.settings) {
          project.preview_settings["seed"] = std::to_string(png.settings->seed);
          project.render_settings["outputSeed"] = std::to_string(png.settings->seed);
          project.render_settings["outputSamples"] = std::to_string(png.settings->samples);
          project.render_settings["deriveOutputSamples"] = "0";
        }
        return serialize_project(project);
      });
      auto reader = read->get_future();
      const auto jobs = pngReaders->jobs;
      jobs->submit(read.get(), [read](std::stop_token stop) { (*read)(stop); });
      if (reader.wait_for(std::chrono::milliseconds(20)) != std::future_status::ready) {
        QProgressDialog progress("Reading editable flame from PNG…", "Cancel", 0, 0, this);
        progress.setWindowTitle("Open editable PNG");
        progress.setMinimumDuration(0);
        QTimer timer;
        connect(&timer, &QTimer::timeout, &progress, [&] {
          if (reader.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
            progress.accept();
        });
        timer.start(30);
        if (progress.exec() != QDialog::Accepted) {
          jobs->cancel(read.get());
          return false;
        }
      }
      pngProject = reader.get();
    }
    if (!confirmDiscard())
      return false;
    if (pngProject) {
      session_.loadBytes(std::move(*pngProject), {}, true);
    } else if (suffix.compare("upr", Qt::CaseInsensitive) == 0) {
      if (!importUpr(path))
        return false;
    } else
      session_.load(path);
    restoreProjectState();
    librarySearch_->clear();
    triangles_->fit();
    previewCanvas_->fit();
    record(pngProject ? "Opened editable PNG as a new project: " + path : "Opened " + path);
    if (useSettings_) {
      auto recent = settings_.value("files/recent").toStringList();
      recent.removeAll(QFileInfo(path).absoluteFilePath());
      recent.prepend(QFileInfo(path).absoluteFilePath());
      while (recent.size() > 10)
        recent.removeLast();
      settings_.setValue("files/recent", recent);
    }
    return true;
  } catch (const std::exception &e) {
    record(text(e.what()));
    QMessageBox::warning(this, "Could not open flame", text(e.what()));
    return false;
  }
}
void MainWindow::chooseOpen() {
  const auto path = QFileDialog::getOpenFileName(
      this, "Open project or flame library", session_.path(),
      "Projects and flames (*.apoproject *.flame *.xml *.temp *.upr *.png);;Native "
      "projects (*.apoproject);;Editable PNG (*.png);;Apophysis UPR (*.upr);;All files (*)");
  if (!path.isEmpty())
    openFile(path);
}
void MainWindow::openExample() {
  QFile file(":/examples/ember-spiral.flame");
  if (!file.open(QIODevice::ReadOnly))
    throw std::runtime_error("The built-in example is missing");
  session_.loadBytes(file.readAll().toStdString());
  restoreProjectState();
  triangles_->fit();
}
bool MainWindow::openStarterLibrary() {
  if (closing_ || saving_ || !confirmDiscard())
    return false;
  QFile file(starterPath);
  if (!file.open(QIODevice::ReadOnly))
    throw std::runtime_error("The built-in starter library is missing");
  session_.loadBytes(file.readAll().toStdString());
  restoreProjectState();
  librarySearch_->clear();
  triangles_->fit();
  previewCanvas_->fit();
  return true;
}

void MainWindow::newDocument() {
  if (!confirmDiscard())
    return;
  run([&] {
    auto flame = demo_flame();
    flame.name = "Untitled flame";
    session_.loadBytes(serialize_flame(flame), {}, true);
    restoreProjectState();
    librarySearch_->clear();
    triangles_->fit();
    previewCanvas_->fit();
  });
}
bool MainWindow::saveDocument(bool saveAs) {
  if (saving_ || !session_.hasDocument())
    return false;
  const QScopedValueRollback saving(saving_, true);
  QString path = session_.path();
  const bool needsNative = session_.hasAnimation();
  if (needsNative && !path.isEmpty() && !is_project_path(filesystemPath(path))) {
    const QFileInfo info(path);
    path = info.absolutePath() + "/" + info.completeBaseName() + ".apoproject";
    saveAs = true;
  }
  for (;;) {
    if (saveAs || path.isEmpty())
      path = QFileDialog::getSaveFileName(
          this, "Save project or flame library",
          path.isEmpty() ? "my-project.apoproject" : path,
          needsNative ? "Native projects (*.apoproject)"
                      : "Native projects (*.apoproject);;Flame libraries (*.flame);;XML files (*.xml)");
    if (path.isEmpty())
      return false;
    try {
      return saveToPath(path);
    } catch (const FileConflict &) {
      QMessageBox box(QMessageBox::Warning, "File changed on disk",
                      "Another change was made to this file after you opened it.",
                      QMessageBox::NoButton, this);
      auto copy = box.addButton("Save a copy…", QMessageBox::AcceptRole);
      auto replace = box.addButton("Replace disk version", QMessageBox::DestructiveRole);
      box.addButton(QMessageBox::Cancel);
      box.exec();
      if (box.clickedButton() == copy) {
        saveAs = true;
        continue;
      }
      if (box.clickedButton() == replace) {
        try {
          return saveToPath(path, true);
        } catch (const std::exception &error) {
          QMessageBox::warning(this, "Save failed", text(error.what()));
        }
      }
    } catch (const std::exception &error) {
      record(text(error.what()));
      QMessageBox::warning(this, "Save failed", text(error.what()));
    }
    return false;
  }
}

void MainWindow::showRenderQueue() {
  if (!queueDialog_) {
    queueDialog_ = new RenderQueueDialog(*queue_, this);
    queueDialog_->setAttribute(Qt::WA_DeleteOnClose);
  }
  queueDialog_->show();
  queueDialog_->raise();
  queueDialog_->activateWindow();
}

void MainWindow::renderImage() {
  if (!model_)
    return;
  const auto original = *model_;
  QDialog dialog(this);
  dialog.setObjectName("renderSettingsDialog");
  dialog.setWindowTitle("Render image");
  dialog.setMinimumWidth(1000);
  auto layout = new QVBoxLayout(&dialog);
  auto columns = new QHBoxLayout;
  auto outputGroup = new QGroupBox("Output and sampling");
  auto outputForm = new QFormLayout(outputGroup);
  auto preset = new QComboBox;
  preset->setObjectName("outputPreset");
  preset->addItem("Custom dimensions", QSize{});
  for (const auto size :
       {QSize(1920, 1080), QSize(2560, 1440), QSize(3840, 2160),
        QSize(4096, 4096), QSize(7680, 4320), QSize(8192, 8192),
        QSize(15360, 8640), QSize(16384, 16384)})
    preset->addItem(QString("%1 × %2").arg(size.width()).arg(size.height()),
                    size);
  outputForm->addRow("Resolution preset", preset);
  auto sizeRow = new QWidget;
  auto sizeLayout = new QHBoxLayout(sizeRow);
  sizeLayout->setContentsMargins(0, 0, 0, 0);
  auto width = new QSpinBox;
  auto height = new QSpinBox;
  width->setObjectName("outputWidth");
  height->setObjectName("outputHeight");
  width->setRange(1, 16384);
  height->setRange(1, 16384);
  width->setValue(original.width);
  height->setValue(original.height);
  sizeLayout->addWidget(width);
  sizeLayout->addWidget(new QLabel("×"));
  sizeLayout->addWidget(height);
  outputForm->addRow("Width × height", sizeRow);
  auto aspect = new QCheckBox("Keep current aspect ratio");
  aspect->setObjectName("outputAspectLock");
  aspect->setChecked(true);
  outputForm->addRow(QString{}, aspect);
  auto quality = new QDoubleSpinBox;
  quality->setObjectName("outputQuality");
  quality->setRange(0.0001, 1000000);
  quality->setDecimals(4);
  quality->setValue(original.quality);
  outputForm->addRow("Sample density", quality);
  auto derived = new QCheckBox("Calculate samples from density, size and zoom");
  derived->setObjectName("deriveOutputSamples");
  derived->setChecked(true);
  outputForm->addRow(QString{}, derived);
  auto samples = new QLineEdit;
  samples->setObjectName("outputSamples");
  outputForm->addRow("Target samples", samples);
  auto oversample = new QSpinBox;
  oversample->setObjectName("outputOversample");
  oversample->setRange(1, 16);
  oversample->setValue(original.oversample);
  outputForm->addRow("Oversampling", oversample);
  auto filterRow = new QWidget;
  auto filterLayout = new QHBoxLayout(filterRow);
  filterLayout->setContentsMargins(0, 0, 0, 0);
  auto filter = new QComboBox;
  filter->setObjectName("outputFilter");
  filter->addItems({"Box", "Gaussian"});
  filter->setCurrentIndex(original.filter_radius ? 1 : 0);
  auto radius = new QDoubleSpinBox;
  radius->setObjectName("outputFilterRadius");
  radius->setRange(0, 1000);
  radius->setDecimals(4);
  radius->setValue(original.filter_radius.value_or(0.5));
  filterLayout->addWidget(filter);
  filterLayout->addWidget(radius);
  outputForm->addRow("Spatial filter / radius", filterRow);
  auto format = new QComboBox;
  format->setObjectName("outputFormat");
  for (const auto value :
       {ImageFormat::png8, ImageFormat::png16, ImageFormat::tiff8,
        ImageFormat::tiff16, ImageFormat::bmp8, ImageFormat::ppm8,
        ImageFormat::ppm16})
    format->addItem(imageFormatText(value), int(value));
  outputForm->addRow("Format / bit depth", format);
  auto alpha = new QCheckBox("Transparent background (straight alpha)");
  alpha->setObjectName("outputAlpha");
  outputForm->addRow(QString{}, alpha);
  auto embed = new QCheckBox("Include editable flame in PNG");
  embed->setObjectName("embedEditableFlame");
  embed->setToolTip("Anyone with this image can reopen its flame, palette and sampling settings.");
  outputForm->addRow(QString{}, embed);
  columns->addWidget(outputGroup, 1);

  auto computeGroup = new QGroupBox("Device and working limits");
  auto computeForm = new QFormLayout(computeGroup);
  auto threads = new QSpinBox;
  threads->setObjectName("outputThreads");
  threads->setRange(0, 256);
  threads->setSpecialValueText("Automatic");
  threads->setValue(preview_.settings().threads);
  auto memory = new QSpinBox;
  memory->setObjectName("outputMemory");
  memory->setRange(16, 1048576);
  memory->setValue(512);
  memory->setSuffix(" MiB");
  auto disk = new QSpinBox;
  disk->setObjectName("outputDisk");
  disk->setRange(16, 1048576);
  disk->setValue(16384);
  disk->setSuffix(" MiB");
  auto seed = new QLineEdit(QString::number(preview_.settings().seed));
  seed->setObjectName("outputSeed");
  computeForm->addRow("CPU workers", threads);
  computeForm->addRow("System buffer budget", memory);
  computeForm->addRow("Working disk budget", disk);
  computeForm->addRow("Random seed", seed);
  const auto compute =
      renderControls(computeForm, &dialog, preview_.settings(), deviceDiscovery_);
  columns->addWidget(computeGroup, 1);
  layout->addLayout(columns);
  auto destinationRow = new QHBoxLayout;
  destinationRow->addWidget(new QLabel("Destination"));
  auto destination = new QLineEdit;
  destination->setObjectName("outputDestination");
  destination->setPlaceholderText("Choose an output image file…");
  destinationRow->addWidget(destination, 1);
  auto browse = new QPushButton("Browse…");
  browse->setObjectName("browseOutputDestination");
  destinationRow->addWidget(browse);
  layout->addLayout(destinationRow);
  auto estimate = new QLabel;
  estimate->setObjectName("renderEstimate");
  estimate->setWordWrap(true);
  estimate->setTextFormat(Qt::PlainText);
  estimate->setMinimumHeight(48);
  layout->addWidget(estimate);
  auto note =
      new QLabel("Each job keeps a flame snapshot. Global sampling and filter "
                 "overlap are preserved across strips. "
                 "Time estimates appear after measured progress in the queue.");
  note->setWordWrap(true);
  layout->addWidget(note);
  auto buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  buttons->button(QDialogButtonBox::Ok)->setText("Add and start queue");
  buttons->button(QDialogButtonBox::Ok)->setObjectName("startOutputRender");
  auto add = buttons->addButton("Add to queue", QDialogButtonBox::ActionRole);
  add->setObjectName("addOutputToQueue");
  auto proof = buttons->addButton("Proof a crop…", QDialogButtonBox::ActionRole);
  proof->setObjectName("openRenderProof");
  layout->addWidget(buttons);
  auto savedRow = new QHBoxLayout;
  auto savedPresets = new QComboBox;
  savedPresets->setObjectName("savedRenderPresets");
  auto savePreset = new QPushButton("Save current preset…");
  savePreset->setObjectName("saveRenderPreset");
  auto removePreset = new QPushButton("Remove preset");
  removePreset->setObjectName("removeRenderPreset");
  savedRow->addWidget(new QLabel("Project preset"));
  savedRow->addWidget(savedPresets, 1);
  savedRow->addWidget(savePreset);
  savedRow->addWidget(removePreset);
  layout->insertLayout(0, savedRow);
  auto populatePresets = [&] {
    const QSignalBlocker block(savedPresets);
    savedPresets->clear();
    savedPresets->addItem("Choose saved settings…");
    for (const auto &p : renderPresets_)
      savedPresets->addItem(text(p.name));
  };
  populatePresets();
  const QStringList savedKeys{
      "outputWidth",         "outputHeight",       "outputQuality",
      "deriveOutputSamples", "outputSamples",      "outputOversample",
      "outputFilter",        "outputFilterRadius", "outputFormat",
      "outputAlpha",         "outputThreads",      "outputMemory",
      "outputDisk",          "outputSeed",         "renderBackend",
      "renderDevice",        "gpuMemory",          "cpuFallback",
      "outputAspectLock"};
  auto captureSettings = [&] {
    auto values = renderSettings_;
    for (const auto &key : savedKeys) {
      auto widget = dialog.findChild<QWidget *>(key);
      if (auto field = qobject_cast<QSpinBox *>(widget))
        values[key.toStdString()] = std::to_string(field->value());
      else if (auto field = qobject_cast<QDoubleSpinBox *>(widget))
        values[key.toStdString()] = number(field->value()).toStdString();
      else if (auto field = qobject_cast<QCheckBox *>(widget))
        values[key.toStdString()] = field->isChecked() ? "1" : "0";
      else if (auto field = qobject_cast<QLineEdit *>(widget))
        values[key.toStdString()] = field->text().toStdString();
      else if (auto field = qobject_cast<QComboBox *>(widget))
        values[key.toStdString()] =
            field->currentData().isValid()
                ? field->currentData().toString().toStdString()
                : std::to_string(field->currentIndex());
    }
    return values;
  };
  auto applySettings = [&](const ProjectSettings &values) {
    std::vector<std::unique_ptr<QSignalBlocker>> blocks;
    for (const auto &key : savedKeys)
      if (auto widget = dialog.findChild<QWidget *>(key))
        blocks.push_back(std::make_unique<QSignalBlocker>(widget));
    for (const auto &key : savedKeys) {
      const auto it = values.find(key.toStdString());
      if (it == values.end())
        continue;
      const auto value = text(it->second);
      auto widget = dialog.findChild<QWidget *>(key);
      bool ok = false;
      if (auto field = qobject_cast<QSpinBox *>(widget)) {
        auto v = value.toInt(&ok);
        if (ok && v >= field->minimum() && v <= field->maximum())
          field->setValue(v);
      } else if (auto field = qobject_cast<QDoubleSpinBox *>(widget)) {
        auto v = QLocale::c().toDouble(value, &ok);
        if (ok && std::isfinite(v) && v >= field->minimum() &&
            v <= field->maximum())
          field->setValue(v);
      } else if (auto field = qobject_cast<QCheckBox *>(widget)) {
        if (value == "0" || value == "1")
          field->setChecked(value == "1");
      } else if (auto field = qobject_cast<QLineEdit *>(widget))
        field->setText(value);
      else if (auto field = qobject_cast<QComboBox *>(widget)) {
        if (field->itemData(0).isValid()) {
          int selected = -1;
          for (int i = 0; i < field->count(); ++i)
            if (field->itemData(i).toString() == value) {
              selected = i;
              break;
            }
          if (selected >= 0)
            field->setCurrentIndex(selected);
        } else {
          const int selected = value.toInt(&ok);
          if (ok && selected >= 0 && selected < field->count())
            field->setCurrentIndex(selected);
        }
      }
    }
  };
  applySettings(renderSettings_);
  bool startNow = true;
  auto renderTone = proof_tone(original);
  auto resized = [&] {
    auto f = original;
    f.brightness = renderTone.brightness;
    f.gamma = renderTone.gamma;
    f.vibrancy = renderTone.vibrancy;
    f.width = width->value();
    f.height = height->value();
    f.scale *= std::min(double(f.width) / original.width,
                        double(f.height) / original.height);
    f.quality = quality->value();
    f.oversample = oversample->value();
    f.filter_radius = filter->currentIndex() == 1
                          ? std::optional(radius->value())
                          : std::nullopt;
    return f;
  };
  auto options = [&] {
    RenderOptions o;
    bool valid = false;
    o.samples = samples->text().toULongLong(&valid);
    if (!valid || !o.samples)
      throw std::invalid_argument("Enter a positive target sample count");
    o.seed = seed->text().toULongLong(&valid);
    if (!valid)
      throw std::invalid_argument("Enter an unsigned 64-bit seed");
    o.threads = unsigned(threads->value());
    o.memory_mib = std::uint64_t(memory->value());
    compute.apply(o);
    return o;
  };
  auto fileOptions = [&] {
    FileRenderOptions f;
    f.disk_mib = std::uint64_t(disk->value());
    f.image.format = static_cast<ImageFormat>(format->currentData().toInt());
    f.image.alpha = alpha->isChecked();
    f.embed_flame = embed->isChecked();
    return f;
  };
  connect(proof, &QPushButton::clicked, &dialog, [&] {
    try {
      const auto image = fileOptions().image;
      RenderProofDialog preview(resized(), options(), image.alpha, &dialog,
                                image_format_bits(image.format) == 16);
      if (preview.exec() == QDialog::Accepted) {
        renderTone.brightness = preview.brightness();
        renderTone.gamma = preview.gamma();
        renderTone.vibrancy = preview.vibrancy();
        note->setText(tr("Tone for this render — brightness %1, gamma %2, vibrancy %3.")
                          .arg(number(renderTone.brightness), number(renderTone.gamma),
                               number(renderTone.vibrancy)));
      }
    } catch (const std::exception &error) {
      estimate->setText(text(error.what()));
    }
  });
  auto suffix = [&] {
    const auto f = fileOptions().image.format;
    return f == ImageFormat::bmp8                              ? QString("bmp")
           : f == ImageFormat::ppm8 || f == ImageFormat::ppm16 ? QString("ppm")
           : f == ImageFormat::tiff8 || f == ImageFormat::tiff16
               ? QString("tif")
               : QString("png");
  };
  std::function<void()> update = [&] {
    try {
      samples->setEnabled(!derived->isChecked());
      quality->setEnabled(derived->isChecked());
      radius->setEnabled(filter->currentIndex() == 1);
      const auto f = resized();
      if (derived->isChecked()) {
        const QSignalBlocker block(samples);
        samples->setText(QString::number(default_samples(f)));
      }
      const auto output = fileOptions().image.format;
      const bool png = output == ImageFormat::png8 || output == ImageFormat::png16;
      embed->setEnabled(png);
      if (!png) {
        const QSignalBlocker block(embed);
        embed->setChecked(false);
      }
      const bool canAlpha =
          output == ImageFormat::png8 || output == ImageFormat::png16 ||
          output == ImageFormat::tiff8 || output == ImageFormat::tiff16;
      alpha->setEnabled(canAlpha);
      if (!canAlpha) {
        const QSignalBlocker block(alpha);
        alpha->setChecked(false);
      }
      const auto plan = plan_file_render(f, options(), fileOptions());
      QString value =
          QString("Planned working buffers: %1 MiB · Working disk allowance "
                  "needed: %2 MiB · %3 strip(s) of up to %4 rows.")
              .arg(double(plan.host_bytes) / 1048576, 0, 'f', 1)
              .arg(double(plan.disk_bytes) / 1048576, 0, 'f', 1)
              .arg(plan.strip_count)
              .arg(plan.strip_rows);
      if (!destination->text().isEmpty()) {
        const QStorageInfo storage(
            QFileInfo(destination->text()).absolutePath());
        if (storage.isValid() && storage.isReady() &&
            storage.bytesAvailable() >= 0)
          value += QString(" Available on output volume: %1 GiB.")
                       .arg(double(storage.bytesAvailable()) / 1073741824, 0,
                            'f', 1);
      }
      estimate->setText(value);
      estimate->setStyleSheet({});
      add->setEnabled(true);
      buttons->button(QDialogButtonBox::Ok)->setEnabled(true);
    } catch (const std::exception &error) {
      estimate->setText(text(error.what()));
      estimate->setStyleSheet("color: #f1ae86");
      add->setEnabled(false);
      buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    }
  };
  double ratio = double(width->value()) / height->value();
  bool changingSize = false;
  connect(preset, &QComboBox::currentIndexChanged, &dialog, [&] {
    const auto size = preset->currentData().toSize();
    if (size.isValid()) {
      const QSignalBlocker blockWidth(width), blockHeight(height);
      width->setValue(size.width());
      height->setValue(size.height());
      ratio = double(size.width()) / size.height();
    }
    update();
  });
  connect(width, &QSpinBox::valueChanged, &dialog, [&] {
    if (changingSize)
      return;
    changingSize = true;
    if (aspect->isChecked())
      height->setValue(int(std::lround(width->value() / ratio)));
    {
      const QSignalBlocker block(preset);
      preset->setCurrentIndex(0);
    }
    changingSize = false;
    update();
  });
  connect(height, &QSpinBox::valueChanged, &dialog, [&] {
    if (changingSize)
      return;
    changingSize = true;
    if (aspect->isChecked())
      width->setValue(int(std::lround(height->value() * ratio)));
    {
      const QSignalBlocker block(preset);
      preset->setCurrentIndex(0);
    }
    changingSize = false;
    update();
  });
  connect(savedPresets, &QComboBox::activated, &dialog, [&](int index) {
    if (index < 1)
      return;
    applySettings(renderPresets_.at(std::size_t(index - 1)).settings);
    ratio = double(width->value()) / height->value();
    update();
  });
  auto updatePresets = [&](std::vector<ProjectPreset> next) {
    try {
      session_.setProjectRenderSettings(renderSettings_, next);
      renderPresets_ = std::move(next);
      populatePresets();
      return true;
    } catch (const std::exception &error) {
      estimate->setText(text(error.what()));
      return false;
    }
  };
  connect(savePreset, &QPushButton::clicked, &dialog, [&] {
    bool ok = false;
    const auto name =
        QInputDialog::getText(&dialog, "Save render preset", "Preset name",
                              QLineEdit::Normal, {}, &ok)
            .trimmed();
    if (!ok || name.isEmpty())
      return;
    if (name.size() > 128 || renderPresets_.size() >= 100) {
      estimate->setText(
          "Use a name up to 128 characters and at most 100 project presets.");
      return;
    }
    const auto nameUtf8 = name.toStdString();
    auto next = renderPresets_;
    auto found =
        std::find_if(next.begin(), next.end(),
                     [&](const auto &p) { return p.name == nameUtf8; });
    ProjectPreset saved{nameUtf8, captureSettings()};
    if (found == next.end())
      next.push_back(std::move(saved));
    else
      *found = std::move(saved);
    if (!updatePresets(std::move(next)))
      return;
    record("Saved render preset " + name +
           ". Save as a native project to retain it.");
  });
  connect(removePreset, &QPushButton::clicked, &dialog, [&] {
    if (savedPresets->currentIndex() < 1)
      return;
    auto next = renderPresets_;
    next.erase(next.begin() + savedPresets->currentIndex() - 1);
    updatePresets(std::move(next));
  });
  connect(aspect, &QCheckBox::toggled, &dialog, [&](bool checked) {
    if (checked)
      ratio = double(width->value()) / height->value();
  });
  for (auto widget : {threads, memory, disk, oversample})
    connect(widget, &QSpinBox::valueChanged, &dialog, [&] { update(); });
  for (auto widget : {quality, radius})
    connect(widget, &QDoubleSpinBox::valueChanged, &dialog, [&] { update(); });
  for (auto widget : {samples, seed, destination})
    connect(widget, &QLineEdit::textChanged, &dialog, [&] { update(); });
  for (auto widget : {derived, alpha, embed, compute.fallback})
    connect(widget, &QCheckBox::toggled, &dialog, [&] { update(); });
  for (auto widget : {filter, format, compute.backend, compute.device})
    connect(widget, &QComboBox::currentIndexChanged, &dialog,
            [&] { update(); });
  connect(compute.gpuMemory, &QSpinBox::valueChanged, &dialog,
          [&] { update(); });
  connect(browse, &QPushButton::clicked, &dialog, [&] {
    const auto extension = suffix();
    const auto path = QFileDialog::getSaveFileName(
        &dialog, "Render image", "render." + extension,
        QString("%1 (*.%2)")
            .arg(imageFormatText(fileOptions().image.format), extension));
    if (!path.isEmpty())
      destination->setText(path);
  });
  auto accept = [&](bool start) {
    try {
      if (destination->text().trimmed().isEmpty())
        throw std::invalid_argument("Choose an output image file");
      const auto actual = QFileInfo(destination->text()).suffix().toLower();
      if (actual != suffix() && !(suffix() == "tif" && actual == "tiff"))
        throw std::invalid_argument(
            "The filename extension must match the selected image format");
      plan_file_render(resized(), options(), fileOptions());
      startNow = start;
      dialog.accept();
    } catch (const std::exception &error) {
      estimate->setText(text(error.what()));
      estimate->setStyleSheet("color: #f1ae86");
    }
  };
  connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] { accept(true); });
  connect(add, &QPushButton::clicked, &dialog, [&] { accept(false); });
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  update();
  if (dialog.exec() != QDialog::Accepted)
    return;
  run([&] {
    auto next = captureSettings();
    session_.setProjectRenderSettings(next, renderPresets_);
    renderSettings_ = std::move(next);
    const auto id = queue_->add(resized(), options(), fileOptions(),
                                destination->text(), session_.path());
    if (startNow)
      queue_->start();
    showRenderQueue();
    record("Queued image " + destination->text() + " · Job " + id);
  });
}

void MainWindow::preferences() {
  QDialog dialog(this);
  dialog.setObjectName("preferencesDialog");
  dialog.setWindowTitle("Rendering and preview preferences");
  dialog.setMinimumWidth(600);
  auto layout = new QVBoxLayout(&dialog);
  auto form = new QFormLayout;
  auto threads = new QSpinBox;
  threads->setRange(0, 256);
  threads->setSpecialValueText("Automatic");
  threads->setValue(preview_.settings().threads);
  auto memory = new QSpinBox;
  memory->setRange(16, 1048576);
  memory->setSuffix(" MiB");
  memory->setValue(int(preview_.settings().memory_mib));
  auto seed = new QLineEdit(QString::number(preview_.settings().seed));
  seed->setObjectName("previewSeed");
  auto limit = new QLineEdit(QString::number(preview_.settings().sample_limit));
  form->addRow("CPU workers", threads);
  form->addRow("System preview budget", memory);
  form->addRow("Random seed", seed);
  form->addRow("Final preview samples", limit);
  const auto compute = renderControls(form, &dialog, preview_.settings(), deviceDiscovery_);
  layout->addLayout(form);
  auto buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  if (dialog.exec() != QDialog::Accepted)
    return;
  run([&] {
    auto s = preview_.settings();
    bool seedOk = false, limitOk = false;
    s.seed = seed->text().toULongLong(&seedOk);
    s.sample_limit = limit->text().toULongLong(&limitOk);
    if (!seedOk || !limitOk || s.sample_limit == 0)
      throw std::runtime_error(
          "Use an unsigned integer seed and a positive sample limit");
    s.threads = unsigned(threads->value());
    s.memory_mib = std::uint64_t(memory->value());
    compute.apply(s);
    preview_.setSettings(s);
    if (useSettings_) {
      settings_.setValue("preview/threads", s.threads);
      settings_.setValue("preview/memory", qulonglong(s.memory_mib));
      settings_.setValue("preview/seed", qulonglong(s.seed));
      settings_.setValue("preview/samples", qulonglong(s.sample_limit));
      settings_.setValue("render/backend", int(s.backend));
      settings_.setValue("render/device", text(s.device));
      settings_.setValue("render/gpuMemory", qulonglong(s.gpu_memory_mib));
      settings_.setValue("render/cpuFallback", s.allow_cpu_fallback);
    }
    const QSignalBlocker block(quality_);
    int selected = -1;
    for (int i = 0; i < quality_->count(); ++i)
      if (quality_->itemData(i).toULongLong() == s.sample_limit)
        selected = i;
    if (selected < 0) {
      quality_->addItem("Custom", qulonglong(s.sample_limit));
      selected = quality_->count() - 1;
    }
    quality_->setCurrentIndex(selected);
  });
}

void MainWindow::recoverPreviousSession() {
  const auto directory =
      QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
      "/recovery";
  QDir folder(directory);
  const auto files = folder.entryInfoList({"*.flame", "*.apoproject"},
                                          QDir::Files, QDir::Time);
  for (const auto &candidate : files) {
    QLockFile lock(candidate.absoluteFilePath() + ".lock");
    lock.setStaleLockTime(0);
    if (!lock.tryLock())
      continue;
    lock.unlock();
    QMessageBox dialog(QMessageBox::Question, "Recover editing session",
                       "A recovery copy from an interrupted session was found. "
                       "Open it as an unsaved library?",
                       QMessageBox::NoButton, this);
    auto recover = dialog.addButton("Recover edits", QMessageBox::AcceptRole);
    auto discard =
        dialog.addButton("Discard recovery copy", QMessageBox::DestructiveRole);
    dialog.addButton("Later", QMessageBox::RejectRole);
    dialog.exec();
    if (dialog.clickedButton() == recover)
      run([&] {
        session_.recover(candidate.absoluteFilePath());
        restoreProjectState();
        record("Recovered edits from " + candidate.fileName());
      });
    else if (dialog.clickedButton() == discard && lock.tryLock()) {
      QFile::remove(candidate.absoluteFilePath());
      QFile::remove(candidate.absoluteFilePath() + ".json");
    }
    break;
  }
}
void MainWindow::closeEvent(QCloseEvent *event) {
  event->ignore();
  if (saving_)
    return;
  if (!closing_) {
    const QPointer<MainWindow> alive(this);
    if (!confirmDiscard() || !alive)
      return;
    // Approval belongs to this close operation. Freeze editing before any
    // completion signals can run; repeated close events must not ask again or
    // allow new edits after a Save/Discard decision.
    closing_ = true;
    localAi_->stop();
    setEnabled(false);
    setAcceptDrops(false);
    for (auto action : findChildren<QAction *>())
      action->setEnabled(false);
    if (queueDialog_) {
      queueDialog_->setEnabled(false);
      queueDialog_->hide();
    }
    if (auto recoveryTimer = findChild<QTimer *>("recoveryTimer"))
      recoveryTimer->stop();
    preview_.stop();
    if (!alive)
      return;
    session_.discardRecovery();
    queue_->pause();
    if (!alive)
      return;
  }
  if (queue_->isBusy() || session_.isRecoveryBusy()) {
    statusBar()->showMessage("Finishing the render checkpoint and recovery cleanup before closing…");
    if (!closeTimer_) {
      closeTimer_ = new QTimer(this);
      closeTimer_->setObjectName("pendingCloseTimer");
      closeTimer_->setInterval(50);
      connect(closeTimer_, &QTimer::timeout, this, [this] {
        if (!queue_->isBusy() && !session_.isRecoveryBusy())
          close();
      });
    }
    if (!closeTimer_->isActive())
      closeTimer_->start();
    return;
  }
  if (closeTimer_)
    closeTimer_->stop();
  if (useSettings_) {
    settings_.setValue("window/geometry", saveGeometry());
    settings_.setValue("window/layout", saveState(1));
    settings_.setValue("preview/samples",
                       qulonglong(preview_.settings().sample_limit));
  }
  event->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event) {
  if (event->mimeData()->hasUrls() && event->mimeData()->urls().size() == 1 &&
      event->mimeData()->urls().front().isLocalFile())
    event->acceptProposedAction();
}
void MainWindow::dropEvent(QDropEvent *event) {
  if (event->mimeData()->hasUrls() && event->mimeData()->urls().size() == 1 &&
      openFile(event->mimeData()->urls().front().toLocalFile()))
    event->acceptProposedAction();
}
} // namespace apo::editor
