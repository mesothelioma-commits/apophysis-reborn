#!/usr/bin/env bash
# Run from Git Bash after extracting the complete milestone ZIP.
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$project_dir"
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) ;; *) echo "Run this check in Git Bash on Windows." >&2; exit 2;; esac
mkdir -p logs out
run_dir="$(mktemp -d "$project_dir/out/verification-XXXXXXXX")"
log="$project_dir/logs/$(basename "$run_dir").txt"
exec > >(tee "$log") 2>&1
trap 'status=$?; echo "Exit code: $status"; echo "Log: $log"; exit "$status"' EXIT
bin=./bin/windows-x64
for exe in flame-cpu reference-tests import-tests renderer-tests legacy-document-tests creative-tests extended-variation-tests gpu-kernel-tests; do
    [[ -f "$bin/$exe.exe" ]] || { echo "Missing $bin/$exe.exe. Extract the full milestone ZIP before running this check."; exit 1; }
done
echo "Apophysis Reborn 0.6.0 / $(date -Iseconds)"
cmd.exe //c ver
"$bin/flame-cpu.exe" --list-devices --json | tee "$run_dir/devices.json"
"$bin/reference-tests.exe" tests/reference-vectors.tsv tests/reference-geometry.flame tests/reference-image.flame tests/reference-image.ppm
"$bin/import-tests.exe"
"$bin/renderer-tests.exe"
"$bin/legacy-document-tests.exe"
"$bin/creative-tests.exe"
"$bin/extended-variation-tests.exe" tests/extended-variation-vectors.tsv examples/variation-studies.flame
"$bin/gpu-kernel-tests.exe" --cpu-only
"$bin/flame-cpu.exe" inspect tests/legacy/fox-3d-hack.flame --json
"$bin/flame-cpu.exe" save tests/legacy/fox-3d-hack.flame --output "$run_dir/legacy-copy.flame"
cmp tests/legacy/fox-3d-hack.flame "$run_dir/legacy-copy.flame"
"$bin/flame-cpu.exe" save "$run_dir/legacy-copy.flame" --name "Windows round trip" --output "$run_dir/renamed.flame"
"$bin/flame-cpu.exe" inspect "$run_dir/renamed.flame" --json
idx=0
for scene in direct low gaussian even wide; do
    idx=$((idx+1))
    "$bin/flame-cpu.exe" --flame tests/legacy/producer-oracle.flame --index "$idx" --threads 1 --output "$run_dir/$scene.ppm"
    cmp "tests/legacy/producer-oracle-$scene.ppm" "$run_dir/$scene.ppm"
done
"$bin/flame-cpu.exe" 64 64 100000 "$run_dir/foundation.ppm"
"$bin/flame-cpu.exe" --flame examples/ember-spiral.flame --output "$run_dir/ember-1.bmp" --seed 123456 --threads 1
"$bin/flame-cpu.exe" --flame examples/ember-spiral.flame --output "$run_dir/ember-4.bmp" --seed 123456 --threads 4
cmp "$run_dir/ember-1.bmp" "$run_dir/ember-4.bmp"
"$bin/flame-cpu.exe" --flame examples/ember-spiral.flame --output "$run_dir/ember-fallback.bmp" \
    --seed 123456 --threads 1 --backend auto --device __apophysis_missing_device__
cmp "$run_dir/ember-1.bmp" "$run_dir/ember-fallback.bmp"
read -r width height < <(od -An -t u4 -j18 -N8 "$run_dir/ember-4.bmp")
[[ "$width" == 768 && "$height" == 576 ]] || { echo "Unexpected BMP dimensions: $width x $height"; exit 1; }
[[ "$(wc -c < "$run_dir/ember-4.bmp")" -eq $((54 + 768 * 576 * 3)) ]] || { echo "BMP size check failed"; exit 1; }
sha256sum "$bin/"*.exe "$run_dir/ember-4.bmp"
echo "Passed historical pixels, legacy preservation, creative algorithms, variation references, portable compute packing/RNG, CPU fallback and BMP replay on this computer."
echo "Device inventory: $run_dir/devices.json"
echo "Optional strict Vulkan execution check: bash scripts/verify-gpu-windows.sh"
echo "Render: $run_dir/ember-4.bmp"
if [[ "${1:-}" != "--no-open" ]]; then
    MSYS2_ARG_CONV_EXCL='*' explorer.exe "/select,$(cygpath -w "$run_dir/ember-4.bmp")" >/dev/null 2>&1 &
fi
