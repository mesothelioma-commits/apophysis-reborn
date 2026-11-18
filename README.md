# Apophysis Reborn 0.11.0

Milestone 11 adds the twelve remaining variations from the original brief. All 40 named starting variations are now available in the editor and on the CPU and Vulkan renderers. The registry also includes noise, foci and pre_blur, for 43 built-ins.

**0.11.0 completes the starting variation list. Complete original Apophysis 2.x/7X/AV feature parity remains unfinished.** The [release compatibility report](docs/RELEASE_COMPATIBILITY.md) traces the original brief's 25 sections to implemented behavior and explicit remaining requirements. Windows 11 lifecycle/UI/DPI checks and physical NVIDIA/AMD/Intel acceptance remain separate from cross-compilation, Linux testing and software Vulkan.

## Choose a distribution

| Edition | Entry point and contents |
| --- | --- |
| Lean portable | Extract `apophysis-reborn-0.11.0-windows-x64-portable.zip`; open `apophysis-reborn.exe` in its `apophysis-reborn-0.11.0-windows-x64` folder. Includes the CLI, required runtime files, examples, documents, notices and corresponding dependency sources. Keep those files together. |
| Per-user installer | Run `apophysis-reborn-0.11.0-windows-x64-setup.exe`. Defaults to `%LOCALAPPDATA%\Programs\Apophysis Reborn\0.11.0`, without administrator elevation or file-association takeover. Versioned shortcuts and an uninstall entry are supplied; the desktop shortcut is optional. |
| Complete source/history and SDK | Extract `apophysis-reborn-0.11.0-starting-variations.zip`; applications are in `apophysis-reborn/bin/windows-x64`. Includes all test executables, scripts, source/history, plugin SDK examples and release evidence. Use this edition for the verification and build commands below. |
| Git source | Extract `apophysis-reborn-0.11.0-git-source.zip` and run `git clone apophysis-reborn.bundle restored-source` in the extracted `apophysis-reborn` directory. The default `main` branch contains the release with its history and tags. |

Setup requires an empty destination and keeps different versions side by side. Uninstall removes unchanged installed payload files and retains modified/extra files; external projects, settings and recovery data remain. An interrupted install can leave a partial folder; automatic repair or replacement is not provided. Portable and installed applications normally share per-user settings/data, so separate program directories are not isolated user profiles. Keep artwork outside the program folder and retain the original executable/plugins for in-progress checkpoints. [Getting started and recovery](docs/USER_MANUAL.md)

## New variations in 0.11.0

Open **File → Open project or library** and choose `examples/variation-studies.flame`. The final twelve studies each demonstrate one addition. Select its third transform, then open **Variations** to edit the weight and available parameters.

| Geometry | Angular and optical | Blur and sampling |
| --- | --- | --- |
| blob, PDJ, curl, rectangles | fan2, rings2, perspective, JuliaScope, ngon | Gaussian blur, radial blur, pie |

![Twelve rendered variation studies showing the differences between the new transformations](preview/starting-variations-0.11.0.png)

[Parameters, compatibility and numerical checks](docs/VARIATIONS.md)

## Earlier responsiveness changes in 0.10.2

- Mutate and Random cancel obsolete candidate and preview work when their windows close. Detail, thumbnail and main preview rendering reuse dedicated workers.
- Native project Save captures the preview seed along with the document state. A successful save updates the file identity and clean state without regenerating the same preview.
- Vulkan worker threads release their render contexts during normal execution. The same worker can reuse its device and rendering pipeline across jobs.

The [Windows freeze investigation](docs/WINDOWS_DIALOG_FREEZE.md) describes the reproduced waits and verification limits.

## Earlier responsiveness changes in 0.10.1

| Area | Change |
| --- | --- |
| Preview lifecycle | Handles stop/replacement/deletion during direct Qt callbacks, rejects stale completions, coalesces progress, and moves validation/fitting to the persistent worker. |
| Device selection | CPU-only settings do not enumerate Vulkan. Other modes discover devices in the background, cache results and offer an explicit refresh; closing the dialog does not join a blocked driver probe. |
| Recovery | The 45-second autosave captures a snapshot and performs thumbnail encoding, preparation, serialization and atomic writes on one worker. Pending saves coalesce, and changing documents invalidates obsolete work. |
| Render queue | Reuses one render worker across jobs, bounds progress delivery, and removes whole-queue saves from cosmetic progress updates while retaining durable state transitions and checkpoints. |
| Renderer | Accounts for CPU chaos-selection tables in the working-memory budget, skips repeated zero-light reconstruction work and reduces repeated-pixel output conversion. The output precision and PNG compression settings are retained. |

