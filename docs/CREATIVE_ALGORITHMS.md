# Creative algorithms — 0.5.0

`src/creative.hpp` and `src/creative.cpp` provide the Qt-independent generation,
mutation, palette and camera-fitting functions. The desktop dialogs use these
functions and apply accepted changes through the undoable document layer.

The generator and mutation distributions are Reborn's deterministic implementation.
Historical Apophysis random-number sequences and candidate distributions have not
been reproduced. Mathematical variation behavior remains governed by the existing
producer-aware engine and its reference tests.

## Random flames

`random_flames(RandomOptions)` accepts a 64-bit seed, a batch size from 1 to 256,
minimum and maximum ordinary-transform counts from 2 to 12, an explicit variation
allowlist, contraction and translation bounds, output dimensions, palette
randomization and optional symmetry. The UI can provide a smaller convenient
batch-size range. Each name includes its original seed and one-based batch index.

The default allowlist is linear, sinusoidal, swirl, spherical, horseshoe, bubble,
Julia and disc. An explicit allowlist replaces those defaults. The list is sorted
and deduplicated before sampling, so changing checkbox order does not change the
result. Pre-blur alone is rejected because it does not provide an ordinary output
variation.

Each ordinary affine transform combines a rotation with two independently sampled
contractions. Its translation lies inside the requested bound; branch locations
are spread around a ring with random offsets. A chosen nonlinear variation is
blended with linear only when linear is allowed. Otherwise the chosen ordinary
variation supplies the whole contribution. Pre-blur is added only when allowed.
JuliaN receives a nonzero power and a finite distance if it is chosen. Transform
probabilities are positive, and palette coordinates are distributed across the
available range.

The contraction limits constrain the affine map before its nonlinear variations.
They do not guarantee that the combined nonlinear orbit is contractive. Every
result is validated and subjected to a bounded orbit-framing pass. A candidate
that produces too few finite samples or a degenerate point is retried with a
deterministically derived seed, up to 12 attempts. Exhaustion produces an
actionable error without a partial returned batch.

### Symmetry

Rotational order `n` adds `n - 1` linear rotation branches, at equal angular
intervals. Reflection adds one linear branch that negates X. These branches have
color speed zero, so symmetry operations retain the orbit's color. They are
additional to the requested ordinary-transform count and are explicit in the
saved flame. Their required linear variation is independent of the ordinary
generation allowlist.

An order of one adds no rotation branch. Supported orders are 1 through 12. The
total can therefore exceed the ordinary-transform maximum of 12.

### Seeds and framing

Independent candidate streams are derived with the SplitMix64 finalizer and then
draw from the engine's `Rng`, which converts MT19937-64 output to exact 53-bit unit
values. Repeating the same options reproduces the same candidates. Increasing a
batch's size retains its earlier candidates. Platform mathematical libraries can
still produce last-bit differences in transcendental formulas and subsequent
orbits; cross-platform bit-identical images are not claimed.

`fit_camera` samples an orbit without rendering an image. It excludes a burn-in,
resets invalid trajectories, and uses the central 98% of sampled X and Y
coordinates by default. A 12% composition margin fits that range into the image
aspect ratio. Camera rotation and zoom are respected. A final transform affects
the plotted bounds without entering the iteration feedback. Generic final
opacity and historical final-transform behavior follow the renderer's selection
convention. A degenerate or unusable orbit returns `false` and leaves the camera
unchanged.

Palette randomization uses a separate derived stream. Disabling it uses the
model's neutral grayscale palette; a caller can substitute the current palette.
Output dimensions are validated up to 16,384 on either axis. This input range is
not evidence that the separate 16K rendering acceptance milestone has passed.

## Mutation candidates

`mutation_candidates(parent, MutationOptions)` returns independent candidates in
a stable order. It does not modify its parent. `mutate_flame` exposes one sequence
index directly, which lets the interface regenerate a particular candidate.

Strength is in `[0,1]`. Zero strength returns an exact copy of the original model.
Trend is in `[-1,1]` and biases signed numerical perturbations: zero is unbiased,
positive values favor increases and negative values favor decreases. It is a
directional parameter bias, not a target flame or historical mutation-trend
compatibility mode.

