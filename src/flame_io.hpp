#pragma once

#include "flame.hpp"

#include <filesystem>

namespace apo {

// Certain historical constructors randomized defaults that can only be recovered from a saved field.
bool needs_explicit_variation_parameter(const VariationParameterInfo& field, Producer producer);

// Functional XML subset for static flames. Parse errors report a line and column.
// Documents are accepted as a unit, so a bad sibling cannot go unnoticed.
std::vector<Flame> parse_flames(std::string_view xml);
std::vector<Flame> load_flames(const std::filesystem::path& path);

// Writes a single flame in canonical UTF-8 XML, with an explicit RGB palette.
enum class FlameXmlMode { canonical, render_snapshot };
std::string serialize_flame(const Flame& flame);
std::string serialize_flame(const Flame& flame, FlameXmlMode mode);

} // namespace apo
