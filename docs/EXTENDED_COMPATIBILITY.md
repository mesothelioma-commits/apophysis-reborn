# Milestone 8: scripting, plugins and additional formats

This milestone adds a working script editor, a modern Lua API, a strict subset
of the original Apophysis scripting language, explicitly enabled native
variation plugins, native projects and additional palette/UPR interchange.
The existing flame editor, original-source preservation, CPU rendering,
Vulkan fallback rules and render queue remain the foundation for these tools.

For implementation details and complete compatibility boundaries, see
[Scripting](SCRIPTING.md), [Legacy scripting](LEGACY_SCRIPTING.md),
[Native variation plugins](PLUGINS.md), [Native projects](PROJECT_FORMAT.md) and
[Additional legacy formats](LEGACY_FORMATS.md).

## Start with the included examples

| Example | What to try |
|---|---|
| `examples/native-workspace.apoproject` | Open a native project with the Ember Spiral flame, preview settings and an HD draft render preset |
| `examples/scripts/palette-and-geometry.lua` | Change transform geometry and create a palette with the Lua API |
| `examples/scripts/seeded-generation.lua` | Generate a reproducible collection and mutate its colors |
| `examples/scripts/legacy-triangles.asc` | Construct a flame with the supported original Apophysis-style commands |
| `examples/scripts/batch-renders.lua` | Prepare deferred render requests for a collection |
| `examples/plugins/reborn-wave` | Build and enable the complete `sample_wave` plugin using the new ABI |
| `examples/plugins/legacy-port` | Study a source-level conversion example for the new plugin ABI |
| `examples/legacy-palettes.ugr` | Choose among three gradients derived from the original Apophysis palette routines |
| `examples/classic-rgb.map` | Import an exact 256-row RGB palette |
| `examples/classic-upr-study.upr` | Import the supported historical Apophysis UPR dialect |

The plugin examples contain source, manifests and build instructions. The main
build also places a compiled `reborn_wave` module and its matching `.apoplugin`
sidecar under `build/plugins/reborn-wave`. A source example and a compiled native
module are different deliverables: enabling requires the matching module for the
host's platform and architecture.

## Script editor workflow

1. Open the flame or collection you want to work on and select its starting
   flame. Open **Tools → Script editor…** (`Ctrl+E`).
2. Choose **Lua** or **Legacy Apophysis (supported subset)**, open a script, or
   choose an included example. The editor offers syntax highlighting, an API
   reference, API-name completion with `Ctrl+Space`, a console and source-line
   highlighting for reported errors.
3. Set the random seed and, if needed, the instruction, time and Lua memory
   limits. The seed accepts the full unsigned 64-bit decimal range.
4. Press **Run**. Execution operates on a private copy of the input collection.
   Review the console, compatibility notes and the list of requested outputs.
   A successful run enables **Apply result**; it does not yet alter the open
   document or publish output files.
5. Press **Apply result** to update the document as one undo step and submit the
   permitted external outputs. Close the dialog to discard an unapplied result.

**Stop** requests cancellation. A stopped run, script error, resource-limit
failure or invalid model returns no partial document edit and no deferred
requests to apply. Changes to source or run options invalidate an old result,
so the editor cannot apply a result from different script text or capabilities.

### Render and export capabilities

**Allow queued renders** and **Allow exports** are off by default. A script that
requests a disabled capability fails visibly. To use the batch-render example,
enable queued renders and choose an existing output folder before applying its
result. **Allow replacing output files** is separate and is also off by default.

Scripts supply relative output names. The host validates them under the chosen
output folder, checks duplicate destinations, protects the input document,
script, explicitly supplied input files and edited-document destination, and
rejects outputs that resolve outside the permitted folder. The desktop also
protects destinations owned by unfinished queue jobs. Scripts cannot choose
checkpoint directories; render checkpoints belong to the host.

**Add input file…** supplies a named input explicitly. The script can import
that named collection through `apo.import_library('alias')`. It cannot open an
arbitrary path, invoke a process, access the network or use Lua's unrestricted
filesystem libraries.

Run-time rollback and external publication have different boundaries. Applying
a successful result creates one undo step for document edits. Undo does not
remove already exported files or cancel render jobs that were submitted from
that result. Each output file is written atomically; a batch is not a multi-file
transaction. If a later I/O operation fails, earlier completed files remain and
the host reports partial publication. The command line similarly publishes the
edited document after its requested outputs complete.

Native plugins are trusted in-process code and do not inherit the Lua host's
filesystem or resource restrictions. A native callback that never returns
cannot be interrupted by the Lua instruction hook or the renderer's normal
cancellation checks.

### Modern Lua scope

The modern API uses one-based flame and transform indices. It supports flame and
transform edits, affine geometry, built-in and enabled plugin variations,
parameters, palettes, deterministic random generation, mutation, collection
selection/addition/removal, named import capabilities and deferred render/export
requests. `time` is stored keyframe metadata; there is no animation evaluator in
this milestone.

