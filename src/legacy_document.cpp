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
#include <numeric>
#include <numbers>
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
    if (ec != std::errc{} || !std::isfinite(value)) throw std::invalid_argument("Numeric values must be finite");
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

// A canonical UTF-8 fragment is already escaped XML. Convert only its Unicode
// characters to references so it can be inserted into any supported encoding.
std::string ascii_xml(Text utf8) {
    std::string result;
    for (std::size_t i = 0; i < utf8.size();) {
        const auto lead = static_cast<unsigned char>(utf8[i]);
        if (lead < 0x80) { result += utf8[i++]; continue; }
        const std::size_t length = lead >= 0xf0 ? 4 : lead >= 0xe0 ? 3 : 2;
        result += escape_attribute(utf8.substr(i, length), '"');
        i += length;
    }
    return result;
}

bool same_field_value(Text field, const std::string& original, const std::string& proposed) {
    if (original == proposed) return true;
    if (field == "name") return false;
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
    std::optional<std::size_t> transform_depth_;
    std::optional<std::size_t> palette_depth_;
    bool active_final_ = false;
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
            doc_.root_is_flame_ = root_flame_;
            doc_.root_range_ = {begin, end, end, end};
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
            doc_.sources_.emplace_back();
            active_depth_ = depth_;
        } else if (active_depth_ && depth_ == *active_depth_ + 1 &&
                   (name == "xform" || name == "finalxform")) {
            const auto [begin, open_end] = position();
            LegacyDocument::TransformSource source{{begin, open_end, open_end}, {}};
            for (; *attrs; attrs += 2) source.attributes.emplace(attrs[0], attrs[1]);
            active_final_ = name == "finalxform";
            if (active_final_) {
                if (!doc_.sources_.back().final_transform) {
                    doc_.sources_.back().final_transform = std::move(source);
                    transform_depth_ = depth_;
                }
            } else {
                doc_.sources_.back().transforms.push_back(std::move(source));
                transform_depth_ = depth_;
            }
        } else if (active_depth_ && depth_ == *active_depth_ + 1 &&
                   (name == "palette" || name == "color" || name == "colors")) {
            const auto [begin, open_end] = position();
            doc_.sources_.back().palettes.push_back({{begin, open_end, open_end, open_end}, {}});
            palette_depth_ = depth_;
        }
        ++depth_;
    }
    void end() {
        --depth_;
        if (transform_depth_ && *transform_depth_ == depth_) {
            auto& sources = doc_.sources_.back();
            auto& source = active_final_ ? *sources.final_transform : sources.transforms.back();
            source.range.end = position().second;
            source.range.close_begin = position().first;
            transform_depth_.reset();
        }
        if (palette_depth_ && *palette_depth_ == depth_) {
            auto& range = doc_.sources_.back().palettes.back().range;
            range.close_begin = position().first;
            range.end = position().second;
            palette_depth_.reset();
        }
        if (active_depth_ && *active_depth_ == depth_) {
            doc_.ranges_.back().end = position().second;
            doc_.ranges_.back().close_begin = position().first;
            active_depth_.reset();
        }
        if (!depth_) {
            doc_.root_range_.close_begin = position().first;
            doc_.root_range_.end = position().second;
        }
    }
    void annotation() {
        if (!palette_depth_) return;
        const auto [begin, end] = position();
        doc_.sources_.back().palettes.back().annotations.push_back({begin, end, end, end});
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
        XML_SetCommentHandler(p, [](void* d, const char*) {
            callback(d, [](auto& s) { s.annotation(); });
        });
        XML_SetProcessingInstructionHandler(p, [](void* d, const char*, const char*) {
            callback(d, [](auto& s) { s.annotation(); });
        });
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
    std::optional<Flame> model;
    // Header edits remain available for preservation-only entries. If a model can
    // be rendered, also validate interactions such as scale multiplied by zoom.
    try { model = render_model(index); }
    catch (const std::runtime_error&) {}
    catch (const std::invalid_argument&) {}
    const bool apophysis = is_apophysis(entry.version);
    AttributeChanges changes;
    if (edits.name) changes["name"] = *edits.name;
    if (edits.size) {
        const auto [w,h] = *edits.size;
        if (w <= 0 || h <= 0) throw std::invalid_argument("Size must contain two positive integers");
        changes["size"] = std::to_string(w) + " " + std::to_string(h);
        if (model) { model->width = w; model->height = h; }
    }
    if (edits.center) {
        changes["center"] = shortest(edits.center->x) + " " + shortest(edits.center->y);
        if (model) model->center = *edits.center;
    }
    const auto scalar = [&](const char* key, const std::optional<double>& value, double Flame::*member) {
        if (!value) return;
        const auto encoded = shortest(*value);
        if (!model || model.value().*member != *value) changes[key] = encoded;
        if (model) model.value().*member = *value;
    };
    if (edits.scale && !(*edits.scale > 0)) throw std::invalid_argument("Scale must be positive");
    if (edits.gamma && !(*edits.gamma > 0)) throw std::invalid_argument("Gamma must be positive");
    if (edits.quality && !(*edits.quality > 0)) throw std::invalid_argument("Quality must be positive");
    if (edits.brightness && !(*edits.brightness >= 0))
        throw std::invalid_argument("Brightness must be nonnegative");
    if (edits.gamma_threshold && !(*edits.gamma_threshold >= 0))
        throw std::invalid_argument("Gamma threshold must be nonnegative");
    if (edits.vibrancy && !(*edits.vibrancy >= 0 && *edits.vibrancy <= 1))
        throw std::invalid_argument("Vibrancy must be in [0,1]");
    scalar("scale", edits.scale, &Flame::scale);
    scalar("zoom", edits.zoom, &Flame::zoom);
    scalar("gamma", edits.gamma, &Flame::gamma);
    scalar("gamma_threshold", edits.gamma_threshold, &Flame::gamma_threshold);
    scalar("brightness", edits.brightness, &Flame::brightness);
    scalar("vibrancy", edits.vibrancy, &Flame::vibrancy);
    scalar("quality", edits.quality, &Flame::quality);
    if (edits.rotate) {
        const auto degrees = shortest(*edits.rotate);
        if (!model || model->rotate != *edits.rotate) {
            if (entry.attributes.contains("angle")) {
                if (entry.attributes.contains("rotate") && !apophysis) {
                    // A requested rotation can repair an ambiguous generic pair.
                    changes["angle"] = std::nullopt;
                    changes["rotate"] = degrees;
                } else {
                    changes["angle"] = shortest(-*edits.rotate * (std::numbers::pi / 180));
                    if (entry.attributes.contains("rotate")) changes["rotate"] = degrees;
                }
            } else changes["rotate"] = degrees;
        }
        if (model) model->rotate = *edits.rotate;
    }
    if (edits.oversample) {
        if (*edits.oversample < 1) throw std::invalid_argument("Oversampling must be positive");
        if (!model || model->oversample != *edits.oversample) {
            const bool historical = entry.attributes.contains("supersample");
            changes[historical ? "supersample" : "oversample"] = std::to_string(*edits.oversample);
            if (historical) changes["oversample"] = std::nullopt;
        }
        if (model) model->oversample = *edits.oversample;
    }
    if (edits.background) {
        const auto [r,g,b] = *edits.background;
        if (!(r >= 0 && r <= 1 && g >= 0 && g <= 1 && b >= 0 && b <= 1))
            throw std::invalid_argument("Background channels must be in [0,1]");
        if (!model || model->background.r != r || model->background.g != g || model->background.b != b)
            changes["background"] = shortest(r) + " " + shortest(g) + " " + shortest(b);
        if (model) model->background = *edits.background;
    }
    if (edits.palette_mode) {
        if (*edits.palette_mode != PaletteMode::step && *edits.palette_mode != PaletteMode::linear)
            throw std::invalid_argument("Unrecognized palette mode");
        if (!model || model->palette_mode != *edits.palette_mode)
            changes["palette_mode"] = *edits.palette_mode == PaletteMode::linear ? "linear" : "step";
        if (model) model->palette_mode = *edits.palette_mode;
    }
    if (edits.clear_filter_radius && edits.filter_radius)
        throw std::invalid_argument("Cannot set and clear the filter radius together");
    if (edits.filter_radius) {
        if (!(*edits.filter_radius >= 0)) throw std::invalid_argument("Filter radius must be nonnegative");
        const auto encoded = shortest(*edits.filter_radius);
        if (!model || model->filter_radius != edits.filter_radius) changes["filter"] = encoded;
        if (model) model->filter_radius = edits.filter_radius;
    } else if (edits.clear_filter_radius) {
        changes["filter"] = std::nullopt;
        changes["filter_shape"] = std::nullopt;
        if (model) model->filter_radius = apophysis ? std::optional<double>(.5) : std::nullopt;
    }
    if (model) validate(*model);
    std::vector<Patch> patches;
    attribute_patches(range, entry.attributes, std::move(changes), patches);
    commit(std::move(patches), model ? std::optional<std::size_t>(index) : std::nullopt);
}

