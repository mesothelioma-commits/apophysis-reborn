#include "flame.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace apo {
namespace {

constexpr std::array<VariationInfo, 15> variations{{
    {Variation::linear, "linear"}, {Variation::sinusoidal, "sinusoidal"},
    {Variation::spherical, "spherical"}, {Variation::swirl, "swirl"},
    {Variation::horseshoe, "horseshoe"}, {Variation::polar, "polar"},
    {Variation::handkerchief, "handkerchief"}, {Variation::heart, "heart"},
    {Variation::disc, "disc"}, {Variation::spiral, "spiral"},
    {Variation::hyperbolic, "hyperbolic"}, {Variation::diamond, "diamond"},
    {Variation::ex, "ex"}, {Variation::julia, "julia"}, {Variation::bent, "bent"}
}};

constexpr double eps = 1e-10;
constexpr double pi = std::numbers::pi;

bool unit_interval(double value) {
    return value >= 0 && value <= 1;
}

bool positive_finite(double value) {
    return value > 0 && std::isfinite(value);
}

bool valid_rgb(RGB color) {
    return unit_interval(color.r) && unit_interval(color.g) && unit_interval(color.b);
}

bool finite_affine(const Affine& a) {
    return std::isfinite(a.a) && std::isfinite(a.b) && std::isfinite(a.c) &&
           std::isfinite(a.d) && std::isfinite(a.e) && std::isfinite(a.f);
}

void validate_transform(const Transform& t) {
    if (!finite_affine(t.pre) || !finite_affine(t.post))
        throw std::invalid_argument("affine coefficients must be finite");
    if (!std::isfinite(t.weight) || t.weight < 0)
        throw std::invalid_argument("weight must be finite and nonnegative");
    if (!unit_interval(t.color) || !unit_interval(t.color_speed) || !unit_interval(t.opacity))
        throw std::invalid_argument("color, color_speed and opacity must be in [0,1]");
    std::array<bool, variations.size()> seen{};
    for (const auto& term : t.variations) {
        const auto index = static_cast<std::size_t>(term.id);
        if (index >= variations.size())
            throw std::invalid_argument("unrecognized variation id");
        if (seen[index])
            throw std::invalid_argument("duplicate variation: " + std::string(variations[index].name));
        if (!std::isfinite(term.weight))
            throw std::invalid_argument("nonfinite variation weight: " + std::string(variations[index].name));
        seen[index] = true;
    }
}

// Store the normalized mantissa and exponent of each product separately. This
// avoids overflow/underflow even on MSVC where long double has the range of double.
struct ScaledWeight {
    double mantissa;
    int exponent;
};

ScaledWeight weight_product(double weight, double multiplier) {
    if (weight == 0 || multiplier == 0)
        return {0, 0};
    int weight_exponent, multiplier_exponent;
    const auto w = std::frexp(weight, &weight_exponent);
    const auto m = std::frexp(multiplier, &multiplier_exponent);
    int adjust;
    const auto product = std::frexp(w * m, &adjust);
    return {product, weight_exponent + multiplier_exponent + adjust};
}

std::vector<double> cumulative_weights(const Flame& flame, std::span<const double> chaos) {
    const auto weight_at = [&](std::size_t i) {
        return weight_product(flame.transforms[i].weight, i < chaos.size() ? chaos[i] : 1.0);
    };
    ScaledWeight largest{0, 0};
    for (std::size_t i = 0; i < flame.transforms.size(); ++i) {
        const auto w = weight_at(i);
        if (w.mantissa > 0 && (largest.mantissa == 0 || w.exponent > largest.exponent ||
              (w.exponent == largest.exponent && w.mantissa > largest.mantissa)))
            largest = w;
    }
    if (largest.mantissa == 0)
        throw std::invalid_argument("transform distribution has no positive weights");
    std::vector<double> cumulative;
    cumulative.reserve(flame.transforms.size());
    double sum = 0;
    for (std::size_t i = 0; i < flame.transforms.size(); ++i) {
        const auto w = weight_at(i);
        if (w.mantissa > 0)
            sum += std::ldexp(w.mantissa / largest.mantissa, w.exponent - largest.exponent);
        cumulative.push_back(sum);
    }
    // Normalize so upper_bound is defined even for the largest legal u and a
    // distribution ending in zeros. Multiplying u by sum can round up to sum.
    for (auto& boundary : cumulative)
        boundary /= sum;
    return cumulative;
}

} // namespace

