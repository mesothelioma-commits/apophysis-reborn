#include "color_curves.hpp"
#include "creative.hpp"
#include "flame_io.hpp"
#include "legacy_document.hpp"
#include "render_file.hpp"
#include "script.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace {
using namespace apo;
namespace fs = std::filesystem;
void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}
template<class F> void rejects(F&& action, const char* message) {
    try { action(); }
    catch (const std::exception&) { return; }
    throw std::runtime_error(message);
}
void near(double actual, double expected, double tolerance, const char* message) {
    require(std::isfinite(actual) && std::abs(actual - expected) <= tolerance, message);
}
ColorCurves modern() {
    ColorCurves result;
    result.mode = ColorCurveMode::continuous;
    result.channels[0] = {{{0, 0, 1}, {.22, .11, 1}, {.77, .92, 1}, {1, 1, 1}}};
    result.channels[1] = {{{0, 0, 1}, {.2, .38, .6}, {.8, .93, 2}, {1, 1, 1}}};
    return result;
}
std::vector<ColorCurves> oracle(const fs::path& folder) {
    std::ifstream inputs(folder / "color-curve-cases.tsv");
    require(bool(inputs), "Missing color curve input cases");
    std::string line;
    std::getline(inputs, line);
    std::vector<ColorCurves> cases(7);
    unsigned points = 0;
    while (std::getline(inputs, line)) {
        unsigned id, channel, point;
        WeightedCurvePoint value;
        std::istringstream row(line);
        require(bool(row >> id >> channel >> point >> value.x >> value.y >> value.weight), "Malformed curve case");
        cases.at(id).channels.at(channel).at(point) = value;
        ++points;
    }
    require(points == 112, "Incomplete curve control points");
    std::vector<ColorCurveEvaluator> evaluators;
    for (const auto& curves : cases) evaluators.emplace_back(curves);
    std::ifstream vectors(folder / "color-curve-vectors.tsv");
    require(bool(vectors), "Missing original Pascal curve vectors");
    std::getline(vectors, line);
    unsigned count = 0;
    while (std::getline(vectors, line)) {
        unsigned id, input;
        std::array<double, 3> output{};
        std::array<double, 4> raw{};
        std::istringstream row(line);
        require(bool(row >> id >> input >> output[0] >> output[1] >> output[2] >>
                     raw[0] >> raw[1] >> raw[2] >> raw[3]), "Malformed original Pascal curve row");
        for (unsigned c = 0; c < 3; ++c)
            require(evaluators.at(id).map_legacy(c, input) == output[c],
                    "Original 7X curve LUT mismatch at case " + std::to_string(id) + ", input " + std::to_string(input));
        for (unsigned c = 0; c < 4; ++c)
            near(evaluate_color_curve(cases.at(id).channels[c], double(input) / 256, ColorCurveMode::legacy_7x),
                 raw[c], 2e-15, "Original Bezier.pas floating-point mismatch");
        ++count;
    }
    require(count == 1799, "Incomplete original Pascal curve vectors");
    require(!evaluators[0].active() && !evaluators[1].active(), "Legacy point-only identity bypass lost");
    require(evaluators[2].active(), "X-only edit must activate historical curves");
    require(evaluators[2].map_legacy(0, 64) != 64, "Activated default legacy channels must compose smoothstep");
    std::cout << "1799 original 7X inputs: 5397 integer outputs and 7196 raw values matched\n";
    return cases;
}
void curve_math() {
    ColorCurves identity;
    const ColorCurveEvaluator legacy(identity);
    require(!legacy.active() && ColorCurveEvaluator::storage_bytes(identity) == 0, "Default curves must allocate no table");
    for (double x : {0., .123456789012345, .5, 1., 3.})
        require(legacy.map(0, x) == x, "Default curve must preserve every input bit");
    identity.mode = ColorCurveMode::continuous;
    identity.channels[0] = {{{0, 0, .2}, {.12, .12, 8}, {.9, .9, 0}, {1, 1, 16}}};
    const ColorCurveEvaluator diagonal(identity);
    require(!diagonal.active(), "Weighted continuous diagonal must bypass exactly");
    auto curves = modern();
    const ColorCurveEvaluator transfer(curves);
    require(transfer.active(), "Modern curve must be active");
    for (unsigned channel = 0; channel < 3; ++channel) {
        require(transfer.map(channel, 0) == 0 && transfer.map(channel, 1) == 1, "Curve endpoints must be exact");
        double previous = -1;
        for (unsigned i = 0; i <= 8192; ++i) {
            const double value = transfer.map(channel, double(i) / 8192);
            require(value >= previous && value <= 1, "Ordered modern controls must yield monotone transfer");
            previous = value;
        }
    }
    const double fraction = transfer.map(0, .3141592653);
    require(std::abs(fraction * 256 - std::round(fraction * 256)) > .001, "Continuous curves must not quantize to RGB8");
    const auto& channel = curves.channels[0];
    near(evaluate_color_curve(channel, .4, ColorCurveMode::continuous),
         ColorCurveEvaluator(ColorCurves{{channel, identity_color_curve(), identity_color_curve(), identity_color_curve()},
                                         ColorCurveMode::continuous}).map(0, .4),
         2e-7, "Interpolated transfer table must follow the analytic curve");
    auto other = curves;
    other.channels[1][1].y = .7;
    require(interpolate_color_curves(curves, other, 0) == curves &&
            interpolate_color_curves(curves, other, 1) == other, "Animation curve endpoints must remain exact");
    near(interpolate_color_curves(curves, other, .5).channels[1][1].y, .54, 1e-15, "Curve controls must interpolate");
    other.mode = ColorCurveMode::legacy_7x;
    rejects([&] { interpolate_color_curves(curves, other, .5); }, "Mixed curve modes must not interpolate silently");
    auto invalid = curves;
    invalid.channels[0][0].weight = 0;
    rejects([&] { ColorCurveEvaluator rejected(invalid); }, "Singular endpoint must be rejected");
    invalid.channels[0][0].weight = std::numeric_limits<double>::denorm_min();
    rejects([&] { ColorCurveEvaluator rejected(invalid); }, "Numerically singular endpoint must be rejected");
    invalid = curves;
    invalid.channels[0][1].x = .9;
    invalid.channels[0][2].x = .1;
    rejects([&] { validate_color_curves(invalid); }, "Unordered continuous X must be rejected");
    invalid.mode = ColorCurveMode::legacy_7x;
    validate_color_curves(invalid); // Historical X ordering has no rendering meaning.
    invalid.channels[0][1].y = std::numeric_limits<double>::quiet_NaN();
    rejects([&] { validate_color_curves(invalid); }, "NaN curves must be rejected");
    invalid = curves;
    invalid.channels[2][2].weight = -1;
    rejects([&] { validate_color_curves(invalid); }, "Negative rational weight must be rejected");
}
void colors() {
    using namespace apo::creative;
    const RGB black{}, white{1, 1, 1};
    const auto encoded = blend_colors(black, white, .5, GradientSpace::rgb);
    const auto linear = blend_colors(black, white, .5, GradientSpace::linear_rgb);
    require(encoded.r == .5, "Existing RGB interpolation changed");
    near(linear.r, .7353569830524495, 1e-14, "Linear-light sRGB midpoint is wrong");
    const auto hsl = blend_colors({1, 0, 0}, {0, 0, 1}, .5, GradientSpace::hsl_shortest);
    near(hsl.r, 1, 1e-15, "HSL hue must take shortest path");
    near(hsl.g, 0, 1e-15, "HSL hue must take shortest path");
    near(hsl.b, 1, 1e-15, "HSL hue must take shortest path");
    const auto neutral = blend_colors(black, {0, 0, 1}, .5, GradientSpace::hsl_shortest);
    near(neutral.r, .125, 1e-15, "Achromatic HSL endpoint must borrow the colored hue");
    near(neutral.b, .375, 1e-15, "HSL saturation and lightness must interpolate");
    const auto palette = default_palette();
    const auto untouched = curve_palette(palette, {});
    require(untouched[127].r == palette[127].r, "Identity palette curves must preserve exact precision");
    const auto changed = curve_palette(palette, modern());
    require(changed[127].r != palette[127].r, "Palette curve must change stored colors");
    rejects([&] { blend_colors(black, white, -.2, GradientSpace::hsl_shortest); }, "Invalid blend amount accepted");
}
void documents(const std::vector<ColorCurves>& cases) {
    auto flame = demo_flame();
    for (const auto& curves : {cases[0], cases[1], cases[3], modern()}) {
        flame.curves = curves;
        const auto restored = parse_flames(serialize_flame(flame)).at(0);
        require(restored.curves == curves, "Canonical XML must retain every curve point, weight and mode");
    }
    flame.source_version = "Apophysis 7X 15D";
    flame.curves = cases[3];
    auto xml = serialize_flame(flame);
    xml.erase(0, xml.find("<flame "));
    const auto scale = xml.find("scale=\"260\"");
    require(scale != std::string::npos, "Unexpected sample scale");
    xml.replace(scale, 11, "scale='2.60000e2'");
    const auto transform = xml.find("<xform");
    xml.insert(transform, "<!-- retain curve provenance -->\n");
    const std::string sibling = "<flame name='Unrelated' vendor='keep'><opaque/></flame>";
    auto doc = LegacyDocument::parse("<flames>" + xml + sibling + "</flames>");
    const auto before = doc.bytes();
    LegacyEdits edit;
    edit.curves = cases[3];
    doc.edit(0, edit);
    require(doc.bytes() == before, "A no-op curve edit must preserve source bytes");
    edit.curves = modern();
    doc.edit(0, edit);
    require(doc.render_model(0).curves == modern(), "Source-preserving curve edit did not apply");
    require(doc.bytes().find("scale='2.60000e2'") != std::string::npos &&
            doc.bytes().find("<!-- retain curve provenance -->") != std::string::npos &&
            doc.bytes().find(sibling) != std::string::npos, "Curve edits rewrote unrelated source data");
    edit.curves = cases[3];
    doc.edit(0, edit);
    require(doc.bytes().find("reborn_curve_mode") == std::string::npos, "Legacy conversion must remove modern marker");
    auto edited = doc.render_model(0);
    edited.curves = modern();
    doc.apply_model(0, edited);
    require(doc.render_model(0).curves == modern(), "Script model application dropped curves");
    ScriptOptions options;
    options.allow_render_requests = true;
    options.allow_export_requests = true;
    const auto script = run_script(doc, 0,
        "apo.set_flame{name='Curved script'}; apo.render('curved.png',{samples=1000}); apo.export_flame('curved.flame')", options);
    require(LegacyDocument::parse(script.document_bytes).render_model(0).curves == modern(), "Script edit dropped curves");
    require(script.requests.at(0).flame.curves == modern(), "Script render snapshot dropped curves");
    require(parse_flames(script.requests.at(1).bytes).at(0).curves == modern(), "Script export snapshot dropped curves");
    rejects([&] { parse_flames("<flame curves='0 1'><xform linear='1'/></flame>"); }, "Short curves attribute accepted");
    rejects([&] { parse_flames("<flame reborn_curve_mode='guess'><xform linear='1'/></flame>"); }, "Unknown curve mode accepted");
}
std::vector<unsigned char> ppm(const fs::path& path, unsigned bits) {
    std::ifstream input(path, std::ios::binary);
    std::string magic;
    unsigned width, height, maximum;
    input >> magic >> width >> height >> maximum;
    require(magic == "P6" && maximum == (bits == 8 ? 255u : 65535u) && input.get() == '\n', "Bad PPM export");
    std::vector<unsigned char> pixels{std::istreambuf_iterator<char>(input), {}};
    require(pixels.size() == std::size_t(width) * height * 3 * (bits / 8), "Incomplete PPM export");
    return pixels;
}
void renders(const std::vector<ColorCurves>& cases) {
    struct Temporary {
        fs::path path = fs::temp_directory_path() / ("apo-color-tests-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
        Temporary() { fs::create_directories(path); }
        ~Temporary() { std::error_code error; fs::remove_all(path, error); }
    } temp;
    auto flame = demo_flame();
    flame.width = 80;
    flame.height = 60;
    flame.scale = 24;
    flame.oversample = 2;
    flame.filter_radius = .5;
    flame.brightness = .12;
    flame.background = {.1, .2, .3};
    RenderOptions options;
    options.samples = 24000;
    options.threads = 2;
    options.memory_mib = 64;
    FileRenderOptions file;
    file.disk_mib = 64;
    file.image.format = ImageFormat::ppm8;
    file.strip_rows = 3;
    for (const auto& producer : {std::string{}, std::string("Apophysis 7X 15D")}) {
        flame.source_version = producer;
        flame.curves = {};
        const auto baseline = render(flame, options).image.rgb;
        flame.curves = cases[1];
        require(render(flame, options).image.rgb == baseline, "Historical point-only bypass must preserve image bytes");
        for (const auto& curves : {cases[3], modern()}) {
            flame.curves = curves;
            const auto dense = render(flame, options).image.rgb;
            require(dense != baseline, "Nondefault curves had no effect on rendered image");
            const auto path = temp.path / "curve.ppm";
            render_to_file(flame, options, path, file);
            require(ppm(path, 8) == dense, "Dense and streaming curve renders differ");
            file.strip_rows = 11;
            render_to_file(flame, options, path, file);
            require(ppm(path, 8) == dense, "Curves introduced reconstruction strip seams");
            file.strip_rows = 3;
        }
    }
    file.image.format = ImageFormat::ppm16;
    flame.curves = modern();
    render_to_file(flame, options, temp.path / "curve16.ppm", file);
    const auto high = ppm(temp.path / "curve16.ppm", 16);
    bool fine = false;
    for (std::size_t i = 0; i < high.size(); i += 2)
        if ((unsigned(high[i]) * 256 + high[i + 1]) % 257) fine = true;
    require(fine, "Modern curve export lost higher-than-RGB8 precision");
    file.checkpoint_directory = temp.path / "checkpoint";
    render_to_file(flame, options, temp.path / "curve16.ppm", file);
    require(has_render_checkpoint(file.checkpoint_directory), "Curve render checkpoint missing");
    file.resume = true;
    flame.curves.channels[1][1].y += .01;
    rejects([&] { render_to_file(flame, options, temp.path / "curve16.ppm", file); },
            "Changed curves must invalidate a render checkpoint");
    require(ppm(temp.path / "curve16.ppm", 16) == high, "Rejected curve resume changed the completed output");
    flame.curves = modern();
    require(render_to_file(flame, options, temp.path / "curve16.ppm", file).resumed,
            "An unchanged curve snapshot should resume");
    require(ppm(temp.path / "curve16.ppm", 16) == high, "Resumed curve output differs");
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "Pass tests/fixtures/color to color-curves-tests");
        const auto cases = oracle(argv[1]);
        curve_math();
        colors();
        documents(cases);
        renders(cases);
        std::cout << "Color curve math, source preservation, script snapshots and render parity passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
