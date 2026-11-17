#pragma once

#include "flame.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace apo::gpu {

// orbit.comp uses only Vulkan 1.0 storage buffers and 32-bit integer atomics.
inline constexpr std::uint32_t samples_per_chain = 256;
inline constexpr std::uint32_t burn_in = 32;
inline constexpr std::uint32_t workgroup_size = 64;
inline constexpr std::uint32_t max_transforms = 64;
inline constexpr std::uint32_t header_words = 32;
inline constexpr std::uint32_t transform_words = 24;
inline constexpr std::uint32_t probe_words = 16;

struct Geometry {
    std::uint32_t width = 1, height = 1, padding = 0;
    double ppu = 1, cosine = 1, sine = 0;
};

// Every item is a 32-bit word; floating fields carry IEEE binary32 bits.
// Binding 0: this model. Binding 1: eight uint words per histogram bin, R/G/B/A
// low/high pairs. Binding 2: plotted low/high and invalid low/high. Binding 3:
// probe records. All storage buffers must exist even when their mode is unused.
struct PackedFlame {
    std::vector<std::uint32_t> words;
};

struct PushConstants {
    std::uint32_t mode = 0;
    std::uint32_t chain_base_low = 0, chain_base_high = 0;
    std::uint32_t chain_count = 0;
    std::uint32_t last_chain_samples = samples_per_chain;
    std::uint32_t seed_low = 0, seed_high = 0;
    std::uint32_t reserved = 0;
};
static_assert(sizeof(PushConstants) == 32);

// Returns an empty string when the current FP32 kernel admits the model.
// Validation failures in the Flame itself still throw std::invalid_argument.
std::string support_reason(const Flame &flame);
PackedFlame pack_flame(const Flame &flame, const Geometry &geometry);

// Mode 1 numerical probes execute one transform repeatedly, keeping its
// historical pre-blur ring between applications. Each record is independent.
// Inputs 0..7: transform index, x/y/color float bits, seed low/high, stream low/high.
// Outputs 8..11: x/y/color float bits and the next RNG unit. Input 12: repetitions.
using ProbeRecord = std::array<std::uint32_t, probe_words>;
ProbeRecord make_probe(std::uint32_t transform, Orbit input, std::uint64_t seed,
                       std::uint64_t stream = 0, std::uint32_t repetitions = 1);
Orbit probe_orbit(const ProbeRecord &record);
double probe_next_unit(const ProbeRecord &record);

// Host reference for the shader's explicit portable stream. Default Rng and the
// established CPU renderer retain their MT19937-64 streams.
std::uint32_t mix32(std::uint32_t value);
std::uint32_t portable_seed(std::uint64_t seed, std::uint64_t stream);
std::uint32_t portable_word(std::uint32_t &state);
double portable_unit(std::uint32_t &state);

} // namespace apo::gpu
