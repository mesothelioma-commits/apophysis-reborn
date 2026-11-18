# Windows x64 core build — 0.8.0

The desktop editor build is documented in [QT_BUILD.md](QT_BUILD.md). These commands reproduce the core CLI and tests with the editor disabled. The M8 core adds native `.apoproject` files, embedded Lua and a supported legacy script subset, trusted native variation plugins, and UGR/MAP/Apophysis-UPR exchange to CPU/Vulkan rendering, bounded reconstruction, checkpoints and BMP/PPM/PNG/TIFF exports. Both core and editor builds require Expat and zlib development files; `APO_ENABLE_VULKAN=OFF` can disable the Vulkan runtime.

Lua 5.5.1 is built from `third_party/lua/src` as a static library. A separate Lua installation or DLL is not needed. Its complete MIT notice ships in [licenses/lua/LICENSE.txt](../licenses/lua/LICENSE.txt) and [third_party/lua/LICENSE](../third_party/lua/LICENSE); the pinned source and C++ compilation requirements are recorded in [third_party/lua/README.md](../third_party/lua/README.md). Retain that notice and source provenance in redistributed packages.

Unzip the whole package. Open Git Bash in the extracted project directory and run the Windows checks.

```bash
bash scripts/verify-windows.sh
```

For a single render, use the executable with the included flame.

```bash
./bin/windows-x64/flame-cpu.exe --flame examples/ember-spiral.flame \
  --samples 2000000 --seed 123456 --threads 4 --output ember.bmp
```

## M8 compatibility workflows

Run these from the extracted project root with new output filenames:

```bash
mkdir -p out/m8
./bin/windows-x64/flame-cpu.exe project create examples/ember-spiral.flame \
  --output out/m8/study.apoproject
./bin/windows-x64/flame-cpu.exe project inspect out/m8/study.apoproject
./bin/windows-x64/flame-cpu.exe script out/m8/study.apoproject \
  --script examples/scripts/palette-and-geometry.lua --output out/m8/lua.apoproject
./bin/windows-x64/flame-cpu.exe script examples/ember-spiral.flame \
  --script examples/scripts/legacy-triangles.asc --output out/m8/legacy.flame
./bin/windows-x64/flame-cpu.exe palette-export out/m8/lua.apoproject \
  --output out/m8/palette.ugr
./bin/windows-x64/flame-cpu.exe palette-export out/m8/lua.apoproject \
  --output out/m8/palette.map
./bin/windows-x64/flame-cpu.exe palette-import out/m8/palette.ugr \
  --flame examples/ember-spiral.flame --output out/m8/ugr-roundtrip.flame
./bin/windows-x64/flame-cpu.exe upr-import examples/classic-upr-study.upr \
  --output out/m8/upr.flame
```

Script edits run on private document copies. Deferred render/export requests require `--allow-renders` and/or `--allow-exports`, plus `--output-dir` naming an existing output folder. Existing result files require `--overwrite`; source files, script inputs and loaded plugin files remain protected. Deferred requests cannot overwrite the primary edited-document destination or supply their own checkpoint paths. The public API is available through `flame-cpu.exe script --api`. See [SCRIPTING.md](SCRIPTING.md), [LEGACY_SCRIPTING.md](LEGACY_SCRIPTING.md), [PROJECT_FORMAT.md](PROJECT_FORMAT.md) and [LEGACY_FORMATS.md](LEGACY_FORMATS.md) for supported semantics and limitations. Generic Ultra Fractal formulas are not an Apophysis UPR compatibility claim.

Inspect the bundled plugin without loading it, then explicitly opt into its native code for a CPU render:

```bash
./bin/windows-x64/flame-cpu.exe plugins info \
  bin/windows-x64/plugins/reborn-wave/reborn_wave.apoplugin
./bin/windows-x64/flame-cpu.exe \
  --plugin bin/windows-x64/plugins/reborn-wave/reborn_wave.apoplugin \
  --flame examples/plugins/reborn-wave/sample-wave.flame \
  --samples 1000000 --backend cpu --output out/m8/sample-wave.png
```

Leading `--plugin` arguments may be repeated. They are a trusted-code opt-in for that process; opening a project or inspecting a manifest does not enable code. Native plugins run in the application process and are CPU-only in M8. Old 32-bit DLLs cannot execute inside this x64 process. [PLUGINS.md](PLUGINS.md) documents the C SDK, source migration, parameter serialization, error handling and native fault-isolation limits.

