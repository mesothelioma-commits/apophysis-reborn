# Feature status at CPU core milestone 0.2.0

| Milestone or feature | Delivered | Verification or remaining requirement |
| --- | --- | --- |
| M1 repository, build, logs, tests | Git history and bundle, CMake, build/verification logs, CTest | Windows x64 cross-build. Native Qt shell, persistent application settings and desktop logging remain open |
| M2 mathematical model | Versioned 2D model, base and chaos weights, affine and final transforms, color controls | FLAM3 transform vectors, chooser boundaries and projection tests |
| M2 variations | Registry and CPU implementations for IDs 0–14 | Per-variation vectors; Julia branches. Additional variations require independent fixtures |
| M2 CPU pipeline | Seeded, multithreaded sampling, histogram, step/linear palette, logarithmic density, box oversampling, gamma/vibrancy, RGB8 output | Determinism, pixel-level tests, malformed-input and buffer-budget checks |
| M2 reference tests | Pinned numeric oracle and independently rendered four-spherical-transform image | Report and thresholds in REFERENCES.md. Product-wide compatibility remains open |
| M3 legacy files | Strict XML reader, version metadata, canonical single-flame serializer and initial fixtures | Constrained subset. Historical editor round trips, catalog palettes, filters, unknown-field preservation and broader corpus still needed |
| M4 primary editor | Open | Native Qt desktop application, transform triangles, flame library and live preview |
| M5 creative tools | Open | Variation expansion, gradient and adjustment editors, mutations and random generation |
| M6 GPU acceleration | Open | Device discovery, Vulkan, optional CUDA and numerical equivalence |
| M7 large renders | Dense buffer budgeting only | Tiling/streaming, true 16K acceptance, checkpoint, queue, cancellation and high-bit-depth formats |
| M8 extended compatibility | Open | Scripting, plugins and other historical formats |
| M9 animation and extended features | Open | Keyframes, morphing, timeline and 7X/AV additions |
| M10 release | Development ZIP with Windows console tools | Desktop release, platform benchmarks, installer, manual and final compatibility report |

This is the engine checkpoint inventory. The full menu, dialog, variation and file-format inventories by historical release still require an audit of original 2.x, 7X and AV sources and applications.
