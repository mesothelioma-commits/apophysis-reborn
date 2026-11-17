#include "legacy_document.hpp"
#include "flame_io.hpp"

#include <expat.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <exception>
#include <fstream>
#include <iterator>
#include <locale>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace apo {
namespace {
using Text = std::string_view;

// Undefined Windows-1252 bytes are invalid input. Expat uses -1 for unmapped bytes.
constexpr std::array<int, 32> cp1252_high{
    0x20ac,-1,0x201a,0x0192,0x201e,0x2026,0x2020,0x2021,0x02c6,0x2030,0x0160,0x2039,
    0x0152,-1,0x017d,-1,-1,0x2018,0x2019,0x201c,0x201d,0x2022,0x2013,0x2014,
    0x02dc,0x2122,0x0161,0x203a,0x0153,-1,0x017e,0x0178
};
bool ascii_equal(Text a, Text b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
        auto upper = [](char c) { return c >= 'a' && c <= 'z' ? c - 32 : int(c); };
        return upper(x) == upper(y);
    });
}
bool western_encoding(Text name) {
    return ascii_equal(name, "windows-1252") || ascii_equal(name, "cp1252");
}
int western_codepoint(unsigned byte) {
    return byte >= 0x80 && byte < 0xa0 ? cp1252_high[byte - 0x80] : int(byte);
}
void append_utf8(std::string& output, unsigned c) {
    if (c <= 0x7f) output += static_cast<char>(c);
    else if (c <= 0x7ff) {
        output += static_cast<char>(0xc0 | (c >> 6));
        output += static_cast<char>(0x80 | (c & 63));
    } else { // All Windows-1252 and Latin-1 code points are in the BMP.
        output += static_cast<char>(0xe0 | (c >> 12));
        output += static_cast<char>(0x80 | ((c >> 6) & 63));
        output += static_cast<char>(0x80 | (c & 63));
    }
}
bool space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

std::string shortest(double value) {
    std::array<char, 64> buffer{};
    const auto [end, ec] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (ec != std::errc{} || !std::isfinite(value)) throw std::invalid_argument("Camera values must be finite");
    return {buffer.data(), end};
}
std::string escape_attribute(Text utf8, char quote) {
    std::string result;
    for (std::size_t i = 0; i < utf8.size();) {
        const unsigned lead = static_cast<unsigned char>(utf8[i++]);
        unsigned code = lead;
        if (lead >= 0x80) {
            const int n = lead >= 0xf0 ? 3 : lead >= 0xe0 ? 2 : 1;
            if (lead < 0xc2 || lead > 0xf4 || utf8.size() - i < static_cast<unsigned>(n))
                throw std::invalid_argument("Name is not valid UTF-8");
            code &= 0x7f >> (n+1);
            for (int k = 0; k < n; ++k) {
                const unsigned next = static_cast<unsigned char>(utf8[i++]);
                if ((next & 0xc0) != 0x80) throw std::invalid_argument("Name is not valid UTF-8");
                code = code * 64 + (next & 63);
            }
            if ((n == 2 && code < 0x800) || (n == 3 && code < 0x10000))
                throw std::invalid_argument("Name is not valid UTF-8");
        }
        if ((code < 32 && code != 9 && code != 10 && code != 13) ||
            (code >= 0xd800 && code <= 0xdfff) || code == 0xfffe || code == 0xffff || code > 0x10ffff)
            throw std::invalid_argument("Name contains a character unavailable in XML 1.0");
        if (code == '&') result += "&amp;";
        else if (code == '<') result += "&lt;";
        else if (code == static_cast<unsigned>(quote)) result += quote == '"' ? "&quot;" : "&apos;";
        // References work in every supported encoding and preserve whitespace on read.
        else if (code < 32 || code > 126) result += "&#" + std::to_string(code) + ';';
        else result += static_cast<char>(code);
    }
    return result;
}

