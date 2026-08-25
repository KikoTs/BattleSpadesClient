# BattleSpadesClient

BattleSpadesClient is a clean C++20 reimplementation of the Ace of Spades:
Battle Builder client. It is a custom engine: SDL3, bgfx, OpenAL Soft,
FreeType, HarfBuzz, and eventually ENet are low-level adapters; gameplay,
voxel rendering, physics, prediction, presentation, and tooling remain owned
by this repository.

The existing Python 3 BattleSpades server remains authoritative. The native
client will preserve Protocol 168 and coexist with the retail client while it
is reconstructed.

## What runs today

The native build is now a real graphical executable. It currently provides:

- an SDL3 window and event loop with a deterministic fixed-step runtime;
- bgfx rendering of the reconstructed retail Select Menu, with a restart-safe
  Graphics API setting for Direct3D 11/12, Vulkan, OpenGL, and Metal where the
  running platform build reports them;
- the original PNG artwork with the recovered cover/contain, slicing,
  filtering, anchoring, and hit-test rules;
- FreeType/HarfBuzz text shaping and rasterization from the original fonts;
- optional OpenAL Soft menu music and confirmation cues;
- the recovered Main, Graphics, and Controls Settings screen, including
  transactional TOML persistence, raw control rebinding, live previews, and a
  15-second display-mode rollback;
- native Join Match, Server Browser, direct IP/port, Public/Quick Play, and
  Custom Match screens: the browser asynchronously consumes the live AoSPlay
  HTTPS list and BattleSpades `HELLOLAN` UDP replies, has generation-checked
  refreshes, retail source/region/full/empty filters, sorting, scrolling,
  row/button hover and pressed states, runtime favourites/history, authored
  map previews, resolved mode descriptions, platform double-click activation,
  and typed Protocol 168 connection requests;
  Quick Play contains all 11 recovered playlists; Custom Match has bounded
  lobby discovery and typed join/host boundaries;
- a native player-identity gate before the Select Menu with AoSPlay sign-in,
  registration, recovery-code acknowledgement, signed guest identity and an
  offline guest fallback. Session secrets use the launcher's version-one
  state format and Windows DPAPI protection; authenticated, ticket-capable
  servers receive a fresh one-use game ticket instead of the visible nickname.
  Logout is available below the player name at the same height as Quit and
  clears the active identity before returning to the gate;
- a Create Match lobby with the ten recovered retail modes, compatible map
  selection, the complete rule catalog, player/settings panels, bot count,
  bot difficulty, preferred port, and animated nested mode/map/rule
  navigation. Start writes a disposable private configuration, launches the
  packaged BattleSpades server without a console, waits for its real HELLOLAN
  reply, and joins it through the normal Protocol 168 loader;
- native UGC Select, Map Creator lobby browser, six-setting Map Creator host
  lobby, Publish Map, Leaderboard, and Player Profile screens; Leaderboard and
  Player Profile use bounded asynchronous HTTPS requests against AoSPlay's
  recovered retail score contract, including real filters, sorting, profile
  drill-down and durable account IDs. Map Creator enters Loading only after
  the host presses Start; absent repository adapters settle into explicit
  offline, unavailable, empty, or not-found states instead of waiting forever
  or displaying invented data;
- the recovered horizontal route animation: child screens slide in from the
  right, Back slides in from the left, outgoing layers remain visible, and
  incoming controls become interactive at retail's halfway threshold without
  snapping when the outgoing layer is released;
- the authored 64x64 retail cursor installed as a native operating-system
  cursor with the recovered hotspot;
- an F12 retail-UI parity browser generated from the source inventory, with
  screen/widget filters, source/control/asset metadata, native-fixture routing,
  and hitbox overlays;
- an F10 gameplay parity lab backed by the real tool state machines: browse all
  18 retail classes and all 65 Protocol 168 tools, inspect team-colored KV6
  class models and class-specific first-person arms, fire/reload/restock, test
  the exact magazine/reserve and block wallets, and spawn either one mannequin
  or the complete class gallery; the shipped default and Mafia UI skins use
  retail's override-then-fallback lookup;
- a playable offline Training.vxl Tutorial with fixed-step movement and
  collision, canonical voxel damage/collapse, falling structures and debris,
  positional impact/weapon audio, the recovered lesson flow, and an F4
  all-weapons sandbox with real ammunition, recoil, zoom, custom actions, and
  visible locally simulated projectiles;
