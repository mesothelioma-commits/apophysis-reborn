# Verification record

## 0.6.0 GPU Rendering — 17 November 2026

The final Linux Release build passed all **20 CTest entries** in 3.42 seconds. The separate Debug build passed the same **20 entries** with AddressSanitizer and UndefinedBehaviorSanitizer in 18.37 seconds. These runs include the final renderer-description layout fix, use GCC 14.2.0 and Qt 6.8.2, and enable `VK_LAYER_KHRONOS_validation`. No Vulkan validation errors or sanitizer errors were reported. Sanitizer options remain `ASAN_OPTIONS=detect_leaks=0` and `UBSAN_OPTIONS=halt_on_error=1`; this is not a LeakSanitizer result.

**Execution boundary.** This host exposes no physical GPU. Actual Vulkan dispatches ran through Mesa 25.0.7-2+deb13u1, reporting **llvmpipe (LLVM 19.1.7, 256 bits)**, device type CPU and Vulkan 1.4.305. The loader and Khronos Validation Layers are 1.4.309. These results verify the Vulkan implementation on a software device. Physical GPU correctness, vendor-driver behavior, speedup and Windows execution remain pending.

| Added or expanded check | Executed behavior |
| --- | --- |
| `gpu_kernel_cpu` | Packed-model layout and limits, FP32 admission, preservation of transform/Xaos CDF intervals and 16,384 portable random draws. |
| `gpu_kernel` | **1,560 direct CPU/Vulkan transform probes** covering all 31 variations, signed weights, generic/classic/7X conventions, pre/post matrices, stochastic draw order, repeated pre-blur, mixed term order and the final-transform slot. Maximum normalized coordinate error was **1.7820791e-05**, below the fixed **2e-4** bound; subsequent random draws matched exactly. [Kernel methods](GPU_KERNEL.md) |
| `gpu` | Five full-image comparisons with the matching-stream double-precision CPU reference, original CPU golden continuity, repeated-render determinism, seed changes, monotonic progress, cancellation, explicit device selection, host/GPU limits, unsupported FP32 ranges and visible CPU fallback. All five tested images had equal plotted/invalid counts and zero RGB8 difference. [Fixtures and fixed bounds](GPU_TESTING.md) |
| `gpu_cli` | Text/JSON discovery, default/explicit CPU identity, fallback diagnostics, strict missing-device and memory errors, actual Vulkan file output, malformed options and preservation of an existing destination after failure. |
| `preview` | Requested backend propagation and actual-backend reporting alongside the existing progressive rendering, replacement, cancellation and output guarantees. |
| `editor_workflow` | Renderer/device/budget/fallback controls, persistence and separate export choices. A regression exercises **CPU → Vulkan → CPU** in both Preferences and Render Image, checking full wrapped descriptions, visible buttons and retained dialog size. |
| Earlier checks | All existing creative, editor, document, import, numerical, historical-producer, image and CLI checks continue to pass. |

**CPU-only build.** A separate core build with `APO_ENABLE_VULKAN=OFF` and `APO_BUILD_EDITOR=OFF` passed **13 checks**; its **two actual-compute checks were recorded as skipped**, for 15 CTest entries total. The discovery/fallback CLI checks and portable kernel packing/random tests still ran. The suite completed in 0.62 seconds. A skip does not count as Vulkan execution. Independent missing-device checks also confirm that strict Vulkan mode fails while the ordinary mode reports CPU fallback.

**Native desktop interaction.** The running Linux desktop rendered a two-million-sample Vulkan preview, switched to the CPU reference and back, and displayed the actual backend after each completed render. The Gradient editor inherited Vulkan rendering, reversed the palette and applied it. Saving through the native file dialog produced `examples/gpu-palette-study.flame`; independent CLI inspection marked the saved entry renderable. The manual CPU-to-Vulkan Preferences transition exposed clipped explanatory text. The corrected build was reopened and the same transition verified with all device/precision text and buttons visible; the Vulkan preview then completed successfully.

**Strict native smoke workflow.** The final `--gpu-smoke-test DIR --device 0` run completed with fallback disabled and reported `backend: Vulkan`, software device identification and an empty fallback reason. It produced all five required files: `editor.png`, `preview.png`, `saved-example.flame`, `render-device.json` and `render-settings.png`. Both window screenshots were visually inspected. The final screenshots are included under `preview`; the complete smoke output and log accompany the distribution under `verification/gpu-smoke`. [Renderer controls and smoke mode](GPU_RENDERING.md)

