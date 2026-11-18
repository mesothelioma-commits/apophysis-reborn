# Native Editor guide — 0.9.0

Extract the complete portable package and open `bin/windows-x64/apophysis-reborn.exe`. The editor uses the same CPU/Vulkan rendering paths, document model, scripting host and plugin registry as the command-line tools. It starts with Ember Spiral when a file has not been supplied. M9 adds the native animation timeline, morphing, queued frame sequences and extended color curves to M8's projects, scripting, trusted native variations and historical palette/UPR exchange. Build and platform verification instructions are in [QT_BUILD.md](QT_BUILD.md); device selection and backend behavior are described in [GPU_RENDERING.md](GPU_RENDERING.md).

## Open a project or flame library

Choose **File → Open project or library…**, use **Ctrl+O**, or drop one local file onto the window. The file picker recognizes native `.apoproject` files, `.flame`, `.xml`, historical `.temp` libraries and supported Apophysis `.upr` collections. Files saved with the 0.3.0 checkpoint can be opened directly.

The **Flame library** panel lists the file's entries. Its search field filters names. Selecting an entry shows its producer, camera, preview, and transforms. A thumbnail appears after that entry has rendered. **File → Recent files** retains the ten most recently opened paths.

A preservation-only entry displays its first render blocker. Its name, output dimensions, center, and scale remain editable. The whole library can be saved with those changes while retaining unsupported variations and other original data. Transform editing becomes available when the selected flame can be converted to the supported render model. The current historical coverage is recorded in [COMPATIBILITY.md](COMPATIBILITY.md) and [LEGACY_CORPUS.md](LEGACY_CORPUS.md).

**File → New flame** creates a new library containing the foundation triangle flame. Saving an untitled library prompts for a destination.

### Native projects and historical exchange

A native `.apoproject` file keeps the original flame library and its encoding inside a versioned envelope. It also retains the selected flame, preview and render settings, named render presets, workspace state, thumbnail, metadata, plugin references and the M9 animation timeline. Unknown project sections and unsupported animation payloads survive round trips; the timeline editor reports unsupported data instead of replacing it. Use **File → Save as…** and choose the native project format to retain this additional state. Opening plugin references never installs or enables native code. See [PROJECT_FORMAT.md](PROJECT_FORMAT.md) for the format and compatibility limits.

Opening a `.upr` collection offers its supported entries and reports incompatible entries. This accepts the historical Apophysis UPR dialect described in [LEGACY_FORMATS.md](LEGACY_FORMATS.md); arbitrary Ultra Fractal formulas and layered scenes are outside that importer. The original source stays untouched, and saving an imported entry as a native project retains its original UPR text as metadata. **File → Export Apophysis UPR…** writes only representable models and reports unsupported features before export.

The **Gradient editor** has **Import…** and **Export…** actions for GPL, UGR and Fractint MAP palettes. UGR collections offer compatible entries by name; unsupported gradient behavior produces a diagnostic. Palette application follows the creative editor's normal preview and undo workflow. [CREATIVE_TOOLS.md](CREATIVE_TOOLS.md) describes those controls, and [LEGACY_FORMATS.md](LEGACY_FORMATS.md) specifies the exchange rules.

## Live preview

The preview restarts after a flame parameter changes. It renders a quick pass, then increases the sample count through successive passes. The standard sequence is 80,000 samples, 400,000 samples, and the selected final limit. Presets provide **Draft · 400k**, **Standard · 2M**, and **Fine · 8M**. The statistics show samples, the actual Vulkan backend or CPU worker count, elapsed time, and completion. Hover over the statistics for plotted samples, invalid orbit steps, the device and any fallback reason. The Activity panel records backend changes.

Use **Pause preview** to stop preview work. Releasing Pause starts the latest edited flame again. **Restart** starts a fresh preview. A change during a render replaces the pending preview, and completed images from an older selection are discarded.

The mouse wheel magnifies the displayed image. Drag with the left or middle mouse button to pan it. **Fit** displays the whole image and **1:1** displays each preview pixel at one screen pixel. Use **Output and camera** to change the saved flame dimensions, center, or scale. The preview fits that composition into the current panel and uses oversampling one. Image export uses the saved output dimensions and oversampling.

