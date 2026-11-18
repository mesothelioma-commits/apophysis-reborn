# Verification record

## 0.10.0 Release Hardening — gate definitions and evidence

M10 retains the existing mathematical, historical-source, file, renderer, editor, extension and animation regressions and adds release/tooling coverage. This section is part of the source freeze: **registered tests, a compiled development build and a passed focused check are not substituted for a final release run.** Exact final outcomes, source/executable identities, pass/skip/failure counts, timings and audit totals are stored with the distributed binaries. This lets the source remain unchanged while those final measurements are collected.

| Final evidence in the complete distribution | Purpose |
| --- | --- |
| [validation-summary.json](../verification/validation-summary.json) | Final configured native builds, CTest outcomes, available-device boundaries and references to individual logs |
| [windows-runtime-record.json](../verification/windows-runtime-record.json) | Windows executable/module identities, recursive import/export audit and source/build provenance; static inspection is distinct from execution |
| [extracted-source-verification.json](../verification/extracted-source-verification.json) | Rebuild/test of the packaged source using its delivered files and release identity |
| [distribution-verification.json](../verification/distribution-verification.json) | Archive inventory, checksum/CRC, bundle/source and distribution consistency checks |
| [benchmark-final/report.json](../verification/benchmark-final/report.json) | Actual final executable, workload, host/device identity, measured repetitions, timings, memory and output hashes |
| [benchmark-16k/report.json](../verification/benchmark-16k/report.json), when the optional case is run | Separate one-observation large benchmark with its own settings/build and memory/timing scope |
| [current-high-resolution/full-16k-independent-verification.json](../verification/current-high-resolution/full-16k-independent-verification.json) | New full-image decode, dimension/precision/alpha/EOF and actual render/output identity; distinct from historical M7 evidence |
| `BUILD.txt`, individual smoke/desktop logs and installer build records | Commands, configuration and platform limits; screenshots, diagnostics, package helper inputs and signing/execution status |

These links name files produced in the complete release distribution; they need not exist in a source-only Git checkout. A missing or failed final record cannot be represented as a passed gate. Older sections below preserve their own revisions, measured values and execution boundaries.

### Configured inventory

| Configuration | Registered CTest entries | Required interpretation |
| --- | ---: | --- |
| Editor enabled, Vulkan enabled | 48 | Native CPU/Qt and six Python release suites; actual Vulkan suites must run on an identified usable device or retain their explicit skip/failure state |
| Editor disabled, Vulkan disabled | 36 | Core/release tooling remains available without Qt/Vulkan; `gpu`, `gpu_stream` and `gpu_kernel` are expected device-execution skips, not GPU passes |

Each CTest entry can contain multiple assertions or Python/Qt cases. The complete Windows target inventory is **32 application/test executables**, including the new `support-editor-tests.exe`; this is a configured inventory, not a claim that those programs executed on Windows. The lean runtime selects the app/CLI and their dependency closure and excludes test-only executables/modules. Final file/module/import totals come from the actual runtime record rather than an extrapolation from M9.

Python **3.10 or newer** is required by CMake when `BUILD_TESTING=ON`. Bash is needed for the wrapper fixtures; source/history tests use Git, and the build-identity fixture invokes CMake without a compiler through `LANGUAGES NONE`. Python is not an application runtime dependency. An application-only configuration with testing disabled does not execute these release gates.

### Added and hardened checks

| CTest entry / workflow | Contract exercised |
| --- | --- |
| `build_info_cli` | Consistent centralized version, source origin/revision/dirty type, compiler/architecture and compiled feature flags; unknown source has unknown cleanliness; informational commands reject render arguments without creating output |
| `editor_startup` | Build information before ordinary startup; malformed/conflicting automation options fail without a modal hang, existing output folders and user files survive rejected startup, and informational/rejected runs do not save a user profile |
| `support_editor` | Embedded manual from an unrelated current directory, search/missing-search behavior, blocked external/file navigation, separate requested/actual rendering fields, exact large integer reporting, clipboard copying and atomic text/JSON report saves |
| `release_staging` | Relative destination admission, module/data roots and adjacent dependencies, reserved/ancestor conflicts and refusal of output symlink escapes |
| `release_benchmark` | Actual backend/device selection, fallback rejection, resource-plan admission, warmup exclusion/aggregation, output verification, literal argument arrays, path boundaries, timeouts and independent child-memory observations |
| `release_packaging` | Windows-safe paths/reparse points, actual runtime dependency closure, missing symbols, case collisions, source/payload/checksum completeness, reproducible ZIP inventory and installer generated inputs with literal paths and compiled hashes |
| `release_source_package` | Clean complete Git history, tag/HEAD agreement, restored source/permissions/bundle, stable archive bytes, unsafe/existing output refusal, source-manifest tampering and tracked export transformations |
| `release_build_identity` | Raw/label-only unknown state, hash-bound clean source, changed/missing/declared-dirty files, malformed stamp/manifest/traversal rejection, and Git revision/dirty refresh after source edits, commits or untracked shipped inputs |
| `release_build_scripts` | Copied scripts with stubs: two-job default, 1–256 bounds/overrides, paths from another working directory, literal spaces/dollar substitutions/backticks/newlines, stage exit propagation, Qt-prefix forwarding and exact editor arguments |
| `--support-smoke-test` | Real manual/diagnostics windows, embedded content checks, text/JSON saves and screenshots in a new isolated output directory; search interaction is checked separately by `support_editor` and desktop observation |
| Existing creative/extension/animation smokes | Retained CPU workflows with isolated settings and separate new output subdirectories; smoke execution must not silently overwrite prior artifacts or user queue state |

During M10 integration, the new `build_info_cli`, `editor_startup` and `support_editor` gates passed in the development build. The bounded source-identity and wrapper suites also passed 14 and 12 Python tests respectively, including rejection of a release stamp that claims `source_revision: "unknown"`. These focused observations establish their tested development scope; the final full builds and release identity must be read from the accompanying records.

### Final execution method

Build and test the final source revision in separate Release, sanitizer and core-only directories. Use bounded compiler concurrency so one configuration does not exhaust the host while another is measuring memory or performance. Run the full applicable CTest suite and preserve its exit status and log. Record sanitizer options explicitly; a run with leak detection disabled is not a leak-check result. A validation-enabled software Vulkan run must retain its software-driver classification, and unsupported hardware is not an implied pass.

Run final editor smoke workflows in fresh directories and keep their input/build hashes, JSON and images. Inspect the native manual and diagnostics windows on the available desktop separately from offscreen Qt tests. The Windows desktop wrapper now writes creative outputs beneath `creative/` and new manual/diagnostics outputs beneath `support/`, alongside `queue/`, `extensions/` and `animation/`. The strict Windows GPU wrapper places its editor preview evidence under `preview/`, keeping it separate from queue exports. Each wrapper reports its actual output location and saves a `.txt` log.

