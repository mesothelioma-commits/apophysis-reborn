# Native Qt desktop build — 0.10.0

The 0.10.0 editor adds embedded offline help/search and system diagnostics to the native editing, creative, queue, extension and animation workflows. It uses Qt 6 Widgets and the same C++20 libraries as the CLI. The packaged Qt kit is 6.8.2; the Windows editor and Qt test programs dynamically link their GUI runtime, while the CLI shares the packaged C++/zlib runtime. Keep the executable directory and its plugin subdirectories together. The [user manual](USER_MANUAL.md) describes normal use; [RELEASE_HANDOFF.md](RELEASE_HANDOFF.md) defines source/runtime/installer packaging and final evidence.

The complete source/history edition keeps applications and all test executables under `bin/windows-x64`. The lean portable and per-user installer editions place the application/CLI at the runtime root and exclude test-only executables/modules. The Windows desktop verification scripts below require the complete edition. They are not a substitute for actual Windows 11 or physical-GPU execution. [ANIMATION_EDITOR.md](ANIMATION_EDITOR.md), [ANIMATION_CLI.md](ANIMATION_CLI.md) and [COLOR_TOOLS.md](COLOR_TOOLS.md) retain the M9 feature contracts.

Lua 5.5.1 is built from the vendored sources as the static `apo-lua` library and linked into the core. End users need no separate Lua installation or Lua DLL. The selected upstream sources are compiled as C++ so Lua's protected errors unwind C++ host callbacks correctly; do not change that build to C/`longjmp` without revising the integration. The complete MIT notice ships in [licenses/lua/LICENSE.txt](../licenses/lua/LICENSE.txt) and beside the source in [third_party/lua/LICENSE](../third_party/lua/LICENSE), with version, source hashes and build details in [third_party/lua/README.md](../third_party/lua/README.md).

## Build with an installed Qt development kit

Install a Qt 6.5 or newer desktop development kit, a matching C++20 compiler, CMake 3.20 or newer, and Expat and zlib development files. Qt Core, Gui, Widgets and Test, plus **Python 3.10 or newer**, are required when `BUILD_TESTING=ON`. The six Python release suites also exercise Git history and Bash wrappers. Python is not an application runtime dependency; an application-only CMake configuration can disable testing. PNG export uses zlib directly, so both editor and core-only builds need its headers/library independently of Qt's own PNG support. On Windows, use the compiler supported by the selected Qt kit. Open Git Bash and supply the kit prefix if discovery needs it.

```bash
export APO_QT_PREFIX=/path/to/Qt/6.8.2/your-desktop-kit
bash scripts/check-deps.sh --no-open
bash scripts/build-editor.sh --build-dir build-release --jobs 2 --no-open
bash scripts/run-editor.sh --build-dir build-release --no-open
```

The dependency wrapper makes a disposable configuration, retains its diagnostics and downloads nothing. The editor wrapper invokes the normal source build with `APO_BUILD_EDITOR=ON`, builds the editor/tests and runs CTest. `--build-dir` selects an explicit build location; `--jobs` accepts 1–256 and defaults to two. `APO_BUILD_DIR`, `APO_BUILD_JOBS` and the existing CMake parallel setting can supply defaults. Each wrapper retains a `.txt` log; omitting `--no-open` selects it in Explorer on Windows. To launch a specific executable and project, use `bash scripts/run-editor.sh --exe PATH --no-open -- PROJECT`.

The Qt directory can also be supplied as `-DCMAKE_PREFIX_PATH=...`. Expat/zlib installed through vcpkg can use `VCPKG_ROOT`; install both for the selected triplet. CMake links the core to `ZLIB::ZLIB`. A core-only build uses `bash scripts/build.sh --build-dir build-core --jobs 2 -DAPO_BUILD_EDITOR=OFF`; it still requires Expat/zlib and Python when tests are enabled. Both configurations build the `sample_wave` SDK example under the selected build's `plugins/reborn-wave`; tests add the valid/incompatible-ABI fixture modules. Lua is built from the included source in either configuration.

### Build/source identity and embedded help

