# Scripting in Milestone 8

Apophysis Reborn embeds **Lua 5.5.1** and a separately parsed **Apophysis Pascal subset**. No Lua installation is needed. Lua is the recommended language for new scripts; [LEGACY_SCRIPTING.md](LEGACY_SCRIPTING.md) defines the supported historical syntax, original API conventions and deliberate limits.

A script receives a private document and selected flame. Successful execution returns the edited document, console messages and requested outputs. **Run does not edit the open document or write output files.** In the editor, **Apply result** applies the completed document as one undo step and submits the permitted outputs. An exception, invalid final model, resource limit or Stop discards the entire evaluation result, including its deferred requests.

The evaluator itself has no filesystem, network, process or native-module loading interface. Render/export requests are descriptions for the application to process after successful evaluation. Publication and rendering have their own failure handling; a batch of external files is not a single filesystem transaction. Already published files are not undone by editing the document's undo history.

## First run

Open **Tools → Script editor…** (`Ctrl+E`), choose an example or open a `.lua` file, and press **Run** (`Ctrl+Return`). Review the console and output list, then choose **Apply result**. **Stop** cancels the worker without applying its document. The script editor provides syntax highlighting, `Ctrl+Space` API completion, an API reference, example scripts, source open/save and an automatically saved draft.

The examples are also ordinary files in [examples/scripts](../examples/scripts):

- `palette-and-geometry.lua` edits the current flame's geometry and palette.
- `seeded-generation.lua` appends three reproducible generated flames and mutates them.
- `batch-renders.lua` prepares one render request per flame.
- `legacy-triangles.asc` constructs a three-transform flame using historical Pascal syntax.

Command-line execution uses a separate result path:

```sh
flame-cpu script examples/ember-spiral.flame \
  --script examples/scripts/palette-and-geometry.lua \
  --output script-study.flame

flame-cpu script examples/ember-spiral.flame \
  --script examples/scripts/batch-renders.lua \
  --output batch-result.apoproject \
  --allow-renders --output-dir rendered-studies

flame-cpu script --api
```

The CLI chooses Lua for `.lua` and legacy mode for other script extensions. `--language lua` or `--language legacy` selects explicitly. `--index N` is one-based; without it, a native project uses its saved selection and a flame library selects its first entry. `--seed`, `--instructions`, `--timeout-ms` and `--lua-memory-mib` configure execution. `--overwrite` explicitly permits replacement after the host's path validation. Ctrl+C requests cancellation.

## Lua values and indices

Modern flame and transform indices begin at **1**. Palette RGB values are floating-point channels in **[0,1]**. Rotation is in degrees; camera scale is pixels per unit at the flame's declared image size. The palette stop's first number is the actual palette slot, **0 through 255**.

`apo.flame()`, `apo.transform(i)` and `apo.palette()` return detached Lua tables. Editing a returned table has no effect until a corresponding setter is called. Setters accept partial updates. Unknown field names, unavailable variation names and invalid parameters produce errors; a misspelled field is not silently ignored.

```lua
local t = apo.transform(1)
t.pre[5] = t.pre[5] + 0.1
apo.set_transform(1, t)

-- A partial update changes only the supplied fields.
apo.set_flame {name = 'Warm study', gamma = 2.3}
apo.set_transform(1, {color = 0.35})
```

Model validity is checked at commit boundaries. A script can temporarily clear all transforms or set all variation weights to zero while constructing a replacement. The result must be valid before selecting another flame, duplicating a flame, requesting an output or finishing. A valid flame has at least one positively weighted ordinary transform and valid variation terms.

## Document and settings API

