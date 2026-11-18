#include "script.hpp"
#include "creative.hpp"
#include "flame_io.hpp"
#include "lauxlib.h"
#include "legacy_script.hpp"
#include "lua.h"
#include "lualib.h"
#include "plugin.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <memory>
#include <numbers>
#include <set>
#include <sstream>

namespace apo {
namespace {
using Clock = std::chrono::steady_clock;
std::uint64_t model_bytes(const Flame &f) {
  std::uint64_t size =
      sizeof(f) + f.name.size() * 6 + f.source_version.size() * 6;
  const auto add = [&](const Transform &t) {
    std::uint64_t n = sizeof(t) + t.chaos.size() * sizeof(double);
    for (const auto &v : t.variations)
      n += sizeof(v) + v.plugin_parameters.size() * sizeof(double) +
           term_name(v).size() * 6;
    return n;
  };
  for (const auto &t : f.transforms)
    size += add(t);
  if (f.final_transform)
    size += add(*f.final_transform);
  return size;
}
struct Context {
  const ScriptOptions &options;
  LegacyDocument document;
  Flame flame;
  std::size_t selected;
  std::vector<std::optional<std::size_t>> transform_sources;
  Rng rng;
  Clock::time_point started = Clock::now();
  std::uint64_t allocated = 0, peak_lua = 0, peak_host = 0, instructions = 0;
  std::size_t host_calls = 0, console_bytes = 0, source_bytes = 0,
              translated_bytes = 0;
  bool dirty = false, cancelled = false, timed_out = false,
       memory_failed = false;
  int active_transform = 0;
  std::string fatal;
  std::vector<std::string> console;
  std::vector<ScriptRequest> requests;
  std::string render_filename = "script.png";
  int render_width = 0, render_height = 0;
  std::uint64_t render_memory = 512;

