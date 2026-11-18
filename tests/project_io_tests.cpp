#include "flame_io.hpp"
#include "legacy_document.hpp"
#include "project_io.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace {
using namespace apo;
void require(bool v, const char *message) {
  if (!v)
    throw std::runtime_error(message);
}
void rejects(const std::function<void()> &action, const char *message) {
  bool caught = false;
  try {
    action();
  } catch (const std::exception &) {
    caught = true;
  }
  require(caught, message);
}
std::string read(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), {}};
}
std::string utf16_library() {
  const std::string ascii =
      "<?xml version=\"1.0\" encoding=\"UTF-16\"?>\r\n<flames "
      "vendor=\"keep\"><!-- untouched --><flame name=\"First\"><xform "
      "custom_future=\"1\"/></flame><flame name=\"Second\"/></flames>\r\n";
  std::string result = "\xff\xfe";
  for (char c : ascii) {
    result += c;
    result += '\0';
  }
  return result;
}
void payload_and_settings() {
  ProjectFile p;
  p.library_bytes = utf16_library();
  p.selected_index = 1;
  p.render_settings = {{"seed", "18446744073709551615"},
                       {"quality", "0.125"},
                       {"backend", "cpu"}};
  p.preview_settings = {{"budget", "200000"}};
  p.workspace = {{"panels", "advanced"},
                 {"layout", "line one\nline two\t&<>\"'"}};
  p.metadata = {{"author", "Zoë"}};
  p.render_presets = {{"Poster", {{"width", "8192"}, {"height", "6144"}}}};
  p.plugin_references = {{"future_formula",
                          "1.2",
                          "/ignored/native.so",
                          "sha256:abc",
                          {{"required", "true"}}}};
  p.animation_data = std::string("\0future\xff", 8);
  const auto bytes = serialize_project(p);
  auto q = parse_project(bytes);
  require(q.library_bytes == p.library_bytes,
          "UTF-16 collection bytes preserved");
  require(q.selected_index == 1, "selected entry preserved");
  require(q.render_settings == p.render_settings &&
              q.preview_settings == p.preview_settings &&
              q.workspace == p.workspace && q.metadata == p.metadata,
          "settings preserve exact strings and wide seed");
  require(q.render_presets == p.render_presets &&
              q.plugin_references == p.plugin_references,
          "presets and plugin metadata preserved");
  require(q.animation_data == p.animation_data,
          "opaque animation bytes preserved");
  require(serialize_project(q) == bytes, "untouched project byte roundtrip");
  auto document = LegacyDocument::parse(q.library_bytes);
  document.edit(1, LegacyEdits{.name = "Edited"});
  q.library_bytes = document.bytes();
  auto r = parse_project(serialize_project(q));
  require(r.library_bytes == document.bytes(),
          "edited original encoding survives native project save");
  require(r.render_settings.at("seed") == "18446744073709551615",
          "seed not rounded through floating point");
}
void unknown_extensions() {
  ProjectFile p;
  p.library_bytes = serialize_flame(demo_flame());
  auto text = serialize_project(p);
  auto root = text.find(" selected=");
  text.insert(root, " future=\"untouched\" xmlns:x=\"urn:next\"");
  text.insert(
      text.find("</apophysis-project>"),
      "  <!-- root comment -->\n  <render future='r'><setting key='seed' "
      "value='1' extra='retain'><x:state "
      "a='1'/></setting><x:reserved/></render>\n"
      "  <preview future='p'/>\n  <metadata><x:future>opaque &amp; "
      "data</x:future></metadata>\n"
      "  <render-presets ext='yes'><preset name='Draft' extra='keep'><setting "
      "key='quality' value='5' "
      "extension='v'/><x:future/></preset><x:reserved/></render-presets>\n"
      "  <plugins><plugin id='native' "
      "extra='keep'><x:private/></plugin><x:reserved/></plugins>\n"
      "  <x:future format='2'><x:child>untouched</x:child></x:future>\n");
  auto q = parse_project(text);
  require(serialize_project(q) == text,
          "untouched unknown XML exact roundtrip without inserted defaults");
  q.render_settings["seed"] = "9";
  q.preview_settings["budget"] = "30";
  q.metadata["author"] = "Me";
  q.render_presets[0].settings["quality"] = "10";
  q.plugin_references[0].version = "2";
  q.plugin_references[0].metadata["source"] = "local";
  auto edited = serialize_project(q);
  require(edited.find("extra='retain'><x:state a='1'/>") != std::string::npos,
          "unknown attributes and children on changed setting retained");
  require(edited.find("future='p'>") != std::string::npos,
          "self closing known section expanded without losing attributes");
  for (const auto *exact :
       {"<!-- root comment -->", "<x:future>opaque &amp; data</x:future>",
        "extension='v'", "<x:private/>",
        "<x:future format='2'><x:child>untouched</x:child></x:future>"})
    require(edited.find(exact) != std::string::npos,
            "unknown metadata retained on edit");
  auto r = parse_project(edited);
  require(r.render_settings.at("seed") == "9" &&
              r.preview_settings.at("budget") == "30" &&
              r.render_presets[0].settings.at("quality") == "10" &&
              r.plugin_references[0].version == "2",
          "all edited values parse");
  q.render_settings.erase("seed");
  q.render_presets.clear();
  q.plugin_references.clear();
  auto removed = parse_project(serialize_project(q));
  require(removed.render_settings.empty() && removed.render_presets.empty() &&
              removed.plugin_references.empty(),
          "explicit removal persists");
}
void malformed_and_limits() {
  ProjectFile p;
  p.library_bytes = serialize_flame(demo_flame());
  auto text = serialize_project(p);
  auto replace = [&](const std::string &a, const std::string &b) {
    auto t = text;
    t.replace(t.find(a), a.size(), b);
    return t;
  };
  rejects([&] { parse_project(replace("version=\"1\"", "version=\"2\"")); },
          "unknown project version rejected");
  rejects([&] { parse_project(replace("selected=\"0\"", "selected=\"1\"")); },
          "out-of-range selection rejected");
  rejects(
      [&] {
        parse_project(replace("encoding=\"base64\"", "encoding=\"zip\""));
      },
      "unknown library encoding rejected");
  rejects(
      [&] {
        parse_project(replace(
            "</apophysis-project>",
            "<flame-library encoding=\"base64\"/></apophysis-project>"));
      },
      "duplicate library rejected");
  rejects(
      [&] {
        parse_project(
            replace("</apophysis-project>",
                    "<render><setting key=\"a\" value=\"1\"/><setting "
                    "key=\"a\" value=\"2\"/></render></apophysis-project>"));
      },
      "duplicate keys rejected");
  rejects(
      [&] {
        parse_project("<!DOCTYPE apophysis-project [<!ENTITY x 'payload'>]>" +
                      text.substr(text.find("<apophysis-project")));
      },
      "DOCTYPE and entities rejected");
  auto depth = text;
  std::string nested;
  for (int i = 0; i < 40; ++i)
    nested += "<x>";
  for (int i = 0; i < 40; ++i)
    nested += "</x>";
  depth.insert(depth.find("</apophysis-project>"), nested);
  rejects([&] { parse_project(depth); }, "XML depth bounded");
  auto invalid = text;
  const auto begin = invalid.find("<flame-library");
  const auto content = invalid.find('>', begin) + 1;
  invalid.replace(content, invalid.find("</flame-library>") - content,
                  "YQ==garbage");
  rejects([&] { parse_project(invalid); }, "malformed base64 rejected");
  p.selected_index = 99;
  rejects([&] { serialize_project(p); }, "invalid in-memory project refused");
  p.selected_index = 0;
  p.metadata["bad"] = std::string(1, '\1');
  rejects([&] { serialize_project(p); }, "XML control characters rejected");
}
void atomic_save() {
  const auto directory =
      std::filesystem::temp_directory_path() /
      ("apo-project-io-test-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  if (!std::filesystem::create_directory(directory))
    throw std::runtime_error(
        "Could not reserve an isolated project test directory");
  struct Cleanup {
    std::filesystem::path path;
    ~Cleanup() {
      std::error_code ignored;
      std::filesystem::remove_all(path, ignored);
    }
  } cleanup{directory};
  const auto path = directory / "atomic.apoproject";
  ProjectFile p;
  p.library_bytes = serialize_flame(demo_flame());
  save_project(p, path);
  const auto good = read(path);
  require(load_project(path).library_bytes == p.library_bytes,
          "file save/load");
  p.version = 999;
  rejects([&] { save_project(p, path); }, "invalid save fails");
  require(read(path) == good, "failed save leaves prior output intact");
  std::filesystem::remove(path);
}
} // namespace
int main() {
  try {
    payload_and_settings();
    unknown_extensions();
    malformed_and_limits();
    atomic_save();
    std::cout << "Project format tests passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
