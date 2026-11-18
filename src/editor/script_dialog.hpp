#pragma once
#include "../script.hpp"
#include <QDialog>
#include <QPointer>
#include <optional>
#include <thread>

class QPlainTextEdit;
class QComboBox;
class QCheckBox;
class QLineEdit;
class QSpinBox;
class QPushButton;
class QLabel;
class QCloseEvent;

namespace apo::editor {
class ScriptDialog : public QDialog {
  Q_OBJECT
public:
  explicit ScriptDialog(const LegacyDocument &document, std::size_t selected,
                        QWidget *parent = nullptr, bool saveDraft = false);
  ~ScriptDialog() override;
  const std::optional<ScriptResult> &scriptResult() const { return result_; }
  QString outputDirectory() const;
  bool replaceExisting() const;
  std::vector<std::filesystem::path> protectedFiles() const;
  bool isRunning() const { return running_; }
  void runScript();
  void stopScript();
  void setSource(const QString &source,
                 ScriptLanguage language = ScriptLanguage::lua);
  void loadScript(const QString &path);
  void saveScript(const QString &path);
signals:
  void executionFinished(bool success);

protected:
  void closeEvent(QCloseEvent *) override;
  void reject() override;

private:
  LegacyDocument input_;
  std::size_t selected_;
  QPlainTextEdit *source_, *console_;
  QComboBox *language_, *examples_;
  QLineEdit *seed_, *directory_;
  QSpinBox *memory_, *seconds_, *instructions_;
  QCheckBox *renders_, *exports_, *replace_;
  QPushButton *run_, *stop_, *apply_, *open_, *save_, *import_;
  QLabel *status_;
  std::map<std::string, std::string> imports_;
  std::map<std::string, std::filesystem::path> importPaths_;
  std::optional<ScriptResult> result_;
  std::jthread worker_;
  bool running_ = false, closing_ = false;
  QString path_;
  bool saveDraft_;
  void finish(std::optional<ScriptResult>, const QString &error,
              std::size_t line);
  void invalidate();
  void updateControls();
  void showReference();
};
} // namespace apo::editor
