# Animation CLI and image sequences

Milestone 9 adds native animation clips, explicit transform correspondence,
camera/palette presets, timed flame-library import, evaluated frame export and
resumable image sequences. `flame-cpu animation --help` lists the commands.
The CLI exports image sequences. To encode them as MP4, open **Tools → Compose image sequences…** in the desktop editor and select the first numbered PNG. [Composer guide](RELEASE_0.17.0.md#compose-and-export)

An animation belongs to a native `.apoproject`. The original flame collection,
its source encoding, the selected entry and unknown project-envelope fields
remain preserved. Each keyframe also carries its exact source collection and
selected entry. Rendering and `.flame` frame export operate on evaluated models;
they do not replace the source collection with a normalized frame.
See [the animation model](ANIMATION_MODEL.md) for schema/resource limits and
[morphing policies](MORPHING.md) for interpolation and correspondence details.

## Create and inspect a clip

Run these commands from the repository or extracted project root. They use
`./build/flame-cpu` for a native source build; in Git Bash with the Windows
package, replace that executable path with `./bin/windows-x64/flame-cpu.exe`.

```bash
./build/flame-cpu animation create examples/ember-spiral.flame \
  --preset camera-orbit --frames 121 --fps 30 \
  --seed 18446744073709551615 --output orbit.apoproject

./build/flame-cpu animation inspect orbit.apoproject
./build/flame-cpu animation inspect orbit.apoproject --json
```

`animation preset INPUT --preset NAME --output FILE` is an explicit alias for
the preset creation workflow. The four presets are `still`, `camera-orbit`,
`zoom` and `palette-cycle`. A still clip has one held keyframe; other presets
capture their motion endpoints or intermediate palette keys. Presets can set
appropriate loop/angle policies; explicitly supplied clip options override them.

Source `--index`, `--from-index` and `--to-index` values start at **1**. An omitted
index uses the native project's selected entry, or the first entry of a plain
flame library. Animation frame numbers start at **0**. A clip with `--frames 121`
contains frames 0 through 120. `--fps 30000/1001` stores an exact rational rate;
`--fps 24` means 24/1. Seeds accept the entire unsigned 64-bit range and are JSON
strings in inspection output so readers do not lose integer precision.

Creation supports these clip options:

| Option | Values / behavior |
| --- | --- |
| `--name TEXT` | Clip label |
| `--frames N` | 1 through 1,000,000 representable frames |
| `--fps N[/D]` | Positive rational frame rate, validated by the animation model |
| `--seed N` | Full unsigned 64-bit seed; default 123456 |
| `--seed-mode fixed` | Same sampling seed at every frame; default, reduces random temporal changes |
| `--seed-mode per-frame` | Deterministic seed derived from the clip seed and global frame index |
| `--loop once`, `repeat`, `ping-pong` | Evaluation outside the clip's ordinary range |
| `--easing hold`, `linear`, `smoothstep`, `smootherstep` | Outgoing interpolation of each captured key |
| `--affine polar` or `linear` | Affine interpolation policy |
| `--palette rgb`, `linear-rgb`, `hsv-shortest`, `hsl-shortest` | Explicit palette interpolation space |
| `--angle shortest` or `unwrapped` | Camera angle interpolation; unwrapped permits full turns |
| `--topology strict` or `fade-unmatched` | Explicit handling of mapped unmatched transforms |

Native CLI edits write a **distinct `.apoproject` destination**. Existing output
files require `--overwrite`. Creating/importing a new clip from a project that
already contains animation data additionally requires `--replace-animation`.
The two flags answer different questions: replacing a file and replacing its
animation payload. Unknown/future animation payloads inspect as unsupported
opaque data and survive ordinary project saves unchanged. They are never
silently converted into a supported empty timeline.

## Add keys and create a mapped morph

```bash
./build/flame-cpu animation add orbit.apoproject \
  --flame examples/animation-morph-target.flame --index 1 --at 60 --name 'Middle pose' \
  --ids transform-1,transform-2,transform-3 --easing smoothstep --output edited.apoproject

./build/flame-cpu animation morph examples/ember-spiral.flame \
  --to examples/animation-morph-target.flame \
  --from-ids spiral,julia,petal --to-ids spiral,julia,petal \
  --frames 121 --fps 30 --easing smootherstep --palette linear-rgb \
  --output morph.apoproject
```

ID lists contain one comma-separated identity per ordinary transform in source
order. Matching IDs explicitly declare correspondence, including reordered
branches. Omitting IDs chooses positional pairing for equal transform counts;
this is not an automatic geometric matcher. Different transform counts require
explicit lists at both morph endpoints and an appropriate topology policy.
`fade-unmatched` permits deliberately unmatched IDs to fade in/out. Unsupported
producer combinations, singular/interpolated models and incompatible native
variation identities report a diagnostic instead of silently changing the
meaning of a transform.

The add example reuses the three IDs assigned by the earlier orbit preset.
For another clip, `animation inspect PROJECT --json` shows its existing IDs;
provide the matching IDs in the new source's transform order.

`animation add` keeps the existing clip policies. It accepts `--frames N` to
extend/change its frame count, `--topology` to make a topology decision explicit,
and `--easing` for the newly captured key's outgoing segment. Inserting at an
occupied position requires `--replace-keyframe`. A `hold` segment keeps its left
model until the right endpoint and can represent an explicit hard cut.

All captures are validated before the destination project is written. Complete
interpolated-frame validation happens when the export range is preflighted.
A source-native plugin must be explicitly enabled before capture or evaluation:

```bash
./build/flame-cpu --plugin trusted/package.apoplugin animation create plugin.flame \
  --frames 61 --output plugin-animation.apoproject
```

Project loading and `animation inspect` never enable native code. Captured module
IDs, versions and SHA-256 fingerprints must match the enabled modules. Moving to
a different binary/platform requires an explicit recapture before a new export.

## Import timed legacy collections

```bash
./build/flame-cpu animation import timed-library.flame \
  --time-units frames --fps 30 --output imported.apoproject

./build/flame-cpu animation import timed-library.flame \
  --time-units seconds --fps 30000/1001 --output imported-seconds.apoproject
```

The selected unit interprets each supported flame's stored `time` value. Seconds
are converted using the rational frame rate and rounded to the nearest frame.
The library must begin at zero and contain ordered, distinct converted frame
positions. Negative/nonfinite times, rounded duplicates and inconsistent
positional topology fail. Missing time attributes use the legacy model's zero
default and consequently cannot silently establish additional timed keys.
The collection's full source bytes remain authoritative in the saved project.

This is an explicit import of supported time metadata, including compatible AV
collections. It does not claim exact historical AV motion/interpolation behavior
or support every legacy animation container. Reborn uses the chosen interpolation
policies, which remain inspectable in the native timeline.

## Export one evaluated frame

```bash
./build/flame-cpu animation frame orbit.apoproject --frame 60 --output middle.flame

./build/flame-cpu animation frame orbit.apoproject --frame 60 \
  --width 1920 --height 1080 --samples 10000000 --threads 4 \
  --format png16 --alpha --output middle.png
```

A `.flame` output is an explicit canonical UTF-8 snapshot. Image outputs use the
same file renderer and reconstruction pipeline as normal high-resolution
renders. The clip determines the frame's seed. Both `--width` and `--height` must
be supplied together. Resizing uses a common fit policy: camera scale is
multiplied by the smaller of the horizontal and vertical scale ratios.
For images, an omitted `--format` is inferred from the filename at 8-bit depth.
An explicit format must agree with the extension. Image-only controls are rejected
for `.flame` output so they cannot appear to have been applied.

## Preflight and render an image sequence

Create the output directory first. Export ranges are inclusive and use the
clip's ordinary zero-based frame domain, regardless of its playback loop policy.

```bash
mkdir -p frames
./build/flame-cpu animation render orbit.apoproject --output-dir frames \
  --first 0 --last 120 --step 1 --prefix orbit \
  --width 1920 --height 1080 --samples 10000000 \
  --threads 4 --memory-mib 512 --disk-mib 16384 \
  --format png16 --alpha --estimate

./build/flame-cpu animation render orbit.apoproject --output-dir frames \
  --first 0 --last 120 --step 1 --prefix orbit \
  --width 1920 --height 1080 --samples 10000000 \
  --threads 4 --memory-mib 512 --disk-mib 16384 \
  --format png16 --alpha
```

The filenames are `orbit_000000.png` through `orbit_000120.png`, using at least
six digits and the **global frame index**, including when a batch begins later
in the clip. A stepped range selects `first + k*step <= last`; an off-step final
endpoint is not added. The prefix accepts 1–96 ASCII letters, digits, underscores
or hyphens. It cannot supply a path. Output filenames use the format's canonical
extension (`.tiff` for TIFF).

A batch admits at most **1,000 selected frames**. Long clips remain representable;
choose explicit ranges and separate state directories/prefixes for subsequent
batches. Nothing is silently truncated. For example, frames 1000–1999 retain
those frame numbers in their filenames. The default omitted range is the whole
clip and therefore fails explicitly for a clip longer than the batch limit.

Each batch uses one fixed canvas. Without overrides, it uses the first selected
frame's dimensions and fits every evaluated model to them. Complete preflight
checks each selected model, image encoding, renderer resource bounds, destination
collisions, portable filenames, protected inputs/modules and sequence state.
`--estimate` also verifies resumed completed images and creates no files or
folders. A missing module or invalid interpolated frame prevents the batch from
starting. Device/model compatibility is checked where it can be determined
without dispatch; a runtime driver/device failure may still stop a later frame.

Image formats are `png8`, `png16`, `tiff8`, `tiff16`, `ppm8`, `ppm16` and `bmp8`.
Transparency uses `--alpha` with PNG or TIFF. CPU is the default renderer.
`--backend auto|vulkan`, `--device ID`, `--gpu-memory-mib N` and
`--no-cpu-fallback` use the existing explicit backend policy. Native variation
callbacks use the CPU path. Actual backend/device information is recorded per
rendered frame in the sequence manifest.

`--memory-mib` controls renderer working buffers; it is not a whole-process RSS
cap. `--disk-mib` admits the entire selected batch's conservative final-image
bound plus peak active checkpoint/staging storage and manifest allowance.
Completed frame checkpoints are removed after durable publication. The Qt queue
retains individual job checkpoints and applies its own aggregate planning.
Different filesystem volumes can still exhaust their actual free space; I/O
failures retain earlier completed outputs and report the failing operation.

## Pause, cancellation and verified resume

The default private state directory is `frames/.apo-sequence-orbit`. Choose
another private directory with `--state-dir DIR`. It contains the sequence
manifest, process lock, current image staging and per-frame render checkpoints.
Keep it host-owned: do not mix source documents or unrelated files into it.
The following pause example writes a five-frame batch in a new `paused-frames`
folder, with its own default sequence state.

```bash
mkdir -p paused-frames
# A reproducible pause after two newly published images. Exit status is 75.
./build/flame-cpu animation render orbit.apoproject --output-dir paused-frames --prefix orbit \
  --first 0 --last 4 --samples 1000000 --pause-after-frames 2

# Reuse the same clip, renderer binary, range, prefix, canvas and render settings.
./build/flame-cpu animation render orbit.apoproject --output-dir paused-frames --prefix orbit \
  --first 0 --last 4 --samples 1000000 --resume
```

Use `--pause-after-samples N` to pause after committed sampling work within the
active frame. Remove the pause flag on resume. Ctrl+C/SIGINT and SIGTERM request
cooperative cancellation and return 130 and 143 respectively. Explicit pause
returns 75. A pause after the final selected frame is a successful completion.
The host releases process locks on normal exit, cancellation and OS termination.

The manifest binds exact clip/source snapshots, per-frame model and native module
identities, chosen seeds, frame list, destination names, canvas, image settings,
backend policy and the renderer executable SHA-256 on Windows/Linux. Vulkan
inventory/driver identity and resource ceilings that can affect GPU fallback are
also bound. A rebuild/upgrade or changed module requires a fresh sequence state;
changing the clip/name/range is not a resume operation. CPU thread count and
memory allowance, and the disk allowance, may change; an existing per-frame
checkpoint still enforces its recorded reconstruction geometry.

Resume first verifies **every completed frame's size and SHA-256**. A missing or
changed image fails before new output is published. Existing uncompleted targets
must still match their original preflight state, including explicitly authorized
old files. There is no blind `skip existing` mode. A changed/missing output,
corrupted manifest or incompatible render checkpoint is diagnosed; keep the
original files for review and start a new output/state location if the original
sequence cannot be restored.

Each image is rendered to private staging, durably recorded as ready, atomically
published, then durably recorded as completed. This also recovers the interruption
window between image publication and the completed journal update. A frame is
skipped only after its published bytes are verified. The host checks destination
changes again immediately before publication. Images are **individually atomic**;
the batch is not an all-files transaction. A later failure/cancellation leaves
earlier successful images available and preserves unrendered prior destinations.
