# Color curves and extended palette tools — Milestone 9

Milestone 9 adds editable master/RGB rational Bézier curves, original Apophysis 7X reconstruction-curve evaluation, HSL color controls, and two additional gradient interpolation spaces. These controls are available for ordinary flames and for the flame snapshots edited inside the animation timeline.

## Use the controls

Open **Adjustment editor → Tone / color → Tone curves…** to change image reconstruction. The dialog edits a private copy of the flame's curves. Choose **Master**, **Red**, **Green** or **Blue**, drag a control point, or enter exact X/Y/weight values. The plot shows the selected channel's transfer. The master transfer is applied before each RGB transfer.

**Original 7X lookup** and **Continuous transfer** are separate evaluation modes. Opening a historical file retains its original mode. Changing the mode deliberately changes what the same control points mean; there is no promise of a visually equivalent automatic conversion. For new work, the continuous presets provide conventional input/output curve shaping. The nonidentity presets select Continuous transfer and replace all four channels, applying the selected effect to the currently selected channel; **Warm balance** sets red and blue together. **Identity** and **Reset all** clear all four curves while retaining the selected evaluation mode. **Reset channel** changes only the selected channel. **Restore original** recovers the exact values present when the dialog opened.

In Continuous transfer, X must run from 0 to 1 in nondecreasing order. Inner handles move horizontally and vertically; the X endpoints stay at 0 and 1. Endpoint Y and weights remain editable through the table. In Original 7X lookup, handles move vertically because the historical renderer does not use X to find the transfer value. The numeric X fields still retain and edit the historical source data. The dialog explains this difference beside the plot.

**Apply curves** returns the working curves to the containing Adjustment editor and refreshes its image preview. **Apply adjustments** then commits one document undo step. Cancelling the curve dialog discards its working copy; cancelling the containing Adjustment editor discards all its pending changes, including accepted inner-dialog edits. Unchanged coordinates are displayed with round-trip decimal precision, so opening and accepting a dialog does not round the underlying doubles.

### Palette curves

Open **Gradient editor → Color adjustments → Palette curves…** to apply curves to the stored 256 palette colors. This starts with continuous identity curves, and Apply bakes the result into the working palette. The palette's local **Undo palette edit** and **Redo** include this operation. The flame's reconstruction curves stay separate. **Apply gradient** creates one document undo step; cancelling the Gradient editor discards the working palette.

Palette curves and tone curves affect different stages. Palette curves change the colors sampled along the orbit. Tone curves change reconstructed foreground channels after density/gamma/vibrancy processing. Applying the same curve in both places can produce a different result from applying it once.

### HSL and gradient interpolation

**Individual color** includes RGB, HSV and HSL controls for the selected palette index. HSL lightness is distinct from HSV value: a fully saturated red at HSL lightness 50% is full red; reducing its HSL lightness to 25% produces half-intensity red. These controls change the selected stored color; merely switching selection or opening the dialog retains its full existing precision.

**Color stops** offers four interpolation choices. The same choices are available for animation palette interpolation.

| Space | Behavior between two stops |
| --- | --- |
| RGB interpolation | Interpolate the stored, encoded RGB channel values. This retains the earlier Reborn behavior. |
| HSV shortest hue path | Interpolate saturation and value while taking the shortest angular hue path. |
| Linear-light sRGB | Decode the standard piecewise sRGB transfer, interpolate linear channel intensities, then encode to sRGB. Black/white at halfway produces about 0.735357 in each stored channel. |
| HSL shortest hue path | Interpolate saturation and lightness while taking the shortest angular hue path. |

An achromatic HSV/HSL endpoint borrows the colored endpoint's hue instead of creating an arbitrary red detour. Exact stop and animation endpoint colors remain exact. The selected stop-building space is baked into the resulting palette; it is independent of **Render interpolation**, which controls sampling between the stored palette indices. Linear-light interpolation assumes sRGB channel encoding and does not introduce profile-based color management.

The four entries in [`examples/color-curve-studies.flame`](../examples/color-curve-studies.flame) compare the same geometry and palette with unchanged tone, continuous soft contrast, continuous warm balance and an original 7X weighted lookup. The example retains its generic camera/orbit profile so the comparison isolates the curve settings; it is not a historical full-render oracle.

