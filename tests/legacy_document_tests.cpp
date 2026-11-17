#include "legacy_document.hpp"
#include "flame_io.hpp"

#include <cmath>
#include <iostream>
#include <limits>
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
}
int main() {
    try { test_archival_collection(); test_boundaries(); test_encodings();
          test_transform_fields(); test_transform_structure(); test_producer_transform_edits(); test_encoded_transform_edits(); }
    catch (const std::exception& e) { std::cerr << e.what() << "\n"; return 1; }
    std::cout << "Legacy document byte preservation, independent rendering, edits, encodings and rejections passed\n";
}
