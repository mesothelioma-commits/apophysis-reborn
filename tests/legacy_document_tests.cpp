#include "legacy_document.hpp"
#include "flame_io.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace {
using namespace apo;
using Text = std::string_view;
const std::string supported = "<xform weight='1' linear='1'/><color index='0' rgb='255 255 255'/>";
void check(bool result, Text label) {
    if (!result) throw std::runtime_error(std::string(label));
}
template<class Fn> void rejects(Fn action, Text expected) {
    try { action(); }
    catch (const std::exception& e) {
        if (Text(e.what()).find(expected) == Text::npos) throw std::runtime_error(e.what());
        return;
    }
    throw std::runtime_error("Expected diagnostic containing " + std::string(expected));
}
std::string replaced(std::string source, Text old, Text replacement) {
    source.replace(source.find(old), old.size(), replacement);
    return source;
}
void test_new_variation_fields() {
    const std::string bytes = "<flame name='Fields'><xform weight='1' var1='23' blob_low='0.2500' blob_high='1'/>"
        "<!--retain--><finalxform perspective='1' perspective_dist='0.0' perspective_angle='.2'/>"
        "<color index='0' rgb='255 255 255'/></flame>";
    auto doc = LegacyDocument::parse(bytes);
    LegacyTransformEdits edit;
    edit.variation_parameters["blob_low"] = .5;
    doc.edit_transform(0, 0, edit);
    check(doc.bytes() == replaced(bytes, "0.2500", "0.5"), "one parameter rewrote other flame bytes");
    edit = {};
    edit.variation_parameters["perspective_angle"] = .125;
    doc.edit_transform(0, 0, edit, true);
    check(doc.bytes() == replaced(replaced(bytes, "0.2500", "0.5"), "'.2'", "'0.125'"),
          "final parameter rewrite affected explicit zero or source layout");
    const auto saved = doc.bytes();
    edit = {};
    edit.variation_parameters["juliascope_power"] = 0;
    rejects([&] { doc.edit_transform(0, 0, edit); }, "nonzero");
    check(doc.bytes() == saved, "invalid parameter edit was partially committed");
}
void test_archival_collection() {
    const std::string before = "\xef\xbb\xbf<?xml version='1.0'?>\r\n<?saved by-app?>\r\n<Flames name='My &amp; your forms'>";
    const std::string first = "<flame alt='a > b'\n name = 'F&#x69;rst' version='Apophysis 2.08 beta 2'"
        " size='040  +32' center='0&#x20;+.50' scale='2.500' >" + supported + "</flame>";
    const std::string second = "<flame name=\"Nested\" future='3'><xform vendor_thing='1'/>"
        "<edit><flame name='Historical version'/></edit><![CDATA[<flame/>]]></flame>";
    const std::string third = "<flame name='Good'>" + supported + "</flame>";
    const std::string after = "<!-- <flame/> --></Flames>\r\n";
    const std::string xml = before+first+second+third+after;
    auto d = LegacyDocument::parse(xml);
    check(d.bytes() == xml && d.entries().size() == 3 && d.entries()[0].name == "First" &&
          d.entries()[0].version == "Apophysis 2.08 beta 2", "original bytes and direct child metadata");
    check(d.element_bytes(1) == second, "original entry range");
    check(d.render_issue(0)->find("alt") != std::string::npos, "unsupported field explained");
    check(d.render_issue(1)->find("future") != std::string::npos, "next entry gives its own diagnostic");
    check(!d.render_issue(2) && d.render_model(2).name == "Good", "uppercase wrapper and sibling independence");

    LegacyEdits change;
    change.name = "First"; change.size = {{40,32}}; change.center = Point{0,.5}; change.scale = 2.5;
    d.edit(0,change);
    check(d.bytes() == xml, "equal values must retain entity spellings, numeric precision and white space");
    change.name = "Caf\xc3\xa9 & 'stars' <\xe9\x9b\xaa>\t\n\"";
    change.size = {{64,48}}; change.center = Point{-.5,.25}; change.scale = 5;
    d.edit(0,change);
    std::string news = replaced(first, "F&#x69;rst", "Caf&#233; &amp; &apos;stars&apos; &lt;&#38634;>&#9;&#10;\"");
    news = replaced(news, "040  +32", "64 48"); news = replaced(news, "0&#x20;+.50", "-0.5 0.25");
    news = replaced(news, "2.500", "5");
    check(d.bytes() == before+news+second+third+after && d.entries()[0].name == *change.name,
          "surgical edit changed unrelated source bytes");
    LegacyEdits again;
    again.name = "A longer & different name"; again.scale = 8;
    d.edit(1,again);
    const auto newsecond = replaced(replaced(second, "Nested", "A longer &amp; different name"),
                                    "future='3'>","future='3' scale=\"8\">");
    check(d.bytes() == before+news+newsecond+third+after && d.element_bytes(1) == newsecond,
          "sequential edit offsets or attribute insert");
    check(!LegacyDocument::parse(d.bytes()).render_issue(2), "round-trip reparsing of unaffected sibling");
}
void test_boundaries() {
    auto d = LegacyDocument::parse("<pick><flame/><flame name='abc'/>"
                                   "<flame name='yes'>" + supported + "</flame></pick>");
    check(d.element_bytes(0) == "<flame/>" && d.element_bytes(1) == "<flame name='abc'/>" &&
          d.render_model(2).name == "yes", "self-closing sibling offsets or pick wrapper");
    LegacyEdits edit; edit.name = "set";
    d.edit(0,edit);
    check(d.element_bytes(0) == "<flame name=\"set\"/>" && d.entries()[1].name == "abc",
          "insert name on empty element");
    const std::string pad(70000,'y');
    const std::string big = "<flame any='" + pad + "' name = \"old\" >" + supported + "</flame>";
    auto large = LegacyDocument::parse(big);
    large.edit(0,edit);
    check(large.bytes() == replaced(big,"old","set"), "Expat streaming boundary inside opening tag");

    for (const char* wrapper : {"flames","Flames","test","pick"}) {
        std::string w = wrapper;
        auto doc = LegacyDocument::parse("<"+w+"><flame>"+supported+"</flame></"+w+">");
        check(!doc.render_issue(0), "supported wrapper");
    }
    auto unknown = LegacyDocument::parse("<future><flame>"+supported+"</flame></future>");
    check(unknown.render_issue(0)->find("wrapper") != std::string::npos, "unrecognized container");
    auto settings = LegacyDocument::parse("<flames global='1'><flame>"+supported+"</flame></flames>");
    check(settings.render_issue(0)->find("global") != std::string::npos, "unrecognized collection settings");
    const std::string global_xml = "<flames><globalCamera scale='22'/><flame>" + supported + "</flame></flames>";
    auto global = LegacyDocument::parse(global_xml);
    check(global.bytes() == global_xml && global.render_issue(0)->find("globalCamera") != std::string::npos,
          "unknown collection-level extension must survive and block unqualified rendering");
    auto extra = LegacyDocument::parse("<flames><shared-palette/><flame>"+supported+"</flame></flames>");
    check(extra.render_issue(0)->find("shared-palette") != std::string::npos, "shared wrapper inputs cannot render silently");
    rejects([]{ (void)LegacyDocument::parse("<flames><flame></flames>"); }, "line");
    rejects([]{ (void)LegacyDocument::parse("<!DOCTYPE flame SYSTEM 'unknown'><flame/>"); }, "DOCTYPE");
    rejects([]{ (void)LegacyDocument::parse("<flames><meta><flame/></meta></flames>"); }, "no direct");
    const auto unedited = d.bytes();
    edit.name = "Bad" + std::string(1,'\0');
    rejects([&]{ d.edit(0,edit); }, "XML"); check(d.bytes() == unedited, "failed edit was committed");
    edit.name = {}; edit.center = Point{std::numeric_limits<double>::infinity(),0};
    rejects([&]{ d.edit(0,edit); }, "finite");
    edit.center = {}; edit.scale = -1;
    rejects([&]{ d.edit(0,edit); }, "positive");
    edit.scale = {}; edit.size = {{1,-1}};
    rejects([&]{ d.edit(0,edit); }, "positive");
    edit.size = {}; edit.name = std::string("\xc0\xa0");
    rejects([&]{ d.edit(0,edit); }, "UTF-8");
    check(d.bytes() == unedited, "invalid edits changed the document");
}

