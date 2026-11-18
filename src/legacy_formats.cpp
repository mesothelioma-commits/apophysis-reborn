#include "legacy_formats.hpp"
#include "flame_io.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace apo {
namespace {
constexpr std::size_t text_limit = 8 * 1024 * 1024;
[[noreturn]] void bad(const std::string &what) {
  throw std::invalid_argument("Legacy format: " + what);
}
struct Token {
  std::string value;
  std::size_t begin, end;
  bool quoted = false;
};
std::vector<Token> tokenize(std::string_view text) {
  if (text.size() > text_limit)
    bad("input exceeds 8 MiB limit");
  std::vector<Token> tokens;
  for (std::size_t i = 0; i < text.size();) {
    const auto c = text[i];
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      ++i;
      continue;
    }
    if (c == ';') {
      while (i < text.size() && text[i] != '\n')
        ++i;
      continue;
    }
    if (c == '\\' && i + 1 < text.size() &&
        (text[i + 1] == '\r' || text[i + 1] == '\n')) {
      ++i;
      continue;
    }
    Token token;
    token.begin = i;
    if (c == '\"') {
      token.quoted = true;
      ++i;
      bool ended = false;
      while (i < text.size()) {
        char ch = text[i++];
        if (ch == '\"') {
          ended = true;
          break;
        }
        if (ch == '\\' && i < text.size()) {
          if (text[i] == '\"' || text[i] == '\\')
            ch = text[i++];
          else if (text[i] == '\r' || text[i] == '\n') {
            while (i < text.size() && (text[i] == '\r' || text[i] == '\n' ||
                                       text[i] == ' ' || text[i] == '\t'))
              ++i;
            continue;
          }
        }
        if (static_cast<unsigned char>(ch) < 32 && ch != '\t')
          bad("control character in quoted value");
        token.value += ch;
      }
      if (!ended)
        bad("unterminated quoted value");
    } else if (c == '{' || c == '}' || c == ':' || c == '=') {
      token.value += c;
      ++i;
    } else {
      while (i < text.size()) {
        const char ch = text[i];
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '{' ||
            ch == '}' || ch == ':' || ch == '=' || ch == ';')
          break;
        if (static_cast<unsigned char>(ch) < 32)
          bad("control character in input");
        token.value += ch;
        ++i;
      }
      if (token.value.empty())
        bad("invalid token");
    }
    token.end = i;
    if (token.value.size() > 1024 * 1024)
      bad("value exceeds 1 MiB");
    tokens.push_back(std::move(token));
    if (tokens.size() > 262144)
      bad("too many tokens");
  }
  return tokens;
}
struct Section {
  std::string name;
  std::vector<std::pair<std::string, std::string>> values;
};
struct Entry {
  std::string name, source;
  std::vector<Section> sections;
};
std::vector<Entry> entries(std::string_view text) {
  auto tokens = tokenize(text);
  std::vector<Entry> result;
  std::size_t i = 0;
  auto is = [&](std::size_t p, std::string_view value) {
    return p < tokens.size() && !tokens[p].quoted && tokens[p].value == value;
  };
  while (i < tokens.size()) {
    const auto begin = tokens[i].begin;
    Entry entry;
    entry.name = tokens[i++].value;
    if (!is(i, "{"))
      bad("expected '{' after entry name");
    ++i;
    while (!is(i, "}")) {
      if (i >= tokens.size())
        bad("unterminated entry");
      if (is(i + 1, ":")) {
        entry.sections.push_back({tokens[i].value, {}});
        i += 2;
        continue;
      }
      if (entry.sections.empty() || i + 2 >= tokens.size() || !is(i + 1, "=") ||
          is(i + 2, "{") || is(i + 2, "}") || is(i + 2, ":") || is(i + 2, "="))
        bad("expected section or key=value");
      entry.sections.back().values.emplace_back(tokens[i].value,
                                                tokens[i + 2].value);
      i += 3;
    }
    entry.source = std::string(text.substr(begin, tokens[i].end - begin));
    ++i;
    result.push_back(std::move(entry));
    if (result.size() > 4096)
      bad("too many entries");
  }
  if (result.empty())
    bad("no entries");
  return result;
}
std::string lower(std::string value) {
  for (auto &c : value)
    if (c >= 'A' && c <= 'Z')
      c = static_cast<char>(c - 'A' + 'a');
  return value;
}
double number(std::string_view text, const std::string &field) {
  double value = 0;
  auto [p, e] = std::from_chars(text.data(), text.data() + text.size(), value,
                                std::chars_format::general);
  if (text.empty() || e != std::errc{} || p != text.data() + text.size() ||
      !std::isfinite(value))
    bad("invalid finite number for " + field);
  return value;
}
std::int64_t integer(std::string_view text, const std::string &field) {
  std::int64_t value = 0;
  auto [p, e] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (text.empty() || e != std::errc{} || p != text.data() + text.size())
    bad("invalid integer for " + field);
  return value;
}
int rgb8(double channel) {
  if (!std::isfinite(channel) || channel < 0 || channel > 1)
    bad("palette RGB channel outside [0,1]");
  return static_cast<int>(std::lround(channel * 255));
}
int round_even(double x) {
  const auto base = std::floor(x);
  const double remainder = x - base;
  return static_cast<int>(
      base + (remainder > 0.5 || (remainder == 0.5 && std::fmod(base, 2) != 0)
                  ? 1
                  : 0));
}
std::string uf_quote(std::string_view value) {
  std::string s = "\"";
  for (char c : value) {
    if (c == '\n' || c == '\r' || static_cast<unsigned char>(c) < 32)
      c = ' ';
    if (c == '\\' || c == '\"')
      s += '\\';
    s += c;
  }
  return s + '\"';
}
std::string identifier(std::string_view name) {
  std::string s;
  for (unsigned char c : name)
    s += ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '_' || c == '-')
             ? static_cast<char>(c)
             : '_';
  return s.empty() ? "Apophysis_Reborn" : s;
}
const Section *one_section(const Entry &entry, std::string_view name) {
  const Section *result = nullptr;
  for (const auto &s : entry.sections)
    if (s.name == name) {
      if (result)
        bad("duplicate " + std::string(name) + " section");
      result = &s;
    }
  return result;
}
std::map<std::string, std::string> fields(const Section *section) {
  std::map<std::string, std::string> m;
  if (!section)
    return m;
  for (const auto &[k, v] : section->values) {
    auto [i, ok] = m.emplace(k, v);
    if (!ok && i->second != v)
      bad("conflicting duplicate field " + k);
  }
  return m;
}
using Fields = std::map<std::string, std::string>;
std::string get(const Fields &m, const std::string &key,
                std::string fallback = {}) {
  auto i = m.find(key);
  return i == m.end() ? fallback : i->second;
}
double real(const Fields &m, const std::string &key, double fallback) {
  auto i = m.find(key);
  return i == m.end() ? fallback : number(i->second, key);
}
void supported_fields(const Fields &values, const std::set<std::string> &names,
                      FormatReport &report, const std::string &section) {
  for (const auto &[k, v] : values) {
    (void)v;
    if (!names.contains(k))
      report.errors.push_back("Unsupported " + section + " field: " + k);
  }
}
PaletteEntry gradient(const Entry &entry) {
  PaletteEntry result;
  result.name = entry.name;
  const auto *section = one_section(entry, "gradient");
  if (!section) {
    result.report.errors.push_back("Missing gradient section");
    return result;
  }
  for (const auto &s : entry.sections)
    if (s.name == "opacity")
      result.report.errors.push_back(
          "UGR opacity gradients are not supported by RGB flame palettes");
  std::map<int, std::array<int, 3>> stops;
  std::optional<int> pending;
  for (const auto &[key, value] : section->values) {
    if (key == "title")
      result.name = value;
    else if (key == "smooth") {
      if (lower(value) != "no")
        result.report.errors.push_back(
            "Only smooth=no linear UGR gradients are supported");
    } else if (key == "index") {
      if (pending)
        bad("gradient index has no color");
      auto index = integer(value, "gradient index");
      if (index < 0)
        index = (index % 400 + 400) % 400;
      if (index > 399)
        bad("UGR gradient index must be in 0..399 (negative indices wrap)");
      pending = round_even(static_cast<double>(index) * 255 / 399);
    } else if (key == "color") {
      if (!pending)
        bad("gradient color has no preceding index");
      const auto color = integer(value, "gradient color");
      if (color < 0 || color > 0xffffff)
        bad("UGR color must be a 24-bit RGB value");
      stops[*pending] = {static_cast<int>(color & 255),
                         static_cast<int>((color >> 8) & 255),
                         static_cast<int>((color >> 16) & 255)};
      pending.reset();
    } else
      result.report.errors.push_back("Unsupported gradient field: " + key);
  }
  if (pending)
    bad("gradient index has no color");
  if (stops.empty())
    bad("gradient contains no color stops");
  std::array<std::array<int, 3>, 256> colors{};
  if (stops.size() == 1)
    colors.fill(stops.begin()->second);
  else {
    for (auto i = stops.begin(); i != stops.end(); ++i) {
      auto j = std::next(i);
      const int begin = i->first;
      int end;
      std::array<int, 3> next;
      if (j == stops.end()) {
        end = stops.begin()->first + 256;
        next = stops.begin()->second;
      } else {
        end = j->first;
        next = j->second;
      }
      for (int channel = 0; channel < 3; ++channel) {
        double c = i->second[static_cast<std::size_t>(channel)];
        const double step =
            static_cast<double>(next[static_cast<std::size_t>(channel)] - c) /
            (end - begin);
        colors[static_cast<std::size_t>(begin)]
              [static_cast<std::size_t>(channel)] = static_cast<int>(c);
        for (int k = begin + 1; k < end; ++k) {
          c += step;
          colors[static_cast<std::size_t>(k % 256)]
                [static_cast<std::size_t>(channel)] = round_even(c);
        }
      }
    }
  }
  for (std::size_t i = 0; i < 256; ++i)
    result.palette[i] = {colors[i][0] / 255.0, colors[i][1] / 255.0,
                         colors[i][2] / 255.0};
  return result;
}
std::string gradient_text(const Palette &palette, std::string_view name,
                          bool title) {
  std::string text = "gradient:\n";
  if (title)
    text += "  title=" + uf_quote(name) + " smooth=no\n";
  for (std::size_t i = 0; i < 256; ++i) {
    const auto &c = palette[i];
    const int packed = rgb8(c.r) | (rgb8(c.g) << 8) | (rgb8(c.b) << 16);
    text += "  index=" +
            std::to_string(round_even(static_cast<double>(i) * 399 / 255)) +
            " color=" + std::to_string(packed) + "\n";
  }
  return text;
}
std::string decimal(double value) {
  std::ostringstream s;
  s.imbue(std::locale::classic());
  s << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
  return s.str();
}
bool identity(const Affine &a) {
  return a.a == 1 && a.b == 0 && a.c == 0 && a.d == 1 && a.e == 0 && a.f == 0;
}
// Ultra Fractal uses a 4-by-3 reference frame, fitting the declared image into
// it.
double camera_units(int width, int height) {
  return (4LL * height < 3LL * width) ? height / 3.0 : width / 4.0;
}
} // namespace
std::vector<PaletteEntry> import_ugr(std::string_view text) {
  std::vector<PaletteEntry> result;
  for (const auto &entry : entries(text)) {
    auto p = gradient(entry);
    for (const auto &s : entry.sections)
      if (s.name != "gradient" && s.name != "opacity")
        p.report.errors.push_back("Unsupported UGR section: " + s.name);
    result.push_back(std::move(p));
  }
  return result;
}
std::string export_ugr(const Palette &palette, std::string_view name) {
  return identifier(name) + " {\n" + gradient_text(palette, name, true) + "}\n";
}
Palette import_fractint_map(std::string_view text) {
  if (text.size() > text_limit)
    bad("MAP input exceeds 8 MiB");
  Palette palette{};
  std::size_t count = 0;
  std::istringstream in{std::string(text)};
  in.imbue(std::locale::classic());
  std::string line;
  while (std::getline(in, line)) {
    const auto first = line.find_first_not_of(" \t\r");
    if (first == std::string::npos || line[first] == ';' || line[first] == '#')
      continue;
    if (count == 256)
      bad("MAP requires exactly 256 RGB rows");
    std::istringstream row(line);
    row.imbue(std::locale::classic());
    std::string r, g, b;
    if (!(row >> r >> g >> b))
      bad("MAP row must contain three RGB integers");
    const auto rr = integer(r, "red"), gg = integer(g, "green"),
               bb = integer(b, "blue");
    if (rr < 0 || rr > 255 || gg < 0 || gg > 255 || bb < 0 || bb > 255)
      bad("MAP RGB values must be in 0..255");
    palette[count++] = {rr / 255.0, gg / 255.0, bb / 255.0};
  }
  if (count != 256)
    bad("MAP requires exactly 256 RGB rows");
  return palette;
}
std::string export_fractint_map(const Palette &palette, std::string_view name) {
  std::string text;
  for (std::size_t i = 0; i < palette.size(); ++i) {
    const auto &c = palette[i];
    text += std::to_string(rgb8(c.r)) + " " + std::to_string(rgb8(c.g)) + " " +
            std::to_string(rgb8(c.b));
    if (i == 0 && !name.empty())
      text += " ; " + identifier(name);
    text += '\n';
  }
  return text;
}
std::vector<UprEntry> import_upr(std::string_view text) {
  std::vector<UprEntry> result;
  for (const auto &entry : entries(text)) {
    UprEntry out;
    out.name = entry.name;
    out.source = entry.source;
    try {
      auto fractal = fields(one_section(entry, "fractal")),
           layer = fields(one_section(entry, "layer")),
           mapping = fields(one_section(entry, "mapping"));
      auto formula = fields(one_section(entry, "formula")),
           inside = fields(one_section(entry, "inside")),
           outside = fields(one_section(entry, "outside"));
      out.name = get(fractal, "title", entry.name);
      if (lower(get(formula, "filename")) != "mt.ufm" ||
          get(formula, "entry") != "mt-pixel")
        out.report.errors.push_back(
            "Unsupported UPR fractal formula; expected mt.ufm / mt-pixel");
      if (lower(get(outside, "filename")) != "apophysis.ucl" ||
          (get(outside, "entry") != "enr-flame-a" &&
           get(outside, "entry") != "apophysis-205"))
        out.report.errors.push_back(
            "Unsupported UPR coloring formula; expected historical "
            "apophysis.ucl flame coloring");
      for (const auto &s : entry.sections)
        if (s.name != "fractal" && s.name != "layer" && s.name != "mapping" &&
            s.name != "formula" && s.name != "inside" && s.name != "outside" &&
            s.name != "gradient")
          out.report.errors.push_back("Unsupported UPR section: " + s.name);
      supported_fields(fractal, {"title", "width", "height", "layers"},
                       out.report, "fractal");
      supported_fields(layer, {"method", "caption", "opacity", "mergemode"},
                       out.report, "layer");
      supported_fields(mapping, {"center", "magn"}, out.report, "mapping");
      supported_fields(formula, {"maxiter", "filename", "entry"}, out.report,
                       "formula");
      supported_fields(inside, {"transfer"}, out.report, "inside");
      if (real(fractal, "layers", 1) != 1 ||
          get(layer, "method", "linear") != "linear" ||
          real(layer, "opacity", 100) != 100 ||
          get(layer, "mergemode", "normal") != "normal")
        out.report.errors.push_back(
            "Only one opaque, normal, linear UPR layer is supported");
      if (real(formula, "maxiter", 1) != 1 ||
          get(inside, "transfer", "none") != "none" ||
          get(outside, "transfer", "linear") != "linear" ||
          get(outside, "repeat", "no") != "no")
        out.report.errors.push_back(
            "Unsupported UPR formula iteration or transfer settings");
      if (real(outside, "p_contrast", 1) != 1 ||
          real(outside, "p_white_level", 200) != 200)
        out.report.errors.push_back(
            "UPR contrast must be 1 and white level must be 200");
      const auto count = integer(get(outside, "p_xforms", "0"), "p_xforms");
      if (count < 1 || count > 64)
        out.report.errors.push_back("UPR transform count must be 1..64");
      if (!out.report.supported()) {
        result.push_back(std::move(out));
        continue;
      }
      Flame f;
      f.name = out.name;
      f.source_version = "Apophysis 2.09 (UPR)";
      const auto width = integer(get(fractal, "width", "640"), "width"),
                 height = integer(get(fractal, "height", "480"), "height");
      if (width < 1 || height < 1 || width > 65536 || height > 65536)
        bad("UPR dimensions must be in 1..65536");
      f.width = static_cast<int>(width);
      f.height = static_cast<int>(height);
      const auto center = get(mapping, "center", "0/0");
      const auto slash = center.find('/');
      if (slash == std::string::npos ||
          center.find('/', slash + 1) != std::string::npos)
        bad("UPR center must have x/y form");
      f.center = {number(center.substr(0, slash), "center x"),
                  -number(center.substr(slash + 1), "center y")};
      const double magn = real(mapping, "magn", 1);
      if (magn <= 0)
        bad("UPR magnification must be positive");
      f.scale = magn * camera_units(f.width, f.height);
      f.zoom = 0;
      f.quality = real(outside, "p_iter_density", 50);
      f.filter_radius = real(outside, "p_spat_filt_rad", 0.5);
      const auto oversample =
          integer(get(outside, "p_oversample", "1"), "p_oversample");
      if (oversample < 1 || oversample > 16)
        bad("UPR oversample must be in 1..16");
      f.oversample = static_cast<int>(oversample);
      f.gamma = real(outside, "p_gamma", 1);
      f.brightness = real(outside, "p_brightness", 1);
      f.vibrancy = 1;
      const auto packed =
          integer(get(outside, "p_bk_color", "-16777216"), "p_bk_color");
      if (packed < std::numeric_limits<std::int32_t>::min() ||
          packed > std::numeric_limits<std::uint32_t>::max())
        bad("UPR background must be a packed 32-bit color");
      const auto color = static_cast<std::uint32_t>(packed);
      if ((color >> 24) != 255)
        out.report.errors.push_back(
            "Transparent UPR backgrounds are unsupported");
      f.background = {((color >> 16) & 255) / 255.0,
                      ((color >> 8) & 255) / 255.0, (color & 255) / 255.0};
      auto palette = gradient(entry);
      f.palette = palette.palette;
      out.report.errors.insert(out.report.errors.end(),
                               palette.report.errors.begin(),
                               palette.report.errors.end());
      std::set<std::string> used = {
          "transfer",       "repeat",          "filename",     "entry",
          "p_iter_density", "p_spat_filt_rad", "p_oversample", "p_bk_color",
          "p_contrast",     "p_brightness",    "p_gamma",      "p_white_level",
          "p_xforms"};
      bool ignored_parameters = false;
      auto transform = [&](const std::string &prefix) {
        Transform t;
        t.parameters.apophysis_math = true;
        t.variations.clear();
        auto v = [&](const std::string &key, double fallback) {
          used.insert(prefix + key);
          return real(outside, prefix + key, fallback);
        };
        t.weight = v("_p", 1);
        t.color = v("_c", 0);
        t.color_speed = (1 - v("_sym", 0)) / 2;
        t.pre = {v("_cfa", 1), v("_cfc", 0), v("_cfb", 0),
                 v("_cfd", 1), v("_cfe", 0), v("_cff", 0)};
        std::set<std::string> active;
        for (const auto &[k, value] : outside)
          if (k.starts_with(prefix + "_var_")) {
            used.insert(k);
            const auto name = k.substr(prefix.size() + 5);
            const auto weight = number(value, k);
            if (weight == 0)
              continue;
            const auto id = find_variation(name);
            if (!id || *id == Variation::plugin)
              out.report.errors.push_back("Unsupported UPR variation: " + name);
            else {
              t.variations.push_back({*id, weight});
              active.insert(name);
            }
          }
        if (t.variations.empty())
          out.report.errors.push_back(prefix +
                                      " has no supported nonzero variation");
        for (const auto &[k, value] : outside)
          if (k.starts_with(prefix + "_par_")) {
            used.insert(k);
            const auto name = k.substr(prefix.size() + 5);
            if (name == "julian_power" && active.contains("julian")) {
              const auto power = integer(value, k);
              if (power < -1024 || power > 1024 || power == 0)
                bad("julian power outside supported range");
              t.parameters.julian_power = static_cast<int>(power);
            } else if (const auto *field = find_variation_parameter(name);
                       field && active.contains(std::string(variation_name(field->variation))))
              field->set(t.parameters, number(value, k));
            else {
              bool affects = false;
              for (const auto &a : active)
                if (name.starts_with(a + "_"))
                  affects = true;
              if (affects)
                out.report.errors.push_back(
                    "Unsupported active variation parameter: " + name);
              else
                ignored_parameters = true;
            }
          }
        for (const auto& field : variation_parameter_registry())
          if (active.contains(std::string(variation_name(field.variation))) &&
              needs_explicit_variation_parameter(field, Producer::classic) &&
              !outside.contains(prefix + "_par_" + std::string(field.name)))
            out.report.errors.push_back("UPR requires explicit parameter: " + std::string(field.name));
        return t;
      };
      for (std::int64_t i = 0; i < count; ++i)
        f.transforms.push_back(transform("p_xf" + std::to_string(i)));
      bool has_final = false;
      for (const auto &[k, v] : outside) {
        (void)v;
        if (k.starts_with("p_finalxf_"))
          has_final = true;
      }
      if (has_final)
        f.final_transform = transform("p_finalxf");
      supported_fields(outside, used, out.report, "outside");
      if (ignored_parameters)
        out.report.warnings.push_back(
            "Inactive variation parameter values are retained in the original "
            "UPR entry, but are not applied to its flame model");
      if (out.report.supported()) {
        validate(f);
        out.flame = std::move(f);
        out.report.warnings.push_back(
            "Imported the Apophysis flame parameters; Ultra Fractal coloring "
            "and sampling are not pixel-equivalent to the Reborn renderer");
      }
    } catch (const std::exception &e) {
      out.report.errors.push_back(e.what());
      out.flame.reset();
    }
    result.push_back(std::move(out));
  }
  return result;
}
UprExport export_upr(const Flame &f) {
  UprExport out;
  try {
    validate(f);
  } catch (const std::exception &e) {
    out.report.errors.push_back(e.what());
    return out;
  }
  if (f.rotate != 0)
    out.report.errors.push_back("UPR export does not support camera rotation");
  if (f.vibrancy != 1 || f.gamma_threshold != 0)
    out.report.errors.push_back(
        "UPR export requires vibrancy=1 and gamma_threshold=0");
  if (f.palette_mode != PaletteMode::step)
    out.report.errors.push_back("UPR export requires a step palette");
  if (!f.filter_radius)
    out.report.errors.push_back(
        "UPR export requires an explicit spatial filter radius");
  if (f.transforms.size() > 64)
    out.report.errors.push_back("UPR export supports at most 64 transforms");
  auto check = [&](const Transform &t) {
    if (!identity(t.post))
      out.report.errors.push_back(
          "UPR export cannot represent post affine transforms");
    if (t.opacity != 1)
      out.report.errors.push_back(
          "UPR export cannot represent transform opacity");
    if (std::any_of(t.chaos.begin(), t.chaos.end(),
                    [](double x) { return x != 1; }))
      out.report.errors.push_back("UPR export cannot represent chaos weights");
    for (const auto &term : t.variations)
      if (term.id == Variation::plugin || term.plugin)
        out.report.errors.push_back(
            "UPR export cannot represent native plugin variations");
    if (t.parameters.separate_blur_angle)
      out.report.errors.push_back(
          "UPR export cannot represent 7X pre-blur angle semantics");
    if (!t.parameters.apophysis_math)
      for (const auto &term : t.variations)
        if (term.weight != 0 && term.id != Variation::linear)
          out.report.errors.push_back("UPR export requires Apophysis variation "
                                      "math for nonlinear transforms");
  };
  for (const auto &t : f.transforms)
    check(t);
  if (f.final_transform)
    check(*f.final_transform);
  if (!out.report.supported())
    return out;
  const double magn =
      f.scale * std::exp2(f.zoom) / camera_units(f.width, f.height);
  if (!std::isfinite(magn) || magn <= 0) {
    out.report.errors.push_back(
        "UPR camera magnification is outside the supported range");
    return out;
  }
  std::string text =
      identifier(f.name) + " {\nfractal:\n  title=" + uf_quote(f.name) +
      " width=" + std::to_string(f.width) +
      " height=" + std::to_string(f.height) +
      " layers=1\nlayer:\n  method=linear caption=\"Background\" opacity=100 "
      "mergemode=normal\nmapping:\n  center=" +
      decimal(f.center.x) + "/" + decimal(-f.center.y) +
      " magn=" + decimal(magn) +
      "\nformula:\n  maxiter=1 filename=\"mt.ufm\" "
      "entry=\"mt-pixel\"\ninside:\n  transfer=none\noutside:\n  "
      "transfer=linear repeat=no filename=\"apophysis.ucl\" "
      "entry=\"enr-flame-a\"\n";
  auto add = [&](const std::string &k, double v) {
    text += "  " + k + "=" + decimal(v) + "\n";
  };
  add("p_iter_density", f.quality);
  add("p_spat_filt_rad", *f.filter_radius);
  add("p_oversample", f.oversample);
  const auto packed = 0xff000000u |
                      (static_cast<std::uint32_t>(rgb8(f.background.r)) << 16) |
                      (static_cast<std::uint32_t>(rgb8(f.background.g)) << 8) |
                      static_cast<std::uint32_t>(rgb8(f.background.b));
  text += "  p_bk_color=" +
          std::to_string(static_cast<std::int64_t>(packed) - 0x100000000LL) +
          " p_contrast=1\n";
  add("p_brightness", f.brightness);
  add("p_gamma", f.gamma);
  text +=
      "  p_white_level=200 p_xforms=" + std::to_string(f.transforms.size()) +
      "\n";
  auto write_transform = [&](const Transform &t, const std::string &prefix) {
    add(prefix + "_p", t.weight);
    add(prefix + "_c", t.color);
    add(prefix + "_sym", 1 - 2 * t.color_speed);
    add(prefix + "_cfa", t.pre.a);
    add(prefix + "_cfb", t.pre.c);
    add(prefix + "_cfc", t.pre.b);
    add(prefix + "_cfd", t.pre.d);
    add(prefix + "_cfe", t.pre.e);
    add(prefix + "_cff", t.pre.f);
    for (const auto &term : t.variations)
      if (term.weight != 0)
        add(prefix + "_var_" + std::string(variation_name(term.id)),
            term.weight);
    for (const auto &term : t.variations)
      if (term.weight != 0)
        for (const auto &field : variation_parameter_registry())
          if (field.variation == term.id)
            add(prefix + "_par_" + std::string(field.name), field.get(t.parameters));
  };
  for (std::size_t i = 0; i < f.transforms.size(); ++i)
    write_transform(f.transforms[i], "p_xf" + std::to_string(i));
  if (f.final_transform)
    write_transform(*f.final_transform, "p_finalxf");
  out.text = text + gradient_text(f.palette, {}, false) + "}\n";
  out.report.warnings.push_back(
      "Requires the historical mt.ufm / mt-pixel and apophysis.ucl / "
      "enr-flame-a formulas in Ultra Fractal; Reborn and Ultra Fractal "
      "renderers are not pixel-equivalent");
  out.report.warnings.push_back(
      "UPR stores the effective camera scale; separate zoom, animation time, "
      "and source-version metadata are not retained");
  out.report.warnings.push_back(
      "UPR palette and background colors are quantized to 8-bit RGB channels");
  return out;
}
} // namespace apo
