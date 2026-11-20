#pragma once

#include "image_stream.hpp"

#include <optional>
#include <stop_token>
#include <string>

namespace apo {
inline constexpr std::size_t editable_xml_limit = 2 * 1024 * 1024;
inline constexpr char editable_png_prefix[] = "flam3_genome\0\0\0\0\0";

struct EmbeddedRenderSettings {
    std::uint64_t seed = 0, samples = 0;
};
struct EditablePng {
    std::string xml;
    std::optional<EmbeddedRenderSettings> settings;
    ImageExport image;
};
// Read only bounded flame data. Validate all chunk CRCs without decoding pixels.
EditablePng read_editable_png(std::istream& input, std::stop_token stop = {});
EditablePng read_editable_png(const std::filesystem::path& path, std::stop_token stop = {});
} // namespace apo
