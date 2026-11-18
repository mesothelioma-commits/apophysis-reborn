# Apophysis Reborn 0.9.0 — Animation and Color

Milestone 9 adds a native keyframe timeline, explicit flame morphing, playback, resumable image-sequence export and extended color tools. Original 7X weighted color curves are now interpreted with their verified historical behavior. The existing source-preserving editor, scripting/plugin system, CPU/Vulkan renderer, persistent queue and large-image exports remain available.

## New in this checkpoint

| Area | Delivered behavior |
| --- | --- |
| Timeline | Keyframe capture/import, frame ruler and scrubbing, playback, rational fps/duration, loop modes, local Undo/Redo and one-step project timeline undo |
| Animation | Camera, affine/post/final transforms, variation weights/real parameters, color, opacity, Xaos, palettes and curves; explicit handling of discrete settings and exact keyframe endpoints |
| Morphing | Stable transform correspondence IDs, strict matching or explicit fading of unmatched branches, polar/linear affine policies and hold cuts for incompatible snapshots |
| Presets | Still, full camera orbit, zoom and palette cycle; timed legacy collection import with explicit frame/second interpretation |
| Frame export | Fixed-canvas PNG/TIFF/PPM/BMP sequences, original frame-number filenames, full-range preflight, deterministic seeds and bounded batches |
| Resume | Existing Qt queue snapshots/checkpoints and a separate CLI sequence manifest with verified SHA-256 outputs, crash recovery and partial-frame sampling resume |
| Color | RGB/HSV/HSL controls, linear-light RGB and shortest-hue palette interpolation, editable weighted master/R/G/B tone curves, exact 7X mode and explicit continuous mode |

![Native animation timeline](preview/timeline-0.9.0.png)

This is the actual Qt timeline. Final verification distinguishes automated offscreen workflows, native Linux desktop interaction, the audited Windows cross-build and the still-pending Windows/physical-GPU execution.

## Start with the timeline

Extract the complete ZIP and launch `bin/windows-x64/apophysis-reborn.exe`. Open **Tools → Animation timeline** (`Ctrl+T`) or **Timeline** on the toolbar. Choose Camera orbit, Zoom or Palette cycle and **Apply preset** for a starting clip. Scrub, play, edit keyframes and adjust timing. **Apply timeline** stores the result in the active document; Cancel discards the dialog's changes.

**Capture editor flame**, **Add at frame** and **Import** build clips from your own flames. **Correspondence** exposes the IDs that pair transforms between keyframes. **Gradient** and **Adjustments** edit individual keyframes. Use **Use frame in editor** for the full transform/variation editor, then capture the edited flame back into the timeline. Review intermediate frames when morphing different flames.

The **Image sequence** tab selects a finite range, output folder, fixed canvas, format/depth/alpha and render budgets. **Apply & queue frames** adds independent frame snapshots to the existing queue. The range is preflighted before admission, and later edits do not change those jobs. Batches and the queue are limited to 1,000 jobs; longer clips use explicit subranges. No video encoder is bundled.

Save animation work as **`.apoproject`**. Save cannot silently strip a timeline into a plain flame file. **File → Export flame library** writes a separate source-preserving collection. Unknown/future animation payloads remain preserved and cannot be implicitly replaced in the timeline editor.

The [timeline guide](docs/ANIMATION_EDITOR.md), [animation model](docs/ANIMATION_MODEL.md), [morphing rules](docs/MORPHING.md), [sequence CLI](docs/ANIMATION_CLI.md) and [color tools](docs/COLOR_TOOLS.md) explain the complete contract and limits.

## Command-line animation

Run these examples from Git Bash in the extracted project directory:

```bash
./bin/windows-x64/flame-cpu.exe animation create examples/ember-spiral.flame \
  --preset camera-orbit --frames 121 --fps 30 --output orbit.apoproject
./bin/windows-x64/flame-cpu.exe animation inspect orbit.apoproject --json
mkdir -p orbit-frames
./bin/windows-x64/flame-cpu.exe animation render orbit.apoproject \
  --output-dir orbit-frames --first 0 --last 120 --width 1280 --height 720 \
  --samples 1000000 --format png16 --memory-mib 256 --disk-mib 8192 --estimate
```

Remove `--estimate` to render. Resume the identical command with `--resume`; completed files are skipped only after their saved size and SHA-256 match. The manifest binds the clip, selected range, canvas, render settings, plugins and renderer executable. An upgrade/rebuild requires new sequence state. Completed earlier images remain available after a later failure. [Complete CLI options](docs/ANIMATION_CLI.md)

## Scripting, plugins and historical formats