## Transform triangles

Select a transform from the selector above the canvas or click its triangle. Triangles are color-coded and labeled by transform number. The final transform uses a gold dashed outline. The numbered origin handle corresponds to the affine translation. The **X** and **Y** handles show the two transformed basis vectors.

| Canvas control | Action |
| --- | --- |
| Drag the origin handle or triangle interior | Translate the selected triangle |
| Drag X or Y | Change that basis vector, allowing rotation, scaling, and shear |
| Middle-button drag | Pan the editing grid |
| Mouse wheel | Zoom around the pointer |
| Esc during a drag | Restore the triangle's values from the start of that drag |
| Fit triangles | Frame the current transform geometry |
| Grid | Show or hide grid lines |
| Snap | Snap dragged coordinates to the selected grid spacing |
| Lock | Temporarily prevent edits to the selected transform while inspecting it |

The canvas uses Cartesian coordinates, with positive Y upward. Coordinate readouts update as the pointer moves. A completed drag creates one undo step. Selection changes clear the temporary Lock state.

The **Pre-affine / Post-affine** selector determines which matrix the triangle and coefficient fields edit. The pre-affine matrix acts before the variation functions. The post-affine matrix acts on their combined output. Numeric edits and triangle drags update the same matrix.

### Geometry controls

The **Geometry** tab exposes all six affine coefficients in their `.flame` order. Their mapping is `x′ = a·x + c·y + e` and `y′ = b·x + d·y + f`. Enter a finite number and press Enter or leave the field to commit. Values use the shortest decimal representation that round-trips to the stored double. Longer values remain fully editable within the field.

**Rotate**, **Scale**, **Move**, **Flip X**, and **Flip Y** operate on the selected matrix. World coordinates apply the operation to its transformed geometry. Local coordinates apply it in the transform's own input space. Rotation and scale use the entered pivot. **Pivot at triangle origin** selects the corresponding origin for the current coordinate mode.

**Selection weight** determines an ordinary transform's relative probability. It must be finite and nonnegative. The final transform is applied after the ordinary orbit step and has no selection weight.

### Variations, color, and Xaos

The **Variations** tab lists the 43 built-in variations. Search by name and edit its weight in the table. Signed weights are supported. Setting a weight to zero disables its contribution. Select a variation to edit its built-in parameters. For example, JuliaScope has power and distance controls, while pie has slice count, rotation and thickness. The [variation guide](VARIATIONS.md) lists the fields.

**Plugin variations…** opens weights and named parameters for enabled native variations. Each parameter uses the module's declared type, default and valid range. The dialog retains existing variation order and full stored numeric precision when unchanged values are applied. Enable a trusted module through **Tools → Plugins…** before adding its variation. Native variations use the CPU renderer; an unavailable variation remains a render blocker and is never replaced with a built-in formula.

The **Color** tab controls palette coordinate, color speed, and opacity, each from zero to one. The palette strip shows the selected flame's current palette. A color speed of zero retains the incoming color coordinate, while one adopts the transform's color. The saved producer determines the historical interpretation of opacity and the serialized color-speed field.

The **Xaos** tab edits the selected ordinary transform's outgoing transition multipliers. Each destination combines its selection weight with the displayed multiplier. Missing values in legacy files appear as one. The editor rejects a change that leaves a positive-weight transform with no positive destination. Final transforms have no Xaos row.

## Add, copy, reorder, and delete

The **Transform** menu contains Add, Duplicate, Delete, Move up, Move down, and Add final transform. Add creates a new ordinary transform with a half-scale affine map. Duplicate appends a copy and splits the original selection weight between the two copies. It also duplicates the incoming chaos column and copies the outgoing row, preserving their combined branch probability initially.

Moving transforms reorders both sides of the Xaos matrix. Deleting one removes its destination from other rows. The editor retains at least one ordinary transform and validates the remaining transition graph before accepting a deletion. Add final transform creates an identity final transform. A file that already contains a disabled historical final definition reports that existing definition.

