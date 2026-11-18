#include "extensions_smoke.hpp"
#include "../legacy_formats.hpp"
#include "../output_file.hpp"
#include "../plugin.hpp"
#include "main_window.hpp"
#include "plugin_dialog.hpp"
#include "script_dialog.hpp"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QSettings>
#include <QTableWidget>
#include <QTimer>
#include <iostream>

namespace apo::editor {
void extensionsSmoke(MainWindow &window, const QString &path,
                     const QString &trustedManifest) {
  if (trustedManifest.isEmpty())
    throw std::invalid_argument("Extension smoke requires --plugin-manifest "
                                "for the explicitly trusted SDK example");
  const auto plugin = enable_plugin(filesystemPath(trustedManifest));
  if (plugin.name != "sample_wave")
    throw std::invalid_argument(
        "Extension smoke expects the bundled sample_wave SDK example");
  auto dialog = new ScriptDialog(window.session().document(), 0, &window);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setSource(
      R"LUA(-- Milestone 8: private edits, plugin parameters and deferred outputs.
apo.set_flame {name = 'Extended compatibility study', gamma = 2.2}
for i = 1, apo.transform_count() do
  local t = apo.transform(i)
  t.variations = {
    {name = 'linear', weight = 0.8},
    {name = 'sample_wave', weight = 0.2,
     parameters = {amplitude = 0.18, frequency = 3.0}}
  }
  apo.set_transform(i, t)
end
apo.palette_from_stops({{0,0.02,0.04,0.15}, {120,0.95,0.16,0.05}, {255,1,0.92,0.65}})
apo.export_flame('script-export.flame')
apo.export_palette('script-palette.gpl')
apo.render('script-render.png', {width=320,height=240,samples=300000,
  seed='18446744073709551615',format='png16',alpha=true,threads=2,
  memory_mib=32,disk_mib=256,backend='cpu'})
print('Prepared', apo.transform_count(), 'transforms with sample_wave')
)LUA");
  dialog->findChild<QCheckBox *>("scriptAllowRenders")->setChecked(true);
  dialog->findChild<QCheckBox *>("scriptAllowExports")->setChecked(true);
  dialog->findChild<QLineEdit *>("scriptOutputDirectory")->setText(path);
  QObject::connect(
      dialog, &ScriptDialog::executionFinished, &window,
      [&window, path, plugin, dialog](bool success) {
        try {
          if (!success || !dialog->scriptResult())
            throw std::runtime_error("Extension script smoke failed");
          const auto result = *dialog->scriptResult();
          QTimer::singleShot(
              100, &window, [&window, path, plugin, dialog, result] {
                try {
                  if (!dialog->grab().save(path + "/script-editor.png"))
                    throw std::runtime_error("Script editor screenshot failed");
                  window.applyScriptResult(result, path);
                  window.session().save(path + "/example.apoproject");
                  window.findChild<QAction *>("actionSave")->trigger();
                  auto f = window.session().document().render_model(0);
                  write_atomic(filesystemPath(path + "/palette.ugr"),
                               [&](std::ostream &out) {
                                 out << export_ugr(f.palette, "M8 study");
                               });
                  write_atomic(filesystemPath(path + "/palette.map"),
                               [&](std::ostream &out) {
                                 out << export_fractint_map(f.palette,
                                                            "M8 study");
                               });
                  ScriptOptions legacy;
                  legacy.language = ScriptLanguage::legacy_pascal;
                  const auto old =
                      run_script(window.session().document(), 0,
                                 script_examples().back().source, legacy);
                  write_atomic(
                      filesystemPath(path + "/legacy-script.flame"),
                      [&](std::ostream &out) { out << old.document_bytes; });
                  write_atomic(
                      filesystemPath(path + "/legacy-translated.lua"),
                      [&](std::ostream &out) { out << old.translated_source; });
                  auto report = std::make_shared<QJsonObject>(QJsonObject{
                      {"version", "0.8.0"},
                      {"qt_platform", QGuiApplication::platformName()},
                      {"plugin", QString::fromStdString(plugin.name)},
                      {"plugin_sha256",
                       QString::fromStdString(plugin.fingerprint)},
                      {"lua_instruction_hook_count",
                       QString::number(result.instructions)},
                      {"lua_instruction_quantum",
                       QString::number(result.instruction_quantum)},
                      {"lua_instruction_upper_bound_exclusive",
                       QString::number(result.instructions +
                                       result.instruction_quantum)},
                      {"peak_lua_bytes",
                       QString::number(result.peak_lua_bytes)},
                      {"peak_host_bytes",
                       QString::number(result.peak_host_bytes)},
                      {"requests", int(result.requests.size())},
                      {"legacy_script_executed", true},
                      {"native_project_saved", true},
                      {"ugr_map_exported", true}});
                  dialog->close();
                  auto settings = std::make_shared<QSettings>(
                      path + "/plugin-test.ini", QSettings::IniFormat);
                  settings->setValue(
                      "plugins/searchPaths",
                      QStringList{
                          QFileInfo(QString::fromStdString(
                                        plugin.manifest_path.generic_string()))
                              .absolutePath()});
                  auto plugins = new PluginDialog(*settings, &window, false);
                  plugins->setAttribute(Qt::WA_DeleteOnClose);
                  plugins->show();
                  if (auto table =
                          plugins->findChild<QTableWidget *>("pluginTable");
                      table && table->rowCount())
                    table->selectRow(0);
                  QTimer::singleShot(
                      150, &window, [&window, path, plugins, settings, report] {
                        try {
                          if (!plugins->grab().save(path +
                                                    "/plugin-manager.png"))
                            throw std::runtime_error(
                                "Plugin manager screenshot failed");
                          plugins->close();
                          window.showRenderQueue();
                          QObject::connect(
                              &window.renderQueue(), &RenderQueue::jobFinished,
                              &window,
                              [&window, path, report](const QString &id,
                                                      bool ok) {
                                try {
                                  if (!ok)
                                    throw std::runtime_error(
                                        "Scripted render failed");
                                  const auto *job =
                                      window.renderQueue().job(id);
                                  if (!job ||
                                      job->state != RenderJobState::complete)
                                    throw std::runtime_error(
                                        "Script render did not complete");
                                  report->insert("render_backend",
                                                 job->backend);
                                  report->insert(
                                      "render_samples",
                                      QString::number(job->completed_samples));
                                  report->insert(
                                      "render_seed",
                                      QString::number(job->render.seed));
                                  report->insert("render_width",
                                                 job->flame.width);
                                  report->insert("render_height",
                                                 job->flame.height);
                                  report->insert(
                                      "render_format",
                                      imageFormatText(job->file.image.format));
                                  window.renderQueue().saveTo(
                                      path + "/script-queue.json");
                                  const auto bytes =
                                      QJsonDocument(*report).toJson();
                                  write_atomic(
                                      filesystemPath(path +
                                                     "/extensions-report.json"),
                                      [&](std::ostream &out) {
                                        out << bytes.toStdString();
                                      });
                                  QTimer::singleShot(
                                      150, &window, [&window, path] {
                                        auto queue =
                                            window
                                                .findChild<RenderQueueDialog *>(
                                                    "renderQueueDialog");
                                        const bool saved =
                                            queue &&
                                            queue->grab().save(
                                                path +
                                                "/script-render-queue.png") &&
                                            window.grab().save(
                                                path +
                                                "/extensions-editor.png");
                                        QCoreApplication::exit(saved ? 0 : 1);
                                      });
                                } catch (const std::exception &e) {
                                  std::cerr << e.what() << '\n';
                                  QCoreApplication::exit(1);
                                }
                              });
                          window.renderQueue().start();
                        } catch (const std::exception &e) {
                          std::cerr << e.what() << '\n';
                          QCoreApplication::exit(1);
                        }
                      });
                } catch (const std::exception &e) {
                  std::cerr << e.what() << '\n';
                  QCoreApplication::exit(1);
                }
              });
        } catch (const std::exception &e) {
          std::cerr << e.what() << '\n';
          QCoreApplication::exit(1);
        }
      });
  dialog->show();
  QTimer::singleShot(100, dialog, &ScriptDialog::runScript);
}
} // namespace apo::editor