Palette default_palette() {
    Palette palette;
    for (std::size_t i = 0; i < palette.size(); ++i) {
        const double level = static_cast<double>(i) / (palette.size() - 1);
        palette[i] = {level, level, level};
    }
    return palette;
}

Point Affine::apply(Point p) const {
    return {a * p.x + c * p.y + e, b * p.x + d * p.y + f};
}

std::span<const VariationInfo> variation_registry() { return variations; }

std::optional<Variation> find_variation(std::string_view name) {
    for (const auto& entry : variations)
        if (entry.name == name) return entry.id;
    return std::nullopt;
}

double Rng::unit() {
    return static_cast<double>(engine_() >> 11) * 0x1p-53;
}

Point apply_variation(Variation id, Point p, Rng& rng) {
    const auto [x, y] = p;
    const double radius_squared = x * x + y * y;
    // The historical polar angle is measured from the positive y axis. Only
    // calculate square roots / atan2 where a variation actually requires them.
    const auto angle = [&] { return std::atan2(x, y); };
    const auto radius = [&] { return std::sqrt(radius_squared); };
    switch (id) {
    case Variation::linear:
        return p;
    case Variation::sinusoidal:
        return {std::sin(x), std::sin(y)};
    case Variation::spherical:
        return {x / (radius_squared + eps), y / (radius_squared + eps)};
    case Variation::swirl: {
        const double s = std::sin(radius_squared), c = std::cos(radius_squared);
        return {x * s - y * c, x * c + y * s};
    }
    case Variation::horseshoe: {
        const double denominator = radius() + eps;
        return {(x - y) * (x + y) / denominator, 2 * x * y / denominator};
    }
    case Variation::polar:
        return {angle() / pi, radius() - 1};
    case Variation::handkerchief: {
        const double r = radius(), a = angle();
        return {r * std::sin(a + r), r * std::cos(a - r)};
    }
    case Variation::heart: {
        const double r = radius(), a = angle() * r;
        return {r * std::sin(a), -r * std::cos(a)};
    }
    case Variation::disc: {
        const double a = angle() / pi, phase = pi * radius();
        return {a * std::sin(phase), a * std::cos(phase)};
    }
    case Variation::spiral: {
        const double r = radius(), guarded = r + eps;
        return {(y / r + std::sin(guarded)) / guarded, (x / r - std::cos(guarded)) / guarded};
    }
    case Variation::hyperbolic: {
        const double r = radius(), guarded = r + eps;
        return {(x / r) / guarded, (y / r) * guarded};
    }
    case Variation::diamond: {
        const double r = radius();
        return {(x / r) * std::cos(r), (y / r) * std::sin(r)};
    }
    case Variation::ex: {
        const double a = angle(), r = radius();
        const double s = std::sin(a + r), c = std::cos(a - r);
        const double first = r * s * s * s, second = r * c * c * c;
        return {first + second, first - second};
    }
    case Variation::julia: {
        const double a = angle() * .5 + (rng.unit() < .5 ? pi : 0.0);
        const double r = std::sqrt(radius());
        return {r * std::cos(a), r * std::sin(a)};
    }
    case Variation::bent:
        return {x < 0 ? 2 * x : x, y < 0 ? y / 2 : y};
    }
    throw std::invalid_argument("unrecognized variation id");
}

Orbit apply_transform(const Transform& transform, Orbit previous, Rng& rng) {
    const Point input = transform.pre.apply(previous.point);
    Point sum;
    for (const auto& term : transform.variations) {
        if (term.weight == 0) continue; // A disabled stochastic variation must not consume a draw.
        const Point v = apply_variation(term.id, input, rng);
        sum.x += term.weight * v.x;
        sum.y += term.weight * v.y;
    }
    return {transform.post.apply(sum),
            (1 - transform.color_speed) * previous.color + transform.color_speed * transform.color};
}

