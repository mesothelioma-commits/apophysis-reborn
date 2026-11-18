#include "plugin_dialog.hpp"
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace apo::editor {
namespace {
namespace fs = std::filesystem;
QString text(std::string_view value) {
    return QString::fromUtf8(value.data(), qsizetype(value.size()));
}
fs::path path(const QString &value) {
#ifdef _WIN32
    return fs::path(value.toStdWString());
#else
    return fs::path(value.toUtf8().constData());
#endif
}
QString pathText(const fs::path &value) {
#ifdef _WIN32
    return QString::fromStdWString(value.native());
#else
    const auto utf8 = value.u8string();
    return QString::fromUtf8(reinterpret_cast<const char *>(utf8.data()), qsizetype(utf8.size()));
#endif
}
QString canonical(const QString &value) {
    const QFileInfo info(value);
    return info.canonicalFilePath().isEmpty() ? info.absoluteFilePath() : info.canonicalFilePath();
}
QString installationDirectory() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
        .filePath("plugins");
}
QStringList defaultSearchPaths() {
    return {installationDirectory(),
            QDir(QCoreApplication::applicationDirPath()).filePath("plugins")};
}
QMap<QString, QString> approvals(QSettings &settings) {
    QMap<QString, QString> result;
    const int count = settings.beginReadArray("plugins/approved");
    for (int i = 0; i < std::min(count, 1024); ++i) {
        settings.setArrayIndex(i);
        const auto manifest = settings.value("manifest").toString();
        if (!manifest.isEmpty())
            result.insert(canonical(manifest), settings.value("fingerprint").toString());
    }
    settings.endArray();
    return result;
}
QString parameterDetails(const PluginInfo &info) {
    QStringList lines;
    for (const auto &p : info.parameters)
        lines << QString("%1 — %2; default %3; range [%4, %5]")
                     .arg(text(p.name), p.integer ? "integer" : "real",
                          QString::number(p.default_value, 'g', 17),
                          QString::number(p.minimum, 'g', 17), QString::number(p.maximum, 'g', 17));
    return lines.isEmpty() ? "No parameters" : lines.join('\n');
}
} // namespace

QStringList loadApprovedPlugins(QSettings &settings) {
    QStringList warnings;
    static const QRegularExpression hash("^[0-9a-f]{64}$");
    for (auto saved = approvals(settings); const auto &manifest : saved.keys()) {
        try {
            const auto fingerprint = saved.value(manifest);
            if (!hash.match(fingerprint).hasMatch())
                throw std::runtime_error("The saved approval has no valid SHA-256 fingerprint; "
                                         "explicitly enable this plugin again");
            enable_plugin(path(manifest), fingerprint.toStdString());
        } catch (const std::exception &e) {
            warnings << manifest + ": " + text(e.what());
        }
    }
    return warnings;
}

