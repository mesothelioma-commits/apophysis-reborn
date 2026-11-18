#include "image_stream.hpp"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace apo {
namespace {

constexpr std::uint64_t compressed_buffer_bytes = 65536;
constexpr std::uint32_t tiff_strip_rows = 32;

bool png(ImageFormat format) { return format == ImageFormat::png8 || format == ImageFormat::png16; }
bool tiff(ImageFormat format) {
    return format == ImageFormat::tiff8 || format == ImageFormat::tiff16;
}
std::uint64_t add(std::uint64_t a, std::uint64_t b) {
    if (b > UINT64_MAX - a)
        throw std::invalid_argument("Image file size overflows");
    return a + b;
}
std::uint64_t multiply(std::uint64_t a, std::uint64_t b) {
    if (b && a > UINT64_MAX / b)
        throw std::invalid_argument("Image file size overflows");
    return a * b;
}
std::string lower(std::string_view value) {
    std::string text(value);
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    return text;
}
void validate_export(const ImageExport &options) {
    (void)image_format_bits(options.format);
    if (options.alpha && !image_format_supports_alpha(options.format))
        throw std::invalid_argument("Transparency requires PNG or TIFF output");
}
std::uint64_t row_bytes(int width, const ImageExport &options) {
    validate_export(options);
    if (width <= 0)
        throw std::invalid_argument("Image width must be positive");
    const auto samples = multiply(std::uint64_t(width), options.alpha ? 4 : 3);
    const auto raw = multiply(samples, image_format_bits(options.format) / 8);
    if (options.format == ImageFormat::bmp8)
        return (raw + 3) / 4 * 4;
    if (png(options.format)) {
        const auto filtered = add(raw, 1);
        if (filtered > std::numeric_limits<uInt>::max())
            throw std::invalid_argument("PNG row exceeds the streaming compressor input range");
        return filtered;
    }
    return raw;
}

struct TiffLayout {
    std::uint32_t strips, bits_offset, sample_format_offset, resolution_offset;
    std::uint32_t offsets_offset, counts_offset, pixels_offset;
    std::uint16_t entries;
    std::uint64_t row, bytes;
};
TiffLayout tiff_layout(int width, int height, const ImageExport &options) {
    if (height <= 0)
        throw std::invalid_argument("Image height must be positive");
    const auto row = row_bytes(width, options);
    const auto channels = options.alpha ? 4u : 3u;
    const auto strips = (std::uint64_t(height) + tiff_strip_rows - 1) / tiff_strip_rows;
    const auto entries = std::uint16_t(options.alpha ? 16 : 15);
    const auto bits = 8u + 2u + std::uint64_t(entries) * 12u + 4u;
    const auto sample_format = bits + channels * 2u;
    const auto resolution = sample_format + channels * 2u;
    const auto offsets = resolution + 16u;
    const auto counts = offsets + (strips > 1 ? strips * 4u : 0);
    const auto pixels = counts + (strips > 1 ? strips * 4u : 0);
    const auto bytes = add(pixels, multiply(row, height));
    if (bytes > UINT32_MAX)
        throw std::invalid_argument(
            "Image exceeds classic TIFF's 32-bit file offsets; choose PNG or PPM");
    return {std::uint32_t(strips),
            std::uint32_t(bits),
            std::uint32_t(sample_format),
            std::uint32_t(resolution),
            std::uint32_t(offsets),
            std::uint32_t(counts),
            std::uint32_t(pixels),
            entries,
            row,
            bytes};
}

void bytes(std::ostream &output, const void *data, std::size_t size) {
    if (size)
        output.write(static_cast<const char *>(data), static_cast<std::streamsize>(size));
    if (!output)
        throw std::runtime_error("Image output write failed");
}
void le(std::ostream &output, std::uint32_t value, unsigned count) {
    std::array<unsigned char, 4> data{};
    for (unsigned i = 0; i < count; ++i)
        data[i] = static_cast<unsigned char>(value >> (8 * i));
    bytes(output, data.data(), count);
}
void be(std::ostream &output, std::uint32_t value) {
    const std::array<unsigned char, 4> data{
        static_cast<unsigned char>(value >> 24), static_cast<unsigned char>(value >> 16),
        static_cast<unsigned char>(value >> 8), static_cast<unsigned char>(value)};
    bytes(output, data.data(), data.size());
}
void png_chunk(std::ostream &output, const char *type, std::span<const unsigned char> data = {}) {
    be(output, static_cast<std::uint32_t>(data.size()));
    bytes(output, type, 4);
    bytes(output, data.data(), data.size());
    auto crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, reinterpret_cast<const Bytef *>(type), 4);
    if (!data.empty())
        crc = crc32(crc, data.data(), static_cast<uInt>(data.size()));
    be(output, static_cast<std::uint32_t>(crc));
}

