#pragma once
#include "flame.hpp"
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace apo {
struct FormatReport {
  std::vector<std::string> warnings, errors;
  bool supported() const { return errors.empty(); }
};
struct PaletteEntry {
  std::string name;
  Palette palette{};
  FormatReport report;
};
// UGR: 400-position, RGB, linear/cyclic gradients. Unsupported opacity and
// smoothing are reported explicitly; malformed source throws invalid_argument.
std::vector<PaletteEntry> import_ugr(std::string_view text);
std::string export_ugr(const Palette &palette, std::string_view name);
Palette import_fractint_map(std::string_view text);
std::string export_fractint_map(const Palette &palette,
                                std::string_view name = {});
struct UprEntry {
  std::string name;
  std::optional<Flame> flame;
  FormatReport report;
  std::string
      source; // Complete original entry, including unsupported formulas.
};
struct UprExport {
  std::string text; // Empty if the compatibility report contains errors.
  FormatReport report;
};
// Only the historical Apophysis flame coloring dialect is supported. These
// functions do not interpret arbitrary Ultra Fractal formulas or layered
// scenes.
std::vector<UprEntry> import_upr(std::string_view text);
UprExport export_upr(const Flame &flame);
} // namespace apo
