#!/usr/bin/env bash
# Git Bash entry point; PowerShell owns the lifecycle report and .txt transcript.
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
installer=""
payload=""
previous_installer=""
out_directory=""
extra=()
usage() {
    echo "Usage: bash /path/to/scripts/verify-installer-windows.sh --installer SETUP.exe --payload RUNTIME_DIR --out NEW_SHORT_DIR [--previous-installer OLD_SETUP.exe] [--no-open]"
    echo "Runs on Windows x64 in Git Bash. Keeps installer-lifecycle.json and .txt logs."
}
while [[ $# -gt 0 ]]; do
    case "$1" in
        --installer|--payload|--out|--previous-installer)
            [[ $# -ge 2 ]] || { usage >&2; exit 2; }
            case "$1" in
                --installer) installer="$2" ;;
                --payload) payload="$2" ;;
                --previous-installer) previous_installer="$2" ;;
                --out) out_directory="$2" ;;
            esac
            shift 2 ;;
        --no-open) extra+=(-NoOpen); shift ;;
        --help|-h) usage; exit 0 ;;
        *) usage >&2; exit 2 ;;
    esac
done
[[ -n "$installer" && -n "$payload" && -n "$out_directory" ]] || { usage >&2; exit 2; }
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) ;;
    *) echo "Run this verifier in Git Bash on Windows x64." >&2; exit 2 ;;
esac
command -v cygpath >/dev/null || { echo "Git Bash cygpath is required." >&2; exit 2; }
command -v powershell.exe >/dev/null || { echo "64-bit Windows PowerShell is required." >&2; exit 2; }
if [[ -n "$previous_installer" ]]; then
    extra+=(-PreviousInstaller "$(cygpath -aw -- "$previous_installer")")
fi
MSYS2_ARG_CONV_EXCL='*' powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass \
    -File "$(cygpath -aw -- "$project_dir/scripts/verify-installer-windows.ps1")" \
    -Installer "$(cygpath -aw -- "$installer")" \
    -Payload "$(cygpath -aw -- "$payload")" \
    -OutDirectory "$(cygpath -aw -- "$out_directory")" "${extra[@]}"
