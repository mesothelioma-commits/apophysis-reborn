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
for exe in flame-cpu reference-tests import-tests renderer-tests; do
    [[ -f "$bin/$exe.exe" ]] || { echo "Missing $bin/$exe.exe. Extract the full milestone ZIP before running this check."; exit 1; }
done
echo "Apophysis Reborn 0.2.0 / $(date -Iseconds)"
cmd.exe //c ver
"$bin/reference-tests.exe" tests/reference-vectors.tsv tests/reference-geometry.flame tests/reference-image.flame tests/reference-image.ppm
"$bin/import-tests.exe"
"$bin/renderer-tests.exe"
"$bin/flame-cpu.exe" 64 64 100000 "$run_dir/foundation.ppm"
"$bin/flame-cpu.exe" --flame examples/ember-spiral.flame --output "$run_dir/ember-1.bmp" --seed 123456 --threads 1
"$bin/flame-cpu.exe" --flame examples/ember-spiral.flame --output "$run_dir/ember-4.bmp" --seed 123456 --threads 4
cmp "$run_dir/ember-1.bmp" "$run_dir/ember-4.bmp"
read -r width height < <(od -An -t u4 -j18 -N8 "$run_dir/ember-4.bmp")
[[ "$width" == 768 && "$height" == 576 ]] || { echo "Unexpected BMP dimensions: $width x $height"; exit 1; }
[[ "$(wc -c < "$run_dir/ember-4.bmp")" -eq $((54 + 768 * 576 * 3)) ]] || { echo "BMP size check failed"; exit 1; }
sha256sum "$bin/"*.exe "$run_dir/ember-4.bmp"
echo "Passed reference vectors, XML import, renderer, foundation CLI and BMP replay on this computer."
echo "Render: $run_dir/ember-4.bmp"
if [[ "${1:-}" != "--no-open" ]]; then
    MSYS2_ARG_CONV_EXCL='*' explorer.exe "/select,$(cygpath -w "$run_dir/ember-4.bmp")" >/dev/null 2>&1 &
fi
