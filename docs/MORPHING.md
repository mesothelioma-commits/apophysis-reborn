# Morphing rules and historical source audit

Reborn evaluates a morph as a piecewise timeline of valid flame models. The keyframe model is exact at each captured endpoint. Intermediates use explicit transform correspondence, explicit interpolation modes and the original producer's supported variation semantics. Unsupported variations and unapproved native modules are errors; there is no substitute variation chosen to make a clip render.

## Transform correspondence

Each ordinary transform has one stable string ID in the keyframe's timeline metadata. IDs are independent of numeric transform indices and are not inserted into the legacy flame XML. Matching IDs declare corresponding transforms, even when their positions differ between source documents. A segment's intermediate order is the left endpoint's IDs followed by new right endpoint IDs. At the right keyframe the original right order is restored exactly.

The **strict** topology policy requires identical ID sets and equal final-transform presence. It permits explicit reordering. Duplicate or missing IDs, mismatched counts and ambiguous maps are rejected. The two-flame factory can assign `transform-1`, `transform-2`, etc. to both endpoints only when the caller requests positional pairing of equal-sized lists; this is a declared pairing, not a shape matcher.

The **fade-unmatched** policy creates the union of the explicit IDs. A matched transform interpolates normally. A left-only transform retains its own affine, variations, parameters and color settings while its selection weight fades to zero. A right-only transform retains its own shape while its selection weight fades from zero. No phantom source transform is invented to pair two unrelated branches.

Xaos rows and destinations are remapped by those IDs. Missing trailing values in an existing row retain their source default of one. A genuinely absent source row or destination contributes zero. Every corresponding edge interpolates from its left to right multiplier; destination selection weights also interpolate. This avoids accidentally treating an absent branch as a default-one transition. A zero-weight unreachable row may legitimately have no exits. Every interior model is validated so that a reachable row always has a positive destination.

A final transform has one implicit reserved correspondence. In strict mode it must exist at both ends or neither. In fade mode a missing final is an identity affine, unit linear variation, full opacity and zero color-speed contribution, with matching historical variation conventions. It interpolates to/from the existing final. This deliberate identity term is limited to final-transform appearance/disappearance; ordinary unmatched branches use selection-weight fades.

## Continuous and discrete fields

| Data | Interior behavior |
| --- | --- |
| Center, scale, zoom, source `time`, quality | Linear interpolation using the eased segment position |
| Camera rotation | Shortest angular path in degrees, or explicit unwrapped linear degrees |
| Pre/post affine matrices | Selected linear or polar mode, independently for each matrix |
| Transform selection weight, color, color speed, opacity | Linear |
| Variation weights | Union by variation name, with absent contributions fading to/from zero |
| JuliaN distance | Linear when present at both ends; a one-sided term retains its own distance |
| JuliaN integer power | Left-held when present at both ends; a one-sided term retains its own power |
| Native real parameters | Linear for the same exact variation/module identity |
| Native integer parameters | Left-held for matched terms; one-sided terms retain their own values |
| Gamma, threshold, brightness, vibrancy | Linear |
| Filter radius | Linear if present at both ends; presence/absence left-held |
| Background | Encoded RGB interpolation |
| Palette | Per-entry encoded RGB, shortest HSV, linear-light sRGB or shortest HSL |
| Color curves | Linear point coordinates and positive weights, within one common mode |
| Width, height, oversampling, palette lookup mode | Left-held until the right endpoint |
| Name, producer spelling and other non-interpolated metadata | Left-held until the right endpoint |

Left-held fields switch exactly at the next keyframe. This avoids rounding an integer branch count through zero or selecting an unspecified intermediate renderer mode. Preparation reports a diagnostic when discrete render settings or integer parameters differ. A hold segment or two adjacent keyframes is an exact cut and can cross producer families, curve modes and unrelated topology without constructing an intermediate model.

Matching variation terms preserve left-side order, followed by names introduced on the right. There is no normalization of variation weights or substitution of a missing plug-in. A one-sided native term retains its own parameter vector while its contribution fades. Changing transform/variation order, or crossing a discrete parameter boundary, can alter random-number consumption even with a fixed seed.

Interpolated segments require the same producer family and compatible per-transform historical flags. The generic, classic, 3D-hack, 7X and AV identifiers continue to select only the static behaviors already supported by the renderer. A morph does not silently turn an AV-identified flame into a 7X or generic flame. Identical producer labels do not make unsupported fields supported.

## Affine modes

XML affine order is `(a,b,c,d,e,f)` for `x' = a*x + c*y + e`, `y' = b*x + d*y + f`. Translation `(e,f)` always interpolates linearly.

**Linear** mode interpolates all six coefficients directly. It follows the elementary matrix interpolation found in the audited original Apophysis routines, but a 180° rotation can pass through a zero matrix. That behavior is an explicit mode choice.

**Polar** mode uses a right polar decomposition of the 2×2 linear part. For positive determinant, `A = R S`, where `R` is a rotation and `S` is symmetric positive definite. For negative determinant, the implementation first removes the same fixed reflection `F = diag(1,-1)`, and uses `A = R S F`. Matching reflection signs therefore use a consistent decomposition. This is a genuine polar decomposition, not a QR decomposition with an arbitrary triangular shear.

