#!/usr/bin/env bash
# Produce a private local backtrace. Nothing is uploaded or sent anywhere.
set -euo pipefail
umask 077

if [[ ${1:-} == --help || $# -lt 2 || $# -gt 3 ]]; then
    printf 'Usage: bash %s EXECUTABLE CORE [REPORT]\n' "$0"
    printf 'Use the executable and debug symbols from the build that crashed.\n'
    printf 'For a systemd dump, first run: coredumpctl dump PID --output=core\n'
    printf 'The report stays on this computer; review it before sharing privately.\n'
    [[ ${1:-} == --help ]] && exit 0
    exit 2
fi

command -v gdb >/dev/null || { printf 'gdb is required.\n' >&2; exit 1; }
executable=$(realpath -- "$1")
core=$(realpath -- "$2")
report=${3:-crash-backtrace.txt}
[[ -f "$executable" && -f "$core" ]] || { printf 'Executable or core is missing.\n' >&2; exit 1; }
# Refuse to overwrite any user file, including a symlink.
set -o noclobber
gdb -nx -batch -iex 'set auto-load off' -iex 'set debuginfod enabled off' \
    -ex 'set pagination off' -ex 'info program' -ex 'info threads' \
    -ex 'thread apply all bt' -ex 'info sharedlibrary' \
    -- "$executable" "$core" > "$report" 2>&1
printf 'Saved local crash report: %s\n' "$report"
