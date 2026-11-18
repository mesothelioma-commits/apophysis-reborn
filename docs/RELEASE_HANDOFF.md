# Release handoff — 0.11.0

## Scope and release identity

Milestone 11 implements the twelve remaining [named starting variations](VARIATIONS.md). It includes the 0.10.2 [Windows dialog and Save fixes](WINDOWS_DIALOG_FREEZE.md). The [compatibility report](RELEASE_COMPATIBILITY.md) and [requirements register](requirements-status.json) track the remaining project scope.

Use the release records for the exact final source commit, executable hashes, test counts and measured timings. Earlier documents retain their original milestone measurements; a historical M7 large-image result is not a new M10 benchmark.

| Distribution | Contents and entry point |
| --- | --- |
| `apophysis-reborn-0.11.0-windows-x64-portable.zip` | Extracted folder `apophysis-reborn-0.11.0-windows-x64`; open `apophysis-reborn.exe` at its root. Includes the CLI, runtime dependency closure, example native plugin, offline documents, examples, notices and corresponding dependency source archives. |
| `apophysis-reborn-0.11.0-windows-x64-setup.exe` | Per-user installation of the runtime payload with versioned shortcuts and an uninstall entry. Default `%LOCALAPPDATA%\Programs\Apophysis Reborn\0.11.0`; no administrator elevation or file-association takeover. |
| `apophysis-reborn-0.11.0-starting-variations.zip` | Complete `apophysis-reborn` source tree, Git bundle, SDK/example sources, tests, build and verification scripts, Windows applications under `bin/windows-x64`, dependency sources and release evidence. Use this edition for the commands below. |
| Git source | Extract `apophysis-reborn-0.11.0-git-source.zip` and run `git clone apophysis-reborn.bundle restored-source` in the extracted `apophysis-reborn` directory. The default `main` branch contains the release with its history and tags. |

The complete edition contains `BUILD.txt`, `release-source.json`, `SOURCE_SHA256SUMS.txt`, `SHA256SUMS.txt` and `apophysis-reborn.bundle`. `release-source.json` identifies the release source commit; the tracked-source manifest lets configuration check the extracted source. `SHA256SUMS.txt` covers the packaged files according to its recorded inventory. Neither a checksum nor a successful dependency audit proves that an executable ran on Windows.

The command-line application exposes `--version` and `--build-info --json`. The GUI exposes **Help → System information…**, with **Copy report** and **Save report…**. Source state describes the source at CMake configuration time; it is not a live scan of files beside an already built executable. A build without usable Git or valid packaged source identity reports an unknown source state. Edited or missing tracked source must not be called a clean reconstruction of the release.

Build/system diagnostics do not dump environment variables or artwork contents. Opening the GUI dialog does not load GPU drivers just to enumerate them. For current device inventory, run `flame-cpu --list-devices --json` explicitly. A last-preview device in the GUI report describes that preview, not every installed GPU.

## Preserve and inspect the handoff

Keep the original archive and its published checksum alongside any local edits. In Git Bash, from the complete extracted project root:

```bash
sha256sum -c SHA256SUMS.txt
sha256sum -c SOURCE_SHA256SUMS.txt
git bundle list-heads apophysis-reborn.bundle
./bin/windows-x64/flame-cpu.exe --version
./bin/windows-x64/flame-cpu.exe --build-info --json
```

The source manifest intentionally covers tracked source; it is not the same inventory as the complete distribution checksum. To restore an editable Git checkout, choose a new sibling directory:

```bash
git clone apophysis-reborn.bundle ../apophysis-reborn-work
git -C ../apophysis-reborn-work status --short
git -C ../apophysis-reborn-work log -1 --format=fuller
```

Compare the restored revision with `release-source.json`. Keep packaged binaries and verification evidence available separately: they need not be tracked in the source repository. Do not copy an older executable into a newer release folder and assume the folder name supplies its identity.

To create a new source/history-only ZIP from a clean, complete Git checkout with its release tag already at `HEAD`:

