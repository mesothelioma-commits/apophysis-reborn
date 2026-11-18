# Renderer benchmarks — Milestone 10

`scripts/benchmark.py` measures the actual CLI executable on the machine running it. It produces reproducible workload descriptions, process timings, memory observations, output hashes and the renderer's own counters. It uses only Python's standard library; Python **3.10 or newer** is required. The native application itself does not require Python.

The benchmark is a separate measurement workflow. It does not turn an untested Windows or physical GPU configuration into an accepted platform, and it does not predict finished-image render quality from a sparse test image. Numerical, image-codec, checkpoint and editor regressions remain in the ordinary test suite.

## Run it

The wrapper works from any current directory. In Git Bash, use the script in the extracted release and point `--exe` at its Windows CLI. If the package has `bin/windows-x64/flame-cpu.exe`, the executable can be discovered automatically:

```bash
bash /path/to/apophysis-reborn/scripts/benchmark.sh --backend cpu
```

For an explicit native or Windows build:

```bash
bash /path/to/apophysis-reborn/scripts/benchmark.sh \
  --exe /path/to/build/flame-cpu \
  --backend cpu --threads 1,2,4 --repeats 3 --warmups 1 --no-open
```

On Windows, use `flame-cpu.exe` and Git Bash paths or ordinary quoted Windows paths. The wrapper looks for `python3`, `py -3`, then `python`, and verifies the interpreter version. The Python entry point accepts the same options directly.

To measure both CPU and Vulkan, the default Vulkan selection is the first usable **physical** device. A missing/unusable physical device causes failure rather than a CPU fallback or an automatic software-device substitution:

```bash
bash /path/to/apophysis-reborn/scripts/benchmark.sh \
  --exe /path/to/flame-cpu.exe --backend both --no-open
```

Choose a particular discovery ID or index when comparing devices. Explicit selection also permits a software Vulkan implementation, whose classification remains visible throughout the report:

```bash
/path/to/flame-cpu.exe --list-devices --json

bash /path/to/apophysis-reborn/scripts/benchmark.sh \
  --exe /path/to/flame-cpu.exe --backend both --device 0 --no-open
```

Vulkan cases pass the CLI's strict no-fallback flag. The benchmark then checks the completed backend and device name against the selected inventory entry, and verifies that the device/driver identity is unchanged at the end. A CPU result cannot count as a Vulkan measurement. Software Vulkan runs measure a software driver on the CPU; they establish no physical GPU speedup.

## Workload profile v1

The fixed source is [`benchmarks/ember-v1.flame`](../benchmarks/ember-v1.flame). The report records its SHA-256 and the generated three-frame orbit project's SHA-256. Every case uses the fixed seed **123456** and fresh processes/output names.

| Case | Configuration | Purpose |
| --- | --- | --- |
| `cpu_sampling_tN` | 512 × 384, PNG8 RGB, oversample 1, filter radius 0.5, default 2,097,152 samples | End-to-end CPU work at each requested worker count |
| `vulkan_sampling_t1` | Same image/samples, explicitly selected Vulkan device, strict no fallback | Observed Vulkan sampling plus CPU reconstruction and file output |
| `cpu_stream_2048` / `vulkan_stream_2048` | 2048 × 2048, PNG16 RGBA, 32-row strips, 128 MiB renderer working-buffer budget, default sample count | Larger streamed reconstruction and direct 16-bit output |
| `cpu_animation_3` / `vulkan_animation_3` | Three 320 × 240 PNG16 RGBA orbit frames, up to 262,144 samples per frame, one sequence invocation | Evaluator, sequence planning, per-frame rendering and durable publication overhead |
| Optional `*_stream_16k` | 16,384 × 16,384 PNG16 RGBA, 32-row strips, 128 MiB renderer buffer budget, 8192 MiB disk allowance | One current large-output observation per selected backend |

