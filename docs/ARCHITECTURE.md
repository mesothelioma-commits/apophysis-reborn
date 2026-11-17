# Engine and editor architecture

| Layer | Files | Responsibility |
| --- | --- | --- |
| Model and math | src/flame.hpp, src/flame.cpp | Model version 1, double-precision affine transforms, named variation registry, color, camera, palettes and weighted selection |
| Legacy documents | src/legacy_document.hpp, src/legacy_document.cpp | Index direct library members by original byte range. Preserve original XML, patch only requested field values and validate the result before publishing it |
| Input and serialization | src/flame_io.hpp, src/flame_io.cpp | Stream XML with Expat; parse and validate a whole collection. Serialize one supported model to UTF-8 XML |
| Sampling and tone mapping | src/render.hpp, src/render.cpp | Keep private orbits, run fixed independent batches, accumulate RGB and visibility, then filter and tone-map |
| Output and image encoding | src/output_file.cpp, src/image.cpp | Emit RGB24 BMP or binary PPM with temporary-file staging and replacement on successful close |
| Application entry | src/main.cpp, src/document_cli.cpp | Parse options, choose a collection member, use camera and quality settings, report counts and execution time |
| Verification | tests | Compare upstream numeric vectors and a golden render; verify XML behavior, image geometry and the public CLI |

The model's schema version describes Reborn's in-memory contract. The XML producer's version selects source-specific numeric conventions in the render model. The document layer retains the original text independently. The CLI first indexes the whole XML document, then decodes only the requested entry. Unsupported document members can coexist with renderable ones. Conversion to a new model version belongs at the input boundary. All rendering receives a const model. The CPU implementation is the reference for future accelerated backends.

Workers take independent 8,192-iteration batches. Each has a deterministic seed, a private orbit, a 32-step settling period and a short buffer of hits. Workers merge exact fixed-point channel sums into one shared histogram. The memory budget covers the histogram, RGB image and simultaneous hit buffers. Thread stacks, parser/model memory, probability tables, output encoder scratch space and allocator overhead are additional.

A native UI can call the core directly. Interactive cancellation, progress callbacks and tiled high-resolution storage belong in the scheduling and rendering milestone before the UI starts unbounded interactive work.


## Native editor

The Qt Widgets layer in `src/editor` owns the main window, dock layout, library selection, numeric controls and triangle canvas. `DocumentSession` keeps the lossless `LegacyDocument`, save-path fingerprint, recovery ownership and Qt undo stack. Each undo command retains complete before/after document bytes and selection. Each change is validated on a copied document before it reaches the active session.

`PreviewController` owns one background worker and the latest immutable flame snapshot. Debounced edits cancel the previous request. Generation numbers exclude stale queued images and status events. The controller renders increasing sample budgets at a viewport-fitted size, leaving the original dimensions and camera in the document. A second controller handles full-resolution file output. The core accepts a C++20 stop token and reports completed samples through a serialized callback. Cancellation is checked in sampling, reconstruction, encoding and immediately before output replacement.

The editor's recovery files have a unique identity and a held lock while their owning session is alive. Interrupted copies are recoverable as unsaved libraries. Saving, explicit discard and undo back to the saved state remove the active recovery. File saves compare the original content digest before staging and immediately before replacement to detect changes from another application.

Window geometry, docking and preview preferences use Qt settings. Activity is written to the visible log and a size-rotated local text file. The editor uses the existing canonical serializer only for a requested standalone export and for new transform markup. Changes to loaded transform fields use the source-preserving document API.
