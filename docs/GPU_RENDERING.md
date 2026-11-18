# CPU and Vulkan rendering — 0.7.0

The desktop editor and command-line renderer can select the existing CPU reference path or the new Vulkan compute path. Vulkan performs transform selection, orbit calculations, camera projection, palette/opacity sampling and histogram accumulation. Orbit calculations use 32-bit floating point, while histogram totals use 64-bit integers. The existing CPU reconstruction applies density scaling, spatial filtering, gamma, brightness, vibrancy, background and RGB image output.

The CPU reference continues to use 64-bit calculations and its existing random stream. A matching flame and seed do not imply matching pixels between these paths: the Vulkan sampler has a different stream, and floating-point rounding can change a chaotic orbit. Exact cross-device pixel identity is not guaranteed. The verification report separates numerical comparisons, software Vulkan execution and physical GPU acceptance.

## Choose a renderer in the editor

Open **Edit → Preferences…**. The dialog contains preview quality and system-memory settings together with these rendering controls:

| Control | Behavior |
| --- | --- |
| CPU (reference) | Uses the existing double-precision CPU renderer. It does not require a Vulkan device. |
| Automatic (GPU when available) | Uses the preferred device when explicitly selected. With automatic device selection, it chooses usable hardware Vulkan and skips software implementations. |
| Vulkan (FP32 sampling) | Explicitly requests Vulkan, including software Vulkan for validation. |
| Preferred device | Chooses automatic selection or a discovered device. Saved choices use the reported device ID. An unavailable saved device remains visible so it can be replaced deliberately. |
| GPU working budget | Limits the memory assigned to a Vulkan render. Device buffer-size limits are checked separately. |
| Use CPU if Vulkan cannot render this flame | Allows a reported fallback when the selected device, model or memory configuration cannot use Vulkan. Disable this to require successful Vulkan execution. |

The capability panel reports the selected device's type, Vulkan version, device memory, maximum storage-buffer size and FP64/Int64 capabilities. These reported capabilities describe the device; this checkpoint's Vulkan orbit kernel still uses FP32. A software device is labeled explicitly and does not represent physical GPU acceleration. Memory capacity is a reported heap capacity, not a promise that all of it is free for the current render.

Renderer, preferred-device, GPU-budget and fallback choices persist between editor sessions. A new application's saved-settings path defaults to Automatic with CPU fallback enabled. The command-line renderer defaults to CPU. Existing flame XML does not store or change the machine-specific device preference.

**Render image…** starts from the same device preferences and allows a different backend, device, budget or fallback choice for that export. Those per-export changes do not replace the saved preview preferences. Gradient, Adjustment, Mutation and Random previews inherit the selected backend, device and budgets while retaining their smaller preview sample limits.

## Read the execution result

The main preview shows the actual backend, with CPU worker count or **Vulkan FP32**. Its tooltip identifies the device and any fallback reason. The Activity panel records backend changes and file-render execution. A fallback is labeled **CPU fallback** rather than presented as a successful GPU render.

The creative dialogs also report the selected preview's execution path; candidate thumbnail tooltips identify their backend and fallback reason. For example, a batch may contain one candidate that requires CPU fallback even when other candidates can use Vulkan.

Disabling fallback makes a capability or device error visible. A failed file render does not replace an existing image. Canceling a render prevents its staged output from replacing the destination. Vulkan cancellation is checked around bounded submissions; it cannot interrupt instructions already executing in a submitted dispatch.

## Discover devices from Git Bash

Run from the extracted package directory. Keep its executable and DLL directories together.

```bash
./bin/windows-x64/flame-cpu.exe --list-devices
./bin/windows-x64/flame-cpu.exe --list-devices --json
```

The text report includes the CPU reference path, whether Vulkan was compiled, whether its loader is available, and the discovered devices. Each device has a zero-based display index and an ID, device/driver versions, memory and storage limits, availability and capability fields. The JSON report exposes `compiled`, `loader_available`, `diagnostic` and `devices`; device records additionally identify software implementations.

An empty list or unavailable loader does not prevent CPU rendering. A Vulkan device can be enumerated yet fail an allocation or a particular model's support checks; the final render result remains the authority for which backend actually ran.

