#include "animation.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <exception>
#include <expat.h>
#include <map>
#include <set>
#include <stdexcept>

namespace apo {
namespace {
[[noreturn]] void fail(const std::string &message) {
  throw std::invalid_argument("Animation: " + message);
}

template <class T> T integer(std::string_view text, const char *field) {
  T value{};
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (text.empty() || error != std::errc{} || end != text.data() + text.size())
    fail(std::string("invalid integer ") + field);
  return value;
}

std::string escape(std::string_view text) {
  std::string out;
  for (unsigned char c : text) {
    switch (c) {
    case '&': out += "&amp;"; break;
    case '<': out += "&lt;"; break;
    case '>': out += "&gt;"; break;
    case '"': out += "&quot;"; break;
    case '\r': out += "&#13;"; break;
    case '\n': out += "&#10;"; break;
    case '\t': out += "&#9;"; break;
    default:
      if (c < 32)
        fail("label contains an XML control character");
      out += static_cast<char>(c);
    }
  }
  return out;
}

constexpr std::string_view alphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
std::string encode(std::string_view bytes) {
  std::string result;
  result.reserve((bytes.size() + 2) / 3 * 4);
  for (std::size_t i = 0; i < bytes.size(); i += 3) {
    const unsigned value = (static_cast<unsigned char>(bytes[i]) << 16) |
        (i + 1 < bytes.size() ? static_cast<unsigned char>(bytes[i + 1]) << 8 : 0) |
        (i + 2 < bytes.size() ? static_cast<unsigned char>(bytes[i + 2]) : 0);
    result += alphabet[(value >> 18) & 63];
    result += alphabet[(value >> 12) & 63];
    result += i + 1 < bytes.size() ? alphabet[(value >> 6) & 63] : '=';
    result += i + 2 < bytes.size() ? alphabet[value & 63] : '=';
  }
  return result;
}

std::string decode(std::string_view source) {
  std::string text;
  text.reserve(source.size());
  for (unsigned char c : source)
    if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
      text += static_cast<char>(c);
  if (text.size() % 4)
    fail("invalid snapshot base64 length");
  std::string result;
  result.reserve(text.size() / 4 * 3);
  for (std::size_t i = 0; i < text.size(); i += 4) {
    unsigned value = 0, padding = 0;
    for (unsigned j = 0; j < 4; ++j) {
      const char c = text[i + j];
      if (c == '=') {
        if (j < 2 || i + 4 != text.size())
          fail("invalid snapshot base64 padding");
        ++padding;
        value <<= 6;
      } else {
        const auto found = alphabet.find(c);
        if (found == std::string_view::npos || padding)
          fail("invalid snapshot base64 data");
        value = (value << 6) | static_cast<unsigned>(found);
      }
    }
    if (padding > 2 || (padding == 2 && (value & 0xffff)) ||
        (padding == 1 && (value & 0xff)))
      fail("noncanonical snapshot base64 padding");
    result += static_cast<char>(value >> 16);
    if (padding < 2) result += static_cast<char>(value >> 8);
    if (padding < 1) result += static_cast<char>(value);
  }
  return result;
}

struct Node {
  std::string name, text;
  std::map<std::string, std::string> attrs;
  std::vector<std::size_t> children;
};

class Parser {
public:
  Parser() : parser_(XML_ParserCreate("UTF-8")) {
    if (!parser_) throw std::bad_alloc();
  }
  ~Parser() { XML_ParserFree(parser_); }
  Parser(const Parser &) = delete;
  std::vector<Node> run(std::string_view source) {
    XML_SetUserData(parser_, this);
    XML_SetElementHandler(parser_,
      [](void *data, const char *name, const char **attrs) {
        auto &self = *static_cast<Parser *>(data);
        self.guard([&] {
          if (self.stack_.size() >= 4 || self.nodes_.size() >= 100000)
            fail("XML depth or element count exceeds limit");
          Node node;
          node.name = name;
          for (std::size_t i = 0; attrs[i]; i += 2) {
            if (i >= 40) fail("too many XML attributes");
            node.attrs.emplace(attrs[i], attrs[i + 1]);
          }
          const auto index = self.nodes_.size();
          if (!self.stack_.empty())
            self.nodes_[self.stack_.back()].children.push_back(index);
          self.nodes_.push_back(std::move(node));
          self.stack_.push_back(index);
        });
      },
      [](void *data, const char *) {
        auto &self = *static_cast<Parser *>(data);
        self.guard([&] { self.stack_.pop_back(); });
      });
    XML_SetCharacterDataHandler(parser_, [](void *data, const char *text, int count) {
      auto &self = *static_cast<Parser *>(data);
      self.guard([&] {
        if (!self.stack_.empty())
          self.nodes_[self.stack_.back()].text.append(text, static_cast<std::size_t>(count));
      });
    });
    XML_SetStartDoctypeDeclHandler(parser_, [](void *data, const char *, const char *, const char *, int) {
      auto &self = *static_cast<Parser *>(data);
      self.guard([] { fail("DOCTYPE declarations are not allowed"); });
    });
    XML_SetXmlDeclHandler(parser_, [](void *data, const char *, const char *encoding, int) {
      auto &self = *static_cast<Parser *>(data);
      self.guard([&] {
        if (!encoding) return;
        std::string value(encoding);
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (value != "utf-8" && value != "utf8")
          fail("animation envelope must use UTF-8");
      });
    });
    const auto status = XML_Parse(parser_, source.data(), static_cast<int>(source.size()), XML_TRUE);
    if (failure_) std::rethrow_exception(failure_);
    if (status == XML_STATUS_ERROR)
      fail(std::string("invalid XML: ") + XML_ErrorString(XML_GetErrorCode(parser_)));
    if (nodes_.empty()) fail("missing XML root");
    return std::move(nodes_);
  }
private:
  template<class F> void guard(F &&function) noexcept {
    // Expat may deliver an empty element's closing callback after a stop from
    // its opening callback. Preserve the first failure and leave state alone.
    if (failure_) return;
    try { function(); }
    catch (...) {
      failure_ = std::current_exception();
      XML_StopParser(parser_, XML_FALSE);
    }
  }
  XML_Parser parser_;
  std::exception_ptr failure_;
  std::vector<Node> nodes_;
  std::vector<std::size_t> stack_;
};

void attributes(const Node &node, std::initializer_list<std::string_view> allowed) {
  for (const auto &[key, value] : node.attrs) {
    (void)value;
    if (std::find(allowed.begin(), allowed.end(), key) == allowed.end())
      fail("unsupported attribute " + key + " on " + node.name);
  }
}
std::string required(const Node &node, const std::string &key) {
  const auto found = node.attrs.find(key);
  if (found == node.attrs.end()) fail("missing " + key + " on " + node.name);
  return found->second;
}
void blank(const Node &node) {
  if (std::any_of(node.text.begin(), node.text.end(), [](unsigned char c) {
        return c != ' ' && c != '\t' && c != '\r' && c != '\n';
      })) fail("unexpected text in " + node.name);
}
template<class T> T choice(const Node &node, const std::string &key,
                           std::initializer_list<T> values) {
  const auto name = required(node, key);
  for (auto value : values)
    if (animation_name(value) == name) return value;
  fail("unsupported " + key + ": " + name);
}
} // namespace

bool is_animation_payload(std::string_view bytes) noexcept {
  if (bytes.starts_with("\xef\xbb\xbf")) bytes.remove_prefix(3);
  while (!bytes.empty()) {
    const auto first = bytes.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return false;
    bytes.remove_prefix(first);
    if (bytes.starts_with("<?")) {
      const auto end = bytes.find("?>");
      if (end == std::string_view::npos) return false;
      bytes.remove_prefix(end + 2);
    } else if (bytes.starts_with("<!--")) {
      const auto end = bytes.find("-->");
      if (end == std::string_view::npos) return false;
      bytes.remove_prefix(end + 3);
    } else {
      constexpr std::string_view prefix = "<apophysis-animation";
      if (!bytes.starts_with(prefix) || bytes.size() == prefix.size()) return false;
      const auto next = bytes[prefix.size()];
      return next == ' ' || next == '\t' || next == '\r' || next == '\n' || next == '>' || next == '/';
    }
  }
  return false;
}

AnimationClip parse_animation(std::string_view bytes) {
  if (bytes.empty() || bytes.size() > animation_payload_limit)
    fail("payload must contain 1 byte to 8 MiB");
  auto nodes = Parser{}.run(bytes);
  const auto &root = nodes.front();
  if (root.name != "apophysis-animation") fail("unsupported animation payload root");
  attributes(root, {"version", "name", "fps-num", "fps-den", "frame-count", "loop", "affine", "palette", "camera-angle", "topology", "seed", "seed-mode"});
  blank(root);
  AnimationClip clip;
  clip.version = integer<unsigned>(required(root, "version"), "version");
  if (clip.version != AnimationClip::current_version)
    fail("unsupported animation version " + std::to_string(clip.version) + "; original payload remains preservable");
  clip.name = required(root, "name");
  clip.rate.numerator = integer<std::uint32_t>(required(root, "fps-num"), "fps-num");
  clip.rate.denominator = integer<std::uint32_t>(required(root, "fps-den"), "fps-den");
  clip.frame_count = integer<std::uint32_t>(required(root, "frame-count"), "frame-count");
  clip.seed = integer<std::uint64_t>(required(root, "seed"), "seed");
  clip.loop = choice(root, "loop", {AnimationLoop::once, AnimationLoop::repeat, AnimationLoop::ping_pong});
  clip.affine = choice(root, "affine", {AnimationAffine::linear, AnimationAffine::polar});
  clip.palette = choice(root, "palette", {AnimationPalette::rgb, AnimationPalette::hsv_shortest, AnimationPalette::linear_rgb, AnimationPalette::hsl_shortest});
  clip.camera_angle = choice(root, "camera-angle", {AnimationAngle::shortest, AnimationAngle::unwrapped});
  clip.topology = choice(root, "topology", {AnimationTopology::strict, AnimationTopology::fade_unmatched});
  clip.seed_mode = choice(root, "seed-mode", {AnimationSeedMode::fixed, AnimationSeedMode::per_frame});
  for (const auto index : root.children) {
    const auto &node = nodes[index];
    if (node.name != "keyframe") fail("unsupported animation child " + node.name);
    if (clip.keyframes.size() >= animation_keyframe_limit) fail("too many keyframes");
    attributes(node, {"frame", "name", "easing", "plugin-identity"});
    blank(node);
    AnimationKeyframe key;
    key.frame = integer<std::uint32_t>(required(node, "frame"), "frame");
    key.name = required(node, "name");
    key.plugin_identity = required(node, "plugin-identity");
    key.easing = choice(node, "easing", {AnimationEasing::hold, AnimationEasing::linear, AnimationEasing::smoothstep, AnimationEasing::smootherstep});
    bool snapshot = false;
    for (const auto child : node.children) {
      const auto &value = nodes[child];
      if (!value.children.empty()) fail("unexpected nested keyframe data");
      if (value.name == "snapshot") {
        if (snapshot) fail("duplicate keyframe snapshot");
        attributes(value, {"encoding", "selected"});
        if (required(value, "encoding") != "base64") fail("snapshot encoding must be base64");
        key.selected_index = integer<std::size_t>(required(value, "selected"), "selected");
        key.library_bytes = decode(value.text);
        snapshot = true;
      } else if (value.name == "transform") {
        attributes(value, {"id"});
        blank(value);
        if (key.transform_ids.size() >= animation_transform_limit) fail("too many correspondence IDs");
        key.transform_ids.push_back(required(value, "id"));
      } else fail("unsupported keyframe child " + value.name);
    }
    if (!snapshot) fail("keyframe is missing its snapshot");
    clip.keyframes.push_back(std::move(key));
  }
  validate_animation_structure(clip);
  return clip;
}

std::string serialize_animation(const AnimationClip &clip) {
  validate_animation_structure(clip);
  std::string out = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<apophysis-animation version=\"" + std::to_string(clip.version) +
      "\" name=\"" + escape(clip.name) + "\" fps-num=\"" + std::to_string(clip.rate.numerator) +
      "\" fps-den=\"" + std::to_string(clip.rate.denominator) + "\" frame-count=\"" + std::to_string(clip.frame_count) +
      "\" loop=\"" + std::string(animation_name(clip.loop)) + "\" affine=\"" + std::string(animation_name(clip.affine)) +
      "\" palette=\"" + std::string(animation_name(clip.palette)) + "\" camera-angle=\"" + std::string(animation_name(clip.camera_angle)) +
      "\" topology=\"" + std::string(animation_name(clip.topology)) + "\" seed=\"" + std::to_string(clip.seed) +
      "\" seed-mode=\"" + std::string(animation_name(clip.seed_mode)) + "\">\n";
  for (const auto &key : clip.keyframes) {
    out += "  <keyframe frame=\"" + std::to_string(key.frame) + "\" name=\"" + escape(key.name) +
        "\" easing=\"" + std::string(animation_name(key.easing)) + "\" plugin-identity=\"" + escape(key.plugin_identity) + "\">\n";
    out += "    <snapshot encoding=\"base64\" selected=\"" + std::to_string(key.selected_index) + "\">" + encode(key.library_bytes) + "</snapshot>\n";
    for (const auto &id : key.transform_ids)
      out += "    <transform id=\"" + escape(id) + "\"/>\n";
    out += "  </keyframe>\n";
    if (out.size() > animation_payload_limit) fail("serialized animation exceeds 8 MiB");
  }
  out += "</apophysis-animation>\n";
  if (out.size() > animation_payload_limit) fail("serialized animation exceeds 8 MiB");
  return out;
}
} // namespace apo
