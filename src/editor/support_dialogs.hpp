#pragma once

#include "preview_controller.hpp"
#include <QDialog>
#include <QJsonObject>
#include <QString>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QTextBrowser;

namespace apo::editor {
class UserManualDialog : public QDialog {
  Q_OBJECT
public:
  explicit UserManualDialog(QWidget *parent = nullptr);
private:
  QTextBrowser *browser_;
  QLineEdit *search_;
  QLabel *status_;
  void findText(bool backwards);
};

QJsonObject system_information(const PreviewSettings &settings,
                               const QString &actualBackend,
                               const QString &actualDevice,
                               const QString &fallbackReason);
QString system_information_text(const QJsonObject &report);

class SystemInformationDialog : public QDialog {
  Q_OBJECT
public:
  explicit SystemInformationDialog(QJsonObject report, QWidget *parent = nullptr);
  // Same atomic write path used by the Save report action.
  void saveReport(const QString &path, bool json) const;
  const QJsonObject &report() const { return report_; }
private:
  QJsonObject report_;
  QPlainTextEdit *text_;
};
} // namespace apo::editor