std::string utf16(Text ascii, bool le) {
    std::string out;
    for (char c : ascii) {
        out += le ? c : '\0';
        out += le ? '\0' : c;
    }
    return out;
}
void test_encodings() {
    for (bool le : {true,false}) {
        const std::string bom = le ? "\xff\xfe" : "\xfe\xff";
        const std::string src = "<?xml version='1.0' encoding='UTF-16'?><flames><!--keep-->"
                                "<flame name='old'>" + supported + "</flame></flames>";
        auto d = LegacyDocument::parse(bom + utf16(src,le));
        check(d.render_model(0).name == "old","UTF-16 strict model");
        LegacyEdits edits; edits.name = "\xe9\x9b\xaa"; d.edit(0,edits);
        check(d.bytes() == bom+utf16(replaced(src,"old","&#38634;"),le) &&
              d.render_model(0).name == "\xe9\x9b\xaa", "surgical UTF-16 edit, BOM or strict model");
    }
    for (const auto& pair : {std::pair{"ISO-8859-1","\xe9"}, {"windows-1252","\x80"}}) {
        const std::string name = pair.second;
        const std::string src = "<?xml version='1.0' encoding='" + std::string(pair.first) + "'?><flames>"
                                "<flame name='" + name + "'>" + supported + "</flame><!--" + name + "--></flames>";
        auto d = LegacyDocument::parse(src);
        check(d.bytes() == src && d.render_model(0).name == (name == "\xe9" ? "\xc3\xa9" : "\xe2\x82\xac"),
              "encoded legacy palette or name transcoded incorrectly");
        LegacyEdits edits; edits.name = "\xe5\xa2\xa8\xe9\x9b\xaa&"; d.edit(0,edits);
        check(d.bytes() == replaced(src,"name='"+name+"'","name='&#22696;&#38634;&amp;'") &&
              d.render_model(0).name == "\xe5\xa2\xa8\xe9\x9b\xaa&", "legacy encoding edit with Unicode characters");
    }
}

