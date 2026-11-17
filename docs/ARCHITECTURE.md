# Core engine architecture

| Layer | Files | Responsibility |
| --- | --- | --- |
| Model and math | src/flame.hpp, src/flame.cpp | Model version 1, double-precision affine transforms, named variation registry, color, camera, palettes and weighted selection |
| Input and serialization | src/flame_io.hpp, src/flame_io.cpp | Stream XML with Expat; parse and validate a whole collection. Serialize one supported model to UTF-8 XML |
| Sampling and tone mapping | src/render.hpp, src/render.cpp | Keep private orbits, run fixed independent batches, accumulate RGB and visibility, then filter and tone-map |
| Image encoding | src/image.cpp | Emit RGB24 BMP or binary PPM with temporary-file staging and replacement on successful close |
| Application entry | src/main.cpp | Parse options, choose a collection member, use camera and quality settings, report counts and execution time |
| Verification | tests | Compare upstream numeric vectors and a golden render; verify XML behavior, image geometry and the public CLI |

The model's schema version describes Reborn's in-memory contract. The XML producer's version is kept separately. Conversion to a new model version belongs at the input boundary. All rendering receives a const model. The CPU implementation is the reference for future accelerated backends.

Workers take independent 8,192-iteration batches. Each has a deterministic seed, a private orbit, a 32-step settling period and a short buffer of hits. Workers merge exact fixed-point channel sums into one shared histogram. The memory budget covers the histogram, RGB image and simultaneous hit buffers. Thread stacks, parser/model memory, probability tables, output encoder scratch space and allocator overhead are additional.

A native UI can call the core directly. Interactive cancellation, progress callbacks and tiled high-resolution storage belong in the scheduling and rendering milestone before the UI starts unbounded interactive work.
