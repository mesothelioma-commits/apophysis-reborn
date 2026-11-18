#include "creative_widgets.hpp"
#include "extensions_smoke.hpp"
#include "flame_io.hpp"
#include "main_window.hpp"
#include "output_file.hpp"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCommandLineParser>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <algorithm>
#include <iostream>

namespace {
void largeRenderSmoke(apo::editor::MainWindow &window, const QString &path) {
  using namespace apo;
  using namespace apo::editor;
  struct Evidence {
    QElapsedTimer clock;
    qint64 last_tick = 0, maximum_gap = 0, pause_requested_at = 0,
           pause_confirmed_at = 0;
    unsigned heartbeats = 0;
    std::uint64_t paused_samples = 0;
    bool pause_requested = false, pause_confirmed = false,
         resume_requested = false, finished = false;
  };
  auto evidence = std::make_shared<Evidence>();
  auto &queue = window.renderQueue();
  auto flame = window.session().document().render_model(0);
  if (flame.width != 768 || flame.height != 576)
    throw std::runtime_error("The large-render fixture must be Ember spiral");
  flame.scale *= 16384.0 / 576.0;
  flame.width = flame.height = 16384;
  flame.oversample = 1;
  flame.filter_radius = 0.5;
  RenderOptions options;
  options.samples = 8388611;
  options.seed = 123456;
  options.memory_mib = 128;
  options.threads = 2;
  options.backend = RenderBackend::cpu;
  FileRenderOptions file;
  file.disk_mib = 8192;
  file.image = {ImageFormat::png16, true};
  const auto plan = plan_file_render(flame, options, file);
  const auto output = path + "/16k.png";
  const QByteArray previous("Existing output survives a durable pause.\n");
  write_atomic(filesystemPath(output),
               [&](std::ostream &out) { out << previous.toStdString(); });
  const auto id =
      queue.add(flame, options, file, output, window.session().path());
  auto pulse = new QTimer(&window);
  pulse->setTimerType(Qt::PreciseTimer);
  pulse->setInterval(10);
  evidence->clock.start();
  QObject::connect(pulse, &QTimer::timeout, &window, [evidence] {
    const auto now = evidence->clock.elapsed();
    evidence->maximum_gap =
        std::max(evidence->maximum_gap, now - evidence->last_tick);
    evidence->last_tick = now;
    ++evidence->heartbeats;
  });
  pulse->start();
  QObject::connect(
      &queue, &RenderQueue::changed, &window,
      [&window, path, output, previous, id, plan, evidence, pulse] {
        if (evidence->finished)
          return;
        try {
          auto &queue = window.renderQueue();
          const auto job = queue.job(id);
          if (!job)
            throw std::runtime_error("The 16K acceptance job disappeared");
          if (job->state == RenderJobState::failed)
            throw std::runtime_error(job->error.toStdString());
          if (!evidence->pause_requested && job->completed_samples > 0 &&
              job->state == RenderJobState::running) {
            evidence->pause_requested = true;
            evidence->pause_requested_at = evidence->clock.elapsed();
            queue.pause();
            return;
          }
          if (job->state == RenderJobState::paused &&
              !evidence->resume_requested && !evidence->pause_confirmed) {
            QFile existing(output);
            if (queue.isBusy() || queue.isRunning() || !job->checkpointed ||
                !has_render_checkpoint(filesystemPath(job->checkpoint)) ||
                !existing.open(QIODevice::ReadOnly) ||
                existing.readAll() != previous)
              throw std::runtime_error(
                  "Pause did not preserve the destination and a valid stopped "
                  "checkpoint");
            evidence->pause_confirmed = true;
            evidence->pause_confirmed_at = evidence->clock.elapsed();
            evidence->paused_samples = job->completed_samples;
            queue.saveTo(path + "/paused-queue.json");
            QTimer::singleShot(100, &window, [&window, path, evidence] {
              try {
                auto dialog =
                    window.findChild<RenderQueueDialog *>("renderQueueDialog");
                if (!dialog ||
                    !dialog->grab().save(path + "/16k-queue-paused.png"))
                  throw std::runtime_error(
                      "Could not save the paused queue screenshot");
                evidence->resume_requested = true;
                window.renderQueue().start();
              } catch (const std::exception &error) {
                evidence->finished = true;
                std::cerr << error.what() << '\n';
                QCoreApplication::exit(1);
              }
            });
            return;
          }
          if (job->state != RenderJobState::complete)
            return;
          evidence->finished = true;
          pulse->stop();
          if (!evidence->pause_confirmed || !job->resumed ||
              job->completed_samples != 8388611 || !evidence->heartbeats ||
              evidence->maximum_gap > 1000 ||
              QFileInfo(output).size() <= previous.size())
            throw std::runtime_error(
                "The 16K acceptance did not meet pause/resume and event-loop "
                "responsiveness checks");
          queue.saveTo(path + "/completed-queue.json");
          const auto report = QJsonObject{
              {"width", job->flame.width},
              {"height", job->flame.height},
              {"format", imageFormatText(job->file.image.format)},
              {"alpha", job->file.image.alpha},
              {"samples", QString::number(job->completed_samples)},
              {"plotted", QString::number(job->plotted)},
              {"invalid", QString::number(job->invalid)},
              {"seed", QString::number(job->render.seed)},
              {"backend", job->backend},
              {"device", job->device},
              {"workers", int(job->workers)},
              {"fallback_reason", job->fallback},
              {"host_budget_mib", QString::number(job->render.memory_mib)},
              {"disk_budget_mib", QString::number(job->file.disk_mib)},
              {"planned_host_bytes", QString::number(plan.host_bytes)},
              {"planned_disk_bytes", QString::number(plan.disk_bytes)},
              {"strip_rows", int(plan.strip_rows)},
              {"completed_strips", int(job->completed_strips)},
              {"total_strips", int(job->total_strips)},
              {"pause_requested_after_committed_samples", true},
              {"pause_confirmed_with_valid_checkpoint", true},
              {"existing_destination_preserved_at_pause", true},
              {"samples_at_pause", QString::number(evidence->paused_samples)},
              {"pause_stop_latency_ms", double(evidence->pause_confirmed_at -
                                               evidence->pause_requested_at)},
              {"resumed", job->resumed},
              {"elapsed_seconds", job->elapsed_seconds},
              {"wall_elapsed_ms", double(evidence->clock.elapsed())},
              {"ui_heartbeat_interval_ms", 10},
              {"ui_heartbeats", int(evidence->heartbeats)},
              {"maximum_ui_heartbeat_gap_ms", double(evidence->maximum_gap)},
              {"qt_platform", QGuiApplication::platformName()},
              {"main_window_visible", window.isVisible()},
              {"checkpoint", job->checkpoint},
              {"destination", job->destination}};
          const auto bytes = QJsonDocument(report).toJson();
          write_atomic(filesystemPath(path + "/16k-report.json"),
                       [&](std::ostream &out) { out << bytes.toStdString(); });
          QTimer::singleShot(100, &window, [&window, path] {
            auto dialog =
                window.findChild<RenderQueueDialog *>("renderQueueDialog");
            const bool saved =
                dialog && dialog->isVisible() &&
                dialog->grab().save(path + "/16k-queue-complete.png") &&
                window.grab().save(path + "/16k-editor-complete.png");
            QCoreApplication::exit(saved ? 0 : 1);
          });
        } catch (const std::exception &error) {
          evidence->finished = true;
          pulse->stop();
          std::cerr << error.what() << '\n';
          QCoreApplication::exit(1);
        }
      });
  window.showRenderQueue();
  queue.start();
}

void queueSmoke(apo::editor::MainWindow &window, const QString &path) {
  using namespace apo;
  using namespace apo::editor;
  auto &queue = window.renderQueue();
  auto flame = window.session().document().render_model(0);
  flame.scale *= 192.0 / flame.width;
  flame.width = 192;
  flame.height = 144;
  flame.oversample = 2;
  flame.filter_radius = 0.5;
  RenderOptions options;
  const auto settings = window.previewController().settings();
  options.samples = 262145;
  options.seed = settings.seed;
  options.memory_mib = 64;
  options.threads = 2;
  options.backend = settings.backend;
  options.device = settings.device;
  options.gpu_memory_mib = settings.gpu_memory_mib;
  options.allow_cpu_fallback = settings.allow_cpu_fallback;
  FileRenderOptions file;
  file.disk_mib = 256;
  file.strip_rows = 19;
  file.image = {ImageFormat::png16, true};
  flame.name = "Transparent PNG16 study";
  queue.add(flame, options, file, path + "/queue-render-16.png",
            window.session().path());
  file.image = {ImageFormat::tiff16, false};
  flame.name = "Opaque TIFF16 study";
  queue.add(flame, options, file, path + "/queue-render-16.tif",
            window.session().path());
  auto heartbeat = std::make_shared<unsigned>(0);
  auto pulse = new QTimer(&window);
  pulse->setInterval(10);
  QObject::connect(pulse, &QTimer::timeout, &window,
                   [heartbeat] { ++*heartbeat; });
  pulse->start();
  auto finished = std::make_shared<bool>(false);
  QObject::connect(
      &queue, &RenderQueue::changed, &window,
      [&window, path, heartbeat, finished, pulse] {
        if (*finished)
          return;
        const auto &jobs = window.renderQueue().jobs();
        for (const auto &job : jobs)
          if (job.state == RenderJobState::failed) {
            *finished = true;
            std::cerr << job.error.toStdString() << '\n';
            QCoreApplication::exit(1);
            return;
          }
        if (jobs.size() != 2 ||
            !std::all_of(jobs.begin(), jobs.end(), [](const auto &job) {
              return job.state == RenderJobState::complete;
            }))
          return;
        *finished = true;
        pulse->stop();
        QTimer::singleShot(100, &window, [&window, path, heartbeat] {
          try {
            auto dialog =
                window.findChild<RenderQueueDialog *>("renderQueueDialog");
            if (!dialog || !dialog->grab().save(path + "/render-queue.png") ||
                *heartbeat == 0)
              throw std::runtime_error("Queue smoke did not produce its window "
                                       "or responsive event-loop evidence");
            window.renderQueue().saveTo(path + "/render-queue.json");
            QJsonArray reports;
            for (const auto &job : window.renderQueue().jobs())
              reports.append(QJsonObject{
                  {"name", job.name},
                  {"backend", job.backend},
                  {"device", job.device},
                  {"fallback_reason", job.fallback},
                  {"format", imageFormatText(job.file.image.format)},
                  {"alpha", job.file.image.alpha},
                  {"samples", QString::number(job.completed_samples)},
                  {"resumed", job.resumed},
                  {"destination", job.destination},
                  {"elapsed_seconds", job.elapsed_seconds}});
            const auto bytes =
                QJsonDocument(QJsonObject{{"jobs", reports},
                                          {"ui_heartbeats", int(*heartbeat)}})
                    .toJson();
            write_atomic(
                filesystemPath(path + "/queue-report.json"),
                [&](std::ostream &out) { out << bytes.toStdString(); });
            dialog->hide();
            QTimer::singleShot(200, &window, [&window, path] {
              auto settings =
                  qobject_cast<QDialog *>(QApplication::activeModalWidget());
              const bool saved =
                  settings &&
                  settings->objectName() == "renderSettingsDialog" &&
                  settings->grab().save(path + "/high-resolution-settings.png");
              if (settings)
                settings->reject();
              QCoreApplication::exit(saved ? 0 : 1);
            });
            auto action = window.findChild<QAction *>("actionRender");
            if (!action)
              throw std::runtime_error("Render settings action is unavailable");
            action->trigger();
          } catch (const std::exception &error) {
            std::cerr << error.what() << '\n';
            QCoreApplication::exit(1);
          }
        });
      });
  window.showRenderQueue();
  queue.start();
}

void creativeSmoke(apo::editor::MainWindow &window, const QString &path) {
  using namespace apo;
  using namespace apo::editor;
  struct State {
    int stage = 0;
    QPointer<QDialog> dialog;
  };
  auto state = std::make_shared<State>();
  const auto flame = window.session().document().render_model(0);
  auto gradient =
      new GradientDialog(flame, &window, window.previewController().settings());
  gradient->resize(1080, 730);
  gradient->findChild<QComboBox *>("palettePresets")->setCurrentIndex(1);
  gradient->findChild<QPushButton *>("usePalettePreset")->click();
  gradient->show();
  state->dialog = gradient;
  auto timer = new QTimer(&window);
  QObject::connect(
      timer, &QTimer::timeout, &window, [state, timer, &window, path, flame] {
        auto dialog = state->dialog.data();
        if (!dialog) {
          QCoreApplication::exit(1);
          return;
        }
        const auto canvas = dialog->findChild<PreviewCanvas *>();
        if (!canvas || canvas->image().isNull())
          return;
        for (auto controller : dialog->findChildren<PreviewController *>())
          if (controller->isBusy())
            return;
        const auto explorer = qobject_cast<ExploreDialog *>(dialog);
        if (explorer) {
          if (explorer->candidates().empty())
            return;
          for (std::size_t i = 0; i < explorer->candidates().size(); ++i) {
            const auto tile = explorer->findChild<QToolButton *>(
                QString("chooseCandidate%1").arg(i));
            if (!tile || tile->icon().isNull())
              return;
          }
        }
        try {
          const std::array names{"gradient-editor.png", "adjustment-editor.png",
                                 "mutation-explorer.png",
                                 "random-explorer.png"};
          if (!dialog->grab().save(path + '/' + names.at(state->stage)))
            throw std::runtime_error(
                "Could not save a creative editor screenshot");
          auto write = [&](const QString &name, const std::string &bytes) {
            write_atomic(filesystemPath(path + '/' + name),
                         [&](std::ostream &out) { out << bytes; });
          };
          if (auto gradient = qobject_cast<GradientDialog *>(dialog)) {
            gradient->exportPaletteFile(path + "/palette.gpl");
            write("gradient-example.flame",
                  serialize_flame(gradient->resultFlame()));
          } else if (auto adjustment =
                         qobject_cast<AdjustmentDialog *>(dialog)) {
            write("adjustment-example.flame",
                  serialize_flame(adjustment->resultFlame()));
          } else if (state->stage == 2) {
            auto document = window.session().document();
            const auto &candidate = explorer->candidates().front();
            applyCreativeFlame(document, 0, candidate);
            LegacyEdits edit;
            edit.name = candidate.name;
            document.edit(0, edit);
            write("mutation-example.flame", document.bytes());
          } else {
            const auto &candidates = explorer->candidates();
            auto document =
                LegacyDocument::parse(serialize_flame(candidates.front()));
            for (std::size_t i = 1; i < candidates.size(); ++i)
              document.append_flame(candidates[i]);
            write("random-batch.flame", document.bytes());
          }
          dialog->hide();
          dialog->deleteLater();
          ++state->stage;
          if (state->stage == 1) {
            auto next = new AdjustmentDialog(
                flame, &window, window.previewController().settings());
            next->resize(980, 680);
            next->findChild<QTabWidget *>("adjustmentTabs")->setCurrentIndex(1);
            state->dialog = next;
          } else if (state->stage < 4) {
            state->dialog = new ExploreDialog(
                state->stage == 2 ? ExploreDialog::Mode::mutation
                                  : ExploreDialog::Mode::random,
                flame, &window, window.previewController().settings());
            state->dialog->resize(1280, 790);
          } else {
            timer->stop();
            QCoreApplication::exit(0);
            return;
          }
          state->dialog->show();
        } catch (const std::exception &error) {
          std::cerr << error.what() << '\n';
          timer->stop();
          QCoreApplication::exit(1);
        }
      });
  timer->start(100);
}
} // namespace

