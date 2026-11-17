#include "render.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <exception>
#include <limits>
#include <mutex>
#include <numbers>
#include <stdexcept>
#include <thread>

namespace apo {
namespace {
constexpr std::uint64_t quantum = (1u << 24) - 1;
constexpr std::uint64_t max_samples = UINT64_MAX / quantum;
constexpr std::uint64_t batch_size = 8192;
constexpr int burn_in = 32;
using Bin = std::array<std::uint64_t, 4>; // R, G, B, opacity
struct Hit { std::size_t bin; std::array<std::uint32_t, 4> value; };

std::uint64_t checked_product(std::uint64_t a, std::uint64_t b) {
    if (b && a > UINT64_MAX / b) throw std::invalid_argument("Requested image storage overflows");
    return a * b;
}

RGB color_at(const Flame& flame, double color, bool apophysis) {
    if (apophysis && flame.palette_mode == PaletteMode::step) {
        const auto i = static_cast<unsigned>(std::nearbyint(std::clamp(color, 0.0, 1.0) * 255));
        return flame.palette[i];
    }
    const double index = std::clamp(color * 256.0, 0.0, 255.0);
    const auto i = static_cast<unsigned>(index);
    const RGB a = flame.palette[i];
    if (flame.palette_mode == PaletteMode::step || i == 255) return a;
    const RGB b = flame.palette[i + 1];
    const double t = index - i;
    return {std::lerp(a.r, b.r, t), std::lerp(a.g, b.g, t), std::lerp(a.b, b.b, t)};
}
std::uint32_t fixed(double value) {
    return static_cast<std::uint32_t>(std::llround(std::clamp(value, 0.0, 1.0) * quantum));
}
constexpr unsigned apophysis_white = 200; // Integer histogram of the historical renderer.
std::array<std::uint32_t,4> bucket_value(RGB color, double opacity, bool apophysis) {
    if (apophysis) {
        const auto channel = [](double v) {
            return static_cast<unsigned>(std::nearbyint(v * 255)) * apophysis_white / 256;
        };
        return {channel(color.r), channel(color.g), channel(color.b), apophysis_white};
    }
    return {fixed(opacity*color.r), fixed(opacity*color.g), fixed(opacity*color.b), fixed(opacity)};
}

Orbit start(Rng& rng) {
    return {{2 * rng.unit() - 1, 2 * rng.unit() - 1}, rng.unit()};
}
bool finite(Orbit s) {
    return std::isfinite(s.point.x) && std::isfinite(s.point.y) && std::isfinite(s.color);
}

double log_one_plus_exp(double x) { // Stable log-density for extreme camera scales.
    return std::max(x, 0.0) + std::log1p(std::exp(-std::abs(x)));
}

// Keep the historical even/odd alignment at subpixel resolution. When the
// kernel is narrower than a pixel, its first tap starts inside that pixel.
struct Footprint {
    unsigned taps, padding, inset;
    double coordinate_step;
};

Footprint footprint(const Flame& f, bool apophysis) {
    const auto os = static_cast<unsigned>(f.oversample);
    if (!f.filter_radius) return {os, 0, 0, 0}; // M2 box reconstruction
    const double support = apophysis ? 1.8 : 1.5;
    double diameter = 2 * support * os * *f.filter_radius;
    if (!std::isfinite(diameter) || diameter > std::numeric_limits<int>::max() - 3)
        throw std::invalid_argument("Gaussian filter is too large for the histogram");
    if (apophysis) diameter = std::floor(diameter);
    unsigned taps = static_cast<unsigned>(diameter) + 1;
    taps += (taps ^ os) & 1u;
    const double step = diameter > 0 ? support / diameter : 1.0 / taps;
    return {taps, taps > os ? (taps-os)/2 : 0, taps < os ? (os-taps)/2 : 0, step};
}

std::vector<double> gaussian_weights(const Footprint& k) {
    std::vector<double> weights(k.taps);
    double total = 0;
    const double nearest2 = (k.taps % 2 == 0) ? 1 : 0;
    for (unsigned i = 0; i < k.taps; ++i) {
        const double offset = 2.0 * i + 1 - k.taps;
        // Subtract the central exponent before normalization, so a narrow
        // filter with even parity cannot underflow every coefficient to zero.
        const double d2 = offset * offset - nearest2;
        weights[i] = d2 == 0 ? 1 : std::exp(-2 * d2 * k.coordinate_step * k.coordinate_step);
        total += weights[i];
    }
    for (auto& v : weights) v /= total;
    return weights;
}

double gamma_alpha(double light, double exponent, double threshold) {
    if (light == 0) return 0;
    const double curved = std::pow(light, exponent);
    if (light >= threshold) return curved;
    const double fraction = light / threshold;
    return std::lerp(fraction * std::pow(threshold, exponent), curved, fraction);
}

Image develop(const Flame& f, const std::vector<Bin>& bins, double ppu, std::uint64_t samples,
              const Footprint& kernel, std::size_t stride, bool apophysis) {
    const auto os = static_cast<std::size_t>(f.oversample);
    const auto width = static_cast<std::size_t>(f.width);
    const auto filter = f.filter_radius ? gaussian_weights(kernel) : std::vector<double>{};
    const double box_weight = 1.0 / double(os * os);
    const double log_gain = 2 * (std::log(double(os)) + std::log(ppu)) - std::log(double(samples));
    const double exposure = (268.0 / 256) * (apophysis ? 2.3 / std::log(10.0) : 1.0);
    const double gamma = 1 / f.gamma;
    const double vibrancy = apophysis ? std::nearbyint(f.vibrancy*256)/256 : f.vibrancy;
    const double mass_unit = apophysis ? apophysis_white : quantum;
    const std::array bg{f.background.r, f.background.g, f.background.b};
    Image image{f.width, f.height, std::vector<std::uint8_t>(width * f.height * 3)};
    for (int y = 0; y < f.height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            std::array<double, 4> sum{};
            for (std::size_t dy = 0; dy < kernel.taps; ++dy) {
                const auto row = (std::size_t(y) * os + kernel.inset + dy) * stride + x * os + kernel.inset;
                for (std::size_t dx = 0; dx < kernel.taps; ++dx) {
                    const Bin& bin = bins[row + dx];
                    if (!bin[3]) continue;
                    const double mass = double(bin[3]) / mass_unit;
                    const double weight = filter.empty() ? box_weight : filter[dy] * filter[dx];
                    const double light = (exposure * weight) * log_one_plus_exp(std::log(mass) + log_gain);
                    sum[3] += light;
                    for (unsigned c = 0; c < 3; ++c) sum[c] += light * (double(bin[c]) / double(bin[3]));
                }
            }
            const double alpha = gamma_alpha(sum[3] * f.brightness, gamma, f.gamma_threshold);
            for (unsigned c = 0; c < 3; ++c) {
                const double average = sum[3] > 0 ? sum[c] / sum[3] : 0;
                double foreground = 0;
                if (vibrancy > 0 && average > 0) foreground += vibrancy * alpha * average;
                if (vibrancy < 1) foreground += (1 - vibrancy) * std::pow(sum[c] * f.brightness, gamma);
                auto& output = image.rgb[(std::size_t(y) * width + x) * 3 + c];
                if (apophysis) {
                    const auto bg8 = static_cast<unsigned>(std::floor(bg[c] * 255));
                    const auto alpha8 = static_cast<unsigned>(std::clamp(std::nearbyint(alpha*256),0.0,255.0));
                    const double channel = std::nearbyint(foreground*256) + ((255-alpha8)*bg8 / 256);
                    output = sum[3] * f.brightness == 0 ? static_cast<std::uint8_t>(bg8) :
                        static_cast<std::uint8_t>(std::clamp(channel,0.0,255.0));
                } else {
                    const auto channel = foreground + (1 - std::min(1.0, alpha)) * bg[c];
                    output = static_cast<std::uint8_t>(std::lround(255 * std::clamp(channel, 0.0, 1.0)));
                }
            }
        }
    }
    return image;
}
}

