#!/usr/bin/env bash
# Shared logging for development scripts. Source this file; do not execute it.
apo_is_windows() {
    case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) return 0;; *) return 1;; esac
}
apo_open_selected() {
    if apo_is_windows && command -v explorer.exe >/dev/null && command -v cygpath >/dev/null; then
        MSYS2_ARG_CONV_EXCL='*' explorer.exe "/select,$(cygpath -w "$1")" >/dev/null 2>&1 &
    fi
}
apo_start_log() {
    mkdir -p "$project_dir/logs"
    apo_log_file="$project_dir/logs/$1-$(date +%Y%m%d-%H%M%S)-$$.txt"
    exec > >(tee "$apo_log_file") 2>&1
    trap 'apo_status=$?; trap - EXIT; printf "Exit code: %s\nLog: %s\n" "$apo_status" "$apo_log_file"; if "$open_output"; then apo_open_selected "$apo_log_file"; fi; exit "$apo_status"' EXIT
}
apo_require_build_tools() {
    command -v cmake >/dev/null && command -v ctest >/dev/null || {
        echo "Install CMake 3.20 or newer and put both cmake and ctest on PATH." >&2
        echo "See docs/QT_BUILD.md and docs/USER_MANUAL.md for Windows Git Bash setup." >&2
        return 1
    }
}
