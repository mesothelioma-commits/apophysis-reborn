#pragma once
#include <filesystem>
#include <functional>
#include <iosfwd>

namespace apo {
void write_atomic(const std::filesystem::path& output, const std::function<void(std::ostream&)>& write);
}
