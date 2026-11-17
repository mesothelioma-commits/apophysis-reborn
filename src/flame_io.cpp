#include "flame_io.hpp"

#include <expat.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <exception>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <memory>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <iterator>
#include <utility>

namespace apo {
namespace {

using Text = std::string_view;
using Attributes = std::map<Text, Text>;
constexpr Text whitespace = " \n\t\r";

[[noreturn]] void fail(std::string message) { throw std::runtime_error(std::move(message)); }

Text trim(Text s) {
    const auto first = s.find_first_not_of(whitespace);
    if (first == Text::npos) return {};
    return s.substr(first, s.find_last_not_of(whitespace) - first + 1);
}

template<typename T>
T number(Text token, Text field) {
    token = trim(token);
    if (token.starts_with('+')) {
        token.remove_prefix(1);
        if (token.starts_with('-') || token.starts_with('+')) token = {};
    }
    T value{};
    if (!token.empty()) {
        const char* end = token.data() + token.size();
        const auto result = std::from_chars(token.data(), end, value);
        if (result.ec == std::errc{} && result.ptr == end && std::isfinite(value)) return value;
    }
    fail("Invalid numeric value for '" + std::string(field) + "'");
}

template<typename T>
std::vector<T> numbers(Text s, Text field, std::size_t expected = 0) {
    std::vector<T> values;
    for (s = trim(s); !s.empty(); s = trim(s)) {
        const auto end = s.find_first_of(whitespace);
        values.push_back(number<T>(s.substr(0, end), field));
        if (end == Text::npos) break;
        s.remove_prefix(end);
    }
    if (values.empty() || (expected && values.size() != expected)) {
        fail("Wrong number of values for '" + std::string(field) + "'");
    }
    return values;
}

Text required(const Attributes& attributes, Text name) {
    const auto it = attributes.find(name);
    if (it == attributes.end()) fail("Missing attribute '" + std::string(name) + "'");
    return it->second;
}

void only(const Attributes& attributes, std::initializer_list<Text> names, Text element) {
    for (const auto& [key, value] : attributes) {
        if (std::find(names.begin(), names.end(), key) == names.end()) {
            fail("Unsupported attribute '" + std::string(key) + "' on <" + std::string(element) + ">");
        }
    }
}

Affine affine(Text s, Text field) {
    const auto v = numbers<double>(s, field, 6);
    return {v[0], v[1], v[2], v[3], v[4], v[5]};
}

bool neutral_curves(Text value) {
    // Four channels, each with four (x,y,weight) points, from ControlPoint.Create.
    constexpr std::array<double,12> identity{0,0,1, 0,0,1, 1,1,1, 1,1,1};
    const auto points = numbers<double>(value, "curves", 48);
    for (std::size_t i = 0; i < points.size(); ++i)
        if (points[i] != identity[i % identity.size()]) return false;
    return true;
}

void flame_settings(Flame& flame, const Attributes& attributes) {
    // FLAM3 clear_cp(..., defaults_on) defaults for the supported XML settings.
    flame.width = flame.height = 100;
    if (attributes.contains("version")) flame.source_version = required(attributes, "version");
    const bool apo = is_apophysis(flame.source_version);
    flame.gamma = apo ? 1 : 4;
    flame.quality = apo ? 50 : 1;
    if (apo) {
        flame.brightness = 1;
        flame.filter_radius = .5;
        flame.name = "untitled";
    }
    const bool de_off = apo && attributes.contains("enable_de") && number<int>(required(attributes,"enable_de"), "enable_de") == 0;
    const bool radius_zero = attributes.contains("estimator_radius") &&
        number<double>(required(attributes,"estimator_radius"), "estimator_radius") == 0;

    for (const auto& [key, value] : attributes) {
        if (key == "name") flame.name = value;
        else if (key == "version") flame.source_version = value;
        else if (key == "time") flame.time = number<double>(value, key);
        else if (key == "scale") flame.scale = number<double>(value, key);
        else if (key == "zoom") flame.zoom = number<double>(value, key);
        else if (key == "rotate") {
            const double degrees = number<double>(value, key);
            if (!apo || !attributes.contains("angle")) flame.rotate = degrees;
        } else if (key == "angle") {
            if (!apo && attributes.contains("rotate")) fail("Ambiguous angle/rotate for an unidentified producer");
            flame.rotate = -number<double>(value, key) * (180.0 / std::numbers::pi);
        } else if (key == "filter") flame.filter_radius = number<double>(value,key);
        else if (key == "filter_shape") {
            if (value != "gaussian") fail("Unsupported filter_shape '" + std::string(value) + "'");
            if (!attributes.contains("filter")) flame.filter_radius = .5;
        } else if (key == "gamma_threshold") flame.gamma_threshold = number<double>(value,key);
        else if (key == "enable_de") {
            if (!de_off) fail("Active density estimation (enable_de) is unsupported");
        } else if (key == "estimator_radius" || key == "estimator_minimum" || key == "estimator_curve") {
            (void)number<double>(value, key);
            if (!de_off && !radius_zero) fail("Density estimation requires disabled enable_de or estimator_radius=0");
        } else if (key == "curves" && apo) {
            if (!neutral_curves(value)) fail("Non-default color curves are unsupported");
        } else if (key == "new_linear" && apo) {
            if (number<int>(value,key) != 1) fail("Unsupported new_linear conversion mode");
        } else if ((key == "plugins" && apo) || key == "nick" || key == "url") {
            // Producer-exported plugin inventory and author metadata. Each actual operation is checked below.
        } else if (key == "batches" || key == "temporal_samples") {
            if (number<int>(value, key) != 1) fail("Unsupported multi-batch or temporal sampling: " + std::string(key));
        } else if (key == "cam_pitch" || key == "cam_yaw" || key == "cam_perspective" ||
                   key == "cam_zpos" || key == "cam_dof") {
            if (number<double>(value, key) != 0) fail("Unsupported 3D camera field '" + std::string(key) + "'");
        }
        else if (key == "gamma") flame.gamma = number<double>(value, key);
        else if (key == "quality") flame.quality = number<double>(value, key);
        else if (key == "brightness") flame.brightness = number<double>(value, key);
        else if (key == "vibrancy") flame.vibrancy = number<double>(value, key);
        else if (key == "palette_mode") {
            if (value == "step") flame.palette_mode = PaletteMode::step;
            else if (value == "linear") flame.palette_mode = PaletteMode::linear;
            else fail("Unsupported palette_mode '" + std::string(value) + "'");
        } else if (key == "oversample" || key == "supersample") {
            if (attributes.contains("oversample") && attributes.contains("supersample"))
                fail("Specify one of oversample or supersample");
            flame.oversample = number<int>(value, key);
        } else if (key == "size") {
            const auto v = numbers<int>(value, key, 2);
            flame.width = v[0];
            flame.height = v[1];
        } else if (key == "center") {
            const auto v = numbers<double>(value, key, 2);
            flame.center = {v[0], v[1]};
        } else if (key == "background") {
            const auto v = numbers<double>(value, key, 3);
            const auto channel = [&](double x) {
                if (x < 0 || x > 1) fail("background channels must be in [0,1]");
                return apo ? std::floor(255*x)/255 : x;
            };
            flame.background = {channel(v[0]), channel(v[1]), channel(v[2])};
        } else if (key == "palette") {
            if (number<int>(value, key) != -1)
                fail("Indexed palette catalogs are unsupported; supply an embedded RGB palette");
        } else {
            fail("Unsupported attribute '" + std::string(key) + "' on <flame>");
        }
    }
}

Variation legacy_variation_id(int id, Text version) {
    // After the first 23 IDs, Apophysis 2 and FLAM3 registries diverge. 7X/AV
    // inserted/reordered built-ins; plugin-dependent indices cannot be inferred from a name tag.
    if (is_apophysis(version) && identify_producer(version) != Producer::classic)
        fail("Numbered variation needs the original 3D-hack/7X/AV registry; use named variations");
    if (is_apophysis_2(version)) {
        if (id == 23) return Variation::eyefish;
        if (id == 24) return Variation::bubble;
        if (id == 25) return Variation::cylinder;
        if (id == 26) return Variation::noise;
        if (id == 27) return Variation::blur;
        if (id == 29) return Variation::pre_blur;
    }
    if (id >= 0 && (!is_apophysis_2(version) || id <= 22))
        for (const auto& entry : variation_registry())
            if (static_cast<unsigned>(entry.id) == static_cast<unsigned>(id)) return entry.id;
    fail("Unsupported variation ID " + std::to_string(id));
}

Transform read_transform(const Attributes& attributes, bool final, std::size_t index, Text version) {
    const bool apo = is_apophysis(version);
    Transform t;
    t.variations.clear();
    t.weight = final ? 1 : 0;
    t.color = apo ? 0 : static_cast<double>(index % 2);
    t.parameters.apophysis_math = apo;
    t.parameters.separate_blur_angle = identify_producer(version) == Producer::seven;
    if (attributes.contains("var") && attributes.contains("var1")) fail("Cannot mix var and var1");
    if (attributes.contains("julian_power")) {
        const double power = number<double>(required(attributes,"julian_power"), "julian_power");
        if (power != std::trunc(power) || power == 0 || std::abs(power) > std::numeric_limits<int>::max())
            fail("Unsupported julian_power; use a nonzero integer within int range");
        t.parameters.julian_power = static_cast<int>(power);
    }
    for (const auto& [key, value] : attributes) {
        if (key == "weight") {
            const auto weight = number<double>(value, key);
            if (final && weight != 0) fail("A finalxform cannot have selection weight");
            if (!final) t.weight = weight;
        } else if (key == "color") t.color = number<double>(value, key);
        else if (key == "opacity") t.opacity = number<double>(value, key);
        else if (key == "plotmode") {
            if (value != "on" && value != "off") fail("Unsupported plotmode '" + std::string(value) + "'");
            if (attributes.contains("opacity")) fail("Specify one of opacity or plotmode");
            t.opacity = value == "on" ? 1 : 0;
        } else if (key == "name") { /* editor metadata */ }
        else if (key == "enabled" && final && apo) {
            const auto enabled = number<int>(value, key);
            if (enabled != 0 && enabled != 1) fail("enabled must be zero or one");
        } else if (key == "julian_power") { /* parsed before the weights */ }
        else if (key == "julian_dist") t.parameters.julian_dist = number<double>(value, key);
        else if (key == "linear3D" && apo && identify_producer(version) != Producer::classic) {
            if (attributes.contains("linear") || attributes.contains("var") || attributes.contains("var1"))
                fail("Cannot mix linear3D with linear or numeric variation weights");
            // The original 3D-hack term contributes (w*x,w*y,w*z). The reader
            // rejects nonzero 3D projection and other operations coupling Z into XY.
            t.variations.push_back({Variation::linear, number<double>(value, key)});
        }
        else if (key == "flatten" && (identify_producer(version) == Producer::seven || identify_producer(version) == Producer::av)) {
            // In the verified 2D path every Z input is zero; flatten only assigns Z.
            (void)number<double>(value,key);
        } else if (key == "var_order" && apo && !is_apophysis_2(version)) {
            // Apply the recorded order after all named weights are available.
        }
        else if (key == "sinusoidal_use3D" && apo && attributes.contains("sinusoidal")) {
            if (number<int>(value,key) != 0) fail("3D sinusoidal is unsupported");
        }
        else if (key == "coefs") t.pre = affine(value, key);
        else if (key == "post") t.post = affine(value, key);
        else if (key == "color_speed") {
            const double speed = number<double>(value, key);
            if (apo && identify_producer(version) != Producer::classic) fail("Use symmetry for this Apophysis color speed");
            t.color_speed = apo ? (1 - speed)/2 : speed;
        }
        else if (key == "symmetry") {
            if (attributes.contains("color_speed")) fail("Specify one of symmetry or color_speed");
            t.color_speed = (1.0 - number<double>(value, key)) / 2.0;
        } else if (key == "chaos") {
            if (final) fail("A finalxform cannot have a chaos row");
            t.chaos = numbers<double>(value, key);
        } else if (key == "var1") {
            t.variations.push_back({legacy_variation_id(number<int>(value,key), version), 1});
        } else if (key == "var") {
            const auto values = numbers<double>(value,key);
            for (std::size_t i = 0; i < values.size(); ++i) {
                if (values[i] == 0) continue;
                if (i > static_cast<std::size_t>(std::numeric_limits<int>::max())) fail("Variation index overflow");
                t.variations.push_back({legacy_variation_id(static_cast<int>(i), version), values[i]});
            }
        } else if (const auto id = find_variation(key)) {
            if (attributes.contains("var1") || attributes.contains("var"))
                fail("Cannot mix numbered and named variation weights");
            t.variations.push_back({*id, number<double>(value, key)});
        } else {
            fail("Unsupported transform attribute or variation '" + std::string(key) + "'");
        }
    }
    const bool julian_active = std::any_of(t.variations.begin(), t.variations.end(),
        [](Term term){ return term.id == Variation::julian && term.weight != 0; });
    if (julian_active && apo && !attributes.contains("julian_power"))
        fail("Apophysis JuliaN requires explicit julian_power (producer default is random)");
    if (attributes.contains("var_order")) {
        std::vector<Term> ordered;
        std::istringstream names(std::string{required(attributes, "var_order")});
        std::string name;
        while (names >> name) {
            if (name == "flatten" && attributes.contains("flatten")) continue; // Z-only pre stage
            const auto id = find_variation(name);
            const auto term = std::find_if(t.variations.begin(), t.variations.end(),
                [&](Term item){ return id && item.id == *id; });
            if (term == t.variations.end()) fail("Unmatched var_order entry '" + name + "'");
            ordered.push_back(*term);
        }
        if (ordered.size() != t.variations.size()) fail("var_order must cover every named variation");
        t.variations = std::move(ordered);
    } else {
        std::sort(t.variations.begin(), t.variations.end(),
                  [](const Term& a, const Term& b) { return a.id < b.id; });
    }
    return t;
}

struct PaletteReader {
    std::map<unsigned, RGB> stops;
    std::string packed;
    unsigned channels = 0;
    bool alpha_first = false;

