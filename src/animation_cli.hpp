#pragma once
#include <filesystem>
#include <optional>
#include <ostream>
#include <vector>
namespace apo {
std::optional<int> animation_command(const std::vector<std::filesystem::path> &args);
void animation_usage(std::ostream &out);
} // namespace apo