struct Deflater {
    z_stream stream{};
    Deflater() {
        // A zlib wrapper, 32 KiB window and memLevel 8 are valid PNG method 0.
        if (deflateInit2(&stream, 6, Z_DEFLATED, 15, 8, Z_DEFAULT_STRATEGY) != Z_OK)
            throw std::runtime_error("Cannot initialize PNG compression");
    }
    ~Deflater() { deflateEnd(&stream); }
};

} // namespace

std::string_view image_format_name(ImageFormat format) {
    switch (format) {
    case ImageFormat::bmp8:
        return "bmp8";
    case ImageFormat::ppm8:
        return "ppm8";
    case ImageFormat::ppm16:
        return "ppm16";
    case ImageFormat::png8:
        return "png8";
    case ImageFormat::png16:
        return "png16";
    case ImageFormat::tiff8:
        return "tiff8";
    case ImageFormat::tiff16:
        return "tiff16";
    }
    throw std::invalid_argument("Unknown image format");
}
std::string_view image_format_extension(ImageFormat format) {
    switch (format) {
    case ImageFormat::bmp8:
        return ".bmp";
    case ImageFormat::ppm8:
    case ImageFormat::ppm16:
        return ".ppm";
    case ImageFormat::png8:
    case ImageFormat::png16:
        return ".png";
    case ImageFormat::tiff8:
    case ImageFormat::tiff16:
        return ".tiff";
    }
    throw std::invalid_argument("Unknown image format");
}
unsigned image_format_bits(ImageFormat format) {
    switch (format) {
    case ImageFormat::bmp8:
    case ImageFormat::ppm8:
    case ImageFormat::png8:
    case ImageFormat::tiff8:
        return 8;
    case ImageFormat::ppm16:
    case ImageFormat::png16:
    case ImageFormat::tiff16:
        return 16;
    }
    throw std::invalid_argument("Unknown image format");
}
bool image_format_supports_alpha(ImageFormat format) {
    (void)image_format_bits(format);
    return png(format) || tiff(format);
}
ImageFormat parse_image_format(std::string_view name) {
    const auto value = lower(name);
    if (value == "bmp" || value == "bmp8")
        return ImageFormat::bmp8;
    if (value == "ppm" || value == "ppm8")
        return ImageFormat::ppm8;
    if (value == "ppm16")
        return ImageFormat::ppm16;
    if (value == "png" || value == "png8")
        return ImageFormat::png8;
    if (value == "png16")
        return ImageFormat::png16;
    if (value == "tif" || value == "tiff" || value == "tif8" || value == "tiff8")
        return ImageFormat::tiff8;
    if (value == "tif16" || value == "tiff16")
        return ImageFormat::tiff16;
    throw std::invalid_argument("Choose bmp8, ppm8, ppm16, png8, png16, tiff8 or tiff16");
}
ImageFormat image_format_for_path(const std::filesystem::path &path, unsigned depth) {
    if (depth != 8 && depth != 16)
        throw std::invalid_argument("Output bit depth must be 8 or 16");
    const auto extension = lower(path.extension().string());
    if (extension == ".bmp") {
        if (depth != 8)
            throw std::invalid_argument("BMP output supports 8-bit RGB channels; "
                                        "choose PNG, TIFF or PPM for 16-bit");
        return ImageFormat::bmp8;
    }
    if (extension == ".ppm")
        return depth == 8 ? ImageFormat::ppm8 : ImageFormat::ppm16;
    if (extension == ".png")
        return depth == 8 ? ImageFormat::png8 : ImageFormat::png16;
    if (extension == ".tif" || extension == ".tiff")
        return depth == 8 ? ImageFormat::tiff8 : ImageFormat::tiff16;
    throw std::invalid_argument("Use .bmp, .ppm, .png, .tif or .tiff for output");
}

std::uint64_t image_export_upper_bound(int width, int height, const ImageExport &options) {
    const auto row = row_bytes(width, options);
    if (height <= 0)
        throw std::invalid_argument("Image height must be positive");
    const auto raw = multiply(row, height);
    if (options.format == ImageFormat::bmp8) {
        const auto total = add(raw, 54);
        if (total > UINT32_MAX)
            throw std::invalid_argument("Image exceeds the BMP 32-bit file size field");
        return total;
    }
    if (tiff(options.format))
        return tiff_layout(width, height, options).bytes;
    if (png(options.format)) {
        // Conservative zlib bound for this fixed configuration using only
        // Z_NO_FLUSH and Z_FINISH, with room for each emitted IDAT's framing.
        const auto compressed = add(add(add(raw, (raw + 7) / 8), (raw + 63) / 64), 16);
        // A short IDAT may be emitted at each input row as well as every full
        // output buffer. This bound therefore does not assume full-sized chunks.
        const auto chunks = add(add(compressed / compressed_buffer_bytes, height), 2);
        return add(add(compressed, multiply(chunks, 12)), 45);
    }
    return add(raw, 128); // More than the largest P6 dimensions/maxval header.
}

