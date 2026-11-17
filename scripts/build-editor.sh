#!/usr/bin/env bash
# Native C++20 build with Qt 6 Widgets/Test, CMake and Expat development files.
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ -n "${APO_QT_PREFIX:-}" ]]; then
    set -- "-DCMAKE_PREFIX_PATH=$APO_QT_PREFIX" "$@"
fi
bash "$project_dir/scripts/build.sh" -DAPO_BUILD_EDITOR=ON "$@"
