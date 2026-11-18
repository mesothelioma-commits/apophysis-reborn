#include "animation_smoke.hpp"
#include "animation_dialog.hpp"
#include "color_curve_dialog.hpp"
#include "main_window.hpp"
#include "../creative.hpp"
#include "../output_file.hpp"
#include "../sha256.hpp"
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <iostream>

namespace apo::editor {
namespace {
void fail(const std::exception &error) {
  std::cerr << "Animation acceptance: " << error.what() << '\n';
  QCoreApplication::exit(1);
}
}
void animationSmoke(MainWindow &window, const QString &path) {
  auto source = window.session().document();
  const auto selected = std::size_t(window.session().selectedFlame());
  auto start = source.render_model(selected);
  start.curves.mode = ColorCurveMode::continuous;
  source.apply_model(selected, start);
  AnimationClip clip;
  clip.name = "Ember · camera and color study";
  clip.rate = {24, 1}; clip.frame_count = 25; clip.seed = UINT64_MAX;
  clip.palette = AnimationPalette::hsv_shortest;
  clip.loop = AnimationLoop::ping_pong;
  clip.keyframes.push_back(capture_animation_keyframe(source, selected, 0, "Ember"));
  auto middle = start;
  middle.rotate += 35; middle.zoom += .22;
  middle.palette = creative::rotate_palette(start.palette, 32);
  middle.curves.channels[0][1] = {.22, .35, 1};
  middle.curves.channels[0][2] = {.78, .88, 1};
  auto middleSource = source; middleSource.apply_model(selected, middle);
  clip.keyframes.push_back(capture_animation_keyframe(middleSource, selected, 12, "Warm light"));
  auto end = middle; end.rotate += 40; end.zoom += .22;
  end.palette = creative::rotate_palette(start.palette, 64);
  end.curves.channels[0][1] = {.18, .42, 1};
  auto endSource = source; endSource.apply_model(selected, end);
  clip.keyframes.push_back(capture_animation_keyframe(endSource, selected, 24, "Gold"));
  window.applyAnimation(clip);
  window.session().undoStack().undo();
  if (window.session().hasAnimation()) throw std::runtime_error("Timeline undo did not restore the original document");
  window.session().undoStack().redo();
  if (parse_animation(*window.session().projectSnapshot().animation_data) != clip)
    throw std::runtime_error("Timeline redo lost data");
  const auto native = path + "/animation.apoproject";
  window.session().save(native);
  window.findChild<QAction *>("actionSave")->trigger();
  if (parse_animation(*load_project(filesystemPath(native)).animation_data) != clip)
    throw std::runtime_error("Saved native timeline did not round-trip");
  auto settings = window.previewController().settings();
  settings.sample_limit = 120000; settings.threads = 2;
  settings.backend = RenderBackend::cpu; settings.debounce_ms = 1;
  auto dialog = new AnimationDialog(source, selected, clip, &window, settings);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  auto captured = std::make_shared<bool>(false);
  QObject::connect(dialog, &AnimationDialog::framePreviewed, &window,
                   [&window, path, clip, middle, dialog, captured](int frame) {
    if (*captured || frame != 12) return;
    *captured = true;
    QTimer::singleShot(80, &window, [&window, path, clip, middle, dialog] {
      try {
        if (!dialog->grab().save(path + "/timeline.png") ||
            !dialog->previewImage().save(path + "/timeline-frame.png"))
          throw std::runtime_error("Timeline evidence image could not be saved");
        dialog->close();
        auto curves = new ColorCurveDialog(middle.curves, &window);
        curves->setAttribute(Qt::WA_DeleteOnClose); curves->show();
        QTimer::singleShot(100, &window, [&window, path, clip, curves] {
          try {
            if (!curves->grab().save(path + "/color-curves.png"))
              throw std::runtime_error("Curve editor evidence image could not be saved");
            curves->close();
            AnimationExportOptions options;
            if (!QDir().mkpath(path + "/frames"))
              throw std::runtime_error("Could not create the sequence output folder");
            options.output_directory = filesystemPath(path + "/frames");
            options.first = 0; options.last = 24; options.step = 6;
            options.width = 320; options.height = 240;
            options.render.samples = 120000; options.render.threads = 2;
            options.render.memory_mib = 64; options.render.backend = RenderBackend::cpu;
            options.file.image = {ImageFormat::png16, true}; options.file.disk_mib = 512;
            const auto ids = window.queueAnimation(clip, options);
            if (ids.size() != 5) throw std::runtime_error("The sequence did not queue all selected frames");
            auto completed = std::make_shared<std::size_t>(0);
            QObject::connect(&window.renderQueue(), &RenderQueue::jobFinished, &window,
                [&window, path, clip, ids, completed](const QString &id, bool success) {
              try {
                if (!success) {
                  const auto job = window.renderQueue().job(id);
                  throw std::runtime_error(job ? job->error.toStdString() : "Missing sequence job");
                }
                if (++*completed != ids.size()) return;
                QJsonArray frames;
                for (const auto &key : ids) {
                  const auto job = window.renderQueue().job(key);
                  if (!job || job->state != RenderJobState::complete)
                    throw std::runtime_error("An animation frame did not complete");
                  frames.append(QJsonObject{{"name", job->name},
                                             {"filename", QFileInfo(job->destination).fileName()},
                                             {"samples", QString::number(job->completed_samples)},
                                             {"seed", QString::number(job->render.seed)},
                                             {"backend", job->backend},
                                             {"sha256", QString::fromStdString(detail::sha256_file(filesystemPath(job->destination)))}});
                }
                window.renderQueue().saveTo(path + "/animation-queue.json");
                const QJsonObject report{{"version", "0.9.0"},
                                         {"qt_platform", QGuiApplication::platformName()},
                                         {"frame_count", int(clip.frame_count)}, {"fps", 24},
                                         {"queued_frames", int(ids.size())},
                                         {"native_project_roundtrip", true},
                                         {"timeline_undo_verified", true}, {"queue_rendered", true},
                                         {"format", "png16"}, {"alpha", true},
                                         {"width", 320}, {"height", 240}, {"frames", frames}};
                const auto bytes = QJsonDocument(report).toJson();
                write_atomic(filesystemPath(path + "/animation-report.json"),
                             [&](std::ostream &out) { out << bytes.toStdString(); });
                QTimer::singleShot(120, &window, [&window, path] {
                  auto queue = window.findChild<RenderQueueDialog *>();
                  const bool ok = queue && queue->grab().save(path + "/animation-queue.png");
                  window.session().undoStack().setClean();
                  QCoreApplication::exit(ok ? 0 : 1);
                });
              } catch (const std::exception &error) { fail(error); }
            });
            window.showRenderQueue(); window.renderQueue().start();
          } catch (const std::exception &error) { fail(error); }
        });
      } catch (const std::exception &error) { fail(error); }
    });
  });
  dialog->resize(1240, 820);
  dialog->show(); dialog->setFrame(12);
}
} // namespace apo::editor
