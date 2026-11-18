# Variation coverage — 0.11.0

The CPU registry contains **43 implemented variations**. Every entry appears in the editor's variation-weight table, accepts signed finite weights, imports and exports by its historical name, and has a study in `examples/variation-studies.flame`. A zero-weight entry contributes nothing and consumes no random numbers. This is an explicit supported subset; the complete historical plugin and variation inventory remains open.

## Milestone 11 additions

Each of the following built-ins supports a signed weight in ordinary transforms and the final transform. Attribute names use the variation name as a prefix, for example `perspective_dist`.

| Name in a flame file | Effect | Parameter suffixes |
| --- | --- | --- |
| `blob` | Modulates radius by angular waves | `low`, `high`, `waves` |
| `pdj` | Combines four sine and cosine terms | `a`, `b`, `c`, `d` |
| `fan2` | Splits the angle into rotating sectors | `x`, `y` |
| `rings2` | Folds the radius into rings | `val` |
| `perspective` | Projects the plane with a tilt and viewing distance | `angle`, `dist` |
| `juliascope` | Chooses a Julia branch and reflects alternating branches | `power`, `dist` |
| `gaussian_blur` | Draws a random direction and a four-sample blur radius | None |
| `radial_blur` | Combines spin and radial displacement | `angle` |
| `pie` | Samples angular slices | `slices`, `rotation`, `thickness` |
| `ngon` | Scales points around a polygon | `sides`, `power`, `circle`, `corners` |
| `curl` | Warps the plane through a quadratic complex denominator | `c1`, `c2` |
| `rectangles` | Reflects points in a rectangular grid. A zero cell size passes that axis through. | `x`, `y` |

The final twelve entries in `examples/variation-studies.flame` use explicit parameter values. The third transform in each study contains its named variation. In radial blur the signed weight enters the spin calculation, so changing its sign can change the visible shape.

The normal test suite reads 432 expected point and RNG results from `tests/starting-variation-vectors.tsv`. They cover six input points, two signed weights and three seeds for each operation. The optional generator compiles the separate upstream FLAM3 functions and precomputations. It feeds them the same uniform random stream used for the comparison. These checks establish formula and draw-order agreement for the tested cases. They do not compare FLAM3’s native ISAAC seed sequence to Reborn’s generator.

```bash
python3 tests/starting_variation_generate.py /path/to/flam3 /tmp/starting-variation-vectors.tsv
diff -u tests/starting-variation-vectors.tsv /tmp/starting-variation-vectors.tsv
```

### Producer-specific settings and render checks

Files with generic FLAM3 parameters use the pinned FLAM3 initial values. The historical Delphi implementations randomly initialized some settings in their constructors. Importing an active variation in a tagged file requires its saved values when that producer's defaults depend on randomness. When editing such a file, retain the parameters from the source. The studies supply explicit settings, including a nonzero viewing distance for perspective. A singular projection can produce an invalid orbit step. The Random and Mutate tools use deterministic authoring values when introducing effects.

In FLAM3, Gaussian blur draws its direction first and accumulates four fresh draws for its radius. Apophysis 2.09's x87 Gaussian shares its four-value history with pre-blur. The 7X Pascal version draws a separate replacement value after drawing the direction. Historical radial blur maintains its own history. The main and final transforms keep independent histories. Other producer-specific checks cover the 7X fan2 axis order, the JuliaScope single-branch shortcuts and origin cases for blob, ngon and zero-angle perspective.

The seven steps below run `pre_blur` at 0.2, `linear` at 0.3, `gaussian_blur` at 0.7 and `radial_blur` at −0.61 with angle 0.37. Each step starts at (0.63, −0.47) and carries its random histories forward. The normal suite checks the full-precision X and Y values as well as the next uniform draw, 0.37532764673233032. The table rounds coordinates to six decimal places.

| Step | Uniform draws consumed through this step | X | Y |
| ---: | ---: | ---: | ---: |
| 1 | 13 | 0.025105 | 0.122804 |
| 2 | 18 | −0.355424 | −0.088719 |
| 3 | 23 | 0.526688 | −0.083155 |
| 4 | 28 | −0.063467 | 0.098141 |
| 5 | 33 | 0.239409 | −0.175681 |
| 6 | 38 | 0.431094 | 0.322800 |
| 7 | 43 | 0.735344 | −0.343177 |