| Call | Behavior |
| --- | --- |
| `apo.count()` | Number of flames in the working library. |
| `apo.selected()` | Current one-based flame index. |
| `apo.select(index)` | Validate pending edits and select another renderable entry. |
| `apo.flame()` | Return current settings. |
| `apo.set_flame(fields)` | Apply the supplied settings to the working flame. |
| `apo.new_flame(name)` | Append and select a new identity-transform flame; return its index. |
| `apo.duplicate_flame(name)` | Append a source-preserving copy of the current flame; select and return it. |
| `apo.remove_flame()` | Remove the selected entry and select a neighboring entry. At least one entry remains. |
| `apo.import_library(alias)` | Replace the private working library with a named, explicitly supplied input. |

Settings accepted by `apo.set_flame`:

| Field | Value |
| --- | --- |
| `name` | UTF-8 text, at most 4,096 bytes. |
| `width`, `height` | Positive integral dimensions; normal renderer limits also apply. |
| `center` | `{x,y}`. |
| `scale`, `zoom`, `rotate` | Camera scale, zoom and rotation in degrees. |
| `gamma`, `gamma_threshold`, `brightness`, `vibrancy` | Tone settings with the renderer's normal validity checks. |
| `quality` | Default samples per nominal image pixel. |
| `oversample` | Spatial oversampling factor. |
| `filter_radius` | Explicit reconstruction filter radius. |
| `clear_filter_radius` | `true` removes an explicit filter override. Producer defaults can still apply. |
| `background` | `{r,g,b}` in [0,1]. |
| `palette_mode` | `'step'` or `'linear'`. |
| `time` | Stored keyframe time metadata. M8 does not evaluate an animation timeline. |
| `source_version` | Returned for inspection; changing the producer dialect is rejected. |

Source-preserving edits retain the library's encoding, other entries, comments and supported metadata. Transform removal preserves the surviving transform's source identity; a copied transform carries its source metadata, and a newly constructed transform receives a fresh element. Producer-specific settings are reparsed before output snapshots, so a captured render agrees with the returned document's interpretation. Unsupported selected entries must be repaired through normal document tools before executing a script on them.

## Transforms, final transforms and variations

```lua
apo.set_transform(1, {
  pre = {0.6, 0.1, -0.2, 0.5, 0.4, -0.1},
  post = {1, 0, 0, 1, 0, 0},
  weight = 1,
  color = 0.25,
  color_speed = 0.5,
  opacity = 1,
  variations = {
    {name = 'linear', weight = 0.7},
    {name = 'julian', weight = 0.3}
  },
  julian_power = 3,
  julian_dist = 0.7
})
```

Affine arrays use XML coefficient order `{a,b,c,d,e,f}`:

```text
x' = a*x + c*y + e
y' = b*x + d*y + f
```

| Call | Behavior |
| --- | --- |
| `apo.transform_count()` | Ordinary transform count. |
| `apo.transform(index)` | Detached transform settings table. |
| `apo.set_transform(index, fields)` | Partial update of an existing transform. |
| `apo.add_transform(fields)` | Append a transform; return its one-based index. Omitted fields have identity/linear defaults. |
| `apo.remove_transform(index)` | Remove a transform and its incoming/outgoing chaos entries. Temporary empty construction state is permitted. |
| `apo.clear_transforms()` | Clear ordinary transforms for reconstruction. The final transform is controlled separately. |
| `apo.final_transform()` | Detached final-transform table, or `nil`. |
| `apo.set_final_transform(fields_or_nil)` | Add/edit the final transform, or remove it with `nil`. |
| `apo.variations()` | Names of available built-ins and explicitly enabled native plugins. |

Transform fields are `pre`, `post`, `weight`, `color`, `color_speed`, `opacity`, `julian_power`, `julian_dist`, `chaos` and `variations`. `chaos` is an array of per-destination weights; omitted trailing entries equal 1. A final transform has no chaos row. Its selection weight is not used.

Built-in terms can set their parameters by local name, for example `{name='perspective', weight=1, parameters={angle=0.25, dist=2}}`. The [variation guide](VARIATIONS.md) lists the available fields.

