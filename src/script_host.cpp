#include "script_host.hpp"
#include "output_file.hpp"
#include <algorithm>
#include <cctype>
#include <climits>
#include <ostream>
#include <set>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace apo {
namespace {
namespace fs = std::filesystem;
std::string portable_name(const fs::path &path) {
  const auto text = path.u8string();
  return {text.begin(), text.end()};
}
void validate_relative(const fs::path &path) {
  const auto text = portable_name(path);
  if (text.empty() || text.size() > 1024 || path.is_absolute() ||
      path.has_root_name() || path.has_root_directory() ||
      text.find_first_of("\\:") != std::string::npos)
    throw std::invalid_argument(
        "Script output must be a relative path within the chosen folder");
  for (const auto &part : path) {
    auto name = portable_name(part);
    if (name.empty() || name == "." || name == ".." || name.back() == '.' ||
        name.back() == ' ' ||
        name.find_first_of("<>\"|?*") != std::string::npos ||
        std::any_of(name.begin(), name.end(),
                    [](unsigned char c) { return c < 32; }))
      throw std::invalid_argument(
          "Script output contains an unsupported path component");
    name.resize(name.find('.') == std::string::npos ? name.size()
                                                    : name.find('.'));
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    if (name == "CON" || name == "PRN" || name == "AUX" || name == "NUL" ||
        (name.size() == 4 &&
         (name.starts_with("COM") || name.starts_with("LPT")) &&
         name.back() >= '1' && name.back() <= '9'))
      throw std::invalid_argument(
          "Script output uses a reserved device filename");
  }
}
bool same_path(const fs::path &a, const fs::path &b) {
#ifdef _WIN32
  const auto &x = a.native();
  const auto &y = b.native();
  if (x.size() > INT_MAX || y.size() > INT_MAX)
    throw std::invalid_argument("Script path is too long");
  return CompareStringOrdinal(x.data(), int(x.size()), y.data(), int(y.size()),
                              TRUE) == CSTR_EQUAL;
#else
  return a == b;
#endif
}
bool beneath(const fs::path &parent, const fs::path &child) {
  auto p = parent.begin(), c = child.begin();
  for (; p != parent.end(); ++p, ++c)
    if (c == child.end() || !same_path(*p, *c))
      return false;
  return c != child.end();
}
} // namespace
std::vector<std::filesystem::path>
plan_script_outputs(const ScriptResult &result,
                    const std::filesystem::path &directory,
                    bool replace_existing,
                    std::span<const std::filesystem::path> protected_files) {
  if (result.requests.empty())
    return {};
  if (directory.empty() || !fs::is_directory(directory))
    throw std::invalid_argument(
        "Choose an existing output folder for the script's requests");
  const auto root = fs::canonical(directory);
  std::vector<fs::path> paths;
  paths.reserve(result.requests.size());
  for (const auto &request : result.requests) {
    validate_relative(request.destination);
    const auto destination = fs::weakly_canonical(root / request.destination);
    if (!beneath(root, destination))
      throw std::invalid_argument(
          "Script output resolves outside the permitted folder");
    for (const auto &other : paths)
      if (same_path(destination, other) || beneath(destination, other) ||
          beneath(other, destination) ||
          (fs::exists(destination) && fs::exists(other) &&
           fs::equivalent(destination, other)))
        throw std::invalid_argument(
            "Script output destinations collide or contain one another");
    for (const auto &protected_file : protected_files) {
      if (protected_file.empty())
        continue;
      const auto protected_path =
          fs::weakly_canonical(fs::absolute(protected_file));
      if (same_path(destination, protected_path) ||
          beneath(protected_path, destination) ||
          beneath(destination, protected_path) ||
          (fs::exists(destination) && fs::exists(protected_path) &&
           fs::equivalent(destination, protected_path)))
        throw std::invalid_argument("A script output would replace its input, "
                                    "script or document destination");
    }
    if (fs::exists(destination) &&
        (!replace_existing || !fs::is_regular_file(destination)))
      throw std::invalid_argument("A script output already exists; choose a "
                                  "new name or allow replacement");
    for (auto parent = destination.parent_path(); parent != root;
         parent = parent.parent_path()) {
      if (parent.empty())
        throw std::invalid_argument("Invalid script output directory");
      if (fs::exists(parent) && !fs::is_directory(parent))
        throw std::invalid_argument(
            "A script output parent is not a directory");
    }
    switch (request.kind) {
    case ScriptRequestKind::render:
      if (request.file.resume || !request.file.checkpoint_directory.empty())
        throw std::invalid_argument("Scripts cannot supply checkpoint paths; "
                                    "the host owns render checkpoints");
      if (image_format_for_path(destination,
                                image_format_bits(request.file.image.format)) !=
          request.file.image.format)
        throw std::invalid_argument(
            "A script render filename does not match its image format");
      plan_file_render(request.flame, request.render, request.file);
      break;
    case ScriptRequestKind::export_library:
    case ScriptRequestKind::export_flame:
    case ScriptRequestKind::export_palette:
      if (request.bytes.empty())
        throw std::invalid_argument("A script export is empty");
      break;
    default:
      throw std::invalid_argument("Unknown script output request");
    }
    paths.push_back(destination);
  }
  return paths;
}
void publish_script_export(const ScriptRequest &request,
                           const std::filesystem::path &destination) {
  if (request.kind == ScriptRequestKind::render)
    throw std::invalid_argument(
        "Render requests must use the renderer or queue");
  write_atomic(destination, [&](std::ostream &out) {
    out.write(request.bytes.data(), std::streamsize(request.bytes.size()));
  });
}
} // namespace apo
