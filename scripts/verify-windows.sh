#!/usr/bin/env bash
# M9 animation, morphing, color curves and cumulative CPU/export verification.
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
    extended-variation-tests gpu-kernel-tests file-render-tests image-stream-tests \
    project-io-tests legacy-formats-tests script-tests script-host-tests plugin-tests \
    animation-tests animation-export-tests color-curves-tests; do
    [[ -f "$bin/$exe.exe" ]] || { echo "Missing $bin/$exe.exe. Extract the full milestone ZIP before running this check."; exit 1; }
done
plugin_manifest="$bin/plugins/reborn-wave/reborn_wave.apoplugin"
plugin_fixture="$bin/tests/plugins/libplugin-fixture.dll"
plugin_bad_abi="$bin/tests/plugins/libplugin-bad-abi.dll"
for file in "$plugin_manifest" "$bin/plugins/reborn-wave/reborn_wave.dll" "$plugin_fixture" "$plugin_bad_abi"; do
    [[ -f "$file" ]] || { echo "Missing $file. Extract the full milestone ZIP before running this check."; exit 1; }
done
echo "Apophysis Reborn 0.9.0 animation, color and CPU verification / $(date -Iseconds)"
echo "This check explicitly enables the bundled sample_wave and test fixture DLLs, which are native code."
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
"$bin/project-io-tests.exe"
"$bin/legacy-formats-tests.exe" tests/fixtures/formats
"$bin/script-tests.exe"
"$bin/script-host-tests.exe"
"$bin/plugin-tests.exe" "$plugin_manifest" "$plugin_fixture" "$plugin_bad_abi"
"$bin/animation-tests.exe" "$plugin_manifest" "$plugin_fixture"
"$bin/animation-export-tests.exe"
"$bin/color-curves-tests.exe" tests/fixtures/color

# M9 exports use a fixed canvas, full-width fixed seed and global frame numbers.
m9_dir="$run_dir/animation"
mkdir -p "$m9_dir/frames"
"$bin/flame-cpu.exe" animation create examples/ember-spiral.flame --preset camera-orbit \
    --frames 5 --fps 30000/1001 --seed 18446744073709551615 --output "$m9_dir/orbit.apoproject"
"$bin/flame-cpu.exe" animation inspect "$m9_dir/orbit.apoproject" --json | tee "$m9_dir/clip.json"
"$bin/flame-cpu.exe" project extract "$m9_dir/orbit.apoproject" --output "$m9_dir/original.flame"
cmp examples/ember-spiral.flame "$m9_dir/original.flame"
"$bin/flame-cpu.exe" animation frame "$m9_dir/orbit.apoproject" --frame 2 --output "$m9_dir/middle.flame"
"$bin/flame-cpu.exe" animation frame "$m9_dir/orbit.apoproject" --frame 2 \
    --width 160 --height 120 --samples 8195 --threads 1 --format png16 --alpha \
    --output "$m9_dir/middle.png"
"$bin/flame-cpu.exe" animation render "$m9_dir/orbit.apoproject" --output-dir "$m9_dir/frames" \
    --first 0 --last 4 --width 160 --height 120 --samples 8195 --threads 1 \
    --format png16 --alpha --estimate
[[ ! -e "$m9_dir/frames/.apo-sequence-frame" ]] || { echo "Animation preflight created sequence state"; exit 1; }
animation_pause_status=0
"$bin/flame-cpu.exe" animation render "$m9_dir/orbit.apoproject" --output-dir "$m9_dir/frames" \
    --first 0 --last 4 --width 160 --height 120 --samples 8195 --threads 1 \
    --format png16 --alpha --pause-after-frames 2 || animation_pause_status=$?
[[ "$animation_pause_status" -eq 75 ]] || { echo "Expected animation pause exit 75, got $animation_pause_status"; exit 1; }
[[ -s "$m9_dir/frames/frame_000001.png" && ! -e "$m9_dir/frames/frame_000002.png" ]] || {
    echo "Animation pause did not preserve exactly its completed frames"; exit 1;
}
"$bin/flame-cpu.exe" animation render "$m9_dir/orbit.apoproject" --output-dir "$m9_dir/frames" \
    --first 0 --last 4 --width 160 --height 120 --samples 8195 --threads 2 \
    --format png16 --alpha --resume
