# Render queue and high-resolution dialog

Apophysis Reborn 0.7 adds a persistent queue to the native editor. Each job owns an immutable flame snapshot and render settings, so you can keep editing the source while a background worker renders the queue in order.

## Add a render

Choose **File → Render image**. Set the destination and choose **Add to queue** to leave the queue stopped, or **Add and start queue** to start queued and paused jobs in their current order. The toolbar and File menu also open **Render queue** directly.

The resolution presets are 1920 × 1080, 2560 × 1440, 3840 × 2160, 4096 × 4096, 7680 × 4320, 8192 × 8192, 15360 × 8640, and 16384 × 16384. Custom dimensions run from 1 to 16384 per side. **Keep current aspect ratio** links the dimension fields; selecting a preset establishes its aspect ratio. The render copy scales the camera with the smaller width/height scale factor. These settings do not change the open document.

| Control | Effect |
| --- | --- |
| Sample density | With automatic samples enabled, calculates the target from density, output dimensions, and zoom. Clear that checkbox to enter an exact positive sample count. |
| Oversampling | Uses a larger sampling histogram before reconstruction. Higher values increase the required working buffers. |
| Spatial filter / radius | Selects the box or Gaussian reconstruction filter. Gaussian strips include the overlap needed by the filter. |
| Format / bit depth | PNG and TIFF support 8- or 16-bit RGB/RGBA, PPM supports 8- or 16-bit RGB, and BMP supports 8-bit RGB. |
| Transparent background | Available for PNG and TIFF. Exports straight, unassociated alpha. |
| System buffer budget | Limits planned rendering buffers. It is not a cap on the editor's total resident memory. |
| Working disk budget | Bounds the conservative storage estimate for committed sample chunks and image output. |
| Random seed | Accepts the full unsigned 64-bit range and preserves it exactly in queue files. |
| Renderer and device | Chooses CPU, automatic selection, or Vulkan; GPU budget and CPU fallback are separate settings. The queue reports the actual resolved backend, device, and fallback reason. |

The estimate shows planned buffer storage, conservative disk allowance, and the number and height of strips. An available-space check is also shown when a destination volume can be inspected. It does not predict render time before work has been measured. Dimensions, sampling settings, budgets, format, and the output extension are checked before a job is accepted.

## Queue controls

One background worker runs one job at a time. The queue window stays responsive while sampling, writing checkpoints, and reconstructing image strips.

| Control | Behavior |
| --- | --- |
| Start / resume | Starts the first queued or paused job, then continues in queue order. A valid checkpoint resumes committed sampling. |
| Pause queue | Stops scheduling new jobs and requests a stop from the active worker. Wait for the final paused status before assuming a checkpoint is usable. |
| Cancel job | Stops that job. Other queued jobs can continue if the queue is still running. An existing destination image is retained. |
| Retry / resume job | Returns a failed, cancelled, or paused job to the queue. Uses a valid checkpoint; otherwise allocates a fresh checkpoint directory. |
| Start selected job over | Queues the same snapshot and settings from the beginning in a new checkpoint directory. Available for completed jobs too. |
| Move up / down | Changes job order. The running job cannot be moved or swapped. |
| Remove | Removes the queue entry. It does not delete image files or checkpoint directories. |
| Save queue | Atomically writes the current list and embedded snapshots to a chosen JSON file. |
| Restore queue | Validates a saved queue completely, then replaces the list. The current worker must be stopped first. Restoring never starts work automatically. |

The table shows dimensions, actual renderer, status, progress, elapsed time, the measured estimate for the current phase, and destination. Select a job for its sample count, seed, format, source, checkpoint status, device, fallback reason, and any error. Hover over the details to inspect its checkpoint directory.

Progress allocates 80% to sampling and 20% to reconstruction strips. This is a work-phase indicator, not a prediction of how runtime is divided. **Phase ETA** appears only after measurable progress over at least half a second and estimates the remaining work in the current phase. It starts over when the phase changes or the job resumes. Elapsed time accumulates across runs and excludes time spent paused.

## Persistence and recovery

Normal editor sessions save `render-queue.json` in Qt's application-local data directory. Sampling checkpoints live under its `render-checkpoints` subdirectory, each in a UUID-named directory. A session lock prevents two editor instances from silently replacing the same automatic queue file. If that file is already owned, another session uses separate persistent checkpoint storage and can save its queue explicitly.

