#include "animation.hpp"
#include "color_curves.hpp"
#include "creative.hpp"
#include "flame_io.hpp"
#include "plugin.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>
#include <numeric>
#include <set>
#include <stdexcept>

namespace apo {
namespace {
[[noreturn]] void fail(const std::string &message) {
  throw std::invalid_argument("Animation: " + message);
}
void stop_if_requested(std::stop_token stop) {
  if (stop.stop_requested()) throw AnimationCancelled();
}
void rate_and_count(const AnimationClip &clip) {
  if (!clip.rate.numerator || clip.rate.numerator > 240000 ||
      !clip.rate.denominator || clip.rate.denominator > 10000 ||
      clip.rate.numerator < clip.rate.denominator ||
      std::uint64_t(clip.rate.numerator) > 240ull * clip.rate.denominator)
    fail("frame rate must be 1 to 240 fps, numerator at most 240000 and denominator at most 10000");
  if (!clip.frame_count || clip.frame_count > animation_frame_limit)
    fail("frame count must be 1 to 1000000");
}
void xml_text(std::string_view text, std::size_t maximum, const char *field) {
  if (text.size() > maximum) fail(std::string(field) + " exceeds its byte limit");
  for (std::size_t i = 0; i < text.size();) {
    const auto first = static_cast<unsigned char>(text[i++]);
    std::uint32_t value = first;
    unsigned continuation = 0;
    std::uint32_t minimum = 0;
    if (first >= 0xc2 && first <= 0xdf) {
      value = first & 0x1f; continuation = 1; minimum = 0x80;
    } else if (first >= 0xe0 && first <= 0xef) {
      value = first & 0xf; continuation = 2; minimum = 0x800;
    } else if (first >= 0xf0 && first <= 0xf4) {
      value = first & 7; continuation = 3; minimum = 0x10000;
    } else if (first >= 0x80) fail(std::string(field) + " is not valid UTF-8");
    if (i + continuation > text.size()) fail(std::string(field) + " has truncated UTF-8");
    for (unsigned j = 0; j < continuation; ++j) {
      const auto c = static_cast<unsigned char>(text[i++]);
      if ((c & 0xc0) != 0x80) fail(std::string(field) + " is not valid UTF-8");
      value = (value << 6) | (c & 63);
    }
    if (value < minimum || value > 0x10ffff ||
        (value >= 0xd800 && value <= 0xdfff) || value == 0xfffe || value == 0xffff ||
        (value < 32 && value != 9 && value != 10 && value != 13))
      fail(std::string(field) + " contains an invalid XML character");
  }
}
bool identifier(std::string_view id) {
  return !id.empty() && id.size() <= 64 && id != "__final" &&
      std::all_of(id.begin(), id.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
      });
}
double blend(double a, double b, double t) { return std::lerp(a, b, t); }
double shortest_delta(double a, double b, double period) {
  auto result = std::remainder(std::remainder(b, period) - std::remainder(a, period), period);
  if (result == -period / 2) result = period / 2;
  return result;
}
double angle(double a, double b, double t, double period) {
  return a + shortest_delta(a, b, period) * t;
}
Affine linear_affine(const Affine &a, const Affine &b, double t) {
  return {blend(a.a,b.a,t), blend(a.b,b.b,t), blend(a.c,b.c,t),
          blend(a.d,b.d,t), blend(a.e,b.e,t), blend(a.f,b.f,t)};
}
struct Polar {
  double rotation = 0, xx = 0, yy = 0, xy = 0;
  int orientation = 0;
};
std::optional<Polar> polar(const Affine &a) {
  const auto maximum = std::max({std::abs(a.a), std::abs(a.b), std::abs(a.c), std::abs(a.d)});
  if (!maximum) return {};
  const auto x = a.a / maximum, y = a.b / maximum;
  auto u = a.c / maximum, v = a.d / maximum;
  const auto determinant = x*v-y*u;
  if (std::abs(determinant) < 1e-12) return {};
  const auto orientation = determinant > 0 ? 1 : -1;
  // For a reflection, factor the same fixed F=diag(1,-1) on both ends.
  // The remaining orientation-preserving matrix has the unique right polar
  // decomposition R*S, with S symmetric positive definite.
  if(orientation<0) {u=-u;v=-v;}
  const auto length=std::hypot(x+v,y-u);
  if(length<1e-12)return {};
  const auto c=(x+v)/length,s=(y-u)/length;
  Polar result{std::atan2(s,c),(c*x+s*y)*maximum,(-s*u+c*v)*maximum,
      .5*((c*u+s*v)+(-s*x+c*y))*maximum,orientation};
  if (!std::isfinite(result.xx) || !std::isfinite(result.yy) || !std::isfinite(result.xy)) return {};
  return result;
}
bool can_polar(const Affine &a, const Affine &b) {
  const auto x=polar(a), y=polar(b);
  return x && y && x->orientation == y->orientation;
}
Affine affine(const Affine &a, const Affine &b, double t, AnimationAffine mode) {
  if (mode == AnimationAffine::linear) return linear_affine(a,b,t);
  const auto x=polar(a), y=polar(b);
  if (!x || !y || x->orientation != y->orientation) return linear_affine(a,b,t);
  const auto rotation = angle(x->rotation,y->rotation,t,2*std::numbers::pi);
  const auto c=std::cos(rotation), s=std::sin(rotation);
  const auto xx=blend(x->xx,y->xx,t),yy=blend(x->yy,y->yy,t),xy=blend(x->xy,y->xy,t);
  return {c*xx-s*xy,s*xx+c*xy,(c*xy-s*yy)*x->orientation,
          (s*xy+c*yy)*x->orientation,blend(a.e,b.e,t),blend(a.f,b.f,t)};
}
creative::GradientSpace palette_space(AnimationPalette space) {
  switch(space) {
  case AnimationPalette::rgb: return creative::GradientSpace::rgb;
  case AnimationPalette::hsv_shortest: return creative::GradientSpace::hsv_shortest;
  case AnimationPalette::linear_rgb: return creative::GradientSpace::linear_rgb;
  case AnimationPalette::hsl_shortest: return creative::GradientSpace::hsl_shortest;
  }
  fail("invalid palette interpolation");
}
void same_plugin(const Term &a, const Term &b) {
  if (a.id != b.id) fail("a variation name identifies different implementations");
  if (a.id != Variation::plugin) return;
  const auto &x=a.plugin->info(), &y=b.plugin->info();
  if (x.id != y.id || x.version != y.version || x.fingerprint != y.fingerprint ||
      x.parameters != y.parameters)
    fail("native variation " + x.name + " changes module identity between keyframes");
}
void compatible_transform(const Transform &a, const Transform &b) {
  if (a.parameters.apophysis_math != b.parameters.apophysis_math ||
      a.parameters.separate_blur_angle != b.parameters.separate_blur_angle)
    fail("matched transforms use different historical variation conventions");
  for (const auto &x : a.variations)
    for (const auto &y : b.variations)
      if (term_name(x) == term_name(y)) same_plugin(x,y);
}
bool hold_parameter(const VariationParameterInfo &field, const Transform &a, const Transform &b) {
  if (field.is_integer(a.parameters)) return true;
  const bool forbidsZero = field.name == "juliascope_power" || field.name == "pie_slices" ||
                           field.name == "ngon_sides";
  return forbidsZero && std::signbit(field.get(a.parameters)) != std::signbit(field.get(b.parameters));
}
bool discrete_parameters_differ(const Transform &a,const Transform &b) {
  for (const auto &field : variation_parameter_registry()) {
    const auto has = [&](const Transform &x) {
      return std::any_of(x.variations.begin(), x.variations.end(),
                         [&](const Term &term) { return term.id == field.variation; });
    };
    if (hold_parameter(field, a, b) && field.get(a.parameters) != field.get(b.parameters) &&
        has(a) && has(b)) return true;
  }
  for(const auto &x:a.variations)
    for(const auto &y:b.variations)
      if(x.id==Variation::plugin && term_name(x)==term_name(y))
        for(std::size_t i=0;i<x.plugin->info().parameters.size();++i)
          if(x.plugin->info().parameters[i].integer && x.plugin_parameters[i]!=y.plugin_parameters[i])return true;
  return false;
}
std::vector<Term> terms(const Transform &a, const Transform &b, double t) {
  std::vector<Term> result;
  std::set<std::string> used;
  for (const auto &x : a.variations) {
    auto term=x;
    const auto name=term_name(x);
    used.emplace(name);
    const auto found=std::find_if(b.variations.begin(),b.variations.end(),[&](const auto &other) { return term_name(other)==name; });
    if (found==b.variations.end()) term.weight=blend(x.weight,0,t);
    else {
      same_plugin(x,*found);
      term.weight=blend(x.weight,found->weight,t);
      if (term.id==Variation::plugin) {
        const auto &parameters=term.plugin->info().parameters;
        for (std::size_t i=0;i<parameters.size();++i)
          if (!parameters[i].integer)
            term.plugin_parameters[i]=blend(x.plugin_parameters[i],found->plugin_parameters[i],t);
      }
    }
    result.push_back(std::move(term));
  }
  for (const auto &y:b.variations)
    if (!used.contains(std::string(term_name(y)))) {
      auto term=y;
      term.weight=blend(0,y.weight,t);
      result.push_back(std::move(term));
    }
  return result;
}
Transform transform(const Transform &a,const Transform &b,double t,AnimationAffine mode) {
  Transform result=a;
  result.pre=affine(a.pre,b.pre,t,mode);
  result.post=affine(a.post,b.post,t,mode);
  result.weight=blend(a.weight,b.weight,t);
  result.color=blend(a.color,b.color,t);
  result.color_speed=blend(a.color_speed,b.color_speed,t);
  result.opacity=blend(a.opacity,b.opacity,t);
  // Hold discrete parameters on the left. A one-sided term keeps its shape as it fades.
  for (const auto &field : variation_parameter_registry()) {
    const auto has = [&](const Transform &x) {
      return std::any_of(x.variations.begin(), x.variations.end(),
                         [&](const Term &term) { return term.id == field.variation; });
    };
    if (!has(b)) continue;
    if (!has(a))
      field.set(result.parameters, field.get(b.parameters));
    else if (!hold_parameter(field, a, b))
      field.set(result.parameters, blend(field.get(a.parameters), field.get(b.parameters), t));
  }
  result.variations=terms(a,b,t);
  result.chaos.clear();
  return result;
}
Transform identity_final(const Transform &other) {
  Transform identity;
  identity.parameters=other.parameters;
  identity.color_speed=0;
  return identity;
}
std::vector<std::string> default_ids(std::size_t count) {
  std::vector<std::string> result;
  for(std::size_t i=0;i<count;++i) result.push_back("transform-"+std::to_string(i+1));
  return result;
}
std::string key_context(std::size_t index, const AnimationKeyframe &key) {
  return "keyframe " + std::to_string(index+1) + " at frame " + std::to_string(key.frame);
}
} // namespace

