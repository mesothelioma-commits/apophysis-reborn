# Development log, changelog and handoff

## Foundation checkpoint

CMake project and single-threaded C++ experiment with a fixed linear IFS, four formulas, a PPM writer and one smoke test. Its source was imported as the first commit of the bundled repository.

## 0.2.0 — Core Engine

The new version reads render parameters from flame definitions. The model, registry, sampler, XML reader, canonical single-flame serializer, camera, color pipeline and exporters now have distinct code paths. The legacy positional CLI still renders the foundation example with the new engine. Tests cover numeric output, actual independently rendered pixels, collection parsing, seed replay between thread counts and output handling. The ZIP adds Windows x64 console builds plus one-command verification in Git Bash.

### Planned work at the end of 0.2.0

1. Start from the 0.2.0 source commit in BUILD.txt or clone the bundle. Run the six native CTest entries and the Windows verification script.
2. Assemble untouched fixtures and identify the producing release for original 2.x, 7X and AV. The adapted FLAM3 fixtures and the AV spot check have narrower coverage.
3. Track shared, producer-specific and unknown fields in a lossless editing model. Preserve unimplemented data through load/save, report what blocks accurate rendering, and test real historical binary round trips.
4. Add the next missing variations, palette catalog and filter/estimator behavior driven by those fixtures. Extend the reference suite before claiming visual compatibility for any additional family.
5. Complete the deferred foundation work on the Windows Qt shell, settings and desktop logging before Milestone 4 editor integration.

The compatibility matrix lists unsupported controls. Keep physical Windows runtime evidence and any future GPU or 16K acceptance evidence distinct from the source and image tests in this milestone.


## 0.3.0 — Legacy Compatibility

The CPU renderer can select a supported flame from a mixed collection. Source-preserving and canonical saves have separate CLI commands. The reader tracks producer semantics and preserves entries it cannot render. New regression evidence includes original publisher collections, an artist-published reference image, and controlled pixel comparisons against original Apophysis Pascal image-making methods.

### Continue with Milestone 4

Start from the 0.3.0 tag in BUILD.txt. Use LegacyDocument as the library and saving layer, its render_model method for the selected flame, and the existing CPU rendering API for previews. Keep the UI responsive by discarding stale preview jobs when the selection or a transform changes. The first editor increment should cover selecting a flame, inspecting a preserve-only entry, previewing a renderable one, editing a transform triangle, undo/redo and saving without damaging unknown settings. Transform-level lossless edits will need to extend the current byte-range editing API. Complete deferred Qt shell, settings and logging during editor integration. Windows 11 testing remains deferred.


## 0.4.0 — Primary Editor

Added the native Qt desktop application, searchable flame library and a refining CPU preview. Transform triangles and numeric fields share pre/post affine updates. The editor supports adding, duplicating, reordering, deleting and copying transforms, final transforms, geometry operations, supported variations and JuliaN variables, color controls and Xaos. Source-preserving edits are undoable. Saves check for external changes, recovery files preserve interrupted work, and preferences/workspace state persist locally.

The render API now supports cooperative cancellation and progress. Preview requests are debounced and superseded work is canceled. Background file renders can be canceled before their staged output replaces a destination. Legacy CLI entry points remain available.

The Windows portable distribution includes the Qt runtime and complete corresponding LGPL sources. The native Qt interaction tests and manual desktop edit/save exercise run on Linux. Windows 11 execution remains pending.

### Continue with Milestone 5

Start from the `v0.4.0-editor` tag and the source commit in BUILD.txt. Reuse DocumentSession for undo and recovery, LegacyDocument for original XML preservation, and PreviewController for responsive preview updates. Extend the preservation API when introducing editable palette and adjustment data. Add the full gradient editor, adjustment editor, mutation/random-generation tools and further source-verified variations. Advanced transform multi-selection, synchronization, persistent locks and configurable shortcut profiles remain on the editor backlog.


## 0.5.0 — Creative Tools