PluginDialog::PluginDialog(QSettings &settings, QWidget *parent, bool persist)
    : QDialog(parent), settings_(settings), persist_(persist),
      searchPaths_(settings.value("plugins/searchPaths", defaultSearchPaths()).toStringList()),
      approvals_(approvals(settings)) {
    setObjectName("pluginDialog");
    setWindowTitle("Variation plugins");
    const auto screen = QGuiApplication::primaryScreen();
    resize(QSize(980, 760).boundedTo(screen ? screen->availableGeometry().size() - QSize(32, 64)
                                            : QSize(980, 760)));
    auto layout = new QVBoxLayout(this);
    auto warning =
        new QLabel("Enable only plugins from authors you trust. Native code runs inside Apophysis "
                   "Reborn and can access your files, crash the application or stop responding. "
                   "Discovery and installation do not execute plugins.");
    warning->setWordWrap(true);
    warning->setObjectName("pluginTrustNotice");
    layout->addWidget(warning);
    auto pathLabel = new QLabel("Search folders (the folder and one package level below it):");
    layout->addWidget(pathLabel);
    auto pathRow = new QHBoxLayout;
    paths_ = new QListWidget;
    paths_->setObjectName("pluginSearchPaths");
    paths_->setMaximumHeight(100);
    paths_->setSelectionMode(QAbstractItemView::SingleSelection);
    pathRow->addWidget(paths_, 1);
    auto pathButtons = new QVBoxLayout;
    auto addPath = new QPushButton("Add folder…");
    addPath->setObjectName("pluginAddPath");
    removePath_ = new QPushButton("Remove folder");
    removePath_->setObjectName("pluginRemovePath");
    auto refresh = new QPushButton("Refresh");
    refresh->setObjectName("pluginRefresh");
    pathButtons->addWidget(addPath);
    pathButtons->addWidget(removePath_);
    pathButtons->addWidget(refresh);
    pathRow->addLayout(pathButtons);
    layout->addLayout(pathRow);
    auto split = new QSplitter(Qt::Vertical);
    table_ = new QTableWidget(0, 5);
    table_->setObjectName("pluginTable");
    table_->setHorizontalHeaderLabels(
        {"Variation", "Version", "Status", "Description", "Manifest"});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->verticalHeader()->hide();
    table_->setWordWrap(false);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Interactive);
    table_->setColumnWidth(4, 240);
    split->addWidget(table_);
    details_ = new QPlainTextEdit;
    details_->setObjectName("pluginDetails");
    details_->setReadOnly(true);
    details_->setPlaceholderText(
        "Select a plugin to inspect its identity, fingerprint, parameters and any errors.");
    split->addWidget(details_);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    layout->addWidget(split, 1);
    auto actions = new QHBoxLayout;
    auto install = new QPushButton("Install…");
    install->setObjectName("pluginInstall");
    enable_ = new QPushButton("Enable…");
    enable_->setObjectName("pluginEnable");
    disable_ = new QPushButton("Disable");
    disable_->setObjectName("pluginDisable");
    remove_ = new QPushButton("Remove files…");
    remove_->setObjectName("pluginRemove");
    actions->addWidget(install);
    actions->addWidget(enable_);
    actions->addWidget(disable_);
    actions->addWidget(remove_);
    actions->addStretch();
    layout->addLayout(actions);
    status_ = new QLabel;
    status_->setObjectName("pluginStatus");
    status_->setWordWrap(true);
    layout->addWidget(status_);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    layout->addWidget(buttons);
    connect(addPath, &QPushButton::clicked, this, &PluginDialog::addSearchPath);
    connect(removePath_, &QPushButton::clicked, this, &PluginDialog::removeSearchPath);
    connect(refresh, &QPushButton::clicked, this, &PluginDialog::refreshPlugins);
    connect(paths_, &QListWidget::currentRowChanged, this,
            [this] { removePath_->setEnabled(paths_->currentRow() >= 0); });
    connect(table_, &QTableWidget::itemSelectionChanged, this, &PluginDialog::selectionChanged);
    connect(install, &QPushButton::clicked, this, &PluginDialog::installPlugin);
    connect(enable_, &QPushButton::clicked, this, &PluginDialog::enableSelected);
    connect(disable_, &QPushButton::clicked, this, &PluginDialog::disableSelected);
    connect(remove_, &QPushButton::clicked, this, &PluginDialog::removeSelected);
    searchPaths_.removeDuplicates();
    paths_->addItems(searchPaths_);
    refreshPlugins();
}

