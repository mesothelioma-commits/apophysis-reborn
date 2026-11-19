#include "image_stream.hpp"
#include "editable_png.hpp"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <vector>

namespace {
using namespace apo;
using Pixel = std::array<double, 4>;
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F action, const char *message) {
    try {
        action();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error(message);
}
unsigned char byte(const std::string &data, std::size_t at) {
    return static_cast<unsigned char>(data.at(at));
}
std::uint32_t le(const std::string &data, std::size_t at, unsigned count = 4) {
    std::uint32_t value = 0;
    for (unsigned i = 0; i < count; ++i)
        value |= std::uint32_t(byte(data, at + i)) << (8 * i);
    return value;
}
std::uint32_t be(const std::string &data, std::size_t at, unsigned count = 4) {
    std::uint32_t value = 0;
    for (unsigned i = 0; i < count; ++i)
        value = (value << 8) | byte(data, at + i);
    return value;
}
std::uint32_t crc(const std::string &data, std::size_t at, std::size_t count) {
    std::uint32_t value = 0xffffffffu;
    for (std::size_t i = 0; i < count; ++i) {
        value ^= byte(data, at + i);
        for (unsigned b = 0; b < 8; ++b)
            value = (value >> 1) ^ ((value & 1) ? 0xedb88320u : 0u);
    }
    return value ^ 0xffffffffu;
}

struct Decoded {
    unsigned width, height, bits, channels;
    std::vector<std::uint16_t> samples;
};
Decoded decode_png(const std::string &data) {
    require(data.substr(0, 8) == std::string("\x89PNG\r\n\x1a\n", 8), "PNG signature");
    Decoded result{};
    std::string compressed;
    bool saw_header = false, saw_end = false;
    unsigned idat_count = 0;
    std::size_t at = 8;
    while (at < data.size()) {
        const auto size = be(data, at);
        require(std::uint64_t(at) + size + 12 <= data.size(), "PNG chunk truncated");
        require(crc(data, at + 4, size + 4) == be(data, at + 8 + size), "PNG chunk CRC");
        const auto type = data.substr(at + 4, 4);
        if (type == "IHDR") {
            require(!saw_header && at == 8 && size == 13, "PNG header order/length");
            result.width = be(data, at + 8);
            result.height = be(data, at + 12);
            result.bits = byte(data, at + 16);
            result.channels = byte(data, at + 17) == 6 ? 4 : 3;
            require(byte(data, at + 17) == 2 || byte(data, at + 17) == 6, "PNG color type");
            require(byte(data, at + 18) == 0 && byte(data, at + 19) == 0 &&
                        byte(data, at + 20) == 0,
                    "PNG methods must be standard/noninterlaced");
            saw_header = true;
        } else if (type == "IDAT") {
            require(saw_header && !saw_end, "PNG IDAT order");
            compressed.append(data, at + 8, size);
            ++idat_count;
        } else if (type == "IEND") {
            require(!saw_end && size == 0 && idat_count > 0, "PNG end marker");
            saw_end = true;
        } else {
            throw std::runtime_error("Unexpected chunk or invented color metadata");
        }
        at += size + 12;
        if (saw_end)
            break;
    }
    require(saw_end && at == data.size(), "PNG missing end or trailing data");
    require(result.bits == 8 || result.bits == 16, "PNG sample depth");
    const auto pixel_bytes = result.channels * result.bits / 8;
    const auto stride = std::size_t(result.width) * pixel_bytes;
    std::vector<unsigned char> raw((stride + 1) * result.height);
    z_stream stream{};
    require(inflateInit(&stream) == Z_OK, "PNG decoder initialization");
    stream.next_in = reinterpret_cast<Bytef *>(compressed.data());
    stream.avail_in = static_cast<uInt>(compressed.size());
    stream.next_out = raw.data();
    stream.avail_out = static_cast<uInt>(raw.size());
    const int status = inflate(&stream, Z_FINISH);
    const bool complete = status == Z_STREAM_END && stream.avail_in == 0 && stream.avail_out == 0;
    inflateEnd(&stream);
    require(complete, "PNG deflate stream/Adler/checksum/length");
    for (unsigned y = 0; y < result.height; ++y) {
        auto *row = raw.data() + (stride + 1) * y;
        require(row[0] == 1, "PNG Sub filter expected");
        for (std::size_t i = pixel_bytes; i < stride; ++i)
            row[i + 1] = static_cast<unsigned char>(row[i + 1] + row[i + 1 - pixel_bytes]);
        for (std::size_t i = 0; i < stride;) {
            std::uint16_t sample = row[++i];
            if (result.bits == 16)
                sample = std::uint16_t((sample << 8) | row[++i]);
            result.samples.push_back(sample);
        }
    }
    return result;
}

Decoded decode_tiff(const std::string &data) {
    require(data.substr(0, 2) == "II" && le(data, 2, 2) == 42, "TIFF signature/endianness");
    const auto directory = le(data, 4);
    const auto count = le(data, directory, 2);
    struct Field {
        unsigned type, count, at;
    };
    std::map<unsigned, Field> tags;
    unsigned previous = 0;
    for (unsigned i = 0; i < count; ++i) {
        const auto at = directory + 2 + i * 12;
        const auto id = le(data, at, 2);
        require(id > previous, "TIFF IFD order or duplicate tag");
        previous = id;
        const auto type = le(data, at + 2, 2), n = le(data, at + 4);
        const auto size = type == 3 ? 2u : type == 4 ? 4u : 8u;
        tags[id] = {type, n, size * n <= 4 ? at + 8 : le(data, at + 8)};
    }
    require(le(data, directory + 2 + count * 12) == 0, "TIFF next IFD");
    const auto get = [&](unsigned tag) {
        const auto field = tags.at(tag);
        require(field.type == 3 || field.type == 4, "TIFF integer field expected");
        std::vector<std::uint32_t> values;
        for (unsigned i = 0; i < field.count; ++i)
            values.push_back(
                le(data, field.at + i * (field.type == 3 ? 2u : 4u), field.type == 3 ? 2u : 4u));
        return values;
    };
    Decoded result{get(256).at(0), get(257).at(0), get(258).at(0), get(277).at(0), {}};
    const auto depths = get(258), kinds = get(339);
    require(depths.size() == result.channels && kinds.size() == result.channels,
            "TIFF channel fields");
    require(std::all_of(depths.begin(), depths.end(), [&](auto n) { return n == result.bits; }) &&
                std::all_of(kinds.begin(), kinds.end(), [](auto n) { return n == 1; }),
            "TIFF unsigned uniform samples");
    require(get(259).at(0) == 1 && get(262).at(0) == 2 && get(274).at(0) == 1 &&
                get(284).at(0) == 1,
            "TIFF RGB/uncompressed/top-left/interleaved tags");
    require(get(296).at(0) == 1, "TIFF has no invented physical DPI");
    if (result.channels == 4)
        require(get(338).at(0) == 2, "TIFF straight alpha tag");
    else
        require(!tags.contains(338), "RGB TIFF should not declare alpha");
    const auto offsets = get(273), lengths = get(279);
    const auto rows_per_strip = get(278).at(0);
    require(offsets.size() == lengths.size() &&
                offsets.size() == (result.height + rows_per_strip - 1) / rows_per_strip,
            "TIFF strip count");
    unsigned rows = 0;
    for (std::size_t strip = 0; strip < offsets.size(); ++strip) {
        const auto rows_here = std::min(rows_per_strip, result.height - rows);
        const auto expected =
            std::size_t(result.width) * result.channels * result.bits / 8 * rows_here;
        require(lengths[strip] == expected, "TIFF strip length");
        require(std::uint64_t(offsets[strip]) + expected <= data.size(), "TIFF truncated strip");
        for (std::size_t i = 0; i < expected; i += result.bits / 8)
            result.samples.push_back(
                static_cast<std::uint16_t>(le(data, offsets[strip] + i, result.bits / 8)));
        rows += rows_here;
    }
    require(std::uint64_t(offsets.back()) + lengths.back() == data.size(),
            "TIFF extra or missing raster bytes");
    return result;
}

Decoded decode_ppm(const std::string &data) {
    std::istringstream header(data);
    std::string magic;
    unsigned width, height, maxval;
    header >> magic >> width >> height >> maxval;
    require(magic == "P6" && (maxval == 255 || maxval == 65535) && header.get() == '\n',
            "PPM header");
    const auto at = static_cast<std::size_t>(header.tellg());
    const auto depth = maxval == 255 ? 8u : 16u;
    const auto n = std::size_t(width) * height * 3;
    require(data.size() - at == n * depth / 8, "PPM raster length");
    Decoded result{width, height, depth, 3, {}};
    for (std::size_t i = 0; i < n; ++i)
        result.samples.push_back(
            static_cast<std::uint16_t>(be(data, at + i * depth / 8, depth / 8)));
    return result;
}
Decoded decode_bmp(const std::string &data) {
    require(data.substr(0, 2) == "BM" && le(data, 2) == data.size(), "BMP signature/length");
    require(le(data, 14) == 40 && le(data, 26, 2) == 1 && le(data, 28, 2) == 24 &&
                le(data, 30) == 0,
            "BMP uncompressed RGB24 header");
    const auto width = le(data, 18);
    const auto signed_height = static_cast<std::int32_t>(le(data, 22));
    require(signed_height < 0, "BMP streaming output must be top-down");
    const auto height = static_cast<unsigned>(-std::int64_t(signed_height));
    const auto offset = le(data, 10), stride = (width * 3u + 3u) / 4u * 4u;
    require(data.size() == offset + std::size_t(stride) * height, "BMP raster length");
    Decoded result{width, height, 8, 3, {}};
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x)
            for (unsigned c = 0; c < 3; ++c)
                result.samples.push_back(
                    byte(data, offset + std::size_t(y) * stride + x * 3 + 2 - c));
        for (unsigned i = width * 3; i < stride; ++i)
            require(byte(data, offset + std::size_t(y) * stride + i) == 0, "BMP row padding");
    }
    return result;
}