    void color(const Attributes& attributes) {
        only(attributes, {"index", "rgb", "rgba"}, "color");
        if (channels) fail("Cannot mix color stops and a packed palette");
        if (attributes.contains("rgb") == attributes.contains("rgba"))
            fail("A color needs exactly one rgb or rgba value");
        const unsigned index = number<unsigned>(required(attributes, "index"), "index");
        if (index > 255) fail("Color index must be between 0 and 255");
        const bool rgba = attributes.contains("rgba");
        const Text key = rgba ? "rgba" : "rgb";
        const auto v = numbers<double>(required(attributes, key), key, rgba ? 4 : 3);
        if (rgba && v[3] != 255) fail("Transparent palette entries are unsupported");
        if (std::any_of(v.begin(), v.end(), [](double n) { return n < 0 || n > 255; }))
            fail("Palette components must be between 0 and 255");
        if (!stops.emplace(index, RGB{v[0]/255, v[1]/255, v[2]/255}).second)
            fail("Duplicate palette color index " + std::to_string(index));
    }

    void begin_packed(const Attributes& attributes) {
        only(attributes, {"count", "format"}, "palette");
        if (channels || !stops.empty()) fail("More than one palette definition");
        if (number<int>(required(attributes, "count"), "count") != 256)
            fail("Packed palette requires exactly 256 entries");
        const Text format = required(attributes, "format");
        if (format == "RGB") channels = 3;
        else if (format == "RGBA") channels = 4;
        else fail("Unsupported palette format '" + std::string(format) + "'");
    }

