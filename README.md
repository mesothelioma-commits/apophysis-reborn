# Apophysis Reborn 0.8.0 — Extended Compatibility

Milestone 8 adds a native script editor, a restricted modern **Lua 5.5.1** runtime, a documented subset of legacy Apophysis Pascal scripting, a versioned native variation-plugin SDK and manager, and more project and palette formats. The existing CPU/Vulkan renderer, creative tools, persistent render queue and 16K export remain available.

## New in this checkpoint

| Area | What you can do |
| --- | --- |
| Modern scripting | Edit flames, transforms, variations, parameters and palettes; generate/mutate reproducible studies; read explicitly supplied inputs; prepare batch renders and exports. Lua is bundled, so no scripting runtime needs installing. |
| Legacy scripts | Run the documented `.asc` Pascal subset, with source-line diagnostics and original-source checks for affine operations, coordinate conventions and palette/parameter behavior. Unsupported syntax or APIs fail before the active document changes. |
| Script editor | Open/save scripts, use highlighted examples and API completion, review console output, Run/Stop a private working copy, and Apply a successful result as one document undo step. Rendering/export capabilities start disabled. |
| Variation plugins | Discover and inspect manifests without executing code; explicitly enable trusted modules; install/remove packages and manage search paths. ABI/version/range checks, saved binary approvals and snapshot lifetime rules make failures visible. |
| Native projects | Save `.apoproject` files containing the exact legacy collection, preview/workspace/render settings, named render presets, a thumbnail, plugin references and retained metadata. Full-file conflict checks and interrupted-session recovery cover the envelope. |
| Additional formats | Import/export UGR gradients and Fractint MAP palettes; import/export the supported historical Apophysis UPR dialect with explicit compatibility reports. |

![Native script editor after preparing a plugin-based render](preview/script-editor-0.8.0.png)

The screenshot is from the actual Qt application using the offscreen platform. A separate Linux desktop check exercised plugin enablement, Lua Run/Apply, Undo/Redo, plugin parameter editing and native project saving. Windows binaries are cross-compiled and dependency-audited; Windows execution and physical-GPU acceptance remain pending.

## Start with the new tools

Extract the complete ZIP and launch `bin/windows-x64/apophysis-reborn.exe`. Open **Tools → Script editor** (`Ctrl+E`), choose an example, press **Run**, review the console, then **Apply result**. Running alone does not alter the active document. Stop or failure discards private edits and all deferred requests. To use the batch example, explicitly allow queued renders and choose an output folder. Document Undo does not remove files already exported or jobs already handed to the queue.

Open **Tools → Plugins** to inspect the bundled `sample_wave` module. It starts disabled. **Enable** presents the module identity and binary hash before executing native code. After enabling it, use **Variations → Plugin variations** to set its weight and parameters. Plugin variations run on the CPU; automatic/Vulkan requests either report CPU fallback or fail when fallback is disabled. Existing render snapshots keep their module alive after disabling it.

Use **Save as → Native projects** to retain workspace and render settings. The Render image dialog now has named project presets. Saving as `.flame` or `.xml` writes the legacy collection without the native project envelope. The Gradient editor's Import/Export actions support `.gpl`, `.ugr` and `.map`; **Open project or library** also accepts supported `.upr` entries.

The [extended compatibility guide](docs/EXTENDED_COMPATIBILITY.md) gives the complete workflow. See [modern scripting](docs/SCRIPTING.md), [legacy scripting scope](docs/LEGACY_SCRIPTING.md), [plugin SDK/lifecycle](docs/PLUGINS.md), [native project contract](docs/PROJECT_FORMAT.md) and [historical formats](docs/LEGACY_FORMATS.md) for exact behavior and limitations.

**Compatibility boundaries:** legacy scripting is a tested subset, not the complete historical interpreter. Native modules execute trusted code inside the application and are not isolated from crashes or hangs. Original 32-bit Apophysis DLLs require source migration to the new 64-bit ABI. UPR support is limited to the identified Apophysis flame-coloring dialect; arbitrary Ultra Fractal formulas and renderer pixel equivalence are not claimed. Animation data can be preserved, but a timeline and animation evaluator are Milestone 9 work.

## Command-line extensions

These examples run from Git Bash in the extracted project directory:

