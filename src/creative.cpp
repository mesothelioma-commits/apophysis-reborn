#include "creative.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <numeric>
#include <numbers>
#include <sstream>
#include <stdexcept>

namespace apo::creative {
namespace {
constexpr double pi = std::numbers::pi;

double unit(double value) {
    return std::clamp(value, 0.0, 1.0);
}
bool valid_color(RGB value) {
    return value.r >= 0 && value.r <= 1 && value.g >= 0 && value.g <= 1 && value.b >= 0 &&
           value.b <= 1;
}
void validate_palette(const Palette &palette) {
    if (!std::all_of(palette.begin(), palette.end(), valid_color))
        throw std::invalid_argument("Palette channels must be finite and in [0,1]");
}
RGB mix(RGB a, RGB b, double amount) {
    return {std::lerp(a.r, b.r, amount), std::lerp(a.g, b.g, amount),
            std::lerp(a.b, b.b, amount)};
}
struct HSV {
    double h, s, v;
};
HSV rgb_hsv(RGB rgb) {
    const double high = std::max({rgb.r, rgb.g, rgb.b});
    const double low = std::min({rgb.r, rgb.g, rgb.b});
    const double delta = high - low;
    double h = 0;
    if (delta > 0) {
        if (high == rgb.r)
            h = 60 * std::fmod((rgb.g - rgb.b) / delta, 6.0);
        else if (high == rgb.g)
            h = 60 * ((rgb.b - rgb.r) / delta + 2);
        else
            h = 60 * ((rgb.r - rgb.g) / delta + 4);
    }
    if (h < 0)
        h += 360;
    return {h, high > 0 ? delta / high : 0, high};
}
RGB hsv_rgb(HSV hsv) {
    const double h = std::fmod(std::fmod(hsv.h, 360.0) + 360, 360.0) / 60;
    const double c = hsv.v * hsv.s;
    const double x = c * (1 - std::abs(std::fmod(h, 2.0) - 1));
    RGB rgb;
    if (h < 1)
        rgb = {c, x, 0};
    else if (h < 2)
        rgb = {x, c, 0};
    else if (h < 3)
        rgb = {0, c, x};
    else if (h < 4)
        rgb = {0, x, c};
    else if (h < 5)
        rgb = {x, 0, c};
    else
        rgb = {c, 0, x};
    const double offset = hsv.v - c;
    return {unit(rgb.r + offset), unit(rgb.g + offset), unit(rgb.b + offset)};
}
RGB mix_hsv(RGB a, RGB b, double amount) {
    auto first = rgb_hsv(a), last = rgb_hsv(b);
    // A neutral stop borrows its colored neighbor's hue instead of adding a red detour.
    if (first.s == 0)
        first.h = last.h;
    if (last.s == 0)
        last.h = first.h;
    const double difference = std::remainder(last.h - first.h, 360.0);
    return hsv_rgb({first.h + amount * difference, std::lerp(first.s, last.s, amount),
                    std::lerp(first.v, last.v, amount)});
}
struct HSL { double h, s, l; };
HSL rgb_hsl(RGB rgb) {
    const auto hsv = rgb_hsv(rgb);
    const double low = std::min({rgb.r, rgb.g, rgb.b});
    const double lightness = (hsv.v + low) / 2;
    const double delta = hsv.v - low;
    return {hsv.h, delta == 0 ? 0 : delta / (1 - std::abs(2 * lightness - 1)), lightness};
}
RGB hsl_rgb(HSL hsl) {
    const double value = hsl.l + hsl.s * std::min(hsl.l, 1 - hsl.l);
    return hsv_rgb({hsl.h, value == 0 ? 0 : 2 * (1 - hsl.l / value), value});
}
RGB mix_hsl(RGB a, RGB b, double amount) {
    auto first = rgb_hsl(a), last = rgb_hsl(b);
    if (first.s == 0) first.h = last.h;
    if (last.s == 0) last.h = first.h;
    return hsl_rgb({first.h + amount * std::remainder(last.h - first.h, 360.0),
                    std::lerp(first.s, last.s, amount), std::lerp(first.l, last.l, amount)});
}
double linear_srgb(double value) {
    return value <= .04045 ? value / 12.92 : std::pow((value + .055) / 1.055, 2.4);
}
double encoded_srgb(double value) {
    return value <= .0031308 ? value * 12.92 : 1.055 * std::pow(value, 1 / 2.4) - .055;
}
bool valid_space(GradientSpace space) {
    return space == GradientSpace::rgb || space == GradientSpace::hsv_shortest ||
           space == GradientSpace::linear_rgb || space == GradientSpace::hsl_shortest;
}
RGB hex(unsigned value) {
    return {double((value >> 16) & 255) / 255, double((value >> 8) & 255) / 255,
            double(value & 255) / 255};
}
std::uint64_t stream_seed(std::uint64_t seed, std::uint64_t sequence) {
    // SplitMix64 finalizer isolates deterministic candidate streams from one another.
    auto value = seed + UINT64_C(0x9e3779b97f4a7c15) * (sequence + 1);
    value = (value ^ (value >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    value = (value ^ (value >> 27)) * UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31);
}
double uniform(Rng &rng, double low, double high) {
    return std::lerp(low, high, rng.unit());
}
std::size_t choose(Rng &rng, std::size_t count) {
    return std::min(count - 1, static_cast<std::size_t>(rng.unit() * double(count)));
}
std::vector<Variation> variation_choices(const std::vector<Variation> &requested) {
    std::vector<Variation> result = requested;
    if (result.empty()) {
        result = {Variation::linear, Variation::sinusoidal, Variation::swirl,
                  Variation::spherical, Variation::horseshoe, Variation::bubble,
                  Variation::julia, Variation::disc};
    }
    std::sort(result.begin(), result.end(), [](Variation a, Variation b) {
        return static_cast<unsigned>(a) < static_cast<unsigned>(b);
    });
    result.erase(std::unique(result.begin(), result.end()), result.end());
    for (const auto id : result)
        (void)variation_name(id);
    if (result.size() == 1 && result.front() == Variation::pre_blur)
        throw std::invalid_argument("Choose at least one ordinary variation besides pre_blur");
    return result;
}
bool contains(const std::vector<Variation> &values, Variation value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}
std::vector<Variation> ordinary_choices(std::vector<Variation> values) {
    std::erase(values, Variation::pre_blur);
    return values;
}
bool finite_point(Point point) {
    return std::isfinite(point.x) && std::isfinite(point.y) && std::abs(point.x) < 1e12 &&
           std::abs(point.y) < 1e12;
}
void check_random(const RandomOptions &options) {
    if (options.batch_size < 1 || options.batch_size > 256)
        throw std::invalid_argument("Random batch size must be in [1,256]");
    if (options.min_transforms < 2 || options.max_transforms > 12 ||
        options.min_transforms > options.max_transforms)
        throw std::invalid_argument("Random transform bounds must satisfy 2 <= minimum <= maximum <= 12");
    if (options.rotational_symmetry < 1 || options.rotational_symmetry > 12)
        throw std::invalid_argument("Rotational symmetry order must be in [1,12]");
    if (!(options.min_contraction > 0 && options.max_contraction < 1 &&
          options.min_contraction <= options.max_contraction))
        throw std::invalid_argument("Contraction bounds must satisfy 0 < minimum <= maximum < 1");
    if (!std::isfinite(options.translation) || options.translation <= 0 || options.translation > 100)
        throw std::invalid_argument("Translation range must be finite and in (0,100]");
    if (options.width < 1 || options.height < 1 || options.width > 16384 || options.height > 16384)
        throw std::invalid_argument("Generated dimensions must be in [1,16384]");
}
void check_mutation(const MutationOptions &options) {
    if (!(options.strength >= 0 && options.strength <= 1))
        throw std::invalid_argument("Mutation strength must be in [0,1]");
    if (!(options.trend >= -1 && options.trend <= 1))
        throw std::invalid_argument("Mutation trend must be in [-1,1]");
    if (options.count < 1 || options.count > 256)
        throw std::invalid_argument("Mutation candidate count must be in [1,256]");
}
double directed(Rng &rng, double trend) {
    return (1 - std::abs(trend)) * uniform(rng, -1, 1) + trend;
}
double finite_or_original(double proposed, double original) {
    return std::isfinite(proposed) ? proposed : original;
}
void mutate_transform(Transform &transform, const MutationOptions &options,
                      const std::vector<Variation> &choices, Rng &rng) {
    const double amount = options.strength;
    const auto delta = [&] { return directed(rng, options.trend); };
    if (options.geometry) {
        const auto old = transform.pre;
        const double angle = amount * .7 * delta();
        const double stretch = std::exp(amount * .45 * delta());
        const double cs = std::cos(angle) * stretch, sn = std::sin(angle) * stretch;
        const double shear = amount * .25 * delta();
        transform.pre.a = finite_or_original(cs * old.a - sn * old.b, old.a);
        transform.pre.b = finite_or_original(sn * old.a + cs * old.b, old.b);
        transform.pre.c = finite_or_original(cs * old.c - sn * old.d + shear * old.a, old.c);
        transform.pre.d = finite_or_original(sn * old.c + cs * old.d + shear * old.b, old.d);
        transform.pre.e = finite_or_original(old.e + amount * .6 * delta(), old.e);
        transform.pre.f = finite_or_original(old.f + amount * .6 * delta(), old.f);
    }
    if (options.variations) {
        for (auto &term : transform.variations) {
            const double proposed = term.weight * std::exp(amount * .8 * delta());
            if (std::isfinite(proposed) && (term.weight == 0 || proposed != 0))
                term.weight = proposed;
        }
        const auto id = choices[choose(rng, choices.size())];
        auto term = std::find_if(transform.variations.begin(), transform.variations.end(),
                                 [&](const Term &value) { return value.id == id; });
        if (term == transform.variations.end())
            transform.variations.push_back({id, amount * uniform(rng, .15, .5)});
        else if (term->weight == 0)
            term->weight = amount * uniform(rng, .15, .5);
        std::sort(transform.variations.begin(), transform.variations.end(), [](Term a, Term b) {
            return static_cast<unsigned>(a.id) < static_cast<unsigned>(b.id);
        });
    }
    if (options.colors) {
        const double coordinate = transform.color + amount * .35 * delta();
        transform.color = coordinate - std::floor(coordinate);
        transform.color_speed = unit(transform.color_speed + amount * .3 * delta());
    }
    if (options.parameters) {
        // Keep zero-probability branches zero, and positive support positive, so
        // a valid Xaos graph remains valid after probability mutation.
        if (transform.weight > 0) {
            const double value = transform.weight * std::exp(amount * .8 * delta());
            if (value > 0 && std::isfinite(value))
                transform.weight = value;
        }
        const bool julian = std::any_of(transform.variations.begin(), transform.variations.end(),
                                        [](Term term) { return term.id == Variation::julian && term.weight != 0; });
        if (julian) {
            const auto change = static_cast<long long>(std::llround(amount * 4 * delta()));
            const auto power = static_cast<long long>(transform.parameters.julian_power) + change;
            if (power != 0 && power >= std::numeric_limits<int>::min() &&
                power <= std::numeric_limits<int>::max())
                transform.parameters.julian_power = static_cast<int>(power);
            const double distance = transform.parameters.julian_dist + amount * .75 * delta();
            transform.parameters.julian_dist = finite_or_original(distance, transform.parameters.julian_dist);
        }
    }
}
} // namespace

RGB blend_colors(RGB from, RGB to, double amount, GradientSpace space) {
    if (!valid_color(from) || !valid_color(to) || !(amount >= 0 && amount <= 1))
        throw std::invalid_argument("Color channels and blend amount must be in [0,1]");
    if (!valid_space(space)) throw std::invalid_argument("Unknown gradient interpolation space");
    if (amount == 0) return from;
    if (amount == 1) return to;
    if (space == GradientSpace::rgb) return mix(from, to, amount);
    if (space == GradientSpace::hsv_shortest) return mix_hsv(from, to, amount);
    if (space == GradientSpace::hsl_shortest) return mix_hsl(from, to, amount);
    const auto channel = [&](double a, double b) {
        return unit(encoded_srgb(std::lerp(linear_srgb(a), linear_srgb(b), amount)));
    };
    return {channel(from.r, to.r), channel(from.g, to.g), channel(from.b, to.b)};
}

Palette curve_palette(const Palette& palette, const ColorCurves& curves) {
    validate_palette(palette);
    const ColorCurveEvaluator evaluator(curves);
    if (!evaluator.active()) return palette;
    auto result = palette;
    for (auto& color : result)
        color = {unit(evaluator.map(0, color.r)), unit(evaluator.map(1, color.g)),
                 unit(evaluator.map(2, color.b))};
    return result;
}

Palette palette_from_stops(std::span<const ColorStop> source, GradientSpace space) {
    if (source.size() < 2 || source.size() > 256)
        throw std::invalid_argument("Use between 2 and 256 palette stops");
    if (!valid_space(space))
        throw std::invalid_argument("Unknown gradient interpolation space");
    std::vector<ColorStop> stops(source.begin(), source.end());
    std::sort(stops.begin(), stops.end(), [](const ColorStop &a, const ColorStop &b) {
        return a.index < b.index;
    });
    for (std::size_t i = 0; i < stops.size(); ++i) {
        if (stops[i].index < 0 || stops[i].index > 255 || !valid_color(stops[i].color))
            throw std::invalid_argument("Palette stop indices must be in [0,255] and RGB channels in [0,1]");
        if (i && stops[i - 1].index == stops[i].index)
            throw std::invalid_argument("Palette stops must have distinct indices");
    }
    Palette result;
    std::size_t right = 0;
    for (int i = 0; i < 256; ++i) {
        while (right < stops.size() && stops[right].index < i)
            ++right;
        if (right < stops.size() && stops[right].index == i)
            result[i] = stops[right].color;
        else if (right == 0)
            result[i] = stops.front().color;
        else if (right == stops.size())
            result[i] = stops.back().color;
        else {
            const auto &left = stops[right - 1], &last = stops[right];
            const double amount = double(i - left.index) / double(last.index - left.index);
            result[i] = blend_colors(left.color, last.color, amount, space);
        }
    }
    return result;
}

Palette rotate_palette(const Palette &palette, int shift) {
    validate_palette(palette);
    Palette result;
    const int offset = ((shift % 256) + 256) % 256;
    for (int i = 0; i < 256; ++i)
        result[(i + offset) % 256] = palette[i];
    return result;
}
Palette reverse_palette(const Palette &palette) {
    validate_palette(palette);
    Palette result;
    std::reverse_copy(palette.begin(), palette.end(), result.begin());
    return result;
}
Palette smooth_palette(const Palette &palette, unsigned radius) {
    validate_palette(palette);
    if (radius > 32)
        throw std::invalid_argument("Palette smoothing radius must be in [0,32]");
    if (radius == 0)
        return palette;
    Palette result;
    for (int i = 0; i < 256; ++i) {
        RGB sum;
        for (int offset = -int(radius); offset <= int(radius); ++offset) {
            const auto color = palette[(i + offset + 256) % 256];
            sum.r += color.r;
            sum.g += color.g;
            sum.b += color.b;
        }
        const double size = 2 * radius + 1;
        result[i] = {unit(sum.r / size), unit(sum.g / size), unit(sum.b / size)};
    }
    return result;
}
Palette adjust_palette(const Palette &palette, const PaletteAdjustment &adjustment) {
    validate_palette(palette);
    if (!std::isfinite(adjustment.hue_degrees) || !std::isfinite(adjustment.saturation) ||
        adjustment.saturation < 0 || !std::isfinite(adjustment.value) || adjustment.value < 0 ||
        !std::isfinite(adjustment.contrast) || adjustment.contrast < 0 ||
        std::any_of(adjustment.gamma.begin(), adjustment.gamma.end(),
                    [](double value) { return !(value > 0 && std::isfinite(value)); }))
        throw std::invalid_argument("Use finite palette adjustments, nonnegative multipliers and positive gamma");
    if (adjustment.hue_degrees == 0 && adjustment.saturation == 1 && adjustment.value == 1 &&
        adjustment.gamma == std::array<double, 3>{1, 1, 1} && adjustment.contrast == 1)
        return palette;
    Palette result;
    for (std::size_t i = 0; i < palette.size(); ++i) {
        auto hsv = rgb_hsv(palette[i]);
        hsv.h += std::remainder(adjustment.hue_degrees, 360.0);
        hsv.s = unit(hsv.s * adjustment.saturation);
        hsv.v = unit(hsv.v * adjustment.value);
        const auto rgb = hsv_rgb(hsv);
        std::array<double, 3> channels{rgb.r, rgb.g, rgb.b};
        for (unsigned channel = 0; channel < 3; ++channel) {
            const double curved = std::pow(channels[channel], 1 / adjustment.gamma[channel]);
            channels[channel] = unit((curved - .5) * adjustment.contrast + .5);
        }
        result[i] = {channels[0], channels[1], channels[2]};
    }
    return result;
}
Palette random_palette(std::uint64_t seed, unsigned stop_count) {
    if (stop_count < 3 || stop_count > 32)
        throw std::invalid_argument("Random palette stop count must be in [3,32]");
    Rng rng(seed);
    const double start = rng.unit() * 360;
    const double hue_span = uniform(rng, 100, 300);
    std::vector<ColorStop> stops;
    for (unsigned i = 0; i < stop_count; ++i) {
        const double position = double(i) / (stop_count - 1);
        const double brightness = (i % 2 == 0) ? uniform(rng, .25, .62) : uniform(rng, .78, 1);
        const auto color = hsv_rgb({start + position * hue_span + uniform(rng, -18, 18),
                                    uniform(rng, .45, .95), brightness});
        stops.push_back({int(std::lround(position * 255)), color});
    }
    stops.back().color = stops.front().color;
    return palette_from_stops(stops, GradientSpace::hsv_shortest);
}
std::vector<PalettePreset> palette_presets() {
    struct Definition {
        const char *name;
        std::array<unsigned, 5> colors;
    };
    const std::array<Definition, 7> definitions{{
        {"Ember", {0x180b22, 0x8e2143, 0xe36c34, 0xffdc88, 0x32243c}},
        {"Glacier", {0x071c36, 0x125c87, 0x43bdcb, 0xd7f6ee, 0x26377d}},
        {"Orchid", {0x211035, 0x783c96, 0xe09ac5, 0xffe7cf, 0x4855a2}},
        {"Forest", {0x0b211d, 0x286b4e, 0x9cad55, 0xf4e6a1, 0x374d34}},
        {"Copper", {0x1d1824, 0x754b42, 0xc28459, 0xf8d8a3, 0x376d70}},
        {"Spectrum", {0xd0465c, 0xf1bb5d, 0x52b894, 0x4c76d0, 0xb45cb3}},
        {"Monochrome", {0x000000, 0x404040, 0x808080, 0xc0c0c0, 0xffffff}},
    }};
    std::vector<PalettePreset> result;
    for (const auto &definition : definitions) {
        std::array<ColorStop, 5> stops;
        for (unsigned i = 0; i < stops.size(); ++i)
            stops[i] = {int(std::lround(double(i) * 255 / 4)), hex(definition.colors[i])};
        result.push_back({definition.name, palette_from_stops(stops)});
    }
    return result;
}

Palette import_gimp_palette(std::string_view source) {
    if (source.size() > 4 * 1024 * 1024)
        throw std::invalid_argument("Palette file is larger than 4 MiB");
    if (source.starts_with("\xef\xbb\xbf"))
        source.remove_prefix(3);
    std::istringstream stream{std::string(source)};
    stream.imbue(std::locale::classic());
    std::string line;
    if (!std::getline(stream, line) || (line != "GIMP Palette" && line != "GIMP Palette\r"))
        throw std::invalid_argument("Expected a GIMP Palette (.gpl) header");
    std::vector<RGB> colors;
    std::size_t line_number = 1;
    while (std::getline(stream, line)) {
        ++line_number;
        const auto start = line.find_first_not_of(" \t\r");
        if (start == std::string::npos || line[start] == '#')
            continue;
        const auto trimmed = std::string_view(line).substr(start);
        if (trimmed.starts_with("Name:") || trimmed.starts_with("Columns:"))
            continue;
        std::istringstream values{std::string(trimmed)};
        values.imbue(std::locale::classic());
        int red, green, blue;
        if (!(values >> red >> green >> blue) || red < 0 || red > 255 || green < 0 || green > 255 ||
            blue < 0 || blue > 255)
            throw std::invalid_argument("Invalid RGB color at palette line " + std::to_string(line_number));
        // Require whitespace between the final number and an optional color name.
        if (values.peek() != std::char_traits<char>::eof() && values.peek() != ' ' &&
            values.peek() != '\t' && values.peek() != '\r')
            throw std::invalid_argument("Invalid RGB color at palette line " + std::to_string(line_number));
        colors.push_back({red / 255.0, green / 255.0, blue / 255.0});
        if (colors.size() > 256)
            throw std::invalid_argument("A flame palette supports at most 256 imported colors");
    }
    if (colors.size() < 2)
        throw std::invalid_argument("A palette must contain at least two colors");
    std::vector<ColorStop> stops;
    for (std::size_t i = 0; i < colors.size(); ++i)
        stops.push_back({int(std::lround(double(i) * 255 / double(colors.size() - 1))), colors[i]});
    return palette_from_stops(stops);
}
std::string export_gimp_palette(const Palette &palette, std::string_view name) {
    validate_palette(palette);
    std::string safe_name(name.substr(0, 120));
    for (auto &c : safe_name)
        if (static_cast<unsigned char>(c) < 32 || c == 127)
            c = ' ';
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << "GIMP Palette\nName: " << safe_name << "\nColumns: 16\n#\n";
    for (std::size_t i = 0; i < palette.size(); ++i) {
        const auto color = palette[i];
        output << std::setw(3) << std::lround(color.r * 255) << ' ' << std::setw(3)
               << std::lround(color.g * 255) << ' ' << std::setw(3) << std::lround(color.b * 255)
               << "\tColor " << i << '\n';
    }
    return output.str();
}

bool fit_camera(Flame &flame, std::uint64_t seed, std::size_t samples, double coverage) {
    validate(flame);
    if (samples < 256 || samples > 1000000)
        throw std::invalid_argument("Camera fitting sample count must be in [256,1000000]");
    if (!(coverage >= .5 && coverage <= 1))
        throw std::invalid_argument("Camera fitting coverage must be in [0.5,1]");
    Rng rng(seed);
    const TransformChooser chooser(flame);
    Orbit orbit{{uniform(rng, -.5, .5), uniform(rng, -.5, .5)}, rng.unit()};
    std::optional<std::size_t> previous;
    unsigned warmup = 64;
    const double angle = std::remainder(flame.rotate, 360.0) * pi / 180;
    const double cs = std::cos(angle), sn = std::sin(angle);
    std::vector<double> xs, ys;
    xs.reserve(samples);
    ys.reserve(samples);
    for (std::size_t i = 0; i < samples * 3 + 128 && xs.size() < samples; ++i) {
        const auto index = chooser.pick(rng.unit(), previous);
        orbit = apply_transform(flame.transforms[index], orbit, rng);
        if (!finite_point(orbit.point)) {
            orbit = {{uniform(rng, -.5, .5), uniform(rng, -.5, .5)}, rng.unit()};
            previous.reset();
            warmup = 64;
            continue;
        }
        previous = index;
        if (warmup) {
            --warmup;
            continue;
        }
        if (flame.transforms[index].opacity == 0)
            continue;
        Orbit sample = orbit;
        if (flame.final_transform &&
            (is_apophysis(flame.source_version) || flame.final_transform->opacity == 1 ||
             (flame.final_transform->opacity > 0 && rng.unit() < flame.final_transform->opacity)))
            sample = apply_transform(*flame.final_transform, sample, rng);
        if (finite_point(sample.point)) {
            xs.push_back(cs * sample.point.x - sn * sample.point.y);
            ys.push_back(sn * sample.point.x + cs * sample.point.y);
        }
    }
    if (xs.size() < samples / 4)
        return false;
    std::sort(xs.begin(), xs.end());
    std::sort(ys.begin(), ys.end());
    const double tail = (1 - coverage) * .5;
    const auto low = static_cast<std::size_t>(std::floor(double(xs.size() - 1) * tail));
    const auto high = static_cast<std::size_t>(std::ceil(double(xs.size() - 1) * (1 - tail)));
    const double span_x = xs[high] - xs[low], span_y = ys[high] - ys[low];
    const double largest = std::max(span_x, span_y);
    if (largest < 1e-10 || !std::isfinite(largest))
        return false;
    const double pixels_per_unit = .88 * std::min(flame.width / std::max(span_x, largest * 1e-6),
                                                 flame.height / std::max(span_y, largest * 1e-6));
    const double scale = pixels_per_unit / std::exp2(flame.zoom);
    if (!(scale > 0 && std::isfinite(scale)))
        return false;
    const double center_x = std::midpoint(xs[high], xs[low]);
    const double center_y = std::midpoint(ys[high], ys[low]);
    flame.center = {cs * center_x + sn * center_y, -sn * center_x + cs * center_y};
    flame.scale = scale;
    return true;
}

std::vector<Flame> random_flames(const RandomOptions &options) {
    check_random(options);
    const auto choices = variation_choices(options.allowed_variations);
    const auto ordinary = ordinary_choices(choices);
    std::vector<Flame> result;
    result.reserve(options.batch_size);
    for (std::size_t sequence = 0; sequence < options.batch_size; ++sequence) {
        bool accepted = false;
        for (unsigned attempt = 0; attempt < 12 && !accepted; ++attempt) {
            const auto candidate_seed = stream_seed(options.seed, sequence * 16 + attempt);
            Rng rng(candidate_seed);
            Flame flame;
            flame.name = "Random " + std::to_string(options.seed) + " / " + std::to_string(sequence + 1);
            flame.source_version = "Apophysis Reborn 0.5.0";
            flame.width = options.width;
            flame.height = options.height;
            flame.palette_mode = PaletteMode::linear;
            if (options.randomize_palette)
                flame.palette = random_palette(stream_seed(candidate_seed, 91));
            const auto count = options.min_transforms +
                               choose(rng, options.max_transforms - options.min_transforms + 1);
            for (unsigned i = 0; i < count; ++i) {
                Transform transform;
                const double angle = uniform(rng, -pi, pi);
                const double first_scale = uniform(rng, options.min_contraction, options.max_contraction);
                const double second_scale = uniform(rng, options.min_contraction, options.max_contraction);
                const double orbit_angle = 2 * pi * i / count + uniform(rng, -.35, .35);
                const double offset = options.translation * uniform(rng, .3, .85);
                transform.pre = {first_scale * std::cos(angle), first_scale * std::sin(angle),
                                 -second_scale * std::sin(angle), second_scale * std::cos(angle),
                                 offset * std::cos(orbit_angle), offset * std::sin(orbit_angle)};
                const auto id = ordinary[choose(rng, ordinary.size())];
                transform.variations = {{id, 1}};
                if (id != Variation::linear && contains(ordinary, Variation::linear)) {
                    const double contribution = uniform(rng, .15, .55);
                    transform.variations = {{Variation::linear, 1 - contribution}, {id, contribution}};
                }
                if (contains(choices, Variation::pre_blur) && rng.unit() < .5)
                    transform.variations.push_back({Variation::pre_blur, uniform(rng, .005, .035)});
                if (id == Variation::julian) {
                    transform.parameters.julian_power = int(choose(rng, 5)) + 2;
                    transform.parameters.julian_dist = uniform(rng, .6, 1.2);
                }
                transform.weight = uniform(rng, .75, 1.25);
                transform.color = (i + uniform(rng, .1, .9)) / count;
                transform.color_speed = uniform(rng, .35, .65);
                flame.transforms.push_back(transform);
            }
            for (unsigned i = 1; i < options.rotational_symmetry; ++i) {
                Transform rotation;
                const double angle = 2 * pi * i / options.rotational_symmetry;
                rotation.pre = {std::cos(angle), std::sin(angle), -std::sin(angle), std::cos(angle), 0, 0};
                rotation.color_speed = 0;
                flame.transforms.push_back(rotation);
            }
            if (options.reflection) {
                Transform reflection;
                reflection.pre.a = -1;
                reflection.color_speed = 0;
                flame.transforms.push_back(reflection);
            }
            validate(flame);
            accepted = fit_camera(flame, stream_seed(candidate_seed, 92));
            if (accepted)
                result.push_back(std::move(flame));
        }
        if (!accepted)
            throw std::runtime_error("The selected generation constraints produced no finite, nondegenerate orbit. "
                                     "Try another seed or include linear, sinusoidal or bubble.");
    }
    return result;
}

Flame mutate_flame(const Flame &parent, const MutationOptions &options, std::size_t sequence) {
    validate(parent);
    check_mutation(options);
    const auto choices = options.variations ? variation_choices(options.allowed_variations)
                                            : std::vector<Variation>{};
    if (options.strength == 0)
        return parent;
    Flame result = parent;
    Rng rng(stream_seed(options.seed, sequence));
    for (auto &transform : result.transforms)
        mutate_transform(transform, options, choices, rng);
    if (options.include_final && result.final_transform)
        mutate_transform(*result.final_transform, options, choices, rng);
    if (options.palette) {
        PaletteAdjustment adjustment;
        adjustment.hue_degrees = 90 * options.strength * directed(rng, options.trend);
        adjustment.saturation = std::exp(.6 * options.strength * directed(rng, options.trend));
        adjustment.value = std::exp(.35 * options.strength * directed(rng, options.trend));
        result.palette = adjust_palette(parent.palette, adjustment);
    }
    validate(result);
    return result;
}
std::vector<Flame> mutation_candidates(const Flame &parent, const MutationOptions &options) {
    check_mutation(options);
    std::vector<Flame> result;
    result.reserve(options.count);
    for (std::size_t i = 0; i < options.count; ++i)
        result.push_back(mutate_flame(parent, options, i));
    return result;
}

} // namespace apo::creative
