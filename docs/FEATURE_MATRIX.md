# Feature status at Creative Tools checkpoint 0.5.0

| Milestone or feature | Delivered | Verification or remaining requirement |
| --- | --- | --- |
| M1 repository, build, logs, tests | Git history and bundle, CMake, build/verification logs, CTest | Native Qt shell, persistent preview/workspace settings and rotating activity log. Windows x64 cross-build verified; native Windows execution pending |
| M2 mathematical model | Versioned 2D model, base and chaos weights, affine and final transforms, color controls | FLAM3 transform vectors, chooser boundaries and projection tests |
| M2 variations | 31 registered CPU implementations, including classic IDs 0–22 and producer-specific mappings | Original reference suite plus 640 new vectors from actual FLAM3 C and Apophysis Pascal functions; full inventory in VARIATIONS.md |
| M2 CPU pipeline | Seeded, multithreaded sampling, histogram, step/linear palette, logarithmic density, box oversampling, gamma/vibrancy, RGB8 output | Determinism, pixel-level tests, malformed-input and buffer-budget checks |
| M2 reference tests | Pinned numeric oracle and independently rendered four-spherical-transform image | Report and thresholds in REFERENCES.md. Product-wide compatibility remains open |
| M3 legacy files | Lossless XML collection save, four targeted header edits, individual entry selection/reporting and normalized model export | Byte equality, unknown-data preservation, historical 2.x/7X/AV fixtures, original Pascal pixel tests and an AV artist image. Native historical editor reopen tests and broader render compatibility remain open |
| M4 primary editor | Qt main window, searchable library, refining CPU preview, transform triangles, pre/post/final editing, variations/color/Xaos, undo, safe save and recovery | Native desktop edits/save and automated Qt event tests passed. Advanced multi-selection, per-transform persistent locks, synchronization, shortcut profiles and workspace presets remain open |
| M5 creative tools | Native gradient and adjustment editors; 1–24 seeded random/mutation candidates; palette presets/GPL library; parent comparison, saved branches and collection operations | Core algorithm tests, native dialog input/apply/cancel/undo checks and source preservation. Historical generator sequence equivalence, additional palette formats/catalogs, alpha, non-default legacy curves, density estimation and more variations remain open |
| M6 GPU acceleration | Open | Device discovery, Vulkan, optional CUDA and numerical equivalence |
| M7 large renders | Dense buffer budgeting, background file render and cooperative cancellation | Tiling/streaming, true 16K acceptance, checkpoint, queue, export pause/resume and high-bit-depth formats |
| M8 extended compatibility | Open | Scripting, plugins and other historical formats |
| M9 animation and extended features | Open | Keyframes, morphing, timeline and 7X/AV additions |
| M10 release | Portable Windows GUI and console package | Desktop release, platform benchmarks, installer, manual and final compatibility report |

This inventory covers work through the creative tools checkpoint. The full menu, dialog, variation and file-format inventories by historical release still require an audit of original 2.x, 7X and AV sources and applications.