void test_transform_fields() {
    const std::string xform = "<xform name='Original &amp; named' weight='1.000' var1='0'"
        " coefs='1 0 0 1 0&#x20;+0.0' plotmode='on' symmetry='0'/>";
    const std::string sibling = "<flame name='Preserve' plugin='new'><xform new_variation='8'/></flame>";
    const std::string tail = "<!-- triangle note --><edit><flame historical='yes'/></edit>"
                             "<color index='0' rgb='32 128 244'/></flame>";
    const std::string prefix = "<Flames><flame name='Editable'>";
    auto document = LegacyDocument::parse(prefix + xform + tail + sibling + "</Flames>");
    const auto original = document.bytes();
    LegacyTransformEdits change;
    change.pre = Affine{}; change.weight = 1; change.opacity = 1;
    change.color_speed = .5; change.variations = std::vector<Term>{{Variation::linear,1}};
    document.edit_transform(0,0,change);
    check(document.bytes() == original,"no-op transform edit changed original spellings");
    change.pre = Affine{.5,.25,-.75,.6,.8,-.9};
    change.post = Affine{1,0,.3,1,0,.125};
    change.opacity = .6; change.color_speed = .25; change.color = .75;
    change.variations = std::vector<Term>{{Variation::linear,.4},{Variation::julian,.6}};
    change.julian_power = -3; change.julian_dist = .8;
    document.edit_transform(0,0,change);
    const auto model = document.render_model(0);
    const auto& t = model.transforms[0];
    check(t.pre.c == -.75 && t.post.f == .125 && t.opacity == .6 && t.color_speed == .25 &&
          t.color == .75 && t.variations.size() == 2 && t.parameters.julian_power == -3 &&
          t.parameters.julian_dist == .8,"edited transform model mismatch");
    check(document.element_bytes(1) == sibling && document.bytes().find(tail) != std::string::npos,
          "transform edit changed sibling, history or palette");
    check(document.bytes().find("plotmode=") == std::string::npos &&
          document.bytes().find("var1=") == std::string::npos &&
          document.bytes().find("symmetry='0.5'") != std::string::npos &&
          document.bytes().find("name='Original &amp; named'") != std::string::npos,
          "semantic aliases or authoring metadata changed incorrectly");
    const auto good = document.bytes();
    change = {}; change.color = 1.01;
    rejects([&]{ document.edit_transform(0,0,change); },"[0,1]");
    change = {}; change.variations = std::vector<Term>{{Variation::linear,1},{Variation::linear,2}};
    rejects([&]{ document.edit_transform(0,0,change); },"duplicate");
    change = {}; change.pre = Affine{std::numeric_limits<double>::infinity()};
    rejects([&]{ document.edit_transform(0,0,change); },"finite");
    change = {}; change.weight = 2;
    rejects([&]{ document.edit_transform(1,0,change); },"plugin");
    check(document.bytes() == good,"rejected transform edit damaged the collection");
}