The reference driver compiles the original Apophysis 7X `TXForm.PreBlur`, `TXForm.Gaussian` and `TVariationRadialBlur.CalcFunction` methods. It substitutes a fixed stream for Delphi's global random generator and initializes the radial history on its first use. The generator verifies both upstream file hashes before compilation. The reference run used Free Pascal 3.2.2 on Linux. Set `FPC` to use a different compiler or unit paths. [Original 7X transform methods](https://github.com/xyrus02/apophysis-7x/blob/1cefa4099927e296d365412bd0b91b96f85606ca/src/Flame/XForm.pas) · [Original radial blur method](https://github.com/xyrus02/apophysis-7x/blob/1cefa4099927e296d365412bd0b91b96f85606ca/src/Variations/varRadialBlur.pas)

```bash
python3 tests/producer_blur_generate.py /path/to/apophysis-7x
```

## Classic formulas added in Milestone 5

These ten functions come from the original Apophysis 2.09 and FLAM3 implementations. They execute between the pre-affine and post-affine matrices, alongside other active variation weights.

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

The model uses FLAM3 variation IDs. For original Apophysis 2.x, numbered `var` and `var1` records use the 2.x registry, including `gaussian_blur` at 28 and `pre_blur` at 29. With the exact stock `Apophysis 2.09` version string, the Delphi 7 project's unit initialization order also identifies IDs 30–39 as `radial_blur`, `rings2`, `fan2`, `blob`, `pdj`, `perspective`, `julian`, `juliascope`, `curl` and `rectangles`, respectively. The reader applies that order to stock 2.09 mixtures unless the source specifies `var_order`. Later indices depend on DLLs installed at export. The reader requires named weights for these and for the reordered 3D-hack, 7X and AV registries. Original source references are `Source/XFormMan.pas`, `Source/XForm.pas` and `Project/delphi7/Apophysis.dpr`.

For the Milestone 5 formulas, unidentified/FLAM3 documents use epsilon `1e-10`; identified Apophysis documents use the existing `1e-300` historical path. Original 2.09's guarded radius, truncated ring expression and fractional fan comparison are retained on that path. FLAM3's remainder expression is retained on the generic path. Fisheye and eyefish deliberately differ by an axis exchange. [FLAM3 variations and precomputation](https://github.com/scottdraves/flam3/blob/1cdfc77275821182ad3c057e629c75a240dd52d3/variations.c) · [Original Apophysis 2.09 source archive](https://downloads.sourceforge.net/project/apophysis/apophysis/Apophysis%202.09/Apophysis-2.09-src.7z)

The Milestone 5 source-level numerical comparison covers FLAM3 and original 2.09. It does not establish pixel equivalence with every 7X/AV plugin bearing a similar name, Delphi's x86 assembly branches, or every extreme floating-point input. In particular, singular generic power/rings inputs at the origin and overflowing exponential/hyperbolic functions can produce an invalid orbit step; the renderer's existing recovery and invalid-step reporting handle those cases. No singular result is silently replaced with another variation.

## Complete supported names

| Group | Registered names |
| --- | --- |
| Original IDs 0–14 | `linear`, `sinusoidal`, `spherical`, `swirl`, `horseshoe`, `polar`, `handkerchief`, `heart`, `disc`, `spiral`, `hyperbolic`, `diamond`, `ex`, `julia`, `bent` |
| Added classic formulas | `waves`, `fisheye`, `popcorn`, `exponential`, `power`, `cosine`, `rings`, `fan`, `eyefish`, `cylinder` |
| Earlier compatibility additions | `bubble`, `noise`, `julian`, `blur`, `foci`, `pre_blur` |
| Milestone 11 formulas | `blob`, `pdj`, `fan2`, `rings2`, `perspective`, `juliascope`, `gaussian_blur`, `radial_blur`, `pie`, `ngon`, `curl`, `rectangles` |

JuliaN exposes an integer `julian_power` and finite `julian_dist`. The Milestone 11 table above lists the additional named parameters. The built-in registry now covers all 40 variations named in the initial brief plus `noise`, `foci` and `pre_blur`.

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
