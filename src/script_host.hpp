#pragma once
#include "script.hpp"
#include <filesystem>
#include <span>
#include <vector>

namespace apo {
// Resolve all deferred outputs before publishing any. Every output must stay
// beneath an explicitly chosen directory, including existing symlink parents.
std::vector<std::filesystem::path> plan_script_outputs(
    const ScriptResult &result, const std::filesystem::path &directory,
    bool replace_existing = false,
    std::span<const std::filesystem::path> protected_files = {});
void publish_script_export(const ScriptRequest &request,
                           const std::filesystem::path &destination);
} // namespace apo