std::string LegacyDocument::encode_ascii(Text ascii) const {
    const bool wide = encoding_ == Encoding::utf16le || encoding_ == Encoding::utf16be;
    if (!wide) return std::string(ascii);
    const bool little = encoding_ == Encoding::utf16le;
    std::string out;
    for (char c : ascii) {
        out += little ? c : '\0';
        out += little ? '\0' : c;
    }
    return out;
}

void LegacyDocument::attribute_patches(const Range& range,
        const std::map<std::string, std::string>& attributes,
        AttributeChanges changes, std::vector<Patch>& patches) const {
    std::erase_if(changes, [&](const auto& item) {
        const auto found = attributes.find(item.first);
        if (!item.second) return found == attributes.end();
        return found != attributes.end() && same_field_value(item.first, found->second, *item.second);
    });
    if (changes.empty()) return;
    const bool wide = encoding_ == Encoding::utf16le || encoding_ == Encoding::utf16be;
    const std::size_t step = wide ? 2 : 1;
    const bool little = encoding_ == Encoding::utf16le;
    // Tokenize only an Expat-validated opening tag, retaining its original delimiters.
    std::string tag;
    for (auto i = range.begin; i < range.open_end; i += step) {
        char c = bytes_[i];
        if (wide) {
            const char high = bytes_[i + (little ? 1 : 0)];
            c = high ? char(0x80) : bytes_[i + (little ? 0 : 1)];
        }
        tag += c;
    }
    std::size_t pos = 1;
    while (pos < tag.size() && !space(tag[pos]) && tag[pos] != '/' && tag[pos] != '>') ++pos;
    while (pos < tag.size()) {
        const auto attribute_begin = pos;
        while (pos < tag.size() && space(tag[pos])) ++pos;
        if (pos == tag.size() || tag[pos] == '>' || tag[pos] == '/') break;
        const auto start = pos;
        while (!space(tag.at(pos)) && tag[pos] != '=') ++pos;
        const auto name = tag.substr(start, pos-start);
        while (space(tag.at(pos))) ++pos;
        ++pos;
        while (space(tag.at(pos))) ++pos;
        const char quote = tag.at(pos++);
        const auto end = tag.find(quote, pos);
        if (end == std::string::npos) throw std::runtime_error("Cannot locate XML attribute delimiter");
        auto change = changes.find(name);
        if (change != changes.end()) {
            if (change->second) {
                patches.push_back({range.begin + step * pos, range.begin + step * end,
                                   encode_ascii(escape_attribute(*change->second, quote))});
            } else {
                patches.push_back({range.begin + step * attribute_begin, range.begin + step * (end+1), {}});
            }
            changes.erase(change);
        }
        pos = end + 1;
    }
    if (!changes.empty()) {
        std::string extra;
        for (const auto& [name, value] : changes)
            if (value) extra += " " + name + "=\"" + escape_attribute(*value, '"') + "\"";
        if (!extra.empty())
            patches.push_back({range.begin + step * pos, range.begin + step * pos, encode_ascii(extra)});
    }
}