std::vector<Pixel> fixture(unsigned width, unsigned height) {
    std::vector<Pixel> pixels(std::size_t(width) * height);
    std::uint32_t rng = 0x65fed321;
    for (auto &pixel : pixels)
        for (auto &value : pixel) {
            rng = rng * 1664525u + 1013904223u;
            value = double(rng >> 16) / 65535;
        }
    pixels.front() = {0, 1, 32768.0 / 65535, 0};
    pixels.back() = {1, 0, 1.0 / 65535, 1};
    return pixels;
}
std::string encode(unsigned width, unsigned height, const std::vector<Pixel> &pixels,
                   ImageExport options) {
    std::ostringstream output(std::ios::binary);
    ImageStreamWriter writer(output, int(width), int(height), options);
    for (unsigned y = 0; y < height; ++y)
        writer.write_row({pixels.data() + std::size_t(y) * width, width});
    writer.finish();
    const auto first_size = output.str().size();
    writer.finish();
    require(output.str().size() == first_size, "Repeated finish duplicated output");
    return output.str();
}
void legacy_editable_text() {
    const auto base = encode(1, 1, {{0, 0, 0, 1}}, {ImageFormat::png8, false});
    auto big = [](std::uint32_t n) {
        std::string bytes;
        for (unsigned shift : {24, 16, 8, 0}) bytes += static_cast<char>(n >> shift);
        return bytes;
    };
    auto extract = [&](std::string_view type, const std::string& value) {
        const auto data = std::string(type) + value;
        const auto start = base.find("IDAT") - 4;
        const auto content = big(static_cast<std::uint32_t>(value.size())) + data +
                             big(crc(data, 0, data.size()));
        std::istringstream stream(base.substr(0, start) + content + base.substr(start));
        return read_editable_png(stream);
    };
    const std::string keyword("flam3_genome\0", 13);
    const std::string xml = "<flame name='External'><!--preserved--></flame>";
    const auto imported = extract("tEXt", keyword + xml);
    require(imported.xml == xml && !imported.settings, "Uncompressed third-party flame text");
    auto localized = keyword + std::string("\0\0fr\0flamme\0", 12) + xml;
    require(extract("iTXt", localized).xml == xml, "International text language fields");
    rejects([&] { extract("tEXt", keyword + "<flames>" + xml + xml + "</flames>"); },
            "Multiple-flame metadata accepted");
    rejects([&] { extract("tEXt", keyword + std::string(editable_xml_limit + 1, 'A')); },
            "Oversized flame metadata accepted");
    rejects([&] { extract("tEXt", keyword + "<!DOCTYPE flame SYSTEM 'external.dtd'>" + xml); },
            "Document type declaration accepted in embedded flame");
    rejects([&] { extract("zTXt", keyword + "compressed"); }, "Compressed recipe accepted");
}
void check(const Decoded &decoded, unsigned width, unsigned height,
           const std::vector<Pixel> &pixels, ImageExport settings) {
    require(decoded.width == width && decoded.height == height &&
                decoded.bits == image_format_bits(settings.format) &&
                decoded.channels == (settings.alpha ? 4u : 3u),
            "Decoded shape/depth/channel mismatch");
    require(decoded.samples.size() == pixels.size() * decoded.channels,
            "Decoded sample count mismatch");
    for (std::size_t i = 0; i < pixels.size(); ++i)
        for (unsigned c = 0; c < decoded.channels; ++c) {
            const auto source16 = static_cast<std::uint32_t>(std::lround(pixels[i][c] * 65535));
            const auto expected =
                decoded.bits == 16 ? source16 : (source16 * 255u + 32767u) / 65535u;
            require(decoded.samples[i * decoded.channels + c] == expected,
                    "Decoded color/alpha/sample precision mismatch");
        }
    if (decoded.bits == 16) {
        std::set<std::uint16_t> red;
        for (std::size_t i = 0; i < pixels.size(); ++i)
            red.insert(decoded.samples[i * decoded.channels]);
        if (pixels.size() > 1000)
            require(red.size() > 256, "16-bit output was reduced to RGB8");
        require(decoded.samples[2] == 32768 &&
                    decoded.samples.back() == (settings.alpha ? 65535 : 1),
                "16-bit boundary/least-significant samples");
    }
}

