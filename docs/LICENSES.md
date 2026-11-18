# Dependency and reference notices

The 0.7.0 desktop distribution dynamically links Qt 6.8.2, the C++ runtime, and the font/text libraries selected by the deployed DLL dependency closure. Expat remains statically linked. The Qt runtime notices and exact package hashes are under `licenses/qt`. Complete corresponding LGPL source archives, including MSYS2 recipes and patches, accompany the distribution in `third-party-sources`. Rebuilding and relinking instructions are in [QT_BUILD.md](QT_BUILD.md). [Qt licensing](https://www.qt.io/licensing/open-source-lgpl-obligations)

The earlier 0.2.0 and 0.3.0 Windows console binaries linked Expat 2.10.0 and the LLVM-MinGW C++/threading runtime statically. The distribution includes the upstream notices under `licenses/windows/`, including Expat, LLVM, compiler-rt, libc++, libc++abi, libunwind, MinGW-w64, and winpthreads. Native source builds use the Expat installation selected by CMake. See WINDOWS_BUILD.md for exact build inputs and hashes.

FLAM3 by Scott Draves and contributors is the independent numerical and image oracle. The packaged CPU renderer builds from the C++ files in src. The optional numeric-vector regeneration script reads a separately obtained FLAM3 source tree into a temporary test build. The selected spherical coefficients in the two test fixtures originate in FLAM3's test.flam3. The reference image was rendered by the upstream executable from the adapted fixture, whose camera and embedded palette are recorded in REFERENCES.md. Upstream licensing text and attribution accompany the fixtures in `tests/reference-FLAM3-LICENSE.txt` and `docs/REFERENCES.md`. [Upstream FLAM3](https://github.com/scottdraves/flam3)

The Apophysis AV corpus spot check uses a separately published flame, identified in LEGACY_CORPUS.md. Neither the original artwork nor the reduced private test copy is bundled. The project-wide audit for legacy plugin, palette, UI and other asset reuse remains open.


The 0.3.0 historical corpus records asset-specific provenance in tests/legacy/ATTRIBUTION.md and docs/LEGACY_CORPUS.md. The Fox parameter listing and the new rendered preview carry CC BY-SA 4.0 attribution in tests/legacy/ATTRIBUTION.md. External artist images and the original application release archives remain available from their publishers. The optional producer-oracle regeneration harness reads a separately obtained, hash-checked Apophysis Pascal source tree. Its controlled output files, source references and test scope are described in docs/PRODUCER_ORACLE.md.


The 0.5.0 extended variation oracle generates vectors by compiling hash-checked original FLAM3 C functions and Apophysis 2.09 Pascal methods in temporary builds. The pinned references, original numerical conventions and generator are documented in VARIATIONS.md. The ten new sample flames and seven bundled palette presets are new Reborn examples; no historical palette catalog or third-party artwork was copied for this increment. GPL palette exchange follows GIMP’s published file-format specification linked in CREATIVE_ALGORITHMS.md.

## Vulkan headers and development tools in 0.6.0

`third_party/vulkan` contains 13 unmodified official C headers from Khronos Vulkan-Headers **v1.4.309**: the core/API/platform headers and the video-codec headers referenced by the core interface. Their copyright notices and **Apache-2.0** SPDX identifiers remain intact. The upstream repository also contains MIT-licensed files; its license notice and both complete license texts are retained, along with source provenance and checksums. [Included provenance](../third_party/vulkan/README.md) · [Upstream pinned source](https://github.com/KhronosGroup/Vulkan-Headers/tree/v1.4.309)

The application dynamically loads the operating system's Vulkan loader. The portable Windows package does not bundle a Vulkan loader, vendor GPU driver, Mesa software driver or CUDA runtime. CPU rendering remains available without them. The shader source and generated SPIR-V header accompany the application source; unchanged source builds do not need a Vulkan SDK or shader compiler. The included regeneration script uses separately installed glslang and SPIRV-Tools.

Development verification used Debian packages for glslang, SPIRV-Tools, the Vulkan tools/validation layer and Mesa llvmpipe. These are external development tools rather than shipped runtime DLLs. Package identities, checksums and the software-device classification are recorded with verification evidence. The shader's PCG-style integer stream and its exact constants/conversion are documented in [GPU_KERNEL.md](GPU_KERNEL.md). The earlier historical variation formulas and their oracle references retain the attribution and scope described above. [GPU comparison methods](GPU_TESTING.md) · [Windows verification scope](WINDOWS_GPU.md)

## Streaming image output in 0.7.0

The core now links zlib for bounded PNG compression. The Windows package uses the same dynamic zlib 1.3.1 runtime already present in the Qt dependency closure; its publisher notice is retained under `licenses/qt/zlib`. Native builds use the zlib installation found by CMake. Expat, zlib and Qt development files are required for the full source build. The PNG/TIFF/BMP/PPM encoders are implemented in the included C++ source; the formats and independent decoding checks are documented in [IMAGE_FORMATS.md](IMAGE_FORMATS.md).

The 16K sample is a new render of the bundled Reborn Ember spiral example. Its preview is a measured downsample of the decoded output, with no exposure adjustment. ImageMagick and Pillow were external validation tools, not bundled Windows runtime dependencies. Their executed decoding scope is recorded in [HIGH_RES_ACCEPTANCE.md](HIGH_RES_ACCEPTANCE.md).
