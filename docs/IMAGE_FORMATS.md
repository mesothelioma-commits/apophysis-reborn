# Streaming image formats — 0.7.0

High-resolution file rendering writes completed rows in order. The exporter does
not allocate the final image in memory. It accepts the renderer's double-valued
RGBA row and quantizes each channel directly to the selected output depth.
Sixteen-bit output is not an eight-bit image expanded into a larger container.

| Format selection | Channels | Sample depth | Encoding |
| --- | --- | --- | --- |
| `bmp8` | RGB | 8 bits per channel | Uncompressed BGR storage, padded rows and a negative height for top-down order |
| `ppm8` | RGB | 8 bits per channel | Binary P6, maximum sample value 255 |
| `ppm16` | RGB | 16 bits per channel | Binary P6, maximum sample value 65535, most significant byte first |
| `png8`, `png16` | RGB or RGBA | 8 or 16 bits per channel | Noninterlaced PNG, Sub row filtering, zlib compression and checked chunk CRCs |
| `tiff8`, `tiff16` | RGB or RGBA | 8 or 16 bits per channel | Little-endian, uncompressed TIFF with interleaved samples and 32-row strips |

PNG and TIFF support transparency. BMP and PPM reject an alpha request so the
caller cannot accidentally discard the selected transparency. TIFF accepts
`.tif` and `.tiff`; extension lookup is case-insensitive. The bit-depth selector
accepts only 8 or 16. BMP supports 8-bit channels only.