Variation arrays preserve their explicit order. A shorthand map such as `{linear=0.7, swirl=0.3}` is also accepted; map terms are sorted by name for deterministic evaluation. For effects whose operation order matters, use an array. JuliaN's built-in parameters remain transform fields. Built-in terms reject arbitrary per-term parameters.

An enabled native plugin accepts its declared parameter names:

```lua
apo.set_transform(1, {
  variations = {
    {name='sample_wave', weight=1,
     parameters={amplitude=0.2, frequency=3}}
  }
})
```

The host must already have enabled the matching plugin. A script cannot install, enable, load or discover native modules. Plugin parameter ranges and integer constraints are validated. [PLUGINS.md](PLUGINS.md) explains the separate trusted-native-code boundary.

## Palettes

`apo.palette()` returns 256 RGB triples. `apo.set_palette(colors)` accepts exactly 256 triples. Values must be finite and inside [0,1] when the model is committed.

```lua
apo.palette_from_stops({
  {0,   0.01, 0.02, 0.10},
  {120, 0.80, 0.12, 0.05},
  {255, 1.00, 0.90, 0.60}
}, 'rgb')
```

`apo.palette_from_stops(stops, space)` accepts 2–256 stops and `'rgb'` or `'hsv'`; HSV follows the shorter hue arc. `apo.rotate_palette(shift)` moves entry 0 toward the positive shift. `apo.reverse_palette()` reverses the entries. `apo.random_palette(seed, stops)` uses a seeded palette, with five stops when omitted. See [CREATIVE_ALGORITHMS.md](CREATIVE_ALGORITHMS.md) for the shared palette algorithms.

## Reproducible generation and mutation

```lua
local first = apo.generate {
  seed = '18446744073709551615',
  count = 3,
  min_transforms = 3,
  max_transforms = 5,
  variations = {'linear', 'swirl'},
  width = 768, height = 576,
  symmetry = 1, reflection = false,
  randomize_palette = true
}

for i = first, first + 2 do
  apo.select(i)
  apo.mutate {seed=100+i, strength=0.2, colors=true}
end
```

Generation appends one flame for each seed `seed+i`, beginning with offset zero, and selects the first. This gives each batch member an explicit reproducible seed. Generation and mutation variation allowlists contain built-in names; custom plugin terms can subsequently be set through the transform API.

Mutation edits the current flame and retains its topology and camera. Fields are `seed`, `strength`, `trend`, `geometry`, `variations`, `colors`, `parameters`, `palette`, `include_final` and `allowed_variations`. Omitted flags use the shared creative tool defaults: geometry enabled, the other mutation groups disabled. A zero strength leaves the model unchanged.

`apo.random()` returns [0,1). `apo.random(max)` and `apo.random(min,max)` return integers in the inclusive interval. `apo.randomseed(seed)` resets this stream. `math.random` and `math.randomseed` use the same bounded host stream. Integer intervals are limited to the precisely representable 53-bit range.

Full unsigned 64-bit seeds can be supplied as decimal **strings**. This avoids rounding in external tools that store numeric JSON values as floating-point numbers. Script random values use the existing Reborn MT19937-64 implementation. They do not reproduce Delphi's historical random sequence.

## Deferred outputs and named inputs

```lua
apo.render('frames/study-001.png', {
  width=1920, height=1080,
  samples=5000000, seed=42,
  format='png16', alpha=true,
  threads=4, memory_mib=512, disk_mib=16384,
  backend='auto'
})
apo.export_flame('study.flame')
apo.export_library('library.flame')
apo.export_palette('palette.gpl')
```

Enable **Allow queued renders** or **Allow exports** in the editor, or pass `--allow-renders` / `--allow-exports` to the CLI. Select an output folder in the host. Requests fail immediately when their capability is absent. Destinations must be unique relative paths; roots, drive names, parent components and unsafe path components are rejected. The host separately checks the resolved output tree, existing files, symlinks and protected application files before publication.