    void append(Text data) {
        for (char c : data) {
            if (whitespace.find(c) != Text::npos) continue;
            if (packed.size() >= channels * 512u) fail("Extra data in packed palette");
            packed += c;
        }
    }

    void finish(Palette& result, bool apo) const {
        if (channels) {
            if (packed.size() != channels * 512u) fail("Incorrect packed palette length");
            const char* cursor = packed.data();
            for (auto& color : result) {
                std::array<unsigned, 4> values{0, 0, 0, 255};
                for (unsigned k = 0; k < channels; ++k, cursor += 2) {
                    auto p = std::from_chars(cursor, cursor + 2, values[k], 16);
                    if (p.ec != std::errc{} || p.ptr != cursor + 2) fail("Invalid hexadecimal palette");
                }
                if (channels == 4 && (apo || alpha_first)) {
                    color = {values[1]/255.0, values[2]/255.0, values[3]/255.0};
                    continue;
                }
                if (values[3] != 255) fail("Transparent palette entries are unsupported");
                color = {values[0]/255.0, values[1]/255.0, values[2]/255.0};
            }
            return;
        }
        if (stops.empty()) fail("Missing embedded palette; supply <color> entries or a packed <palette>");
        if (apo && stops.size() != 256)
            fail("Apophysis indexed palette requires all 256 entries; missing slots depend on editor state");
        // Fill every interval, including the last-to-first interval across index 255.
        for (auto left = stops.begin(); left != stops.end(); ++left) {
            auto right = std::next(left);
            if (right == stops.end()) right = stops.begin();
            unsigned end = right->first;
            if (end <= left->first) end += 256;
            for (unsigned i = left->first; i < end; ++i) {
                const double t = static_cast<double>(i-left->first) / (end-left->first);
                result[i % 256] = {std::lerp(left->second.r, right->second.r, t),
                                   std::lerp(left->second.g, right->second.g, t),
                                   std::lerp(left->second.b, right->second.b, t)};
            }
        }
    }
};

class Parser {
    using Handle = std::unique_ptr<XML_ParserStruct, decltype(&XML_ParserFree)>;
    Handle handle_{XML_ParserCreate(nullptr), XML_ParserFree};
    std::vector<std::string> stack_;
    std::exception_ptr callback_error_;
    std::vector<Flame> flames_;
    PaletteReader palette_;
    unsigned edit_depth_ = 0;
    bool final_seen_ = false;

