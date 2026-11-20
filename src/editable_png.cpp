#include "editable_png.hpp"
#include "legacy_document.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <zlib.h>

namespace apo {
namespace {
void read_exact(std::istream& in, unsigned char* out, std::size_t size) {
    if (!in.read(reinterpret_cast<char*>(out), static_cast<std::streamsize>(size)))
        throw std::runtime_error("Incomplete PNG file");
}
std::uint64_t number(const unsigned char* p, unsigned size) {
    std::uint64_t result = 0;
    for (unsigned i = 0; i < size; ++i) result = (result << 8) | p[i];
    return result;
}
void parse_text(std::string data, bool international, EditablePng& result) {
    if (!result.xml.empty()) throw std::runtime_error("PNG contains multiple editable flames");
    const auto keyword = data.find('\0');
    auto start = keyword + 1;
    if (international) {
        if (data.size() < start + 2 || data[start] != 0 || data[start + 1] != 0)
            throw std::runtime_error("Editable PNG requires uncompressed flame text");
        start += 2;
        for (int i = 0; i < 2; ++i) {
            const auto end = data.find('\0', start);
            if (end == std::string::npos) throw std::runtime_error("Invalid PNG flame text header");
            start = end + 1;
        }
    }
    if (start == data.size() || data.size() - start > editable_xml_limit)
        throw std::runtime_error("Empty or oversized editable PNG flame");
    data.erase(0, start);
    auto doc = LegacyDocument::parse(std::move(data));
    if (doc.entries().size() != 1) throw std::runtime_error("Editable PNG requires exactly one flame");
    result.xml = doc.standalone_flame(0);
}
}

EditablePng read_editable_png(std::istream& input, std::stop_token stop) {
    using Byte = unsigned char;
    constexpr std::array<Byte, 8> signature{137,80,78,71,13,10,26,10};
    std::array<Byte, 8> head;
    read_exact(input, head.data(), head.size());
    if (head != signature) throw std::runtime_error("This file has an invalid PNG signature");
    EditablePng result;
    std::array<Byte, 32768> buffer;
    bool pixels = false, pixelsEnded = false;
    unsigned color = 0;
    for (unsigned chunk = 0; chunk < 1000000; ++chunk) {
        if (stop.stop_requested()) throw std::runtime_error("Opening PNG cancelled");
        read_exact(input, head.data(), head.size());
        const auto length = number(head.data(), 4);
        const std::string_view type(reinterpret_cast<const char*>(head.data() + 4), 4);
        if (!std::all_of(type.begin(), type.end(), [](char c) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
            }) || (type[2] >= 'a' && type[2] <= 'z'))
            throw std::runtime_error("Invalid PNG chunk type");
        if (type[0] >= 'A' && type[0] <= 'Z' && type != "IHDR" && type != "PLTE" &&
            type != "IDAT" && type != "IEND")
            throw std::runtime_error("Unsupported critical PNG chunk");
        if (length > 0x7fffffffu || (type == "IHDR") != (chunk == 0) ||
            (type == "IHDR" && length != 13) ||
            (type == "IEND" && (!pixels || length)) || (type == "IDAT" && pixelsEnded))
            throw std::runtime_error("Invalid PNG chunk layout");
        if (type == "arRS" && (length != 17 || result.settings))
            throw std::runtime_error("Invalid editable PNG render settings");
        if (type == "IDAT") pixels = true;
        else if (pixels) pixelsEnded = true;
        const bool text = type == "iTXt" || type == "tEXt" || type == "zTXt";
        bool flameText = false;
        std::string payload;
        auto crc = crc32(0, head.data() + 4, 4);
        for (std::uint64_t done = 0; done < length;) {
            if (stop.stop_requested()) throw std::runtime_error("Opening PNG cancelled");
            const auto count = static_cast<unsigned>(std::min<std::uint64_t>(buffer.size(), length - done));
            read_exact(input, buffer.data(), count);
            crc = crc32(crc, buffer.data(), count);
            if (text && !done) {
                constexpr std::string_view keyword("flam3_genome\0", 13);
                flameText = std::string_view(reinterpret_cast<const char*>(buffer.data()), count).starts_with(keyword);
                if (flameText && length > editable_xml_limit + 1024)
                    throw std::runtime_error("Editable PNG metadata exceeds the size limit");
            }
            if (flameText || type == "IHDR" || type == "arRS")
                payload.append(reinterpret_cast<const char*>(buffer.data()), count);
            done += count;
        }
        std::array<Byte, 4> checksum;
        read_exact(input, checksum.data(), checksum.size());
        if (crc != number(checksum.data(), checksum.size())) throw std::runtime_error("PNG chunk checksum mismatch");
        const auto* p = reinterpret_cast<const Byte*>(payload.data());
        if (type == "IHDR") {
            const auto width = number(p, 4), height = number(p + 4, 4);
            const auto depth = p[8];
            color = p[9];
            const bool lowDepth = depth == 1 || depth == 2 || depth == 4;
            const bool validDepth = color == 3 ? (lowDepth || depth == 8) :
                                    (color == 0 || color == 2 || color == 4 || color == 6) &&
                                    (depth == 8 || depth == 16 || (color == 0 && lowDepth));
            if (!width || width > 0x7fffffff || !height || height > 0x7fffffff ||
                !validDepth || p[10] || p[11] || p[12] > 1)
                throw std::runtime_error("Invalid PNG image header");
            result.image.format = depth == 16 ? ImageFormat::png16 : ImageFormat::png8;
            result.image.alpha = color == 4 || color == 6;
        } else if (type == "tRNS") {
            if (pixels || (color != 0 && color != 2 && color != 3))
                throw std::runtime_error("Invalid PNG transparency chunk");
            result.image.alpha = true;
        } else if (type == "arRS") {
            if (p[0] != 1 || !number(p + 9, 8) || number(p + 9, 8) > UINT64_MAX / 16777215)
                throw std::runtime_error("Unsupported editable PNG render settings");
            result.settings = EmbeddedRenderSettings{number(p + 1, 8), number(p + 9, 8)};
        } else if (flameText) {
            if (type == "zTXt") throw std::runtime_error("Editable PNG requires uncompressed flame text");
            parse_text(std::move(payload), type == "iTXt", result);
        } else if (type == "IEND") {
            if (input.peek() != std::istream::traits_type::eof())
                throw std::runtime_error("Unexpected data after PNG end");
            if (result.xml.empty()) throw std::runtime_error(
                "This PNG contains no editable flame. Open a .flame or .apoproject file, or select "
                "Include editable flame in PNG when exporting the image.");
            return result;
        }
    }
    throw std::runtime_error("PNG contains too many chunks");
}
EditablePng read_editable_png(const std::filesystem::path& path, std::stop_token stop) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open PNG for reading");
    return read_editable_png(input, stop);
}
} // namespace apo
