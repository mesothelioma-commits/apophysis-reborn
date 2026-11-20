#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <memory>
#include <span>
#include <string_view>

namespace apo {

enum class ImageFormat { bmp8, ppm8, ppm16, png8, png16, tiff8, tiff16 };

struct ImageExport {
    ImageFormat format = ImageFormat::png8;
    bool alpha = false;
};

std::string_view image_format_name(ImageFormat format);
std::string_view image_format_extension(ImageFormat format);
unsigned image_format_bits(ImageFormat format);
bool image_format_supports_alpha(ImageFormat format);
ImageFormat parse_image_format(std::string_view name);
ImageFormat image_format_for_path(const std::filesystem::path &path, unsigned bit_depth = 8);

// Conservative encoded-file bound, including format headers. Rejects
// dimensions, alpha combinations and classic TIFF/BMP lengths the selected
// format cannot hold.
std::uint64_t image_export_upper_bound(int width, int height, const ImageExport &options);

// Sequential top-down writer. The caller supplies tone-mapped, straight RGBA
// samples; RGB-only exports take RGB as already composited by the renderer.
// Finite channels are clamped to [0,1] and quantized directly at the selected
// depth. No intermediate RGB8 conversion or additional gamma transform occurs.
// This writer does not own/replace a destination file. Wrap it in write_atomic.
struct EditablePng;
class ImageStreamWriter {
  public:
    ImageStreamWriter(std::ostream &output, int width, int height, const ImageExport &options,
                      const EditablePng* editable = nullptr);
    ~ImageStreamWriter();
    ImageStreamWriter(const ImageStreamWriter &) = delete;
    ImageStreamWriter &operator=(const ImageStreamWriter &) = delete;

    void write_row(std::span<const std::array<double, 4>> rgba);
    void finish();

    // Does not include the caller's RGBA input row. Independent of image height:
    // TIFF index values are computed and emitted directly without an index array.
    static std::uint64_t working_bytes(int width, const ImageExport &options);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace apo