**Shader reproducibility.** The checked-in SPIR-V header matched a fresh compilation of `shaders/orbit.comp` by glslang reporting 15.2.0 and passed SPIRV-Tools 2025.1 validation for Vulkan 1.0. Ordinary application builds use the embedded header and vendored Vulkan-Headers v1.4.309; they do not need the Vulkan SDK or a shader compiler. Development toolchain package identities and hashes are retained with the verification evidence.

**Windows build and runtime audit.** The final x86-64 build compiled and linked all **15 executables** after the UI fix. The editor uses the Windows GUI subsystem. Its portable runtime contains **48 files**, including **46 PE modules**. The audit resolved **13,232 non-system imports**, with zero missing. Every staged executable matches its build output, and all **90** recorded source/test/shader/header fingerprints match the final inputs. The system Vulkan loader is loaded dynamically and is not bundled. These Windows programs were compiled and audited, not executed.

The distribution's `verification` directory retains the final native, sanitizer, CPU-only, Windows, shader, numerical and smoke evidence. The package is also checked by extracting its source, building it afresh, running CTest, restoring the Git bundle and validating every file checksum; the result of that distribution check is recorded in `BUILD.txt` and `verification/distribution-verification.json`.

**Remaining acceptance and next milestone.** The strict [Windows GPU workflow](WINDOWS_GPU.md) records and tests one selected device without accepting a CPU fallback as GPU success. Physical NVIDIA/AMD/Intel execution, Windows 11 UI/DPI behavior and performance measurement are pending. CUDA is optional future work and is not implemented here. Milestone 7 adds memory-aware high-resolution rendering, tiled processing, queues/checkpoints and higher-bit-depth output; native 16K acceptance is not claimed for this checkpoint. Earlier sections below retain the results and limits of their own milestones.

## 0.5.0 Creative Tools — 17 November 2026

The final Linux Release build passed all **16 CTest entries** with GCC 14.2.0 and Qt 6.8.2 in 1.01 seconds. A separate Debug build with AddressSanitizer and UndefinedBehaviorSanitizer passed the same 16 entries in 9.98 seconds. Its options were `ASAN_OPTIONS=detect_leaks=0` and `UBSAN_OPTIONS=halt_on_error=1`; this does not claim a LeakSanitizer result. The distribution's `verification` directory retains the executed test logs and Windows build/runtime records.

| Added or expanded check | Executed behavior |
| --- | --- |
| `creative` | Palette interpolation and validation, neutral adjustments, rotation/reversal/smoothing, GPL exchange, seeded generation, variation constraints, symmetry, mutation groups and zero-strength identity, finite-orbit framing and failed-fit rollback. |
| `extended_variations` | All 640 independently generated original-source vectors for the ten new variations, named and numbered import, save/reopen, producer registry differences, affine coupling, signed weights, random-state preservation and ten nonuniform sample renders with zero invalid steps. [Methods and pinned sources](VARIATIONS.md) |
| `creative_editor` | RGB precision, gradient history/reset, stop building and invalid-input rollback, palette/flame import with Unicode and spaced paths, adjustment validation/reset/aspect/filter behavior, source-preserving Apply/undo, locked/unlocked random seeds, batch insertion, zero-strength mutation, branch history and dialog destruction during pending input/generation. The default candidate grid also checks complete visibility of the first row. |
| `editor_workflow` and `document_session` | Creative actions and modal Cancel/Apply, saved mutation branches, collection append/duplicate/delete, restored selection on undo/redo, and invalid-entry-selection rollback. |
| `legacy_document` | Palette replacement across historical encodings and representations, retained comments/history/siblings, source aliases, adjustment validation, append/duplicate/delete and byte-preserving rollback. |
| Earlier checks | All prior FLAM3 vectors, producer-pixel fixtures, XML import, rendering, CLI, preview, canvas and editor workflows continue to pass. |

**Native desktop interaction.** On the actual Linux Qt desktop, the Gradient editor applied Glacier, reversed the palette, undid that operation and committed the result. The Adjustment editor changed gamma to 2.6 and committed it. The Mutation explorer selected candidate 4, used it as a new parent, selected a descendant and added its branch history. Saving through the native file dialog produced the three-entry `examples/creative-branches.flame`; independent CLI inspection marked all three entries renderable. A subsequent launch verified that the corrected default 1280 × 790 explorer shows all nine candidate tiles without resizing its dividers.

**Creative smoke workflow.** The final `--creative-smoke-test DIR` run rendered the main editor and all four creative dialogs and exited successfully after producing all 12 required files: five window screenshots, a preview PNG, five flame files and a GPL palette. Every screenshot decoded. The nine entries in the saved random batch were independently inspected as renderable and are included as `examples/seeded-random.flame`. The current dialog screenshots are in `preview`; [the creative guide](CREATIVE_TOOLS.md) explains their controls.

