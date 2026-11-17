#include "main_window.hpp"
#include "flame_io.hpp"
#include "output_file.hpp"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
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
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScreen>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <climits>
#include <charconv>
#include <numbers>

namespace apo::editor {
namespace {
QString text(std::string_view value) {
    return QString::fromUtf8(value.data(), qsizetype(value.size()));
}
QString number(double value) {
    char buffer[128];
    const auto result = std::to_chars(std::begin(buffer), std::end(buffer), value);
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
        throw std::invalid_argument("Enter a finite number in " + field->accessibleName().toStdString());
    return value;
}
QLineEdit *numeric(const QString &name, double initial, QWidget *parent = nullptr) {
    auto field = new QLineEdit(parent);
    field->setObjectName(name);
    field->setAccessibleName(name);
    field->setAlignment(Qt::AlignRight);
    field->setMinimumWidth(64);
    setNumber(field, initial);
    return field;
}
void numberEdited(QLineEdit *field, QObject *receiver, const std::function<void()> &commit) {
    QObject::connect(field, &QLineEdit::editingFinished, receiver, [field, commit] {
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
Affine compose(const Affine &left, const Affine &right) {
    return {left.a * right.a + left.c * right.b,          left.b * right.a + left.d * right.b,
            left.a * right.c + left.c * right.d,          left.b * right.c + left.d * right.d,
            left.a * right.e + left.c * right.f + left.e, left.b * right.e + left.d * right.f + left.f};
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
        "QToolBar { spacing: 5px; padding: 5px; border: 0; border-bottom: 1px solid #39434e; }"
        "QToolButton { padding: 5px 7px; }"
        "QDockWidget::title { background: #252e39; padding: 7px; font-weight: 600; }"
        "QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox { padding: 4px; border: 1px solid #435060; "
        "border-radius: 3px; }"
        "QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus { border-color: "
        "#56c8da; }"
        "QPushButton { padding: 5px 9px; }"
        "QListWidget { border: 0; outline: 0; }"
        "QListWidget::item { padding: 7px; border-bottom: 1px solid #2b3542; }"
        "QHeaderView::section { background: #2b3542; padding: 5px; border: 0; }"
        "QGroupBox { font-weight: 600; border: 1px solid #394553; border-radius: 4px; margin-top: 12px; "
        "padding-top: 9px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 9px; padding: 0 4px; }"
        "QTabWidget::pane { border: 1px solid #394553; }"
        "QStatusBar { color: #aab8c8; }"
        "QProgressBar { border: 0; background: #171d25; text-align: center; max-height: 16px; }"
        "QProgressBar::chunk { background: #387f92; }");
}

MainWindow::MainWindow(bool useSettings, QWidget *parent)
    : QMainWindow(parent), session_(this), preview_(this), exporter_(this), useSettings_(useSettings) {
    setObjectName("mainWindow");
    const auto screen = QGuiApplication::primaryScreen();
    const auto available =
        screen ? screen->availableGeometry().size() - QSize(32, 64) : QSize(1420, 900);
    resize(QSize(1420, 900).boundedTo(available));
    setWindowIcon(QIcon(":/examples/ember-spiral.png"));
    setAcceptDrops(true);
    setDockOptions(QMainWindow::AllowNestedDocks | QMainWindow::AllowTabbedDocks |
                   QMainWindow::AnimatedDocks);
    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
    makeActions();
    makeLibrary();
    makePreview();
    makeTransformEditor();
    makeProperties();
    makeLogs();
    resizeDocks({libraryDock_, propertiesDock_}, {220, 370}, Qt::Horizontal);
    resizeDocks({transformDock_}, {260}, Qt::Vertical);
    defaultLayout_ = saveState(1);
    auto view = menuBar()->addMenu("&View");
    for (auto dock : {libraryDock_, transformDock_, propertiesDock_, logDock_})
        view->addAction(dock->toggleViewAction());
    view->addSeparator();
    view->addAction("Reset workspace", this, [this] { restoreState(defaultLayout_, 1); });
    auto full = view->addAction("Full screen", this,
                                [this] { isFullScreen() ? showNormal() : showFullScreen(); });
    full->setShortcut(Qt::Key_F11);
    auto help = menuBar()->addMenu("&Help");
    help->addAction("Editor controls", this, [this] {
        QMessageBox::information(
            this, "Editor controls",
            "Drag a triangle's O handle or interior to move it. Drag X or Y to change its basis. "
            "Middle-drag pans the grid and the wheel zooms. Esc cancels a drag.\n\n"
            "The matrix selector switches between pre-affine and post-affine editing. Rotate and scale "
            "use the chosen world or local pivot. Every edit supports undo.\n\n"
            "The preview refines automatically. Pause stops preview work. Render image writes the "
            "selected flame at its declared output size.\n\n"
            "Unsupported flames keep their original settings and can be renamed and saved. The "
            "diagnostics panel identifies their first render blocker.");
    });
    help->addAction("About Apophysis Reborn", this, [this] {
        QMessageBox::about(
            this, "Apophysis Reborn 0.4.0",
            "Primary Editor development checkpoint\nC++20 / Qt 6 / multithreaded CPU renderer\n\n"
            "21 registered variations and source-preserving legacy documents.\n"
            "See the packaged compatibility report for historical rendering coverage.\n\n"
            "Qt and other third-party license texts accompany the portable distribution.");
    });
    help->addAction("About Qt", qApp, &QApplication::aboutQt);
    connect(&session_, &DocumentSession::changed, this, &MainWindow::refresh);
    connect(&session_, &DocumentSession::selectionChanged, this, &MainWindow::refreshSelection);
    connect(&session_, &DocumentSession::modifiedChanged, this, [this] { updateTitle(); });
    connect(&preview_, &PreviewController::imageReady, this,
            [this](const QImage &image, quint64 samples, quint64 plotted, quint64 invalid,
                   unsigned workers, qint64 elapsed, bool complete) {
                previewCanvas_->setImage(image);
                stats_->setText(QString("%1 samples  ·  %2 workers  ·  %3 ms%4")
                                    .arg(QLocale().toString(samples))
                                    .arg(workers)
                                    .arg(elapsed)
                                    .arg(complete ? "  ·  Refined" : ""));
                stats_->setToolTip(
                    QString("%1 plotted samples\n%2 invalid orbit steps").arg(plotted).arg(invalid));
                if (auto item = library_->item(session_.selectedFlame()))
                    item->setIcon(QPixmap::fromImage(
                        image.scaled(72, 54, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
            });
    connect(&preview_, &PreviewController::statusChanged, this,
            [this](const QString &message) { statusBar()->showMessage(message); });
    connect(&preview_, &PreviewController::progressChanged, progress_, &QProgressBar::setValue);
    connect(&preview_, &PreviewController::busyChanged, progress_, &QWidget::setVisible);
    connect(&preview_, &PreviewController::failed, this, [this](const QString &error) {
        record(error);
        statusBar()->showMessage(error);
    });
    connect(&exporter_, &PreviewController::statusChanged, exportStatus_, &QLabel::setText);
    connect(&exporter_, &PreviewController::progressChanged, exportProgress_, &QProgressBar::setValue);
    connect(&exporter_, &PreviewController::busyChanged, this, [this](bool busy) {
        render_->setEnabled(model_.has_value() && !busy);
        exportProgress_->setVisible(busy);
    });
    connect(&exporter_, &PreviewController::failed, this, [this](const QString &error) {
        record(error);
        QMessageBox::warning(this, "Render failed", error);
    });
    connect(&exporter_, &PreviewController::fileReady, this, [this](const QString &path) {
        record("Rendered image " + path);
        exportStatus_->setText("Saved " + QFileInfo(path).fileName());
    });
    connect(previewCanvas_, &PreviewCanvas::viewportChanged, this, [this] { requestPreview(); });
    auto timer = new QTimer(this);
    timer->setInterval(45000);
    connect(timer, &QTimer::timeout, this, [this] {
        try {
            if (!session_.writeRecovery().isEmpty())
                record("Recovery copy updated");
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
        configuration.memory_mib = settings_.value("preview/memory", 256).toULongLong();
        configuration.seed = settings_.value("preview/seed", 123456).toULongLong();
        configuration.sample_limit = settings_.value("preview/samples", 2000000).toULongLong();
        try {
            preview_.setSettings(configuration);
        } catch (const std::exception &) {
            preview_.setSettings(PreviewSettings{});
        }
        const QSignalBlocker qualityBlock(quality_);
        int preset = quality_->findData(qulonglong(preview_.settings().sample_limit));
        if (preset < 0) {
            quality_->addItem("Custom", qulonglong(preview_.settings().sample_limit));
            preset = quality_->count() - 1;
        }
        quality_->setCurrentIndex(preset);
    }
    refresh();
}

void MainWindow::makeActions() {
    auto file = menuBar()->addMenu("&File");
    auto create = file->addAction(style()->standardIcon(QStyle::SP_FileIcon), "&New flame", this,
                                  &MainWindow::newDocument);
    create->setShortcut(QKeySequence::New);
    create->setObjectName("actionNew");
    auto open = file->addAction(style()->standardIcon(QStyle::SP_DirOpenIcon), "&Open library…", this,
                                &MainWindow::chooseOpen);
    open->setShortcut(QKeySequence::Open);
    open->setObjectName("actionOpen");
    auto recent = file->addMenu("Recent files");
    connect(recent, &QMenu::aboutToShow, this, [this, recent] {
        recent->clear();
        for (const auto &path : settings_.value("files/recent").toStringList())
            recent->addAction(path, this, [this, path] { openFile(path); });
        if (recent->isEmpty())
            recent->addAction("No recent files")->setEnabled(false);
    });
    save_ = file->addAction(style()->standardIcon(QStyle::SP_DialogSaveButton), "&Save library", this,
                            [this] { saveDocument(); });
    save_->setShortcut(QKeySequence::Save);
    save_->setObjectName("actionSave");
    auto saveAs = file->addAction("Save library &as…", this, [this] { saveDocument(true); });
    saveAs->setShortcut(QKeySequence::SaveAs);
    file->addSeparator();
    auto standalone = file->addAction("Export selected flame…", this, [this] {
        run([&] {
            if (!model_)
                throw std::runtime_error("Select a renderable flame to export");
            const auto path = QFileDialog::getSaveFileName(this, "Export standalone flame", {},
                                                           "Flame files (*.flame)");
            if (path.isEmpty())
                return;
            if (!session_.path().isEmpty() &&
                (QFileInfo(path).absoluteFilePath() == session_.path() ||
                 (QFileInfo(path).exists() &&
                  std::filesystem::equivalent(filesystemPath(path), filesystemPath(session_.path())))))
                throw std::runtime_error("Choose a different file for a standalone export");
            const auto bytes = serialize_flame(*model_);
            write_atomic(filesystemPath(path), [&](std::ostream &out) { out << bytes; });
            record("Exported selected flame " + path);
        });
    });
    standalone->setToolTip(
        "Creates a standalone supported model. Comments, history and collection metadata are omitted.");
    render_ = file->addAction(style()->standardIcon(QStyle::SP_DialogApplyButton), "&Render image…",
                              this, &MainWindow::renderImage);
    render_->setShortcut(QKeySequence("Ctrl+R"));
    render_->setObjectName("actionRender");
    file->addSeparator();
    file->addAction("E&xit", this, &QWidget::close)->setShortcut(QKeySequence::Quit);
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
                [&](auto &doc) { doc.add_transform(session_.selectedFlame(), value); },
                int(model_->transforms.size()), false);
        });
    });
    paste_->setShortcut(QKeySequence("Ctrl+Shift+V"));
    edit->addSeparator();
    edit->addAction("Preferences…", this, &MainWindow::preferences);
    auto transform = menuBar()->addMenu("&Transform");
    add_ = transform->addAction("Add transform", this, [this] {
        run([&] {
            if (!model_)
                return;
            Transform value;
            value.pre = {.5, 0, 0, .5, 0, 0};
            value.color = .5;
            session_.change(
                "Add transform", [&](auto &doc) { doc.add_transform(session_.selectedFlame(), value); },
                int(model_->transforms.size()), false);
        });
    });
    add_->setObjectName("actionAddTransform");
    duplicate_ = transform->addAction("Duplicate transform", this, [this] {
        run([&] {
            session_.change(
                "Duplicate transform",
                [&](auto &doc) {
                    doc.duplicate_transform(session_.selectedFlame(), session_.selectedTransform());
                },
                int(model_->transforms.size()), false);
        });
    });
    duplicate_->setShortcut(QKeySequence("Ctrl+D"));
    duplicate_->setObjectName("actionDuplicateTransform");
    remove_ = transform->addAction("Delete transform", this, [this] {
        run([&] {
            const int next =
                session_.isFinalTransform() ? 0 : std::max(0, session_.selectedTransform() - 1);
            session_.change(
                "Delete transform",
                [&](auto &doc) {
                    doc.remove_transform(session_.selectedFlame(), session_.selectedTransform(),
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
                [&](auto &doc) { doc.move_transform(session_.selectedFlame(), i, i - 1); }, i - 1,
                false);
        });
    });
    moveDown_ = transform->addAction("Move transform down", this, [this] {
        run([&] {
            const auto i = session_.selectedTransform();
            session_.change(
                "Reorder transform",
                [&](auto &doc) { doc.move_transform(session_.selectedFlame(), i, i + 1); }, i + 1,
                false);
        });
    });
    addFinal_ = transform->addAction("Add final transform", this, [this] {
        run([&] {
            session_.change(
                "Add final transform",
                [&](auto &doc) { doc.add_transform(session_.selectedFlame(), Transform{}, true); }, 0,
                true);
        });
    });
    addFinal_->setObjectName("actionAddFinal");
    auto toolbar = addToolBar("Main");
    toolbar->setObjectName("mainToolbar");
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolbar->addAction(open);
    toolbar->addAction(save_);
    toolbar->addSeparator();
    auto toolbarUndo = toolbar->addAction("Undo", &session_.undoStack(), &QUndoStack::undo);
    auto toolbarRedo = toolbar->addAction("Redo", &session_.undoStack(), &QUndoStack::redo);
    toolbarUndo->setEnabled(false);
    toolbarRedo->setEnabled(false);
    connect(&session_.undoStack(), &QUndoStack::canUndoChanged, toolbarUndo, &QAction::setEnabled);
    connect(&session_.undoStack(), &QUndoStack::canRedoChanged, toolbarRedo, &QAction::setEnabled);
    connect(undo, &QAction::changed, toolbarUndo,
            [undo, toolbarUndo] { toolbarUndo->setToolTip(undo->text()); });
    connect(redo, &QAction::changed, toolbarRedo,
            [redo, toolbarRedo] { toolbarRedo->setToolTip(redo->text()); });
    toolbar->addSeparator();
    toolbar->addAction(render_);
    toolbar->addSeparator();
    pause_ = toolbar->addAction(style()->standardIcon(QStyle::SP_MediaPause), "Pause preview");
    pause_->setCheckable(true);
    pause_->setObjectName("actionPause");
    connect(pause_, &QAction::toggled, &preview_, &PreviewController::setPaused);
    toolbar->addAction(style()->standardIcon(QStyle::SP_BrowserReload), "Restart", this, [this] {
        pause_->setChecked(false);
        preview_.restart();
    });
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
    library_ = new QListWidget;
    library_->setObjectName("flameLibrary");
    library_->setIconSize({64, 48});
    layout->addWidget(library_);
    connect(librarySearch_, &QLineEdit::textChanged, this, &MainWindow::filterLibrary);
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
    connect(actual, &QPushButton::clicked, previewCanvas_, &PreviewCanvas::actualPixels);
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
    tools->addWidget(add);
    tools->addWidget(duplicate);
    tools->addWidget(remove);
    tools->addStretch();
    connect(add, &QPushButton::clicked, add_, &QAction::trigger);
    connect(duplicate, &QPushButton::clicked, duplicate_, &QAction::trigger);
    connect(remove, &QPushButton::clicked, remove_, &QAction::trigger);
    connect(add_, &QAction::changed, add, [this, add] { add->setEnabled(add_->isEnabled()); });
    connect(duplicate_, &QAction::changed, duplicate,
            [this, duplicate] { duplicate->setEnabled(duplicate_->isEnabled()); });
    connect(remove_, &QAction::changed, remove,
            [this, remove] { remove->setEnabled(remove_->isEnabled()); });
    lock_ = new QCheckBox("Lock");
    lock_->setToolTip("Lock the selected transform while inspecting it");
    tools->addWidget(lock_);
    layout->addLayout(tools);
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
    footer->addWidget(fit);
    footer->addStretch();
    coordinatesLabel_ = new QLabel("x 0.0000    y 0.0000");
    footer->addWidget(coordinatesLabel_);
    layout->addLayout(footer);
    connect(fit, &QPushButton::clicked, triangles_, &TriangleCanvas::fit);
    connect(grid_, &QCheckBox::toggled, triangles_, &TriangleCanvas::setGrid);
    connect(snap_, &QCheckBox::toggled, triangles_, &TriangleCanvas::setSnap);
    connect(spacing, &QDoubleSpinBox::valueChanged, triangles_, &TriangleCanvas::setGridSpacing);
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
    connect(matrixMode_, &QComboBox::currentIndexChanged, this, [this](int index) {
        triangles_->setPost(index == 1);
        refreshTransform();
    });
    connect(triangles_, &TriangleCanvas::selected, &session_, &DocumentSession::selectTransform);
    connect(triangles_, &TriangleCanvas::coordinateChanged, this, [this](double x, double y) {
        coordinatesLabel_->setText(QString("x %1    y %2").arg(x, 0, 'f', 4).arg(y, 0, 'f', 4));
    });
    connect(triangles_, &TriangleCanvas::affinePreview, this, [this](const Affine &affine) {
        if (!model_)
            return;
        setCoefficients(affine);
        auto draft = *model_;
        auto &transform = session_.isFinalTransform()
                              ? draft.final_transform.value()
                              : draft.transforms.at(session_.selectedTransform());
        (matrixMode_->currentIndex() == 1 ? transform.post : transform.pre) = affine;
        preview_.request(draft, previewCanvas_->size());
    });
    connect(triangles_, &TriangleCanvas::affineCommitted, this, &MainWindow::commitAffine);
    connect(triangles_, &TriangleCanvas::editCanceled, this, [this] {
        refreshTransform();
        requestPreview();
    });
    transformDock_->setWidget(body);
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
            session_.change("Rename flame",
                            [&](auto &doc) { doc.edit(session_.selectedFlame(), edits); });
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
            session_.change("Resize output", [&](auto &d) { d.edit(session_.selectedFlame(), e); });
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
            session_.change("Move camera", [&](auto &d) { d.edit(session_.selectedFlame(), e); });
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
        coefficients_[i] = numeric(QString("coefficient_%1").arg(names[i]), i == 0 || i == 3 ? 1 : 0);
        const int column = i / 2, row = i % 2;
        auto cell = new QVBoxLayout;
        cell->setSpacing(2);
        cell->addWidget(new QLabel(names[i]));
        cell->addWidget(coefficients_[i]);
        matrixLayout->addLayout(cell, row, column);
        numberEdited(coefficients_[i], this, [this] {
            run([&] {
                commitAffine({readNumber(coefficients_[0]), readNumber(coefficients_[1]),
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
            applyGeometry("Move transform",
                          {1, 0, 0, 1, readNumber(translateX_), readNumber(translateY_)});
        });
    });
    auto flipX = new QPushButton("Flip X");
    auto flipY = new QPushButton("Flip Y");
    op->addWidget(flipX, 6, 0);
    op->addWidget(flipY, 6, 1);
    connect(flipX, &QPushButton::clicked, this,
            [this] { run([&] { applyGeometry("Flip transform horizontally", {-1, 0, 0, 1, 0, 0}); }); });
    connect(flipY, &QPushButton::clicked, this,
            [this] { run([&] { applyGeometry("Flip transform vertically", {1, 0, 0, -1, 0, 0}); }); });
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
    variations_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    variations_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    int row = 0;
    for (const auto &v : variation_registry()) {
        auto item = new QTableWidgetItem(text(v.name));
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        variations_->setItem(row, 0, item);
        variations_->setItem(row, 1, new QTableWidgetItem("0"));
        ++row;
    }
    variationLayout->addWidget(variations_, 1);
    connect(search, &QLineEdit::textChanged, this, [this](const QString &query) {
        for (int i = 0; i < variations_->rowCount(); ++i)
            variations_->setRowHidden(
                i, !variations_->item(i, 0)->text().contains(query, Qt::CaseInsensitive));
    });
    connect(variations_, &QTableWidget::cellChanged, this, [this](int row, int column) {
        if (refreshing_ || column != 1 || !model_)
            return;
        run([&] {
            bool ok = false;
            const double value = QLocale::c().toDouble(variations_->item(row, 1)->text(), &ok);
            if (!ok || !std::isfinite(value))
                throw std::runtime_error("Variation weight must be a finite number");
            auto terms = selectedTransform().variations;
            const auto id = variation_registry()[row].id;
            auto term =
                std::find_if(terms.begin(), terms.end(), [&](const Term &t) { return t.id == id; });
            if (term != terms.end())
                term->weight = value;
            else if (value != 0)
                terms.push_back({id, value});
            LegacyTransformEdits edit;
            edit.variations = std::move(terms);
            editTransform("Change variation weight", edit);
        });
    });
    auto variables = new QFormLayout;
    power_ = numeric("julianPower", 1);
    distance_ = numeric("julianDistance", 1);
    variables->addRow("JuliaN power", power_);
    variables->addRow("JuliaN distance", distance_);
    variationLayout->addLayout(variables);
    numberEdited(power_, this, [this] {
        run([&] {
            const double value = readNumber(power_);
            if (value == 0 || value != std::trunc(value) || value < INT_MIN || value > INT_MAX)
                throw std::runtime_error("JuliaN power must be a nonzero 32-bit integer");
            LegacyTransformEdits e;
            e.julian_power = int(value);
            editTransform("Change JuliaN power", e);
        });
    });
    numberEdited(distance_, this, [this] {
        run([&] {
            LegacyTransformEdits e;
            e.julian_dist = readNumber(distance_);
            editTransform("Change JuliaN distance", e);
        });
    });
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
    colorSpeed_->setToolTip("0 keeps the incoming color. 1 adopts this transform's color.");
    opacity_->setToolTip("Visibility from 0 to 1, interpreted using the original producer's convention");
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
                    throw std::runtime_error("Xaos multipliers must be finite and nonnegative");
                values.push_back(v);
            }
            LegacyTransformEdits e;
            e.chaos = std::move(values);
            editTransform("Change transition weights", e);
        });
    });
    propertiesDock_->setWidget(body);
    addDockWidget(Qt::RightDockWidgetArea, propertiesDock_);
}

void MainWindow::makeLogs() {
    logDock_ = new QDockWidget("Activity", this);
    logDock_->setObjectName("activityDock");
    auto body = new QWidget;
    auto layout = new QVBoxLayout(body);
    auto row = new QHBoxLayout;
    exportStatus_ = new QLabel("Render to disk is idle");
    row->addWidget(exportStatus_, 1);
    exportProgress_ = new QProgressBar;
    exportProgress_->setRange(0, 100);
    exportProgress_->hide();
    row->addWidget(exportProgress_);
    auto cancel = new QPushButton("Cancel render");
    row->addWidget(cancel);
    connect(cancel, &QPushButton::clicked, &exporter_, &PreviewController::stop);
    connect(&exporter_, &PreviewController::busyChanged, cancel, &QPushButton::setEnabled);
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
    const int count = session_.hasDocument() ? int(session_.document().entries().size()) : 0;
    if (library_->count() != count) {
        library_->clear();
        for (int i = 0; i < count; ++i)
            library_->addItem(new QListWidgetItem);
    }
    for (int i = 0; i < count; ++i) {
        const auto &entry = session_.document().entries()[i];
        auto item = library_->item(i);
        item->setText(entry.name.empty() ? QString("Untitled %1").arg(i + 1) : text(entry.name));
        const auto issue = session_.document().render_issue(i);
        item->setToolTip(text(entry.version) +
                         (issue ? "\nPreserved · " + text(*issue) : "\nCPU preview available"));
        item->setForeground(issue ? QColor("#d3af89") : palette().color(QPalette::Text));
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
    name_->setEnabled(available);
    for (auto field : {centerX_, centerY_, scale_})
        field->setEnabled(available);
    width_->setEnabled(available);
    height_->setEnabled(available);
    const QSignalBlocker lockBlock(lock_);
    lock_->setChecked(false);
    triangles_->setLocked(false);
    if (available) {
        const auto &entry = session_.document().entries().at(session_.selectedFlame());
        name_->setText(text(entry.name));
        producer_->setText(entry.version.empty() ? "Generic flame · CPU renderer" : text(entry.version));
        const auto issue = session_.document().render_issue(session_.selectedFlame());
        diagnostic_->setVisible(issue.has_value());
        diagnostic_->setText(issue ? "Preservation only\n" + text(*issue) : QString{});
        if (!issue)
            model_ = session_.document().render_model(session_.selectedFlame());
        if (model_) {
            width_->setValue(model_->width);
            height_->setValue(model_->height);
            setNumber(centerX_, model_->center.x);
            setNumber(centerY_, model_->center.y);
            setNumber(scale_, model_->scale);
        } else {
            // Header fields can still be read and edited while a plugin prevents rendering.
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
        int row =
            session_.isFinalTransform() ? int(model_->transforms.size()) : session_.selectedTransform();
        if (row >= transforms_->count() || row < 0)
            row = 0;
        transforms_->setCurrentIndex(row);
    }
    transforms_->setEnabled(model_.has_value());
    matrixMode_->setEnabled(model_.has_value());
    lock_->setEnabled(model_.has_value());
    render_->setEnabled(model_.has_value() && !exporter_.isBusy());
    properties_->setEnabled(model_.has_value());
    refreshing_ = false;
    refreshTransform();
    requestPreview();
}

const Transform &MainWindow::selectedTransform() const {
    return session_.isFinalTransform() ? model_->final_transform.value()
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
    remove_->setEnabled(editable && (final || (model_ && model_->transforms.size() > 1)));
    moveUp_->setEnabled(editable && !final && index > 0);
    moveDown_->setEnabled(editable && !final && index + 1 < int(model_ ? model_->transforms.size() : 0));
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
        setNumber(power_, t.parameters.julian_power);
        setNumber(distance_, t.parameters.julian_dist);
        bool hasJuliaN = false;
        for (int row = 0; row < variations_->rowCount(); ++row) {
            const auto id = variation_registry()[row].id;
            auto term = std::find_if(t.variations.begin(), t.variations.end(),
                                     [&](const Term &v) { return v.id == id; });
            const double weight = term == t.variations.end() ? 0 : term->weight;
            variations_->item(row, 1)->setText(number(weight));
            variations_->item(row, 0)->setForeground(weight ? QColor("#76d5dd")
                                                            : palette().color(QPalette::Text));
            if (id == Variation::julian && weight != 0)
                hasJuliaN = true;
        }
        power_->setEnabled(hasJuliaN);
        distance_->setEnabled(hasJuliaN);
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
            chaos_->setItem(
                i, 1, new QTableWidgetItem(number(std::size_t(i) < t.chaos.size() ? t.chaos[i] : 1)));
        }
    }
    triangles_->setFlame(model_, index, final);
    refreshing_ = false;
}
void MainWindow::requestPreview() {
    if (model_) {
        preview_.request(*model_, previewCanvas_->size());
        return;
    }
    preview_.stop();
    stats_->setText("CPU preview unavailable");
    previewCanvas_->setMessage(session_.hasDocument()
                                   ? "This flame is preserved with its original settings.\n\n" +
                                         diagnostic_->text()
                                   : "Open a flame library to begin");
}
void MainWindow::editTransform(const QString &label, const LegacyTransformEdits &edit) {
    if (!model_ || lock_->isChecked())
        return;
    session_.change(label, [&](auto &document) {
        document.edit_transform(session_.selectedFlame(), session_.selectedTransform(), edit,
                                session_.isFinalTransform());
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
        compose(Affine{1, 0, 0, 1, px, py}, compose(operation, Affine{1, 0, 0, 1, -px, -py}));
    const auto &t = selectedTransform();
    const auto &current = matrixMode_->currentIndex() == 1 ? t.post : t.pre;
    const auto result =
        coordinates_->currentIndex() == 0 ? compose(aboutPivot, current) : compose(current, aboutPivot);
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
    auto title = session_.path().isEmpty() ? "Untitled library" : QFileInfo(session_.path()).fileName();
    setWindowModified(session_.isModified());
    setWindowTitle(title + "[*] — Apophysis Reborn");
}
void MainWindow::record(const QString &message) {
    if (!log_)
        return;
    log_->appendPlainText(QDateTime::currentDateTime().toString("HH:mm:ss") + "  " + message);
    if (!useSettings_)
        return;
    const auto directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(directory);
    const auto path = directory + "/editor.log";
    if (QFileInfo(path).size() > 2 * 1024 * 1024) {
        QFile::remove(path + ".1");
        QFile::rename(path, path + ".1");
    }
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append))
        file.write((QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs) + "\t" + message + "\n")
                       .toUtf8());
}
void MainWindow::run(const std::function<void()> &operation) {
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
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (choice == QMessageBox::Cancel)
        return false;
    if (choice == QMessageBox::Save)
        return saveDocument();
    return true;
}
bool MainWindow::openFile(const QString &path) {
    if (!confirmDiscard())
        return false;
    try {
        session_.load(path);
        for (int i = 0; i < library_->count(); ++i)
            library_->item(i)->setIcon({});
        librarySearch_->clear();
        triangles_->fit();
        previewCanvas_->fit();
        record("Opened " + path);
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
    const auto path =
        QFileDialog::getOpenFileName(this, "Open flame library", session_.path(),
                                     "Flame libraries (*.flame *.xml *.temp);;All files (*)");
    if (!path.isEmpty())
        openFile(path);
}
void MainWindow::openExample() {
    QFile file(":/examples/ember-spiral.flame");
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error("The built-in example is missing");
    session_.loadBytes(file.readAll().toStdString());
    triangles_->fit();
}
void MainWindow::newDocument() {
    if (!confirmDiscard())
        return;
    run([&] {
        auto flame = demo_flame();
        flame.name = "Untitled flame";
        session_.loadBytes(serialize_flame(flame), {}, true);
        triangles_->fit();
        previewCanvas_->fit();
    });
}
bool MainWindow::saveDocument(bool saveAs) {
    if (!session_.hasDocument())
        return false;
    QString path = session_.path();
    if (saveAs || path.isEmpty())
        path = QFileDialog::getSaveFileName(this, "Save flame library",
                                            path.isEmpty() ? "my-flames.flame" : path,
                                            "Flame libraries (*.flame);;XML files (*.xml)");
    if (path.isEmpty())
        return false;
    try {
        session_.save(path);
        record("Saved " + path);
        return true;
    } catch (const FileConflict &) {
        QMessageBox box(QMessageBox::Warning, "File changed on disk",
                        "Another change was made to this file after you opened it.",
                        QMessageBox::NoButton, this);
        auto copy = box.addButton("Save a copy…", QMessageBox::AcceptRole);
        auto replace = box.addButton("Replace disk version", QMessageBox::DestructiveRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() == copy)
            return saveDocument(true);
        if (box.clickedButton() == replace) {
            try {
                session_.save(path, true);
                record("Replaced disk version " + path);
                return true;
            } catch (const std::exception &e) {
                QMessageBox::warning(this, "Save failed", text(e.what()));
            }
        }
    } catch (const std::exception &e) {
        record(text(e.what()));
        QMessageBox::warning(this, "Save failed", text(e.what()));
    }
    return false;
}

void MainWindow::renderImage() {
    if (!model_ || exporter_.isBusy())
        return;
    QDialog dialog(this);
    dialog.setWindowTitle("Render image");
    auto layout = new QVBoxLayout(&dialog);
    auto form = new QFormLayout;
    form->addRow(new QLabel(QString("%1 × %2 pixels").arg(model_->width).arg(model_->height)));
    auto samples = new QLineEdit;
    auto threads = new QSpinBox;
    auto memory = new QSpinBox;
    std::uint64_t budget = 2000000;
    try {
        budget = default_samples(*model_);
    } catch (const std::exception &) {
    }
    samples->setText(QString::number(budget));
    threads->setRange(0, 256);
    threads->setSpecialValueText("Automatic");
    threads->setValue(preview_.settings().threads);
    memory->setRange(16, 1048576);
    memory->setValue(512);
    memory->setSuffix(" MiB");
    form->addRow("Samples", samples);
    form->addRow("CPU workers", threads);
    form->addRow("Buffer budget", memory);
    layout->addLayout(form);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
        return;
    run([&] {
        bool valid = false;
        const auto count = samples->text().toULongLong(&valid);
        if (!valid || count == 0)
            throw std::runtime_error("Enter a positive sample count");
        const auto path = QFileDialog::getSaveFileName(this, "Render image", "render.bmp",
                                                       "Bitmap (*.bmp);;Portable pixmap (*.ppm)");
        if (path.isEmpty())
            return;
        const auto suffix = QFileInfo(path).suffix().toLower();
        if (suffix != "bmp" && suffix != "ppm")
            throw std::runtime_error("Use a .bmp or .ppm filename");
        if (!session_.path().isEmpty() && QFileInfo(path).exists() &&
            std::filesystem::equivalent(filesystemPath(path), filesystemPath(session_.path())))
            throw std::runtime_error("Choose a different filename for the rendered image");
        RenderOptions options;
        options.samples = count;
        options.seed = preview_.settings().seed;
        options.threads = unsigned(threads->value());
        options.memory_mib = std::uint64_t(memory->value());
        exporter_.requestFileRender(*model_, path, options);
        logDock_->show();
        logDock_->raise();
        record("Started render " + path);
    });
}

void MainWindow::preferences() {
    QDialog dialog(this);
    dialog.setWindowTitle("Preview preferences");
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
    auto limit = new QLineEdit(QString::number(preview_.settings().sample_limit));
    form->addRow("CPU workers", threads);
    form->addRow("Buffer budget", memory);
    form->addRow("Random seed", seed);
    form->addRow("Final preview samples", limit);
    layout->addLayout(form);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
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
            throw std::runtime_error("Use an unsigned integer seed and a positive sample limit");
        s.threads = unsigned(threads->value());
        s.memory_mib = std::uint64_t(memory->value());
        preview_.setSettings(s);
        if (useSettings_) {
            settings_.setValue("preview/threads", s.threads);
            settings_.setValue("preview/memory", qulonglong(s.memory_mib));
            settings_.setValue("preview/seed", qulonglong(s.seed));
            settings_.setValue("preview/samples", qulonglong(s.sample_limit));
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
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/recovery";
    QDir folder(directory);
    const auto files = folder.entryInfoList({"*.flame"}, QDir::Files, QDir::Time);
    for (const auto &candidate : files) {
        QLockFile lock(candidate.absoluteFilePath() + ".lock");
        lock.setStaleLockTime(0);
        if (!lock.tryLock())
            continue;
        lock.unlock();
        QMessageBox dialog(
            QMessageBox::Question, "Recover editing session",
            "A recovery copy from an interrupted session was found. Open it as an unsaved library?",
            QMessageBox::NoButton, this);
        auto recover = dialog.addButton("Recover edits", QMessageBox::AcceptRole);
        auto discard = dialog.addButton("Discard recovery copy", QMessageBox::DestructiveRole);
        dialog.addButton("Later", QMessageBox::RejectRole);
        dialog.exec();
        if (dialog.clickedButton() == recover)
            run([&] {
                session_.recover(candidate.absoluteFilePath());
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
    if (exporter_.isBusy()) {
        const auto answer = QMessageBox::question(this, "Render in progress",
                                                  "Cancel the current image render and close?",
                                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            event->ignore();
            return;
        }
    }
    if (!confirmDiscard()) {
        event->ignore();
        return;
    }
    if (useSettings_) {
        settings_.setValue("window/geometry", saveGeometry());
        settings_.setValue("window/layout", saveState(1));
        settings_.setValue("preview/samples", qulonglong(preview_.settings().sample_limit));
    }
    preview_.stop();
    exporter_.stop();
    session_.discardRecovery();
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
