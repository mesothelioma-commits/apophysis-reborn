#pragma once
#include "render.hpp"

namespace apo::detail {
// FLAM3's density filter acts on log-exposed, premultiplied histogram bins,
// before the independent spatial reconstruction filter and tone map.
using LightBin = std::array<double, 4>;
struct DensityPlan {
    unsigned levels = 0, radius = 0, camera_margin = 0, count_radius = 0;
    std::uint64_t triangle = 0, table_bytes = 0;
    double max_width = 0, min_width = 0, count_limit = 0, curve = 0;
    std::uint64_t column_bytes(unsigned width) const {
        return count_radius > 1 ? std::uint64_t(width) * sizeof(std::uint64_t) : 0;
    }
};
DensityPlan density_plan(const Flame&);
struct DensityKernels {
    std::vector<unsigned> radii;
    std::vector<double> weights;
    unsigned last = 0;
    DensityKernels(const DensityPlan&, std::stop_token);
};
struct RenderGeometry;
std::vector<LightBin> density_strip(const Flame&, const RenderGeometry&,
    std::span<const std::array<std::uint64_t,4>> bins, unsigned first_input_row,
    unsigned first_output_bin_row, unsigned output_bin_rows, std::uint64_t samples, std::stop_token, unsigned input_width = 0);
}
