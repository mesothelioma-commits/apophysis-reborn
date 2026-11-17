# Development log, changelog and handoff

## Foundation checkpoint

CMake project and single-threaded C++ experiment with a fixed linear IFS, four formulas, a PPM writer and one smoke test. Its source was imported as the first commit of the bundled repository.

## 0.2.0 — Core Engine

The new version reads render parameters from flame definitions. The model, registry, sampler, XML reader, canonical single-flame serializer, camera, color pipeline and exporters now have distinct code paths. The legacy positional CLI still renders the foundation example with the new engine. Tests cover numeric output, actual independently rendered pixels, collection parsing, seed replay between thread counts and output handling. The ZIP adds Windows x64 console builds plus one-command verification in Git Bash.

### Continue with Milestone 3

1. Start from the 0.2.0 source commit in BUILD.txt or clone the bundle. Run the six native CTest entries and the Windows verification script.
2. Assemble untouched fixtures and identify the producing release for original 2.x, 7X and AV. The adapted FLAM3 fixtures and the AV spot check have narrower coverage.
3. Track shared, producer-specific and unknown fields in a lossless editing model. Preserve unimplemented data through load/save, report what blocks accurate rendering, and test real historical binary round trips.
4. Add the next missing variations, palette catalog and filter/estimator behavior driven by those fixtures. Extend the reference suite before claiming visual compatibility for any additional family.
5. Complete the deferred foundation work on the Windows Qt shell, settings and desktop logging before Milestone 4 editor integration.

The compatibility matrix lists unsupported controls. Keep physical Windows runtime evidence and any future GPU or 16K acceptance evidence distinct from the source and image tests in this milestone.