```lua
apo.set_flame {name = 'Palette study', gamma = 2.2}
for i = 1, apo.transform_count() do
  local t = apo.transform(i)
  t.pre[5] = t.pre[5] + 0.05 * math.sin(i)
  apo.set_transform(i, t)
end
apo.palette_from_stops({{0,0.03,0.02,0.12}, {128,0.9,0.1,0.04}, {255,1,0.9,0.6}})
print('Updated', apo.transform_count(), 'transforms')
```

Lua source is text-only. The exposed standard library is intentionally limited;
there are no `io`, `os`, `package`, `require`, `debug`, coroutine, dynamic-load or
protected-call facilities. Unhooked Lua string-pattern operations are excluded.
Instruction, time, Lua memory, host data, source/document size, console, host-call
and request-count limits all apply. The API reference describes the available
functions and exact field names.

### Legacy scripting scope

The legacy mode parses a documented Pascal-like subset before execution. It
supports the tested variable/expression/control-flow forms and original-style
flame, transform, palette, random and renderer commands exposed by that subset.
Legacy transform/flame indices are zero-based, as in the original host.

```pascal
begin
  Flame.Name := 'Legacy study';
  ActiveTransform := 0;
  Transform.Color := 0.5;
  Print('Edited the first transform');
end.
```

This is not the original Delphi script runtime. Unsupported syntax, properties
and APIs fail during preflight; they are not silently ignored or delegated to a
native interpreter. Arbitrary Delphi units, original GUI automation, unrestricted
file operations and every historical script found online are outside the
supported subset. The legacy guide lists precise commands and incompatibilities.

## Native variation plugins

Open **Tools → Plugins…** to inspect discovery results, add search folders or
install a `.apoplugin` package. Discovery and installation read metadata and
module bytes without executing the plugin. Select an entry to see its name,
stable ID, version, parameter ranges, module path, fingerprint and any diagnostic.

Use **Enable…** only after reviewing the concrete module and its fingerprint.
This is the explicit native-code trust boundary. The module runs inside Reborn
with the user's permissions; the fingerprint records identity, not a publisher
signature or a security assessment.

Enabled approvals are saved with the expected SHA-256. On the next launch,
Reborn loads approved unchanged modules before restoring documents and the
queue. Changed, missing or invalid modules produce diagnostics and are not
silently approved. Opening a flame or native project never enables a referenced
module. Removing a search folder is separate from revoking an approval; use
**Disable** to revoke it.

Disable prevents new parsing and creation of terms using that variation.
Existing preview, render and queue snapshots retain their module so their
behavior remains defined. **Remove files…** refuses modules that are still
enabled or retained. Release those snapshots or restart after disabling before
removing/replacing a retained module. The detailed plugin guide explains module
lifetime and checkpoint identity.

### Editing a plugin variation

Select an ordinary or final transform and open **Plugin variations…** in its
variation controls. Set the weight and declared parameter values, then choose
**Apply**. Values are checked against the module's declared real/integer ranges.
The edit participates in document undo and preserves unrelated source data.

Plugin weights and parameters serialize into flame XML with module ID/version
references and an explicit variation order. A missing or disabled variation,
unknown parameter or mismatched saved identity is an explicit render blocker.
The file remains available for preservation and compatible edits; Reborn does
not substitute a different variation.

ABI v1 supports ordinary additive 2D CPU variations. It does not provide native
pre/post stages, direct color, 3D callbacks, GPU plugin kernels, arbitrary
rendering-tool extensions or process isolation. A plugin-dependent flame uses
CPU fallback when allowed; a forced Vulkan request without permitted fallback
fails with a reason.

### Original DLLs and source ports

Original Apophysis DLLs do not implement Reborn's versioned query interface.
Copying an old DLL beside a new sidecar does not convert it. A 32-bit legacy DLL
also cannot be loaded into an x64 process. Reborn reports the relevant
architecture/ABI issue.

The SDK and `legacy-port` example demonstrate conversion at source level for a
representable ordinary 2D calculation. Preserve the original author's license
and test the port's math. Operations that depend on unsupported color, Z,
pre/post or mutable per-orbit behavior need further host support. No automatic
conversion of arbitrary DLL machine code is provided.

## Native projects and persistence

Choose `.apoproject` in **Save As** when you want to preserve more than flame
parameters. The native project stores the entire original flame collection,
preview settings, render settings, named render presets, workspace controls,
metadata, plugin references and reserved opaque future-animation data. The
legacy collection remains in its original encoding, including unsupported
entries and unknown attributes.

The render dialog includes a **Project preset** selector, **Save current
preset…** and **Remove preset** controls. Named presets and the current render
settings are captured in the project. Preview samples, seed, worker/memory/device
choices and workspace settings are restored when applicable. Settings the
current application cannot apply are reported and retained where possible.
Plugin references remain informational; use the plugin manager to explicitly
enable the corresponding code.

Document undo preserves project metadata while restoring flame edits. Workspace
and preset settings are tracked separately from model undo. Native autosave and
recovery include the complete project envelope. Conflict detection fingerprints
the native file bytes, so an external change to project-only settings is still a
file conflict. Saving the project as a plain `.flame` collection carries flame
content; use the native format to retain project-only settings and metadata.