**Windows build and runtime audit.** The final x86-64 build compiled and linked all **13 executables**. Its editor uses the Windows GUI subsystem. The staged runtime contains **46 files**, including **44 PE modules**. Inspection resolved **12,922 non-system imports** against the exact packaged exports, with zero missing. Every staged executable matched its build output, and recorded fingerprints match all 39 source, test and resource files. The Windows programs were compiled and audited; they were not executed.

**Pending acceptance.** Windows 11 execution, DPI/multiple-monitor behavior and historical-editor reopening remain pending. The new deterministic random and mutation workflows do not claim historical generator sequence equivalence. Broader palette formats/catalogs, alpha output, non-default historical reconstruction curves, density estimation and additional variations remain open. GPU acceleration is Milestone 6; tiled output and native 16K acceptance remain subsequent work. The older milestone sections below are historical records of their own scope.

Use `bash scripts/build-editor.sh` for a native rebuild and all 16 CTest entries. On Windows, `bash scripts/verify-editor-windows.sh` runs the packaged core/Qt tests and the creative smoke workflow. Its outputs and exact filenames are described in [QT_BUILD.md](QT_BUILD.md).

## 0.4.0 Primary Editor — 17 November 2026

The full Release build passed all 13 CTest entries on x86-64 Linux with GCC 14.2.0 and Qt 6.8.2. The Debug build with AddressSanitizer and UndefinedBehaviorSanitizer also passed all 13. Leak detection was disabled because the host tracer prevents LeakSanitizer from running. The editor was additionally exercised through the actual Linux desktop using the packaged native Qt runtime.

| New check | Executed behavior |
| --- | --- |
| `editor_workflow` | Open the built-in flame, wait for a rendered image, edit the name and an affine coefficient through Qt input events, undo/redo, save to a path with spaces, reopen, add/duplicate/delete/final/post operations, one undo command per drag, mixed supported/unsupported library selection, and pause/resume. |
| `canvas` | Origin and both basis-handle drags, snapping, Esc rollback, screen/world coordinate inversion, panning, anchored zoom, empty selection and coincident handles. Expected geometry accounts for the actual integral screen pixel delivered by the event. |
| `document_session` | Failed-change rollback, exact-byte undo, source encoding and CRLF preservation, save-clean state, external changes/deletions, relative paths, recovery ownership, lock rejection, adoption and cleanup. |
| `preview` | Increasing preview budgets, aspect/camera fitting, debounce, pause/resume, replacing active work without stale images, destruction during rendering, full-size file output, and cancellation that preserves an existing destination. |
| Expanded legacy document cases | Surgical transform edits, historical aliases, variation lists, duplicate probability/chaos behavior, reordering and deletion, implicit colors, final transforms, failed-operation rollback, UTF-16 in both byte orders. |
| Existing core reference and CLI checks | All nine earlier entries continue to pass, including the original Pascal image references and numerical FLAM3 vectors. |

**Desktop interaction.** The running native window rendered Ember Spiral. Changing coefficient a from 0.69 to 0.58 updated the numeric field, triangle and fractal. Undo restored 0.69. A subsequent triangle-origin drag changed the translation and refreshed the image. The file dialog saved the result, cleared the modified marker and showed its filename. Independent inspection of the saved XML confirmed that only the selected transform's coefficient attribute changed. The complete automated editor workflow also verifies save/reopen.

**Portable launch.** The relocated Linux runtime launched under the desktop's XCB platform. Its offscreen smoke mode generated a complete 1420 × 900 editor screenshot, a decoded preview PNG and a saved flame byte-identical to the built-in source. The source package includes that editor screenshot. The normal application now sizes its first window to the available screen.

**Windows build.** All ten executables compiled and linked for x86-64 Windows. The desktop program has the Windows GUI subsystem. A dependency closure inspection resolved 11,349 non-system imports against the actual packaged DLL exports with zero missing. The portable directory contains 43 executables, DLLs, plugins and metadata/configuration files. The runtime and source notices are in QT_BUILD.md and licenses/qt.

**Pending acceptance.** Windows 11 execution, DPI/multiple-monitor behavior on Windows, and historical-editor reopening remain unverified. Multi-selection, synchronization, persistent per-transform locks and customizable shortcut profiles remain open editor work. GPU, full palette/mutation tools, tiled 16K rendering and an installer belong to subsequent milestones.

Use `bash scripts/build-editor.sh` to rebuild the editor and run the source checks. The Windows package includes `bash scripts/verify-editor-windows.sh`. The source's `--smoke-test DIR` option opens the native editor, waits for its refined preview, writes the screenshot/preview/flame artifacts, and exits with a status code.

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