cmp "$m9_dir/middle.png" "$m9_dir/frames/frame_000002.png"
"$bin/flame-cpu.exe" animation render "$m9_dir/orbit.apoproject" --output-dir "$m9_dir/frames" \
    --first 0 --last 4 --width 160 --height 120 --samples 8195 --threads 1 \
    --format png16 --alpha --resume
cp "$m9_dir/frames/frame_000000.png" "$m9_dir/before-corruption.png"
cp "$m9_dir/frames/.apo-sequence-frame/manifest.aposequence" "$m9_dir/before-corruption.aposequence"
printf 'changed completed image\n' >> "$m9_dir/frames/frame_000000.png"
animation_corrupt_status=0
"$bin/flame-cpu.exe" animation render "$m9_dir/orbit.apoproject" --output-dir "$m9_dir/frames" \
    --first 0 --last 4 --width 160 --height 120 --samples 8195 --threads 1 \
    --format png16 --alpha --resume || animation_corrupt_status=$?
[[ "$animation_corrupt_status" -ne 0 ]] || { echo "Animation resume blindly skipped a changed completed image"; exit 1; }
cmp "$m9_dir/before-corruption.aposequence" "$m9_dir/frames/.apo-sequence-frame/manifest.aposequence"
mv -f "$m9_dir/before-corruption.png" "$m9_dir/frames/frame_000000.png"
"$bin/flame-cpu.exe" animation morph examples/ember-spiral.flame --to "$m9_dir/middle.flame" \
    --frames 5 --easing smootherstep --palette linear-rgb --output "$m9_dir/morph.apoproject"
"$bin/flame-cpu.exe" animation inspect "$m9_dir/morph.apoproject"
"$bin/flame-cpu.exe" --plugin "$plugin_manifest" animation create examples/plugins/reborn-wave/sample-wave.flame \
    --frames 2 --output "$m9_dir/plugin.apoproject"
"$bin/flame-cpu.exe" animation inspect "$m9_dir/plugin.apoproject" --json
"$bin/flame-cpu.exe" --plugin "$plugin_manifest" animation frame "$m9_dir/plugin.apoproject" \
    --frame 1 --width 160 --height 120 --samples 8195 --threads 1 --output "$m9_dir/plugin.png"
for file in orbit.apoproject original.flame middle.flame middle.png morph.apoproject plugin.apoproject plugin.png \
    frames/frame_000000.png frames/frame_000001.png frames/frame_000002.png \
    frames/frame_000003.png frames/frame_000004.png frames/.apo-sequence-frame/manifest.aposequence; do
    [[ -s "$m9_dir/$file" ]] || { echo "M9 verification did not produce $file"; exit 1; }
done

# M8 workflows use isolated outputs and never modify the bundled source files.
m8_dir="$run_dir/extensions"
mkdir -p "$m8_dir"
"$bin/flame-cpu.exe" plugins info "$plugin_manifest"
"$bin/flame-cpu.exe" project create examples/ember-spiral.flame --output "$m8_dir/example.apoproject"
"$bin/flame-cpu.exe" project inspect "$m8_dir/example.apoproject"
"$bin/flame-cpu.exe" project extract "$m8_dir/example.apoproject" --output "$m8_dir/extracted.flame"
cmp examples/ember-spiral.flame "$m8_dir/extracted.flame"
"$bin/flame-cpu.exe" script "$m8_dir/example.apoproject" \
    --script examples/scripts/palette-and-geometry.lua --output "$m8_dir/lua.apoproject"
"$bin/flame-cpu.exe" inspect "$m8_dir/lua.apoproject" --json
"$bin/flame-cpu.exe" script examples/ember-spiral.flame \
    --script examples/scripts/legacy-triangles.asc --output "$m8_dir/legacy-script.flame"
