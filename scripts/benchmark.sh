#!/usr/bin/env bash
# Reproducible renderer measurements, from any working directory.
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
if command -v python3 >/dev/null 2>&1; then
    python=(python3)
elif command -v py >/dev/null 2>&1; then
    python=(py -3)
elif command -v python >/dev/null 2>&1; then
    python=(python)
else
    echo "Python 3.10 or newer is required. Install Python with its launcher/PATH option, then run this script again." >&2
    exit 2
fi
"${python[@]}" -c 'import sys; raise SystemExit(0 if sys.version_info >= (3, 10) else 2)' || {
    echo "Python 3.10 or newer is required." >&2; exit 2;
}
exec "${python[@]}" "$project_dir/scripts/benchmark.py" "$@"
