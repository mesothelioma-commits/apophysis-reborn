# Legacy flame compatibility in 0.3.0

Opening a document, converting an entry for rendering, and exporting a normalized entry have different requirements. The document path retains the complete original file, including unsupported variations, comments, unknown namespaces, history and other entries. Its no-edit save preserves the original bytes. The renderer checks the selected entry independently; an unsupported sibling can still be preserved in the same collection. The internal `parse_flames` bulk API continues to validate all entries together.

## Producer-specific settings

Profiles use the XML producer string. The recognized prefixes are Apophysis 2.x, Apophysis 7X and Apophysis AV, case-insensitive. A 2.x string containing “3D hack” selects that branch. Untagged files retain the earlier FLAM3-style interpretation. A version selects the verified conventions below. Files using unsupported controls still receive a diagnostic.

| Setting | Recognized Apophysis | Generic / FLAM3-style |
| --- | --- | --- |
| Omitted settings | Constructor defaults include 100 × 100, scale 50, quality 50, brightness/gamma/vibrancy 1, oversample 1 and Gaussian radius 0.5. The XML reader resets an omitted gamma threshold to zero. Ordinary xforms default to weight 0 and color 0. | 100 × 100, scale 50, quality 1, brightness/gamma 4, vibrancy 1, oversample 1. Omitted radius retains the 0.2.0 box reconstruction. Transform default color alternates by input index. |
| Camera | `rotate` is degrees. `angle` is the original internal angle in radians. Reborn's rotation is −180 × angle / π. When both appear the historical parser gives `angle` precedence. | `rotate` is degrees. The reader accepts `angle` on its own; a mixed pair with no identified Apophysis producer is ambiguous. |
| Color speed | `symmetry` sets the incoming target-color weight to (1 − symmetry)/2. Classic 2.x also accepts a signed `color_speed` with this meaning. Other tagged branches use `symmetry`. | `color_speed` is a direct target-color weight; `symmetry` uses the legacy conversion. |
| Opacity and palette sampling | Ordinary opacity acts as a plot probability; the enabled final transform acts on the plotting copy. Step palette sampling uses a 255-scaled rounded index. | Ordinary opacity contributes adjusted sample mass; final opacity selects an application probability. Step/linear lookup uses the 256-scaled FLAM3 coordinate. |
| Filter and tone | Reads nonnegative Gaussian `filter` and `gamma_threshold`, with branch-specific kernel support/rounding. Packed RGB entries use byte channels, and the tagged background is quantized to whole bytes. | Accepts Gaussian `filter` / `filter_shape="gaussian"` and nonnegative `gamma_threshold`. Other kernels are blocked. |
| 4-byte packed palette | In a tagged `<palette format="RGBA">`, Apophysis skips a leading byte and uses the three following bytes as RGB. The older `<colors count="256" data="..."/>` has the same leading-byte layout. | `<palette format="RGBA">` uses trailing alpha and requires it to be 255. The old `<colors>` form uses its historical leading-byte layout. |
| Palette entries | Three-byte RGB packed palettes and complete indexed palettes (all 256 slots). An incomplete indexed palette can depend on the previous editor state and is blocked. | Packed RGB/RGBA and one or more decimal indexed RGB stops. Missing entries interpolate cyclically across the palette. |

The recognized subset supports `size`, `center`, `scale`, `zoom`, `quality`, `oversample`/`supersample`, `background`, `gamma`, `brightness`, `vibrancy`, pre- and post-affines, per-transform weights, chaos, color, final transforms and `palette_mode`. Scale is pixels per unit at the declared resolution. Six coefficients a b c d e f map (x,y) to (ax+cy+e,bx+dy+f). One `<flame>` and direct entries in `<flames>`, `<Flames>` or `<pick>` can be read. Render conversion ignores the contents of `<edit>` history and author/editor metadata; the document path still retains them.

Tagged 7X/AV files commonly write density estimator parameters alongside `enable_de="0"`, a plugin inventory, `new_linear="1"` and default color curves. The reader checks the disabled estimator configuration and the exact four neutral curve definitions. It checks each actual transform operation, irrespective of the plugin inventory. Active density estimation, changed color curves, multi-batch/time sampling, unknown kernels and active 3D camera controls block conversion. A catalog index requires the corresponding external palette and is currently blocked.

## Mathematical coverage

