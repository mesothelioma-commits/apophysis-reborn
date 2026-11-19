#!/usr/bin/env bash
# Windows Git Bash. Use the absolute script location so the caller can start anywhere.
set -uo pipefail
KIT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)" || exit 2
VERSION=0.13.0
APP_HINT='' OUTPUT_HINT='' DEVICE='' GUIDED=0 LARGE=0 OPEN_REPORT=1 GPU_MODE=auto
RUN='' APP_DIR='' CLI='' EDITOR='' STARTER='' CLI_GATE=0 PS=''
help_text() {
    cat <<'HELP'
Apophysis Reborn 0.13.0 | Windows x64 test kit
  bash run-tests.sh [--app FOLDER|EXE] [--guided] [--no-gpu] [--device ID|INDEX]
                    [--large-render] [--output FOLDER] [--no-open]
The default runs CPU, eligible physical GPU, and automated editor tests.
--guided also walks you through the seed/save, Mutate, and divider checks.
--large-render adds a CPU 16,384 x 16,384 export and a full PNG decode.
--output selects a parent for a new unique run folder. --no-open skips Explorer.
HELP
}
parse_args() {
    while (($#)); do
        case $1 in
            --app|--output|--device)
                if (($# < 2)) || [[ -z $2 || $2 == --* ]]; then
                    printf 'Missing value for %s\n' "$1" >&2; return 2
                fi
                case $1 in --app) APP_HINT=$2;; --output) OUTPUT_HINT=$2;; --device) DEVICE=$2;; esac
                shift 2 ;;
            --guided) GUIDED=1; shift ;;
            --large-render) LARGE=1; shift ;;
            --no-gpu) GPU_MODE=off; shift ;;
            --no-open) OPEN_REPORT=0; shift ;;
            --help|-h) help_text; return 10 ;;
            *) printf 'Unknown option: %s\n' "$1" >&2; help_text; return 2 ;;
        esac
    done
    if [[ $GPU_MODE == off && -n $DEVICE ]]; then printf '%s\n' 'Choose either --device or --no-gpu.' >&2; return 2; fi
    return 0
}
win_path() { cygpath -aw -- "$1"; }
shell_path() {
    local p=$1
    p=${p#\"}; p=${p%\"}; p=${p#\'}; p=${p%\'}
    cygpath -au -- "$p"
}
call_ps() {
    local script=$1
    shift
    MSYS2_ARG_CONV_EXCL='*' "$PS" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "$(win_path "$KIT_DIR/lib/$script")" "$@"
}
ps_json() {
    local mode=$1 file=$2
    shift 2
    call_ps check.ps1 "$mode" "$(win_path "$file")" "$@"
}
check_png() { ps_json png "$@"; }
note() { printf '%s\n' "$*" | tee -a "$RUN/run-log.txt"; }
record() {
    local status=$1 id=$2 message=$3
    message=${message//$'\r'/ }; message=${message//$'\n'/ }; message=${message//$'\t'/ }
    printf '%s\t%s\t%s\n' "$status" "$id" "$message" >> "$RUN/results.tsv"
    note "[$status] $id  $message"
}
run_cmd() {
    local id=$1 limit=$2 exe=$3 arg list err log rc=0 started=$SECONDS
    shift 3
    list=$RUN/control/$id.args; err=$RUN/control/$id-supervisor.txt; log=$RUN/logs/$id.txt
    : >"$list" || return 125
    for arg in "$@"; do
        if [[ $arg == /* ]]; then arg=$(win_path "$arg") || return 125; fi
        printf '%s\0' "$arg" >>"$list" || return 125
    done
    call_ps run-process.ps1 "$(win_path "$exe")" "$(win_path "$list")" "$(win_path "$log")" "$(win_path "$RUN")" "$limit" >"$err" 2>&1 || rc=$?
    if [[ -s $err ]]; then cat -- "$err" >>"$log"; fi
    printf '%s\t%s\t%s\n' "$id" "$rc" "$((SECONDS-started))" >>"$RUN/timings.tsv"
    return "$rc"
}
choose_app() {
    local p=$1 leaf
    [[ -n $p ]] || return 1
    p=$(shell_path "$p") || return 1
    if [[ -f $p ]]; then
        leaf=${p##*/}
        case ${leaf,,} in apophysis-reborn.exe|flame-cpu.exe) p=${p%/*};; *) return 1;; esac
    fi
    [[ -f "$p/apophysis-reborn.exe" && -f "$p/flame-cpu.exe" ]] || return 1
    APP_DIR=$(cd -- "$p" && pwd -P) || return 1
}
find_app() {
    local p downloads profile localdata
    if [[ -n $APP_HINT ]]; then choose_app "$APP_HINT"; return; fi
    localdata=$(shell_path "$LOCALAPPDATA") || return 1
    profile=$(shell_path "$USERPROFILE") || return 1
    downloads=$profile/Downloads
    for p in "$localdata/Programs/Apophysis Reborn/$VERSION" "$KIT_DIR" "$KIT_DIR/.." \
        "$KIT_DIR"/../apophysis-reborn-0.13.0-windows-x64{,-portable/apophysis-reborn-0.13.0-windows-x64} \
        "$downloads"/apophysis-reborn-0.13.0-windows-x64{,-portable/apophysis-reborn-0.13.0-windows-x64}; do
        if choose_app "$p"; then return 0; fi
    done
    if [[ -t 0 ]]; then
        printf 'Paste the application folder or full path to apophysis-reborn.exe, then press Enter:\n'
        IFS= read -r p || return 1
        choose_app "$p"; return
    fi
    return 1
}
finalize() {
    local run_exit=$1 count status file bundle zip_ok=0
    trap - EXIT INT TERM
    [[ -n $RUN && -d $RUN ]] || exit "$run_exit"
    if [[ $run_exit == 130 || $run_exit == 143 ]]; then record WARN interrupted 'Run interrupted before all checks completed.'; fi
    if [[ -s "$RUN/results.tsv" ]] && awk -F '\t' '$1=="FAIL"{found=1} END{exit !found}' "$RUN/results.tsv"; then
        if [[ $run_exit == 0 ]]; then run_exit=1; fi
    fi
    {
        printf 'Apophysis Reborn %s - Windows test report\n' "$VERSION"
        printf 'Completed: %s\nApplication: %s\n' "$(date -Iseconds)" "${APP_DIR:-unresolved}"
        printf 'Options: guided=%s  physical_gpu=%s  device=%s  optional_16k=%s\n' "$GUIDED" "$GPU_MODE" "${DEVICE:-first usable physical}" "$LARGE"
        if [[ $run_exit == 0 ]]; then printf 'Outcome: no failed checks among those executed. Review SKIP entries for untested areas.\n'
        else printf 'Outcome: failed, blocked or interrupted (exit %d). See individual results.\n' "$run_exit"; fi
        for status in PASS FAIL SKIP INFO WARN; do
            count=$(awk -F '\t' -v s="$status" '$1==s{n++} END{print n+0}' "$RUN/results.tsv"); printf '%s %s   ' "$status" "$count"
        done
        printf '\n\nIndividual results\n'; cat "$RUN/results.tsv"
        printf '\nPer-command exit code and elapsed wall seconds (includes launcher overhead)\n'; cat "$RUN/timings.tsv"
        printf '\nThe individual command logs accompany this report. Guided checks record the tester\x27s observations.\n'
    } >"$RUN/report.txt"
    bundle="$RUN/Apophysis-results-${RUN##*/}.zip"
    if [[ -n $PS ]] && call_ps pack-report.ps1 "$(win_path "$RUN")" "$(win_path "$bundle")" > "$RUN/control/archive.txt" 2>&1; then
        file=$bundle; zip_ok=1
    else
        file=$RUN/report.txt
        printf '\nResult ZIP could not be created. Send report.txt and the logs folder instead.\n' >> "$file"
        if [[ $run_exit == 0 ]]; then run_exit=1; fi
    fi
    printf '\nReport: %s\n' "$(win_path "$RUN/report.txt")"
    if [[ $zip_ok == 1 ]]; then printf 'Share this result ZIP: %s\n' "$(win_path "$bundle")"; fi
    if [[ $OPEN_REPORT == 1 ]] && command -v explorer.exe >/dev/null; then
        MSYS2_ARG_CONV_EXCL='*' explorer.exe "/select,$(win_path "$file")" >/dev/null 2>&1 &
    fi
    exit "$run_exit"
}
main() {
    local rc=0 out profile stamp sha_cli sha_editor editor_rev cli_rev
    parse_args "$@" || rc=$?
    [[ $rc != 10 ]] || return 0
    [[ $rc == 0 ]] || return "$rc"
    case "$(uname -s)" in MINGW*|MSYS*) ;; *) printf 'Run this kit in Git Bash on Windows 11.\n' >&2; return 2;; esac
    PS=$(command -v powershell.exe) || { printf 'Windows PowerShell is required.\n' >&2; return 2; }
    [[ -n ${USERPROFILE:-} && -n ${LOCALAPPDATA:-} ]] || { printf 'Windows user folders were unavailable.\n' >&2; return 2; }
    profile=$(shell_path "$USERPROFILE") || return 2
    if [[ -n $OUTPUT_HINT ]]; then out=$(shell_path "$OUTPUT_HINT") || return 2
    else out=$profile/Projects/Apophysis-Reborn-Tests/results; fi
    mkdir -p -- "$out" || return 2
    stamp=$(date +%Y%m%d-%H%M%S)
    RUN=$(mktemp -d "$out/$stamp-XXXXXXXX") || return 2
    mkdir -p -- "$RUN"/{logs,control} || return 2
    : > "$RUN/run-log.txt"; : > "$RUN/results.tsv"
    printf 'command\texit_code\twall_seconds\n' > "$RUN/timings.tsv"
    trap 'finalize $?' EXIT
    trap 'exit 130' INT
    trap 'exit 143' TERM
    note "Apophysis Reborn $VERSION | Windows tester | $(date -Iseconds)"
    note "Working folder: $(win_path "$RUN")"
    if ! find_app; then
        record FAIL application-location 'Could not find both Windows executables. Extract the full portable ZIP or point --app at the installed folder.'
        return 2
    fi
    CLI="$APP_DIR/flame-cpu.exe"; EDITOR="$APP_DIR/apophysis-reborn.exe"; STARTER="$APP_DIR/examples/starter-library.flame"
    note "Testing: $(win_path "$APP_DIR")"
    sha_cli=$(sha256sum < "$CLI") || return 2; sha_cli=${sha_cli%% *}
    sha_editor=$(sha256sum < "$EDITOR") || return 2; sha_editor=${sha_editor%% *}
    if [[ $sha_cli == b9f668c16ba3dd692efc3ca88a3b4506f84d3fea34a91bcac7b315d5cf5f961e && $sha_editor == a192a37f7e0f129a195b418e019d5bcc9999e3363b69a52d9b5efeee8d5ac1c6 ]]; then
        record PASS package-fingerprint 'Both executable hashes match the distributed 0.13.0 recovery build.'
    else
        record INFO package-fingerprint "Different binary fingerprints. Checking reported 0.13.0 identities. CLI=$sha_cli editor=$sha_editor"
    fi
    run_cli_tests || :
    if [[ $CLI_GATE != 1 ]]; then
        record SKIP desktop 'The renderer build could not be verified.'
        return 1
    fi
    if ! run_cmd editor-build-info 25 "$EDITOR" --build-info --json ||
       ! ps_json build "$RUN/logs/editor-build-info.txt" >"$RUN/logs/editor-build-validation.txt" 2>&1; then
        record FAIL editor-identity 'The editor could not report a compatible 0.13.0 Windows x64 identity.'
        return 1
    fi
    cli_rev=$(tr -d '\r\n' <"$RUN/logs/build-validation.txt")
    editor_rev=$(tr -d '\r\n' <"$RUN/logs/editor-build-validation.txt")
    if [[ $cli_rev == unknown || $editor_rev == unknown ]]; then
        record WARN editor-identity 'Both binaries report compatible 0.13.0 builds, but one or both source revisions are unavailable.'
    elif [[ $cli_rev != "$editor_rev" ]]; then
        record FAIL editor-identity "Editor and CLI report different source revisions; use a matching package."
        return 1
    else
        record PASS editor-identity "Editor and CLI report matching 0.13.0 identities. Source: $editor_rev."
    fi
    note 'Automated desktop windows will open and close. Leave those windows alone until the checks finish.'
    run_editor_tests
    run_guided_tests
    return 0
}
source "$KIT_DIR/lib/cli-tests.sh" || exit 2
source "$KIT_DIR/lib/editor-tests.sh" || exit 2
if [[ ${BASH_SOURCE[0]} == "$0" ]]; then main "$@"; exit $?; fi
