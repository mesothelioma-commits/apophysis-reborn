# Windows x64 core build — 0.6.0

The desktop editor build is documented in [QT_BUILD.md](QT_BUILD.md). These commands reproduce the core CLI and tests with the editor disabled. The current core includes CPU rendering and dynamically loaded Vulkan compute; `APO_ENABLE_VULKAN=OFF` can disable the Vulkan runtime.

Unzip the whole package. Open Git Bash in the extracted project directory and run the Windows checks.

```bash
bash scripts/verify-windows.sh
```

For a single render, use the executable with the included flame.

```bash
./bin/windows-x64/flame-cpu.exe --flame examples/ember-spiral.flame \
  --samples 2000000 --seed 123456 --threads 4 --output ember.bmp
```

The CLI defaults to CPU. Discover the available Vulkan devices and, when ready, require a Vulkan render with:

```bash
./bin/windows-x64/flame-cpu.exe --list-devices --json
./bin/windows-x64/flame-cpu.exe --flame examples/ember-spiral.flame \
  --samples 2000000 --backend vulkan --device 0 --no-fallback --output ember-vulkan.bmp
```

Use a zero-based index or ID from your own discovery report. The Vulkan path uses FP32 sampling and CPU reconstruction; its device and fallback behavior are explained in [GPU_RENDERING.md](GPU_RENDERING.md). A later strict Windows hardware check is available as `bash scripts/verify-gpu-windows.sh`; [WINDOWS_GPU.md](WINDOWS_GPU.md) explains that separate gate.

## What was verified

The 0.3.0 core checkpoint compiled and linked the CLI and four C++ test programs for 64-bit Windows with static Expat, C++ and threading runtimes. The current **0.6.0** desktop package builds the CLI, **eight core test programs**, five Qt test programs and the editor with a shared C++ runtime: **15 executables** in total. Preserve the complete `bin/windows-x64` folder so its DLLs remain available. The core executables have PE32+ x86-64 console headers. The editor uses the Windows GUI subsystem. The staged runtime contains **48 files**, including **46 PE modules**, and its final audit resolved all **13,232 non-system imports** with zero missing. [QT_BUILD.md](QT_BUILD.md) records the desktop runtime and smoke checks.

The corresponding native Linux Release and ASan/UBSan builds passed all **20 CTest entries**. The separate core-only, Vulkan-disabled build passed 13 entries and skipped two device-execution entries, for 15 total. Actual Vulkan execution used llvmpipe software Vulkan; the host exposes no physical GPU. The distributed Windows programs were compiled and audited, not executed. Running them on Windows 11 and measuring physical GPU performance remain separate acceptance checks. Wine 10.0 could not start its local IPC server on the test host because the host denies Unix-domain sockets.

The normal source build requires no Vulkan SDK or shader compiler: pinned headers and the compiled SPIR-V module are included. The Windows application loads the system `vulkan-1.dll` dynamically and does not bundle a replacement graphics loader. The DLL import audit therefore cannot establish that a user's Vulkan driver works; the strict execution gate covers that distinction. CPU rendering remains available without the loader. CUDA is not included.

## Reproduce a standalone core cross-build on Linux

Use GNU Make, CMake (4.4.5 for the packaged build), the llvm-mingw 20261104 UCRT Ubuntu x86-64 host toolchain (Clang 23.1.5), and tag R_2_10_0 of libexpat. The commands assume Bash is open in the project source directory. Set the first two paths to your unpacked dependency locations.

```bash
export APO_TOOLCHAIN=/path/to/llvm-mingw-20261104-ucrt-ubuntu-22.04-x86_64
export APO_EXPAT=/path/to/libexpat-R_2_10_0
export APO_BUILD="$(mktemp -d)"
export APO_WIN_DEPS="$APO_BUILD/deps"
cat > "$APO_BUILD/win64.cmake" <<'CMAKE'
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)
set(CMAKE_C_COMPILER "$ENV{APO_TOOLCHAIN}/bin/x86_64-w64-mingw32-clang")
set(CMAKE_CXX_COMPILER "$ENV{APO_TOOLCHAIN}/bin/x86_64-w64-mingw32-clang++")
set(CMAKE_RC_COMPILER "$ENV{APO_TOOLCHAIN}/bin/x86_64-w64-mingw32-windres")
set(CMAKE_FIND_ROOT_PATH "$ENV{APO_WIN_DEPS}" "$ENV{APO_TOOLCHAIN}/x86_64-w64-mingw32")
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
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DAPO_BUILD_EDITOR=OFF
cmake --build "$APO_BUILD/app" --parallel
"$APO_TOOLCHAIN/bin/x86_64-w64-mingw32-objdump" -p "$APO_BUILD/app/flame-cpu.exe"
```

The core cross-build emits **nine executables** in `$APO_BUILD/app`:

| Executable | Purpose |
| --- | --- |
| `flame-cpu.exe` | CLI, CPU/Vulkan rendering and device discovery |
| `reference-tests.exe` | Original numerical and reference-image checks |
| `import-tests.exe` | XML/model import checks |
| `renderer-tests.exe` | CPU rendering checks |
| `legacy-document-tests.exe` | Source-preserving document checks |
| `creative-tests.exe` | Palette, generation and mutation checks |
| `extended-variation-tests.exe` | Additional original-source variation comparisons |
| `gpu-kernel-tests.exe` | Portable packing/RNG and direct CPU/Vulkan formula probes |
| `gpu-tests.exe` | Backend selection, fallback and rendering-equivalence checks |

Copy the nine binaries into the extracted package's `bin/windows-x64` directory to run the native Windows checks above. The verification script supplies required arguments. A direct invocation of the reference suite also needs the reference image flame and its upstream-rendered image, alongside the transform vector table and its geometry flame. Preserve the `tests` and `examples` directories from the package. The full desktop verification additionally requires the Qt editor, its five Qt test executables and the complete runtime described in [QT_BUILD.md](QT_BUILD.md).

Add `-DAPO_ENABLE_VULKAN=OFF` to the app configure command for a CPU-only runtime. The two GPU execution tests are skipped when no backend can run; portable packing and CPU/reference checks still execute. Shader changes use `scripts/compile_shader.py` as described in the Qt build guide. Tiled output, queues, checkpointing and high-bit-depth exports remain Milestone 7 work.

| Build input | SHA-256 of archive used |
|---|---|
| llvm-mingw-20261104-ucrt-ubuntu-22.04-x86_64.tar.xz | `919380403de87dca3c581c2053230cd298af5e891286628ba0613b7fed370926` |
| libexpat GitHub source tar.gz, R_2_10_0 | `b3644a12f62d7762b62c444f69d169ba0234f13138d0240244bca64cbe35286f` |

[LLVM-MinGW release](https://github.com/mstorsjo/llvm-mingw/releases/tag/20261104) · [Expat source](https://github.com/libexpat/libexpat/tree/R_2_10_0)
