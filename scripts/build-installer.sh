#!/usr/bin/env bash
# Run from any directory. Python, NSIS >=3.13 and a MinGW-w64 C++20 compiler are required.
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
arguments=()
open_output=true
installer_output=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --no-open) open_output=false; shift ;;
        --output)
            [[ $# -ge 2 ]] || { echo "--output needs an .exe filename" >&2; exit 2; }
            installer_output="$2"; arguments+=("$1" "$2"); shift 2 ;;
        *) arguments+=("$1"); shift ;;
    esac
done
log_directory="${APO_LOG_DIR:-$project_dir/logs}"
mkdir -p "$log_directory"
installer_log="$log_directory/installer-build-$(date +%Y%m%d-%H%M%S)-$$.txt"
exec > >(tee "$installer_log") 2>&1
finish_installer_build() {
    local status=$?
    echo "Exit code: $status"
    echo "Build log: $installer_log"
    if [[ $status == 0 && -f "$installer_output" ]] && "$open_output" &&
       command -v explorer.exe >/dev/null && command -v cygpath >/dev/null; then
        MSYS2_ARG_CONV_EXCL='*' explorer.exe "/select,$(cygpath -w -- "$installer_output")" >/dev/null 2>&1 &
    fi
    exit "$status"
}
trap finish_installer_build EXIT
command -v python3 >/dev/null || { echo "Install Python 3 and make python3 available in Git Bash."; exit 2; }
python3 "$project_dir/packaging/build_installer.py" "${arguments[@]}"
