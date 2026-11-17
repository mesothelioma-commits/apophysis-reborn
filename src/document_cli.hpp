#pragma once
#include <filesystem>
#include <vector>

namespace apo {
int document_command(const std::vector<std::filesystem::path>& args);
}
