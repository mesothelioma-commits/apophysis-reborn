#include "color_curves.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace apo {
namespace {
double coordinate(const ColorCurve& curve, double t, bool x) {
    // Keep the original Core/Bezier.pas evaluation order for the legacy oracle.
    const double s = 1 - t, s2 = s * s, s3 = s * s * s;
    const double t2 = t * t, t3 = t * t * t;
    const auto value = [&](unsigned i) { return x ? curve[i].x : curve[i].y; };
    const double numerator = curve[0].weight * s3 * value(0) +
        curve[1].weight * s2 * 3 * t * value(1) +
        curve[2].weight * s * 3 * t2 * value(2) + curve[3].weight * t3 * value(3);
    const double denominator = curve[0].weight * s3 + curve[1].weight * s2 * 3 * t +
        curve[2].weight * s * 3 * t2 + curve[3].weight * t3;
    return numerator / denominator;
}
bool diagonal(const ColorCurve& curve) {
    return std::all_of(curve.begin(), curve.end(), [](const auto& point) { return point.x == point.y; });
}
} // namespace

void validate_color_curves(const ColorCurves& curves) {
    if (curves.mode != ColorCurveMode::legacy_7x && curves.mode != ColorCurveMode::continuous)
        throw std::invalid_argument("Unsupported color curve mode");
    for (const auto& curve : curves.channels) {
        for (const auto& point : curve) {
            if (!(point.x >= 0 && point.x <= 1 && point.y >= 0 && point.y <= 1) ||
                !std::isfinite(point.weight) || point.weight < 0 || point.weight > 1000000)
                throw std::invalid_argument("Curve coordinates must be in [0,1] and weights in [0,1000000]");
        }
        if (curve.front().weight < std::numeric_limits<double>::min() ||
            curve.back().weight < std::numeric_limits<double>::min())
            throw std::invalid_argument("Curve endpoint weights must be positive normal numbers");
        if (curves.mode == ColorCurveMode::continuous) {
            if (curve.front().x != 0 || curve.back().x != 1)
                throw std::invalid_argument("Continuous curve X endpoints must be 0 and 1");
            for (unsigned i = 1; i < curve.size(); ++i)
                if (curve[i - 1].x > curve[i].x)
                    throw std::invalid_argument("Continuous curve X coordinates must be ordered");
        }
    }
}

bool color_curves_are_identity(const ColorCurves& curves) {
    for (const auto& curve : curves.channels) {
        if (curves.mode == ColorCurveMode::continuous) {
            if (!diagonal(curve)) return false;
        } else {
            const auto original = identity_color_curve();
            for (unsigned i = 0; i < curve.size(); ++i)
                if (curve[i].x != original[i].x || curve[i].y != original[i].y) return false;
        }
    }
    return true;
}

double evaluate_color_curve(const ColorCurve& curve, double input, ColorCurveMode mode) {
    if (!(input >= 0 && input <= 1))
        throw std::invalid_argument("Curve input must be in [0,1]");
    if (mode == ColorCurveMode::legacy_7x) return coordinate(curve, input, false);
    if (mode != ColorCurveMode::continuous) throw std::invalid_argument("Unsupported color curve mode");
    if (diagonal(curve)) return input;
    if (input == 0) return curve.front().y;
    if (input == 1) return curve.back().y;
    // Positive endpoint weights and ordered X give a unique monotone solution.
    // Bisection also handles repeated endpoint/control X values and zero inner weights.
    double low = 0, high = 1;
    for (unsigned i = 0; i < 52; ++i) {
        const double t = (low + high) / 2;
        if (coordinate(curve, t, true) < input) low = t;
        else high = t;
    }
    return std::clamp(coordinate(curve, (low + high) / 2, false), 0.0, 1.0);
}

ColorCurves interpolate_color_curves(const ColorCurves& from, const ColorCurves& to, double amount) {
    if (!(amount >= 0 && amount <= 1)) throw std::invalid_argument("Curve interpolation amount must be in [0,1]");
    validate_color_curves(from);
    validate_color_curves(to);
    if (amount == 0) return from;
    if (amount == 1) return to;
    if (from.mode != to.mode)
        throw std::invalid_argument("Color curve modes differ; convert both keyframes to the same mode");
    auto result = from;
    for (unsigned c = 0; c < 4; ++c) {
        for (unsigned i = 0; i < 4; ++i) {
            auto& point = result.channels[c][i];
            const auto& a = from.channels[c][i];
            const auto& b = to.channels[c][i];
            point = {std::lerp(a.x, b.x, amount), std::lerp(a.y, b.y, amount),
                     std::lerp(a.weight, b.weight, amount)};
        }
    }
    validate_color_curves(result);
    return result;
}

std::size_t ColorCurveEvaluator::storage_bytes(const ColorCurves& curves) {
    return color_curves_are_identity(curves) ? 0 : 3 * sizeof(double) *
        (curves.mode == ColorCurveMode::legacy_7x ? 257 : continuous_intervals + 1);
}

ColorCurveEvaluator::ColorCurveEvaluator(const ColorCurves& curves) : mode_(curves.mode) {
    validate_color_curves(curves);
    if (color_curves_are_identity(curves)) return;
    if (mode_ == ColorCurveMode::legacy_7x) {
        tables_.resize(3 * 257);
        for (unsigned i = 0; i <= 256; ++i) {
            const double master = std::clamp(std::nearbyint(
                evaluate_color_curve(curves.channels[0], double(i) / 256, mode_) * 256), 0.0, 256.0);
            for (unsigned c = 0; c < 3; ++c)
                tables_[c * 257 + i] = std::nearbyint(
                    evaluate_color_curve(curves.channels[c + 1], master / 256, mode_) * 256);
        }
    } else {
        const auto count = continuous_intervals + 1;
        tables_.resize(3 * count);
        for (std::size_t i = 0; i < count; ++i) {
            const double master = evaluate_color_curve(curves.channels[0], double(i) / continuous_intervals, mode_);
            for (unsigned c = 0; c < 3; ++c)
                tables_[c * count + i] = evaluate_color_curve(curves.channels[c + 1], master, mode_);
        }
    }
}

double ColorCurveEvaluator::map_legacy(unsigned rgb_channel, double rounded_foreground) const {
    if (rgb_channel > 2) throw std::invalid_argument("RGB curve channel must be in [0,2]");
    if (std::isnan(rounded_foreground)) throw std::invalid_argument("Curve foreground must not be NaN");
    if (!active() || rounded_foreground < 0 || rounded_foreground > 256) return rounded_foreground;
    if (mode_ != ColorCurveMode::legacy_7x)
        return std::nearbyint(map(rgb_channel, rounded_foreground / 256) * 256);
    return tables_[rgb_channel * 257 + std::size_t(rounded_foreground)];
}

double ColorCurveEvaluator::map(unsigned rgb_channel, double foreground) const {
    if (rgb_channel > 2) throw std::invalid_argument("RGB curve channel must be in [0,2]");
    if (std::isnan(foreground)) throw std::invalid_argument("Curve foreground must not be NaN");
    if (!active()) return foreground;
    if (mode_ == ColorCurveMode::legacy_7x)
        return map_legacy(rgb_channel, std::nearbyint(foreground * 256)) / 256;
    const double index = std::clamp(foreground, 0.0, 1.0) * continuous_intervals;
    const auto left = std::min(std::size_t(index), continuous_intervals - 1);
    const auto offset = rgb_channel * (continuous_intervals + 1);
    return std::lerp(tables_[offset + left], tables_[offset + left + 1], index - double(left));
}

} // namespace apo