Added native gradient and adjustment dialogs, deterministic random flame batches, and a mutation explorer with candidate thumbnails, parent comparison and branching history. Palette tools include indexed RGB/HSV edits, RGB/HSV stops, presets, a local GPL library, rotation/reversal/smoothing, hue/saturation/value and RGB gamma/contrast operations. Adjustment controls expose the current camera, tone and reconstruction model. Changes preview independently and apply through one undo command.

Source-preserving edits now cover palettes and adjustment fields, with historical aliases and encodings retained. Collections support append, exact-data duplication and removal; undo restores before/after entry selection. Kept mutation branches become saved library entries. Ten additional deterministic classic variations bring the registry to 31, checked against 640 new vectors from original FLAM3 and Apophysis implementations.

The generator uses documented Reborn sampling distributions. Exact historical random-sequence parity, non-default historical curves, active density estimation, alpha output and additional palette formats remain open. Windows runtime acceptance is still deferred.

### Continue with Milestone 6

Start from `v0.5.0-creative` and the source commit in BUILD.txt. Preserve the CPU engine and all producer-aware oracle checks while adding device discovery and a real GPU rendering backend. Vulkan is the cross-vendor target; CUDA is optional. Report actual available devices and capability gaps, keep CPU fallback available, and compare numerical behavior and rendered output on real hardware before marking acceleration verified. Tiled 16K output, checkpointing and queues remain Milestone 7 work. The creative and advanced-editor compatibility backlog stays explicit in FEATURE_MATRIX.md and CREATIVE_TOOLS.md.


## 0.6.0 — GPU Rendering

Added dynamically loaded Vulkan compute sampling, device/capability discovery and explicit CPU/Automatic/Vulkan selection. The shader covers all 31 supported variations, transform and Xaos selection, affine/final/color behavior, palette/opacity sampling and projection. FP32 orbit calculations feed exact integer histogram totals through low/high 32-bit atomic words. The existing CPU code retains density reconstruction, Gaussian filtering, gamma, vibrancy, background and image output.

Compute invocations own 256-sample chains and a portable PCG-style 32-bit stream. A separate CPU comparison path uses those same streams with double formulas; the normal MT19937-64 CPU renderer and its earlier golden images remain unchanged. Admission checks diagnose unsupported FP32 models and probabilities. Resource limits, failure reasons, bounded submissions, cancellation and explicit fallback are part of the public render result and CLI contract. Cancellation does not restart a CPU render.

The desktop persists renderer, device and GPU-budget/fallback preferences and exposes actual execution in previews, file output and creative dialogs. New saved settings default to Automatic with fallback; the CLI defaults to CPU. Automatic device selection skips software Vulkan unless a device is explicitly selected. Dedicated Windows checks require the chosen device throughout probes, integration tests, CLI output and the native GPU smoke workflow.

Executed evidence includes the 20-entry native Release and ASan/UBSan suites, 1,560 shader transform probes and five matched-stream image fixtures. The image fixtures produced identical RGB8 output on the tested llvmpipe device. A Vulkan-disabled core build passed 13 entries and correctly skipped the two device-dependent suites. Windows builds produce 15 executables; the final 48-file runtime audit resolves 13,232 non-system imports across 46 PE modules with zero missing. Windows programs were compiled and audited, not executed. [GPU_TESTING.md](GPU_TESTING.md) and [TEST_PLAN.md](TEST_PLAN.md) retain the exact evidence and boundaries.

The execution environment provides software Vulkan only. Physical-GPU performance, NVIDIA/AMD/Intel driver acceptance and Windows runtime acceptance remain open. CUDA is not implemented. Normal source builds use the embedded shader and 13 pinned official Vulkan C headers with license/provenance; no Vulkan SDK or shader compiler is needed unless regenerating the shader.

### Continue with Milestone 7