`CMakeLists.txt` supplies the central version. `cmake/BuildIdentity.cmake` derives source state from the actual Git checkout or the extracted release's `release-source.json` and hash-bound `SOURCE_SHA256SUMS.txt`. A raw export reports unknown source state, and a revision stamp alone cannot establish clean extracted source. Changed/missing tracked files are marked dirty; invalid stamps and path/manifest tampering fail configuration. Source state is scoped to configuration, with declared dependencies triggering reconfiguration after tracked/shipped input changes. Keep the matching release metadata when building an archive.

The CLI/editor `--version` and `--build-info --json` expose this identity and configured compiler/target/features. They do not claim a GPU driver is usable. `docs/USER_MANUAL.html` is compiled into the Qt resources, so **Help → User manual** works without a current-directory file lookup or network assets. **Help → System information…** reports runtime Qt/OS/CPU information, requested preview settings and the last actual backend/device; it does not probe drivers when opened. Both dialogs have native regression coverage.

## Vulkan build and runtime

`APO_ENABLE_VULKAN` defaults to `ON`. The repository includes the pinned Vulkan headers under `third_party/vulkan`, readable GLSL in `shaders/orbit.comp`, and the validated embedded SPIR-V in `src/generated/orbit_spv.hpp`. An ordinary build therefore needs neither the Vulkan SDK nor a shader compiler. The application loads the platform Vulkan loader dynamically; on Windows it uses the system `vulkan-1.dll` supplied by the graphics-driver installation. The portable package does not replace that loader. Missing or unsuitable Vulkan support leaves the CPU renderer available.

The complete runtime can be disabled while retaining the editor:

```bash
bash scripts/build-editor.sh -DAPO_ENABLE_VULKAN=OFF
```

For a core-only, Vulkan-disabled build, use `bash scripts/build.sh -DAPO_BUILD_EDITOR=OFF -DAPO_ENABLE_VULKAN=OFF`. The device-execution tests report skipped when Vulkan cannot run; this must not be interpreted as a passed GPU execution check. Portable packing/random-stream checks and CPU fallback checks remain useful in this configuration.

After changing the shader, regenerate and validate the embedded module with the pinned glslang/SPIRV-Tools versions recorded in [GPU_TESTING.md](GPU_TESTING.md). These tools are development dependencies for shader edits, not application runtime dependencies.

```bash
python3 scripts/compile_shader.py
python3 scripts/compile_shader.py --check
```

The script targets Vulkan 1.0, runs `spirv-val` and records source/module hashes in the generated header. `--compiler` and `--validator` can supply explicit executable paths. The [kernel guide](GPU_KERNEL.md) describes the current FP32 and atomic requirements; [GPU_RENDERING.md](GPU_RENDERING.md) describes selection, limits and fallback.

## Packaged Windows toolchain