std::string LegacyDocument::edited_element(const TransformSource& source, const AttributeChanges& changes) const {
    std::vector<Patch> patches;
    attribute_patches(source.range, source.attributes, changes, patches);
    std::string output = bytes_.substr(source.range.begin, source.range.end - source.range.begin);
    for (auto it = patches.rbegin(); it != patches.rend(); ++it)
        output.replace(it->begin - source.range.begin, it->end - it->begin, it->text);
    return output;
}

void LegacyDocument::commit(std::vector<Patch> patches, std::optional<std::size_t> render_index) {
    if (patches.empty()) return;
    std::sort(patches.begin(), patches.end(), [](const Patch& a, const Patch& b) { return a.begin > b.begin; });
    auto output = bytes_;
    for (const auto& patch : patches)
        output.replace(patch.begin, patch.end - patch.begin, patch.text);
    auto updated = parse(std::move(output));
    if (render_index) (void)updated.render_model(*render_index);
    *this = std::move(updated);
}

void LegacyDocument::edit_palette(std::size_t index, const Palette& palette) {
    const auto& range = ranges_.at(index);
    for (const auto& [r,g,b] : palette) {
        if (!(r >= 0 && r <= 1 && g >= 0 && g <= 1 && b >= 0 && b <= 1))
            throw std::invalid_argument("Palette channels must be in [0,1]");
    }
    bool renderable = false;
    try {
        const auto original = render_model(index);
        renderable = true;
        if (std::equal(palette.begin(), palette.end(), original.palette.begin(), [](RGB a, RGB b) {
                return a.r == b.r && a.g == b.g && a.b == b.b;
            })) return;
    } catch (const std::runtime_error&) {
        // Supplying an embedded palette can repair a missing/catalog palette.
    } catch (const std::invalid_argument&) {}
    const std::string newline = bytes_.find(encode_ascii("\r\n")) != std::string::npos ? "\r\n" : "\n";
    std::string payload;
    for (std::size_t i = 0; i < palette.size(); ++i) {
        if (i) payload += newline + "  ";
        const auto& [r,g,b] = palette[i];
        payload += "<color index=\"" + std::to_string(i) + "\" rgb=\"" + shortest(r * 255) +
                   " " + shortest(g * 255) + " " + shortest(b * 255) + "\"/>";
    }
    std::vector<Patch> patches;
    if (entries_[index].attributes.contains("palette"))
        attribute_patches(range, entries_[index].attributes, {{"palette", "-1"}}, patches);
    const auto& sources = sources_[index].palettes;
    if (!sources.empty()) {
        for (std::size_t i = 0; i < sources.size(); ++i) {
            const auto& source = sources[i];
            std::string replacement = i == 0 ? encode_ascii(payload) : std::string{};
            for (const auto& note : source.annotations)
                replacement += bytes_.substr(note.begin, note.end - note.begin);
            patches.push_back({source.range.begin, source.range.end, std::move(replacement)});
        }
    } else {
        const auto ending = encode_ascii("/>");
        if (range.open_end >= ending.size() &&
                bytes_.compare(range.open_end - ending.size(), ending.size(), ending) == 0) {
            patches.push_back({range.open_end - ending.size(), range.open_end,
                encode_ascii(">" + newline + "  " + payload + newline + "</flame>")});
        } else {
            patches.push_back({range.close_begin, range.close_begin,
                              encode_ascii(newline + "  " + payload + newline)});
        }
    }
    commit(std::move(patches), renderable ? std::optional<std::size_t>(index) : std::nullopt);
}

