#include "project_io.hpp"
#include "legacy_document.hpp"
#include "output_file.hpp"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <expat.h>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

namespace apo {
namespace {
struct Node {
  std::string name, text;
  ProjectSettings attrs;
  std::vector<std::size_t> children;
  std::size_t begin = 0, open_end = 0, close_begin = 0, end = 0;
  bool empty = false;
};
struct Patch {
  std::size_t begin, end;
  std::string text;
};
[[noreturn]] void fail(const std::string &what) {
  throw std::invalid_argument("Project: " + what);
}
std::string attr(const Node &n, std::string_view key,
                 std::string fallback = {}) {
  const auto i = n.attrs.find(std::string(key));
  return i == n.attrs.end() ? fallback : i->second;
}
std::string required(const Node &n, const std::string &key) {
  const auto i = n.attrs.find(key);
  if (i == n.attrs.end())
    fail("missing " + key + " on " + n.name);
  return i->second;
}
std::size_t integer(std::string_view text, const std::string &name) {
  std::size_t value = 0;
  auto [p, e] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (text.empty() || e != std::errc{} || p != text.data() + text.size())
    fail("invalid " + name);
  return value;
}
std::string escape(std::string_view value) {
  std::string out;
  out.reserve(value.size());
  for (const unsigned char c : value) {
    switch (c) {
    case '&':
      out += "&amp;";
      break;
    case '<':
      out += "&lt;";
      break;
    case '>':
      out += "&gt;";
      break;
    case '\"':
      out += "&quot;";
      break;
    case '\'':
      out += "&apos;";
      break;
    case '\r':
      out += "&#13;";
      break;
    case '\n':
      out += "&#10;";
      break;
    case '\t':
      out += "&#9;";
      break;
    default:
      if (c < 32)
        fail("settings contain an XML control character");
      out += static_cast<char>(c);
    }
  }
  return out;
}
std::size_t xml_byte_index(XML_Parser parser) {
#if XML_MAJOR_VERSION > 2 || (XML_MAJOR_VERSION == 2 && XML_MINOR_VERSION >= 9)
  return static_cast<std::size_t>(XML_GetCurrentByteIndex64(parser));
#else
  return static_cast<std::size_t>(XML_GetCurrentByteIndex(parser));
#endif
}
std::size_t xml_byte_count(XML_Parser parser) {
#if XML_MAJOR_VERSION > 2 || (XML_MAJOR_VERSION == 2 && XML_MINOR_VERSION >= 9)
  return static_cast<std::size_t>(XML_GetCurrentByteCount64(parser));
#else
  return static_cast<std::size_t>(XML_GetCurrentByteCount(parser));
#endif
}
std::size_t xml_line(XML_Parser parser) {
#if XML_MAJOR_VERSION > 2 || (XML_MAJOR_VERSION == 2 && XML_MINOR_VERSION >= 9)
  return static_cast<std::size_t>(XML_GetCurrentLineNumber64(parser));
#else
  return static_cast<std::size_t>(XML_GetCurrentLineNumber(parser));
#endif
}
constexpr std::string_view alphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
std::string encode(std::string_view bytes) {
  std::string out;
  out.reserve(((bytes.size() + 2) / 3) * 4);
  for (std::size_t i = 0; i < bytes.size(); i += 3) {
    unsigned v = static_cast<unsigned char>(bytes[i]) << 16;
    if (i + 1 < bytes.size())
      v |= static_cast<unsigned char>(bytes[i + 1]) << 8;
    if (i + 2 < bytes.size())
      v |= static_cast<unsigned char>(bytes[i + 2]);
    out += alphabet[(v >> 18) & 63];
    out += alphabet[(v >> 12) & 63];
    out += i + 1 < bytes.size() ? alphabet[(v >> 6) & 63] : '=';
    out += i + 2 < bytes.size() ? alphabet[v & 63] : '=';
  }
  return out;
}
std::string decode(std::string_view text, std::size_t limit) {
  std::string compact;
  compact.reserve(text.size());
  for (const unsigned char c : text) {
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
      continue;
    compact += static_cast<char>(c);
  }
  if (compact.size() % 4)
    fail("invalid base64 length");
  if (compact.size() / 4 > limit / 3 + 1)
    fail("decoded payload exceeds limit");
  std::string out;
  out.reserve(std::min(limit, compact.size() / 4 * 3));
  for (std::size_t i = 0; i < compact.size(); i += 4) {
    unsigned v = 0;
    int padding = 0;
    for (unsigned j = 0; j < 4; ++j) {
      const char c = compact[i + j];
      if (c == '=') {
        if (j < 2 || i + 4 != compact.size())
          fail("invalid base64 padding");
        ++padding;
        v <<= 6;
      } else {
        const auto n = alphabet.find(c);
        if (n == std::string_view::npos || padding)
          fail("invalid base64 data");
        v = (v << 6) | static_cast<unsigned>(n);
      }
    }
    if (padding > 2 || (padding == 2 && (v & 0xffff)) ||
        (padding == 1 && (v & 0xff)))
      fail("noncanonical base64 padding");
    out += static_cast<char>(v >> 16);
    if (padding < 2)
      out += static_cast<char>(v >> 8);
    if (padding < 1)
      out += static_cast<char>(v);
    if (out.size() > limit)
      fail("decoded payload exceeds limit");
  }
  return out;
}
std::string read_file(const std::filesystem::path &path) {
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f)
    fail("cannot open " + path.string());
  const auto size = f.tellg();
  if (size < 0 || static_cast<std::uintmax_t>(size) > project_file_limit)
    fail("file exceeds 96 MiB limit");
  std::string text(static_cast<std::size_t>(size), '\0');
  f.seekg(0);
  if (!text.empty() &&
      !f.read(text.data(), static_cast<std::streamsize>(text.size())))
    fail("cannot read " + path.string());
  return text;
}
void validate_settings(const ProjectSettings &settings) {
  if (settings.size() > 4096)
    fail("too many settings");
  for (const auto &[key, value] : settings) {
    if (key.empty() || key.size() > 4096 || value.size() > 1024 * 1024)
      fail("setting name or value exceeds limit");
    (void)escape(key);
    (void)escape(value);
  }
}
void validate_project(const ProjectFile &p) {
  if (p.version != ProjectFile::current_version)
    fail("unsupported project version " + std::to_string(p.version));
  if (p.library_bytes.empty() || p.library_bytes.size() > project_library_limit)
    fail("library must contain 1 byte to 64 MiB");
  const auto doc = LegacyDocument::parse(p.library_bytes);
  if (p.selected_index >= doc.entries().size())
    fail("selected flame index is outside the collection");
  validate_project_render_settings(p.render_settings, p.render_presets);
  for (const auto *s : {&p.preview_settings, &p.workspace, &p.metadata})
    validate_settings(*s);
  if (p.plugin_references.size() > 4096)
    fail("too many plugin references");
  std::set<std::string> seen;
  for (const auto &ref : p.plugin_references) {
    if (ref.id.empty() || ref.id.size() > 4096 || !seen.insert(ref.id).second)
      fail("empty or duplicate plugin id");
    for (const auto *s : {&ref.version, &ref.path, &ref.fingerprint})
      if (s->size() > 16384)
        fail("plugin reference exceeds limit");
    validate_settings(ref.metadata);
  }
  if (p.animation_data && p.animation_data->size() > 8 * 1024 * 1024)
    fail("animation payload exceeds 8 MiB");
}
} // namespace
void validate_project_render_settings(const ProjectSettings &settings,
                                      const std::vector<ProjectPreset> &presets) {
  validate_settings(settings);
  if (presets.size() > 256)
    fail("too many render presets");
  std::set<std::string> seen;
  for (const auto &preset : presets) {
    if (preset.name.empty() || preset.name.size() > 4096 ||
        !seen.insert(preset.name).second)
      fail("empty or duplicate preset name");
    (void)escape(preset.name);
    validate_settings(preset.settings);
  }
}
namespace detail {
struct ProjectSource {
  std::string bytes;
  std::vector<Node> nodes;
  ProjectFile baseline;
};
} // namespace detail
namespace {
class Parser {
public:
  explicit Parser(std::string bytes)
      : source(std::make_shared<detail::ProjectSource>()) {
    source->bytes = std::move(bytes);
  }
  std::shared_ptr<detail::ProjectSource> run() {
    if (source->bytes.size() > project_file_limit)
      fail("file exceeds 96 MiB limit");
    parser = XML_ParserCreate("UTF-8");
    if (!parser)
      throw std::bad_alloc();
    XML_SetUserData(parser, this);
    XML_SetElementHandler(
        parser,
        [](void *d, const char *name, const char **attrs) {
          static_cast<Parser *>(d)->guard(
              [&] { static_cast<Parser *>(d)->start(name, attrs); });
        },
        [](void *d, const char *) {
          static_cast<Parser *>(d)->guard(
              [&] { static_cast<Parser *>(d)->end(); });
        });
    XML_SetCharacterDataHandler(parser, [](void *d, const char *s, int n) {
      static_cast<Parser *>(d)->guard([&] {
        auto &self = *static_cast<Parser *>(d);
        if (!self.stack.empty())
          self.source->nodes[self.stack.back()].text.append(
              s, static_cast<std::size_t>(n));
      });
    });
    XML_SetStartDoctypeDeclHandler(
        parser, [](void *d, const char *, const char *, const char *, int) {
          static_cast<Parser *>(d)->guard(
              [] { fail("DOCTYPE declarations are not allowed"); });
        });
    XML_SetXmlDeclHandler(parser, [](void *d, const char *,
                                     const char *encoding, int) {
      static_cast<Parser *>(d)->guard([&] {
        if (encoding) {
          std::string e(encoding);
          std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
          });
          if (e != "utf-8" && e != "utf8")
            fail("project envelope must use UTF-8");
        }
      });
    });
    const auto status =
        XML_Parse(parser, source->bytes.data(),
                  static_cast<int>(source->bytes.size()), XML_TRUE);
    if (status == XML_STATUS_ERROR && error.empty())
      error = "XML line " + std::to_string(xml_line(parser)) + ": " +
              XML_ErrorString(XML_GetErrorCode(parser));
    XML_ParserFree(parser);
    parser = nullptr;
    if (!error.empty())
      fail(error);
    if (source->nodes.empty())
      fail("missing root element");
    return source;
  }

