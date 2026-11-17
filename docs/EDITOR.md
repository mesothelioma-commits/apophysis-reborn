# Primary Editor guide — 0.4.0

Extract the complete portable package and open `bin/windows-x64/apophysis-reborn.exe`. The editor uses the same CPU engine and legacy document model as the command-line tools. It starts with Ember Spiral when a file has not been supplied. Build and platform verification instructions are in [QT_BUILD.md](QT_BUILD.md).

## Open a flame library

Choose **File → Open library**, use **Ctrl+O**, or drop one local file onto the window. The file picker recognizes `.flame`, `.xml`, and historical `.temp` libraries. Files saved with the 0.3.0 checkpoint can be opened directly.

The **Flame library** panel lists the file's entries. Its search field filters names. Selecting an entry shows its producer, camera, preview, and transforms. A thumbnail appears after that entry has rendered. **File → Recent files** retains the ten most recently opened paths.

A preservation-only entry displays its first render blocker. Its name, output dimensions, center, and scale remain editable. The whole library can be saved with those changes while retaining unsupported variations and other original data. Transform editing becomes available when the selected flame can be converted to the supported render model. The current historical coverage is recorded in [COMPATIBILITY.md](COMPATIBILITY.md) and [LEGACY_CORPUS.md](LEGACY_CORPUS.md).

**File → New flame** creates a new library containing the foundation triangle flame. Saving an untitled library prompts for a destination.

## Live preview

The preview restarts after a flame parameter changes. It renders a quick pass, then increases the sample count through successive passes. The standard sequence is 80,000 samples, 400,000 samples, and the selected final limit. Presets provide **Draft · 400k**, **Standard · 2M**, and **Fine · 8M**. The statistics show samples, workers, elapsed time, and completion. Hover over the statistics for plotted samples and invalid orbit steps.

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

The **Variations** tab lists the 21 registered variations. Search by name and edit its weight in the table. Signed weights are supported. Setting a weight to zero disables its contribution. JuliaN exposes its nonzero integer power and finite distance parameter when its weight is active.

The **Color** tab controls palette coordinate, color speed, and opacity, each from zero to one. The palette strip shows the selected flame's current palette. A color speed of zero retains the incoming color coordinate, while one adopts the transform's color. The saved producer determines the historical interpretation of opacity and the serialized color-speed field.

The **Xaos** tab edits the selected ordinary transform's outgoing transition multipliers. Each destination combines its selection weight with the displayed multiplier. Missing values in legacy files appear as one. The editor rejects a change that leaves a positive-weight transform with no positive destination. Final transforms have no Xaos row.

## Add, copy, reorder, and delete

The **Transform** menu contains Add, Duplicate, Delete, Move up, Move down, and Add final transform. Add creates a new ordinary transform with a half-scale affine map. Duplicate appends a copy and splits the original selection weight between the two copies. It also duplicates the incoming chaos column and copies the outgoing row, preserving their combined branch probability initially.

Moving transforms reorders both sides of the Xaos matrix. Deleting one removes its destination from other rows. The editor retains at least one ordinary transform and validates the remaining transition graph before accepting a deletion. Add final transform creates an identity final transform. A file that already contains a disabled historical final definition reports that existing definition.

**Copy transform** and **Paste transform** keep a transform in the editor's internal clipboard and append a new ordinary transform. Paste clears the copied Xaos row because its destination indices refer to the source library. Across producer families, copied numeric coefficients and variation settings use the destination flame's producer conventions. Historical stochastic ordering and formula conventions can therefore change the resulting image.

Undo and redo restore complete document snapshots, including selection and original XML details. The session retains the latest 100 undoable changes. Returning to the last saved state clears the modified marker in the window title.

## Save libraries and recover work

**Save library** writes the entire loaded document. An unchanged save retains its original bytes. Edited saves patch the affected fields and transform elements, retaining unrelated entries, comments, history, and palettes. The original XML encoding is preserved. **Save library as** writes to a chosen path. If the current file changed on disk after opening, the editor offers **Save a copy**, **Replace disk version**, or **Cancel**.

**Export selected flame** creates a canonical standalone file for the selected renderable model. It includes the supported parameters and embedded palette. Collection metadata, comments, and editor history are omitted by that export operation. Use Save library when those original details should accompany the edited file.

While a library has unsaved edits, the editor writes a recovery copy every 45 seconds. Startup offers the newest available recovery copy as an unsaved library, with **Recover edits**, **Discard recovery copy**, and **Later** choices. Copies belonging to an editor that is still open are skipped. Saving or discarding a recovered session removes its adopted recovery copy. Recovery files and the rotating activity log are stored in the application's per-user local data directory. Opening another library, creating a new flame, and closing the editor prompt about unsaved changes.

## Render an image

Choose **File → Render image** or press **Ctrl+R**. The dialog shows the saved output dimensions and accepts a sample count, CPU worker count, and buffer budget. The default sample count derives from the flame's quality and zoom. Select a `.bmp` or `.ppm` destination after accepting the settings.

The render runs in the background. **Activity** shows its status and progress and provides **Cancel render**. Output is staged and replaces the destination after rendering and writing complete. The renderer's memory estimate includes the histogram, output pixels, and worker hit buffers. Increasing output dimensions or oversampling can require a larger budget.

## Preferences and workspace

**Edit → Preferences** sets the preview's CPU worker count, buffer budget, random seed, and final sample limit. Automatic preview worker selection leaves one reported hardware thread for the interface when more than one is available. Preferences are stored per user through Qt's settings system.

Library, transform, properties, and activity panels can be docked, tabbed, floated, hidden, and restored through **View**. The editor saves the window geometry and dock layout between sessions. **View → Reset workspace** restores the initial arrangement. **F11** toggles full screen.

| Windows shortcut | Command |
| --- | --- |
| Ctrl+N | New flame |
| Ctrl+O | Open library |
| Ctrl+S | Save library |
| Ctrl+Shift+S | Save library as |
| Ctrl+Z | Undo |
| Ctrl+Y | Redo |
| Ctrl+D | Duplicate transform |
| Ctrl+Shift+C | Copy transform |
| Ctrl+Shift+V | Paste transform |
| Ctrl+R | Render image |
| F11 | Full screen |

## Further editor work

The next creative-tools work includes the full gradient and adjustment editors, mutation and random-generation tools, and further variation coverage. Multi-selection, transform synchronization, persistent per-transform locks, custom shortcut profiles, and broader workspace presets remain open. Native Windows execution and historical-application reopen results are recorded separately in the checkpoint's [test report](TEST_PLAN.md).