std::size_t LegacyDocument::append_fragment(std::string fragment) {
    const auto index = entries_.size();
    const std::string newline = bytes_.find(encode_ascii("\r\n")) != std::string::npos ? "\r\n" : "\n";
    std::vector<Patch> patches;
    if (root_is_flame_) {
        patches.push_back({root_range_.begin, root_range_.begin, encode_ascii("<flames>" + newline)});
        patches.push_back({root_range_.end, root_range_.end,
                          encode_ascii(newline) + fragment + encode_ascii(newline + "</flames>")});
    } else {
        patches.push_back({root_range_.close_begin, root_range_.close_begin,
                          encode_ascii(newline) + fragment + encode_ascii(newline)});
    }
    commit(std::move(patches));
    return index;
}

std::size_t LegacyDocument::append_flame(const Flame& flame) {
    const auto generated = parse(serialize_flame(flame));
    (void)generated.render_model(0);
    return append_fragment(encode_ascii(ascii_xml(generated.element_bytes(0))));
}

std::size_t LegacyDocument::duplicate_flame(std::size_t index, const std::string& new_name) {
    const TransformSource source{ranges_.at(index), entries_.at(index).attributes};
    return append_fragment(edited_element(source, {{"name", new_name}}));
}

void LegacyDocument::remove_flame(std::size_t index) {
    const auto& range = ranges_.at(index);
    if (entries_.size() <= 1) throw std::invalid_argument("At least one flame must remain");
    commit({{range.begin, range.end, {}}});
}

