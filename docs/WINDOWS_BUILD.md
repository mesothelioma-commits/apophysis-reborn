# Windows x64 core build — 0.7.0

The desktop editor build is documented in [QT_BUILD.md](QT_BUILD.md). These commands reproduce the core CLI and tests with the editor disabled. The current core includes CPU rendering, dynamically loaded Vulkan compute, bounded disk-backed reconstruction, resumable sampling checkpoints and BMP/PPM/PNG/TIFF exports. Both core and editor builds require Expat and zlib development files; `APO_ENABLE_VULKAN=OFF` can disable the Vulkan runtime.

Unzip the whole package. Open Git Bash in the extracted project directory and run the Windows checks.

```bash
bash scripts/verify-windows.sh
```

For a single render, use the executable with the included flame.

```bash
./bin/windows-x64/flame-cpu.exe --flame examples/ember-spiral.flame \
  --samples 2000000 --seed 123456 --threads 4 --output ember.bmp
```

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

## What was verified

The 0.3.0 core checkpoint compiled and linked the CLI and four C++ test programs for 64-bit Windows with static Expat, C++ and threading runtimes. M6 later built 15 executables, with 48 runtime files, 46 PE modules and 13,232 resolved non-system imports. The current **0.7.0** desktop package builds the CLI, **eleven core test programs**, six Qt test programs and the editor with a shared C++ and zlib runtime: **19 executables** in total. Preserve the complete `bin/windows-x64` folder so its DLLs remain available. The core executables have PE32+ x86-64 console headers. The editor uses the Windows GUI subsystem. The M7 staged runtime contains **52 files**, including **50 PE modules**, and its audit resolves **14,948 non-system imported symbols** with zero missing. Executable hashes and source-tree fingerprints are in `verification/windows-runtime-record.json`. [QT_BUILD.md](QT_BUILD.md) records the desktop runtime and smoke checks.

M7 has **25 CTest entries** in the full native Release and ASan/UBSan configurations. The separate core-only, Vulkan-disabled configuration has 19 entries: 16 pass and three device-execution entries skip. The distribution carries the final native test records. Actual Vulkan execution uses llvmpipe software Vulkan; the host exposes no physical GPU. The distributed Windows programs were compiled and audited, not executed. Running them on Windows 11 and measuring physical GPU performance remain separate acceptance checks. The earlier Wine 10.0 attempt could not start its local IPC server because the host denied Unix-domain sockets; it did not establish Windows execution. The historical M6 full builds passed their 20 entries, while its core-only Vulkan-disabled build passed 13 and skipped two.

`verify-windows.sh` explicitly runs the file renderer in CPU-only test mode, checks all seven export depth/format combinations plus transparent PNG/TIFF, and verifies destination-preserving pause/resume. Its 16K estimate does not write a 16K image. Queue and real 16K UI acceptance are available through the desktop gate:

```bash
bash scripts/verify-editor-windows.sh --no-open
bash scripts/verify-editor-windows.sh --large-render --no-open
```

The second command additionally renders an actual 16384×16384 PNG RGBA16 with a 128 MiB working-buffer budget, pauses and resumes through the queue, and saves responsiveness and checkpoint evidence. These scripts do not require or claim Vulkan execution. `verify-gpu-windows.sh --device ID` is the separate strict device gate, including bounded GPU streaming, checkpoint identity and queued 16-bit exports.

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

The core cross-build emits **twelve executables** in `$APO_BUILD/app`:

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

Copy the twelve binaries into the extracted package's `bin/windows-x64` directory to run the native Windows checks above. The verification script supplies required arguments. A direct invocation of the reference suite also needs the reference image flame and its upstream-rendered image, alongside the transform vector table and its geometry flame. Preserve the `tests` and `examples` directories from the package. The full desktop verification additionally requires the Qt editor, its six Qt test executables and the complete runtime described in [QT_BUILD.md](QT_BUILD.md). The distributed package uses dynamic zlib/C++ runtime linkage instead of this optional standalone static configuration.

Add `-DAPO_ENABLE_VULKAN=OFF` to the app configure command for a CPU-only runtime. Three GPU execution tests are skipped when no backend can run; portable packing and CPU/reference/file-render checks still execute. Shader changes use `scripts/compile_shader.py` as described in the Qt build guide. M7 implements high-resolution output, queues, checkpoints and high-bit-depth export; CUDA remains outside the checkpoint.

| Build input | SHA-256 of archive used |
|---|---|
| llvm-mingw-20261104-ucrt-ubuntu-22.04-x86_64.tar.xz | `919380403de87dca3c581c2053230cd298af5e891286628ba0613b7fed370926` |
| libexpat GitHub source tar.gz, R_2_10_0 | `b3644a12f62d7762b62c444f69d169ba0234f13138d0240244bca64cbe35286f` |
| mingw-w64-clang-x86_64-zlib-1.3.1-1-any.pkg.tar.zst | `b7f0c06e6d48128209a3a441f96b92351e565a0d031278c56a8db8a9b5ec291a` |

[LLVM-MinGW release](https://github.com/mstorsjo/llvm-mingw/releases/tag/20261104) · [Expat source](https://github.com/libexpat/libexpat/tree/R_2_10_0)