**Tools → Script editor** (`Ctrl+E`) runs bundled Lua 5.5.1 or the documented legacy Pascal subset on a private working document. Run/Stop and Apply remain separate. Rendering/export capabilities start disabled. **Tools → Plugins** discovers manifests without executing them and explicitly enables trusted native modules. The bundled `sample_wave` illustrates the 64-bit variation ABI; old 32-bit DLLs require source migration. Native plugin callbacks run in-process on the CPU.

Native projects retain exact source collections, settings/presets, thumbnails, plugin references and unknown metadata. GPL/UGR/MAP palettes and the supported historical Apophysis UPR dialect remain available. See [extended compatibility](docs/EXTENDED_COMPATIBILITY.md), [Lua](docs/SCRIPTING.md), [legacy scripts](docs/LEGACY_SCRIPTING.md), [plugins](docs/PLUGINS.md), [project format](docs/PROJECT_FORMAT.md) and [historical formats](docs/LEGACY_FORMATS.md).

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
| Gradient | Individual RGB/HSV/HSL colors, indexed stops, RGB/linear-light RGB/shortest-hue HSV/HSL interpolation, presets, GPL/UGR/MAP exchange and local palette library, rotation/reversal/smoothing, seeded random palettes, hue/saturation/value, weighted palette curves, gamma and contrast |
| Adjustment | Dimensions and aspect ratio, camera center/scale/zoom/rotation, orbit framing, background, gamma/threshold/brightness/vibrancy, sample density, oversampling, master/R/G/B tone curves and the supported Gaussian filter |
| Random generation | 1–24 candidates, a 64-bit seed, variation allowlist, transform counts, contraction and translation limits, explicit rotation/reflection symmetry and palette choice |
| Mutation | Candidate grid, strength/trend, independent geometry/variation/color/parameter/palette controls, parent comparison, branching history and selected/batch/branch saving |

Read the [creative tools guide](docs/CREATIVE_TOOLS.md) for exact controls, save behavior and limits.

## Verify the Windows package

When ready, open Git Bash in the extracted `apophysis-reborn` directory and run the desktop verification.

```bash
bash scripts/verify-editor-windows.sh
```

It runs the CPU/core and Qt tests, records device discovery and fallback, exercises the creative dialogs and render queue, and verifies the M8 scripting/plugin/project workflow and M9 timeline/color/sequence workflow with the bundled sample module. It saves screenshots, image exports, native projects, example flames and palette files with a log. Add `--large-render` for the separate full 16K queue acceptance and `--no-open` to suppress Explorer. A GPU is not required.

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

The M9 verification suite contains **39 CTest entries**, covering the animation model, independent original 7X curves, sequence crash/resume and CLI behavior, native timeline/color editing and all earlier gates. The final executed results, sanitizer settings, platform boundaries and package checks are recorded in [TEST_PLAN.md](docs/TEST_PLAN.md) and the included `verification` directory. Earlier CPU images, original-producer checks, 1,560 transform probes, streaming output and checkpoint gates remain covered.

The retained M7 acceptance record documents an actual 16K RGBA16 render that used 8,388,611 samples and peaked at **39.2 MiB process RSS** with a 128 MiB working-buffer budget. Two independent decoders agreed on every one of its 2,147,483,648 decoded channel bytes. A matching-camera comparison produced identical complete files using 63-row and 247-row strips, including queue pause/resume. The queue's whole Qt process peaked at 182.8 MiB; the 128 MiB setting limits rendering buffers, not all Qt/library/process memory. The [16K acceptance report](docs/HIGH_RES_ACCEPTANCE.md) distinguishes these measurements and documents the sparse sample density of the acceptance image.

## Included records

| File | What it covers |
| --- | --- |
| [Animation timeline](docs/ANIMATION_EDITOR.md) | Native keyframes, morphing, playback, queue and project workflows |
| [Animation model](docs/ANIMATION_MODEL.md) | Versioned payload, interpolation, correspondence and exact endpoints |
| [Animation CLI](docs/ANIMATION_CLI.md) | Frame creation/export, bounded sequence planning and verified resume |
| [Color tools](docs/COLOR_TOOLS.md) | RGB/HSV/HSL palettes, original 7X and continuous curves, oracle provenance |
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

**Next: Milestone 10 — release hardening, packaging and remaining platform acceptance.** Physical NVIDIA/AMD/Intel execution, Windows 11 behavior and hardware performance measurement remain open. CUDA is optional and not implemented. The compatibility inventory in [FEATURE_MATRIX.md](docs/FEATURE_MATRIX.md) tracks additional variations, active 3D/direct-color behavior, density estimation, broader historical animation support and remaining editor controls.