CPU defaults use distinct worker counts from 1, 2 and 4, limited to the reported logical-processor count. `--threads` chooses explicit values. The completed renderer's actual worker count is retained separately: Vulkan reports zero CPU sampling workers. The larger CPU and animation cases use the largest requested CPU worker count.

The normal default is **one warmup and three measured repetitions** of every case. Each repetition launches a fresh CLI process; a warmup can warm filesystem/driver caches but does not reuse an in-process renderer or Vulkan context. No image files, manifests or sample checkpoints are reused between measured repetitions. All cases are preflighted before the first timed render.

The 16K case is opt-in because it can take substantially longer and produce a large temporary image. It always runs **once per selected backend, without warmups**, irrespective of the ordinary repetition settings:

```bash
bash /path/to/apophysis-reborn/scripts/benchmark.sh \
  --exe /path/to/flame-cpu.exe --backend cpu --large-16k --no-open
```

Adding `--keep-images` retains every rendered image, including warmups and any 16K output. Without it, the runner records the complete file size/SHA-256 and expected PNG header, then removes only the known images it produced inside its private new run directory. Sequence manifests and logs remain as evidence; the retained sequence state is not a resumable image collection after its images have been removed. The benchmark checks headers and hashes, not an independent full PNG pixel decode. Use the release's high-resolution acceptance workflow when that additional validation is required.

## Options and limits

| Option | Default and behavior |
| --- | --- |
| `--exe PATH` | Discover the packaged Windows CLI or a conventional local `build` output; otherwise require an explicit path |
| `--backend cpu`, `vulkan`, `both` | `cpu` |
| `--device ID` | First usable physical Vulkan device; an explicit ID/index may select software Vulkan |
| `--output-dir NEW_DIR` | A unique directory under the project's `out` folder; an explicitly named parent must already exist |
| `--samples N` | 2,097,152; admitted range 1–100,000,000. Animation uses `min(262144, max(1, N/8))` samples per frame, with integer division |
| `--threads 1,2,4` | CPU defaults described above; up to eight distinct values in 1–64 |
| `--repeats N` | 3; admitted range 1–10 |
| `--warmups N` | 1; admitted range 0–3 |
| `--gpu-memory-mib N` | 256; admitted range 16–65,536 |
| `--timeout N` | 120 seconds per ordinary process; admitted range 1–3600. Optional 16K runs get at least 600 seconds |
| `--large-16k` | Add the explicit, single-observation large-image cases |
| `--keep-images` | Retain images rather than only their size/hash/header evidence |
| `--no-open` | Do not select `report.txt` in Windows Explorer |

Existing output directories are refused, even if empty. The runner does not replace or recursively delete a prior report. Symlinks, junctions and reparse-point paths are refused; benchmark outputs cannot overlap protected inputs. Commands are passed as argument arrays with `shell=False`, so spaces and shell metacharacters in filenames are not interpreted as commands. The new report directory and its run directories are created privately where the host supports POSIX permissions.

An invalid option/path fails before a report is admitted. After admission, a failed subprocess, timeout, changed binary/input, altered output, changed device or malformed measurement produces a **FAIL** report and a nonzero exit status. Cancellation returns 130. A failure does not overwrite unrelated files. Per-process logs are capped at 4 MiB each; an overflow fails the case and records truncation.

## Reading the results

The new directory contains:

- **`report.txt`** — readable results, timing/memory boundaries and CPU scaling observations.
- **`report.json`** — schema `apophysis-reborn.benchmark.v1`, workload settings, build/source/compiler identity when supported, executable/source hashes, host CPU/OS/Python information, device inventory, all repetitions and summaries.
- **Per-run `command.json`, `stdout.txt`, `stderr.txt`, `process.json`** — exact argument list, original CLI evidence, process outcome, wall time and memory method.
- **Preflight and animation-source records** — planner output and the exact generated orbit project.