  Context(const LegacyDocument &doc, std::size_t index,
          const ScriptOptions &opts, std::size_t source,
          Clock::time_point start)
      : options(opts), document(doc), flame(doc.render_model(index)),
        selected(index), rng(opts.seed), started(start), source_bytes(source) {
    reset_sources();
  }
  void reset_sources() {
    transform_sources.clear();
    for (std::size_t i = 0; i < flame.transforms.size(); ++i)
      transform_sources.push_back(i);
  }
  void check() {
    if (options.stop_token.stop_requested() || cancelled) {
      cancelled = true;
      throw ScriptCancelled();
    }
    if (timed_out || Clock::now() - started >=
                         std::chrono::milliseconds(options.limits.timeout_ms)) {
      timed_out = true;
      throw ScriptError("Script time limit exceeded");
    }
    if (!fatal.empty())
      throw ScriptError(fatal);
  }
  void host_check(std::uint64_t reserve = 0) {
    check();
    const auto &limit = options.limits;
    if (document.bytes().size() > limit.document_bytes)
      throw ScriptError("Script document byte limit exceeded");
    if (document.entries().size() > limit.flame_count)
      throw ScriptError("Script flame count limit exceeded");
    if (flame.transforms.size() > limit.transforms_per_flame)
      throw ScriptError("Script transform count limit exceeded");
    // Conservative reservations cover source indexing, canonical merge
    // temporaries, and value snapshots; this is a host data budget, not process
    // RSS accounting.
    std::uint64_t total = document.bytes().size() * 4ULL +
                          model_bytes(flame) * 12ULL + source_bytes * 4ULL +
                          translated_bytes * 4ULL + console_bytes * 2ULL +
                          reserve;
    for (const auto &[name, bytes] : options.imports)
      total += name.size() + bytes.size() * 2ULL;
    for (const auto &r : requests)
      total += model_bytes(r.flame) * 2ULL + r.bytes.size() * 2ULL +
               r.destination.native().size() * 4ULL + sizeof(r);
    peak_host = std::max(peak_host, total);
    if (total > limit.host_memory_bytes)
      throw ScriptError("Script host memory limit exceeded");
  }
  void begin_call() {
    check();
    if (++host_calls > options.limits.host_calls)
      throw ScriptError("Script host call limit exceeded");
  }
  void flush() {
    host_check();
    if (dirty) {
      validate(flame);
      document.apply_model(selected, flame, transform_sources);
      flame = document.render_model(selected);
      reset_sources();
      dirty = false;
      host_check();
    }
  }
  void line(std::string value) {
    if (value.size() + 1 > options.limits.console_bytes - console_bytes)
      throw ScriptError("Script console output limit exceeded");
    console_bytes += value.size() + 1;
    console.push_back(std::move(value));
  }
  Transform &active() {
    if (active_transform < 0 ||
        std::size_t(active_transform) >= flame.transforms.size())
      throw ScriptError(
          "Legacy ActiveTransform is outside the ordinary transform range");
    return flame.transforms[std::size_t(active_transform)];
  }
};
Context &context(lua_State *L) {
  return **static_cast<Context **>(lua_getextraspace(L));
}
void *allocate(void *ud, void *ptr, std::size_t old_size,
               std::size_t size) noexcept {
  auto &c = *static_cast<Context *>(ud);
  if (!ptr)
    old_size = 0;
  if (!size) {
    std::free(ptr);
    c.allocated -= old_size;
    return nullptr;
  }
  if (c.options.stop_token.stop_requested()) {
    c.cancelled = true;
    return nullptr;
  }
  if (Clock::now() - c.started >=
      std::chrono::milliseconds(c.options.limits.timeout_ms)) {
    c.timed_out = true;
    return nullptr;
  }
  if (size > c.options.limits.lua_memory_bytes ||
      c.allocated - old_size > c.options.limits.lua_memory_bytes - size) {
    c.memory_failed = true;
    return nullptr;
  }
  void *result = std::realloc(ptr, size);
  if (result) {
    c.allocated = c.allocated - old_size + size;
    c.peak_lua = std::max(c.peak_lua, c.allocated);
  } else
    c.memory_failed = true;
  return result;
}
void instruction_hook(lua_State *L, lua_Debug *) {
  auto &c = context(L);
  c.instructions += static_cast<unsigned>(lua_gethookcount(L));
  try {
    c.check();
    if (c.instructions >= c.options.limits.instructions)
      throw ScriptError("Script instruction limit exceeded");
  } catch (const ScriptCancelled &) {
    c.cancelled = true;
    luaL_error(L, "Script execution stopped");
  } catch (const std::exception &e) {
    c.fatal = e.what();
    luaL_error(L, "%s", e.what());
  }
}
std::string string(lua_State *L, int i) {
  if (lua_type(L, i) != LUA_TSTRING)
    throw ScriptError("Expected a string");
  std::size_t n = 0;
  const auto *p = lua_tolstring(L, i, &n);
  if (n > 65536)
    throw ScriptError("String argument exceeds the 65536-byte host limit");
  context(L).host_check(n * 2ULL);
  return {p, n};
}
double number(lua_State *L, int i) {
  if (lua_type(L, i) != LUA_TNUMBER)
    throw ScriptError("Expected a finite number");
  const double n = lua_tonumber(L, i);
  if (!std::isfinite(n))
    throw ScriptError("Expected a finite number");
  return n;
}
std::int64_t integer(lua_State *L, int i, std::int64_t min, std::int64_t max) {
  int valid = 0;
  const auto n = lua_tointegerx(L, i, &valid);
  if (lua_type(L, i) != LUA_TNUMBER || !valid)
    throw ScriptError("Expected an integer");
  if (n < min || n > max)
    throw ScriptError("Integer argument is outside its allowed range");
  return n;
}
std::uint64_t seed(lua_State *L, int i) {
  if (lua_type(L, i) == LUA_TSTRING) {
    const auto s = string(L, i);
    std::uint64_t n = 0;
    const auto result = std::from_chars(s.data(), s.data() + s.size(), n);
    if (result.ec != std::errc{} || result.ptr != s.data() + s.size())
      throw ScriptError("Seed must be an unsigned decimal integer");
    return n;
  }
  return static_cast<std::uint64_t>(
      integer(L, i, 0, std::numeric_limits<lua_Integer>::max()));
}
bool boolean(lua_State *L, int i) {
  if (lua_type(L, i) != LUA_TBOOLEAN)
    throw ScriptError("Expected a boolean");
  return lua_toboolean(L, i) != 0;
}
void table(lua_State *L, int i) {
  if (lua_type(L, i) != LUA_TTABLE)
    throw ScriptError("Expected a table");
}
void keys(lua_State *L, int index,
          std::initializer_list<std::string_view> allowed) {
  table(L, index);
  index = lua_absindex(L, index);
  lua_pushnil(L);
  while (lua_next(L, index)) {
    const auto key = string(L, -2);
    if (std::find(allowed.begin(), allowed.end(), key) == allowed.end())
      throw ScriptError("Unknown field '" + key + "'");
    lua_pop(L, 1);
  }
}
template <class Fn>
void field(lua_State *L, int index, const char *key, Fn fn) {
  lua_getfield(L, index, key);
  if (!lua_isnil(L, -1))
    fn(-1);
  lua_pop(L, 1);
}
void push(lua_State *L, std::string_view s) {
  lua_pushlstring(L, s.data(), s.size());
}
void put(lua_State *L, const char *key, double value) {
  lua_pushnumber(L, value);
  lua_setfield(L, -2, key);
}
void put(lua_State *L, const char *key, const std::string &value) {
  push(L, value);
  lua_setfield(L, -2, key);
}
void push_array(lua_State *L, std::initializer_list<double> values) {
  lua_createtable(L, static_cast<int>(values.size()), 0);
  int i = 1;
  for (double v : values) {
    lua_pushnumber(L, v);
    lua_rawseti(L, -2, i++);
  }
}
std::vector<double> array(lua_State *L, int index, std::size_t max,
                          std::size_t exact = 0) {
  table(L, index);
  index = lua_absindex(L, index);
  const auto n = lua_rawlen(L, index);
  if (n > max || (exact && n != exact))
    throw ScriptError("Array has an invalid number of elements");
  std::vector<double> values;
  values.reserve(static_cast<std::size_t>(n));
  for (lua_Unsigned i = 1; i <= n; ++i) {
    lua_rawgeti(L, index, static_cast<lua_Integer>(i));
    values.push_back(number(L, -1));
    lua_pop(L, 1);
  }
  return values;
}
Affine affine(lua_State *L, int index) {
  const auto v = array(L, index, 6, 6);
  return {v[0], v[1], v[2], v[3], v[4], v[5]};
}
std::map<std::string, double> parameters(lua_State *L, int index) {
  table(L, index);
  index = lua_absindex(L, index);
  std::map<std::string, double> out;
  lua_pushnil(L);
  while (lua_next(L, index)) {
    if (out.size() >= 128)
      throw ScriptError("Too many variation parameters");
    const auto key = string(L, -2);
    if (key.size() > 128)
      throw ScriptError("Variation parameter name is too long");
    out.emplace(key, number(L, -1));
    lua_pop(L, 1);
  }
  return out;
}
Term script_term(const std::string &name, double weight,
                 const std::map<std::string, double> &values, VariationParameters &parameters) {
  const auto id = find_variation(name);
  if (!id) return make_variation_term(name, weight, values);
  for (const auto &[key, value] : values) {
    auto field = find_variation_parameter(name + "_" + key);
    if (!field || field->variation != *id)
      throw ScriptError("Unknown " + name + " parameter: " + key);
    field->set(parameters, value);
  }
  return make_variation_term(name, weight);
}
std::vector<Term> terms(lua_State *L, int index, VariationParameters &variationParameters) {
  table(L, index);
  index = lua_absindex(L, index);
  std::vector<Term> out;
  const auto n = lua_rawlen(L, index);
  if (n > 128)
    throw ScriptError("Too many variation terms");
  if (n) {
    for (lua_Unsigned i = 1; i <= n; ++i) {
      lua_rawgeti(L, index, static_cast<lua_Integer>(i));
      keys(L, -1, {"name", "weight", "parameters"});
      const int item = lua_absindex(L, -1);
      std::string name;
      double weight = 1;
      std::map<std::string, double> params;
      field(L, item, "name", [&](int k) { name = string(L, k); });
      field(L, item, "weight", [&](int k) { weight = number(L, k); });
      field(L, item, "parameters", [&](int k) { params = parameters(L, k); });
      out.push_back(script_term(name, weight, params, variationParameters));
      lua_pop(L, 1);
    }
  } else {
    std::map<std::string, Term> ordered;
    lua_pushnil(L);
    while (lua_next(L, index)) {
      if (ordered.size() >= 128)
        throw ScriptError("Too many variation terms");
      const auto name = string(L, -2);
      double weight = 1;
      std::map<std::string, double> params;
      if (lua_type(L, -1) == LUA_TNUMBER)
        weight = number(L, -1);
      else {
        keys(L, -1, {"weight", "parameters"});
        const int item = lua_absindex(L, -1);
        field(L, item, "weight", [&](int k) { weight = number(L, k); });
        field(L, item, "parameters", [&](int k) { params = parameters(L, k); });
      }
      ordered.emplace(name, script_term(name, weight, params, variationParameters));
      lua_pop(L, 1);
    }
    for (auto &[name, term] : ordered)
      out.push_back(std::move(term));
  }
  return out;
}
Transform read_transform(lua_State *L, int index, Transform t,
                         const Context &c) {
  keys(L, index,
       {"pre", "post", "weight", "color", "color_speed", "opacity",
        "julian_power", "julian_dist", "chaos", "variations"});
  index = lua_absindex(L, index);
  field(L, index, "pre", [&](int k) { t.pre = affine(L, k); });
  field(L, index, "post", [&](int k) { t.post = affine(L, k); });
  field(L, index, "weight", [&](int k) { t.weight = number(L, k); });
  field(L, index, "color", [&](int k) { t.color = number(L, k); });
  field(L, index, "color_speed", [&](int k) { t.color_speed = number(L, k); });
  field(L, index, "opacity", [&](int k) { t.opacity = number(L, k); });
  field(L, index, "julian_power", [&](int k) {
    t.parameters.julian_power =
        static_cast<int>(integer(L, k, -100000, 100000));
  });
  field(L, index, "julian_dist",
        [&](int k) { t.parameters.julian_dist = number(L, k); });
  field(L, index, "chaos", [&](int k) {
    t.chaos = array(L, k, c.options.limits.transforms_per_flame);
  });
  field(L, index, "variations", [&](int k) { t.variations = terms(L, k, t.parameters); });
  return t;
}
void push_transform(lua_State *L, const Transform &t) {
  lua_createtable(L, 0, 10);
  push_array(L, {t.pre.a, t.pre.b, t.pre.c, t.pre.d, t.pre.e, t.pre.f});
  lua_setfield(L, -2, "pre");
  push_array(L, {t.post.a, t.post.b, t.post.c, t.post.d, t.post.e, t.post.f});
  lua_setfield(L, -2, "post");
  put(L, "weight", t.weight);
  put(L, "color", t.color);
  put(L, "color_speed", t.color_speed);
  put(L, "opacity", t.opacity);
  lua_pushinteger(L, t.parameters.julian_power);
  lua_setfield(L, -2, "julian_power");
  put(L, "julian_dist", t.parameters.julian_dist);
  lua_createtable(L, static_cast<int>(t.chaos.size()), 0);
  for (std::size_t i = 0; i < t.chaos.size(); ++i) {
    lua_pushnumber(L, t.chaos[i]);
    lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
  }
  lua_setfield(L, -2, "chaos");
  lua_createtable(L, static_cast<int>(t.variations.size()), 0);
  for (std::size_t i = 0; i < t.variations.size(); ++i) {
    const auto &v = t.variations[i];
    lua_createtable(L, 0, 3);
    put(L, "name", std::string(term_name(v)));
    put(L, "weight", v.weight);
    lua_newtable(L);
    for (const auto &[key, val] : term_parameters(v))
      put(L, key.c_str(), val);
    // JuliaN retains its existing top-level fields on a get/set round trip.
    if (v.id != Variation::julian)
      for (const auto &field : variation_parameter_registry())
        if (field.variation == v.id) {
          auto localName = std::string(field.name.substr(term_name(v).size() + 1));
          put(L, localName.c_str(), field.get(t.parameters));
        }
    lua_setfield(L, -2, "parameters");
    lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
  }
  lua_setfield(L, -2, "variations");
}
void push_flame(lua_State *L, const Flame &f) {
  lua_createtable(L, 0, 22);
  put(L, "name", f.name);
  put(L, "source_version", f.source_version);
  for (const auto &[key, value] :
       std::initializer_list<std::pair<const char *, double>>{
           {"time", f.time},
           {"width", double(f.width)},
           {"height", double(f.height)},
           {"scale", f.scale},
           {"zoom", f.zoom},
           {"rotate", f.rotate},
           {"gamma", f.gamma},
           {"brightness", f.brightness},
           {"vibrancy", f.vibrancy},
           {"quality", f.quality},
           {"gamma_threshold", f.gamma_threshold}})
    put(L, key, value);
  // Integers remain integers when a returned settings table is sent back.
  lua_pushinteger(L, f.width);
  lua_setfield(L, -2, "width");
  lua_pushinteger(L, f.height);
  lua_setfield(L, -2, "height");
  lua_pushinteger(L, f.oversample);
  lua_setfield(L, -2, "oversample");
  if (f.filter_radius)
    put(L, "filter_radius", *f.filter_radius);
  push_array(L, {f.center.x, f.center.y});
  lua_setfield(L, -2, "center");
  push_array(L, {f.background.r, f.background.g, f.background.b});
  lua_setfield(L, -2, "background");
  put(L, "palette_mode",
      std::string(f.palette_mode == PaletteMode::linear ? "linear" : "step"));
}
void read_flame(lua_State *L, int index, Flame &f) {
  keys(L, index,
       {"name", "source_version", "time", "width", "height", "center", "scale",
        "zoom", "rotate", "gamma", "brightness", "vibrancy", "quality",
        "gamma_threshold", "oversample", "filter_radius", "clear_filter_radius",
        "background", "palette_mode"});
  index = lua_absindex(L, index);
  field(L, index, "name", [&](int k) {
    const auto value = string(L, k);
    if (value.size() > 4096)
      throw ScriptError("Flame name is too long");
    f.name = value;
  });
  field(L, index, "source_version", [&](int k) {
    if (string(L, k) != f.source_version)
      throw ScriptError("source_version is read-only");
  });
  field(L, index, "width",
        [&](int k) { f.width = static_cast<int>(integer(L, k, 1, 1000000)); });
  field(L, index, "height",
        [&](int k) { f.height = static_cast<int>(integer(L, k, 1, 1000000)); });
  field(L, index, "oversample",
        [&](int k) { f.oversample = static_cast<int>(integer(L, k, 1, 16)); });
  field(L, index, "center", [&](int k) {
    const auto v = array(L, k, 2, 2);
    f.center = {v[0], v[1]};
  });
  field(L, index, "background", [&](int k) {
    const auto v = array(L, k, 3, 3);
    f.background = {v[0], v[1], v[2]};
  });
  field(L, index, "palette_mode", [&](int k) {
    const auto v = string(L, k);
    if (v != "linear" && v != "step")
      throw ScriptError("palette_mode must be linear or step");
    f.palette_mode = v == "linear" ? PaletteMode::linear : PaletteMode::step;
  });
  for (const auto &[key, member] :
       std::initializer_list<std::pair<const char *, double Flame::*>>{
           {"time", &Flame::time},
           {"scale", &Flame::scale},
           {"zoom", &Flame::zoom},
           {"rotate", &Flame::rotate},
           {"gamma", &Flame::gamma},
           {"brightness", &Flame::brightness},
           {"vibrancy", &Flame::vibrancy},
           {"quality", &Flame::quality},
           {"gamma_threshold", &Flame::gamma_threshold}})
    field(L, index, key, [&](int k) { f.*member = number(L, k); });
  field(L, index, "filter_radius",
        [&](int k) { f.filter_radius = number(L, k); });
  field(L, index, "clear_filter_radius", [&](int k) {
    if (boolean(L, k))
      f.filter_radius.reset();
  });
}
std::size_t transform_index(lua_State *L, int index, const Context &c) {
  return static_cast<std::size_t>(
      integer(L, index, 1,
              static_cast<std::int64_t>(c.flame.transforms.size())) -
      1);
}
std::filesystem::path destination(const std::string &s) {
  if (s.empty() || s.size() > 1024 || s.front() == '/' || s.front() == '\\' ||
      s.find(':') != std::string::npos || s.find('\0') != std::string::npos)
    throw ScriptError("Output destination must be a safe relative path");
  std::size_t start = 0;
  for (std::size_t i = 0; i <= s.size(); ++i)
    if (i == s.size() || s[i] == '/' || s[i] == '\\') {
      const auto part = s.substr(start, i - start);
      if (part.empty() || part == "." || part == ".." || part.back() == '.' ||
          part.back() == ' ')
        throw ScriptError(
            "Output destination contains an unsafe path component");
      for (unsigned char ch : part)
        if (ch < 32 || ch == '<' || ch == '>' || ch == '|' || ch == '?' ||
            ch == '*')
          throw ScriptError("Output destination contains an unsafe character");
      start = i + 1;
    }
  std::string normalized = s;
  std::replace(normalized.begin(), normalized.end(), '\\', '/');
  return std::filesystem::path(
      std::u8string(normalized.begin(), normalized.end()));
}
void add_request(Context &c, ScriptRequest request) {
  if (c.requests.size() >= c.options.limits.requests)
    throw ScriptError("Script request count limit exceeded");
  for (const auto &old : c.requests)
    if (old.destination == request.destination)
      throw ScriptError("Duplicate script output destination: " +
                        request.destination.generic_string());
  c.host_check(request.bytes.size() * 2ULL + model_bytes(request.flame) * 2ULL +
               sizeof(request));
  c.requests.push_back(std::move(request));
}
void render_request(Context &c, const std::string &name, lua_State *L = nullptr,
                    int index = 0) {
  if (!c.options.allow_render_requests)
    throw ScriptError("Render requests are disabled; enable the render "
                      "capability in the host");
  c.flush();
  ScriptRequest r;
  r.destination = destination(name);
  r.flame = c.flame;
  r.render.seed = c.options.seed;
  r.render.samples = default_samples(r.flame);
  r.file.image.format = image_format_for_path(r.destination);
  bool samples_set = false;
  if (L && !lua_isnoneornil(L, index)) {
    keys(L, index,
         {"samples", "seed", "width", "height", "format", "alpha", "threads",
          "memory_mib", "disk_mib", "backend"});
    index = lua_absindex(L, index);
    field(L, index, "samples", [&](int k) {
      r.render.samples = seed(L, k);
      samples_set = true;
      if (!r.render.samples)
        throw ScriptError("Render samples must be positive");
    });
    field(L, index, "seed", [&](int k) { r.render.seed = seed(L, k); });
    field(L, index, "width", [&](int k) {
      r.flame.width = static_cast<int>(integer(L, k, 1, 1000000));
    });
    field(L, index, "height", [&](int k) {
      r.flame.height = static_cast<int>(integer(L, k, 1, 1000000));
    });
    field(L, index, "format", [&](int k) {
      r.file.image.format = parse_image_format(string(L, k));
    });
    field(L, index, "alpha",
          [&](int k) { r.file.image.alpha = boolean(L, k); });
    field(L, index, "threads", [&](int k) {
      r.render.threads = static_cast<unsigned>(integer(L, k, 0, 256));
    });
    field(L, index, "memory_mib", [&](int k) {
      r.render.memory_mib =
          static_cast<std::uint64_t>(integer(L, k, 16, 65536));
    });
    field(L, index, "disk_mib", [&](int k) {
      r.file.disk_mib = static_cast<std::uint64_t>(integer(L, k, 16, 1048576));
    });
    field(L, index, "backend", [&](int k) {
      const auto v = string(L, k);
      if (v == "cpu")
        r.render.backend = RenderBackend::cpu;
      else if (v == "auto")
        r.render.backend = RenderBackend::automatic;
      else if (v == "vulkan")
        r.render.backend = RenderBackend::vulkan;
      else
        throw ScriptError("Unknown render backend");
    });
  } else if (!L) {
    if (c.render_width)
      r.flame.width = c.render_width;
    if (c.render_height)
      r.flame.height = c.render_height;
    r.render.memory_mib = c.render_memory;
    r.render.samples = default_samples(r.flame);
  }
  r.flame.scale *= static_cast<double>(r.flame.width) / c.flame.width;
  if (!samples_set)
    r.render.samples = default_samples(r.flame);
  (void)plan_file_render(r.flame, r.render, r.file);
  add_request(c, std::move(r));
}
void export_request(Context &c, const std::string &name,
                    ScriptRequestKind kind) {
  if (!c.options.allow_export_requests)
    throw ScriptError("Export requests are disabled; enable the export "
                      "capability in the host");
  c.flush();
  c.host_check(kind == ScriptRequestKind::export_library
                   ? c.document.bytes().size() * 2ULL
                   : model_bytes(c.flame) * 12ULL);
  ScriptRequest r;
  r.kind = kind;
  r.destination = destination(name);
  if (kind == ScriptRequestKind::export_library)
    r.bytes = c.document.bytes();
  else if (kind == ScriptRequestKind::export_flame)
    r.bytes = serialize_flame(c.flame);
  else
    r.bytes = creative::export_gimp_palette(c.flame.palette, c.flame.name);
  add_request(c, std::move(r));
}
enum class Op {
  print,
  count,
  selected,
  select,
  flame,
  set_flame,
  transform_count,
  transform,
  set_transform,
  add_transform,
  remove_transform,
  clear_transforms,
  final_transform,
  set_final_transform,
  palette,
  set_palette,
  palette_from_stops,
  rotate_palette,
  reverse_palette,
  random_palette,
  new_flame,
  duplicate_flame,
  remove_flame,
  generate,
  mutate,
  random,
  randomseed,
  render,
  export_library,
  export_flame,
  export_palette,
  import_library,
  variations,
  legacy
};
std::vector<Variation> variation_allowlist(lua_State *L, int index) {
  table(L, index);
  index = lua_absindex(L, index);
  const auto n = lua_rawlen(L, index);
  if (n > 128)
    throw ScriptError("Variation allowlist is too long");
  std::vector<Variation> out;
  for (lua_Unsigned i = 1; i <= n; ++i) {
    lua_rawgeti(L, index, static_cast<lua_Integer>(i));
    const auto name = string(L, -1);
    lua_pop(L, 1);
    const auto v = find_variation(name);
    if (!v)
      throw ScriptError(
          "Generation/mutation allowlists support built-in variations only: " +
          name);
    out.push_back(*v);
  }
  return out;
}
void remove_transform(Context &c, std::size_t i) {
  c.flame.transforms.erase(c.flame.transforms.begin() +
                           static_cast<std::ptrdiff_t>(i));
  c.transform_sources.erase(c.transform_sources.begin() +
                            static_cast<std::ptrdiff_t>(i));
  for (auto &t : c.flame.transforms)
    if (i < t.chaos.size())
      t.chaos.erase(t.chaos.begin() + static_cast<std::ptrdiff_t>(i));
  c.active_transform =
      c.flame.transforms.empty()
          ? -1
          : static_cast<int>(std::min(i, c.flame.transforms.size() - 1));
  c.dirty = true;
}
void set_term(Transform &t, const std::string &name, double weight,
              const std::map<std::string, double> &params = {}) {
  auto term = make_variation_term(name, weight, params);
  const auto it =
      std::find_if(t.variations.begin(), t.variations.end(),
                   [&](const auto &v) { return term_name(v) == name; });
  if (it == t.variations.end())
    t.variations.push_back(std::move(term));
  else
    *it = std::move(term);
}
const std::vector<VariationInfo> &legacy_script_variations() {
  // Earlier Reborn scripts may have stored these compact indices. The original
  // 0..22 prefix matches FLAM3, followed by eight previously shipped shapes.
  static const auto entries = [] {
    std::vector<VariationInfo> result;
    const auto add = [&](Variation id) { result.push_back({id, variation_name(id)}); };
    for (const auto &entry : variation_registry())
      if (entry.id <= Variation::fan) add(entry.id);
    for (auto id : {Variation::eyefish, Variation::bubble, Variation::cylinder, Variation::noise,
                    Variation::julian, Variation::blur, Variation::foci, Variation::pre_blur}) add(id);
    for (const auto &entry : variation_registry())
      if (std::none_of(result.begin(), result.end(), [&](const auto &v) { return v.id == entry.id; }))
        add(entry.id);
    return result;
  }();
  return entries;
}
std::string legacy_variation(lua_State *L, int index) {
  const auto n = integer(
      L, index, 0, static_cast<std::int64_t>(variation_registry().size() - 1));
  return std::string(legacy_script_variations()[static_cast<std::size_t>(n)].name);
}
int legacy(lua_State *L, Context &c) {
  const auto op = string(L, 1), key = string(L, 2);
  const bool write = op == "set";
  if (op == "coerce") {
    if (key == "integer")
      lua_pushinteger(L, integer(L, 3, std::numeric_limits<lua_Integer>::min(),
                                 std::numeric_limits<lua_Integer>::max()));
    else if (key == "boolean")
      lua_pushboolean(L, boolean(L, 3));
    else if (key == "string") {
      if (lua_type(L, 3) != LUA_TSTRING)
        throw ScriptError("Expected a string for the declared legacy variable");
      lua_pushvalue(L, 3);
    } else if (key == "variant")
      lua_pushvalue(L, 3);
    else
      lua_pushnumber(L, number(L, 3));
    return 1;
  }
  if (op == "truth") {
    if (lua_type(L, 3) == LUA_TBOOLEAN) {
      lua_pushboolean(L, lua_toboolean(L, 3));
      return 1;
    }
    if (lua_type(L, 3) == LUA_TNUMBER) {
      lua_pushboolean(L, number(L, 3) != 0);
      return 1;
    }
    throw ScriptError("Legacy conditions must be boolean or numeric");
  }
  if (op == "binary") {
    if (key == "+" && lua_type(L, 3) == LUA_TSTRING &&
        lua_type(L, 4) == LUA_TSTRING) {
      const auto a = string(L, 3), b = string(L, 4);
      c.host_check((a.size() + b.size()) * 2ULL);
      push(L, a + b);
      return 1;
    }
    const double a = number(L, 3), b = number(L, 4);
    double result = 0;
    if (key == "+")
      result = a + b;
    else {
      if (b == 0)
        throw ScriptError("Legacy integer division by zero");
      if (a != std::trunc(a) || b != std::trunc(b))
        throw ScriptError("div/mod require integer operands");
      result = key == "div" ? std::trunc(a / b) : std::fmod(a, b);
    }
    if (!std::isfinite(result))
      throw ScriptError("Legacy numeric result is not finite");
    if (result >= double(std::numeric_limits<lua_Integer>::min()) &&
        result < double(std::numeric_limits<lua_Integer>::max()) &&
        result == std::trunc(result))
      lua_pushinteger(L, static_cast<lua_Integer>(result));
    else
      lua_pushnumber(L, result);
    return 1;
  }
  if (op == "get" || write) {
    const int value = lua_gettop(L);
    auto scalar = [&](double &target) {
      if (write) {
        target = number(L, value);
        c.dirty = true;
        return 0;
      }
      lua_pushnumber(L, target);
      return 1;
    };
    auto int_scalar = [&](int &target, int lo, int hi) {
      if (write) {
        target = static_cast<int>(integer(L, value, lo, hi));
        c.dirty = true;
        return 0;
      }
      lua_pushinteger(L, target);
      return 1;
    };
    if (key == "activetransform") {
      if (write) {
        c.active_transform = static_cast<int>(
            integer(L, value, 0,
                    static_cast<std::int64_t>(c.flame.transforms.size() - 1)));
        return 0;
      }
      lua_pushinteger(L, c.active_transform);
      return 1;
    }
    if (key == "transforms") {
      lua_pushinteger(L, static_cast<lua_Integer>(c.flame.transforms.size()));
      return 1;
    }
    if (key == "filecount") {
      lua_pushinteger(L, static_cast<lua_Integer>(c.document.entries().size()));
      return 1;
    }
    if (key == "nvars") {
      lua_pushinteger(L, static_cast<lua_Integer>(variation_registry().size()));
      return 1;
    }
    if (key == "nxforms") {
      lua_pushinteger(
          L, static_cast<lua_Integer>(c.options.limits.transforms_per_flame));
      return 1;
    }
    if (key == "stopped") {
      lua_pushboolean(L, c.options.stop_token.stop_requested());
      return 1;
    }
    if (key == "flame.name" || key == "renderer.filename") {
      auto &target = key == "flame.name" ? c.flame.name : c.render_filename;
      if (write) {
        target = string(L, value);
        if (target.size() > 4096)
          throw ScriptError("Legacy text property is too long");
        c.dirty |= key == "flame.name";
        return 0;
      }
      push(L, target);
      return 1;
    }
    if (key == "renderer.width")
      return int_scalar(c.render_width, 1, 1000000);
    if (key == "renderer.height")
      return int_scalar(c.render_height, 1, 1000000);
    if (key == "renderer.maxmemory") {
      if (write)
        c.render_memory =
            static_cast<std::uint64_t>(integer(L, value, 16, 65536));
      else
        lua_pushinteger(L, static_cast<lua_Integer>(c.render_memory));
      return write ? 0 : 1;
    }
    if (key == "flame.width")
      return int_scalar(c.flame.width, 1, 1000000);
    if (key == "flame.height")
      return int_scalar(c.flame.height, 1, 1000000);
    if (key == "flame.oversample")
      return int_scalar(c.flame.oversample, 1, 4);
    if (key == "flame.scale") {
      if (write) {
        c.flame.scale = number(L, value) * c.flame.width / 100;
        c.dirty = true;
        return 0;
      }
      lua_pushnumber(L, 100 * c.flame.scale / c.flame.width);
      return 1;
    }
    if (key == "flame.angle") {
      if (write) {
        c.flame.rotate = -number(L, value) * 180 / std::numbers::pi;
        c.dirty = true;
        return 0;
      }
      lua_pushnumber(L, -c.flame.rotate * std::numbers::pi / 180);
      return 1;
    }
    if (key == "flame.filterradius") {
      if (write) {
        c.flame.filter_radius = number(L, value);
        c.dirty = true;
        return 0;
      }
      lua_pushnumber(L, c.flame.filter_radius.value_or(0));
      return 1;
    }
    if (key == "flame.x")
      return scalar(c.flame.center.x);
    if (key == "flame.y")
      return scalar(c.flame.center.y);
    for (const auto &[name, member] :
         std::initializer_list<std::pair<std::string_view, double Flame::*>>{
             {"flame.gamma", &Flame::gamma},
             {"flame.brightness", &Flame::brightness},
             {"flame.vibrancy", &Flame::vibrancy},
             {"flame.time", &Flame::time},
             {"flame.zoom", &Flame::zoom},
             {"flame.quality", &Flame::quality},
             {"flame.sampledensity", &Flame::quality}})
      if (key == name)
        return scalar(c.flame.*member);
    if (key == "flame.gradient" || key == "flame.background") {
      const bool gradient = key == "flame.gradient";
      const int slot = gradient ? static_cast<int>(integer(L, 3, 0, 255)) : 0;
      const int channel = static_cast<int>(integer(L, gradient ? 4 : 3, 0, 2));
      auto &color =
          gradient ? c.flame.palette[std::size_t(slot)] : c.flame.background;
      double &target = channel == 0   ? color.r
                       : channel == 1 ? color.g
                                      : color.b;
      if (write) {
        target = static_cast<double>(integer(L, value, 0, 255)) / 255;
        c.dirty = true;
        return 0;
      }
      lua_pushinteger(L, static_cast<lua_Integer>(std::lround(target * 255)));
      return 1;
    }
    if (key.starts_with("transform.")) {
      auto &t = c.active();
      const auto property = key.substr(10);
      if (property == "weight") {
        if (write) {
          const double v = number(L, value);
          if (!(v > 0 && v <= 1000))
            throw ScriptError("Legacy Transform.Weight must be in (0,1000]");
        }
        return scalar(t.weight);
      }
      if (property == "color") {
        if (write) {
          const double v = number(L, value);
          if (v < 0 || v > 1)
            throw ScriptError("Legacy Transform.Color must be in [0,1]");
        }
        return scalar(t.color);
      }
      if (property == "symmetry") {
        if (write) {
          const double v = number(L, value);
          if (v < -1 || v > 1)
            throw ScriptError("Transform.Symmetry must be in [-1,1]");
          t.color_speed = (1 - v) / 2;
          c.dirty = true;
          return 0;
        }
        lua_pushnumber(L, 1 - 2 * t.color_speed);
        return 1;
      }
      if (property == "plotmode") {
        if (write) {
          t.opacity = integer(L, value, std::numeric_limits<lua_Integer>::min(),
                              std::numeric_limits<lua_Integer>::max()) != 0
                          ? 1
                          : 0;
          c.dirty = true;
          return 0;
        }
        lua_pushinteger(L, t.opacity != 0 ? 1 : 0);
        return 1;
      }
      if (property == "julian_power")
        return int_scalar(t.parameters.julian_power, -100000, 100000);
      if (const auto *field = find_variation_parameter(property)) {
        if (write) {
          field->set(t.parameters, number(L, value));
          c.dirty = true;
          return 0;
        }
        lua_pushnumber(L, field->get(t.parameters));
        return 1;
      }
      if (property == "a")
        return scalar(t.pre.a);
      if (property == "b")
        return scalar(t.pre.c);
      if (property == "c")
        return scalar(t.pre.b);
      if (property == "d")
        return scalar(t.pre.d);
      if (property == "e")
        return scalar(t.pre.e);
      if (property == "f")
        return scalar(t.pre.f);
      if (property == "coefs" || property == "post") {
        const int i = static_cast<int>(integer(L, 3, 0, 2)),
                  j = static_cast<int>(integer(L, 4, 0, 1));
        auto &m = property == "coefs" ? t.pre : t.post;
        double *fields[]{&m.a, &m.b, &m.c, &m.d, &m.e, &m.f};
        const int k = i * 2 + j;
        const double sign = k == 0 || k == 3 || k == 4 ? 1 : -1;
        if (write) {
          *fields[k] = number(L, value) * sign;
          c.dirty = true;
          return 0;
        }
        lua_pushnumber(L, *fields[k] * sign);
        return 1;
      }
      if (property == "chaos") {
        const auto i = static_cast<std::size_t>(integer(
            L, 3, 0, static_cast<std::int64_t>(c.flame.transforms.size() - 1)));
        if (write) {
          if (t.chaos.size() <= i)
            t.chaos.resize(i + 1, 1);
          t.chaos[i] = number(L, value);
          c.dirty = true;
          return 0;
        }
        lua_pushnumber(L, i < t.chaos.size() ? t.chaos[i] : 1);
        return 1;
      }
      const auto name =
          property == "variation" ? legacy_variation(L, 3) : property;
      if (!find_variation(name))
        throw ScriptError("Unsupported legacy transform property: " + property);
      if (write) {
        set_term(t, name, number(L, value));
        c.dirty = true;
        return 0;
      }
      const auto it = std::find_if(
          t.variations.begin(), t.variations.end(),
          [&](const Term &term) { return term_name(term) == name; });
      lua_pushnumber(L, it == t.variations.end() ? 0 : it->weight);
      return 1;
    }
    throw ScriptError("Unsupported legacy property: " + key);
  }
  if (op != "call")
    throw ScriptError("Invalid legacy operation");
  if (key == "print" || key == "showstatus") {
    std::size_t length = 0;
    const char *value = luaL_tolstring(L, 3, &length);
    if (length >= c.options.limits.console_bytes - c.console_bytes)
      throw ScriptError("Script console output limit exceeded");
    c.host_check(length * 2ULL);
    c.line(std::string(value, length));
    lua_pop(L, 1);
    return 0;
  }
  if (key == "trunc" || key == "round" || key == "frac" || key == "sqr") {
    const double n = number(L, 3);
    const double v = key == "trunc"   ? std::trunc(n)
                     : key == "round" ? std::nearbyint(n)
                     : key == "frac"  ? n - std::trunc(n)
                                      : n * n;
    if (!std::isfinite(v))
      throw ScriptError("Legacy numeric result is not finite");
    if ((key == "round" || key == "trunc") &&
        v >= double(std::numeric_limits<lua_Integer>::min()) &&
        v < double(std::numeric_limits<lua_Integer>::max()))
      lua_pushinteger(L, static_cast<lua_Integer>(v));
    else
      lua_pushnumber(L, v);
    return 1;
  }
  if (key == "inttostr" || key == "floattostr") {
    if (key == "inttostr") {
      push(L,
           std::to_string(integer(L, 3, std::numeric_limits<lua_Integer>::min(),
                                  std::numeric_limits<lua_Integer>::max())));
      return 1;
    }
    const double n = number(L, 3);
    std::array<char, 64> buffer{};
    const auto converted =
        std::to_chars(buffer.data(), buffer.data() + buffer.size(), n);
    if (converted.ec != std::errc{})
      throw ScriptError("Cannot format number");
    push(L,
         std::string_view(buffer.data(), static_cast<std::size_t>(
                                             converted.ptr - buffer.data())));
    return 1;
  }
  if (key == "strtoint" || key == "strtofloat") {
    const auto s = string(L, 3);
    if (key == "strtoint") {
      lua_Integer n = 0;
      const auto r = std::from_chars(s.data(), s.data() + s.size(), n);
      if (r.ec != std::errc{} || r.ptr != s.data() + s.size())
        throw ScriptError("Invalid legacy integer string");
      lua_pushinteger(L, n);
    } else {
      double n = 0;
      const auto r = std::from_chars(s.data(), s.data() + s.size(), n);
      if (r.ec != std::errc{} || r.ptr != s.data() + s.size() ||
          !std::isfinite(n))
        throw ScriptError("Invalid legacy number string");
      lua_pushnumber(L, n);
    }
    return 1;
  }
  if (key == "clear") {
    c.flame.transforms.clear();
    c.transform_sources.clear();
    c.flame.final_transform.reset();
    c.flame.zoom = 0;
    c.active_transform = -1;
    c.dirty = true;
    return 0;
  }
  if (key == "addtransform") {
    if (c.flame.transforms.size() >= c.options.limits.transforms_per_flame)
      throw ScriptError("Script transform count limit exceeded");
    Transform t;
    t.weight = .5;
    c.flame.transforms.push_back(t);
    c.transform_sources.push_back(std::nullopt);
    c.active_transform = static_cast<int>(c.flame.transforms.size() - 1);
    c.dirty = true;
    return 0;
  }
  if (key == "transform.clear") {
    c.active() = Transform{};
    c.active().weight = .5;
    c.dirty = true;
    return 0;
  }
  if (key == "deletetransform") {
    c.active();
    if (c.flame.transforms.size() > 1)
      remove_transform(c, static_cast<std::size_t>(c.active_transform));
    return 0;
  }
  if (key == "copytransform") {
    if (c.flame.transforms.size() >= c.options.limits.transforms_per_flame)
      throw ScriptError("Script transform count limit exceeded");
    const auto old = static_cast<std::size_t>(c.active_transform);
    auto t = c.active();
    const auto next = c.flame.transforms.size();
    c.flame.transforms.push_back(t);
    c.transform_sources.push_back(c.transform_sources[old]);
    for (auto &tr : c.flame.transforms) {
      const double incoming = old < tr.chaos.size() ? tr.chaos[old] : 1;
      if (incoming != 1) {
        tr.chaos.resize(next + 1, 1);
        tr.chaos[next] = incoming;
      }
    }
    c.active_transform = static_cast<int>(next);
    c.dirty = true;
    return 0;
  }
  if (key == "setactivetransform") {
    c.active_transform = static_cast<int>(integer(
        L, 3, 0, static_cast<std::int64_t>(c.flame.transforms.size() - 1)));
    return 0;
  }
  if (key == "rotate" || key == "scale" || key == "translate" ||
      key == "multiply") {
    auto &m = c.active().pre;
    const auto old = m;
    if (key == "translate") {
      const double x = number(L, 3), y = number(L, 4);
      m.e = old.a * x + old.b * y + old.e;
      m.f = old.c * x + old.d * y + old.f;
    } else if (key == "scale") {
      const double s = number(L, 3);
      m.a *= s;
      m.b *= s;
      m.c *= s;
      m.d *= s;
    } else {
      double a, b, cc, d;
      if (key == "rotate") {
        const double r = number(L, 3) * std::numbers::pi / 180;
        a = d = std::cos(r);
        b = -std::sin(r);
        cc = -b;
      } else {
        a = number(L, 3);
        b = number(L, 4);
        cc = number(L, 5);
        d = number(L, 6);
      }
      m.a = old.a * a + old.b * cc;
      m.b = old.a * b + old.b * d;
      m.c = old.c * a + old.d * cc;
      m.d = old.c * b + old.d * d;
    }
    c.dirty = true;
    return 0;
  }
  if (key == "normalizevars") {
    const auto normalize = [&](Transform &t) {
      c.check();
      double total = 0;
      for (const auto &v : t.variations)
        total += std::abs(v.weight);
      if (total > 0) {
        for (auto &v : t.variations)
          v.weight = std::abs(v.weight) / total;
      } else
        set_term(t, "linear", 1);
    };
    for (auto &t : c.flame.transforms)
      normalize(t);
    if (c.flame.final_transform)
      normalize(*c.flame.final_transform);
    c.dirty = true;
    return 0;
  }
  if (key == "randomgradient") {
    c.flame.palette = creative::random_palette(
        static_cast<std::uint64_t>(c.rng.unit() * 9007199254740992.0));
    c.dirty = true;
    return 0;
  }
  if (key == "variationindex") {
    const auto name = string(L, 3);
    const auto &entries = legacy_script_variations();
    const auto it = std::find_if(entries.begin(), entries.end(),
                                 [&](const auto &e) { return e.name == name; });
    if (it == entries.end())
      throw ScriptError("Unsupported legacy variation: " + name);
    lua_pushinteger(L, std::distance(entries.begin(), it));
    return 1;
  }
  if (key == "variationname") {
    push(L, legacy_variation(L, 3));
    return 1;
  }
  if (key == "render") {
    render_request(c, c.render_filename);
    return 0;
  }
  if (key == "saveflame") {
    export_request(c, string(L, 3), ScriptRequestKind::export_flame);
    return 0;
  }
  if (key == "loadflame") {
    c.flush();
    const auto n = static_cast<std::size_t>(integer(
        L, 3, 0, static_cast<std::int64_t>(c.document.entries().size() - 1)));
    c.flame = c.document.render_model(n);
    c.reset_sources();
    c.selected = n;
    c.active_transform = 0;
    return 0;
  }
  if (key == "random") {
    lua_pushnumber(L, c.rng.unit());
    return 1;
  }
  throw ScriptError("Unsupported legacy operation: " + key);
}
int dispatch(lua_State *L) {
  auto &c = context(L);
  c.begin_call();
  const auto op = static_cast<Op>(lua_tointeger(L, lua_upvalueindex(1)));
  switch (op) {
  case Op::print: {
    std::string line;
    const auto limit = c.options.limits.console_bytes;
    for (int i = 1; i <= lua_gettop(L); ++i) {
      const std::size_t separator = i > 1 ? 1 : 0;
      if (line.size() >= limit || separator > limit - line.size())
        throw ScriptError("Script console output limit exceeded");
      std::size_t size = 0;
      const char *s = luaL_tolstring(L, i, &size);
      if (size > limit - line.size() - separator)
        throw ScriptError("Script console output limit exceeded");
      c.host_check((line.size() + separator + size) * 2ULL);
      if (separator)
        line += '\t';
      line.append(s, size);
      lua_pop(L, 1);
    }
    c.line(std::move(line));
    return 0;
  }

  case Op::count:
    lua_pushinteger(L, static_cast<lua_Integer>(c.document.entries().size()));
    return 1;
  case Op::selected:
    lua_pushinteger(L, static_cast<lua_Integer>(c.selected + 1));
    return 1;
  case Op::select: {
    c.flush();
    const auto index = static_cast<std::size_t>(
        integer(L, 1, 1,
                static_cast<std::int64_t>(c.document.entries().size())) -
        1);
    c.flame = c.document.render_model(index);
    c.reset_sources();
    c.selected = index;
    c.active_transform = 0;
    return 0;
  }
  case Op::flame:
    push_flame(L, c.flame);
    return 1;
  case Op::set_flame:
    read_flame(L, 1, c.flame);
    c.dirty = true;
    return 0;
  case Op::transform_count:
    lua_pushinteger(L, static_cast<lua_Integer>(c.flame.transforms.size()));
    return 1;
  case Op::transform:
    push_transform(L, c.flame.transforms[transform_index(L, 1, c)]);
    return 1;
  case Op::set_transform: {
    auto &t = c.flame.transforms[transform_index(L, 1, c)];
    t = read_transform(L, 2, t, c);
    c.dirty = true;
    return 0;
  }
  case Op::add_transform: {
    if (c.flame.transforms.size() >= c.options.limits.transforms_per_flame)
      throw ScriptError("Script transform count limit exceeded");
    auto t = lua_isnoneornil(L, 1) ? Transform{}
                                   : read_transform(L, 1, Transform{}, c);
    c.flame.transforms.push_back(std::move(t));
    c.transform_sources.push_back(std::nullopt);
    c.dirty = true;
    lua_pushinteger(L, static_cast<lua_Integer>(c.flame.transforms.size()));
    return 1;
  }
  case Op::remove_transform:
    remove_transform(c, transform_index(L, 1, c));
    return 0;
  case Op::clear_transforms:
    c.flame.transforms.clear();
    c.transform_sources.clear();
    c.active_transform = -1;
    c.dirty = true;
    return 0;
  case Op::final_transform:
    if (c.flame.final_transform)
      push_transform(L, *c.flame.final_transform);
    else
      lua_pushnil(L);
    return 1;
  case Op::set_final_transform:
    if (lua_isnil(L, 1))
      c.flame.final_transform.reset();
    else
      c.flame.final_transform = read_transform(
          L, 1, c.flame.final_transform.value_or(Transform{}), c);
    c.dirty = true;
    return 0;
  case Op::palette: {
    lua_createtable(L, 256, 0);
    for (int i = 0; i < 256; ++i) {
      const auto rgb = c.flame.palette[std::size_t(i)];
      push_array(L, {rgb.r, rgb.g, rgb.b});
      lua_rawseti(L, -2, i + 1);
    }
    return 1;
  }
  case Op::set_palette: {
    table(L, 1);
    if (lua_rawlen(L, 1) != 256)
      throw ScriptError("A palette must contain exactly 256 RGB triples");
    Palette p;
    for (int i = 0; i < 256; ++i) {
      c.check();
      lua_rawgeti(L, 1, i + 1);
      const auto rgb = array(L, -1, 3, 3);
      lua_pop(L, 1);
      p[std::size_t(i)] = {rgb[0], rgb[1], rgb[2]};
    }
    c.flame.palette = p;
    c.dirty = true;
    return 0;
  }
  case Op::palette_from_stops: {
    table(L, 1);
    const auto n = lua_rawlen(L, 1);
    if (n < 2 || n > 256)
      throw ScriptError("Supply 2..256 palette stops");
    std::vector<creative::ColorStop> stops;
    for (lua_Unsigned i = 1; i <= n; ++i) {
      c.check();
      lua_rawgeti(L, 1, static_cast<lua_Integer>(i));
      const auto v = array(L, -1, 4, 4);
      lua_pop(L, 1);
      if (v[0] != std::floor(v[0]) || v[0] < 0 || v[0] > 255)
        throw ScriptError("Stop index must be in 0..255");
      stops.push_back({static_cast<int>(v[0]), {v[1], v[2], v[3]}});
    }
    const auto mode = lua_isnoneornil(L, 2) ? "rgb" : string(L, 2);
    if (mode != "rgb" && mode != "hsv")
      throw ScriptError("Gradient space must be rgb or hsv");
    c.flame.palette = creative::palette_from_stops(
        stops, mode == "hsv" ? creative::GradientSpace::hsv_shortest
                             : creative::GradientSpace::rgb);
    c.dirty = true;
    return 0;
  }
  case Op::rotate_palette:
    c.flame.palette = creative::rotate_palette(
        c.flame.palette, static_cast<int>(integer(L, 1, -1000000, 1000000)));
    c.dirty = true;
    return 0;
  case Op::reverse_palette:
    c.flame.palette = creative::reverse_palette(c.flame.palette);
    c.dirty = true;
    return 0;
  case Op::random_palette:
    c.flame.palette = creative::random_palette(
        lua_isnoneornil(L, 1) ? c.options.seed : seed(L, 1),
        lua_isnoneornil(L, 2) ? 5
                              : static_cast<unsigned>(integer(L, 2, 2, 256)));
    c.dirty = true;
    return 0;
  case Op::new_flame: {
    c.flush();
    if (c.document.entries().size() >= c.options.limits.flame_count)
      throw ScriptError("Script flame count limit exceeded");
    Flame f;
    f.name = lua_isnoneornil(L, 1) ? "Script flame" : string(L, 1);
    if (f.name.size() > 4096)
      throw ScriptError("Flame name is too long");
    f.transforms.emplace_back();
    c.selected = c.document.append_flame(f);
    c.flame = std::move(f);
    c.reset_sources();
    c.active_transform = 0;
    lua_pushinteger(L, static_cast<lua_Integer>(c.selected + 1));
    return 1;
  }
  case Op::duplicate_flame: {
    c.flush();
    if (c.document.entries().size() >= c.options.limits.flame_count)
      throw ScriptError("Script flame count limit exceeded");
    const auto name =
        lua_isnoneornil(L, 1) ? c.flame.name + " copy" : string(L, 1);
    if (name.size() > 4096)
      throw ScriptError("Flame name is too long");
    c.selected = c.document.duplicate_flame(c.selected, name);
    c.flame = c.document.render_model(c.selected);
    c.reset_sources();
    lua_pushinteger(L, static_cast<lua_Integer>(c.selected + 1));
    return 1;
  }
  case Op::remove_flame: {
    c.flush();
    c.document.remove_flame(c.selected);
    c.selected = std::min(c.selected, c.document.entries().size() - 1);
    c.flame = c.document.render_model(c.selected);
    c.reset_sources();
    c.active_transform = 0;
    return 0;
  }
  case Op::generate: {
    keys(L, 1,
         {"seed", "count", "min_transforms", "max_transforms", "variations",
          "width", "height", "symmetry", "reflection", "randomize_palette"});
    creative::RandomOptions options;
    options.seed = c.options.seed;
    std::size_t count = 1;
    field(L, 1, "seed", [&](int i) { options.seed = seed(L, i); });
    field(L, 1, "count", [&](int i) {
      count = static_cast<std::size_t>(integer(
          L, i, 1, static_cast<std::int64_t>(c.options.limits.flame_count)));
    });
    field(L, 1, "min_transforms", [&](int i) {
      options.min_transforms = static_cast<unsigned>(integer(L, i, 1, 64));
    });
    field(L, 1, "max_transforms", [&](int i) {
      options.max_transforms = static_cast<unsigned>(integer(L, i, 1, 64));
    });
    field(L, 1, "variations", [&](int i) {
      options.allowed_variations = variation_allowlist(L, i);
    });
    field(L, 1, "width", [&](int i) {
      options.width = static_cast<int>(integer(L, i, 1, 1000000));
    });
    field(L, 1, "height", [&](int i) {
      options.height = static_cast<int>(integer(L, i, 1, 1000000));
    });
    field(L, 1, "symmetry", [&](int i) {
      options.rotational_symmetry = static_cast<unsigned>(integer(L, i, 1, 64));
    });
    field(L, 1, "reflection",
          [&](int i) { options.reflection = boolean(L, i); });
    field(L, 1, "randomize_palette",
          [&](int i) { options.randomize_palette = boolean(L, i); });
    if (count > c.options.limits.flame_count - c.document.entries().size())
      throw ScriptError("Script flame count limit exceeded");
    c.flush();
    const auto first = c.document.entries().size();
    for (std::size_t i = 0; i < count; ++i) {
      c.check();
      c.host_check(131072);
      auto generation = options;
      generation.seed += i;
      auto results = creative::random_flames(generation);
      c.check();
      if (results[0].transforms.size() > c.options.limits.transforms_per_flame)
        throw ScriptError("Script transform count limit exceeded");
      c.document.append_flame(results[0]);
      c.host_check();
    }
    c.selected = first;
    c.flame = c.document.render_model(first);
    c.reset_sources();
    c.active_transform = 0;
    lua_pushinteger(L, static_cast<lua_Integer>(first + 1));
    return 1;
  }
  case Op::mutate: {
    keys(L, 1,
         {"seed", "strength", "trend", "geometry", "variations", "colors",
          "parameters", "palette", "include_final", "allowed_variations"});
    creative::MutationOptions o;
    o.seed = c.options.seed;
    field(L, 1, "seed", [&](int i) { o.seed = seed(L, i); });
    field(L, 1, "strength", [&](int i) { o.strength = number(L, i); });
    field(L, 1, "trend", [&](int i) { o.trend = number(L, i); });
    for (const auto &[key, member] : std::initializer_list<
             std::pair<const char *, bool creative::MutationOptions::*>>{
             {"geometry", &creative::MutationOptions::geometry},
             {"variations", &creative::MutationOptions::variations},
             {"colors", &creative::MutationOptions::colors},
             {"parameters", &creative::MutationOptions::parameters},
             {"palette", &creative::MutationOptions::palette},
             {"include_final", &creative::MutationOptions::include_final}})
      field(L, 1, key, [&](int i) { o.*member = boolean(L, i); });
    field(L, 1, "allowed_variations",
          [&](int i) { o.allowed_variations = variation_allowlist(L, i); });
    c.flame = creative::mutate_flame(c.flame, o);
    c.dirty = true;
    return 0;
  }
  case Op::random: {
    const double r = c.rng.unit();
    if (lua_gettop(L) == 0) {
      lua_pushnumber(L, r);
      return 1;
    }
    const auto lo = lua_gettop(L) == 1 ? 1
                                       : integer(L, 1, -9007199254740991LL,
                                                 9007199254740991LL);
    const auto hi =
        integer(L, lua_gettop(L) == 1 ? 1 : 2, lo, 9007199254740991LL);
    if (hi - lo > 9007199254740991LL)
      throw ScriptError("Random integer interval is too wide");
    lua_pushinteger(
        L, lo + static_cast<lua_Integer>(r * static_cast<double>(hi - lo + 1)));
    return 1;
  }
  case Op::randomseed:
    c.rng = Rng(seed(L, 1));
    return 0;
  case Op::render:
    render_request(c, string(L, 1), L, 2);
    return 0;
  case Op::export_library:
    export_request(c, string(L, 1), ScriptRequestKind::export_library);
    return 0;
  case Op::export_flame:
    export_request(c, string(L, 1), ScriptRequestKind::export_flame);
    return 0;
  case Op::export_palette:
    export_request(c, string(L, 1), ScriptRequestKind::export_palette);
    return 0;
  case Op::import_library: {
    const auto name = string(L, 1);
    const auto it = c.options.imports.find(name);
    if (it == c.options.imports.end())
      throw ScriptError("Import capability was not supplied: " + name);
    if (it->second.size() > c.options.limits.document_bytes)
      throw ScriptError("Imported document byte limit exceeded");
    c.host_check(it->second.size() * 4ULL);
    auto doc = LegacyDocument::parse(it->second);
    if (doc.entries().empty() ||
        doc.entries().size() > c.options.limits.flame_count)
      throw ScriptError("Imported flame count is outside the limit");
    auto f = doc.render_model(0);
    c.document = std::move(doc);
    c.flame = std::move(f);
    c.reset_sources();
    c.selected = 0;
    c.active_transform = 0;
    c.dirty = false;
    return 0;
  }
  case Op::variations: {
    lua_newtable(L);
    int i = 1;
    for (const auto &v : variation_registry()) {
      push(L, v.name);
      lua_rawseti(L, -2, i++);
    }
    for (const auto &v : enabled_plugins()) {
      push(L, v.name);
      lua_rawseti(L, -2, i++);
    }
    return 1;
  }
  case Op::legacy:
    return legacy(L, c);
  }
  throw ScriptError("Invalid script API operation");
}
int callback(lua_State *L) {
  auto &c = context(L);
  try {
    const int results = dispatch(L);
    c.host_check();
    return results;
  } catch (const ScriptCancelled &) {
    c.cancelled = true;
    return luaL_error(L, "Script execution stopped");
  } catch (const std::exception &e) {
    c.fatal = std::string_view(e.what()).substr(0, 4096);
    return luaL_error(L, "%s", c.fatal.c_str());
  }
}
void function(lua_State *L, const char *name, Op op) {
  lua_pushinteger(L, static_cast<lua_Integer>(op));
  lua_pushcclosure(L, callback, 1);
  lua_setfield(L, -2, name);
}
int initialize(lua_State *L) {
  luaL_requiref(L, "_G", luaopen_base, 1);
  lua_pop(L, 1);
  luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1);
  lua_pop(L, 1);
  luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
  lua_pop(L, 1);
  luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
  lua_pop(L, 1);
  luaL_requiref(L, LUA_UTF8LIBNAME, luaopen_utf8, 1);
  lua_pop(L, 1);
  for (const auto *name :
       {"dofile", "loadfile", "load", "collectgarbage", "pcall", "xpcall",
        "warn", "setmetatable", "getmetatable"}) {
    lua_pushnil(L);
    lua_setglobal(L, name);
  }
  // Lua patterns can perform unhooked exponential C work. Keep deterministic,
  // bounded string primitives; unsupported pattern APIs are visibly absent.
  lua_getglobal(L, "string");
  for (const auto *name : {"dump", "find", "match", "gmatch", "gsub"}) {
    lua_pushnil(L);
    lua_setfield(L, -2, name);
  }
  lua_pop(L, 1);
  lua_getglobal(L, "math");
  function(L, "random", Op::random);
  function(L, "randomseed", Op::randomseed);
  lua_pop(L, 1);
  lua_newtable(L);
  for (const auto &[name, op] :
       std::initializer_list<std::pair<const char *, Op>>{
           {"print", Op::print},
           {"count", Op::count},
           {"selected", Op::selected},
           {"select", Op::select},
           {"flame", Op::flame},
           {"set_flame", Op::set_flame},
           {"transform_count", Op::transform_count},
           {"transform", Op::transform},
           {"set_transform", Op::set_transform},
           {"add_transform", Op::add_transform},
           {"remove_transform", Op::remove_transform},
           {"clear_transforms", Op::clear_transforms},
           {"final_transform", Op::final_transform},
           {"set_final_transform", Op::set_final_transform},
           {"palette", Op::palette},
           {"set_palette", Op::set_palette},
           {"palette_from_stops", Op::palette_from_stops},
           {"rotate_palette", Op::rotate_palette},
           {"reverse_palette", Op::reverse_palette},
           {"random_palette", Op::random_palette},
           {"new_flame", Op::new_flame},
           {"duplicate_flame", Op::duplicate_flame},
           {"remove_flame", Op::remove_flame},
           {"generate", Op::generate},
           {"mutate", Op::mutate},
           {"random", Op::random},
           {"randomseed", Op::randomseed},
           {"render", Op::render},
           {"export_library", Op::export_library},
           {"export_flame", Op::export_flame},
           {"export_palette", Op::export_palette},
           {"import_library", Op::import_library},
           {"variations", Op::variations}})
    function(L, name, op);
  lua_setglobal(L, "apo");
  lua_pushinteger(L, static_cast<lua_Integer>(Op::print));
  lua_pushcclosure(L, callback, 1);
  lua_setglobal(L, "print");
  if (context(L).options.language == ScriptLanguage::legacy_pascal) {
    lua_pushinteger(L, static_cast<lua_Integer>(Op::legacy));
    lua_pushcclosure(L, callback, 1);
    lua_setglobal(L, "__apo_legacy");
  }
  return 0;
}
std::size_t error_line(const std::string &error) {
  const auto colon = error.find(':');
  if (colon == std::string::npos)
    return 0;
  std::size_t line = 0;
  const auto read = std::from_chars(error.data() + colon + 1,
                                    error.data() + error.size(), line);
  return read.ec == std::errc{} ? line : 0;
}
} // namespace
static ScriptResult run_script_impl(const LegacyDocument &document,
                                    std::size_t selected_index,
                                    std::string_view source,
                                    const ScriptOptions &options) {
  const auto started = Clock::now();
  if (options.language != ScriptLanguage::lua &&
      options.language != ScriptLanguage::legacy_pascal)
    throw ScriptError("Unknown script language");
  const auto &limits = options.limits;
  if (options.stop_token.stop_requested())
    throw ScriptCancelled();
  if (!limits.instructions || !limits.lua_memory_bytes ||
      !limits.host_memory_bytes || !limits.timeout_ms ||
      limits.timeout_ms > 86400000)
    throw ScriptError(
        "Script execution limits must be positive (timeout at most 24 hours)");
  if (source.size() > limits.source_bytes)
    throw ScriptError("Script source byte limit exceeded");
  if (document.bytes().size() > limits.document_bytes)
    throw ScriptError("Script document byte limit exceeded");
  if (selected_index >= document.entries().size())
    throw ScriptError("Script selection is outside the document");
  if (document.entries().size() > limits.flame_count)
    throw ScriptError("Script flame count limit exceeded");
  std::uint64_t initial = document.bytes().size() * 4ULL + source.size() * 4ULL;
  for (const auto &[name, bytes] : options.imports) {
    if (bytes.size() > limits.document_bytes)
      throw ScriptError("Imported document byte limit exceeded");
    initial += name.size() + bytes.size() * 2ULL;
  }
  if (initial > limits.host_memory_bytes)
    throw ScriptError("Script host memory limit exceeded");
  if (source.starts_with("\xef\xbb\xbf"))
    source.remove_prefix(3);
  Context c(document, selected_index, options, source.size(), started);
  c.host_check();
  detail::LegacyTranslation translation;
  if (options.language == ScriptLanguage::legacy_pascal) {
    c.host_check(source.size() * 128ULL + 16384);
    translation = detail::translate_legacy_script(source, [&] { c.check(); });
    c.translated_bytes = translation.source.size();
    c.host_check();
    source = translation.source;
  }
  std::unique_ptr<lua_State, decltype(&lua_close)> state(
      lua_newstate(allocate, &c, static_cast<unsigned>(options.seed)),
      lua_close);
  if (!state) {
    c.check();
    throw ScriptError(
        "Cannot allocate Lua state within the script memory limit");
  }
  auto *L = state.get();
  *static_cast<Context **>(lua_getextraspace(L)) = &c;
  lua_pushcfunction(L, initialize);
  int status = lua_pcall(L, 0, 0, 0);
  if (status == LUA_OK) {
    lua_sethook(
        L, instruction_hook, LUA_MASKCOUNT,
        static_cast<int>(std::min<std::uint64_t>(1000, limits.instructions)));
    status = luaL_loadbufferx(L, source.data(), source.size(), "@script", "t");
  }
  if (status == LUA_OK)
    status = lua_pcall(L, 0, 0, 0);
  if (c.cancelled || options.stop_token.stop_requested())
    throw ScriptCancelled();
  if (c.timed_out)
    throw ScriptError("Script time limit exceeded");
  if (status != LUA_OK) {
    // Never coerce a non-string error outside a protected Lua call: numeric
    // coercion allocates and could panic if the script exhausted its heap.
    std::string error = "Lua execution failed (non-string error object)";
    if (lua_type(L, -1) == LUA_TSTRING) {
      std::size_t length = 0;
      const char *message = lua_tolstring(L, -1, &length);
      error.assign(message, std::min<std::size_t>(length, 4096));
      if (length > 4096)
        error += " [diagnostic truncated]";
    }
    if (status == LUA_ERRMEM)
      error = "Script Lua memory limit exceeded: " + error;
    auto line = error_line(error);
    if (options.language == ScriptLanguage::legacy_pascal && line &&
        line <= translation.source_lines.size())
      line = translation.source_lines[line - 1];
    throw ScriptError(error, line);
  }
  c.check();
  c.flush();
  c.check();
  ScriptResult result;
  result.document_bytes = c.document.bytes();
  result.selected_index = c.selected;
  result.console = std::move(c.console);
  result.requests = std::move(c.requests);
  result.instructions = c.instructions;
  result.instruction_quantum = static_cast<std::uint32_t>(
      std::min<std::uint64_t>(1000, limits.instructions));
  result.peak_lua_bytes = c.peak_lua;
  result.peak_host_bytes = c.peak_host;
  result.translated_source = std::move(translation.source);
  result.compatibility_notes = std::move(translation.notes);
  return result;
}
ScriptResult run_script(const LegacyDocument &document,
                        std::size_t selected_index, std::string_view source,
                        const ScriptOptions &options) {
  try {
    return run_script_impl(document, selected_index, source, options);
  } catch (const ScriptCancelled &) {
    throw;
  } catch (const ScriptError &) {
    throw;
  } catch (const std::exception &error) {
    throw ScriptError(error.what());
  }
}
std::string_view script_language_name(ScriptLanguage language) {
  return language == ScriptLanguage::lua ? "Lua 5.5"
                                         : "Apophysis Pascal subset";
}
std::string script_api_reference() {
  return R"API(Apophysis Reborn scripting — Lua 5.5

All modern flame/transform indices start at 1. RGB channels are 0..1.
Scripts run on a private document. Success applies one undo step. Stop/error
rolls back all edits and discards every render/export request.

apo.count(), apo.selected(), apo.select(index)
apo.flame(), apo.set_flame{ name, width, height, center={x,y}, scale,
  zoom, rotate, gamma, brightness, vibrancy, quality, oversample,
  filter_radius, clear_filter_radius, gamma_threshold, background={r,g,b},
  palette_mode='step'|'linear', time }
  time is stored keyframe metadata only; no animation evaluator is present.
apo.transform_count(), apo.transform(index), apo.set_transform(index, fields)
apo.add_transform(fields), apo.remove_transform(index), apo.clear_transforms()
apo.final_transform(), apo.set_final_transform(fields_or_nil)
  Transform fields: pre/post={a,b,c,d,e,f}, weight, color, color_speed,
  opacity, julian_power, julian_dist, chaos={...}, variations={...}.
  Affine: x'=a*x+c*y+e; y'=b*x+d*y+f.
  Variations: {{name='linear',weight=1,parameters={}}}, or {linear=1}.
  Enabled native plugins use the same name/weight/parameters structure.
apo.palette(), apo.set_palette(256_rgb_triples)
apo.palette_from_stops({{0,r,g,b},{255,r,g,b}}, 'rgb'|'hsv')
apo.rotate_palette(shift), apo.reverse_palette(), apo.random_palette(seed, stops)
apo.new_flame(name), apo.duplicate_flame(name), apo.remove_flame()
apo.generate{seed,count,min_transforms,max_transforms,variations={names},
  width,height,symmetry,reflection,randomize_palette}
  Appends generated flames and selects the first. Returns its index.
apo.mutate{seed,strength,trend,geometry,variations,colors,parameters,palette,
  include_final,allowed_variations={names}}
apo.random(), apo.random(max), apo.random(min,max), apo.randomseed(seed)
  Full uint64 seeds can be supplied as decimal strings.
Built-in term parameters use local names, e.g. {name="perspective",weight=1,parameters={angle=0.25,dist=2}}.
apo.variations() -> available built-in and enabled plugin names
print(...), apo.print(...) -> bounded script console
apo.render('relative.png', {samples,seed,width,height,format,alpha,threads,
  memory_mib,disk_mib,backend='cpu'|'auto'|'vulkan'})
apo.export_flame('relative.flame'), apo.export_library('library.flame')
apo.export_palette('palette.gpl')
  Deferred requests require the matching host capability. Destinations must be
  unique safe relative paths. The host resolves them under its output folder.
apo.import_library('alias') replaces the working document with a host-supplied
  named import; it never opens a filesystem path.

Safe base/math/table/utf8 and bounded string primitives are available.
No io, os, package, require, debug, coroutine, load, dofile, pcall/xpcall,
metatable access, string patterns or bytecode loading. Resource limits apply.

Legacy mode accepts the documented Pascal subset in LEGACY_SCRIPTING.md.
Legacy indices start at 0; unsupported syntax/APIs fail during preflight.
)API";
}
std::vector<ScriptExample> script_examples() {
  return {
      {"Palette and geometry", ScriptLanguage::lua,
       R"LUA(-- Edit the current flame; all changes form one undo step.
apo.set_flame {name = 'Script study', gamma = 2.2}
for i = 1, apo.transform_count() do
  local t = apo.transform(i)
  t.color = (i - 1) / math.max(1, apo.transform_count() - 1)
  t.pre[5] = t.pre[5] + 0.08 * math.sin(i)
  apo.set_transform(i, t)
end
apo.palette_from_stops({{0,0.02,0.03,0.12}, {128,0.9,0.12,0.05}, {255,1,0.9,0.6}})
print('Edited', apo.transform_count(), 'transforms')
)LUA"},
      {"Seeded generation", ScriptLanguage::lua,
       R"LUA(local first = apo.generate {seed = '18446744073709551615', count = 3,
  min_transforms = 3, max_transforms = 4, variations = {'linear', 'swirl'}}
for i = first, first + 2 do
  apo.select(i)
  apo.mutate {seed = 70 + i, strength = 0.15, colors = true}
  print(i, apo.flame().name)
end
)LUA"},
      {"Batch render requests", ScriptLanguage::lua,
       R"LUA(-- Enable render requests and choose an output folder in the host.
for i = 1, apo.count() do
  apo.select(i)
  apo.render(string.format('study-%03d.png', i),
             {width = 640, height = 480, samples = 300000, seed = 91})
end
print('Prepared', apo.count(), 'render requests')
)LUA"},
      {"Legacy triangle construction", ScriptLanguage::legacy_pascal,
       R"ASC({ Original Apophysis-style API, interpreted by the strict Pascal subset. }
var i: Integer;
begin
  Clear;
  Flame.Name := 'Legacy triangles';
  Flame.Width := 640;
  Flame.Height := 480;
  Flame.Scale := 12;
  for i := 0 to 2 do
  begin
    AddTransform;
    Transform.a := 0.5;
    Transform.d := 0.5;
    Transform.e := Cos(i * 2 * Pi / 3);
    Transform.f := Sin(i * 2 * Pi / 3);
    Transform.Color := i / 2;
    Transform.Weight := 1;
    Transform.Linear := 1;
  end;
  Print('Created ' + IntToStr(Transforms) + ' transforms');
end.
)ASC"}};
}
} // namespace apo