    void start(Text name, const XML_Char** raw) {
        if (edit_depth_) { ++edit_depth_; return; }
        Attributes attributes;
        for (; *raw; raw += 2) attributes.emplace(raw[0], raw[1]);
        const Text parent = stack_.empty() ? Text{} : Text(stack_.back());
        const bool collection = parent == "flames" || parent == "Flames" || parent == "pick";
        if (parent == "flame" && name == "edit") { edit_depth_ = 1; return; }
        if ((name == "flames" || name == "Flames" || name == "pick") && parent.empty()) {
            // The optional wrapper name describes the external collection.
            only(attributes, {"name"}, name);
        } else if (name == "flame" && (parent.empty() || collection)) {
            flame_settings(flames_.emplace_back(), attributes);
            palette_ = {};
            final_seen_ = false;
        } else if (parent == "flame" && (name == "xform" || name == "finalxform")) {
            auto& f = flames_.back();
            const bool final = name == "finalxform";
            // FLAM3 assigns color parity before moving an existing final xform to the end.
            const auto input_index = f.transforms.size() + static_cast<std::size_t>(final_seen_);
            auto t = read_transform(attributes, final, input_index, f.source_version);
            if (!final) f.transforms.push_back(std::move(t));
            else {
                if (final_seen_) fail("More than one finalxform");
                final_seen_ = true;
                if (!attributes.contains("enabled") || number<int>(required(attributes,"enabled"),"enabled") == 1)
                    f.final_transform = std::move(t);
            }
        } else if (parent == "flame" && name == "color") palette_.color(attributes);
        else if (parent == "flame" && name == "colors") {
            only(attributes, {"count","data"}, name);
            palette_.begin_packed({{"count",required(attributes,"count")},{"format","RGBA"}});
            palette_.alpha_first = true;
            palette_.append(required(attributes,"data"));
        }
        else if (parent == "flame" && name == "palette") palette_.begin_packed(attributes);
        else fail("Unsupported or misplaced element <" + std::string(name) + ">");
        stack_.emplace_back(name);
    }

