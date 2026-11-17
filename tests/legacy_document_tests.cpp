#include "legacy_document.hpp"

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
}
int main() {
    try { test_archival_collection(); test_boundaries(); test_encodings(); }
    catch (const std::exception& e) { std::cerr << e.what() << "\n"; return 1; }
    std::cout << "Legacy document byte preservation, independent rendering, edits, encodings and rejections passed\n";
}
