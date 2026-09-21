#!/usr/bin/env bash
set -euo pipefail

profile="dev"
skip_tests=0

while (($#)); do
    case "$1" in
        dev|release)
            profile="$1"
            ;;
        --skip-tests)
            skip_tests=1
            ;;
        *)
            echo "usage: $0 [dev|release] [--skip-tests]" >&2
            exit 2
            ;;
    esac
    shift
done

if [[ -z "${VCPKG_ROOT:-}" ]]; then
    echo "VCPKG_ROOT must point to a vcpkg checkout." >&2
    exit 2
fi

case "$(uname -s)" in
    Linux)
        platform="linux"
        ;;
    Darwin)
        platform="macos"
        ;;
    *)
        echo "build.sh supports Linux and macOS; use scripts/build.ps1 on Windows." >&2
        exit 2
        ;;
esac

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"
preset="native-${platform}-${profile}"
configuration="RelWithDebInfo"
if [[ "$profile" == "release" ]]; then
    configuration="Release"
fi

cmake --fresh --preset "$preset" -S "$root"
cmake --build --preset "$preset" --parallel
if ((skip_tests == 0)); then
    ctest --preset "$preset"
fi

if [[ "$profile" == "release" ]]; then
    cmake --install "$root/out/build/$preset" \
        --config "$configuration" \
        --prefix "$root/out/install/$preset"
fi
