# Windows Vulkan verification — 0.6.0

The portable application includes the Vulkan compute backend, its compiled shader and the existing CPU renderer. The Vulkan loader and graphics driver come from the Windows installation. The application loads the system `vulkan-1.dll` dynamically; it does not bundle a replacement loader or require the Vulkan SDK to run. CPU operation remains available when the loader or a suitable GPU is absent.

The distributed Windows executables are cross-compiled and their DLL imports audited during checkpoint preparation. Running them on Windows 11 and measuring physical GPU performance are separate acceptance checks. The development host's Vulkan execution uses a software device and is reported separately in [GPU_KERNEL.md](GPU_KERNEL.md) and [GPU_TESTING.md](GPU_TESTING.md).

## Normal desktop checks

Extract the complete ZIP, including its DLLs, Qt plugin folders, examples and tests. From Git Bash in the extracted project directory, run:

```bash
bash scripts/verify-editor-windows.sh
```

This runs the existing CPU, legacy, creative and native Qt tests. It also checks portable compute packing/random streams on the CPU, records Vulkan device discovery, verifies an explicit missing-device CPU fallback, and renders the creative smoke workflow with the CPU backend. A Vulkan-capable GPU is not required by this script.

Logs are saved as `.txt` files under `logs`. Screenshots, saved flames and device inventories go into a unique directory under `out`. The script opens that directory in Explorer when it succeeds; `--no-open` suppresses the final Explorer window. The scripts locate the project relative to their own file paths, so they also work when invoked by absolute path from another current directory.

## Discover devices

The CLI reports the available CPU renderer and any discovered Vulkan devices:

```bash
./bin/windows-x64/flame-cpu.exe --list-devices
./bin/windows-x64/flame-cpu.exe --list-devices --json
```

Each Vulkan entry includes its discovery index and ID, name, type, software flag, API and driver versions, device-memory total, maximum storage-buffer range, numeric capabilities and current-kernel eligibility. Reported memory is the advertised device memory, not a live estimate of currently free memory. Discovery can succeed and report no usable GPU; it does not imply that a Vulkan render has executed.

The editor exposes the same selection through Preferences and Render Image. Device selection and the corresponding renderer/memory/fallback preferences persist between normal sessions. The actual renderer and any fallback reason are shown after a render.

## Strict Vulkan execution check

When a compatible GPU and driver are available, run the dedicated check:

```bash
bash scripts/verify-gpu-windows.sh
```

The script records a JSON device inventory and uses Windows PowerShell to choose the first usable physical GPU in that inventory. It passes that same discovery ID to both numerical test programs, the CLI render and the desktop smoke workflow. If PowerShell is unavailable, supply a device explicitly. If no usable physical GPU is found, the default strict check fails instead of reporting a CPU render as a GPU success.

Choose a particular device by its zero-based index or discovery ID:

```bash
bash scripts/verify-gpu-windows.sh --device 0
```

An explicitly selected software Vulkan device can be tested too. Its software status remains in the inventory and output report; such a result verifies compute execution without establishing physical GPU acceleration.

The optional GPU memory budget and Explorer behavior can be selected together:

```bash
bash scripts/verify-gpu-windows.sh --device 0 --gpu-memory-mib 256 --no-open
```

The numerical test fixtures use their own small budgets to exercise resource limits. The supplied `--gpu-memory-mib` value controls the example CLI render and desktop smoke workflow.

### What the strict script requires

The script runs the 1,560 direct transform probes and the GPU integration suite, renders Ember Spiral through the CLI with `--backend vulkan --no-fallback`, and launches the native editor's `--gpu-smoke-test`. A missing device, a skipped GPU suite, a failed comparison, a Vulkan error or a CPU fallback fails this gate.

The desktop smoke mode waits for an actual Vulkan preview, verifies that its backend is Vulkan with no fallback reason, then saves the main window, preview, original flame, render-device report and renderer settings screenshot. The complete required output set is:

| File | Evidence |
| --- | --- |
| `devices.json` | Available devices and capabilities before execution |
| `vulkan-ember.bmp` | CLI image produced by strict Vulkan rendering |
| `editor.png` | Running native editor with its preview |
| `preview.png` | Preview image saved by the native editor |
| `saved-example.flame` | Source-preserving saved example |
| `render-device.json` | Actual desktop backend, device name and fallback reason |
| `render-settings.png` | Native renderer preferences and device selection |

The command log records the OS version, test results, actual devices, CLI sampling/reconstruction times and executable/output hashes. Interpret timings as results for that device, scene and sample budget. They are not a claim of a universal speedup over the CPU renderer.

## Direct rendering and fallback

An ordinary command can allow recovery to the CPU:

```bash
./bin/windows-x64/flame-cpu.exe --flame examples/ember-spiral.flame \
  --output ember-auto.bmp --backend auto
```

To require a specific Vulkan device and report any failure:

```bash
./bin/windows-x64/flame-cpu.exe --flame examples/ember-spiral.flame \
  --output ember-vulkan.bmp --backend vulkan --device 0 --no-fallback
```

The compute path currently uses FP32 sampling and the existing CPU image reconstruction. The supported model/range limits, deterministic compute streams and expected CPU/device differences are documented in [GPU_KERNEL.md](GPU_KERNEL.md). CUDA, tiled high-resolution rendering and 16K acceptance remain separate work.