namespace {
std::string numeric_list(std::span<const double> values) {
    std::string result;
    for (double value : values) {
        if (!result.empty()) result += ' ';
        result += shortest(value);
    }
    return result;
}
std::string affine_values(const Affine& a) {
    return numeric_list(std::array{a.a, a.b, a.c, a.d, a.e, a.f});
}
bool same_affine(const Affine& a, const Affine& b) {
    return a.a == b.a && a.b == b.b && a.c == b.c && a.d == b.d && a.e == b.e && a.f == b.f;
}
bool same_terms(const std::vector<Term>& a, const std::vector<Term>& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(),
        [](Term lhs, Term rhs) { return lhs.id == rhs.id && lhs.weight == rhs.weight; });
}
}

void LegacyDocument::edit_transform(std::size_t index, std::size_t transform_index,
        const LegacyTransformEdits& edits, bool final) {
    auto model = render_model(index);
    const auto& sources = sources_.at(index);
    if (final && !model.final_transform) throw std::invalid_argument("No enabled final transform to edit");
    const auto& source = final ? *sources.final_transform : sources.transforms.at(transform_index);
    auto& transform = final ? *model.final_transform : model.transforms.at(transform_index);
    const bool apophysis = is_apophysis(model.source_version);
    AttributeChanges changes;
    if (edits.pre && !same_affine(transform.pre, *edits.pre)) {
        transform.pre = *edits.pre;
        changes["coefs"] = affine_values(*edits.pre);
    }
    if (edits.post && !same_affine(transform.post, *edits.post)) {
        transform.post = *edits.post;
        changes["post"] = affine_values(*edits.post);
    }
    if (edits.weight) {
        if (final) throw std::invalid_argument("A final transform has no selection weight");
        if (transform.weight != *edits.weight) changes["weight"] = shortest(*edits.weight);
        transform.weight = *edits.weight;
    }
    if (edits.color && transform.color != *edits.color) {
        transform.color = *edits.color;
        changes["color"] = shortest(*edits.color);
    }
    if (edits.color_speed && transform.color_speed != *edits.color_speed) {
        transform.color_speed = *edits.color_speed;
        const bool symmetry = source.attributes.contains("symmetry") ||
                              (apophysis && !source.attributes.contains("color_speed"));
        changes[symmetry ? "symmetry" : "color_speed"] =
            shortest(symmetry || apophysis ? 1 - 2 * *edits.color_speed : *edits.color_speed);
    }
    if (edits.opacity && transform.opacity != *edits.opacity) {
        transform.opacity = *edits.opacity;
        if (source.attributes.contains("plotmode") && (*edits.opacity == 0 || *edits.opacity == 1))
            changes["plotmode"] = *edits.opacity == 0 ? "off" : "on";
        else {
            changes["plotmode"] = std::nullopt;
            changes["opacity"] = shortest(*edits.opacity);
        }
    }
    if (edits.variations && !same_terms(transform.variations, *edits.variations)) {
        transform.variations = *edits.variations;
        changes["var"] = std::nullopt;
        changes["var1"] = std::nullopt;
        changes["linear3D"] = std::nullopt;
        changes["var_order"] = std::nullopt;
        for (const auto& entry : variation_registry()) changes[std::string(entry.name)] = std::nullopt;
        bool has_sinusoidal = false;
        std::string order;
        if (source.attributes.contains("flatten")) order = "flatten";
        for (const auto& term : transform.variations) {
            const auto name = std::string(variation_name(term.id));
            const bool linear3d = term.id == Variation::linear && source.attributes.contains("linear3D");
            changes[linear3d ? "linear3D" : name] = shortest(term.weight);
            if (term.id == Variation::sinusoidal) has_sinusoidal = true;
            if (!order.empty()) order += ' ';
            order += name;
        }
        if (!has_sinusoidal) changes["sinusoidal_use3D"] = std::nullopt;
        if (apophysis && !is_apophysis_2(model.source_version)) changes["var_order"] = order;
        if (std::any_of(transform.variations.begin(), transform.variations.end(),
                [](Term term) { return term.id == Variation::julian; })) {
            changes["julian_power"] = std::to_string(transform.parameters.julian_power);
            changes["julian_dist"] = shortest(transform.parameters.julian_dist);
        }
    }
    if (edits.julian_power && transform.parameters.julian_power != *edits.julian_power) {
        transform.parameters.julian_power = *edits.julian_power;
        changes["julian_power"] = std::to_string(*edits.julian_power);
    }
    if (edits.julian_dist && transform.parameters.julian_dist != *edits.julian_dist) {
        transform.parameters.julian_dist = *edits.julian_dist;
        changes["julian_dist"] = shortest(*edits.julian_dist);
    }
    if (edits.chaos) {
        if (final) throw std::invalid_argument("A final transform has no chaos transitions");
        if (transform.chaos != *edits.chaos) {
            transform.chaos = *edits.chaos;
            changes["chaos"] = edits.chaos->empty() ? std::nullopt :
                std::optional<std::string>(numeric_list(*edits.chaos));
        }
    }
    validate(model);
    std::vector<Patch> patches;
    attribute_patches(source.range, source.attributes, std::move(changes), patches);
    commit(std::move(patches), index);
}

