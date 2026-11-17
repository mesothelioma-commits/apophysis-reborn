# Vulkan compute kernel — 0.6.0

The Vulkan backend computes orbit sampling, transform selection, variation evaluation, palette lookup and histogram accumulation in a compute shader. The existing CPU image reconstruction then applies the flame's filtering, density, gamma, vibrancy and background conventions to that histogram. This is a hybrid renderer: the sampling work runs through Vulkan, while image development and file encoding retain the established CPU implementation.

The shader targets Vulkan 1.0 and uses binary32 floating-point arithmetic, storage buffers and 32-bit integer atomics. It requires no optional shader `Int64`, `Float64`, floating-point atomics or 64-bit atomic extension. Discovery still reports the device's additional numeric capabilities; their presence does not change this kernel's precision. The readable source is [orbit.comp](../shaders/orbit.comp), and [gpu_kernel.hpp](../src/gpu_kernel.hpp) defines its buffer contract. The checked-in SPIR-V header allows an ordinary source build without a shader compiler; the shader compilation script reproduces that header from the readable GLSL source.

## Shared flame interpretation

All 31 currently registered variations have a compute implementation. Signed variation weights, declared term order, pre-affine and post-affine matrices, JuliaN parameters, color speed, transform opacity, Xaos transition rows and an optional final transform retain their existing roles. Pre-blur modifies the shared variation input before ordinary terms execute. Historical pre-blur keeps its four-value ring separately for each transform within a sampling stream, including the additional replacement draw used by the supported 7X convention. A disabled stochastic term consumes no random draws.

The transform chooser's robust, normalized CPU cumulative distributions are packed into the model. Missing chaos destinations retain their established defaults. A final transform changes only the plotted copy, never the next orbit state. Palette interpolation, historical palette index rounding, historical integer channel quantization, generic opacity weighting and the historical opacity probability test follow the existing renderer's interpretation.

The shader has a limit of **64 ordinary transforms**, plus one final-transform slot. This is a compute implementation limit, not a new file-format limit. Models outside the compute admission rules remain available to the CPU renderer. [VARIATIONS.md](VARIATIONS.md) documents the mathematical definitions and earlier original-source evidence for the CPU registry.

## Portable sampling streams

The established CPU renderer keeps its MT19937-64 generator and 8,192-sample batch boundaries. The compute path uses separate, explicitly defined portable streams. The CPU comparison function uses the same portable streams and chain boundaries as the shader, but executes the existing double-precision CPU formulas. It does not replace the normal CPU rendering mode.

Each compute invocation owns one **256-sample chain**, preceded by **32 burn-in iterations**. The last chain may contain fewer requested samples. The chain's seed depends on the user's entire 64-bit seed and its 64-bit chain index. Dispatch partitioning does not choose a seed or change a chain boundary. Invalid orbit steps restart the point and color and reset the burn-in counter; they do not reset the random stream or its existing pre-blur rings.

All arithmetic in the following generator definition wraps modulo 2³². Its mixing function is:

```text
x ^= x >> 16
x *= 0x7feb352d
x ^= x >> 15
x *= 0x846ca68b
x ^= x >> 16
```

The initial state is `mix(seed_low XOR mix(seed_high + 0x9e3779b9) XOR mix(chain_low + 0x85ebca6b) XOR mix(chain_high + 0xc2b2ae35))`. A draw advances the state with `state = state * 747796405 + 2891336453`, then evaluates:

```text
word = ((state >> ((state >> 28) + 4)) XOR state) * 277803737
word = (word >> 22) XOR word
unit = (word >> 8) * 2^-24
```

This PCG-style 32-bit permutation produces a unit value in `[0,1)` with 24 binary fraction bits. Every such draw is exactly representable in both binary32 and binary64. High seed and chain bits influence the state, but the mapping into a 32-bit state is not one-to-one. These are deterministic Reborn compute streams; they do not reproduce historical Apophysis random sequences or the normal CPU renderer's MT19937-64 sequence.

## Exact integer accumulation

Every histogram bin stores four unsigned 64-bit sums: red, green, blue and opacity. Each sum is represented by a low/high pair of 32-bit storage-buffer words. For each contribution, the shader atomically adds to the low word and obtains its previous value. If the unsigned addition wraps, it atomically increments the high word.