    void end(Text name) {
        if (edit_depth_) { --edit_depth_; return; }
        if (name == "flame") {
            auto& f = flames_.back();
            palette_.finish(f.palette, is_apophysis(f.source_version));
            validate(f);
        }
        stack_.pop_back();
    }

    void characters(Text value) {
        if (edit_depth_) return;
        if (!stack_.empty() && stack_.back() == "palette") palette_.append(value);
        else if (!trim(value).empty()) fail("Unexpected text outside a palette");
    }

    // Expat is a C library. Never propagate an exception through its callback frames.
    template<class Function>
    static void callback(void* data, Function body) noexcept {
        auto& p = *static_cast<Parser*>(data);
        if (p.callback_error_) return;
        try { body(p); }
        catch (...) {
            p.callback_error_ = std::current_exception();
            XML_StopParser(p.handle_.get(), XML_FALSE);
        }
    }

public:
    Parser() {
        if (!handle_) throw std::bad_alloc();
        auto parser = handle_.get();
        XML_SetUserData(parser, this);
        XML_SetElementHandler(parser,
            [](void* d, const XML_Char* n, const XML_Char** a) {
                callback(d, [&](Parser& p) { p.start(n, a); });
            },
            [](void* d, const XML_Char* n) {
                callback(d, [&](Parser& p) { p.end(n); });
            });
        XML_SetCharacterDataHandler(parser, [](void* d, const XML_Char* s, int n) {
            callback(d, [&](Parser& p) { p.characters({s, static_cast<std::size_t>(n)}); });
        });
        XML_SetStartDoctypeDeclHandler(parser,
            [](void* d, const XML_Char*, const XML_Char*, const XML_Char*, int) {
                callback(d, [](Parser&) { fail("DOCTYPE declarations are unsupported"); });
            });
    }