- a persistent Protocol 168 match connection: ENet/range-coder transport,
  offline zero-length Steam-ticket compatibility, ordered loader handshake,
  bounded LZF packet decoding, full zlib MapSync reconstruction, StateData,
  CreatePlayer roster state, NewPlayerConnection, continuous ClientData and
  WorldUpdate transforms, post-MapSync terrain catch-up, and START handoff to
  the playable renderer;
- separate boot-preload and match-loading presentations, with bounded worker
  texture decoding, deterministic render-thread GPU uploads, and one-screen-
  per-frame font/glyph warmup before the Select Menu appears;
- mouse and keyboard/controller-style focus input;
- executable-adjacent resources, developer fallbacks, and a portable ZIP
  layout;
- headless lifecycle, UI, frontend, resource-discovery, presentation, and text
  tests.

This is not yet a complete multiplayer replacement client. The offline
Tutorial is playable, and Server Browser/Connect to IP now join a real
BattleSpades match, retain the connection, mesh the server map, enter on START,
send movement/look/tool state and render continuously replicated player
transforms. Authoritative shooting, damage/death/UI, objectives, entities,
chat and the remaining match packet families are still being ported.

## Windows native build

Requirements:

- CMake 3.25 or newer;
- Visual Studio 2022 or newer with the C++ x64 tools;
- Ninja;
- Git and a local vcpkg checkout;
- `VCPKG_ROOT` set to that checkout.

From a PowerShell prompt at the repository root:

```powershell
$env:VCPKG_ROOT = "C:\src\vcpkg"
.\scripts\build.ps1 -Profile Dev -Native
```

The wrapper configures, builds, and runs all native tests. Launch the result
with:

```powershell
& .\out\build\native-dev\src\RelWithDebInfo\BattleSpadesClient.exe
```

## Original asset import

BattleSpadesClient does not redistribute Ace of Spades artwork, audio, maps,
models, fonts, or other retail content. Player packages include only the
native client, `BattleSpadesAssetInstaller`, shaders/configuration, and a
path/size/SHA-256 manifest.

On first graphical launch, the client verifies the local asset tree. If it is
missing or incomplete, the packaged installer opens automatically and asks
for an existing **Ace of Spades: Battle Builder** installation directory. The
installer accepts either the content directory itself or a parent containing
the retail `src` tree, validates all required files, copies them into an
isolated staging directory, and atomically activates the result. Cancelling
leaves the client unchanged; a failed import cannot expose a partial tree or
overwrite a previously valid one.

For automated/local setup, the same importer has a non-interactive form:

```powershell
& .\BattleSpadesAssetInstaller.exe `
  --source "C:\Path\To\Ace Of Spades" `
  --manifest .\asset-manifest.json `
  --destination .\assets\original