std::string_view animation_name(AnimationLoop value) {
  switch(value) { case AnimationLoop::once:return "once";case AnimationLoop::repeat:return "repeat";case AnimationLoop::ping_pong:return "ping-pong"; }
  fail("invalid loop mode");
}
std::string_view animation_name(AnimationEasing value) {
  switch(value) { case AnimationEasing::hold:return "hold";case AnimationEasing::linear:return "linear";case AnimationEasing::smoothstep:return "smoothstep";case AnimationEasing::smootherstep:return "smootherstep"; }
  fail("invalid easing mode");
}
std::string_view animation_name(AnimationAffine value) {
  switch(value) { case AnimationAffine::linear:return "linear";case AnimationAffine::polar:return "polar"; }
  fail("invalid affine mode");
}
std::string_view animation_name(AnimationPalette value) {
  switch(value) { case AnimationPalette::rgb:return "rgb";case AnimationPalette::hsv_shortest:return "hsv-shortest";case AnimationPalette::linear_rgb:return "linear-rgb";case AnimationPalette::hsl_shortest:return "hsl-shortest"; }
  fail("invalid palette mode");
}
std::string_view animation_name(AnimationAngle value) {
  switch(value) { case AnimationAngle::shortest:return "shortest";case AnimationAngle::unwrapped:return "unwrapped"; }
  fail("invalid camera angle mode");
}
std::string_view animation_name(AnimationTopology value) {
  switch(value) { case AnimationTopology::strict:return "strict";case AnimationTopology::fade_unmatched:return "fade-unmatched"; }
  fail("invalid topology mode");
}
std::string_view animation_name(AnimationSeedMode value) {
  switch(value) { case AnimationSeedMode::fixed:return "fixed";case AnimationSeedMode::per_frame:return "per-frame"; }
  fail("invalid seed mode");
}
std::string_view animation_name(AnimationPreset value) {
  switch(value) { case AnimationPreset::still:return "still";case AnimationPreset::camera_orbit:return "camera-orbit";case AnimationPreset::zoom:return "zoom";case AnimationPreset::palette_cycle:return "palette-cycle"; }
  fail("invalid animation preset");
}

