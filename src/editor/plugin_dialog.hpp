#pragma once
#include "../plugin.hpp"
#include <QDialog>
#include <QMap>
#include <QStringList>

class QSettings;
class QListWidget;
class QTableWidget;
class QPlainTextEdit;
class QPushButton;
class QLabel;

namespace apo::editor {
// Must run before restoring documents/jobs. Failed approvals remain in settings
// so the user can repair the installation and restore the original queue later.
QStringList loadApprovedPlugins(QSettings &settings);

class PluginDialog : public QDialog {
    Q_OBJECT
  public:
    explicit PluginDialog(QSettings &settings, QWidget *parent = nullptr, bool persist = true);
    void refreshPlugins();
  signals:
    void pluginsChanged();

  private:
    struct Row {
        PluginDiscovery discovered;
        std::optional<PluginInfo> active;
    };
    QSettings &settings_;
    bool persist_;
    QStringList searchPaths_;
    QMap<QString, QString> approvals_;
    std::vector<Row> rows_;
    QListWidget *paths_;
    QTableWidget *table_;
    QPlainTextEdit *details_;
    QPushButton *enable_, *disable_, *remove_, *removePath_;
    QLabel *status_;
    void selectionChanged();
    void addSearchPath();
    void removeSearchPath();
    void installPlugin();
    void enableSelected();
    void disableSelected();
    void removeSelected();
    void savePreferences();
    const Row *selected() const;
    void showError(const QString &title, const std::exception &error);
};
} // namespace apo::editor