Each request captures the state at the request call. Later edits do not alter previous render or export snapshots. Render width overrides scale the camera proportionally to width, preserving horizontal framing. Without an explicit `samples`, the resized flame's normal quality setting supplies the sample count. Format names come from the existing image exporters: `bmp8`, `ppm8`, `ppm16`, `png8`, `png16`, `tiff8`, `tiff16`; a filename extension chooses the default format. Alpha is validated against the selected exporter. The evaluator plans the render and returns it; sampling runs through the existing render queue or CLI host.

Scripts cannot supply checkpoint directories or request resume of arbitrary work files. Those are host responsibilities. Render outputs retain immutable flame/plugin snapshots for later execution.

Named imports are explicit read capabilities:

```sh
flame-cpu script original.flame --script combine.lua --output result.flame \
  --import source second-library.flame
```

```lua
apo.import_library('source')
apo.set_flame {name='Imported selection'}
```

In the editor, **Add input file…** makes a file available under the displayed basename. `apo.import_library` replaces the private working library and selects its first flame. It does not merge libraries, interpret a path or open additional files.

## Sandbox and resource limits

Available libraries are selected base functions, `math`, `table`, `utf8` and bounded string primitives. The following interfaces are absent: `io`, `os`, `package`, `require`, `debug`, `coroutine`, `load`, `loadfile`, `dofile`, `collectgarbage`, `warn`, `pcall`, `xpcall`, `setmetatable`, `getmetatable`, `string.dump`, and string pattern functions `find`, `match`, `gmatch`, `gsub`. Lua source is loaded in text-only mode. No callable host userdata or native handles are exposed.

Protected calls and metatable creation are omitted so scripts cannot indefinitely catch quota failures or register arbitrary finalizers. Pattern matching is omitted because Lua's C pattern engine can perform extensive backtracking without triggering a VM instruction hook. Ordinary string operations, formatting, sorting, numeric work and UTF-8 functions remain available. Host operations have explicit size bounds and check cancellation/deadline at their boundaries and in their host loops. Cancellation is cooperative; a bounded native operation finishes before its next check.

| Default budget | Limit |
| --- | ---: |
| Lua VM instructions | 2,000,000 |
| Wall-clock deadline | 10 seconds |
| Lua allocation budget | 32 MiB |
| Conservative aggregate host data budget | 64 MiB |
| Source bytes | 1 MiB |
| Working/imported document bytes | 16 MiB per document |
| Console text | 64 KiB including separators/newlines |
| Deferred requests | 64 |
| Host API calls | 10,000 |
| Flame entries | 1,024 |
| Ordinary transforms per flame | 256 |

Host accounting reserves space for source indexing, working documents, conversion/merge temporaries, preloaded imports, console text and all retained output snapshots. The host budget is a conservative data reservation, not a measurement of process RSS. Legacy translation reserves additional parser space and can reach the aggregate limit before the raw source-byte limit. Other fixed bounds include 128 variation terms, 128 plugin parameters per term, 64 KiB per host string argument and 4 KiB for error diagnostics.

`ScriptResult.instructions` reports completed hook quanta, not an exact instruction count. On successful execution the VM count is at least `instructions` and below `instructions + instruction_quantum`; `instruction_quantum` is the smaller of 1,000 and the configured instruction limit. Native host work is governed by host/deadline limits rather than this VM counter.

The regression suite covers Lua memory failure during startup, runtime allocation exhaustion, host budget exhaustion, bounded console/diagnostic output, infinite loops, cancellation, strict legacy preflight, transaction rollback, immutable requests and source preservation. Lua is compiled as C++ so Lua errors unwind C++ callback objects safely. See [the vendored Lua provenance](../third_party/lua/README.md), [Lua's official manual](https://www.lua.org/manual/5.5/manual.html) and [its license](https://www.lua.org/license.html).