void validate(const Flame& flame) {
    if (flame.model_version != Flame::current_model_version)
        throw std::invalid_argument("unsupported flame model version");
    if (flame.width < 1 || flame.height < 1 || flame.oversample < 1)
        throw std::invalid_argument("width, height and oversampling must be positive");
    if (!std::isfinite(flame.time) || !positive_finite(flame.quality))
        throw std::invalid_argument("time must be finite and quality must be positive and finite");
    if (!std::isfinite(flame.center.x) || !std::isfinite(flame.center.y) || !std::isfinite(flame.rotate))
        throw std::invalid_argument("camera center and rotation must be finite");
    if (!positive_finite(flame.scale) || !std::isfinite(flame.zoom) ||
        !positive_finite(flame.scale * std::exp2(flame.zoom)))
        throw std::invalid_argument("camera scale and zoom must yield a positive finite density");
    if (!positive_finite(flame.gamma) || !std::isfinite(flame.brightness) || flame.brightness < 0 ||
        !unit_interval(flame.vibrancy))
        throw std::invalid_argument("gamma must be positive, brightness nonnegative and vibrancy in [0,1]");
    if (flame.palette_mode != PaletteMode::step && flame.palette_mode != PaletteMode::linear)
        throw std::invalid_argument("unsupported palette mode");
    if (!valid_rgb(flame.background) || !std::all_of(flame.palette.begin(), flame.palette.end(), valid_rgb))
        throw std::invalid_argument("background and palette channels must be in [0,1]");

    std::optional<std::size_t> last_positive;
    for (std::size_t i = 0; i < flame.transforms.size(); ++i)
        if (flame.transforms[i].weight > 0) last_positive = i;
    if (!last_positive)
        throw std::invalid_argument("at least one regular transform must have positive weight");
    for (std::size_t i = 0; i < flame.transforms.size(); ++i) {
        const auto& t = flame.transforms[i];
        try {
            validate_transform(t);
            if (t.chaos.size() > flame.transforms.size())
                throw std::invalid_argument("too many chaos destinations");
            bool has_exit = *last_positive >= t.chaos.size();
            for (std::size_t j = 0; j < t.chaos.size(); ++j) {
                const double multiplier = t.chaos[j];
                if (!std::isfinite(multiplier) || multiplier < 0)
                    throw std::invalid_argument("chaos weights must be finite and nonnegative");
                if (multiplier > 0 && flame.transforms[j].weight > 0)
                    has_exit = true;
            }
            if (t.weight > 0 && !has_exit)
                throw std::invalid_argument("reachable chaos row has no positive destination");
        } catch (const std::invalid_argument& e) {
            throw std::invalid_argument("transform " + std::to_string(i + 1) + ": " + e.what());
        }
    }
    if (flame.final_transform) {
        try {
            validate_transform(*flame.final_transform);
            if (!flame.final_transform->chaos.empty())
                throw std::invalid_argument("final transform cannot have chaos transitions");
        } catch (const std::invalid_argument& e) {
            throw std::invalid_argument(std::string("final transform: ") + e.what());
        }
    }
}

Flame demo_flame() {
    Flame flame;
    flame.name = "Foundation triangle";
    flame.palette_mode = PaletteMode::linear;
    flame.width = 640;
    flame.height = 640;
    flame.center = {0, .65};
    flame.scale = 260;
    for (std::size_t i = 0; i < flame.palette.size(); ++i) {
        const double t = static_cast<double>(i) / (flame.palette.size() - 1);
        flame.palette[i] = {t, 1 - std::abs(t - .5), 1 - t};
    }
    for (const auto& [offset, color] : std::array<std::pair<Point, double>, 3>{{
            {{-.5, 0}, .1}, {{.5, 0}, .6}, {{0, .7}, .95}}}) {
        Transform transform;
        transform.pre = {.5, 0, 0, .5, offset.x, offset.y};
        transform.color = color;
        flame.transforms.push_back(transform);
    }
    return flame;
}

TransformChooser::TransformChooser(const Flame& flame)
    : initial_(cumulative_weights(flame, {})), rows_(flame.transforms.size()) {
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const auto& t = flame.transforms[i];
        if (t.weight == 0) continue;
        if (std::any_of(t.chaos.begin(), t.chaos.end(), [](double w) { return w != 1; }))
            rows_[i] = cumulative_weights(flame, t.chaos);
    }
}

std::size_t TransformChooser::pick(double unit, std::optional<std::size_t> previous) const {
    if (!(unit >= 0 && unit < 1))
        throw std::invalid_argument("transform selection requires a variate in [0,1)");
    if (previous && *previous >= rows_.size())
        throw std::invalid_argument("previous transform index is out of range");
    const auto& row = previous && !rows_[*previous].empty() ? rows_[*previous] : initial_;
    return static_cast<std::size_t>(std::upper_bound(row.begin(), row.end(), unit) - row.begin());
}

} // namespace apo