The rotation takes the shortest path. The entries of the symmetric stretch matrix interpolate linearly, preserving positive definiteness in ordinary finite arithmetic. Reconstruction uses the interpolated rotation and stretch, then restores the reflection. An identity-to-180° rotation consequently retains a nonsingular 90° rotation at the midpoint.

The decomposition scales the input by its largest coefficient before forming a determinant. A normalized determinant magnitude below `1e-12`, a zero/ill-conditioned matrix, nonfinite derived stretch, or opposite reflection signs triggers deterministic coefficient-linear fallback for that matrix. Preparation reports this fallback. Opposite orientation cannot pass continuously through invertible real matrices, so crossing a singular matrix is expected in that case. A scale threshold defines an explicit numerical policy; it is not an artistic guarantee against collapse.

Exactly half-turn angular ties choose the positive direction. Camera shortest-path interpolation reduces endpoints modulo 360° before computing the delta, while retaining the original left angle as the result's base. Thus 350° to 10° passes through 360°, and the exact right endpoint still returns 10°. Unwrapped camera mode preserves deliberate full turns such as 0° to 360°. Affine rotations are always shortest-path; additional revolutions must be represented by intermediate keyframes.

All intermediate models pass normal flame validation. This catches overflow, invalid camera density, impossible Xaos rows and invalid plugin parameter ranges. A mathematically valid model may still produce sparse, divergent or unattractive orbits; morphing is not an automatic attractor-fitting algorithm.

## Palette and curve details

Encoded RGB interpolates stored channel values. Linear-light RGB decodes the sRGB transfer function, interpolates light values and re-encodes them. HSV and HSL use the shortest hue path; achromatic endpoints borrow the colored endpoint's hue. Each of the 256 palette entries is interpolated independently. Exact endpoint palettes bypass conversion.

Color curves interpolate all four channels' four rational Bézier control points and weights. Their mode must match for an interpolated segment. The `legacy_7x` mode retains the historical global identity bypass: default point coordinates bypass all curves, while a point change activates the nonlinear master-plus-RGB lookup path. Consequently, interpolating away from the exact default legacy curves can introduce a visual discontinuity immediately after the endpoint even though the stored point values vary continuously. This is retained producer behavior, not smoothed away by the animation engine. The continuous mode uses monotone-X inversion and does not use that historical bypass; choose one mode deliberately for an entire smooth segment. See [COLOR_TOOLS.md](COLOR_TOOLS.md).

## Original Apophysis source audit

The audit inspected complete `TControlPoint.InterpolateX` methods in the original 2.09 `Source/ControlPoint.pas` and the pinned 7X `src/Flame/ControlPoint.pas`, together with their local call sites. These sources establish a historical baseline; M9 does not claim that its entire timeline or all palette/integer choices reproduce the original animation application.

| Pinned source | Identity |
| --- | --- |
| Original 2.09 source archive | SHA-256 `c302b44194e3552999b5279c30900876e2063587e0137af02a3cad4caaec395a` |
| 2.09 `Source/ControlPoint.pas` | SHA-256 `fff973828c6669777641108279b2ce51a5cd8f4c5c11c412bc08b5d45e54f5d3` |
| 7X source revision | `1cefa4099927e296d365412bd0b91b96f85606ca` |
| 7X `src/Flame/ControlPoint.pas` | SHA-256 `8ffa29355b7ef957c0c4f1f22de2535973a3408d165893b520d5d3d9f98427db` |

[Original 2.09 release archive](https://downloads.sourceforge.net/project/apophysis/apophysis/Apophysis%202.09/Apophysis-2.09-src.7z) · [Pinned 7X ControlPoint source](https://github.com/xyrus02/apophysis-7x/blob/1cefa4099927e296d365412bd0b91b96f85606ca/src/Flame/ControlPoint.pas)

Both methods derive weights from source times when `cp2.time - cp1.time > 1e-6`; otherwise they choose the first source. They interpolate affine coefficients directly, pair transforms by numeric array position, interpolate variation weights and pass interpolated variable values through each variation's setter. They interpolate center, angle, pixels-per-unit, zoom, brightness, gamma, vibrancy, gamma threshold, filter radius and sample density. Width and height come from the first source, whereas oversampling and some other integer renderer values are rounded. Background interpolation is commented out. Final-transform slots are temporarily rearranged while interpolating and then restored.

When `cmap_inter = 0`, the inspected routines convert byte RGB to HSV, interpolate hue directly without a shortest-path correction, convert back and round to RGB8. M9's shortest HSV option is therefore a deliberate modern choice; it is not a byte-for-byte reproduction of that historical hue path. M9 also offers encoded RGB, linear-light RGB and HSL, interpolates background, holds discrete settings, uses explicit correspondence, and can use polar affines. These differences are intentional and documented.

The source call sites use more than one time convention, including mutation's normalized `Time/100` argument and script-provided interpolation values. This audit does not establish a universal historical FPS or binary AV timeline schema. Importing a legacy timed collection consequently requires an explicit frames/seconds conversion and rejects ambiguous timing instead of guessing.

The regression suite verifies the Reborn policies numerically, including coefficient-linear midpoint values, true polar stretch, exact endpoints, shortest rotations, reflected and singular fallbacks, explicit topology and native-module boundaries. These animation tests are not a compiled original-application pixel oracle. Existing static variation and image-maker oracle coverage remains documented separately in [PRODUCER_ORACLE.md](PRODUCER_ORACLE.md).
