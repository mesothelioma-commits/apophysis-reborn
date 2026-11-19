# shellcheck shell=bash
# Loaded by the Windows test kit. All destinations are in this run's private folder.

# Read the final measurement only. Include the full renderer line in the TXT report
# so backend, device, sampling and reconstruction times travel with the summary.
kit_render_line() {
    local log="$1" backend="$2" line
    line=$(grep -E '^Rendered [0-9]+x[0-9]+ ' "$log" | tail -n 1 | tr -d '\r') || return 1
    [[ " $line " == *" backend=$backend "* ]] || return 1
    printf '%s\n' "$line"
}

kit_render_small() {
    local id="$1" index="$2" backend="$3" output="$4"
    shift 4
    local measurement
    if ! run_cmd "$id" 150 "$CLI" --flame "$STARTER" --index "$index" --size 320 240 \
        --samples 500000 --seed 123456 --threads 2 --oversample 1 --memory-mib 192 \
        --disk-mib 384 --format png --backend "$backend" --output "$output" "$@"; then
        record FAIL "$id" "Render command failed or timed out. See logs/$id.txt."
        return 1
    fi
    if ! measurement=$(kit_render_line "$RUN/logs/$id.txt" "$backend"); then
        record FAIL "$id" "Expected actual backend=$backend in the completed render record."
        return 1
    fi
    if [[ $backend == vulkan && $measurement == *'(software Vulkan)'* ]]; then
        record FAIL "$id" "The completed render reported a software Vulkan device where a physical card was requested."
        return 1
    fi
    if ! check_png "$output" 320 240 >"$RUN/logs/$id-image.txt" 2>&1; then
        record FAIL "$id" "PNG failed the 320 x 240 image checks. See logs/$id-image.txt."
        return 1
    fi
    record PASS "$id" "$measurement"
}

kit_project_roundtrip() {
    local dir="$RUN/outputs"
    if run_cmd project-create 45 "$CLI" project create "$STARTER" --index 3 --output "$dir/selected.apoproject" &&
        run_cmd project-extract 45 "$CLI" project extract "$dir/selected.apoproject" --output "$dir/extracted.flame" &&
        cmp -s "$STARTER" "$dir/extracted.flame"; then
        record PASS project-roundtrip "All eight starter definitions survived an .apoproject save and extraction byte-for-byte."
    else
        record FAIL project-roundtrip "Project creation, extraction or exact byte comparison failed."
        record SKIP project-edit "Requires a verified test project."
        return
    fi

    # The unique third entry is selected in the project. Only its name may change.
    if run_cmd project-edit 45 "$CLI" save "$dir/selected.apoproject" --output "$dir/edited.apoproject" --name 'QA Ruby rosette' &&
        run_cmd project-reopen 45 "$CLI" project extract "$dir/edited.apoproject" --output "$dir/edited.flame" &&
        run_cmd project-reinspect 45 "$CLI" inspect "$dir/edited.apoproject" --json &&
        grep -Eq '"index":[[:space:]]*3,[[:space:]]*"name":[[:space:]]*"QA Ruby rosette"' "$RUN/logs/project-reinspect.txt" &&
        sed -b 's/QA Ruby rosette/Ruby rosette/' "$dir/edited.flame" >"$dir/restored-name-check.flame" &&
        cmp -s "$STARTER" "$dir/restored-name-check.flame"; then
        record PASS project-edit "Reopened an edited native project. Only the selected flame changed."
    else
        record FAIL project-edit "Saving the edited project, reopening it, or verifying unchanged flame data failed."
    fi
}

kit_checkpoint() {
    local dir="$RUN/outputs" code=0 id=cpu-checkpoint
    local output="$dir/resumed.png"
    local -a args=(--flame "$STARTER" --index 1 --size 320 240 --samples 500000 --seed 123456
        --threads 2 --oversample 1 --memory-mib 192 --disk-mib 384 --format png
        --backend cpu --checkpoint "$dir/checkpoint" --output "$output")
    run_cmd checkpoint-pause 150 "$CLI" "${args[@]}" --pause-after 65536 || code=$?
    if [[ $code != 75 || -e $output ]] || ! grep -q '^Paused at a committed checkpoint:' "$RUN/logs/checkpoint-pause.txt"; then
        record FAIL "$id" "Pause must exit 75 at a committed checkpoint without publishing a PNG (exit=$code)."
        return
    fi
    local summary
    if run_cmd checkpoint-resume 150 "$CLI" "${args[@]}" --resume &&
        summary=$(kit_render_line "$RUN/logs/checkpoint-resume.txt" cpu) &&
        [[ " $summary " == *' resumed=1 '* ]] && cmp -s "$output" "$dir/starter-1.png"; then
        record PASS "$id" "Paused at a committed checkpoint, resumed, and matched the uninterrupted CPU PNG exactly."
    else
        record FAIL "$id" "Resume must confirm resumed=1 and exactly match the uninterrupted render."
    fi
}

