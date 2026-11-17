# Variation coverage — 0.5.0

The CPU registry contains **31 implemented variations**. Every entry appears in the editor's variation-weight table, accepts signed finite weights, imports and exports by its historical name, and has a study in `examples/variation-studies.flame`. A zero-weight entry contributes nothing and consumes no random numbers. This is an explicit supported subset; the complete historical plugin and variation inventory remains open.

## Additions in this checkpoint

The ten new functions come from the original Apophysis 2.09 and FLAM3 implementations. They execute between the pre-affine and post-affine matrices, alongside other active variation weights.

| Name | FLAM3/model ID | Original 2.09 ID | Behavior and controls |
| --- | ---: | ---: | --- |
| `waves` | 15 | 15 | Adds sinusoidal displacement. The pre-affine coefficients `c` and `d` set its X/Y amplitudes; `e²` and `f²`, with the producer's epsilon, set its denominators. |
| `fisheye` | 16 | 16 | Radial compression by `2 / (r + 1)`, with the historical X/Y exchange retained. |
| `popcorn` | 17 | 17 | Adds `sin(tan(3y))` and `sin(tan(3x))`, scaled by pre-affine `e` and `f`. |
| `exponential` | 18 | 18 | Exponential radius `exp(x − 1)` and angle `πy`. |
| `power` | 19 | 19 | Raises the input radius to the sine of its historical polar angle. |
| `cosine` | 20 | 20 | Complex cosine using `πx`, `cosh(y)` and `sinh(y)`. |
| `rings` | 21 | 21 | Folds radial distance into rings whose spacing derives from pre-affine `e²`. |
| `fan` | 22 | 22 | Folds angular sectors using pre-affine `e²` and phase `f`. |
| `eyefish` | 27 | 23 | The radial compression used by fisheye, with the original X/Y orientation retained. |
| `cylinder` | 29 | 25 | Applies `sin(x)` and preserves `y`. |

The coefficient names follow the stored XML order: `x′ = a·x + c·y + e` and `y′ = b·x + d·y + f`. The triangle and numeric matrix controls therefore also change the four affine-coupled variations. These variations do not introduce independent named parameter fields.

The pre-affine values are passed explicitly into the variation evaluator. They are never inferred from the already transformed point or copied into stale cached parameters. Final transforms use their own pre-affine matrix in the same way.

## Producer interpretation

Named weights retain their names when a legacy document is edited. The canonical model uses FLAM3 variation IDs internally. Numbered `var` and `var1` input from original Apophysis 2.x uses that producer's registry: its first 23 IDs agree with FLAM3, while eyefish is 23 and cylinder is 25. Previously implemented bubble, noise, blur and pre-blur retain their original 2.x mappings. A numeric identifier in a 3D-hack, 7X or AV document remains rejected because its reordered/plugin-dependent registry cannot be inferred safely from the version label.

