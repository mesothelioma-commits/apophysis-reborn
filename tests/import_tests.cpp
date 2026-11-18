#include "flame_io.hpp"

#include <cmath>
#include <iostream>
#include <numbers>
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
    if (!std::isfinite(actual) || std::abs(expected - actual) > 1e-12) throw std::runtime_error(std::string(detail));
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

void test_producer_import() {
    std::string hex;
    for (int i = 0; i < 256; ++i) hex += "001234fe";
    const std::string packed = "<palette count='256' format='RGBA'>" + hex + "</palette>";
    const auto test_xml = document("<xform weight='1' var='0 .5 1' color_speed='-.5' plotmode='off'/>"
         "<finalxform enabled='0' linear='1'/><edit><flame unknown='ignore'><p>History only</p></flame></edit>" + packed,
         "version='Apophysis 2.09' angle='1.5707963267948966' rotate='17' filter='0' gamma_threshold='.04'");
    const auto f = parse_flames("<Flames name='stock'>" + test_xml + "</Flames>")[0];
    check(f.filter_radius == 0 && f.gamma_threshold == .04 && f.gamma == 1 && f.quality == 50,
          "specific producer filter and constructor defaults");
    close(-90, f.rotate, "angle in radians takes precedence for tagged Apophysis");
    check(!f.final_transform && f.transforms[0].opacity == 0 && f.transforms[0].color_speed == .75,
          "disabled final, off and signed classic color_speed");
    check(f.transforms[0].variations.size() == 2 && f.transforms[0].variations[0].id == Variation::sinusoidal &&
        f.transforms[0].variations[0].weight == .5 && f.transforms[0].variations[1].id == Variation::spherical,
        "numbered weights in a classic vector");
    close(0x12/255., f.palette[8].r, "packed leading unused byte");
    close(0xfe/255., f.palette[4].b, "packed blue");
    const auto reread = parse_flames(serialize_flame(f))[0];
    check(reread.transforms[0].color_speed == .75 && reread.filter_radius == 0 && reread.gamma_threshold == .04,
        "canonical producer output reads consistently");
    const auto colors = "<colors count='256' data='" + hex + "'/>";
    auto seven = parse_flames(document("<xform weight='1' julian='1' julian_power='4' julian_dist='-1'/>" + colors,
           "version='Apophysis 7x' filter='.5' enable_de='0' estimator_radius='9' new_linear='1' plugins='julian'"))[0];
    check(seven.filter_radius == .5 && seven.transforms[0].parameters.julian_power == 4 &&
          seven.transforms[0].parameters.julian_dist == -1, "7X JuliaN parameters");
    close(0x34/255., seven.palette[0].g, "legacy colors attribute");
    auto hack = parse_flames(document("<xform weight='1' linear3D='2'/>"+colors,"version='Apophysis 2.08 3D hack'"))[0];
    check(hack.transforms[0].variations.size() == 1 && hack.transforms[0].variations[0].id == Variation::linear &&
         hack.transforms[0].variations[0].weight == 2, "uncoupled 3D-hack linear projection");
    const std::string opts = "version='Apophysis AV Phoenix Edition' new_linear='1'";
    const auto av = parse_flames(document("<xform weight='1' blur='.3' flatten='1' noise='.4' var_order='noise blur flatten'/>"+colors,opts))[0];
    check(av.transforms[0].variations.size() == 2 && av.transforms[0].variations[0].id == Variation::noise &&
          av.transforms[0].variations[1].id == Variation::blur, "Z-only flatten and AV execution order");
    const auto ordered = parse_flames(document("<xform weight='1' blur='.3' noise='.4' var_order='blur noise'/>"+colors,opts))[0];
    check(parse_flames(serialize_flame(ordered))[0].transforms[0].variations[0].id == Variation::blur,
          "canonical save retains non-canonical additive order");
    rejects(document("<xform weight='1' julian='1'/>"+colors,opts), "explicit julian_power");
    rejects(document("<xform weight='1' var1='2'/>"+colors,opts), "original");
    const auto classic = "version='Apophysis 2.09'";
    auto gaussian = parse_flames(document("<xform weight='1' var1='28'/>" + colors, classic))[0];
    check(gaussian.transforms[0].variations[0].id == Variation::gaussian_blur, "2.09 gaussian ID");
    rejects(document("<xform weight='1' var1='40'/>" + colors, classic), "ID 40");
    rejects(document("<xform weight='1' var1='30'/>" + colors, "version='Apophysis 2.08'"), "ID 30");
    const auto stock = parse_flames(document("<xform weight='1' var1='38' curl_c1='-.4' curl_c2='.125'/>"
        "<finalxform var1='37' juliascope_power='4'/>" + colors, classic));
    check(stock[0].transforms[0].variations[0].id == Variation::curl &&
          stock[0].final_transform->variations[0].id == Variation::juliascope, "stock 2.09 registered module IDs");
    const auto mixed = parse_flames(document("<xform weight='1' juliascope='1' juliascope_power='4' "
        "radial_blur='.4' radial_blur_angle='.2' blur='.2'/>" + colors, classic))[0];
    check(mixed.transforms[0].variations[0].id == Variation::blur &&
          mixed.transforms[0].variations[1].id == Variation::radial_blur &&
          mixed.transforms[0].variations[2].id == Variation::juliascope, "stock 2.09 iteration order");
    rejects(document("<xform weight='1' pdj='1' pdj_a='.5'/>" + colors, classic), "explicit pdj_b");
    const auto historical = parse_flames(document(
        "<xform weight='1' juliascope='1' juliascope_power='-2.8'/>"
        "<finalxform blob='1' blob_low='.25' blob_high='.9' blob_waves='4.5'/>" + colors, classic))[0];
    close(-3, historical.transforms[0].parameters.juliascope_power, "historical rounded JuliaScope");
    close(4, historical.final_transform->parameters.blob_waves, "historical ties-to-even blob");
    const auto defaults7 = parse_flames(document("<xform weight='1' curl='1' ngon='1'/>" + colors,
                                                  "version='Apophysis 7x' new_linear='1'"))[0];
    const auto &p7 = defaults7.transforms[0].parameters;
    check(p7.curl_c1 == 0 && p7.curl_c2 == 0 && p7.ngon_sides == 4 &&
          p7.ngon_power == 2 && p7.ngon_corners == 1, "7X deterministic defaults");
    for (auto id : {Variation::blob, Variation::pdj, Variation::fan2, Variation::rings2,
                    Variation::perspective, Variation::juliascope, Variation::gaussian_blur,
                    Variation::radial_blur, Variation::pie, Variation::ngon, Variation::curl,
                    Variation::rectangles}) {
        std::string weights;
        for (unsigned n = 0; n < static_cast<unsigned>(id); ++n) weights += "0 ";
        const auto flame = parse_flames(document("<xform weight='1' var1='" +
            std::to_string(static_cast<unsigned>(id)) + "'/><finalxform var='" + weights + ".4'/>" +
            std::string(white)))[0];
        check(flame.transforms[0].variations[0].id == id && flame.final_transform->variations[0].id == id &&
              flame.final_transform->variations[0].weight == .4, "generic numbered variation IDs");
    }
    rejects(document("<xform weight='1' julian='1' julian_power='0'/>"+colors,opts), "julian_power");
    rejects(document("<xform weight='1' blur='1' noise='1' var_order='blur'/>"+colors,opts), "cover every");
    rejects(document(std::string(transform)+colors,opts + " enable_de='1' estimator_radius='9'"), "density");
    rejects(document(std::string(transform)+colors,opts + " cam_pitch='1'"), "3D");
    rejects(document("<xform weight='1' linear3D='1'/>"+colors, "version='Apophysis 2.08 3D hack' cam_yaw='.2'"), "3D");
    rejects(document(std::string(transform)+colors,opts + " curves='0 1'"), "curves");
    rejects(document(std::string(transform)+colors,"filter_shape='lanczos3'"), "filter_shape");
    rejects(document(std::string(transform)+colors,"gamma_threshold='-1'"), "gamma_threshold");
}