```bash
python3 scripts/package-source.py --repo /path/to/clean-checkout \
  --output /path/to/new-source-history.zip --tag v0.11.0-starting-variations
```

The helper refuses a dirty or shallow checkout, a mismatched tag, unsafe/nonportable paths, existing output or output inside the checkout. It includes the tracked source, a Git bundle with history reachable from the local branches/tags, source identity and manifests. It does **not** add application binaries, SDK runtime staging or verification reports; assembling the complete maintenance edition adds those separately. Identical source/ref inputs and matching Git/Python/zlib versions define the helper's reproducibility scope. An optional `--version 0.11.0` asserts the committed CMake version.

## Build and launch from source

Install a matching C++20 compiler and Qt 6 desktop kit, CMake 3.20 or newer, Expat and zlib development files. With testing enabled, Qt Core, Gui, Widgets and Test are needed. Python 3.10 or newer is needed for the benchmark/release tooling; it is not an application runtime dependency. Git is needed to work with source history. The installer has additional development tools described below.

[QT_BUILD.md](QT_BUILD.md) records the Qt/compiler pairing and Windows cross-build. [WINDOWS_BUILD.md](WINDOWS_BUILD.md) records the standalone core build. Use a compiler and libraries with matching target architecture, C++ runtime and ABI. The packaged Windows build's exact dependency inputs belong to its recorded build, not an assurance that arbitrary Qt distributions are interchangeable.

The application includes Lua source, Vulkan headers and the compiled shader. An ordinary application build needs neither an external Lua installation nor the Vulkan SDK. Shader development still requires the compiler/validator workflow in [GPU_TESTING.md](GPU_TESTING.md).

First check the actual installed dependencies without compiling the application. Set the Qt prefix only if automatic discovery needs it:

```bash
export APO_QT_PREFIX=/path/to/Qt/your-matching-desktop-kit
bash scripts/check-deps.sh --no-open
```

The dependency script runs a disposable CMake configuration and retains its diagnostics. It does not download a toolchain or claim that application tests executed. `VCPKG_ROOT`, when set to an existing installation, selects its CMake toolchain. Ordinary additional CMake configure options can be passed to the dependency and build wrappers.

Build the editor and execute the configured CTest suite in a separate build directory:

```bash
bash scripts/build-editor.sh --build-dir build-release --jobs 2 --no-open
bash scripts/run-editor.sh --build-dir build-release --no-open
```

The build wrapper configures Release with tests enabled, builds and runs CTest. The default compiler/test concurrency is two jobs; `--jobs`, `APO_BUILD_JOBS` or the existing CMake parallel setting can change it. The wrapper accepts build directories with spaces when quoted. Multi-configuration Windows builds use the `Release` executable location. Launch an explicit executable or open a project with:

```bash
bash scripts/run-editor.sh --exe /path/to/apophysis-reborn.exe --no-open \
  -- /path/to/study.apoproject
```

For a core-only build with Vulkan excluded:

```bash
bash scripts/check-deps.sh --cpu-only --no-open
bash scripts/build.sh --build-dir build-cpu-only --jobs 2 --no-open \
  -DAPO_BUILD_EDITOR=OFF -DAPO_ENABLE_VULKAN=OFF
```

For an editor with Vulkan excluded, use `build-editor.sh` with `-DAPO_ENABLE_VULKAN=OFF`. Device-execution tests that cannot run must remain skips, not GPU passes. CPU reference, source-preservation, output and checkpoint checks still have value in that configuration.

Build, dependency and launch wrappers save readable `.txt` logs under `logs`. On Windows they select their log in Explorer when finished unless `--no-open` is given. Existing verification scripts and the benchmark/installer wrappers also retain their own logs or reports; the installer selects the successful output executable. These conveniences do not change command exit status. Run from Git Bash; no PowerShell conversion of the examples is required.

## Build the runtime and installer editions