```bash
./bin/windows-x64/flame-cpu.exe project create examples/ember-spiral.flame --output study.apoproject
./bin/windows-x64/flame-cpu.exe script study.apoproject \
  --script examples/scripts/palette-and-geometry.lua --output scripted.apoproject
./bin/windows-x64/flame-cpu.exe project inspect scripted.apoproject
./bin/windows-x64/flame-cpu.exe palette-export scripted.apoproject --output study.ugr
./bin/windows-x64/flame-cpu.exe plugins info bin/windows-x64/plugins/reborn-wave/reborn_wave.apoplugin
```

The extension commands require `--overwrite` before replacing an existing output. Script renders/exports additionally require their capability flags and an explicit output folder. Paths are checked before publishing outputs; each output is atomic, while an I/O failure after earlier outputs completed does not roll those earlier files back. `script --api` prints the API reference. Prefix a command with `--plugin TRUSTED_MANIFEST` only when you intend to execute that native module.

## Render a large image

Open **File → Render image** (`Ctrl+R`). Select a preset or enter dimensions, choose PNG or TIFF and **16 bit**, and optionally enable **Transparent background (straight alpha)**. The dialog displays the planned working buffers, disk allowance and strip count before adding a job. Choose a destination, then use **Add and start queue** or **Add to queue**.

**Queue** on the toolbar opens the job list. **Pause queue** retains committed sampling; **Start / resume** continues it. The job list records progress, measured phase ETA, actual backend/device and errors. Editing the flame afterward does not change a queued snapshot. Closing the editor requests a safe pause. Keep a job's checkpoint directory if you want to resume later: saving a queue JSON references that directory rather than copying its sampled data.

| Output | Channel depth | Transparency |
| --- | --- | --- |
| PNG / TIFF | 8 or 16 bits | RGB or straight RGBA |
| PPM | 8 or 16 bits | RGB |
| BMP | 8 bits | RGB |

Sixteen-bit values come directly from reconstruction, without expansion from RGB8. The formats are tone-mapped integer images; JPEG, OpenEXR, ICC color management and dithering remain open output work. The [high-resolution guide](docs/HIGH_RES_RENDERING.md), [queue guide](docs/RENDER_QUEUE.md) and [format contract](docs/IMAGE_FORMATS.md) explain budgets, alpha, restart identity and storage behavior.

## Open the editor

Extract the complete ZIP and open `bin/windows-x64/apophysis-reborn.exe`. Keep the accompanying DLLs and `platforms` folder beside it. The program starts with Ember Spiral and works offline. Use **Open project or library** to load a native project, `.flame`, `.xml`, historical `.temp` collection or a supported Apophysis `.upr` entry.

Select a transform in the lower canvas, then drag its origin or its X/Y handles. The **Pre-affine / Post-affine** selector and the coefficient fields edit the same matrix. The right-hand tabs expose geometry, all 31 supported variation weights, JuliaN parameters, color controls, and Xaos. **Save** retains the original collection and unrelated XML. The [editor guide](docs/EDITOR.md) covers the controls and recovery behavior.

## Choose the rendering device

Open **Edit → Preferences** to select **CPU (reference)**, **Automatic**, or **Vulkan (FP32 sampling)**. The same controls appear in **Render image**. A new settings profile defaults to Automatic with CPU fallback enabled; the CLI defaults to CPU. Creative previews inherit the chosen backend, device and budgets.

| Setting | Behavior |
| --- | --- |
| CPU | Retains the existing double-precision renderer and its original random stream |
| Automatic | Uses an explicitly preferred device, or chooses usable hardware Vulkan when device selection is automatic |
| Vulkan | Requests Vulkan compute explicitly; software implementations are identified in discovery and output |
| GPU working budget | Limits Vulkan allocations alongside the separate host budget and device storage-buffer limits |
| CPU fallback | Reports the reason and actual CPU backend when Vulkan cannot render; disabling it makes the Vulkan failure explicit |

The Vulkan kernel uses **32-bit orbit calculations**, exact **64-bit integer histogram totals**, and CPU image reconstruction. CPU reference calculations use 64-bit floating point. Different streams, rounding and chaotic amplification mean matching seeds do not guarantee identical pixels across backends or devices. The preview, Activity panel and CLI report what actually ran.

