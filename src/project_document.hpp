#pragma once
#include "legacy_document.hpp"
#include "project_io.hpp"

namespace apo {
struct DocumentFile {
  LegacyDocument document;
  std::optional<ProjectFile> project;
};
bool is_project_path(const std::filesystem::path &path);
DocumentFile parse_document_file(std::string bytes, bool project_hint = false);
DocumentFile load_document_file(const std::filesystem::path &path);
} // namespace apo