void test_transform_structure() {
    const std::string xml = "<flame><xform name='First' weight='1' linear='1' chaos='1 .3 .5'/>"
        "<!-- kept in its slot --><xform name='Second' weight='2' swirl='1' chaos='.2 1 .3'/>"
        "<xform name='Third' weight='3' bubble='1' chaos='1 .7 .8'/>"
        "<finalxform linear='1'/><color index='0' rgb='255 128 32'/></flame>";
    auto d = LegacyDocument::parse(xml);
    const auto original = d.render_model(0);
    check(d.duplicate_transform(0,1) == 3,"duplicate index");
    auto m = d.render_model(0);
    check(m.transforms.size() == 4 && m.transforms[1].weight == 1 && m.transforms[3].weight == 1,
          "duplicate must split selection weight");
    check(m.transforms[0].chaos == std::vector<double>({1,.3,.5,.3}) &&
          m.transforms[2].chaos == std::vector<double>({1,.7,.8,.7}) &&
          m.transforms[3].chaos == original.transforms[1].chaos,
          "duplicate incoming or outgoing chaos columns");
    check(m.final_transform->color == original.final_transform->color &&
          m.transforms[3].color == original.transforms[1].color,"duplicate changed implicit color values");
    check(d.bytes().find("<!-- kept in its slot -->") != std::string::npos,"duplicate removed comments");
    d.move_transform(0,3,0);
    m = d.render_model(0);
    check(m.transforms[0].variations[0].id == Variation::swirl && m.transforms[0].color == 1 &&
          m.transforms[0].chaos == std::vector<double>({1,.2,1,.3}) &&
          m.transforms[1].chaos == std::vector<double>({.3,1,.3,.5}),"move changed graph identity");
    d.remove_transform(0,2);
    m = d.render_model(0);
    check(m.transforms.size() == 3 && m.transforms[0].chaos == std::vector<double>({1,.2,.3}) &&
          m.transforms[1].chaos == std::vector<double>({.3,1,.5}),"remove destination column");
    const auto before_add = m;
    Transform added; added.pre.e = 1.5; added.color = .27;
    check(d.add_transform(0,added) == 3,"append index");
    m = d.render_model(0);
    check(m.transforms.size() == 4 && m.transforms[3].pre.e == 1.5 && m.transforms[3].color == .27 &&
          m.final_transform->color == before_add.final_transform->color &&
          m.transforms[0].chaos == before_add.transforms[0].chaos,"append model or implicit transitions");
    LegacyTransformEdits edit; edit.post = Affine{.4,0,0,.4,0,0};
    d.edit_transform(0,0,edit,true);
    check(d.render_model(0).final_transform->post.a == .4,"final affine edit");
    rejects([&]{ d.add_transform(0,added,true); },"already contains");
    d.remove_transform(0,0,true);
    check(!d.render_model(0).final_transform,"final removal");
    check(d.add_transform(0,added,true) == 0 && d.render_model(0).final_transform->pre.e == 1.5,"final addition");
    const auto good = d.bytes();
    rejects([&]{ d.move_transform(0,0,9); },"out of range");
    edit = {}; edit.weight = 2;
    rejects([&]{ d.edit_transform(0,0,edit,true); },"selection weight");
    check(d.bytes() == good,"failed structural operation changed bytes");

    auto trapped = LegacyDocument::parse("<flame><xform weight='1' linear='1' chaos='0 1'/>"
        "<xform weight='1' linear='1' chaos='0 1'/><color index='0' rgb='32 32 32'/></flame>");
    const auto trapped_before = trapped.bytes();
    rejects([&]{ trapped.remove_transform(0,1); },"no positive destination");
    check(trapped.bytes() == trapped_before,"disconnecting deletion was committed");
    auto single = LegacyDocument::parse("<flame>"+supported+"</flame>");
    rejects([&]{ single.remove_transform(0,0); },"must remain");
}

