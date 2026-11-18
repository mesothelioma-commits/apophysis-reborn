#include "density_estimation.hpp"
#include "render_internal.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace apo::detail {
namespace {
void check_stop(std::stop_token stop) { if (stop.stop_requested()) throw RenderCancelled(); }
constexpr double quantum = (1u << 24) - 1;
constexpr unsigned threshold = 100;
std::uint64_t triangle_offset(unsigned y) { return std::uint64_t(y) * (y + 1ull) / 2; }
}
DensityPlan density_plan(const Flame& f) {
    if (!f.density_estimator) return {};
    const auto& de = *f.density_estimator;
    const auto os = unsigned(f.oversample);
    DensityPlan p;
    p.max_width = de.radius * os + 1;
    p.min_width = de.minimum * os + 1;
    p.curve = de.curve;
    const double count = std::ceil(std::pow(p.max_width / p.min_width, 1 / p.curve));
    if (!(count >= 1 && count <= 1e7))
        throw std::invalid_argument("Density estimator requires too many filter levels");
    const double margin = std::ceil(de.radius) * os + os - 1;
    if (!(margin <= std::numeric_limits<int>::max() / 2))
        throw std::invalid_argument("Density estimator exceeds the histogram coordinate range");
    p.camera_margin = unsigned(margin);
    if (count == 1) return p; // In FLAM3 a zero maximum filter index uses the unfiltered log bucket.
    const double levels = count > threshold ? std::ceil(threshold + std::pow(count - threshold, p.curve)) + 1 : count;
    if (!(levels < std::numeric_limits<int>::max()))
        throw std::invalid_argument("Density filter level count exceeds the addressable range");
    p.levels = unsigned(levels);
    p.count_limit = count > threshold ? std::floor(std::pow(levels - threshold, 1 / p.curve)) + threshold : levels;
    p.radius = unsigned(std::ceil(p.max_width)) - 1;
    p.count_radius = os / 2;
    p.triangle = triangle_offset(p.radius + 1);
    if (p.triangle > (UINT64_MAX / p.levels - sizeof(unsigned)) / sizeof(double))
        throw std::invalid_argument("Density filter tables exceed the addressable range");
    p.table_bytes = p.levels * (p.triangle * sizeof(double) + sizeof(unsigned));
    return p;
}
DensityKernels::DensityKernels(const DensityPlan& p, std::stop_token stop) {
    check_stop(stop);
    if (!p.levels) return;
    weights.resize(std::size_t(p.levels) * p.triangle);
    radii.resize(p.levels);
    for (unsigned level = 0; level < p.levels; ++level) {
        check_stop(stop);
        const double count = level < threshold ? level + 1.0 :
            threshold + 1.0 + std::pow(level - threshold, 1 / p.curve);
        const double width = std::max(p.min_width, p.max_width / std::pow(count, p.curve));
        radii[level] = unsigned(std::ceil(width)) - 1;
        last = level;
        const auto offset = std::size_t(level) * p.triangle;
        // A narrower integer width includes its boundary in normalization even
        // though the historical scatter traverses only ceil(width)-1.
        const unsigned end = std::min(p.radius, unsigned(std::floor(width)));
        double norm = 0;
        for (unsigned y = 0; y <= end; ++y) {
            check_stop(stop);
            for (unsigned x = 0; x <= y; ++x) {
                const double d2 = double(y)*y + double(x)*x;
                if (d2 > width * width) break;
                const double w = std::exp(-4.5 * d2 / (width * width));
                weights[offset + triangle_offset(y) + x] = w;
                norm += w * (y == 0 ? 1 : (x == 0 || x == y ? 4 : 8));
            }
        }
        for (unsigned y = 0; y <= radii[level]; ++y) {
            check_stop(stop);
            for (unsigned x = 0; x <= y; ++x)
                weights[offset + triangle_offset(y) + x] /= norm;
        }
        if (width <= p.min_width) break;
    }
}

