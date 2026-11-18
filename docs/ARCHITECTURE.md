# Engine and editor architecture

| Layer | Files | Responsibility |
| --- | --- | --- |
| Model and math | src/flame.hpp, src/flame.cpp | Model version 1, double-precision affine transforms, named variation registry, color, camera, palettes and weighted selection |
| Creative algorithms | src/creative.hpp, src/creative.cpp | Seeded generation/mutation, palette operations and exchange, robust orbit framing without Qt dependencies |
| Legacy documents | src/legacy_document.hpp, src/legacy_document.cpp | Index direct library members by original byte range. Preserve original XML, patch only requested field values and validate the result before publishing it |
| Input and serialization | src/flame_io.hpp, src/flame_io.cpp | Stream XML with Expat; parse and validate a whole collection. Serialize one supported model to UTF-8 XML |
| Sampling and tone mapping | src/render.hpp, src/render.cpp | Select the CPU or Vulkan sampler, retain the CPU compatibility and compute-reference streams, then apply shared filtering and tone mapping |
| Compute contract and packing | src/gpu.hpp, src/gpu_kernel.hpp, src/gpu_kernel.cpp | Report devices, check FP32/model limits, pack the immutable render model and define the portable compute stream |
| Vulkan runtime | src/vulkan_runtime.hpp, src/vulkan_runtime.cpp | Load the system API dynamically, select a compute device, check budgets, submit bounded work and read back the histogram |
| Compute shader | shaders/orbit.comp, src/generated/orbit_spv.hpp | Evaluate all 31 variations, select transforms, advance independent orbits, project and accumulate integer histogram channels |
| File render planner and checkpoint engine | src/render_file.hpp, src/render_file.cpp, src/render_internal.hpp | Budget bounded hit sampling and strip reconstruction; persist flushed chunks and an atomic checksummed manifest; validate resume identity and preserve destinations |
| Output and image encoding | src/output_file.cpp, src/image.cpp, src/image_stream.hpp, src/image_stream.cpp | Stream BMP8, PPM8/16, PNG8/16 and TIFF8/16 rows, including straight PNG/TIFF alpha; atomically publish completed output |
| Native render queue | src/editor/render_queue.hpp, src/editor/render_queue.cpp | Own immutable job snapshots, a sequential worker, persisted progress/settings, pause/resume and lifecycle |
| Application entry | src/main.cpp, src/document_cli.cpp, src/extension_cli.cpp | Parse options, choose a collection member, use camera and quality settings, report counts and execution time |
| Verification | tests | Compare upstream numeric vectors and a golden render; verify XML behavior, image geometry and the public CLI |

The model's schema version describes Reborn's in-memory contract. The XML producer's version selects source-specific numeric conventions in the render model. The document layer retains the original text independently. The CLI first indexes the whole XML document, then decodes only the requested entry. Unsupported document members can coexist with renderable ones. Conversion to a new model version belongs at the input boundary. All rendering receives a const model. The double-precision CPU implementation remains the compatibility path and numerical reference for the Vulkan backend.

Ordinary CPU workers take independent 8,192-iteration batches. Each has a deterministic seed, a private orbit, a 32-step settling period and a short buffer of hits. Workers merge exact fixed-point channel sums into one shared histogram. The memory budget covers the histogram, RGB image and simultaneous hit buffers. Thread stacks, parser/model memory, probability tables, output encoder scratch space and allocator overhead are additional.

The native UI calls the core directly. Preview rendering retains the dense histogram path. File exports use the separate bounded streaming path below, preserving the same global sampling and reconstruction semantics.

## Vulkan sampling and CPU fallback

The Vulkan sampler owns one 256-sample chain per compute invocation, with 32 burn-in iterations and a portable PCG-style 32-bit stream derived from the full seed and chain index. It uses FP32 orbit calculations and represents each 64-bit histogram channel as two 32-bit atomic words with carry. This avoids optional FP64 and 64-bit atomic requirements. The runtime submits at most 1,024 chains at a time, uses explicit buffer barriers and fence completion before readback, and retains a compute context per rendering thread. The read-back histogram enters the same CPU density, filter, gamma, vibrancy and background reconstruction as ordinary CPU samples.

`render_compute_reference` evaluates the CPU's double formulas with the compute chain boundaries and portable stream. This comparison path is separate from the established MT19937-64 CPU renderer, whose old golden images remain pinned. Input admission checks protect FP32 conversion, probability resolution and shader addressing; device limits and the host/GPU working budgets constrain allocations separately. Cancellation is checked around bounded submissions and does not restart work on the CPU. It cannot interrupt a dispatch already executing on a device.

`RenderResult` identifies the actual backend, device, fallback reason, sample counts and separate sampling/reconstruction times. A missing loader, unavailable device, unsupported compute model or applicable resource failure can return an ordinary CPU render only when fallback is enabled. Strict requests surface the reason as an error. Invalid model settings, invalid sample counts and inadequate host image storage remain input errors. Automatic device selection excludes software Vulkan unless a device is explicitly chosen.

