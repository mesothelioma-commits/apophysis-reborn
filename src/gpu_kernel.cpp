#include "gpu_kernel.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string_view>

namespace apo::gpu {
namespace {
constexpr std::uint32_t no_final = UINT32_MAX;
std::uint32_t bits(double value) {
    return std::bit_cast<std::uint32_t>(static_cast<float>(value));
}
double number(std::uint32_t value) {
    return std::bit_cast<float>(value);
}

bool representable(double value) {
    const auto f = static_cast<float>(value);
    return std::isfinite(f) &&
           (value == 0 || (f != 0 && std::abs(f) >= std::numeric_limits<float>::min()));
}

std::string field_reason(double value, std::string_view name) {
    return representable(value) ? std::string{} : std::string(name) + " is outside normal FP32 range";
}

// The optional four-word tail on a variation record holds its constants.
// Precompute trigonometric and power coefficients once per packed transform.
using Constants = std::array<double, 4>;
std::optional<Constants> variation_constants(Variation id, const VariationParameters &p) {
    constexpr double pi = std::numbers::pi;
    switch (id) {
    case Variation::blob:
        return Constants{p.blob_low, p.blob_high - p.blob_low, p.blob_waves};
    case Variation::pdj:
        return Constants{p.pdj_a, p.pdj_b, p.pdj_c, p.pdj_d};
    case Variation::fan2:
        return Constants{pi * (p.fan2_x * p.fan2_x + 1e-10), p.fan2_y};
    case Variation::rings2:
        return Constants{p.rings2_val * p.rings2_val + 1e-10};
    case Variation::perspective: {
        const auto a = p.perspective_angle * (pi / 2);
        return Constants{std::sin(a), p.perspective_dist, std::cos(a),
                         double(p.perspective_angle == 0)};
    }
    case Variation::juliascope:
        return Constants{p.juliascope_power, p.juliascope_dist / (2.0 * p.juliascope_power),
                         double(p.juliascope_dist == 1)};
    case Variation::radial_blur: {
        const auto a = p.radial_blur_angle * (pi / 2);
        return Constants{std::sin(a), std::cos(a)};
    }
    case Variation::pie:
        return Constants{p.pie_slices, p.pie_rotation, p.pie_thickness};
    case Variation::ngon:
        return Constants{2 * pi / p.ngon_sides, p.ngon_power / 2, p.ngon_corners, p.ngon_circle};
    case Variation::curl:
        return Constants{p.curl_c1, p.curl_c2};
    case Variation::splits:
        return Constants{p.splits_x, p.splits_y};
    case Variation::rectangles:
        return Constants{p.rectangles_x, p.rectangles_y};
    default:
        return std::nullopt;
    }
}

std::string transform_reason(const Transform &t) {
    for (const auto &matrix : {t.pre, t.post})
        for (double value : {matrix.a, matrix.b, matrix.c, matrix.d, matrix.e, matrix.f})
            if (auto reason = field_reason(value, "Affine coefficient"); !reason.empty())
                return reason;
    for (double value : {t.color, t.color_speed, t.opacity, t.parameters.julian_dist})
        if (auto reason = field_reason(value, "Transform parameter"); !reason.empty())
            return reason;
    if (std::abs(double(t.parameters.julian_power)) > 16777216)
        return "JuliaN power exceeds exact FP32 integer range";
    for (const auto &term : t.variations) {
        if (term.id == Variation::plugin)
            return "Native plugin variations require the CPU renderer";
        if (auto reason = field_reason(term.weight, "Variation weight"); !reason.empty())
            return reason;
        if (term.weight) {
            if (const auto constants = variation_constants(term.id, t.parameters))
                for (const double v : *constants)
                    if (!representable(v))
                        return std::string(variation_name(term.id)) + " parameter is outside normal FP32 range";
            if (term.id == Variation::juliascope && std::abs(t.parameters.juliascope_power) > 16777216)
                return "JuliaScope branch count exceeds exact FP32 integer range";
            if (term.id == Variation::pie && std::abs(t.parameters.pie_slices) > 16777216)
                return "Pie slice count exceeds exact FP32 integer range";
        }
        if (!term.weight || !t.parameters.apophysis_math)
            continue;
        // The historical 1e-300 guards cannot be expressed by this FP32 kernel.
        // Zero-radius point guards are implemented explicitly in orbit.comp,
        // but an affine-coupled denominator must remain finite and nonzero.
        if (term.id == Variation::waves || term.id == Variation::rings || term.id == Variation::fan) {
            const auto square_ok = [](double x) { return x != 0 && representable(x * x); };
            if (!square_ok(t.pre.e) || (term.id == Variation::waves && !square_ok(t.pre.f)))
                return "Historical waves/rings/fan needs a denominator outside FP32 range";
        }
    }
    return {};
}

std::string cdf_reason(const std::vector<double> &row) {
    double previous = 0;
    float previous_float = 0;
    for (double value : row) {
        const float f = static_cast<float>(value);
        if (value > previous && value - previous < 0x1p-24)
            return "A positive transform probability is below the compute sampler's 24-bit resolution";
        if (value > previous && f <= previous_float)
            return "A positive transform probability collapses at FP32 precision";
        previous = value;
        previous_float = f;
    }
    return {};
}
} // namespace

std::uint32_t mix32(std::uint32_t value) {
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    return value ^ (value >> 16);
}
std::uint32_t portable_seed(std::uint64_t seed, std::uint64_t stream) {
    return mix32(static_cast<std::uint32_t>(seed) ^
                 mix32(static_cast<std::uint32_t>(seed >> 32) + 0x9e3779b9u) ^
                 mix32(static_cast<std::uint32_t>(stream) + 0x85ebca6bu) ^
                 mix32(static_cast<std::uint32_t>(stream >> 32) + 0xc2b2ae35u));
}
std::uint32_t portable_word(std::uint32_t &state) {
    state = state * 747796405u + 2891336453u;
    const auto word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}
double portable_unit(std::uint32_t &state) {
    return static_cast<double>(portable_word(state) >> 8u) * 0x1p-24;
}

std::string support_reason(const Flame &flame) {
    validate(flame);
    if (flame.transforms.size() > max_transforms)
        return "Vulkan supports at most 64 ordinary transforms in this checkpoint";
    for (double value : {flame.center.x, flame.center.y, flame.scale * std::exp2(flame.zoom)})
        if (auto reason = field_reason(value, "Camera value"); !reason.empty())
            return reason;
    for (const auto &color : flame.palette)
        for (double value : {color.r, color.g, color.b})
            if (auto reason = field_reason(value, "Palette channel"); !reason.empty())
                return reason;
    for (const auto &t : flame.transforms) {
        if (auto reason = transform_reason(t); !reason.empty())
            return reason;
        const auto opacity =
            is_apophysis(flame.source_version) ? t.opacity : std::pow(t.opacity, std::log2(10.0));
        if (auto reason = field_reason(opacity, "Effective opacity"); !reason.empty())
            return reason;
    }
    if (flame.final_transform)
        if (auto reason = transform_reason(*flame.final_transform); !reason.empty())
            return reason;
    TransformChooser chooser(flame);
    if (auto reason = cdf_reason(chooser.initial()); !reason.empty())
        return reason;
    for (const auto &row : chooser.rows())
        if (auto reason = cdf_reason(row); !reason.empty())
            return reason;
    return {};
}

PackedFlame pack_flame(const Flame &flame, const Geometry &geometry, bool hit_stream) {
    if (auto reason = support_reason(flame); !reason.empty())
        throw std::invalid_argument(reason);
    if (!geometry.width || !geometry.height || !(geometry.ppu > 0) ||
        (!hit_stream && std::uint64_t(geometry.width) * geometry.height > UINT32_MAX / 8u))
        throw std::invalid_argument("Vulkan histogram addressing exceeds its 32-bit word range");
    for (double value : {geometry.ppu, geometry.cosine, geometry.sine})
        if (!representable(value))
            throw std::invalid_argument("Vulkan camera projection is outside FP32 range");
    if (flame.width > 16777216 || flame.height > 16777216 || geometry.width > 16777216 ||
        geometry.height > 16777216 || geometry.padding > 16777216 || flame.oversample > 16777216)
        throw std::invalid_argument("Vulkan camera dimensions exceed exact FP32 integer range");
    PackedFlame packed;
    auto &data = packed.words;
    const auto n = static_cast<std::uint32_t>(flame.transforms.size());
    const auto count = n + (flame.final_transform ? 1 : 0);
    data.resize(header_words + count * transform_words);
    data[0] = n;
    data[1] = flame.final_transform ? n : no_final;
    data[2] = header_words;
    data[5] = is_apophysis(flame.source_version);
    data[6] = flame.palette_mode == PaletteMode::linear;
    data[7] = geometry.width;
    data[8] = geometry.height;
    data[9] = geometry.padding;
    data[10] = static_cast<std::uint32_t>(flame.width);
    data[11] = static_cast<std::uint32_t>(flame.height);
    data[12] = static_cast<std::uint32_t>(flame.oversample);
    data[13] = bits(flame.center.x);
    data[14] = bits(flame.center.y);
    data[15] = bits(geometry.ppu);
    data[16] = bits(geometry.cosine);
    data[17] = bits(geometry.sine);
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto &t = i < n ? flame.transforms[i] : *flame.final_transform;
        const auto base = header_words + i * transform_words;
        const std::array values{t.pre.a,   t.pre.b,
                                t.pre.c,   t.pre.d,
                                t.pre.e,   t.pre.f,
                                t.post.a,  t.post.b,
                                t.post.c,  t.post.d,
                                t.post.e,  t.post.f,
                                t.color,   t.color_speed,
                                t.opacity, data[5] ? t.opacity : std::pow(t.opacity, std::log2(10.0))};
        for (std::uint32_t j = 0; j < values.size(); ++j)
            data[base + j] = bits(values[j]);
        data[base + 16] = static_cast<std::uint32_t>(t.parameters.julian_power);
        data[base + 17] = bits(t.parameters.julian_dist);
        data[base + 18] =
            (t.parameters.apophysis_math ? 1u : 0u) | (t.parameters.separate_blur_angle ? 2u : 0u);
        data[base + 19] = static_cast<std::uint32_t>(data.size());
        // Leave compact two-word records in place for models without
        // parameterized additions. Mixed transforms use a uniform stride.
        const bool extended = std::any_of(t.variations.begin(), t.variations.end(), [&](const Term &term) {
            return term.weight != 0 && variation_constants(term.id, t.parameters).has_value();
        });
        data[base + 22] = extended ? 6 : 2;
        for (const auto &term : t.variations) {
            if (!term.weight)
                continue;
            if (term.id == Variation::pre_blur) {
                data[base + 21] = bits(term.weight);
                continue;
            }
            data.push_back(static_cast<std::uint32_t>(term.id));
            data.push_back(bits(term.weight));
            if (extended) {
                const auto constants = variation_constants(term.id, t.parameters).value_or(Constants{});
                for (double v : constants)
                    data.push_back(bits(v));
            }
            ++data[base + 20];
        }
    }
    data[3] = static_cast<std::uint32_t>(data.size());
    const TransformChooser chooser(flame);
    const auto append_row = [&](const std::vector<double> &row) {
        for (double value : row)
            data.push_back(bits(value));
    };
    append_row(chooser.initial());
    for (const auto &row : chooser.rows())
        append_row(row.empty() ? chooser.initial() : row);
    data[4] = static_cast<std::uint32_t>(data.size());
    for (const auto &color : flame.palette) {
        data.push_back(bits(color.r));
        data.push_back(bits(color.g));
        data.push_back(bits(color.b));
    }
    data[18] = static_cast<std::uint32_t>(data.size());
    return packed;
}

ProbeRecord make_probe(std::uint32_t transform, Orbit input, std::uint64_t seed, std::uint64_t stream,
                       std::uint32_t repetitions) {
    if (!repetitions || repetitions > 1024)
        throw std::invalid_argument("A GPU transform probe needs 1–1024 repetitions");
    for (double value : {input.point.x, input.point.y, input.color})
        if (!representable(value))
            throw std::invalid_argument("GPU probe input is outside normal FP32 range");
    return {transform,
            bits(input.point.x),
            bits(input.point.y),
            bits(input.color),
            static_cast<std::uint32_t>(seed),
            static_cast<std::uint32_t>(seed >> 32),
            static_cast<std::uint32_t>(stream),
            static_cast<std::uint32_t>(stream >> 32),
            0,
            0,
            0,
            0,
            repetitions,
            0,
            0,
            0};
}
Orbit probe_orbit(const ProbeRecord &record) {
    return {{number(record[8]), number(record[9])}, number(record[10])};
}
double probe_next_unit(const ProbeRecord &record) {
    return number(record[11]);
}
} // namespace apo::gpu
