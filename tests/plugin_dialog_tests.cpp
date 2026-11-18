#include "editor/main_window.hpp"
#include "editor/plugin_dialog.hpp"
#include "editor/render_queue.hpp"
#include "flame_io.hpp"
#include "script.hpp"
#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <cmath>
#include <iostream>

namespace {
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
void approval(QSettings &settings, const QString &manifest, const QString &hash) {
    settings.beginWriteArray("plugins/approved", 1);
    settings.setArrayIndex(0);
    settings.setValue("manifest", manifest);
    settings.setValue("fingerprint", hash);
    settings.endArray();
    settings.sync();
}
QByteArray bytes(const QString &path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Cannot read queue fixture");
    return file.readAll();
}
void write(const QString &path, const QByteArray &content) {
    QFile file(path);
    require(file.open(QIODevice::WriteOnly) && file.write(content) == content.size(),
            "Cannot write queue fixture");
}
template <class F> void rejects(F action, const char *message) {
    try {
        action();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error(message);
}
void queueIdentity(QTemporaryDir &directory, const apo::PluginInfo &info) {
    using namespace apo;
    using namespace apo::editor;
    auto f = demo_flame();
    f.width = 37;
    f.height = 29;
    f.scale = 17;
    f.transforms[0].variations = {make_variation_term("sample_wave")};
    RenderOptions render;
    render.samples = 1000;
    render.threads = 1;
    FileRenderOptions file;
    file.image.format = ImageFormat::ppm8;
    file.disk_mib = 64;
    const auto state = directory.filePath("queue.json");
    RenderQueue queue(state, directory.filePath("checkpoints"));
    const auto id = queue.add(f, render, file, directory.filePath("output.ppm"));
    const auto original = bytes(state);
    auto object = QJsonDocument::fromJson(original).object();
    auto jobs = object["jobs"].toArray();
    auto job = jobs[0].toObject();
    require(job["plugin_identity"].toString() == QString::fromStdString(plugin_identity(f)),
            "Queue omitted native module fingerprint identity");
    job["plugin_identity"] = "different module";
    jobs[0] = job;
    object["jobs"] = jobs;
    const auto bad = directory.filePath("bad-queue.json");
    write(bad, QJsonDocument(object).toJson());
    rejects([&] { queue.loadFrom(bad); }, "Queue accepted changed native module identity");
    require(queue.jobs().size() == 1 && queue.jobs()[0].id == id && bytes(state) == original,
            "Failed changed-module queue restore replaced original jobs or recovery bytes");
    job.remove("plugin_identity");
    jobs[0] = job;
    object["jobs"] = jobs;
    write(bad, QJsonDocument(object).toJson());
    rejects([&] { queue.loadFrom(bad); }, "Queue accepted missing native module identity");
    disable_plugin("sample_wave");
    rejects([&] { queue.loadFrom(state); }, "Queue silently substituted disabled plugin");
    require(queue.jobs().size() == 1 && queue.jobs()[0].id == id && bytes(state) == original,
            "Missing-plugin queue restore changed original queue");
    enable_plugin(info.manifest_path, info.fingerprint);
    queue.loadFrom(state);
    require(queue.jobs().size() == 1 && queue.jobs()[0].id == id,
            "Queue could not restore after matching plugin was enabled");
}
void unchangedVariationEditor() {
    using namespace apo;
    using namespace apo::editor;
    auto f = demo_flame();
    f.width = 37;
    f.height = 29;
    f.scale = 17;
    f.transforms[0].variations = {
        make_variation_term("sample_wave", .12345678901234567,
                            {{"amplitude", .23456789012345678}, {"frequency", 3.1234567890123456}}),
        make_variation_term("noise", .05), make_variation_term("linear", .7)};
    MainWindow window(false);
    auto preview = window.previewController().settings();
    preview.sample_limit = 500;
    preview.threads = 1;
    window.previewController().setSettings(preview);
    window.session().loadBytes(serialize_flame(f));
    const auto original = window.session().document().bytes();
    const auto editor = window.findChild<QPushButton *>("editPluginVariations");
    require(editor, "Plugin variation editor entry is unavailable");
    bool accepted = false;
    QTimer::singleShot(0, &window, [&] {
        const auto dialog = window.findChild<QDialog *>("pluginVariationsDialog");
        const auto apply =
            dialog ? dialog->findChild<QPushButton *>("applyPluginVariations") : nullptr;
        accepted = apply != nullptr;
        if (apply)
            apply->click();
        else if (auto modal = QApplication::activeModalWidget())
            modal->close();
    });
    editor->click();
    require(accepted, "Plugin variation dialog could not be accepted");
    require(window.session().document().bytes() == original,
            "Accepting unchanged plugin values changed full-precision numbers or variation order");
    require(window.session().undoStack().count() == 0,
            "Unchanged plugin edit created an undo record");
}
void protectQueueFromExports(QTemporaryDir &directory) {
    using namespace apo;
    using namespace apo::editor;
    MainWindow window(false, nullptr, directory.filePath("protected-queue"));
    auto f = demo_flame();
    f.width = 37;
    f.height = 29;
    f.scale = 17;
    f.transforms[0].variations = {make_variation_term("sample_wave")};
    auto preview = window.previewController().settings();
    preview.sample_limit = 500;
    preview.threads = 1;
    window.previewController().setSettings(preview);
    window.session().loadBytes(serialize_flame(f));
    RenderOptions render;
    render.samples = 1000;
    render.threads = 1;
    FileRenderOptions file;
    file.image.format = ImageFormat::ppm8;
    file.disk_mib = 64;
    window.renderQueue().add(f, render, file, directory.filePath("protected-output.ppm"));
    const auto state = window.renderQueue().stateFile();
    const auto before = bytes(state);
    ScriptOptions script;
    script.allow_export_requests = true;
    const auto result =
        run_script(window.session().document(), 0, "apo.export_flame('render-queue.json')", script);
    rejects([&] { window.applyScriptResult(result, directory.filePath("protected-queue"), true); },
            "Script export was allowed to overwrite persistent render queue state");
    require(bytes(state) == before && window.renderQueue().jobs().size() == 1,
            "Script export damaged queue recovery or live jobs");
}
} // namespace
int main(int argc, char **argv) {
    const auto manifest = argc == 2 ? QString::fromLocal8Bit(argv[1]) : QString{};
    int appArgc = 1;
    QApplication app(appArgc, argv);
    using namespace apo;
    using namespace apo::editor;
    try {
        require(!manifest.isEmpty(), "plugin-dialog-tests requires the example manifest");
        QTemporaryDir directory;
        require(directory.isValid(), "Cannot create settings fixture directory");
        QSettings settings(directory.filePath("plugins.ini"), QSettings::IniFormat);
#ifdef _WIN32
        const auto source = std::filesystem::path(manifest.toStdWString());
#else
        const auto source = std::filesystem::path(manifest.toUtf8().constData());
#endif
        const auto info = inspect_plugin(source);
        const auto hash = QString::fromStdString(info.fingerprint);
        settings.setValue("plugins/searchPaths", QStringList{manifest});
        approval(settings, manifest, "");
        require(loadApprovedPlugins(settings).size() == 1 && enabled_plugins().empty(),
                "Missing approval fingerprint auto-enabled native code");
        approval(settings, manifest, QString(64, '0'));
        require(loadApprovedPlugins(settings).size() == 1 && enabled_plugins().empty(),
                "Changed approved binary auto-enabled native code");
        settings.beginReadArray("plugins/approved");
        settings.setArrayIndex(0);
        require(settings.value("fingerprint").toString() == QString(64, '0'),
                "Failed startup load erased the original approval");
        settings.endArray();
        approval(settings, manifest, hash);
        require(loadApprovedPlugins(settings).isEmpty() && find_plugin_variation("sample_wave"),
                "Valid startup approval failed to enable plugin");
        queueIdentity(directory, info);
        unchangedVariationEditor();
        protectQueueFromExports(directory);
        auto retained = make_variation_term("sample_wave");
        {
            PluginDialog dialog(settings, nullptr, false);
            auto table = dialog.findChild<QTableWidget *>("pluginTable");
            auto disable = dialog.findChild<QPushButton *>("pluginDisable");
            auto enable = dialog.findChild<QPushButton *>("pluginEnable");
            require(table && disable && enable && table->rowCount() == 1,
                    "Plugin manager did not expose the inspected module");
            table->selectRow(0);
            QSignalSpy changed(&dialog, &PluginDialog::pluginsChanged);
            require(disable->isEnabled() && !enable->isEnabled(),
                    "Plugin active-state controls incorrect");
            disable->click();
            require(changed.count() == 1 && !find_plugin_variation("sample_wave") &&
                        enable->isEnabled(),
                    "Plugin manager disable did not update registry and controls");
            Rng rng(7);
            require(
                std::isfinite(
                    retained.plugin->evaluate({.2, -.1}, {}, 1, retained.plugin_parameters, rng).x),
                "Disabling through UI invalidated retained module");
        }
        require(loadApprovedPlugins(settings).isEmpty() && find_plugin_variation("sample_wave"),
                "persist=false changed saved plugin approvals");
        {
            PluginDialog dialog(settings, nullptr, true);
            dialog.findChild<QTableWidget *>("pluginTable")->selectRow(0);
            dialog.findChild<QPushButton *>("pluginDisable")->click();
        }
        require(loadApprovedPlugins(settings).isEmpty() && !find_plugin_variation("sample_wave"),
                "Persistent disable did not revoke next-start approval");
        std::cout << "Plugin UI: startup hash validation, retained approvals, discovery, live "
                     "disable and preference persistence passed\n";
        return 0;
    } catch (const std::exception &e) {
        for (const auto &plugin : enabled_plugins())
            disable_plugin(plugin.name);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
