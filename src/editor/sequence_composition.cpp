#include "sequence_composition.hpp"
#include "document_session.hpp"
#include "../animation_export.hpp"
#include "../project_io.hpp"
#include <QEventLoop>
#include <QTimer>
#include <future>
#include <mutex>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QImageReader>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QVBoxLayout>
#include <stdexcept>

namespace apo::editor {
namespace {
[[noreturn]] void fail(const QString &message) { throw std::runtime_error(message.toStdString()); }

struct Layer {
    QStringList paths;
    QList<int> numbers;
};

Layer readLayer(const QString &first) {
    const QFileInfo info(first);
    const QRegularExpression pattern("^(.*_)([0-9]{6})\\.png$");
    const auto start = pattern.match(info.fileName());
    if (!start.hasMatch() || !info.isFile()) fail("Select a numbered PNG such as frame_000000.png.");
    Layer layer;
    const auto dir = info.absoluteDir();
    for (const auto &name : dir.entryList({"*.png"}, QDir::Files, QDir::Name)) {
        const auto match = pattern.match(name);
        if (!match.hasMatch() || match.captured(1) != start.captured(1) || name < info.fileName()) continue;
        layer.paths.append(dir.absoluteFilePath(name));
        layer.numbers.append(match.captured(2).toInt());
    }
    if (layer.paths.isEmpty() || layer.paths.size() > 1000) fail("Select a sequence of 1 to 1,000 frames.");
    if (layer.numbers.size() > 2) {
        const int step = layer.numbers[1] - layer.numbers[0];
        for (int i = 2; i < layer.numbers.size(); ++i)
            if (layer.numbers[i] - layer.numbers[i-1] != step) fail("Source frame numbering has an irregular gap.");
    }
    return layer;
}
}

void composeSequence(const QStringList &layers, const QString &destination, bool video,
                     unsigned fpsNum, unsigned fpsDen, std::uint64_t samples, std::stop_token stop,
                     const std::function<void(QString,int,int)> &progress) {
    if (layers.empty() || !fpsNum || !fpsDen) fail("Add a layer and enter a positive frame rate.");
    const QFileInfo target(destination);
    if (target.exists() || target.isSymLink()) fail("Choose a new output name.");
    const QString ffmpeg = video ? QStandardPaths::findExecutable("ffmpeg") : QString{};
    if (video && ffmpeg.isEmpty()) fail("MP4 export requires ffmpeg on PATH.");
    QTemporaryDir sources;
    if (!sources.isValid()) fail("Cannot create temporary layer directory.");
    auto update = [&](const QString &phase, int index, int total) {
        if (stop.stop_requested()) fail("Export cancelled.");
        if (progress) progress(phase, index, total);
    };
    QList<Layer> inputs;
    for (const auto &first : layers) {
        if (first.endsWith(".apoproject", Qt::CaseInsensitive)) {
            const auto project = load_project(filesystemPath(first));
            if (!project.animation_data) fail("Layer project needs a saved animation timeline: " + first);
            PreparedAnimation animation(parse_animation(*project.animation_data), stop);
            AnimationExportOptions settings;
            const auto directory = sources.filePath(QString::number(inputs.size()));
            if (!QDir().mkpath(directory)) fail("Cannot create temporary project frame directory.");
            settings.output_directory = filesystemPath(directory);
            settings.last = animation.clip().frame_count - 1;
            settings.file.image.format = ImageFormat::png16;
            settings.file.image.alpha = true;
            settings.render.samples = samples;
            settings.render.stop_token = stop;
            const auto label = "Rendering " + QFileInfo(first).fileName();
            settings.progress = [&](const AnimationExportProgress &p) {
                update(label, int(p.completed), int(p.total));
            };
            export_animation_sequence(animation, settings);
            inputs.push_back(readLayer(QDir(directory).filePath("frame_000000.png")));
        } else {
            inputs.push_back(readLayer(first));
        }
        if (inputs.back().numbers != inputs.front().numbers) fail("Layers must have matching frame numbers.");
    }
    QTemporaryDir work(target.absoluteDir().filePath(".apo-compose-XXXXXX"));
    if (!work.isValid()) fail("Cannot create a temporary output directory.");
    const int count = int(inputs.front().paths.size());

    QSize canvas;
    for (int frame = 0; frame < count; ++frame) {
        update("Composing frames…", frame, count);
        QImage composite;
        for (const auto &input : inputs) {
            const auto &path = input.paths[frame];
            QImageReader reader(path, "PNG");
            if (!canvas.isValid()) canvas = reader.size();
            if (!canvas.isValid() || reader.size() != canvas) fail("Frame dimensions differ or are unreadable: " + path);
            const auto image = reader.read();
            if (image.isNull()) fail("Cannot read " + path + ": " + reader.errorString());
            if (composite.isNull()) {
                composite = QImage(canvas, QImage::Format_RGBA64_Premultiplied);
                if (composite.isNull()) fail("Insufficient memory for composition.");
                composite.fill(video ? Qt::black : Qt::transparent);
            }
            QPainter painter(&composite);
            painter.drawImage(0, 0, image);
        }
        const auto number = video ? frame : inputs.front().numbers[frame];
        const QString name = QString("frame_%1.png").arg(number, 6, 10, QChar('0'));
        if (!composite.save(work.filePath(name), "PNG")) fail("Cannot write composed frame " + name);
    }
    update(video ? "Encoding MP4…" : "Publishing frames…", count, count);
    if (!video) {
        if (!QDir().rename(work.path(), target.absoluteFilePath())) fail("Cannot publish the frame directory.");
        work.setAutoRemove(false);
        return;
    }
    QProcess encoder;
    encoder.setWorkingDirectory(work.path());
    encoder.start(ffmpeg, {"-nostdin", "-loglevel", "error", "-framerate",
        QString("%1/%2").arg(fpsNum).arg(fpsDen), "-i", "frame_%06d.png", "-c:v", "libx264",
        "-pix_fmt", "yuv420p", "-vf", "pad=ceil(iw/2)*2:ceil(ih/2)*2", "-movflags", "+faststart", "video.mp4"});
    if (!encoder.waitForStarted()) fail("Cannot start FFmpeg: " + encoder.errorString());
    while (!encoder.waitForFinished(100) && encoder.state() != QProcess::NotRunning) {
        if (stop.stop_requested()) {
            encoder.kill(); encoder.waitForFinished();
            fail("Export cancelled.");
        }
    }
    if (encoder.exitStatus() != QProcess::NormalExit || encoder.exitCode() != 0)
        fail("FFmpeg export failed: " + QString::fromUtf8(encoder.readAllStandardError()).right(2000));
    if (stop.stop_requested()) fail("Export cancelled.");
    if (!QFile::rename(work.filePath("video.mp4"), target.absoluteFilePath())) fail("Cannot publish the MP4.");
}

void showSequenceComposer(QWidget *parent) {
    QDialog dialog(parent);
    dialog.setWindowTitle("Compose image sequences");
    auto layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel("Add animated flame projects or numbered PNG sequences, bottom layer first. Drag layers to reorder."));
    auto list = new QListWidget;
    list->setDragDropMode(QAbstractItemView::InternalMove);
    layout->addWidget(list);
    auto buttons = new QHBoxLayout;
    auto add = new QPushButton("Add layer…"), remove = new QPushButton("Remove layer");
    buttons->addWidget(add); buttons->addWidget(remove); layout->addLayout(buttons);
    QObject::connect(add, &QPushButton::clicked, &dialog, [&] {
        auto file = QFileDialog::getOpenFileName(&dialog, "Select animated project or first PNG", {}, "Layer (*.apoproject *.png)");
        if (!file.isEmpty()) list->addItem(file);
    });
    QObject::connect(remove, &QPushButton::clicked, &dialog, [&] { delete list->takeItem(list->currentRow()); });
    auto form = new QFormLayout;
    auto numerator = new QSpinBox, denominator = new QSpinBox;
    numerator->setRange(1, 240000); numerator->setValue(30);
    denominator->setRange(1, 10000);
    form->addRow("Playback FPS numerator", numerator);
    form->addRow("Playback FPS denominator", denominator);
    auto samples = new QSpinBox;
    samples->setRange(1, 1000000000); samples->setValue(1000000);
    form->addRow("Samples per project frame", samples);
    layout->addLayout(form);
    auto actions = new QDialogButtonBox(QDialogButtonBox::Close);
    layout->addWidget(actions);
    QObject::connect(actions, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    for (bool video : {false, true}) {
        auto button = actions->addButton(video ? "Export MP4…" : "Export PNG frames…", QDialogButtonBox::ActionRole);
        QObject::connect(button, &QPushButton::clicked, &dialog, [&, video] {
            QStringList layers;
            for (int i = 0; i < list->count(); ++i) layers.append(list->item(i)->text());
            if (layers.empty()) { QMessageBox::information(&dialog, "Composition", "Add a layer first."); return; }
            auto dest = QFileDialog::getSaveFileName(&dialog, video ? "New MP4 file" : "New output folder name",
                video ? "composition.mp4" : "composed-frames", video ? "MP4 video (*.mp4)" : QString{});
            if (dest.isEmpty()) return;
            if (video && !dest.endsWith(".mp4", Qt::CaseInsensitive)) dest += ".mp4";
            QProgressDialog progress("Composing frames…", "Cancel", 0, 100, &dialog);
            progress.setWindowModality(Qt::WindowModal); progress.setAutoClose(false);
            progress.setAutoReset(false); progress.setMinimumDuration(0);
            std::stop_source cancel;
            std::mutex mutex;
            QString phase = "Preparing layers…";
            int percent = 0;
            auto task = std::async(std::launch::async, [&, num = numerator->value(), den = denominator->value(),
                                                        budget = samples->value()] {
                composeSequence(layers, dest, video, num, den, budget, cancel.get_token(),
                    [&](QString label, int i, int total) {
                        std::lock_guard lock(mutex);
                        phase = std::move(label); percent = i * 100 / total;
                    });
            });
            QEventLoop wait;
            QTimer timer;
            QObject::connect(&progress, &QProgressDialog::canceled, &wait, [&] { cancel.request_stop(); });
            QObject::connect(&timer, &QTimer::timeout, &wait, [&] {
                QString label;
                int value;
                { std::lock_guard lock(mutex); label = phase; value = percent; }
                progress.setLabelText(label); progress.setValue(value);
                if (task.wait_for(std::chrono::seconds(0)) == std::future_status::ready) wait.quit();
            });
            timer.start(100); wait.exec(); timer.stop(); progress.close();
            try {
                task.get();
                QMessageBox::information(&dialog, "Export complete", dest);
            } catch (const std::exception &error) {
                if (!cancel.stop_requested()) QMessageBox::warning(&dialog, "Composition", QString::fromUtf8(error.what()));
            }
        });
    }
    dialog.exec();
}
}