int main(int argc, char **argv) {
  QApplication application(argc, argv);
  application.setOrganizationName("ApophysisReborn");
  application.setApplicationName("Apophysis Reborn");
  application.setApplicationVersion("0.8.0");
  QCommandLineParser parser;
  parser.setApplicationDescription("Native flame library and transform editor");
  parser.addHelpOption();
  parser.addVersionOption();
  parser.addPositionalArgument("flame", "Optional .flame library to open");
  QCommandLineOption smoke(
      "smoke-test",
      "Open the editor, render the example, save a screenshot and exit.",
      "directory");
  parser.addOption(smoke);
  QCommandLineOption creativeSmokeOption(
      "creative-smoke-test",
      "Render the main editor and all creative dialogs, save examples and "
      "screenshots, then exit.",
      "directory");
  parser.addOption(creativeSmokeOption);
  QCommandLineOption gpuSmokeOption(
      "gpu-smoke-test",
      "Require a Vulkan preview, save its renderer report and settings "
      "screenshot, then exit.",
      "directory");
  parser.addOption(gpuSmokeOption);
  QCommandLineOption queueSmokeOption("queue-smoke-test",
                                      "Render two queued 16-bit images, save "
                                      "queue/settings evidence, then exit.",
                                      "directory");
  parser.addOption(queueSmokeOption);
  QCommandLineOption largeSmokeOption(
      "large-render-smoke-test",
      "Render an actual 16384-square queued PNG16 with pause/resume and UI "
      "heartbeat evidence. Requires several GiB of free storage.",
      "directory");
  parser.addOption(largeSmokeOption);
  QCommandLineOption extensionsSmokeOption("extensions-smoke-test", "Run scripting, plugin, project and palette acceptance; requires an explicitly trusted SDK manifest.", "directory");
  QCommandLineOption pluginManifestOption("plugin-manifest", "Trusted native SDK manifest for extension smoke only.", "manifest");
  parser.addOptions({extensionsSmokeOption,pluginManifestOption});
  QCommandLineOption backendOption("backend", "Renderer: cpu, auto or vulkan.",
                                   "mode");
  QCommandLineOption deviceOption(
      "device", "Vulkan discovery ID or zero-based device index.", "device");
  QCommandLineOption gpuMemoryOption(
      "gpu-memory-mib", "Vulkan working-buffer budget in MiB.", "MiB");
  QCommandLineOption strictOption(
      "no-fallback",
      "Fail when Vulkan cannot render instead of using CPU fallback.");
  parser.addOptions(
      {backendOption, deviceOption, gpuMemoryOption, strictOption});
  parser.process(application);
  apo::editor::applyDarkTheme();
  try {
    const bool creative = parser.isSet(creativeSmokeOption);
    const bool gpu = parser.isSet(gpuSmokeOption);
    const bool queue = parser.isSet(queueSmokeOption);
    const bool large = parser.isSet(largeSmokeOption);
    const bool extensions = parser.isSet(extensionsSmokeOption);
    const auto manifest = parser.value(pluginManifestOption);
    if (parser.isSet(pluginManifestOption) && !extensions) throw std::invalid_argument("--plugin-manifest is restricted to extension smoke verification");
    const bool testing =
        parser.isSet(smoke) || creative || gpu || queue || large || extensions;
    if (int(parser.isSet(smoke)) + int(creative) + int(gpu) + int(queue) +
            int(large) + int(extensions) >
        1)
      throw std::invalid_argument("Choose only one smoke-test mode");
    const auto smokePath = parser.value(extensions ? extensionsSmokeOption
                                         : large ? largeSmokeOption
                                        : queue    ? queueSmokeOption
                                        : gpu      ? gpuSmokeOption
                                        : creative ? creativeSmokeOption
                                                   : smoke);
    apo::editor::MainWindow window(
        !testing, nullptr, large ? smokePath + "/queue-data" : QString{});
    auto configuration = window.previewController().settings();
    if (gpu) {
      configuration.backend = apo::RenderBackend::vulkan;
      configuration.allow_cpu_fallback = false;
    }
    if (parser.isSet(backendOption)) {
      const auto name = parser.value(backendOption);
      if (name == "cpu")
        configuration.backend = apo::RenderBackend::cpu;
      else if (name == "auto")
        configuration.backend = apo::RenderBackend::automatic;
      else if (name == "vulkan")
        configuration.backend = apo::RenderBackend::vulkan;
      else
        throw std::invalid_argument("--backend must be cpu, auto or vulkan");
    }
    if (parser.isSet(deviceOption))
      configuration.device = parser.value(deviceOption).toStdString();
    if (parser.isSet(gpuMemoryOption)) {
      bool ok = false;
      configuration.gpu_memory_mib =
          parser.value(gpuMemoryOption).toULongLong(&ok);
      if (!ok || !configuration.gpu_memory_mib)
        throw std::invalid_argument(
            "--gpu-memory-mib must be a positive integer");
    }
    if (parser.isSet(strictOption))
      configuration.allow_cpu_fallback = false;
    if (gpu && configuration.backend != apo::RenderBackend::vulkan)
      throw std::invalid_argument(
          "--gpu-smoke-test requires the Vulkan backend");
    window.previewController().setSettings(configuration);
    if (testing) {
      window.resize(1420, 900);
      const auto path = QDir(smokePath).absolutePath();
      if (!QDir().mkpath(path))
        throw std::runtime_error("Cannot create smoke-test output directory");
      auto started = std::make_shared<bool>(false);
      auto renderer = std::make_shared<QJsonObject>();
      QObject::connect(&window.previewController(),
                       &apo::editor::PreviewController::backendResolved,
                       &window,
                       [renderer](const QString &backend, const QString &device,
                                  const QString &fallback) {
                         renderer->insert("backend", backend);
                         renderer->insert("device_name", device);
                         renderer->insert("fallback_reason", fallback);
                       });
      QObject::connect(&window.previewController(),
                       &apo::editor::PreviewController::failed, &window,
                       [](const QString &error) {
                         std::cerr << error.toStdString() << '\n';
                         QCoreApplication::exit(1);
                       });
      QObject::connect(
          &window.previewController(),
          &apo::editor::PreviewController::imageReady, &window,
          [&window, path, creative, gpu, queue, large, extensions, manifest, started,
           renderer](const QImage &image, quint64, quint64, quint64, unsigned,
                     qint64, bool final) {
            if (!final || *started)
              return;
            *started = true;
            QTimer::singleShot(
                100, &window,
                [&window, path, image, creative, gpu, queue, large, extensions, manifest, renderer] {
                  bool ok = image.save(path + "/preview.png") &&
                            window.grab().save(path + "/editor.png");
                  try {
                    window.session().save(path + "/saved-example.flame");
                    if (gpu) {
                      ok = ok &&
                           renderer->value("backend").toString() == "Vulkan" &&
                           renderer->value("fallback_reason")
                               .toString()
                               .isEmpty();
                      const auto bytes = QJsonDocument(*renderer).toJson();
                      apo::write_atomic(apo::editor::filesystemPath(
                                            path + "/render-device.json"),
                                        [&](std::ostream &out) {
                                          out << bytes.toStdString();
                                        });
                    }
                  } catch (const std::exception &) {
                    ok = false;
                  }
                  if (ok && extensions) {
                    window.previewController().setPaused(true);
                    try { apo::editor::extensionsSmoke(window,path,manifest); }
                    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';QCoreApplication::exit(1);}
                  } else if (ok && (queue || large)) {
                    window.previewController().setPaused(true);
                    try {
                      if (large)
                        largeRenderSmoke(window, path);
                      else
                        queueSmoke(window, path);
                    } catch (const std::exception &error) {
                      std::cerr << error.what() << '\n';
                      QCoreApplication::exit(1);
                    }
                  } else if (ok && creative) {
                    window.previewController().setPaused(true);
                    creativeSmoke(window, path);
                  } else if (ok && gpu) {
                    window.previewController().setPaused(true);
                    QTimer::singleShot(150, &window, [&window, path] {
                      auto dialog = qobject_cast<QDialog *>(
                          QApplication::activeModalWidget());
                      const bool saved =
                          dialog &&
                          dialog->objectName() == "preferencesDialog" &&
                          dialog->grab().save(path + "/render-settings.png");
                      if (dialog)
                        dialog->reject();
                      QCoreApplication::exit(saved ? 0 : 1);
                    });
                    auto action =
                        window.findChild<QAction *>("actionPreferences");
                    if (action)
                      action->trigger();
                    else
                      QCoreApplication::exit(1);
                  } else
                    QCoreApplication::exit(ok ? 0 : 1);
                });
          });
      QTimer::singleShot(large                      ? 900000
                         : creative || gpu || queue || extensions ? 120000
                                                    : 30000,
                         &application, [] { QCoreApplication::exit(2); });
      window.openExample();
      window.show();
    } else {
      window.show();
      const auto args = parser.positionalArguments();
      if (!args.isEmpty())
        window.openFile(args.front());
      else {
        window.openExample();
        QTimer::singleShot(0, &window,
                           &apo::editor::MainWindow::recoverPreviousSession);
      }
    }
    return application.exec();
  } catch (const std::exception &error) {
    if (parser.isSet(smoke) || parser.isSet(creativeSmokeOption) ||
        parser.isSet(gpuSmokeOption) || parser.isSet(queueSmokeOption) ||
        parser.isSet(largeSmokeOption) || parser.isSet(extensionsSmokeOption)) {
      std::cerr << error.what() << '\n';
      return 1;
    }
    QMessageBox::critical(nullptr, "Apophysis Reborn could not start",
                          QString::fromUtf8(error.what()));
    return 1;
  }
}