The release cross-build combines the LLVM-MinGW 20261104 UCRT x86-64 toolchain, static Expat 2.10.0, and MSYS2's clang64 Qt 6.8.2 package. The zlib development files and `zlib1.dll` come from `mingw-w64-clang-x86_64-zlib-1.3.1-1-any.pkg.tar.zst`, already part of the Qt dependency prefix. Native Linux Qt 6.8.2 supplies the matching `moc`, `uic` and `rcc` host tools. Windows Qt headers, import libraries and DLLs come from `mingw-w64-clang-x86_64-qt6-base-6.8.2-1-any.pkg.tar.zst`. The Qt source package and all MSYS2 build patches are included in the release's `third-party-sources` directory. The prior toolchain and Expat provenance are in WINDOWS_BUILD.md. [Qt cross-compilation documentation](https://doc.qt.io/qt-6.8/cross-compiling-qt.html) · [Windows Qt package](https://repo.msys2.org/mingw/clang64/mingw-w64-clang-x86_64-qt6-base-6.8.2-1-any.pkg.tar.zst)

The Windows GUI DLLs use MSYS2 libc++ 23.1.5. Its exported unwind entry points are required by the distributed Qt build. The staging script checks each imported non-system symbol against the chosen DLL's exports. It also resolves the recursive dependencies of the Windows platform, offscreen testing, Windows style and JPEG plugins.

This is the toolchain configuration used for the editor. Set each environment variable to an unpacked dependency prefix. On Debian, `APO_HOST_QT_CMAKE` is the multiarch CMake directory under the native Qt prefix.

```bash
export APO_TOOLCHAIN=/path/to/llvm-mingw-20261104-ucrt-ubuntu-22.04-x86_64
export APO_TARGET_QT=/path/to/msys2-package-root/clang64
export APO_HOST_QT=/path/to/native-qt/usr
export APO_HOST_QT_CMAKE="$APO_HOST_QT/lib/x86_64-linux-gnu/cmake"
export APO_WIN_DEPS=/path/to/installed-windows-expat
export APO_EDITOR_BUILD="$(mktemp -d)"
cat > "$APO_EDITOR_BUILD/win64.cmake" <<'CMAKE'
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)
set(CMAKE_C_COMPILER "$ENV{APO_TOOLCHAIN}/bin/x86_64-w64-mingw32-clang")
set(CMAKE_CXX_COMPILER "$ENV{APO_TOOLCHAIN}/bin/x86_64-w64-mingw32-clang++")
set(CMAKE_RC_COMPILER "$ENV{APO_TOOLCHAIN}/bin/x86_64-w64-mingw32-windres")
set(CMAKE_FIND_ROOT_PATH "$ENV{APO_TARGET_QT}" "$ENV{APO_WIN_DEPS}" "$ENV{APO_TOOLCHAIN}/x86_64-w64-mingw32")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_PREFIX_PATH "$ENV{APO_TARGET_QT}" "$ENV{APO_WIN_DEPS}")
set(QT_HOST_PATH "$ENV{APO_HOST_QT}" CACHE PATH "")
set(QT_HOST_PATH_CMAKE_DIR "$ENV{APO_HOST_QT_CMAKE}" CACHE PATH "")
CMAKE
cmake -S . -B "$APO_EDITOR_BUILD/app" \
  -DCMAKE_TOOLCHAIN_FILE="$APO_EDITOR_BUILD/win64.cmake" \
  -DCMAKE_BUILD_TYPE=Release -DAPO_BUILD_EDITOR=ON -DBUILD_TESTING=ON
cmake --build "$APO_EDITOR_BUILD/app" --parallel 2
python3 scripts/stage_editor_runtime.py \
  --qt-prefix "$APO_TARGET_QT" \
  --compiler-runtime "$APO_TOOLCHAIN/x86_64-w64-mingw32/bin" \
  --objdump "$APO_TOOLCHAIN/bin/llvm-objdump" \
  --output "$APO_EDITOR_BUILD/portable" \
  --module "$APO_EDITOR_BUILD/app/plugins/reborn-wave/reborn_wave.dll" \
           plugins/reborn-wave/reborn_wave.dll \
  --data "$APO_EDITOR_BUILD/app/plugins/reborn-wave/reborn_wave.apoplugin" \
         plugins/reborn-wave/reborn_wave.apoplugin \
  --module "$APO_EDITOR_BUILD/app/libplugin-fixture.dll" \
           tests/plugins/libplugin-fixture.dll \
  --module "$APO_EDITOR_BUILD/app/libplugin-bad-abi.dll" \
           tests/plugins/libplugin-bad-abi.dll \
  "$APO_EDITOR_BUILD/app/"*.exe
```

For an extracted Debian host Qt installation, set `LD_LIBRARY_PATH` to its `usr/lib/x86_64-linux-gnu` directory while running CMake and host Qt tools. This is specific to the isolated development environment. Installed Qt kits use their normal runtime lookup.

The runtime staging script requires all package dependencies to be extracted into the Qt prefix. Downloaded input names and checksums are in `licenses/qt/PACKAGES.sha256`. Repeatable `--module SOURCE RELATIVE` arguments add PE roots at the requested relative locations and include their dependency checks; repeatable `--data SOURCE RELATIVE` arguments copy sidecars or other data without loading them. Relative destinations use `/`, and traversal, conflicting file/directory destinations and output symlink escapes are rejected before payloads are copied. The script checks imported non-system symbols; it does not execute the Windows files. Its path/dependency regression checks can be run with `python3 tests/staging_tests.py`.

The generated `qt.conf` keeps Qt plugin lookup relative to the executable directory. `runtime-dependencies.json` lists the actual staged files, data files and Windows system imports. Copy the staged runtime to `bin/windows-x64` in a full source distribution and retain the examples, notices and corresponding source archives. The native variation sample is `bin/windows-x64/plugins/reborn-wave/reborn_wave.dll` with its adjacent `.apoplugin` sidecar. Test-only modules are `bin/windows-x64/tests/plugins/libplugin-fixture.dll` and `libplugin-bad-abi.dll`; the plugin test executable supplies their explicit paths.

## Native and Windows verification

Native Qt tests can use the offscreen platform when no desktop is available. An extracted Debian kit also needs its platform plugin path.

```bash
export QT_QPA_PLATFORM=offscreen
export QT_QPA_PLATFORM_PLUGIN_PATH=/path/to/native-qt/usr/lib/x86_64-linux-gnu/qt6/plugins/platforms
ctest --test-dir build --output-on-failure
```

The Linux editor kit uses Qt 6.8.2 from Debian's `6.8.2+dfsg-9+deb13u2` packages. M10 adds `support_editor`, `editor_startup`, `build_info_cli` and six `release_*` Python CTest entries to the earlier animation, source, scripting/plugin, numerical/rendering and queue gates. The configured inventory is 48 full entries or 36 core-only entries; a Vulkan-disabled core run expects three device-execution skips. The complete Windows configuration adds `support-editor-tests.exe` for 32 application/test executables. These counts describe registered targets, not executed passes or final runtime file totals. [TEST_PLAN.md](TEST_PLAN.md), `BUILD.txt` and the packaged verification records contain the actual final outcomes and identities.

Windows cross-compilation and static dependency auditing are distinct from Windows runtime execution. Available Vulkan execution on the development host uses software Vulkan and does not establish physical-GPU performance or target-driver acceptance. Use the target-system checks below to collect that separate evidence. The current benchmark runner and installer lifecycle checks are described in [BENCHMARKS.md](BENCHMARKS.md) and [RELEASE_HANDOFF.md](RELEASE_HANDOFF.md).

### Historical M8 measurements

M8 introduced project-envelope, Lua/legacy script, script-output boundary, UGR/MAP/UPR and native plugin tests, plus Qt extension and plugin-dialog tests. Its GUI checks covered private script execution, one-step document undo, queue batches, source/recovery protection, plugin approvals and fingerprint-checked queue restoration.

The final M8 native Release suite passed **33/33 CTest entries in 6.09 seconds**. A separate ASan/UBSan build passed **33/33 in 48.83 seconds** with leak detection disabled; this does not establish a leak-check result. The Vulkan-disabled core-only build passed **22 checks** and skipped **three device-execution checks**, for **25 entries in 1.95 seconds**. [TEST_PLAN.md](TEST_PLAN.md) records the options, execution scope and limits. Full logs are retained in the package's `verification` directory; a registered test or a cross-compiled test executable alone is not an execution result.

The complete M8 Windows build produced **26 executables**: the CLI, sixteen core test programs, the editor and eight Qt test programs. It also produced the SDK example DLL/manifest and two plugin test DLLs listed above. Lua was statically linked, while the chosen Qt, C++ and zlib runtime files remained beside the executables. The final M8 stage contained **63 runtime files**, including **60 PE modules**. Its recursive dependency audit resolved **19,959 non-system imported symbols**, with **zero missing**. These totals describe M8; the current package's executable/plugin fingerprints and audit totals are recorded in `verification/windows-runtime-record.json`.

The Windows binaries are cross-compiled and statically audited here, with **no Windows runtime execution** in this environment. The audit checks the packaged imports and exports; the dynamically loaded system Vulkan driver requires a separate Windows execution check.

### Historical M7 and M6 measurements

M7 registered **25 CTest entries**, including file rendering/checkpoints, streaming image codecs, bounded Vulkan hit streaming, CLI file workflows and the render queue. Its separate core-only, Vulkan-disabled configuration had **19 CTest entries**: 16 passed and three device-execution checks explicitly skipped. The M7 Release and ASan/UBSan records describe that earlier source revision; leak detection was disabled for the sanitizer run. A skipped check does not establish GPU execution.

For historical comparison, the M6 Release build passed its 20 entries in 3.42 seconds and the M6 ASan/UBSan build passed the same 20 in 18.37 seconds. Its core-only Vulkan-disabled configuration had 15 entries: 13 passed and two skipped.

The historical compute executions used llvmpipe software Vulkan on this host. M7 compared dense and streamed GPU histograms and final pixels, tested partial GPU checkpoint resume and rejected an incompatible shader identity. Its native acceptance also rendered an actual 16384×16384 PNG RGBA16 through the queue, paused with a committed checkpoint, resumed and recorded responsive event-loop heartbeats. Those results verify the earlier algorithms, compute/API path and bounded rendering workflow; they do not establish physical GPU performance or Windows driver acceptance. The M6 native desktop interaction and `examples/gpu-palette-study.flame` remain part of that earlier verification history.

The historical M7 Windows stage contained **19 executables**, **52 files** and **50 PE modules**. Its dependency audit resolved **14,948 non-system imported symbols** against its copied libraries with zero missing. The prior M6 stage contained 15 executables, 48 files and 46 PE modules, with 13,232 resolved non-system symbols. These are historical stage measurements, not the current release's audit totals. Neither stage established Windows runtime execution. Retained M7 full-16K long runs remain historical evidence; a new measurement needs its own build and result records.

### Run the current Windows checks

When ready to run the check on Windows 11, extract the complete source/history ZIP and run from Git Bash. The lean runtime edition does not include all verification executables/fixtures.

```bash
bash scripts/verify-editor-windows.sh
```

The script records build/OS identity and Vulkan discovery, runs the CPU/core and Qt tests, checks CPU fallback/checkpoint resume, then launches creative, queue, extension, animation and support smoke workflows explicitly with the CPU backend. It enables only the supplied SDK/test modules for their plugin checks. Core workflows exercise native projects, modern/legacy scripts, UGR/MAP/UPR exchange, animation and streamed image formats/depth/alpha. The queue checks UI heartbeats; the support smoke checks embedded manual content and saved diagnostics, while `support-editor-tests.exe` exercises search. Required outputs are checked and saved documents are inspected through the CLI. Logs stay under `logs`, with a unique run directory under `out` and separate `creative/`, `queue/`, `extensions/`, `animation/` and `support/` subdirectories. A GPU is not required and CPU fallback is not represented as Vulkan execution. Add `--no-open` to suppress Explorer. The full edition's desktop program is `bin/windows-x64/apophysis-reborn.exe`.

Run the M10 support workflow directly into a new or empty output directory with:

```bash
./bin/windows-x64/apophysis-reborn.exe \
  --support-smoke-test out/support-smoke --backend cpu
```

It writes `user-manual.png`, `system-information.png`, `system-information.json`, `system-information.txt` and `support-report.json`. Smoke modes use isolated application state, reject a nonempty output directory and do not combine with a user document argument. Keep each run's output in its own directory.

The current verification scripts also run the M9 core/CLI animation and curve checks, Qt timeline/color tests and the CPU animation smoke workflow. To run the M9 desktop workflow directly from the extracted project root:

```bash
./bin/windows-x64/apophysis-reborn.exe \
  --animation-smoke-test out/animation-smoke --backend cpu
```

The [timeline guide](ANIMATION_EDITOR.md) and [color guide](COLOR_TOOLS.md) describe what those controls do. The smoke workflow uses isolated application settings and exercises native-project saving and queued frame output; compiling it does not establish Windows execution.

The retained M8 workflow can also run directly from the extracted project root:

```bash
./bin/windows-x64/apophysis-reborn.exe \
  --extensions-smoke-test out/extensions-smoke \
  --plugin-manifest bin/windows-x64/plugins/reborn-wave/reborn_wave.apoplugin \
  --backend cpu
```

This explicit plugin argument trusts the bundled sample's native code for the smoke run. The workflow exercises the actual script editor, plugin manager and render queue; it saves a native project, executes a supported legacy script and exports UGR/MAP palettes. Its report identifies the loaded module fingerprint and actual render backend.

| M8 smoke outputs | Files |
| --- | --- |
| Window screenshots | `script-editor.png`, `plugin-manager.png`, `script-render-queue.png`, `extensions-editor.png` |
| Report / native project | `extensions-report.json`, `example.apoproject` |
| Scripted output | `script-export.flame`, `script-palette.gpl`, `script-render.png` |
| Legacy script result | `legacy-script.flame`, `legacy-translated.lua` |
| Palette exchange | `palette.ugr`, `palette.map` |

The ordinary gate includes a 16K storage estimate. Request the actual 16K render and pause/resume acceptance explicitly when ready:

```bash
bash scripts/verify-editor-windows.sh --large-render --no-open
./bin/windows-x64/apophysis-reborn.exe --queue-smoke-test out/queue-smoke --backend cpu
./bin/windows-x64/apophysis-reborn.exe --large-render-smoke-test out/16k-smoke --backend cpu
```

The large workflow renders 16384×16384 PNG RGBA16 with 8,388,611 samples and a 128 MiB working-buffer budget. It retains checkpoint chunks and uses an 8 GiB disk budget; the planner checks free space before rendering. The `16k-report.json` records pause/checkpoint preservation, resumed completion and event-loop measurements. Its screenshots show the paused and completed queue and the editor. Working-buffer limits do not represent a cap on the entire Qt process or driver memory.

The creative workflow can also be invoked directly into its own new or empty directory:

```bash
./bin/windows-x64/apophysis-reborn.exe --creative-smoke-test out/creative-smoke
```

It waits for the main preview, renders the Gradient, Adjustment, Mutation and Random dialogs, waits for their previews and candidate thumbnails, writes the following files, and exits with a status code. It has a 120-second timeout. The original `--smoke-test DIR` remains available for the main-editor-only workflow.

| Outputs | Files |
| --- | --- |
| Window screenshots | `editor.png`, `gradient-editor.png`, `adjustment-editor.png`, `mutation-explorer.png`, `random-explorer.png` |
| Preview image | `preview.png` |
| Flame examples | `saved-example.flame`, `gradient-example.flame`, `adjustment-example.flame`, `mutation-example.flame`, `random-batch.flame` |
| Palette exchange | `palette.gpl` |

## Strict Vulkan checks

When ready to execute the Windows GPU gate, run:

```bash
bash scripts/verify-gpu-windows.sh
```

This optional script selects a usable physical GPU by default and requires actual Vulkan execution from the formula tests, dense rendering comparisons, bounded hit-stream tests, exact file reconstruction and partial checkpoint resume. The same selected device ID is passed to every test, CLI export and desktop/queue smoke. It also writes PNG RGBA16 and queued PNG/TIFF 16-bit exports. `--device ID` or a zero-based index selects a specific device; an explicitly selected software implementation remains labeled as software. A skipped compute test, missing device or fallback fails the strict gate. The [Windows GPU guide](WINDOWS_GPU.md) lists its outputs, memory option and device-selection details.

The strict wrapper keeps its preview artifacts under the run's `preview/` subdirectory, separately from queued outputs. The native GPU smoke mode can also run directly:

```bash
./bin/windows-x64/apophysis-reborn.exe --gpu-smoke-test out/gpu-smoke --device 0
```

Choose `0` from your actual discovery inventory, or supply its ID. The mode requires Vulkan without fallback and writes `editor.png`, `preview.png`, `saved-example.flame`, `render-device.json` and `render-settings.png`. It checks actual backend identity and has a 120-second timeout. The M7 queue and 16K acceptance evidence is recorded separately from the historical M6 GPU screenshots. CUDA is not included. Windows execution and physical GPU acceptance remain pending until the corresponding scripts run on that hardware.