```

The graphical executable runs until the window is closed or Quit is selected.
A confirmed configuration is stored in `settings.toml` beside the executable;
see the [Settings reference](docs/SETTINGS.md) for all options, live/restart
semantics, rebinding rules, and display rollback behavior.

UI placement and translations are also external runtime data. Press **F11**
while offline to drag, resize, reset, and save screen elements to
`ui-layout.json`; edit `localization.json` beside the executable to select or
extend a UTF-8 language pack without rebuilding the client. Both files are
preserved by later staging runs. See the
[UI customization guide](docs/UI_CUSTOMIZATION.md) for controls, schemas,
fallback behavior, and the language catalog regeneration command.

Player identity is stored in `%LOCALAPPDATA%\AoS Revival\launcher_state.json`,
the same protected format used by the Python launcher. Existing sessions are
refreshed before the Select Menu appears. Registration usernames are 3-24
ASCII characters and must begin with a letter; registration shows its
one-time recovery code until the player explicitly continues. Play as Guest
creates a persistent signed guest identity when AoSPlay is reachable and a
persistent unranked offline identity otherwise. Servers must advertise
`identity=ticket-v1` and provide their AoSPlay identifier before the client
replaces the normal Protocol 168 player name with a one-use ticket; LAN,
direct-connect and older servers retain the legacy nickname handshake.

See the [UI parity browser](docs/UI_PARITY_BROWSER.md) for the F12 inspector,
catalog regeneration, and retail animation/cursor contracts.

Select **Tutorial**, wait for map synchronization, and press **START** for the
playable native vertical slice. Press **F4** to install/restock all 65 tools;
use 1-0 or the wheel to select, R to reload, RMB to scope with snipers or use
the equipped tool's secondary action, and E or middle mouse for weapon-custom
behavior. Minigun pre-spin is RMB. This is a
developer sandbox and does not replace class-valid loadouts in network play.

The gameplay lab opens with **F10** after startup. Tab switches among a close
character/held-tool view, the retail first-person pose, and the 18-class
gallery. Use Up/Down for class, Left/Right or the mouse wheel for tool, Q/E to
rotate, and -/= to zoom. T cycles the retail default team colors; C selects an
RGB channel and [/] edits that server-defined color. Enter/left mouse exercises
primary use, right mouse secondary use, R reloads, A refills ammunition, B/N
spend/refill blocks, K changes UI skin, M selects one character, Shift+M opens
the gallery, and Delete clears it. F10 or Escape returns to the previous screen.
See the [Map Creator route evidence](docs/research/UGC_EDITOR_FRONTEND_PARITY.md)
for the recovered UGC lobby flow and setting invariants.
A bounded smoke run that closes after two seconds is:

```powershell
& .\out\build\native-dev\src\RelWithDebInfo\BattleSpadesClient.exe --ticks 120 --pace
```

Run only the test suite with:

```powershell
ctest --preset native-dev
```

## Linux and macOS native builds

The renderer/window/resource boundaries are cross-platform and the repository
now includes native CMake presets for Linux and macOS. bgfx supplies Vulkan and
OpenGL on Linux and Metal on macOS; the Settings menu filters itself to the
backends compiled into that executable. The official bgfx defaults are
Direct3D 12 on Windows, Vulkan on Linux, and Metal on macOS.

Requirements on both platforms are CMake 3.25+, Ninja, a C++20 compiler, Git,
and a vcpkg checkout exported through `VCPKG_ROOT`. On Linux, install the
development packages required by SDL3/OpenAL and the X11 or Wayland stack
before vcpkg configuration. On macOS, install current Xcode command-line tools.

Build and test:

```bash
export VCPKG_ROOT="$HOME/src/vcpkg"
bash scripts/build.sh dev
```

Build and install a release tree:

```bash
bash scripts/build.sh release
```

The platform-specific presets are `native-linux-dev`,
`native-linux-release`, `native-macos-dev`, and `native-macos-release`.
`aos_graphics_backend_probe` prints the APIs included in the resulting build.

Windows is the platform validated locally today. Linux and macOS now have
first-class source/build paths, but a release must not be labelled supported
until its native CI runner completes the full suite and a real graphical smoke
on that OS. macOS packaging is currently an executable release tree rather
than a signed/notarized `.app` bundle.

## Local Create Match packaging

Create Match searches for a complete portable server bundle in this order:

1. `AOS_BATTLESPADES_SERVER`, when explicitly set;
2. `server/` beside the client executable;
3. the developer BattleSpades tree in local development builds.

For a distributable client, configure with
`-DAOS_BUNDLED_SERVER_ROOT=<portable-server-directory>` and run the normal
CMake install/package step. The bundle is copied read-only into `bin/server`;
the source server repository is never configured or modified. Each hosted
match receives a private temporary `config.toml`. The selected port is a
preference: if occupied, the launcher scans upward for a free UDP port. The
owned process receives a graceful shutdown when the client leaves or exits,
with a bounded forced-stop fallback.

With a local BattleSpades server listening on UDP 32887, exercise the real
loader handshake and full VXL transfer with:

```powershell
& .\out\build\native-dev\src\RelWithDebInfo\aos_protocol168_live_smoke.exe `
  127.0.0.1 32887 NativeClient
```

The probe deliberately sends an empty packet-105 ticket. BattleSpades accepts
that as its offline identity path; it is not a fabricated Steam credential.

Keep the connection alive after MapSync, send 60 Hz idle ClientData and require
continuous WorldUpdate traffic for five seconds:

```powershell
& .\out\build\native-dev\src\RelWithDebInfo\aos_protocol168_live_session_smoke.exe `
  127.0.0.1 32887 5
```

Exercise the same discovery adapters used by the Server Browser with:

```powershell
# Live https://www.aosplay.net/serverlist/ rows
& .\out\build\native-dev\src\RelWithDebInfo\aos_server_discovery_live_smoke.exe

# BattleSpades LAN query on explicit preferred ports
& .\out\build\native-dev\src\RelWithDebInfo\aos_server_discovery_live_smoke.exe `
  --lan 27015 32887
```

The retail **Connect to IP** screen accepts `host`, `host:port`,
`aos://host:port`, and `local` (`127.0.0.1:32887`). Unknown maps use the retail
placeholder; known official maps use their authored
`png/ui/game_loading/map_previews` image.

The dependency-free core/headless configuration remains available:

```powershell
.\scripts\build.ps1 -Profile Dev
& .\out\build\dev\src\RelWithDebInfo\BattleSpadesClient.exe --headless --ticks 120
```

## Release install and ZIP