    void feed(const char* bytes, int count, bool last) {
        if (XML_Parse(handle_.get(), bytes, count, last) == XML_STATUS_OK) return;
        std::string reason = XML_ErrorString(XML_GetErrorCode(handle_.get()));
        if (callback_error_) {
            try { std::rethrow_exception(callback_error_); }
            catch (const std::exception& e) { reason = e.what(); }
        }
#if XML_MAJOR_VERSION > 2 || (XML_MAJOR_VERSION == 2 && XML_MINOR_VERSION >= 9)
        const auto line = XML_GetCurrentLineNumber64(handle_.get());
        const auto column = XML_GetCurrentColumnNumber64(handle_.get());
#else
        const auto line = XML_GetCurrentLineNumber(handle_.get());
        const auto column = XML_GetCurrentColumnNumber(handle_.get());
#endif
        fail("XML line " + std::to_string(line) + ", column " + std::to_string(column + 1) + ": " + reason);
    }

    std::vector<Flame> finish() {
        feed("", 0, true);
        if (flames_.empty()) fail("The document contains no flames");
        return std::move(flames_);
    }
};

std::string escaped(Text input) {
    std::string output;
    for (unsigned char c : input) {
        switch (c) {
        case '&': output += "&amp;"; break;
        case '"': output += "&quot;"; break;
        case '<': output += "&lt;"; break;
        case '>': output += "&gt;"; break;
        case '\r': output += "&#13;"; break;
        case '\n': output += "&#10;"; break;
        case '\t': output += "&#9;"; break;
        default:
            if (c < 32) fail("Unsupported XML control character in flame metadata");
            output += static_cast<char>(c);
        }
    }
    return output;
}

void write_affine(std::ostream& out, Text field, Affine m) {
    out << ' ' << field << "=\"" << m.a << ' ' << m.b << ' ' << m.c << ' ' << m.d
        << ' ' << m.e << ' ' << m.f << '"';
}

void write_transform(std::ostream& out, const Transform& t, bool final, bool apo) {
    out << (final ? "  <finalxform" : "  <xform");
    if (!final) out << " weight=\"" << t.weight << '"';
    out << " color=\"" << t.color << '"';
    if (apo) out << " symmetry=\"" << (1 - 2 * t.color_speed) << '"';
    else out << " color_speed=\"" << t.color_speed << '"';
    out << " opacity=\"" << t.opacity << '"';
    write_affine(out, "coefs", t.pre);
    write_affine(out, "post", t.post);
    if (!t.chaos.empty()) {
        out << " chaos=\"";
        for (std::size_t i = 0; i < t.chaos.size(); ++i) {
            if (i) out << ' ';
            out << t.chaos[i];
        }
        out << '"';
    }
    for (const auto& term : t.variations) {
        out << ' ' << variation_name(term.id)
            << "=\"" << term.weight << '"';
    }
    if (apo && !std::is_sorted(t.variations.begin(), t.variations.end(), [](Term a, Term b){ return a.id < b.id; })) {
        out << " var_order=\"";
        for (const auto& term : t.variations) out << variation_name(term.id) << ' ';
        out << '"';
    }
    if (std::any_of(t.variations.begin(), t.variations.end(), [](Term term){ return term.id == Variation::julian; }))
        out << " julian_power=\"" << t.parameters.julian_power << "\" julian_dist=\"" << t.parameters.julian_dist << '"';
    out << "/>\n";
}

} // namespace

std::vector<Flame> parse_flames(std::string_view xml) {
    Parser p;
    while (!xml.empty()) {
        auto size = std::min<std::size_t>(xml.size(), 32768);
        p.feed(xml.data(), static_cast<int>(size), false);
        xml.remove_prefix(size);
    }
    return p.finish();
}

std::vector<Flame> load_flames(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) fail("Cannot open flame file: " + path.string());
    try {
        Parser p;
        std::array<char, 32768> bytes;
        while (in.read(bytes.data(), bytes.size()) || in.gcount() != 0)
            p.feed(bytes.data(), static_cast<int>(in.gcount()), false);
        if (!in.eof() || in.bad()) fail("Input read failed");
        return p.finish();
    } catch (const std::runtime_error& e) {
        fail(path.string() + ": " + e.what());
    }
}