For the new formulas, unidentified/FLAM3 documents use epsilon `1e-10`; identified Apophysis documents use the existing `1e-300` historical path. Original 2.09's guarded radius, truncated ring expression and fractional fan comparison are retained on that path. FLAM3's remainder expression is retained on the generic path. Fisheye and eyefish deliberately differ by an axis exchange. [FLAM3 variations and precomputation](https://github.com/scottdraves/flam3/blob/1cdfc77275821182ad3c057e629c75a240dd52d3/variations.c) · [Original Apophysis 2.09 source archive](https://downloads.sourceforge.net/project/apophysis/apophysis/Apophysis%202.09/Apophysis-2.09-src.7z)

The new source-level numerical comparison covers FLAM3 and original 2.09. It does not establish pixel equivalence with every 7X/AV plugin bearing a similar name, Delphi's x86 assembly branches, or every extreme floating-point input. In particular, singular generic power/rings inputs at the origin and overflowing exponential/hyperbolic functions can produce an invalid orbit step; the renderer's existing recovery and invalid-step reporting handle those cases. No singular result is silently replaced with another variation.

## Complete supported names

| Group | Registered names |
| --- | --- |
| Original IDs 0–14 | `linear`, `sinusoidal`, `spherical`, `swirl`, `horseshoe`, `polar`, `handkerchief`, `heart`, `disc`, `spiral`, `hyperbolic`, `diamond`, `ex`, `julia`, `bent` |
| Added classic formulas | `waves`, `fisheye`, `popcorn`, `exponential`, `power`, `cosine`, `rings`, `fan`, `eyefish`, `cylinder` |
| Earlier compatibility additions | `bubble`, `noise`, `julian`, `blur`, `foci`, `pre_blur` |

JuliaN continues to expose integer `julian_power` and finite `julian_dist`. All other entries in this subset have no independent named numeric variables. The applicable affine coefficients, producer-specific stochastic behavior and pre-blur ordering still matter.

Gaussian blur, JuliaScope, blob, PDJ, fan2, rings2, perspective, radial blur, pie, ngon, curl, rectangles and the broader plugin inventory remain unimplemented. Gaussian blur was not added in this checkpoint: its historical implementations differ in their random draw order, cached ring state and sharing with pre-blur. Treating those paths as interchangeable would overstate compatibility. GPU execution of every variation remains subsequent work.

## Independent numerical evidence

`tests/extended-variation-vectors.tsv` contains **640 independent transform results**: 320 produced by compiling the original FLAM3 C functions and 320 by compiling the original 2.09 Pascal methods. The harness extracts and compiles their bodies from separate, hash-checked source trees. It does not call or copy the Reborn implementation to generate expected values.

Each producer runs all ten variations through two asymmetric pre-affine matrices, positive and negative weights, eight inputs spanning quadrants/axes/a near-origin point, and one asymmetric post-affine matrix. This checks coefficient coupling, weighting, axes and operation order. Each coordinate must agree to `1e-12 × max(1, |reference|)`. All 640 passed. The prior 126 FLAM3 vectors and original image/CLI checks also passed after the registry expansion.

The new `extended_variations` test additionally checks name/number import, save/reopen, the classic registry gap, preservation of random state by deterministic terms, singularity guards, the fisheye axis distinction and all ten new sample renders. The samples run at 120 × 90 with 80,000 samples and seed 123456; each produced a nonuniform, framed image with zero invalid orbit steps.

| Pinned input or output | SHA-256 |
| --- | --- |
| FLAM3 `variations.c` | `e5880158e4dd35cf9f4e89be23fe3d659954e6574cb218db5e20279e53cefab2` |
| FLAM3 `private.h` | `548a082cd1f73bb5180b69220e94a5641a61b49403cb68eb5540c2cfd685c2f0` |
| Apophysis 2.09 `Source/XForm.pas` | `0509deeacdbcd5878c4ef20bcc2488f07228b13c1a375fab9b37e4a6f08e6950` |
| Generated vector table | `d89fcc9e8a3025daa67c4f86406d23ee7e1df0575158a7beac91bd2fd40e72b9` |

The source revisions are FLAM3 commit `1cdfc77275821182ad3c057e629c75a240dd52d3` and the original Apophysis 2.09 source archive, whose archive SHA-256 is recorded in [PRODUCER_ORACLE.md](PRODUCER_ORACLE.md). FLAM3 credits Spotworks LLC and is GPL-3.0-or-later; the original Apophysis source credits Mark Townsend and subsequent contributors and is GPL-2.0-or-later. The optional regeneration builds are temporary external reference programs. No original source tree or reference executable is required by the application or normal tests. [FLAM3 source/license](https://github.com/scottdraves/flam3/tree/1cdfc77275821182ad3c057e629c75a240dd52d3) · [Existing dependency and reference notices](LICENSES.md)

To regenerate with a C compiler, Free Pascal and the separately obtained source trees:

```bash
python3 tests/extended_variation_generate.py /path/to/flam3 /path/to/apophysis-209 /tmp/extended-variation-vectors.tsv
diff -u tests/extended-variation-vectors.tsv /tmp/extended-variation-vectors.tsv
```

`CC` and `FPC` can specify alternate compiler commands and include/unit flags. The recorded vectors were generated with GCC 14.2 and Free Pascal 3.2.2 on x86-64 Linux. Last-bit changes in another math library can affect regenerated text; the C++ regression uses the numerical tolerance above.
