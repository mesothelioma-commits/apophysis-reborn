#include "../flame_io.hpp"
#include "../animation.hpp"
#include "../legacy_formats.hpp"
#include "../output_file.hpp"
#include "../plugin.hpp"
#include "../script_host.hpp"
#include "main_window.hpp"
#include "build_info.hpp"
#include "plugin_dialog.hpp"
#include "script_dialog.hpp"
#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDoubleValidator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <charconv>
#include <limits>
#include <set>

namespace apo::editor {
namespace {
class PluginNumber : public QDoubleSpinBox {
protected:
  QString textFromValue(double value) const override {
    char buffer[128];
    const auto result =
        std::to_chars(std::begin(buffer), std::end(buffer), value);
    return QString::fromLatin1(buffer, qsizetype(result.ptr - buffer));
  }
  double valueFromText(const QString &value) const override {
    return QLocale::c().toDouble(value);
  }
  QValidator::State validate(QString &input, int &position) const override {
    QDoubleValidator validator(minimum(), maximum(), decimals());
    validator.setLocale(QLocale::c());
    validator.setNotation(QDoubleValidator::ScientificNotation);
    return validator.validate(input, position);
  }
};
QString text(std::string_view source) {
  return QString::fromUtf8(source.data(), qsizetype(source.size()));
}
QString pathText(const std::filesystem::path &path) {
#ifdef _WIN32
  return QString::fromStdWString(path.wstring());
#else
  return text(path.native());
#endif
}
template <class T>
void readSetting(const ProjectSettings &values, const char *key, T &value) {
  const auto it = values.find(key);
  if (it == values.end())
    return;
  T parsed{};
  const auto &s = it->second;
  const auto [end, error] =
      std::from_chars(s.data(), s.data() + s.size(), parsed);
  if (error != std::errc{} || end != s.data() + s.size() || s.empty())
    throw std::invalid_argument(std::string("Invalid saved project setting: ") +
                                key);
  value = parsed;
}
std::string readSmallFile(const QString &path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    throw std::runtime_error(file.errorString().toStdString());
  if (file.size() > 16 * 1024 * 1024)
    throw std::invalid_argument("The format input exceeds 16 MiB");
  const auto bytes = file.read(16 * 1024 * 1024 + 1);
  if (file.error() != QFileDevice::NoError || bytes.size() > 16 * 1024 * 1024)
    throw std::runtime_error("Could not read the complete format input");
  return bytes.toStdString();
}
} // namespace
void MainWindow::showScriptEditor() {
  if (!session_.hasDocument())
    return;
  ScriptDialog dialog(session_.document(),
                      std::size_t(session_.selectedFlame()), this,
                      useSettings_);
  if (dialog.exec() != QDialog::Accepted || !dialog.scriptResult())
    return;
  run([&] {
    applyScriptResult(*dialog.scriptResult(), dialog.outputDirectory(),
                      dialog.replaceExisting(), dialog.protectedFiles());
  });
}
void MainWindow::applyScriptResult(
    const ScriptResult &result, const QString &directory, bool replaceExisting,
    const std::vector<std::filesystem::path> &protectedFiles) {
  auto document = LegacyDocument::parse(result.document_bytes);
  if (result.selected_index >= document.entries().size())
    throw std::invalid_argument("Script selected a missing flame");
  auto inputs = protectedFiles;
  if (!session_.path().isEmpty())
    inputs.push_back(filesystemPath(session_.path()));
  for (const auto &path : queue_->protectedPaths())
    inputs.push_back(filesystemPath(path));
  if (!session_.recoveryPath().isEmpty())
    for (const auto &suffix : QStringList{QString{}, ".json", ".lock"})
      inputs.push_back(filesystemPath(session_.recoveryPath() + suffix));
  for (const auto location : {QStandardPaths::AppLocalDataLocation,
                              QStandardPaths::AppConfigLocation}) {
    const auto path = QStandardPaths::writableLocation(location);
    if (!path.isEmpty())
      inputs.push_back(filesystemPath(path));
  }
  for (const auto &plugin : enabled_plugins()) {
    inputs.push_back(plugin.manifest_path);
    inputs.push_back(plugin.library_path);
  }
  // Existing queued jobs also own their destinations until they finish.
  for (const auto &job : queue_->jobs())
    if (job.state != RenderJobState::complete &&
        job.state != RenderJobState::cancelled &&
        job.state != RenderJobState::failed)
      inputs.push_back(filesystemPath(job.destination));
  const auto paths = plan_script_outputs(result, filesystemPath(directory),
                                         replaceExisting, inputs);
  std::vector<RenderJob> jobs;
  for (std::size_t i = 0; i < result.requests.size(); ++i) {
    const auto &r = result.requests[i];
    if (r.kind != ScriptRequestKind::render)
      continue;
    if (image_format_for_path(paths[i],
                              image_format_bits(r.file.image.format)) !=
        r.file.image.format)
      throw std::invalid_argument(
          "A script render filename does not match its selected image format");
    RenderJob job;
    job.flame = r.flame;
    job.render = r.render;
    job.file = r.file;
    job.destination = pathText(paths[i]);
    job.source = session_.path();
    jobs.push_back(std::move(job));
  }
  if (queue_->jobs().size() + jobs.size() > 1000)
    throw std::invalid_argument(
        "Script would exceed the 1,000-job queue limit");
  std::size_t published = 0;
  try {
    for (std::size_t i = 0; i < result.requests.size(); ++i) {
      std::filesystem::create_directories(paths[i].parent_path());
      if (result.requests[i].kind == ScriptRequestKind::render)
        continue;
      publish_script_export(result.requests[i], paths[i]);
      ++published;
      record("Script exported " + pathText(paths[i]));
    }
    queue_->addBatch(jobs);
    session_.change(
        "Apply script result", [&](auto &target) { target = document; }, 0,
        false, int(result.selected_index));
  } catch (...) {
    if (published)
      record(QString("%1 script export(s) completed before a later failure; "
                     "completed files remain available.")
                 .arg(published));
    throw;
  }
  for (const auto &line : result.console)
    record("Script: " + text(line));
  for (const auto &line : result.compatibility_notes)
    record("Compatibility: " + text(line));
  record(QString("Applied script result · %1 render job(s), %2 export(s). "
                 "Document edits support Undo.")
             .arg(jobs.size())
             .arg(published));
  if (!jobs.empty())
    showRenderQueue();
}
void MainWindow::showPluginManager() {
  PluginDialog dialog(settings_, this, useSettings_);
  dialog.exec();
  refreshSelection();
}
void MainWindow::editPluginVariations() {
  if (!model_ || lock_->isChecked())
    return;
  const auto original = selectedTransform().variations;
  auto candidates = enabled_plugins();
  for (const auto &term : original)
    if (term.plugin) {
      const auto &info = term.plugin->info();
      if (std::none_of(candidates.begin(), candidates.end(),
                       [&](const auto &p) { return p.name == info.name; }))
        candidates.push_back(info);
    }
  QDialog dialog(this);
  dialog.setObjectName("pluginVariationsDialog");
  dialog.setWindowTitle("Plugin variations");
  dialog.resize(720, 560);
  auto layout = new QVBoxLayout(&dialog);
  auto note =
      new QLabel("Weights and parameter values belong to the selected "
                 "transform. Enable additional trusted modules in Tools → "
                 "Plugins. Plugin variations use the CPU renderer.");
  note->setWordWrap(true);
  layout->addWidget(note);
  auto panel = new QWidget;
  auto form = new QFormLayout(panel);
  auto scroll = new QScrollArea;
  scroll->setWidgetResizable(true);
  scroll->setWidget(panel);
  layout->addWidget(scroll, 1);
  struct Fields {
    PluginInfo info;
    QDoubleSpinBox *weight;
    std::vector<QDoubleSpinBox *> parameters;
    double displayedWeight = 0;
    std::vector<double> displayedParameters;
  };
  std::vector<Fields> fields;
  for (const auto &info : candidates) {
    auto found =
        std::find_if(original.begin(), original.end(), [&](const auto &t) {
          return t.plugin && t.plugin->info().name == info.name;
        });
    auto weight = new PluginNumber;
    weight->setDecimals(323);
    weight->setRange(-std::numeric_limits<double>::max(),
                     std::numeric_limits<double>::max());
    weight->setSingleStep(.1);
    weight->setObjectName("pluginWeight_" + text(info.name));
    weight->setValue(found == original.end() ? 0 : found->weight);
    form->addRow(text(info.name) + " · weight", weight);
    Fields row{info, weight, {}, weight->value(), {}};
    for (std::size_t i = 0; i < info.parameters.size(); ++i) {
      const auto &p = info.parameters[i];
      auto field = new PluginNumber;
      field->setDecimals(p.integer ? 0 : 323);
      field->setRange(p.minimum, p.maximum);
      field->setSingleStep(p.integer ? 1 : .1);
      field->setObjectName("pluginParameter_" + text(info.name) + "_" +
                           text(p.name));
      field->setValue(found == original.end() ? p.default_value
                                              : found->plugin_parameters.at(i));
      form->addRow("    " + text(p.name), field);
      row.parameters.push_back(field);
      row.displayedParameters.push_back(field->value());
    }
    fields.push_back(std::move(row));
  }
  if (fields.empty())
    form->addRow(new QLabel("No enabled plugin variations. Open Tools → "
                            "Plugins to inspect and enable a module."));
  auto buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  buttons->button(QDialogButtonBox::Ok)->setText("Apply");
  buttons->button(QDialogButtonBox::Ok)->setObjectName("applyPluginVariations");
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  if (dialog.exec() != QDialog::Accepted)
    return;
  run([&] {
    auto terms = original;
    for (const auto &field : fields) {
      auto found = std::find_if(terms.begin(), terms.end(), [&](const auto &t) {
        return t.plugin && t.plugin->info().name == field.info.name;
      });
      const bool existing = found != terms.end();
      if (!existing && field.weight->value() == 0)
        continue;
      Term term = existing ? *found
                           : make_variation_term(field.info.name,
                                                 field.weight->value());
      const bool weightChanged = field.weight->value() != field.displayedWeight;
      if (!existing || weightChanged)
        term.weight = field.weight->value();
      if (existing && weightChanged && term.weight == 0) {
        terms.erase(found);
        continue;
      }
      for (std::size_t i = 0; i < field.parameters.size(); ++i)
        if (!existing ||
            field.parameters[i]->value() != field.displayedParameters[i])
          term.plugin_parameters[i] = field.parameters[i]->value();
      validate_plugin_term(term);
      if (existing)
        *found = std::move(term);
      else
        terms.push_back(std::move(term));
    }
    if (terms.size() == original.size() &&
        std::equal(terms.begin(), terms.end(), original.begin(),
                   [](const Term &a, const Term &b) {
                     return a.id == b.id && a.weight == b.weight &&
                            a.plugin == b.plugin &&
                            a.plugin_parameters == b.plugin_parameters;
                   }))
      return;
    LegacyTransformEdits edits;
    edits.variations = std::move(terms);
    editTransform("Edit plugin variations", edits);
  });
}
bool MainWindow::importUpr(const QString &path) {
  const auto entries = import_upr(readSmallFile(path));
  if (entries.empty())
    throw std::invalid_argument("No UPR entries found");
  QStringList labels;
  for (const auto &e : entries)
    labels << text(e.name) +
                  (e.flame && e.report.supported() ? "" : " (unsupported)");
  int selected = 0;
  if (entries.size() > 1) {
    bool ok = false;
    const auto chosen = QInputDialog::getItem(
        this, "Import Apophysis UPR", "Choose an entry", labels, 0, false, &ok);
    if (!ok)
      return false;
    selected = labels.indexOf(chosen);
  }
  const auto &entry = entries.at(std::size_t(selected));
  for (const auto &warning : entry.report.warnings)
    record("UPR: " + text(warning));
  if (!entry.flame || !entry.report.supported()) {
    QStringList errors;
    for (const auto &error : entry.report.errors)
      errors << text(error);
    throw std::invalid_argument(
        ("This UPR entry cannot be imported:\n" + errors.join('\n'))
            .toStdString());
  }
  session_.loadBytes(serialize_flame(*entry.flame), {}, true);
  auto project = session_.projectSnapshot();
  project.metadata["source-format"] = "Apophysis UPR";
  project.metadata["source-entry"] = entry.source;
  project.metadata["source-name"] = QFileInfo(path).fileName().toStdString();
  session_.setProjectState(std::move(project));
  record("Imported supported Apophysis UPR entry from " + path +
         ". The original entry is retained in native project metadata.");
  return true;
}
void MainWindow::exportUpr() {
  run([&] {
    if (!model_)
      throw std::invalid_argument("Select a renderable flame first");
    const auto result = export_upr(*model_);
    for (const auto &warning : result.report.warnings)
      record("UPR export: " + text(warning));
    if (!result.report.supported()) {
      QStringList errors;
      for (const auto &error : result.report.errors)
        errors << text(error);
      throw std::invalid_argument(
          ("This flame cannot be represented by the supported UPR dialect:\n" +
           errors.join('\n'))
              .toStdString());
    }
    const auto path = QFileDialog::getSaveFileName(
        this, "Export Apophysis UPR", {}, "Ultra Fractal parameters (*.upr)");
    if (path.isEmpty())
      return;
    if (!session_.path().isEmpty() &&
        (QFileInfo(path).absoluteFilePath() == session_.path() ||
         (QFileInfo(path).exists() &&
          std::filesystem::equivalent(filesystemPath(path),
                                      filesystemPath(session_.path())))))
      throw std::invalid_argument(
          "Choose a destination distinct from the open document");
    write_atomic(filesystemPath(path),
                 [&](std::ostream &out) { out << result.text; });
    record("Exported historical Apophysis UPR: " + path);
  });
}
void MainWindow::captureProjectState() {
  if (!session_.hasDocument())
    return;
  auto project = session_.projectSnapshot();
  const auto &preview = preview_.settings();
  auto &p = project.preview_settings;
  p["samples"] = std::to_string(preview.sample_limit);
  p["seed"] = std::to_string(preview.seed);
  p["threads"] = std::to_string(preview.threads);
  p["memory-mib"] = std::to_string(preview.memory_mib);
  p["gpu-memory-mib"] = std::to_string(preview.gpu_memory_mib);
  p["backend"] = std::to_string(int(preview.backend));
  p["device"] = preview.device;
  p["cpu-fallback"] = preview.allow_cpu_fallback ? "1" : "0";
  auto &workspace = project.workspace;
  workspace["qt-layout"] = saveState(1).toBase64().toStdString();
  workspace["transform"] = std::to_string(session_.selectedTransform());
  workspace["final-transform"] = session_.isFinalTransform() ? "1" : "0";
  workspace["matrix-mode"] = std::to_string(matrixMode_->currentIndex());
  workspace["coordinates"] = std::to_string(coordinates_->currentIndex());
  workspace["grid"] = grid_->isChecked() ? "1" : "0";
  workspace["snap"] = snap_->isChecked() ? "1" : "0";
  if (!previewCanvas_->image().isNull()) {
    QByteArray png;
    QBuffer buffer(&png);
    if (buffer.open(QIODevice::WriteOnly) &&
        previewCanvas_->image()
            .scaled(384, 384, Qt::KeepAspectRatio, Qt::SmoothTransformation)
            .save(&buffer, "PNG"))
      p["thumbnail-png"] = png.toBase64().toStdString();
  }
  project.render_settings = renderSettings_;
  project.render_presets = renderPresets_;
  project.metadata["application"] = "Apophysis Reborn";
  project.metadata["application-version"] = application_version();
  auto remember = [&](const Transform &transform) {
    for (const auto &term : transform.variations)
      if (term.plugin) {
        const auto &info = term.plugin->info();
        auto it = std::find_if(
            project.plugin_references.begin(), project.plugin_references.end(),
            [&](const auto &ref) { return ref.id == info.id; });
        ProjectPluginReference ref{info.id,
                                   info.version,
                                   pathText(info.manifest_path).toStdString(),
                                   info.fingerprint,
                                   {}};
        if (it == project.plugin_references.end())
          project.plugin_references.push_back(ref);
        else {
          ref.metadata = it->metadata;
          *it = std::move(ref);
        }
      }
  };
  for (std::size_t i = 0; i < session_.document().entries().size(); ++i)
    if (!session_.document().render_issue(i)) {
      const auto f = session_.document().render_model(i);
      for (const auto &t : f.transforms)
        remember(t);
      if (f.final_transform)
        remember(*f.final_transform);
    }
  if (project.animation_data && is_animation_payload(*project.animation_data)) {
    try {
      const auto clip = parse_animation(*project.animation_data);
      for (const auto &key : clip.keyframes) {
        const auto document = LegacyDocument::parse(key.library_bytes);
        if (document.render_issue(key.selected_index))
          continue;
        const auto frame = document.render_model(key.selected_index);
        for (const auto &transform : frame.transforms)
          remember(transform);
        if (frame.final_transform)
          remember(*frame.final_transform);
      }
    } catch (const std::exception &) {
      // Preserve newer or unsupported animation payloads without interpretation.
    }
  }
  session_.setProjectState(std::move(project));
}
void MainWindow::restoreProjectState() {
  renderSettings_.clear();
  renderPresets_.clear();
  if (!session_.isProject())
    return;
  const auto project = session_.projectSnapshot();
  renderSettings_ = project.render_settings;
  renderPresets_ = project.render_presets;
  try {
    auto preview = preview_.settings();
    const auto &p = project.preview_settings;
    readSetting(p, "samples", preview.sample_limit);
    readSetting(p, "seed", preview.seed);
    readSetting(p, "threads", preview.threads);
    readSetting(p, "memory-mib", preview.memory_mib);
    readSetting(p, "gpu-memory-mib", preview.gpu_memory_mib);
    int backend = int(preview.backend),
        fallback = preview.allow_cpu_fallback ? 1 : 0;
    readSetting(p, "backend", backend);
    readSetting(p, "cpu-fallback", fallback);
    if (backend < 0 || backend > 2 || fallback < 0 || fallback > 1)
      throw std::invalid_argument("Invalid project renderer selection");
    preview.backend = static_cast<RenderBackend>(backend);
    preview.allow_cpu_fallback = fallback == 1;
    if (const auto i = p.find("device"); i != p.end())
      preview.device = i->second;
    preview_.setSettings(preview);
    const QSignalBlocker block(quality_);
    int index = quality_->findData(qulonglong(preview.sample_limit));
    if (index < 0) {
      quality_->addItem("Project", qulonglong(preview.sample_limit));
      index = quality_->count() - 1;
    }
    quality_->setCurrentIndex(index);
  } catch (const std::exception &error) {
    record("Project preview settings retained but not applied: " +
           text(error.what()));
  }
  const auto &w = project.workspace;
  if (const auto i = w.find("qt-layout"); i != w.end())
    if (!restoreState(
            QByteArray::fromBase64(QByteArray::fromStdString(i->second)), 1))
      record("The saved project layout could not be restored on this screen.");
  try {
    int transform = 0, final = 0, matrix = 0, coordinates = 0, grid = 1,
        snap = 0;
    readSetting(w, "transform", transform);
    readSetting(w, "final-transform", final);
    readSetting(w, "matrix-mode", matrix);
    readSetting(w, "coordinates", coordinates);
    readSetting(w, "grid", grid);
    readSetting(w, "snap", snap);
    if (model_ && transform >= 0 &&
        ((final == 1 && model_->final_transform) ||
         (final == 0 && std::size_t(transform) < model_->transforms.size())))
      session_.selectTransform(transform, final == 1);
    if (matrix >= 0 && matrix < matrixMode_->count())
      matrixMode_->setCurrentIndex(matrix);
    if (coordinates >= 0 && coordinates < coordinates_->count())
      coordinates_->setCurrentIndex(coordinates);
    grid_->setChecked(grid == 1);
    snap_->setChecked(snap == 1);
  } catch (const std::exception &error) {
    record("Project workspace settings retained but not applied: " +
           text(error.what()));
  }
  const auto plugins = enabled_plugins();
  for (const auto &ref : project.plugin_references) {
    const auto found =
        std::find_if(plugins.begin(), plugins.end(),
                     [&](const auto &p) { return p.id == ref.id; });
    if (found == plugins.end())
      record("Project requires plugin " + text(ref.id) +
             ". Inspect and enable its trusted module in Tools → Plugins.");
    else if (found->version != ref.version ||
             (!ref.fingerprint.empty() &&
              found->fingerprint != ref.fingerprint))
      record("Project plugin version or binary differs: " + text(ref.id) +
             ". Review the enabled module before rendering.");
  }
  refreshSelection();
}
} // namespace apo::editor