void test_producer_transform_edits() {
    for (const char* version : {"Apophysis 2.09", "Apophysis 7x", "Apophysis AV Phoenix Edition"}) {
        auto flame = demo_flame();
        flame.source_version = version;
        std::string xml = serialize_flame(flame);
        const bool classic = std::string_view(version) == "Apophysis 2.09";
        if (classic) xml = replaced(xml, "symmetry=", "color_speed=");
        auto document = LegacyDocument::parse(xml);
        LegacyTransformEdits edit;
        edit.color_speed = .125;
        edit.variations = std::vector<Term>{{Variation::bubble,.6},{Variation::julian,.4}};
        edit.julian_power = 3;
        document.edit_transform(0,0,edit);
        const auto t = document.render_model(0).transforms[0];
        check(t.color_speed == .125 && t.parameters.julian_power == 3 &&
              t.variations[0].id == Variation::bubble, "producer-specific transformed values");
        const auto field = classic ? "color_speed=\"0.75\"" : "symmetry=\"0.75\"";
        check(document.bytes().find(field) != std::string::npos, "producer color-speed alias");
        const auto original_palette = xml.substr(xml.find("  <color"));
        check(document.bytes().ends_with(original_palette), "producer palette changed in transform edit");
    }
    auto final_first = LegacyDocument::parse("<flame><finalxform linear='1'/>"
        "<xform weight='1' linear='1'/><xform weight='2' linear='1'/>"
        "<color index='0' rgb='128 128 128'/></flame>");
    check(final_first.render_model(0).transforms[0].color == 1, "fixture default parity");
    final_first.remove_transform(0,0,true);
    const auto model = final_first.render_model(0);
    check(model.transforms[0].color == 1 && model.transforms[1].color == 0, "final removal changed following colors");
}

void test_encoded_transform_edits() {
    const std::string src = "<?xml version='1.0' encoding='UTF-16'?><Flames>\r\n"
        "<flame name='Keep'><xform weight='1' linear='1'/>"
        "<color index='0' rgb='255 255 255'/></flame></Flames>";
    for (bool le : {true,false}) {
        const std::string bom = le ? "\xff\xfe" : "\xfe\xff";
        auto d = LegacyDocument::parse(bom+utf16(src,le));
        LegacyTransformEdits edit; edit.pre = Affine{1,0,0,1,.3,.5};
        d.edit_transform(0,0,edit);
        const std::string expected = replaced(src,"linear='1'/>","linear='1' coefs=\"1 0 0 1 0.3 0.5\"/>");
        check(d.bytes() == bom+utf16(expected,le),"UTF-16 transform patch changed encoding");
        d.duplicate_transform(0,0);
        check(d.bytes().starts_with(bom) && d.render_model(0).transforms.size() == 2,"UTF-16 clone");
        d.move_transform(0,1,0);
        d.remove_transform(0,1);
        check(d.render_model(0).transforms[0].pre.e == .3,"UTF-16 move or remove");
        Transform add; add.pre.e = -1;
        d.add_transform(0,add);
        check(d.render_model(0).transforms[1].pre.e == -1,"UTF-16 append");
    }
}
void same_palette(const Palette& actual, const Palette& expected) {
    for (std::size_t i = 0; i < actual.size(); ++i) {
        check(std::abs(actual[i].r - expected[i].r) < 1e-15 &&
              std::abs(actual[i].g - expected[i].g) < 1e-15 &&
              std::abs(actual[i].b - expected[i].b) < 1e-15, "edited palette values changed");
    }
}