Read the [GPU rendering guide](docs/GPU_RENDERING.md) for controls and commands, [kernel description](docs/GPU_KERNEL.md) for precision and model admission, [CPU/GPU test methods](docs/GPU_TESTING.md) for the comparisons, and [Windows GPU guide](docs/WINDOWS_GPU.md) for later hardware acceptance.

## Creative tools

Use **Flame → Gradient editor**, **Adjustment editor**, **Mutation explorer**, or **Random flame generator**. The toolbar provides the same tools. Each editor previews a working copy; Apply commits one undo step, while Cancel retains the loaded flame. Random batches and kept mutation branches become ordinary collection entries.

| Tool | Available controls |
| --- | --- |
| Gradient | Individual RGB/HSV colors, indexed stops, RGB/HSV interpolation, presets, GPL/UGR/MAP exchange and local palette library, rotation/reversal/smoothing, seeded random palettes, hue/saturation/value, RGB gamma curves and contrast |
| Adjustment | Dimensions and aspect ratio, camera center/scale/zoom/rotation, orbit framing, background, gamma/threshold/brightness/vibrancy, sample density, oversampling and the supported Gaussian filter |
| Random generation | 1–24 candidates, a 64-bit seed, variation allowlist, transform counts, contraction and translation limits, explicit rotation/reflection symmetry and palette choice |
| Mutation | Candidate grid, strength/trend, independent geometry/variation/color/parameter/palette controls, parent comparison, branching history and selected/batch/branch saving |

Read the [creative tools guide](docs/CREATIVE_TOOLS.md) for exact controls, save behavior and limits.

## Verify the Windows package

When ready, open Git Bash in the extracted `apophysis-reborn` directory and run the desktop verification.

```bash
bash scripts/verify-editor-windows.sh
```

It runs the CPU/core and Qt tests, records device discovery and fallback, exercises the creative dialogs and render queue, and verifies the M8 scripting/plugin/project workflow with the bundled sample module. It saves screenshots, image exports, native projects, example flames and palette files with a log. Add `--large-render` for the separate full 16K queue acceptance and `--no-open` to suppress Explorer. A GPU is not required.

The separate strict GPU check can be run later when a suitable Windows GPU and driver are available:

```bash
bash scripts/verify-gpu-windows.sh
```

It selects a usable physical device by default and requires actual Vulkan execution with no fallback. `--device ID` selects a device from your discovery report; explicitly selected software devices are labeled as such. The [test report](docs/TEST_PLAN.md) distinguishes the executed Linux checks from pending Windows and physical GPU acceptance.

## Command-line legacy files

Run these examples from Git Bash in the extracted project folder. All paths can contain spaces.

```bash
./bin/windows-x64/flame-cpu.exe inspect tests/legacy/fox-3d-hack.flame
./bin/windows-x64/flame-cpu.exe save tests/legacy/fox-3d-hack.flame --output my-copy.flame --name "My study"
./bin/windows-x64/flame-cpu.exe inspect my-copy.flame --json
```

| Command | Result |
| --- | --- |
| `inspect INPUT [--json]` | Lists entries, original names, declared producers, source lines, CPU renderability and the first render blocker for each entry |
| `save INPUT --output COPY [--index N]` | Writes the entire original document. Optional `--name TEXT`, `--size W H`, `--center X Y` and `--scale S` edit one entry. Indexing starts at 1 |
| `export INPUT --output SINGLE --index N` | Writes a canonical, standalone XML representation of one supported render model. This operation omits wrapper data, history, comments and editor-only metadata. Use `save` to retain the original document |
| `--flame INPUT --index N --output IMAGE.bmp` | Renders the selected entry. Unsupported features in other entries do not block it |

A `preserve-only` entry can still be renamed and saved. Make a copy before editing an original you wish to keep. Providing the input path as the save destination requests an in-place replacement. Failed validation leaves an existing destination intact. The CLI stages file writes next to the destination and replaces the output after a successful close.

## Command-line rendering

Render a different file from the supported subset with:

```bash
./bin/windows-x64/flame-cpu.exe --flame examples/ember-spiral.flame --output ember.bmp
./bin/windows-x64/flame-cpu.exe --list-variations
./bin/windows-x64/flame-cpu.exe --list-devices
./bin/windows-x64/flame-cpu.exe --list-devices --json
./bin/windows-x64/flame-cpu.exe --flame examples/ember-spiral.flame \
  --output ember-vulkan.bmp --backend vulkan --device 0 --no-fallback
```

Replace device `0` with a zero-based index or ID from your own device inventory. The final command requires Vulkan; it cannot silently succeed through CPU fallback. `--backend auto` allows reported CPU fallback by default. Completion output includes the actual backend, device and stage timings.

The 31 studies in `examples/variation-studies.flame` cover every registered variation. Use `inspect` to list their names, then select a study by its one-based `--index`. `examples/seeded-random.flame` contains a nine-candidate generated batch; `examples/creative-branches.flame` contains an original, parent and child saved through the native editor. `examples/gpu-palette-study.flame` was edited and saved during the native Vulkan desktop check.

The CLI accepts the earlier seed, samples, threads and backend options plus `--size W H`, `--oversample`, `--filter-radius`, `--bit-depth`, `--alpha`, `--disk-mib`, `--strip-rows`, `--checkpoint`, `--resume` and `--estimate`. The foundation syntax `flame-cpu width height samples output.ppm` remains available.

```bash
./bin/windows-x64/flame-cpu.exe --flame examples/ember-spiral.flame \
  --output ember-4k.png --size 4096 3072 --samples 20000000 \
  --bit-depth 16 --memory-mib 256 --disk-mib 2048 \
  --checkpoint ember-render-work
```

Append `--estimate` to inspect the plan without creating files. For a controlled pause, append `--pause-after 1000000`; successful pause returns 75. Resume with the same settings and `--resume`, omitting `--pause-after`. Ctrl+C also preserves committed work when an explicit checkpoint was chosen. The [CLI guide](docs/HIGH_RES_RENDERING.md) covers all options and exit statuses. A successful export replaces an existing destination only after encoding completes.

## Build from source

CMake 3.20+, a C++20 compiler, Qt 6.5+ Widgets/Test development files, and Expat and zlib development files are required. The packaged GUI uses Qt 6.8.2. Supply the installed Qt kit prefix when needed.

```bash
export APO_QT_PREFIX=/path/to/Qt/6.8.2/your-kit
bash scripts/build-editor.sh
```

A core-only build can use `bash scripts/build.sh -DAPO_BUILD_EDITOR=OFF`. To disable the Vulkan runtime entirely, add `-DAPO_ENABLE_VULKAN=OFF`. Ordinary builds use the bundled headers and embedded SPIR-V; shader regeneration is documented in the [Qt build guide](docs/QT_BUILD.md), alongside the exact Windows toolchain, runtime staging and corresponding dependency sources.

## Verification in this checkpoint

The M8 verification suite contains **33 CTest entries**, adding script runtime/host boundaries, plugin loading/lifetimes and editor controls, native project preservation, historical formats and extension CLI workflows. The final executed results, sanitizer settings, platform boundaries and package checks are recorded in [TEST_PLAN.md](docs/TEST_PLAN.md) and the included `verification` directory. Earlier CPU images, original-producer checks, 1,560 transform probes, streaming output and checkpoint gates remain covered.

The retained M7 acceptance record documents an actual 16K RGBA16 render that used 8,388,611 samples and peaked at **39.2 MiB process RSS** with a 128 MiB working-buffer budget. Two independent decoders agreed on every one of its 2,147,483,648 decoded channel bytes. A matching-camera comparison produced identical complete files using 63-row and 247-row strips, including queue pause/resume. The queue's whole Qt process peaked at 182.8 MiB; the 128 MiB setting limits rendering buffers, not all Qt/library/process memory. The [16K acceptance report](docs/HIGH_RES_ACCEPTANCE.md) distinguishes these measurements and documents the sparse sample density of the acceptance image.

## Included records

