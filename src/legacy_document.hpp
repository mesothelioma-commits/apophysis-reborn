#pragma once
#include "flame.hpp"

#include <filesystem>
#include <map>

namespace apo {

struct LegacyEntry {
    std::string name, version;
    std::size_t line = 0; // One-based source line at the opening flame element.
    std::map<std::string, std::string> attributes; // Expat-decoded UTF-8.
};

struct LegacyEdits {
    std::optional<std::string> name;
    std::optional<std::array<int, 2>> size;
    std::optional<Point> center;
    std::optional<double> scale;
};

// bytes() is the entire original document, with only requested field edits applied.
// All public index arguments are zero-based. A failed edit leaves bytes and entries intact.
class LegacyDocument {
public:
    static LegacyDocument parse(std::string bytes);
    static LegacyDocument load(const std::filesystem::path& path);
    const std::string& bytes() const { return bytes_; }
    const std::vector<LegacyEntry>& entries() const { return entries_; }
    std::string_view element_bytes(std::size_t index) const; // Source encoding; valid until edit.
    Flame render_model(std::size_t index) const;
    std::optional<std::string> render_issue(std::size_t index) const;
    void edit(std::size_t index, const LegacyEdits& edits);
private:
    LegacyDocument() = default;
    friend class LegacyIndexer;
    struct Range { std::size_t begin, open_end, end; };
    std::string bytes_;
    std::vector<LegacyEntry> entries_;
    std::vector<Range> ranges_;
    enum class Encoding { utf8, ascii, latin1, windows1252, utf16le, utf16be };
    Encoding encoding_ = Encoding::utf8;
    std::string wrapper_issue_;
};
}