The source/history release must restore from `apophysis-reborn.bundle`, agree with `release-source.json` and `SOURCE_SHA256SUMS.txt`, and rebuild from the extracted source rather than rely on workspace-only files. Hash-bound source state is observed at configuration time, with configured dependencies triggering refresh; it is not a live runtime audit of files beside the executable. The ordinary distribution manifest and the tracked-source manifest have different inventories.

Runtime packaging must re-audit the actual PE dependency closure, include all needed notices/corresponding sources, exclude test-only modules from the lean edition and verify every payload/ZIP inventory and checksum. Installer compilation must retain the generated payload inputs, private helper, compiler/NSIS identity, final installer hash and signing/execution state. A compiled installer is not proof of a Windows installation or uninstall.

### Benchmarks and high-resolution evidence

Use the final CLI after competing compiler/render jobs stop. The [benchmark guide](BENCHMARKS.md) defines the fixed source, sampling/thread-scaling, streamed PNG16 and three-frame animation cases. Ordinary cases use warmups and repeated fresh processes; the report distinguishes median total-process time from internal renderer stage time. The Vulkan path requires the selected actual backend with no fallback and rechecks device identity. Encoded output hashes establish repeatability only for the cases/settings actually repeated.

Memory reports use the measured child process, with units and completeness stated. Linux per-child `wait4` maxima are not a cumulative maximum across previous children. Windows observations identify whether the complete child lifetime was captured. Process RSS/working set is neither the configured working-buffer limit nor measured GPU VRAM. No CPU/Vulkan speedup or untested physical-device prediction is inferred from software Vulkan.

The optional benchmark `--large-16k` case is one observation per selected backend, not a repeated performance claim. It validates expected headers and encoded hashes but does not independently decode every pixel. Any new M10 full-image acceptance must retain its own exact source/executable/settings, genuine 16384×16384 render, complete independent decoding/EOF/CRC and precision/alpha evidence, and relevant memory/UI/restart observations. Those results belong to their new records. The complete M7 image, two-decoder comparison, strip/queue equality and measured memory remain explicitly **historical M7** in [HIGH_RES_ACCEPTANCE.md](HIGH_RES_ACCEPTANCE.md).

### Still required on target systems

Windows 11 portable launch, installer/shortcut/coexistence/uninstall behavior, high-DPI/multiple-monitor handling and physical NVIDIA/AMD/Intel driver/performance acceptance require actual target execution. The per-user installer must demonstrate its empty-destination rule and preservation of modified/extra payload files and external projects/settings. The Git Bash `verify-installer-windows.sh` wrapper launches the supplied Windows PowerShell lifecycle checks and retains JSON/text evidence; syntax checks alone do not run that lifecycle. Wine, a PE dependency audit, Linux Qt tests and software Vulkan cannot substitute for those outcomes. The exact commands and observation checklist are in [RELEASE_HANDOFF.md](RELEASE_HANDOFF.md) and [WINDOWS_GPU.md](WINDOWS_GPU.md).

The final compatibility report remains explicit about the missing historical inventory and product features. A successful M10 release gate validates the delivered subset; it does not implement the remaining mathematics, encoders, preferences, scripting/animation behavior or fault isolation listed in [RELEASE_COMPATIBILITY.md](RELEASE_COMPATIBILITY.md).

## 0.9.0 Animation and Color — 17 November 2026

The final M9 builds execute the following suites. Counts describe CTest entries, each of which may contain multiple assertions or Qt test cases.

| Configuration | Entries | Passed | Explicitly skipped | Elapsed |
| --- | ---: | ---: | ---: | ---: |
| Linux Release, editor and Vulkan enabled | 39 | 39 | 0 | 9.92 seconds |
| Linux Debug, AddressSanitizer and UndefinedBehaviorSanitizer | 39 | 39 | 0 | 202.98 seconds |
| Linux Release, editor and Vulkan disabled | 29 | 26 | 3 | 3.25 seconds |

The full suites use GCC 14.2, Qt 6.8.2 and `VK_LAYER_KHRONOS_validation`; actual Vulkan dispatch uses the previously identified llvmpipe software driver. The final detailed logs contain no AddressSanitizer, UndefinedBehaviorSanitizer or Vulkan validation errors. Sanitizer options are `ASAN_OPTIONS=detect_leaks=0` and `UBSAN_OPTIONS=halt_on_error=1`; leak detection is not claimed. The three core-only skips are `gpu`, `gpu_stream` and `gpu_kernel`, whose actual device execution is unavailable when Vulkan is compiled out. Portable GPU model checks and CLI fallback/error checks still execute. These suite durations are validation measurements, not rendering-performance benchmarks.

| Added or expanded check | Executed behavior |
| --- | --- |
| `animation` | Exact source/keyframe round trips including UTF-16 and opaque future payloads; strict XML/base64/schema limits; rational time, loops, full-width seeds and cancellation; exact endpoints, easing bounds near a million-frame clip's end, camera/affine/polar interpolation, discrete settings, palette/curve interpolation, correspondence and both Xaos axes, final-transform/topology policies, presets and explicit legacy-time conversion. Native plugin tests bind module identities and distinguish real and integer parameters. |
| `animation_export` | Read-only range/path/resource admission, fixed canvas and global frame numbering, destination conflicts and replacement guards, bounded batches, partial-frame checkpoints, publication-journal recovery, completed-image size/SHA-256 validation and cancellation/resume. A later frame cannot silently replace externally changed output. |
| `animation_cli` | Create, preset, inspect, morph, add, timed import and single-frame/sequence exports; real formats and frame dimensions, dry runs, pause/resume, invalid options and ranges, changed outputs/settings and explicit replacement of existing animation data. |
| `color_curves` | Independently generated original 7X integer and floating-value comparisons; global identity bypass, weighted curves, legacy and continuous modes, invalid/singular input, source-preserving/no-op edits, canonical snapshots, dense/streamed equality, strip independence and direct 16-bit precision. Default identity curves retain earlier image goldens. |
| `animation_editor` | Native-project timeline undo/save/recovery and plain-library save guards; opaque-payload preservation and failed-edit rollback; private local edits, playback/pause, exact rational-rate entry, atomic queue batches and immutable snapshots. Pending or restored frame jobs preserve destinations that appeared or changed after admission. |
| `color_editor` | Actual point dragging and numeric fields, presets/reset, HSL and interpolation controls, palette curve baking and undo, nested Cancel/Apply behavior, exact document undo and queue persistence. A regression exercises Adjustment edits inside the timeline, including camera/tone fields. |
| Earlier gates | All previous CPU/reference and original-producer tests, creative/editor workflows, Lua/plugins/project exchange, software-Vulkan probes, dense/file reconstruction, high-depth image encoders, checkpoint integrity and render-queue checks remain passing. |

