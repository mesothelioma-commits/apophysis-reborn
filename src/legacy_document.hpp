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

struct LegacyTransformEdits {
    std::optional<Affine> pre, post;
    std::optional<double> weight, color, color_speed, opacity;
    std::optional<std::vector<Term>> variations;
    std::optional<int> julian_power;
    std::optional<double> julian_dist;
    std::optional<std::vector<double>> chaos;
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
    // Transform operations require a renderable selected flame and commit atomically.
    // final=true addresses its final transform and ignores transform_index.
    void edit_transform(std::size_t index, std::size_t transform_index,
                        const LegacyTransformEdits& edits, bool final = false);
    std::size_t add_transform(std::size_t index, const Transform& transform, bool final = false);
    // Append a clone, split its original weight, and duplicate both directions of chaos.
    std::size_t duplicate_transform(std::size_t index, std::size_t transform_index);
    void remove_transform(std::size_t index, std::size_t transform_index, bool final = false);
    void move_transform(std::size_t index, std::size_t from, std::size_t to);
private:
    LegacyDocument() = default;
    friend class LegacyIndexer;
    struct Range { std::size_t begin, open_end, end; };
    struct TransformSource {
        Range range;
        std::map<std::string, std::string> attributes;
    };
    struct EntrySource {
        std::vector<TransformSource> transforms;
        std::optional<TransformSource> final_transform;
    };
    struct Patch { std::size_t begin, end; std::string text; };
    using AttributeChanges = std::map<std::string, std::optional<std::string>>;
    std::string encode_ascii(std::string_view ascii) const;
    void attribute_patches(const Range& range, const std::map<std::string, std::string>& attributes,
                           AttributeChanges changes, std::vector<Patch>& patches) const;
    std::string edited_element(const TransformSource& source, const AttributeChanges& changes) const;
    void commit(std::vector<Patch> patches, std::optional<std::size_t> render_index = {});
    std::string bytes_;
    std::vector<LegacyEntry> entries_;
    std::vector<Range> ranges_;
    std::vector<EntrySource> sources_;
    enum class Encoding { utf8, ascii, latin1, windows1252, utf16le, utf16be };
    Encoding encoding_ = Encoding::utf8;
    std::string wrapper_issue_;
};
}
