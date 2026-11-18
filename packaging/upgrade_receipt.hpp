// Per-user upgrade selection and the schema-1 inventory emitted by package-runtime.py.
// The caller authenticates the complete receipt before using its file list.
#pragma once
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace apo::installer {
using Version = std::array<unsigned, 3>;
inline std::optional<Version> parse_version(std::wstring_view text) {
    Version result{};
    for (auto &part : result) {
        const auto end = text.find(L'.');
        const auto digits = text.substr(0, end);
        if (digits.empty() || (digits.size() > 1 && digits.front() == L'0')) return {};
        for (const auto c : digits) {
            if (c < L'0' || c > L'9' || part > 6553 || (part == 6553 && c > L'5')) return {};
            part = part * 10 + static_cast<unsigned>(c - L'0');
        }
        if (end == text.npos) return &part == &result.back() ? std::optional{result} : std::nullopt;
        text.remove_prefix(end + 1);
    }
    return {};
}
class UpgradeSelection {
    Version current_;
    std::optional<Version> previous_;
    bool blocked_ = false;
public:
    explicit UpgradeSelection(Version current) : current_(current) {}
    void consider(Version candidate) {
        if (candidate >= current_) blocked_ = true;
        else if (!previous_ || candidate > *previous_) previous_ = candidate;
    }
    std::optional<Version> previous() const { return blocked_ ? std::nullopt : previous_; }
};

inline bool is_digest(std::string_view s) {
    return s.size() == 64 && s.find_first_not_of("0123456789abcdef") == s.npos;
}

namespace detail {
inline void expect(bool ok) {
    if (!ok) throw std::runtime_error("Invalid or unsupported installed package receipt");
}
// Small reader for package JSON. Future optional metadata is skipped structurally,
// so metadata strings or nested keys can never masquerade as inventory records.
class Json {
    std::string_view text_;
    void space() { text_.remove_prefix(std::min(text_.find_first_not_of(" \r\n\t"), text_.size())); }
    unsigned hex4() {
        expect(text_.size() >= 4);
        unsigned n = 0;
        const auto r = std::from_chars(text_.data(), text_.data()+4, n, 16);
        expect(r.ec == std::errc{} && r.ptr == text_.data()+4);
        text_.remove_prefix(4);
        return n;
    }
    void unicode(std::string &out) {
        unsigned c = hex4();
        if (c >= 0xd800 && c <= 0xdbff) {
            expect(text_.starts_with("\\u")); text_.remove_prefix(2);
            const unsigned low = hex4();
            expect(low >= 0xdc00 && low <= 0xdfff);
            c = 0x10000 + (c-0xd800)*1024 + low-0xdc00;
        } else expect(c < 0xdc00 || c > 0xdfff);
        if (c < 128) { out.push_back(static_cast<char>(c)); return; }
        const unsigned tails = c <= 0x7ff ? 1 : (c <= 0xffff ? 2 : 3);
        out.push_back(static_cast<char>((tails == 1 ? 0xc0 : tails == 2 ? 0xe0 : 0xf0) | c >> (6*tails)));
        for (auto i=tails; i>0; --i) out.push_back(static_cast<char>(0x80 | ((c >> (6*(i-1))) & 63)));
    }
public:
    explicit Json(std::string_view input) : text_(input) {}
    bool take(char c) {
        space();
        if (text_.empty() || text_.front() != c) return false;
        text_.remove_prefix(1);
        return true;
    }
    bool finished() { space(); return text_.empty(); }
    std::string string() {
        expect(take('"'));
        std::string out;
        while (!text_.empty()) {
            const char c = text_.front(); text_.remove_prefix(1);
            if (c == '"') return out;
            expect(static_cast<unsigned char>(c) >= 32);
            if (c != '\\') { out.push_back(c); continue; }
            expect(!text_.empty());
            const char escape = text_.front(); text_.remove_prefix(1);
            if (escape == 'u') { unicode(out); continue; }
            constexpr std::string_view keys = "\"\\/bfnrt";
            constexpr std::string_view values = "\"\\/\b\f\n\r\t";
            const auto i = keys.find(escape);
            expect(i != keys.npos);
            out.push_back(values[i]);
        }
        throw std::runtime_error("Unterminated package receipt string");
    }
    std::string_view scalar() {
        space();
        const auto n = std::min(text_.find_first_of(" \t\r\n,]}"), text_.size());
        expect(n > 0);
        const auto s = text_.substr(0, n); text_.remove_prefix(n); return s;
    }
    template<class F> void members(F f) {
        expect(take('{'));
        if (take('}')) return;
        do {
            const auto key=string();
            expect(take(':'));
            f(key);
        } while (take(','));
        expect(take('}'));
    }
    void skip(unsigned depth=0) {
        expect(depth < 32);
        space(); expect(!text_.empty());
        if (text_.front() == '{') members([&](const auto &) { skip(depth+1); });
        else if (take('[')) {
            if (!take(']')) { do { skip(depth+1); } while (take(',')); expect(take(']')); }
        } else if (text_.front() == '"') (void)string();
        else (void)scalar();
    }
};
}
inline std::map<std::string,std::string> receipt_files(std::string_view json, std::string_view version) {
    using detail::expect;
    detail::Json r(json);
    std::map<std::string,std::string> files;
    bool schema=false, format=false, release=false, inventory=false, edition=false;
    r.members([&](const std::string &key) {
        if (key=="schema") { expect(!schema && r.string()=="apophysis-reborn.runtime-package"); schema=true; }
        else if (key=="schema_version") { expect(!format && r.scalar()=="1"); format=true; }
        else if (key=="version") { expect(!release && r.string()==version); release=true; }
        else if (key=="edition") { expect(!edition && r.string()=="Windows x64 runtime"); edition=true; }
        else if (key=="files") {
            expect(!inventory); inventory=true;
            r.members([&](const std::string &name) {
                expect(!name.empty() && files.size()<65536 && name.find_first_of("\\:\"<>|?*") == name.npos &&
                       std::none_of(name.begin(), name.end(), [](unsigned char c){ return c<32; }));
                for (std::string_view rest=name;;) {
                    const auto slash=rest.find('/');
                    const auto part=rest.substr(0,slash);
                    expect(!part.empty() && part.back()!='.' && part.back()!=' ');
                    if (slash==rest.npos) break;
                    rest.remove_prefix(slash+1);
                }
                std::string hash;
                bool bytes=false;
                r.members([&](const std::string &field) {
                    if (field=="sha256") { expect(hash.empty()); hash=r.string(); expect(is_digest(hash)); }
                    else if (field=="bytes") {
                        auto value=r.scalar(); std::uint64_t n{};
                        auto parsed=std::from_chars(value.data(),value.data()+value.size(),n);
                        expect(!bytes && parsed.ec==std::errc{} && parsed.ptr==value.data()+value.size());
                        bytes=true;
                    } else r.skip();
                });
                expect(bytes && is_digest(hash) && files.emplace(name,hash).second);
            });
        } else r.skip();
    });
    expect(schema && format && release && inventory && edition && files.contains("apophysis-reborn.exe") &&
           files.contains("flame-cpu.exe") && r.finished());
    return files;
}

// The original schema excludes the two generated metadata files. Reproduce the
// checksum list using the packaging host's pathlib ordering and line endings.
inline std::string receipt_checksums(const std::map<std::string,std::string> &files,
                                     const std::string &manifest_hash, bool windows_text) {
    auto entries = files;
    detail::expect(entries.emplace("package-manifest.json", manifest_hash).second);
    std::vector<std::pair<std::string, std::string>> lines;
    for (const auto &[name, hash] : entries) {
        std::string key = name;
        for (auto &c : key) {
            if (c == '/') c = '\0'; // A path component sorts before its extensions.
            else if (windows_text && c >= 'A' && c <= 'Z') c += 'a' - 'A';
        }
        lines.emplace_back(key, hash + "  " + name + (windows_text ? "\r\n" : "\n"));
    }
    std::sort(lines.begin(), lines.end());
    std::string bytes;
    for (const auto &line : lines) bytes += line.second;
    return bytes;
}
}