**Original-source color evidence.** The fixture generator compiles the unchanged, hash-pinned original `Bezier.pas` unit and extracted original image-maker expressions with Free Pascal. Seven configurations cover defaults, weight-only changes, X-only activation, weighted master/RGB including a zero inner weight, reversed Y, nonordered X and changed endpoints. Across **1,799 inputs**, all **5,397 integer RGB values match exactly**, and **7,196 raw values match within `2e-15`**. This establishes the documented original lookup contract, including its point-only identity bypass and parameter-Y evaluation; it does not establish complete historical renderer equivalence. The modern inverse-X transfer is a separate, explicitly selected mode. [Color contract and generator](COLOR_TOOLS.md)

**Final animation workflow.** The final release editor's `--animation-smoke-test` runs on Qt's **offscreen** platform. It creates a 25-frame, 24 fps project with three camera/color keyframes, verifies timeline undo and native-project reload, and queues global frames **0, 6, 12, 18 and 24**. All five exports are **320 × 240 PNG16 RGBA**, **120,000 samples each**, CPU with two workers, and seed **18,446,744,073,709,551,615**. Independent verification checks every PNG chunk CRC, decompresses and unfilters all **3,072,000 RGBA16 bytes**, and compares every channel's high byte with Pillow's RGBA8 decode. Every image contains precision beyond RGB8 expansion. It also checks exact queue snapshots, frame settings, project payloads and artifact SHA-256 values. Timeline, curve and queue screenshots accompany the output and were visually inspected.

The M8 extension workflow is rerun on the final M9 editor: the explicitly enabled `sample_wave` module, Lua edits and three deferred requests, source/native-project exports, UGR/MAP, the legacy triangle script and a queued **320 × 240 PNG16 RGBA**, **300,000-sample** image all complete. The independent decoder checks its additional **614,400 channel bytes**, palette equality and native-module fingerprint. This is current M9 execution; separate retained M8 records remain labeled historical.

**Actual process interruption.** Separate final-CLI processes render one selected **12 × 8 PNG8** frame with **5,000,000 samples**, CPU, one worker and the maximum unsigned 64-bit seed. After a durable chunk is observed, one process receives SIGTERM and exits 143; another receives SIGKILL and exits −9. Neither has published its destination before restart. Fresh processes reacquire the locks, resume committed samples and finish successfully. Both resulting images are **byte-identical** to an uninterrupted reference. `verification/sequence-process/report.json` records the exact CLI/project/image hashes, settings and outcomes. This bounded process test complements simulated interruption/publication boundaries in the unit suite.

**Actual native desktop interaction.** A separate Linux **XCB** session opened Timeline, applied Camera orbit, played and paused, chose exact **30000/1001 fps**, renamed the clip, applied it, used document Undo/Redo and saved a native project through the actual file dialog. Independent CLI inspection confirms the saved `Native orbit study`: two keyframes, 121 frames, repeat looping and unwrapped rotation. The observed desktop executable precedes the final near-end easing clamp; its binary identity and that boundary are recorded explicitly. The final complete suites and offscreen workflows cover the later source. Desktop observation is not represented as an offscreen run or Windows execution.

**Windows build and audit.** All **31 executables** compile and link for Windows x64: the desktop and CLI applications plus 29 test executables. The portable runtime contains **68 files**, including **65 PE modules**; static import resolution covers **23,040 non-system symbols**, with zero missing. Every staged executable and plugin matches its build output. The audit retains source/test/header/data fingerprints and checks compiler dependency files, owning objects, link inputs, generated resources and CMake configuration freshness. A test-only edit is checked against its test executable rather than an unrelated editor timestamp. Five Python runtime-staging regressions pass. The CPU, editor and optional GPU verification scripts pass `bash -n`, and their target inventory and special fixture arguments were reviewed. No Windows executable or DLL was executed here.

**Distribution integrity.** The release is built from clean tagged source and includes a restorable Git bundle with all **eight milestone tags, 0.2.0 through 0.9.0**. Packaging verifies source, object/build provenance and runtime hashes, rebuilds the extracted source and requires all 39 CTest entries with zero skips. The final ZIP is checked for CRC errors, complete file inventory and every listed SHA-256. Exact extracted-build and distribution results are written into `BUILD.txt` and `verification/distribution-verification.json`. Complete corresponding LGPL sources, Lua provenance/notices, examples, guides, screenshots and bounded test evidence accompany the release. The complete M7 16K image and its original decoding, restart and queue comparisons remain explicitly **historical M7**; those long runs are not claimed as new M9 performance measurements.

**Remaining scope.** Windows 11 runtime/UI/DPI behavior and physical NVIDIA/AMD/Intel GPU acceptance/performance remain open. M9 exports image sequences; video/audio encoding, FFmpeg integration, temporal motion blur, per-property tracks, automatic transform matching and a complete historical animation interpreter are not included. Active 3D/direct-color behavior, density estimation, additional variations, ICC management and broader historical compatibility remain tracked in [FEATURE_MATRIX.md](FEATURE_MATRIX.md). Milestone 10 is release hardening, packaging and the remaining platform acceptance. All older sections below describe their own historical checkpoints.

## 0.8.0 Extended Compatibility — 17 November 2026

The final Linux Release build passes **33/33 CTest entries** in **6.09 seconds**. A separate Debug build with AddressSanitizer and UndefinedBehaviorSanitizer passes **33/33** in **48.83 seconds**. Both runs enable `VK_LAYER_KHRONOS_validation`, execute the software-Vulkan checks and report no sanitizer or Vulkan validation errors. Sanitizer settings are `ASAN_OPTIONS=detect_leaks=0` and `UBSAN_OPTIONS=halt_on_error=1`; this is not a leak-check result. The Vulkan-disabled, core-only build passes **22 checks** and explicitly skips **three device-execution checks**, for **25 entries** in **1.95 seconds**.

The first integration run exposed a script-construction exception boundary and an incomplete CLI test fixture; both were fixed before the final runs. Running several compiler configurations concurrently exceeded the workspace memory budget. The final native and sanitizer builds ran sequentially with reduced compiler concurrency; their complete test suites then passed. No source failure is hidden by a skipped applicable test.