void test_adjustment_fields() {
    auto flame = demo_flame();
    flame.source_version = "Apophysis 7x";
    std::string xml = serialize_flame(flame);
    xml = replaced(xml, "rotate=\"0\"", "angle='-0.25' rotate='14'");
    xml = replaced(xml, "oversample=", "supersample=");
    const std::string history = "<!-- keep adjustment note --><edit><flame angle='0.1' gamma='8'/></edit>";
    xml = replaced(xml, "</flame>", history+"</flame>");
    auto standalone = LegacyDocument::parse(xml);
    const std::string entry(standalone.element_bytes(0));
    const std::string sibling = "<flame name='Archive' vendor='1'><xform future='1'/></flame>";
    auto doc = LegacyDocument::parse("<Flames>"+entry+sibling+"</Flames>");
    const auto original = doc.bytes();
    auto model = doc.render_model(0);
    LegacyEdits no_change;
    no_change.zoom = model.zoom; no_change.rotate = model.rotate;
    no_change.gamma = model.gamma; no_change.gamma_threshold = model.gamma_threshold;
    no_change.brightness = model.brightness; no_change.vibrancy = model.vibrancy;
    no_change.quality = model.quality; no_change.filter_radius = model.filter_radius;
    no_change.oversample = model.oversample; no_change.background = model.background;
    no_change.palette_mode = model.palette_mode;
    doc.edit(0, no_change);
    check(doc.bytes() == original, "same-value adjustments changed original source");

    LegacyEdits change;
    change.zoom = 1.25; change.rotate = 90; change.gamma = 2.5; change.gamma_threshold = .025;
    change.brightness = 1.25; change.vibrancy = .75; change.quality = 25;
    change.filter_radius = .8; change.oversample = 3; change.background = RGB{.25,.5,.75};
    change.palette_mode = PaletteMode::step;
    doc.edit(0, change);
    model = doc.render_model(0);
    check(model.zoom == 1.25 && std::abs(model.rotate - 90) < 1e-12 && model.gamma == 2.5 &&
          model.gamma_threshold == .025 && model.brightness == 1.25 && model.vibrancy == .75 &&
          model.quality == 25 && model.filter_radius == .8 && model.oversample == 3 &&
          model.palette_mode == PaletteMode::step, "adjustment values or producer aliases did not survive");
    check(model.background.r == 63.0/255 && model.background.g == 127.0/255 &&
          model.background.b == 191.0/255, "Apophysis background quantization changed");
    check(doc.bytes().find("supersample=\"3\"") != std::string::npos &&
          doc.bytes().find("oversample=") == std::string::npos &&
          doc.bytes().find("rotate='90'") != std::string::npos &&
          doc.bytes().find("angle='-1.5707963267948966'") != std::string::npos,
          "historical camera and sampling aliases were not retained");
    check(doc.element_bytes(1) == sibling && doc.bytes().find(history) != std::string::npos,
          "adjustments changed sibling or historical markup");
    change = {}; change.clear_filter_radius = true;
    doc.edit(0, change);
    check(doc.render_model(0).filter_radius == .5 && doc.entries()[0].attributes.count("filter") == 0,
          "clearing Apophysis filter should restore its producer default");

    auto generic = LegacyDocument::parse("<flame filter='1' filter_shape='gaussian' angle='0.3' "
                                         "supersample='1'>"+supported+"</flame>");
    change = {}; change.clear_filter_radius = true; change.rotate = 180; change.oversample = 2;
    generic.edit(0,change);
    check(!generic.render_model(0).filter_radius &&
          std::abs(generic.render_model(0).rotate - 180) < 1e-12 &&
          generic.bytes().find("rotate=") == std::string::npos &&
          generic.bytes().find("filter_shape") == std::string::npos,
          "generic angle alias or filter reset");
    auto repaired = LegacyDocument::parse("<flame rotate='0' angle='1'>"+supported+"</flame>");
    change = {}; change.rotate = 45; repaired.edit(0,change);
    check(repaired.render_model(0).rotate == 45 && repaired.bytes().find("angle=") == std::string::npos,
          "explicit rotation should repair ambiguous generic aliases");

    const auto good = doc.bytes();
    const auto rejected = [&](LegacyEdits invalid, Text diagnostic) {
        rejects([&]{ doc.edit(0,invalid); },diagnostic);
        check(doc.bytes() == good,"rejected adjustment changed source bytes");
    };
    change = {}; change.gamma = 0; rejected(change,"positive");
    change = {}; change.quality = 0; rejected(change,"positive");
    change = {}; change.brightness = -1; rejected(change,"nonnegative");
    change = {}; change.gamma_threshold = -1; rejected(change,"nonnegative");
    change = {}; change.vibrancy = 1.01; rejected(change,"[0,1]");
    change = {}; change.oversample = 0; rejected(change,"positive");
    change = {}; change.filter_radius = std::numeric_limits<double>::infinity(); rejected(change,"finite");
    change = {}; change.filter_radius = -.1; rejected(change,"nonnegative");
    change = {}; change.filter_radius = .2; change.clear_filter_radius = true; rejected(change,"together");
    change = {}; change.background = RGB{1,0,std::numeric_limits<double>::quiet_NaN()}; rejected(change,"[0,1]");
    change = {}; change.zoom = 1e6; rejected(change,"density");
    change = {}; change.palette_mode = static_cast<PaletteMode>(7); rejected(change,"palette mode");
    change = {}; change.gamma = 2.75; doc.edit(1,change);
    check(doc.entries()[1].attributes.at("gamma") == "2.75" &&
          doc.entries()[1].attributes.at("vendor") == "1", "preservation-only tone edit");
}