The report includes local executable/output paths in the recorded command lists. It does not upload results, collect credentials, enumerate unrelated files or dump the environment. Sharing the report is a separate user action.

### Timing

The headline median is measured by `time.perf_counter()` from process launch through completion. It includes executable startup, flame/project parsing, backend initialization, sampling, reconstruction, encoding, filesystem writes and the observer's polling/scheduling delay. The process completion polling interval is 5 ms. Output hashing, report writing and per-case preflight are outside that timed interval.

For individual images, the report additionally retains the renderer's internal sampling and reconstruction milliseconds, plotted samples, invalid orbit steps, actual worker count, backend and device. Sequence manifests provide verified publication, image and backend identities; they do not provide per-frame plotted counters, so those are not invented for the sequence case.

The summaries report minimum, median/p50, maximum and mean wall time. Requested-sample throughput is **requested samples divided by median total-process time**; it is not pure kernel throughput or a quality score. CPU scaling ratios use the one-worker median and require byte-identical encoded outputs across the requested CPU worker counts. No CPU/Vulkan speedup ratio is generated: their numerical precision and sampling streams differ. Timings have no brittle minimum-speed pass threshold.

All repeated identical cases, including warmups, must produce identical encoded image hashes. A one-observation 16K case cannot establish repeatability by itself; its `measured_repetitions` records that limit explicitly.

### Memory

On Linux, `os.wait4` obtains the specific child's resource record; `ru_maxrss` is converted from KiB to bytes. The runner does not use the cumulative maximum from `RUSAGE_CHILDREN`, which could incorrectly carry an earlier large process's peak into a later small run. On Windows, `GetProcessMemoryInfo` supplies `PeakWorkingSetSize` in bytes from the child process handle. If the post-exit counter cannot be obtained, an earlier observed kernel peak is retained with `memory_peak_complete_lifetime: false`; polling may have missed the final peak. When no supported counter can be obtained, the JSON value is `null` with a reason; it is not reported as zero memory use. Darwin's per-child resource maximum uses its native byte unit, but macOS execution is not established by Linux/Windows records.

These resident-memory values cover the CLI process, including runtime/driver mappings and overhead. They are not GPU VRAM measurements and are not equivalent to the 128 MiB renderer working-buffer budget. The report identifies OS/architecture, CPU model, logical/affinity processor counts and exposed cgroup limits so constrained or shared-host results are not mistaken for an unconstrained workstation. Cache state, background load, thermals and power policy can still affect results; close competing work when measuring your own hardware.

Primary API references: [Python `os.wait4`](https://docs.python.org/3/library/os.html#os.wait4), [Linux resource-usage units](https://man7.org/linux/man-pages/man2/getrusage.2.html), [Windows process-memory counters](https://learn.microsoft.com/en-us/windows/win32/api/psapi/ns-psapi-process_memory_counters), and [Python subprocess argument handling](https://docs.python.org/3/library/subprocess.html).

## Release evidence and remaining acceptance

The release's `verification` directory identifies the exact executable and source used for any included benchmark report. A report made during development against an earlier executable is development evidence, even if the current runner created it. The final release report must be rerun against the final build with other compiler/benchmark jobs stopped, and its executable SHA-256 must match that build.

Linux CPU and software-Vulkan results do not complete Windows 11 runtime/DPI checks or NVIDIA/AMD/Intel physical-GPU acceptance. The retained M7 large-image performance measurements remain historical M7 evidence. Current optional 16K benchmark records have their own executable, source, settings and timing identity; they are not substituted for those earlier measurements or silently labeled as finished-quality rendering.

The focused runner regressions execute with:

```bash
python3 tests/benchmark_tests.py
```

They cover measurement parsing, explicit software-device selection, refusal to count fallback as Vulkan, budget-plan validation, warmup exclusion and median aggregation, changed/malformed outputs, safe path admission, literal subprocess arguments, timeout reporting and independent per-child memory maxima on Linux.