Version 1 has explicit size, nesting and count limits and rejects unknown
project versions, invalid payload encodings and entity declarations. Unknown
extension XML is preserved through ordinary supported edits. The reserved
animation payload is preserved data, not a claim of animation playback.

## UGR, MAP and UPR

The gradient editor now imports and exports `.ugr`, `.map` and the existing
`.gpl` format. UGR collections present an explicit compatible-entry picker.
Linear RGB gradients follow the historical 400-position-to-256-color mapping,
cyclic interpolation and packed RGB conventions. Smooth or opacity gradients
are reported as unsupported. MAP requires exactly 256 RGB rows. RGB export
quantizes to 8-bit channels.

Open a `.upr` file to select a supported historical Apophysis entry. Its complete
original entry is retained as native-project metadata. **File → Export Apophysis
UPR…** checks whether the selected flame can be represented before asking for an
output file.

UPR support is limited to the old `mt.ufm / mt-pixel` fractal formula and
`apophysis.ucl` flame coloring dialect. Unknown formulas, layered scenes,
unsupported semantic fields and unrepresentable flame features fail explicitly.
Exports identify the loss of separate zoom, time/source metadata and RGB
precision. Formula availability in an Ultra Fractal installation and
pixel-equivalent rendering there are not established by these format tests.
Reborn does not execute arbitrary UFM/UCL formulas.

## Command-line reference

Paths below are examples. Use platform-appropriate native plugin modules and
create the output folder before requesting side outputs. `--index` and
`--palette-index` are one-based. For a native project, an omitted flame index
uses its persisted selected entry.

### Scripts

```sh
flame-cpu script --api
flame-cpu script INPUT --script FILE --output RESULT \
  [--language lua|legacy] [--seed N] [--index N] \
  [--instructions N] [--timeout-ms N] [--lua-memory-mib N] \
  [--allow-renders] [--allow-exports] [--output-dir FOLDER] \
  [--import NAME FILE] [--overwrite]
```

`--import NAME FILE` can repeat with distinct names. The other options may appear
once. The CLI chooses Lua for a `.lua` script and legacy mode for other script
extensions unless `--language` is supplied. Use the explicit language option for
ambiguous `.txt` scripts. An existing result or requested output is protected
unless `--overwrite` is supplied; inputs and scripts remain protected even then.

```sh
flame-cpu script examples/ember-spiral.flame \
  --script examples/scripts/palette-and-geometry.lua --output study.apoproject

flame-cpu script examples/ember-spiral.flame \
  --script examples/scripts/legacy-triangles.asc --output legacy-study.flame

mkdir script-output
flame-cpu script study.apoproject --script examples/scripts/batch-renders.lua \
  --output rendered-study.apoproject --allow-renders --output-dir script-output
```

The CLI executes permitted render requests directly; the desktop adds them to
its queue. SIGINT/termination requests cancellation. The final edited document
is published only after the requested outputs complete. Earlier completed files
remain if a later request fails.

### Plugins

```sh
flame-cpu plugins list FOLDER
flame-cpu plugins info MANIFEST
flame-cpu plugins install MANIFEST FOLDER
flame-cpu plugins remove MANIFEST
flame-cpu --plugin TRUSTED_MANIFEST [--plugin TRUSTED_MANIFEST ...] COMMAND...
```

`list`, `info` and `install` do not enable code. `--plugin` applies explicitly to
that invocation and does not create a desktop startup approval. For example:

```sh
flame-cpu --plugin build/plugins/reborn-wave/reborn_wave.apoplugin \
  --flame examples/plugins/reborn-wave/sample-wave.flame \
  --samples 1000000 --output sample-wave.png
```

### Projects, palettes and UPR

```sh
flame-cpu project create INPUT --output FILE.apoproject [--index N] [--overwrite]
flame-cpu project inspect INPUT.apoproject
flame-cpu project extract INPUT.apoproject --output FILE.flame [--overwrite]

flame-cpu palette-import PALETTE --flame INPUT --output RESULT \
  [--index N] [--palette-index N] [--overwrite]
flame-cpu palette-export INPUT --output palette.ugr|map|gpl [--index N] [--overwrite]

flame-cpu upr-import INPUT.upr --output RESULT [--index N] [--overwrite]
flame-cpu upr-export INPUT --output RESULT.upr [--index N] [--overwrite]
```

Project extraction returns the complete original-encoding collection. Use the
existing single-flame `export` command when you want an individual render model.
The existing `inspect`, `save`, script and render paths accept native projects.

## Platform and verification boundaries

Format parsing, source-preservation tests, script/API tests, plugin fixture tests,
command-line workflows and Qt editor tests cover the implemented contracts.
Palette and UPR fixtures are independently generated by executing pinned
original Apophysis Pascal routines; the detailed format guide identifies exactly
which routines and scaffolding are involved.

A native plugin must match the operating system, architecture and ABI of its
host. Windows runtime behavior, desktop driver behavior and a user's installed
Ultra Fractal formula collection require checks on those target installations.
The release's test report records which builds and runtime checks were actually
performed. This guide does not promote a cross-compilation result to a desktop
runtime pass or claim support for untested historical script/plugin families.
