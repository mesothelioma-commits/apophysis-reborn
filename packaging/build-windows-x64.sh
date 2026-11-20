#!/usr/bin/env bash
# Linux x64 release host with Qt 6.8.2 host tools, CMake, Ninja, curl, dpkg-deb, tar, zstd and unzip.
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
work=$(realpath -m "${1:?Supply a build directory outside the source checkout}")
mkdir -p "$work"
cd "$work"

fetch() {
    local name=$1 digest=$2 url=$3
    if [[ ! -f "$name" ]]; then
        curl --fail --location --retry 3 "$url" -o "$name.part"
        mv "$name.part" "$name"
    fi
    echo "$digest  $name" | sha256sum -c -
}

llvm=llvm-mingw-20261104-ucrt-ubuntu-22.04-x86_64
msys=https://repo.msys2.org/mingw/clang64
debian=https://deb.debian.org/debian/pool/main/n/nsis
baseline=apophysis-reborn-0.15.0-windows-x64
fetch llvm.tar.xz 919380403de87dca3c581c2053230cd298af5e891286628ba0613b7fed370926 "https://github.com/mstorsjo/llvm-mingw/releases/download/20261104/$llvm.tar.xz"
fetch qt.zst 0dd9322532c5b91c56b17051bbe6bd11ff3423a16512b2729531dd517caa5d89 "$msys/mingw-w64-clang-x86_64-qt6-base-6.8.2-1-any.pkg.tar.zst"
fetch zlib.zst b7f0c06e6d48128209a3a441f96b92351e565a0d031278c56a8db8a9b5ec291a "$msys/mingw-w64-clang-x86_64-zlib-1.3.1-1-any.pkg.tar.zst"
fetch expat.tar.gz b3644a12f62d7762b62c444f69d169ba0234f13138d0240244bca64cbe35286f https://codeload.github.com/libexpat/libexpat/tar.gz/refs/tags/R_2_10_0
fetch base.zip 7e325bb207719532c1c44cc965ec5762caa38293a9fc5734ef9cb08ee7745589 "https://github.com/mesothelioma-commits/apophysis-reborn/releases/download/v0.15.0/$baseline-portable.zip"
fetch nsis_3.13-1_amd64.deb 12a93aea3d2ee7f54c6c6449cc0b72ed7fbf7de8f53b639974a0a983175c5f06 "$debian/nsis_3.13-1_amd64.deb"
fetch nsis-common_3.13-1_all.deb 5d66cf54a09214dd7874a038a94af42894066d1fb136d662619528b0c7ed1c8c "$debian/nsis-common_3.13-1_all.deb"
tar xf llvm.tar.xz
mkdir -p deps
tar xf qt.zst -C deps
tar xf zlib.zst -C deps
tar xf expat.tar.gz
unzip -qo base.zip -d prev
dpkg-deb -x nsis_3.13-1_amd64.deb nsis
dpkg-deb -x nsis-common_3.13-1_all.deb nsis

export APO_TOOLCHAIN="$work/$llvm" APO_TARGET_QT="$work/deps/clang64" APO_WIN_DEPS="$work/static"
export APO_HOST_QT="${APO_HOST_QT:-/usr}"
export APO_HOST_QT_CMAKE="${APO_HOST_QT_CMAKE:-$APO_HOST_QT/lib/x86_64-linux-gnu/cmake}"
export NSISDIR="$work/nsis/usr/share/nsis"
cat > toolchain.cmake <<'CMAKE'
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
config=(-G Ninja -DCMAKE_TOOLCHAIN_FILE="$work/toolchain.cmake" -DCMAKE_BUILD_TYPE=Release)
cmake -S libexpat-R_2_10_0/expat -B expat-static "${config[@]}" -DCMAKE_INSTALL_PREFIX="$APO_WIN_DEPS" \
    -DEXPAT_SHARED_LIBS=OFF -DEXPAT_BUILD_EXAMPLES=OFF -DEXPAT_BUILD_TESTS=OFF -DEXPAT_BUILD_TOOLS=OFF -DEXPAT_BUILD_DOCS=OFF
cmake --build expat-static --target install --parallel 3
cmake -S "$repo" -B app "${config[@]}" -DBUILD_TESTING=OFF
cmake --build app --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-3}"
# Reuse only the exact dependency binaries and corresponding sources from the pinned distribution.
cp "prev/$baseline/"*.dll "$APO_TARGET_QT/bin/"
objdump="$APO_TOOLCHAIN/bin/llvm-objdump"
python3 "$repo/scripts/stage_editor_runtime.py" --qt-prefix "$APO_TARGET_QT" \
    --compiler-runtime "$APO_TOOLCHAIN/x86_64-w64-mingw32/bin" --objdump "$objdump" --output stage \
    --module app/plugins/reborn-wave/reborn_wave.dll plugins/reborn-wave/reborn_wave.dll \
    --data app/plugins/reborn-wave/reborn_wave.apoplugin plugins/reborn-wave/reborn_wave.apoplugin \
    app/apophysis-reborn.exe app/apophysis-mcp.exe app/flame-cpu.exe
python3 "$repo/scripts/package-runtime.py" --runtime stage --output output --objdump "$objdump" \
    --lgpl-sources "prev/$baseline/third-party-sources" --source-commit "$(git -C "$repo" rev-parse HEAD)" \
    --source-date-epoch "$(git -C "$repo" show -s --format=%ct HEAD)"
payload=(output/*-windows-x64)
python3 "$repo/packaging/build_installer.py" --payload "${payload[0]}" --output "${payload[0]}-setup.exe" \
    --compiler "$APO_TOOLCHAIN/bin/x86_64-w64-mingw32-clang++" --objdump "$objdump" --makensis "$work/nsis/usr/bin/makensis"
sha256sum "${payload[0]}-setup.exe"
