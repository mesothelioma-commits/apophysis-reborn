#include "render.hpp"
#include "plugin.hpp"
#include "render_internal.hpp"
#include "gpu.hpp"
#include "vulkan_runtime.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <chrono>
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
constexpr std::uint64_t legacy_batch_size = 8192;
constexpr int burn_in = 32;
using Bin = std::array<std::uint64_t, 4>; // R, G, B, opacity
using Hit = detail::SampleHit;

void check_cancelled(std::stop_token stop) {
    if (stop.stop_requested()) throw RenderCancelled();
}

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

std::uint64_t sample_batch(const Flame& f, const RenderOptions& opt,
                           const gpu::Geometry& g, const TransformChooser& choose,
                           const std::vector<double>& visibility, std::uint64_t batch,
                           std::uint64_t n, bool portable, std::vector<Hit>& pending) {
    const bool apophysis = is_apophysis(f.source_version);
    const auto os = static_cast<std::uint64_t>(f.oversample);
    const auto width = g.width, height = g.height;
    const double cs = g.cosine, sn = g.sine, ppu = g.ppu;
    Rng rng = portable ? Rng(opt.seed, batch) : Rng(opt.seed + batch * UINT64_C(0x9e3779b97f4a7c15));
    Orbit orbit = start(rng);
    std::optional<std::size_t> previous;
    std::uint64_t bad = 0;
    int warmup = burn_in;
    pending.clear();
    for (std::uint64_t i = 0; i < n + burn_in; ++i) {
        if ((i & 255u) == 0) check_cancelled(opt.stop_token);
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
        double x = ((cs * dx - sn * dy) * ppu + f.width * 0.5) * os + g.padding;
        double y = ((sn * dx + cs * dy) * ppu + f.height * 0.5) * os + g.padding;
        if (!(x >= 0 && x < double(width) && y >= 0 && y < double(height))) continue;
        if (apophysis) {
            x = std::min(double(width-1), std::nearbyint(x * (1 - .5 / double(width))));
            y = std::min(double(height-1), std::nearbyint(y * (1 - .5 / double(height))));
        }
        const auto value = bucket_value(color_at(f, sample.color, apophysis), visibility[idx], apophysis);
        pending.push_back({std::uint32_t(x), std::uint32_t(y), value[0], value[1], value[2], value[3]});
    }
    return bad;
}

double log_one_plus_exp(double x) { // Stable log-density for extreme camera scales.
    return std::max(x, 0.0) + std::log1p(std::exp(-std::abs(x)));
}

