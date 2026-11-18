#include "animation.hpp"
#include "creative.hpp"
#include "flame_io.hpp"
#include "plugin.hpp"
#include "project_io.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <numbers>
#include <stop_token>

namespace {
using namespace apo;
namespace fs = std::filesystem;
void require(bool condition, std::string_view message) {
  if (!condition) throw std::runtime_error(std::string(message));
}
void near(double actual, double expected, std::string_view message, double tolerance = 1e-12) {
  if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance)
    throw std::runtime_error(std::string(message) + ": " + std::to_string(actual) +
                             " != " + std::to_string(expected));
}
template<class F> std::string rejects(F action, std::string_view expected = {}) {
  try { action(); }
  catch (const std::exception &error) {
    require(expected.empty() || std::string_view(error.what()).find(expected) != std::string_view::npos,
            std::string("Unexpected diagnostic: ") + error.what());
    return error.what();
  }
  throw std::runtime_error("Expected rejection: " + std::string(expected));
}
std::string replaced(std::string text, std::string_view before, std::string_view after) {
  const auto where = text.find(before);
  require(where != std::string::npos, "Missing test source replacement");
  text.replace(where, before.size(), after);
  return text;
}
Flame base() {
  Flame result;
  result.name = "Source";
  result.width = 32; result.height = 24; result.scale = 11;
  result.transforms = {Transform{}};
  result.palette.fill({1, 0, 0});
  return result;
}
LegacyDocument document(const Flame &flame) { return LegacyDocument::parse(serialize_flame(flame)); }
AnimationKeyframe key(const Flame &flame, std::uint32_t frame,
                      std::span<const std::string> ids = {}) {
  auto result = capture_animation_keyframe(document(flame), 0, frame, {}, ids);
  result.easing = AnimationEasing::linear;
  return result;
}
AnimationClip clip_for(const Flame &a, const Flame &b, std::uint32_t count = 5) {
  AnimationClip clip;
  clip.frame_count = count;
  clip.keyframes = {key(a, 0), key(b, count - 1)};
  return clip;
}
bool diagnostic(const PreparedAnimation &animation, std::string_view expected) {
  return std::any_of(animation.diagnostics().begin(), animation.diagnostics().end(),
      [&](const auto &text) { return text.find(expected) != std::string::npos; });
}
void exact(const Flame &actual, const Flame &expected, std::string_view message) {
  require(serialize_flame(actual) == serialize_flame(expected), message);
  require(actual.curves == expected.curves, "Endpoint curve model changed");
}
void io_and_source_tests() {
  const std::string supported = "<xform weight='1' linear='1'/><color index='0' rgb='255 0 0'/>";
  const std::string source = "\xef\xbb\xbf<?xml version='1.0'?>\r\n<?archive preserve?>"
      "<flames name='Library'><flame name='Future' vendor='opaque'>"
      "<vendor data='a &amp; b'/><![CDATA[raw <future/>]]></flame>"
      "<!-- between --> <flame name='Selected' size='032 024' scale='1.100e1'>" +
      supported + "<!-- inside --></flame></flames>\r\n";
  auto original = LegacyDocument::parse(source);
  AnimationClip clip;
  clip.name = "Orbit & \xe9\x9b\xaa \"name\"\n";
  clip.rate = {30000, 1001}; clip.frame_count = 31;
  clip.seed = std::numeric_limits<std::uint64_t>::max();
  clip.keyframes = {capture_animation_keyframe(original, 1, 0)};
  const auto payload = serialize_animation(clip);
  require(is_animation_payload(payload), "Known payload not recognized");
  require(is_animation_payload("<!-- preamble -->\n" + payload), "Comment preamble not recognized");
  require(!is_animation_payload("<apophysis-animation-other/>"), "Root prefix confused with known schema");
  const auto roundtrip = parse_animation(payload);
  require(roundtrip == clip && roundtrip.keyframes[0].library_bytes == source &&
          roundtrip.keyframes[0].selected_index == 1, "Animation source or selected entry changed");
  exact(PreparedAnimation(roundtrip).evaluate(7).flame, original.render_model(1), "Captured endpoint changed");
  require(original.bytes() == source, "Capturing a keyframe modified its source document");
  const auto orbit = make_animation_preset(original,1,AnimationPreset::camera_orbit,5);
  const auto orbit_end = LegacyDocument::parse(orbit.keyframes.back().library_bytes);
  require(orbit.keyframes.front().library_bytes == source && orbit_end.element_bytes(0) == original.element_bytes(0) &&
          orbit_end.bytes().find("<!-- between -->") != std::string::npos &&
          orbit_end.bytes().find("<!-- inside -->") != std::string::npos,
          "Preset capture discarded unknown siblings or source comments");

  ProjectFile project;
  project.library_bytes = source; project.selected_index = 1; project.animation_data = payload;
  const auto native = parse_project(serialize_project(project));
  require(native.animation_data == payload && native.library_bytes == source,
          "Native project changed the exact animation payload or legacy bytes");
  auto unknown = replaced(payload, "<apophysis-animation version=\"1\"", "<apophysis-animation version=\"9001\"");
  project.animation_data = unknown;
  auto preserved = parse_project(serialize_project(project));
  rejects([&] { parse_animation(*preserved.animation_data); }, "unsupported animation version");
  preserved.workspace["future-view"] = "retained";
  require(parse_project(serialize_project(preserved)).animation_data == unknown,
          "Editing project settings destroyed an unknown animation version");

  for (bool little : {false, true}) {
    const std::string ascii = "<?xml version='1.0' encoding='UTF-16'?><flames>"
        "<flame unsupported='opaque'/><flame name='Wide'>" + supported + "</flame></flames>";
    std::string wide = little ? "\xff\xfe" : "\xfe\xff";
    for (char c : ascii) { wide += little ? c : '\0'; wide += little ? '\0' : c; }
    auto wide_document = LegacyDocument::parse(wide);
    clip.keyframes = {capture_animation_keyframe(wide_document, 1, 0)};
    const auto parsed = parse_animation(serialize_animation(clip));
    require(parsed.keyframes[0].library_bytes == wide, "UTF-16 source BOM/bytes changed");
    exact(PreparedAnimation(parsed).evaluate(0).flame, wide_document.render_model(1), "UTF-16 keyframe model changed");
  }
}
void hostile_input_tests() {
  auto clip = clip_for(base(), base());
  const auto payload = serialize_animation(clip);
  rejects([&] { parse_animation(std::string(animation_payload_limit + 1, 'x')); }, "8 MiB");
  rejects([&] { parse_animation(""); }, "1 byte");
  rejects([&] { parse_animation("<!DOCTYPE apophysis-animation [<!ENTITY x 'expanded'>]>" +
      payload.substr(payload.find("<apophysis-animation"))); }, "DOCTYPE");
  rejects([&] { parse_animation(replaced(payload, "fps-num=\"30\"", "fps-num=\"0\"")); }, "frame rate");
  rejects([&] { parse_animation(replaced(payload, "seed=\"123456\"", "seed=\"18446744073709551616\"")); }, "integer seed");
  rejects([&] { parse_animation(replaced(payload, "loop=\"once\"", "loop=\"forever\"")); }, "loop");
  rejects([&] { parse_animation(replaced(payload, "name=\"Animation\"", "name=\"Animation\" future=\"true\"")); }, "unsupported attribute");
  rejects([&] { parse_animation(replaced(payload, "<transform id=", "<future id=")); }, "unsupported keyframe child");
  rejects([&] { parse_animation(replaced(payload, "selected=\"0\"", "selected=\"-1\"")); }, "integer selected");
  rejects([&] { parse_animation(replaced(payload, "selected=\"0\"", "selected=\"1\"")); }, "selected entry");
  rejects([&] { parse_animation(replaced(payload, "encoding=\"UTF-8\"", "encoding=\"UTF-16\"")); });
  rejects([&] { parse_animation(replaced(payload, "name=\"Animation\"", std::string("name=\"") + char(0xff) + "\"")); }, "invalid XML");
  const auto start = payload.find('>', payload.find("<snapshot")) + 1;
  const auto end = payload.find("</snapshot>", start);
  for (const auto bad : {"A", "AB==", "ABC=", "AA=A", "!!!!", "=AAA"}) {
    auto corrupted = payload; corrupted.replace(start, end - start, bad);
    rejects([&] { parse_animation(corrupted); }, "base64");
  }
  auto nested = replaced(payload, "<transform id=\"transform-1\"/>",
      "<transform id=\"transform-1\"><a><b><c/></b></a></transform>");
  rejects([&] { parse_animation(nested); }, "depth");
  auto missing = payload; missing.erase(missing.find("<snapshot"), end + 11 - missing.find("<snapshot"));
  rejects([&] { parse_animation(missing); }, "missing its snapshot");
  auto duplicate = payload; duplicate.insert(end + 11, payload.substr(payload.rfind("<snapshot", start), end + 11 - payload.rfind("<snapshot", start)));
  rejects([&] { parse_animation(duplicate); }, "duplicate keyframe snapshot");

  clip.keyframes[0].library_bytes = "<flame vendor='unknown'><xform future_variation='1'/></flame>";
  const auto opaque = parse_animation(serialize_animation(clip));
  require(opaque.keyframes[0].library_bytes == clip.keyframes[0].library_bytes,
          "Parsing discarded unsupported flame data");
  rejects([&] { PreparedAnimation prepared(opaque); }, "keyframe 1");
  clip = clip_for(base(), base());
  clip.name = std::string("bad\0label", 9);
  rejects([&] { serialize_animation(clip); }, "XML");
  clip.name = "\xc0\xa0";
  rejects([&] { serialize_animation(clip); }, "UTF-8");
  clip.name.assign(1025, 'n');
  rejects([&] { serialize_animation(clip); }, "byte limit");
  clip = clip_for(base(), base());
  clip.keyframes[0].transform_ids[0] = "ambiguous id";
  rejects([&] { serialize_animation(clip); }, "correspondence IDs");

  clip = clip_for(base(), base()); clip.keyframes.resize(1);
  const std::string prefix = "<flame><!--", suffix = "--><xform linear='1'/></flame>";
  clip.keyframes[0].library_bytes = prefix + std::string(6 * 1024 * 1024 - prefix.size() - suffix.size(), 'x') + suffix;
  rejects([&] { serialize_animation(clip); }, "8 MiB");
  clip.keyframes[0].library_bytes += " ";
  rejects([&] { validate_animation_structure(clip); }, "6 MiB");
}
void timeline_tests() {
  auto clip = clip_for(base(), base());
  require(animation_frame_at(clip, -1) == 0 && animation_frame_at(clip, 100) == 4,
          "Once frame clamping changed");
  clip.loop = AnimationLoop::repeat;
  require(animation_frame_at(clip, 5) == 0 && animation_frame_at(clip, -1) == 4 &&
          animation_frame_at(clip, std::numeric_limits<std::int64_t>::min()) == 2,
          "Repeat wrap or INT64_MIN mapping changed");
  clip.loop = AnimationLoop::ping_pong;
  const std::array<std::uint32_t, 10> expected{0,1,2,3,4,3,2,1,0,1};
  for (std::size_t i = 0; i < expected.size(); ++i)
    require(animation_frame_at(clip, static_cast<std::int64_t>(i)) == expected[i], "Ping-pong duplicated endpoints");
  require(animation_frame_at(clip, -1) == 1 && animation_frame_at(clip, std::numeric_limits<std::int64_t>::min()) == 0,
          "Negative ping-pong mapping changed");
  clip.frame_count = 1;
  for (auto loop : {AnimationLoop::once, AnimationLoop::repeat, AnimationLoop::ping_pong}) {
    clip.loop = loop; require(animation_frame_at(clip, -100) == 0, "One-frame loop failed");
  }
  clip = clip_for(base(), base(), 31); clip.rate = {30000, 1001};
  near(animation_seconds(clip, 30), 1.001, "Rational frame timestamp");
  near(animation_duration(clip), 31 * 1001. / 30000, "Duration must include every exported display interval");
  clip.seed = 0; clip.seed_mode = AnimationSeedMode::per_frame;
  require(animation_seed(clip, 0) == 0xe220a8397b1dcdafull &&
          animation_seed(clip, 1) == 0x6e789e6aa1b965f4ull,
          "Per-frame SplitMix64 seed vectors changed");
  clip.seed_mode = AnimationSeedMode::fixed; clip.seed = std::numeric_limits<std::uint64_t>::max();
  require(animation_seed(clip, 0) == clip.seed && animation_seed(clip, 30) == clip.seed, "64-bit fixed seed truncated");
  near(animation_ease(AnimationEasing::smoothstep, .25), .15625, "Smoothstep curve");
  near(animation_ease(AnimationEasing::smootherstep, .25), .103515625, "Smootherstep curve");
  // Near-one quintic rounding must not turn an admitted long clip into an
  // out-of-range palette/curve interpolation or an extrapolated model.
  for (const auto easing : {AnimationEasing::smoothstep, AnimationEasing::smootherstep}) {
    for (const auto position : {std::nextafter(1.0, 0.0), 999997.0 / 999999.0}) {
      const auto amount = animation_ease(easing, position);
      require(amount >= 0 && amount <= 1, "Easing escaped its closed interval near one");
    }
    auto first = base(); first.gamma = 1;
    auto last = base(); last.palette.fill({0, 0, 1}); last.gamma = 2;
    auto long_clip = clip_for(first, last, animation_frame_limit);
    long_clip.keyframes[0].easing = easing;
    const auto near_end = PreparedAnimation(long_clip).evaluate(999997);
    require(near_end.position >= 0 && near_end.position <= 1 &&
                near_end.flame.palette[0].b <= 1 && near_end.flame.gamma <= 2,
            "Long eased segment failed or extrapolated near its endpoint");
  }
  require(animation_ease(AnimationEasing::hold, .999) == 0 && animation_ease(AnimationEasing::hold, 1) == 1,
          "Hold boundary is not exact");
  rejects([&] { animation_ease(AnimationEasing::linear, std::numeric_limits<double>::quiet_NaN()); }, "finite");
  for (auto rate : {AnimationRate{0,1}, {241,1}, {1,2}, {30,0}, {240001,10000}}) {
    auto invalid = clip; invalid.rate = rate;
    rejects([&] { validate_animation_structure(invalid); }, "frame rate");
  }
  for (auto count : {0u, animation_frame_limit + 1}) {
    auto invalid = clip; invalid.frame_count = count;
    rejects([&] { validate_animation_structure(invalid); }, "frame count");
  }
  auto invalid = clip; invalid.keyframes[0].frame = 1;
  rejects([&] { validate_animation_structure(invalid); }, "frame zero");
  invalid = clip; invalid.keyframes[1].frame = 0;
  rejects([&] { validate_animation_structure(invalid); }, "strictly ordered");
  invalid = clip; invalid.keyframes[1].frame = clip.frame_count;
  rejects([&] { validate_animation_structure(invalid); }, "inside");
  invalid = clip; invalid.version = 2;
  rejects([&] { PreparedAnimation prepared(invalid); }, "version");
  invalid = clip; invalid.keyframes.resize(257);
  rejects([&] { validate_animation_structure(invalid); }, "256 keyframes");
  std::stop_source stop; stop.request_stop();
  try { PreparedAnimation prepared(clip, stop.get_token()); throw std::runtime_error("Cancelled preparation succeeded"); }
  catch (const AnimationCancelled &) {}
  PreparedAnimation prepared(clip);
  try { prepared.validateFrames(0, 30, stop.get_token()); throw std::runtime_error("Cancelled validation succeeded"); }
  catch (const AnimationCancelled &) {}
  rejects([&] { prepared.validateFrames(3, 2); }, "validation range");
}
void interpolation_tests() {
  auto a = base(), b = a;
  a.time = -2; b.time = 8; a.center = {-2, 4}; b.center = {6, -8};
  a.scale = 12; b.scale = 28; a.zoom = -1; b.zoom = 3;
  a.rotate = 350; b.rotate = 10; a.gamma = 1; b.gamma = 3;
  a.brightness = 2; b.brightness = 6; a.vibrancy = .2; b.vibrancy = .8;
  a.gamma_threshold = .1; b.gamma_threshold = .3;
  a.filter_radius = .5; b.filter_radius = 1.5;
  a.transforms[0].pre = {.2,.3,-.4,.5,-2,3}; b.transforms[0].pre = {.6,-.1,.8,-.7,4,-1};
  a.transforms[0].post.e = -3; b.transforms[0].post.e = 5;
  a.transforms[0].color = .2; b.transforms[0].color = .8;
  a.transforms[0].opacity = .4; b.transforms[0].opacity = 1;
  a.transforms[0].color_speed = .1; b.transforms[0].color_speed = .9;
  b.palette.fill({0,0,1}); b.width = 64; b.height = 48; b.oversample = 3; b.palette_mode = PaletteMode::linear;
  auto clip = clip_for(a,b); clip.affine = AnimationAffine::linear;
  PreparedAnimation prepared(clip);
  const auto left = document(a).render_model(0), right = document(b).render_model(0);
  exact(prepared.evaluate(0).flame, left, "Left endpoint was normalized");
  exact(prepared.evaluate(4).flame, right, "Right endpoint was normalized");
  const auto frame = prepared.evaluate(2); const auto &mid = frame.flame;
  require(frame.frame == 2 && frame.left_keyframe == 0 && frame.right_keyframe == 1 && frame.position == .5,
          "Frame provenance mismatch");
  near(mid.center.x, 2, "Center X interpolation"); near(mid.center.y, -2, "Center Y interpolation");
  near(mid.time, 3, "Source time interpolation"); near(mid.scale, 20, "Scale interpolation");
  near(mid.zoom, 1, "Zoom interpolation"); near(mid.rotate, 360, "Camera shortest wrap");
  near(mid.gamma, 2, "Gamma interpolation"); near(mid.brightness, 4, "Brightness interpolation");
  near(mid.vibrancy, .5, "Vibrancy interpolation"); near(mid.gamma_threshold, .2, "Threshold interpolation");
  near(*mid.filter_radius, 1, "Filter radius interpolation");
  near(mid.transforms[0].pre.a,.4,"Linear affine a"); near(mid.transforms[0].pre.b,.1,"Linear affine b");
  near(mid.transforms[0].pre.c,.2,"Linear affine c"); near(mid.transforms[0].pre.d,-.1,"Linear affine d");
  near(mid.transforms[0].pre.e,1,"Linear affine translation X"); near(mid.transforms[0].pre.f,1,"Linear affine translation Y");
  near(mid.transforms[0].post.e,1,"Post affine translation"); near(mid.transforms[0].color,.5,"Transform color");
  near(mid.transforms[0].opacity,.7,"Opacity interpolation"); near(mid.transforms[0].color_speed,.5,"Color speed interpolation");
  near(mid.palette[0].r,.5,"RGB palette red"); near(mid.palette[0].b,.5,"RGB palette blue");
  require(mid.width == 32 && mid.height == 24 && mid.oversample == 1 && mid.palette_mode == PaletteMode::step,
          "Discrete field switched before endpoint");
  require(diagnostic(prepared,"discrete"), "Discrete field discontinuity was not diagnosed");
  prepared.validateFrames(0,4);
  clip.camera_angle = AnimationAngle::unwrapped;
  near(PreparedAnimation(clip).evaluate(2).flame.rotate,180,"Unwrapped camera rotation");
  clip.palette = AnimationPalette::hsv_shortest;
  const auto hsv = PreparedAnimation(clip).evaluate(2).flame.palette[0];
  near(hsv.r,1,"HSV hue wrap red"); near(hsv.b,1,"HSV hue wrap blue"); near(hsv.g,0,"HSV hue wrap green");
  clip.palette = AnimationPalette::linear_rgb;
  near(PreparedAnimation(clip).evaluate(2).flame.palette[0].r,.7353569830524495,"Linear-light palette",1e-10);
  clip.palette = AnimationPalette::hsl_shortest;
  near(PreparedAnimation(clip).evaluate(2).flame.palette[0].r,1,"HSL palette interpolation");

  a = base(); b = a;
  b.curves.channels[0][1] = {.25,.75,2};
  clip = clip_for(a,b);
  near(PreparedAnimation(clip).evaluate(2).flame.curves.channels[0][1].weight,1.5,"Curve weight interpolation");
  near(PreparedAnimation(clip).evaluate(2).flame.curves.channels[0][1].y,.375,"Curve coordinate interpolation");
  b.curves.mode = ColorCurveMode::continuous;
  clip = clip_for(a,b);
  rejects([&] { PreparedAnimation mixed(clip); }, "color-curve modes differ");
  clip.keyframes[0].easing = AnimationEasing::hold;
  exact(PreparedAnimation(clip).evaluate(3).flame, document(a).render_model(0), "Curve-mode hold changed source");

  a = base(); b = a; a.scale = 1e250; b.scale = 1e-100; b.zoom = 1000;
  PreparedAnimation overflow(clip_for(a,b));
  overflow.validateFrames(0,0); overflow.validateFrames(4,4);
  rejects([&] { overflow.validateFrames(0,4); }, "frame 1 is invalid");
}
void polar_and_variation_tests() {
  auto a = base(), b = a;
  b.transforms[0].pre = {-1,0,0,-1,2,4};
  auto clip = clip_for(a,b);
  auto mid = PreparedAnimation(clip).evaluate(2).flame.transforms[0].pre;
  near(mid.a,0,"Polar rotation a"); near(mid.b,1,"Polar rotation b");
  near(mid.c,-1,"Polar rotation c"); near(mid.d,0,"Polar rotation d");
  near(mid.e,1,"Polar translation X"); near(mid.f,2,"Polar translation Y");
  near(mid.a*mid.d-mid.b*mid.c,1,"Polar rotation collapsed through zero");
  b.transforms[0].pre = {1,0,2,1,0,0};
  const auto shear = PreparedAnimation(clip_for(a,b)).evaluate(2).flame.transforms[0].pre;
  const auto c = std::cos(std::numbers::pi / 8), s = std::sin(std::numbers::pi / 8);
  const auto xx = .5 + .5 / std::sqrt(2.), xy = .5 / std::sqrt(2.);
  near(shear.a,c*xx+s*xy,"Polar stretch must be symmetric, not a QR decomposition");
  near(shear.b,-s*xx+c*xy,"Polar shear rotation");
  b.transforms[0].pre = {-1,0,0,-1,2,4};
  clip.affine = AnimationAffine::linear;
  near(PreparedAnimation(clip).evaluate(2).flame.transforms[0].pre.a,0,"Coefficient-linear mode");
  near(PreparedAnimation(clip).evaluate(2).flame.transforms[0].pre.b,0,"Coefficient-linear mode should permit collapse");
  a.transforms[0].pre = {-1,0,0,1,0,0}; b.transforms[0].pre = {0,-1,-1,0,0,0};
  PreparedAnimation reflected(clip_for(a,b));
  mid = reflected.evaluate(2).flame.transforms[0].pre;
  near(mid.a*mid.d-mid.b*mid.c,-1,"Matching reflections changed orientation");
  require(!diagnostic(reflected,"reflection"),"Same-orientation reflections unnecessarily fell back");
  a.transforms[0].pre = {}; b.transforms[0].pre = {-1,0,0,1,0,0};
  PreparedAnimation opposite(clip_for(a,b));
  require(diagnostic(opposite,"reflection"),"Opposite reflection fallback not diagnosed");
  near(opposite.evaluate(2).flame.transforms[0].pre.a,0,"Opposite reflection linear fallback");
  a.transforms[0].pre = {0,0,0,1,0,0}; b.transforms[0].pre = {};
  PreparedAnimation singular(clip_for(a,b));
  require(diagnostic(singular,"singular"),"Singular fallback not diagnosed");
  near(singular.evaluate(2).flame.transforms[0].pre.a,.5,"Singular fallback coefficient");

  a = base(); b = a;
  a.transforms[0].variations = {{Variation::julian,1}};
  a.transforms[0].parameters.julian_power = -2; a.transforms[0].parameters.julian_dist = 1;
  b.transforms[0].variations = {{Variation::julian,.5},{Variation::swirl,.5}};
  b.transforms[0].parameters.julian_power = 5; b.transforms[0].parameters.julian_dist = 3;
  auto varied = PreparedAnimation(clip_for(a,b)).evaluate(2).flame.transforms[0];
  require(varied.parameters.julian_power == -2, "Integer JuliaN power was interpolated through invalid zero");
  near(varied.parameters.julian_dist,2,"JuliaN real parameter interpolation");
  near(varied.variations[0].weight,.75,"Shared variation weight");
  near(varied.variations[1].weight,.25,"Introduced variation weight");
  a = base();
  varied = PreparedAnimation(clip_for(a,b)).evaluate(2).flame.transforms[0];
  require(varied.parameters.julian_power == 5, "Introduced JuliaN lost its own integer parameter");
  near(varied.parameters.julian_dist,3,"Introduced JuliaN changed shape while fading");
}
void topology_tests() {
  auto a = base(), b = a;
  a.transforms.resize(2); b.transforms.resize(2);
  a.transforms[0].pre.e = 2; a.transforms[1].pre.e = 8;
  b.transforms[0].pre.e = 12; b.transforms[1].pre.e = 4;
  a.transforms[0].chaos = {1,2}; a.transforms[1].chaos = {3,4};
  b.transforms[0].chaos = {8,6}; b.transforms[1].chaos = {4,2};
  const std::vector<std::string> left_ids{"A","B"}, right_ids{"B","A"};
  auto clip = clip_for(a,b);
  clip.keyframes[0] = key(a,0,left_ids); clip.keyframes[1] = key(b,4,right_ids);
  PreparedAnimation mapped(clip);
  const auto mid = mapped.evaluate(2).flame;
  near(mid.transforms[0].pre.e,3,"Explicit correspondence ignored reordered A");
  near(mid.transforms[1].pre.e,10,"Explicit correspondence ignored reordered B");
  near(mid.transforms[0].chaos[0],1.5,"Xaos A to A was not remapped");
  near(mid.transforms[0].chaos[1],3,"Xaos A to B was not remapped");
  near(mid.transforms[1].chaos[0],4.5,"Xaos B to A was not remapped");
  near(mid.transforms[1].chaos[1],6,"Xaos B to B was not remapped");
  exact(mapped.evaluate(4).flame,document(b).render_model(0),"Right endpoint lost original transform/chaos order");
  clip.keyframes[1].transform_ids = {"B","C"};
  rejects([&] { PreparedAnimation incompatible(clip); }, "strict morph");
  clip.topology = AnimationTopology::fade_unmatched;
  PreparedAnimation fade(clip);
  const auto faded = fade.evaluate(2).flame;
  require(faded.transforms.size() == 3, "Unmatched transforms were silently paired");
  near(faded.transforms[0].weight,.5,"Lost branch selection fade");
  near(faded.transforms[1].weight,1,"Matched branch weight");
  near(faded.transforms[2].weight,.5,"New branch selection fade");
  near(faded.transforms[0].chaos[2],0,"Invented a missing Xaos edge");
  near(faded.transforms[2].chaos[1],2,"New row edges must fade from zero");
  fade.validateFrames(0,4);
  require(diagnostic(fade,"unmatched branches"),"Topology fade not diagnosed");
  clip.keyframes[0].transform_ids = {"A","A"};
  rejects([&] { PreparedAnimation ambiguous(clip); }, "unique");

  a = base(); a.transforms.push_back(Transform{}); a.transforms[1].weight = 0;
  a.transforms[0].chaos = {1,7}; a.transforms[1].chaos = {0,0};
  b = base(); b.transforms[0].chaos = {1};
  const std::vector<std::string> two_ids{"anchor","disabled"}, one_id{"anchor"};
  clip = make_morph_animation(document(a),0,document(b),0,5,{},two_ids,one_id,AnimationTopology::fade_unmatched);
  PreparedAnimation unreachable(clip);
  unreachable.validateFrames(0,4);
  const auto disabled = unreachable.evaluate(2).flame;
  require(disabled.transforms.size() == 2 && disabled.transforms[1].weight == 0 &&
          disabled.transforms[1].chaos[0] == 0, "Zero-weight branch was made reachable or given invented exits");

  a = base(); b = a; b.final_transform.emplace(); b.final_transform->pre.e = 2;
  b.final_transform->color_speed = .8;
  clip = clip_for(a,b);
  rejects([&] { PreparedAnimation final_mismatch(clip); }, "strict morph");
  clip.topology = AnimationTopology::fade_unmatched;
  const auto with_final = PreparedAnimation(clip).evaluate(2).flame;
  require(with_final.final_transform.has_value(),"Missing final did not use identity interpolation");
  near(with_final.final_transform->pre.e,1,"Final transform fade from identity");
  near(with_final.final_transform->color_speed,.4,"Identity final altered color before fading");

  a = base(); b = base(); b.source_version = "Apophysis 7x";
  clip = clip_for(a,b);
  rejects([&] { PreparedAnimation producer_mismatch(clip); }, "producer families differ");
  clip.keyframes[0].easing = AnimationEasing::hold;
  exact(PreparedAnimation(clip).evaluate(3).flame,document(a).render_model(0),"Hold cut mixed producer semantics");
  exact(PreparedAnimation(clip).evaluate(4).flame,document(b).render_model(0),"Hold cut omitted right endpoint");
  clip.keyframes[0].easing = AnimationEasing::linear;
  clip.frame_count = 2; clip.keyframes[1].frame = 1;
  PreparedAnimation adjacent(clip);
  exact(adjacent.evaluate(1).flame,document(b).render_model(0),"Adjacent keyframes should form an exact cut");

  a = base(); b = a; a.transforms.resize(256); b.transforms.resize(256);
  clip = clip_for(a,b);
  for (auto &id : clip.keyframes[1].transform_ids) id += "-other";
  clip.keyframes[0].easing = AnimationEasing::hold;
  PreparedAnimation wide_cut(clip); wide_cut.validateFrames(0,4);
  clip.keyframes[0].easing = AnimationEasing::linear; clip.topology = AnimationTopology::fade_unmatched;
  rejects([&] { PreparedAnimation oversized_union(clip); }, "union exceeds");
}
void preset_and_legacy_time_tests() {
  auto source = document(base());
  for (auto preset : {AnimationPreset::still,AnimationPreset::camera_orbit,AnimationPreset::zoom,AnimationPreset::palette_cycle}) {
    for (auto count : {1u,2u,5u,9u}) {
      auto clip = make_animation_preset(source,0,preset,count);
      PreparedAnimation prepared(clip); prepared.validateFrames(0,count-1);
      require(clip.keyframes.front().library_bytes == source.bytes(),"Preset rewrote original keyframe source");
    }
  }
  auto orbit = make_animation_preset(source,0,AnimationPreset::camera_orbit,9);
  near(PreparedAnimation(orbit).evaluate(4).flame.rotate,180,"Orbit shortcut erased full revolution");
  near(PreparedAnimation(orbit).evaluate(8).flame.rotate,360,"Orbit endpoint lost unwrapped rotation");
  auto zoom = make_animation_preset(source,0,AnimationPreset::zoom,9);
  near(PreparedAnimation(zoom).evaluate(4).flame.zoom,.5,"Zoom preset midpoint");
  require(zoom.loop == AnimationLoop::ping_pong,"Zoom preset playback mode");
  auto colored = base(); colored.palette = default_palette(); source = document(colored);
  auto palette = make_animation_preset(source,0,AnimationPreset::palette_cycle,9);
  const auto palette_end = PreparedAnimation(palette).evaluate(8).flame.palette;
  const auto palette_start = source.render_model(0).palette;
  for (std::size_t i=0;i<256;++i) {
    near(palette_end[i].r,palette_start[i].r,"Palette-cycle endpoint red");
    near(palette_end[i].g,palette_start[i].g,"Palette-cycle endpoint green");
    near(palette_end[i].b,palette_start[i].b,"Palette-cycle endpoint blue");
  }
  auto larger = base(); larger.transforms.resize(2);
  rejects([&] { make_morph_animation(document(base()),0,document(larger),0); }, "explicit correspondence");

  auto timed = LegacyDocument::parse("<flames><!-- original timing --><flame time='0'><xform weight='1' linear='1'/>"
      "<color index='0' rgb='255 0 0'/></flame><flame time='0.5'><xform weight='1' linear='1' coefs='1 0 0 1 2 0'/>"
      "<color index='0' rgb='255 0 0'/></flame></flames>");
  auto imported = animation_from_timed_library(timed,AnimationTimeUnits::seconds,{24,1});
  require(imported.frame_count == 13 && imported.keyframes[1].frame == 12 && imported.keyframes[1].selected_index == 1,
          "Explicit seconds conversion or selected collection index failed");
  near(PreparedAnimation(imported).evaluate(6).flame.transforms[0].pre.e,1,"Timed library midpoint");
  require(imported.keyframes[0].library_bytes == timed.bytes() && imported.keyframes[1].library_bytes == timed.bytes(),
          "Timed import discarded original collection");
  rejects([&] { animation_from_timed_library(LegacyDocument::parse(replaced(timed.bytes(),"time='0.5'","time='0.01'")),AnimationTimeUnits::seconds,{24,1}); }, "distinct frames");
  rejects([&] { animation_from_timed_library(LegacyDocument::parse(replaced(timed.bytes(),"time='0.5'","time='-1'")),AnimationTimeUnits::frames); }, "supported frame range");
  rejects([&] { animation_from_timed_library(LegacyDocument::parse(replaced(timed.bytes(),"time='0'","time='1'")),AnimationTimeUnits::seconds); }, "ordered");
}
struct Temporary {
  fs::path directory = fs::temp_directory_path() / ("apo-animation-tests-" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  Temporary() { require(fs::create_directory(directory),"Cannot create animation fixture directory"); }
  ~Temporary() { std::error_code error; fs::remove_all(directory,error); }
};
void plugin_tests(const fs::path &sample_manifest, const fs::path &integer_module) {
  enable_plugin(sample_manifest,inspect_plugin(sample_manifest).fingerprint);
  auto a = base(), b = a;
  a.transforms[0].variations = {make_variation_term("sample_wave",.4,{{"amplitude",-.5},{"frequency",2}})};
  b.transforms[0].variations = {make_variation_term("sample_wave",.8,{{"amplitude",1.5},{"frequency",8}})};
  auto clip = clip_for(a,b);
  require(!clip.keyframes[0].plugin_identity.empty(),"Native keyframe omitted exact module identity");
  PreparedAnimation prepared(clip);
  const auto mid = prepared.evaluate(2).flame.transforms[0].variations[0];
  near(mid.weight,.6,"Native variation contribution interpolation");
  near(term_parameters(mid).at("amplitude"),.5,"Native real amplitude interpolation");
  near(term_parameters(mid).at("frequency"),5,"Native real frequency interpolation");
  auto stale = clip; stale.keyframes[1].plugin_identity += "changed";
  rejects([&] { PreparedAnimation rejected(stale); }, "native module identity differs");
  const auto payload = serialize_animation(clip);
  disable_plugin("sample_wave");
  const auto archived = parse_animation(payload);
  require(!find_plugin_variation("sample_wave"),"Animation parsing implicitly enabled native code");
  rejects([&] { PreparedAnimation unavailable(archived); }, "sample_wave");
  prepared.validateFrames(0,4);
  Rng rng(123);
  const auto orbit = apply_transform(prepared.evaluate(2).flame.transforms[0],{{.2,.3},.4},rng);
  require(std::isfinite(orbit.point.x) && std::isfinite(orbit.point.y), "Prepared clip lost native module lifetime after disable");
  enable_plugin(sample_manifest,inspect_plugin(sample_manifest).fingerprint);

  Temporary temp;
  const auto name = "fixture" + integer_module.extension().string();
  fs::copy_file(integer_module,temp.directory / name);
  const auto manifest = temp.directory / "integer.apoplugin";
  {
    std::ofstream out(manifest,std::ios::binary);
    out << "<apophysis-plugin abi='1' id='org.apophysis.reborn.fixture' name='fixture_probe' "
        "version='1.0.0' description='Deterministic plugin contract fixture' library='" << name << "'>"
        "<parameter name='mode' default='0' min='0' max='4' type='integer'/></apophysis-plugin>";
  }
  enable_plugin(manifest,inspect_plugin(manifest).fingerprint);
  {
    a = base(); b = a;
    a.transforms[0].variations = {make_variation_term("fixture_probe",1,{{"mode",0}})};
    b.transforms[0].variations = {make_variation_term("fixture_probe",1,{{"mode",4}})};
    PreparedAnimation integers(clip_for(a,b));
    require(diagnostic(integers,"discrete"),"Native integer parameter hold was not diagnosed");
    near(term_parameters(integers.evaluate(3).flame.transforms[0].variations[0]).at("mode"),0,"Native integer switched before right endpoint");
    near(term_parameters(integers.evaluate(4).flame.transforms[0].variations[0]).at("mode"),4,"Native integer endpoint lost exact value");
    a = base();
    PreparedAnimation introduced(clip_for(a,b));
    near(term_parameters(introduced.evaluate(1).flame.transforms[0].variations[1]).at("mode"),4,"Introduced native term lost its own integer value");
  }
  a = base(); b = a;
  disable_plugin("fixture_probe");
  disable_plugin("sample_wave");
}
} // namespace

int main(int argc, char **argv) {
  try {
    require(argc == 3, "Usage: animation-tests SAMPLE.apoplugin INTEGER_FIXTURE_MODULE");
    io_and_source_tests(); hostile_input_tests(); timeline_tests(); interpolation_tests();
    polar_and_variation_tests(); topology_tests(); preset_and_legacy_time_tests();
    plugin_tests(argv[1],argv[2]);
    std::cout << "Animation source, schema, timing, morph, numerical, and native-identity regressions passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
