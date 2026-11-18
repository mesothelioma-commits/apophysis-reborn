#!/usr/bin/env bash
# Configure a disposable build to check the actual C++/library dependencies.
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
source "$project_dir/scripts/script-common.sh"
open_output=true
editor=ON
configure_args=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --no-open) open_output=false; shift ;;
        --cpu-only) editor=OFF; configure_args+=(-DAPO_ENABLE_VULKAN=OFF); shift ;;
        -h|--help)
            echo "Usage: bash scripts/check-deps.sh [--cpu-only] [--no-open] [CMake configure options]"
            echo "Checks compiler, CMake, Expat, zlib and optional Qt Widgets/Test without downloading tools or compiling the app."
            exit 0 ;;
        --) shift; configure_args+=("$@"); break ;;
        *) configure_args+=("$1"); shift ;;
    esac
done
apo_start_log dependencies
apo_require_build_tools
if [[ -n "${VCPKG_ROOT:-}" ]]; then
    toolchain="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
    [[ -f "$toolchain" ]] || { echo "VCPKG_ROOT does not contain scripts/buildsystems/vcpkg.cmake." >&2; exit 1; }
    configure_args=("-DCMAKE_TOOLCHAIN_FILE=$toolchain" "${configure_args[@]}")
fi
if [[ -n "${APO_QT_PREFIX:-}" ]]; then configure_args+=("-DCMAKE_PREFIX_PATH=$APO_QT_PREFIX"); fi
check_dir="$(mktemp -d "$project_dir/logs/dependency-check-XXXXXXXX")"
cmake --version
ctest --version
if command -v python3 >/dev/null; then python3 --version; else echo "Python 3.10+ is needed for the release tests, packaging and benchmarks."; fi
if command -v git >/dev/null; then git --version; else echo "Git is needed for source-history release packages."; fi
if command -v makensis >/dev/null; then makensis -VERSION; else echo "NSIS 3.13 or newer is needed only when building the Windows installer."; fi
if ! cmake -S "$project_dir" -B "$check_dir" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
    "-DAPO_BUILD_EDITOR=$editor" "${configure_args[@]}"; then
    echo "Dependency configuration failed. Read the missing-package/compiler diagnostic above."
    echo "Set APO_QT_PREFIX or VCPKG_ROOT for your installed toolchain, or use --cpu-only for the core."
    echo "The disposable configuration is retained at: $check_dir"
    exit 1
fi
echo "CMake successfully configured the requested compiler and libraries."
echo "This checks dependencies; it does not claim the application or tests were executed."
echo "Disposable configuration: $check_dir"