std::string serialize_flame(const Flame& f) {
    validate(f);
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<double>::max_digits10);
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out << "<flame name=\"" << escaped(f.name) << '"';
    if (!f.source_version.empty()) out << " version=\"" << escaped(f.source_version) << '"';
    if (f.filter_radius) out << " filter=\"" << *f.filter_radius << '"';
    if (f.gamma_threshold != 0) out << " gamma_threshold=\"" << f.gamma_threshold << '"';
    if (is_apophysis(f.source_version) && !is_apophysis_2(f.source_version)) out << " new_linear=\"1\"";
    out << " time=\"" << f.time << "\" size=\"" << f.width << ' ' << f.height
        << "\" center=\"" << f.center.x << ' ' << f.center.y << "\" scale=\"" << f.scale
        << "\" rotate=\"" << f.rotate << "\" zoom=\"" << f.zoom << "\" quality=\"" << f.quality
        << "\" palette_mode=\"" << (f.palette_mode == PaletteMode::linear ? "linear" : "step")
        << "\" oversample=\"" << f.oversample << "\" brightness=\"" << f.brightness
        << "\" gamma=\"" << f.gamma << "\" vibrancy=\"" << f.vibrancy << "\" background=\""
        << f.background.r << ' ' << f.background.g << ' ' << f.background.b << "\">\n";
    for (const auto& t : f.transforms) write_transform(out, t, false, is_apophysis(f.source_version));
    if (f.final_transform) write_transform(out, *f.final_transform, true, is_apophysis(f.source_version));
    // RGB decimal stops retain sub-byte channels from interpolated or generated palettes.
    for (std::size_t i = 0; i < f.palette.size(); ++i) {
        const auto& color = f.palette[i];
        out << "  <color index=\"" << i << "\" rgb=\"" << color.r * 255 << ' '
            << color.g * 255 << ' ' << color.b * 255 << "\"/>\n";
    }
    out << "</flame>\n";
    return out.str();
}

} // namespace apo
