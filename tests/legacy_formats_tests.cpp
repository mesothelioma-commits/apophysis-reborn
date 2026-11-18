#include "flame_io.hpp"
#include "legacy_formats.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace apo;
void require(bool v, const char *message) {
  if (!v)
    throw std::runtime_error(message);
}
void near(double a, double b, const char *message) {
  require(std::isfinite(a) && std::abs(a - b) < 1e-12, message);
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
  if (!in)
    throw std::runtime_error("Cannot open fixture: " + path.string());
  return {std::istreambuf_iterator<char>(in), {}};
}
std::filesystem::path fixture_directory =
    std::filesystem::path(__FILE__).parent_path() / "fixtures/formats";
std::filesystem::path fixtures() { return fixture_directory; }
void palette_equal(const Palette &a, const Palette &b, const char *message) {
  for (std::size_t i = 0; i < 256; ++i) {
    near(a[i].r, b[i].r, message);
    near(a[i].g, b[i].g, message);
    near(a[i].b, b[i].b, message);
  }
}
void source_palette_vectors() {
  for (const std::string name :
       {"apophysis-209-export", "apophysis-209-cyclic", "apophysis-209-ties"}) {
    const auto imported = import_ugr(read(fixtures() / (name + ".ugr")));
    require(imported.size() == 1 && imported[0].report.supported(),
            "original source UGR supported");
    const auto expected =
        import_fractint_map(read(fixtures() / (name + ".map")));
    palette_equal(imported[0].palette, expected,
                  "independent Pascal interpolation vectors");
    const auto exported =
        import_ugr(export_ugr(expected, "Roundtrip \"quoted\""));
    require(exported[0].name == "Roundtrip \"quoted\"", "quoted UGR title");
    palette_equal(exported[0].palette, expected,
                  "256-color UGR exact RGB8 roundtrip");
    palette_equal(
        import_fractint_map(export_fractint_map(expected, "MAP roundtrip")),
        expected, "256-row MAP exact RGB8 roundtrip");
  }
}
void palette_boundaries() {
  const auto pairs = import_ugr(
      "; comment\nA { gradient: title=\"A\" smooth=no index=0 color=255 "
      "index=399 color=16711680 }\nB { gradient: index=-1 color=65280 }");
  require(pairs.size() == 2, "multiple UGR entries");
  near(pairs[0].palette[0].r, 1, "packed UGR red byte");
  near(pairs[0].palette[255].b, 1, "packed UGR blue byte");
  near(pairs[1].palette[0].g, 1,
       "single and wrapped stop produces constant palette");
  require(!import_ugr("A { gradient: smooth=yes index=0 color=0 }")[0]
               .report.supported(),
          "unsupported gradient smoothing reported");
  require(!import_ugr(
               "A { gradient: index=0 color=0 opacity: index=0 opacity=0 }")[0]
               .report.supported(),
          "opacity not silently lost");
  require(!import_ugr("A { gradient: index=0 color=0 hue=1 }")[0]
               .report.supported(),
          "unknown color behavior reported");
  for (const auto *text :
       {"A { gradient: index=400 color=0 }",
        "A { gradient: index=0 color=16777216 }", "A { gradient: color=1 }",
        "A { gradient: index=0 }", "A { gradient: index=0 color=1",
        "A { gradient: title=\"unterminated }"})
    rejects([&] { import_ugr(text); }, "malformed UGR rejected");
  rejects([&] { import_ugr(std::string(8 * 1024 * 1024 + 1, ' ')); },
          "UGR input bounded");
  rejects([&] { import_fractint_map("0 0 0\n"); }, "short MAP refused");
  auto map = export_fractint_map(default_palette());
  rejects([&] { import_fractint_map(map + "0 0 0\n"); }, "long MAP refused");
  map.replace(0, map.find('\n'), "256 -1 0");
  rejects([&] { import_fractint_map(map); }, "invalid MAP channel refused");
  auto invalid = default_palette();
  invalid[9].r = std::numeric_limits<double>::quiet_NaN();
  rejects([&] { export_ugr(invalid, "bad"); },
          "invalid palette export refused");
}
void upr_source_and_roundtrip() {
  auto imported = import_upr(read(fixtures() / "apophysis-209-export.upr"));
  require(imported.size() == 1 && imported[0].report.supported() &&
              imported[0].flame,
          "original source UPR supported");
  const auto &f = *imported[0].flame;
  require(f.width == 800 && f.height == 600 && f.transforms.size() == 2 &&
              f.final_transform,
          "original dimensions, transforms and final transform");
  near(f.center.x, .25, "UPR camera X");
  near(f.center.y, -.375, "UPR camera Y inversion");
  near(f.scale, 400, "UPR reference camera normalization");
  const auto &t = f.transforms[0];
  near(t.pre.a, .5, "UPR cfa");
  near(t.pre.b, .125, "UPR cfc maps to XML b");
  near(t.pre.c, -.25, "UPR cfb maps to XML c");
  near(t.pre.d, .5, "UPR cfd");
  near(t.pre.e, .75, "UPR cfe");
  near(t.pre.f, -.5, "UPR cff");
  near(t.color_speed, .75, "UPR legacy symmetry conversion");
  near(f.background.r, 17.0 / 255, "UPR packed background red");
  near(f.background.b, 51.0 / 255, "UPR packed background blue");
  require(t.parameters.apophysis_math,
          "UPR activates identified producer math");
  auto exported = export_upr(f);
  require(exported.report.supported() && !exported.text.empty() &&
              !exported.report.warnings.empty(),
          "compatible UPR export with limitations report");
  const auto second = import_upr(exported.text);
  require(second[0].flame.has_value(), "exported UPR reimports");
  require(serialize_flame(*second[0].flame) == serialize_flame(f),
          "UPR supported model roundtrip");
  auto wide = f;
  wide.width = 1600;
  wide.height = 500;
  wide.zoom = 1.5;
  auto wider = import_upr(export_upr(wide).text);
  near(wider[0].flame->scale, wide.scale * std::exp2(wide.zoom),
       "wide UPR camera effective scale");
  auto narrow = f;
  narrow.width = 500;
  narrow.height = 1200;
  auto narrower = import_upr(export_upr(narrow).text);
  near(narrower[0].flame->scale, narrow.scale, "portrait UPR camera scale");
}
void upr_reports() {
  const auto source = read(fixtures() / "apophysis-209-export.upr");
  auto replace = [&](const std::string &a, const std::string &b) {
    auto s = source;
    const auto pos = s.find(a);
    require(pos != std::string::npos, "replacement field exists");
    s.replace(pos, a.size(), b);
    return s;
  };
  for (const auto &s :
       {replace("mt-pixel", "mandelbrot"), replace("layers=1", "layers=2"),
        replace("p_xf0_var_linear=1", "p_xf0_var_unknown=1"),
        replace("p_contrast=1", "p_contrast=2"),
        replace("p_xforms=2", "p_xforms=999999"), replace("magn=2", "magn=nan"),
        replace("mergemode=normal", "mergemode=multiply"),
        replace("p_xf0_var_linear=1",
                "p_xf0_var_linear=1 p_xf0_par_linear_unrecognized=1")}) {
    auto result = import_upr(s);
    require(result.size() == 1 && !result[0].report.supported() &&
                !result[0].flame,
            "incompatible UPR does not yield renderable model");
    require(result[0].source.find('{') != std::string::npos,
            "unsupported original entry retained");
  }
  auto extra =
      import_upr(replace("p_xf0_var_linear=1",
                         "p_xf0_var_linear=1 p_xf0_par_unused_module_power=2 "
                         "p_xf0_par_unused_module_power=2"));
  require(extra[0].flame && extra[0].report.supported() &&
              extra[0].report.warnings.size() >= 2,
          "historical redundant inactive parameters accepted with report");
  auto f = *import_upr(source)[0].flame;
  auto refuse = [&](const std::function<void(Flame &)> &change) {
    auto copy = f;
    change(copy);
    auto e = export_upr(copy);
    require(!e.report.supported() && e.text.empty(),
            "unsupported UPR export refused atomically");
  };
  refuse([](Flame &a) { a.rotate = 10; });
  refuse([](Flame &a) { a.vibrancy = .5; });
  refuse([](Flame &a) { a.gamma_threshold = .1; });
  refuse([](Flame &a) { a.transforms[0].post.e = .1; });
  refuse([](Flame &a) { a.transforms[0].opacity = .5; });
  refuse([](Flame &a) { a.transforms[0].chaos = {0, 1}; });
  refuse([](Flame &a) { a.filter_radius.reset(); });
  refuse([](Flame &a) { a.palette_mode = PaletteMode::linear; });
  auto nonlinear = f;
  nonlinear.transforms[0].variations = {{Variation::swirl, 1}};
  nonlinear.transforms[0].parameters.apophysis_math = false;
  require(!export_upr(nonlinear).report.supported(),
          "non-Apophysis nonlinear math blocked");
}
} // namespace
int main(int argc, char **argv) {
  if (argc > 1)
    fixture_directory = argv[1];
  try {
    source_palette_vectors();
    palette_boundaries();
    upr_source_and_roundtrip();
    upr_reports();
    std::cout << "Legacy format tests passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
