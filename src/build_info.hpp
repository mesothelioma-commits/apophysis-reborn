#pragma once

#include <optional>
#include <string>

namespace apo {
struct BuildInfo {
  std::string version, source_revision, source_origin;
  std::optional<bool> source_dirty;
  std::string compiler, compiler_version, target_os, target_arch, build_type;
  bool vulkan_compiled = false, editor_compiled = false;
  std::string qt_version;
};

// Identity is captured at CMake configuration, not inferred from the machine
// running a distributed binary. No hostname, username or build paths are kept.
const BuildInfo &build_info();
const char *application_version();
std::string build_info_json();
std::string build_info_text();
} // namespace apo