Start with a complete, dependency-audited Windows runtime staging directory. [QT_BUILD.md](QT_BUILD.md) documents `scripts/stage_editor_runtime.py`, recursive DLL import checking, Qt plugin placement and the source/license inputs. Runtime packaging selects the app/CLI dependency closure and supplied SDK example; it does not copy the entire test suite into the runtime edition.

Use the exact source commit that built the staged executables. The following paths are inputs to substitute, and the output location must not already contain that version's payload or portable ZIP:

```bash
python3 scripts/package-runtime.py \
  --runtime /path/to/audited/windows-x64 \
  --output /path/to/new-runtime-release \
  --lgpl-sources /path/to/corresponding-third-party-sources \
  --objdump /path/to/llvm-objdump \
  --source-commit FULL_40_CHARACTER_SOURCE_COMMIT
```

The corresponding-source directory needs its supplied `SHA256SUMS.txt`; its archives are checked before packaging. The packager verifies the payload inventory and generated ZIP, rejects unsafe input/output paths, and emits `package-manifest.json` and payload `SHA256SUMS.txt`. Repeated packaging from identical inputs can use the same explicit `--source-date-epoch` to keep archive timestamps stable. This does not make differently compiled application binaries identical. Verify an existing payload without rebuilding it with:

```bash
python3 scripts/package-runtime.py --verify \
  /path/to/new-runtime-release/apophysis-reborn-0.11.0-windows-x64
```

The installer builder needs NSIS **3.13 or newer**, a MinGW-w64 C++20 compiler for its private Windows helper, and a PE-capable objdump. Its default compiler is `x86_64-w64-mingw32-g++`; select another compatible executable with `--compiler`. Select NSIS with `--makensis` or `MAKENSIS`, and objdump with `--objdump` or `APO_OBJDUMP`:

```bash
bash scripts/build-installer.sh \
  --payload /path/to/new-runtime-release/apophysis-reborn-0.11.0-windows-x64 \
  --output /path/to/new-runtime-release/apophysis-reborn-0.11.0-windows-x64-setup.exe \
  --compiler /path/to/x86_64-w64-mingw32-g++ \
  --makensis /path/to/makensis \
  --objdump /path/to/llvm-objdump \
  --work /path/to/new-installer-build-records \
  --no-open
```

`--work` names a new directory for generated inputs, the helper and `installer-build.json`. If omitted, the builder creates and reports one beside the output. The record includes the installer/payload/source hashes, compiler and NSIS identity, helper imports and signing/execution status. A successful compilation is still not an installer lifecycle test. Current release acceptance must separately identify any Windows or Wine execution and whether an Authenticode signature was applied.

The installer uses a version-specific directory and requires an empty destination. Other versions remain installed. Start Menu entries are versioned and the desktop shortcut is optional. Uninstall is constrained by the compiled payload inventory and hashes: modified or extra files remain instead of being recursively deleted. Settings, recovery data and projects outside the installation remain. An interrupted installation can leave a partial version folder; automatic replacement or cleanup is refused. Inspect that folder before removing only its partial program payload, or choose another empty destination. The installer has no automatic repair or old-version removal workflow. Test those rules on Windows before treating lifecycle acceptance as complete.

## Regression and measurement gates

The authoritative executed results are [TEST_PLAN.md](TEST_PLAN.md) and the full distribution's `verification/validation-summary.json`, `distribution-verification.json`, `windows-runtime-record.json`, `extracted-source-verification.json` and retained individual logs. Native GUI smoke evidence and benchmarks have their own output directories. Evidence must identify the executable it actually used.

The release CTest configuration also runs Python checks of source/package identity, packaging, benchmarks and the development wrappers. The identity fixture uses CMake with `LANGUAGES NONE`; the wrapper fixture copies scripts into a temporary project and substitutes argument-recording tools. Neither compiles another application or writes logs into the original checkout. The two bounded tooling suites can also be run directly:

```bash
python3 tests/build_identity_tests.py --cmake /path/to/cmake
python3 tests/build_scripts_tests.py
```