**Copy transform** and **Paste transform** keep a transform in the editor's internal clipboard and append a new ordinary transform. Paste clears the copied Xaos row because its destination indices refer to the source library. Across producer families, copied numeric coefficients and variation settings use the destination flame's producer conventions. Historical stochastic ordering and formula conventions can therefore change the resulting image.

Undo and redo restore complete document snapshots, including selection and original XML details. The session retains the latest 100 undoable changes. Returning to the last saved state clears the modified marker in the window title.

## Save documents and recover work

**File → Save** writes the entire loaded document. In a legacy library, an unchanged save retains its original bytes. Edited saves patch the affected fields and transform elements, retaining unrelated entries, comments, history, and palettes. The original XML encoding is preserved. **Save as…** writes to a chosen path. Native projects retain this library inside the project envelope along with their additional state. A document containing animation must be saved as `.apoproject`; **Export flame library** writes a separate `.flame` or `.xml` copy without discarding the open timeline. Without animation, choosing a legacy save format writes the library without project settings. If the current file changed on disk after opening, the editor offers **Save a copy**, **Replace disk version**, or **Cancel**.

**Export selected flame** creates a canonical standalone file for the selected renderable model. It includes the supported parameters and embedded palette. Collection metadata, comments, and editor history are omitted by that export operation. Use **Save** when those original details should accompany the edited file.

While a document has unsaved edits, the editor writes a recovery copy every 45 seconds. Native project recovery retains its envelope as well as the flame library. Startup offers the newest available recovery copy as an unsaved document, with **Recover edits**, **Discard recovery copy**, and **Later** choices. Copies belonging to an editor that is still open are skipped. Saving or discarding a recovered session removes its adopted recovery copy. Recovery files and the rotating activity log are stored in the application's per-user local data directory. Opening another document, creating a new flame, and closing the editor prompt about unsaved changes.

## Animation, morphing and color curves

Open **Tools → Animation timeline…** (`Ctrl+T`) or **Timeline** on the creative toolbar. Capture source flames at the playhead, set their outgoing transitions and review transform correspondence before morphing different entries. The timeline edits a private clip, provides local Undo/Redo, and previews scrubbed or played frames. **Apply timeline** creates a document undo step; **Cancel** discards the private changes. **Gradient** and **Adjustments** edit a selected keyframe, and **Use frame in editor** loads the evaluated playhead for ordinary editing. Save as `.apoproject` to retain the timeline and its source snapshots. The [timeline guide](ANIMATION_EDITOR.md) explains rational timing, presets, capture/import, playback and frame sequences; [MORPHING.md](MORPHING.md) defines correspondence and interpolation limits.

**Adjustment editor → Tone / color → Tone curves…** edits master and RGB reconstruction curves. **Gradient editor → Color adjustments → Palette curves…** bakes curves into the working palette. Both offer plotted control points, numeric weights, presets and reset; cancelling a containing dialog discards its pending edits. The Gradient editor also provides HSL controls and linear-light RGB/HSL stop interpolation. Original 7X lookup and continuous transfer are explicit modes with different evaluation rules. See [COLOR_TOOLS.md](COLOR_TOOLS.md) before changing a historical curve's mode.

On the timeline's **Image sequence** tab, choose a finite frame range and output settings, then use **Apply & queue frames**. Each admitted job keeps its own evaluated model and seed; later edits do not change it. Queue controls and checkpoint storage are covered in [RENDER_QUEUE.md](RENDER_QUEUE.md). The independent, resumable CLI sequence workflow is documented in [ANIMATION_CLI.md](ANIMATION_CLI.md).

## Run scripts

Open **Tools → Script editor…** or press **Ctrl+E**. Choose **Lua** or **Legacy Apophysis (supported subset)**, open a script or select an included example, and use **API reference** for the available host operations. The editor provides syntax highlighting and **Ctrl+Space** API completion. **Run**, also available with **Ctrl+Return**, executes on a private copy of the selected document; the open document and requested output files stay unchanged until **Apply result**.

