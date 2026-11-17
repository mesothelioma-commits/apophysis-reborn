#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace apo {

struct Point { double x = 0, y = 0; };
struct RGB { double r = 0, g = 0, b = 0; }; // Channels in [0,1].
enum class PaletteMode { step, linear };
using Palette = std::array<RGB, 256>;
Palette default_palette();
enum class Producer { generic, classic, hack3d, seven, av };
Producer identify_producer(std::string_view version);
bool is_apophysis(std::string_view version);
bool is_apophysis_2(std::string_view version);

struct Affine {
    // XML coefs order: x' = a*x + c*y + e; y' = b*x + d*y + f.
    double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
    Point apply(Point p) const;
};

enum class Variation : unsigned {
    linear, sinusoidal, spherical, swirl, horseshoe, polar, handkerchief, heart,
    disc, spiral, hyperbolic, diamond, ex, julia, bent, bubble = 28, noise = 31, julian = 32, blur = 34, foci = 64, pre_blur = 67
};
struct VariationInfo { Variation id; std::string_view name; };
std::span<const VariationInfo> variation_registry();
std::optional<Variation> find_variation(std::string_view name);
std::string_view variation_name(Variation id);
struct VariationParameters {
    int julian_power = 1;
    double julian_dist = 1;
    bool apophysis_math = false;
    bool separate_blur_angle = false; // 7X Pascal pre-blur draws a separate angle.
};
struct Term { Variation id = Variation::linear; double weight = 1; };

struct Transform {
    Affine pre, post;
    VariationParameters parameters;
    std::vector<Term> variations{{Variation::linear, 1}};
    double weight = 1, color = 0, color_speed = 0.5, opacity = 1;
    std::vector<double> chaos; // Per-destination multipliers; missing trailing values are 1.
};
struct Flame {
    static constexpr unsigned current_model_version = 1;
    unsigned model_version = current_model_version;
    std::string name, source_version;
    double time = 0, quality = 10; // Keyframe time and samples per nominal image pixel.
    int width = 640, height = 480;
    Point center;
    double scale = 50, zoom = 0, rotate = 0; // Pixels per unit at declared size; rotation in degrees.
    double gamma = 2.2, brightness = 4, vibrancy = 1;
    std::optional<double> filter_radius; // Absence retains the original Reborn box reconstruction.
    double gamma_threshold = 0;
    int oversample = 1;
    RGB background;
    PaletteMode palette_mode = PaletteMode::step;
    Palette palette = default_palette();
    std::vector<Transform> transforms;
    std::optional<Transform> final_transform;
};

class Rng {
public:
    explicit Rng(std::uint64_t seed) : engine_(seed) {}
    Point pre_blur_offset(const VariationParameters& transform);
    double unit(); // Exactly 53 random bits, in [0,1).
private:
    struct BlurHistory {
        const VariationParameters* transform;
        std::array<double, 4> ring;
        unsigned cursor = 0;
    };
    std::mt19937_64 engine_;
    std::vector<BlurHistory> blur_histories_; // Per transform, private to this orbit stream.
};

Point apply_variation(Variation id, Point p, Rng& rng, const VariationParameters& parameters = {});
struct Orbit { Point point; double color = 0; };
Orbit apply_transform(const Transform& transform, Orbit previous, Rng& rng);
void validate(const Flame& flame); // Throws std::invalid_argument with the offending setting.
Flame demo_flame();

class TransformChooser {
public:
    // Flame must have passed validate(). Each row is weighted by destination weight and source chaos.
    explicit TransformChooser(const Flame& flame);
    std::size_t pick(double unit, std::optional<std::size_t> previous = {}) const;
private:
    std::vector<double> initial_;
    std::vector<std::vector<double>> rows_;
};

} // namespace apo
