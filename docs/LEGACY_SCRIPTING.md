# Legacy Apophysis scripting subset

Milestone 8 accepts a **strict, tested Pascal-style subset** of the original Apophysis script API. It is a separate lexer/parser that validates the complete source, translates supported statements into bounded Lua calls and executes them on the same private document transaction as modern scripts. Renaming a Lua file to `.asc` does not make it a legacy script.

This is not the original TatScripter VM and does not promise execution of every historical `.asc` script. Unsupported syntax, functions, properties and assignments produce visible errors before any script statement executes. A runtime error, Stop or invalid final flame discards all edits and deferred requests. The translated Lua and compatibility notes are returned by the C++ API for inspection.

Use **Apophysis Pascal subset** in the script editor, or:

```sh
flame-cpu script examples/ember-spiral.flame \
  --script examples/scripts/legacy-triangles.asc \
  --language legacy --output legacy-study.flame
```

Sources should be UTF-8; a UTF-8 BOM is accepted. Existing flame documents retain their supported source encoding, including UTF-16. Convert a historical script in an ANSI code page to UTF-8 when its string literals contain non-ASCII text.

## Audited primary references

The API was checked against these original sources, rather than inferred from similarly named modern functions:

- [Apophysis 2.09 source archive](https://downloads.sourceforge.net/project/apophysis/apophysis/Apophysis%202.09/Apophysis-2.09-src.7z), archive SHA-256 `c302b44194e3552999b5279c30900876e2063587e0137af02a3cad4caaec395a`.
- [Apophysis 7X](https://github.com/xyrus02/apophysis-7x/tree/1cefa4099927e296d365412bd0b91b96f85606ca), pinned commit `1cefa4099927e296d365412bd0b91b96f85606ca`; its scripting/language/interface documentation and `src/Forms/ScriptForm.pas` provide the cross-check.

| 2.09 file | Role | SHA-256 |
| --- | --- | --- |
| `Source/ScriptForm.pas` | Method/property registration, scalar/indexed getters/setters, add/copy/delete behavior | `30f08f0f804e3ef26fc62da2b155c137209d9e8f37f0964b9590dbccf1c54b25` |
| `Source/XForm.pas` | Original affine operations and transform defaults | `0509deeacdbcd5878c4ef20bcc2488f07228b13c1a375fab9b37e4a6f08e6950` |
| `Source/ScriptRender.pas` | Renderer snapshot and proportional camera resize | `4e9202b6393ec9394852bf26e980d04f8b2ba47c4bc636922a014e71a5b607cc` |
| `Source/ControlPoint.pas` | `Clear` and `AdjustScale` semantics | `fff973828c6669777641108279b2ce51a5cd8f4c5c11c412bc08b5d45e54f5d3` |

`Main.pas::NormalizeVariations` establishes that normalization first takes absolute variation weights and restores linear when all weights are zero. The new examples and compatibility implementation were authored for Reborn. No original script collection or original Pascal implementation is bundled. The optional reference generator reads a separately obtained, hash-checked source tree into a temporary compilation.

## Accepted language

Identifiers and API names are case-insensitive. Strings use Pascal single quotes, with doubled quotes for an embedded apostrophe. `{ ... }`, `(* ... *)` and `//` comments are supported. Numeric literals are decimal, optionally with a fraction and exponent.

The parser supports:

- Optional `program Name;` and a `begin ... end.` main block; a simple statement sequence is also accepted.
- Scalar `var` declarations of `Integer`, `Double`, `Real`, `Extended`, `String`, `Boolean`, `Variant`, and scalar `const` declarations.
- Assignments, including indexed API properties. A simple variable first assigned without a `var` declaration is also accepted.
- `begin/end`, `if/then/else`, `for/to/do`, `for/downto/do`, `while/do`, `repeat/until`.
- `Inc(variable[, amount])` and `Dec(variable[, amount])`.
- Arithmetic `+ - * / div mod`, comparisons `= <> < > <= >=`, and logical `and or not`.
- String concatenation with `+` when both operands are strings.

Declared scalar types are checked on assignment. Integer assignments require integral values; the subset does not silently apply every coercion of the old Variant VM. Loop bounds are evaluated once. Assigning an active `for` variable or reusing it in a nested `for` is rejected. After a loop, its variable retains the last executed iteration value; scripts should not depend on a compiler-specific value after a zero-iteration loop. Expression/statement nesting is capped at 64 and token count at 100,000.

Math functions: `Sin`, `Cos`, `Tan`, `ArcTan`, `ArcTan2`, `Abs`, `Sqrt`, `Exp`, `Ln`, `Min`, `Max`, `Sqr`, `Trunc`, `Round`, `Frac`. Trigonometric functions take radians. `Round` uses nearest-even rounding, `Trunc` truncates toward zero, and `div`/`mod` use Pascal-style truncation/remainder for integral operands. Conversion helpers are `IntToStr`, `FloatToStr`, `StrToInt`, `StrToFloat`. String conversions use locale-independent decimal syntax.

`Pi`, `True` and `False` are available. `Random` or `Random()` returns [0,1) from the script's reproducible Reborn stream. It is **not** Delphi RNG parity. The host seed controls repeatability; wall-clock `Randomize` is deliberately unsupported.

The subset does not include user-defined procedures/functions, arrays/records, `with`, `case`, `try/except/finally`, include directives, classes, pointers, native calls or arbitrary built-in libraries. Even an unsupported call in a branch that would never execute fails preflight.

## Global operations

All historical flame/transform/palette indices begin at **0**.

| API | Supported behavior |
| --- | --- |
| `ActiveTransform` / `SetActiveTransform(i)` | Get/select an ordinary transform. Access to the historical hidden/final-transform slot is not exposed by this subset. |
| `Transforms`, `FileCount`, `NVars`, `NXforms`, `Stopped` | Read-only counts/status. Count functions also accept empty parentheses. `NXforms` reports the configured transform limit. Stop cancels the transaction. |
| `Clear` | Clear ordinary/final transforms, set active index to -1 and reset zoom; retain camera/tone/name/palette settings that original `TControlPoint.Clear` retained. |
| `AddTransform` | Add/select a fresh identity/linear transform with weight 0.5. |
| `CopyTransform` | Append/select a copy; duplicate incoming chaos-column values as the original API does. |
| `DeleteTransform` | Delete active ordinary transform and its chaos row/column; deleting the only ordinary transform leaves it in place. |
| `Transform.Clear` | Reset the active transform to identity/linear with weight 0.5. |
| `Rotate(degrees)`, `Scale(factor)`, `Translate(x,y)`, `Multiply(a,b,c,d)` | Original `TXForm` affine operations, including its matrix orientation. |
| `NormalizeVars` | Normalize absolute variation weights on ordinary/final transforms; all-zero terms restore linear. |
| `RandomGradient` | Generate a new palette with Reborn's seeded palette algorithm. Historical palette RNG output is not reproduced. |
| `VariationIndex(name)`, `VariationName(index)` | Resolve the current supported built-in variation list. |
| `Print(value)`, `ShowStatus(value)` | Append bounded console messages. `ShowStatus` does not modify the main-window status bar. |
| `LoadFlame(index)` | Select an entry in the already loaded working library. It does not open a file. |
| `Render` | Capture a deferred render using `Renderer` settings; requires render capability. |
| `SaveFlame(relative_name)` | Capture a deferred canonical single-flame export; requires export capability. It does not append to an arbitrary existing disk library. |

The variation index list covers Reborn's available built-ins, not the entire plugin installation of an old Apophysis copy. Use `VariationIndex('name')` or named variation properties; hard-coded extended plugin indices are not portable. This subset does not expose plugin parameter indices through `Transform.Variable[]`. Built-in JuliaN parameters have named properties. Use modern Lua for custom native-plugin parameters.

## Flame properties

| Property | Convention |
| --- | --- |
| `Flame.Name` | UTF-8 text. |
| `Flame.Gamma`, `Brightness`, `Vibrancy` | Tone settings, subject to supported render-model limits. |
| `Flame.Time` | Stored time metadata only; no M8 timeline/animation evaluation. |
| `Flame.Zoom` | Camera zoom. |
| `Flame.X`, `Flame.Y` | Camera center. |
| `Flame.Width`, `Flame.Height` | Positive integral dimensions. |
| `Flame.Quality`, `Flame.SampleDensity` | Default samples per nominal pixel. A completed renderable model needs positive quality. |
| `Flame.Oversample` | Original exposed range 1–4. |
| `Flame.FilterRadius` | Reconstruction filter radius. |
| `Flame.Scale` | **100 × pixels-per-unit / width**; setting it multiplies by width/100. This differs from modern `apo.set_flame{scale=...}`. |
| `Flame.Angle` | **Radians**, with modern rotation = `-Angle × 180 / Pi`. |
| `Flame.Gradient[index,channel]` | Palette index 0–255, channel 0–2, integer values 0–255. |
| `Flame.Background[channel]` | Channel 0–2, integer values 0–255. |

Unsupported properties such as historical application options, pivot state, author/UI settings, 3D fields and final-transform enable toggles are rejected. The original VM ignored some out-of-range property writes; Reborn reports errors or rejects an invalid final model instead of silently retaining an earlier value. Renderer constraints may be stricter than the old editor's accepted values. This distinction is visible in diagnostics.

## Transform properties and affine mapping

`Transform.Weight` accepts `(0,1000]`; `Transform.Color` uses [0,1]. `Transform.Symmetry` is the original signed [-1,1] value, with modern color speed `(1 - Symmetry)/2`. `Transform.PlotMode` maps zero to hidden and nonzero integral values to visible. `Transform.Chaos[index]` is the per-destination multiplier.

Named built-in variation properties are supported, for example:

```pascal
Transform.Linear := 0;
Transform.JuliaN := 1;
Transform.julian_power := 3;
Transform.julian_dist := 0.6;
```

`Transform.Variation[index]` accesses variation weights by the current registry index. It takes a numeric index, as the audited original method does. For clarity:

```pascal
Transform.Variation[VariationIndex('swirl')] := 0.25;
```

The original direct coefficient properties are **not** the same letter order as modern XML arrays:

| Historical property | Reborn/XML coefficient |
| --- | --- |
| `Transform.A` | `pre.a` |
| `Transform.B` | `pre.c` |
| `Transform.C` | `pre.b` |
| `Transform.D` | `pre.d` |
| `Transform.E` | `pre.e` |
| `Transform.F` | `pre.f` |

`Transform.Coefs[i,j]` and `Transform.Post[i,j]` use the original editor coordinate signs:

| Index | Read/write value |
| --- | --- |
| `[0,0]` | `a` |
| `[0,1]` | `-b` |
| `[1,0]` | `-c` |
| `[1,1]` | `d` |
| `[2,0]` | `e` |
| `[2,1]` | `-f` |

The table applies to pre or post affine respectively. The global `Rotate`/`Scale`/`Translate` functions use `TXForm` operations. Pivot-aware `Transform.Rotate`, `Transform.Scale`, `RotateOrigin` and `Pivot.*` are deliberately not substituted with those global functions; their dependence on original editor state remains unsupported.

## Rendering and filesystem differences

Supported renderer properties are `Renderer.Filename`, `Renderer.Width`, `Renderer.Height`, `Renderer.MaxMemory`. Filename must be a safe relative destination. Width overrides multiply camera scale by `new_width / old_width`, matching original `TControlPoint.AdjustScale`. MaxMemory is an explicit host memory budget in MiB and must be 16–65,536 when set.

`Render` and `SaveFlame` require host capabilities, capture immutable state and produce no external effects during evaluation. Unsupported original calls include `SetFlameFile`, `ListFile`, `CopyFile`, `DeleteFile`, save/open dialogs, `BM_Open`, `BM_DllCFunc`, `Preview`, `ShowMessage`, `InputQuery`, `Morph`, `StoreFlame`/`GetFlame` and `RandomFlame`. These are not hidden no-ops. Use modern named imports, `apo.generate`, document selection, explicit host outputs and the editor's normal preview where appropriate.

The script changes the current document's existing producer dialect; it does not convert a generic flame into an Apophysis renderer dialect just because the source is Pascal. Existing legacy documents retain their producer-specific numerical interpretation. Modern generation creates new Reborn models.

## Verification against original code

`script-tests` exercises complete original-style programs with declarations, construction after `Clear`, loops, branches, trigonometry, scalar/indexed properties, named variations, JuliaN parameters, palettes, output requests and intentional rejection paths. Expected model values include the B/C swap, indexed coefficient signs, scale percent-of-width and angle radians. It also tests metadata identity through removal/copy/clear and source-encoding preservation.

For affine parity, the original **unmodified** `TXForm.Mul33`, `Identity`, `Rotate`, `Translate`, `Multiply`, and `Scale` method bodies were extracted from the pinned 2.09 `XForm.pas` and compiled with **Free Pascal 3.2.2** on x86-64 Linux. The reference initialized XML-order coefficients to:

```text
0.4  0.2  -0.3  0.5  1  -2
```

It then called `Rotate(37); Scale(0.75); Translate(1.2,-0.7); Multiply(0.9,-0.4,0.2,1.1);`. The six original-code results embedded in the tests are:

| Coefficient | Original Pascal result |
| --- | ---: |
| a | 0.28472677975059152 |
| b | -0.19876926107717069 |
| c | 0.12836821882462862 |
| d | 0.45999130855608933 |
| e | 1.43835991409135850 |
| f | -2.24924251482820910 |

The test runs the corresponding legacy script through the new parser/evaluator and checks each result within `3e-12`. This tests the affine APIs against executable original methods. It does not run the proprietary TatScripter interpreter or establish arbitrary historical script compatibility.

To repeat the independent numerical extraction with an installed Free Pascal compiler:

```sh
python3 tests/script_reference_generate.py /path/to/Apophysis-2.09-source \
  /tmp/script-affine-reference.tsv --fpc fpc
```

The generator verifies the original unit's SHA-256 before extraction and writes original code only into a temporary build directory. An optional repeatable `--unit-path` supplies compiler unit directories when using an unpacked FPC distribution. Ordinary CTest execution needs neither the old source archive nor Free Pascal.
