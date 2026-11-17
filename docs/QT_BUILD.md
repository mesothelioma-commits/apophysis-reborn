# Native Qt desktop build

The primary editor uses Qt 6 Widgets and the same C++20 rendering and legacy-document library as the CLI. The milestone binaries use Qt 6.8.2. The Windows editor and Qt test programs dynamically link their GUI runtime. The existing core CLI remains available and shares the packaged C++ runtime. Keep the complete executable directory together.

## Build with an installed Qt development kit

Install a Qt 6 desktop development kit, a matching C++20 compiler, CMake 3.20 or newer, and Expat development files. Qt Core, Gui, Widgets and Test are required for a build with tests enabled. On Windows, use the compiler shipped or supported by that Qt kit. Open Git Bash in the source directory and supply the kit prefix if CMake cannot discover it automatically.

```bash
export APO_QT_PREFIX=/path/to/Qt/6.8.2/your-desktop-kit
bash scripts/build-editor.sh
```

The script invokes the normal source build with `APO_BUILD_EDITOR=ON`, builds the editor and tests, and runs CTest. The Qt directory can also be supplied as `-DCMAKE_PREFIX_PATH=...`. Expat installed through vcpkg can use the existing `VCPKG_ROOT` handling in `scripts/build.sh`. A core-only build uses `bash scripts/build.sh -DAPO_BUILD_EDITOR=OFF`.

## Packaged Windows toolchain

The release cross-build combines the LLVM-MinGW 20261104 UCRT x86-64 toolchain, static Expat 2.10.0, and MSYS2's clang64 Qt 6.8.2 package. Native Linux Qt 6.8.2 supplies the matching `moc`, `uic` and `rcc` host tools. Windows Qt headers, import libraries and DLLs come from `mingw-w64-clang-x86_64-qt6-base-6.8.2-1-any.pkg.tar.zst`. The source package and all MSYS2 build patches are included in the release's `third-party-sources` directory. The prior toolchain and Expat provenance are in WINDOWS_BUILD.md. [Qt cross-compilation documentation](https://doc.qt.io/qt-6.8/cross-compiling-qt.html) · [Windows Qt package](https://repo.msys2.org/mingw/clang64/mingw-w64-clang-x86_64-qt6-base-6.8.2-1-any.pkg.tar.zst)

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
  "$APO_EDITOR_BUILD/app/apophysis-reborn.exe" \
  "$APO_EDITOR_BUILD/app/editor-tests.exe" \
  "$APO_EDITOR_BUILD/app/preview-tests.exe" \
  "$APO_EDITOR_BUILD/app/canvas-tests.exe" \
  "$APO_EDITOR_BUILD/app/document-session-tests.exe"
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

The Linux editor was compiled with Qt 6.8.2 from Debian's `6.8.2+dfsg-9+deb13u2` packages. A relocated runtime also executed its complete offscreen smoke workflow, producing an editor screenshot, a preview image and a saved flame. The Windows editor and nine console executables cross-compiled against the matched Qt headers and import libraries. The staged set contains 41 executables, DLLs and plugins. Import inspection resolved all 11,349 non-system imported symbols against the exact copied libraries. Execution of the Windows editor remains a separate platform check.

When ready to run that check on Windows 11, extract the complete milestone ZIP and run from Git Bash.

```bash
bash scripts/verify-editor-windows.sh
```

The script records the OS version, runs the core and Qt test programs, launches the editor's automated smoke workflow, and keeps a log and its screenshots under the project's `logs` and `out` directories. The normal desktop program is `bin/windows-x64/apophysis-reborn.exe`.
