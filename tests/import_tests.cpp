#include "flame_io.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {

using namespace apo;
using Text = std::string_view;
constexpr Text transform = R"(<xform weight="1" linear="1"/>)";
constexpr Text white = R"(<color index="0" rgb="255 255 255"/>)";

std::string document(Text body, Text settings = {}) {
    return "<flame " + std::string(settings) + ">" + std::string(body) + "</flame>";
}

void check(bool pass, Text detail) {
    if (!pass) throw std::runtime_error(std::string(detail));
}

void close(double expected, double actual, Text detail) {
    if (std::abs(expected - actual) > 1e-12) throw std::runtime_error(std::string(detail));
}

void rejects(Text xml, Text diagnostic) {
    try { (void)parse_flames(xml); }
    catch (const std::runtime_error& e) {
        check(Text(e.what()).find(diagnostic) != Text::npos, e.what());
        return;
    }
    throw std::runtime_error("Expected rejection for " + std::string(diagnostic));
}

void test_collection_and_geometry() {
    const auto body = R"(<xform weight="1" color=".25" opacity=".5" symmetry="-.5" chaos=".2 1"
        coefs="2 3 4 5 6 7" post="-1 +2 3 4 5 6" swirl=".2" linear="1.25"/>
      <xform weight="2" var1="4"/>
      <finalxform linear="1" opacity=".7"/>
      <color index="32" rgb="0 200 128"/><color index="160" rgb="255 0 128"/>)";
    const auto settings = R"(size="320 200" scale="39.5" center="-.4 +.25" rotate="15" zoom="1.2"
       supersample="2" gamma="1.5" quality="12.25" brightness="3" vibrancy=".8" palette_mode="linear"
       background="0 .5 1" name="A &amp; B" time="1.5" version="source &quot;1&quot;")";
    const auto other = document(std::string(transform) + std::string(white));
    const auto batch = parse_flames("<flames name='collection'>" + document(body, settings) + other + "</flames>");
    check(batch.size() == 2, "multi-flame collection");
    const auto& flame = batch[0];
    check(flame.name == "A & B" && flame.source_version == "source \"1\"" && flame.time == 1.5, "metadata");
    check(flame.width == 320 && flame.height == 200 && flame.oversample == 2, "size/supersample");
    check(flame.center.x == -.4 && flame.center.y == .25 && flame.rotate == 15 && flame.zoom == 1.2,
          "camera");
    check(flame.scale == 39.5 && flame.gamma == 1.5 && flame.quality == 12.25 &&
          flame.vibrancy == .8 && flame.brightness == 3, "tone and quality");
    check(flame.palette_mode == PaletteMode::linear && flame.background.g == .5, "palette mode/background");
    const auto& x = flame.transforms.at(0);
    check(x.pre.apply({2, 1}).x == 14 && x.pre.apply({2, 1}).y == 18, "XML coefficient order");
    check(x.post.apply({2, 1}).x == 6 && x.post.apply({2, 1}).y == 14, "post coefficient order");
    check(x.color_speed == .75 && x.color == .25 && x.opacity == .5, "symmetry/opacity");
    check(x.chaos == std::vector<double>({.2, 1}), "transition row");
    check(x.variations.size() == 2 && x.variations[0].id == Variation::linear && x.variations[0].weight == 1.25 &&
          x.variations[1].id == Variation::swirl, "canonical mixed terms");
    check(flame.transforms[1].variations[0].id == Variation::horseshoe && flame.transforms[1].color == 1, "var1/default color");
    check(flame.final_transform && flame.final_transform->opacity == .7, "final transform");
    close(.5, flame.palette[96].r, "sparse interior");
    close(.5, flame.palette[224].r, "sparse wrap");
    close(.25, flame.palette[0].r, "sparse wrap at zero");
    check(batch[1].width == 100 && batch[1].height == 100 && batch[1].gamma == 4 && batch[1].quality == 1 &&
          batch[1].palette_mode == PaletteMode::step, "document defaults or leakage");
    check(batch[1].palette[200].g == 1 && !batch[1].final_transform, "document palette leakage");
    const auto reordered = parse_flames(document("<finalxform linear='1'/>" + std::string(transform) +
                                                 std::string(white))).at(0);
    check(reordered.final_transform->color == 0 && reordered.transforms[0].color == 1,
          "input index before moving final transform");
    const auto canonical = serialize_flame(flame);
    const auto reread = parse_flames(canonical).at(0);
    check(reread.name == flame.name && reread.source_version == flame.source_version &&
          reread.scale == flame.scale && reread.quality == flame.quality && reread.palette_mode == flame.palette_mode,
          "round-trip settings");
    const auto& rx = reread.transforms[0];
    check(rx.chaos == x.chaos && rx.variations[1].id == x.variations[1].id && rx.variations[1].weight == .2 &&
          rx.pre.c == x.pre.c && rx.post.b == x.post.b && rx.color_speed == x.color_speed &&
          reread.final_transform->opacity == .7, "round-trip geometry");
    for (std::size_t i = 0; i < flame.palette.size(); ++i) {
        close(flame.palette[i].r, reread.palette[i].r, "round-trip red");
        close(flame.palette[i].g, reread.palette[i].g, "round-trip green");
        close(flame.palette[i].b, reread.palette[i].b, "round-trip blue");
    }
}

