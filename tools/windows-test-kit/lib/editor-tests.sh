# Desktop checks for the 0.13.0 runtime. Sourced by the main Git Bash runner.
# Smoke modes open the embedded example in a fresh window, skip the persistent
# render queue and plugin approvals, and never accept a user document.
editor_smoke_command() (
    local kind=$1 seconds=$2 output=$3 temp="$RUN/editor/temporary"
    local native_temp
    local -a platform=()
    mkdir -p "$temp" || return 1
    native_temp=$temp
    case "$(uname -s)" in
        MINGW*|MSYS*|CYGWIN*)
            native_temp=$(cygpath -aw "$temp") || return 1
            platform=(-platform windows)
            ;;
    esac
    export TMPDIR="$native_temp" TMP="$native_temp" TEMP="$native_temp"
    run_cmd "editor-$kind" "$seconds" "$EDITOR" "${platform[@]}" "--$kind-smoke-test" "$output" --backend cpu
)

editor_evidence() {
    local directory=$1 name
    shift
    for name in "$@"; do
        if [[ ! -s "$directory/$name" ]]; then
            printf 'Missing editor evidence: %s\n' "$name"
            return 1
        fi
    done
}

editor_png16() {
    local image=$1 width=$2 height=$3 header
    # PNG IHDR stores bit depth, then color type, at byte offsets 24 and 25.
    header=$(od -An -tu1 -j24 -N2 -- "$image") || return 1
    if ! [[ $header =~ ^[[:space:]]*16[[:space:]]+6[[:space:]]*$ ]]; then
        printf 'Expected a 16-bit RGBA PNG: %s\n' "$image"
        return 1
    fi
    check_png "$image" "$width" "$height"
}

verify_editor_outputs() {
    local kind=$1 output=$2 frame
    editor_evidence "$output" editor.png preview.png saved-example.flame || return 1
    case "$kind" in
        support)
            editor_evidence "$output" support-report.json user-manual.png system-information.png \
                system-information.json system-information.txt || return 1
            grep -Eq '"diagnostics_roundtrip_verified"[[:space:]]*:[[:space:]]*true' "$output/support-report.json" &&
                grep -Eq '"embedded_manual_verified"[[:space:]]*:[[:space:]]*true' "$output/support-report.json"
            ;;
        creative)
            editor_evidence "$output" gradient-editor.png gradient-example.flame palette.gpl \
                adjustment-editor.png adjustment-example.flame mutation-explorer.png mutation-example.flame \
                random-explorer.png random-batch.flame
            ;;
        queue)
            editor_evidence "$output" queue-report.json render-queue.png render-queue.json \
                queue-render-16.png queue-render-16.tif high-resolution-settings.png || return 1
            grep -Eq '"ui_heartbeats"[[:space:]]*:[[:space:]]*[1-9][0-9]*' "$output/queue-report.json" || return 1
            editor_png16 "$output/queue-render-16.png" 192 144
            ;;
        animation)
            editor_evidence "$output" timeline.png timeline-frame.png color-curves.png \
                animation.apoproject animation-report.json animation-queue.png || return 1
            for frame in 000000 000006 000012 000018 000024; do
                editor_png16 "$output/frames/frame_$frame.png" 320 240 || return 1
            done
            ;;
    esac
}

run_editor_tests() {
    local kind output message seconds rc
    for kind in support creative queue animation; do
        output="$RUN/editor/$kind"
        seconds=150
        case "$kind" in
            support) seconds=50; message='Main preview, example save, offline manual and diagnostic report round-trip' ;;
            creative) message='Gradient, adjustment, mutation and random dialogs produced images and saved example files' ;;
            queue) message='Two queued 16-bit outputs completed with UI heartbeats; 192 x 144 RGBA PNG decoded' ;;
            animation) message='Timeline undo/redo, project save/reload, and five numbered 320 x 240 RGBA16 frames' ;;
        esac
        printf 'Testing editor: %s. Please let the test window close itself.\n' "$kind"
        if editor_smoke_command "$kind" "$seconds" "$output"; then
            if verify_editor_outputs "$kind" "$output" >>"$RUN/logs/editor-$kind.txt" 2>&1; then
                record PASS "editor-$kind" "$message."
            else
                record FAIL "editor-$kind" "Editor exited normally, but its expected evidence was missing or invalid. See logs/editor-$kind.txt."
            fi
        else
            rc=$?
            record FAIL "editor-$kind" "Editor check exited with code $rc. See logs/editor-$kind.txt and editor/$kind."
        fi
    done
}