| Added or expanded check | Executed behavior |
| --- | --- |
| `script` | Fifteen groups covering modern edits, topology/source identities, UTF-16, producer-aware snapshots, deterministic generation/mutation, deferred capabilities, resource limits, cancellation, strict legacy preflight, original affine values and all bundled examples. Tiny allocator/host budgets and non-string or oversized Lua errors fail cleanly. |
| `script_host` | Complete destination planning before writes; traversal, absolute/drive paths, reserved names, duplicate/hardlink/symlink aliases, ancestor conflicts, protected inputs/state/future directories, overwrite rules, render formats and host-owned checkpoints. Windows Unicode ordinal collision cases compile for Windows; Windows runtime execution remains pending. |
| `plugins` | Data-only discovery versus explicit loading; SHA-256 vectors, ABI/descriptor/parameter checks, native callback error/nonfinite handling, deterministic callback RNG, immutable module lifetime, plugin XML and Lua parameter edits, CPU fallback/strict Vulkan refusal and checkpoint fingerprint identity. |
| `project_io` | Exact embedded collection bytes and untouched envelope round trips; unknown XML/settings preservation; presets, plugin references and opaque animation; strict version/base64/DTD/depth/size validation and atomic save failure. |
| `legacy_formats` | Original Pascal-generated UGR/MAP/UPR fixtures; exact 256-color results, coefficient/camera/symmetry conventions, collections, invalid input and explicit rejection of unsupported formulas or unrepresentable export settings. |
| `extensions_cli` | Project create/inspect/extract and selection defaults; native save preservation; palette and supported UPR exchange; plugin discovery versus explicit enablement; Lua/legacy runs, capability refusal/permitted output, syntax/runtime failures and instruction limits. |
| `extensions_editor` | Native project save, conflict handling, recovery, unknown metadata and exact large integer settings; private script Run, single-step Apply/undo, cancellation, close while running, error highlighting and output preflight. |
| `plugin_dialog` | Saved approval/fingerprint lifecycle, disable with live module owners, missing or changed module restoration, immutable queue plugin identity, unchanged parameter-control precision/order and protection of the real queue JSON from script output. |
| Existing core/editor/queue gates | All prior numerical and historical producer oracles, 31 built-in variations, creative editing, previews/canvas/documents, GPU probes, streaming images, dense/file equivalence, cancellation, checkpoint integrity/resume and queue behavior remain passing. Gradient editor tests now include UGR/MAP and failed-import preservation; queue batching rejects a bad group without partially adding jobs. |

**Original-source comparisons.** The optional legacy script generator compiles unchanged, hash-checked Apophysis 2.09 affine methods with Free Pascal 3.2.2. All six coefficients match the new evaluator within `3e-12`. The palette/UPR generator similarly builds pinned original methods; its seven checked-in fixtures exercise full palette interpolation and the supported UPR profile. These are bounded numerical/format comparisons, not claims of full TatScripter VM behavior or arbitrary Ultra Fractal formula compatibility. The Lua source provenance covers all 63 vendored upstream files without modification.

**Final extension workflow.** `--extensions-smoke-test` ran the release editor on Qt's **offscreen** platform. It explicitly enabled the bundled `sample_wave` module, edited three transforms through Lua, exported flame/GPL files, saved `.apoproject`, exported UGR/MAP, executed the legacy triangle example and completed a queued **320 × 240 PNG16 RGBA**, **300,000 samples**, seed **18,446,744,073,709,551,615**, on the CPU. The report records three deferred requests and an instruction upper bound below 1,000 VM instructions, not a claim of zero instructions. The independent artifact check validates every PNG chunk CRC, decompresses and unfilters all **614,400 RGBA16 bytes**, and confirms every high byte against Pillow's RGBA8 decode. Values include precision beyond expanded RGB8 and alpha from 0 to 65,535. All 256 UGR/MAP RGB entries match, and the project's plugin fingerprint matches the enabled binary. Screenshots, outputs, the decoder and JSON evidence accompany the package.

**Actual native desktop interaction.** A separate Linux **XCB** session discovered and explicitly enabled the bundled plugin, ran the palette/geometry example, applied the result, undid/redid it, changed the plugin weight to 0.2 and saved a native project. The final CLI independently inspected that saved project, confirming its selected entry, three transforms, plugin reference and source bytes. This manual workflow used the M8 integration binary before final status/number-format polish; the final release suite and offscreen workflow cover the later snapshot. The desktop observation and offscreen smoke are separately labeled in the evidence.

**Windows cross-build and package audit.** All **26 executables** compile and link for Windows x64. The staged runtime contains **63 files**, including **60 PE modules**, the SDK sample and two deliberately distinct plugin test modules. Its static dependency audit resolves **19,959 non-system imported symbols** with **zero missing**. The audit checks GUI subsystem, all staged executable/module hashes and 228 source/test/header/provenance fingerprints. No input is newer than the final editor. Lua is statically linked; the runtime needs no Lua DLL. Five separate Python staging tests pass, and both Windows verification scripts pass `bash -n`. No Windows executable or DLL was executed here.

**Release integrity and retained evidence.** Packaging uses the clean tagged source, includes all seven milestone tags in a restorable Git bundle, verifies the Windows source fingerprints and runtime hashes, and rebuilds the extracted source with all 33 CTest entries. It checks the final ZIP CRC and every listed file SHA-256. Exact extracted-build results are written into `BUILD.txt` and `verification/distribution-verification.json`. The full M7 16K PNG and its independent decoding, restart and queue comparison evidence are retained and explicitly labeled **historical M7**; those long acceptance runs were not repeated as M8 performance measurements.

**Remaining acceptance and next milestone.** Vulkan execution used llvmpipe software rendering with validation, not a physical graphics card. Windows 11 UI/DPI/runtime acceptance and NVIDIA/AMD/Intel hardware performance remain open. Native plugins are trusted in-process code; a callback crash or hang is not isolated. Legacy scripts support the documented subset, old 32-bit DLLs require migration, and UPR support is restricted to the declared Apophysis profile. Animation data is preserved but no timeline/evaluator exists yet. Milestone 9 adds animation, morphing, extended color tools and additional 7X/AV features; the broader compatibility inventory remains in [FEATURE_MATRIX.md](FEATURE_MATRIX.md).

## 0.7.0 High-Resolution Rendering — 17 November 2026

The final native Release and ASan/UBSan builds each pass **25 CTest entries**, in 5.74 and 44.02 seconds respectively, including all earlier CPU/reference, historical-producer, creative/editor and Vulkan gates. Both enable `VK_LAYER_KHRONOS_validation`; no sanitizer or Vulkan validation errors are reported. Sanitizer options are `ASAN_OPTIONS=detect_leaks=0` and `UBSAN_OPTIONS=halt_on_error=1`. A separate Vulkan-disabled, core-only build passes **16 checks** and explicitly skips **three device-execution checks**, for 19 entries. LeakSanitizer, Windows execution and physical GPU execution are not claimed.