void test_palette_edits() {
    Palette palette = default_palette();
    for (std::size_t i = 0; i < palette.size(); ++i)
        palette[i] = {double(i)/255, .123456789012345, double(255-i)/255};
    const std::string sibling = "<flame name='Unchanged' future='1'><palette unknown='yes'/></flame>";
    const std::string history = "<edit><flame><palette count='2'>historical data</palette></flame></edit>";
    std::string packed;
    for (int i = 0; i < 256; ++i) packed += "112233";
    packed.insert(60,"<!-- palette note --><?palette source?>");
    const std::string entry = "<flame name='Palette'><xform weight='1' linear='1'/>"+history+
        "<palette count='256' format='RGB'>"+packed+"</palette></flame>";
    auto doc = LegacyDocument::parse("<flames>"+entry+sibling+"</flames>");
    const auto original = doc.bytes();
    doc.edit_palette(0,doc.render_model(0).palette);
    check(doc.bytes() == original,"same palette edit should retain original payload");
    doc.edit_palette(0,palette);
    same_palette(doc.render_model(0).palette,palette);
    check(doc.element_bytes(1) == sibling && doc.bytes().find(history) != std::string::npos &&
          doc.bytes().find("<!-- palette note --><?palette source?>") != std::string::npos,
          "palette edit lost comments, processing instruction, history or siblings");
    const auto after = doc.bytes();
    auto invalid = palette; invalid[23].b = std::numeric_limits<double>::quiet_NaN();
    rejects([&]{ doc.edit_palette(0,invalid); },"[0,1]");
    check(doc.bytes() == after,"invalid palette committed");

    for (const auto& payload : {std::string("<color index='0' rgb='32 64 128'/><!-- stop note -->"),
            std::string("<color index='0' rgba='32 64 128 255'/><color index='128' rgb='255 255 255'/>")}) {
        auto stops = LegacyDocument::parse("<flame>"+payload+supported.substr(0,supported.find("<color"))+"</flame>");
        stops.edit_palette(0,palette);
        same_palette(stops.render_model(0).palette,palette);
    }
    std::string argb;
    for (int i = 0; i < 256; ++i) argb += "FF112233";
    auto old_colors = LegacyDocument::parse("<flame><xform weight='1' linear='1'/><colors count='256' data='"+argb+"'/></flame>");
    old_colors.edit_palette(0,palette);
    same_palette(old_colors.render_model(0).palette,palette);
    for (const auto* reference : {"17","named-gradient"}) {
        auto catalog = LegacyDocument::parse("<flame palette='"+std::string(reference)+"'><xform weight='1' linear='1'/></flame>");
        catalog.edit_palette(0,palette);
        same_palette(catalog.render_model(0).palette,palette);
        check(catalog.entries()[0].attributes.at("palette") == "-1", "palette reference was not made explicit");
    }
    auto empty = LegacyDocument::parse("<flame name='Incomplete' />");
    empty.edit_palette(0,palette);
    check(empty.bytes().find("<flame name='Incomplete' >") == 0 && empty.bytes().ends_with("</flame>"),
          "palette insertion in a self-closing preserved flame");
}

