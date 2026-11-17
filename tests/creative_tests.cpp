#include "creative.hpp"
#include "flame_io.hpp"
#include "render.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace apo;
using namespace apo::creative;

void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
void near(double actual, double expected, const char *message, double tolerance = 2e-12) {
    require(std::isfinite(actual) && std::abs(actual - expected) <= tolerance, message);
}
void color(RGB actual, RGB expected, const char *message) {
    near(actual.r, expected.r, message);
    near(actual.g, expected.g, message);
    near(actual.b, expected.b, message);
}
void same_palette(const Palette &actual, const Palette &expected, const char *message) {
    for (std::size_t i = 0; i < actual.size(); ++i)
        color(actual[i], expected[i], message);
}
void rejects(const std::function<void()> &operation, const char *message) {
    bool rejected = false;
    try {
        operation();
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    require(rejected, message);
}
bool identical(const Flame &a, const Flame &b) {
    return serialize_flame(a) == serialize_flame(b);
}

void test_stops_and_interpolation() {
    const std::array<ColorStop, 3> stops{{{255, {1, 1, 1}}, {0, {0, 0, 0}}, {128, {1, 0, 0}}}};
    const auto palette = palette_from_stops(stops);
    color(palette[0], {0, 0, 0}, "first stop");
    color(palette[64], {.5, 0, 0}, "RGB interpolation midpoint");
    color(palette[128], {1, 0, 0}, "middle stop");
    color(palette[192], {1, 64.0 / 127, 64.0 / 127}, "RGB interpolation unequal segment");
    color(palette[255], {1, 1, 1}, "last stop");
    const std::array<ColorStop, 2> hue{{{0, {1, 0, 0}}, {254, {0, 0, 1}}}};
    const auto hsv = palette_from_stops(hue, GradientSpace::hsv_shortest);
    color(hsv[127], {1, 0, 1}, "HSV follows shorter arc across hue zero");
    color(hsv[254], hue[1].color, "HSV retains exact stop");
    color(hsv[255], hue[1].color, "palette clamps beyond final stop");
    const std::array<ColorStop, 2> neutral{{{10, {0, 0, 0}}, {210, {0, 0, 1}}}};
    const auto blue = palette_from_stops(neutral, GradientSpace::hsv_shortest);
    color(blue[0], {0, 0, 0}, "palette clamps before first stop");
    color(blue[110], {.25, .25, .5}, "neutral stop borrows neighboring hue while saturation interpolates");
    auto invalid = stops;
    invalid[1].index = 255;
    rejects([&] { palette_from_stops(invalid); }, "duplicate stops rejected");
    invalid = stops;
    invalid[1].index = -1;
    rejects([&] { palette_from_stops(invalid); }, "negative stop rejected");
    invalid = stops;
    invalid[1].color.r = std::numeric_limits<double>::quiet_NaN();
    rejects([&] { palette_from_stops(invalid); }, "NaN color rejected");
    rejects([&] { palette_from_stops(std::span(stops).first(1)); }, "single stop rejected");
    rejects([&] { palette_from_stops(stops, static_cast<GradientSpace>(77)); }, "unknown color space rejected");
}

void test_palette_operations() {
    auto palette = default_palette();
    const auto rotated = rotate_palette(palette, 17);
    color(rotated[17], palette[0], "positive rotation direction");
    color(rotated[0], palette[239], "rotation wraps");
    same_palette(rotate_palette(rotated, -17), palette, "inverse rotation");
    same_palette(rotate_palette(palette, std::numeric_limits<int>::min()), palette, "large negative rotation");
    same_palette(reverse_palette(reverse_palette(palette)), palette, "reverse involution");
    Palette impulse{};
    impulse[0] = {1, 1, 1};
    const auto smoothed = smooth_palette(impulse, 1);
    color(smoothed[0], {1.0 / 3, 1.0 / 3, 1.0 / 3}, "smoothing central entry");
    color(smoothed[1], smoothed[0], "smoothing right neighbor");
    color(smoothed[255], smoothed[0], "smoothing wraps left neighbor");
    color(smoothed[2], {}, "smoothing finite support");
    same_palette(smooth_palette(palette, 0), palette, "zero smoothing is exact identity");
    rejects([&] { smooth_palette(palette, 33); }, "smoothing size bound");
    same_palette(adjust_palette(palette, {}), palette, "neutral adjustments preserve entries");
    palette.fill({1, 0, 0});
    PaletteAdjustment adjust;
    adjust.hue_degrees = 120;
    color(adjust_palette(palette, adjust)[0], {0, 1, 0}, "120-degree hue rotation");
    adjust = {};
    adjust.saturation = 0;
    color(adjust_palette(palette, adjust)[0], {1, 1, 1}, "HSV desaturation retains value");
    adjust = {};
    adjust.value = .5;
    color(adjust_palette(palette, adjust)[0], {.5, 0, 0}, "value adjustment");
    palette.fill({.25, .25, .25});
    adjust = {};
    adjust.gamma = {2, 1, .5};
    color(adjust_palette(palette, adjust)[0], {.5, .25, .0625}, "independent channel curves");
    adjust = {};
    adjust.contrast = 0;
    color(adjust_palette(palette, adjust)[0], {.5, .5, .5}, "zero contrast is mid-gray");
    adjust.gamma[1] = 0;
    rejects([&] { adjust_palette(palette, adjust); }, "zero gamma rejected");
    adjust = {};
    adjust.saturation = -1;
    rejects([&] { adjust_palette(palette, adjust); }, "negative multiplier rejected");
    const auto random = random_palette(719, 7);
    same_palette(random, random_palette(719, 7), "random palette repeatability");
    color(random.front(), random.back(), "random palette closes color loop");
    require(export_gimp_palette(random) != export_gimp_palette(random_palette(720, 7)),
            "random palette seed changes colors");
    rejects([] { random_palette(1, 2); }, "random stop lower bound");
    const auto presets = palette_presets();
    require(presets.size() == 7 && presets.front().name == "Ember", "preset catalog");
    for (const auto &preset : presets)
        require(!export_gimp_palette(preset.colors, preset.name).empty(), "preset validity");
}

void test_palette_files() {
    const auto two = import_gimp_palette("\xef\xbb\xbfGIMP Palette\r\nName: Two colors\r\nColumns: 2\r\n# sample\r\n255 0 0 Red\r\n0 0 255 Blue\r\n");
    color(two.front(), {1, 0, 0}, "GPL red endpoint");
    color(two.back(), {0, 0, 1}, "GPL blue endpoint");
    color(two[128], {127.0 / 255, 0, 128.0 / 255}, "GPL smaller palette interpolation");
    const auto source = random_palette(901);
    const auto exported = export_gimp_palette(source, "My\nPalette\rName");
    require(exported.starts_with("GIMP Palette\nName: My Palette Name\nColumns:"), "GPL safe header");
    const auto reopened = import_gimp_palette(exported);
    for (std::size_t i = 0; i < reopened.size(); ++i)
        color(reopened[i], {std::round(source[i].r * 255) / 255, std::round(source[i].g * 255) / 255,
                            std::round(source[i].b * 255) / 255}, "GPL preserves every encoded index");
    rejects([] { import_gimp_palette("GIMP Palette\n256 0 0\n0 0 0\n"); }, "GPL out-of-range component");
    rejects([] { import_gimp_palette("GIMP Palette\n1 2 3x\n0 0 0\n"); }, "GPL malformed component");
    rejects([] { import_gimp_palette("GIMP Palette\n1 2 3\n"); }, "GPL single color");
    rejects([] { import_gimp_palette("1 2 3\n0 0 0\n"); }, "GPL missing header");
    auto too_many = exported + "0 0 0 Extra\n";
    rejects([&] { import_gimp_palette(too_many); }, "GPL more than 256 entries");
}

void test_random_generation() {
    RandomOptions options;
    options.seed = 2468013579;
    options.batch_size = 3;
    options.min_transforms = options.max_transforms = 4;
    options.allowed_variations = {Variation::linear};
    options.rotational_symmetry = 4;
    options.reflection = true;
    const auto generated = random_flames(options);
    const auto repeated = random_flames(options);
    require(generated.size() == 3, "requested random batch size");
    for (std::size_t index = 0; index < generated.size(); ++index) {
        const auto &flame = generated[index];
        validate(flame);
        require(identical(flame, repeated[index]), "generation seed repeatability");
        require(flame.name == "Random 2468013579 / " + std::to_string(index + 1), "random name provenance");
        require(flame.transforms.size() == 8, "four ordinary branches, three rotations and reflection");
        for (unsigned i = 0; i < 4; ++i) {
            const auto &transform = flame.transforms[i];
            require(transform.variations.size() == 1 && transform.variations[0].id == Variation::linear,
                    "random allowlist respected");
            const auto &a = transform.pre;
            const double first = std::hypot(a.a, a.b), second = std::hypot(a.c, a.d);
            require(first >= options.min_contraction && first <= options.max_contraction &&
                        second >= options.min_contraction && second <= options.max_contraction,
                    "requested contractions bound both singular values");
            near(a.a * a.c + a.b * a.d, 0, "random affine columns orthogonal");
            require(std::abs(a.e) <= options.translation && std::abs(a.f) <= options.translation,
                    "requested translation bounds");
        }
        const auto &quarter_turn = flame.transforms[4];
        const auto point = quarter_turn.pre.apply({2, 1});
        near(point.x, -1, "quarter-turn x");
        near(point.y, 2, "quarter-turn y");
        require(quarter_turn.color_speed == 0 && quarter_turn.variations[0].id == Variation::linear,
                "symmetry branches preserve color");
        const auto reflected = flame.transforms.back().pre.apply({2, 1});
        near(reflected.x, -2, "reflection x");
        near(reflected.y, 1, "reflection y");
    }
    options.batch_size = 1;
    require(identical(random_flames(options).front(), generated.front()), "batch prefix is stable");
    options.seed++;
    require(!identical(random_flames(options).front(), generated.front()), "random geometry seed matters");
    options = {};
    options.seed = 481;
    options.batch_size = 3;
    options.width = 96;
    options.height = 72;
    const auto defaults = random_flames(options);
    for (const auto &flame : defaults) {
        const auto rendered = render(flame, RenderOptions{40000, 93, 1});
        std::size_t colored = 0;
        for (std::size_t i = 0; i < rendered.image.rgb.size(); i += 3)
            colored += rendered.image.rgb[i] != 0 || rendered.image.rgb[i + 1] != 0 ||
                       rendered.image.rgb[i + 2] != 0;
        require(rendered.plotted > 25000 && colored > 150, "default random result forms a framed visible image");
    }
    options = {};
    options.randomize_palette = false;
    same_palette(random_flames(options).front().palette, default_palette(), "neutral palette option");
    options.allowed_variations = {Variation::pre_blur};
    rejects([&] { random_flames(options); }, "pre-blur alone has no ordinary output");
    options = {};
    options.min_transforms = 1;
    rejects([&] { random_flames(options); }, "transform lower bound");
    options = {};
    options.min_contraction = std::numeric_limits<double>::quiet_NaN();
    rejects([&] { random_flames(options); }, "nonfinite random constraint");
    options = {};
    options.batch_size = 257;
    rejects([&] { random_flames(options); }, "batch upper bound");
}

void test_mutations() {
    auto parent = demo_flame();
    parent.name = "Retain my name";
    parent.source_version = "Apophysis 7X";
    parent.center = {12, -5};
    parent.zoom = 1.2;
    parent.rotate = 47;
    parent.gamma_threshold = .03;
    parent.filter_radius = .6;
    parent.transforms[0].variations = {{Variation::linear, .8}, {Variation::sinusoidal, -.2}};
    parent.transforms[1].variations = {{Variation::julian, 1}};
    parent.transforms[1].parameters.julian_power = -3;
    parent.transforms[1].parameters.julian_dist = 1.3;
    parent.transforms[2].weight = 0;
    parent.transforms[0].chaos = {1, 2, 0};
    parent.final_transform.emplace();
    parent.final_transform->pre.e = .8;
    MutationOptions options;
    options.seed = 701;
    options.strength = 0;
    options.variations = options.colors = options.parameters = options.palette = options.include_final = true;
    require(identical(mutate_flame(parent, options), parent), "zero strength is exact model identity");
    options.strength = .6;
    options.include_final = false;
    const auto candidate = mutate_flame(parent, options);
    require(!identical(candidate, parent), "enabled mutation changes parent");
    require(identical(candidate, mutate_flame(parent, options)), "mutation repeatability");
    require(!identical(candidate, mutate_flame(parent, options, 1)), "candidate stream varies");
    auto restored = candidate;
    restored.transforms = parent.transforms;
    restored.palette = parent.palette;
    require(identical(restored, parent), "mutation preserves camera, tone, final transform and source identity");
    require(candidate.transforms[2].weight == 0 && candidate.transforms[0].weight > 0,
            "probability mutation retains graph support");
    require(candidate.transforms[0].chaos == parent.transforms[0].chaos, "mutation retains Xaos graph");
    const auto negative = std::find_if(candidate.transforms[0].variations.begin(),
                                      candidate.transforms[0].variations.end(),
                                      [](Term term) { return term.id == Variation::sinusoidal; });
    require(negative != candidate.transforms[0].variations.end() && negative->weight < 0,
            "signed variation weights retain their sign");
    require(candidate.transforms[1].parameters.julian_power != 0, "JuliaN power remains valid");
    options.count = 9;
    const auto candidates = mutation_candidates(parent, options);
    require(candidates.size() == 9 && identical(candidates[0], candidate), "candidate set order and count");
    for (const auto &flame : candidates)
        validate(flame);
    options = {};
    options.geometry = false;
    require(identical(mutate_flame(parent, options), parent), "all groups disabled leaves parent intact");
    options.geometry = true;
    options.include_final = true;
    require(mutate_flame(parent, options).final_transform->pre.e != parent.final_transform->pre.e,
            "explicit final-transform mutation");
    options = {};
    options.geometry = false;
    options.colors = true;
    auto color_only = mutate_flame(parent, options);
    for (std::size_t i = 0; i < color_only.transforms.size(); ++i) {
        color_only.transforms[i].color = parent.transforms[i].color;
        color_only.transforms[i].color_speed = parent.transforms[i].color_speed;
    }
    require(identical(color_only, parent), "color mode does not change geometry or variations");
    auto tiny = demo_flame();
    const double minimum = std::numeric_limits<double>::denorm_min();
    tiny.transforms[0].variations = {{Variation::linear, minimum}, {Variation::sinusoidal, -minimum}};
    options = {};
    options.geometry = false;
    options.variations = true;
    options.allowed_variations = {Variation::linear};
    options.strength = 1;
    options.trend = -1;
    const auto retained = mutate_flame(tiny, options);
    require(retained.transforms[0].variations[0].weight == minimum &&
                retained.transforms[0].variations[1].weight == -minimum,
            "variation weight mutation retains sign at underflow boundary");
    options.strength = std::numeric_limits<double>::quiet_NaN();
    rejects([&] { mutate_flame(parent, options); }, "NaN mutation strength rejected");
    options = {};
    options.trend = 2;
    rejects([&] { mutation_candidates(parent, options); }, "trend limit enforced");
}

void test_camera_fitting() {
    auto flame = demo_flame();
    require(fit_camera(flame, 81), "nondegenerate orbit fitted");
    auto same = demo_flame();
    require(fit_camera(same, 81) && identical(same, flame), "camera fit repeatable");
    auto final = demo_flame();
    final.final_transform.emplace();
    final.final_transform->post.e = 10;
    final.final_transform->post.f = -15;
    require(fit_camera(final, 81), "final transformed orbit fitted");
    near(final.center.x, flame.center.x + 10, "camera includes final x translation");
    near(final.center.y, flame.center.y - 15, "camera includes final y translation");
    near(final.scale, flame.scale, "final translation does not affect framing size", 1e-9);
    final.final_transform->opacity = 0;
    require(fit_camera(final, 81), "disabled final transform framing");
    near(final.center.x, flame.center.x, "disabled final transform x");
    near(final.center.y, flame.center.y, "disabled final transform y");
    auto point = demo_flame();
    point.transforms.resize(1);
    point.transforms[0].pre = {0, 0, 0, 0, 2, 3};
    const auto before = point;
    require(!fit_camera(point, 81) && identical(point, before), "degenerate orbit leaves camera unchanged");
    rejects([&] { fit_camera(flame, 81, 255); }, "camera sample lower bound");
    rejects([&] { fit_camera(flame, 81, 12000, 1.1); }, "camera coverage bound");
}
} // namespace

int main() {
    try {
        test_stops_and_interpolation();
        test_palette_operations();
        test_palette_files();
        test_random_generation();
        test_mutations();
        test_camera_fitting();
        std::cout << "Creative algorithms: palettes, files, generation, mutation and camera checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Creative algorithms: " << error.what() << '\n';
        return 1;
    }
}
