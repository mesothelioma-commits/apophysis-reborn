# High-resolution rendering — 0.7.0

File renders now sample the complete flame once, retain bounded chunks on disk,
and reconstruct the image in horizontal strips. Output rows go directly to the
selected encoder. This supports dimensions up to **16,384 × 16,384** without
requiring a complete oversampled histogram or final image buffer in RAM.
The ordinary editor preview remains a separate, interactive rendering path.

The renderer supports CPU and Vulkan sampling, persistent render jobs, restart
checkpoints, and true 16-bit integer export. See [TEST_PLAN.md](TEST_PLAN.md) for
executed acceptance results and platform limits; supported dimensions alone
are not a performance or physical-GPU compatibility claim.

## Render from the command line

The examples use `flame-cpu`; on Windows, use `flame-cpu.exe` from the portable
package. Quote paths that contain spaces.

```bash
flame-cpu --flame examples/ember-spiral.flame --output ember.png \
  --size 4096 3072 --samples 20000000 --seed 48 \
  --oversample 2 --filter-radius 0.5 \
  --memory-mib 512 --disk-mib 2048 --bit-depth 16
```

`--size W H` fits the existing camera by multiplying its scale by the smaller
of the width and height ratios. It preserves the camera center, rotation and
zoom. A changed aspect ratio therefore changes the amount visible along one
axis. Dimensions must be positive and no larger than 16,384 each. If `--samples`
is omitted, the existing XML quality/zoom rule is evaluated after the size and
oversampling overrides. Increasing dimensions while retaining a fixed sample
count lowers the sample density; a large file is not automatically a clean
high-quality render.

CPU remains the command-line default. Add `--backend auto` to allow Vulkan
selection and CPU fallback, or `--backend vulkan --no-fallback` to require
successful Vulkan sampling. `--list-devices` and `--list-devices --json` list
available devices; `--device ID` selects an ID or zero-based index. Automatic
selection skips software Vulkan implementations unless one is explicitly
selected. `--gpu-memory-mib` separately limits Vulkan working allocations.
See [GPU_STREAMING.md](GPU_STREAMING.md) for the bounded hit-buffer contract.

### Plan before allocating

```bash
flame-cpu --flame examples/ember-spiral.flame --output large.png \
  --size 16384 16384 --samples 100000000 --oversample 2 \
  --memory-mib 256 --disk-mib 8192 --bit-depth 16 --alpha --estimate
```

`--estimate` validates the flame, dimensions, selected format, filter footprint
and host/disk budgets. It prints these fields without creating an output,
checkpoint directory or render job:

| Field | Meaning |
| --- | --- |
| `width`, `height`, `samples` | Requested output geometry and iteration budget |
| `format`, `alpha` | Actual selected encoding/depth and transparency mode |
| `requested_backend` | Requested CPU, automatic or Vulkan sampling policy |
| `host_bytes` | Conservative peak allowance for the renderer's host working buffers |
| `disk_bytes` | Conservative allowance for sampling chunks, their indexes, final output and metadata |
| `strip_rows`, `strips` | Reconstruction strip height and number of strips |

`--output` is optional when estimating. With no output path or explicit format,
the estimate uses PNG8. The plan does not select a Vulkan device, measure driver
allocations or predict a render time. Device admission is checked when sampling
starts. A memory-budget rejection happens before destination replacement.

### Export format and transparency

| File extension | Default | Other supported selection |
| --- | --- | --- |
| `.bmp` | RGB8 | No 16-bit or alpha variant |
| `.ppm` | RGB8 | RGB16 with `--bit-depth 16` |
| `.png` | RGB8 | RGB16 and RGBA8/16 |
| `.tif`, `.tiff` | RGB8 | RGB16 and RGBA8/16 |

Use `--bit-depth 16` to select 16-bit channels and `--alpha` for straight RGBA
in PNG or TIFF. BMP/PPM reject `--alpha`. `--format png`, `--format tiff`, and
the other base names select a family; full names such as `png16` or `tiff8`
also select depth. A conflicting full name and `--bit-depth` are errors, as is
a filename extension that disagrees with the selected format.

```bash
flame-cpu --flame examples/ember-spiral.flame --output transparent.tif \
  --size 2048 1536 --samples 10000000 --format tiff16 --alpha
```

