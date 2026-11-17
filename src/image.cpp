#include "render.hpp"
#include "output_file.hpp"

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <string>

namespace apo {
namespace fs = std::filesystem;
namespace {
void little_endian(std::ostream& out, std::uint32_t value, int bytes) {
    for (int i = 0; i < bytes; ++i) {
        out.put(static_cast<char>(value & 255u));
        value >>= 8;
    }
}
void write_bmp(std::ostream& out, const Image& image, std::stop_token stop) {
    const std::uint64_t stride = (std::uint64_t(image.width) * 3 + 3) / 4 * 4;
    const auto bytes = stride * image.height;
    if (bytes > UINT32_MAX - 54) throw std::invalid_argument("Image exceeds the BMP file size field");
    out.write("BM", 2);
    little_endian(out, static_cast<std::uint32_t>(54 + bytes), 4);
    little_endian(out, 0, 4);
    little_endian(out, 54, 4);
    little_endian(out, 40, 4);
    little_endian(out, image.width, 4);
    little_endian(out, image.height, 4);
    little_endian(out, 1, 2);
    little_endian(out, 24, 2);
    little_endian(out, 0, 4);
    little_endian(out, static_cast<std::uint32_t>(bytes), 4);
    for (int i = 0; i < 4; ++i) little_endian(out, 0, 4);
    std::vector<char> row(static_cast<std::size_t>(stride), 0);
    for (int y = image.height - 1; y >= 0; --y) {
        if (stop.stop_requested()) throw RenderCancelled();
        const auto* src = image.rgb.data() + std::size_t(y) * image.width * 3;
        for (int x = 0; x < image.width; ++x) {
            const auto i = std::size_t(x) * 3;
            row[i] = static_cast<char>(src[i+2]);
            row[i+1] = static_cast<char>(src[i+1]);
            row[i+2] = static_cast<char>(src[i]);
        }
        out.write(row.data(), static_cast<std::streamsize>(row.size()));
    }
}
}
void write_image(const fs::path& output, const Image& image, bool bmp, std::stop_token stop) {
    const auto check = [&] { if (stop.stop_requested()) throw RenderCancelled(); };
    check();
    write_atomic(output, [&](std::ostream& out) {
        if (bmp) {
            write_bmp(out, image, stop);
        } else {
            out << "P6\n" << image.width << " " << image.height << "\n255\n";
            const std::size_t stride = std::size_t(image.width) * 3;
            for (int y = 0; y < image.height; ++y) {
                check();
                out.write(reinterpret_cast<const char*>(image.rgb.data() + std::size_t(y)*stride),
                          static_cast<std::streamsize>(stride));
            }
        }
    }, check);
}
}