bool same_field_value(Text field, const std::string& original, const std::string& proposed) {
    if (field == "name") return original == proposed;
    // An explicit edit with the same numeric value preserves signs, padding and entities.
    std::istringstream a(original), b(proposed);
    a.imbue(std::locale::classic());
    b.imbue(std::locale::classic());
    auto equal_numbers = [](Text lhs, Text rhs, auto type) {
        if (lhs.starts_with('+')) lhs.remove_prefix(1);
        using Number = decltype(type);
        Number x{}, y{};
        const auto read = [](Text s, Number& value) {
            if (s.empty()) return false;
            const auto end = s.data() + s.size();
            auto result = std::from_chars(s.data(), end, value);
            return result.ec == std::errc{} && result.ptr == end;
        };
        return read(lhs, x) && read(rhs, y) && x == y;
    };
    std::string x, y;
    while (b >> y) {
        if (!(a >> x)) return false;
        if (!(field == "size" ? equal_numbers(x,y,0) : equal_numbers(x,y,0.0))) return false;
    }
    return !(a >> x);
}
} // namespace

// The XML parser establishes structural boundaries in the original byte stream.
// No DOM serialization participates in saving a legacy file.
class LegacyIndexer {
    LegacyDocument& doc_;
    std::unique_ptr<XML_ParserStruct, decltype(&XML_ParserFree)> parser_{XML_ParserCreate(nullptr), XML_ParserFree};
    std::exception_ptr error_;
    std::size_t depth_ = 0;
    std::optional<std::size_t> active_depth_;
    bool root_flame_ = false;

    template<class Fn>
    static void callback(void* ptr, Fn&& fn) noexcept {
        auto& indexer = *static_cast<LegacyIndexer*>(ptr);
        if (indexer.error_) return;
        try { fn(indexer); }
        catch (...) {
            indexer.error_ = std::current_exception();
            XML_StopParser(indexer.parser_.get(), XML_FALSE);
        }
    }
    std::pair<std::size_t,std::size_t> position() const {
#if XML_MAJOR_VERSION > 2 || (XML_MAJOR_VERSION == 2 && XML_MINOR_VERSION >= 9)
        const auto pos = XML_GetCurrentByteIndex64(parser_.get());
        const auto count = XML_GetCurrentByteCount64(parser_.get());
#else
        const auto pos = XML_GetCurrentByteIndex(parser_.get());
        const auto count = XML_GetCurrentByteCount(parser_.get());
#endif
        if (pos < 0 || std::uint64_t(pos) > doc_.bytes_.size() ||
                std::uint64_t(count) > doc_.bytes_.size() - std::size_t(pos))
            throw std::runtime_error("XML parser returned an invalid source offset");
        return {std::size_t(pos), std::size_t(pos) + std::size_t(count)};
    }
    std::size_t line() const {
#if XML_MAJOR_VERSION > 2 || (XML_MAJOR_VERSION == 2 && XML_MINOR_VERSION >= 9)
        return XML_GetCurrentLineNumber64(parser_.get());
#else
        return XML_GetCurrentLineNumber(parser_.get());
#endif
    }
    void start(Text name, const char** attrs) {
        if (!depth_) {
            // Root markup unambiguously identifies UTF-16 byte order even after a prolog.
            const auto [begin, end] = position();
            if (end-begin >= 2 && doc_.bytes_[begin] == '\0') doc_.encoding_ = LegacyDocument::Encoding::utf16be;
            else if (end-begin >= 2 && doc_.bytes_[begin+1] == '\0') doc_.encoding_ = LegacyDocument::Encoding::utf16le;
            root_flame_ = name == "flame";
            if (!root_flame_) {
                if (name != "flames" && name != "Flames" && name != "pick" && name != "test")
                    doc_.wrapper_issue_ = "Unrecognized collection wrapper <" + std::string(name) + ">";
                for (auto p = attrs; *p && doc_.wrapper_issue_.empty(); p += 2) {
                    const Text attr = *p;
                    if (attr != "name" && attr != "version")
                        doc_.wrapper_issue_ = "Unsupported collection attribute '" + std::string(attr) + "'";
                }
            }
        }
        if (depth_ == 1 && !root_flame_ && name != "flame" && doc_.wrapper_issue_.empty())
            doc_.wrapper_issue_ = "Unsupported collection element <" + std::string(name) + ">";
        if (name == "flame" && (depth_ == 0 || (depth_ == 1 && !root_flame_))) {
            auto& entry = doc_.entries_.emplace_back();
            for (; *attrs; attrs += 2) {
                entry.attributes.emplace(attrs[0], attrs[1]);
                if (Text(attrs[0]) == "name") entry.name = attrs[1];
                if (Text(attrs[0]) == "version") entry.version = attrs[1];
            }
            entry.line = line();
            const auto [begin, open_end] = position();
            doc_.ranges_.push_back({begin, open_end, open_end});
            active_depth_ = depth_;
        }
        ++depth_;
    }
    void end() {
        --depth_;
        if (active_depth_ && *active_depth_ == depth_) {
            doc_.ranges_.back().end = position().second;
            active_depth_.reset();
        }
    }
    void feed(const char* bytes, int count, bool last) {
        if (XML_Parse(parser_.get(), bytes, count, last) == XML_STATUS_OK) return;
        if (error_) std::rethrow_exception(error_);
        throw std::runtime_error("XML line " + std::to_string(line()) + ": " + XML_ErrorString(XML_GetErrorCode(parser_.get())));
    }
public:
    explicit LegacyIndexer(LegacyDocument& doc) : doc_(doc) {
        if (!parser_) throw std::bad_alloc();
        auto p = parser_.get();
        using Encoding = LegacyDocument::Encoding;
        // XML's initial byte pattern identifies byte order even without a declaration.
        Text bytes = doc.bytes_;
        if (bytes.starts_with(std::string_view("\xff\xfe",2)) || bytes.starts_with(std::string_view("<\0",2)))
            doc_.encoding_ = Encoding::utf16le;
        else if (bytes.starts_with(std::string_view("\xfe\xff",2)) || bytes.starts_with(std::string_view("\0<",2)))
            doc_.encoding_ = Encoding::utf16be;
        XML_SetUserData(p, this);
        XML_SetElementHandler(p,
            [](void* d, const char* n, const char** a) { callback(d,[&](auto& s){ s.start(n,a); }); },
            [](void* d, const char*) { callback(d,[](auto& s){ s.end(); }); });
        XML_SetXmlDeclHandler(p, [](void* d,const char*,const char* encoding,int) {
            if (!encoding) return;
            auto& enc = static_cast<LegacyIndexer*>(d)->doc_.encoding_;
            if (ascii_equal(encoding,"ISO-8859-1")) enc = Encoding::latin1;
            else if (ascii_equal(encoding,"US-ASCII")) enc = Encoding::ascii;
            else if (western_encoding(encoding)) enc = Encoding::windows1252;
        });
        XML_SetUnknownEncodingHandler(p, [](void*,const char* name,XML_Encoding* info) -> int {
            if (!western_encoding(name)) return XML_STATUS_ERROR;
            for (unsigned i = 0; i < 256; ++i) info->map[i] = western_codepoint(i);
            info->data = nullptr;
            info->convert = nullptr;
            info->release = nullptr;
            return XML_STATUS_OK;
        }, nullptr);
        XML_SetStartDoctypeDeclHandler(p, [](void* d, const char*, const char*, const char*, int) {
            callback(d,[](auto&) { throw std::runtime_error("DOCTYPE declarations are unsupported"); });
        });
    }
    void index() {
        Text remaining = doc_.bytes_;
        while (!remaining.empty()) {
            const auto n = std::min<std::size_t>(remaining.size(), 32768);
            feed(remaining.data(), int(n), false);
            remaining.remove_prefix(n);
        }
        feed("", 0, true);
        if (doc_.entries_.empty()) throw std::runtime_error("Document contains no direct flame entries");
    }
};