void validate_animation_structure(const AnimationClip &clip) {
  if (clip.version != AnimationClip::current_version) fail("unsupported animation version " + std::to_string(clip.version));
  rate_and_count(clip);
  xml_text(clip.name,1024,"animation name");
  (void)animation_name(clip.loop); (void)animation_name(clip.affine);
  (void)animation_name(clip.palette); (void)animation_name(clip.camera_angle);
  (void)animation_name(clip.topology); (void)animation_name(clip.seed_mode);
  if (clip.keyframes.empty() || clip.keyframes.size()>animation_keyframe_limit) fail("animation needs 1 to 256 keyframes");
  if (clip.keyframes.front().frame!=0) fail("the first keyframe must be at frame zero");
  std::size_t bytes=0;
  for (std::size_t i=0;i<clip.keyframes.size();++i) {
    const auto &key=clip.keyframes[i];
    if (key.frame>=clip.frame_count || (i && key.frame<=clip.keyframes[i-1].frame))
      fail("keyframes must be strictly ordered, unique and inside the frame range");
    xml_text(key.name,1024,"keyframe name");
    xml_text(key.plugin_identity,64*1024,"plugin identity");
    (void)animation_name(key.easing);
    if (key.library_bytes.empty() || key.library_bytes.size()>6*1024*1024-bytes)
      fail("combined keyframe source snapshots exceed 6 MiB");
    bytes+=key.library_bytes.size();
    const auto document=LegacyDocument::parse(key.library_bytes);
    if (key.selected_index>=document.entries().size()) fail("keyframe selected entry is outside its collection");
    if (key.transform_ids.empty() || key.transform_ids.size()>animation_transform_limit)
      fail("each keyframe needs 1 to 256 transform correspondence IDs");
    std::set<std::string> ids;
    for (const auto &id:key.transform_ids)
      if (!identifier(id) || !ids.insert(id).second)
        fail("correspondence IDs must be unique ASCII letters/digits/._- of length 1 to 64");
  }
}

