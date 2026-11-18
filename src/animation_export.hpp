#pragma once
#include "animation.hpp"
#include "render_file.hpp"
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace apo {
inline constexpr std::size_t animation_export_frame_limit = 1000;

struct AnimationExportProgress {
  std::uint32_t frame = 0;
  std::size_t completed = 0, total = 0;
  std::string phase;
  std::optional<FileRenderProgress> render;
};

struct AnimationExportOptions {
  std::filesystem::path output_directory;
  // Empty chooses output_directory/.apo-sequence-PREFIX. It contains only
  // host-owned state; frame files are always outside the state directory.
  std::filesystem::path state_directory;
  std::string prefix = "frame";
  std::uint32_t first = 0, last = 0, step = 1; // Inclusive, zero-based range.
  // Zero/zero uses the first selected frame's canvas. Every frame is resized
  // to that fixed canvas with the same fit policy as the desktop renderer.
  int width = 0, height = 0;
  RenderOptions render;
  FileRenderOptions file;
  bool replace_existing = false, resume = false;
  std::vector<std::filesystem::path> protected_files;
  std::function<void(const AnimationExportProgress &)> progress;
};

struct AnimationExportFramePlan {
  std::uint32_t frame = 0;
  std::uint64_t seed = 0;
  std::filesystem::path destination;
  RenderFilePlan render;
  std::string model_sha256;
  bool completed = false;
};

struct AnimationExportPlan {
  std::filesystem::path output_directory, state_directory;
  int width = 0, height = 0;
  std::vector<AnimationExportFramePlan> frames;
  std::uint64_t output_bytes_upper_bound = 0, disk_bytes_upper_bound = 0;
  std::string identity_sha256;
};

struct AnimationExportResult {
  std::size_t completed = 0, rendered = 0, verified_existing = 0;
  std::filesystem::path manifest;
};

// Read-only full-range preflight. Existing completed outputs are verified on
// resume; no directory, file, manifest or checkpoint is created here.
AnimationExportPlan plan_animation_export(const PreparedAnimation &animation,
                                          const AnimationExportOptions &options);
// Images are individually atomic, not one multi-file transaction. Successful
// frames remain available after a later cancellation or failure. Resume binds
// the exact clip, frame models, plugin binaries and renderer settings and only
// skips completed files whose size and SHA-256 still match the saved manifest.
AnimationExportResult export_animation_sequence(
    const PreparedAnimation &animation, const AnimationExportOptions &options);
// The fixed-canvas policy shared by sequence planning and rendering.
Flame animation_export_flame(const AnimationFrame &frame, int width, int height);
} // namespace apo