The runtime dynamically loads the system Vulkan loader; normal builds use the embedded SPIR-V payload and the pinned C headers under `third_party/vulkan`. A Vulkan SDK or shader compiler is not required for an unchanged source build. `APO_ENABLE_VULKAN=OFF` keeps the CPU, discovery diagnostics and fallback contract. Shader regeneration is a separate development operation. The [rendering guide](GPU_RENDERING.md), [kernel contract](GPU_KERNEL.md), [comparison methods](GPU_TESTING.md) and [Windows checks](WINDOWS_GPU.md) give the exact controls and acceptance scope. CUDA is not implemented.


## Native editor

The Qt Widgets layer in `src/editor` owns the main window, dock layout, library selection, numeric controls and triangle canvas. `DocumentSession` keeps the lossless `LegacyDocument`, save-path fingerprint, recovery ownership and Qt undo stack. Each undo command retains complete before/after document bytes and selection. Each change is validated on a copied document before it reaches the active session.

`PreviewController` owns one background worker and the latest immutable flame snapshot. Debounced edits cancel the previous request. Generation numbers exclude stale queued images and status events. The controller renders increasing sample budgets at a viewport-fitted size, leaving the original dimensions and camera in the document. The persistent `RenderQueue` owns a separate sequential worker for full-resolution output; the controller's direct file-render API also routes through `render_to_file`. The core accepts a C++20 stop token and reports completed samples through a serialized callback. Cancellation is checked in sampling, reconstruction, encoding and immediately before output replacement.

The editor's recovery files have a unique identity and a held lock while their owning session is alive. Interrupted copies are recoverable as unsaved libraries. Saving, explicit discard and undo back to the saved state remove the active recovery. File saves compare the original content digest before staging and immediately before replacement to detect changes from another application.

Window geometry, docking and preview preferences use Qt settings. Activity is written to the visible log and a size-rotated local text file. The editor uses the existing canonical serializer only for a requested standalone export and for new transform markup. Changes to loaded transform fields use the source-preserving document API.

New saved editor settings select Automatic with CPU fallback; the CLI and direct core API retain CPU defaults. Renderer, preferred-device ID and memory/fallback preferences are machine settings, independent of the flame XML. Preview and file-render status expose the actual backend. Per-export choices do not overwrite saved preview preferences.


## Creative editors

The gradient and adjustment dialogs hold independent working models and their own cancelable preview controllers. Accepted values become one document command. Palette edits replace only the chosen entry’s palette and retain its source encoding, surrounding comments, history and siblings. Adjustment edits retain historical attribute aliases.

The explorer generates bounded random or mutation batches on a background thread. A separate preview controller processes candidate thumbnails sequentially; another refines the selected candidate. Generation/preview state is owned by the dialog, and workers finish before its destruction. Child input signals are disconnected before derived dialog members are destroyed, including the focused-field teardown path.

Creative preview controllers inherit the selected backend, device and budgets while retaining their smaller sample limits. Each candidate can report its own CPU fallback if its generated model is outside the compute admission rules.

Mutation ancestry is a local branch graph while the explorer is open. Add branch history saves the chosen ancestry as source-preserving copies in the flame library, making that path durable through ordinary Save and reopen. Batch/duplicate/delete commands record both before and after flame selections in the undo stack. New random entries use canonical model serialization; changes to existing and duplicated legacy entries patch only changed fields.

## High-resolution file rendering in 0.7.0

`plan_file_render` validates dimensions through 16,384 per axis, sample and storage budgets, model/metadata allowances, format limits and the full reconstruction footprint before work begins. The host allowance includes bounded sampling buffers, the chooser/model, metadata copies, one reconstructed row, encoder buffers and a full-width strip plus filter overlap. It is not an OS cap on the entire process. `render_to_file` uses the same plan and a separate conservative disk estimate. [Budget details](HIGH_RES_RENDERING.md)

CPU file sampling uses the established independent 8,192-iteration batches and their original seeds/burn-in. GPU file sampling uses absolute 256-sample chain ranges and a bounded shader hit-output mode. Both emit six-word global-coordinate RGB/mass records. Integer accumulation remains exact. File sampling happens once for the complete camera; no cropped flame is independently sampled per strip. Immutable chunks partition hits by global row group and index each group's payload and checksum. Reconstruction reads the groups required by one strip and its complete filter footprint, then applies global exposure/normalization and writes output rows immediately. [Compute streaming contract](GPU_STREAMING.md)

An explicit checkpoint retains a manifest and sampled chunks after success or cancellation. Each chunk is staged, closed and flushed before the atomic manifest commit can reference it. The manifest has its own checksum. Resume checks structure, continuity, counters, payload checksums, the flame and render settings, and engine/compiler/platform identity. Committed GPU work also binds the device/driver/shader identity. A partial GPU checkpoint never silently switches to CPU or another GPU. Process-owned file locks reject concurrent writers and are released after process termination. Unreferenced chunks from an interrupted commit are ignored. Reconstruction resumes from strip zero using saved samples; a partially encoded image is not appended to. [Checkpoint contract](HIGH_RES_RENDERING.md)