struct ImageStreamWriter::Impl {
    std::ostream &output;
    int width, height, rows = 0;
    ImageExport options;
    unsigned bits, channels;
    bool finished = false, failed = false;
    std::vector<unsigned char> row, compressed;
    std::unique_ptr<Deflater> compressor;

    Impl(std::ostream &stream, int w, int h, const ImageExport &settings)
        : output(stream), width(w), height(h), options(settings),
          bits(image_format_bits(settings.format)), channels(settings.alpha ? 4 : 3) {
        (void)image_export_upper_bound(width, height, options);
        const auto size = row_bytes(width, options);
        if (size > std::numeric_limits<std::size_t>::max() ||
            size > std::uint64_t(std::numeric_limits<std::streamsize>::max()))
            throw std::invalid_argument("Image row exceeds the process address range");
        row.resize(static_cast<std::size_t>(size));
        if (png(options.format)) {
            compressed.resize(compressed_buffer_bytes);
            compressor = std::make_unique<Deflater>();
            const std::array<unsigned char, 8> signature{137, 80, 78, 71, 13, 10, 26, 10};
            bytes(output, signature.data(), signature.size());
            std::array<unsigned char, 13> header{};
            for (unsigned i = 0; i < 4; ++i) {
                header[i] = static_cast<unsigned char>(std::uint32_t(width) >> (24 - 8 * i));
                header[i + 4] = static_cast<unsigned char>(std::uint32_t(height) >> (24 - 8 * i));
            }
            header[8] = static_cast<unsigned char>(bits);
            header[9] = options.alpha ? 6 : 2;
            png_chunk(output, "IHDR", header);
        } else if (options.format == ImageFormat::bmp8) {
            bytes(output, "BM", 2);
            le(output, static_cast<std::uint32_t>(54 + size * height), 4);
            le(output, 0, 4);
            le(output, 54, 4);
            le(output, 40, 4);
            le(output, std::uint32_t(width), 4);
            le(output, static_cast<std::uint32_t>(-std::int64_t(height)), 4);
            le(output, 1, 2);
            le(output, 24, 2);
            le(output, 0, 4);
            le(output, static_cast<std::uint32_t>(size * height), 4);
            for (unsigned i = 0; i < 4; ++i)
                le(output, 0, 4);
        } else if (tiff(options.format)) {
            tiff_header();
        } else {
            const auto header = "P6\n" + std::to_string(width) + " " + std::to_string(height) +
                                "\n" + (bits == 16 ? "65535\n" : "255\n");
            bytes(output, header.data(), header.size());
        }
    }

    void tiff_header() {
        const auto layout = tiff_layout(width, height, options);
        const auto tag = [&](unsigned id, unsigned type, std::uint32_t count, std::uint32_t value) {
            le(output, id, 2);
            le(output, type, 2);
            le(output, count, 4);
            le(output, value, 4);
        };
        bytes(output, "II", 2);
        le(output, 42, 2);
        le(output, 8, 4);
        le(output, layout.entries, 2);
        tag(256, 4, 1, width);
        tag(257, 4, 1, height);
        tag(258, 3, channels, layout.bits_offset);
        tag(259, 3, 1, 1); // No compression.
        tag(262, 3, 1, 2); // RGB.
        tag(273, 4, layout.strips,
            layout.strips == 1 ? layout.pixels_offset : layout.offsets_offset);
        tag(274, 3, 1, 1); // Top-left orientation.
        tag(277, 3, 1, channels);
        tag(278, 4, 1, tiff_strip_rows);
        tag(279, 4, layout.strips,
            layout.strips == 1 ? std::uint32_t(layout.row * height) : layout.counts_offset);
        tag(282, 5, 1, layout.resolution_offset);
        tag(283, 5, 1, layout.resolution_offset + 8);
        tag(284, 3, 1, 1); // Interleaved components.
        tag(296, 3, 1, 1); // No physical resolution unit or invented DPI.
        if (options.alpha)
            tag(338, 3, 1, 2);                              // Unassociated/straight alpha.
        tag(339, 3, channels, layout.sample_format_offset); // Unsigned integer.
        le(output, 0, 4);                                   // No next IFD.
        for (unsigned c = 0; c < channels; ++c)
            le(output, bits, 2);
        for (unsigned c = 0; c < channels; ++c)
            le(output, 1, 2);
        for (unsigned i = 0; i < 4; ++i)
            le(output, 1, 4); // X/Y aspect ratio 1:1.
        if (layout.strips > 1) {
            for (std::uint32_t i = 0; i < layout.strips; ++i)
                le(output,
                   static_cast<std::uint32_t>(layout.pixels_offset +
                                              std::uint64_t(i) * tiff_strip_rows * layout.row),
                   4);
            for (std::uint32_t i = 0; i < layout.strips; ++i) {
                const auto rows_here = std::min<std::uint64_t>(
                    tiff_strip_rows, std::uint64_t(height) - std::uint64_t(i) * tiff_strip_rows);
                le(output, static_cast<std::uint32_t>(rows_here * layout.row), 4);
            }
        }
    }