Sixteen-bit values are quantized directly from the double-valued reconstruction
result. They are not expanded eight-bit values. RGB output composites the
chosen flame background. RGBA output carries straight foreground color and
the renderer's density-derived opacity, with transparent black for zero
opacity. The writer preserves that opacity without a second gamma transform.
The exports are tone-mapped integer images; no scene-linear/HDR or invented
color profile is claimed. [IMAGE_FORMATS.md](IMAGE_FORMATS.md) specifies the
encoding, precision, color and alpha contracts.

All command-line file exports now use the streaming writer. BMP rows are
stored top-down using a negative header height. This changes the file bytes
relative to the older positive-height, bottom-up BMP writer; decoded RGB8
orientation and values are covered by compatibility checks.

## Memory, disk and filter boundaries

`--memory-mib` is a working-buffer limit, not an operating-system cap on the
entire process. The plan includes the model and metadata allowance, sample
buffers, one double-valued output row, encoder state and the reconstruction
strip with its filter overlap. Runtime libraries, thread stacks, the editor,
the graphics driver and filesystem caches can contribute additional process
or system memory. `--gpu-memory-mib` is a separate limit on charged Vulkan
allocations; reported device heap size is not a free-VRAM measurement.

Automatic strip selection uses at most 256 output rows and reduces the height
to fit the host budget. `--strip-rows N` requests a specific positive height.
The planner rejects it if the strip plus overlap and fixed buffers do not fit.
Increasing oversampling or filter radius increases the padded histogram and
the overlap required around every strip. If even one complete output row and
its filter footprint do not fit, increase the budget or reduce those settings.

Sampling always uses the full padded histogram's camera coordinates. Chunks
are partitioned by their global row group after sampling. Reconstruction reads
the groups needed for a strip and its complete filter footprint, accumulates
their integer contributions, and applies the existing density/tone rules.
Sampling does not restart a cropped flame for each strip. Filtering, exposure
and normalization use the complete job's settings and sample count, preventing
strip boundaries from introducing separate density scales or missing overlap.

Disk storage scales mainly with the number of plotted samples. Each hit record
uses 24 bytes; chunk headers and row-group indexes are additional. Planning
conservatively assumes that every requested sample plots and charges indexes
at the minimum supported chunk size, so an actual job can occupy much less
than its estimate. The allowance also covers the selected output's upper
bound and metadata. PNG's actual compressed size depends on image content.

The default `--disk-mib` is 16,384 MiB. It is an admission budget, not a quota
reserved from the operating system. Free space is checked before rendering;
other applications can still consume that space later. Write failures abort
the render and preserve any previously completed destination. The new image
is written to a temporary sibling and replaces the destination only after
successful encoding and final cancellation checks. Existing destination bytes
remain on disk until replacement and are not counted as new job storage.

With no explicit checkpoint directory, the CLI uses private working storage
alongside the output and removes it after completion or cancellation. For
long renders that must survive a process restart, choose `--checkpoint DIR`.
The final output must be outside that directory, including its descendants.

## Pause and resume a checkpoint

```bash
flame-cpu --flame examples/ember-spiral.flame --output final.png \
  --size 4096 3072 --samples 20000000 --seed 48 --bit-depth 16 \
  --checkpoint "work/ember render" --pause-after 1000000
```

`--pause-after N` requests a reproducible pause after at least N samples are
committed. It requires a checkpoint directory and a threshold between one and
the total sample count. A normal chunk contains up to 65,536 requested samples,
so the saved count may exceed the threshold. Smaller memory budgets can reduce
the chunk size. The command returns **75** for a successful requested pause,
prints the committed and total sample counts, and leaves an existing output
untouched. It does not claim an unfinished file as a successful image.

Resume in a new process with the same settings, add `--resume`, and omit
`--pause-after` to finish:

```bash
flame-cpu --flame examples/ember-spiral.flame --output final.png \
  --size 4096 3072 --samples 20000000 --seed 48 --bit-depth 16 \
  --checkpoint "work/ember render" --resume
```

Ctrl+C also requests cancellation. The signal handler records the request;
the render's ordinary progress code converts it into a stop request. Completed
chunks remain usable with an explicit checkpoint, while work in the current
uncommitted chunk can be discarded. Interruption returns 130 for SIGINT or
143 for SIGTERM. During a long operation the request can wait until a bounded
sampling/progress boundary is reached. A forced process termination cannot
perform normal temporary-file cleanup, but already committed explicit
checkpoint chunks are designed to survive it.