Each job saves its source label and destination, embedded flame XML, transform math conventions, all render/export settings, status, progress, elapsed time, actual device/backend, fallback reason, and error. Full-width integer counters and seeds are stored as decimal strings. Queue files use `QSaveFile` to commit atomically. Invalid JSON, incompatible versions, duplicate identifiers, invalid snapshots, and invalid render settings fail before replacing the current list or its automatic save file.

At normal startup the editor restores its saved list. Interrupted running jobs become paused, and **no restored job starts without an explicit Start / resume action**. A malformed automatic file is retained; the editor reports the error and disables writes to that file so a new session cannot overwrite the recovery evidence.

Pausing first shows a stopping state. The worker returns after stopping safely, then the queue checks the committed checkpoint before showing **Paused · checkpoint saved**. If no sampling chunk was committed yet, the status says the job is ready to restart. A chunk consists of immutable sampled hits followed by an atomic manifest commit. Only complete, referenced chunks participate in a resume. The core validates checkpoint identity and chunk integrity when resuming.

Sampling uses global chains, so resuming does not re-sample committed chains. If a stop occurs during reconstruction, the next run reconstructs the image again from its saved samples, beginning with the first strip. Output is streamed into a temporary file and replaces the destination only after successful completion. A stop, failed render, or cancelled job therefore preserves a previous destination image.

Closing the editor with an active queue requests a pause and keeps processing UI events until the worker has stopped. Destruction also waits for the stopped worker and saves its final state. Unfinished queued jobs remain stopped for the next launch. This is a graceful-close guarantee; forced termination and subsequent recovery are covered separately by the core checkpoint tests.

Saving a queue JSON does **not** copy its checkpoint chunks. The file references absolute destination and checkpoint paths. Keep the referenced checkpoint directory when moving queue files between machines or installations. Missing checkpoints require starting the job over; the editor does not silently claim such a job was resumed. Completed, removed, and superseded checkpoint directories are retained in this milestone. No operating-system shutdown or power-management action is implemented.

## Verification modes

`render-queue-tests` covers immutable snapshots, exact 64-bit settings, model math conventions, queue ordering, atomic restore rollback, responsive timer callbacks during real queued work, pause/checkpoint/restore/resume, byte equality with uninterrupted rendering, destination preservation, failure/retry, and graceful destruction into a stopped resumable queue. The existing editor regression also checks that renderer details remain visible when backend selection changes.

For a small strict native UI check:

```sh
apophysis-reborn --queue-smoke-test OUTPUT_DIRECTORY
```

This runs two 192 × 144 jobs and requires successful 16-bit transparent PNG and opaque TIFF exports, plus event-loop heartbeats while work is active. It saves `queue-render-16.png`, `queue-render-16.tif`, `render-queue.png`, `high-resolution-settings.png`, `render-queue.json`, and `queue-report.json`, as well as the shared `editor.png`, `preview.png`, and `saved-example.flame` smoke artifacts. The report records actual execution backend, device, fallback reason, samples, elapsed time, and heartbeat count. Smoke modes use isolated settings; the small mode's sample checkpoints are temporary.

For an optional full-resolution acceptance run, outside normal CTest:

```sh
apophysis-reborn --large-render-smoke-test OUTPUT_DIRECTORY
```

This renders the built-in Ember spiral at **16384 × 16384**, using 8,388,611 samples, seed 123456, CPU rendering, 128 MiB of planned buffer budget, an 8192 MiB disk budget, automatic strip height, one-times oversampling, and 16-bit RGBA PNG output. Its camera scale is the source scale multiplied by 16384/576. It is a native full-resolution render, not an enlarged preview, and requires several GiB of free storage.

The large mode writes a known prior destination, pauses after committed sampling, verifies a usable checkpoint and preservation of that destination, then explicitly resumes to `16k.png`. It records 10 ms UI heartbeat counts and maximum gaps, the Qt platform in use, completed sample/strip counts, actual backend/device, elapsed time, and pause/resume evidence in `16k-report.json`. A gap over one second fails the mode. It also saves `16k-queue-paused.png`, `16k-queue-complete.png`, `16k-editor-complete.png`, `paused-queue.json`, and `completed-queue.json`; durable queue/checkpoint data remain under `queue-data`. Its timeout is 15 minutes. This mode renders through the real native queue and visible Qt widgets; a report naming the offscreen platform does not claim a desktop interaction test.

See [High-resolution rendering](HIGH_RES_RENDERING.md) for the core and command-line workflow, [GPU rendering](GPU_RENDERING.md) for Vulkan selection and limitations, and [the test plan](TEST_PLAN.md) for the milestone validation matrix.
