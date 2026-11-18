#include "build_info.hpp"
#include "apo_build_config.hpp"
#include <sstream>

#ifndef APO_BUILD_CONFIGURATION
#define APO_BUILD_CONFIGURATION "Unknown"
#endif

namespace apo {
namespace {
std::string quoted(const std::string &value) {
  static constexpr char hex[] = "0123456789abcdef";
  std::string result = "\"";
  for (const unsigned char c : value) {
    if (c == '"' || c == '\\') { result += '\\'; result += char(c); }
    else if (c < 0x20) {
      result += "\\u00"; result += hex[c >> 4]; result += hex[c & 15];
    } else result += char(c);
  }
  return result + '"';
}
} // namespace

const BuildInfo &build_info() {
  static const BuildInfo info{
      APO_VERSION_STRING, APO_SOURCE_REVISION, APO_SOURCE_ORIGIN,
      APO_SOURCE_DIRTY_KNOWN ? std::optional<bool>(APO_SOURCE_DIRTY) : std::nullopt,
      APO_COMPILER_ID, APO_COMPILER_VERSION, APO_TARGET_OS, APO_TARGET_ARCH,
      APO_BUILD_CONFIGURATION, APO_ENABLE_VULKAN != 0,
      APO_EDITOR_COMPILED != 0, APO_QT_VERSION};
  return info;
}
const char *application_version() { return build_info().version.c_str(); }

std::string build_info_json() {
  const auto &b = build_info();
  std::ostringstream out;
  out << "{\"schema\":\"apophysis-reborn.build-info\",\"schema_version\":1"
      << ",\"version\":" << quoted(b.version)
      << ",\"source_revision\":" << quoted(b.source_revision)
      << ",\"source_origin\":" << quoted(b.source_origin)
      << ",\"source_dirty\":"
      << (b.source_dirty ? (*b.source_dirty ? "true" : "false") : "null")
      << ",\"source_state_scope\":\"CMake configuration\""
      << ",\"compiler\":" << quoted(b.compiler)
      << ",\"compiler_version\":" << quoted(b.compiler_version)
      << ",\"target_os\":" << quoted(b.target_os)
      << ",\"target_arch\":" << quoted(b.target_arch)
      << ",\"pointer_bits\":" << sizeof(void *) * 8
      << ",\"build_type\":" << quoted(b.build_type)
      << ",\"vulkan_compiled\":" << (b.vulkan_compiled ? "true" : "false")
      << ",\"editor_compiled\":" << (b.editor_compiled ? "true" : "false")
      << ",\"qt_version\":" << quoted(b.qt_version) << '}';
  return out.str();
}

std::string build_info_text() {
  const auto &b = build_info();
  std::ostringstream out;
  out << "Apophysis Reborn " << b.version << '\n'
      << "Source revision: " << b.source_revision << '\n'
      << "Source metadata: " << b.source_origin << "; "
      << (b.source_dirty ? (*b.source_dirty ? "modified" : "clean") : "state unknown")
      << " at CMake configuration\n"
      << "Compiler: " << b.compiler << ' ' << b.compiler_version << '\n'
      << "Target: " << b.target_os << " / " << b.target_arch << " / "
      << sizeof(void *) * 8 << "-bit\n"
      << "Configuration: " << b.build_type << '\n'
      << "CPU renderer: available\nVulkan support compiled: "
      << (b.vulkan_compiled ? "yes" : "no") << '\n'
      << "Editor configured: " << (b.editor_compiled ? "yes" : "no") << '\n';
  if (!b.qt_version.empty()) out << "Qt build version: " << b.qt_version << '\n';
  out << "Runtime device availability is reported separately.\n";
  return out.str();
}
} // namespace apo
