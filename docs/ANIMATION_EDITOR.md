# Animation timeline

Open **Tools → Animation timeline** (`Ctrl+T`) or **Timeline** on the creative toolbar. The dialog owns a private copy. **Apply timeline** stores it as one undoable document change; **Cancel** discards the dialog's edits. Local Undo/Redo keeps up to 16 versions within a 32 MiB history budget (at least the current and preceding version). Project Undo does not remove jobs already admitted to the render queue.

## Create and edit keyframes

The playhead and filenames use **zero-based frame numbers**. A 121-frame clip contains frames 0 through 120. At 30 fps its exported duration is 121/30 seconds; the final frame begins at 120/30 seconds.

1. Open a flame collection and choose the starting flame.
2. Open Timeline and set the total frames and rational frame rate on **Timing & interpolation**. Rates such as 30000/1001 are retained exactly. Supported rates are 1–240 fps; the clip limit is 1,000,000 frames.
3. Scrub to another frame. **Capture editor flame** stores the selected editor flame there. **Replace selected** replaces the selected keyframe while preserving its name, outgoing easing and existing correspondence IDs when the transform count matches.
4. **Add at frame** captures the selected entry in the library dropdown. **Import** reads another project or collection and lets you select an entry. Captured keyframes retain complete exact source collections, selected entry and transform IDs; unrelated source data is retained.
5. Edit frame positions and names in the table. The Transition column controls the outgoing segment: Hold/cut, Linear, Smooth or Smoother. Frame zero must remain present, and positions must be unique and within the clip. Invalid edits restore the last valid controls and show a diagnostic.
6. **Gradient** and **Adjustments** edit a selected keyframe privately, including tone curves. **Use frame in editor** applies the timeline and loads the evaluated playhead into the normal editor, with undo for both operations. After editing transforms or variation parameters, reopen Timeline and capture or replace the intended keyframe.

Changing duration changes the frame count, rounded to the nearest whole frame. Existing keyframes are not silently retimed: shortening a clip past its last keyframe is rejected. Move or remove that keyframe first. A maximum of 256 keyframes and 256 ordinary transforms per snapshot is supported within the 8 MiB serialized animation payload. A large source collection may reach that byte limit before the count limit.

## Morphing and correspondence

**Morph to library selection** makes a two-keyframe morph from the active editor flame to the library dropdown selection. This is an explicit positional pairing: transform 1 is paired with transform 1, and so on. **Correspondence** lets you inspect and change the stable IDs in each keyframe. IDs must be unique within a keyframe. Reordering source transforms does not change an explicitly shared ID.

**Require matching transform IDs** rejects incompatible sets. **Fade unmatched transforms** interpolates missing ordinary branches through zero selection weight and fades associated Xaos edges. It can also transition final-transform presence using an identity endpoint. This is a numerical policy, not a promise that arbitrary flames make an attractive or stable morph. Review intermediate frames before a long export. Hold/cut can separate snapshots that cannot be continuously interpolated, including different producer semantics.

Continuous camera, affine, variation-weight/real-parameter, transform color/opacity/Xaos, palette and curve settings interpolate. Integer and discrete choices hold the left value until the next exact keyframe. Exact keyframe frames return their original parsed models. The [model contract](ANIMATION_MODEL.md) and [morphing guide](MORPHING.md) list the precise rules and fallback diagnostics.

The interpolation tab offers linear affine coefficients or polar rotation/stretch, shortest or unwrapped camera rotation, RGB/linear-light RGB/shortest-hue HSV/HSL palettes, and fixed or deterministic per-frame sampling seeds. A fixed seed makes successive CPU samples more comparable; it does not guarantee matching pixels across different backends or GPU drivers. The full unsigned 64-bit seed is preserved.

## Presets and playback

Still, Camera orbit, Zoom and Palette cycle replace the private clip and support local Undo. Camera orbit deliberately uses unwrapped rotation so a full 360-degree turn is retained. Zoom previews with ping-pong looping. Loop mode changes playback frame lookup; exports always use the explicitly selected finite frame range.