std::vector<LightBin> density_strip(const Flame& f, const RenderGeometry& geometry,
        std::span<const Bin> bins, unsigned first, unsigned out_first, unsigned out_rows,
        std::uint64_t samples, std::stop_token stop) {
    check_stop(stop);
    const auto& p = geometry.density;
    const std::int64_t stride = geometry.gpu.width, height = geometry.gpu.height;
    const std::int64_t out_end = std::int64_t(out_first) + out_rows;
    const std::int64_t os = f.oversample;
    if (!f.density_estimator || !stride || !samples || !out_rows || out_end > height || bins.size() % stride)
        throw std::invalid_argument("Invalid density reconstruction geometry");
    const std::int64_t in_end = first + std::int64_t(bins.size() / stride);
    const std::int64_t border = p.levels ? os - 1 : 0;
    const auto y0 = std::max<std::int64_t>(border, std::int64_t(out_first) - p.radius);
    // The source renderer's last swath includes one more row at the bottom.
    // Never read a center or density neighbor past the actual histogram.
    const auto source_end = p.levels ? std::min(height - border + 1, height - p.count_radius) : height;
    const auto y1 = std::min<std::int64_t>(source_end, out_end + p.radius);
    if (y0 - p.count_radius < first || y1 + p.count_radius > in_end)
        throw std::invalid_argument("Density reconstruction strip lacks the required histogram overlap");
    if (p.levels && !geometry.density_kernels)
        throw std::invalid_argument("Density filter tables were not prepared");
    std::vector<LightBin> output(std::size_t(stride) * out_rows);
    const double log_gain = 2 * (std::log(double(os)) + std::log(geometry.gpu.ppu)) - std::log(double(samples));
    const double even_scale = (os & 1) ? 1.0 : std::pow(double(os) / (os + 1.0), 2);
    // Larger oversampling windows share all but one row/column with their
    // neighbors. A rolling integer sum avoids rescanning each square per hit.
    std::vector<std::uint64_t> columns(p.column_bytes(geometry.gpu.width) / sizeof(std::uint64_t));
    if (!columns.empty() && y0 < y1) {
        for (auto row = y0 - p.count_radius; row <= y0 + p.count_radius; ++row) {
            check_stop(stop);
            const auto start = std::size_t(row - first) * stride;
            for (std::size_t x = 0; x < columns.size(); ++x) columns[x] += bins[start + x][3];
        }
    }
    for (auto y = y0; y < y1; ++y) {
        check_stop(stop);
        std::uint64_t window = 0;
        if (!columns.empty()) {
            if (y > y0) {
                const auto outgoing = std::size_t(y - first - p.count_radius - 1) * stride;
                const auto incoming = std::size_t(y - first + p.count_radius) * stride;
                for (std::size_t x = 0; x < columns.size(); ++x)
                    columns[x] = columns[x] - bins[outgoing + x][3] + bins[incoming + x][3];
            }
            for (auto x = border - p.count_radius; x < border + p.count_radius; ++x) window += columns[x];
        }
        for (auto x = border; x < stride - border; ++x) {
            if ((x & 255) == 0) check_stop(stop);
            std::uint64_t rolling_count = 0;
            if (!columns.empty()) {
                rolling_count = window + columns[x + p.count_radius];
                window = rolling_count - columns[x - p.count_radius];
            }
            const auto& src = bins[std::size_t(y - first) * stride + x];
            if (!src[3]) continue;
            const double mass = double(src[3]) / quantum;
            const double exponent = std::log(mass) + log_gain;
            const double light = (268.0 / 256) * (std::max(exponent, 0.0) + std::log1p(std::exp(-std::abs(exponent))));
            const LightBin rgba{light * (double(src[0]) / src[3]), light * (double(src[1]) / src[3]),
                                light * (double(src[2]) / src[3]), light};
            if (!p.levels) {
                output[std::size_t(y-out_first) * stride+x] = rgba;
                continue;
            }
            // With Reborn's opaque RGB palettes the fourth channel also carries
            // FLAM3's visibility-weighted hit count. Sum integer mass first so a
            // whole number of hits always selects the same level.
            std::uint64_t count_sum = src[3];
            if (!columns.empty()) {
                count_sum = rolling_count;
            } else if (p.count_radius) {
                count_sum = 0;
                for (auto row = y - p.count_radius; row <= y + p.count_radius; ++row) {
                    for (auto col = x - p.count_radius; col <= x + p.count_radius; ++col)
                        count_sum += bins[std::size_t(row - first) * stride + col][3];
                }
            }
            const auto& k = *geometry.density_kernels;
            const double hits = (double(count_sum) / quantum) * even_scale;
            unsigned level = k.last;
            if (hits <= p.count_limit) {
                const double index = hits <= threshold ? std::ceil(hits)-1 : threshold + std::floor(std::pow(hits-threshold,p.curve));
                level = unsigned(std::clamp(index, 0.0, double(k.last)));
            }
            const auto radius = std::int64_t(k.radii[level]);
            const auto base = std::size_t(level) * p.triangle;
            for (auto dest_y = std::max<std::int64_t>(out_first, y-radius);
                 dest_y <= std::min(out_end-1, y+radius); ++dest_y) {
                if (radius > 32) check_stop(stop);
                const unsigned dy = unsigned(std::abs(dest_y-y));
                for (auto dest_x = std::max<std::int64_t>(0, x-radius);
                     dest_x <= std::min(stride-1, x+radius); ++dest_x) {
                    const auto dx = unsigned(std::abs(dest_x-x));
                    const double w = k.weights[base + triangle_offset(std::max(dy,dx)) + std::min(dy,dx)];
                    if (!w) continue;
                    auto& dest = output[std::size_t(dest_y-out_first)*stride+dest_x];
                    for (unsigned c=0; c<4; ++c) dest[c] += w * rgba[c];
                }
            }
        }
    }
    return output;
}
}
