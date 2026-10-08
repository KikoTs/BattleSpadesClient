#!/bin/sh
# Run on x86_64 Haiku. Installs libraries into a private, relocatable build prefix.
set -eu
[ "$(uname -s)" = Haiku ] || { echo 'Run this script inside Haiku.' >&2; exit 1; }
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
prefix=${1:-"$root/out/haiku-deps"}
jobs=${CMAKE_BUILD_PARALLEL_LEVEL:-2}

pkgman install -y cmd:git cmd:cmake cmd:ninja cmd:pkg_config \
    libsdl3_devel freetype_devel harfbuzz_devel curl_devel \
    nlohmann_json libsodium_devel openal_devel zlib_devel enet_devel

mkdir -p "$prefix/src"
fetch() {
    name=$1 url=$2 revision=$3
    dir="$prefix/src/$name"
    if [ ! -d "$dir/.git" ]; then
        git init "$dir"
        git -C "$dir" remote add origin "$url"
    fi
    if [ ! -f "$dir/.battlespades-revision" ]; then
        git -C "$dir" fetch --depth 1 origin "$revision"
        git -C "$dir" checkout --detach FETCH_HEAD
        [ "$(git -C "$dir" rev-parse HEAD)" = "$revision" ]
        printf '%s\n' "$revision" > "$dir/.battlespades-revision"
    fi
    [ "$(cat "$dir/.battlespades-revision")" = "$revision" ] || {
        echo "Dependency revision changed; use a fresh prefix: $prefix" >&2; exit 1;
    }
    [ "$(git -C "$dir" rev-parse HEAD)" = "$revision" ] || {
        echo "Dependency checkout changed: $dir" >&2; exit 1;
    }
}

# Dmitry's native BGLView port, not an unpinned moving branch.
fetch bgfx https://github.com/DmitrySenpai/bgfx-haiku.git a81aefa585af7015e54ee3752c7b5268850d78bc
patch="$root/packaging/haiku/patches/bgfx-clean-platform.patch"
if git -C "$prefix/src/bgfx" apply --check "$patch"; then
    git -C "$prefix/src/bgfx" apply "$patch"
else
    git -C "$prefix/src/bgfx" apply --reverse --check "$patch"
fi
cmake -S "$prefix/src/bgfx" -B "$prefix/build/bgfx" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DBGFX_BUILD_TOOLS=OFF -DBGFX_BUILD_EXAMPLES=OFF -DBGFX_BUILD_TESTS=OFF \
    -DBGFX_CONFIG_MULTITHREADED=OFF -DBGFX_INSTALL=ON
cmake --build "$prefix/build/bgfx" --parallel "$jobs"
cmake --install "$prefix/build/bgfx"

fetch stb https://github.com/nothings/stb.git 2c980bb59875b0d32144a71867fbdebb2f77cd20
mkdir -p "$prefix/include/stb"
cp "$prefix/src/stb/stb_image.h" "$prefix/src/stb/stb_image_write.h" \
    "$prefix/src/stb/stb_vorbis.c" "$prefix/include/stb/"
printf '\nDependencies ready in %s\n' "$prefix"
