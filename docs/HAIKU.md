# Native Haiku x86_64

This port is based on current client code, with the platform work from
[DmitrySenpai's PR #4](https://github.com/KikoTs/BattleSpadesClient/pull/4)
reapplied selectively. It does not carry that snapshot's gameplay/UI rollbacks,
release-version downgrade, removed tests, source backup files, or Windows
installer changes. Credits to DmitrySenpai for the original Haiku experiments
and the native bgfx backend.

Haiku is an independent OS with its own kernel and BeOS-derived native APIs.
This build uses SDL3's Haiku backend, BGLView/OpenGL, OpenAL and Haiku's native
socket library. It needs neither Wine nor an X server. Steam's proprietary
runtime has no Haiku build, so the port uses direct ENet connections and keeps
Steam networking disabled.

## Build on Haiku

Use 64-bit Haiku R1/beta6 or a compatible newer nightly and its GCC toolchain.
The GCC2/32-bit BeOS compatibility environment is not a supported target.

```sh
git clone https://github.com/KikoTs/BattleSpadesClient.git
cd BattleSpadesClient
# Until merged, select the branch containing this port:
git switch codex/haiku-native
sh scripts/haiku-dependencies.sh
sh scripts/build-haiku.sh
```

The dependency script installs development packages with `pkgman`, fetches
fixed revisions of bgfx and stb, then builds into `out/haiku-deps`. It never
replaces system libraries. Set `CMAKE_BUILD_PARALLEL_LEVEL=2` for a small VM;
use 4 with sufficient RAM. Both scripts accept a private dependency prefix as
their first argument. Use a fresh prefix when changing dependency revisions.

The build uses C++20 and keeps warnings as errors. It produces
`out/haiku-package/bin/BattleSpadesClient`, the asset installer, project-owned
UI resources, shaders, localization and licenses. No retail assets are included.
Import your owned Ace of Spades assets with the bundled asset installer.

For a package built elsewhere, install runtime dependencies first:

```sh
pkgman install -y libsdl3 freetype harfbuzz curl libsodium openal zlib enet
```

Settings and connection diagnostics use the native user settings directory,
normally `/boot/home/config/settings/BattleSpades`. When the install directory
is read-only, the asset installer uses the native user non-packaged data
directory, normally `/boot/home/config/non-packaged/data/BattleSpades`.

## Dependency patches

`scripts/haiku-dependencies.sh` pins
[bgfx-haiku](https://github.com/DmitrySenpai/bgfx-haiku) at
`a81aefa585af7015e54ee3752c7b5268850d78bc` (bgfx API 129). Its BGLView context,
Haiku platform detection, embedded GLSL and file-I/O portability work are
retained. `packaging/haiku/patches/bgfx-clean-platform.patch` removes two
game-specific per-draw sampler overrides and links bx against Haiku's libroot
instead of assuming Linux's libdl/librt. Samplers use bgfx's normal shader
metadata, including the current client shaders.

SDL creates and owns BGLView. The client releases SDL's initial current-context
lock before passing the borrowed view to bgfx. Rendering stays on the window
thread, and bgfx shuts down before SDL destroys the view. Dummy-driver event
tests do not request OpenGL. RmlUi 6.3 is built statically; the scoped CMake
patch disables runtime-dependency scanning that CMake cannot perform on Haiku.

## Tests and CI

The `Haiku native build` workflow runs on an Ubuntu worker using a pinned
`vmactions/haiku-vm` action and an actual Haiku x86_64 guest. It can be started
manually and also runs for pull requests touching the port or client code.
Build and CTest logs are uploaded, together with a package after success.

`ctest --preset native-haiku-release` runs the asset-independent platform,
protocol, ENet loopback and localization tests. The Haiku graphics test creates
a real SDL/BGLView window, checks rendered pixels, resizes it, and repeats
startup/shutdown. It requires the guest's app_server/OpenGL stack; do not set
`SDL_VIDEODRIVER=dummy` for that test. The world vertex-layout test also uses
a native window on Haiku and measures the shipped GLSL shader's face shading
and corner occlusion through GPU readback. The wider test suite includes tests that
need an owned retail asset tree and separate live servers.

Validated on 2026-10-08 with Haiku R1/beta6 `hrev59866+79`, GCC 13.3,
and Mesa llvmpipe in a QEMU VM: the complete Release build and all 16 tests in
the Haiku preset passed. The installed client ran `--version` and a bounded
headless launch from outside its install directory; the asset installer ran
`--help`. Fifteen affected-area Windows regression tests also passed, including
the Direct3D world-shader readback.

A separate live test connected two clients from Haiku to a private Windows
BattleSpades server, joined teams/classes, loaded Mayan Jungle and verified
authoritative weapon feedback. Over 15 seconds, the measured client received
869 world updates and stayed connected until the test deliberately closed it.
The reported graphical team-selection kick was not reproduced by this network
test; it remains a diagnostic question, not a confirmed encoding bug.

## Investigating a disconnect

A bare server `ENET DISCONNECT` line does not identify whether the client left,
the connection timed out, or the server rejected a packet. Do not change byte
order or packet packing based on that line alone. Current protocol code encodes
fields explicitly and runs ENet service on a separate worker from rendering.

After a failed graphical join, inspect
`~/config/settings/BattleSpades/connection-diagnostics.log` and capture the
matching server log. The client records received/sent counts, queued packets
and a reason code. Reason 13 is invalid data, 11 is timeout, and 0 alone does
not distinguish a graceful close from all transport failures. Preserve the
messages immediately before the server's disconnect, too.

To test the real network path without rendering, against your own test server:

```sh
out/build/native-haiku-release/src/aos_protocol168_live_session_smoke HOST PORT 15
```

This joins two clients, chooses a team/class, receives world updates and checks
weapon feedback. Use a private test server because it spawns players and fires
a test shot. Compare this result with the graphical client on the same server.
A passing wire test does not establish smooth rendering or working IME: test
those separately in the VM and on real hardware.

## Remaining platform limits

OpenGL is the supported renderer. VM performance and Haiku's available graphics
drivers can differ substantially from other platforms; a successful build or
pixel test is not a frame-rate guarantee. Steam relay joins, Steam overlay and
Steam presence are unavailable. A native `.hpkg`, Tracker metadata/icons, and
physical-hardware/IME coverage remain separate follow-up work.