Build, test, and install the Windows Release configuration:

```powershell
$env:VCPKG_ROOT = "C:\src\vcpkg"
.\scripts\build.ps1 -Profile Release -Native
```

The installed executables are:

```text
out/install/native-release/bin/BattleSpadesClient.exe
out/install/native-release/bin/aos.exe
```

`aos.exe` is a byte-identical compatibility alias for overlaying the portable
client onto the retail Steam directory so Steam can retain its expected launch
name and play-time tracking. It is not a separate build or code path.

Create the portable ZIP after the Release build succeeds:

```powershell
cmake --build .\out\build\native-release --config Release --target package
```

CPack writes
`out/build/native-release/BattleSpadesClient-0.0.1-Windows-AMD64.zip` and a
matching `.sha256` checksum.
Extract the complete archive and run `bin/BattleSpadesClient.exe` (or the
identical `bin/aos.exe` Steam alias); do not copy
the executable away from its adjacent `assets`, `shaders`, and DLL files.
Use a user-writable extraction directory because confirmed settings are saved
as `bin/settings.toml`. The archive deliberately contains no shared default
settings file, so separate extracted copies remain independent.

The Windows native Release target embeds the byte-identical preserved retail
`game.ico` from `src/platform/windows/game.ico` (SHA-256
`CCAEAE5BFEA3E00E98EBB49871323D771023ECF2B42CC8AA5B12342DB033B784`)
and uses the Windows GUI subsystem, so a normal launch does not open a second
console window. Debug, Dev, and dependency-free builds remain console
executables. An explicit Release command such as `--help`, `--version`,
`--ticks`, or `--headless` preserves redirected output and attaches to an
existing parent console when needed; it never allocates a surprise console
window. Configure with
`-DAOS_WINDOWS_RELEASE_GUI=OFF` when a console-subsystem Release executable is
needed for low-level diagnosis.

The install/package rules also deploy the compiler-selected MSVC runtime and
the app-local Universal CRT set beside the executable, in addition to SDL3,
OpenAL, and the other vcpkg runtime DLLs. The archive does not assume that the
destination machine has Visual Studio or the VC++ Redistributable installed.
The Windows wrapper deliberately selects the newest installed Visual Studio
2022-or-newer toolchain and restores the caller's `VCPKG_ROOT` after
`VsDevCmd`. It also refreshes native CMake caches and verifies the compiler,
headers, libraries, redistributables, and pinned vcpkg tree all belong to that
same toolchain.

## Resource resolution

Graphical startup is independent of the process working directory. Each
resource family is resolved separately in this order:

1. `<executable directory>/assets/original` and
   `<executable directory>/shaders`;
2. the read-only source-tree roots compiled into a developer build.

Packaged resources therefore win even when the executable is launched from a
shortcut, another shell directory, or an extracted ZIP. Startup validates
every manifest path and size rather than accepting a merely present directory;
missing or damaged retail content invokes the adjacent importer. A missing
shader root still fails closed with every attempted path. Headless mode does
not require graphical resources.

The preserved content under `assets/original` is immutable at runtime.
Generated caches belong under `assets/generated`; authored maps and UGC belong
under `assets/user`.

```powershell
py -3 .\tools\assets.py sync --source G:\AoSRevival\aos-nonsteam\src
py -3 .\tools\assets.py verify
cmake --build --preset native-dev --target verify-assets
```

The binary assets remain local, ignored by Git, and excluded from release
archives. Their versioned verification catalog is
`assets/catalog/original-assets.json`.

## Select Menu visual acceptance

The retail reference is documented in
[`docs/research/SELECT_MENU_VISUAL_BASELINE.md`](docs/research/SELECT_MENU_VISUAL_BASELINE.md).
On an interactive, unlocked Windows desktop, capture the exact native client
area at both accepted sizes with:

```powershell
.\tools\capture-client-window.ps1 `
    -Executable .\out\build\native-dev\src\RelWithDebInfo\BattleSpadesClient.exe `
    -OutputPath .\out\evidence\native-main-menu-800x600-final.png `
    -WorkingDirectory . -ClientWidth 800 -ClientHeight 600 `
    -WindowTitle 'Ace of Spades'

.\tools\capture-client-window.ps1 `
    -Executable .\out\build\native-dev\src\RelWithDebInfo\BattleSpadesClient.exe `
    -OutputPath .\out\evidence\native-main-menu-1680x1050-final.png `
    -WorkingDirectory . -ClientWidth 1680 -ClientHeight 1050 `
    -WindowTitle 'Ace of Spades'
```

Measure full-frame RGB SSIM and retain difference images with:

