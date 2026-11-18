# Additional legacy formats

Milestone 8 adds Ultra Fractal gradient collections (`.ugr`), Fractint palette
files (`.map`) and a deliberately bounded Apophysis UPR interchange path. These
are implemented in the Qt-independent `legacy_formats` core. Existing GIMP
palette and original flame collection support continue to use their existing
implementations.

## RGB palette interchange

| Format | Import | Export | Compatibility boundary |
|---|---|---|---|
| UGR | Multiple named gradients, integer stops, linear cyclic RGB interpolation | One named 256-color gradient | `smooth=no`; no opacity gradient or unknown gradient behavior |
| Fractint MAP | Exactly 256 RGB integer rows | Exactly 256 RGB integer rows | RGB channels in 0–255; no alpha |
| GIMP GPL | Existing implementation | Existing implementation | Existing GPL behavior |

The gradient editor's Import and Export actions accept GPL, UGR and MAP. For a
UGR collection, the picker lists compatible entries with their original entry
numbers. Unsupported entries are reported. The original palette remains intact
when the import fails or the picker is cancelled. Palette export writes
atomically and refuses an unknown filename extension.

The core and CLI limit additional-format inputs to 8 MiB. The desktop palette
picker retains its existing smaller 2 MiB file limit. Token values are bounded to
1 MiB, collections to 4,096 entries and token counts to 262,144.

### UGR conventions

The implementation follows Apophysis 2.09's `Main.pas::GradientFromPalette`,
`Main.pas::GradientString` and `cmap.pas::GetPalette` / `RGBBlend`:

- UGR positions are integers 0–399. Negative positions wrap modulo 400, matching
  the original importer. Positions above 399 are rejected.
- Positions map to the flame palette using `Round(position * 255 / 399)` with
  nearest-even rounding. Export uses the inverse 0–255 to 0–399 mapping.
- Decimal UGR colors contain red in the low byte, green in the middle byte and
  blue in the high byte: `red + 256*green + 65536*blue`.
- Interpolation uses the original incremental RGB blend and nearest-even
  rounding. The last stop connects cyclically to the first. The original
  source-produced fixtures compare all 256 RGB entries.
- Stops are processed in ascending mapped position; coincident mapped positions
  use the last supplied color. A single stop produces a constant palette.
- Export quantizes RGB values to 8-bit channels and writes every palette entry,
  so importing its output recovers the quantized 256-color palette exactly.

Quoted titles, escaped quotes, semicolon comments and line continuation between
tokens are accepted. Unsupported `smooth=yes`, opacity sections, unknown gradient
attributes and non-gradient UGR sections produce a compatibility error rather
than silently replacing their behavior. Arbitrary Ultra Fractal smooth-gradient
and opacity semantics are not implemented.

MAP import accepts blank lines and full-line `;` or `#` comments. It reads the
first three decimal integers of each data row, as the original Apophysis
Fractint importer did, and permits trailing annotations. Fewer or more than 256
data rows and out-of-range channel values are errors. Export begins immediately
with RGB rows; a name annotation can follow the first row, so old readers that
expect the first row to contain colors can still read it.

```sh
flame-cpu palette-export examples/ember-spiral.flame --output ember.ugr
flame-cpu palette-import examples/legacy-palettes.ugr --palette-index 2 \
  --flame examples/ember-spiral.flame --output recolored.flame
flame-cpu palette-export recolored.flame --output recolored.map
```

CLI indices are one-based. The core returns a `FormatReport` for each UGR entry;
callers must check `supported()` before applying its palette. Malformed container
syntax and invalid numeric color data throw `std::invalid_argument`.

## Apophysis UPR subset

A `.upr` file is an Ultra Fractal parameter collection, not a generic flame
collection. Reborn imports and exports the historical parameter dialect emitted
by `TMainForm.UPRString` in Apophysis 2.09. It recognizes:

- `formula: filename="mt.ufm" entry="mt-pixel"`;
- `outside: filename="apophysis.ucl" entry="enr-flame-a"`, and the later
  `apophysis-205` entry name documented by Ultra Fractal's author;
- one opaque normal layer with the original linear transfer settings;
- positive camera magnification, image dimensions, center, iteration density,
  spatial filter radius, oversampling, brightness, gamma and opaque background;
- up to 64 ordinary transforms, an optional final transform, affine coefficients,
  selection weights, color positions, historical color symmetry and known core
  variation weights/parameters;
- an RGB gradient using the supported UGR conventions.

Import preserves the original complete entry string in `UprEntry::source`,
including unsupported entries. A recognized, representable entry receives a
`Flame`; an incompatible entry receives errors and no renderable model. Formula
filenames and identifiers are recognized as data. Import does not download or
execute formula files.

The original writer emits affine fields in a different order from flame XML:
`cfa=a`, `cfb=c`, `cfc=b`, `cfd=d`, `cfe=e`, `cff=f`. Camera Y is negated. The
historical color symmetry converts to Reborn color speed as `(1 - symmetry)/2`.
These mappings are tested against a fixture emitted by the actual original
Pascal writer.

Effective camera scale uses Ultra Fractal's 4-by-3 reference-frame convention:
`magn * min(width/4, height/3)`. Reborn exports its current effective scale rather
than running the original stochastic `CalcUPRMagn` auto-framing step. The
individual Reborn `scale` and `zoom` settings therefore reimport as one effective
scale with zero zoom. The resulting camera is not a reconstruction of camera
parameters that Apophysis discarded before writing an old UPR file.

