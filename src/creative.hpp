#pragma once

#include "flame.hpp"
#include <span>

namespace apo::creative {

struct ColorStop {
    int index = 0;
    RGB color;
};
enum class GradientSpace { rgb, hsv_shortest };
struct PaletteAdjustment {
    double hue_degrees = 0;
    double saturation = 1, value = 1;
    std::array<double, 3> gamma{1, 1, 1};
    double contrast = 1;
};
struct PalettePreset {
    std::string name;
    Palette colors;
};

// Palette operations return a new 256-entry palette; the source is unchanged.
// Stops may be unsorted, but indices must be unique and in [0,255].
Palette palette_from_stops(std::span<const ColorStop> stops,
                           GradientSpace space = GradientSpace::rgb);
// A positive rotation moves the original entry 0 to the given positive index.
Palette rotate_palette(const Palette &palette, int shift);
Palette reverse_palette(const Palette &palette);
Palette smooth_palette(const Palette &palette, unsigned radius = 2);
Palette adjust_palette(const Palette &palette, const PaletteAdjustment &adjustment);
Palette random_palette(std::uint64_t seed, unsigned stop_count = 5);
std::vector<PalettePreset> palette_presets();
// GPL files with 2..256 colors are accepted; smaller palettes are interpolated.
// Export writes all 256 entries at 8-bit RGB precision.
Palette import_gimp_palette(std::string_view source);
std::string export_gimp_palette(const Palette &palette,
                                std::string_view name = "Apophysis Reborn palette");

struct RandomOptions {
    std::uint64_t seed = 1;
    std::size_t batch_size = 1;
    unsigned min_transforms = 3, max_transforms = 5;
    std::vector<Variation> allowed_variations;
    // Symmetry adds n-1 rotation transforms and optionally one reflection.
    unsigned rotational_symmetry = 1;
    bool reflection = false, randomize_palette = true;
    double min_contraction = .35, max_contraction = .78, translation = 1.2;
    int width = 768, height = 576;
};

// New flames have a reproducible name containing the generation seed and index.
// The allowlist is exact; an empty list chooses the documented built-in default.
std::vector<Flame> random_flames(const RandomOptions &options);

struct MutationOptions {
    std::uint64_t seed = 1;
    std::size_t count = 8;
    double strength = .25, trend = 0;
    bool geometry = true, variations = false, colors = false, parameters = false;
    bool palette = false, include_final = false;
    std::vector<Variation> allowed_variations;
};

// Mutation retains name, producer, camera, tone, output and topology. Only enabled
// parameter groups change. A zero strength returns the exact original model.
Flame mutate_flame(const Flame &parent, const MutationOptions &options,
                   std::size_t sequence = 0);
std::vector<Flame> mutation_candidates(const Flame &parent, const MutationOptions &options);

// Fit the camera to robust quantiles of an orbit. Final transforms participate in
// framing but never feed back. Returns false without edits for a degenerate orbit.
bool fit_camera(Flame &flame, std::uint64_t seed, std::size_t samples = 12000,
                 double coverage = .98);

} // namespace apo::creative
