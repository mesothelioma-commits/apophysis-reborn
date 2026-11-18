#pragma once

#include <array>
#include <cstddef>
#include <vector>

namespace apo {

// Original 7X evaluates rational Bezier Y(t), not Y(X). Its identity bypass and
// 257-entry, rounded master -> RGB lookup are retained in legacy_7x mode.
// Continuous curves invert a monotone X coordinate and use interpolated tables.
enum class ColorCurveMode { legacy_7x, continuous };
struct WeightedCurvePoint {
    double x = 0, y = 0, weight = 1;
    bool operator==(const WeightedCurvePoint&) const = default;
};
using ColorCurve = std::array<WeightedCurvePoint, 4>;
constexpr ColorCurve identity_color_curve() {
    return {{{0, 0, 1}, {0, 0, 1}, {1, 1, 1}, {1, 1, 1}}};
}
struct ColorCurves {
    // Master, red, green, blue, matching the original 48-number XML attribute.
    std::array<ColorCurve, 4> channels{identity_color_curve(), identity_color_curve(),
                                       identity_color_curve(), identity_color_curve()};
    ColorCurveMode mode = ColorCurveMode::legacy_7x;
    bool operator==(const ColorCurves&) const = default;
};

void validate_color_curves(const ColorCurves& curves);
bool color_curves_are_identity(const ColorCurves& curves);
// Raw channel evaluator: legacy identity bypass is a property of all four
// channels together and is implemented by ColorCurveEvaluator, not here.
double evaluate_color_curve(const ColorCurve& curve, double input, ColorCurveMode mode);
// Exact endpoint copies. Interior interpolation requires equal modes; all
// coordinates and weights interpolate, and the resulting curves are validated.
ColorCurves interpolate_color_curves(const ColorCurves& from, const ColorCurves& to, double amount);

class ColorCurveEvaluator {
public:
    explicit ColorCurveEvaluator(const ColorCurves& curves);
    bool active() const { return !tables_.empty(); }
    // Apply to a normalized, premultiplied foreground before background blending
    // or straight-alpha division. Legacy mode quantizes at the original 1/256.
    double map(unsigned rgb_channel, double foreground) const;
    // The original integer-domain operation. Out-of-range values pass through.
    double map_legacy(unsigned rgb_channel, double rounded_foreground) const;
    static std::size_t storage_bytes(const ColorCurves& curves);
    static constexpr std::size_t continuous_intervals = 4096;
private:
    ColorCurveMode mode_;
    std::vector<double> tables_;
};

} // namespace apo