### Explicit refusals and reports

Unknown fractal or coloring formulas, layered scenes, unsupported transfer or
merge modes, transparency, unrecognized semantic fields, unknown active
variations and unsupported parameters on active variations are rejected. Extra
historical parameter defaults for inactive variations are retained in the source
entry and explicitly reported as unused; they do not change the flame model.
Repeated identical fields are accepted because the original writer redundantly
emitted some parameter defaults. Conflicting duplicates are rejected.

Export refuses rotation, post affine transforms, nontrivial chaos weights,
transform opacity, native plugin variations, non-step palettes, nonunit
vibrancy, nonzero gamma threshold, missing explicit filter radius, 7X pre-blur
angle behavior and non-Apophysis nonlinear variation math. The output text is
empty on an export compatibility error, allowing callers to refuse the operation
before opening a file. Unsupported settings are not silently discarded.

Successful import and export still carry an explicit renderer limitation:
**Ultra Fractal's formula execution, sampling and coloring are not
pixel-equivalent to Reborn's renderer.** UPR export requires the matching
historical formula modules to be installed in Ultra Fractal. Formula-module
availability and rendering in an installed Ultra Fractal application were not
tested here. Arbitrary UFM/UCL formula execution, modern layered UPR scenes and
all third-party module variations are outside this milestone.

Palette and background colors use 8-bit channels. Separate zoom, source-version
metadata and animation time have no representation in this supported dialect;
the export report identifies that limitation. Project-only workspace settings,
render presets and plugin references belong in `.apoproject`.

```sh
flame-cpu upr-import examples/classic-upr-study.upr --output imported.apoproject
flame-cpu upr-export imported.apoproject --output exported.upr
```

Importing to a native project allows the CLI to retain the original UPR entry as
project metadata. Export from a legacy library starts from its renderable flame
model; use the native project or original flame document to preserve unrelated
original XML metadata.

## Original-source fixtures and reproduction

`tests/fixtures/formats` contains three UGR/MAP pairs and one UPR fixture. The
MAP files are independent expected results, not values computed by Reborn's
implementation. `tests/legacy_formats_generate.py` extracts and compiles the
original Apophysis 2.09 routines with Free Pascal 3.2.2 in Delphi mode.

The UGR fixture generator executes `GetPalette`, `RGBBlend`, `GradientFromPalette`
and `GradientString`. The UPR fixture executes `UPRString` with a small prepared
control-point scaffold, two ordinary linear transforms, one final transform,
asymmetric affine coefficients and a supplied magnification of 2. The scaffold
replaces VCL application plumbing and does not test `Prepare`, stochastic camera
fitting, formula rendering, Windows UI behavior or Ultra Fractal execution.

```sh
FPC=fpc python3 tests/legacy_formats_generate.py /path/to/Apophysis-2.09-src \
  tests/fixtures/formats
```

Optional `FPCFLAGS` supplies compiler unit paths. The script verifies the exact
source hashes before extracting routines:

| File | SHA-256 |
|---|---|
| `Source/cmap.pas` | `4a6d7db9634f983346b260ee599b0b8c7334b4b6abef9a49047a57a065abe9f0` |
| `Source/Main.pas` | `24476b62589d45508c5c90cee1d9273ae66848f322b6f2d5a78aa792921f2b61` |

`legacy-formats-tests [fixture-directory]` validates source-derived palette
interpolation and RGB packing, coefficient order, camera/symmetry/background
conversion, supported model round trips, malformed-input boundaries and explicit
compatibility refusals. Packaged Windows tests can receive the source fixture
directory as their first argument.

## Primary references

- [Apophysis 2.09 source archive](https://downloads.sourceforge.net/project/apophysis/apophysis/Apophysis%202.09/Apophysis-2.09-src.7z), archive SHA-256
  `c302b44194e3552999b5279c30900876e2063587e0137af02a3cad4caaec395a`.
  Relevant routines: `Main.pas::GradientFromPalette`, `GradientString`,
  `TMainForm.UPRString`; `cmap.pas::GetPalette`, `RGBBlend`;
  `Browser.pas::LoadFractintMap`; `ControlPoint.pas::CalcUPRMagn`;
  `Regstry.pas` UPR defaults; `XForm.pas` color symmetry coefficients.
- [Ultra Fractal file types](https://www.ultrafractal.com/help/browsers/filetypes.html).
- [Ultra Fractal gradient files](https://www.ultrafractal.com/help/gradients/gradientfiles.html).
- [Ultra Fractal parameter files](https://www.ultrafractal.com/help/fractalwindows/parameters.html).
- [Ultra Fractal flame-fractal guidance](https://www.ultrafractal.com/kb/flamefractals.html).
- [Ultra Fractal author on importing Apophysis UPR and the `apophysis-205` module](https://www.ultrafractal.com/forum/index.php?u=/topic/680/what-style-is-this-fractal-and-how-would-i-go-about-creating-this).
- [Ultra Fractal pixel-spacing discussion with a tested 4-by-3 normalization formula](https://www.ultrafractal.com/forum/index.php?u=/topic/553/pixel-spacing).