## Rendering and checkpoints

High-resolution output can be planned before rendering. The same global sampling model is reconstructed in memory-bounded strips; changing strip height does not crop the camera or restart sampling.

```bash
./bin/windows-x64/flame-cpu.exe --flame examples/ember-spiral.flame \
  --size 16384 16384 --samples 8388611 --memory-mib 128 --disk-mib 8192 \
  --format png16 --alpha --checkpoint out/ember-checkpoint \
  --output out/ember-16k.png --estimate
```

Create the output parent directory, then remove `--estimate` to render. Add `--pause-after 65536` to stop after a committed chunk (exit status 75); continue with the same flame, samples, seed, backend and export settings using `--resume` and the same checkpoint directory. The destination is replaced only after the complete output has been written. Thread count and memory budgets can change if the checkpoint's reconstruction strip still fits. See [HIGH_RES_RENDERING.md](HIGH_RES_RENDERING.md) for checkpoint and output-format details.

The CLI defaults to CPU. Discover the available Vulkan devices and, when ready, require a Vulkan render with:

```bash
./bin/windows-x64/flame-cpu.exe --list-devices --json
./bin/windows-x64/flame-cpu.exe --flame examples/ember-spiral.flame \
  --samples 2000000 --backend vulkan --device 0 --no-fallback --output ember-vulkan.bmp
```

Use a zero-based index or ID from your own discovery report. The Vulkan path uses FP32 sampling and CPU reconstruction; its device and fallback behavior are explained in [GPU_RENDERING.md](GPU_RENDERING.md). A later strict Windows hardware check is available as `bash scripts/verify-gpu-windows.sh`; [WINDOWS_GPU.md](WINDOWS_GPU.md) explains that separate gate.

## Build inventory and verification scope

The complete **0.8.0** desktop package contains **26 executables**: the CLI, **sixteen core test programs**, eight Qt test programs and the editor. It also includes the SDK example DLL/sidecar and two test DLLs. Preserve the complete `bin/windows-x64` folder, including `plugins`, `tests/plugins`, Qt plugins and shared C++/zlib runtime files. The core executables use PE32+ x86-64 console headers; the editor uses the Windows GUI subsystem. [QT_BUILD.md](QT_BUILD.md) provides the full cross-build and staging command, including the new `--module SOURCE RELATIVE` and `--data SOURCE RELATIVE` options.

The final M8 runtime contains **63 files**, including **60 PE modules**. The recursive DLL audit resolved **19,959 non-system imported symbols**, with **zero missing**; the package's `verification/windows-runtime-record.json` contains the executable and plugin fingerprints. These are static dependency results for the cross-compiled package. **No Windows binary was executed in this environment.** Native Linux tests and software-Vulkan execution do not establish Windows runtime behavior or physical-GPU performance. [TEST_PLAN.md](TEST_PLAN.md) and the packaged verification logs record the separate native test results.

Historical context: the 0.3.0 core checkpoint built the CLI and four C++ test programs with static Expat/C++/threading runtimes. M6 built 15 executables with 48 runtime files, 46 PE modules and 13,232 resolved non-system imports. M7 built 19 executables with 52 files, 50 PE modules and 14,948 resolved non-system imports, with zero missing in that audit. M7 registered 25 full native CTest entries; its core-only Vulkan-disabled configuration passed 16 and skipped three device-execution checks. The earlier M6 full builds passed 20 entries, and its core-only build passed 13 with two skips. These measurements describe the earlier milestones. The historical Wine 10.0 attempt failed to start its local IPC server and did not establish Windows execution.

`verify-windows.sh` runs the M8 project, legacy-format, script, script-host and plugin tests alongside the existing core regressions. It exercises Lua/legacy CLI scripts, UGR/MAP/UPR exchange, project round trips and the explicitly enabled bundled native plugin. It also runs the file renderer in CPU-only mode, checks all seven export depth/format combinations plus transparent PNG/TIFF, and verifies destination-preserving pause/resume. The plugin tests explicitly load only their supplied SDK and fixture modules. Its 16K estimate does not write a 16K image. Qt integration, scripted batch queues and optional real 16K UI acceptance are available through the desktop gate:

```bash
bash scripts/verify-editor-windows.sh --no-open
bash scripts/verify-editor-windows.sh --large-render --no-open
```

The ordinary desktop command runs `extensions-editor-tests.exe`, `plugin-dialog-tests.exe` and the M8 extension smoke as well as the earlier Qt checks. The second command additionally renders an actual 16384×16384 PNG RGBA16 with a 128 MiB working-buffer budget, pauses and resumes through the queue, and saves responsiveness/checkpoint evidence. These scripts do not require or claim Vulkan execution. `verify-gpu-windows.sh --device ID` is the separate strict device gate, including bounded GPU streaming, checkpoint identity and queued 16-bit exports.

The normal source build requires no Vulkan SDK or shader compiler: pinned headers and the compiled SPIR-V module are included. The Windows application loads the system `vulkan-1.dll` dynamically and does not bundle a replacement graphics loader. The DLL import audit therefore cannot establish that a user's Vulkan driver works; the strict execution gate covers that distinction. CPU rendering remains available without the loader. CUDA is not included.

## Reproduce a standalone core cross-build on Linux

Use GNU Make, CMake (4.4.5 for the packaged build), the llvm-mingw 20261104 UCRT Ubuntu x86-64 host toolchain (Clang 23.1.5), tag R_2_10_0 of libexpat and matching Windows zlib development files. The packaged build uses MSYS2 clang64 zlib 1.3.1. Its dependency package includes `include/zlib.h`, `include/zconf.h`, `lib/libz.a` and `lib/libz.dll.a`; the standalone static example below selects `libz.a` explicitly. The commands assume Bash is open in the project source directory. Set the first three paths to your unpacked dependency locations.

```bash
export APO_TOOLCHAIN=/path/to/llvm-mingw-20261104-ucrt-ubuntu-22.04-x86_64
export APO_EXPAT=/path/to/libexpat-R_2_10_0
export APO_ZLIB_PREFIX=/path/to/unpacked-msys2-zlib/clang64
export APO_BUILD="$(mktemp -d)"
export APO_WIN_DEPS="$APO_BUILD/deps"
cat > "$APO_BUILD/win64.cmake" <<'CMAKE'
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)
set(CMAKE_C_COMPILER "$ENV{APO_TOOLCHAIN}/bin/x86_64-w64-mingw32-clang")
set(CMAKE_CXX_COMPILER "$ENV{APO_TOOLCHAIN}/bin/x86_64-w64-mingw32-clang++")
set(CMAKE_RC_COMPILER "$ENV{APO_TOOLCHAIN}/bin/x86_64-w64-mingw32-windres")
set(CMAKE_FIND_ROOT_PATH "$ENV{APO_WIN_DEPS}" "$ENV{APO_ZLIB_PREFIX}" "$ENV{APO_TOOLCHAIN}/x86_64-w64-mingw32")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")
CMAKE

cmake -S "$APO_EXPAT/expat" -B "$APO_BUILD/expat" \
  -DCMAKE_TOOLCHAIN_FILE="$APO_BUILD/win64.cmake" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$APO_WIN_DEPS" -DEXPAT_SHARED_LIBS=OFF \
  -DEXPAT_BUILD_EXAMPLES=OFF -DEXPAT_BUILD_TESTS=OFF -DEXPAT_BUILD_TOOLS=OFF \
  -DEXPAT_BUILD_DOCS=OFF
cmake --build "$APO_BUILD/expat" --parallel
cmake --install "$APO_BUILD/expat"
cmake -S . -B "$APO_BUILD/app" -DCMAKE_TOOLCHAIN_FILE="$APO_BUILD/win64.cmake" \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DAPO_BUILD_EDITOR=OFF \
  -DZLIB_INCLUDE_DIR="$APO_ZLIB_PREFIX/include" -DZLIB_LIBRARY="$APO_ZLIB_PREFIX/lib/libz.a"
cmake --build "$APO_BUILD/app" --parallel
"$APO_TOOLCHAIN/bin/x86_64-w64-mingw32-objdump" -p "$APO_BUILD/app/flame-cpu.exe"
```

The core cross-build emits **seventeen executables** in `$APO_BUILD/app`:

