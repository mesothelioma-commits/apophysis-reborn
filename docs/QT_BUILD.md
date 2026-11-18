# Native Qt desktop build

The 0.7.0 editor, creative tools and persistent render queue use Qt 6 Widgets and the same C++20 CPU/Vulkan rendering and legacy-document library as the CLI. The milestone binaries use Qt 6.8.2. The Windows editor and Qt test programs dynamically link their GUI runtime. The core CLI remains available and shares the packaged C++ and zlib runtimes. Keep the complete executable directory together.

## Build with an installed Qt development kit

Install a Qt 6 desktop development kit, a matching C++20 compiler, CMake 3.20 or newer, and Expat and zlib development files. Qt Core, Gui, Widgets and Test are required for a build with tests enabled. PNG export uses zlib directly; both editor and core-only builds require its headers and library, independently of Qt's own PNG support. On Windows, use the compiler shipped or supported by that Qt kit. Open Git Bash in the source directory and supply the kit prefix if CMake cannot discover it automatically.

```bash
export APO_QT_PREFIX=/path/to/Qt/6.8.2/your-desktop-kit
bash scripts/build-editor.sh
```

The script invokes the normal source build with `APO_BUILD_EDITOR=ON`, builds the editor and tests, and runs CTest. The Qt directory can also be supplied as `-DCMAKE_PREFIX_PATH=...`. Expat and zlib installed through vcpkg can use the existing `VCPKG_ROOT` handling in `scripts/build.sh`; install both for the selected target triplet. CMake links the core to `ZLIB::ZLIB`. A core-only build uses `bash scripts/build.sh -DAPO_BUILD_EDITOR=OFF` and still requires Expat and zlib.

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
cmake --build "$APO_EDITOR_BUILD/app" --parallel
python3 scripts/stage_editor_runtime.py \
  --qt-prefix "$APO_TARGET_QT" \
  --compiler-runtime "$APO_TOOLCHAIN/x86_64-w64-mingw32/bin" \
  --objdump "$APO_TOOLCHAIN/bin/llvm-objdump" \
  --output "$APO_EDITOR_BUILD/portable" \
  "$APO_EDITOR_BUILD/app/"*.exe
```

For an extracted Debian host Qt installation, set `LD_LIBRARY_PATH` to its `usr/lib/x86_64-linux-gnu` directory while running CMake and host Qt tools. This is specific to the isolated development environment. Installed Qt kits use their normal runtime lookup.

The runtime staging script requires all package dependencies to be extracted into the Qt prefix. Downloaded input names and checksums are in `licenses/qt/PACKAGES.sha256`. Its `qt.conf` keeps plugin lookup relative to the executable directory. The `runtime-dependencies.json` beside the editor lists the actual copied DLLs and Windows system imports. Copy the staged runtime to `bin/windows-x64` in a full source distribution and retain the examples, notices and corresponding source archives.

## Native and Windows verification

Native Qt tests can use the offscreen platform when no desktop is available. An extracted Debian kit also needs its platform plugin path.

```bash
export QT_QPA_PLATFORM=offscreen
export QT_QPA_PLATFORM_PLUGIN_PATH=/path/to/native-qt/usr/lib/x86_64-linux-gnu/qt6/plugins/platforms
ctest --test-dir build --output-on-failure
```

The Linux editor uses Qt 6.8.2 from Debian's `6.8.2+dfsg-9+deb13u2` packages. M7 registers **25 CTest entries**, including file rendering/checkpoints, streaming image codecs, bounded Vulkan hit streaming, CLI file workflows and the render queue. The final Release and ASan/UBSan execution records accompany the distribution; leak detection is disabled for the sanitizer run. The core-only build with Vulkan disabled has **19 CTest entries**: 16 pass and three device-execution checks explicitly skip. A skipped check does not establish GPU execution.

For historical comparison, the M6 Release build passed its 20 entries in 3.42 seconds and the M6 ASan/UBSan build passed the same 20 in 18.37 seconds. Its core-only Vulkan-disabled configuration had 15 entries: 13 passed and two skipped.

The compute tests execute through llvmpipe software Vulkan on this host. M7 compares dense and streamed GPU histograms and final pixels, tests partial GPU checkpoint resume and rejects an incompatible shader identity. Native acceptance also renders an actual 16384×16384 PNG RGBA16 through the queue, pauses with a committed checkpoint, resumes and records responsive event-loop heartbeats. These results verify the algorithms, compute/API path and bounded rendering workflow; physical GPU performance and Windows driver acceptance remain separate checks. The M6 native desktop interaction and `examples/gpu-palette-study.flame` remain part of the earlier verification history.

The M7 Windows editor and eighteen console executables cross-compile against the matched Qt headers and import libraries: **19 executables** in total. The staged runtime contains **52 files**, including **50 PE modules**. Its dependency audit resolves all **14,948 non-system imported symbols** against the exact copied libraries, with zero missing; `verification/windows-runtime-record.json` records the source/executable fingerprints. All staged executables match the final build. The Windows programs were compiled and audited, not executed. The system Vulkan loader is loaded dynamically, so the static import audit does not itself verify a Windows Vulkan driver. The prior M6 stage contained 15 executables, 48 files and 46 PE modules, with 13,232 resolved non-system symbols.

When ready to run that check on Windows 11, extract the complete milestone ZIP and run from Git Bash.

```bash
bash scripts/verify-editor-windows.sh
```

The script records the OS version and Vulkan discovery, runs the CPU/core and Qt tests, checks CPU fallback and checkpoint resume, then launches both the creative and queue smoke workflows explicitly with the CPU backend. The core script covers BMP, PPM, PNG and TIFF, true 16-bit output and transparent PNG/TIFF; the queue smoke writes PNG RGBA16 and TIFF RGB16 and checks UI heartbeats. It verifies every required output and inspects the saved original, random batch and mutation flame through the CLI. It keeps a log and artifacts under the project's `logs` and `out` directories. A GPU is not required, and the CPU script does not claim Vulkan execution. Add `--no-open` to suppress opening Explorer. The normal desktop program is `bin/windows-x64/apophysis-reborn.exe`.

The ordinary gate includes a 16K storage estimate. Request the actual 16K render and pause/resume acceptance explicitly when ready:

```bash
bash scripts/verify-editor-windows.sh --large-render --no-open
./bin/windows-x64/apophysis-reborn.exe --queue-smoke-test out/queue-smoke --backend cpu
./bin/windows-x64/apophysis-reborn.exe --large-render-smoke-test out/16k-smoke --backend cpu
```

The large workflow renders 16384×16384 PNG RGBA16 with 8,388,611 samples and a 128 MiB working-buffer budget. It retains checkpoint chunks and uses an 8 GiB disk budget; the planner checks free space before rendering. The `16k-report.json` records pause/checkpoint preservation, resumed completion and event-loop measurements. Its screenshots show the paused and completed queue and the editor. Working-buffer limits do not represent a cap on the entire Qt process or driver memory.

The creative workflow can also be invoked directly:

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

The native GPU smoke mode can also run directly:

```bash
./bin/windows-x64/apophysis-reborn.exe --gpu-smoke-test out/gpu-smoke --device 0
```

Choose `0` from your actual discovery inventory, or supply its ID. The mode requires Vulkan without fallback and writes `editor.png`, `preview.png`, `saved-example.flame`, `render-device.json` and `render-settings.png`. It checks actual backend identity and has a 120-second timeout. The M7 queue and 16K acceptance evidence is recorded separately from the historical M6 GPU screenshots. CUDA is not included. Windows execution and physical GPU acceptance remain pending until the corresponding scripts run on that hardware.