void PluginDialog::savePreferences() {
    if (!persist_)
        return;
    settings_.setValue("plugins/searchPaths", searchPaths_);
    settings_.beginWriteArray("plugins/approved", approvals_.size());
    int index = 0;
    for (auto it = approvals_.cbegin(); it != approvals_.cend(); ++it) {
        settings_.setArrayIndex(index++);
        settings_.setValue("manifest", it.key());
        settings_.setValue("fingerprint", it.value());
    }
    settings_.endArray();
    settings_.sync();
    if (settings_.status() != QSettings::NoError)
        status_->setText(
            "The runtime change succeeded, but plugin preferences could not be saved.");
}
void PluginDialog::refreshPlugins() {
    QString selectedPath;
    if (const auto row = selected())
        selectedPath = canonical(pathText(row->discovered.path));
    const auto active = enabled_plugins();
    auto scan = searchPaths_;
    scan += approvals_.keys();
    for (const auto &info : active)
        scan << pathText(info.manifest_path);
    scan.removeDuplicates();
    std::vector<fs::path> folders;
    for (const auto &folder : scan) {
        // The per-user installation folder is created by Install on demand.
        // Its absence on first launch is not an invalid plugin installation.
        if (canonical(folder) == canonical(installationDirectory()) && !QFileInfo::exists(folder))
            continue;
        folders.push_back(path(folder));
    }
    rows_.clear();
    try {
        for (auto item : discover_plugins(folders)) {
            Row row{std::move(item), {}};
            const auto manifest = canonical(pathText(row.discovered.path));
            const auto loaded = std::find_if(active.begin(), active.end(), [&](const auto &info) {
                return canonical(pathText(info.manifest_path)) == manifest;
            });
            if (loaded != active.end())
                row.active = *loaded;
            rows_.push_back(std::move(row));
        }
        status_->setText(QString("%1 discovered entries; %2 enabled. Existing render snapshots "
                                 "retain their modules after disabling.")
                             .arg(rows_.size())
                             .arg(active.size()));
    } catch (const std::exception &e) {
        status_->setText("Discovery stopped: " + text(e.what()));
    }
    table_->setRowCount(int(rows_.size()));
    int select = rows_.empty() ? -1 : 0;
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const auto &row = rows_[i];
        const auto &info = row.discovered.info ? row.discovered.info : row.active;
        const bool changed = row.active && (!row.discovered.info || !row.discovered.info->enabled);
        const QString status = row.active ? (changed ? "Enabled; source changed" : "Enabled")
                               : row.discovered.info ? "Disabled"
                                                     : "Unavailable";
        const QStringList columns{
            info ? text(info->name) : QFileInfo(pathText(row.discovered.path)).fileName(),
            info ? text(info->version) : QString{}, status,
            row.discovered.error.empty() ? (info ? text(info->description) : QString{})
                                         : text(row.discovered.error),
            pathText(row.discovered.path)};
        for (int c = 0; c < columns.size(); ++c) {
            auto item = new QTableWidgetItem(columns[c]);
            item->setToolTip(columns[c]);
            table_->setItem(int(i), c, item);
        }
        if (canonical(pathText(row.discovered.path)) == selectedPath)
            select = int(i);
    }
    if (select >= 0)
        table_->selectRow(select);
    selectionChanged();
}
const PluginDialog::Row *PluginDialog::selected() const {
    const auto index = table_->currentRow();
    return index >= 0 && std::size_t(index) < rows_.size() ? &rows_[std::size_t(index)] : nullptr;
}
void PluginDialog::selectionChanged() {
    const auto row = selected();
    enable_->setEnabled(row && row->discovered.info && !row->active);
    disable_->setEnabled(row && row->active.has_value());
    remove_->setEnabled(row && row->discovered.info && !row->active);
    removePath_->setEnabled(paths_->currentRow() >= 0);
    if (!row) {
        details_->clear();
        return;
    }
    const auto &info = row->discovered.info ? row->discovered.info : row->active;
    QString details;
    if (info) {
        details =
            QString("%1 %2\n%3\nID: %4\nManifest: %5\nLibrary: %6\nSHA-256: %7\n\nParameters\n%8")
                .arg(text(info->name), text(info->version), text(info->description), text(info->id),
                     pathText(info->manifest_path), pathText(info->library_path),
                     text(info->fingerprint), parameterDetails(*info));
        if (row->active && !info->enabled)
            details += "\n\nThe active module retains its previously loaded bytes. Disable it and "
                       "close its render snapshots before replacing the files.";
    }
    if (!row->discovered.error.empty())
        details += "\n\nError: " + text(row->discovered.error);
    details_->setPlainText(details.trimmed());
}
void PluginDialog::showError(const QString &title, const std::exception &error) {
    QMessageBox::warning(this, title, text(error.what()));
}
void PluginDialog::addSearchPath() {
    const auto folder = QFileDialog::getExistingDirectory(this, "Add plugin search folder");
    if (folder.isEmpty())
        return;
    const auto resolved = canonical(folder);
    if (!searchPaths_.contains(resolved)) {
        searchPaths_ << resolved;
        paths_->addItem(resolved);
    }
    refreshPlugins();
    savePreferences();
}
void PluginDialog::removeSearchPath() {
    const auto row = paths_->currentRow();
    if (row < 0)
        return;
    searchPaths_.removeAt(row);
    delete paths_->takeItem(row);
    refreshPlugins();
    savePreferences();
}
void PluginDialog::installPlugin() {
    const auto manifest = QFileDialog::getOpenFileName(this, "Install Reborn plugin manifest", {},
                                                       "Reborn plugin (*.apoplugin)");
    if (manifest.isEmpty())
        return;
    try {
        const auto directory = installationDirectory();
        const auto installed = install_plugin(path(manifest), path(directory));
        if (!searchPaths_.contains(directory)) {
            searchPaths_ << directory;
            paths_->addItem(directory);
        }
        refreshPlugins();
        savePreferences();
        status_->setText("Installed " + text(installed.name) +
                         ". Select Enable to review and trust its native code.");
    } catch (const std::exception &e) {
        showError("Plugin installation failed", e);
    }
}
void PluginDialog::enableSelected() {
    const auto row = selected();
    if (!row || !row->discovered.info || row->active)
        return;
    try {
        const auto inspected = inspect_plugin(row->discovered.info->manifest_path);
        QMessageBox prompt(
            QMessageBox::Warning, "Trust native plugin?",
            QString(
                "Enable %1 %2 from %3?\n\nThis executes native code with your application's access "
                "to files and the operating system. It can crash or hang the application. Enable "
                "only if you trust the author and this binary.\n\nLibrary: %4\nSHA-256: %5")
                .arg(text(inspected.name), text(inspected.version), text(inspected.id),
                     pathText(inspected.library_path), text(inspected.fingerprint)),
            QMessageBox::Cancel, this);
        auto trust = prompt.addButton("Trust and enable", QMessageBox::AcceptRole);
        prompt.setDefaultButton(QMessageBox::Cancel);
        prompt.exec();
        if (prompt.clickedButton() != trust)
            return;
        const auto enabled = enable_plugin(inspected.manifest_path, inspected.fingerprint);
        approvals_.insert(canonical(pathText(enabled.manifest_path)), text(enabled.fingerprint));
        refreshPlugins();
        savePreferences();
        emit pluginsChanged();
    } catch (const std::exception &e) {
        showError("Plugin could not be enabled", e);
        refreshPlugins();
    }
}
void PluginDialog::disableSelected() {
    const auto row = selected();
    if (!row || !row->active)
        return;
    const auto info = *row->active;
    disable_plugin(info.name);
    approvals_.remove(canonical(pathText(info.manifest_path)));
    refreshPlugins();
    savePreferences();
    emit pluginsChanged();
}
void PluginDialog::removeSelected() {
    const auto row = selected();
    if (!row || !row->discovered.info || row->active)
        return;
    const auto info = *row->discovered.info;
    const auto answer =
        QMessageBox::question(this, "Remove plugin files?",
                              QString("Remove these two files?\n\n%1\n%2\n\nA plugin retained by "
                                      "an open flame or render job cannot be removed yet.")
                                  .arg(pathText(info.manifest_path), pathText(info.library_path)),
                              QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes)
        return;
    try {
        remove_plugin(info.manifest_path);
        approvals_.remove(canonical(pathText(info.manifest_path)));
        refreshPlugins();
        savePreferences();
        emit pluginsChanged();
    } catch (const std::exception &e) {
        showError("Plugin files could not be removed", e);
    }
}
} // namespace apo::editor