| Executable | Purpose |
| --- | --- |
| `flame-cpu.exe` | CLI, CPU/Vulkan file rendering, checkpoints and device discovery |
| `reference-tests.exe` | Original numerical and reference-image checks |
| `import-tests.exe` | XML/model import checks |
| `renderer-tests.exe` | CPU rendering checks |
| `legacy-document-tests.exe` | Source-preserving document checks |
| `creative-tests.exe` | Palette, generation and mutation checks |
| `extended-variation-tests.exe` | Additional original-source variation comparisons |
| `gpu-kernel-tests.exe` | Portable packing/RNG and direct CPU/Vulkan formula probes |
| `gpu-tests.exe` | Backend selection, fallback and rendering-equivalence checks |
| `gpu-stream-tests.exe` | Bounded global Vulkan hit records, range/budget limits and dense equivalence |
| `image-stream-tests.exe` | Streaming BMP/PPM/PNG/TIFF encoding, 16-bit precision and alpha |
| `file-render-tests.exe` | Exact strip reconstruction, checkpoint integrity/resume, locks and export precision |
| `project-io-tests.exe` | Native project envelopes, metadata and preservation of unknown fields |
| `legacy-formats-tests.exe` | UGR/MAP and supported Apophysis UPR import/export against included fixtures |
| `script-tests.exe` | Lua host API, supported legacy translation, resource limits and atomic model edits |
| `script-host-tests.exe` | Deferred-output preflight, path confinement and protected destinations |
| `plugin-tests.exe` | Real module loading, ABI/parameters/RNG, immutable lifetimes and checkpoint fingerprints |

Copy the seventeen binaries into the extracted package's `bin/windows-x64` directory. Also retain the generated plugin payloads at these locations:

| Cross-build output | Packaged location under `bin/windows-x64` |
| --- | --- |
| `plugins/reborn-wave/reborn_wave.dll` | `plugins/reborn-wave/reborn_wave.dll` |
| `plugins/reborn-wave/reborn_wave.apoplugin` | `plugins/reborn-wave/reborn_wave.apoplugin` |
| `libplugin-fixture.dll` | `tests/plugins/libplugin-fixture.dll` |
| `libplugin-bad-abi.dll` | `tests/plugins/libplugin-bad-abi.dll` |

The verification script supplies the plugin test's manifest and two DLL arguments, and `tests/fixtures/formats` for the legacy-format suite. A direct reference-suite invocation also needs the reference image flame and upstream-rendered image, alongside the vector table and geometry flame. Preserve the package's `tests` and `examples` directories. The full desktop verification additionally requires the editor, its eight Qt test executables and the complete runtime described in [QT_BUILD.md](QT_BUILD.md). The distributed desktop package uses dynamic zlib/C++ runtime linkage instead of the optional standalone static executable configuration above; inspect module DLL imports separately when producing a custom core-only package.

Add `-DAPO_ENABLE_VULKAN=OFF` to the app configure command for a CPU-only runtime. Device-execution tests report skips when no backend can run; portable packing and CPU/reference/file-render checks still execute. Shader changes use `scripts/compile_shader.py` as described in the Qt build guide. Native variation plugins use the CPU renderer regardless of this build option. M8 retains M7's high-resolution output, queues, checkpoints and high-bit-depth export; CUDA remains outside the checkpoint.

| Build input | SHA-256 of archive used |
|---|---|
| llvm-mingw-20261104-ucrt-ubuntu-22.04-x86_64.tar.xz | `919380403de87dca3c581c2053230cd298af5e891286628ba0613b7fed370926` |
| libexpat GitHub source tar.gz, R_2_10_0 | `b3644a12f62d7762b62c444f69d169ba0234f13138d0240244bca64cbe35286f` |
| mingw-w64-clang-x86_64-zlib-1.3.1-1-any.pkg.tar.zst | `b7f0c06e6d48128209a3a441f96b92351e565a0d031278c56a8db8a9b5ec291a` |
| lua-5.5.1.tar.gz | `1c4b4068d67061f2a2231ad2b5422e77acea1487ea9890f6320af614f4373dce` |

[LLVM-MinGW release](https://github.com/mstorsjo/llvm-mingw/releases/tag/20261104) · [Expat source](https://github.com/libexpat/libexpat/tree/R_2_10_0)