## Original 7X semantics

The implementation was checked against the pinned Apophysis 7X source tree at commit `1cefa4099927e296d365412bd0b91b96f85606ca`, including:

- [`Core/Bezier.pas`](https://github.com/xyrus02/apophysis-7x/blob/1cefa4099927e296d365412bd0b91b96f85606ca/src/Core/Bezier.pas): rational Bézier evaluation.
- [`Rendering/ImageMaker.pas`](https://github.com/xyrus02/apophysis-7x/blob/1cefa4099927e296d365412bd0b91b96f85606ca/src/Rendering/ImageMaker.pas): identity detection, lookup generation and application order.
- [`Flame/ControlPoint.pas`](https://github.com/xyrus02/apophysis-7x/blob/1cefa4099927e296d365412bd0b91b96f85606ca/src/Flame/ControlPoint.pas): defaults and XML point/weight ordering.
- [`System/CurvesControl.pas`](https://github.com/xyrus02/apophysis-7x/blob/1cefa4099927e296d365412bd0b91b96f85606ca/src/System/CurvesControl.pas) and [`Forms/Curves.pas`](https://github.com/xyrus02/apophysis-7x/blob/1cefa4099927e296d365412bd0b91b96f85606ca/src/Forms/Curves.pas): original handles and weight controls.

The XML `curves` attribute contains exactly **48 numbers**: four channels in master/red/green/blue order, each containing four `(X, Y, weight)` triples. Every channel defaults to `(0,0,1), (0,0,1), (1,1,1), (1,1,1)`.

Three historical details materially affect the image:

1. **Rendering uses Y at Bézier parameter t, not Y at the inverse X coordinate.** X values are stored and can appear in the original editor's graph, but changing them does not change the evaluated rational Y function.
2. **The identity bypass checks all four channels' point coordinates together and ignores weights.** If all points equal the defaults, curves are bypassed even when weights differ. If any point differs, all four channels are evaluated. With unit weights, a default channel evaluates the smoothstep function `3t² − 2t³` once this bypass is defeated. Consequently, changing only one X coordinate can activate a nonlinear effect across all channels. This behavior is retained deliberately.
3. **Lookup and rounding happen twice.** Original 7X builds 257 values per channel at `i/256`, scales each result by 256, rounds the master result to select an RGB lookup entry, and rounds that RGB value. Reborn preserves the nearest-even integer operation, including the inclusive 0…256 input domain and the original pass-through for values outside it.

For historical opaque RGB8 reconstruction, curve application occurs after foreground gamma/vibrancy calculation and its original integer quantization, before the background contribution and final clamp. Empty pixels retain their exact background. Dense CPU rendering and the streaming renderer use the same curve evaluator; Vulkan's CPU reconstruction stage uses it as well.

This is a source-verified color-curve contract. It does not claim complete pixel equivalence for arbitrary original 7X flames: historical random streams, floating-point paths, unsupported variations and other unsupported renderer features remain separate compatibility limits. Reborn's transparent and 16-bit outputs retain the existing Reborn alpha/high-depth pipeline. In legacy mode they use the same original quantized foreground curve values before background composition or straight-alpha division; they do not claim original Delphi transparent-image byte equivalence or additional curve precision absent from the historical lookup.

## Continuous transfer contract

Continuous mode evaluates the rational cubic at the parameter whose weighted X coordinate matches the input. Ordered X coordinates and positive endpoint weights give a monotone X path. Y control values need not be ordered, allowing deliberate nonmonotone color shaping. All-diagonal channels are recognized as exact identity and bypassed without numeric changes.

For bounded reconstruction cost, the renderer precomputes the composed master→RGB transfer at **4097 points**, covering **4096 equal input intervals**, then linearly interpolates adjacent entries. This table is the defined modern reconstruction transfer. It avoids RGB8 quantization and supports the existing 16-bit exports, but it is not an analytic per-pixel inversion or a promise of a universal error bound for arbitrarily steep weighted curves. The plot samples the analytic selected channel; its display and the tabulated composed renderer can differ slightly at unusually steep settings.

The foreground is mapped before background composition or straight-alpha division. Continuous mode clamps its input to the unit interval when the curve is active. Identity curves bypass all these calculations and retain prior image bytes. Alpha itself and the background color are not remapped. A fully empty pixel continues to use the original background (or transparent black).

The active lookup uses 6,168 bytes in legacy mode or 98,328 bytes in continuous mode; identity curves allocate no lookup table. The dense and streaming render memory admission calculations include these table bytes. Strip size does not change the result.

## Validation, persistence and animation

Both modes require finite X/Y in `[0,1]`, finite nonnegative weights no larger than `1,000,000`, and positive normal endpoint weights. Zero inner weights are supported, including the original UI's zero-weight setting. Singular endpoint weights, out-of-range coordinates, unsupported mode strings and malformed 48-number arrays produce a diagnostic. The original 7X UI's usual fixed endpoints and inner weights in `[0,16]` lie within these admitted bounds. Invalid source entries remain available through the source-preserving document layer.

Continuous mode additionally requires X endpoints exactly 0 and 1 and nondecreasing intermediate X. Original 7X mode retains nonordered X because those coordinates do not participate in its transfer evaluation.

Canonical XML writes `curves` whenever the stored curve model differs from the default, even when altered historical weights are bypassed. Continuous mode also writes `reborn_curve_mode="continuous"`; absent mode means original 7X evaluation. An explicit `reborn_curve_mode="legacy_7x"` is accepted. The modern marker is a Reborn extension; opening such a file in an older application does not guarantee the same rendering.

Source-preserving edits patch only `curves` and the mode marker. A no-op edit retains existing attribute spelling, precision and ordering. Returning to legacy mode removes the modern marker. Comments, unrelated attributes, edit history, encoding and sibling flame bytes survive. The model field participates in canonical serialization used by checkpoint identities, queued jobs, script exports and render requests, and animation snapshots. Ordinary script mutations preserve curves; this milestone does not add a separate Lua or legacy Pascal curve-editing API.

Animation interpolates every curve coordinate and weight when both keyframes use the same evaluation mode. Exact endpoints retain the complete original model. Interior interpolation between different curve modes is rejected with an explanation rather than dropping one endpoint's curves. Legacy interpolation can cross the original global identity-bypass boundary; its historical discontinuity is therefore possible. For smooth modern curve animation, use continuous mode consistently across keyframes.

## Numerical and workflow evidence

[`tests/color_curves_generate.py`](../tests/color_curves_generate.py) compiles the **unmodified original `Bezier.pas` unit** with Free Pascal and extracts the original image-maker identity test, lookup expressions and rounded lookup application into a small driver. Source hashes are pinned in the generator. The saved fixture is independent of the Reborn curve evaluator and can be consumed without Pascal installed.

The seven fixture configurations exercise defaults, weight-only changes, an X-only activation, weighted master/RGB channels including a zero inner weight, reversed Y, nonordered X and changed endpoint values/weights. Across **1,799 inputs**, tests compare **5,397 integer RGB outputs exactly** and **7,196 raw floating values within `2e-15`**.

The focused core suite also covers exact default/diagonal identity, endpoint preservation, monotonicity, invalid inputs, interpolation mode rejection, source-preserving/no-op edits, canonical round trips, scripting snapshots, dense/streaming RGB8 equality across different strip heights, and continuous 16-bit precision. Qt tests exercise actual control-point dragging, numeric validation, presets/reset, HSL and gradient interpolation, nested dialog cancellation, palette undo, a single source-preserving document undo step and immutable queue save/load snapshots. Existing producer pixel oracles remain part of the broader regression suite.

To regenerate the independent fixture:

```sh
python tests/color_curves_generate.py /path/to/apophysis-7x tests/fixtures/color
```

Set `FPC` to the Free Pascal compiler command when it is not on `PATH`. The generator rejects upstream files whose bytes differ from the pinned versions. Regeneration requires no renderer executable from this project.

Density estimation, ICC profiles, OpenEXR, direct-color 3D behavior and the complete historical renderer remain outside this curve milestone. The existing compatibility and output-format documents define those remaining boundaries.
