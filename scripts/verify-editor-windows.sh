#!/usr/bin/env bash
# M9 desktop timeline, color curves and cumulative render-queue verification.
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$project_dir"
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) ;; *) echo "Run this check in Git Bash on Windows." >&2; exit 2;; esac
open_output=true
large_render=false
while [[ $# -gt 0 ]]; do
    case "$1" in
        --no-open) open_output=false; shift ;;
        --large-render) large_render=true; shift ;;
        *) echo "Usage: bash scripts/verify-editor-windows.sh [--large-render] [--no-open]" >&2; exit 2 ;;
    esac
done
mkdir -p logs out
run_dir="$(mktemp -d "$project_dir/out/editor-verification-XXXXXXXX")"
log="$project_dir/logs/$(basename "$run_dir").txt"
exec > >(tee "$log") 2>&1
trap 'status=$?; echo "Exit code: $status"; echo "Log: $log"; exit "$status"' EXIT
bin=./bin/windows-x64
for file in apophysis-reborn.exe editor-tests.exe creative-editor-tests.exe preview-tests.exe \
    document-session-tests.exe canvas-tests.exe render-queue-tests.exe Qt6Core.dll Qt6Gui.dll \
    Qt6Widgets.dll Qt6Test.dll platforms/qwindows.dll extensions-editor-tests.exe \
    plugin-dialog-tests.exe animation-editor-tests.exe color-editor-tests.exe \
    plugins/reborn-wave/reborn_wave.dll plugins/reborn-wave/reborn_wave.apoplugin; do
    [[ -f "$bin/$file" ]] || { echo "Missing $bin/$file. Extract the complete milestone ZIP."; exit 1; }
done
plugin_manifest="$bin/plugins/reborn-wave/reborn_wave.apoplugin"
echo "Apophysis Reborn 0.9.0 desktop timeline, color and render queue verification / $(date -Iseconds)"
cmd.exe //c ver
bash scripts/verify-windows.sh --no-open
"$bin/document-session-tests.exe"
"$bin/preview-tests.exe"
"$bin/canvas-tests.exe"
"$bin/editor-tests.exe"
"$bin/creative-editor-tests.exe"
"$bin/render-queue-tests.exe"
"$bin/extensions-editor-tests.exe"
"$bin/plugin-dialog-tests.exe" "$plugin_manifest"
"$bin/animation-editor-tests.exe"
"$bin/color-editor-tests.exe"
"$bin/apophysis-reborn.exe" --creative-smoke-test "$run_dir" --backend cpu
"$bin/apophysis-reborn.exe" --queue-smoke-test "$run_dir/queue" --backend cpu
"$bin/apophysis-reborn.exe" --extensions-smoke-test "$run_dir/extensions" \
    --plugin-manifest "$plugin_manifest" --backend cpu
"$bin/apophysis-reborn.exe" --animation-smoke-test "$run_dir/animation" --backend cpu
for file in editor.png preview.png saved-example.flame \
    gradient-editor.png adjustment-editor.png mutation-explorer.png random-explorer.png \
    gradient-example.flame adjustment-example.flame mutation-example.flame random-batch.flame palette.gpl; do
    [[ -s "$run_dir/$file" ]] || { echo "Smoke test did not produce $file"; exit 1; }
done
for file in render-queue.png render-queue.json queue-report.json high-resolution-settings.png \
    queue-render-16.png queue-render-16.tif; do
    [[ -s "$run_dir/queue/$file" ]] || { echo "Queue smoke test did not produce $file"; exit 1; }
done
cat "$run_dir/queue/queue-report.json"
for file in script-editor.png plugin-manager.png script-render-queue.png extensions-editor.png \
    extensions-report.json example.apoproject script-export.flame script-palette.gpl script-render.png \
    legacy-script.flame legacy-translated.lua palette.ugr palette.map; do
    [[ -s "$run_dir/extensions/$file" ]] || { echo "Extensions smoke test did not produce $file"; exit 1; }
done
cat "$run_dir/extensions/extensions-report.json"
"$bin/flame-cpu.exe" project inspect "$run_dir/extensions/example.apoproject"
"$bin/flame-cpu.exe" --plugin "$plugin_manifest" inspect "$run_dir/extensions/script-export.flame" --json
"$bin/flame-cpu.exe" --plugin "$plugin_manifest" inspect "$run_dir/extensions/legacy-script.flame" --json
for file in editor.png preview.png saved-example.flame timeline.png timeline-frame.png color-curves.png \
    animation-queue.png animation.apoproject animation-queue.json animation-report.json \
    frames/frame_000000.png frames/frame_000006.png frames/frame_000012.png \
    frames/frame_000018.png frames/frame_000024.png; do
    [[ -s "$run_dir/animation/$file" ]] || { echo "Animation smoke test did not produce $file"; exit 1; }
done
cat "$run_dir/animation/animation-report.json"
"$bin/flame-cpu.exe" animation inspect "$run_dir/animation/animation.apoproject" --json
if "$large_render"; then
    echo "Starting actual 16384x16384 PNG RGBA16 queue render, pause and resume with a 128 MiB buffer budget."
    "$bin/apophysis-reborn.exe" --large-render-smoke-test "$run_dir/16k" --backend cpu
    for file in 16k.png 16k-report.json 16k-queue-paused.png 16k-queue-complete.png \
        16k-editor-complete.png paused-queue.json completed-queue.json; do
        [[ -s "$run_dir/16k/$file" ]] || { echo "16K acceptance test did not produce $file"; exit 1; }
    done
    cat "$run_dir/16k/16k-report.json"
else
    echo "Actual 16K queue acceptance was not requested; enable it with --large-render."
fi
"$bin/flame-cpu.exe" inspect "$run_dir/saved-example.flame" --json
"$bin/flame-cpu.exe" inspect "$run_dir/random-batch.flame" --json
"$bin/flame-cpu.exe" inspect "$run_dir/mutation-example.flame" --json
sha256sum "$bin/apophysis-reborn.exe" "$bin/Qt6Core.dll" "$bin/Qt6Gui.dll" "$bin/Qt6Widgets.dll"
echo "Desktop checks completed: timeline editing/undo, native animation persistence, color curves, globally numbered queued sequences, private Lua/legacy editing, trusted plugins and PNG RGBA16/TIFF RGB16 rendering."
echo "Smoke-test output: $run_dir"
echo "Vulkan execution is a separate optional gate: bash scripts/verify-gpu-windows.sh"
if "$open_output"; then
    MSYS2_ARG_CONV_EXCL='*' explorer.exe "$(cygpath -w "$run_dir")" >/dev/null 2>&1 &
fi
