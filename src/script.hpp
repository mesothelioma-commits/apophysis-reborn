#pragma once

#include "legacy_document.hpp"
#include "render_file.hpp"

#include <cstdint>
#include <map>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace apo {

enum class ScriptLanguage { lua, legacy_pascal };
enum class ScriptRequestKind {
  render,
  export_library,
  export_flame,
  export_palette
};

struct ScriptLimits {
  std::uint64_t instructions = 2'000'000;
  std::uint64_t lua_memory_bytes = 32 * 1024 * 1024;
  std::uint64_t host_memory_bytes = 64 * 1024 * 1024;
  std::uint64_t timeout_ms = 10'000;
  std::size_t source_bytes = 1024 * 1024;
  std::size_t document_bytes = 16 * 1024 * 1024;
  std::size_t console_bytes = 64 * 1024;
  std::size_t requests = 64;
  std::size_t host_calls = 10'000;
  std::size_t flame_count = 1024;
  std::size_t transforms_per_flame = 256;
};
struct ScriptOptions {
  ScriptLanguage language = ScriptLanguage::lua;
  std::uint64_t seed = 123456;
  std::stop_token stop_token;
  ScriptLimits limits;
  bool allow_render_requests = false;
  bool allow_export_requests = false;
  // Explicit read capabilities: the host supplies named document bytes. Scripts
  // never open paths; an absent name is inaccessible even if a file exists.
  std::map<std::string, std::string> imports;
};
struct ScriptRequest {
  ScriptRequestKind kind = ScriptRequestKind::render;
  // A safe relative path, resolved by the host inside its chosen output root.
  std::filesystem::path destination;
  Flame flame;
  RenderOptions render;
  FileRenderOptions file;
  std::string bytes; // Captured export bytes; empty for a render request.
};
struct ScriptResult {
  std::string document_bytes;
  std::size_t selected_index = 0;
  std::vector<std::string> console;
  std::vector<ScriptRequest> requests;
  std::uint64_t instructions = 0;
  // On success, VM instructions are in [instructions, instructions + quantum).
  std::uint32_t instruction_quantum = 1000;
  std::uint64_t peak_lua_bytes = 0;
  std::uint64_t peak_host_bytes = 0;
  // Legacy translation is available for review. It is empty for modern Lua.
  std::string translated_source;
  std::vector<std::string> compatibility_notes;
};
class ScriptError : public std::runtime_error {
public:
  explicit ScriptError(std::string message, std::size_t line = 0)
      : std::runtime_error(std::move(message)), line_(line) {}
  std::size_t line() const noexcept { return line_; }

private:
  std::size_t line_;
};
class ScriptCancelled : public std::runtime_error {
public:
  ScriptCancelled() : std::runtime_error("Script execution stopped") {}
};
struct ScriptExample {
  std::string name;
  ScriptLanguage language = ScriptLanguage::lua;
  std::string source;
};

// Runs against private copies. Failure/cancellation returns no partial result;
// this function never modifies the input document or accesses the filesystem.
ScriptResult run_script(const LegacyDocument &document,
                        std::size_t selected_index, std::string_view source,
                        const ScriptOptions &options = {});
std::string_view script_language_name(ScriptLanguage language);
std::string script_api_reference();
std::vector<ScriptExample> script_examples();

} // namespace apo
