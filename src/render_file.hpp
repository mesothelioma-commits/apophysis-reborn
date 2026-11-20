#pragma once
#include "render.hpp"
#include "image_stream.hpp"

namespace apo {
struct FileRenderProgress {
    std::string phase;
    std::uint64_t completed_samples = 0, total_samples = 0;
    unsigned completed_strips = 0, total_strips = 0;
    double elapsed_seconds = 0;
    std::string backend, device_name, fallback_reason;
};
struct FileRenderOptions {
    ImageExport image;
    // Empty uses temporary working storage, removed on completion or cancellation.
    // An explicit directory retains committed chunks for restart-safe resume.
    std::filesystem::path checkpoint_directory;
    bool resume = false;
    std::uint64_t disk_mib = 16384;
    unsigned strip_rows = 0; // Zero chooses a height within the working-memory budget.
    std::function<void(const FileRenderProgress&)> status;
    // Host-side publication guard, called after reconstruction immediately
    // before the atomic image replacement. Not part of rendering identity.
    std::function<void()> before_publish;
    bool embed_flame = false; // Include an editable single-flame recipe in PNG output.
};
struct RenderFilePlan {
    int width = 0, height = 0;
    unsigned strip_rows = 0, strip_count = 0;
    std::uint64_t host_bytes = 0, disk_bytes = 0;
};
struct FileRenderResult {
    std::uint64_t plotted = 0, invalid = 0;
    unsigned workers = 0;
    RenderBackend backend = RenderBackend::cpu;
    std::string device_name = "CPU", fallback_reason;
    double sampling_ms = 0, reconstruction_ms = 0;
    bool resumed = false;
};
RenderFilePlan plan_file_render(const Flame&, const RenderOptions&, const FileRenderOptions&);
FileRenderResult render_to_file(const Flame&, const RenderOptions&, const std::filesystem::path&,
                               const FileRenderOptions& = {});
// Checks the committed manifest and all referenced chunk headers/lengths.
// Full record checksums are verified when a checkpoint is resumed.
bool has_render_checkpoint(const std::filesystem::path&) noexcept;
} // namespace apo
