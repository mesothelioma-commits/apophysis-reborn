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
struct SampledHits {
    // Global histogram x/y followed by R/G/B/opacity integer contributions.
    // Stable chain/iteration order; no dense histogram is allocated.
    std::vector<std::array<std::uint32_t, 6>> hits;
    std::uint64_t plotted = 0, invalid = 0;
    std::string device_name;
    std::string checkpoint_identity;
};
// Sample a bounded range of the whole job's 256-sample chains. options.samples
// remains the complete job count, including its possibly partial final chain.
// Returned storage and backend buffers are charged here; host_reserved_bytes
// accounts for other caller-held storage. Progress belongs to the outer job
// after its durable chunk commit, so this function does not invoke progress.
SampledHits sample_hits(const Flame &flame, const RenderOptions &options, const Geometry &geometry,
                        std::uint64_t first_chain, std::uint32_t chain_count,
                        std::uint64_t host_reserved_bytes);

std::vector<std::uint32_t> run_probes(const PackedFlame &flame,
                                      std::span<const std::uint32_t> probe_words,
                                      const RenderOptions &options);
} // namespace apo::gpu
