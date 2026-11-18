#pragma once
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace apo {
using ProjectSettings = std::map<std::string, std::string>;
struct ProjectPreset {
  std::string name;
  ProjectSettings settings;
  bool operator==(const ProjectPreset &) const = default;
};
struct ProjectPluginReference {
  std::string id, version, path, fingerprint;
  ProjectSettings metadata;
  bool operator==(const ProjectPluginReference &) const = default;
};
namespace detail {
struct ProjectSource;
}
// A project contains the original legacy collection, including its source
// encoding. Settings are UTF-8 strings; the owning application validates its
// own setting values. Plugin references are informational. Loading a project
// never loads native code.
struct ProjectFile {
  static constexpr unsigned current_version = 1;
  unsigned version = current_version;
  std::string library_bytes;
  std::size_t selected_index = 0;
  ProjectSettings render_settings, preview_settings, workspace, metadata;
  std::vector<ProjectPreset> render_presets;
  std::vector<ProjectPluginReference> plugin_references;
  std::optional<std::string>
      animation_data; // Opaque bytes, reserved for future animation.
  // Parser-owned source spans retain unrecognized XML and untouched formatting.
  std::shared_ptr<const detail::ProjectSource> retained_source;
};
inline constexpr std::size_t project_file_limit = 96 * 1024 * 1024;
inline constexpr std::size_t project_library_limit = 64 * 1024 * 1024;
ProjectFile parse_project(std::string bytes);
ProjectFile load_project(const std::filesystem::path &path);
std::string serialize_project(const ProjectFile &project);
void save_project(const ProjectFile &project,
                  const std::filesystem::path &path);
} // namespace apo