private:
  template <class F> void guard(F &&f) noexcept {
    try {
      f();
    } catch (const std::exception &e) {
      error = e.what();
      XML_StopParser(parser, XML_FALSE);
    } catch (...) {
      error = "parser failed";
      XML_StopParser(parser, XML_FALSE);
    }
  }
  void start(const char *name, const char **attrs) {
    if (stack.size() >= 32 || source->nodes.size() >= 65536)
      fail("XML nesting or node count exceeds limit");
    Node n;
    n.name = name;
    n.begin = xml_byte_index(parser);
    n.open_end = n.begin + xml_byte_count(parser);
    if (n.open_end > source->bytes.size() || n.open_end <= n.begin)
      fail("invalid element span");
    n.empty = n.open_end >= 2 && source->bytes[n.open_end - 2] == '/';
    for (std::size_t i = 0; attrs[i]; i += 2)
      n.attrs.emplace(attrs[i], attrs[i + 1]);
    const auto id = source->nodes.size();
    if (!stack.empty())
      source->nodes[stack.back()].children.push_back(id);
    source->nodes.push_back(std::move(n));
    stack.push_back(id);
  }
  void end() {
    auto &n = source->nodes[stack.back()];
    stack.pop_back();
    n.close_begin = n.empty ? n.open_end : xml_byte_index(parser);
    n.end = n.empty ? n.open_end : n.close_begin + xml_byte_count(parser);
  }
  std::shared_ptr<detail::ProjectSource> source;
  XML_Parser parser = nullptr;
  std::string error;
  std::vector<std::size_t> stack;
};
std::optional<std::size_t> child(const detail::ProjectSource &source,
                                 std::size_t parent, std::string_view name) {
  std::optional<std::size_t> result;
  for (auto id : source.nodes[parent].children)
    if (source.nodes[id].name == name) {
      if (result)
        fail("duplicate " + std::string(name) + " section");
      result = id;
    }
  return result;
}
ProjectSettings settings(const detail::ProjectSource &source,
                         std::size_t parent) {
  ProjectSettings result;
  for (auto id : source.nodes[parent].children) {
    const auto &n = source.nodes[id];
    if (n.name != "setting")
      continue;
    auto key = required(n, "key");
    if (!result.emplace(std::move(key), required(n, "value")).second)
      fail("duplicate setting key");
  }
  validate_settings(result);
  return result;
}
std::string setting_xml(const ProjectSettings &settings,
                        std::string_view indent = "    ") {
  std::string out;
  for (const auto &[k, v] : settings)
    out += std::string(indent) + "<setting key=\"" + escape(k) + "\" value=\"" +
           escape(v) + "\"/>\n";
  return out;
}
std::string preset_xml(const ProjectPreset &p) {
  return "    <preset name=\"" + escape(p.name) + "\">\n" +
         setting_xml(p.settings, "      ") + "    </preset>\n";
}
std::string plugin_xml(const ProjectPluginReference &p) {
  return "    <plugin id=\"" + escape(p.id) + "\" version=\"" +
         escape(p.version) + "\" path=\"" + escape(p.path) +
         "\" fingerprint=\"" + escape(p.fingerprint) + "\">\n" +
         setting_xml(p.metadata, "      ") + "    </plugin>\n";
}
class Editor {
public:
  explicit Editor(const detail::ProjectSource &s) : source(s) {}
  void attribute(std::size_t id, const std::string &key,
                 const std::string &value) {
    const auto &n = source.nodes[id];
    if (n.attrs.contains(key) && n.attrs.at(key) == value)
      return;
    const auto &bytes = source.bytes;
    std::size_t p = n.begin + 1;
    while (p < n.open_end &&
           !std::isspace(static_cast<unsigned char>(bytes[p])) &&
           bytes[p] != '/' && bytes[p] != '>')
      ++p;
    while (p < n.open_end) {
      while (p < n.open_end &&
             std::isspace(static_cast<unsigned char>(bytes[p])))
        ++p;
      if (p >= n.open_end || bytes[p] == '/' || bytes[p] == '>')
        break;
      const auto start = p;
      while (p < n.open_end &&
             !std::isspace(static_cast<unsigned char>(bytes[p])) &&
             bytes[p] != '=')
        ++p;
      const auto name = bytes.substr(start, p - start);
      while (p < n.open_end &&
             std::isspace(static_cast<unsigned char>(bytes[p])))
        ++p;
      if (p >= n.open_end || bytes[p++] != '=')
        fail("invalid attribute span");
      while (p < n.open_end &&
             std::isspace(static_cast<unsigned char>(bytes[p])))
        ++p;
      if (p >= n.open_end || (bytes[p] != '\"' && bytes[p] != '\''))
        fail("invalid attribute quote");
      const char quote = bytes[p++];
      const auto begin = p;
      while (p < n.open_end && bytes[p] != quote)
        ++p;
      if (p >= n.open_end)
        fail("unterminated attribute");
      if (name == key) {
        patches.push_back({begin, p, escape(value)});
        return;
      }
      ++p;
    }
    additions[n.open_end - (n.empty ? 2 : 1)] +=
        " " + key + "=\"" + escape(value) + "\"";
  }
  void erase(std::size_t id) {
    const auto &n = source.nodes[id];
    patches.push_back({n.begin, n.end, {}});
  }
  void body(std::size_t id, std::string text) {
    const auto &n = source.nodes[id];
    if (n.empty)
      patches.push_back({n.open_end - 2, n.open_end,
                         ">" + std::move(text) + "</" + n.name + ">"});
    else
      patches.push_back({n.open_end, n.close_begin, std::move(text)});
  }
  void append(std::size_t id, std::string text) {
    const auto &n = source.nodes[id];
    if (n.empty)
      empty_additions[id] += std::move(text);
    else
      additions[n.close_begin] += std::move(text);
  }
  void map(std::size_t id, const ProjectSettings &values) {
    std::set<std::string> found;
    for (auto item : source.nodes[id].children) {
      const auto &n = source.nodes[item];
      if (n.name != "setting")
        continue;
      const auto key = required(n, "key");
      const auto it = values.find(key);
      if (it == values.end())
        erase(item);
      else {
        found.insert(key);
        attribute(item, "value", it->second);
      }
    }
    ProjectSettings added;
    for (const auto &[k, v] : values)
      if (!found.contains(k))
        added[k] = v;
    if (!added.empty())
      append(id, "\n" + setting_xml(added));
  }
  void map_section(std::string_view name, const ProjectSettings &values) {
    if (auto id = child(source, 0, name))
      map(*id, values);
    else if (!values.empty())
      append(0, "  <" + std::string(name) + ">\n" + setting_xml(values) +
                    "  </" + std::string(name) + ">\n");
  }
  std::string finish() {
    for (const auto &[id, text] : empty_additions) {
      const auto &n = source.nodes[id];
      patches.push_back(
          {n.open_end - 2, n.open_end, ">" + text + "</" + n.name + ">"});
    }
    for (auto &[position, text] : additions)
      patches.push_back({position, position, std::move(text)});
    std::stable_sort(
        patches.begin(), patches.end(), [](const auto &a, const auto &b) {
          return a.begin < b.begin || (a.begin == b.begin && a.end < b.end);
        });
    std::string out;
    std::size_t cursor = 0;
    for (const auto &p : patches) {
      if (p.begin < cursor || p.end > source.bytes.size())
        fail("overlapping source edits");
      out.append(source.bytes, cursor, p.begin - cursor);
      out += p.text;
      cursor = p.end;
    }
    out.append(source.bytes, cursor, std::string::npos);
    return out;
  }

private:
  const detail::ProjectSource &source;
  std::vector<Patch> patches;
  std::map<std::size_t, std::string> additions, empty_additions;
};
std::string fresh(const ProjectFile &p) {
  std::string out =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<apophysis-project "
      "version=\"1\" selected=\"" +
      std::to_string(p.selected_index) + "\">\n";
  out += "  <flame-library encoding=\"base64\">" + encode(p.library_bytes) +
         "</flame-library>\n";
  const std::pair<std::string_view, const ProjectSettings *> maps[] = {
      {"render", &p.render_settings},
      {"preview", &p.preview_settings},
      {"workspace", &p.workspace},
      {"metadata", &p.metadata}};
  for (const auto &[name, m] : maps)
    if (!m->empty())
      out += "  <" + std::string(name) + ">\n" + setting_xml(*m) + "  </" +
             std::string(name) + ">\n";
  if (!p.render_presets.empty()) {
    out += "  <render-presets>\n";
    for (const auto &s : p.render_presets)
      out += preset_xml(s);
    out += "  </render-presets>\n";
  }
  if (!p.plugin_references.empty()) {
    out += "  <plugins>\n";
    for (const auto &s : p.plugin_references)
      out += plugin_xml(s);
    out += "  </plugins>\n";
  }
  if (p.animation_data)
    out += "  <animation encoding=\"base64\">" + encode(*p.animation_data) +
           "</animation>\n";
  return out + "</apophysis-project>\n";
}
} // namespace
ProjectFile parse_project(std::string bytes) {
  auto source = Parser(std::move(bytes)).run();
  const auto &root = source->nodes[0];
  if (root.name != "apophysis-project")
    fail("expected apophysis-project root");
  ProjectFile p;
  const auto version = integer(required(root, "version"), "version");
  if (version != ProjectFile::current_version)
    fail("unsupported project version " + std::to_string(version));
  p.version = static_cast<unsigned>(version);
  p.selected_index = integer(attr(root, "selected", "0"), "selected index");
  auto lib = child(*source, 0, "flame-library");
  if (!lib)
    fail("missing flame-library");
  const auto &ln = source->nodes[*lib];
  if (required(ln, "encoding") != "base64")
    fail("unsupported flame-library encoding");
  if (!ln.children.empty())
    fail("flame-library must contain only base64 text");
  p.library_bytes = decode(ln.text, project_library_limit);
  const std::pair<std::string_view, ProjectSettings *> maps[] = {
      {"render", &p.render_settings},
      {"preview", &p.preview_settings},
      {"workspace", &p.workspace},
      {"metadata", &p.metadata}};
  for (const auto &[name, m] : maps)
    if (auto id = child(*source, 0, name))
      *m = settings(*source, *id);
  if (auto id = child(*source, 0, "render-presets"))
    for (auto c : source->nodes[*id].children)
      if (source->nodes[c].name == "preset")
        p.render_presets.push_back(
            {required(source->nodes[c], "name"), settings(*source, c)});
  if (auto id = child(*source, 0, "plugins"))
    for (auto c : source->nodes[*id].children)
      if (source->nodes[c].name == "plugin") {
        const auto &n = source->nodes[c];
        p.plugin_references.push_back({required(n, "id"), attr(n, "version"),
                                       attr(n, "path"), attr(n, "fingerprint"),
                                       settings(*source, c)});
      }
  if (auto id = child(*source, 0, "animation")) {
    const auto &n = source->nodes[*id];
    if (required(n, "encoding") != "base64" || !n.children.empty())
      fail("unsupported animation encoding");
    p.animation_data = decode(n.text, 8 * 1024 * 1024);
  }
  validate_project(p);
  source->baseline = p;
  p.retained_source = std::move(source);
  return p;
}
ProjectFile load_project(const std::filesystem::path &path) {
  return parse_project(read_file(path));
}
std::string serialize_project(const ProjectFile &p) {
  validate_project(p);
  std::string result;
  if (!p.retained_source)
    result = fresh(p);
  else {
    const auto &s = *p.retained_source;
    const auto &base = s.baseline;
    Editor e(s);
    if (p.selected_index != base.selected_index)
      e.attribute(0, "selected", std::to_string(p.selected_index));
    if (p.library_bytes != base.library_bytes)
      e.body(*child(s, 0, "flame-library"), encode(p.library_bytes));
    e.map_section("render", p.render_settings);
    e.map_section("preview", p.preview_settings);
    e.map_section("workspace", p.workspace);
    e.map_section("metadata", p.metadata);
    const auto presets = child(s, 0, "render-presets");
    if (presets) {
      std::set<std::string> found;
      for (auto id : s.nodes[*presets].children)
        if (s.nodes[id].name == "preset") {
          const auto name = required(s.nodes[id], "name");
          auto it =
              std::find_if(p.render_presets.begin(), p.render_presets.end(),
                           [&](const auto &a) { return a.name == name; });
          if (it == p.render_presets.end())
            e.erase(id);
          else {
            found.insert(name);
            e.map(id, it->settings);
          }
        }
      for (const auto &v : p.render_presets)
        if (!found.contains(v.name))
          e.append(*presets, preset_xml(v));
    } else if (!p.render_presets.empty()) {
      std::string text = "  <render-presets>\n";
      for (const auto &v : p.render_presets)
        text += preset_xml(v);
      e.append(0, text + "  </render-presets>\n");
    }
    const auto plugins = child(s, 0, "plugins");
    if (plugins) {
      std::set<std::string> found;
      for (auto id : s.nodes[*plugins].children)
        if (s.nodes[id].name == "plugin") {
          const auto name = required(s.nodes[id], "id");
          auto it = std::find_if(p.plugin_references.begin(),
                                 p.plugin_references.end(),
                                 [&](const auto &a) { return a.id == name; });
          if (it == p.plugin_references.end())
            e.erase(id);
          else {
            found.insert(name);
            if (attr(s.nodes[id], "version") != it->version)
              e.attribute(id, "version", it->version);
            if (attr(s.nodes[id], "path") != it->path)
              e.attribute(id, "path", it->path);
            if (attr(s.nodes[id], "fingerprint") != it->fingerprint)
              e.attribute(id, "fingerprint", it->fingerprint);
            e.map(id, it->metadata);
          }
        }
      for (const auto &v : p.plugin_references)
        if (!found.contains(v.id))
          e.append(*plugins, plugin_xml(v));
    } else if (!p.plugin_references.empty()) {
      std::string text = "  <plugins>\n";
      for (const auto &v : p.plugin_references)
        text += plugin_xml(v);
      e.append(0, text + "  </plugins>\n");
    }
    const auto animation = child(s, 0, "animation");
    if (p.animation_data != base.animation_data) {
      if (animation) {
        if (p.animation_data)
          e.body(*animation, encode(*p.animation_data));
        else
          e.erase(*animation);
      } else if (p.animation_data)
        e.append(0, "  <animation encoding=\"base64\">" +
                        encode(*p.animation_data) + "</animation>\n");
    }
    result = e.finish();
  }
  if (result.size() > project_file_limit)
    fail("serialized project exceeds 96 MiB limit");
  // Reparse the envelope to reject invalid UTF-8 and verify all edited source
  // spans.
  (void)Parser(result).run();
  return result;
}
void save_project(const ProjectFile &p, const std::filesystem::path &path) {
  const auto bytes = serialize_project(p);
  write_atomic(path, [&](std::ostream &out) {
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  });
}
} // namespace apo
