# Core Engine verification — 0.2.0

## Executed on 17 November 2026

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

## Local timing

Ember Spiral, 768 × 576, 7,962,624 samples, seed 123456, release executable. Wall time from process launch through file close, three runs per configuration on the same shared Linux host.

| CPU workers | Wall times (ms) | Median (ms) |
| ---: | --- | ---: |
| 1 | 1,064 / 1,072 / 1,038 | 1,064 |
| 4 | 357 / 357 / 343 | 357 |

Both outputs had identical bytes. BMP SHA-256: `82133ca765ddd8440d495c383c26da79c26af6185d3a7c76a273e9f14f22235e`. Timings characterize this flame on this host. Platform math libraries can change the last bits and eventually stochastic orbits. The Windows verification script compares two Windows outputs to each other.

## Open acceptance checks

- Execute the included x64 binaries on Windows 11 using `bash scripts/verify-windows.sh`. The script captures local test output and hashes in `logs/`. PE headers and imported DLL names were inspected here. This Linux host could not launch Wine's IPC server.
- Expand XML and render fixtures using authentic 2.x, 7X and AV applications and their files. Exercise original readers on Reborn exports and document unknown-field handling.
- Verify editor lifecycle, settings, queue/cancellation, GPU equivalence and image quality on actual hardware when those components exist.
- Perform the separate native 16,384 × 16,384 acceptance test following implementation of memory-aware high-resolution rendering. No 16K acceptance test was attempted in this checkpoint.
