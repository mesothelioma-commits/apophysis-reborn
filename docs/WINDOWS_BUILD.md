# Windows x64 CPU build

Unzip the whole package. Open Git Bash in the extracted project directory and run the Windows checks.

```bash
bash scripts/verify-windows.sh
```

For a single render, use the executable with the included flame.

```bash
./bin/windows-x64/flame-cpu.exe --flame examples/ember-spiral.flame \
  --samples 2000000 --seed 123456 --threads 4 --output ember.bmp
```

## What was verified

The CLI and all four C++ test programs compiled and linked for 64-bit Windows. Each executable has a PE32+ x86-64 console header. Inspection of the import tables found Windows Kernel32 and Universal CRT interfaces in all five programs, and Shell32 in the CLI. Expat and the C++ and threading runtimes are linked statically. The host executed the corresponding native Linux tests. Running the distributed binaries on Windows 11 is a separate pending check. Wine 10.0 could not start its local IPC server on the test host because the host denies Unix-domain sockets.

## Reproduce the Windows cross-build on Linux

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
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build "$APO_BUILD/app" --parallel
"$APO_TOOLCHAIN/bin/x86_64-w64-mingw32-objdump" -p "$APO_BUILD/app/flame-cpu.exe"
```

The cross-build emits `flame-cpu.exe`, `reference-tests.exe`, `import-tests.exe`, `renderer-tests.exe` and `legacy-document-tests.exe` in `$APO_BUILD/app`. Copy the five binaries into the extracted package's `bin/windows-x64` directory to run the native Windows checks above. The verification script supplies the required arguments. A direct invocation of the reference suite also needs the reference image flame and its upstream-rendered image, alongside the transform vector table and its geometry flame. Preserve the `tests` and `examples` directories from the package.

| Build input | SHA-256 of archive used |
|---|---|
| llvm-mingw-20261104-ucrt-ubuntu-22.04-x86_64.tar.xz | `919380403de87dca3c581c2053230cd298af5e891286628ba0613b7fed370926` |
| libexpat GitHub source tar.gz, R_2_10_0 | `b3644a12f62d7762b62c444f69d169ba0234f13138d0240244bca64cbe35286f` |

[LLVM-MinGW release](https://github.com/mstorsjo/llvm-mingw/releases/tag/20261104) · [Expat source](https://github.com/libexpat/libexpat/tree/R_2_10_0)