| File | What it covers |
| --- | --- |
| [Extended compatibility](docs/EXTENDED_COMPATIBILITY.md) | M8 user workflow, scripting permissions, plugins, native projects and format conversion |
| [Lua scripting](docs/SCRIPTING.md) | Modern API, examples, execution limits and deferred outputs |
| [Legacy scripting](docs/LEGACY_SCRIPTING.md) | Audited Pascal subset, original conventions and unsupported APIs |
| [Plugins](docs/PLUGINS.md) | C ABI, lifecycle, parameter serialization and historical source migration |
| [Native projects](docs/PROJECT_FORMAT.md) | Versioned envelope, exact embedded sources and unknown-data retention |
| [Legacy formats](docs/LEGACY_FORMATS.md) | UGR/MAP/UPR definitions, original-source fixtures and limits |
| [High-resolution rendering](docs/HIGH_RES_RENDERING.md) | CLI options, memory/disk planning, checkpoints and resume |
| [Render queue](docs/RENDER_QUEUE.md) | Native job settings, immutable snapshots, persistence and lifecycle |
| [Image formats](docs/IMAGE_FORMATS.md) | PNG/TIFF/PPM/BMP precision, encoding, color and alpha contracts |
| [16K acceptance](docs/HIGH_RES_ACCEPTANCE.md) | Full-image decoding, exact strip comparison, restart and UI measurements |
| [Vulkan hit streaming](docs/GPU_STREAMING.md) | Bounded compute batches and device checkpoint identity |
| [Editor](docs/EDITOR.md) | Native library, preview, geometry, saving and recovery controls |
| [GPU rendering](docs/GPU_RENDERING.md) | Device selection, precision, budgets, fallback and CLI/native smoke commands |
| [Compute kernel](docs/GPU_KERNEL.md) | GPU stages, portable streams, integer accumulation and admission limits |
| [CPU/GPU verification](docs/GPU_TESTING.md) | Numerical/image comparison fixtures, thresholds and execution evidence |
| [Windows GPU verification](docs/WINDOWS_GPU.md) | Optional strict physical-device tests and diagnostic output |
| [Creative tools](docs/CREATIVE_TOOLS.md) | Gradient, adjustment, random generation and mutation workflows |
| [Creative algorithms](docs/CREATIVE_ALGORITHMS.md) | Deterministic generation, palette operations and framing definitions |
| [Variations](docs/VARIATIONS.md) | Complete supported registry, producer conventions and original-source vector evidence |
| [Qt build](docs/QT_BUILD.md) | Desktop build, runtime deployment and Windows checks |
| [Engine](docs/RENDERING_ENGINE.md) | How inputs become pixels, accepted numeric settings, rendering differences |
| [Architecture](docs/ARCHITECTURE.md) | Model, parser, sampling, image output and extension points |
| [Compatibility](docs/COMPATIBILITY.md) | Exact XML subset, omitted-field behavior, serializer limits, historical files |
| [Historical corpus](docs/LEGACY_CORPUS.md) | Original producer files, exact round trips and reference-image evidence |
| [Producer pixel tests](docs/PRODUCER_ORACLE.md) | Direct comparisons with historical Pascal image reconstruction |
| [Reference tests](docs/REFERENCES.md) | Upstream C function comparisons, independently rendered image, regeneration |
| [Test report and plan](docs/TEST_PLAN.md) | Executed checks, local timings and pending platform acceptance |
| [Feature matrix](docs/FEATURE_MATRIX.md) | Current milestone and outstanding work |
| [Development log](docs/DEVELOPMENT_LOG.md) | Changes since the foundation and next milestone handoff |
| [Third-party notices](docs/LICENSES.md) | Dependency and reference provenance |

The ZIP also includes `apophysis-reborn.bundle` and `BUILD.txt`. For a checkout with the complete checkpoint history, run `git clone apophysis-reborn.bundle ../apophysis-reborn-git` from the extracted folder. The checkout holds source, example flames, tests and documentation. The precompiled Windows executables and their runtime DLLs are distribution outputs. Corresponding third-party sources accompany the distribution. No remote repository is required.

**Next: Milestone 9 — animation and extended features**, beginning with keyframes, interpolation, morphing and a native timeline. Physical NVIDIA/AMD/Intel execution, Windows 11 behavior and hardware performance measurement remain separate acceptance work. CUDA is optional and not implemented. Advanced editor and creative compatibility gaps remain listed in [FEATURE_MATRIX.md](docs/FEATURE_MATRIX.md), including additional variations, historical reconstruction curves, density estimation, palette catalogs and broader workspace controls.