std::size_t LegacyDocument::add_transform(std::size_t index, const Transform& transform, bool final) {
    auto model = render_model(index);
    const auto& sources = sources_.at(index);
    const auto added_index = final ? 0 : model.transforms.size();
    if (final) {
        if (sources.final_transform)
            throw std::invalid_argument("The document already contains a final transform, possibly disabled");
        model.final_transform = transform;
    } else model.transforms.push_back(transform);
    validate(model);
    // Reuse the producer-aware canonical writer for the new element only.
    const auto generated = parse(serialize_flame(model));
    const auto& new_source = final ? *generated.sources_[0].final_transform :
                                    generated.sources_[0].transforms[added_index];
    const auto fragment = generated.bytes_.substr(new_source.range.begin,
                                                  new_source.range.end - new_source.range.begin);
    const auto position = sources.transforms.back().range.end;
    const auto newline = bytes_.find(encode_ascii("\r\n")) != std::string::npos ? "\r\n  " : "\n  ";
    std::vector<Patch> patches{{position, position, encode_ascii(newline + fragment)}};
    // A generic final placed after the appended element would otherwise inherit a new color parity.
    if (!final && !is_apophysis(model.source_version) && sources.final_transform &&
            sources.final_transform->range.begin >= position &&
            !sources.final_transform->attributes.contains("color")) {
        attribute_patches(sources.final_transform->range, sources.final_transform->attributes,
                          {{"color", shortest(model.final_transform->color)}}, patches);
    }
    commit(std::move(patches), index);
    return added_index;
}

std::size_t LegacyDocument::duplicate_transform(std::size_t index, std::size_t transform_index) {
    const auto model = render_model(index);
    const auto& sources = sources_.at(index);
    const auto& source = sources.transforms.at(transform_index);
    const auto& original = model.transforms.at(transform_index);
    const auto count = model.transforms.size();
    const double clone_weight = original.weight * .5;
    AttributeChanges clone_changes{{"weight", shortest(clone_weight)}};
    std::vector<Patch> patches;
    for (std::size_t i = 0; i < count; ++i) {
        const auto& old_row = model.transforms[i].chaos;
        const double incoming = transform_index < old_row.size() ? old_row[transform_index] : 1;
        AttributeChanges changes;
        if (incoming != 1) {
            auto row = old_row;
            row.resize(count, 1);
            row.push_back(incoming);
            changes["chaos"] = numeric_list(row);
        }
        if (i == transform_index) {
            clone_changes.insert(changes.begin(), changes.end());
            changes["weight"] = shortest(original.weight - clone_weight);
        }
        attribute_patches(sources.transforms[i].range, sources.transforms[i].attributes, std::move(changes), patches);
    }
    const auto position = sources.transforms.back().range.end;
    if (!is_apophysis(model.source_version)) {
        const double expected_color = (count + static_cast<std::size_t>(sources.final_transform &&
            sources.final_transform->range.begin < position)) % 2;
        if (!source.attributes.contains("color") && original.color != expected_color)
            clone_changes["color"] = shortest(original.color);
        if (sources.final_transform && sources.final_transform->range.begin >= position &&
                !sources.final_transform->attributes.contains("color")) {
            attribute_patches(sources.final_transform->range, sources.final_transform->attributes,
                              {{"color", shortest(model.final_transform->color)}}, patches);
        }
    }
    const auto newline = bytes_.find(encode_ascii("\r\n")) != std::string::npos ? "\r\n  " : "\n  ";
    patches.push_back({position, position, encode_ascii(newline) + edited_element(source, clone_changes)});
    commit(std::move(patches), index);
    return count;
}