A checkpoint contains an atomically published, checksummed manifest and
immutable, flushed sampling chunks. The manifest advances only after its
referenced chunk has been closed and flushed. Resume verifies manifest
integrity, all referenced chunk structure and payload checksums, and sampling
continuity before accepting the saved data. Process-owned file locks prevent
concurrent writers and are released by the operating system on termination;
the retained lock filename does not mean a job remains locked.

Resume requires the same flame model, camera/quality/tone settings, sample
budget, seed, requested backend/fallback policy, export format and alpha mode.
It also validates the engine/compiler/platform identity. Committed Vulkan
sampling additionally requires the saved device, driver and shader identity;
there is no silent switch to CPU or another GPU after sampling has been
committed. An unavailable GPU may fall back before any sampling is committed
when the requested policy allows it.

CPU worker count and resource budgets may change on resume if the saved
geometry still fits. The saved strip height is retained; requesting another
explicit strip height is rejected. The output destination may change because
it is not part of the sampling identity. A fully sampled checkpoint can
reconstruct the image again. Reconstruction/encoding restarts from the first
output strip, so interruption during that phase saves sampling work rather
than a partially encoded image.

An explicit checkpoint is retained after success. Keep it while more renders
or verification are useful, then remove the directory when no process uses
it. Starting a fresh job in a nonempty checkpoint directory is rejected; use
`--resume` or choose another directory. Checksums detect accidental corruption;
checkpoint formats are internal engine data, not a portable archival format.

## Render queue in the editor

**File → Render image** (`Ctrl+R`) creates an immutable snapshot of the selected
flame and output settings. The render settings show the planned memory and
disk allowance before a job is added. Editing the document afterward does not
change an existing queued job. The queue renders one job at a time, while the
editor remains available for document work.

The queue window provides **Start / resume**, pause, cancel, retry/resume,
restart, ordering and removal controls. Progress identifies the current
sampling or reconstruction phase. Phase ETA is calculated only from measured
progress in that phase; it is not a hardware-independent preflight prediction.
Completion reports the actual backend/device and any fallback reason.

The queue persists job snapshots and checkpoint locations. A restored queue
waits for **Start / resume** rather than automatically consuming resources on
application launch. Paused jobs with valid checkpoints continue their saved
sampling. **Start selected job over** creates fresh sampling work; use it for a
job whose prior checkpoint is incompatible. Closing the editor requests a
pause and waits asynchronously for the worker to return. Canceling or failing
a job does not publish a partial destination.

Exported queue JSON contains snapshots and absolute file/checkpoint paths; it
does not bundle the sampling data. Removing a job or starting it over retains
the old checkpoints and output. See [RENDER_QUEUE.md](RENDER_QUEUE.md) for the
queue persistence, restore, close and storage-management behavior.

## Command exit status and completion reporting

| Exit status | Meaning |
| --- | --- |
| `0` | Completed export, successful estimate, or successful informational command |
| `1` | Validation, storage, backend, checkpoint or render failure |
| `2` | Required render input/output arguments were omitted |
| `75` | Requested pause reached a usable committed checkpoint |
| `130`, `143` | SIGINT / SIGTERM interruption |

Progress goes to standard error. Successful export writes a completion line to
standard output retaining `samples`, `plotted`, `invalid_orbit_steps`, `workers`,
`seed`, `elapsed_ms`, `backend`, `device`, `sampling_ms` and
`reconstruction_ms`; it adds `format`, `alpha` and `resumed`. Reported timing
covers the current invocation, including checkpoint validation where relevant.
It is not the accumulated wall time of previous processes.

`tests/file_cli.cmake` exercises no-write planning, high-depth format selection,
memory/disk rejection, separate-process pause/resume equality, complete
checkpoint reconstruction, corrupt data and incompatible settings. The
independent codec tests and external decoding evidence are described in
[IMAGE_FORMATS.md](IMAGE_FORMATS.md). Full-resolution execution results belong
in [TEST_PLAN.md](TEST_PLAN.md), separately from smaller deterministic tests.

## Remaining output work

JPEG export, floating-point OpenEXR, ICC/profile-based color management and
dithering remain on the project backlog. This milestone provides BMP/PPM,
PNG/TIFF with true 16-bit integer channels, and PNG/TIFF straight alpha. Those
formats do not imply completion of every final-product export feature.
