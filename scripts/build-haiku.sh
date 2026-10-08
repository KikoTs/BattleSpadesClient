#!/bin/sh
set -eu
[ "$(uname -s)" = Haiku ] || { echo 'Run this script inside Haiku.' >&2; exit 1; }
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"
prefix=${1:-"$root/out/haiku-deps"}
jobs=${CMAKE_BUILD_PARALLEL_LEVEL:-2}
cmake --preset native-haiku-release -DCMAKE_PREFIX_PATH="$prefix"
cmake --build --preset native-haiku-release --parallel "$jobs"
ctest --preset native-haiku-release
cmake --install out/build/native-haiku-release --component Runtime --prefix "$root/out/haiku-package"
mkdir -p out/haiku-package/bin/licenses
for library in bgfx bx bimg; do
    cp "$prefix/src/bgfx/$library/LICENSE" "out/haiku-package/bin/licenses/LICENSE-$library"
done
cp "$prefix/src/stb/LICENSE" out/haiku-package/bin/licenses/LICENSE-stb
cp docs/HAIKU.md out/haiku-package/bin/HAIKU.md
