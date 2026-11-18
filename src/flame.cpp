#include "flame.hpp"
#include "plugin.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <cctype>
#include <numbers>
#include <stdexcept>
#include <set>
#include <utility>

namespace apo {
namespace {

constexpr std::array<VariationInfo, 43> variations{{
    {Variation::linear, "linear"}, {Variation::sinusoidal, "sinusoidal"},
    {Variation::spherical, "spherical"}, {Variation::swirl, "swirl"},
    {Variation::horseshoe, "horseshoe"}, {Variation::polar, "polar"},
    {Variation::handkerchief, "handkerchief"}, {Variation::heart, "heart"},
    {Variation::disc, "disc"}, {Variation::spiral, "spiral"},
    {Variation::hyperbolic, "hyperbolic"}, {Variation::diamond, "diamond"},
    {Variation::ex, "ex"}, {Variation::julia, "julia"}, {Variation::bent, "bent"},
    {Variation::waves, "waves"}, {Variation::fisheye, "fisheye"},
    {Variation::popcorn, "popcorn"}, {Variation::exponential, "exponential"},
    {Variation::power, "power"}, {Variation::cosine, "cosine"},
    {Variation::rings, "rings"}, {Variation::fan, "fan"}, {Variation::blob, "blob"},
    {Variation::pdj, "pdj"}, {Variation::fan2, "fan2"}, {Variation::rings2, "rings2"},
    {Variation::eyefish, "eyefish"},
    {Variation::bubble, "bubble"}, {Variation::cylinder, "cylinder"},
    {Variation::perspective, "perspective"}, {Variation::noise, "noise"},
    {Variation::julian, "julian"}, {Variation::juliascope, "juliascope"}, {Variation::blur, "blur"},
    {Variation::gaussian_blur, "gaussian_blur"}, {Variation::radial_blur, "radial_blur"},
    {Variation::pie, "pie"}, {Variation::ngon, "ngon"}, {Variation::curl, "curl"},
    {Variation::rectangles, "rectangles"}, {Variation::foci, "foci"},
    {Variation::pre_blur, "pre_blur"}
}};

constexpr VariationParameterInfo builtin_parameters[]{
    {Variation::blob, "blob_low", &VariationParameters::blob_low},
    {Variation::blob, "blob_high", &VariationParameters::blob_high},
    {Variation::blob, "blob_waves", &VariationParameters::blob_waves},
    {Variation::pdj, "pdj_a", &VariationParameters::pdj_a},
    {Variation::pdj, "pdj_b", &VariationParameters::pdj_b},
    {Variation::pdj, "pdj_c", &VariationParameters::pdj_c},
    {Variation::pdj, "pdj_d", &VariationParameters::pdj_d},
    {Variation::fan2, "fan2_x", &VariationParameters::fan2_x},
    {Variation::fan2, "fan2_y", &VariationParameters::fan2_y},
    {Variation::rings2, "rings2_val", &VariationParameters::rings2_val},
    {Variation::perspective, "perspective_angle", &VariationParameters::perspective_angle},
    {Variation::perspective, "perspective_dist", &VariationParameters::perspective_dist},
    {Variation::julian, "julian_power", &VariationParameters::julian_power},
    {Variation::julian, "julian_dist", &VariationParameters::julian_dist},
    {Variation::juliascope, "juliascope_power", &VariationParameters::juliascope_power},
    {Variation::juliascope, "juliascope_dist", &VariationParameters::juliascope_dist},
    {Variation::radial_blur, "radial_blur_angle", &VariationParameters::radial_blur_angle},
    {Variation::pie, "pie_slices", &VariationParameters::pie_slices},
    {Variation::pie, "pie_rotation", &VariationParameters::pie_rotation},
    {Variation::pie, "pie_thickness", &VariationParameters::pie_thickness},
    {Variation::ngon, "ngon_sides", &VariationParameters::ngon_sides},
    {Variation::ngon, "ngon_power", &VariationParameters::ngon_power},
    {Variation::ngon, "ngon_circle", &VariationParameters::ngon_circle},
    {Variation::ngon, "ngon_corners", &VariationParameters::ngon_corners},
    {Variation::curl, "curl_c1", &VariationParameters::curl_c1},
    {Variation::curl, "curl_c2", &VariationParameters::curl_c2},
    {Variation::rectangles, "rectangles_x", &VariationParameters::rectangles_x},
    {Variation::rectangles, "rectangles_y", &VariationParameters::rectangles_y},
};

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
    for (const auto& field : builtin_parameters)
        field.validate(t.parameters, field.get(t.parameters));
    std::set<std::string> seen;
    for (const auto& term : t.variations) {
        validate_plugin_term(term);
        const auto name = term_name(term);
        if (!seen.insert(std::string(name)).second)
            throw std::invalid_argument("duplicate variation: " + std::string(name));
        if (!std::isfinite(term.weight))
            throw std::invalid_argument("nonfinite variation weight: " + std::string(name));
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

namespace {
bool prefix_ci(std::string_view text, std::string_view lower_prefix) {
    return text.size() >= lower_prefix.size() &&
        std::equal(lower_prefix.begin(), lower_prefix.end(), text.begin(),
            [](char lower, unsigned char c){ return lower == std::tolower(c); });
}
}
Producer identify_producer(std::string_view version) {
    if (prefix_ci(version, "apophysis 7x")) return Producer::seven;
    if (prefix_ci(version, "apophysis av")) return Producer::av;
    if (prefix_ci(version, "apophysis 2.")) {
        for (std::size_t i = 11; i < version.size(); ++i)
            if (prefix_ci(version.substr(i), "3d hack")) return Producer::hack3d;
        return Producer::classic;
    }
    return Producer::generic;
}
bool is_apophysis_2(std::string_view version) {
    const auto producer = identify_producer(version);
    return producer == Producer::classic || producer == Producer::hack3d;
}
bool is_apophysis(std::string_view version) { return identify_producer(version) != Producer::generic; }

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

std::string_view variation_name(Variation id) {
    for (const auto& entry : variations) if (entry.id == id) return entry.name;
    throw std::invalid_argument("unrecognized variation id");
}

std::span<const VariationParameterInfo> variation_parameter_registry() { return builtin_parameters; }

const VariationParameterInfo* find_variation_parameter(std::string_view name) {
    for (const auto& field : builtin_parameters)
        if (field.name == name)
            return &field;
    return nullptr;
}

bool VariationParameterInfo::is_integer(const VariationParameters& parameters) const {
    return integer || (parameters.apophysis_math &&
           (real_ == &VariationParameters::blob_waves || real_ == &VariationParameters::juliascope_power ||
            real_ == &VariationParameters::ngon_sides));
}

double VariationParameterInfo::get(const VariationParameters& parameters) const {
    return integer ? parameters.*integral_ : parameters.*real_;
}

void VariationParameterInfo::validate(const VariationParameters& parameters, double value) const {
    if (!std::isfinite(value))
        throw std::invalid_argument(std::string(name) + " must be finite");
    if (is_integer(parameters) && (std::trunc(value) != value ||
        value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max()))
        throw std::invalid_argument(std::string(name) + " must be a representable integer");
    if (value == 0 && (integral_ == &VariationParameters::julian_power ||
        real_ == &VariationParameters::juliascope_power || real_ == &VariationParameters::pie_slices ||
        real_ == &VariationParameters::ngon_sides))
        throw std::invalid_argument(std::string(name) + " must be nonzero");
}

void VariationParameterInfo::set(VariationParameters& parameters, double value) const {
    validate(parameters, value);
    if (integer)
        parameters.*integral_ = static_cast<int>(value);
    else
        parameters.*real_ = value;
}

Rng::Rng(std::uint64_t seed, std::uint64_t stream) : engine_(seed) {
    const auto mix = [](std::uint32_t x) {
        x ^= x >> 16;
        x *= 0x7feb352du;
        x ^= x >> 15;
        x *= 0x846ca68bu;
        return x ^ (x >> 16);
    };
    portable_state_ = mix(std::uint32_t(seed) ^ mix(std::uint32_t(seed >> 32) + 0x9e3779b9u) ^
                          mix(std::uint32_t(stream) + 0x85ebca6bu) ^
                          mix(std::uint32_t(stream >> 32) + 0xc2b2ae35u));
}

double Rng::unit() {
    if (portable_state_) {
        auto& state = *portable_state_;
        state = state * 747796405u + 2891336453u;
        std::uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
        word = (word >> 22u) ^ word;
        return double(word >> 8) * 0x1p-24;
    }
    return static_cast<double>(engine_() >> 11) * 0x1p-53;
}

Rng::BlurHistory& Rng::history(std::vector<BlurHistory>& histories, const VariationParameters& transform) {
    const auto found = std::find_if(histories.begin(), histories.end(),
        [&](const BlurHistory& h){ return h.transform == &transform; });
    if (found != histories.end())
        return *found;
    return histories.emplace_back(BlurHistory{&transform, {unit(), unit(), unit(), unit()}});
}

Point Rng::pre_blur_offset(const VariationParameters& transform) {
    double radius = -2;
    double direction;
    if (transform.apophysis_math) {
        auto& h = history(blur_histories_, transform);
        for (double draw : h.ring) radius += draw;
        direction = unit();
        h.ring[h.cursor] = transform.separate_blur_angle ? unit() : direction;
        h.cursor = (h.cursor + 1) % h.ring.size();
    } else {
        for (unsigned i = 0; i < 4; ++i) radius += unit();
        direction = unit();
    }
    const double angle = 2 * pi * direction;
    return {radius * std::cos(angle), radius * std::sin(angle)};
}

double Rng::radial_blur_sample(const VariationParameters& transform) {
    double sum = -2;
    if (transform.apophysis_math) {
        // The historical plugin has its own ring, independent of Gaussian/pre-blur.
        auto& h = history(radial_histories_, transform);
        for (double draw : h.ring) sum += draw;
        h.ring[h.cursor] = unit();
        h.cursor = (h.cursor + 1) % h.ring.size();
    } else {
        for (unsigned i = 0; i < 4; ++i) sum += unit();
    }
    return sum;
}

namespace {
Point radial_blur(Point p, Rng& rng, const VariationParameters& par, double weight) {
    // The signed coefficient scales the angular perturbation as well as zoom.
    const double sample = weight * rng.radial_blur_sample(par);
    const double angle = par.radial_blur_angle * (pi / 2);
    const double spin = std::sin(angle) * sample;
    const double zoom = std::cos(angle) * sample;
    if (spin == 0)
        return {zoom * p.x, zoom * p.y};
    const double radius = std::sqrt(p.x * p.x + p.y * p.y);
    const double direction = std::atan2(p.y, p.x) + spin;
    return {radius * std::cos(direction) + (zoom - 1) * p.x,
            radius * std::sin(direction) + (zoom - 1) * p.y};
}
} // namespace

Point apply_variation(Variation id, Point p, Rng& rng, const VariationParameters& parameters,
                      const Affine& pre) {
    const auto [x, y] = p;
    const double radius_squared = x * x + y * y;
    // The historical polar angle is measured from the positive y axis. Only
    // calculate square roots / atan2 where a variation actually requires them.
    const double guard = parameters.apophysis_math ? 1e-300 : eps;
    const auto angle = [&] { return std::atan2(x, y); };
    const auto radius = [&] { return std::sqrt(radius_squared); };
    switch (id) {
    case Variation::linear:
        return p;
    case Variation::sinusoidal:
        return {std::sin(x), std::sin(y)};
    case Variation::spherical: {
        const double d = radius_squared + guard;
        return {x / d, y / d};
    }
    case Variation::swirl: {
        const double s = std::sin(radius_squared), c = std::cos(radius_squared);
        return {x * s - y * c, x * c + y * s};
    }
    case Variation::horseshoe: {
        const double denominator = radius() + guard;
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
        if (parameters.apophysis_math) return {x / (radius_squared + guard), y};
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
    case Variation::waves: {
        const double dx = 1 / (pre.e * pre.e + guard);
        const double dy = 1 / (pre.f * pre.f + guard);
        // XML coefs c,d are the second basis vector (upstream c[1][0],c[1][1]).
        return {x + pre.c * std::sin(y * dx), y + pre.d * std::sin(x * dy)};
    }
    case Variation::fisheye: {
        const double factor = 2 / (radius() + 1);
        return {factor * y, factor * x}; // Historical fisheye intentionally exchanges X and Y.
    }
    case Variation::popcorn:
        return {x + pre.e * std::sin(std::tan(3 * y)),
                y + pre.f * std::sin(std::tan(3 * x))};
    case Variation::exponential: {
        const double extent = std::exp(x - 1), phase = pi * y;
        return {extent * std::cos(phase), extent * std::sin(phase)};
    }
    case Variation::power: {
        const double r = radius() + (parameters.apophysis_math ? guard : 0);
        const double extent = std::pow(r, x / r);
        return {extent * (y / r), extent * (x / r)};
    }
    case Variation::cosine: {
        const double phase = pi * x;
        return {std::cos(phase) * std::cosh(y), -std::sin(phase) * std::sinh(y)};
    }
    case Variation::rings: {
        const double spacing = pre.e * pre.e + guard;
        const double r = radius() + (parameters.apophysis_math ? guard : 0);
        const double extent = parameters.apophysis_math
            ? 2 * r - spacing * (std::trunc((r / spacing + 1) / 2) * 2 + r)
            : std::fmod(r + spacing, 2 * spacing) - spacing + r * (1 - spacing);
        return {extent * (y / r), extent * (x / r)};
    }
    case Variation::fan: {
        const double period = pi * (pre.e * pre.e + guard);
        const double half = .5 * period;
        double a = angle();
        if (parameters.apophysis_math) {
            const double turns = (a + pre.f) / period;
            a += (turns - std::trunc(turns) > .5) ? -half : half;
        } else {
            a += (std::fmod(a + pre.f, period) > half) ? -half : half;
        }
        return {radius() * std::cos(a), radius() * std::sin(a)};
    }
    case Variation::blob: {
        const double gain = parameters.blob_low + (parameters.blob_high - parameters.blob_low) *
                            (.5 + .5 * std::sin(parameters.blob_waves * angle()));
        // FLAM3 normalizes by radius; its origin is singular. Apophysis
        // cancels radius algebraically in its implementation and preserves zero.
        if (radius_squared == 0 && !parameters.apophysis_math) {
            const double invalid = std::numeric_limits<double>::quiet_NaN();
            return {invalid, invalid};
        }
        return {x * gain, y * gain};
    }
    case Variation::pdj:
        return {std::sin(parameters.pdj_a * y) - std::cos(parameters.pdj_b * x),
                std::sin(parameters.pdj_c * x) - std::cos(parameters.pdj_d * y)};
    case Variation::fan2: {
        const double period = pi * (parameters.fan2_x * parameters.fan2_x + eps);
        const double half = period / 2;
        const double a = angle();
        const double turns = (a + parameters.fan2_y) / period;
        const bool clockwise = parameters.apophysis_math
            ? turns - std::trunc(turns) > .5
            : a + parameters.fan2_y - period * std::trunc(turns) > half;
        const double phase = a + (clockwise ? -half : half);
        const double r = radius();
        if (parameters.apophysis_math && parameters.separate_blur_angle)
            return {r * std::cos(phase), r * std::sin(phase)}; // 7X Pascal output order.
        return {r * std::sin(phase), r * std::cos(phase)};
    }
    case Variation::rings2: {
        const double spacing = parameters.rings2_val * parameters.rings2_val + eps;
        const double r = radius();
        const double turns = std::trunc((r + spacing) / (2 * spacing));
        const double gain = 2 - spacing - 2 * spacing * turns / r;
        return {x * gain, y * gain};
    }
    case Variation::eyefish: {
        const double factor = 2 / (radius() + 1);
        return {factor * x, factor * y};
    }
    case Variation::cylinder:
        return {std::sin(x), y};
    case Variation::perspective: {
        if (parameters.apophysis_math && parameters.perspective_angle == 0)
            return p;
        const double angle = parameters.perspective_angle * (pi / 2);
        const double scale = parameters.perspective_dist /
                             (parameters.perspective_dist - y * std::sin(angle));
        return {x * scale, y * (scale * std::cos(angle))};
    }
    case Variation::pre_blur:
        return rng.pre_blur_offset(parameters);
    case Variation::bubble: {
        const double divisor = 1 + radius_squared/4;
        return {x/divisor, y/divisor};
    }
    case Variation::noise:
    case Variation::blur: {
        const double direction = rng.unit() * 2 * pi;
        const double extent = rng.unit();
        Point delta{extent * std::cos(direction), extent * std::sin(direction)};
        if (id == Variation::noise) { delta.x *= x; delta.y *= y; }
        return delta;
    }
    case Variation::julian: {
        const double power = parameters.julian_power;
        const double branch = std::floor(rng.unit() * std::abs(power));
        const double theta = (std::atan2(y, x) + 2 * pi * branch) / power;
        const double r = std::pow(radius_squared, parameters.julian_dist / (2 * power));
        return {r * std::cos(theta), r * std::sin(theta)};
    }
    case Variation::juliascope: {
        const double power = parameters.juliascope_power;
        if (parameters.apophysis_math && parameters.juliascope_dist == 1) {
            // Original +/-1 plugins skip branch selection, preserving the next random draw.
            if (power == 1) return p;
            if (power == -1) return {x / radius_squared, -y / radius_squared};
        }
        const double branch = std::floor(rng.unit() * std::abs(power));
        const double a = std::atan2(y, x);
        const double theta = (2 * pi * branch + (std::fmod(branch, 2) == 0 ? a : -a)) / power;
        const double r = std::pow(radius_squared, parameters.juliascope_dist / (2 * power));
        return {r * std::cos(theta), r * std::sin(theta)};
    }
    case Variation::gaussian_blur: {
        if (parameters.apophysis_math)
            return rng.pre_blur_offset(parameters);
        const double direction = rng.unit() * (2 * pi);
        double extent = -2;
        for (unsigned i = 0; i < 4; ++i) extent += rng.unit();
        return {extent * std::cos(direction), extent * std::sin(direction)};
    }
    case Variation::radial_blur:
        return radial_blur(p, rng, parameters, 1);
    case Variation::pie: {
        const double slice = std::trunc(rng.unit() * parameters.pie_slices + .5);
        const double phase = parameters.pie_rotation + 2 * pi *
                             (slice + rng.unit() * parameters.pie_thickness) / parameters.pie_slices;
        const double r = rng.unit();
        return {r * std::cos(phase), r * std::sin(phase)};
    }
    case Variation::ngon: {
        const double sector = 2 * pi / parameters.ngon_sides;
        const double a = std::atan2(y, x);
        double offset = a - sector * std::floor(a / sector);
        if (offset > sector / 2) offset -= sector;
        const double cosine = std::cos(offset);
        if (parameters.apophysis_math) {
            if (radius_squared == 0) return {};
            const double shape = parameters.ngon_corners * (1 / cosine - 1) + parameters.ngon_circle;
            const double gain = shape * std::pow(radius_squared, -.5 * parameters.ngon_power);
            return {x * gain, y * gain};
        }
        const double shape = parameters.ngon_corners * (1 / (cosine + eps) - 1) + parameters.ngon_circle;
        const double gain = shape / (std::pow(radius_squared, .5 * parameters.ngon_power) + eps);
        return {x * gain, y * gain};
    }
    case Variation::curl: {
        const double re = 1 + parameters.curl_c1 * x + parameters.curl_c2 * (x*x - y*y);
        const double im = parameters.curl_c1 * y + 2 * parameters.curl_c2 * x * y;
        const double denominator = re*re + im*im;
        return {(x * re + y * im) / denominator, (y * re - x * im) / denominator};
    }
    case Variation::rectangles: {
        const auto reflected = [](double v, double spacing) {
            return spacing == 0 ? v : (2 * std::floor(v / spacing) + 1) * spacing - v;
        };
        return {reflected(x, parameters.rectangles_x), reflected(y, parameters.rectangles_y)};
    }
    case Variation::foci: {
        const double e = .5 * std::exp(x);
        const double inverse = .25 / e;
        const double d = e + inverse - std::cos(y);
        const double denominator = d == 0 && parameters.apophysis_math ? 1e-6 : d;
        return {(e - inverse) / denominator, std::sin(y) / denominator};
    }
    case Variation::bent:
        return {x < 0 ? 2 * x : x, y < 0 ? y / 2 : y};
    case Variation::plugin:
        throw std::invalid_argument("Plugin variations need their retained Term descriptor");
    }
    throw std::invalid_argument("unrecognized variation id");
}

Orbit apply_transform(const Transform& transform, Orbit previous, Rng& rng) {
    Point input = transform.pre.apply(previous.point);
    // Pre-blur changes the shared input before any ordinary term reads it.
    for (const auto& term : transform.variations) {
        if (term.id == Variation::pre_blur && term.weight != 0) {
            const Point delta = rng.pre_blur_offset(transform.parameters);
            input.x += term.weight * delta.x;
            input.y += term.weight * delta.y;
            break;
        }
    }
    Point sum;
    for (const auto& term : transform.variations) {
        if (term.id == Variation::pre_blur || term.weight == 0) continue; // A disabled stochastic variation must not consume a draw.
        if (term.id == Variation::radial_blur) {
            const auto v = radial_blur(input, rng, transform.parameters, term.weight);
            sum.x += v.x;
            sum.y += v.y;
            continue;
        }
        const Point v = term.id == Variation::plugin
            ? term.plugin->evaluate(input, transform.pre, term.weight, term.plugin_parameters, rng)
            : apply_variation(term.id, input, rng, transform.parameters, transform.pre);
        sum.x += term.weight * v.x;
        sum.y += term.weight * v.y;
    }
    return {transform.post.apply(sum),
            (1 - transform.color_speed) * previous.color + transform.color_speed * transform.color};
}

void validate(const Flame& flame) {
    // A serialized variation name must identify one module throughout the model.
    (void)plugin_identity(flame);
    validate_color_curves(flame.curves);
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
    if ((flame.filter_radius && (!std::isfinite(*flame.filter_radius) || *flame.filter_radius < 0)) ||
        !std::isfinite(flame.gamma_threshold) || flame.gamma_threshold < 0)
        throw std::invalid_argument("filter and gamma_threshold must be finite and nonnegative");
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

std::uint64_t TransformChooser::storage_bytes(const Flame& flame) {
    const auto n = std::uint64_t(flame.transforms.size());
    std::uint64_t distributions = 1;
    for (const auto& t : flame.transforms)
        if (t.weight != 0 && std::any_of(t.chaos.begin(), t.chaos.end(),
                                       [](double weight) { return weight != 1; })) {
            if (distributions == UINT64_MAX)
                throw std::invalid_argument("Transform selector storage overflows");
            ++distributions;
        }
    const auto multiply = [](std::uint64_t a, std::uint64_t b) {
        if (b && a > UINT64_MAX / b)
            throw std::invalid_argument("Transform selector storage overflows");
        return a * b;
    };
    const auto headers = multiply(n, sizeof(std::vector<double>));
    const auto entries = multiply(multiply(n, distributions), sizeof(double));
    if (entries > UINT64_MAX - headers)
        throw std::invalid_argument("Transform selector storage overflows");
    return headers + entries;
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
