#include "support_dialogs.hpp"
#include "build_info.hpp"
#include "document_session.hpp"
#include "output_file.hpp"
#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShortcut>
#include <QSysInfo>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>
#include <stdexcept>

// Referencing the resource explicitly also links it from the static editor
// library into every executable that opens this dialog.
static void initialize_support_resources() {
  Q_INIT_RESOURCE(support_resources);
}

namespace apo::editor {
UserManualDialog::UserManualDialog(QWidget *parent) : QDialog(parent) {
  initialize_support_resources();
  setObjectName("userManualDialog");
  setWindowTitle("Apophysis Reborn — User manual");
  resize(920, 740);
  auto layout = new QVBoxLayout(this);
  auto row = new QHBoxLayout;
  search_ = new QLineEdit;
  search_->setObjectName("manualSearch");
  search_->setPlaceholderText("Find in manual…");
  search_->setAccessibleName("Find in user manual");
  auto previous = new QPushButton("Previous");
  auto next = new QPushButton("Next");
  previous->setObjectName("manualPrevious");
  next->setObjectName("manualNext");
  row->addWidget(search_, 1); row->addWidget(previous); row->addWidget(next);
  layout->addLayout(row);
  browser_ = new QTextBrowser;
  browser_->setObjectName("manualBrowser");
  browser_->setOpenLinks(false);
  browser_->setOpenExternalLinks(false);
  auto colors = browser_->palette();
  colors.setColor(QPalette::Link, QColor("#63c1dc"));
  colors.setColor(QPalette::LinkVisited, QColor("#c3a4ed"));
  browser_->setPalette(colors);
  // Rich-text HTML otherwise uses its built-in blue link color, which is
  // difficult to read against the editor's dark document background.
  browser_->document()->setDefaultStyleSheet(
      QString("a { color: %1; }").arg(colors.color(QPalette::Link).name()));
  QFile source(":/help/USER_MANUAL.html");
  if (!source.open(QIODevice::ReadOnly))
    throw std::runtime_error("The embedded user manual is unavailable");
  browser_->setHtml(QString::fromUtf8(source.readAll()));
  layout->addWidget(browser_, 1);
  status_ = new QLabel("Offline manual · Ctrl+F to find text");
  status_->setObjectName("manualSearchStatus");
  layout->addWidget(status_);
  auto buttons = new QDialogButtonBox(QDialogButtonBox::Close);
  buttons->button(QDialogButtonBox::Close)->setAutoDefault(false);
  buttons->button(QDialogButtonBox::Close)->setDefault(false);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(next, &QPushButton::clicked, this, [this] { findText(false); });
  connect(previous, &QPushButton::clicked, this, [this] { findText(true); });
  connect(search_, &QLineEdit::returnPressed, this, [this] { findText(false); });
  auto find = new QShortcut(QKeySequence::Find, this);
  connect(find, &QShortcut::activated, this, [this] { search_->setFocus(); search_->selectAll(); });
  connect(browser_, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
    if (url.scheme().isEmpty() && url.host().isEmpty() && url.path().isEmpty() &&
        url.query().isEmpty() && !url.fragment().isEmpty())
      browser_->scrollToAnchor(url.fragment());
  });
  next->setAutoDefault(false); previous->setAutoDefault(false);
}

void UserManualDialog::findText(bool backwards) {
  if (search_->text().isEmpty()) { status_->setText("Enter text to find."); return; }
  const auto flags = backwards ? QTextDocument::FindBackward : QTextDocument::FindFlags{};
  bool found = browser_->find(search_->text(), flags);
  if (!found) {
    auto cursor = browser_->textCursor();
    cursor.movePosition(backwards ? QTextCursor::End : QTextCursor::Start);
    browser_->setTextCursor(cursor);
    found = browser_->find(search_->text(), flags);
  }
  status_->setText(found ? "Match selected." : "No matching text in the manual.");
}