[Responsiveness and rendering review](docs/RESPONSIVENESS_REVIEW.md) explains the findings, reproductions, measurements and platform limits. The original intermittent freezes reported in 0.8/0.9 have not been conclusively tied to one cause on the user’s machine.

## Release tooling introduced in Milestone 10

| Area | Implemented behavior |
| --- | --- |
| Offline manual | **Help → User manual** (`F1`), embedded in the executable with section links and **Find in manual** (`Ctrl+F`, Next/Previous). Markdown and self-contained HTML are also shipped. |
| Diagnostics | **Help → System information…**, with Copy report and Save report; version/source/compiler/target and runtime Qt/OS information, requested preview settings and last actual renderer/device. The dialog does not probe GPU drivers or upload reports. |
| Build identity | `--version` and `--build-info --json`; configure-time Git state or hash-bound packaged source identity. Unknown source stays unknown; edited or missing tracked source is not labeled clean. |
| Git Bash tools | Dependency configuration checks, bounded build/test concurrency, explicit build/executable paths, launch logging, portable/source packaging and installer building. |
| Benchmarks | Repeated fresh-process CPU/Vulkan workloads, output hashes, actual backend/device identity, timing boundaries and per-child memory observations; optional separate 16K observation. |
| Release gates | Added build/startup/help regressions and six Python tooling suites; source/bundle manifests, runtime import audit, extracted-source rebuilding and distribution verification. Exact final results accompany the binaries. |

Inspect build identity and explicitly discover devices from the complete package:

```bash
./bin/windows-x64/flame-cpu.exe --version
./bin/windows-x64/flame-cpu.exe --build-info --json
./bin/windows-x64/flame-cpu.exe --list-devices --json
```

The build report describes configured capabilities; device discovery describes what the current machine can use. Requested Vulkan settings and a last-preview CPU fallback remain separate fields. The [user manual](docs/USER_MANUAL.md), [release handoff](docs/RELEASE_HANDOFF.md) and [benchmark guide](docs/BENCHMARKS.md) explain the complete workflow.

## Animation and color workflows

The timeline and extended color tools introduced in M9 remain part of this release. Original 7X weighted color curves retain their verified historical evaluation; the explicit continuous curve mode has separate semantics.

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

This actual Qt timeline screenshot is retained from M9. Release records distinguish automated offscreen workflows, native desktop interaction, the audited Windows cross-build and still-pending Windows/physical-GPU acceptance.

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

Sixteen-bit values come directly from reconstruction, without expansion from RGB8. The formats are tone-mapped integer images; JPEG, WebP, OpenEXR/HDR, ICC color management and dithering remain open output work. The [high-resolution guide](docs/HIGH_RES_RENDERING.md), [queue guide](docs/RENDER_QUEUE.md) and [format contract](docs/IMAGE_FORMATS.md) explain budgets, alpha, restart identity and storage behavior.

## Open the editor

Extract the complete ZIP and open `bin/windows-x64/apophysis-reborn.exe`. Keep the accompanying DLLs and `platforms` folder beside it. The program starts with Ember Spiral and works offline. Use **Open project or library** to load a native project, `.flame`, `.xml`, historical `.temp` collection or a supported Apophysis `.upr` entry.

Select a transform in the lower canvas, then drag its origin or its X/Y handles. The **Pre-affine / Post-affine** selector and the coefficient fields edit the same matrix. The right-hand tabs expose geometry, all 43 supported variation weights, their built-in parameters, color controls, and Xaos. **Save** retains the original collection and unrelated XML. The [editor guide](docs/EDITOR.md) covers the controls and recovery behavior.

## Choose the rendering device

Open **Edit → Preferences** to select **CPU (reference)**, **Automatic**, or **Vulkan (FP32 sampling)**. The same controls appear in **Render image**. A new settings profile defaults to Automatic with CPU fallback enabled; the CLI defaults to CPU. Creative previews inherit the chosen backend, device and budgets.