The console shows messages, compatibility notes, errors and deferred output destinations. A failed or stopped run cannot be applied. **Stop** requests cooperative cancellation. **Seed**, **Time limit**, **Lua memory** and **Instructions** bound the script run; initial limits are 10 seconds, 32 MiB of Lua allocation and two million VM instructions. These are script limits, separate from render memory or the whole editor process. The legacy option translates the documented subset and reports unsupported syntax or operations; it does not execute arbitrary Delphi or Pascal programs. See [SCRIPTING.md](SCRIPTING.md) and [LEGACY_SCRIPTING.md](LEGACY_SCRIPTING.md) for supported calls, limits and examples.

**Allow queued renders** and **Allow exports** start unchecked. Enable the capabilities needed by the script and choose an existing **Output folder**. **Add input file…** supplies an explicitly selected input for the script's named import API. **Allow replacing output files** permits replacement of ordinary existing destinations. Scripts cannot directly access the filesystem, network, processes or native module loader; the host validates their requests and protects source inputs, application settings and recovery data, queue/checkpoint state, loaded plugin files and destinations already reserved by pending jobs.

After reviewing a successful run, **Apply result** creates one document undo step and publishes the permitted outputs. All requested render jobs are validated and added as one persisted queue batch before new work starts. A stopped queue waits for **Start / resume**; a running queue continues into the new pending jobs in queue order. Each request keeps the flame snapshot captured at that script call. Document undo does not remove queued jobs or exported files. Exports are committed atomically one file at a time; if a later write fails, earlier completed files can remain and the Activity panel reports them. [RENDER_QUEUE.md](RENDER_QUEUE.md) explains the batch and persistence behavior.

## Manage native variation plugins

Open **Tools → Plugins…**. **Add folder…**, **Remove folder** and **Refresh** manage the saved search paths. The table reads `.apoplugin` manifests and inspects their library files without executing them. Select a row to review identity, version, description, parameter defaults/ranges, SHA-256 fingerprint and any discovery error.

**Install…** copies a manifest and its adjacent native library into the user plugin directory; installation does not enable it. **Enable…** shows the exact library and fingerprint and requires **Trust and enable**. This runs native code inside the application with its filesystem and operating-system access. A faulty module can crash or hang the process; the script resource limits do not isolate native plugin faults. The approved manifest path and binary fingerprint are saved. On the next launch, only an unchanged approved binary is enabled, before the queue is restored. A replaced binary requires fresh approval.

**Disable** removes the variation from new model parsing and removes its startup approval. Existing immutable render snapshots retain the loaded module until their work and references are released. **Remove files…** is available for disabled packages and refuses removal while a flame or job still retains the module. Project references and old queue files never enable modules themselves. Re-enable the required trusted module to render a blocked document or restore its queue; changed plugin binaries invalidate exact queue/checkpoint identities.

M8 plugins are CPU-only. Automatic or Vulkan rendering reports the CPU fallback when allowed; a strict Vulkan request fails for a native plugin variation. Old 32-bit Apophysis DLLs cannot load into this x64 application. The bundled sample and C SDK provide the source migration path described in [PLUGINS.md](PLUGINS.md).

## Render an image

Choose **File → Render image** or press **Ctrl+R**. Select an output preset from 1920 × 1080 through 16384 × 16384, or enter custom dimensions up to 16384 per side. **Keep current aspect ratio** links the dimension fields. The dialog scales a render copy of the camera to the chosen dimensions; the open document remains unchanged.

Set sample density or disable automatic calculation to enter an exact sample count. Oversampling, spatial filter and radius, random seed, CPU worker count, system buffer budget, and working disk budget are independent controls. Renderer, preferred device, GPU working budget, and CPU fallback start with the preview preferences and apply to this queued export. The estimate shows planned buffers, conservative storage allowance, strip count, and strip height. Phase time estimates become available after the queue measures actual progress.

The **Project preset** selector restores named render settings. **Save current preset…** stores the current controls under a name, and **Remove preset** removes the selected preset. Save the document as `.apoproject` to retain presets and render settings across sessions. A preset does not enqueue work; choose a destination and add a job separately.

Choose the destination in the same dialog. **PNG** and **TIFF** support 8- or 16-bit output with optional transparent backgrounds using straight alpha. **PPM** supports 8- or 16-bit RGB, and **BMP** supports 8-bit RGB. The filename extension must match the selected format. Choose **Add to queue** to leave the new job pending, or **Add and start queue** to start queued and paused jobs in their current order.