## Render from the command line

```bash
./bin/windows-x64/flame-cpu.exe --flame examples/ember-spiral.flame \
  --output ember-cpu.bmp --backend cpu --samples 2000000

./bin/windows-x64/flame-cpu.exe --flame examples/ember-spiral.flame \
  --output ember-auto.bmp --backend auto --samples 2000000

./bin/windows-x64/flame-cpu.exe --flame examples/ember-spiral.flame \
  --output ember-vulkan.bmp --backend vulkan --device 0 \
  --gpu-memory-mib 512 --memory-mib 512 --samples 2000000 --no-fallback
```

Replace `0` with an index or device ID from your own discovery output. Index order can change after a driver or hardware change; the preferred ID is more suitable for a stored setting. `--threads` controls CPU work and does not set a Vulkan workgroup count. `--memory-mib` and `--gpu-memory-mib` are separate system and GPU working budgets.

The completion line reports `backend`, `device`, `sampling_ms` and `reconstruction_ms` alongside the existing dimensions, sample count, plotted samples, invalid steps, worker count, seed and elapsed time. A fallback adds its reason on a separate line. Compare timings only with the same flame, dimensions, sampling budget, filtering and machine; software Vulkan is not a hardware-performance result.

The native editor accepts the same `--backend`, `--device`, `--gpu-memory-mib` and `--no-fallback` overrides for that launch. Without these overrides it uses the saved preferences.

## Strict native Vulkan smoke check

The following mode requires a Vulkan preview and disables CPU fallback. It is suitable for an explicitly selected software implementation during development or a physical device during platform acceptance.

```bash
./bin/windows-x64/apophysis-reborn.exe --gpu-smoke-test out/gpu-smoke --device 0
```

The mode opens the actual native editor, renders its example, and writes:

| File | Contents |
| --- | --- |
| `editor.png` | Main window with the rendered preview and backend status |
| `preview.png` | The rendered preview image |
| `saved-example.flame` | Saved example definition |
| `render-device.json` | Actual backend, device name and fallback reason |
| `render-settings.png` | Native renderer preferences and capability display |

It exits unsuccessfully if Vulkan was not the actual backend, a preview fails, an output cannot be written, or the 120-second timeout expires. The existing `--smoke-test` and `--creative-smoke-test` modes remain available. Only one smoke-test mode may be selected for a launch.

## Scope

Vulkan is a compute sampling backend with shared CPU reconstruction. CUDA is not included. Preview rendering retains dense-buffer limits. Version 0.7.0 file rendering uses bounded global-coordinate hit buffers and disk-backed strip reconstruction, with a persistent queue, checkpoints and PNG/TIFF16 output. The file path does not require a full GPU histogram. [GPU streaming](GPU_STREAMING.md) · [High-resolution controls](HIGH_RES_RENDERING.md)

See [TEST_PLAN.md](TEST_PLAN.md) for executed results and remaining platform acceptance, [CREATIVE_TOOLS.md](CREATIVE_TOOLS.md) for the creative workflow, and [VARIATIONS.md](VARIATIONS.md) for the CPU variation definitions.

## Checkpointed GPU file jobs in 0.7.0

The file renderer uses absolute compute-chain ranges and the same global camera and shader formulas as dense sampling. Completed hit chunks are committed before progress is published. If Vulkan cannot start and fallback is enabled, the result records CPU and its reason. Once GPU samples have committed, resume requires the saved device, driver and shader identity; a later error preserves the checkpoint instead of mixing CPU samples into it. Resource budgets and CPU worker count may change when the saved geometry still fits. The queue records the actual backend/device throughout the job.

`gpu-stream-tests` checks exact dense/stream hits and counts, split ranges and bounded global 16K geometry. `file-render-tests --require-vulkan --device ID` checks streamed pixels and GPU resume on the selected device. Those executed here on software Vulkan. Full 16K output acceptance was separately CPU-based; see [HIGH_RES_ACCEPTANCE.md](HIGH_RES_ACCEPTANCE.md). The ordinary and strict Windows scripts include the relevant new tests.