AnimationKeyframe capture_animation_keyframe(const LegacyDocument &document,
    std::size_t selected_index,std::uint32_t frame,std::string name,
    std::span<const std::string> ids) {
  const auto model=document.render_model(selected_index);
  if (model.transforms.empty() || model.transforms.size()>animation_transform_limit)
    fail("a captured keyframe must contain 1 to 256 ordinary transforms");
  AnimationKeyframe result;
  result.frame=frame;
  result.name=name.empty()?model.name:std::move(name);
  result.library_bytes=document.bytes();
  result.selected_index=selected_index;
  result.transform_ids=ids.empty()?default_ids(model.transforms.size()):std::vector<std::string>(ids.begin(),ids.end());
  if (result.transform_ids.size()!=model.transforms.size()) fail("correspondence ID count differs from transform count");
  std::set<std::string> unique;
  for(const auto &id:result.transform_ids)
    if (!identifier(id) || !unique.insert(id).second) fail("invalid or duplicate correspondence ID");
  result.plugin_identity=plugin_identity(model);
  return result;
}

std::uint32_t animation_frame_at(const AnimationClip &clip,std::int64_t frame) {
  rate_and_count(clip);
  (void)animation_name(clip.loop);
  if (clip.frame_count==1) return 0;
  if (clip.loop==AnimationLoop::once)
    return frame<0?0:static_cast<std::uint32_t>(std::min<std::int64_t>(frame,clip.frame_count-1));
  const auto period=clip.loop==AnimationLoop::repeat?std::int64_t(clip.frame_count):2ll*(clip.frame_count-1);
  if (clip.loop!=AnimationLoop::repeat && clip.loop!=AnimationLoop::ping_pong) fail("invalid loop mode");
  auto value=frame%period;
  if(value<0)value+=period;
  if(clip.loop==AnimationLoop::ping_pong && value>=clip.frame_count)value=period-value;
  return static_cast<std::uint32_t>(value);
}
double animation_seconds(const AnimationClip &clip,std::uint32_t frame) {
  rate_and_count(clip);
  if(frame>=clip.frame_count) fail("frame is outside the animation");
  return static_cast<double>(frame)*clip.rate.denominator/clip.rate.numerator;
}
double animation_duration(const AnimationClip &clip) {
  rate_and_count(clip);
  return static_cast<double>(clip.frame_count)*clip.rate.denominator/clip.rate.numerator;
}
std::uint64_t animation_seed(const AnimationClip &clip,std::uint32_t frame) {
  rate_and_count(clip);
  if(frame>=clip.frame_count) fail("seed frame is outside the animation");
  if(clip.seed_mode==AnimationSeedMode::fixed)return clip.seed;
  if(clip.seed_mode!=AnimationSeedMode::per_frame)fail("invalid seed mode");
  auto value=clip.seed+0x9e3779b97f4a7c15ull*(std::uint64_t(frame)+1);
  value=(value^(value>>30))*0xbf58476d1ce4e5b9ull;
  value=(value^(value>>27))*0x94d049bb133111ebull;
  return value^(value>>31);
}
double animation_ease(AnimationEasing easing,double t) {
  if(!std::isfinite(t) || t<0 || t>1)fail("easing position must be finite and in [0,1]");
  switch(easing) {
  case AnimationEasing::hold:return t==1?1:0;
  case AnimationEasing::linear:return t;
  case AnimationEasing::smoothstep:return std::clamp(t*t*(3-2*t),0.0,1.0);
  case AnimationEasing::smootherstep:return std::clamp(t*t*t*(t*(t*6-15)+10),0.0,1.0);
  }
  fail("invalid easing mode");
}