Start from the 0.6.0 source commit in BUILD.txt. Extend the shared reconstruction/output pipeline with memory-aware tiling or streaming while preserving CPU and Vulkan sampling contracts and source-independent renderer preferences. Define deterministic tile boundaries and filter gutters, then verify seams, decoded dimensions, memory use, responsiveness and cancellation before accepting 16K output. Add render queues, durable checkpoints and resume, and high-bit-depth formats with explicit color/alpha semantics. Preserve atomic destination replacement and report actual backend/fallback for every queued job. Physical-GPU and Windows acceptance remain separate gates; the outstanding historical/creative/editor inventory stays in [FEATURE_MATRIX.md](FEATURE_MATRIX.md).


## 0.7.0 — High-Resolution Rendering

Introduced `render_to_file` and a preflight planner for images through 16,384 × 16,384. CPU and Vulkan now emit bounded global-coordinate hit chunks for file rendering; disk-backed samples reconstruct in full-width strips with complete filter overlap. The CPU batch stream and prior opaque RGB8 interpretation are preserved, and Vulkan retains absolute chain identities. Preview rendering remains dense and interactive. No full 16K histogram or final image buffer is required.

The new row encoders write PNG/TIFF8/16, PPM8/16 and BMP8 with direct double-to-16-bit quantization and explicit PNG/TIFF straight alpha. PNG compression uses zlib; TIFF uses uncompressed strips. Output is staged beside its destination until successful completion. File BMP output now uses a top-down negative header height while retaining decoded orientation and channels.

Durable sampling checkpoints use flushed immutable chunks, checksummed atomic manifests and process-owned locks. Resume validates the effective model, settings, chain continuity and engine/compiler/platform identity; committed GPU samples also bind device, driver and shader identity. CPU fallback can occur before GPU sampling commits, never by mixing a partial GPU job with CPU samples. Reconstruction restarts from its first strip using the saved sampling work. Explicit checkpoints survive completion; automatic private work directories are removed when their invocation ends normally.

The native queue persists immutable snapshots, exact 64-bit settings, progress/phase ETA, elapsed time, backend/device/fallback and checkpoint locations. It renders one job at a time and supports pause/resume, cancel, retry, start-over, order changes, save/restore and graceful editor close. Restored jobs never start automatically. The expanded Render image dialog adds all requested presets, custom size/aspect, density/sample controls, budgets, depth/alpha and a preflight plan. Legacy direct file-controller calls and all CLI exports use the streaming renderer.

Verification adds five CTest entries for file rendering, CLI, streaming encoders, GPU hit streams and the queue. All25 entries pass both Release and ASan/UBSan; the Vulkan-disabled build passes 16 and explicitly skips 3 actual-device suites. Actual 16K execution is independently decoded in full, compared byte-for-byte across different strip heights and queue pause/resume, and measured for process memory and UI responsiveness. Separate SIGKILL/restart and SIGINT tests preserve the prior destination and recover committed work. Final budget review covers escaped model metadata and both atomic manifest copies. Windows 19 executables are compiled and audited, with platform execution still pending. [Acceptance report](HIGH_RES_ACCEPTANCE.md) · [Complete test record](TEST_PLAN.md)

### Continue with Milestone 8

Start from `v0.7.0-high-resolution` and the source commit recorded in BUILD.txt. Preserve the established CPU/producer oracle gates, GPU admission/identity, source-preserving documents and streaming/checkpoint correctness. Audit original scripting interfaces, plugin conventions and additional historical file formats before implementing compatibility claims. Distinguish source/API compatibility from native binary-plugin compatibility and document platform/architecture boundaries. Build a practical modern scripting/plugin interface with explicit versioning and ownership before connecting legacy formats. Continue to report unsupported features without dropping source data. Physical-GPU and Windows acceptance remain separate; output and creative/editor gaps stay in [FEATURE_MATRIX.md](FEATURE_MATRIX.md).

## 0.8.0 — Extended Compatibility

Added an embedded Lua 5.5.1 runtime with a native script editor, API reference/completion, bundled examples, line diagnostics, seeded operations and cancellable background execution. Scripts edit a private, source-preserving document. Successful results apply as one document undo operation; errors, invalid final models, cancellation and resource-limit failures discard pending edits and requests. The host controls named imports and deferred file/render requests. Evaluation has no direct filesystem, process, network or native-loader API. Output paths are preflighted together against the input files, application state, active queue, loaded modules, path aliases and ancestor conflicts. Individual published files are atomic; document undo does not remove previously published exports.

