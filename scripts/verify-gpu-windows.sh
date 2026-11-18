#!/usr/bin/env bash
# M7 strict selected-device Vulkan, bounded streaming and render queue verification.
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$project_dir"
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) ;; *) echo "Run this check in Git Bash on Windows." >&2; exit 2;; esac
open_output=true
device_args=()
gpu_memory=512
while [[ $# -gt 0 ]]; do
    case "$1" in
        --no-open) open_output=false; shift ;;
        --device)
            [[ $# -ge 2 && -n "$2" ]] || { echo "--device requires a discovery ID or zero-based index." >&2; exit 2; }
            device_args=(--device "$2"); shift 2 ;;
        --gpu-memory-mib)
            [[ $# -ge 2 && "$2" =~ ^[1-9][0-9]*$ ]] || { echo "--gpu-memory-mib requires a positive integer." >&2; exit 2; }
            gpu_memory="$2"; shift 2 ;;
        *) echo "Usage: bash scripts/verify-gpu-windows.sh [--device ID|INDEX] [--gpu-memory-mib N] [--no-open]" >&2; exit 2 ;;
    esac
done
mkdir -p logs out
run_dir="$(mktemp -d "$project_dir/out/gpu-verification-XXXXXXXX")"
log="$project_dir/logs/$(basename "$run_dir").txt"
exec > >(tee "$log") 2>&1
trap 'status=$?; echo "Exit code: $status"; echo "Log: $log"; exit "$status"' EXIT
bin=./bin/windows-x64
for file in flame-cpu.exe gpu-tests.exe gpu-kernel-tests.exe gpu-stream-tests.exe file-render-tests.exe \
    apophysis-reborn.exe \
    Qt6Core.dll Qt6Gui.dll Qt6Widgets.dll platforms/qwindows.dll; do
    [[ -f "$bin/$file" ]] || { echo "Missing $bin/$file. Extract the complete milestone ZIP."; exit 1; }
done
echo "Apophysis Reborn 0.7.0 strict Vulkan streaming verification / $(date -Iseconds)"
cmd.exe //c ver
"$bin/flame-cpu.exe" --list-devices --json | tee "$run_dir/devices.json"
"$bin/flame-cpu.exe" --list-devices
if [[ "${#device_args[@]}" -eq 0 ]]; then
    command -v powershell.exe >/dev/null || {
        echo "PowerShell is unavailable for automatic selection. Re-run with --device ID from the inventory."; exit 1;
    }
    if ! selected_device="$(APO_GPU_REPORT="$(cygpath -w "$run_dir/devices.json")" powershell.exe -NoProfile -NonInteractive -Command \
        '$report = Get-Content -LiteralPath $env:APO_GPU_REPORT -Raw -Encoding UTF8 | ConvertFrom-Json; $gpu = @($report.devices | Where-Object { $_.usable -and -not $_.software })[0]; if ($null -eq $gpu) { exit 3 }; [Console]::Write($gpu.id)')"; then
        echo "No usable physical GPU was found. Strict Vulkan verification did not pass."
        echo "A software Vulkan device can be tested explicitly with --device ID; it is reported as software."
        exit 1
    fi
    [[ -n "$selected_device" ]] || { echo "Device selection returned no ID."; exit 1; }
    device_args=(--device "$selected_device")
fi
echo "Selected device: ${device_args[1]}"

require_gpu() {
    local status=0
    "$@" || status=$?
    if [[ "$status" -eq 77 ]]; then
        echo "Vulkan execution was skipped because no usable device was found. This strict check did not pass."
        return 1
    fi
    return "$status"
}

require_gpu "$bin/gpu-kernel-tests.exe" --require-vulkan "${device_args[@]}"
require_gpu "$bin/gpu-tests.exe" --require-vulkan "${device_args[@]}"
require_gpu "$bin/gpu-stream-tests.exe" --require-vulkan "${device_args[@]}"
require_gpu "$bin/file-render-tests.exe" --require-vulkan "${device_args[@]}"
"$bin/flame-cpu.exe" --flame examples/ember-spiral.flame --samples 262145 --seed 123456 \
    --backend vulkan --no-fallback "${device_args[@]}" --gpu-memory-mib "$gpu_memory" \
    --output "$run_dir/vulkan-ember.bmp"
"$bin/apophysis-reborn.exe" --gpu-smoke-test "$run_dir" --backend vulkan --no-fallback \
    "${device_args[@]}" --gpu-memory-mib "$gpu_memory"
"$bin/flame-cpu.exe" --flame examples/ember-spiral.flame --samples 65539 --size 192 144 \
    --backend vulkan --no-fallback "${device_args[@]}" --gpu-memory-mib "$gpu_memory" \
    --format png16 --alpha --strip-rows 7 --output "$run_dir/vulkan-rgba16.png"
"$bin/apophysis-reborn.exe" --queue-smoke-test "$run_dir/queue" --backend vulkan --no-fallback \
    "${device_args[@]}" --gpu-memory-mib "$gpu_memory"
for file in devices.json vulkan-ember.bmp editor.png preview.png saved-example.flame \
    render-device.json render-settings.png vulkan-rgba16.png queue/render-queue.png \
    queue/queue-report.json queue/queue-render-16.png queue/queue-render-16.tif; do
    [[ -s "$run_dir/$file" ]] || { echo "Strict Vulkan check did not produce $file"; exit 1; }
done
cat "$run_dir/render-device.json"
cat "$run_dir/queue/queue-report.json"
"$bin/flame-cpu.exe" inspect "$run_dir/saved-example.flame" --json
sha256sum "$bin/"*.exe "$run_dir/vulkan-ember.bmp" "$run_dir/render-device.json"
echo "Strict Vulkan formulas, bounded hit streaming, exact file reconstruction, checkpoint identity and queued 16-bit exports completed on the selected device."
echo "The device report identifies software Vulkan implementations; those do not establish physical GPU performance."
echo "Output: $run_dir"
if "$open_output"; then
    MSYS2_ARG_CONV_EXCL='*' explorer.exe "$(cygpath -w "$run_dir")" >/dev/null 2>&1 &
fi