| Setting | Behavior |
| --- | --- |
| CPU | Uses the existing double-precision renderer and Reborn CPU random stream |
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
| Mutation | Candidate grid, strength/trend, independent geometry/variation/color/parameter/palette controls, parent comparison, saved ancestry and selected/batch/branch saving |

Read the [creative tools guide](docs/CREATIVE_TOOLS.md) for exact controls, save behavior and limits.

## Verify the Windows package

When ready, open Git Bash in the complete source/history edition's extracted `apophysis-reborn` directory and run the desktop verification. The lean runtime does not contain the full test suite.

```bash
bash scripts/verify-editor-windows.sh
```

It runs the CPU/core and Qt tests, records build/device identity and fallback, exercises offline help/search and diagnostics, creative dialogs, the render queue, scripting/plugin/project workflow and timeline/color/sequence workflow. It saves screenshots, images, projects and reports in separate `creative`, `support`, `queue`, `extensions` and `animation` subdirectories with a log. Add `--large-render` for the separate full 16K queue acceptance and `--no-open` to suppress Explorer. A GPU is not required.

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

The 43 studies in `examples/variation-studies.flame` cover every registered variation. Use `inspect` to list their names, then select a study by its one-based `--index`. `examples/seeded-random.flame` contains a nine-candidate generated batch; `examples/creative-branches.flame` contains an original, parent and child saved through the native editor. `examples/gpu-palette-study.flame` was edited and saved during the native Vulkan desktop check.

The CLI accepts the earlier seed, samples, threads and backend options plus `--size W H`, `--oversample`, `--filter-radius`, `--bit-depth`, `--alpha`, `--disk-mib`, `--strip-rows`, `--checkpoint`, `--resume` and `--estimate`. The foundation syntax `flame-cpu width height samples output.ppm` remains available.

```bash
./bin/windows-x64/flame-cpu.exe --flame examples/ember-spiral.flame \
  --output ember-4k.png --size 4096 3072 --samples 20000000 \
  --bit-depth 16 --memory-mib 256 --disk-mib 2048 \
  --checkpoint ember-render-work
```

Append `--estimate` to inspect the plan without creating files. For a controlled pause, append `--pause-after 1000000`; successful pause returns 75. Resume with the same settings and `--resume`, omitting `--pause-after`. Ctrl+C also preserves committed work when an explicit checkpoint was chosen. The [CLI guide](docs/HIGH_RES_RENDERING.md) covers all options and exit statuses. A successful export replaces an existing destination only after encoding completes.

## Build from source

CMake 3.20+, a matching C++20 compiler and Qt 6.5+ desktop kit, and Expat and zlib development files are required. The packaged GUI uses Qt 6.8.2. With `BUILD_TESTING=ON`, Qt Test and **Python 3.10+** are required for the full test/release gates; Python is not needed to run the application. Supply the installed Qt kit prefix when needed.

```bash
export APO_QT_PREFIX=/path/to/Qt/6.8.2/your-kit
bash scripts/check-deps.sh --no-open
bash scripts/build-editor.sh --build-dir build-release --jobs 2 --no-open
bash scripts/run-editor.sh --build-dir build-release --no-open
```

The dependency script checks configuration without compiling or downloading tools. Build wrappers default to two compiler/test jobs, retain `.txt` logs and support paths with spaces. Omit `--no-open` to select their completed logs in Explorer. `run-editor.sh --exe PATH -- PROJECT` selects an explicit executable and forwards project arguments.

A core-only build uses `bash scripts/build.sh --build-dir build-cpu --jobs 2 -DAPO_BUILD_EDITOR=OFF`; it still needs Expat/zlib and Python when tests are enabled. Add `-DAPO_ENABLE_VULKAN=OFF` to exclude Vulkan. An application-only CMake configuration can use `-DBUILD_TESTING=OFF` without Python; it does not execute release gates. Ordinary builds use the bundled Vulkan headers and embedded SPIR-V. The [Qt build guide](docs/QT_BUILD.md) records the Windows toolchain/staging workflow, and [RELEASE_HANDOFF.md](docs/RELEASE_HANDOFF.md) gives source, portable and installer packaging commands.

