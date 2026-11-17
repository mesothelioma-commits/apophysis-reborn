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
