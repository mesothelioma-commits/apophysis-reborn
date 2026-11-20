# Apophysis Reborn 0.17.0 — User manual

Apophysis Reborn is a native fractal-flame editor with a CPU renderer, Vulkan sampling, source-preserving flame libraries, creative tools, persistent render queues and a keyframe timeline. It works offline. This release is a development and maintenance checkpoint: the implemented controls are usable, while complete original Apophysis 2.x, 7X and AV feature parity remains unfinished. The [release compatibility report](RELEASE_COMPATIBILITY.md) lists the implemented subset and the remaining requirements.

## Contents

- [Getting started](#getting-started)
- [Your first image](#your-first-image)
- [Workspace and preview](#workspace-and-preview)
- [Favorites and variation explorer](#favorites-and-variation-explorer)
- [Saving and recovery](#saving-and-recovery)
- [Transform editing](#transform-editing)
- [Gradients, adjustments and creative tools](#gradients-adjustments-and-creative-tools)
- [Rendering an image](#rendering-an-image)
- [Proof a crop and reopen an editable PNG](#proof-and-editable-png)
- [CPU and Vulkan](#cpu-and-vulkan)
- [Queue and checkpoint recovery](#queue-and-checkpoint-recovery)
- [Animation and morphing](#animation-and-morphing)
- [Property keys and layered export](#property-keys-and-layered-export)
- [Local AI connection](#local-ai-connection)
- [Scripts and native plugins](#scripts-and-native-plugins)
- [Command-line workflows](#command-line-workflows)
- [Diagnostics, benchmarks and verification](#diagnostics-benchmarks-and-verification)
- [Shortcuts and troubleshooting](#shortcuts-and-troubleshooting)

## Getting started

The primary target is Windows 11 x64. Choose one of these distributions:

| Package | What to open | Intended use |
| --- | --- | --- |
| Windows portable ZIP | Extract the complete ZIP, then open `apophysis-reborn.exe` in its top-level application folder. | Run without an installer. Keep the DLLs, Qt plugin folders, documents and examples together. |
| Windows setup EXE | Run the supplied setup EXE. | Install for the current Windows user with versioned shortcuts and an uninstall entry. |
| Complete source/history ZIP | Use the included Git bundle to restore a checkout, or build the extracted source. | Development, complete verification tools, source history and release evidence. |

Run the 0.17.0 Windows x64 setup from your usual Windows account. Close the editor, render processes and local AI companion first. Setup installs 0.17.0, then removes all older versions registered to that account when upgrading. Portable copies are independent. If any older installation is locked, setup identifies it in the installation log so you can close the program and retry removal from Installed apps. The uninstaller removes the recorded, unchanged installed payload and retains modified or extra files. User settings, recovery data and project files outside that directory are retained. An interrupted install can leave a partial program folder: choose a different empty destination or inspect the partial payload before removing it. Setup does not automatically repair or replace a nonempty installation.

Portable operation describes the executable distribution, not a separate settings profile: normal sessions still use the application's per-user settings and data locations. Different installed versions can share those locations. Keep your artwork in a working folder outside the program directory. To continue an existing render checkpoint, retain the original renderer build and exact native plugins; an upgrade is not a promise of checkpoint compatibility.

The editor runs offline. MP4 export requires FFmpeg with the libx264 encoder on PATH. Vulkan requires a suitable installed system loader/driver; the CPU path works without it. The actual Windows runtime, installer lifecycle and physical GPU acceptance status are recorded in [TEST_PLAN.md](TEST_PLAN.md), separately from cross-compilation and dependency audits.

## Your first image

1. Start the application. With no filename supplied, it opens eight editable starters in the Flame library. Select a thumbnail to preview its flame. Use File → Open starter library to reopen the collection, and File → Save as… to save your changes.
2. Use **File → Open project or library…** (`Ctrl+O`) to open your own file or an example from the `examples` folder. Select an entry in **Flame library** and wait for its preview.
3. Open **Gradient editor** (`Ctrl+G`), select a preset and choose **Use preset**. Choose **Apply gradient** to keep the change, or **Cancel** to leave the flame unchanged.
4. Use **File → Save as…** to save your work as `.apoproject`. This retains the flame collection and native project settings.
5. Choose **File → Render image** (`Ctrl+R`). Select a destination, dimensions, sample budget and format, then choose **Add and start queue**. The preview and final image have separate sampling settings.
6. Read the finished job's actual backend, samples and destination in **Render queue**. The final file appears only after encoding completes successfully.

Useful starting files include `examples/ember-spiral.flame`, `examples/variation-studies.flame`, `examples/color-curve-studies.flame`, and the four `examples/animation-*.apoproject` studies. An original file with unsupported operations may be preserved without being renderable; its diagnostic identifies a blocker instead of silently replacing the mathematics.

## Workspace and preview

The **Flame library** panel searches entry names. In narrow panels the name wraps beneath the thumbnail. The central preview displays the selected flame. Transform controls, output/camera fields and the Activity log occupy resizable docks. Drag dock titles to arrange them; **View** toggles panels and **Reset workspace** restores the default arrangement. **F11** toggles full screen. Window geometry and panel layout are saved in normal sessions. The current theme is dark; a light-theme selector and named/classic workspace profiles are not supplied.

The preview runs in the background. A quick pass refines toward the selected final sample limit. **Draft**, **Standard** and **Fine** presets trade speed for sample quality; a custom limit can be set in **Edit → Preferences**. Editing replaces stale work, so an older selection cannot overwrite the latest preview.

Drag the vertical divider beside Flame library or Flame and transform to resize Live Preview from that side. Fit preserves the proportions of the output. When the image already fills the available height, drag the horizontal divider above Transform editor down to make the artwork larger.

Use the mouse wheel to magnify the displayed preview and drag to pan. **Fit** shows the full image; **1:1** shows each preview pixel at one screen pixel. These are display controls. Change **Output and camera** or **Adjustment editor → Camera** to change the saved composition. Interactive preview uses viewport-sized output and oversampling one; it does not establish the quality of a large export. The preview is composited against the flame background; there is no transparency-checkerboard mode.

**Pause preview** stops preview work. Release it to render the latest state, or use **Restart**. The status text reports samples, elapsed time and the actual CPU/Vulkan path. Its tooltip and the Activity panel include device and fallback details. A requested Vulkan renderer may have resolved to CPU when fallback is permitted.

## Favorites and variation explorer

1. Select **Ember spiral** in the Flame library. Click the star in that dock or choose **Flame → Save to Favorites…**. Add a tag such as `spirals` and save.
2. Open a different project. Click **Favorites…**, search for your tag or the flame name, select the result and add it to the current library. The favorite is an independent snapshot and can still be used after the original file moves. Save your project to keep the inserted copy.
3. Select the copy’s first transform. Open **Flame → Variation explorer…** or the explorer button in the **Variations** panel. Choose **swirl** and explore its weight. Compare the labeled tiles. Each uses the same camera and seed, so you can see the effect of changing one value.
4. Apply the value you like, or close the explorer to discard the preview. **Ctrl+Z** undoes an applied edit. Save the result as another favorite to keep both versions.

## Saving and recovery

### Choose the right file operation

| Operation | Retained data |
| --- | --- |
| **Save** a legacy library | Original collection bytes when unchanged; targeted edits preserve unrelated entries, metadata, comments and encoding. |
| **Save as `.apoproject`** | The exact collection plus supported native project metadata, selection, preview/workspace/render settings, presets, plugin references and animation. |
| **Export selected flame** | A canonical single supported flame and embedded palette. It omits collection comments, history and unrelated source data. |
| **Export flame library** | A separate exact legacy collection copy; the native project and timeline remain open. The exported collection has no timeline. |
| **Export Apophysis UPR** | Only models representable by the documented Apophysis UPR profile; incompatible features produce a diagnostic. |

A timeline must be saved in `.apoproject`. Normal Save prompts for a native filename when needed, and a plain-library Save cannot silently discard an animation payload. Unknown project sections and future animation data are retained; unsupported animation data are not silently converted by the timeline editor.

When the current file changes outside the editor after opening, Save offers **Save a copy**, **Replace disk version**, or **Cancel**. An atomic save stages the new file and replaces its destination only after writing succeeds. Choose a new filename when retaining both versions of an artwork.

**Undo** and **Redo** restore document snapshots, including source bytes and selection. Creative dialogs and Timeline work privately until Apply. Their Cancel buttons discard pending edits. Document Undo does not delete already exported images or jobs already admitted to the render queue. Project preferences can be captured separately from ordinary flame-content undo; save the project after changing settings you wish to retain.

### Recover an interrupted editing session

Every 45 seconds, modified documents request a background recovery save. Thumbnail preparation, serialization and disk writes run off the interface thread; a pending request is replaced by the newest snapshot. Recovery completes when that worker finishes. On a later launch, a stale recovery copy offers **Recover edits**, **Discard recovery copy**, or **Later**. Recovery opens an unsaved document so you can inspect it and choose where to save. Copies owned by another live editor are skipped. Native recovery retains the project envelope and timeline.

Recovery, the automatic queue and the rotating Activity log use the application's per-user local data directory. Recovery is not a replacement for a backup of your artwork folder. Its interval is currently fixed, and uninstalling a version does not intentionally clear those user-data locations. Queue checkpoints are separate from editing recovery files.

Open “Tidal filaments” in examples/density-study.flame to try density estimation. In Adjust → Rendering, use Enable density estimation, Maximum radius, Minimum radius and Estimator curve to compare the result with the same seed. It spreads sparse samples over a wider area and keeps dense detail sharper. The offline file docs/DENSITY_ESTIMATION.md describes the supported historical settings.

## Transform editing

Select a transform from the selector or its triangle. Ordinary triangles are color-coded; a final transform has a dashed gold outline. The origin handle is the translation, and X/Y handles are the transformed basis vectors.

| Action | Control |
| --- | --- |
| Move a triangle | Drag its origin handle or interior. |
| Change its basis | Drag X or Y; this permits rotation, scale and shear. |
| Pan / zoom the grid | Middle-button drag / mouse wheel. |
| Cancel the current drag | Press `Esc`. |
| Snap coordinates | Enable **Snap** and choose grid spacing. |
| Inspect without changing the selected triangle | Enable the temporary **Lock**. Selection changes clear that lock. |

The **Pre-affine / Post-affine** selector controls both the triangle and numeric matrix fields. In stored coefficient order, the transform is `x′ = a·x + c·y + e` and `y′ = b·x + d·y + f`. Numeric edits and geometry changes update the same matrix. Enter a finite number and press Enter or leave the field to commit it. **Rotate**, **Scale**, **Move**, **Flip X/Y**, world/local coordinates and pivot fields provide precise geometry operations.

Use **Transform** to add, duplicate, delete, reorder, copy/paste or add a final transform. Structural changes preserve valid Xaos destination mapping. At least one ordinary transform remains. The final transform applies to the plotting copy and has no ordinary selection weight or Xaos row.

**Variations** supports 45 built-ins, including splits and elliptic. Search by name and edit signed weights; zero disables that contribution. Selecting a parameterized variation exposes its own controls, including JuliaN and JuliaScope power and distance. **Plugin variations…** supplies fields declared by an explicitly enabled module. Unknown variations remain blockers rather than being replaced with linear or another formula.

**Color** changes palette coordinate, color speed and opacity. **Xaos** edits transition multipliers from the selected transform to its destinations. A positive-weight branch needs a positive reachable destination. Producer-specific field interpretation remains attached to the flame. Copying a transform between producer families can change how its values are interpreted.

Multi-selection, persistent per-transform lock profiles and general transform-synchronization utilities remain open. [EDITOR.md](EDITOR.md) and [COMPATIBILITY.md](COMPATIBILITY.md) define the complete current control and source-preservation rules.

## Gradients, adjustments and creative tools

**Gradient editor** changes the selected flame's 256 stored palette colors. Edit an individual slot through RGB, HSV/HSL or the color picker; build a gradient from stops; rotate, reverse, smooth or randomize the palette. Stop-building supports RGB, linear-light RGB and shortest-hue HSV/HSL choices. **Render interpolation** separately chooses step/indexed or interpolation between stored palette entries.

The palette library includes built-in presets and personal GPL palettes saved with **Save to library…**. Import/export supports GPL, supported UGR/MAP and palettes from supported flame files. An imported palette does not import that flame's geometry. Broader original palette catalogs and a full library folder/tag manager are not implemented.

**Adjustment editor** groups Camera, Tone/color and Rendering settings. It controls output size/aspect, camera center/scale/zoom/rotation, orbit framing, gamma/threshold/brightness/vibrancy, background, density, oversampling and the supported filter radius. Increasing zoom by one doubles camera scale. **Frame visible orbit** estimates composition from a finite orbit sample; refine it numerically if distant structures should be included.

### Tone curves and palette curves

**Adjustment editor → Tone / color → Tone curves…** edits master/R/G/B reconstruction curves. **Gradient editor → Color adjustments → Palette curves…** applies curves to the working palette colors. These affect different stages and can produce different images even with the same control points.

Drag control points or enter exact X/Y/weight values. **Original 7X lookup** preserves the documented historical parameter-Y evaluation, point-only identity bypass and integer lookup. **Continuous transfer** uses ordered X and a composed transfer table. Changing modes deliberately changes the meaning of the curve; it is not a visual-equivalence conversion. A master curve is applied before each RGB curve. Defaults bypass curve math and retain previous image output.

Apply in the curve dialog returns to its containing editor; the outer **Apply adjustments** or **Apply gradient** commits the document change. Cancelling the outer dialog also discards accepted inner-dialog changes. See [COLOR_TOOLS.md](COLOR_TOOLS.md) for exact modes, presets, bounds and the original-source comparison.

### Generate and mutate

**Random flame generator** produces 1–24 candidates with a seed, transform/geometry constraints, allowed variations, symmetry and optional random palettes. Select a tile to refine its preview. **Add selected** or **Add batch** appends the results to the library as an undoable change.

**Mutation explorer** starts from the selected parent. Choose strength, trend, mutation groups and seed behavior, then regenerate. Zero strength retains the parent parameters. Parent comparison and saved ancestry help keep a useful branch. The algorithms are reproducible Reborn workflows; they do not reproduce historical Apophysis random sequences. Save resulting flame definitions when preserving an exact creative checkpoint. [CREATIVE_TOOLS.md](CREATIVE_TOOLS.md) describes each control.

## Rendering an image

Open **File → Render image**. The dialog makes an export copy; changing its size does not alter the open flame. Its camera is fitted using the smaller width/height scale ratio. Choose a standard preset or custom dimensions from 1 to 16,384 per side, and use aspect lock when appropriate.

| Setting | Practical meaning |
| --- | --- |
| Sample density / automatic samples | Derives the sample target from density, dimensions and zoom. Clear automatic samples to enter an exact positive count. |
| Oversampling / filter | Increase reconstruction detail and the working-buffer requirement. Box/source-default and supported Gaussian filtering have documented producer behavior. |
| System buffer budget | A limit on planned rendering buffers, not total application, operating-system or driver memory. |
| GPU budget | A separate limit on charged Vulkan allocations. Reported device heap size is not measured free VRAM. |
| Working disk budget | A conservative bound for sampled-hit chunks, indexes, output and metadata. Check actual free space too. |
| Seed | An unsigned 64-bit value retained exactly in saved jobs. |
| Project preset | Saves/restores render settings in a native project; it does not itself add or start a job. |

The estimate reports geometry, sample count, buffer/storage allowances and reconstruction strips. It does not invent a finish time before performance has been observed. A 16K image with few samples can be sparse or noisy; larger dimensions do not automatically increase artistic quality. High oversampling or wide filters may require more memory even when the output itself streams.

### Export capabilities

| Format | Channels and depth |
| --- | --- |
| PNG | RGB or straight RGBA, 8 or 16 bits per channel. |
| TIFF | RGB or straight RGBA, 8 or 16 bits per channel; uncompressed classic TIFF. |
| PPM | RGB, 8 or 16 bits per channel. |
| BMP | RGB, 8 bits per channel. |

Sixteen-bit output is quantized directly from reconstructed values, not expanded RGB8. Alpha is straight/unassociated; transparent pixels have zero RGB. The output is tone-mapped integer color, with no invented ICC/HDR profile. PNG exports can optionally carry an editable flame. Save an `.apoproject` or flame file separately to retain the full project or library. [IMAGE_FORMATS.md](IMAGE_FORMATS.md) gives the exact encoder contract.

**Add to queue** leaves a stopped queue stopped. **Add and start queue** starts work in queue order. A completed output replaces its destination only after encoding succeeds; cancellation or failure retains the previous destination. Ordinary image exports, scripts and animation frames have their documented conflict rules, so review the destination before starting.

<a name="proof-and-editable-png"></a>

## Proof a crop and reopen an editable PNG

Start with **Ember spiral**. Open **Render image…**, choose the final dimensions, seed, sample count and output format, then follow these steps.

1. Click **Proof a crop…**. Set the crop width and height, then use **Center crop** or enter X and Y measured from the top-left corner. Click **Render crop**. Each proof pixel uses the final render’s camera, density settings, filters and sample budget. Scroll to examine larger crops at actual pixel size.
2. Adjust **Brightness**, **Gamma** or **Vibrancy**. These changes reuse the finished crop’s samples. Use the transparency checkbox to inspect edges against a checkerboard. Changing the crop requires a new sampling pass. A small crop can still take time when the full output has a high sample count.
3. Click **Use tone for render** to apply those three values to the pending output. Closing the proof discards its tone changes. Set **Transparent background** in the outer Render image window to export alpha. The checkerboard only appears in the proof.
4. Choose **PNG** (8 or 16 bit), enable **Include editable flame in PNG**, select a destination and choose **Add and start queue**. Embedding is optional for each job. Use an ordinary PNG for a picture without editable parameters.
5. Open the exported PNG with **File → Open** or drop it on the editor. The result is a new, unsaved project containing one flame. Choose **Save As** to keep developing it. Reborn restores the rendered flame, seed and sample count. Keep your original project for its other flames, timeline and scripts.

Image editors and sharing websites can strip PNG text chunks. Share the original exported file when the recipient needs to edit it. A flame that uses a third-party variation requires the corresponding trusted plug-in on the recipient’s computer.

## CPU and Vulkan

**Edit → Preferences** sets preview workers, final sample limit, seed, host budget, requested renderer, device, GPU budget and fallback policy. Render dialogs retain separate export choices. Creative previews inherit the renderer settings with smaller preview budgets.

CPU-only controls do not load Vulkan for device discovery. Automatic and Vulkan modes check devices in the background; you can continue using or close the dialog while discovery runs. The result is cached. Use **Refresh devices** to request a new inventory.

The CPU path uses double-precision orbit calculations and the established CPU random stream. Vulkan samples with FP32 and a separate portable random stream, then uses CPU reconstruction. The same seed does not promise identical pixels between CPU and Vulkan, different drivers or different builds. Numerical tests describe the admitted comparison envelope; unsupported FP32 inputs may still be valid on CPU.

**Automatic** prefers a suitable physical Vulkan device and can fall back to CPU. Software Vulkan is labeled and is not automatically chosen as a physical GPU. Strict Vulkan disables fallback and reports unsupported models or unavailable devices. Native variation plugins have CPU implementations only.

Read the **actual** backend/device and fallback reason after rendering. A successful image from Automatic is not proof that a GPU ran. Device discovery is available through the CLI, and the optional strict Windows GPU verification uses one selected device consistently. [GPU_RENDERING.md](GPU_RENDERING.md) and [WINDOWS_GPU.md](WINDOWS_GPU.md) explain selection and acceptance.

## Queue and checkpoint recovery

One background worker runs one queued job at a time; CPU workers operate within that job. Each entry keeps an immutable flame snapshot, settings and plugin ownership, so later edits do not change already admitted work.

| Queue action | Effect |
| --- | --- |
| **Start / resume** | Processes queued/paused jobs in order and resumes valid committed samples. |
| **Pause queue** | Requests a safe stop. Wait for the final paused/checkpoint state. |
| **Cancel job** | Stops that job; other jobs may continue while the queue remains running. |
| **Retry / resume job** | Requeues a stopped/failed job, using a valid checkpoint when available. |
| **Start selected job over** | Uses a new checkpoint and samples from the beginning. |
| **Move up/down** | Reorders jobs that are not running. |
| **Remove** | Removes the list entry; it does not delete images or checkpoint directories. |
| **Save / Restore queue** | Saves or validates/restores the job list. Restore requires stopped work and never starts it automatically. |

The progress bar allocates work to sampling and reconstruction; it is not a guaranteed wall-clock percentage. **Phase ETA** uses measured progress within the current phase and restarts after a phase change or resume. Elapsed time excludes pauses.

Normal sessions save the automatic queue in the per-user data directory. A lock prevents two sessions from silently overwriting the same list. On startup, interrupted running jobs become paused. A corrupt/incompatible automatic queue is retained and reported rather than overwritten with an empty list.

Committed checkpoint chunks are checksummed and identity-bound. Only referenced complete chunks resume. Stopping during reconstruction can require rebuilding the image from strip one while retaining the expensive sampling work. A queue JSON references its checkpoint folders; it does not contain their sampled data. Keep those folders and the original compatible renderer/plugin binaries when retaining an unfinished job.

Checkpoint compatibility is stricter than project-file compatibility. A different build, device/driver/shader identity after GPU sampling, changed plugin binary or changed model/settings may require a fresh job. Do not edit manifests to bypass those checks. Completed, removed and superseded Qt queue checkpoint directories are retained for deliberate cleanup. No automatic operating-system shutdown is implemented. [RENDER_QUEUE.md](RENDER_QUEUE.md) describes the recovery states and storage policy.

## Animation and morphing

Open **Tools → Animation timeline** (`Ctrl+T`). Timeline owns a private working clip. **Apply timeline** stores it as an undoable project change; Cancel discards it. Local Undo/Redo navigates its bounded edit history.

Frame numbers are zero-based. A 121-frame clip contains frames 0 through 120; its exported duration at 30 fps is 121/30 seconds. The final frame begins at 120/30 seconds. The timing tab accepts rational rates such as 30000/1001. Duration changes the frame count; it does not silently retime keyframes outside a shortened clip.

1. Set the total frames and frame rate, or choose Still, Camera orbit, Zoom or Palette cycle and **Apply preset**.
2. Move the playhead. **Capture editor flame** stores the current selected editor flame there; **Replace selected** replaces a selected keyframe. **Add at frame** captures a library entry, and Import reads another collection/project.
3. Edit keyframe frame/name/outgoing transition in the table. Frame zero must exist; positions must be unique and within the clip. Hold makes an explicit cut; Linear, Smooth and Smoother interpolate.
4. Use **Gradient** or **Adjustments** for private keyframe changes. **Use frame in editor** loads the evaluated playhead for normal editing and can be undone.
5. Scrub or play to review intermediate frames, then Apply and save `.apoproject`.

Playback advances after the current preview finishes; expensive frames can play slower than the chosen rate. Stored frame timing governs exported sequences, not the interactive preview's speed.

**Morph to library selection** makes an explicit positional pairing. **Correspondence** lets you assign stable transform IDs. Strict matching rejects different ID sets; **Fade unmatched transforms** fades branches through zero selection weight and remaps their Xaos edges. This policy does not guarantee an attractive or stable morph between arbitrary flames. Review the middle of a segment as well as its endpoints.

Continuous camera, affine, real-parameter, palette and curve values interpolate under the chosen policies. Integer/discrete values hold until the next exact keyframe. Use compatible curve modes and native modules across a continuous segment, or an explicit Hold cut where appropriate. **Import timed library** asks whether legacy `time` means frames or seconds; an omitted `time` retains the legacy zero default, and conflicting positions are rejected.

On **Image sequence**, choose an existing output folder, prefix, inclusive first/last frames, step, fixed canvas, format, samples and budgets. **Apply & queue frames** validates the whole selected batch. A stopped queue waits for Start; an already running queue continues into new jobs. Filenames retain original global frame numbers. A batch and the queue are bounded to 1,000 selected/jobs respectively; use explicit subranges for longer clips.

Frame destinations that appear or change after admission are refused immediately before publication. Queue batches budget retained checkpoints for all jobs. The separate CLI sequence workflow removes completed frame checkpoints and keeps its own manifest; do not use a CLI manifest as a Qt queue file. The desktop composer can combine these image sequences or saved animated projects. [ANIMATION_EDITOR.md](ANIMATION_EDITOR.md), [ANIMATION_CLI.md](ANIMATION_CLI.md) and [MORPHING.md](MORPHING.md) give the exact contracts.

## Property keys and layered export

In Tools → Animation timeline… → Property keys, move the playhead, choose zoom, rotation, brightness, gamma or vibrancy, enter a value and choose Set key at playhead. Repeat at another frame. Choose Hold, Linear, Smooth or Smoother for the transition leaving each key. The track holds its endpoint values outside the keyed range. Rotation uses unwrapped degrees, so 0 to 720 makes two turns. Apply the timeline and save the project.

In Tools → Compose image sequences…, add saved animated projects or select the first numbered PNG of each layer, such as frame_000000.png. Drag the bottom layer to the top of the list and place overlays below it in drawing order. Layers need matching frame numbers and canvas dimensions. Use transparency where the lower layers should show through. Each project keeps its own palette, transforms and timeline.

Choose Export PNG frames… for transparent composite frames, or Export MP4… for H.264 video. MP4 export uses FFmpeg from PATH with the libx264 encoder. For 23.976 fps, enter 24000 and 1001 in the playback rate fields. Choose a new destination. MP4 fills transparent regions with black and pads odd dimensions to even values. The composer accepts up to 1,000 frames and uses temporary disk space for rendered layers and output.

## Local AI connection

Open **Tools → Local AI connection…**, copy the configuration into a local stdio-capable MCP client, and choose **Enable for this session**. Close the dialog. Ask the assistant to inspect the selected flame and show its preview. If it proposes Lua, click **Run** in the review, preview the results, then use **Apply result** or close to discard. **Undo** reverses the applied edit. Disable sharing from the same Tools dialog or close the editor. The Windows companion is **apophysis-mcp.exe** beside the editor. Your AI client controls whether shared artwork goes to a model provider. [Setup, example prompt and troubleshooting](LOCAL_AI.md)

## Scripts and native plugins

### Script workflow

Open **Tools → Script editor…** (`Ctrl+E`), choose Lua or the supported legacy Apophysis subset, and load a bundled example. **Run** (`Ctrl+Return`) evaluates a private document. Review console messages and deferred outputs, then use **Apply result**. A failed or stopped result cannot be applied. The editor includes highlighting, `Ctrl+Space` API completion, reference text, examples and source open/save.

Exports and queued renders begin disabled. Enable the required capability, choose an existing output folder and add explicitly supplied input files. Scripts cannot directly access the filesystem, network, processes or native module loader. The host preflights protected inputs/state and output collisions. Lua allocation, instruction and time limits bound script execution, not total application/render memory.

Applying a successful script creates one document undo step and submits approved outputs. Document Undo does not remove exported files or admitted jobs. Exports are individually atomic; an I/O failure later in a group can leave earlier completed outputs. Modern Lua indices are generally one-based; the historical subset uses original zero-based indices. Do not assume every old `.asc` script is supported. Dedicated timeline/curve-editing script APIs are not supplied; the animation CLI is a separate interface. See [SCRIPTING.md](SCRIPTING.md) and [LEGACY_SCRIPTING.md](LEGACY_SCRIPTING.md).

### Native plugin workflow

Open **Tools → Plugins…**. Add a search folder and refresh to inspect manifests without executing the libraries. **Install…** copies a manifest and adjacent native library; it does not enable them. **Enable… → Trust and enable** explicitly approves the displayed binary fingerprint.

An enabled native plugin runs code inside the application with the process's operating-system permissions. Descriptor/callback validation does not isolate a crash or hang. Use trusted, compatible x64 modules; old 32-bit Apophysis DLLs cannot load directly. To build your own variation, follow the [Windows walkthrough](PLUGINS.md#build-and-distribute-your-own-variation). The source archive and offline guides ZIP include the C example and `src/plugin_api.h`.

Startup approvals bind the principal library's SHA-256. Replacing that binary requires fresh approval. Disable prevents new parsing/startup loading, but existing immutable jobs retain their module owners. The manager refuses file removal while a document or job still owns the module. Project references and restored queues never enable code by themselves. A changed module can require new animation captures and render checkpoints. [PLUGINS.md](PLUGINS.md) records the precise ABI and trust boundary.

## Command-line workflows

The examples below run in Git Bash from the extracted **complete source/history** package. For the lean portable application folder, set `cli="./flame-cpu.exe"` instead. Native builds normally use `./build/flame-cpu`.

```bash
cli="./bin/windows-x64/flame-cpu.exe"
"$cli" --version
"$cli" --build-info --json
"$cli" inspect examples/ember-spiral.flame --json
"$cli" --list-devices --json
```

Create a working output directory and render a modest first image:

```bash
mkdir -p out/manual-study
"$cli" --flame examples/ember-spiral.flame \
  --output out/manual-study/ember.png --size 2048 1536 \
  --samples 2000000 --seed 48 --threads 2 \
  --memory-mib 256 --disk-mib 1024 --bit-depth 16
```

Add `--estimate` to validate resource planning without writing an output. Ordinary image commands use `--size W H`; animation commands use `--width W --height H`.

```bash
"$cli" animation create examples/ember-spiral.flame \
  --preset camera-orbit --frames 61 --fps 30 \
  --output out/manual-study/orbit.apoproject
mkdir -p out/manual-study/orbit-frames
"$cli" animation render out/manual-study/orbit.apoproject \
  --output-dir out/manual-study/orbit-frames \
  --first 0 --last 60 --step 15 --width 640 --height 480 \
  --samples 250000 --format png16 --memory-mib 128 --disk-mib 512
```

After an interrupted CLI sequence, repeat the same render command with `--resume`. Completed frames are reused only when their stored size and SHA-256 match. Preserve its source clip, renderer binary, settings, state directory and required plugin binaries. A new renderer build requires new sequence state. The full [CLI guide](ANIMATION_CLI.md) explains controlled pauses, single-frame exports, morphs, timing import and conflict rules.

## Diagnostics, benchmarks and verification

**Help → User manual** (`F1`) opens this guide offline. Inside the manual, use `Ctrl+F` to focus **Find in manual…**, then **Next** or **Previous** to move between matches. The contents links jump to sections without opening another page.

**Help → System information…** shows build/source identity, compiler/target, Qt/runtime platform and logical CPU information, requested preview settings, and the last preview's actual backend/device/fallback. **Copy report** copies the text; **Save report…** writes a local report you choose. Opening this dialog does not probe or load GPU drivers and does not include source flame contents or environment-variable dumps.

Use `flame-cpu --list-devices --json` for active Vulkan discovery. It is separate from the static build report (`--build-info --json`). No diagnostic report is sent automatically. When reporting an issue, retain the exact version/source identity, error text, operation and relevant reproduction file; inspect a file before choosing to share it.

From the complete package, the Windows verification scripts write logs/artifacts and can open Explorer. `--no-open` suppresses that final convenience action:

```bash
bash scripts/verify-windows.sh --no-open
bash scripts/verify-editor-windows.sh --no-open
```

The CPU/editor checks do not establish GPU execution. Choose an actual device from discovery before running the separate strict GPU gate:

```bash
bash scripts/verify-gpu-windows.sh --device 0 --no-open
```

Replace `0` with your intended device ID/index. Missing devices, skipped computation or CPU fallback fail that strict gate. Actual full-resolution acceptance is optional because it requires substantial storage:

```bash
bash scripts/verify-editor-windows.sh --large-render --no-open
```

This large workflow tests full 16K queue rendering, pause/resume, output integrity and UI heartbeats; it is not a final-artwork sample-density recommendation. The release retains earlier M7 full-image measurements as historical evidence. Read the report generated by your current run for current hardware/build results.

The benchmark runner records repeated fresh-process timings, warmups, output identities and actual backend/device information:

```bash
bash scripts/benchmark.sh --exe ./bin/windows-x64/flame-cpu.exe \
  --backend cpu --threads 1,2,4 --repeats 3 --warmups 1 --no-open
```

Vulkan benchmarking uses `--backend vulkan` or `--backend both`; choose `--device` explicitly for a software device. `--large-16k` adds a separate large case. Reports measure the actual host and settings, not an advertised speedup for every GPU. [BENCHMARKS.md](BENCHMARKS.md) defines the case sizes, timing/RSS scope and report fields. [RELEASE_HANDOFF.md](RELEASE_HANDOFF.md) covers dependencies, building and acceptance evidence.

## Shortcuts and troubleshooting

| Shortcut | Action |
| --- | --- |
| `Ctrl+O` / `Ctrl+S` / `Ctrl+Shift+S` | Open / Save / Save as. |
| `Ctrl+Z` / `Ctrl+Y` | Undo / Redo. |
| `Ctrl+D` | Duplicate selected transform. |
| `Ctrl+G` / `Ctrl+J` | Gradient / Adjustment editor. |
| `Ctrl+M` / `Ctrl+Shift+N` | Mutation / Random generator. |
| `Ctrl+E` / `Ctrl+T` | Script editor / Animation timeline. |
| `Ctrl+R` | Render image. |
| `F1` / `F11` | User manual / Full screen. |

| Symptom | Next step |
| --- | --- |
| A flame opens but will not preview | Read its render blocker. Preserve the original file; inspect unsupported variations, 3D/direct-color/density settings or missing plugins. GPU selection does not implement missing features. |
| Automatic renders on CPU | Read the actual fallback reason. Check selected device/model capability and budgets; use a strict GPU check when confirming Vulkan execution. |
| High-resolution plan is rejected | Inspect the host and disk estimate; reduce oversampling/filter radius, select a smaller range or increase the relevant budget/free space. Do not confuse the buffer budget with whole-process RSS. |
| A completed large image looks noisy | Increase sampling density/count. The output dimensions and 16-bit depth do not supply missing samples. |
| A restored queue does not start | Restoration is intentionally stopped. Enable the exact required trusted plugin, retain checkpoint folders and choose Start/resume after reviewing the list. |
| Resume reports an incompatible identity | Use the original compatible build/device/plugin/settings or start a fresh job. Editing a manifest to force acceptance can invalidate its sampled data. |
| An animation frame conflicts with an external file | Preserve/review that file, select another output location, or deliberately re-admit replacement. The old batch cannot silently accept changed destination bytes. |
| An old script is rejected | Read the preflight message and supported-language/API guide. Use the tested subset or modern Lua; renaming the extension does not convert syntax. |
| A plugin will not enable after replacement | Inspect its new version/fingerprint and explicitly approve it if trusted. Old queue/animation identities may require recapture or new renders. |
| Setup rejects the destination | Choose a fresh empty version directory. Setup does not merge files into an unknown nonempty folder. |
| A panel is missing | Enable it from View or use Reset workspace. Full Windows multi-monitor/DPI acceptance remains a separate test. |

For exact capabilities, use [RELEASE_COMPATIBILITY.md](RELEASE_COMPATIBILITY.md). Advanced guides are ordinary files in `docs`; this embedded HTML guide is self-contained and does not fetch them or any internet assets.
