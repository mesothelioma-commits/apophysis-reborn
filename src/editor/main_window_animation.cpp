#include "main_window.hpp"
#include "animation_dialog.hpp"
#include "../output_file.hpp"
#include "../plugin.hpp"
#include "../project_document.hpp"
#include "../sha256.hpp"
#include <QFileDialog>
#include <QFileInfo>
#include <QStandardPaths>
#include <limits>
#include <ostream>

namespace apo::editor {
namespace {
QString text(std::string_view s) { return QString::fromUtf8(s.data(), qsizetype(s.size())); }
QString pathText(const std::filesystem::path &path) {
#ifdef _WIN32
  return QString::fromStdWString(path.wstring());
#else
  return text(path.native());
#endif
}
void ensureRecognizedAnimation(const ProjectFile &project) {
  if (project.animation_data) {
    if (!is_animation_payload(*project.animation_data))
      throw std::invalid_argument("This project contains an unrecognized animation payload. Its bytes are preserved; this timeline editor cannot replace it.");
    (void)parse_animation(*project.animation_data);
  }
}
} // namespace
void MainWindow::showAnimationTimeline() {
  if (!session_.hasDocument()) return;
  run([&] {
    const auto project = session_.projectSnapshot();
    ensureRecognizedAnimation(project);
    std::optional<AnimationClip> clip;
    if (project.animation_data) clip = parse_animation(*project.animation_data);
    const bool paused = preview_.isPaused();
    preview_.setPaused(true);
    // Restore the main preview on both normal completion and exceptions.
    struct Restore {
      PreviewController &preview; bool paused;
      ~Restore() { preview.setPaused(paused); }
    } restore{preview_, paused};
    AnimationDialog dialog(session_.document(), std::size_t(session_.selectedFlame()),
                           std::move(clip), this, preview_.settings());
    if (dialog.exec() != QDialog::Accepted) return;
    if (dialog.disposition() == AnimationDialog::Disposition::queue) {
      const auto ids = queueAnimation(dialog.clip(), dialog.exportOptions());
      record(QString("Added %1 animation frame(s) to the render queue.").arg(ids.size()));
      showRenderQueue();
    } else {
      if (dialog.disposition() == AnimationDialog::Disposition::use_frame) {
        const auto current = dialog.currentFlame();
        if (!current) throw std::invalid_argument("No evaluated frame is available");
        const auto selected = std::size_t(session_.selectedFlame());
        auto candidate = session_.document();
        candidate.apply_model(selected, *current);
        // Preflight the source-preserving model replacement before the timeline
        // changes. Incompatible producer snapshots must leave both untouched.
        applyAnimation(dialog.clip());
        session_.change("Use animation frame in editor", [&](auto &document) {
          document = candidate;
        });
        record(QString("Loaded frame %1 for editing. Capture it in the timeline to update a keyframe.").arg(dialog.frame()));
      } else applyAnimation(dialog.clip());
    }
  });
}
void MainWindow::applyAnimation(const AnimationClip &clip) {
  if (!session_.hasDocument()) throw std::invalid_argument("Open a flame or project before applying a timeline");
  ensureRecognizedAnimation(session_.projectSnapshot());
  session_.changeAnimation("Apply animation timeline", serialize_animation(clip));
  record(QString("Applied timeline · %1 keyframe(s), %2 frames. Save as .apoproject to retain the animation.")
             .arg(clip.keyframes.size()).arg(clip.frame_count));
}
std::vector<QString> MainWindow::queueAnimation(const AnimationClip &clip,
                                               AnimationExportOptions options) {
  if (!session_.hasDocument()) throw std::invalid_argument("Open a flame or project before queueing a timeline");
  ensureRecognizedAnimation(session_.projectSnapshot());
  const auto payload = serialize_animation(clip);
  auto candidate = session_.projectSnapshot(); candidate.animation_data = payload;
  (void)serialize_project(candidate);
  if (options.resume) throw std::invalid_argument("Use the render queue's resume controls for queued frames");
  auto &inputs = options.protected_files;
  if (!session_.path().isEmpty()) inputs.push_back(filesystemPath(session_.path()));
  for (const auto &path : queue_->protectedPaths()) inputs.push_back(filesystemPath(path));
  if (!session_.recoveryPath().isEmpty())
    for (const auto &suffix : QStringList{QString{}, ".json", ".lock"})
      inputs.push_back(filesystemPath(session_.recoveryPath() + suffix));
  for (const auto location : {QStandardPaths::AppLocalDataLocation, QStandardPaths::AppConfigLocation}) {
    const auto path = QStandardPaths::writableLocation(location);
    if (!path.isEmpty()) inputs.push_back(filesystemPath(path));
  }
  for (const auto &plugin : enabled_plugins()) {
    inputs.push_back(plugin.manifest_path); inputs.push_back(plugin.library_path);
  }
  for (const auto &job : queue_->jobs())
    if (job.state != RenderJobState::complete && job.state != RenderJobState::cancelled && job.state != RenderJobState::failed)
      inputs.push_back(filesystemPath(job.destination));
  const PreparedAnimation prepared(clip);
  const auto plan = plan_animation_export(prepared, options);
  if (queue_->jobs().size() + plan.frames.size() > 1000)
    throw std::invalid_argument("This batch would exceed the 1,000-job queue limit. Remove old jobs or select a smaller frame range.");
  // Queue checkpoints are retained after completion, unlike CLI sequence state.
  // Reserve all of their conservative scratch bounds for the selected batch.
  std::uint64_t retained = plan.output_bytes_upper_bound;
  for (const auto &frame : plan.frames) {
    if (frame.render.disk_bytes > std::numeric_limits<std::uint64_t>::max() - retained)
      throw std::invalid_argument("Animation batch disk estimate overflow");
    retained += frame.render.disk_bytes;
  }
  constexpr std::uint64_t mib = 1024 * 1024;
  if (options.file.disk_mib <= std::numeric_limits<std::uint64_t>::max() / mib &&
      retained > options.file.disk_mib * mib)
    throw std::invalid_argument("The batch exceeds its disk budget when all queue checkpoints are retained. Increase the budget or select fewer frames.");
  std::vector<RenderJob> jobs;
  for (const auto &frame : plan.frames) {
    RenderJob job;
    job.name = text(clip.name) + QString(" · frame %1").arg(frame.frame);
    job.flame = animation_export_flame(prepared.evaluate(frame.frame), plan.width, plan.height);
    job.render = options.render; job.render.seed = frame.seed;
    job.file = options.file;
    job.file.checkpoint_directory.clear(); job.file.resume = false;
    job.destination = pathText(frame.destination); job.source = session_.path();
    job.destination_sha256 = std::filesystem::exists(frame.destination)
                                 ? detail::sha256_file(frame.destination)
                                 : std::string{};
    jobs.push_back(std::move(job));
  }
  // All paths, frame models, budgets and payload bytes have passed validation.
  // Queue persistence is atomic; only then make the timeline one undoable edit.
  std::filesystem::create_directories(plan.output_directory);
  const auto ids = queue_->addBatch(jobs);
  session_.changeAnimation("Apply animation timeline", payload);
  return ids;
}
void MainWindow::exportFlameLibrary() {
  if (!session_.hasDocument()) return;
  run([&] {
    const auto path = QFileDialog::getSaveFileName(this, "Export flame library", {},
                                                  "Flame libraries (*.flame);;XML files (*.xml)");
    if (path.isEmpty()) return;
    const auto destination = filesystemPath(path);
    if (is_project_path(destination)) throw std::invalid_argument("Choose a .flame or .xml filename for a library export");
    if (!session_.path().isEmpty()) {
      const auto source = filesystemPath(session_.path());
      std::error_code error;
      const bool same = std::filesystem::equivalent(destination, source, error);
      if (same || QFileInfo(path).absoluteFilePath() == session_.path())
        throw std::invalid_argument("Choose a different filename for the exported library");
    }
    const auto &bytes = session_.document().bytes();
    write_atomic(destination, [&](std::ostream &out) { out << bytes; });
    record("Exported source-preserving flame library " + path);
  });
}
} // namespace apo::editor