class LimitedBuffer : public std::streambuf {
    std::size_t remaining;

  public:
    explicit LimitedBuffer(std::size_t limit) : remaining(limit) {}
    std::streamsize xsputn(const char *, std::streamsize n) override {
        const auto written = std::min(remaining, static_cast<std::size_t>(n));
        remaining -= written;
        return static_cast<std::streamsize>(written);
    }
    int_type overflow(int_type character) override {
        if (traits_type::eq_int_type(character, traits_type::eof()))
            return traits_type::not_eof(character);
        if (!remaining)
            return traits_type::eof();
        --remaining;
        return character;
    }
};

void validate_contract() {
    std::ostringstream output;
    const std::array<Pixel, 1> one{{{.25, .5, .75, 1}}};
    rejects([&] { ImageStreamWriter bad(output, 0, 1, {}); }, "zero width accepted");
    rejects([&] { ImageStreamWriter bad(output, 1, -1, {}); }, "negative height accepted");
    rejects([&] { ImageStreamWriter bad(output, 1, 1, {ImageFormat::bmp8, true}); },
            "BMP transparency accepted");
    rejects([&] { ImageStreamWriter bad(output, 1, 1, {ImageFormat::ppm16, true}); },
            "PPM transparency accepted");
    rejects([&] { ImageStreamWriter bad(output, 1, 1, {static_cast<ImageFormat>(99), false}); },
            "unknown format accepted");
    rejects([&] { image_export_upper_bound(65536, 65536, {ImageFormat::bmp8, false}); },
            "oversize BMP accepted");
    rejects([&] { image_export_upper_bound(65536, 65536, {ImageFormat::tiff16, true}); },
            "oversize classic TIFF accepted");
    require(image_export_upper_bound(16384, 16384, {ImageFormat::png16, true}) >
                UINT64_C(2147483648),
            "16K high-bit-depth PNG bound overflow");
    require(ImageStreamWriter::working_bytes(16384, {ImageFormat::png16, true}) < 1024 * 1024,
            "PNG writer working memory should stay below 1 MiB at 16K width");
    require(ImageStreamWriter::working_bytes(16384, {ImageFormat::tiff16, true}) < 256 * 1024,
            "TIFF writer working memory should be one encoded row plus fixed "
            "overhead");
    ImageStreamWriter writer(output, 1, 2, {ImageFormat::ppm16, false});
    rejects([&] { writer.finish(); }, "incomplete image accepted");
    rejects([&] { writer.write_row({}); }, "wrong row width accepted");
    auto invalid = one;
    invalid[0][2] = std::numeric_limits<double>::quiet_NaN();
    const auto before = output.str();
    rejects([&] { writer.write_row(invalid); }, "NaN channel accepted");
    require(output.str() == before, "invalid row partially reached destination");
    invalid[0][2] = .75;
    invalid[0][3] = std::numeric_limits<double>::infinity();
    rejects([&] { writer.write_row(invalid); }, "nonfinite unused alpha accepted");
    writer.write_row(one);
    writer.write_row(one);
    rejects([&] { writer.write_row(one); }, "extra row accepted");
    writer.finish();
    rejects([&] { writer.write_row(one); }, "write after finish accepted");
    for (auto format : {ImageFormat::ppm8, ImageFormat::png16, ImageFormat::tiff16}) {
        LimitedBuffer limited(format == ImageFormat::ppm8    ? 13
                              : format == ImageFormat::png16 ? 40
                                                             : 30);
        std::ostream failing(&limited); // Do not rely on caller enabling exceptions.
        rejects(
            [&] {
                ImageStreamWriter image(failing, 1, 1, {format, false});
                image.write_row(one);
                image.finish();
            },
            "short output write not reported");
    }
    const std::vector<Pixel> outside{{{-2, 2, .5, -5}}};
    const auto clamped = decode_png(encode(1, 1, outside, {ImageFormat::png16, true}));
    require(clamped.samples == std::vector<std::uint16_t>{0, 65535, 32768, 0},
            "finite channel clamp");
    require(image_format_for_path("image.PNG", 16) == ImageFormat::png16 &&
                image_format_for_path("image.TIF", 8) == ImageFormat::tiff8 &&
                parse_image_format("TIFF16") == ImageFormat::tiff16,
            "format parsing");
    rejects([] { image_format_for_path("image.bmp", 16); }, "BMP16 silently reduced");
    rejects([] { image_format_for_path("image.png", 12); }, "unsupported bit depth accepted");
    rejects([] { image_format_for_path("image.bin"); }, "unknown extension accepted");
}