Open **Render queue** from the File menu or toolbar to inspect and manage jobs. Each job keeps an immutable flame snapshot and its output settings while one background worker processes the list. You can keep editing, reorder pending jobs, pause the queue, cancel a job, retry or resume a stopped job, or start the selected snapshot over. The table and details show progress, elapsed time, measured phase ETA, actual backend and device, fallback reasons, and errors; **Activity** also reports the active export.

The queue saves automatically in the application's per-user local data directory and can also be saved or restored as a JSON file. Restored queues wait for an explicit **Start / resume** action. Jobs with native variations record the exact plugin binary fingerprint; a missing, disabled or changed module blocks restoration before any job list or automatic queue file is replaced. Pausing first requests a safe stop; a saved-checkpoint status appears only after the worker returns and the committed checkpoint is verified. Closing the editor pauses active work before exiting. Checkpoint samples stay in their referenced working directories when a queue JSON is saved elsewhere; removing a queue entry does not delete its output or checkpoint files.

High-resolution output uses bounded reconstruction strips with global sampling and filter overlap. Images are staged and replace the destination only after rendering and writing complete, so a failed or cancelled render preserves an existing destination. A resumed reconstruction starts its output again from saved samples. System, GPU, and disk budgets are checked separately. See [RENDER_QUEUE.md](RENDER_QUEUE.md) for queue controls, persistence, and recovery, and [HIGH_RES_RENDERING.md](HIGH_RES_RENDERING.md) for memory planning, file formats, and command-line rendering.

## Preferences and workspace

**Edit → Preferences** sets the preview's CPU worker count, system buffer budget, random seed, final sample limit, renderer, preferred device, GPU working budget and fallback policy. Automatic CPU worker selection leaves one reported hardware thread for the interface when more than one is available. Preferences are stored per user through Qt's settings system and are inherited by creative previews.

**CPU (reference)** retains the 64-bit calculation path. **Vulkan (FP32 sampling)** uses 32-bit orbit calculations with CPU image reconstruction; identical pixels across backends or devices are not guaranteed. **Automatic** uses an explicitly selected device, or chooses suitable hardware Vulkan when device selection is automatic. Software implementations are labeled and skipped by automatic device selection. A new saved-settings profile defaults to Automatic with CPU fallback enabled. The capability panel shows device memory and limits; **CPU fallback** is reported when it actually occurs. [GPU_RENDERING.md](GPU_RENDERING.md) gives the full behavior and command-line equivalents.

Library, transform, properties, and activity panels can be docked, tabbed, floated, hidden, and restored through **View**. The editor saves the window geometry and dock layout between sessions. **View → Reset workspace** restores the initial arrangement. **F11** toggles full screen.

| Windows shortcut | Command |
| --- | --- |
| Ctrl+N | New flame |
| Ctrl+O | Open project or library |
| Ctrl+S | Save |
| Ctrl+Shift+S | Save as |
| Ctrl+Z | Undo |
| Ctrl+Y | Redo |
| Ctrl+D | Duplicate transform |
| Ctrl+Shift+C | Copy transform |
| Ctrl+Shift+V | Paste transform |
| Ctrl+Shift+D | Duplicate flame entry |
| Ctrl+G | Gradient editor |
| Ctrl+J | Adjustment editor |
| Ctrl+M | Mutation explorer |
| Ctrl+Shift+N | Random flame generator |
| Ctrl+E | Script editor |
| Ctrl+T | Animation timeline |
| Ctrl+R | Render image |
| F11 | Full screen |

## Further editor work

The **Flame** menu and **Gradient / Adjust / Mutate / Random** toolbar open the creative editors. See [CREATIVE_TOOLS.md](CREATIVE_TOOLS.md) for their controls, previews and save behavior. The Flame menu also duplicates and deletes collection entries; both operations restore entry selection on undo/redo. At least one entry remains. Further historical variation and creative-workflow coverage remains open. Multi-selection, transform synchronization, persistent per-transform locks, custom shortcut profiles, and broader workspace presets remain open. Native Windows execution and historical-application reopen results are recorded separately in the checkpoint's [test report](TEST_PLAN.md).
