# CPU and Vulkan verification — 0.7.0

Version 0.7.0 retains the M6 numerical and dense-image comparison methods below and adds exact GPU hit-stream, streamed file reconstruction and checkpoint tests. `gpu_stream` compares global hit/counter results with the dense path across split ranges and 16K coordinates; `file_render` includes six exact GPU dense/strip RGB8 cases and device/shader-bound resume. Both accept an explicitly selected device through their executable options. The complete M7 Release and sanitizer suites each pass 25 entries. Full 16K execution, independent decoding and CPU strip/queue equality are detailed in [HIGH_RES_ACCEPTANCE.md](HIGH_RES_ACCEPTANCE.md). The original M6 image measurements below remain labeled by their executed scope.


The Vulkan renderer has two distinct comparison paths. The established CPU
renderer keeps its original MT19937-64 streams and 8,192-sample batches. A
separate `render_compute_reference` path evaluates the CPU's double-precision
formulas using the compute kernel's portable random stream and 256-sample chain
boundaries. Matching streams make numerical differences observable without
confusing a different random sequence with a shader defect.

Vulkan sampling uses FP32 arithmetic. Density reconstruction remains shared CPU
code. The comparisons below establish the specified behavior for the admitted
models and tested implementation; they do not promise identical chaotic orbits
on every graphics driver or historical application.

## Integration fixtures and acceptance criteria

`tests/gpu_tests.cpp` verifies that an actual Vulkan render reports the Vulkan
backend and an empty fallback reason before comparing its pixels. It does not
accept a successful CPU fallback as evidence of GPU execution. Every comparison
requires equal plotted and invalid-step counts, a useful nonempty image, and no
invalid orbit steps.

| Fixture | Behavior checked | Pixel criterion |
| --- | --- | --- |
| Generic impulse with final translation | Final transform affects plotting only; odd output dimensions keep the converged point away from pixel edges | Exact RGB8 equality |
| Original 2.09 impulse | Historical integer buckets, projection rounding, oversampling of two, Gaussian reconstruction | Exact RGB8 equality |
| Generic nonlinear | Mixed linear/sinusoidal/bubble/noise/fisheye/cylinder terms; nonuniform transform weights; Xaos rows; conditional final transform; opacity; linear palette; rotated camera; background, gamma threshold and vibrancy | Bounds below |
| Original 2.09 nonlinear | Historical palette/bucket/opacity behavior, pre-blur ring and shared angle behavior, final transform | Bounds below |
| 7X nonlinear | Historical behavior with the separate pre-blur angle stream | Bounds below |

Each image comparison uses 65,539 requested samples, including a three-sample
partial final chain, and the 64-bit seed `0x9f123456abcdef01`. The nonlinear
fixtures are bounded and contractive. Their parameters avoid singularities,
while their nonuniform selection and stochastic variations exercise the
sampling pipeline. The nonlinear outputs are 128 × 96 with oversampling of two
and a Gaussian filter radius of 0.5.

The nonlinear release gate requires all of:

- Mean absolute channel error at most **0.25 of one RGB8 code value**.
- Root-mean-square channel error at most **2 code values**, or about 0.8% of
  the full 0–255 channel range.
- Maximum individual channel error at most **32 code values**.

These are image-error bounds, not a relative comparison against arbitrary tiny
floating-point coordinates. A small FP32 coordinate difference can move a hit
across a histogram-bin boundary. The mean and RMS requirements constrain the
total visible discrepancy; the maximum requirement separately rejects a large
localized discrepancy. The test prints all three observed metrics, changed
channel counts and orbit counts for each fixture. The limits are fixed in the
test, rather than inferred from the image being tested.

### Executed image results

The 17 November 2026 native Release run on the software device identified below
passed every integration check. The observed pixel errors were stricter than
the permitted nonlinear limits:

| Fixture | Plotted samples, both paths | Invalid steps, both paths | Mean / RMS / maximum RGB8 error |
| --- | ---: | ---: | --- |
| Generic impulse | 65,539 | 0 | 0 / 0 / 0 |
| Original 2.09 filtered impulse | 65,539 | 0 | 0 / 0 / 0 |
| Generic nonlinear | 65,539 | 0 | 0 / 0 / 0 |
| Original 2.09 pre-blur | 50,283 | 0 | 0 / 0 / 0 |
| 7X pre-blur | 50,208 | 0 | 0 / 0 / 0 |

Historical opacity is a plot probability, so the two historical nonlinear
fixtures plot fewer than the requested 65,539 iterations. Their CPU reference
and shader counts agree exactly. Identical RGB8 images for these fixtures do
not establish identical FP32 and double coordinates for every orbit step.

