#!/usr/bin/env bash
# C++20/CMake build, with bounded default compiler concurrency and saved logs.
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
caller_dir="$PWD"
source "$project_dir/scripts/script-common.sh"
open_output=true
build_dir="${APO_BUILD_DIR:-$project_dir/build}"
jobs="${APO_BUILD_JOBS:-${CMAKE_BUILD_PARALLEL_LEVEL:-2}}"
configure_args=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --no-open) open_output=false; shift ;;
        --build-dir|--jobs)
            [[ $# -ge 2 ]] || { echo "$1 requires a value." >&2; exit 2; }
            if [[ "$1" == --build-dir ]]; then build_dir="$2"; else jobs="$2"; fi
            shift 2 ;;
        -h|--help)
            echo "Usage: bash scripts/build.sh [--build-dir PATH] [--jobs N] [--no-open] [CMake configure options]"
            echo "Defaults: build/ beside the source, Release, tests enabled, 2 compiler jobs."
            echo "APO_BUILD_DIR and APO_BUILD_JOBS override defaults; VCPKG_ROOT selects its toolchain."
            exit 0 ;;
        --) shift; configure_args+=("$@"); break ;;
        *) configure_args+=("$1"); shift ;;
    esac
done
[[ "$jobs" =~ ^[1-9][0-9]{0,2}$ && "$jobs" -le 256 ]] || {
    echo "Compiler jobs must be an integer from 1 through 256." >&2; exit 2; }
[[ -n "$build_dir" ]] || { echo "The build directory must not be empty." >&2; exit 2; }
if apo_is_windows && command -v cygpath >/dev/null; then build_dir="$(cygpath -u "$build_dir")"; fi
case "$build_dir" in /*) ;; *) build_dir="$caller_dir/$build_dir";; esac
apo_start_log build
apo_require_build_tools
if [[ -n "${VCPKG_ROOT:-}" ]]; then
    toolchain="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
    [[ -f "$toolchain" ]] || { echo "VCPKG_ROOT does not contain scripts/buildsystems/vcpkg.cmake." >&2; exit 1; }
    configure_args=("-DCMAKE_TOOLCHAIN_FILE=$toolchain" "${configure_args[@]}")
fi
printf "Source: %s\nBuild: %s\nCompiler jobs: %s\n" "$project_dir" "$build_dir" "$jobs"
cmake -S "$project_dir" -B "$build_dir" -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release "${configure_args[@]}"
cmake --build "$build_dir" --config Release --parallel "$jobs"
ctest --test-dir "$build_dir" -C Release --output-on-failure --parallel "$jobs"
