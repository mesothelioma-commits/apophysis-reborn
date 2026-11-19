#!/usr/bin/env bash
kit_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P) || exit 2
bash "$kit_root/run-tests.sh" --guided
status=$?
printf '\nTest kit exit code: %s\n' "$status"
read -r -p 'Press Enter to close this window. ' _ || :
exit "$status"
