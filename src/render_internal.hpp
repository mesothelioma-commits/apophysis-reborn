#pragma once
#include "render.hpp"
#include "gpu_kernel.hpp"
#include "density_estimation.hpp"
#include <span>

namespace apo::detail {
inline constexpr std::uint64_t cpu_batch_size = 8192;
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
    std::shared_ptr<const ColorCurveEvaluator> curves;
    DensityPlan density;
    std::shared_ptr<const DensityKernels> density_kernels;
};
struct CpuHits {
    std::vector<SampleHit> hits;
    std::uint64_t invalid = 0;
    unsigned workers = 0;
};
// Admission-only callers can skip constructing tables; storage_bytes still
// accounts for the table that actual reconstruction will need.
RenderGeometry render_geometry(const Flame&, const RenderOptions&, bool prepare_curves = true);
CpuHits sample_cpu_hits(const Flame&, const RenderOptions&, const RenderGeometry&,
                       std::uint64_t first_batch, unsigned batch_count);
// A crop stores only the histogram columns covering its output filter footprints.
struct RowWindow { unsigned first_column = 0, stride = 0; int output_x = 0; };
// Filtering produces four linear-light channels before brightness, gamma,
// vibrancy, curves and background. A proof retains these for re-development.
void filter_row(const Flame&, const RenderGeometry&, std::span<const Bin>, unsigned first_histogram_row,
                int output_y, std::uint64_t samples, std::span<LightBin>, std::stop_token,
                std::span<const LightBin> density_light, RowWindow);
void tone_row(const Flame&, const RenderGeometry&, std::span<LightBin>, bool sixteen_bit,
              bool transparent, std::stop_token);
// A file render supplies the full-width strip beginning at global histogram y.
// Reconstruct an output row with the original full-frame camera and normalization.
void develop_row(const Flame&, const RenderGeometry&, std::span<const Bin>,
                 std::uint32_t first_histogram_row, int output_y, std::uint64_t samples,
                 std::span<std::array<double, 4>> rgba, bool sixteen_bit, bool transparent,
                 std::stop_token, std::span<const LightBin> density_light = {});
} // namespace apo::detail