The row encoder receives double-valued tone-mapped RGB/alpha. Sixteen-bit formats quantize that result directly. The existing opaque RGB8 historical reconstruction retains its integer-background and quantization conventions. Straight-alpha export excludes the flame background and uses density-derived opacity; an empty pixel is transparent black. PNG uses bounded zlib compression; TIFF uses uncompressed strips. No HDR/scene-linear or ICC profile is inferred. `atomic_write_output` stages the entire result beside its destination and replaces it only after successful encoder completion and a final cancellation check. [Encoding contract](IMAGE_FORMATS.md)

`RenderQueue` uses one `std::jthread`, immutable per-job models and Qt queued progress notifications. `QSaveFile` persists exact 64-bit settings as strings, backend/device details, progress, errors and checkpoint paths. `QLockFile` guards ownership of the automatic queue. Pausing requests a stop, waits for the worker, and verifies the checkpoint before reporting it resumable. Restored active jobs become paused and never start automatically. Main-window close processes UI events until the active worker has stopped; destruction joins it. Starting a job over allocates a new identity/checkpoint location and retains the previous directory. [Queue lifecycle](RENDER_QUEUE.md)

## Extended compatibility in 0.8.0

`script.cpp` owns one private document/model and a pinned Lua state per invocation. Lua is compiled as C++ so protected errors unwind C++ callback temporaries. Only selected safe libraries are opened; no filesystem, process, module-loader, debug or network API is supplied. Host imports are named byte capabilities. Time, instruction-hook quanta, Lua allocations, conservative host-data reservations, source/document size, API-call count, console output and request counts are bounded. Stop and failure return no result. Successful scripts return document bytes, selection, console entries and immutable deferred requests; they do not publish files themselves.

`legacy_script.cpp` tokenizes and validates a restricted Pascal dialect before execution, emitting Lua with a source-line map. Historical calls route through the same bounded host API. `script_document.cpp` merges complete validated models into original source ranges. Explicit transform-source identities keep surviving transform comments/attributes with the correct transform when scripts remove or rebuild topology. Canonical/private XML is decoded before capturing deferred render models so source producer flags, palette quantization and legacy defaults agree with what the document will reopen as.

`script_host.cpp` preflights portable relative destinations, format constraints, canonical confinement, duplicates, ancestor collisions and protected sources/state. Windows uses Unicode ordinal case-insensitive comparison. The editor protects its open document, script/imports, settings/recovery, queue state/checkpoint locations, active destinations and native modules. Exports are individually atomic; requests are not a multi-file transaction. `RenderQueue::addBatch` validates and persists the entire new group before starting new work. `ScriptDialog` runs on a joined worker, exposes Stop and only hands successful results back to the document session. Model application is one undo command; completed exports and queued jobs have independent lifecycles.

`plugin_api.h` defines a versioned C ABI with no C++ objects crossing the boundary. Discovery reads sidecar XML and module bytes without executing them. Enabling is the trusted-code boundary: architecture, ABI, exact descriptor/parameter metadata and approved binary hashes are validated. Immutable `PluginVariation` references in `Term` keep modules alive for queued/worker snapshots. Disabling removes lookup for new parses, without invalidating those snapshots. Module callbacks are reentrant, receive the current orbit RNG capability and return an unweighted contribution. GPU admission rejects these CPU callbacks explicitly. Checkpoints and persisted queue jobs additionally bind module binary identities; portable canonical XML records stable IDs/versions/parameters.

`project_io.cpp` preserves a versioned native XML envelope with exact base64-encoded legacy bytes and typed string maps for settings, named presets, plugin references and optional opaque animation. It validates bounds, versions and XML structure while retaining unknown source spans. `project_document.cpp` routes native and legacy documents. `DocumentSession` fingerprints and recovers the whole envelope, with model undo retaining its existing library-byte semantics. Loading plugin references never enables native code. `legacy_formats.cpp` implements original-source-checked UGR/MAP and the strictly admitted Apophysis UPR dialect with per-entry reports.

## Animation and color in 0.9.0

`animation_io.cpp` validates a bounded versioned UTF-8/base64 schema.
`animation.cpp` prepares immutable snapshot models once, verifies correspondence
and native module identities, and evaluates exact or interpolated frames. The
project envelope remains independent of this schema and preserves unknown bytes.
The native `AnimationDialog` owns private history and preview work; accepted
content enters `DocumentSession` undo. `MainWindow::queueAnimation` validates a
complete finite range and submits immutable frame jobs as one persisted batch.

`animation_export.cpp` shares fixed-canvas evaluation and destination admission
with the editor. Its CLI path uses a separate locked manifest, verified complete
outputs, ready/publication crash recovery and per-frame renderer checkpoints.
The shared SHA-256 implementation also retains plugin fingerprint semantics.
Sequence and Qt queue resume are distinct workflows. Queue publication guards
persist admitted destination identities and refuse intervening external changes.

`color_curves.cpp` implements the original 257-entry 7X LUT and an explicitly
selected continuous-mode LUT. Dense and file reconstruction share the same
evaluator and account its storage in the working-memory plan. Identity curves
skip extra arithmetic/allocation. `creative::blend_colors` centralizes RGB,
linear-light RGB, HSV and HSL interpolation for gradient editing and animation.