| Added or expanded check | Executed behavior |
| --- | --- |
| `file_render` | 30 exact CPU dense/stream RGB8 cases and six exact software-Vulkan cases; global filter overlap, historical math, opacity/final/pre-blur/rotation, strip and worker independence; direct RGB16/straight-alpha precision; partial and complete checkpoint resume; corruption, orphan, identity, lock and cancellation checks; model, host, disk and strip bounds. |
| `file_cli` | No-write estimates, all output families and true 16-bit selection, transparent export, dimension/filter overrides, incompatible settings, memory/disk rejection, separate-process pause75/resume, complete checkpoint reconstruction and corrupt-data failure. |
| `image_stream` | All 11 RGB/RGBA/depth combinations, exact header/row/sample checks, real PNG deflate/CRC, TIFF strip offsets/alpha tags, finite/range handling and injected stream/row-count failures. Independent Pillow and ImageMagick decode every fixture. |
| `gpu_stream` | Exact hit/counter equality with dense sampling, global coordinates, split absolute ranges and partial chains, high chain indices, generic/2.09/7X math, invalid orbits, cancellation and host/GPU admission. A 16K oversample-two coordinate case avoids a dense GPU histogram. |
| `render_queue` | Immutable snapshots and source math conventions, exact large integer settings, order/save/restore, atomic rollback, timers during real work, checkpoint pause/restore/resume equality, immediate stop after worker-start signal, failure/retry/start-over, destination preservation and graceful destruction. |
| Existing regression gates | The original CPU golden, historical Pascal pixels, 1,560 shader transform probes, all-variation vectors, source-preserving edits and unchanged renderer-details layout regression remain passing. CLI BMP checks now accept the streamed top-down header while testing decoded orientation. |

**Actual 16K acceptance.** Three full renders were executed to distinguish the original CLI camera from the explicitly configured queue camera. The independently decoded baseline is 16,384 × 16,384 PNG16 RGBA with 8,388,611 plotted samples and zero invalid steps. It took 63.802 seconds and peaked at 40,180 KiB RSS with a 128 MiB renderer-buffer budget. A bounded Python decoder and external ImageMagick stream decoder agreed on all 2,147,483,648 decoded channel bytes, with complete CRC/deflate/row/EOF checks and measured 16-bit color/alpha values. The included preview is reduced from those full-resolution pixels. [Complete measurements and hashes](HIGH_RES_ACCEPTANCE.md)

**Seams and resumability.** A matching-camera CLI render using 63-row strips and the resumed native queue using 247-row strips produced identical complete 359,217,798-byte PNG files. The queue paused at 65,536 committed samples, preserved its previous destination, resumed and finished all 67 strips. Its 10 ms UI timer ran 6,972 times, with a maximum gap of 98 ms and a pause-stop latency of 5 ms. The Qt process used the explicitly reported offscreen platform; its whole-process RSS was 187,156 KiB while charged rendering buffers remained below 128 MiB. [Queue and process evidence](HIGH_RES_ACCEPTANCE.md)

**Actual termination recovery.** A separate render process was killed with SIGKILL after a durable chunk. A new process resumed and produced the same PNG bytes and counters as an uninterrupted render with a different worker count. Actual SIGINT separately preserved the old destination and returned 130 with a usable checkpoint. Unit tests additionally corrupt manifests and payloads, simulate orphan chunks and reject concurrent checkpoint ownership. Checksums detect accidental corruption, and checkpoint identity prevents incompatible reuse; these are not portable cross-build archives.

**Native desktop and smoke checks.** Actual Linux XCB interaction inspected the resolution/depth/alpha/budget controls, added a stopped 512 × 512 PNG16 RGBA job, started it and observed complete CPU rendering. The output fully decoded independently. The final native queue smoke exports both PNG16 RGBA and TIFF16 RGB while recording event-loop heartbeats. It includes settings and queue screenshots. The full-resolution offscreen check and manual desktop check are separately labeled in the distribution evidence.

**Windows build and runtime audit.** All **19 executables** compile and link for Windows x64. The portable runtime has **52 files**, including **50 PE modules**. Auditing resolves **14,948 non-system imports** with zero missing, checks the GUI subsystem and matches staged executable hashes to their build outputs. Vulkan remains dynamically loaded. Zlib 1.3.1 supplies PNG compression through the existing runtime dependency. The programs were compiled and audited, not executed; [Windows build instructions](QT_BUILD.md) describe the packaged checks.

**Source and distribution checks.** The embedded 49,728-byte SPIR-V payload matches a fresh glslang build and passes Vulkan 1.0 SPIR-V validation. The release retains build/test/shader/decoder/process/UI evidence. Packaging verifies the clean tagged Git source and Windows input hashes, rebuilds extracted source and runs all 25 tests with actual software-Vulkan execution, restores the Git bundle, then checks ZIP CRCs and every distribution file checksum. Final packaging results and precise suite times are in `BUILD.txt` and `verification/distribution-verification.json`.

**Remaining acceptance and next milestone.** Physical GPU driver/performance acceptance, Windows 11 UI/DPI execution, JPEG/OpenEXR, ICC color management and dithering remain open. The test image's modest sample density is not a finished-quality render claim. Milestone 8 covers scripting, plugins and additional historical formats. Earlier sections below retain the facts and limitations of their own checkpoints.

## 0.6.0 GPU Rendering — 17 November 2026

The final Linux Release build passed all **20 CTest entries** in 3.42 seconds. The separate Debug build passed the same **20 entries** with AddressSanitizer and UndefinedBehaviorSanitizer in 18.37 seconds. These runs include the final renderer-description layout fix, use GCC 14.2.0 and Qt 6.8.2, and enable `VK_LAYER_KHRONOS_validation`. No Vulkan validation errors or sanitizer errors were reported. Sanitizer options remain `ASAN_OPTIONS=detect_leaks=0` and `UBSAN_OPTIONS=halt_on_error=1`; this is not a LeakSanitizer result.

**Execution boundary.** This host exposes no physical GPU. Actual Vulkan dispatches ran through Mesa 25.0.7-2+deb13u1, reporting **llvmpipe (LLVM 19.1.7, 256 bits)**, device type CPU and Vulkan 1.4.305. The loader and Khronos Validation Layers are 1.4.309. These results verify the Vulkan implementation on a software device. Physical GPU correctness, vendor-driver behavior, speedup and Windows execution remain pending.