`APO_TEST_CMAKE` can provide the identity suite's CMake path; otherwise it uses `cmake`. The wrapper suite needs Bash and accepts `APO_TEST_BASH` when the executable is not on `PATH`. Actual Windows application and installer acceptance remain separate from these fixture tests.

| Gate | Required interpretation |
| --- | --- |
| Native CTest | Preserve mathematical/original-source references, malformed-file behavior, source round trips, editor transactions, scripting/plugins, animation, encoders and checkpoints while changing the product. |
| Core-only build | Establish that CPU use does not require Qt or a working Vulkan driver. Report device skips explicitly. |
| Qt smoke workflows | Exercise real widgets/event processing, private dialogs, queue publication and recovery; retain screenshots and reports. Offscreen checks do not establish Windows DPI or multi-monitor behavior. |
| Windows PE/import audit | Establish that staged dependencies and imported symbols resolve. It does not launch the executable or validate a graphics driver. |
| Extracted-source rebuild | Establish that the delivered source and metadata reconstruct the application/test configuration, rather than relying on unpublished workspace files. |
| Runtime/installer packaging | Verify inventory, checksums, dependencies and source/license materials. Windows lifecycle behavior needs separate execution. |
| Benchmarks | Record actual workload/build/host/backend/device, repeated measurements and output identities. Avoid invented hardware speedups or extrapolated finished-image quality. |

On a Windows 11 machine, run these from the complete source/history edition:

```bash
bash scripts/verify-windows.sh --no-open
bash scripts/verify-editor-windows.sh --no-open
```

The desktop wrapper already includes the core gate, so a single desktop run is enough when all its prerequisites are present. The explicit core command is useful when isolating a core failure. For an actual large queue/render exercise, allocate the documented disk space and run:

```bash
bash scripts/verify-editor-windows.sh --large-render --no-open
```

That path produces a genuine 16384×16384 RGBA16 image and queue pause/resume evidence; an estimate alone does not count. [HIGH_RES_ACCEPTANCE.md](HIGH_RES_ACCEPTANCE.md) explains the additional independent full-decode, output, memory and UI checks behind the historical M7 acceptance. A new benchmark header/hash check does not replace that decoder evidence.

For the separate installer lifecycle gate, use the setup executable and its matching extracted portable payload, and choose a new output directory:

```bash
bash scripts/verify-installer-windows.sh \
  --installer /c/release/apophysis-reborn-0.11.0-windows-x64-setup.exe \
  --payload /c/release/apophysis-reborn-0.11.0-windows-x64 \
  --out /c/temp/apo-installer-check --no-open
```

This Git Bash wrapper handles path conversion and launches Windows PowerShell 5.1 or newer. The gate refuses an already registered same version or existing versioned shortcuts. It retains JSON/text evidence and deliberate retained-file fixtures while exercising nonempty/junction/shortcut conflicts, install paths with spaces/Unicode, locked-file removal, changed/extra/read-only/hard-linked file preservation, reinstall and optional shortcuts. It does not launch the editor or recursively clean unrelated user data. Use separate application launch/DPI checks as well. [Packaging and lifecycle contract](../packaging/README.md) gives the full scope; compiling or syntax-checking these tools is not Windows execution.

Record a device inventory before selecting a strict hardware gate:

```bash
./bin/windows-x64/flame-cpu.exe --list-devices --json
bash scripts/verify-gpu-windows.sh --device DEVICE_ID_OR_INDEX --no-open
```

Choose an actual reported physical device. A software Vulkan implementation remains explicitly classified as software even when chosen deliberately. Do not accept a CPU fallback as a GPU pass. The release still needs Windows 11 execution and representative physical NVIDIA, AMD and Intel results; neither this host's software Vulkan nor Wine substitutes for those systems.

For repeatable performance measurements, close competing compiler/render work and run the finished CLI:

```bash
bash scripts/benchmark.sh --exe ./bin/windows-x64/flame-cpu.exe \
  --backend cpu --threads 1,2,4 --repeats 3 --warmups 1 --no-open

bash scripts/benchmark.sh --exe ./bin/windows-x64/flame-cpu.exe \
  --backend both --device DEVICE_ID_OR_INDEX --repeats 3 --warmups 1 --no-open
```

Use `--output-dir` only with a new directory. The runner keeps `report.txt`, `report.json`, exact arguments, stdout/stderr, per-process observations and input/output hashes. Its total-process timings include startup, rendering, encoding and disk publication. Resident memory is separate from renderer-buffer budgets and GPU VRAM. `--large-16k` adds one large observation per selected backend; `--keep-images` retains image files. Read [BENCHMARKS.md](BENCHMARKS.md) before comparing reports or interpreting memory completeness.

## Pending target acceptance

The user can test later; these steps define the outstanding evidence rather than make continued development depend on an immediate run. Keep one report per build/device/configuration, including failures.

| Target exercise | Observe and retain |
| --- | --- |
| Fresh portable launch | Launch from an extracted folder with spaces and a non-ASCII path; open the manual, save/copy diagnostics, open a sample, edit/undo, save/reopen, render and exit. Preserve the actual OS/build identities. |
| Installer lifecycle | Install as a standard user; confirm the empty-target rule, optional shortcuts and versioned entries. Start the installed program. Keep a prior version installed while adding the new one. Verify uninstall retains modified payload files, added files and external projects/settings, while removing unchanged installed files and that version's registration. |
| DPI and multiple monitors | Move the editor and open dialogs between monitors at distinct scale factors; check readable text, usable hit targets, saved geometry, docks, preview and F1 manual. Record scaling and screenshots. |
| CPU editing/rendering | Run the supplied core/desktop gates, then check representative original files and source-preserving saves. Reopen supported exports in the original target application where available. |
| Physical Vulkan | Record adapter/driver identity; run strict device tests, streamed exports, checkpoint recovery and queued output. Compare against the documented tolerance/precision contract, not a promise of CPU/Vulkan pixel identity. |
| Large output | Run the large queue case, independently decode the entire output, inspect dimensions/depth/alpha/seams/EOF, and retain memory, responsiveness and cancellation/resume observations. |

Run failure cases on disposable artwork/output copies. Do not erase prior evidence when a failure is found. A corrected build gets a new identity and its affected gates must be rerun. A static audit, absent device or skipped test must keep that status in the final record.

## Data continuity and next engineering work

Program version directories are separate, but normal sessions use the same per-user application settings/data locations. Portable mode is not a separate profile system. Save artwork outside the program folder. Back up the source project, queue manifest and checkpoint directory together when preserving in-progress work; retain the original executable and exact plugin binaries as well. Queue/sequence identity checks can reject a different build or plugin hash, and must not be bypassed to force an incompatible resume. Completed images remain useful independent files.

When extending a supported file format, preserve unknown source/envelope data and supply explicit blockers for unsupported calculations. When extending animation or scripts, keep private evaluation and model commit separate from external files and admitted render jobs. Tests of these boundaries already protect real user work and remain required.

The next implementation work should be selected from [RELEASE_COMPATIBILITY.md](RELEASE_COMPATIBILITY.md), not inferred from the milestone number. In particular, the brief’s 40 named starting variations are covered, and the complete historical inventory is larger. Active density estimation, broader filters/3D/direct-color behavior, additional image encoders and metadata/color management, fuller legacy scripting, preferences/workspace features and fault isolation remain open. Each addition needs original-source semantics, an explicit supported scope, examples and regression evidence before its requirement row is upgraded.

Keep [DEVELOPMENT_LOG.md](DEVELOPMENT_LOG.md), [FEATURE_MATRIX.md](FEATURE_MATRIX.md), [TEST_PLAN.md](TEST_PLAN.md), the user manual and both compatibility records synchronized with implemented behavior. Retain historical reports with their original revision and hardware labels. Full-parity release acceptance requires closing or explicitly agreeing changes to the original requirements; completing packaging alone does not do that.