The legacy language implementation is a strict Pascal-style subset with complete preflight, typed assignments, original zero-based transform APIs and the audited coefficient, camera and angle conventions. Affine operations are compared with six values generated by compiling unchanged original Apophysis 2.09 methods. This is not full TatScripter interpreter parity, and Reborn's reproducible RNG stream is distinct from historical random sequences. Transform source identities preserve the correct unknown attributes, comments and children through deletion, copying and rebuilding. Deferred renders refresh the producer-aware model before taking their immutable snapshots.

Introduced a versioned C ABI for ordinary additive 2D CPU variation plugins. Data-only discovery reads sidecars without loading code; explicit approval validates the module's ABI, metadata, parameter ranges and SHA-256. Saved approvals are tied to the binary fingerprint. The editor includes discovery, installation, enable/disable/removal and parameter controls; the package includes a working `sample_wave` module, source SDK and a source-migration example. Immutable descriptors retain live module ownership in documents and render jobs, and checkpoints/queue restoration bind the module identity. Plugin terms report CPU fallback or reject strict Vulkan requests. Native faults and hangs are not isolated, and old 32-bit DLLs cannot load directly in the 64-bit application.

The native `.apoproject` envelope stores the exact legacy collection bytes plus selection, preview/workspace preferences, render settings and named presets, plugin references, thumbnails and opaque animation data. Unknown envelope XML and retained settings attributes survive edits. Native project save, conflict detection, recovery and CLI round trips use the same document wrapper. Plugin references remain informational and never authorize loading code. Animation data is retained but not evaluated in this milestone.

Additional exchange includes UGR collections, Fractint MAP palettes and a restricted Apophysis UPR profile. Original Pascal-generated fixtures cover gradient interpolation, RGB packing, transform/camera conventions and supported UPR serialization. The gradient editor accepts UGR/MAP, and the main window imports/exports the supported UPR profile. Unsupported Ultra Fractal formulas and unrepresentable settings fail with diagnostics; the native project can retain imported source text and warnings. All existing historical producer, CPU, creative/editor, Vulkan and streaming/checkpoint gates remain in the release suite.

Integration review fixed transform metadata misassignment after removal, pre-normalization script request snapshots, bounded console/error accounting, incomplete output-path collisions, plugin editor no-op precision/order changes and scripted output conflicts with persistent queue state. The focused regressions exercise each issue. Final executed results, software-Vulkan boundaries, Windows cross-build audit and native desktop observations are recorded in [TEST_PLAN.md](TEST_PLAN.md); the package also retains the earlier M7 full-resolution image and acceptance evidence as historical results.

### Continue with Milestone 9

Start from `v0.8.0-extended-compatibility` and the source commit in BUILD.txt. Implement animation and morphing as a versioned project model with explicit keyframe timing, easing, loop behavior and deterministic per-frame evaluation. Define how transforms and plugin parameters correspond between keyframes before interpolation; incompatible topology, module identities and integer/discrete properties need explicit behavior. Preserve the source documents and existing opaque animation data when migrating to an understood schema. Extend the editor with timeline/keyframe controls and connect frame sequences to the existing immutable render queue, cancellation and checkpoint contracts.

Add extended color tools and prioritize further 7X/AV functionality from the existing compatibility inventory, with original-source fixtures for each new historical claim. Keep static flame output and seeded renders stable while testing keyframe endpoints, interpolation continuity, angle wrapping, palette behavior and interruption/restart of frame sequences. Physical-GPU and Windows runtime acceptance remain separate gates. Full legacy scripting coverage, binary-plugin migration, broader Ultra Fractal support and the remaining creative/editor features stay visible in [FEATURE_MATRIX.md](FEATURE_MATRIX.md).