// Keep the historical even/odd alignment at subpixel resolution. When the
// kernel is narrower than a pixel, its first tap starts inside that pixel.
using Footprint = detail::Footprint;

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
              const Footprint& kernel, std::size_t stride, bool apophysis, std::stop_token stop) {
    const ColorCurveEvaluator curves(f.curves);
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
    std::array<std::uint8_t, 3> empty_pixel{};
    for (unsigned c = 0; c < 3; ++c)
        empty_pixel[c] = apophysis ? static_cast<std::uint8_t>(std::floor(bg[c] * 255))
                                  : static_cast<std::uint8_t>(std::lround(255 * std::clamp(bg[c], 0.0, 1.0)));
    Image image{f.width, f.height, std::vector<std::uint8_t>(width * f.height * 3)};
    for (int y = 0; y < f.height; ++y) {
        check_cancelled(stop);
        for (std::size_t x = 0; x < width; ++x) {
            std::array<double, 4> sum{};
            for (std::size_t dy = 0; dy < kernel.taps; ++dy) {
                if ((dy & 31u) == 0) check_cancelled(stop);
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
            // Empty areas have a defined background even when a curve lifts
            // black. Avoid gamma/power and channel work for their zero light.
            const double light = sum[3] * f.brightness;
            if (light == 0) {
                std::copy(empty_pixel.begin(), empty_pixel.end(),
                          image.rgb.begin() + (std::size_t(y) * width + x) * 3);
                continue;
            }
            const double alpha = gamma_alpha(light, gamma, f.gamma_threshold);
            for (unsigned c = 0; c < 3; ++c) {
                const double average = sum[3] > 0 ? sum[c] / sum[3] : 0;
                double foreground = 0;
                if (vibrancy > 0 && average > 0) foreground += vibrancy * alpha * average;
                if (vibrancy < 1) foreground += (1 - vibrancy) * std::pow(sum[c] * f.brightness, gamma);
                if (curves.active() && sum[3] * f.brightness > 0) foreground = curves.map(c, foreground);
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

namespace {
Image reconstruct(const Flame& f, const RenderOptions& opt, detail::RenderGeometry& geometry,
                  const std::vector<Bin>& bins) {
    const auto& kernel = geometry.kernel;
    if (!geometry.density.levels)
        return develop(f, bins, geometry.gpu.ppu, opt.samples, kernel, geometry.gpu.width, geometry.historical, opt.stop_token);
    if (!color_curves_are_identity(f.curves))
        geometry.curves = std::make_shared<ColorCurveEvaluator>(f.curves);
    geometry.density_kernels = std::make_shared<detail::DensityKernels>(geometry.density, opt.stop_token);
    const unsigned lit_rows = (unsigned(f.height) - 1) * unsigned(f.oversample) + kernel.taps;
    const auto light = detail::density_strip(f, geometry, bins, 0, kernel.inset, lit_rows, opt.samples, opt.stop_token);
    std::vector<detail::LightBin> row(f.width);
    Image image{f.width, f.height, std::vector<std::uint8_t>(std::size_t(f.width) * f.height * 3)};
    for (int y = 0; y < f.height; ++y) {
        detail::develop_row(f, geometry, {}, kernel.inset, y, opt.samples, row, false, false, opt.stop_token, light);
        for (int x = 0; x < f.width; ++x)
            for (unsigned c = 0; c < 3; ++c)
                image.rgb[(std::size_t(y) * f.width + x) * 3 + c] = std::uint8_t(std::lround(255 * row[x][c]));
    }
    return image;
}

RenderResult render_impl(const Flame& f, const RenderOptions& opt, bool portable) {
    check_cancelled(opt.stop_token);
    auto geometry = detail::render_geometry(f, opt, false);
    const auto& kernel = geometry.kernel;
    const auto width = geometry.gpu.width, height = geometry.gpu.height;
    const auto apophysis = geometry.historical;
    const auto bin_count = checked_product(width, height);
    const auto hist_bytes = checked_product(bin_count, sizeof(Bin));
    const auto image_bytes = checked_product(checked_product(f.width, f.height), 3);
    const auto budget = checked_product(opt.memory_mib, 1024 * 1024);
    if (hist_bytes > budget || image_bytes > budget - hist_bytes)
        throw std::invalid_argument("Histogram and image exceed --memory-mib; reduce size or oversampling");
    const auto remaining = budget - hist_bytes - image_bytes;
    std::uint64_t kernel_bytes = 0;
    const auto charge = [&](std::uint64_t bytes) {
        if (bytes > remaining - kernel_bytes)
            throw std::invalid_argument("Reconstruction filters and color curves exceed --memory-mib");
        kernel_bytes += bytes;
    };
    if (f.filter_radius) charge(std::uint64_t(kernel.taps) * sizeof(double));
    charge(ColorCurveEvaluator::storage_bytes(f.curves));
    if (geometry.density.levels) {
        charge(geometry.density.table_bytes);
        charge(geometry.density.column_bytes(width));
        const auto lit_rows = std::uint64_t(f.height - 1) * f.oversample + kernel.taps;
        charge(checked_product(checked_product(width, lit_rows), sizeof(detail::LightBin)));
        charge(checked_product(f.width, sizeof(detail::LightBin)));
    }
    const auto started = std::chrono::steady_clock::now();
    std::string fallback;
    if (!portable && opt.backend != RenderBackend::cpu) {
        try {
            if (has_plugin_variations(f))
                throw gpu::Unavailable("Native plugin variations require the CPU renderer");
            auto sampled = gpu::sample(f, opt, geometry.gpu, hist_bytes + image_bytes + kernel_bytes);
            const auto sampled_at = std::chrono::steady_clock::now();
            auto image = reconstruct(f, opt, geometry, sampled.bins);
            const auto finished = std::chrono::steady_clock::now();
            RenderResult result{std::move(image), sampled.plotted, sampled.invalid, 0};
            result.backend = RenderBackend::vulkan;
            result.device_name = std::move(sampled.device_name);
            result.sampling_ms = std::chrono::duration<double, std::milli>(sampled_at - started).count();
            result.reconstruction_ms = std::chrono::duration<double, std::milli>(finished - sampled_at).count();
            return result;
        } catch (const gpu::Unavailable& error) {
            if (!opt.allow_cpu_fallback) throw;
            fallback = error.what();
        }
    }
    const std::uint64_t batch_size = portable ? gpu::samples_per_chain : legacy_batch_size;
    const auto batch_count = (opt.samples + batch_size - 1) / batch_size;
    const auto per_worker = sizeof(Hit) * std::min(batch_size, opt.samples);
    const auto chooser_bytes = TransformChooser::storage_bytes(f);
    const auto visibility_bytes = checked_product(f.transforms.size(), sizeof(double));
    const auto sampling_budget = remaining - kernel_bytes;
    if (chooser_bytes > sampling_budget || visibility_bytes > sampling_budget - chooser_bytes)
        throw std::invalid_argument("Transform selection tables exceed --memory-mib; increase the buffer budget or simplify chaos");
    const auto fits = (sampling_budget - chooser_bytes - visibility_bytes) / per_worker;
    if (!fits) throw std::invalid_argument("Worker hit buffer exceeds --memory-mib");
    const unsigned hardware = std::max(1u, std::thread::hardware_concurrency());
    const unsigned workers = static_cast<unsigned>(std::min({batch_count, fits,
        std::uint64_t(opt.threads ? opt.threads : hardware)}));
    TransformChooser choose(f);
    std::vector<double> visibility;
    visibility.reserve(f.transforms.size());
    for (const auto& t : f.transforms) visibility.push_back(apophysis ? t.opacity : std::pow(t.opacity, std::log2(10.0)));

    check_cancelled(opt.stop_token);
    std::vector<Bin> bins(static_cast<std::size_t>(bin_count));
    std::atomic<std::uint64_t> next{0};
    std::mutex merge;
    std::exception_ptr error;
    std::uint64_t plotted = 0, invalid = 0, completed = 0;
    if (opt.progress) opt.progress(0, opt.samples);
    auto run = [&]() {
        try {
            std::vector<Hit> pending;
            pending.reserve(static_cast<std::size_t>(std::min(batch_size, opt.samples)));
            for (;;) {
                check_cancelled(opt.stop_token);
                const auto batch = next.fetch_add(1, std::memory_order_relaxed);
                if (batch >= batch_count) break;
                const auto n = std::min(batch_size, opt.samples - batch * batch_size);
                // The stream belongs to the batch. Scheduling never selects a seed.
                const auto bad = sample_batch(f, opt, geometry.gpu, choose, visibility, batch, n, portable, pending);
                std::lock_guard lock(merge);
                for (const auto& hit : pending) {
                    auto& bin = bins[std::size_t(hit[1]) * width + hit[0]];
                    for (unsigned c = 0; c < 4; ++c) bin[c] += hit[c + 2];
                }
                plotted += pending.size();
                invalid += bad;
                completed += n;
                if (opt.progress) opt.progress(completed, opt.samples);
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
    check_cancelled(opt.stop_token);
    const auto sampled_at = std::chrono::steady_clock::now();
    RenderResult result{reconstruct(f, opt, geometry, bins), plotted, invalid, workers};
    result.fallback_reason = std::move(fallback);
    result.device_name = portable ? "CPU compute-sequence reference" : "CPU";
    result.sampling_ms = std::chrono::duration<double, std::milli>(sampled_at - started).count();
    result.reconstruction_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - sampled_at).count();
    return result;
}
}

RenderResult render(const Flame& flame, const RenderOptions& options) {
    return render_impl(flame, options, false);
}
RenderResult render_compute_reference(const Flame& flame, const RenderOptions& options) {
    return render_impl(flame, options, true);
}
std::string compute_support_reason(const Flame& flame) {
    return gpu::support_reason(flame);
}
std::string_view backend_name(RenderBackend backend) {
    switch (backend) {
    case RenderBackend::cpu: return "cpu";
    case RenderBackend::automatic: return "auto";
    case RenderBackend::vulkan: return "vulkan";
    }
    return "unknown";
}

namespace detail {
RenderGeometry render_geometry(const Flame& f, const RenderOptions& opt, bool prepare_curves) {
    validate(f);
    if (!opt.samples || opt.samples > max_samples)
        throw std::invalid_argument("Sample count exceeds the supported positive fixed-point accumulator range");
    const double ppu = f.scale * std::exp2(f.zoom);
    if (!(ppu > 0 && std::isfinite(ppu))) throw std::invalid_argument("Invalid effective camera scale");
    const bool historical = is_apophysis(f.source_version);
    auto kernel = footprint(f, historical);
    const auto density = density_plan(f);
    if (density.camera_margin > kernel.padding) {
        kernel.inset += density.camera_margin - kernel.padding;
        kernel.padding = density.camera_margin;
    }
    const auto width = checked_product(f.width, f.oversample) + 2ull * kernel.padding;
    const auto height = checked_product(f.height, f.oversample) + 2ull * kernel.padding;
    if (width > UINT32_MAX || height > UINT32_MAX)
        throw std::invalid_argument("Histogram dimensions exceed the supported coordinate range");
    const double angle = std::remainder(f.rotate, 360.0) * (std::numbers::pi / 180.0);
    return {{std::uint32_t(width), std::uint32_t(height), kernel.padding, ppu,
             std::cos(angle), std::sin(angle)}, kernel, historical,
             !prepare_curves || color_curves_are_identity(f.curves) ? nullptr : std::make_shared<ColorCurveEvaluator>(f.curves),
             density, prepare_curves && density.levels ? std::make_shared<DensityKernels>(density, opt.stop_token) : nullptr};
}

CpuHits sample_cpu_hits(const Flame& f, const RenderOptions& opt, const RenderGeometry& geometry,
                       std::uint64_t first_batch, unsigned count) {
    check_cancelled(opt.stop_token);
    const auto batches = (opt.samples + legacy_batch_size - 1) / legacy_batch_size;
    if (!count || count > 128 || first_batch >= batches || count > batches - first_batch)
        throw std::invalid_argument("Invalid CPU streaming batch range");
    const unsigned hardware = std::max(1u, std::thread::hardware_concurrency());
    const unsigned workers = std::min(count, opt.threads ? opt.threads : hardware);
    TransformChooser chooser(f);
    std::vector<double> visibility;
    visibility.reserve(f.transforms.size());
    for (const auto& t : f.transforms)
        visibility.push_back(geometry.historical ? t.opacity : std::pow(t.opacity, std::log2(10.0)));
    std::vector<std::vector<Hit>> blocks(count);
    std::vector<std::uint64_t> bad(count);
    std::atomic<unsigned> next{0};
    std::mutex failure;
    std::exception_ptr error;
    const auto run = [&] {
        try {
            for (;;) {
                const auto i = next.fetch_add(1);
                if (i >= count) break;
                const auto batch = first_batch + i;
                const auto n = std::min(legacy_batch_size, opt.samples - batch * legacy_batch_size);
                blocks[i].reserve(static_cast<std::size_t>(n));
                bad[i] = sample_batch(f, opt, geometry.gpu, chooser, visibility, batch, n, false, blocks[i]);
            }
        } catch (...) {
            std::lock_guard lock(failure);
            if (!error) error = std::current_exception();
            next.store(count);
        }
    };
    {
        std::vector<std::jthread> threads;
        for (unsigned i = 1; i < workers; ++i) threads.emplace_back(run);
        run();
    }
    if (error) std::rethrow_exception(error);
    CpuHits result;
    result.workers = workers;
    result.hits.reserve(static_cast<std::size_t>(std::min(std::uint64_t(count) * legacy_batch_size,
                                                        opt.samples - first_batch * legacy_batch_size)));
    for (unsigned i = 0; i < count; ++i) {
        result.hits.insert(result.hits.end(), blocks[i].begin(), blocks[i].end());
        result.invalid += bad[i];
    }
    check_cancelled(opt.stop_token);
    return result;
}

void develop_row(const Flame& f, const RenderGeometry& geometry, std::span<const Bin> bins,
                 std::uint32_t first_row, int y, std::uint64_t samples,
                 std::span<std::array<double, 4>> rgba, bool sixteen_bit, bool transparent,
                 std::stop_token stop, std::span<const LightBin> density_light) {
    check_cancelled(stop);
    const auto& kernel = geometry.kernel;
    const auto os = static_cast<std::size_t>(f.oversample);
    const auto stride = std::size_t(geometry.gpu.width);
    const auto footprint_bins = density_light.empty() ? bins.size() : density_light.size();
    if (y < 0 || y >= f.height || rgba.size() != std::size_t(f.width) ||
        std::size_t(y) * os + kernel.inset < first_row ||
        (std::size_t(y) * os + kernel.inset + kernel.taps - first_row) * stride > footprint_bins)
        throw std::invalid_argument("Reconstruction strip does not contain the complete filter footprint");
    const auto filter = f.filter_radius ? gaussian_weights(kernel) : std::vector<double>{};
    const double box_weight = 1.0 / double(os * os);
    const bool historical = geometry.historical;
    const double log_gain = 2 * (std::log(double(os)) + std::log(geometry.gpu.ppu)) - std::log(double(samples));
    const double exposure = (268.0 / 256) * (historical ? 2.3 / std::log(10.0) : 1.0);
    const double gamma = 1 / f.gamma;
    const double vibrancy = historical ? std::nearbyint(f.vibrancy * 256) / 256 : f.vibrancy;
    const double mass_unit = historical ? apophysis_white : quantum;
    const std::array bg{f.background.r, f.background.g, f.background.b};
    std::array<double, 4> empty_pixel{0, 0, 0, transparent ? 0.0 : 1.0};
    if (!transparent)
        for (unsigned c = 0; c < 3; ++c)
            empty_pixel[c] = historical && !sixteen_bit ? std::floor(bg[c] * 255) / 255
                                                       : std::clamp(bg[c], 0.0, 1.0);
    for (std::size_t x = 0; x < rgba.size(); ++x) {
        if ((x & 255u) == 0) check_cancelled(stop);
        std::array<double, 4> sum{};
        for (std::size_t dy = 0; dy < kernel.taps; ++dy) {
            if ((dy & 31u) == 0) check_cancelled(stop);
            const auto row = (std::size_t(y) * os + kernel.inset + dy - first_row) * stride + x * os + kernel.inset;
            for (std::size_t dx = 0; dx < kernel.taps; ++dx) {
                const double weight = filter.empty() ? box_weight : filter[dy] * filter[dx];
                if (!density_light.empty()) {
                    const auto& light = density_light[row + dx];
                    for (unsigned c = 0; c < 4; ++c) sum[c] += weight * light[c];
                    continue;
                }
                const auto& bin = bins[row + dx];
                if (!bin[3]) continue;
                const double mass = double(bin[3]) / mass_unit;
                const double light = (exposure * weight) * log_one_plus_exp(std::log(mass) + log_gain);
                sum[3] += light;
                for (unsigned c = 0; c < 3; ++c) sum[c] += light * (double(bin[c]) / double(bin[3]));
            }
        }
        const double light = sum[3] * f.brightness;
        if (light == 0) {
            rgba[x] = empty_pixel;
            continue;
        }
        const double alpha = gamma_alpha(light, gamma, f.gamma_threshold);
        const double a = std::clamp(alpha, 0.0, 1.0);
        rgba[x][3] = transparent ? a : 1;
        for (unsigned c = 0; c < 3; ++c) {
            const double average = sum[3] > 0 ? sum[c] / sum[3] : 0;
            double foreground = 0;
            if (vibrancy > 0 && average > 0) foreground += vibrancy * alpha * average;
            if (vibrancy < 1) foreground += (1 - vibrancy) * std::pow(sum[c] * f.brightness, gamma);
            if (geometry.curves && sum[3] * f.brightness > 0) foreground = geometry.curves->map(c, foreground);
            if (transparent) {
                rgba[x][c] = a > 0 ? std::clamp(foreground / a, 0.0, 1.0) : 0;
            } else if (historical && !sixteen_bit) {
                const auto bg8 = static_cast<unsigned>(std::floor(bg[c] * 255));
                const auto alpha8 = static_cast<unsigned>(std::clamp(std::nearbyint(alpha * 256), 0.0, 255.0));
                const double channel = std::nearbyint(foreground * 256) + ((255 - alpha8) * bg8 / 256);
                rgba[x][c] = (sum[3] * f.brightness == 0 ? double(bg8) : std::clamp(channel, 0.0, 255.0)) / 255;
            } else {
                rgba[x][c] = std::clamp(foreground + (1 - a) * bg[c], 0.0, 1.0);
            }
        }
    }
}
} // namespace detail
}