kit_gpu() {
    local selector="${DEVICE:-}" pick rc=0
    if [[ $GPU_MODE == off ]]; then
        record SKIP physical-vulkan "GPU test disabled by request."
        return
    fi
    if ! run_cmd devices 35 "$CLI" --list-devices --json; then
        record FAIL physical-vulkan "Device discovery failed or timed out. Inspect logs/devices.txt."
        return
    fi
    # The helper only returns a usable, non-software device. No '--device' default
    # is used for rendering, so a physical card must be explicitly selected.
    if [[ -n $selector ]]; then
        pick=$(ps_json device "$RUN/logs/devices.txt" "$selector" 2>"$RUN/logs/device-selection.txt") || rc=$?
    else
        pick=$(ps_json device "$RUN/logs/devices.txt" 2>"$RUN/logs/device-selection.txt") || rc=$?
    fi
    pick=${pick//$'\r'/}
    case $rc in
        0) ;;
        3) record SKIP physical-vulkan "No usable physical Vulkan device was reported. CPU results are separate. See logs/devices.txt."; return ;;
        4) record FAIL physical-vulkan "The requested device is missing, unsupported, or software-only. See logs/devices.txt."; return ;;
        *) record FAIL physical-vulkan "Could not validate the device report. See logs/device-selection.txt."; return ;;
    esac
    if [[ -z $pick ]]; then
        record FAIL physical-vulkan "Device discovery returned an empty identity."
        return
    fi
    record INFO selected-gpu "Strict physical Vulkan test selected $pick. Compare its elapsed_ms with cpu-starter-1 (same flame, dimensions and seed)."
    kit_render_small physical-vulkan 1 vulkan "$RUN/outputs/vulkan-ember.png" --device "$pick" \
        --no-fallback --gpu-memory-mib 256 || :
}

kit_large_render() {
    if [[ $LARGE != 1 ]]; then
        record SKIP cpu-16k "Optional full-size render was not requested."
        return
    fi
    local id=cpu-16k out="$RUN/outputs/ember-16k.png" needed free_kib
    local -a args=(--flame "$STARTER" --index 1 --size 16384 16384 --samples 2000000
        --seed 123456 --threads 2 --oversample 1 --memory-mib 192 --disk-mib 2048
        --format png --backend cpu --output "$out")
    if ! run_cmd estimate-16k 45 "$CLI" "${args[@]}" --estimate; then
        record FAIL "$id" "16K resource estimation failed. See logs/estimate-16k.txt."
        return
    fi
    needed=$(sed -n 's/^Render plan: .* disk_bytes=\([0-9][0-9]*\) .*/\1/p' "$RUN/logs/estimate-16k.txt")
    free_kib=$(df -Pk "$RUN" 2>/dev/null | awk 'END {print $4}') || free_kib=
    if [[ ! $needed =~ ^[0-9]{1,12}$ || ! $free_kib =~ ^[0-9]{1,16}$ ]]; then
        record SKIP "$id" "Could not confirm available disk space for the 16K run."
        return
    fi
    if (( free_kib < (needed + 1023) / 1024 + 524288 )); then
        record SKIP "$id" "Insufficient free space for the estimated $needed bytes plus a 512 MiB reserve."
        return
    fi
    local summary
    if run_cmd render-16k 1200 "$CLI" "${args[@]}" &&
        summary=$(kit_render_line "$RUN/logs/render-16k.txt" cpu) &&
        check_png "$out" 16384 16384 >"$RUN/logs/16k-image.txt" 2>&1; then
        record PASS "$id" "Full 16,384-square PNG decoded and checked. $summary"
    else
        record FAIL "$id" "Full-size render, timeout, backend or image validation failed. See logs/render-16k.txt and logs/16k-image.txt."
    fi
}

run_cli_tests() {
    CLI_GATE=0
    if ! run_cmd build-info 25 "$CLI" --build-info --json ||
        ! ps_json build "$RUN/logs/build-info.txt" >"$RUN/logs/build-validation.txt" 2>&1; then
        record FAIL build-identity "Expected an Apophysis Reborn 0.13.0 Windows x64 release. See logs/build-info.txt and logs/build-validation.txt. Further tests are blocked."
        return 1
    fi
    CLI_GATE=1
    record PASS build-identity "Confirmed the 0.13.0 Windows x64 release identity. Build details are in logs/build-info.txt."
    if ! mkdir -p "$RUN/outputs"; then
        record FAIL output-directory "Could not create the test output folder."
        return 0
    fi
    if [[ ! -f $STARTER ]] || ! run_cmd starter-inventory 45 "$CLI" inspect "$STARTER" --json ||
        ! ps_json starter "$RUN/logs/starter-inventory.txt" >"$RUN/logs/starter-validation.txt" 2>&1; then
        record FAIL starter-library "The eight expected renderable starter flames could not be verified. Check that the complete application folder was extracted."
        record SKIP render-suite "The project, CPU and Vulkan checks use the verified starter collection."
        return 0
    fi
    record PASS starter-library "All eight named starter flames are present in release order and marked renderable."
    kit_project_roundtrip
    local index ok_reference=0
    for index in 1 2 3 4 5 6 7 8; do
        if kit_render_small "cpu-starter-$index" "$index" cpu "$RUN/outputs/starter-$index.png"; then
            if [[ $index == 1 ]]; then ok_reference=1; fi
        fi
    done
    if [[ $ok_reference == 1 ]]; then
        kit_checkpoint
    else
        record SKIP cpu-checkpoint "Requires a valid uninterrupted CPU reference render."
    fi
    kit_gpu
    kit_large_render
    return 0
}
