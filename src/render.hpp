#pragma once
#include "flame.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace apo {
struct RenderOptions {
    std::uint64_t samples = 1000000, seed = 123456;
    unsigned threads = 0; // 0 uses available hardware, up to the memory budget.
    std::uint64_t memory_mib = 512; // Histogram, output pixels and worker hit buffers.
};
struct Image {
    int width, height;
    std::vector<std::uint8_t> rgb; // Top-down RGB8, three channels per pixel.
};
struct RenderResult {
    Image image;
    std::uint64_t plotted = 0, invalid = 0;
    unsigned workers = 0;
};
RenderResult render(const Flame& flame, const RenderOptions& options);
std::uint64_t default_samples(const Flame& flame);
void write_image(const std::filesystem::path& output, const Image& image, bool bmp);
}