## Measure the renderer

From the complete package, choose the actual executable to measure:

```bash
bash scripts/benchmark.sh --exe ./bin/windows-x64/flame-cpu.exe \
  --backend cpu --threads 1,2,4 --repeats 3 --warmups 1 --no-open
```

`--backend both --device ID` adds strict Vulkan measurements on a discovered device. Software Vulkan must be explicitly selected and remains identified as software. Reports retain `report.txt`, `report.json`, exact commands, process logs and input/output hashes. Timings include process startup, rendering, encoding and disk publication; RSS is distinct from renderer-buffer budgets and VRAM. `--large-16k` adds one separate large observation, while `--keep-images` retains outputs. [Case definitions, timing/memory methods and limits](docs/BENCHMARKS.md)

## Verification in this checkpoint

The M10 configuration registers **48 CTest entries** with the editor and **36 core-only entries**. A Vulkan-disabled core build expects three explicitly skipped device-execution checks. These are inventory counts, not a claim that an unexecuted configuration passed. Exact final source/executable identities, pass/skip/failure results, sanitizer settings, measurements and audit totals are recorded in `BUILD.txt` and the packaged `verification` records, with methods in [TEST_PLAN.md](docs/TEST_PLAN.md). The source suites retain original-producer/curve references, animation and sequence recovery, 1,560 transform probes, streaming output and checkpoint gates.

The retained M7 acceptance record documents an actual 16K RGBA16 render that used 8,388,611 samples and peaked at **39.2 MiB process RSS** with a 128 MiB working-buffer budget. Two independent decoders agreed on every one of its 2,147,483,648 decoded channel bytes. A matching-camera comparison produced identical complete files using 63-row and 247-row strips, including queue pause/resume. The queue's whole Qt process peaked at 182.8 MiB; the 128 MiB setting limits rendering buffers, not all Qt/library/process memory. The [16K acceptance report](docs/HIGH_RES_ACCEPTANCE.md) distinguishes these measurements and documents the sparse sample density of the acceptance image.

## Included records

| File | What it covers |
| --- | --- |
| [User manual](docs/USER_MANUAL.md) | Getting started, editing/recovery, rendering, queues, animation, extensions and troubleshooting; embedded offline HTML is available with F1 |
| [Release compatibility](docs/RELEASE_COMPATIBILITY.md) | Traceability to all 25 brief sections and explicit partial/missing/platform-unverified requirements |
| [Requirements register](docs/requirements-status.json) | Stable requirement IDs, bounded status, source-brief fingerprint and evidence paths |
| [Release handoff](docs/RELEASE_HANDOFF.md) | Exact build/package commands, source identity, target acceptance and next engineering work |
| [Benchmarks](docs/BENCHMARKS.md) | Repeatable workloads, total-process timing, memory observations and hardware boundaries |
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

The complete source/history ZIP also includes `apophysis-reborn.bundle`, `BUILD.txt`, `release-source.json`, `SOURCE_SHA256SUMS.txt` and `SHA256SUMS.txt`. For a checkout with the complete checkpoint history, run `git clone apophysis-reborn.bundle ../apophysis-reborn-git` from the extracted folder. The checkout holds source, examples, tests and documentation. Windows applications and runtime files are distribution outputs; corresponding third-party sources accompany the package. No remote repository is required.

## Remaining work

The original brief names 40 starting variations. All 40 are implemented, alongside three additional built-ins for 43 total. That list is not the complete historical inventory. Active density estimation, broader filters/3D/direct-color behavior, JPEG/WebP/OpenEXR/HDR, ICC/embedded metadata, the full legacy script/animation surface, light theme and more workspace/preferences controls remain open. Native plugins are trusted in-process CPU code; local crash reporting and stronger isolation remain incomplete. CUDA is optional and not implemented.

Continue with targeted compatibility work and Windows/physical-GPU acceptance using [RELEASE_COMPATIBILITY.md](docs/RELEASE_COMPATIBILITY.md), [FEATURE_MATRIX.md](docs/FEATURE_MATRIX.md) and [RELEASE_HANDOFF.md](docs/RELEASE_HANDOFF.md). M10 packaging does not turn those outstanding requirements into completed product functionality.