The PNG representation follows the [W3C PNG specification](https://www.w3.org/TR/png-3/):
truecolor type 2 or truecolor/alpha type 6, network byte order for 16-bit
samples, and one zlib stream distributed across IDAT chunks. PPM follows
[Netpbm's P6 layout](https://netpbm.sourceforge.net/doc/ppm.html). The top-down BMP
header uses Microsoft's [negative-height BI_RGB convention](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-wmf/567172fa-b8a2-4d79-86a2-5e21d6659ef3).
Streaming CLI BMP output consequently has different row/header bytes from the
earlier bottom-up BMP writer while retaining the same decoded RGB8 image.
TIFF follows [Revision 6.0](https://www.itu.int/itudoc/itu-t/com16/tiff-fx/docs/tiff6.pdf),
with unsigned sample type, RGB photometric interpretation, top-left orientation
and `ExtraSamples=2` for straight alpha. Classic TIFF and BMP file-size/offset
limits are checked before writing; BigTIFF is not implemented.

## Color, precision and alpha

Input colors are the renderer's already tone-mapped output values. The writer
does not apply a second gamma curve or claim an ICC, sRGB, linear-light or HDR
color space that the render model has not established. PNG carries no invented
color-space chunks. TIFF carries no invented ICC profile or physical DPI.
Applications can make different display assumptions for untagged images. PPM
stores the same render values as the other formats; it does not convert them to
the PPM specification's nominal BT.709 transfer curve.

For RGB output, the renderer supplies the image already composited against its
chosen background. For RGBA output, the renderer supplies straight RGB and an
opacity channel; the writer does not premultiply color or composite a
background. Alpha is encoded at the same precision as RGB and receives no
additional gamma transform. Zero means transparent and the maximum sample
means opaque. The renderer defines the density-to-opacity mapping; the writer
preserves those supplied values.

Finite values are clamped to the range zero through one, multiplied by 255 or
65535, and rounded to the nearest integer with halfway values rounded upward.
NaN and infinity are errors. Quantization is performed directly from double
values, so fine gradients can contain more than 256 levels in a 16-bit file.
These are integer, tone-mapped images; they are not floating-point scene-linear
EXR exports.

## Memory and completion

`ImageStreamWriter::working_bytes` reports a conservative encoding allowance
independent of image height. It covers one encoded row and fixed overhead; PNG
also reserves 512 KiB for its zlib state, a 64 KiB compressed buffer and related
allocation overhead. The caller budgets its double-valued RGBA row separately.
TIFF strip offsets are derived from the known geometry and emitted directly,
so no growing strip-index array is kept in memory. The PNG compressor uses the
documented [zlib streaming interface](https://www.zlib.net/manual.html), with a
32 KiB window and memory level eight.

`image_export_upper_bound` supplies the planner with exact BMP/TIFF sizes and
conservative PPM/PNG bounds. Compression makes the actual PNG file size
content-dependent. The bound includes row and chunk framing and avoids using
a 32-bit zlib length for a complete large image.

The writer checks the row width, total row count, dimensions, format limits and
stream errors. `finish()` succeeds only after every row has been supplied and
the output stream has completed. Destruction does not silently finish a
partial file. The render-to-file layer wraps the writer in atomic destination
replacement and checks cancellation between rows. A canceled or failed render
must not replace a previously completed destination.

## Executed format checks

`image-stream-tests` independently parses all eleven RGB/RGBA format/depth
combinations. Its 1025 × 35 fixtures exercise multiple TIFF strips, PNG data
larger than the compression buffer, nonuniform alpha and fine 16-bit samples.
Every decoded sample is compared to the fixture's expected integer value. The
tests also cover a single-strip TIFF, PNG chunk CRCs and complete zlib stream
decoding, byte order, row padding, malformed requests, incomplete/extra rows,
nonfinite values, finite clamping and short output writes. All eleven
combinations pass.

The same files were fully decoded by Pillow and ImageMagick 7.1.1-43 Q16.
ImageMagick's raw output matched every expected color and alpha sample,
including the exact 16-bit values. This codec test is separate from the full
16K fractal-render acceptance recorded in [TEST_PLAN.md](TEST_PLAN.md).

To reproduce the fixtures from a built test executable:

```bash
./image-stream-tests --write-fixtures out/image-formats
identify out/image-formats/*
```

ImageMagick/Pillow are independent verification tools, not application runtime
dependencies. The application requires zlib for PNG compression; the Windows
runtime already carries its zlib dependency alongside Qt.

### Full 16K decoding

The CPU acceptance fixture was rendered at 16,384 × 16,384 with 8,388,611
requested samples, PNG16 and straight alpha. A separate bounded-row Python
decoder checked all 9,415 PNG chunk CRCs, the complete zlib stream/Adler check,
every Sub-filtered row, dimensions, sample depth and exact end of file.
[ImageMagick's row-streaming decoder](https://imagemagick.org/stream/) independently
decoded the complete file. The two paths produced the same SHA-256 over all
2,147,483,648 bytes of network-order RGBA16:

```text
eede6bc849443ac0c04e697a113cb778770b03f320feda5df0adf6cdc4924e8d
```

| Channel | Distinct encoded values | Minimum | Maximum |
| --- | ---: | ---: | ---: |
| Red | 52,564 | 0 | 65,535 |
| Green | 52,701 | 0 | 65,535 |
| Blue | 46,824 | 0 | 65,535 |
| Alpha | 227 | 0 | 65,535 |

This particular flame/sample budget produces a relatively small set of opacity
levels. Its 30,410,278 partially transparent pixels still use values that are
not multiples of 257, so those levels are not expanded eight-bit alpha. The
remaining pixels include 230,449,365 fully transparent and 7,575,813 fully
opaque pixels. All zero-alpha pixels have zero RGB. The independent synthetic
format fixture separately exercises a much wider range of exact 16-bit alpha
values.

The acceptance preview is a 1024 × 1024, 16-by-16 box average of the decoded
full-resolution image composited over black. The 1024 × 1024 detail is a native
1:1 crop beginning at (7680, 7680), also composited over black. Neither image
is an upsampled substitute for the full render. This deliberately modest sample
budget demonstrates export integrity and precision; the native crop remains
sparse and is not a final-quality sampling claim. Rendering memory and
different-strip/resume comparisons are recorded separately in
[TEST_PLAN.md](TEST_PLAN.md).