| Added or expanded check | Executed behavior |
| --- | --- |
| `gpu_kernel_cpu` | Packed-model layout and limits, FP32 admission, preservation of transform/Xaos CDF intervals and 16,384 portable random draws. |
| `gpu_kernel` | **1,560 direct CPU/Vulkan transform probes** covering all 31 variations, signed weights, generic/classic/7X conventions, pre/post matrices, stochastic draw order, repeated pre-blur, mixed term order and the final-transform slot. Maximum normalized coordinate error was **1.7820791e-05**, below the fixed **2e-4** bound; subsequent random draws matched exactly. [Kernel methods](GPU_KERNEL.md) |
| `gpu` | Five full-image comparisons with the matching-stream double-precision CPU reference, original CPU golden continuity, repeated-render determinism, seed changes, monotonic progress, cancellation, explicit device selection, host/GPU limits, unsupported FP32 ranges and visible CPU fallback. All five tested images had equal plotted/invalid counts and zero RGB8 difference. [Fixtures and fixed bounds](GPU_TESTING.md) |
| `gpu_cli` | Text/JSON discovery, default/explicit CPU identity, fallback diagnostics, strict missing-device and memory errors, actual Vulkan file output, malformed options and preservation of an existing destination after failure. |
| `preview` | Requested backend propagation and actual-backend reporting alongside the existing progressive rendering, replacement, cancellation and output guarantees. |
| `editor_workflow` | Renderer/device/budget/fallback controls, persistence and separate export choices. A regression exercises **CPU → Vulkan → CPU** in both Preferences and Render Image, checking full wrapped descriptions, visible buttons and retained dialog size. |
| Earlier checks | All existing creative, editor, document, import, numerical, historical-producer, image and CLI checks continue to pass. |

**CPU-only build.** A separate core build with `APO_ENABLE_VULKAN=OFF` and `APO_BUILD_EDITOR=OFF` passed **13 checks**; its **two actual-compute checks were recorded as skipped**, for 15 CTest entries total. The discovery/fallback CLI checks and portable kernel packing/random tests still ran. The suite completed in 0.62 seconds. A skip does not count as Vulkan execution. Independent missing-device checks also confirm that strict Vulkan mode fails while the ordinary mode reports CPU fallback.

**Native desktop interaction.** The running Linux desktop rendered a two-million-sample Vulkan preview, switched to the CPU reference and back, and displayed the actual backend after each completed render. The Gradient editor inherited Vulkan rendering, reversed the palette and applied it. Saving through the native file dialog produced `examples/gpu-palette-study.flame`; independent CLI inspection marked the saved entry renderable. The manual CPU-to-Vulkan Preferences transition exposed clipped explanatory text. The corrected build was reopened and the same transition verified with all device/precision text and buttons visible; the Vulkan preview then completed successfully.

**Strict native smoke workflow.** The final `--gpu-smoke-test DIR --device 0` run completed with fallback disabled and reported `backend: Vulkan`, software device identification and an empty fallback reason. It produced all five required files: `editor.png`, `preview.png`, `saved-example.flame`, `render-device.json` and `render-settings.png`. Both window screenshots were visually inspected. The final screenshots are included under `preview`; the complete smoke output and log accompany the distribution under `verification/gpu-smoke`. [Renderer controls and smoke mode](GPU_RENDERING.md)

**Shader reproducibility.** The checked-in SPIR-V header matched a fresh compilation of `shaders/orbit.comp` by glslang reporting 15.2.0 and passed SPIRV-Tools 2025.1 validation for Vulkan 1.0. Ordinary application builds use the embedded header and vendored Vulkan-Headers v1.4.309; they do not need the Vulkan SDK or a shader compiler. Development toolchain package identities and hashes are retained with the verification evidence.

**Windows build and runtime audit.** The final x86-64 build compiled and linked all **15 executables** after the UI fix. The editor uses the Windows GUI subsystem. Its portable runtime contains **48 files**, including **46 PE modules**. The audit resolved **13,232 non-system imports**, with zero missing. Every staged executable matches its build output, and all **90** recorded source/test/shader/header fingerprints match the final inputs. The system Vulkan loader is loaded dynamically and is not bundled. These Windows programs were compiled and audited, not executed.

The distribution's `verification` directory retains the final native, sanitizer, CPU-only, Windows, shader, numerical and smoke evidence. The package is also checked by extracting its source, building it afresh, running CTest, restoring the Git bundle and validating every file checksum; the result of that distribution check is recorded in `BUILD.txt` and `verification/distribution-verification.json`.

**Remaining acceptance and next milestone.** The strict [Windows GPU workflow](WINDOWS_GPU.md) records and tests one selected device without accepting a CPU fallback as GPU success. Physical NVIDIA/AMD/Intel execution, Windows 11 UI/DPI behavior and performance measurement are pending. CUDA is optional future work and is not implemented here. Milestone 7 adds memory-aware high-resolution rendering, tiled processing, queues/checkpoints and higher-bit-depth output; native 16K acceptance is not claimed for this checkpoint. Earlier sections below retain the results and limits of their own milestones.

## 0.5.0 Creative Tools — 17 November 2026

The final Linux Release build passed all **16 CTest entries** with GCC 14.2.0 and Qt 6.8.2 in 1.01 seconds. A separate Debug build with AddressSanitizer and UndefinedBehaviorSanitizer passed the same 16 entries in 9.98 seconds. Its options were `ASAN_OPTIONS=detect_leaks=0` and `UBSAN_OPTIONS=halt_on_error=1`; this does not claim a LeakSanitizer result. The distribution's `verification` directory retains the executed test logs and Windows build/runtime records.

| Added or expanded check | Executed behavior |
| --- | --- |
| `creative` | Palette interpolation and validation, neutral adjustments, rotation/reversal/smoothing, GPL exchange, seeded generation, variation constraints, symmetry, mutation groups and zero-strength identity, finite-orbit framing and failed-fit rollback. |
| `extended_variations` | All 640 independently generated original-source vectors for the ten new variations, named and numbered import, save/reopen, producer registry differences, affine coupling, signed weights, random-state preservation and ten nonuniform sample renders with zero invalid steps. [Methods and pinned sources](VARIATIONS.md) |
| `creative_editor` | RGB precision, gradient history/reset, stop building and invalid-input rollback, palette/flame import with Unicode and spaced paths, adjustment validation/reset/aspect/filter behavior, source-preserving Apply/undo, locked/unlocked random seeds, batch insertion, zero-strength mutation, branch history and dialog destruction during pending input/generation. The default candidate grid also checks complete visibility of the first row. |
| `editor_workflow` and `document_session` | Creative actions and modal Cancel/Apply, saved mutation branches, collection append/duplicate/delete, restored selection on undo/redo, and invalid-entry-selection rollback. |
| `legacy_document` | Palette replacement across historical encodings and representations, retained comments/history/siblings, source aliases, adjustment validation, append/duplicate/delete and byte-preserving rollback. |
| Earlier checks | All prior FLAM3 vectors, producer-pixel fixtures, XML import, rendering, CLI, preview, canvas and editor workflows continue to pass. |

