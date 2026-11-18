#include "script_dialog.hpp"
#include "../output_file.hpp"
#include "document_session.hpp"
#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCompleter>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QSettings>
#include <QShortcut>
#include <QSpinBox>
#include <QSplitter>
#include <QStringConverter>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextEdit>
#include <QVBoxLayout>
#include <charconv>
#include <ostream>

namespace apo::editor {
namespace {
QString text(std::string_view source) {
  return QString::fromUtf8(source.data(), qsizetype(source.size()));
}
std::string readSource(const QString &path, qint64 limit) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    throw std::runtime_error(file.errorString().toStdString());
  if (file.size() > limit)
    throw std::runtime_error(
        "The input file exceeds the script editor's size limit");
  const auto bytes = file.read(limit + 1);
  if (file.error() != QFileDevice::NoError || bytes.size() > limit)
    throw std::runtime_error(
        "Could not read the complete input within its size limit");
  return bytes.toStdString();
}
class CodeHighlighter : public QSyntaxHighlighter {
public:
  explicit CodeHighlighter(QTextDocument *document)
      : QSyntaxHighlighter(document) {}
  void legacy(bool enabled) {
    legacy_ = enabled;
    rehighlight();
  }

protected:
  void highlightBlock(const QString &block) override {
    auto apply = [&](const QString &pattern, const QColor &color,
                     bool bold = false) {
      QRegularExpression regex(
          pattern, legacy_ ? QRegularExpression::CaseInsensitiveOption
                           : QRegularExpression::NoPatternOption);
      QTextCharFormat format;
      format.setForeground(color);
      if (bold)
        format.setFontWeight(QFont::DemiBold);
      auto matches = regex.globalMatch(block);
      while (matches.hasNext()) {
        const auto m = matches.next();
        setFormat(int(m.capturedStart()), int(m.capturedLength()), format);
      }
    };
    apply(legacy_
              ? "\\b(begin|end|var|integer|real|boolean|string|if|then|else|"
                "for|to|downto|do|while|repeat|until|and|or|not|div|mod|true|"
                "false)\\b"
              : "\\b(and|break|do|else|elseif|end|false|for|function|goto|if|"
                "in|local|nil|not|or|repeat|return|then|true|until|while)\\b",
          QColor("#c4a5f5"), true);
    apply("\\b[0-9]+(?:\\.[0-9]+)?(?:[eE][+-]?[0-9]+)?\\b", QColor("#e4bd7a"));
    apply("\\bapo\\.[A-Za-z_][A-Za-z_0-9.]*", QColor("#67d6df"));
    apply("\"(?:[^\"\\\\]|\\\\.)*\"|'(?:[^'\\\\]|\\\\.|'')*'",
          QColor("#a2d39b"));
    apply(legacy_ ? "//.*$|\\{[^}]*\\}" : "--.*$", QColor("#7e91a7"));
  }

private:
  bool legacy_ = false;
};
class CodeEdit : public QPlainTextEdit {
public:
  CodeEdit() {
    QStringList words{"apo",       "math.abs", "math.cos", "math.sin",
                      "math.sqrt", "math.pi",  "ipairs",   "pairs",
                      "print",     "for",      "function", "local",
                      "return",    "while"};
    QRegularExpression api("\\bapo\\.[A-Za-z_][A-Za-z_0-9.]*");
    auto matches = api.globalMatch(text(script_api_reference()));
    while (matches.hasNext())
      words << matches.next().captured();
    words.removeDuplicates();
    words.sort();
    completer_ = new QCompleter(words, this);
    completer_->setWidget(this);
    completer_->setCompletionMode(QCompleter::PopupCompletion);
    completer_->setCaseSensitivity(Qt::CaseInsensitive);
    QObject::connect(
        completer_, qOverload<const QString &>(&QCompleter::activated), this,
        [this](const QString &completion) {
          auto cursor = textCursor();
          cursor.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor,
                              completer_->completionPrefix().size());
          cursor.insertText(completion);
          setTextCursor(cursor);
        });
  }

protected:
  void keyPressEvent(QKeyEvent *event) override {
    if (completer_->popup()->isVisible() &&
        (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ||
         event->key() == Qt::Key_Escape || event->key() == Qt::Key_Tab)) {
      event->ignore();
      return;
    }
    const bool completion = event->key() == Qt::Key_Space &&
                            event->modifiers().testFlag(Qt::ControlModifier);
    if (!completion)
      QPlainTextEdit::keyPressEvent(event);
    auto cursor = textCursor();
    const auto left = cursor.block().text().left(cursor.positionInBlock());
    int start = left.size();
    while (start && (left[start - 1].isLetterOrNumber() ||
                     left[start - 1] == '_' || left[start - 1] == '.'))
      --start;
    const auto prefix = left.mid(start);
    if (!completion &&
        (event->text().isEmpty() || prefix.size() < 3 || isReadOnly())) {
      completer_->popup()->hide();
      return;
    }
    completer_->setCompletionPrefix(prefix);
    auto rectangle = cursorRect();
    rectangle.setWidth(320);
    completer_->complete(rectangle);
  }

private:
  QCompleter *completer_;
};
} // namespace
ScriptDialog::ScriptDialog(const LegacyDocument &document, std::size_t selected,
                           QWidget *parent, bool saveDraft)
    : QDialog(parent), input_(document), selected_(selected),
      saveDraft_(saveDraft) {
  setObjectName("scriptDialog");
  setWindowTitle("Script editor");
  const auto screen = QGuiApplication::primaryScreen();
  resize(QSize(1180, 820).boundedTo(
      screen ? screen->availableGeometry().size() - QSize(32, 64)
             : QSize(1180, 820)));
  auto layout = new QVBoxLayout(this);
  auto tools = new QHBoxLayout;
  open_ = new QPushButton("Open…");
  open_->setObjectName("scriptOpen");
  save_ = new QPushButton("Save…");
  save_->setObjectName("scriptSave");
  language_ = new QComboBox;
  language_->setObjectName("scriptLanguage");
  language_->addItems({"Lua", "Legacy Apophysis (supported subset)"});
  examples_ = new QComboBox;
  examples_->setObjectName("scriptExamples");
  examples_->addItem("Choose an example…");
  for (const auto &example : script_examples())
    examples_->addItem(text(example.name));
  auto help = new QPushButton("API reference");
  help->setObjectName("scriptReference");
  tools->addWidget(open_);
  tools->addWidget(save_);
  tools->addWidget(language_);
  tools->addWidget(examples_, 1);
  tools->addWidget(help);
  layout->addLayout(tools);
  auto split = new QSplitter(Qt::Vertical);
  split->setObjectName("scriptSplit");
  source_ = new CodeEdit;
  source_->setObjectName("scriptSource");
  source_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  source_->setTabStopDistance(
      QFontMetricsF(source_->font()).horizontalAdvance(' ') * 4);
  source_->setLineWrapMode(QPlainTextEdit::NoWrap);
  auto syntax = new CodeHighlighter(source_->document());
  console_ = new QPlainTextEdit;
  console_->setObjectName("scriptConsole");
  console_->setReadOnly(true);
  console_->setFont(source_->font());
  console_->setMaximumBlockCount(2500);
  console_->setPlaceholderText(
      "Console, errors and deferred outputs appear here.");
  split->addWidget(source_);
  split->addWidget(console_);
  split->setStretchFactor(0, 4);
  split->setStretchFactor(1, 1);
  layout->addWidget(split, 1);
  auto options = new QHBoxLayout;
  seed_ = new QLineEdit("123456");
  seed_->setObjectName("scriptSeed");
  seed_->setMaximumWidth(170);
  seconds_ = new QSpinBox;
  seconds_->setObjectName("scriptTimeLimit");
  seconds_->setRange(1, 60);
  seconds_->setValue(10);
  seconds_->setSuffix(" s");
  memory_ = new QSpinBox;
  memory_->setObjectName("scriptMemoryLimit");
  memory_->setRange(4, 256);
  memory_->setValue(32);
  memory_->setSuffix(" MiB");
  instructions_ = new QSpinBox;
  instructions_->setObjectName("scriptInstructionLimit");
  instructions_->setRange(1000, 100000000);
  instructions_->setValue(2000000);
  instructions_->setSingleStep(100000);
  options->addWidget(new QLabel("Seed"));
  options->addWidget(seed_);
  options->addWidget(new QLabel("Time limit"));
  options->addWidget(seconds_);
  options->addWidget(new QLabel("Lua memory"));
  options->addWidget(memory_);
  options->addWidget(new QLabel("Instructions"));
  options->addWidget(instructions_);
  options->addStretch();
  layout->addLayout(options);
  auto permissions = new QHBoxLayout;
  renders_ = new QCheckBox("Allow queued renders");
  renders_->setObjectName("scriptAllowRenders");
  exports_ = new QCheckBox("Allow exports");
  exports_->setObjectName("scriptAllowExports");
  replace_ = new QCheckBox("Allow replacing output files");
  replace_->setObjectName("scriptReplaceExisting");
  import_ = new QPushButton("Add input file…");
  import_->setObjectName("scriptAddInput");
  permissions->addWidget(renders_);
  permissions->addWidget(exports_);
  permissions->addWidget(replace_);
  permissions->addStretch();
  permissions->addWidget(import_);
  layout->addLayout(permissions);
  auto paths = new QHBoxLayout;
  paths->addWidget(new QLabel("Output folder"));
  directory_ = new QLineEdit;
  directory_->setObjectName("scriptOutputDirectory");
  directory_->setPlaceholderText(
      "Choose a folder if the script requests renders or exports");
  auto browse = new QPushButton("Browse…");
  browse->setObjectName("scriptBrowseOutput");
  paths->addWidget(directory_, 1);
  paths->addWidget(browse);
  layout->addLayout(paths);
  auto explanation = new QLabel(
      "Run works on a private copy. Apply result creates one undo step and "
      "adds permitted outputs. Scripts have no direct network, process or "
      "filesystem access. Ctrl+Space completes API names.");
  explanation->setWordWrap(true);
  layout->addWidget(explanation);
  status_ = new QLabel("Ready");
  status_->setObjectName("scriptStatus");
  status_->setWordWrap(true);
  layout->addWidget(status_);
  auto buttons = new QHBoxLayout;
  run_ = new QPushButton("Run");
  run_->setObjectName("scriptRun");
  run_->setDefault(true);
  stop_ = new QPushButton("Stop");
  stop_->setObjectName("scriptStop");
  apply_ = new QPushButton("Apply result");
  apply_->setObjectName("scriptApply");
  auto close = new QPushButton("Close");
  close->setObjectName("scriptClose");
  buttons->addWidget(run_);
  buttons->addWidget(stop_);
  buttons->addStretch();
  buttons->addWidget(apply_);
  buttons->addWidget(close);
  layout->addLayout(buttons);
  connect(run_, &QPushButton::clicked, this, &ScriptDialog::runScript);
  connect(stop_, &QPushButton::clicked, this, &ScriptDialog::stopScript);
  connect(close, &QPushButton::clicked, this, &ScriptDialog::reject);
  connect(apply_, &QPushButton::clicked, this, &QDialog::accept);
  connect(help, &QPushButton::clicked, this, &ScriptDialog::showReference);
  connect(browse, &QPushButton::clicked, this, [this] {
    const auto directory = QFileDialog::getExistingDirectory(
        this, "Choose script output folder", directory_->text());
    if (!directory.isEmpty())
      directory_->setText(directory);
  });
  connect(open_, &QPushButton::clicked, this, [this] {
    const auto path = QFileDialog::getOpenFileName(
        this, "Open script", path_,
        "Scripts (*.lua *.asc *.pas *.script);;Text files (*.txt);;All files "
        "(*)");
    if (!path.isEmpty())
      try {
        loadScript(path);
      } catch (const std::exception &e) {
        status_->setText(text(e.what()));
      }
  });
  connect(save_, &QPushButton::clicked, this, [this] {
    const auto path = QFileDialog::getSaveFileName(
        this, "Save script",
        path_.isEmpty()
            ? (language_->currentIndex() ? "script.asc" : "script.lua")
            : path_,
        "Scripts (*.lua *.asc *.pas)");
    if (!path.isEmpty())
      try {
        saveScript(path);
      } catch (const std::exception &e) {
        status_->setText(text(e.what()));
      }
  });
  connect(import_, &QPushButton::clicked, this, [this] {
    const auto path = QFileDialog::getOpenFileName(
        this, "Allow script to read this input file", {},
        "Flame libraries (*.flame *.xml *.temp);;All files (*)");
    if (path.isEmpty())
      return;
    try {
      auto bytes = readSource(path, 16 * 1024 * 1024);
      std::size_t size = bytes.size();
      for (const auto &item : imports_)
        size += item.second.size();
      if (size > 32 * 1024 * 1024)
        throw std::runtime_error(
            "Named input files exceed the 32 MiB host limit");
      const auto name = QFileInfo(path).fileName().toStdString();
      imports_[name] = std::move(bytes);
      importPaths_[name] = filesystemPath(path);
      invalidate();
      console_->appendPlainText("Input available as: " + text(name));
    } catch (const std::exception &e) {
      status_->setText(text(e.what()));
    }
  });
  connect(examples_, &QComboBox::activated, this, [this](int index) {
    if (index < 1)
      return;
    const auto examples = script_examples();
    const auto &example = examples.at(std::size_t(index - 1));
    setSource(text(example.source), example.language);
    path_.clear();
  });
  connect(language_, &QComboBox::currentIndexChanged, this,
          [this, syntax](int index) {
            syntax->legacy(index == 1);
            invalidate();
          });
  connect(source_, &QPlainTextEdit::textChanged, this,
          &ScriptDialog::invalidate);
  for (auto control : {renders_, exports_})
    connect(control, &QCheckBox::toggled, this, &ScriptDialog::invalidate);
  connect(seed_, &QLineEdit::textChanged, this, &ScriptDialog::invalidate);
  auto shortcut = new QShortcut(QKeySequence("Ctrl+Return"), this);
  connect(shortcut, &QShortcut::activated, this, &ScriptDialog::runScript);
  const auto examples = script_examples();
  if (!examples.empty())
    setSource(text(examples.front().source), examples.front().language);
  if (saveDraft_) {
    QSettings settings;
    if (settings.contains("scripting/draft"))
      setSource(settings.value("scripting/draft").toString(),
                settings.value("scripting/language").toInt() == 1
                    ? ScriptLanguage::legacy_pascal
                    : ScriptLanguage::lua);
  }
  updateControls();
}
ScriptDialog::~ScriptDialog() {
  for (auto child : findChildren<QObject *>())
    QObject::disconnect(child, nullptr, this, nullptr);
  if (worker_.joinable()) {
    worker_.request_stop();
    worker_.join();
  }
  if (saveDraft_) {
    QSettings settings;
    const auto source = source_->toPlainText();
    if (source.toUtf8().size() <= 1024 * 1024) {
      settings.setValue("scripting/draft", source);
      settings.setValue("scripting/language", language_->currentIndex());
    }
  }
}
QString ScriptDialog::outputDirectory() const { return directory_->text(); }
bool ScriptDialog::replaceExisting() const { return replace_->isChecked(); }
std::vector<std::filesystem::path> ScriptDialog::protectedFiles() const {
  std::vector<std::filesystem::path> paths;
  if (!path_.isEmpty())
    paths.push_back(filesystemPath(path_));
  for (const auto &input : importPaths_)
    paths.push_back(input.second);
  return paths;
}
void ScriptDialog::setSource(const QString &source, ScriptLanguage language) {
  if (running_)
    throw std::runtime_error(
        "Stop the current script before replacing its source");
  language_->setCurrentIndex(language == ScriptLanguage::legacy_pascal ? 1 : 0);
  source_->setPlainText(source);
  invalidate();
}
void ScriptDialog::loadScript(const QString &path) {
  const auto bytes = readSource(path, 1024 * 1024);
  QStringDecoder decoder(QStringDecoder::Utf8);
  const auto source =
      decoder.decode(QByteArrayView(bytes.data(), qsizetype(bytes.size())));
  if (decoder.hasError())
    throw std::runtime_error("The script must use UTF-8 text encoding");
  const auto suffix = QFileInfo(path).suffix().toLower();
  setSource(source, suffix == "asc" || suffix == "pas" || suffix == "script"
                        ? ScriptLanguage::legacy_pascal
                        : ScriptLanguage::lua);
  path_ = path;
  status_->setText("Opened " + path);
}
void ScriptDialog::saveScript(const QString &path) {
  const auto bytes = source_->toPlainText().toUtf8();
  if (bytes.size() > 1024 * 1024)
    throw std::runtime_error("Script source exceeds 1 MiB");
  write_atomic(filesystemPath(path), [&](std::ostream &out) {
    out.write(bytes.data(), bytes.size());
  });
  path_ = path;
  source_->document()->setModified(false);
  status_->setText("Saved " + path);
}
void ScriptDialog::invalidate() {
  if (running_)
    return;
  result_.reset();
  source_->setExtraSelections({});
  status_->setText("Ready · Run to review changes");
  updateControls();
}
void ScriptDialog::updateControls() {
  source_->setReadOnly(running_);
  run_->setEnabled(!running_);
  stop_->setEnabled(running_);
  apply_->setEnabled(!running_ && result_.has_value());
  for (auto widget : std::initializer_list<QWidget *>{
           open_, save_, import_, language_, examples_, seed_, seconds_,
           memory_, instructions_, renders_, exports_})
    widget->setEnabled(!running_);
}
void ScriptDialog::runScript() {
  if (running_)
    return;
  try {
    ScriptOptions options;
    const auto seed = seed_->text().toStdString();
    const auto conversion =
        std::from_chars(seed.data(), seed.data() + seed.size(), options.seed);
    if (conversion.ec != std::errc{} ||
        conversion.ptr != seed.data() + seed.size() || seed.empty())
      throw std::runtime_error("Seed must be an unsigned 64-bit integer");
    options.language = language_->currentIndex() == 1
                           ? ScriptLanguage::legacy_pascal
                           : ScriptLanguage::lua;
    options.limits.instructions = std::uint64_t(instructions_->value());
    options.limits.timeout_ms = std::uint64_t(seconds_->value()) * 1000;
    options.limits.lua_memory_bytes =
        std::uint64_t(memory_->value()) * 1024 * 1024;
    options.allow_render_requests = renders_->isChecked();
    options.allow_export_requests = exports_->isChecked();
    options.imports = imports_;
    const auto source = source_->toPlainText().toUtf8().toStdString();
    if (source.size() > options.limits.source_bytes)
      throw std::runtime_error("Script source exceeds 1 MiB");
    if (worker_.joinable())
      worker_.join();
    result_.reset();
    running_ = true;
    closing_ = false;
    console_->clear();
    source_->setExtraSelections({});
    status_->setText("Running on a private document copy…");
    updateControls();
    worker_ = std::jthread(
        [this, input = input_, selected = selected_, source,
         options = std::move(options)](std::stop_token stop) mutable {
          options.stop_token = stop;
          std::optional<ScriptResult> result;
          QString error;
          std::size_t line = 0;
          try {
            result = run_script(input, selected, source, options);
          } catch (const ScriptError &e) {
            error = text(e.what());
            line = e.line();
          } catch (const std::exception &e) {
            error = text(e.what());
          }
          QMetaObject::invokeMethod(
              this,
              [this, result = std::move(result), error, line]() mutable {
                finish(std::move(result), error, line);
              },
              Qt::QueuedConnection);
        });
  } catch (const std::exception &error) {
    running_ = false;
    updateControls();
    status_->setText(text(error.what()));
  }
}
void ScriptDialog::stopScript() {
  if (worker_.joinable() && running_) {
    worker_.request_stop();
    status_->setText("Stopping…");
    stop_->setEnabled(false);
  }
}
void ScriptDialog::finish(std::optional<ScriptResult> result,
                          const QString &error, std::size_t line) {
  if (worker_.joinable())
    worker_.join();
  running_ = false;
  result_ = std::move(result);
  if (result_) {
    for (const auto &message : result_->console)
      console_->appendPlainText(text(message));
    for (const auto &note : result_->compatibility_notes)
      console_->appendPlainText("Compatibility: " + text(note));
    for (const auto &request : result_->requests)
      console_->appendPlainText(
          (request.kind == ScriptRequestKind::render ? "Queue render: "
                                                     : "Export: ") +
          QString::fromStdString(request.destination.generic_string()));
    status_->setText(QString("Completed · under %1 VM instructions · Lua peak "
                             "%2 KiB · %3 deferred "
                             "outputs. Review the console, then Apply result.")
                         .arg(qulonglong(result_->instructions +
                                         result_->instruction_quantum))
                         .arg(qulonglong(result_->peak_lua_bytes / 1024))
                         .arg(result_->requests.size()));
  } else {
    console_->appendPlainText(error);
    status_->setText("No changes applied · " + error);
    if (line && line <= std::size_t(source_->blockCount())) {
      QTextEdit::ExtraSelection selection;
      selection.cursor =
          QTextCursor(source_->document()->findBlockByNumber(int(line) - 1));
      selection.format.setBackground(QColor("#633343"));
      selection.format.setProperty(QTextFormat::FullWidthSelection, true);
      source_->setExtraSelections({selection});
      source_->setTextCursor(selection.cursor);
      source_->ensureCursorVisible();
    }
  }
  updateControls();
  emit executionFinished(result_.has_value());
  if (closing_)
    QDialog::reject();
}
void ScriptDialog::reject() {
  if (running_) {
    closing_ = true;
    stopScript();
    return;
  }
  QDialog::reject();
}
void ScriptDialog::closeEvent(QCloseEvent *event) {
  if (running_) {
    closing_ = true;
    stopScript();
    event->ignore();
  } else
    QDialog::closeEvent(event);
}
void ScriptDialog::showReference() {
  QDialog dialog(this);
  dialog.setWindowTitle("Scripting API reference");
  dialog.resize(900, 650);
  auto layout = new QVBoxLayout(&dialog);
  auto textView = new QTextBrowser;
  textView->setOpenExternalLinks(false);
  textView->setPlainText(text(script_api_reference()));
  textView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  layout->addWidget(textView);
  auto buttons = new QDialogButtonBox(QDialogButtonBox::Close);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(buttons);
  dialog.exec();
}
} // namespace apo::editor
