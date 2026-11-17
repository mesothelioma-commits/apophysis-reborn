# Verification record

## 0.3.0 Legacy Compatibility — 17 November 2026

All nine CTest entries passed in a Release build on x86-64 Linux (GCC 14.2.0, CMake 4.4.5) and a second Debug build using AddressSanitizer and UndefinedBehaviorSanitizer. Leak detection was disabled in the sanitizer run because the host tracer prevents LeakSanitizer from starting. The Windows x64 CLI and four test executables were cross-compiled and their PE headers and import tables inspected. Native Windows 11 execution and reopening in the original Windows editors remain pending.

| Check | Result on this checkpoint |
| --- | --- |
| Legacy document layer | Byte-identical untouched save. Targeted edits to name, size, center and scale. Coverage for XML comments, history, unknown settings, sibling entries, alternate encodings, failed edits and malformed input. |
| Public CLI | Mixed library inspection, save and in-place edit, invalid operations, independent entry rendering and canonical export. The original and the standalone export of Fox's 2.08 3D Hack flame rendered identically at a fixed seed across one and two workers. |
| Published files | All 19 entries in five historical documents passed preservation, edited-name and reopen checks. Fourteen rendered. Five AV entries carried specific diagnostics and could still be edited and saved. [Corpus records](LEGACY_CORPUS.md) |
| Original producer pixels | Five 8 × 6 controlled histograms reconstructed by original 2.09 and 7X Pascal methods. Reborn matched all 720 RGB channel values under both producer labels. [Producer verification](PRODUCER_ORACLE.md) |
| Original producer fields | Original 2.09 Pascal routines read the new dimensions, center and scale, and the same 768 palette bytes from edited and exported XML. The test included an authentic 2.08 entry. |
| Artist-published images | Full-frame AV render at 800 × 800 had mean absolute RGB error 1.214890/255 and intensity correlation 0.988834. Against a 7X editor screenshot viewport the error was 12.181279/255 and correlation was 0.689425 at 764 × 549. Correlation rose to 0.983465 on a 96 × 69 box-downsample. [Methods and source images](LEGACY_CORPUS.md) |
| Existing FLAM3 reference | 126 numerical vectors matched. The original external image check measured RGB MAE 4.832694/255, RMSE 6.465777/255 and mean-channel intensity correlation 0.984265. |
| Example studies | Rendered all 21 from the public CLI at 350,000 samples, seed 123456, two workers. Pillow decoded every 480 × 360 image. Each was nonuniform and the renderer reported zero invalid orbit steps. |

**Additional cross-renderer comparison.** The upstream FLAM3 executable and Reborn rendered the first untouched *Spherical* entry from the publisher's 2.08 collection. At 1500 × 1000, RGB MAE was 19.462591/255, RMSE 25.015175/255 and pixel intensity correlation 0.878334. This measures the difference between two different renderers' interpretations of an authored file. It has no pass threshold. FLAM3 used its native defaults with `nthreads=1 bits=64 format=ppm seed=27 isaac_seed=27 qs=8`. Reborn used seed 27, 12,000,000 samples and four workers, reading directly from the collection. The PPM SHA-256 digests were `f825374c18eacb1cf7e3f2b5efbabe78ba589f343fe640e815c6fa880c5610c2` for FLAM3 and `c2b650dff66c87d4a7769fbf18e0c1010e28572c3a56649ca9a220fca1b580ef` for Reborn.

Run `bash scripts/build.sh` for a native source build and the nine CTest entries. The separate historical corpus uses the commands in LEGACY_CORPUS.md. Git Bash on Windows runs the packaged executable checks with `bash scripts/verify-windows.sh`.

## 0.2.0 Core Engine — 17 November 2026

Environment: Debian 13, x86-64, GCC 14.2.0, CMake 4.4.5. The host reported Intel Xeon Platinum 8573C and nine schedulable CPUs. Windows targets cross-compiled separately with LLVM-MinGW. Reproduce the release checks with `bash scripts/build.sh` from a source checkout.

| CTest entry | Executed checks | Linux Release |
| --- | --- | --- |
| flam3_reference | 126 upstream numeric vectors, transform selection/chaos boundaries, independently generated full-raster FLAM3 regression | Passed |
| xml_import | Single/collection XML, defaults, matrices, packed palettes, wrapped color stops, malformed files, unsupported fields, semantic save/reopen | Passed |
| renderer | Exact projected point positions, final-transform isolation, opacity, palette interpolation, box filtering, singular points, worker determinism, sample and budget limits | Passed |
| cli_roundtrip | Same-path replacement, filenames with spaces, BMP header and length, seeded one/four-worker equality, preservation of a good file on invalid options | Passed |
| foundation_cli | Original positional command, 64 × 64 PPM, 100,000 samples | Passed |
| flame_file_cli | Ember Spiral from XML, 768 × 576 BMP, 100,000 samples | Passed |

All six also passed an independent Debug build with AddressSanitizer and UndefinedBehaviorSanitizer, GCC warnings enabled. LeakSanitizer could not run under the host tracer, so only leak detection was disabled for that run. Pillow independently decoded BMP and PPM outputs.

The 15 members of `examples/variation-studies.flame` were rendered using the CLI, seed 123456, 350,000 samples and two workers each. Every output decoded at 480 × 360. Each had visible variation geometry and zero non-finite steps. The engine also completed Ember Spiral at its declared quality, 7,962,624 samples. All requested samples plotted in that view.

The 192 × 144 external reference fixture measured mean RGB error 4.832694/255, RGB RMSE 6.465777/255 and mean-channel intensity correlation 0.984265 at 3,317,760 requested samples. See REFERENCES.md for exact inputs, independent output, definitions and thresholds.

### 0.2.0 local timing

Ember Spiral, 768 × 576, 7,962,624 samples, seed 123456, release executable. Wall time from process launch through file close, three runs per configuration on the same shared Linux host.

| CPU workers | Wall times (ms) | Median (ms) |
| ---: | --- | ---: |
| 1 | 1,064 / 1,072 / 1,038 | 1,064 |
| 4 | 357 / 357 / 343 | 357 |

Both outputs had identical bytes. BMP SHA-256: `82133ca765ddd8440d495c383c26da79c26af6185d3a7c76a273e9f14f22235e`. Timings characterize this flame on this host. Platform math libraries can change the last bits and eventually stochastic orbits. The Windows verification script compares two Windows outputs to each other.

### Open at the end of 0.2.0

- Execute the included x64 binaries on Windows 11 using `bash scripts/verify-windows.sh`. The script captures local test output and hashes in `logs/`. PE headers and imported DLL names were inspected here. This Linux host could not launch Wine's IPC server.
- Expand XML and render fixtures using authentic 2.x, 7X and AV applications and their files. Exercise original readers on Reborn exports and document unknown-field handling.
- Verify editor lifecycle, settings, queue/cancellation, GPU equivalence and image quality on actual hardware when those components exist.
- Perform the separate native 16,384 × 16,384 acceptance test following implementation of memory-aware high-resolution rendering. No 16K acceptance test was attempted in this checkpoint.