Click or drag the ruler, use the slider, enter a frame number, or step with First/−1/+1/Last. Diamonds mark keyframes. **Play** advances only after the current preview finishes; **Pause** stops advancing. This avoids repeatedly cancelling an unfinished preview. Playback can run below the chosen fps when rendering is expensive. Exported frame timing is defined by the stored rational rate, not by interactive playback speed.

Missing plugins or incompatible continuous segments leave the timeline editable and saveable with a diagnostic. Preview, Use frame and queue admission require evaluable frames. Native plugin enablement remains explicit; a project cannot enable code by containing a reference. Keyframes bind captured native module hashes. Re-capture intentionally after a trusted module/platform change.

## Queue an image sequence

On **Image sequence**, choose the output folder, prefix, inclusive first/last frames and step. Filenames are `PREFIX_000000.png` and retain the original frame numbers when a subrange or larger step is selected. Choose one fixed output canvas for the sequence, or leave both dimensions at First frame. All selected frames use the same fit policy.

PNG/TIFF support 8-bit or direct 16-bit RGB/RGBA; PPM supports RGB8/16 and BMP RGB8. Choose sample count, workers, host/GPU memory, backend, device and fallback policy. **Apply & queue frames** preflights every selected frame, path, output format, payload and resource budget before adding the batch. The queue limit is 1,000 total jobs; a sequence batch also cannot exceed 1,000 selected frames. Longer clips can be exported in explicit subranges without truncation.

The batch disk budget includes final images and the conservative scratch estimates for **all retained queue checkpoints**. It may require more space than CLI sequence export, which removes each completed frame's checkpoint. Reduce the selected range or increase the budget when needed. The dialog adds stopped jobs; start them in the existing Render queue. If the queue was already running, admitted jobs join it.

Each job owns its evaluated flame, seed, canvas, render settings and native module owners. Later timeline or flame edits do not alter those snapshots. A filename that appears or changes after sequence admission is refused immediately before publication, and the external file is preserved. Existing files are admitted only when Replace existing frame images was selected. Re-queue a conflicting frame with deliberate replacement to accept a newer destination. The guard persists when saving/restoring the queue; completed jobs update the guard to their own successful output.

Queue pause/resume and checkpoint integrity follow [RENDER_QUEUE.md](RENDER_QUEUE.md). Each frame is published atomically; successful earlier frames remain available if a later job fails. A queue JSON references its checkpoint directories and does not embed sampled data. The independent CLI sequence manifest and the Qt queue are separate persistence mechanisms; use the respective resume controls for the workflow that created the frames.

## Projects and legacy files

A timeline is saved in `.apoproject`. Saving a document with any animation payload to a plain `.flame`/`.xml` is blocked; normal Save prompts for a native project filename when necessary. **File → Export flame library** writes a separate exact legacy collection while the native project remains open. **Export selected flame** writes the supported canonical model. Neither export includes the timeline.

Unknown animation payloads and future schema versions remain byte-preserved in the project envelope. This timeline editor refuses implicit replacement of an unrecognized payload. The CLI has an explicit `--replace-animation` option for intentional replacement. Project recovery includes the timeline, source collections and retained metadata.

**Import timed library** asks whether legacy `time` values mean frames or seconds. There is no universal Apophysis AV time-unit assumption. An omitted `time` retains the legacy zero default. Import rejects invalid or negative time, colliding rounded positions and a missing frame-zero start. It does not infer missing keyframes or silently reorder/retime them. The [CLI guide](ANIMATION_CLI.md) describes equivalent operations and resumable sequence exports.

## Current scope

Use the Property keys tab for independent zoom, rotation and tone controls. Open the sequence composer from Tools to stack animated projects or PNG layers and export transparent frames or a silent H.264 MP4. [Animation and composition walkthrough](RELEASE_0.17.0.md)
