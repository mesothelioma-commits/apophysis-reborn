# Bounded Vulkan sampling — 0.7.0

Large file renders can obtain global samples from Vulkan without allocating a
full image histogram on the device. The new `gpu::sample_hits` path uses the same
shader formulas, portable random streams, chain boundaries, camera projection,
palette lookup and integer contribution rules as the dense renderer introduced
in 0.6.0. Storage partitioning occurs after sampling. The sampler does not crop
or restart the flame independently for each output strip.

## Range and record contract

`RenderOptions.samples` is the complete job's requested iteration count. A call
selects between 1 and 1,024 existing chains by their absolute 64-bit chain index.
Each chain owns 256 requested iterations and 32 burn-in iterations. The last
chain of the complete job can be partial. A chunk ending earlier uses a full
256-iteration final chain; the number of iterations is not inferred from the
chunk's size. Changing chunk boundaries therefore retains the same streams.

The supplied `gpu::Geometry` always describes the complete padded histogram.
Coordinates are projected with the full image's dimensions, camera scale,
rotation, oversampling and filter padding. Producer-specific rounding still
uses the full histogram dimensions.

Each returned record contains six unsigned 32-bit words:

| Word | Meaning |
| --- | --- |
| 0 | Global histogram x coordinate |
| 1 | Global histogram y coordinate |
| 2–4 | Integer red, green and blue contributions |
| 5 | Integer opacity/mass contribution |

Records are compacted into chain order and then iteration order. Their values
can be accumulated into the existing 64-bit bins without further rounding.
The result includes plotted and invalid-step counts, the actual device name,
and a sampling identity for checkpoint compatibility.

## Device storage and limits

Shader mode 2 uses binding 1 for fixed records instead of dense histogram words.
Each invocation owns 256 record slots; no two invocations write the same slot.
Unwritten slots retain an `0xffffffff` x sentinel. Valid coordinates remain
below the existing exact-FP32 integer limit and cannot equal that sentinel.
The counters retain their original low/high 32-bit atomic representation.

One call uses at most **6 MiB** of device hit records for 1,024 chains. The caller
can select smaller ranges. The host readback buffer and returned vector each
need at most the same record capacity; model, counter and descriptor resources
are additional. The runtime checks storage-buffer/workgroup limits and charges
actual Vulkan allocation requirements against the configured host and GPU
budgets. The returned vector capacity and host packed-model vector are also
charged. `host_reserved_bytes` describes other storage retained by the caller.
As in the dense backend, a reported device heap capacity is not a measurement
of currently free memory.

Dense mode retains its 32-bit histogram-word address limit. Hit mode addresses
x and y separately and does not require the full frame's bin product to fit
that range. It still enforces the existing model admission rules, normal FP32
field conversion, exact coordinate-dimension limits, probability resolution
and per-call device/host memory budgets. Enabling hit streaming does not enable
previously unsupported variation parameters.

The runtime waits for a bounded submission before reading back or freeing its
resources. Cancellation propagates as `RenderCancelled`; it does not become
CPU fallback inside this function. The hit sampler does not call the progress
callback. The outer file-render job reports progress after it has committed a
complete chunk, so displayed restart-safe progress need not rely on uncommitted
GPU work.

## Resume identity

The returned identity contains the hit-stream contract version, application
sampling version, selected device ID, API version, driver version, pipeline
cache UUID, and a fingerprint of every byte of the compiled SPIR-V module.
The FNV-1a-64 shader fingerprint identifies algorithm compatibility; it is not
a cryptographic integrity check for checkpoint contents. The file-render
layer must validate its own stored data and must reject an incompatible
sampling identity before incorporating a new chunk into a resumed job.

## Executed comparison scope

`tests/gpu_stream_tests.cpp` compares the complete integer histogram obtained by
folding returned hit records with the existing dense Vulkan histogram. It
requires exact equality of every bin and both counters. It also requires exact
record equality when the same job is split at different chain boundaries,
including a partial final chain.

The development run executed on **llvmpipe (LLVM 19.1.7, 256 bits)** with Khronos
validation enabled. These results are software Vulkan execution evidence.

| Fixture | Plotted | Invalid | Dense histogram and split record equality |
| --- | ---: | ---: | --- |
| Generic stochastic/final-transform flame | 65,539 | 0 | Exact |
| Apophysis 2.09 pre-blur and opacity | 38,202 | 0 | Exact |
| Apophysis 7X pre-blur and opacity | 37,963 | 0 | Exact |
| Deliberately invalid orbit | 0 | 36,829 | Exact |

The suite also executes a **16,384 × 16,384 global camera with oversampling 2**
using a **16 MiB host budget and 8 MiB GPU budget**, verifies repeated records,
checks a chain index crossing `2^32`, and rejects invalid ranges and inadequate
budgets. That bounded-sampling check does not itself create or certify a full
16K image. Complete output decoding, strip/filter consistency, memory and
restart tests belong to the file-render acceptance record.

The existing direct variation probes and dense image comparisons remain
separate gates. Physical GPU/vendor-driver behavior and Windows execution
still require platform acceptance. See [GPU_KERNEL.md](GPU_KERNEL.md),
[GPU_TESTING.md](GPU_TESTING.md), [GPU_RENDERING.md](GPU_RENDERING.md) and
[TEST_PLAN.md](TEST_PLAN.md) for the other contracts and execution boundaries.