struct PreparedAnimation::Data {
  struct Segment { std::vector<std::string> ids; std::vector<int> from,to; };
  AnimationClip clip;
  std::vector<Flame> models;
  std::vector<Segment> segments;
  std::vector<std::string> diagnostics;
};

PreparedAnimation::PreparedAnimation(AnimationClip clip,std::stop_token stop) {
  stop_if_requested(stop);
  validate_animation_structure(clip);
  auto data=std::make_shared<Data>();
  data->clip=std::move(clip);
  for(std::size_t i=0;i<data->clip.keyframes.size();++i) {
    stop_if_requested(stop);
    const auto &key=data->clip.keyframes[i];
    try {
      auto model=LegacyDocument::parse(key.library_bytes).render_model(key.selected_index);
      if(model.transforms.size()!=key.transform_ids.size())fail("correspondence ID count differs from transform count");
      if(plugin_identity(model)!=key.plugin_identity)fail("native module identity differs from captured keyframe; explicitly re-capture after approving the intended module");
      for(const auto &transform:model.transforms)
        if(transform.variations.size()>128)fail("transform exceeds 128 variation terms");
      if(model.final_transform && model.final_transform->variations.size()>128)
        fail("final transform exceeds 128 variation terms");
      data->models.push_back(std::move(model));
    }catch(const std::exception &error) { fail(key_context(i,key)+": "+error.what()); }
  }
  for(std::size_t segment=0;segment+1<data->models.size();++segment) {
    stop_if_requested(stop);
    const auto &left=data->clip.keyframes[segment], &right=data->clip.keyframes[segment+1];
    const auto &a=data->models[segment], &b=data->models[segment+1];
    Data::Segment mapping;
    // A cut never constructs a union model, so unrelated complete topologies
    // remain legal even if their combined identity count would exceed 256.
    if(left.easing==AnimationEasing::hold || right.frame-left.frame==1) {
      data->segments.push_back(std::move(mapping));
      continue;
    }
    mapping.ids=left.transform_ids;
    for(const auto &id:right.transform_ids)
      if(std::find(mapping.ids.begin(),mapping.ids.end(),id)==mapping.ids.end())mapping.ids.push_back(id);
    if(mapping.ids.size()>animation_transform_limit)fail("morph union exceeds 256 transform identities");
    for(const auto &id:mapping.ids) {
      const auto x=std::find(left.transform_ids.begin(),left.transform_ids.end(),id);
      const auto y=std::find(right.transform_ids.begin(),right.transform_ids.end(),id);
      mapping.from.push_back(x==left.transform_ids.end()?-1:static_cast<int>(x-left.transform_ids.begin()));
      mapping.to.push_back(y==right.transform_ids.end()?-1:static_cast<int>(y-right.transform_ids.begin()));
    }
    if(left.easing!=AnimationEasing::hold && right.frame-left.frame>1) {
      const auto context="segment starting at frame "+std::to_string(left.frame)+": ";
      if(identify_producer(a.source_version)!=identify_producer(b.source_version))fail(context+"producer families differ; use an explicit hold/cut or normalize the source deliberately");
      if(a.curves.mode!=b.curves.mode)fail(context+"color-curve modes differ; use a hold/cut or one common curve mode");
      if(data->clip.topology==AnimationTopology::strict &&
         (left.transform_ids.size()!=right.transform_ids.size() || mapping.ids.size()!=left.transform_ids.size() ||
          a.final_transform.has_value()!=b.final_transform.has_value()))
        fail(context+"strict morph needs the same transform IDs and final-transform presence");
      bool fallback=false,discrete=false;
      for(std::size_t i=0;i<mapping.ids.size();++i) {
        const auto x=mapping.from[i],y=mapping.to[i];
        if(x>=0 && y>=0) {
          try { compatible_transform(a.transforms[x],b.transforms[y]); }
          catch(const std::exception &error) { fail(context+mapping.ids[i]+": "+error.what()); }
          if(data->clip.affine==AnimationAffine::polar &&
             (!can_polar(a.transforms[x].pre,b.transforms[y].pre)||!can_polar(a.transforms[x].post,b.transforms[y].post)))fallback=true;
          if(discrete_parameters_differ(a.transforms[x],b.transforms[y]))discrete=true;
        }
      }
      if(a.final_transform && b.final_transform) {
        compatible_transform(*a.final_transform,*b.final_transform);
        if(discrete_parameters_differ(*a.final_transform,*b.final_transform))discrete=true;
        if(data->clip.affine==AnimationAffine::polar &&
           (!can_polar(a.final_transform->pre,b.final_transform->pre)||!can_polar(a.final_transform->post,b.final_transform->post)))fallback=true;
      } else if(a.final_transform || b.final_transform) {
        const auto &present=a.final_transform?*a.final_transform:*b.final_transform;
        if(data->clip.affine==AnimationAffine::polar &&
           (!can_polar(present.pre,Affine{})||!can_polar(present.post,Affine{})))fallback=true;
      }
      if(fallback)data->diagnostics.push_back(context+"singular/ill-conditioned matrices or opposite reflection signs use linear affine interpolation");
      if(a.width!=b.width||a.height!=b.height||a.oversample!=b.oversample||a.palette_mode!=b.palette_mode||
         a.filter_radius.has_value()!=b.filter_radius.has_value()||
         a.density_estimator.has_value()!=b.density_estimator.has_value()||discrete)
        data->diagnostics.push_back(context+"discrete settings hold their left-keyframe value until the right endpoint");
      if(mapping.ids.size()!=left.transform_ids.size()||mapping.ids.size()!=right.transform_ids.size()||a.final_transform.has_value()!=b.final_transform.has_value())
        data->diagnostics.push_back(context+"unmatched branches fade by selection weight; new/lost Xaos edges fade from/to zero; a missing final transform uses identity");
    }
    data->segments.push_back(std::move(mapping));
  }
  data_=std::move(data);
}
const AnimationClip &PreparedAnimation::clip() const noexcept {return data_->clip;}
const std::vector<std::string> &PreparedAnimation::diagnostics() const noexcept {return data_->diagnostics;}