```powershell
.\tools\compare-client-captures.ps1 `
    -Reference .\out\evidence\retail-main-menu-client-800x600.png `
    -Candidate .\out\evidence\native-main-menu-800x600-final.png `
    -DifferencePath .\out\evidence\native-vs-retail-difference-800-final.png `
    -MinimumSsim 0.98

.\tools\compare-client-captures.ps1 `
    -Reference .\out\evidence\retail-main-menu-client-1680x1050.png `
    -Candidate .\out\evidence\native-main-menu-1680x1050-final.png `
    -DifferencePath .\out\evidence\native-vs-retail-difference-1680-final.png `
    -MinimumSsim 0.96
```

The accepted captures score `0.985400` at 800x600 and `0.967606` at
1680x1050. The real-window smoke also passes exact 1000x700 sizing, the retail
320x240 minimum, minimize/restore, focus-loss and mouse-leave capture
cancellation, and graceful Quit:

```powershell
.\tools\smoke-client-window-input.ps1 `
    -Executable .\out\build\native-dev\src\RelWithDebInfo\BattleSpadesClient.exe `
    -WorkingDirectory . `
    -EvidenceDirectory .\out\platform-check\window-input-final `
    -ClientWidth 1000 -ClientHeight 700
```

These are local desktop gates rather than a built-in `--screenshot` command.
The original SSIM thresholds accept the static idle Select Menu. Nested route
composition, direction, and transition-time input gating have a separate
real-window smoke:

```powershell
.\tools\smoke-client-navigation.ps1 `
    -Executable .\out\build\native-dev\src\RelWithDebInfo\BattleSpadesClient.exe `
    -EvidenceDirectory .\out\evidence\frontend-navigation
```

That smoke drives Main through Join Match, Server Browser, Direct Connect, Quick
Play, Create Match and its mode picker, UGC Select, Map Creator browser and
host lobby, Publish Map,
Leaderboard, Player Profile, and match loading. It captures both settled
screens and forward-transition frames under
`out/evidence/frontend-navigation`; evidence in `out` is local and ignored by
Git. Passing this frontend smoke does not exercise live server discovery,
network connection, hosting, HTTP, gameplay, or UGC editing.

Leaderboard, match-result, chat, and generic-vote presentations also have
direct offline capture routes. These bypass login/navigation but use the
production models and draw paths, and never connect to a server:

```powershell
.\out\build\native-dev\src\RelWithDebInfo\BattleSpadesClient.exe `
    --debug-ui leaderboard --run-forever
.\out\build\native-dev\src\RelWithDebInfo\BattleSpadesClient.exe `
    --debug-ui scoreboard --run-forever
.\out\build\native-dev\src\RelWithDebInfo\BattleSpadesClient.exe `
    --debug-ui endgame --run-forever
.\out\build\native-dev\src\RelWithDebInfo\BattleSpadesClient.exe `
    --debug-ui chat --run-forever
.\out\build\native-dev\src\RelWithDebInfo\BattleSpadesClient.exe `
    --debug-ui vote --run-forever
```

## Linux and macOS status

Executable-path discovery, adjacent resource layout, shader variants, install
rules, and ZIP generation are written for Windows, Linux, and macOS. A generic
single-configuration native build is:

```sh
cmake -S . -B out/build/native-local -G Ninja \
  -DAOS_ENABLE_NATIVE_BACKENDS=ON \
  -DAOS_BUILD_PROFILE=release \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build out/build/native-local --parallel
ctest --test-dir out/build/native-local --output-on-failure
cmake --install out/build/native-local --prefix out/install/native-local
cpack --config out/build/native-local/CPackConfig.cmake
```

Only the Windows x64 native archive has been built and smoke-tested so far.
Linux and macOS still need native CI runners, shared-library closure checks,
package smoke tests, and platform-specific release work. There is no macOS
`.app` bundle, signing, or notarization yet. Treat packages from the generic
commands as developer artifacts, not supported releases.

## Command-line options

```text
--headless       Disable platform, graphics, text, and audio backends
--ticks N        Stop after N fixed simulation ticks
--tick-rate N    Use a fixed rate from 1 through 1000 Hz
--pace           Pace ticks against the monotonic wall clock
--run-forever    Run paced until the window/process is closed
--version        Print version and build profile
--help, -h       Print command help
```

See the [architecture](docs/ARCHITECTURE.md),
[frontend reconstruction](docs/FRONTEND.md),
[Settings reference](docs/SETTINGS.md),
[compatibility contract](docs/COMPATIBILITY.md), and
[roadmap](docs/ROADMAP.md).
