#pragma once
#include "gpu.hpp"
#include "gpu_kernel.hpp"

#include <array>
#include <span>
#include <stdexcept>

namespace apo::gpu {
class Unavailable : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};
struct SampledHistogram {
    std::vector<std::array<std::uint64_t, 4>> bins;
    std::uint64_t plotted = 0, invalid = 0;
    std::string device_name;
};
SampledHistogram sample(const Flame &flame, const RenderOptions &options, const Geometry &geometry,
                        std::uint64_t host_reserved_bytes);
std::vector<std::uint32_t> run_probes(const PackedFlame &flame,
                                      std::span<const std::uint32_t> probe_words,
                                      const RenderOptions &options);
} // namespace apo::gpu
