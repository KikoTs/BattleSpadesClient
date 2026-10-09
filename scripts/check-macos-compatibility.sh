#!/usr/bin/env bash
# Inspect every Mach-O, including bundled server extensions and helper tools.
set -euo pipefail
root="${1:?usage: check-macos-compatibility.sh APP [minimum]}"
minimum="${2:-11.0}"
checked=0
while IFS= read -r -d '' binary; do
    file -b "$binary" | grep -q 'Mach-O' || continue
    checked=$((checked + 1))
    commands="$(otool -l "$binary")"
    versions="$(awk '$1 == "cmd" { legacy = ($2 == "LC_VERSION_MIN_MACOSX") }
        $1 == "minos" || (legacy && $1 == "version") { print $2 }' <<< "$commands")"
    if [[ -z "$versions" ]]; then
        echo "::error::Cannot determine deployment target: $binary"; exit 1
    fi
    while read -r version; do
        if ! awk -v found="$version" -v allowed="$minimum" 'BEGIN {
            split(found,f,"."); split(allowed,a,".");
            for(i=1;i<=3;i++) { if(f[i]+0 > a[i]+0) exit 1; if(f[i]+0 < a[i]+0) exit 0 }
        }'; then
            echo "::error::$binary requires macOS $version (package target is $minimum)"; exit 1
        fi
    done <<< "$versions"
    if otool -L "$binary" | tail -n +2 | grep -E '^[[:space:]]+/(opt/|usr/local/|Users/|Applications/)'; then
        echo "::error::Unbundled build-machine dependency: $binary"; exit 1
    fi
done < <(find "$root" -type f -print0)
if [[ "$checked" == 0 ]]; then echo "::error::No Mach-O binaries in $root"; exit 1; fi
echo "Checked $checked Mach-O files against macOS $minimum. An older-OS runtime test is still required."