std::uint64_t default_samples(const Flame& f) {
    const double n = std::ceil(f.quality * double(f.width) * f.height * std::exp2(2 * f.zoom));
    if (!std::isfinite(n) || n > double(max_samples))
        throw std::invalid_argument("Derived sample count exceeds accumulator capacity; specify --samples");
    return static_cast<std::uint64_t>(std::max(1.0, n));
}

RenderResult render(const Flame& f, const RenderOptions& opt) {
    validate(f);
    if (opt.samples == 0 || opt.samples > max_samples)
        throw std::invalid_argument("Sample count exceeds the supported positive fixed-point accumulator range");
    const double ppu = f.scale * std::exp2(f.zoom);
    if (!(ppu > 0 && std::isfinite(ppu))) throw std::invalid_argument("Invalid effective camera scale");
    const double angle = std::remainder(f.rotate, 360.0) * (std::numbers::pi / 180.0);
    const double cs = std::cos(angle), sn = std::sin(angle);
    const auto os = static_cast<std::uint64_t>(f.oversample);
    const bool apophysis = is_apophysis(f.source_version);
    const Footprint kernel = footprint(f, apophysis);
    const auto width = checked_product(f.width, os) + 2ull * kernel.padding;
    const auto height = checked_product(f.height, os) + 2ull * kernel.padding;
    const auto bin_count = checked_product(width, height);
    const auto hist_bytes = checked_product(bin_count, sizeof(Bin));
    const auto image_bytes = checked_product(checked_product(f.width, f.height), 3);
    const auto budget = checked_product(opt.memory_mib, 1024 * 1024);
    if (hist_bytes > budget || image_bytes > budget - hist_bytes)
        throw std::invalid_argument("Histogram and image exceed --memory-mib; reduce size or oversampling");
    const auto batch_count = (opt.samples + batch_size - 1) / batch_size;
    const auto per_worker = sizeof(Hit) * std::min(batch_size, opt.samples);
    const auto kernel_bytes = f.filter_radius ? std::uint64_t(kernel.taps) * sizeof(double) : 0;
    const auto remaining = budget - hist_bytes - image_bytes;
    if (kernel_bytes > remaining) throw std::invalid_argument("Filter exceeds --memory-mib");
    const auto fits = (remaining - kernel_bytes) / per_worker;
    if (!fits) throw std::invalid_argument("Worker hit buffer exceeds --memory-mib");
    const unsigned hardware = std::max(1u, std::thread::hardware_concurrency());
    const unsigned workers = static_cast<unsigned>(std::min({batch_count, fits,
        std::uint64_t(opt.threads ? opt.threads : hardware)}));
    TransformChooser choose(f);
    std::vector<double> visibility;
    for (const auto& t : f.transforms) visibility.push_back(apophysis ? t.opacity : std::pow(t.opacity, std::log2(10.0)));

    std::vector<Bin> bins(static_cast<std::size_t>(bin_count));
    std::atomic<std::uint64_t> next{0};
    std::mutex merge;
    std::exception_ptr error;
    std::uint64_t plotted = 0, invalid = 0;
    auto run = [&]() {
        try {
            std::vector<Hit> pending;
            pending.reserve(static_cast<std::size_t>(std::min(batch_size, opt.samples)));
            for (;;) {
                const auto batch = next.fetch_add(1, std::memory_order_relaxed);
                if (batch >= batch_count) break;
                const auto n = std::min(batch_size, opt.samples - batch * batch_size);
                // The stream belongs to the batch. Scheduling never selects a seed.
                Rng rng(opt.seed + batch * UINT64_C(0x9e3779b97f4a7c15));
                Orbit orbit = start(rng);
                std::optional<std::size_t> previous;
                std::uint64_t bad = 0;
                int warmup = burn_in;
                pending.clear();
                for (std::uint64_t i = 0; i < n + burn_in; ++i) {
                    const auto idx = choose.pick(rng.unit(), previous);
                    orbit = apply_transform(f.transforms[idx], orbit, rng);
                    if (!finite(orbit)) {
                        ++bad;
                        orbit = start(rng);
                        previous.reset();
                        warmup = burn_in;
                        continue;
                    }
                    previous = idx;
                    if (warmup > 0) { --warmup; continue; }
                    if (visibility[idx] == 0) continue;
                    if (apophysis && visibility[idx] < 1 && rng.unit() >= visibility[idx]) continue;
                    Orbit sample = orbit;
                    if (f.final_transform) {
                        const auto& final = *f.final_transform;
                        if (apophysis || final.opacity == 1 || (final.opacity > 0 && rng.unit() < final.opacity))
                            sample = apply_transform(final, orbit, rng);
                        if (!finite(sample)) { ++bad; continue; }
                    }
                    const double dx = sample.point.x - f.center.x, dy = sample.point.y - f.center.y;
                    double x = ((cs * dx - sn * dy) * ppu + f.width * 0.5) * os + kernel.padding;
                    double y = ((sn * dx + cs * dy) * ppu + f.height * 0.5) * os + kernel.padding;
                    if (!(x >= 0 && x < double(width) && y >= 0 && y < double(height))) continue;
                    if (apophysis) {
                        x = std::min(double(width-1), std::nearbyint(x * (1 - .5 / double(width))));
                        y = std::min(double(height-1), std::nearbyint(y * (1 - .5 / double(height))));
                    }
                    const RGB color = color_at(f, sample.color, apophysis);
                    pending.push_back({std::size_t(y) * std::size_t(width) + std::size_t(x),
                        bucket_value(color, visibility[idx], apophysis)});
                }
                std::lock_guard lock(merge);
                for (const auto& hit : pending) {
                    auto& bin = bins[hit.bin];
                    for (unsigned c = 0; c < 4; ++c) bin[c] += hit.value[c];
                }
                plotted += pending.size();
                invalid += bad;
            }
        } catch (...) {
            std::lock_guard lock(merge);
            if (!error) error = std::current_exception();
            next.store(batch_count);
        }
    };
    {
        std::vector<std::jthread> pool;
        for (unsigned i = 1; i < workers; ++i) pool.emplace_back(run);
        run();
    }
    if (error) std::rethrow_exception(error);
    return {develop(f, bins, ppu, opt.samples, kernel, static_cast<std::size_t>(width), apophysis), plotted, invalid, workers};
}
}
