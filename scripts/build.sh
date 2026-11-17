#!/usr/bin/env bash
# Native rebuild with C++20, CMake, Expat and Qt 6 Widgets/Test development files.
# Pass -DAPO_BUILD_EDITOR=OFF to build only the core and CLI.
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$project_dir"
mkdir -p logs
log="$project_dir/logs/build-$(date +%Y%m%d-%H%M%S)-$$.txt"
exec > >(tee "$log") 2>&1
trap 'status=$?; echo "Build log: $log"; exit "$status"' EXIT
command -v cmake >/dev/null && command -v ctest >/dev/null || {
    echo "Install CMake 3.20 or newer, with ctest on PATH."; exit 1; }
if [[ -n "${VCPKG_ROOT:-}" ]]; then
    toolchain="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
    [[ -f "$toolchain" ]] || { echo "Check VCPKG_ROOT; the vcpkg toolchain file is missing."; exit 1; }
    set -- "-DCMAKE_TOOLCHAIN_FILE=$toolchain" "$@"
fi
cmake -S . -B build -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release "$@"
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
