#pragma once
#include "flame.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <stop_token>
#include <vector>

namespace apo {
enum class RenderBackend { cpu, automatic, vulkan };
std::string_view backend_name(RenderBackend backend);
struct RenderOptions {
    std::uint64_t samples = 1000000, seed = 123456;
    unsigned threads = 0; // 0 uses available hardware, up to the memory budget.
    std::uint64_t memory_mib = 512; // Histogram, output pixels and worker hit buffers.
    std::stop_token stop_token{};
    // Called serially after committed sample batches. May run on a sampling worker.
    std::function<void(std::uint64_t completed, std::uint64_t total)> progress{};
    RenderBackend backend = RenderBackend::cpu;
    std::string device; // Empty selects a suitable GPU; otherwise discovery ID or decimal index.
    std::uint64_t gpu_memory_mib = 512;
    bool allow_cpu_fallback = true;
};
class RenderCancelled : public std::runtime_error {
public:
    RenderCancelled() : std::runtime_error("Rendering cancelled") {}
};
struct Image {
    int width, height;
    std::vector<std::uint8_t> rgb; // Top-down RGB8, three channels per pixel.
};
struct RenderResult {
    Image image;
    std::uint64_t plotted = 0, invalid = 0;
    unsigned workers = 0;
    RenderBackend backend = RenderBackend::cpu;
    std::string device_name = "CPU";
    std::string fallback_reason;
    double sampling_ms = 0, reconstruction_ms = 0;
};
RenderResult render(const Flame& flame, const RenderOptions& options);
std::uint64_t default_samples(const Flame& flame);
void write_image(const std::filesystem::path& output, const Image& image, bool bmp, std::stop_token stop = {});
}