QJsonObject system_information(const PreviewSettings &settings,
                               const QString &actualBackend,
                               const QString &actualDevice,
                               const QString &fallbackReason) {
  const auto build = QJsonDocument::fromJson(QByteArray::fromStdString(build_info_json())).object();
  return {{"schema", "apophysis-reborn.system-information"}, {"schema_version", 1},
          {"build", build},
          {"runtime", QJsonObject{{"qt_version", qVersion()},
                                  {"qt_platform", QGuiApplication::platformName()},
                                  {"operating_system", QSysInfo::prettyProductName()},
                                  {"kernel_type", QSysInfo::kernelType()},
                                  {"kernel_version", QSysInfo::kernelVersion()},
                                  {"cpu_architecture", QSysInfo::currentCpuArchitecture()},
                                  {"logical_cpu_threads", QThread::idealThreadCount()}}},
          {"preview", QJsonObject{{"requested_backend", QString::fromUtf8(backend_name(settings.backend).data(), qsizetype(backend_name(settings.backend).size()))},
                                  {"requested_device", QString::fromStdString(settings.device)},
                                  {"cpu_fallback_allowed", settings.allow_cpu_fallback},
                                  {"workers", int(settings.threads)},
                                  {"memory_mib", QString::number(settings.memory_mib)},
                                  {"gpu_memory_mib", QString::number(settings.gpu_memory_mib)},
                                  {"sample_limit", QString::number(settings.sample_limit)},
                                  {"last_reported_backend", actualBackend},
                                  {"last_reported_device", actualDevice},
                                  {"last_fallback_reason", fallbackReason}}}};
}

QString system_information_text(const QJsonObject &report) {
  const auto runtime = report.value("runtime").toObject();
  const auto preview = report.value("preview").toObject();
  QString text = QString::fromStdString(build_info_text());
  text += "\nRuntime\nQt: " + runtime.value("qt_version").toString() +
          " (" + runtime.value("qt_platform").toString() + ")\nOS: " +
          runtime.value("operating_system").toString() + "\nKernel: " +
          runtime.value("kernel_type").toString() + " " + runtime.value("kernel_version").toString() +
          "\nCPU architecture: " + runtime.value("cpu_architecture").toString() +
          "\nLogical CPU threads: " + QString::number(runtime.value("logical_cpu_threads").toInt()) +
          "\n\nPreview configuration\nRequested backend: " + preview.value("requested_backend").toString() +
          "\nRequested device: " + (preview.value("requested_device").toString().isEmpty() ? "automatic" : preview.value("requested_device").toString()) +
          "\nCPU fallback allowed: " + (preview.value("cpu_fallback_allowed").toBool() ? "yes" : "no") +
          "\nWorkers: " + QString::number(preview.value("workers").toInt()) + " (0 selects automatically)" +
          "\nHost / GPU buffer budgets: " + preview.value("memory_mib").toString() + " / " +
          preview.value("gpu_memory_mib").toString() + " MiB\nPreview sample limit: " +
          preview.value("sample_limit").toString() + "\nLast reported preview backend: " +
          preview.value("last_reported_backend").toString() + "\nLast reported preview device: " +
          preview.value("last_reported_device").toString() + "\nLast fallback reason: " +
          preview.value("last_fallback_reason").toString() +
          "\n\nDevice inventory: run flame-cpu --list-devices --json.\n";
  return text;
}

SystemInformationDialog::SystemInformationDialog(QJsonObject report, QWidget *parent)
    : QDialog(parent), report_(std::move(report)) {
  setObjectName("systemInformationDialog");
  setWindowTitle("Apophysis Reborn — System information");
  resize(820, 640);
  auto layout = new QVBoxLayout(this);
  auto label = new QLabel("Build identity, runtime configuration and the last preview result.");
  label->setWordWrap(true); layout->addWidget(label);
  text_ = new QPlainTextEdit(system_information_text(report_));
  text_->setObjectName("systemInformationText");
  text_->setReadOnly(true);
  layout->addWidget(text_, 1);
  auto buttons = new QDialogButtonBox(QDialogButtonBox::Close);
  auto copy = buttons->addButton("Copy report", QDialogButtonBox::ActionRole);
  auto save = buttons->addButton("Save report…", QDialogButtonBox::ActionRole);
  copy->setObjectName("copySystemInformation"); save->setObjectName("saveSystemInformation");
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(copy, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(text_->toPlainText()); });
  connect(save, &QPushButton::clicked, this, [this] {
    QString filter = "Text report (*.txt)";
    auto path = QFileDialog::getSaveFileName(this, "Save system information", "apophysis-system-information.txt",
                                           "Text report (*.txt);;JSON report (*.json)", &filter);
    if (path.isEmpty()) return;
    const bool json = filter.startsWith("JSON");
    if (QFileInfo(path).suffix().isEmpty()) path += json ? ".json" : ".txt";
    try { saveReport(path, json); }
    catch (const std::exception &error) { QMessageBox::warning(this, "Could not save report", QString::fromUtf8(error.what())); }
  });
}

void SystemInformationDialog::saveReport(const QString &path, bool json) const {
  const QByteArray data = json ? QJsonDocument(report_).toJson(QJsonDocument::Indented)
                              : system_information_text(report_).toUtf8();
  write_atomic(filesystemPath(path), [&](std::ostream &stream) {
    stream.write(data.constData(), data.size());
  });
}
} // namespace apo::editor