# Return nonzero on an explicit failure, skip, or missing input. The guided
# workflow decides which later observations still make sense.
guided_observation() {
    local id=$1 question=$2 success=$3 answer note
    while true; do
        printf '\n%s\n[P]ass / [F]ail / [S]kip: ' "$question"
        if ! IFS= read -r answer; then
            record SKIP "$id" 'Manual observation was not supplied.'
            return 2
        fi
        case "${answer,,}" in
            p|pass|y|yes) record PASS "$id" "Human observation: $success"; return 0 ;;
            f|fail|n|no)
                printf 'Optional detail (last action, wait time, renderer; Enter to omit): '
                IFS= read -r note || note=
                note=${note//$'\r'/ }
                note=${note//$'\t'/ }
                record FAIL "$id" "Human observation: ${note:-tester reported a problem}."
                return 1
                ;;
            s|skip) record SKIP "$id" 'Tester skipped this observation.'; return 2 ;;
            *) printf 'Enter P, F or S.\n' ;;
        esac
    done
}

run_guided_tests() {
    local folder="$RUN/guided" display
    local -a pending=(manual-starters manual-dividers manual-seed-save manual-mutate)
    local id
    if [[ ${GUIDED:-0} != 1 ]] || [[ ! -t 0 ]]; then
        for id in "${pending[@]}"; do
            record SKIP "$id" 'Requires --guided in an interactive Git Bash terminal.'
        done
        return 0
    fi
    if ! mkdir -p "$folder" || ! run_cmd guided-project 45 "$CLI" project create "$STARTER" \
        --index 1 --output "$folder/Friend-test.apoproject" || [[ ! -s "$folder/Friend-test.apoproject" ]]; then
        record FAIL manual-setup 'Could not create the disposable native starter project in guided/.'
        for id in "${pending[@]}"; do record SKIP "$id" 'Working copy was unavailable.'; done
        return 0
    fi
    display=$folder
    if command -v cygpath >/dev/null 2>&1; then display=$(cygpath -aw "$folder") || display=$folder; fi
    printf '\nGuided checks in your normal 0.13.0 editor\n'
    printf 'Working folder: %s\n' "$display"
    cat <<'GUIDED_PREP'
Save any open work first. Launch version 0.13.0 without opening a file.
In an existing editor, you can choose File > Open starter library instead.
Click three different starter thumbnails. Note whether the preview follows
each selection. Switch back to this terminal to record your observation.
GUIDED_PREP
    guided_observation manual-starters \
        'Are all eight starter flames listed, and do the preview images change when selected?' \
        'Eight starters were available and the selected previews updated.' || :
    cat <<'GUIDED_DIVIDERS'
Note your usual seed under Edit > Preferences, then click Cancel. Now open
Friend-test.apoproject from the working folder above with Ctrl+O. Confirm its
name in the title bar before editing. Use only this disposable project.
Drag the vertical divider on each side of Live preview. Check that the
neighboring panels change width and the flame keeps its proportions in Fit.
Return the dividers to your preferred position when finished.
GUIDED_DIVIDERS
    guided_observation manual-dividers 'Did both dividers work and did the editor remain responsive?' \
        'Both live-preview dividers moved and the editor remained responsive.' || :
    cat <<'GUIDED_SEED'
With Friend-test.apoproject open, visit Edit > Preferences and note your
current Random seed so you can restore it later. Enter 123457 (or 123458
if the old value was already 123457), select OK, then press Ctrl+S. Use
Ctrl+O to reopen Friend-test.apoproject. Confirm the new value in Preferences.
Close Preferences and click a different flame. Note the actual renderer
shown below the preview. If the editor hangs, record F here in Git Bash.
GUIDED_SEED
    guided_observation manual-seed-save 'Did saving and reopening keep the new seed, and can you select a flame?' \
        'Seed persisted after save/reopen and the editor accepted input.' || :
    cat <<'GUIDED_MUTATE'
With Live preview running, open Mutate and click Cancel immediately. Try it
again, this time clicking Cancel while candidate thumbnails are being drawn.
After each close, select another flame and wait for a fresh preview. If an
earlier freeze prevents testing, choose S for this check.
GUIDED_MUTATE
    guided_observation manual-mutate 'Did both Cancel operations return control and allow a new preview?' \
        'Mutation explorer closed promptly both times and the main preview updated.' || :
    printf 'Restore the original seed under Edit > Preferences. Keep or close your test copy.\n'
    return 0
}