void test_packed_palette() {
    for (const bool alpha : {false, true}) {
        const std::string spec = alpha ? "RGBA" : "RGB";
        const std::string code = alpha ? "2A00bFff" : "2A00bF";
        std::string pal = "<palette format='" + spec + "' count='256'>";
        for (int i = 0; i < 256; ++i) pal += code + ((i%7 == 0) ? "\n" : "");
        pal += "</palette>";
        std::string xml = document(std::string(transform) + pal);
        // Split the XML across the parser's 32 KiB streaming boundary inside palette text.
        xml.insert(0, std::string(32768 - 190, ' '));
        auto f = parse_flames(xml).at(0);
        close(42. / 255, f.palette[0].r, "packed first red");
        close(191. / 255, f.palette[255].b, "packed last blue");
        if (alpha) {
            pal.replace(pal.find(code), code.size(), "2A00bFfe");
            rejects(document(std::string(transform) + pal), "Transparent");
        }
    }
}

void test_rejections() {
    const std::string normal = std::string(transform) + std::string(white);
    rejects(document(normal, "size='32 40x'"), "size");
    rejects(document(normal, "gamma='NaN'"), "gamma");
    rejects(document(normal, "scale='1e9999'"), "scale");
    rejects(document(normal, "palette='15'"), "Indexed palette");
    rejects(document(normal, "palette_mode='other'"), "palette_mode");
    rejects(document(normal, "filter='0.5'"), "filter");
    rejects(document(normal, "oversample='2' supersample='2'"), "one of");
    rejects(document("<xform weight='1' mystery='1'/>" + std::string(white)), "mystery");
    rejects(document("<xform weight='1' var1='99'/>" + std::string(white)), "99");
    rejects(document("<xform weight='1' var1='1' linear='1'/>" + std::string(white)), "Cannot mix");
    rejects(document("<xform weight='1' coefs='1 0 0 1 0'/>" + std::string(white)), "coefs");
    rejects(document("<xform weight='0' linear='1'/>" + std::string(white)), "weight");
    rejects(document(normal + "<finalxform/><finalxform/>"), "More than one final");
    rejects(document(normal + "<finalxform weight='1'/>"), "selection weight");
    rejects(document(normal + "<finalxform chaos='1'/>"), "chaos");
    rejects(document(normal + "<xform><motion/></xform>"), "motion");
    rejects(document(transform), "Missing embedded palette");
    rejects(document(normal + std::string(white)), "Duplicate palette");
    rejects(document(std::string(transform) + "<color index='256' rgb='0 0 0'/>"), "Color index");
    rejects(document(std::string(transform) + "<color index='1' rgba='0 0 0 123'/>"), "Transparent");
    rejects(document(std::string(transform) + "<palette format='RGB' count='256'>123</palette>"), "length");
    rejects(document(normal + "<palette format='RGB' count='256'/>"), "palette definition");
    rejects(document(normal + "bad"), "Unexpected text");
    rejects("<flames/>", "no flames");
    rejects("<flame>\n<xform></flame>", "line 2");
    rejects("<!DOCTYPE flame [<!ENTITY title 'test'>]><flame/>", "DOCTYPE");
    rejects("<flames>" + document(normal) + document(normal, "unknown='1'") + "</flames>", "unknown");
}

} // namespace

int main() {
    try {
        test_collection_and_geometry();
        test_packed_palette();
        test_rejections();
        std::cout << "XML collection, geometry, palette, round-trip and diagnostic checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Import tests: " << e.what() << '\n';
        return 1;
    }
}
