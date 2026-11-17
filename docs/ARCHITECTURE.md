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
| Output and image encoding | src/output_file.cpp, src/image.cpp | Emit RGB24 BMP or binary PPM with temporary-file staging and replacement on successful close |
| Application entry | src/main.cpp, src/document_cli.cpp | Parse options, choose a collection member, use camera and quality settings, report counts and execution time |
| Verification | tests | Compare upstream numeric vectors and a golden render; verify XML behavior, image geometry and the public CLI |

The model's schema version describes Reborn's in-memory contract. The XML producer's version selects source-specific numeric conventions in the render model. The document layer retains the original text independently. The CLI first indexes the whole XML document, then decodes only the requested entry. Unsupported document members can coexist with renderable ones. Conversion to a new model version belongs at the input boundary. All rendering receives a const model. The double-precision CPU implementation remains the compatibility path and numerical reference for the Vulkan backend.

Ordinary CPU workers take independent 8,192-iteration batches. Each has a deterministic seed, a private orbit, a 32-step settling period and a short buffer of hits. Workers merge exact fixed-point channel sums into one shared histogram. The memory budget covers the histogram, RGB image and simultaneous hit buffers. Thread stacks, parser/model memory, probability tables, output encoder scratch space and allocator overhead are additional.

The native UI calls the core directly. Both samplers implement cancellation and progress; tiled high-resolution storage and resumable queues remain subsequent work.

## Vulkan sampling and CPU fallback

The Vulkan sampler owns one 256-sample chain per compute invocation, with 32 burn-in iterations and a portable PCG-style 32-bit stream derived from the full seed and chain index. It uses FP32 orbit calculations and represents each 64-bit histogram channel as two 32-bit atomic words with carry. This avoids optional FP64 and 64-bit atomic requirements. The runtime submits at most 1,024 chains at a time, uses explicit buffer barriers and fence completion before readback, and retains a compute context per rendering thread. The read-back histogram enters the same CPU density, filter, gamma, vibrancy and background reconstruction as ordinary CPU samples.

`render_compute_reference` evaluates the CPU's double formulas with the compute chain boundaries and portable stream. This comparison path is separate from the established MT19937-64 CPU renderer, whose old golden images remain pinned. Input admission checks protect FP32 conversion, probability resolution and shader addressing; device limits and the host/GPU working budgets constrain allocations separately. Cancellation is checked around bounded submissions and does not restart work on the CPU. It cannot interrupt a dispatch already executing on a device.

`RenderResult` identifies the actual backend, device, fallback reason, sample counts and separate sampling/reconstruction times. A missing loader, unavailable device, unsupported compute model or applicable resource failure can return an ordinary CPU render only when fallback is enabled. Strict requests surface the reason as an error. Invalid model settings, invalid sample counts and inadequate host image storage remain input errors. Automatic device selection excludes software Vulkan unless a device is explicitly chosen.

The runtime dynamically loads the system Vulkan loader; normal builds use the embedded SPIR-V payload and the pinned C headers under `third_party/vulkan`. A Vulkan SDK or shader compiler is not required for an unchanged source build. `APO_ENABLE_VULKAN=OFF` keeps the CPU, discovery diagnostics and fallback contract. Shader regeneration is a separate development operation. The [rendering guide](GPU_RENDERING.md), [kernel contract](GPU_KERNEL.md), [comparison methods](GPU_TESTING.md) and [Windows checks](WINDOWS_GPU.md) give the exact controls and acceptance scope. CUDA is not implemented.


## Native editor

The Qt Widgets layer in `src/editor` owns the main window, dock layout, library selection, numeric controls and triangle canvas. `DocumentSession` keeps the lossless `LegacyDocument`, save-path fingerprint, recovery ownership and Qt undo stack. Each undo command retains complete before/after document bytes and selection. Each change is validated on a copied document before it reaches the active session.

`PreviewController` owns one background worker and the latest immutable flame snapshot. Debounced edits cancel the previous request. Generation numbers exclude stale queued images and status events. The controller renders increasing sample budgets at a viewport-fitted size, leaving the original dimensions and camera in the document. A second controller handles full-resolution file output. The core accepts a C++20 stop token and reports completed samples through a serialized callback. Cancellation is checked in sampling, reconstruction, encoding and immediately before output replacement.

The editor's recovery files have a unique identity and a held lock while their owning session is alive. Interrupted copies are recoverable as unsaved libraries. Saving, explicit discard and undo back to the saved state remove the active recovery. File saves compare the original content digest before staging and immediately before replacement to detect changes from another application.

Window geometry, docking and preview preferences use Qt settings. Activity is written to the visible log and a size-rotated local text file. The editor uses the existing canonical serializer only for a requested standalone export and for new transform markup. Changes to loaded transform fields use the source-preserving document API.

New saved editor settings select Automatic with CPU fallback; the CLI and direct core API retain CPU defaults. Renderer, preferred-device ID and memory/fallback preferences are machine settings, independent of the flame XML. Preview and file-render status expose the actual backend. Per-export choices do not overwrite saved preview preferences.


## Creative editors

The gradient and adjustment dialogs hold independent working models and their own cancelable preview controllers. Accepted values become one document command. Palette edits replace only the chosen entry’s palette and retain its source encoding, surrounding comments, history and siblings. Adjustment edits retain historical attribute aliases.

The explorer generates bounded random or mutation batches on a background thread. A separate preview controller processes candidate thumbnails sequentially; another refines the selected candidate. Generation/preview state is owned by the dialog, and workers finish before its destruction. Child input signals are disconnected before derived dialog members are destroyed, including the focused-field teardown path.

Creative preview controllers inherit the selected backend, device and budgets while retaining their smaller sample limits. Each candidate can report its own CPU fallback if its generated model is outside the compute admission rules.

Mutation ancestry is a local branch graph while the explorer is open. Add branch history saves the chosen ancestry as source-preserving copies in the flame library, making that path durable through ordinary Save and reopen. Batch/duplicate/delete commands record both before and after flame selections in the undo stack. New random entries use canonical model serialization; changes to existing and duplicated legacy entries patch only changed fields.