void test_density_estimator_settings() {
    const auto body = std::string(transform) + std::string(white);
    for (auto version : {"FLAM3-LNX-3.1.1", "FLAM3-WIN-3.1.1", "FLAM3-OSX-3.1.1"}) {
        auto tag = std::string("version='") + version + "'";
        check(parse_flames(document(body,tag))[0].density_estimator == DensityEstimator{},
              "Tagged FLAM3 constructors enable the default radius");
        check(!parse_flames(document(body,tag + " estimator_radius='0'"))[0].density_estimator,
              "Explicit zero must override FLAM3 defaults");
    }
    check(!parse_flames(document(body))[0].density_estimator, "Unversioned flame changed without an estimator");
    check(parse_flames(document(body,"estimator_radius='2'"))[0].density_estimator == DensityEstimator{2,0,.4},
          "An explicit radius should use the default minimum and curve");
    rejects(document(body, "version='Other renderer' estimator_radius='3'"), "density");
    rejects(document(body, "estimator_curve='.4'"), "estimator_radius");
    rejects(document(body, "estimator_radius='3' estimator_curve='0'"), "density");
    const auto all_colors = std::string(transform) + "<colors count='256' data='" + std::string(256 * 8, '0') + "'/>";
    for (auto flag : {"0", "1"}) {
        const auto attrs = std::string("version='Apophysis 7X Version 15D' enable_de='") + flag + "' estimator_radius='9'";
        check(!parse_flames(document(all_colors,attrs))[0].density_estimator, "Pinned 7X does not enable the FLAM3 estimator");
    }
}
void test_extended_variations() {
    constexpr double pi = std::numbers::pi;
    Rng rng(2), draws(2);
    Point bubble = apply_variation(Variation::bubble,{2,0},rng);
    close(1.,bubble.x,"bubble radius"); close(0,bubble.y,"bubble axis");
    Point foci = apply_variation(Variation::foci,{std::log(2.),pi/2},rng);
    close(.6,foci.x,"foci exponential pair"); close(.8,foci.y,"foci sine");
    VariationParameters julian{4, 1};
    const double branch = std::floor(draws.unit()*4);
    Point j = apply_variation(Variation::julian,{4,0},rng,julian);
    close(std::sqrt(2.)*std::cos(branch*pi/2),j.x,"JuliaN radius and branch x");
    close(std::sqrt(2.)*std::sin(branch*pi/2),j.y,"JuliaN radius and branch y");
    for (Variation id : {Variation::blur, Variation::noise}) {
        const double theta = 2*pi*draws.unit(), r = draws.unit();
        Point sample = apply_variation(id,{-2,3},rng);
        close(r*std::cos(theta)*(id == Variation::noise ? -2 : 1),sample.x,"random term x");
        close(r*std::sin(theta)*(id == Variation::noise ? 3 : 1),sample.y,"random term y");
    }
    Transform t;
    t.parameters.apophysis_math = true;
    t.variations = {{Variation::bubble, 1},{Variation::pre_blur, .6}};
    std::array<double,4> ring{draws.unit(),draws.unit(),draws.unit(),draws.unit()};
    for (unsigned n = 0; n < 6; ++n) {
        const double r = .6*(ring[0]+ring[1]+ring[2]+ring[3]-2);
        ring[n%4] = draws.unit();
        const double angle = ring[n%4]*2*pi;
        const double x = 1 + r*std::cos(angle), y = .25 + r*std::sin(angle);
        const double d = 1+(x*x+y*y)/4;
        const auto actual = apply_transform(t,{{1,.25},.5},rng);
        close(x/d,actual.point.x,"pre-blur ring before ordinary variation x");
        close(y/d,actual.point.y,"pre-blur ring before ordinary variation y");
    }
}

void test_rejections() {
    const std::string normal = std::string(transform) + std::string(white);
    rejects(document(normal, "size='32 40x'"), "size");
    rejects(document(normal, "gamma='NaN'"), "gamma");
    rejects(document(normal, "scale='1e9999'"), "scale");
    rejects(document(normal, "palette='15'"), "Indexed palette");
    rejects(document(normal, "palette_mode='other'"), "palette_mode");
    rejects(document(normal, "filter='-0.5'"), "filter");
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
        test_producer_import();
        test_density_estimator_settings();
        test_extended_variations();
        std::cout << "XML collection, geometry, palette, round-trip and diagnostic checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Import tests: " << e.what() << '\n';
        return 1;
    }
}