void LegacyDocument::remove_transform(std::size_t index, std::size_t transform_index, bool final) {
    const auto model = render_model(index);
    const auto& sources = sources_.at(index);
    if (final && !sources.final_transform) throw std::invalid_argument("No final transform to remove");
    if (!final && model.transforms.size() == 1)
        throw std::invalid_argument("At least one ordinary transform must remain");
    const auto& removed = final ? *sources.final_transform : sources.transforms.at(transform_index);
    std::vector<Patch> patches{{removed.range.begin, removed.range.end, {}}};
    const bool generic = !is_apophysis(model.source_version);
    for (std::size_t i = 0; i < sources.transforms.size(); ++i) {
        if (!final && i == transform_index) continue;
        const auto& source = sources.transforms[i];
        AttributeChanges changes;
        if (!final && transform_index < model.transforms[i].chaos.size()) {
            auto row = model.transforms[i].chaos;
            row.erase(row.begin() + static_cast<std::ptrdiff_t>(transform_index));
            changes["chaos"] = row.empty() ? std::nullopt : std::optional<std::string>(numeric_list(row));
        }
        if (generic && source.range.begin > removed.range.begin && !source.attributes.contains("color"))
            changes["color"] = shortest(model.transforms[i].color);
        attribute_patches(source.range, source.attributes, std::move(changes), patches);
    }
    if (!final && generic && sources.final_transform &&
            sources.final_transform->range.begin > removed.range.begin &&
            !sources.final_transform->attributes.contains("color")) {
        attribute_patches(sources.final_transform->range, sources.final_transform->attributes,
                          {{"color", shortest(model.final_transform->color)}}, patches);
    }
    commit(std::move(patches), index);
}

void LegacyDocument::move_transform(std::size_t index, std::size_t from, std::size_t to) {
    const auto model = render_model(index);
    const auto& sources = sources_.at(index);
    const auto count = sources.transforms.size();
    if (from >= count || to >= count) throw std::out_of_range("Transform move index is out of range");
    if (from == to) return;
    std::vector<std::size_t> order(count);
    std::iota(order.begin(), order.end(), 0);
    order.erase(order.begin() + static_cast<std::ptrdiff_t>(from));
    order.insert(order.begin() + static_cast<std::ptrdiff_t>(to), from);
    std::vector<Patch> patches;
    for (std::size_t slot = 0; slot < count; ++slot) {
        const auto original_index = order[slot];
        const auto& source = sources.transforms[original_index];
        const auto& destination = sources.transforms[slot];
        const auto& transform = model.transforms[original_index];
        AttributeChanges changes;
        auto old_row = transform.chaos;
        old_row.resize(count, 1);
        std::vector<double> new_row;
        for (const auto previous_index : order) new_row.push_back(old_row[previous_index]);
        if (new_row != old_row) changes["chaos"] = numeric_list(new_row);
        const double expected_color = (slot + static_cast<std::size_t>(sources.final_transform &&
            sources.final_transform->range.begin < destination.range.begin)) % 2;
        if (!is_apophysis(model.source_version) && !source.attributes.contains("color") &&
                transform.color != expected_color)
            changes["color"] = shortest(transform.color);
        patches.push_back({destination.range.begin, destination.range.end, edited_element(source, changes)});
    }
    commit(std::move(patches), index);
}
}
