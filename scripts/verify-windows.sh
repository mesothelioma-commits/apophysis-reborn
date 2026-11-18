#!/usr/bin/env bash
# M7 high-resolution CPU/export/checkpoint verification in Git Bash on Windows.
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
for exe in flame-cpu reference-tests import-tests renderer-tests legacy-document-tests creative-tests \
    extended-variation-tests gpu-kernel-tests file-render-tests image-stream-tests; do
    [[ -f "$bin/$exe.exe" ]] || { echo "Missing $bin/$exe.exe. Extract the full milestone ZIP before running this check."; exit 1; }
done
echo "Apophysis Reborn 0.7.0 CPU and file-export verification / $(date -Iseconds)"
cmd.exe //c ver
"$bin/flame-cpu.exe" --list-devices --json | tee "$run_dir/devices.json"
"$bin/reference-tests.exe" tests/reference-vectors.tsv tests/reference-geometry.flame tests/reference-image.flame tests/reference-image.ppm
"$bin/import-tests.exe"
"$bin/renderer-tests.exe"
"$bin/legacy-document-tests.exe"
"$bin/creative-tests.exe"
"$bin/extended-variation-tests.exe" tests/extended-variation-vectors.tsv examples/variation-studies.flame
"$bin/gpu-kernel-tests.exe" --cpu-only
"$bin/image-stream-tests.exe"
"$bin/file-render-tests.exe" --cpu-only
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
read -r width height < <(od -An -t d4 -j18 -N8 "$run_dir/ember-4.bmp")
[[ "$width" == 768 && "$height" == -576 ]] || { echo "Unexpected top-down BMP dimensions: $width x $height"; exit 1; }
[[ "$(wc -c < "$run_dir/ember-4.bmp")" -eq $((54 + 768 * 576 * 3)) ]] || { echo "BMP size check failed"; exit 1; }
for format in bmp ppm ppm16 png png16 tiff tiff16; do
    extension="$format"
    case "$format" in ppm16) extension=ppm;; png16) extension=png;; tiff16) extension=tiff;; esac
    "$bin/flame-cpu.exe" --flame examples/ember-spiral.flame --size 192 144 --samples 8195 \
        --threads 2 --backend cpu --memory-mib 32 --strip-rows 7 --format "$format" \
        --output "$run_dir/stream-$format.$extension"
    [[ -s "$run_dir/stream-$format.$extension" ]] || { echo "Missing $format output"; exit 1; }
done
for format in png16 tiff16; do
    extension="${format%16}"
    "$bin/flame-cpu.exe" --flame examples/ember-spiral.flame --size 192 144 --samples 8195 \
        --backend cpu --format "$format" --alpha --output "$run_dir/rgba16.$extension"
done
"$bin/flame-cpu.exe" --flame examples/ember-spiral.flame --size 16384 16384 --samples 8195 \
    --oversample 1 --filter-radius 0.5 --memory-mib 64 --disk-mib 4096 --backend cpu \
    --output "$run_dir/16k-estimate.png" --estimate
[[ ! -e "$run_dir/16k-estimate.png" ]] || { echo "Estimate unexpectedly wrote an image"; exit 1; }
printf 'existing destination\n' > "$run_dir/resumed.ppm"
cp "$run_dir/resumed.ppm" "$run_dir/before-pause.txt"
pause_status=0
"$bin/flame-cpu.exe" --flame examples/ember-spiral.flame --size 192 144 --samples 65539 \
    --threads 1 --backend cpu --strip-rows 7 --checkpoint "$run_dir/checkpoint" --pause-after 1 \
    --output "$run_dir/resumed.ppm" || pause_status=$?
[[ "$pause_status" -eq 75 ]] || { echo "Expected pause exit 75, got $pause_status"; exit 1; }
cmp "$run_dir/before-pause.txt" "$run_dir/resumed.ppm"
[[ -s "$run_dir/checkpoint/manifest.apocheckpoint" && -s "$run_dir/checkpoint/chunk-0.apohits" ]] || {
    echo "Pause did not leave committed checkpoint data"; exit 1;
}
"$bin/flame-cpu.exe" --flame examples/ember-spiral.flame --size 192 144 --samples 65539 \
    --threads 4 --backend cpu --strip-rows 7 --checkpoint "$run_dir/checkpoint" --resume \
    --output "$run_dir/resumed.ppm"
"$bin/flame-cpu.exe" --flame examples/ember-spiral.flame --size 192 144 --samples 65539 \
    --threads 1 --backend cpu --strip-rows 7 --output "$run_dir/fresh.ppm"
cmp "$run_dir/resumed.ppm" "$run_dir/fresh.ppm"
sha256sum "$bin/"*.exe "$run_dir/ember-4.bmp"
echo "Passed historical pixels, legacy preservation, creative algorithms, CPU references, bounded file rendering, all image formats, true 16-bit/alpha export and committed pause/resume on this computer."
echo "The 16K check above validates the storage plan; it does not render a 16K image."
echo "No Vulkan execution is required or claimed by this CPU verification script."
echo "Device inventory: $run_dir/devices.json"
echo "Optional strict Vulkan execution check: bash scripts/verify-gpu-windows.sh"
echo "Render: $run_dir/ember-4.bmp"
if [[ "${1:-}" != "--no-open" ]]; then
    MSYS2_ARG_CONV_EXCL='*' explorer.exe "/select,$(cygpath -w "$run_dir/ember-4.bmp")" >/dev/null 2>&1 &
fi