AnimationFrame PreparedAnimation::evaluate(std::int64_t requested) const {
  const auto &clip=data_->clip;
  const auto frame=animation_frame_at(clip,requested);
  AnimationFrame output;
  output.frame=frame;output.seed=animation_seed(clip,frame);
  const auto after=std::upper_bound(clip.keyframes.begin(),clip.keyframes.end(),frame,
      [](std::uint32_t value,const AnimationKeyframe &key){return value<key.frame;});
  const auto left=after==clip.keyframes.begin()?0:static_cast<std::size_t>(after-clip.keyframes.begin()-1);
  output.left_keyframe=left;output.right_keyframe=left;
  if(after==clip.keyframes.end()||clip.keyframes[left].frame==frame||clip.keyframes[left].easing==AnimationEasing::hold) {
    output.flame=data_->models[left];return output;
  }
  const auto right=left+1;
  output.right_keyframe=right;
  const auto &a=data_->models[left], &b=data_->models[right];
  const auto &mapping=data_->segments[left];
  const auto t=animation_ease(clip.keyframes[left].easing,
      static_cast<double>(frame-clip.keyframes[left].frame)/(clip.keyframes[right].frame-clip.keyframes[left].frame));
  output.position=t;
  auto &result=output.flame;
  result=a;
  result.time=blend(a.time,b.time,t);result.quality=blend(a.quality,b.quality,t);
  result.center={blend(a.center.x,b.center.x,t),blend(a.center.y,b.center.y,t)};
  result.scale=blend(a.scale,b.scale,t);result.zoom=blend(a.zoom,b.zoom,t);
  result.rotate=clip.camera_angle==AnimationAngle::unwrapped?blend(a.rotate,b.rotate,t):angle(a.rotate,b.rotate,t,360);
  result.gamma=blend(a.gamma,b.gamma,t);result.gamma_threshold=blend(a.gamma_threshold,b.gamma_threshold,t);
  result.brightness=blend(a.brightness,b.brightness,t);result.vibrancy=blend(a.vibrancy,b.vibrancy,t);
  if(a.filter_radius&&b.filter_radius)result.filter_radius=blend(*a.filter_radius,*b.filter_radius,t);
  if(a.density_estimator && b.density_estimator) {
    const auto& from = *a.density_estimator;
    const auto& to = *b.density_estimator;
    result.density_estimator = DensityEstimator{blend(from.radius,to.radius,t), blend(from.minimum,to.minimum,t),
                                                blend(from.curve,to.curve,t)};
  }
  result.background=creative::blend_colors(a.background,b.background,t,creative::GradientSpace::rgb);
  result.curves=interpolate_color_curves(a.curves,b.curves,t);
  for(std::size_t i=0;i<result.palette.size();++i)
    result.palette[i]=creative::blend_colors(a.palette[i],b.palette[i],t,palette_space(clip.palette));
  result.transforms.clear();
  for(std::size_t i=0;i<mapping.ids.size();++i) {
    const auto x=mapping.from[i],y=mapping.to[i];
    Transform value;
    if(x>=0&&y>=0)value=transform(a.transforms[x],b.transforms[y],t,clip.affine);
    else if(x>=0) {value=a.transforms[x];value.weight=blend(value.weight,0,t);}
    else {value=b.transforms[y];value.weight=blend(0,value.weight,t);}
    value.chaos.clear();
    for(std::size_t j=0;j<mapping.ids.size();++j) {
      const auto u=mapping.from[j],v=mapping.to[j];
      const auto edge=[](const Transform &row,int destination) {
        return destination<0?0.0:(static_cast<std::size_t>(destination)<row.chaos.size()?row.chaos[destination]:1.0);
      };
      const auto from=x>=0?edge(a.transforms[x],u):0.0;
      const auto to=y>=0?edge(b.transforms[y],v):0.0;
      value.chaos.push_back(blend(from,to,t));
    }
    result.transforms.push_back(std::move(value));
  }
  if(a.final_transform&&b.final_transform)result.final_transform=transform(*a.final_transform,*b.final_transform,t,clip.affine);
  else if(a.final_transform)result.final_transform=transform(*a.final_transform,identity_final(*a.final_transform),t,clip.affine);
  else if(b.final_transform)result.final_transform=transform(identity_final(*b.final_transform),*b.final_transform,t,clip.affine);
  try {validate(result);}
  catch(const std::exception &error) {fail("frame "+std::to_string(frame)+" is invalid: "+error.what());}
  return output;
}
void PreparedAnimation::validateFrames(std::uint32_t first,std::uint32_t last,std::stop_token stop) const {
  if(first>last||last>=data_->clip.frame_count)fail("validation range is outside the animation");
  for(auto frame=first;;++frame) {
    stop_if_requested(stop);(void)evaluate(frame);if(frame==last)break;
  }
}
AnimationFrame evaluate_animation(const AnimationClip &clip,std::int64_t frame) {return PreparedAnimation(clip).evaluate(frame);}