void test_library_operations() {
    const std::string body = "<flame name='Parent' unknown='kept'>"+supported+
        "<edit><flame name='history'/></edit></flame>";
    const std::string prefix = "\xef\xbb\xbf<?xml version='1.0'?>\r\n<!-- before -->";
    const std::string suffix = "<!-- after -->\r\n";
    auto doc = LegacyDocument::parse(prefix+body+suffix);
    check(doc.duplicate_flame(0,"Caf\xc3\xa9 & copy") == 1,"duplicate flame index");
    check(doc.element_bytes(0) == body && doc.entries()[1].name == "Caf\xc3\xa9 & copy" &&
          doc.bytes().starts_with(prefix+"<flames>\r\n") && doc.bytes().ends_with("</flames>"+suffix),
          "standalone wrapping changed prolog, original entry or trailing comments");
    check(doc.element_bytes(1) == replaced(body,"name='Parent'","name='Caf&#233; &amp; copy'"),
          "duplication rewrote unrequested source fields");
    auto generated = demo_flame(); generated.name = "\xe9\x9b\xaa snow";
    check(doc.append_flame(generated) == 2 && doc.render_model(2).name == generated.name,
          "generated flame append or Unicode name");
    doc.remove_flame(1);
    check(doc.entries().size() == 2 && doc.element_bytes(0) == body && doc.entries()[1].name == generated.name,
          "flame deletion changed retained entries");
    doc.remove_flame(0);
    const auto good = doc.bytes();
    rejects([&]{ doc.remove_flame(0); },"must remain");
    generated.gamma = 0;
    rejects([&]{ doc.append_flame(generated); },"positive");
    rejects([&]{ doc.duplicate_flame(0,std::string("bad\0name",8)); },"XML");
    check(doc.bytes() == good,"rejected library edit changed source");
    auto restored = LegacyDocument::parse(prefix+body+suffix);
    check(restored.bytes() == prefix+body+suffix,"source snapshot restore changed original bytes");

    for (const auto* wrapper : {"flames","Flames","pick","test"}) {
        const std::string before = "<"+std::string(wrapper)+" name='kept'><!-- leading -->"+body+"<!-- trailing -->";
        const std::string end = "</"+std::string(wrapper)+">";
        auto collection = LegacyDocument::parse(before+end);
        collection.duplicate_flame(0,"Copied");
        check(collection.bytes().starts_with(before) && collection.bytes().ends_with(end) &&
              collection.element_bytes(0) == body, "existing collection wrapper changed");
    }
}

void test_encoded_creative_edits() {
    const std::string source = "<?xml version='1.0' encoding='UTF-16'?><flame name='Old'>"+supported+
        "<!-- encoding note --></flame>";
    const auto palette = default_palette();
    for (bool le : {false,true}) {
        const std::string bom = le ? "\xff\xfe" : "\xfe\xff";
        auto doc = LegacyDocument::parse(bom+utf16(source,le));
        LegacyEdits edit; edit.gamma = 2.2; edit.rotate = 45; edit.oversample = 2;
        doc.edit(0,edit); doc.edit_palette(0,palette);
        const std::string parent(doc.element_bytes(0));
        doc.duplicate_flame(0,"\xe9\x9b\xaa");
        auto generated = demo_flame(); generated.name = "\xf0\x9f\x8c\x8c";
        doc.append_flame(generated);
        same_palette(doc.render_model(0).palette,palette);
        check(doc.bytes().starts_with(bom+utf16("<?xml version='1.0' encoding='UTF-16'?>",le)) &&
              doc.element_bytes(0) == parent && doc.entries()[1].name == "\xe9\x9b\xaa" &&
              doc.render_model(2).name == generated.name &&
              doc.bytes().find(utf16("<!-- encoding note -->",le)) != std::string::npos,
              "UTF-16 creative edit changed encoding, Unicode or original source");
        doc.remove_flame(1);
        check(doc.entries().size() == 2 && doc.render_model(1).name == generated.name,"UTF-16 library remove");
    }
    for (const auto* encoding : {"ISO-8859-1", "windows-1252", "US-ASCII"}) {
        auto doc = LegacyDocument::parse("<?xml version='1.0' encoding='"+std::string(encoding)+"'?>"
            "<flame name='Original'>"+supported+"</flame>");
        auto generated = demo_flame(); generated.name = "Caf\xc3\xa9 \xe9\x9b\xaa";
        doc.append_flame(generated); doc.edit_palette(0,palette);
        check(doc.render_model(1).name == generated.name && doc.bytes().find("&#233;") != std::string::npos,
              "single-byte append failed to escape Unicode metadata");
        same_palette(doc.render_model(0).palette,palette);
    }
}

}
int main() {
    try { test_new_variation_fields(); test_archival_collection(); test_boundaries(); test_encodings();
          test_transform_fields(); test_transform_structure(); test_producer_transform_edits(); test_encoded_transform_edits();
          test_adjustment_fields(); test_palette_edits(); test_library_operations(); test_encoded_creative_edits(); }
    catch (const std::exception& e) { std::cerr << e.what() << "\n"; return 1; }
    std::cout << "Legacy document byte preservation, independent rendering, edits, encodings and rejections passed\n";
}