All 15 original registry entries from Milestone 2 remain available. New entries can be inspected using `--list-variations`.

| FLAM3 ID | XML name | Computation / historical controls |
| ---: | --- | --- |
| 28 | bubble | XY scaled by 1/(1+r²/4) |
| 31 | noise | Input X and Y modulate a random polar displacement with uniform radius and angle |
| 32 | julian | Integral, nonzero `julian_power` selects branch count and direction. `julian_dist` scales the radius exponent. Tagged Apophysis requires explicit power, whose original authoring default could be randomized. |
| 34 | blur | Uniform radius and angle, independent of the incoming XY point |
| 64 | foci | Exponential-pair and trigonometric mapping. The tagged path handles a zero denominator as in the 7X variation. |
| 67 | pre_blur | Displaces the affine input before any ordinary variation is evaluated. The Apophysis branch maintains four rolling radius draws per transform per orbit stream. The FLAM3 branch uses four fresh draws. |

For example, a pre-blur followed by bubble evaluates bubble on the displaced point. The order of XML attributes cannot change that stage boundary. The original x86 Apophysis and AV pre-blur use one draw for the direction and ring replacement; the 7X Pascal branch uses a separate replacement draw. Reborn's seeded generator and thread-independent batches determine the actual random sequence. Original Delphi/x87 floating-point paths and historical random streams can yield different orbits.

`var1` and vector `var` support verified numerical indices. For original 2.x the first 15 are shared with FLAM3; its bubble/noise/blur/pre_blur indices are 24/26/27/29. 3D hack, 7X and AV changed built-in order or loaded plugins at runtime. Named variations are required there. Mixed numbered and named definitions are rejected. Tagged Apophysis uses the source-level 1e-300 spherical denominator guard; FLAM3 uses 1e-10. A known additive `var_order` can specify ordinary term order. Unknown, incomplete or duplicated XY operations fail conversion.

The XY part of `linear3D` equals linear when no camera or transform couples Z to XY. Reborn validates those constraints, then uses the XY projection for the bundled 2.08 3D-hack artwork. In admitted 7X/AV inputs, flatten's Z-only action has no effect on projected XY. `sinusoidal_use3D="0"` is accepted; active 3D parameters remain blocked.

## Save and export

Use the document save operation to preserve a historical file. A normalized export converts one renderable entry and writes the supported model as UTF-8 XML, both affine matrices, explicit named variation weights and 256 decimal palette entries. It preserves the tagged renderer's interpretation, including signed symmetry, Gaussian radius, threshold, JuliaN parameters and non-default ordinary variation order. It can discard comments, history, author metadata, inactive parameters, degenerate Z-only terms and the collection wrapper. Historical XML readers have differing expectations for numeric precision and the shape of their source text.

## Evidence and remaining work

The engine tests pin branch-specific numeric defaults, control precedence, old vector IDs, two distinct four-byte palette orders, xform ordering, known/inactive field handling, and actual deterministic numerical results for all six new registry entries. The corpus report records untouched assets, per-entry renderability, published-image comparisons and provenance. The producer-oracle report describes which original code ran and which checks still need the Windows editors.

Sources inspected include the original 2.09 source archive (SHA-256 `c302b44194e3552999b5279c30900876e2063587e0137af02a3cad4caaec395a`), its `Main.pas`, `ControlPoint.pas`, `XForm.pas` and `XFormMan.pas`; 2.08 3D-hack `XForm.pas` and `ControlPoint.pas` (archive SHA-256 `f17cd12b86dfb707484128fd0a4dd01e76fcc8fe8aa52101b9ff651a3ae1be1f`); and the Apophysis 7X 1cefa409 source, including `Forms/Main.pas`, `Flame/XForm.pas`, `varJuliaN.pas`, and `varFoci.pas`. The pinned FLAM3 sources are listed in REFERENCES.md. [Original 2.09 release](https://sourceforge.net/projects/apophysis/files/apophysis/Apophysis%202.09/) · [2.08 3D-hack source](https://sourceforge.net/projects/apophysis/files/Apophysis%203D%20%28experimental%29/Apophysis%202.08%20_3D%20hack_/Apophysis208_3Dhack_src.7z/download) · [7X source tree](https://github.com/xyrus02/apophysis-7x/tree/1cefa4099927e296d365412bd0b91b96f85606ca/src) · [AV historical corpus](LEGACY_CORPUS.md)