void repeated_quantization_tests() {
    const auto check_quantization = [](const Decoded &decoded, unsigned width, unsigned height,
                                       const std::vector<Pixel> &pixels, ImageExport settings) {
        const unsigned channels = settings.alpha ? 4 : 3;
        const unsigned bits = image_format_bits(settings.format);
        require(decoded.width == width && decoded.height == height && decoded.bits == bits &&
                    decoded.channels == channels && decoded.samples.size() == pixels.size() * channels,
                "Repeated-pixel output shape/depth/channel mismatch");
        const double maximum = bits == 16 ? 65535 : 255;
        for (std::size_t i = 0; i < pixels.size(); ++i)
            for (unsigned c = 0; c < channels; ++c)
                require(decoded.samples[i * channels + c] ==
                            std::lround(std::clamp(pixels[i][c], 0.0, 1.0) * maximum),
                        "Repeated-pixel clamp/rounding/channel mismatch");
    };
    const double half_step = (12345.0 + .5) / 65535;
    const std::array<Pixel, 10> values{{
        {0, 0, 0, 0}, {-0.0, 0, -0.0, 0}, {0, 0, 0, 1},
        {.12345, .45678, .76543, .25}, {.12345, .45678, .76543, .75},
        {-2, 2, .5, -5}, {1, 1, 1, 1},
        {half_step, .5, 0, 1},
        {std::nextafter(half_step, 0.0), .5, 0, 1},
        {std::nextafter(half_step, 1.0), .5, 0, 1}
    }};
    constexpr unsigned repeats = 9, height = 3;
    const unsigned width = unsigned(values.size()) * repeats;
    std::vector<Pixel> pixels;
    for (unsigned y = 0; y < height; ++y)
        for (const auto &value : values)
            for (unsigned n = 0; n < repeats; ++n)
                pixels.push_back(value);
    for (auto format : {ImageFormat::bmp8, ImageFormat::ppm8, ImageFormat::ppm16,
                        ImageFormat::png8, ImageFormat::png16, ImageFormat::tiff8,
                        ImageFormat::tiff16}) {
        for (bool alpha : {false, true}) {
            if (alpha && !image_format_supports_alpha(format)) continue;
            const ImageExport settings{format, alpha};
            const auto data = encode(width, height, pixels, settings);
            const auto decoded = format == ImageFormat::bmp8 ? decode_bmp(data)
                : (format == ImageFormat::ppm8 || format == ImageFormat::ppm16) ? decode_ppm(data)
                : (format == ImageFormat::png8 || format == ImageFormat::png16) ? decode_png(data)
                : decode_tiff(data);
            check_quantization(decoded, width, height, pixels, settings);
        }
        // A run of identical RGB values must not bypass finite validation of
        // an unused alpha channel. A rejected input row remains retryable.
        std::vector<Pixel> row(17, Pixel{.25, .5, .75, 1});
        std::ostringstream output(std::ios::binary);
        ImageStreamWriter writer(output, 17, 1, {format, false});
        row.back()[3] = std::numeric_limits<double>::quiet_NaN();
        rejects([&] { writer.write_row(row); }, "repeated RGB run accepted nonfinite alpha");
        row.back()[3] = std::numeric_limits<double>::infinity();
        rejects([&] { writer.write_row(row); }, "repeated RGB run accepted infinite alpha");
        row.back()[3] = 1;
        writer.write_row(row);
        writer.finish();
        const auto data = output.str();
        const auto decoded = format == ImageFormat::bmp8 ? decode_bmp(data)
            : (format == ImageFormat::ppm8 || format == ImageFormat::ppm16) ? decode_ppm(data)
            : (format == ImageFormat::png8 || format == ImageFormat::png16) ? decode_png(data)
            : decode_tiff(data);
        check_quantization(decoded, 17, 1, row, {format, false});
    }
}
} // namespace

