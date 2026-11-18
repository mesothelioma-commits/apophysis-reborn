#!/usr/bin/env bash
# Launch a built or portable native editor from any working directory.
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
source "$project_dir/scripts/script-common.sh"
open_output=true
build_dir="${APO_BUILD_DIR:-$project_dir/build}"
executable=""
editor_args=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --no-open) open_output=false; shift ;;
        --build-dir|--exe)
            [[ $# -ge 2 ]] || { echo "$1 requires a value." >&2; exit 2; }
            if [[ "$1" == --build-dir ]]; then build_dir="$2"; else executable="$2"; fi
            shift 2 ;;
        -h|--help)
            echo "Usage: bash scripts/run-editor.sh [--build-dir PATH|--exe PATH] [--no-open] [-- EDITOR_ARGUMENTS...]"
            echo "Example: bash scripts/run-editor.sh -- \"/path/to/study.apoproject\""
            exit 0 ;;
        --) shift; editor_args+=("$@"); break ;;
        *) editor_args+=("$1"); shift ;;
    esac
done
apo_start_log editor
if [[ -z "$executable" ]]; then
    for candidate in "$build_dir/apophysis-reborn" "$build_dir/apophysis-reborn.exe" \
        "$build_dir/Release/apophysis-reborn.exe" "$project_dir/bin/windows-x64/apophysis-reborn.exe"; do
        if [[ -f "$candidate" ]]; then executable="$candidate"; break; fi
    done
fi
[[ -n "$executable" && -f "$executable" ]] || {
    echo "No native editor found. Run scripts/build-editor.sh, extract the portable package, or pass --exe PATH." >&2
    exit 1
}
printf "Launching: %s\n" "$executable"
"$executable" "${editor_args[@]}"
