#include "extension_cli.hpp"
#include "creative.hpp"
#include "flame_io.hpp"
#include "legacy_formats.hpp"
#include "output_file.hpp"
#include "plugin.hpp"
#include "project_document.hpp"
#include "script_host.hpp"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <csignal>
#include <fstream>
#include <iostream>
#include <set>
#include <thread>

namespace apo {
namespace {
namespace fs = std::filesystem;
std::string utf8(const fs::path &path) {
  const auto s = path.u8string();
  return {s.begin(), s.end()};
}
std::string read(const fs::path &path, std::size_t limit = 16 * 1024 * 1024) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in)
    throw std::runtime_error("Cannot read " + utf8(path));
  const auto size = in.tellg();
  if (size < 0 || std::uintmax_t(size) > limit)
    throw std::invalid_argument("Input exceeds its size limit: " + utf8(path));
  std::string bytes(std::size_t(size), '\0');
  in.seekg(0);
  if (!in.read(bytes.data(), std::streamsize(bytes.size())))
    throw std::runtime_error("Incomplete input: " + utf8(path));
  return bytes;
}
void write(const fs::path &path, std::string_view bytes) {
  write_atomic(path, [&](std::ostream &out) {
    out.write(bytes.data(), std::streamsize(bytes.size()));
  });
}
template <class T> T number(const fs::path &path) {
  const auto s = utf8(path);
  T n{};
  const auto [end, error] = std::from_chars(s.data(), s.data() + s.size(), n);
  if (error != std::errc{} || end != s.data() + s.size() || s.empty())
    throw std::invalid_argument("Invalid integer: " + s);
  return n;
}
std::string extension(const fs::path &path) {
  auto s = path.extension().string();
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return s;
}
void print_report(const FormatReport &report) {
  for (const auto &warning : report.warnings)
    std::cerr << "Warning: " << warning << '\n';
  for (const auto &error : report.errors)
    std::cerr << "Unsupported: " << error << '\n';
}
void output_allowed(const fs::path &path, bool replace,
                    const std::vector<fs::path> &protectedFiles = {}) {
  if (path.empty())
    throw std::invalid_argument("Specify --output PATH");
  if (fs::exists(path) && (!replace || !fs::is_regular_file(path)))
    throw std::invalid_argument(
        "Output already exists; use --overwrite to replace a regular file");
  for (const auto &input : protectedFiles)
    if (!input.empty() && (fs::weakly_canonical(fs::absolute(path)) ==
                               fs::weakly_canonical(fs::absolute(input)) ||
                           (fs::exists(path) && fs::exists(input) &&
                            fs::equivalent(path, input))))
      throw std::invalid_argument(
          "Choose a destination distinct from the input files");
}
volatile std::sig_atomic_t interrupted = 0;
extern "C" void script_interrupt(int s) { interrupted = s; }
struct Interrupts {
  using Handler = void (*)(int);
  Handler a, b;
  Interrupts() {
    interrupted = 0;
    a = std::signal(SIGINT, script_interrupt);
    if (a == SIG_ERR)
      throw std::runtime_error("Cannot install interruption handler");
    b = std::signal(SIGTERM, script_interrupt);
    if (b == SIG_ERR) {
      std::signal(SIGINT, a);
      throw std::runtime_error("Cannot install termination handler");
    }
  }
  ~Interrupts() {
    std::signal(SIGINT, a);
    std::signal(SIGTERM, b);
  }
};
int script_command(const std::vector<fs::path> &args) {
  if (args.size() == 2 && args[1] == "--api") {
    std::cout << script_api_reference() << '\n';
    return 0;
  }
  if (args.size() < 2)
    throw std::invalid_argument("script requires an input flame or project");
  ScriptOptions options;
  fs::path source, output, outputDir;
  std::optional<std::size_t> index;
  bool replace = false, languageSet = false;
  std::vector<fs::path> protectedFiles{args[1]};
  std::set<std::string> seen;
  for (std::size_t i = 2; i < args.size();) {
    const auto key = utf8(args[i++]);
    if (key != "--import" && !seen.insert(key).second)
      throw std::invalid_argument("Duplicate option " + key);
    auto value = [&]() -> const fs::path & {
      if (i == args.size())
        throw std::invalid_argument("Missing value for " + key);
      return args[i++];
    };
    if (key == "--script")
      source = value();
    else if (key == "--output")
      output = value();
    else if (key == "--output-dir")
      outputDir = value();
    else if (key == "--overwrite")
      replace = true;
    else if (key == "--allow-renders")
      options.allow_render_requests = true;
    else if (key == "--allow-exports")
      options.allow_export_requests = true;
    else if (key == "--seed")
      options.seed = number<std::uint64_t>(value());
    else if (key == "--index")
      index = number<std::size_t>(value());
    else if (key == "--instructions")
      options.limits.instructions = number<std::uint64_t>(value());
    else if (key == "--timeout-ms")
      options.limits.timeout_ms = number<std::uint64_t>(value());
    else if (key == "--lua-memory-mib") {
      const auto mib = number<std::uint64_t>(value());
      if (!mib || mib > 256)
        throw std::invalid_argument("Lua memory must be 1..256 MiB");
      options.limits.lua_memory_bytes = mib * 1048576;
    } else if (key == "--language") {
      const auto lang = utf8(value());
      if (lang == "lua")
        options.language = ScriptLanguage::lua;
      else if (lang == "legacy")
        options.language = ScriptLanguage::legacy_pascal;
      else
        throw std::invalid_argument("Language must be lua or legacy");
      languageSet = true;
    } else if (key == "--import") {
      const auto name = utf8(value());
      const auto path = value();
      if (!options.imports.emplace(name, read(path)).second)
        throw std::invalid_argument("Duplicate named import");
      protectedFiles.push_back(path);
    } else
      throw std::invalid_argument("Unknown script option " + key);
  }
  if (source.empty())
    throw std::invalid_argument("Specify --script PATH");
  if (!languageSet && extension(source) != ".lua")
    options.language = ScriptLanguage::legacy_pascal;
  protectedFiles.push_back(source);
  for (const auto &plugin : enabled_plugins()) {
    protectedFiles.push_back(plugin.manifest_path);
    protectedFiles.push_back(plugin.library_path);
  }
  output_allowed(output, replace, protectedFiles);
  auto input = load_document_file(args[1]);
  const auto selected =
      index.value_or(input.project ? input.project->selected_index + 1 : 1);
  if (!selected || selected > input.document.entries().size())
    throw std::invalid_argument("Flame index is out of range");
  const auto script = read(source, options.limits.source_bytes);
  Interrupts signals;
  std::stop_source stop;
  options.stop_token = stop.get_token();
  std::jthread monitor([&](std::stop_token done) {
    while (!done.stop_requested()) {
      if (interrupted) {
        stop.request_stop();
        return;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  });
  auto result = run_script(input.document, selected - 1, script, options);
  for (const auto &line : result.console)
    std::cout << line << '\n';
  for (const auto &note : result.compatibility_notes)
    std::cerr << "Compatibility: " << note << '\n';
  protectedFiles.push_back(output);
  const auto destinations =
      plan_script_outputs(result, outputDir, replace, protectedFiles);
  std::string documentBytes = result.document_bytes;
  if (is_project_path(output)) {
    auto project = input.project.value_or(ProjectFile{});
    project.library_bytes = result.document_bytes;
    project.selected_index = result.selected_index;
    documentBytes = serialize_project(project);
  }
  // All requests and destinations are validated before publishing any file.
  // Files are individually atomic; a later I/O failure cannot undo prior files.
  std::size_t published = 0;
  try {
    for (std::size_t i = 0; i < result.requests.size(); ++i) {
      if (stop.stop_requested())
        throw ScriptCancelled();
      fs::create_directories(destinations[i].parent_path());
      const auto &request = result.requests[i];
      if (request.kind == ScriptRequestKind::render) {
        auto render = request.render;
        render.stop_token = stop.get_token();
        render_to_file(request.flame, render, destinations[i], request.file);
      } else
        publish_script_export(request, destinations[i]);
      ++published;
      std::cout << "Wrote " << utf8(destinations[i]) << '\n';
    }
    if (stop.stop_requested())
      throw ScriptCancelled();
    write(output, documentBytes);
  } catch (...) {
    if (published)
      std::cerr << published
                << " earlier output(s) completed before this failure; the "
                   "edited document was not saved.\n";
    throw;
  }
  std::cout << "Script completed; under "
            << result.instructions + result.instruction_quantum
            << " VM instructions; peak Lua " << result.peak_lua_bytes
            << " bytes; peak host " << result.peak_host_bytes
            << " bytes; saved " << utf8(output) << '\n';
  return 0;
}
} // namespace
void extension_usage(std::ostream &out) {
  out << "Extended compatibility:\n"
      << "  flame-cpu script INPUT --script FILE --output RESULT [--language "
         "lua|legacy]\n"
      << "      [--seed N] [--index N] [--allow-renders] [--allow-exports]\n"
      << "      [--output-dir FOLDER] [--import NAME FILE] [--overwrite]\n"
      << "      [--instructions N] [--timeout-ms N] [--lua-memory-mib N]\n"
      << "  flame-cpu script --api\n"
      << "  flame-cpu project create|inspect|extract INPUT [--output FILE] "
         "[--overwrite]\n"
      << "  flame-cpu palette-import PALETTE --flame INPUT --output RESULT "
         "[--index N] [--palette-index N]\n"
      << "  flame-cpu palette-export INPUT --output palette.ugr|map|gpl "
         "[--index N]\n"
      << "  flame-cpu upr-import INPUT --output RESULT [--index N]\n"
      << "  flame-cpu upr-export INPUT --output RESULT.upr [--index N]\n"
      << "  flame-cpu plugins list FOLDER | info MANIFEST | install MANIFEST "
         "FOLDER | remove MANIFEST\n"
      << "  flame-cpu --plugin TRUSTED_MANIFEST [--plugin TRUSTED_MANIFEST "
         "...] COMMAND...\n"
      << "Native plugins execute trusted code in this process. Project "
         "references do not enable them.\n";
}
std::optional<int> extension_command(const std::vector<fs::path> &args) {
  if (args.empty())
    return {};
  const auto command = utf8(args[0]);
  if (command == "script")
    return script_command(args);
  if (command == "plugins") {
    if (args.size() < 3)
      throw std::invalid_argument("Use plugins list FOLDER, info MANIFEST, "
                                  "install MANIFEST FOLDER or remove MANIFEST");
    auto info = [](const PluginInfo &p) {
      std::cout << p.id << " / " << p.name << " / " << p.version
                << "\n  SHA-256: " << p.fingerprint << "\n  " << p.description
                << '\n';
      for (const auto &field : p.parameters)
        std::cout << "  " << field.name << " = " << field.default_value << " ["
                  << field.minimum << ", " << field.maximum << "]\n";
    };
    if (args[1] == "list" && args.size() == 3) {
      bool errors = false;
      for (const auto &item : discover_plugins({args[2]})) {
        if (item.info)
          info(*item.info);
        else {
          std::cerr << utf8(item.path) << ": " << item.error << '\n';
          errors = true;
        }
      }
      return errors ? 1 : 0;
    }
    if (args[1] == "info" && args.size() == 3) {
      info(inspect_plugin(args[2]));
      return 0;
    }
    if (args[1] == "install" && args.size() == 4) {
      info(install_plugin(args[2], args[3]));
      return 0;
    }
    if (args[1] == "remove" && args.size() == 3) {
      remove_plugin(args[2]);
      std::cout << "Removed plugin package.\n";
      return 0;
    }
    throw std::invalid_argument("Invalid plugins command or arguments");
  }
  const bool project = command == "project",
             paletteImport = command == "palette-import",
             paletteExport = command == "palette-export",
             uprImport = command == "upr-import",
             uprExport = command == "upr-export";
  if (!project && !paletteImport && !paletteExport && !uprImport && !uprExport)
    return {};
  const auto start = project ? 3u : 2u;
  if (args.size() < start)
    throw std::invalid_argument("Missing input file for " + command);
  const auto &input = args[start - 1];
  fs::path output, flamePath;
  std::size_t index = 1, paletteIndex = 1;
  bool replace = false;
  std::set<std::string> seen;
  for (std::size_t i = start; i < args.size();) {
    const auto key = utf8(args[i++]);
    if (!seen.insert(key).second)
      throw std::invalid_argument("Duplicate option " + key);
    auto value = [&]() -> const fs::path & {
      if (i == args.size())
        throw std::invalid_argument("Missing value for " + key);
      return args[i++];
    };
    if (key == "--output")
      output = value();
    else if (key == "--overwrite")
      replace = true;
    else if (key == "--index")
      index = number<std::size_t>(value());
    else if (paletteImport && key == "--palette-index")
      paletteIndex = number<std::size_t>(value());
    else if (paletteImport && key == "--flame")
      flamePath = value();
    else
      throw std::invalid_argument("Unknown option " + key);
  }
  if (project) {
    const auto verb = utf8(args[1]);
    auto file = load_document_file(input);
    if (verb == "inspect") {
      if (!output.empty() || replace || seen.contains("--index"))
        throw std::invalid_argument("project inspect accepts only INPUT");
      if (!file.project)
        throw std::invalid_argument(
            "Input is a flame library, not a native project");
      const auto &p = *file.project;
      std::cout << "Native project version " << p.version
                << "\nFlames: " << file.document.entries().size()
                << "\nSelected: " << p.selected_index + 1
                << "\nRender presets: " << p.render_presets.size()
                << "\nPlugin references: " << p.plugin_references.size()
                << "\nAnimation data: "
                << (p.animation_data ? "preserved opaque data" : "none")
                << '\n';
      return 0;
    }
    output_allowed(output, replace, {input});
    if (verb == "create") {
      if (!is_project_path(output))
        throw std::invalid_argument(
            "Native projects use the .apoproject extension");
      auto p = file.project.value_or(ProjectFile{});
      p.library_bytes = file.document.bytes();
      if (seen.contains("--index")) {
        if (!index || index > file.document.entries().size())
          throw std::invalid_argument("Flame index out of range");
        p.selected_index = index - 1;
      }
      write(output, serialize_project(p));
    } else if (verb == "extract") {
      if (seen.contains("--index"))
        throw std::invalid_argument("project extract writes the full "
                                    "collection and does not accept --index");
      if (!file.project)
        throw std::invalid_argument("Input is not a native project");
      if (is_project_path(output))
        throw std::invalid_argument("Extract to a .flame or .xml file");
      write(output, file.document.bytes());
    } else
      throw std::invalid_argument(
          "Project operation must be create, inspect or extract");
    return 0;
  }
  output_allowed(output, replace, {input, flamePath});
  if (uprImport) {
    const auto entries = import_upr(read(input));
    if (!index || index > entries.size())
      throw std::invalid_argument("UPR entry index out of range");
    const auto &entry = entries[index - 1];
    print_report(entry.report);
    if (!entry.flame || !entry.report.supported())
      throw std::invalid_argument(
          "Selected UPR entry is unsupported; no output was written");
    auto bytes = serialize_flame(*entry.flame);
    if (is_project_path(output)) {
      ProjectFile p;
      p.library_bytes = bytes;
      p.metadata["source-format"] = "Apophysis UPR";
      p.metadata["source-entry"] = entry.source;
      bytes = serialize_project(p);
    }
    write(output, bytes);
    return 0;
  }
  if (paletteImport && flamePath.empty())
    throw std::invalid_argument("palette-import requires --flame INPUT");
  auto file = load_document_file(paletteImport ? flamePath : input);
  if (!seen.contains("--index") && file.project)
    index = file.project->selected_index + 1;
  if (!index || index > file.document.entries().size())
    throw std::invalid_argument("Flame index out of range");
  if (uprExport) {
    auto result = export_upr(file.document.render_model(index - 1));
    print_report(result.report);
    if (!result.report.supported())
      throw std::invalid_argument(
          "Flame cannot be represented by the supported UPR dialect; no output "
          "was written");
    write(output, result.text);
    return 0;
  }
  if (paletteExport) {
    const auto f = file.document.render_model(index - 1);
    const auto ext = extension(output);
    if (ext == ".ugr")
      write(output, export_ugr(f.palette, f.name));
    else if (ext == ".map")
      write(output, export_fractint_map(f.palette, f.name));
    else if (ext == ".gpl")
      write(output, creative::export_gimp_palette(f.palette, f.name));
    else
      throw std::invalid_argument("Palette output must use .ugr, .map or .gpl");
    return 0;
  }
  Palette palette;
  const auto ext = extension(input);
  const auto bytes = read(input);
  if (ext == ".ugr") {
    const auto entries = import_ugr(bytes);
    if (!paletteIndex || paletteIndex > entries.size())
      throw std::invalid_argument("Palette index out of range");
    const auto &entry = entries[paletteIndex - 1];
    print_report(entry.report);
    if (!entry.report.supported())
      throw std::invalid_argument("Selected gradient is unsupported");
    palette = entry.palette;
  } else if (ext == ".map")
    palette = import_fractint_map(bytes);
  else if (ext == ".gpl")
    palette = creative::import_gimp_palette(bytes);
  else
    throw std::invalid_argument("Palette input must use .ugr, .map or .gpl");
  file.document.edit_palette(index - 1, palette);
  std::string result = file.document.bytes();
  if (is_project_path(output)) {
    auto p = file.project.value_or(ProjectFile{});
    p.library_bytes = result;
    p.selected_index = index - 1;
    result = serialize_project(p);
  }
  write(output, result);
  return 0;
}
} // namespace apo
