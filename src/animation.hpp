#pragma once

#include "legacy_document.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace apo {

enum class AnimationLoop { once, repeat, ping_pong };
enum class AnimationEasing { hold, linear, smoothstep, smootherstep };
enum class AnimationAffine { linear, polar };
enum class AnimationPalette { rgb, hsv_shortest, linear_rgb, hsl_shortest };
enum class AnimationAngle { shortest, unwrapped };
enum class AnimationTopology { strict, fade_unmatched };
enum class AnimationSeedMode { fixed, per_frame };
enum class AnimationPreset { still, camera_orbit, zoom, palette_cycle };
enum class AnimationTimeUnits { frames, seconds };

struct AnimationRate {
  std::uint32_t numerator = 30, denominator = 1;
  bool operator==(const AnimationRate &) const = default;
};

struct AnimationKeyframe {
  std::uint32_t frame = 0;
  std::string name;
  // The complete exact source collection, with the selected entry identified
  // separately. A keyframe does not normalize or discard unrelated source data.
  std::string library_bytes;
  std::size_t selected_index = 0;
  // Stable correspondence identities, one per ordinary transform in source
  // order. Reusing an identity explicitly declares the transforms correspond.
  std::vector<std::string> transform_ids;
  // Exact native identity captured with the snapshot; empty for built-ins only.
  // Re-capture explicitly after a trusted module/platform change before a new
  // evaluation. This is not an enablement permission or a module installer.
  std::string plugin_identity;
  AnimationEasing easing = AnimationEasing::smoothstep; // Outgoing segment.
  bool operator==(const AnimationKeyframe &) const = default;
};

struct AnimationClip {
  static constexpr unsigned current_version = 1;
  unsigned version = current_version;
  std::string name = "Animation";
  AnimationRate rate;
  std::uint32_t frame_count = 121; // Exportable frames are 0 through count-1.
  AnimationLoop loop = AnimationLoop::once;
  AnimationAffine affine = AnimationAffine::polar;
  AnimationPalette palette = AnimationPalette::rgb;
  AnimationAngle camera_angle = AnimationAngle::shortest;
  AnimationTopology topology = AnimationTopology::strict;
  AnimationSeedMode seed_mode = AnimationSeedMode::fixed;
  std::uint64_t seed = 123456;
  std::vector<AnimationKeyframe> keyframes;
  bool operator==(const AnimationClip &) const = default;
};

inline constexpr std::size_t animation_payload_limit = 8 * 1024 * 1024;
inline constexpr std::size_t animation_keyframe_limit = 256;
inline constexpr std::size_t animation_transform_limit = 256;
inline constexpr std::uint32_t animation_frame_limit = 1'000'000;

class AnimationCancelled : public std::runtime_error {
public:
  AnimationCancelled() : std::runtime_error("Animation operation stopped") {}
};

// Strict versioned XML; input parsing never loads native modules. Parsing and
// serialization validate structure/source XML, not renderability. An unknown
// version/payload can remain opaque in ProjectFile.animation_data unchanged.
bool is_animation_payload(std::string_view bytes) noexcept;
AnimationClip parse_animation(std::string_view bytes);
std::string serialize_animation(const AnimationClip &clip);
void validate_animation_structure(const AnimationClip &clip);

// Factory captures exact source bytes. Empty ids assigns transform-1, ... to a
// newly captured frame. Reusing those IDs for another flame is an explicit
// positional correspondence choice by the caller, not an automatic matcher.
AnimationKeyframe capture_animation_keyframe(
    const LegacyDocument &document, std::size_t selected_index,
    std::uint32_t frame, std::string name = {},
    std::span<const std::string> ids = {});

std::uint32_t animation_frame_at(const AnimationClip &clip, std::int64_t frame);
double animation_seconds(const AnimationClip &clip, std::uint32_t frame);
double animation_duration(const AnimationClip &clip);
std::uint64_t animation_seed(const AnimationClip &clip, std::uint32_t frame);
double animation_ease(AnimationEasing easing, double position);

struct AnimationFrame {
  Flame flame;
  std::uint32_t frame = 0;
  std::uint64_t seed = 0;
  std::size_t left_keyframe = 0, right_keyframe = 0;
  double position = 0;
};

// Preparation parses each render model once, validates correspondence/producer
// and plugin identity compatibility, and retains immutable native module owners.
// It does not execute native callbacks or render samples. Evaluation returns
// exact parsed models at keyframe positions and validates every interpolated
// model. Call validateFrames for the complete output range before publication.
class PreparedAnimation {
public:
  explicit PreparedAnimation(AnimationClip clip, std::stop_token stop = {});
  const AnimationClip &clip() const noexcept;
  const std::vector<std::string> &diagnostics() const noexcept;
  AnimationFrame evaluate(std::int64_t frame) const;
  void validateFrames(std::uint32_t first, std::uint32_t last,
                      std::stop_token stop = {}) const;

private:
  struct Data;
  std::shared_ptr<const Data> data_;
};

AnimationFrame evaluate_animation(const AnimationClip &clip, std::int64_t frame);
AnimationClip make_animation_preset(
    const LegacyDocument &document, std::size_t selected_index,
    AnimationPreset preset, std::uint32_t frame_count = 121,
    AnimationRate rate = {}, std::uint64_t seed = 123456);

// Explicit maps on both sides make arbitrary topology decisions reviewable.
// Empty maps are accepted only for equal topology and mean positional pairing.
// Distinct IDs can be used with fade_unmatched to fade different branches.
AnimationClip make_morph_animation(
    const LegacyDocument &from, std::size_t from_index,
    const LegacyDocument &to, std::size_t to_index,
    std::uint32_t frame_count = 121, AnimationRate rate = {},
    std::span<const std::string> from_ids = {},
    std::span<const std::string> to_ids = {},
    AnimationTopology topology = AnimationTopology::strict,
    std::uint64_t seed = 123456);

// Time conversion is explicit: stored legacy time has no universal AV unit.
// Duplicate rounded frames, negative/nonfinite times and missing zero-start
// keyframes are rejected rather than silently retimed or reordered.
AnimationClip animation_from_timed_library(
    const LegacyDocument &document, AnimationTimeUnits units,
    AnimationRate rate = {}, std::uint64_t seed = 123456);

std::string_view animation_name(AnimationLoop value);
std::string_view animation_name(AnimationEasing value);
std::string_view animation_name(AnimationAffine value);
std::string_view animation_name(AnimationPalette value);
std::string_view animation_name(AnimationAngle value);
std::string_view animation_name(AnimationTopology value);
std::string_view animation_name(AnimationSeedMode value);
std::string_view animation_name(AnimationPreset value);

} // namespace apo