**Native desktop interaction.** On the actual Linux Qt desktop, the Gradient editor applied Glacier, reversed the palette, undid that operation and committed the result. The Adjustment editor changed gamma to 2.6 and committed it. The Mutation explorer selected candidate 4, used it as a new parent, selected a descendant and added its branch history. Saving through the native file dialog produced the three-entry `examples/creative-branches.flame`; independent CLI inspection marked all three entries renderable. A subsequent launch verified that the corrected default 1280 × 790 explorer shows all nine candidate tiles without resizing its dividers.

**Creative smoke workflow.** The final `--creative-smoke-test DIR` run rendered the main editor and all four creative dialogs and exited successfully after producing all 12 required files: five window screenshots, a preview PNG, five flame files and a GPL palette. Every screenshot decoded. The nine entries in the saved random batch were independently inspected as renderable and are included as `examples/seeded-random.flame`. The current dialog screenshots are in `preview`; [the creative guide](CREATIVE_TOOLS.md) explains their controls.

**Windows build and runtime audit.** The final x86-64 build compiled and linked all **13 executables**. Its editor uses the Windows GUI subsystem. The staged runtime contains **46 files**, including **44 PE modules**. Inspection resolved **12,922 non-system imports** against the exact packaged exports, with zero missing. Every staged executable matched its build output, and recorded fingerprints match all 39 source, test and resource files. The Windows programs were compiled and audited; they were not executed.

**Pending acceptance.** Windows 11 execution, DPI/multiple-monitor behavior and historical-editor reopening remain pending. The new deterministic random and mutation workflows do not claim historical generator sequence equivalence. Broader palette formats/catalogs, alpha output, non-default historical reconstruction curves, density estimation and additional variations remain open. GPU acceleration is Milestone 6; tiled output and native 16K acceptance remain subsequent work. The older milestone sections below are historical records of their own scope.

Use `bash scripts/build-editor.sh` for a native rebuild and all 16 CTest entries. On Windows, `bash scripts/verify-editor-windows.sh` runs the packaged core/Qt tests and the creative smoke workflow. Its outputs and exact filenames are described in [QT_BUILD.md](QT_BUILD.md).

## 0.4.0 Primary Editor — 17 November 2026

The full Release build passed all 13 CTest entries on x86-64 Linux with GCC 14.2.0 and Qt 6.8.2. The Debug build with AddressSanitizer and UndefinedBehaviorSanitizer also passed all 13. Leak detection was disabled because the host tracer prevents LeakSanitizer from running. The editor was additionally exercised through the actual Linux desktop using the packaged native Qt runtime.

| New check | Executed behavior |
| --- | --- |
| `editor_workflow` | Open the built-in flame, wait for a rendered image, edit the name and an affine coefficient through Qt input events, undo/redo, save to a path with spaces, reopen, add/duplicate/delete/final/post operations, one undo command per drag, mixed supported/unsupported library selection, and pause/resume. |
| `canvas` | Origin and both basis-handle drags, snapping, Esc rollback, screen/world coordinate inversion, panning, anchored zoom, empty selection and coincident handles. Expected geometry accounts for the actual integral screen pixel delivered by the event. |
| `document_session` | Failed-change rollback, exact-byte undo, source encoding and CRLF preservation, save-clean state, external changes/deletions, relative paths, recovery ownership, lock rejection, adoption and cleanup. |
| `preview` | Increasing preview budgets, aspect/camera fitting, debounce, pause/resume, replacing active work without stale images, destruction during rendering, full-size file output, and cancellation that preserves an existing destination. |
| Expanded legacy document cases | Surgical transform edits, historical aliases, variation lists, duplicate probability/chaos behavior, reordering and deletion, implicit colors, final transforms, failed-operation rollback, UTF-16 in both byte orders. |
| Existing core reference and CLI checks | All nine earlier entries continue to pass, including the original Pascal image references and numerical FLAM3 vectors. |

**Desktop interaction.** The running native window rendered Ember Spiral. Changing coefficient a from 0.69 to 0.58 updated the numeric field, triangle and fractal. Undo restored 0.69. A subsequent triangle-origin drag changed the translation and refreshed the image. The file dialog saved the result, cleared the modified marker and showed its filename. Independent inspection of the saved XML confirmed that only the selected transform's coefficient attribute changed. The complete automated editor workflow also verifies save/reopen.

**Portable launch.** The relocated Linux runtime launched under the desktop's XCB platform. Its offscreen smoke mode generated a complete 1420 × 900 editor screenshot, a decoded preview PNG and a saved flame byte-identical to the built-in source. The source package includes that editor screenshot. The normal application now sizes its first window to the available screen.

**Windows build.** All ten executables compiled and linked for x86-64 Windows. The desktop program has the Windows GUI subsystem. A dependency closure inspection resolved 11,349 non-system imports against the actual packaged DLL exports with zero missing. The portable directory contains 43 executables, DLLs, plugins and metadata/configuration files. The runtime and source notices are in QT_BUILD.md and licenses/qt.

**Pending acceptance.** Windows 11 execution, DPI/multiple-monitor behavior on Windows, and historical-editor reopening remain unverified. Multi-selection, synchronization, persistent per-transform locks and customizable shortcut profiles remain open editor work. GPU, full palette/mutation tools, tiled 16K rendering and an installer belong to subsequent milestones.

Use `bash scripts/build-editor.sh` to rebuild the editor and run the source checks. The Windows package includes `bash scripts/verify-editor-windows.sh`. The source's `--smoke-test DIR` option opens the native editor, waits for its refined preview, writes the screenshot/preview/flame artifacts, and exits with a status code.

## 0.3.0 Legacy Compatibility — 17 November 2026

All nine CTest entries passed in a Release build on x86-64 Linux (GCC 14.2.0, CMake 4.4.5) and a second Debug build using AddressSanitizer and UndefinedBehaviorSanitizer. Leak detection was disabled in the sanitizer run because the host tracer prevents LeakSanitizer from starting. The Windows x64 CLI and four test executables were cross-compiled and their PE headers and import tables inspected. Native Windows 11 execution and reopening in the original Windows editors remain pending.

