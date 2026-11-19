#include "render_proof.hpp"
#include "plugin.hpp"
#include "render_internal.hpp"
#include "vulkan_runtime.hpp"

#include <algorithm>
#include <cmath>
#include <thread>

namespace apo {
namespace {
constexpr std::uint64_t mib = 1024 * 1024;
void check_stop(std::stop_token stop) { if (stop.stop_requested()) throw RenderCancelled(); }

struct BinRange {
    unsigned first, end, lit_first, lit_count;
    unsigned size() const { return end - first; }
};
BinRange bin_range(int origin, int count, unsigned os, const detail::Footprint& filter,
                   std::uint64_t halo, unsigned frame_size) {
    const auto lit_first = std::uint64_t(origin) * os + filter.inset;
    const auto lit_count = std::uint64_t(count - 1) * os + filter.taps;
    if (lit_first + lit_count > frame_size)
        throw std::invalid_argument("Proof filter extends beyond the export histogram");
    return {unsigned(lit_first > halo ? lit_first - halo : 0),
            unsigned(std::min<std::uint64_t>(frame_size, lit_first + lit_count + halo)),
            unsigned(lit_first), unsigned(lit_count)};
}
struct WorkingMemory {
    std::uint64_t available, reserved = 0;
    void charge(std::uint64_t count, std::uint64_t bytes = 1) {
        if (bytes && count > (available - reserved) / bytes)
            throw std::invalid_argument("Proof exceeds the render memory budget. Select a smaller crop or increase memory.");
        reserved += count * bytes;
    }
};
void check_tone(const ProofTone& t) {
    const auto unit = [](double v) { return v >= 0 && v <= 1; };
    if (!std::isfinite(t.brightness) || t.brightness < 0 || !std::isfinite(t.gamma) || t.gamma <= 0 ||
        !std::isfinite(t.gamma_threshold) || t.gamma_threshold < 0 || !unit(t.vibrancy) ||
        !unit(t.background.r) || !unit(t.background.g) || !unit(t.background.b))
        throw std::invalid_argument("Invalid proof brightness, gamma, threshold, vibrancy or background");
}
}

struct ProofHistogram {
    detail::RenderGeometry geometry;
    // Spatially filtered histogram, before every adjustable tone operation.
    std::vector<detail::LightBin> pixels;
};

ProofTone proof_tone(const Flame& f) {
    return {f.brightness, f.gamma, f.vibrancy, f.gamma_threshold, f.background};
}

std::shared_ptr<const RenderProof> sample_render_proof(const Flame& f, const RenderOptions& opt,
                                                       ProofRegion region) {
    check_stop(opt.stop_token);
    if (f.width < 1 || f.height < 1 || f.width > 16384 || f.height > 16384 ||
        region.x < 0 || region.y < 0 || region.width < 1 || region.height < 1 ||
        region.x > f.width - region.width || region.y > f.height - region.height)
        throw std::invalid_argument("Select a proof rectangle within the final output dimensions (up to 16384 by 16384)");
    if (!opt.memory_mib || opt.memory_mib > UINT64_MAX / mib || !opt.gpu_memory_mib ||
        opt.gpu_memory_mib > UINT64_MAX / mib)
        throw std::invalid_argument("Invalid proof rendering memory budget");
    if (opt.backend != RenderBackend::cpu && opt.backend != RenderBackend::automatic &&
        opt.backend != RenderBackend::vulkan)
        throw std::invalid_argument("Unknown proof backend");

    auto hist = std::make_shared<ProofHistogram>();
    auto& geometry = hist->geometry;
    geometry = detail::render_geometry(f, opt, false);
    const auto& density = geometry.density;
    const unsigned os = unsigned(f.oversample);
    // density_strip skips os-1 columns on each physical edge. At an interior
    // crop edge, retain that border beyond every center that can scatter into
    // a wanted bin. At a true frame edge, preserve the original clipped bounds.
    const auto halo_x = density.levels ? std::uint64_t(density.radius) + std::max(os - 1, density.count_radius) : 0;
    const auto halo_y = std::uint64_t(density.radius) + density.count_radius;
    const auto x = bin_range(region.x, region.width, os, geometry.kernel, halo_x, geometry.gpu.width);
    const auto y = bin_range(region.y, region.height, os, geometry.kernel, halo_y, geometry.gpu.height);

    WorkingMemory memory{opt.memory_mib * mib};
    const auto count = std::uint64_t(x.size()) * y.size();
    memory.charge(count, sizeof(detail::Bin));
    memory.charge(65536); // Fixed model, row metadata and CPU RNG state.
    memory.charge(TransformChooser::storage_bytes(f));
    const auto workers = std::clamp(opt.threads ? opt.threads : std::thread::hardware_concurrency(), 1u, 8u);
    memory.charge(f.transforms.size() + 1, 256ull * workers); // Visibility and per-worker blur histories.
    memory.charge(std::uint64_t(region.width) * region.height, 4 + sizeof(detail::LightBin));
    memory.charge(region.width, sizeof(detail::LightBin));
    if (f.filter_radius) memory.charge(geometry.kernel.taps, sizeof(double));
    memory.charge(ColorCurveEvaluator::storage_bytes(f.curves));
    if (density.levels) {
        memory.charge(density.table_bytes);
        memory.charge(density.column_bytes(x.size()));
        memory.charge(std::uint64_t(x.size()) * y.lit_count, sizeof(detail::LightBin));
    }
    // Each chunk starts on both the CPU batch and GPU chain boundary. Charge
    // simultaneous hit buffers (including device readback on unified memory).
    constexpr auto batch = detail::cpu_batch_size;
    const auto capacity = (memory.available - memory.reserved) / (4 * sizeof(detail::SampleHit));
    if (capacity < std::min(batch, opt.samples))
        throw std::invalid_argument("Proof sampling buffers exceed the memory budget. Select a smaller crop or increase memory.");
    const auto chunk = capacity < batch ? opt.samples : std::min<std::uint64_t>(8, capacity / batch) * batch;
    std::vector<detail::Bin> bins(static_cast<std::size_t>(count));
    if (!color_curves_are_identity(f.curves))
        geometry.curves = std::make_shared<ColorCurveEvaluator>(f.curves);
    if (density.levels)
        geometry.density_kernels = std::make_shared<detail::DensityKernels>(density, opt.stop_token);

    auto proof = std::shared_ptr<RenderProof>(new RenderProof);
    proof->region = region;
    proof->samples = opt.samples;
    proof->histogram_bytes = std::uint64_t(region.width) * region.height * sizeof(detail::LightBin);
    proof->backend = opt.backend == RenderBackend::cpu ? RenderBackend::cpu : RenderBackend::vulkan;
    gpu::ContextScope context;
    if (opt.progress) opt.progress(0, opt.samples);
    for (std::uint64_t completed = 0; completed < opt.samples;) {
        check_stop(opt.stop_token);
        const auto n = std::min(chunk, opt.samples - completed);
        std::vector<detail::SampleHit> hits;
        if (proof->backend == RenderBackend::vulkan) {
            try {
                if (has_plugin_variations(f)) throw gpu::Unavailable("Native plugin variations require the CPU renderer");
                constexpr auto chain = gpu::samples_per_chain;
                auto result = gpu::sample_hits(f, opt, geometry.gpu, completed / chain,
                                               unsigned((n + chain - 1) / chain), memory.reserved);
                proof->device_name = std::move(result.device_name);
                hits = std::move(result.hits);
            } catch (const gpu::Unavailable& error) {
                if (!opt.allow_cpu_fallback || completed) throw;
                proof->backend = RenderBackend::cpu;
                proof->fallback_reason = error.what();
            }
        }
        if (proof->backend == RenderBackend::cpu) {
            auto result = detail::sample_cpu_hits(f, opt, geometry, completed / batch, unsigned((n + batch - 1) / batch));
            hits = std::move(result.hits);
        }
        for (const auto& hit : hits) {
            if (hit[0] < x.first || hit[0] >= x.end || hit[1] < y.first || hit[1] >= y.end) continue;
            auto& bin = bins[std::size_t(hit[1] - y.first) * x.size() + (hit[0] - x.first)];
            for (unsigned c = 0; c < 4; ++c) bin[c] += hit[c + 2];
        }
        completed += n;
        if (opt.progress) opt.progress(completed, opt.samples);
    }
    check_stop(opt.stop_token);
    const auto light = density.levels
        ? detail::density_strip(f, geometry, bins, y.first, y.lit_first, y.lit_count,
                                opt.samples, opt.stop_token, x.size())
        : std::vector<detail::LightBin>{};
    hist->pixels.resize(std::size_t(region.width) * region.height);
    for (int row = 0; row < region.height; ++row) {
        auto dst = std::span(hist->pixels).subspan(std::size_t(row) * region.width, region.width);
        detail::filter_row(f, geometry, bins, light.empty() ? y.first : y.lit_first,
                           region.y + row, opt.samples, dst, opt.stop_token, light, {x.first, x.size(), region.x});
    }
    geometry.density_kernels.reset();
    check_stop(opt.stop_token);
    proof->histogram_ = std::move(hist);
    return proof;
}

std::vector<std::uint8_t> develop_render_proof(const RenderProof& proof, const ProofTone& tone,
                                              bool alpha, std::stop_token stop, bool sixteen_bit) {
    check_stop(stop);
    check_tone(tone);
    const auto& h = *proof.histogram_;
    Flame f;
    f.brightness = tone.brightness;
    f.gamma = tone.gamma;
    f.vibrancy = tone.vibrancy;
    f.gamma_threshold = tone.gamma_threshold;
    f.background = tone.background;
    const auto r = proof.region;
    std::vector<detail::LightBin> row(static_cast<std::size_t>(r.width));
    std::vector<std::uint8_t> out(std::size_t(r.width) * r.height * 4);
    auto dest = out.begin();
    for (int i = 0; i < r.height; ++i) {
        std::copy_n(h.pixels.begin() + std::size_t(i) * r.width, r.width, row.begin());
        detail::tone_row(f, h.geometry, row, sixteen_bit, alpha, stop);
        for (const auto& pixel : row) {
            for (const auto channel : pixel) {
                if (!std::isfinite(channel)) throw std::invalid_argument("Image samples must be finite");
                const auto v = std::clamp(channel, 0.0, 1.0);
                const unsigned sample = unsigned(v * (sixteen_bit ? 65535 : 255) + .5);
                *dest++ = std::uint8_t(sixteen_bit ? (sample + 128) / 257 : sample);
            }
        }
    }
    check_stop(stop);
    return out;
}
}