| Enabled group | Changes |
| --- | --- |
| Geometry | Pre-affine rotation, scale, shear and translation. Post-affine coefficients remain as authored. |
| Variations | Existing weights change multiplicatively, retaining their signs. One allowed variation may be introduced or activated. The allowlist governs new introductions; existing authored variations remain. |
| Colors | Palette coordinates wrap around `[0,1)`, and color speed changes within `[0,1]`. Opacity remains as authored. |
| Parameters | Positive selection weights change without changing which branches have zero probability. Active JuliaN powers remain nonzero and distances finite. |
| Palette | A hue shift plus saturation and value adjustments. |
| Include final | The chosen transform groups also affect an existing final transform. No final transform is created implicitly. |

Name, producer, output dimensions, camera, tone, background, palette interpolation
mode, transform count and Xaos topology are retained. The selected parameter
groups do not rewrite other source XML; the desktop document integration patches
only the accepted model differences. Returning to a previous parent or accepting
a candidate is a desktop/history operation rather than an implicit change to
these algorithms.

Mutation preserves valid numeric settings and probability support. It does not
guarantee that every creative candidate is visually attractive or that an
extreme authored orbit remains inside the current camera. Camera fitting is an
explicit separate operation for mutations.

## Palette operations

All operations return a new 256-entry RGB palette. Input palettes must contain
finite channels in `[0,1]`; no operation changes the supplied source object.

Color stops have unique integer indices from 0 to 255. Two to 256 stops are
supported. Stops can arrive in any order. Their exact colors are retained at
their own indices, and entries outside the first/last stop use the nearest stop.
Intermediate entries use either direct RGB-channel interpolation or HSV
interpolation along the shortest hue arc. A gray stop borrows its colored
neighbor's hue; saturation and value still interpolate independently. RGB
interpolation uses stored channel values, without a linear-light conversion.

The gradient-construction interpolation setting is separate from a flame's
step/linear palette sampling mode during rendering.

| Operation | Defined behavior |
| --- | --- |
| Rotate | A positive shift moves original index 0 to that positive index, wrapping at 256. |
| Reverse | Entry `i` becomes entry `255-i`. |
| Smooth | Circular box average with a radius from 0 to 32; radius zero is an exact identity. |
| Hue | Add degrees around the hue circle. |
| Saturation/value | Multiply HSV saturation/value and clamp to the valid range. |
| Channel curves | Apply separate positive red/green/blue gamma parameters, then centered contrast. |
| Randomize | Seeded, closed-loop colors with alternating dark/light stops; 3 to 32 stops. |

For an RGB channel `x`, a channel curve computes
`clamp((pow(x, 1/gamma) - 0.5) * contrast + 0.5, 0, 1)` after the HSV adjustment.
Gamma one and contrast one leave that stage unchanged. An entirely neutral
adjustment returns the original palette exactly, avoiding a needless RGB/HSV
round trip. These are palette curves, not histogram tone curves.

Seven original preset palettes are included: Ember, Glacier, Orchid, Forest,
Copper, Spectrum and Monochrome.

### GIMP palette exchange

The `.gpl` reader and writer follow the RGB, 8-bit text representation in GIMP's
[official palette format specification](https://developer.gimp.org/core/standards/gpl/).
The reader accepts an optional UTF-8 BOM, LF or CRLF lines, comments, palette
metadata and optional color names. Two to 256 colors are accepted. A 256-color
file preserves each encoded index; a smaller palette is distributed across the
256 entries using RGB interpolation. Invalid channels and oversized palettes are
rejected. Input is limited to 4 MiB.

Export writes all 256 entries, rounding each channel to the nearest 8-bit value.
The palette's display name is included, while individual imported color names
are not retained in the flame model. GPL does not encode alpha or the editor's
gradient stops and curves; the exported file contains their resulting colors.
GIMP `.ggr`, Photoshop palette formats and multi-palette containers are outside
this exchange implementation. The desktop library stores separate named GPL
files, and flame-library palette import uses the legacy document reader.

## Verification

`tests/creative_tests.cpp` checks exact stop values, independently calculable RGB
and HSV midpoints, wrapped palette operations, channel-curve values, malformed
files and 256-entry GPL round trips. Generator checks cover reproducibility,
batch-prefix stability, allowlists, affine bounds, explicit symmetry matrices,
and visible image output from default batches. Mutation checks cover group
isolation, signed variation weights, probability support, unchanged camera and
metadata, zero-strength identity, final-transform opt-in and invalid settings.
Camera checks include final-transform translation and degenerate-orbit rollback.