"$bin/flame-cpu.exe" inspect "$m8_dir/legacy-script.flame" --json
"$bin/flame-cpu.exe" --flame "$m8_dir/legacy-script.flame" --size 160 120 \
    --samples 8195 --threads 1 --backend cpu --output "$m8_dir/legacy-script.png"
"$bin/flame-cpu.exe" palette-export "$m8_dir/lua.apoproject" --output "$m8_dir/palette.ugr"
"$bin/flame-cpu.exe" palette-export "$m8_dir/lua.apoproject" --output "$m8_dir/palette.map"
"$bin/flame-cpu.exe" palette-import "$m8_dir/palette.ugr" --flame examples/ember-spiral.flame \
    --output "$m8_dir/ugr-roundtrip.flame"
"$bin/flame-cpu.exe" palette-export "$m8_dir/ugr-roundtrip.flame" --output "$m8_dir/ugr-roundtrip.map"
cmp "$m8_dir/palette.map" "$m8_dir/ugr-roundtrip.map"
"$bin/flame-cpu.exe" upr-import examples/classic-upr-study.upr --output "$m8_dir/upr.flame"
"$bin/flame-cpu.exe" upr-export "$m8_dir/upr.flame" --output "$m8_dir/roundtrip.upr"
"$bin/flame-cpu.exe" --plugin "$plugin_manifest" --flame examples/plugins/reborn-wave/sample-wave.flame \
    --size 160 120 --samples 8195 --threads 1 --backend cpu --output "$m8_dir/plugin.png"
cat > "$m8_dir/plugin-requests.lua" <<'LUA'
apo.set_flame {name = 'Windows plugin script'}
local t = apo.transform(1)
t.variations = {
  {name = 'linear', weight = 0.8},
  {name = 'sample_wave', weight = 0.2, parameters = {amplitude = 0.18, frequency = 3}}
}
apo.set_transform(1, t)
apo.export_flame('plugin-script.flame')
apo.render('plugin-script.png', {width=160,height=120,samples=8195,
  seed='18446744073709551615',threads=1,backend='cpu',format='png16',alpha=true})
LUA
"$bin/flame-cpu.exe" --plugin "$plugin_manifest" script examples/ember-spiral.flame \
    --script "$m8_dir/plugin-requests.lua" --output "$m8_dir/plugin-result.apoproject" \
    --allow-renders --allow-exports --output-dir "$m8_dir"
"$bin/flame-cpu.exe" --plugin "$plugin_manifest" inspect "$m8_dir/plugin-script.flame" --json
for file in example.apoproject lua.apoproject legacy-script.flame legacy-script.png palette.ugr \
    palette.map upr.flame roundtrip.upr plugin.png plugin-result.apoproject plugin-script.flame plugin-script.png; do
    [[ -s "$m8_dir/$file" ]] || { echo "M8 verification did not produce $file"; exit 1; }
done
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
sha256sum "$bin/"*.exe "$bin/plugins/reborn-wave/reborn_wave.dll" "$plugin_fixture" "$plugin_bad_abi" "$run_dir/ember-4.bmp"
echo "Passed native timelines, explicit morphs, color curves, verified animation pause/resume, native projects, Lua/legacy scripts, UGR/MAP/UPR exchange, trusted native plugin contracts, historical pixels, legacy preservation, creative algorithms, CPU references, bounded file rendering, all image formats, true 16-bit/alpha export and committed pause/resume on this computer."
echo "The 16K check above validates the storage plan; it does not render a 16K image."
echo "No Vulkan execution is required or claimed by this CPU verification script."
echo "Device inventory: $run_dir/devices.json"
echo "Optional strict Vulkan execution check: bash scripts/verify-gpu-windows.sh"
echo "Render: $run_dir/ember-4.bmp"
if [[ "${1:-}" != "--no-open" ]]; then
    MSYS2_ARG_CONV_EXCL='*' explorer.exe "/select,$(cygpath -w "$run_dir/ember-4.bmp")" >/dev/null 2>&1 &
fi