| Check | Result on this checkpoint |
| --- | --- |
| Legacy document layer | Byte-identical untouched save. Targeted edits to name, size, center and scale. Coverage for XML comments, history, unknown settings, sibling entries, alternate encodings, failed edits and malformed input. |
| Public CLI | Mixed library inspection, save and in-place edit, invalid operations, independent entry rendering and canonical export. The original and the standalone export of Fox's 2.08 3D Hack flame rendered identically at a fixed seed across one and two workers. |
| Published files | All 19 entries in five historical documents passed preservation, edited-name and reopen checks. Fourteen rendered. Five AV entries carried specific diagnostics and could still be edited and saved. [Corpus records](LEGACY_CORPUS.md) |
| Original producer pixels | Five 8 × 6 controlled histograms reconstructed by original 2.09 and 7X Pascal methods. Reborn matched all 720 RGB channel values under both producer labels. [Producer verification](PRODUCER_ORACLE.md) |
| Original producer fields | Original 2.09 Pascal routines read the new dimensions, center and scale, and the same 768 palette bytes from edited and exported XML. The test included an authentic 2.08 entry. |
| Artist-published images | Full-frame AV render at 800 × 800 had mean absolute RGB error 1.214890/255 and intensity correlation 0.988834. Against a 7X editor screenshot viewport the error was 12.181279/255 and correlation was 0.689425 at 764 × 549. Correlation rose to 0.983465 on a 96 × 69 box-downsample. [Methods and source images](LEGACY_CORPUS.md) |
| Existing FLAM3 reference | 126 numerical vectors matched. The original external image check measured RGB MAE 4.832694/255, RMSE 6.465777/255 and mean-channel intensity correlation 0.984265. |
| Example studies | Rendered all 21 from the public CLI at 350,000 samples, seed 123456, two workers. Pillow decoded every 480 × 360 image. Each was nonuniform and the renderer reported zero invalid orbit steps. |

**Additional cross-renderer comparison.** The upstream FLAM3 executable and Reborn rendered the first untouched *Spherical* entry from the publisher's 2.08 collection. At 1500 × 1000, RGB MAE was 19.462591/255, RMSE 25.015175/255 and pixel intensity correlation 0.878334. This measures the difference between two different renderers' interpretations of an authored file. It has no pass threshold. FLAM3 used its native defaults with `nthreads=1 bits=64 format=ppm seed=27 isaac_seed=27 qs=8`. Reborn used seed 27, 12,000,000 samples and four workers, reading directly from the collection. The PPM SHA-256 digests were `f825374c18eacb1cf7e3f2b5efbabe78ba589f343fe640e815c6fa880c5610c2` for FLAM3 and `c2b650dff66c87d4a7769fbf18e0c1010e28572c3a56649ca9a220fca1b580ef` for Reborn.

Run `bash scripts/build.sh` for a native source build and the nine CTest entries. The separate historical corpus uses the commands in LEGACY_CORPUS.md. Git Bash on Windows runs the packaged executable checks with `bash scripts/verify-windows.sh`.

## 0.2.0 Core Engine — 17 November 2026

Environment: Debian 13, x86-64, GCC 14.2.0, CMake 4.4.5. The host reported Intel Xeon Platinum 8573C and nine schedulable CPUs. Windows targets cross-compiled separately with LLVM-MinGW. Reproduce the release checks with `bash scripts/build.sh` from a source checkout.

| CTest entry | Executed checks | Linux Release |
| --- | --- | --- |
| flam3_reference | 126 upstream numeric vectors, transform selection/chaos boundaries, independently generated full-raster FLAM3 regression | Passed |
| xml_import | Single/collection XML, defaults, matrices, packed palettes, wrapped color stops, malformed files, unsupported fields, semantic save/reopen | Passed |
| renderer | Exact projected point positions, final-transform isolation, opacity, palette interpolation, box filtering, singular points, worker determinism, sample and budget limits | Passed |
| cli_roundtrip | Same-path replacement, filenames with spaces, BMP header and length, seeded one/four-worker equality, preservation of a good file on invalid options | Passed |
| foundation_cli | Original positional command, 64 × 64 PPM, 100,000 samples | Passed |
| flame_file_cli | Ember Spiral from XML, 768 × 576 BMP, 100,000 samples | Passed |

All six also passed an independent Debug build with AddressSanitizer and UndefinedBehaviorSanitizer, GCC warnings enabled. LeakSanitizer could not run under the host tracer, so only leak detection was disabled for that run. Pillow independently decoded BMP and PPM outputs.

The 15 members of `examples/variation-studies.flame` were rendered using the CLI, seed 123456, 350,000 samples and two workers each. Every output decoded at 480 × 360. Each had visible variation geometry and zero non-finite steps. The engine also completed Ember Spiral at its declared quality, 7,962,624 samples. All requested samples plotted in that view.

The 192 × 144 external reference fixture measured mean RGB error 4.832694/255, RGB RMSE 6.465777/255 and mean-channel intensity correlation 0.984265 at 3,317,760 requested samples. See REFERENCES.md for exact inputs, independent output, definitions and thresholds.

### 0.2.0 local timing

Ember Spiral, 768 × 576, 7,962,624 samples, seed 123456, release executable. Wall time from process launch through file close, three runs per configuration on the same shared Linux host.

| CPU workers | Wall times (ms) | Median (ms) |
| ---: | --- | ---: |
| 1 | 1,064 / 1,072 / 1,038 | 1,064 |
| 4 | 357 / 357 / 343 | 357 |

Both outputs had identical bytes. BMP SHA-256: `82133ca765ddd8440d495c383c26da79c26af6185d3a7c76a273e9f14f22235e`. Timings characterize this flame on this host. Platform math libraries can change the last bits and eventually stochastic orbits. The Windows verification script compares two Windows outputs to each other.

### Open at the end of 0.2.0

- Execute the included x64 binaries on Windows 11 using `bash scripts/verify-windows.sh`. The script captures local test output and hashes in `logs/`. PE headers and imported DLL names were inspected here. This Linux host could not launch Wine's IPC server.
- Expand XML and render fixtures using authentic 2.x, 7X and AV applications and their files. Exercise original readers on Reborn exports and document unknown-field handling.
- Verify editor lifecycle, settings, queue/cancellation, GPU equivalence and image quality on actual hardware when those components exist.
- Perform the separate native 16,384 × 16,384 acceptance test following implementation of memory-aware high-resolution rendering. No 16K acceptance test was attempted in this checkpoint.

## 0.11.0 starting variations

The extension test compares 432 FLAM3 operation results, verifies the following RNG draw, exercises the historical blur streams and renders all 43 studies. The GPU kernel test covers 2,304 direct comparisons. The GPU render tests put every addition in both an ordinary transform and the final transform with strict backend selection. The stream tests compare mixtures that use both historical blur histories.

The desktop and file checks cover parameter edits, Save and reopen, 2.09 numeric IDs, native and legacy script access, and interpolation of parameters. The checked-in gallery uses the CPU renderer. Its twelve source flames can also be selected individually from the study library.