AnimationClip make_animation_preset(const LegacyDocument &document,std::size_t selected_index,
    AnimationPreset preset,std::uint32_t frame_count,AnimationRate rate,std::uint64_t seed) {
  (void)animation_name(preset);
  AnimationClip clip;
  clip.frame_count=frame_count;clip.rate=rate;clip.seed=seed;
  clip.name=std::string(animation_name(preset));
  rate_and_count(clip);
  auto original=capture_animation_keyframe(document,selected_index,0);
  original.easing=AnimationEasing::linear;
  clip.keyframes.push_back(original);
  const auto source=document.render_model(selected_index);
  const auto add=[&](Flame model,std::uint32_t frame) {
    auto changed=document;
    changed.apply_model(selected_index,model);
    auto key=capture_animation_keyframe(changed,selected_index,frame,{},original.transform_ids);
    key.easing=AnimationEasing::linear;
    clip.keyframes.push_back(std::move(key));
  };
  if(frame_count>1&&preset!=AnimationPreset::still) {
    if(preset==AnimationPreset::camera_orbit) {
      auto target=source;target.rotate+=360;
      clip.camera_angle=AnimationAngle::unwrapped;clip.loop=AnimationLoop::repeat;
      add(std::move(target),frame_count-1);
    } else if(preset==AnimationPreset::zoom) {
      auto target=source;target.zoom+=1;
      clip.loop=AnimationLoop::ping_pong;
      add(std::move(target),frame_count-1);
    } else {
      const auto steps=std::min<std::uint32_t>(4,frame_count-1);
      for(std::uint32_t i=1;i<=steps;++i) {
        const auto frame=static_cast<std::uint32_t>((std::uint64_t(frame_count-1)*i+steps/2)/steps);
        auto target=source;
        target.palette=creative::rotate_palette(source.palette,static_cast<int>((256ull*i+steps/2)/steps));
        add(std::move(target),frame);
      }
      clip.loop=AnimationLoop::repeat;
    }
  }
  (void)PreparedAnimation(clip);
  (void)serialize_animation(clip);
  return clip;
}

