#pragma once
#include <functional>
#include <string>
#include <string_view>
#include <vector>
namespace apo::detail {
struct LegacyTranslation {
  std::string source;
  std::vector<std::size_t> source_lines;
  std::vector<std::string> notes;
};
LegacyTranslation translate_legacy_script(std::string_view source,
                                          const std::function<void()> &check);
} // namespace apo::detail