int main(int argc, char **argv) {
    try {
        std::filesystem::path output_dir;
        if (argc == 3 && std::string_view(argv[1]) == "--write-fixtures") {
            output_dir = argv[2];
            std::filesystem::create_directories(output_dir);
        } else if (argc != 1) {
            throw std::invalid_argument("Use image-stream-tests [--write-fixtures DIRECTORY]");
        }
        const unsigned width = 1025, height = 35;
        const auto pixels = fixture(width, height);
        unsigned combinations = 0;
        for (auto format :
             {ImageFormat::bmp8, ImageFormat::ppm8, ImageFormat::ppm16, ImageFormat::png8,
              ImageFormat::png16, ImageFormat::tiff8, ImageFormat::tiff16}) {
            for (bool alpha : {false, true}) {
                if (alpha && !image_format_supports_alpha(format))
                    continue;
                const ImageExport settings{format, alpha};
                const auto data = encode(width, height, pixels, settings);
                const auto decoded = format == ImageFormat::bmp8 ? decode_bmp(data)
                                     : (format == ImageFormat::ppm8 || format == ImageFormat::ppm16)
                                         ? decode_ppm(data)
                                     : (format == ImageFormat::png8 || format == ImageFormat::png16)
                                         ? decode_png(data)
                                         : decode_tiff(data);
                check(decoded, width, height, pixels, settings);
                require(data.size() <= image_export_upper_bound(width, height, settings),
                        "encoded file exceeds bound");
                if (!output_dir.empty()) {
                    const auto name = std::string(image_format_name(format)) +
                                      (alpha ? "-alpha" : "-rgb") +
                                      std::string(image_format_extension(format));
                    std::ofstream file(output_dir / name, std::ios::binary);
                    file.write(data.data(), static_cast<std::streamsize>(data.size()));
                    require(bool(file), "fixture write failed");
                }
                ++combinations;
            }
        }
        // A single-strip TIFF stores offsets/counts in the IFD values themselves.
        const auto small = fixture(2, 3);
        check(decode_tiff(encode(2, 3, small, {ImageFormat::tiff16, true})), 2, 3, small,
              {ImageFormat::tiff16, true});
        validate_contract();
        legacy_editable_text();
        repeated_quantization_tests();
        std::cout << combinations
                  << " RGB/RGBA format/depth combinations decoded exactly; "
                     "16-bit precision, PNG CRC/deflate, TIFF strips/alpha, row "
                     "limits, repeated pixels and write failures passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
