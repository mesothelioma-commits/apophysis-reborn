#!/usr/bin/env bash
# Run from Git Bash after extracting the complete desktop-editor milestone ZIP.
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$project_dir"
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) ;; *) echo "Run this check in Git Bash on Windows." >&2; exit 2;; esac
mkdir -p logs out
run_dir="$(mktemp -d "$project_dir/out/editor-verification-XXXXXXXX")"
log="$project_dir/logs/$(basename "$run_dir").txt"
exec > >(tee "$log") 2>&1
trap 'status=$?; echo "Exit code: $status"; echo "Log: $log"; exit "$status"' EXIT
bin=./bin/windows-x64
for file in apophysis-reborn.exe editor-tests.exe creative-editor-tests.exe preview-tests.exe document-session-tests.exe canvas-tests.exe Qt6Core.dll Qt6Gui.dll Qt6Widgets.dll Qt6Test.dll platforms/qwindows.dll; do
    [[ -f "$bin/$file" ]] || { echo "Missing $bin/$file. Extract the complete milestone ZIP."; exit 1; }
done
echo "Apophysis Reborn 0.5.0 desktop verification / $(date -Iseconds)"
cmd.exe //c ver
bash scripts/verify-windows.sh --no-open
"$bin/document-session-tests.exe"
"$bin/preview-tests.exe"
"$bin/canvas-tests.exe"
"$bin/editor-tests.exe"
"$bin/creative-editor-tests.exe"
"$bin/apophysis-reborn.exe" --creative-smoke-test "$run_dir"
for file in editor.png preview.png saved-example.flame \
    gradient-editor.png adjustment-editor.png mutation-explorer.png random-explorer.png \
    gradient-example.flame adjustment-example.flame mutation-example.flame random-batch.flame palette.gpl; do
    [[ -s "$run_dir/$file" ]] || { echo "Smoke test did not produce $file"; exit 1; }
done
"$bin/flame-cpu.exe" inspect "$run_dir/saved-example.flame" --json
"$bin/flame-cpu.exe" inspect "$run_dir/random-batch.flame" --json
"$bin/flame-cpu.exe" inspect "$run_dir/mutation-example.flame" --json
sha256sum "$bin/apophysis-reborn.exe" "$bin/Qt6Core.dll" "$bin/Qt6Gui.dll" "$bin/Qt6Widgets.dll"
echo "Desktop checks completed. Smoke-test output: $run_dir"
if [[ "${1:-}" != "--no-open" ]]; then
    MSYS2_ARG_CONV_EXCL='*' explorer.exe "$(cygpath -w "$run_dir")" >/dev/null 2>&1 &
fi