The separate `tests/gpu_kernel_tests.cpp` suite measures coordinates before
image reconstruction. Its 1,560 probes cover all 31 variation functions,
positive/negative weights, generic/classic/7X conventions, mixed term order,
repeated pre-blur and the final-transform slot. The probe methods, numerical
tolerance and observed errors are recorded in [GPU_KERNEL.md](GPU_KERNEL.md).

## CPU continuity and backend selection

The integration suite also pins an image generated by the retained, unmodified
0.5.0 native executable. Its four-transform affine fixture uses dyadic
coefficients and offsets, a step palette, 96 × 72 output, seed 123456 and 65,539
samples. The FNV-1a-64 digest of its 20,736 RGB bytes is
`09da8711936d84d8`. This is a regression identity, not a cryptographic checksum.
Default rendering and an explicit CPU request must retain the image, including
when the thread count changes or a GPU selector is present. The earlier
independent numerical, image and historical-producer tests remain separate
gates for CPU compatibility.

The suite checks these selection and failure rules:

- A missing explicit device is an error when fallback is disabled. With
  fallback enabled, the returned backend must be CPU, the reason must be
  nonempty, and the pixels must match ordinary CPU rendering.
- If discovery finds no usable hardware GPU, automatic selection excludes a
  software Vulkan device. An explicitly selected usable software device can
  still be exercised for validation.
- A strict 1 MiB GPU budget rejects a 256 × 256 histogram. Allowing fallback
  produces a reported CPU result instead.
- A coefficient of `1e-50` is valid for the CPU but outside the normal FP32
  range. The admission check must diagnose it; strict Vulkan rejects it and
  enabled fallback identifies the CPU path.
- Invalid flame settings, zero samples and insufficient host histogram memory
  remain input errors. They are not converted into a fallback render.
- A second identical GPU request must reproduce its image and counters. A new
  seed must change the stochastic fixture.
- Progress starts, remains monotonic and reaches the requested sample count.
  A pre-cancelled request and cancellation after the first committed dispatch
  must throw `RenderCancelled`, including when CPU fallback is enabled.

## CLI and saved-output checks

`tests/gpu_cli.cmake` validates the device-discovery JSON schema and text report,
default/explicit CPU consistency, visible fallback diagnostics, strict device
and budget failures, malformed options and preservation of an existing output
after failure. On a usable device it also performs explicit Vulkan file
rendering through a numeric device selector and checks the PPM signature and
reported backend. Its paths contain spaces.

The CLI contract checks run without a GPU. The C++ integration executable
returns **77** when no usable Vulkan device is available, after exercising its
CPU and selection checks. CTest must record that as skipped, not as a passed
compute execution. The CLI explicitly reports when its actual compute-file
rendering portion was not run.

`gpu-tests --device ID` selects the reported discovery ID or decimal index for
the integration comparisons. `--require-vulkan` changes an unavailable-device
skip to a failure. An explicitly requested missing or unusable device is also
a failure. These options let a hardware verification run name the exact device
being accepted instead of silently testing a different adapter.

## Execution environment and evidence

The development host exposes no physical GPU device. Its Vulkan execution
environment is Mesa **25.0.7-2+deb13u1**, reporting **llvmpipe (LLVM 19.1.7,
256 bits)** with Vulkan **1.4.305** and device type **CPU**. The system loader is
1.4.309. Khronos Validation Layers 1.4.309 are available. The supplied shader is
compiled with glslang reporting 15.2.0 and validated with SPIRV-Tools 2025.1.
The exact downloaded package identities and checksums are retained with the
checkpoint's verification evidence.

A preliminary, independent Vulkan 1.0 storage-buffer dispatch returned all 256
expected integers with zero validation errors. This established a working
compute environment before integrating the renderer. Full renderer results
and metrics are recorded with the executed test logs; a toolchain smoke result
alone is not renderer-equivalence evidence.

Physical-GPU execution on NVIDIA, AMD or Intel hardware and Windows driver
acceptance remain separate checks. Software Vulkan results establish that the
compute shader and API path execute; they establish no hardware speedup.

## 0.11.0 numerical gate

The current kernel coverage includes all 43 built-ins. The [kernel reference](GPU_KERNEL.md) describes direct coordinate probes, RNG comparisons and full-image checks. Run `gpu-kernel-tests`, `gpu-tests` and `gpu-stream-tests` on a Vulkan device to reproduce these checks.
