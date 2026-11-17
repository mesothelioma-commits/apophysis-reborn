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

RGB color_at(const Flame& flame, double color) {
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
Orbit start(Rng& rng) {
    return {{2 * rng.unit() - 1, 2 * rng.unit() - 1}, rng.unit()};
}
bool finite(Orbit s) {
    return std::isfinite(s.point.x) && std::isfinite(s.point.y) && std::isfinite(s.color);
}

double log_one_plus_exp(double x) { // Stable log-density for extreme camera scales.
    return std::max(x, 0.0) + std::log1p(std::exp(-std::abs(x)));
}

Image develop(const Flame& f, const std::vector<Bin>& bins, double ppu, std::uint64_t samples) {
    const auto os = static_cast<std::size_t>(f.oversample);
    const auto width = static_cast<std::size_t>(f.width);
    const auto stride = width * os;
    const double log_gain = 2 * (std::log(double(os)) + std::log(ppu)) - std::log(double(samples));
    const double exposure = (268.0 / 256) / double(os * os);
    const double gamma = 1 / f.gamma;
    const std::array bg{f.background.r, f.background.g, f.background.b};
    Image image{f.width, f.height, std::vector<std::uint8_t>(width * f.height * 3)};
    for (int y = 0; y < f.height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            std::array<double, 4> sum{};
            for (std::size_t dy = 0; dy < os; ++dy) {
                const auto row = (std::size_t(y) * os + dy) * stride + x * os;
                for (std::size_t dx = 0; dx < os; ++dx) {
                    const Bin& bin = bins[row + dx];
                    if (!bin[3]) continue;
                    const double mass = double(bin[3]) / quantum;
                    const double light = exposure * log_one_plus_exp(std::log(mass) + log_gain);
                    sum[3] += light;
                    for (unsigned c = 0; c < 3; ++c) sum[c] += light * (double(bin[c]) / double(bin[3]));
                }
            }
            const double alpha = std::pow(sum[3] * f.brightness, gamma);
            for (unsigned c = 0; c < 3; ++c) {
                const double average = sum[3] > 0 ? sum[c] / sum[3] : 0;
                double channel = (1 - std::min(1.0, alpha)) * bg[c];
                if (f.vibrancy > 0 && average > 0) channel += f.vibrancy * alpha * average;
                if (f.vibrancy < 1) channel += (1 - f.vibrancy) * std::pow(sum[c] * f.brightness, gamma);
                image.rgb[(std::size_t(y) * width + x) * 3 + c] =
                    static_cast<std::uint8_t>(std::lround(255 * std::clamp(channel, 0.0, 1.0)));
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
    const auto width = checked_product(f.width, os), height = checked_product(f.height, os);
    const auto bin_count = checked_product(width, height);
    const auto hist_bytes = checked_product(bin_count, sizeof(Bin));
    const auto image_bytes = checked_product(checked_product(f.width, f.height), 3);
    const auto budget = checked_product(opt.memory_mib, 1024 * 1024);
    if (hist_bytes > budget || image_bytes > budget - hist_bytes)
        throw std::invalid_argument("Histogram and image exceed --memory-mib; reduce size or oversampling");
    const auto batch_count = (opt.samples + batch_size - 1) / batch_size;
    const auto per_worker = sizeof(Hit) * std::min(batch_size, opt.samples);
    const auto fits = (budget - hist_bytes - image_bytes) / per_worker;
    if (!fits) throw std::invalid_argument("Worker hit buffer exceeds --memory-mib");
    const unsigned hardware = std::max(1u, std::thread::hardware_concurrency());
    const unsigned workers = static_cast<unsigned>(std::min({batch_count, fits,
        std::uint64_t(opt.threads ? opt.threads : hardware)}));
    TransformChooser choose(f);
    std::vector<double> visibility;
    for (const auto& t : f.transforms) visibility.push_back(std::pow(t.opacity, std::log2(10.0)));

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
                    Orbit sample = orbit;
                    if (f.final_transform) {
                        const auto& final = *f.final_transform;
                        if (final.opacity == 1 || (final.opacity > 0 && rng.unit() < final.opacity))
                            sample = apply_transform(final, orbit, rng);
                        if (!finite(sample)) { ++bad; continue; }
                    }
                    const double dx = sample.point.x - f.center.x, dy = sample.point.y - f.center.y;
                    const double x = ((cs * dx - sn * dy) * ppu + f.width * 0.5) * os;
                    const double y = ((sn * dx + cs * dy) * ppu + f.height * 0.5) * os;
                    if (!(x >= 0 && x < double(width) && y >= 0 && y < double(height))) continue;
                    const RGB color = color_at(f, sample.color);
                    const double v = visibility[idx];
                    pending.push_back({std::size_t(y) * std::size_t(width) + std::size_t(x),
                        {fixed(v * color.r), fixed(v * color.g), fixed(v * color.b), fixed(v)}});
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
    return {develop(f, bins, ppu, opt.samples), plotted, invalid, workers};
}
}