This preserves the exact integer sum without requiring a 64-bit atomic operation. The atomic low-word additions have a definite order, and each wrap contributes exactly one carry. High-word additions commute. The renderer reads a completed histogram only after the dispatch has finished and the required memory synchronization has made its writes visible; it never reads a partially updated low/high pair.

Generic color and opacity contributions use the existing 24-bit fixed-point scale, `2²⁴ − 1`. The binary32 conversion is clamped to that maximum after rounding, preventing an all-white contribution from rounding up to `2²⁴`. Historical inputs use the existing integer channel quantization and opacity mass of 200. Histogram addition is exact for the contributions produced by a given shader run. Floating-point variation and color calculations before that quantization can still differ between CPU and devices.

Each bin occupies **32 bytes**. Dense histogram storage, readback staging, model buffers, output pixels and reconstruction storage remain subject to the applicable host and device budgets. The Vulkan runtime also checks the selected device's storage-buffer and dispatch limits. This milestone does not add tiled histograms or establish 16K rendering acceptance.

## Precision admission and fallback

The model packer checks values before converting them to binary32. It rejects nonzero values that overflow, become zero or become subnormal at that precision. This covers camera values, affine coefficients, variation weights, transform parameters, effective opacity and palette channels. JuliaN powers must fit the exact binary32 integer range. Camera and histogram dimensions must also fit their declared addressing and exact-integer limits.

A positive transform-selection interval must survive binary32 cumulative-boundary conversion and be at least `2^-24`, the portable sampler's unit resolution. Otherwise the compute path reports that probability as unsupported. This prevents a tiny but nonzero branch from silently becoming unreachable or receiving the probability of the first representable random draw.

The historical `1e-300` guards cannot be stored in binary32. The shader explicitly handles the finite zero-radius cases of historical spherical, horseshoe, hyperbolic, power and rings. Historical waves, rings and fan require their active affine-coupled denominators to remain nonzero and within normal binary32 range; a zero or unrepresentable denominator requests CPU fallback. Generic formulas retain their `1e-10` guard.

These admission checks protect the packed input. They do not promise that every intermediate point of an arbitrary nonlinear orbit remains within binary32 range. Overflowing, singular or otherwise invalid orbit results use the existing invalid-step accounting and recovery behavior. Trigonometric approximations, cancellation, discontinuities and chaotic amplification can produce different trajectories even for admitted models. Equal seeds therefore do not imply identical pixels across CPU and GPU, between different GPU drivers, or between the normal CPU and compute sampling sequences.

When CPU fallback is enabled, a missing loader, unavailable device, unsupported model or applicable resource/capability failure can be reported together with the actual CPU result. Strict Vulkan rendering reports the failure instead. Cancellation remains cancellation and does not restart a CPU render. Device capability detection and the output's actual backend must be used to interpret a result; a Vulkan-capable software device is not evidence of physical GPU acceleration.

## Executed numerical evidence

The compute kernel was executed through **Mesa llvmpipe, LLVM 19.1.7**, a software Vulkan implementation. The host has no exposed physical GPU device. This verifies a real Vulkan compute dispatch and its results, but does not establish hardware performance or Windows-driver acceptance.

`gpu-kernel-tests` compares **1,560 direct CPU/GPU transform probes** against the existing CPU `apply_transform` function. They cover all 31 variations, positive and negative weights, two asymmetric pre-affine matrices and an asymmetric post-affine matrix, eight inputs, generic/classic/7X parameter conventions, mixed stochastic term order, repeated historical pre-blur ring use, and the final-transform slot. Each probe also checks the next random draw exactly, which detects a changed number or order of stochastic draws.

All probes passed. The maximum observed normalized coordinate error was **1.7820791 × 10⁻⁵**, against a bound of **2 × 10⁻⁴ × max(1, |CPU reference|)** for each coordinate. Every subsequent random draw matched exactly. The companion CPU-only checks verify packing, geometry and probability admission, zero-probability destinations, and **16,384 portable random draws**, including nonzero high seed and chain bits. These are bounded formula and stream tests, not a claim of bit-identical full-render output across arbitrary devices.

The full rendering, cancellation, fallback, accumulator-carry and distribution checks are recorded with the milestone's [verification report](TEST_PLAN.md). Physical GPU speed, Windows execution and broader driver equivalence remain separately reported acceptance work.