LegacyDocument LegacyDocument::parse(std::string bytes) {
    LegacyDocument doc;
    doc.bytes_ = std::move(bytes);
    LegacyIndexer(doc).index();
    return doc;
}
LegacyDocument LegacyDocument::load(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open flame document: " + path.string());
    std::array<char,32768> buffer;
    std::string bytes;
    while (input.read(buffer.data(), buffer.size()) || input.gcount() != 0)
        bytes.append(buffer.data(), static_cast<std::size_t>(input.gcount()));
    if (!input.eof() || input.bad()) throw std::runtime_error("Cannot read flame document: " + path.string());
    return parse(std::move(bytes));
}
std::string_view LegacyDocument::element_bytes(std::size_t index) const {
    const auto& range = ranges_.at(index);
    return Text(bytes_).substr(range.begin, range.end-range.begin);
}
Flame LegacyDocument::render_model(std::size_t index) const {
    const auto element = element_bytes(index);
    if (!wrapper_issue_.empty()) throw std::runtime_error(wrapper_issue_);
    // Preserve declared single-byte encodings. Standalone UTF-16 elements get their BOM.
    std::string xml;
    switch (encoding_) {
    case Encoding::latin1:
    case Encoding::windows1252:
        xml.reserve(element.size());
        for (unsigned char c : element)
            append_utf8(xml, encoding_ == Encoding::windows1252 ? western_codepoint(c) : c);
        break;
    case Encoding::utf16le: xml = "\xff\xfe"; xml += element; break;
    case Encoding::utf16be: xml = "\xfe\xff"; xml += element; break;
    default: xml = element; break;
    }
    auto flames = parse_flames(xml);
    return std::move(flames.at(0));
}
std::optional<std::string> LegacyDocument::render_issue(std::size_t index) const {
    try { (void)render_model(index); }
    catch (const std::runtime_error& e) { return std::string(e.what()); }
    catch (const std::invalid_argument& e) { return std::string(e.what()); }
    return {};
}