    void compress_row() {
        auto &stream = compressor->stream;
        stream.next_in = row.data();
        stream.avail_in = static_cast<uInt>(row.size());
        while (stream.avail_in) {
            stream.next_out = compressed.data();
            stream.avail_out = static_cast<uInt>(compressed.size());
            if (deflate(&stream, Z_NO_FLUSH) != Z_OK)
                throw std::runtime_error("PNG compression failed");
            const auto produced = compressed.size() - stream.avail_out;
            if (produced)
                png_chunk(output, "IDAT", {compressed.data(), produced});
        }
    }

    void write(std::span<const std::array<double, 4>> rgba) {
        if (finished || failed)
            throw std::logic_error("Image writer is already finished or failed");
        if (rows >= height)
            throw std::invalid_argument("Too many image rows");
        if (rgba.size() != static_cast<std::size_t>(width))
            throw std::invalid_argument("Image row width does not match the output");
        std::size_t at = png(options.format) ? 1 : 0;
        for (const auto &pixel : rgba) {
            for (double value : pixel)
                if (!std::isfinite(value))
                    throw std::invalid_argument("Image samples must be finite");
            for (unsigned c = 0; c < channels; ++c) {
                const auto component = options.format == ImageFormat::bmp8 ? 2 - c : c;
                const auto quantized = static_cast<std::uint32_t>(std::floor(
                    std::clamp(pixel[component], 0.0, 1.0) * (bits == 16 ? 65535 : 255) + 0.5));
                if (bits == 16) {
                    if (tiff(options.format)) {
                        row[at++] = static_cast<unsigned char>(quantized);
                        row[at++] = static_cast<unsigned char>(quantized >> 8);
                    } else {
                        row[at++] = static_cast<unsigned char>(quantized >> 8);
                        row[at++] = static_cast<unsigned char>(quantized);
                    }
                } else {
                    row[at++] = static_cast<unsigned char>(quantized);
                }
            }
        }
        try {
            if (png(options.format)) {
                row[0] = 1; // Sub filter: no second image row is retained.
                const auto bpp = channels * (bits / 8);
                for (std::size_t i = row.size(); i-- > 1 + bpp;)
                    row[i] = static_cast<unsigned char>(row[i] - row[i - bpp]);
                compress_row();
            } else {
                bytes(output, row.data(), row.size());
            }
            ++rows;
        } catch (...) {
            failed = true;
            throw;
        }
    }

    void finish() {
        if (failed)
            throw std::runtime_error("Image writer previously failed");
        if (finished)
            return;
        if (rows != height)
            throw std::invalid_argument("Image is incomplete: not all rows were written");
        try {
            if (compressor) {
                auto &stream = compressor->stream;
                stream.next_in = Z_NULL;
                stream.avail_in = 0;
                int status;
                do {
                    stream.next_out = compressed.data();
                    stream.avail_out = static_cast<uInt>(compressed.size());
                    status = deflate(&stream, Z_FINISH);
                    if (status != Z_OK && status != Z_STREAM_END)
                        throw std::runtime_error("Cannot complete PNG compression");
                    const auto produced = compressed.size() - stream.avail_out;
                    if (produced)
                        png_chunk(output, "IDAT", {compressed.data(), produced});
                } while (status != Z_STREAM_END);
                png_chunk(output, "IEND");
            }
            output.flush();
            if (!output)
                throw std::runtime_error("Cannot complete image output");
            finished = true;
        } catch (...) {
            failed = true;
            throw;
        }
    }
};

ImageStreamWriter::ImageStreamWriter(std::ostream &output, int width, int height,
                                     const ImageExport &options)
    : impl_(std::make_unique<Impl>(output, width, height, options)) {}
ImageStreamWriter::~ImageStreamWriter() = default;
void ImageStreamWriter::write_row(std::span<const std::array<double, 4>> rgba) {
    impl_->write(rgba);
}
void ImageStreamWriter::finish() { impl_->finish(); }
std::uint64_t ImageStreamWriter::working_bytes(int width, const ImageExport &options) {
    // Fixed zlib configuration uses about 268 KiB. The 512 KiB allowance also
    // covers its 64 KiB output buffer and compressor/object allocation overhead.
    return add(row_bytes(width, options), png(options.format) ? 512u * 1024u : 4096u);
}

} // namespace apo