AnimationClip make_morph_animation(const LegacyDocument &from,std::size_t from_index,
    const LegacyDocument &to,std::size_t to_index,std::uint32_t frame_count,AnimationRate rate,
    std::span<const std::string> from_ids,std::span<const std::string> to_ids,
    AnimationTopology topology,std::uint64_t seed) {
  if(frame_count<2)fail("a morph needs at least two frames");
  const auto a=from.render_model(from_index),b=to.render_model(to_index);
  if(from_ids.empty()!=to_ids.empty())fail("provide correspondence IDs for both morph endpoints");
  if(from_ids.empty()&&a.transforms.size()!=b.transforms.size())
    fail("different transform counts require explicit correspondence IDs on both endpoints");
  AnimationClip clip;
  clip.name="Morph";clip.frame_count=frame_count;clip.rate=rate;clip.seed=seed;clip.topology=topology;
  clip.keyframes={capture_animation_keyframe(from,from_index,0,{},from_ids),
                  capture_animation_keyframe(to,to_index,frame_count-1,{},to_ids)};
  (void)PreparedAnimation(clip);
  (void)serialize_animation(clip);
  return clip;
}

AnimationClip animation_from_timed_library(const LegacyDocument &document,AnimationTimeUnits units,
    AnimationRate rate,std::uint64_t seed) {
  if(units!=AnimationTimeUnits::frames&&units!=AnimationTimeUnits::seconds)fail("invalid legacy time units");
  if(document.entries().size()>animation_keyframe_limit)fail("timed library exceeds 256 keyframes");
  AnimationClip clip;clip.name="Imported timed library";clip.rate=rate;clip.seed=seed;
  rate_and_count(clip);
  std::size_t transform_count=0;
  for(std::size_t i=0;i<document.entries().size();++i) {
    const auto model=document.render_model(i);
    const auto position=units==AnimationTimeUnits::seconds?model.time*rate.numerator/rate.denominator:model.time;
    if(!std::isfinite(position)||position<0||position>animation_frame_limit-1)
      fail("legacy keyframe time is outside the supported frame range");
    const auto frame=static_cast<std::uint32_t>(std::llround(position));
    if(i&&frame<=clip.keyframes.back().frame)fail("legacy times must be ordered and round to distinct frames");
    if(!i)transform_count=model.transforms.size();
    else if(transform_count!=model.transforms.size())fail("timed library topology differs; capture frames with explicit correspondence instead");
    auto key=capture_animation_keyframe(document,i,frame);
    key.easing=AnimationEasing::linear;
    clip.keyframes.push_back(std::move(key));
  }
  if(clip.keyframes.empty())fail("timed library contains no keyframes");
  clip.frame_count=clip.keyframes.back().frame+1;
  (void)PreparedAnimation(clip);
  (void)serialize_animation(clip);
  return clip;
}
} // namespace apo
