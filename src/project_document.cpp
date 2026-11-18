#include "project_document.hpp"
#include <algorithm>
#include <cctype>
#include <exception>
#include <fstream>
#include <stdexcept>

namespace apo {
bool is_project_path(const std::filesystem::path &path) {
  auto extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return extension == ".apoproject";
}
DocumentFile parse_document_file(std::string bytes, bool project_hint) {
  if (bytes.size() > project_file_limit)
    throw std::invalid_argument("The document exceeds the 96 MiB file limit");
  if (project_hint || bytes.find("<apophysis-project") != std::string::npos) {
    // The real XML parser validates the root, attributes and nested content.
    // A marker in a legacy comment must not change the legacy document type.
    try {
      auto project = parse_project(bytes);
      auto document = LegacyDocument::parse(project.library_bytes);
      if (project.selected_index >= document.entries().size())
        throw std::invalid_argument(
            "The project's selected flame does not exist");
      return {std::move(document), std::move(project)};
    } catch (...) {
      if (project_hint)
        throw;
      auto original = std::current_exception();
      try {
        return {LegacyDocument::parse(std::move(bytes)), {}};
      } catch (...) {
        std::rethrow_exception(original);
      }
    }
  }
  return {LegacyDocument::parse(std::move(bytes)), {}};
}
DocumentFile load_document_file(const std::filesystem::path &path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file)
    throw std::runtime_error("Could not open the project or flame file");
  const auto size = file.tellg();
  if (size < 0 || static_cast<std::uintmax_t>(size) > project_file_limit)
    throw std::invalid_argument("The document exceeds the 96 MiB file limit");
  std::string bytes(static_cast<std::size_t>(size), '\0');
  file.seekg(0);
  if (!file.read(bytes.data(), std::streamsize(bytes.size())))
    throw std::runtime_error(
        "Could not read the complete project or flame file");
  return parse_document_file(std::move(bytes), is_project_path(path));
}
} // namespace apo
