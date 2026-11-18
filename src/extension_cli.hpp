#pragma once
#include <filesystem>
#include <optional>
#include <ostream>
#include <vector>
namespace apo {
std::optional<int>
extension_command(const std::vector<std::filesystem::path> &args);
void extension_usage(std::ostream &out);
} // namespace apo
