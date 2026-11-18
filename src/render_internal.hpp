#pragma once
#include "render.hpp"
#include "gpu_kernel.hpp"
#include <span>

namespace apo::detail {
using Bin = std::array<std::uint64_t, 4>;
using SampleHit = std::array<std::uint32_t, 6>; // Global histogram x/y and R/G/B/mass.
struct Footprint {
    unsigned taps, padding, inset;
    double coordinate_step;
};
struct RenderGeometry {
    gpu::Geometry gpu;
    Footprint kernel;
    bool historical;
};
struct CpuHits {
    std::vector<SampleHit> hits;
    std::uint64_t invalid = 0;
    unsigned workers = 0;
};
RenderGeometry render_geometry(const Flame&, const RenderOptions&);
CpuHits sample_cpu_hits(const Flame&, const RenderOptions&, const RenderGeometry&,
                       std::uint64_t first_batch, unsigned batch_count);
// The bins contain full-width histogram rows beginning at global histogram y.
// Reconstruct an output row with the original full-frame camera and normalization.
void develop_row(const Flame&, const RenderGeometry&, std::span<const Bin>,
                 std::uint32_t first_histogram_row, int output_y, std::uint64_t samples,
                 std::span<std::array<double, 4>> rgba, bool sixteen_bit, bool transparent,
                 std::stop_token);
} // namespace apo::detail