void LegacyDocument::edit(std::size_t index, const LegacyEdits& edits) {
    const auto& range = ranges_.at(index);
    const auto& entry = entries_[index];
    std::map<std::string,std::string> changes;
    if (edits.name) changes["name"] = *edits.name;
    if (edits.size) {
        const auto [w,h] = *edits.size;
        if (w <= 0 || h <= 0) throw std::invalid_argument("Size must contain two positive integers");
        changes["size"] = std::to_string(w) + " " + std::to_string(h);
    }
    if (edits.center) changes["center"] = shortest(edits.center->x) + " " + shortest(edits.center->y);
    if (edits.scale) {
        if (!(*edits.scale > 0)) throw std::invalid_argument("Scale must be positive");
        changes["scale"] = shortest(*edits.scale);
    }
    std::erase_if(changes, [&](const auto& item) {
        auto found = entry.attributes.find(item.first);
        return found != entry.attributes.end() && same_field_value(item.first, found->second, item.second);
    });
    if (changes.empty()) return;

    const bool wide = encoding_ == Encoding::utf16le || encoding_ == Encoding::utf16be;
    const std::size_t step = wide ? 2 : 1;
    const bool little = encoding_ == Encoding::utf16le;
    auto encoded = [=](Text ascii) {
        if (!wide) return std::string(ascii);
        std::string out;
        for (char c : ascii) {
            out += little ? c : '\0';
            out += little ? '\0' : c;
        }
        return out;
    };
    // Tokenize only the Expat-validated opening tag. XML attribute values can contain
    // '>' and the delimiter of the other quote style. Edits replace only value ranges.
    std::string tag;
    for (auto i = range.begin; i < range.open_end; i += step) {
        char c = bytes_[i];
        if (wide) {
            const char high = bytes_[i + (little ? 1 : 0)];
            c = high ? char(0x80) : bytes_[i + (little ? 0 : 1)];
        }
        tag += c;
    }
    struct Patch { std::size_t begin, end; std::string text; };
    std::vector<Patch> patches;
    std::size_t pos = 6; // "<flame"
    while (pos < tag.size()) {
        while (pos < tag.size() && space(tag[pos])) ++pos;
        if (pos == tag.size() || tag[pos] == '>' || tag[pos] == '/') break;
        const auto start = pos;
        while (!space(tag.at(pos)) && tag[pos] != '=') ++pos;
        const auto name = tag.substr(start, pos-start);
        while (space(tag.at(pos))) ++pos;
        ++pos; // equals
        while (space(tag.at(pos))) ++pos;
        const char quote = tag.at(pos++);
        const auto end = tag.find(quote, pos);
        if (end == std::string::npos) throw std::runtime_error("Cannot locate XML attribute delimiter");
        auto change = changes.find(name);
        if (change != changes.end()) {
            patches.push_back({pos, end, escape_attribute(change->second, quote)});
            changes.erase(change);
        }
        pos = end + 1;
    }
    if (!changes.empty()) {
        std::string extra;
        for (const auto& [name, value] : changes)
            extra += " " + name + "=\"" + escape_attribute(value, '"') + "\"";
        patches.push_back({pos, pos, std::move(extra)});
    }
    auto output = bytes_;
    for (auto it = patches.rbegin(); it != patches.rend(); ++it)
        output.replace(range.begin + step * it->begin, step * (it->end - it->begin), encoded(it->text));
    auto updated = parse(std::move(output));
    *this = std::move(updated);
}
}
