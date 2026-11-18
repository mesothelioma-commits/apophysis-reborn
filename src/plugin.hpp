#pragma once
#include "flame.hpp"
#include "plugin_api.h"
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace apo {
struct PluginParameter {
    std::string name;
    double default_value = 0, minimum = 0, maximum = 0;
    bool integer = false;
    bool operator==(const PluginParameter &) const = default;
};
struct PluginInfo {
    std::string id, name, version, description;
    std::filesystem::path manifest_path, library_path;
    std::vector<PluginParameter> parameters;
    std::string fingerprint; // SHA-256 of actual module bytes; not a signature/trust claim.
    bool enabled = false;
};
struct PluginDiscovery {
    std::filesystem::path path;
    std::optional<PluginInfo> info;
    std::string error;
};
class PluginVariation {
  public:
    const PluginInfo &info() const noexcept { return info_; }
    Point evaluate(Point input, const Affine &pre, double weight,
                   std::span<const double> parameters, Rng &rng) const;

  private:
    struct Module;
    PluginInfo info_;
    std::shared_ptr<Module> module_;
    const ApoPluginV1 *api_ = nullptr;
    friend PluginInfo enable_plugin(const std::filesystem::path &, std::string_view);
};
// Inspection/discovery reads sidecar and module bytes; it NEVER loads executable code.
PluginInfo inspect_plugin(const std::filesystem::path &manifest);
std::vector<PluginDiscovery>
discover_plugins(const std::vector<std::filesystem::path> &search_paths);
// Explicit trusted-native-code boundary. Persist expected_fingerprint for approved startup loads.
PluginInfo enable_plugin(const std::filesystem::path &manifest,
                         std::string_view expected_fingerprint = {});
bool disable_plugin(std::string_view variation_name);
std::vector<PluginInfo> enabled_plugins();
std::shared_ptr<const PluginVariation> find_plugin_variation(std::string_view name);
// Installation creates one new subdirectory and never overwrites an existing package.
PluginInfo install_plugin(const std::filesystem::path &manifest,
                          const std::filesystem::path &installation_directory);
// Removal refuses modules retained by any live Flame/queue snapshot, even after disable.
void remove_plugin(const std::filesystem::path &manifest);
Term make_variation_term(std::string_view name, double weight = 1,
                         const std::map<std::string, double> &parameters = {});
std::string_view term_name(const Term &term);
std::map<std::string, double> term_parameters(const Term &term);
void validate_plugin_term(const Term &term);
std::string plugin_identity(const Flame &flame); // Stable module IDs/versions/binary hashes.
bool has_plugin_variations(const Flame &flame);
} // namespace apo
