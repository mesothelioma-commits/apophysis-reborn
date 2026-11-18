#include "flame_io.hpp"
#include "gpu_kernel.hpp"
#include "legacy_document.hpp"
#include "plugin.hpp"
#include "render_file.hpp"
#include "script.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <stop_token>

namespace {
using namespace apo;
namespace fs = std::filesystem;
void require(bool condition, std::string_view message) {
    if (!condition)
        throw std::runtime_error(std::string(message));
}
template <class F> std::string rejects(F action, std::string_view message) {
    try {
        action();
    } catch (const std::exception &e) {
        return e.what();
    }
    throw std::runtime_error(std::string(message));
}
void near(double a, double b, std::string_view message) {
    require(std::abs(a - b) < 1e-13, message);
}
std::string read(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    require(bool(in), "Cannot read plugin fixture");
    return {std::istreambuf_iterator<char>(in), {}};
}
void write(const fs::path &path, std::string_view bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    require(bool(out), "Cannot write plugin fixture");
}
void replace(std::string &s, std::string_view from, std::string_view to) {
    const auto at = s.find(from);
    require(at != std::string::npos, "Missing expected fixture text");
    s.replace(at, from.size(), to);
}
struct Temporary {
    fs::path directory =
        fs::temp_directory_path() /
        ("apo-plugin-tests-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temporary() { require(fs::create_directory(directory), "Cannot create plugin test directory"); }
    ~Temporary() {
        std::error_code e;
        fs::remove_all(directory, e);
    }
};
std::string fixture_manifest(std::string_view library, bool bad = false) {
    return "<apophysis-plugin abi=\"1\" id=\"org.apophysis.reborn.fixture" +
           std::string(bad ? ".badabi" : "") + "\" name=\"" +
           (bad ? "fixture_badabi" : "fixture_probe") +
           "\" version=\"1.0.0\" description=\"Deterministic plugin contract fixture\" library=\"" +
           std::string(library) +
           "\"><parameter name=\"mode\" default=\"0\" min=\"0\" max=\"4\" "
           "type=\"integer\"/></apophysis-plugin>";
}
Flame plugin_flame() {
    auto f = demo_flame();
    f.width = 37;
    f.height = 29;
    f.scale = 17;
    f.name = "Plugin contract";
    f.transforms[0].variations = {
        make_variation_term("sample_wave", .7, {{"amplitude", .35}, {"frequency", 4}}),
        make_variation_term("linear", .3)};
    f.final_transform.emplace();
    f.final_transform->variations = {make_variation_term("fixture_probe")};
    validate(f);
    return f;
}
void metadata_tests(const fs::path &directory, const fs::path &fixture, const fs::path &badabi) {
    const auto extension = fixture.extension().string();
    fs::copy_file(fixture, directory / ("probe" + extension));
    fs::copy_file(badabi, directory / ("badabi" + extension));
    const auto manifest = directory / "probe.apoplugin";
    const auto bad_manifest = directory / "badabi.apoplugin";
    write(manifest, fixture_manifest("probe" + extension));
    write(bad_manifest, fixture_manifest("badabi" + extension, true));
    const auto marker = directory / "load-marker.txt";
#ifdef _WIN32
    _putenv_s("APO_PLUGIN_TEST_MARKER", marker.string().c_str());
#else
    setenv("APO_PLUGIN_TEST_MARKER", marker.string().c_str(), 1);
#endif
    const auto found = discover_plugins({directory});
    require(found.size() == 2 && found[0].info && found[1].info && !fs::exists(marker),
            "Discovery executed native module code");
    const auto inspected = inspect_plugin(manifest);
    require(!inspected.enabled && inspected.parameters.size() == 1 &&
                inspected.parameters[0].integer,
            "Data-only metadata inspection failed");
    require(rejects([&] { enable_plugin(manifest, std::string(64, '0')); },
                    "Fingerprint guard accepted changed bytes")
                        .find("changed") != std::string::npos &&
                !fs::exists(marker),
            "Approval fingerprint was checked after native loading");
    enable_plugin(manifest, inspected.fingerprint);
    require(fs::exists(marker) && inspect_plugin(manifest).enabled,
            "Explicit enable did not load module");
    require(rejects([&] { enable_plugin(bad_manifest); }, "Bad native ABI accepted").find("ABI") !=
                std::string::npos,
            "Bad native ABI was not diagnosed");
    auto changed = read(manifest);
    replace(changed, "version=\"1.0.0\"", "version=\"2.0.0\"");
    write(manifest, changed);
    require(!inspect_plugin(manifest).enabled, "Changed sidecar was reported as active");
    rejects([&] { enable_plugin(manifest); }, "Changed active descriptor accepted");
    write(manifest, fixture_manifest("probe" + extension));
    const auto invalid = directory / "invalid.apoplugin";
    auto invalid_text = fixture_manifest("probe" + extension);
    replace(invalid_text, "name=\"fixture_probe\"", "name=\"linear\"");
    write(invalid, invalid_text);
    rejects([&] { inspect_plugin(invalid); }, "Builtin name collision accepted");
    replace(invalid_text, "name=\"linear\"", "name=\"weight\"");
    write(invalid, invalid_text);
    rejects([&] { inspect_plugin(invalid); }, "Transform-field name collision accepted");
    replace(invalid_text, "name=\"weight\"", "name=\"fixture_other\"");
    write(invalid, invalid_text);
    require(
        rejects([&] { enable_plugin(invalid); }, "Duplicate module ID accepted").find("owns ID") !=
            std::string::npos,
        "Duplicate plugin identity was not diagnosed before native loading");
    invalid_text = fixture_manifest("../probe" + extension);
    write(invalid, invalid_text);
    rejects([&] { inspect_plugin(invalid); }, "Traversal in module path accepted");
    invalid_text = "<!DOCTYPE apophysis-plugin [<!ENTITY x 'expanded'>]>" +
                   fixture_manifest("probe" + extension);
    write(invalid, invalid_text);
    rejects([&] { inspect_plugin(invalid); }, "DTD accepted in plugin manifest");
    invalid_text = fixture_manifest("probe" + extension);
    replace(invalid_text, "min=\"0\"", "min=\"nan\"");
    write(invalid, invalid_text);
    rejects([&] { inspect_plugin(invalid); }, "Non-finite parameter bound accepted");
    const auto bad_discovery = discover_plugins({directory});
    require(std::any_of(bad_discovery.begin(), bad_discovery.end(),
                        [](const auto &item) { return !item.error.empty(); }),
            "Discovery concealed invalid sidecar");
    fs::remove(invalid);

    // A known SHA-256 vector also proves inspection doesn't require executable bytes.
    write(directory / "abc.bin", "abc");
    write(invalid, fixture_manifest("abc.bin"));
    require(inspect_plugin(invalid).fingerprint ==
                "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            "SHA-256 implementation disagrees with the standard abc vector");
    rejects([&] { enable_plugin(invalid); }, "Non-executable bytes loaded");
    write(directory / "abc.bin", std::string(1'000'000, 'a'));
    require(inspect_plugin(invalid).fingerprint ==
                "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
            "Streaming SHA-256 disagrees with the million-a vector");
    // PE32 has a specific actionable x64 diagnosis before any loader invocation.
    std::string pe(128, '\0');
    pe[0] = 'M';
    pe[1] = 'Z';
    pe[60] = 64;
    pe[64] = 'P';
    pe[65] = 'E';
    pe[68] = 0x4c;
    pe[69] = 1;
    write(directory / "abc.bin", pe);
    if (sizeof(void *) == 8)
        require(rejects([&] { enable_plugin(invalid); }, "PE32 loaded in x64")
                        .find("32-bit legacy DLL") != std::string::npos,
                "PE32 migration diagnosis missing");
    fs::remove(invalid);
    fs::remove(directory / "abc.bin");
}
void evaluation_tests() {
    auto term = make_variation_term("sample_wave", -.75);
    const auto parameters = term_parameters(term);
    near(parameters.at("amplitude"), .2, "Default parameter not applied");
    near(parameters.at("frequency"), 3, "Second default parameter not applied");
    Transform t;
    t.pre = {.5, .1, -.2, .7, .3, -.4};
    t.post = {.8, .05, .03, .9, -.2, .1};
    t.variations = {term};
    Rng rng(12);
    const Point p{.25, -.8}, input = t.pre.apply(p);
    const auto actual = apply_transform(t, {p, .4}, rng).point;
    const auto expected = t.post.apply({-.75 * (input.x + .2 * std::sin(3 * input.y)),
                                        -.75 * (input.y + .2 * std::sin(3 * input.x))});
    near(actual.x, expected.x, "Plugin formula/weight/affine X mismatch");
    near(actual.y, expected.y, "Plugin formula/weight/affine Y mismatch");
    rejects([&] { make_variation_term("sample_wave", 1, {{"amplitude", 3}}); },
            "Range violation accepted");
    rejects(
        [&] {
            make_variation_term("sample_wave", 1,
                                {{"frequency", std::numeric_limits<double>::infinity()}});
        },
        "Non-finite parameter accepted");
    rejects([&] { make_variation_term("sample_wave", 1, {{"missing", 1}}); },
            "Unknown parameter accepted");
    rejects([&] { make_variation_term("fixture_probe", 1, {{"mode", 1.5}}); },
            "Fractional integer parameter accepted");
    rejects([&] { make_variation_term("unknown_plugin"); }, "Missing plugin silently substituted");
    rejects([&] { make_variation_term("linear", 1, {{"mode", 1}}); }, "Builtin parameter ignored");
    t.pre = {};
    t.post = {};
    t.variations = {make_variation_term("fixture_probe", .5, {{"mode", 3}})};
    Rng actual_rng(123), expected_rng(123);
    const auto random = apply_transform(t, {p, 0}, actual_rng).point;
    near(random.x, .5 * (p.x + expected_rng.unit()), "Plugin did not use orbit RNG for X");
    near(random.y, .5 * (p.y + expected_rng.unit()), "Plugin did not use orbit RNG for Y");
    near(actual_rng.unit(), expected_rng.unit(), "Plugin RNG draw count changed");
    t.variations[0].weight = 0;
    Rng zero_rng(72), no_draw(72);
    (void)apply_transform(t, {p, 0}, zero_rng);
    near(zero_rng.unit(), no_draw.unit(), "Zero-weight plugin consumed a random draw");
    t.variations = {make_variation_term("fixture_probe", .3, {{"mode", 4}})};
    t.pre = {1, 2, 3, 4, 5, 6};
    const auto context = apply_transform(t, {p, 0}, actual_rng).point;
    near(context.x, .3 * 9, "Affine context order changed");
    near(context.y, .3 * 12.3, "Weight context was lost or applied twice");
    t.variations = {make_variation_term("fixture_probe", 1, {{"mode", 1}})};
    require(rejects([&] { apply_transform(t, {p, 0}, actual_rng); }, "Plugin NaN accepted")
                    .find("non-finite") != std::string::npos,
            "Non-finite native output lacks diagnostic");
    t.variations = {make_variation_term("fixture_probe", 1, {{"mode", 2}})};
    require(rejects([&] { apply_transform(t, {p, 0}, actual_rng); }, "Plugin error status accepted")
                    .find("fixture failure") != std::string::npos,
            "Plugin callback error detail was lost");
}
void serialization_and_lifetime(const fs::path &wave_manifest) {
    auto f = plugin_flame();
    const auto xml = serialize_flame(f);
    const auto roundtrip = parse_flames(xml).at(0);
    require(serialize_flame(roundtrip) == xml && plugin_identity(roundtrip) == plugin_identity(f),
            "Custom parameters/order/identity did not round-trip");
    require(term_name(roundtrip.transforms[0].variations[0]) == "sample_wave",
            "Custom/builtin variation order changed");
    auto document_xml = xml;
    document_xml.insert(document_xml.find("<xform"), "<!-- retained plugin edit comment -->\n");
    auto document = LegacyDocument::parse(document_xml);
    LegacyTransformEdits changes;
    changes.variations = std::vector<Term>{
        make_variation_term("sample_wave", .7, {{"amplitude", -.1}, {"frequency", 5}})};
    document.edit_transform(0, 0, changes);
    require(
        document.bytes().find("<!-- retained plugin edit comment -->") != std::string::npos &&
            term_parameters(document.render_model(0).transforms[0].variations[0]).at("amplitude") ==
                -.1,
        "Preserving document edit lost plugin data or unrelated bytes");
    const auto saved = document.bytes();
    const auto scripted = run_script(document, 0,
                                     "apo.set_transform(1,{variations={{name='sample_wave',weight="
                                     "1,parameters={amplitude=.5,frequency=2}}}})");
    const auto script_model = LegacyDocument::parse(scripted.document_bytes).render_model(0);
    require(term_parameters(script_model.transforms[0].variations[0]).at("amplitude") == .5 &&
                term_parameters(script_model.transforms[0].variations[0]).at("frequency") == 2,
            "Script host failed to edit registered plugin parameters");
    rejects(
        [&] {
            run_script(document, 0,
                       "apo.set_transform(1,{variations={{name='sample_wave',weight=1,parameters={"
                       "missing=2}}}})");
        },
        "Script host ignored an unknown plugin parameter");
    require(document.bytes() == saved, "Script plugin edit modified its input document");
    auto bad = *changes.variations;
    bad[0].plugin_parameters[0] = 10;
    changes.variations = bad;
    rejects([&] { document.edit_transform(0, 0, changes); }, "Invalid plugin edit accepted");
    require(document.bytes() == saved, "Rejected plugin edit changed source bytes");
    RenderOptions options;
    options.samples = 10001;
    options.threads = 1;
    const auto before = render(f, options);
    disable_plugin("sample_wave");
    rejects([&] { parse_flames(xml); }, "Disabled plugin silently parsed");
    rejects([&] { make_variation_term("sample_wave"); },
            "Disabled plugin remained available for new terms");
    rejects([&] { remove_plugin(wave_manifest); }, "Module retained by snapshot was removed");
    const auto after = render(f, options);
    require(after.image.rgb == before.image.rgb,
            "Disabling plugin invalidated live render snapshot");
    enable_plugin(wave_manifest);
    options.backend = RenderBackend::vulkan;
    options.allow_cpu_fallback = true;
    const auto fallback = render(f, options);
    require(fallback.backend == RenderBackend::cpu && fallback.image.rgb == before.image.rgb &&
                fallback.fallback_reason.find("plugin") != std::string::npos,
            "Custom plugin GPU request did not explicitly fall back to CPU");
    options.allow_cpu_fallback = false;
    rejects([&] { render(f, options); }, "No-fallback Vulkan silently rendered native plugin");
    require(gpu::support_reason(f).find("plugin") != std::string::npos,
            "GPU support check accepted custom term");
}
void checkpoint_identity(const fs::path &directory, const fs::path &wave_manifest) {
    auto opt = RenderOptions{};
    opt.samples = 65539;
    opt.threads = 1;
    opt.memory_mib = 64;
    FileRenderOptions file;
    file.image.format = ImageFormat::ppm8;
    file.strip_rows = 7;
    file.disk_mib = 64;
    file.checkpoint_directory = directory / "checkpoint";
    const auto output = directory / "plugin.ppm";
    std::string xml, original_identity;
    {
        auto f = plugin_flame();
        xml = serialize_flame(f);
        original_identity = plugin_identity(f);
        std::stop_source stopped;
        opt.stop_token = stopped.get_token();
        opt.progress = [&](auto completed, auto) {
            if (completed)
                stopped.request_stop();
        };
        rejects([&] { render_to_file(f, opt, output, file); }, "Plugin render did not pause");
        require(has_render_checkpoint(file.checkpoint_directory),
                "Plugin pause did not retain checkpoint");
    }
    // Release every old wave descriptor before changing the module; Windows also
    // prevents overwriting a mapped DLL. Appended bytes preserve this fixture's code.
    disable_plugin("sample_wave");
    const auto info = inspect_plugin(wave_manifest);
    const auto old_library = read(info.library_path);
    write(info.library_path, old_library + "checkpoint identity fixture");
    const auto changed = enable_plugin(wave_manifest);
    require(changed.fingerprint != info.fingerprint,
            "Module-byte fingerprint ignored changed bytes");
    {
        const auto f = parse_flames(xml).at(0);
        require(plugin_identity(f) != original_identity,
                "Checkpoint identity omitted native module bytes");
        opt.stop_token = {};
        opt.progress = {};
        file.resume = true;
        require(rejects([&] { render_to_file(f, opt, output, file); },
                        "Changed module resumed old checkpoint")
                        .find("match") != std::string::npos,
                "Changed-module checkpoint rejection lacks mismatch diagnosis");
        require(has_render_checkpoint(file.checkpoint_directory),
                "Rejected resume damaged checkpoint");
    }
    disable_plugin("sample_wave");
    write(info.library_path, old_library);
    enable_plugin(wave_manifest, info.fingerprint);
    {
        const auto f = parse_flames(xml).at(0);
        const auto resumed = render_to_file(f, opt, output, file);
        require(resumed.resumed && fs::is_regular_file(output),
                "Original plugin could not resume its checkpoint");
    }
}
} // namespace
int main(int argc, char **argv) {
    try {
        require(argc == 4, "Usage: plugin-tests WAVE_MANIFEST FIXTURE_LIBRARY BAD_ABI_LIBRARY");
        Temporary temporary;
        metadata_tests(temporary.directory, argv[2], argv[3]);
        const auto installed = install_plugin(argv[1], temporary.directory / "installed");
        require(!installed.enabled, "Install executed/enabled native module");
        rejects([&] { install_plugin(argv[1], temporary.directory / "installed"); },
                "Install overwrote existing package");
        enable_plugin(installed.manifest_path, installed.fingerprint);
        evaluation_tests();
        serialization_and_lifetime(installed.manifest_path);
        checkpoint_identity(temporary.directory, installed.manifest_path);
        disable_plugin("sample_wave");
        remove_plugin(installed.manifest_path);
        require(!fs::exists(installed.manifest_path),
                "Disabled unreferenced plugin removal failed");
        disable_plugin("fixture_probe");
        std::cout
            << "Plugins: data-only discovery, ABI/metadata/ranges, formulas/RNG, serialization, "
               "live snapshots, GPU fallback and checkpoint fingerprints passed\n";
        return 0;
    } catch (const std::exception &e) {
        for (const auto &plugin : enabled_plugins())
            disable_plugin(plugin.name);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
