# Client build and validation runbook

Run commands from the client repository root. `CMakePresets.json`, the build
wrappers, `tools/stage-native-client.ps1` and CMake install rules define the
supported commands. Regenerate output rather than accumulating dated builds.

## Windows

Requirements: CMake 3.25+, Visual Studio 2022 or newer with C++ x64 tools,
Ninja, Git and `VCPKG_ROOT` pointing to a vcpkg checkout.

```powershell
$env:VCPKG_ROOT = 'C:\src\vcpkg'
.\scripts\build.ps1 -Profile Dev -Native
& .\out\build\native-dev\src\RelWithDebInfo\BattleSpadesClient.exe
ctest --preset native-dev --output-on-failure
```

The wrapper builds the x86 Steam helper, loads the selected Visual Studio
environment, configures the native preset, builds and tests. `-Profile Debug`
uses `native-debug`/`Debug`; `-Profile Release` uses `native-release`/`Release`.
Omit `-Native` for the corresponding core/headless configuration. `-SkipTests`
skips tests explicitly and does not establish release readiness.

A direct `cmake --build --preset native-dev` needs a compatible compiler
environment when recompilation is required. Use the wrapper from an ordinary
shell instead of hardcoding a Visual Studio installation path.

## Staging and release packaging

```powershell
# Update the local playable directory after building.
.\tools\stage-native-client.ps1

# Build, test, install and package Release.
.\scripts\build.ps1 -Profile Release -Native
cmake --build .\out\build\native-release --config Release --target package
```

Staging defaults to `native-dev`, `RelWithDebInfo`, `dist/bin`. Its
`-BuildPreset`, `-Configuration` and `-Destination` arguments select a different
existing build. Both launch names, DLLs, shaders and client content are copied
together. Existing `ui-layout.json` and locale files are preserved unless an
explicit localization refresh is requested. Retail assets are imported separately.

The Release wrapper installs into `out/install/native-release`. CPack writes a
version/platform-named ZIP and SHA-256 checksum under `out/build/native-release`;
`CMakeLists.txt` defines the name. `out/package/` is the local shelf for a
retained distributable, not CPack's default output. An older retained ZIP can
predate the staged executable. Validate the exact archive before distribution.

Windows Release uses the GUI subsystem for normal play; explicit command-line
and redirected-output modes remain available. `AOS_WINDOWS_RELEASE_GUI=OFF`
selects a console Release build. Install rules supply the selected MSVC/UCRT
and native runtime dependencies, plus the importer/manifest. Verify an extracted
archive on a clean machine before claiming dependency closure.

After editing shaders, run `scripts/compile-shaders.ps1`, then stage the whole
backend shader tree with the executable. The default compiler is
`out/shaderc-build/cmake/bgfx/shaderc.exe`; its bgfx include tree is
`out/vcpkg/packages/bgfx_x64-windows/include/bgfx`. Keep those dependencies and
the bgfx source used by the shader compiler build.

## Linux and macOS

```bash
export VCPKG_ROOT="$HOME/src/vcpkg"
bash scripts/build.sh dev
bash scripts/build.sh release
```

The wrapper selects `native-linux-dev`/`native-linux-release` or
`native-macos-dev`/`native-macos-release`, tests, and installs Release under
`out/install/<preset>`. Install the compiler and native window/audio development
dependencies required by the selected platform. Platform presets and workflow
definitions exist; this does not prove a current CI run or graphical package
check passed. Test on the target OS and verify signing/application packaging
for the actual release artifact.

## Hosting

Local hosting resolves `AOS_BATTLESPADES_SERVER`, an adjacent `server/` bundle,
then supported developer fallbacks. Prefer an explicit current portable bundle:

```powershell
$env:AOS_BATTLESPADES_SERVER = (Resolve-Path '..\BattleSpades\release-dist\BattleSpades-0.0.3-alpha.9-windows-x86_64').Path
& .\dist\bin\BattleSpadesClient.exe
```

The versioned directory above is the retained local release; use the bundle
you actually intend to run. For distribution configure `AOS_BUNDLED_SERVER_ROOT`
and use CMake install/package. A complete bundle includes its executable,
`_internal` and maps. See the [server runbook](../../BattleSpades/docs/RUNBOOK.md).

Create Match supervises a hidden child with private session configuration and
waits for real readiness before connecting. Public hosting also depends on
signed identity, social state and a live relay allocation. Leaving an owned
match stops its child/tunnel. Preserve persistent hosted-result queues and
authored UGC projects when replacing builds.

## Validation

```powershell
ctest --preset native-dev -N
ctest --preset native-dev --output-on-failure
& .\dist\bin\BattleSpadesClient.exe --headless --ticks 120
```

On a Windows machine with all four drivers available, exercise the real GPU
paths explicitly after building. CTest selects D3D11 by default so machines
without another backend do not silently claim coverage:

```powershell
& .\out\build\native-dev\tests\RelWithDebInfo\aos_ui_renderer_tests.exe all
foreach ($backend in @('direct3d11', 'direct3d12', 'vulkan', 'opengl')) {
    & .\out\build\native-dev\tests\RelWithDebInfo\aos_shadow_stability_render_tests.exe `
        $backend "out/evidence/shadows/$backend"
    if ($LASTEXITCODE -ne 0) { throw "Shadow regression failed: $backend" }
}
```

The UI test verifies captured pixels, clipping, texture lifetime, minimize/reset
and exact-slot reuse after restart. The shadow test checks direction, floor
contact, light-volume culling and 192 moving-camera frames per backend. Run GPU
tests sequentially. These checks do not establish frame rates on other hardware.
Compiling Metal shader binaries and testing its matrix/vertex conventions are
separate from actually running Metal on a Mac; the release needs both.

Use `--help` for the current CLI. Offline `--debug-ui` fixtures include
`leaderboard`, `scoreboard`, `endgame`, `chat`, `vote`, `inventory`,
`inventory/crates`, `ugc_lobby`, `ugc_browser` and `ugc_loadout`. They exercise
presentation without granting account rewards or publishing maps.

Loading fixtures are `loading/map`, `loading/mode`, `loading/scores`,
`loading/hosting` and `loading/error`. `create_match/notice` supplies a long
lobby notification and a diagnostic tail to check wrapping, truncation and
footer clearance. Opening a fixture does not create a server or connect to a
match. The Create Match fixture retains working controls: clicking Local Match
starts the bundled private server and follows the real loading path.
For example, after staging the rebuilt client:

```powershell
.\tools\capture-client-window.ps1 `
    -Executable .\dist\bin\BattleSpadesClient.exe `
    -OutputPath .\out\evidence\loading-mode-800x600.png `
    -ArgumentList @('--debug-ui', 'loading/mode', '--run-forever', '--pace') `
    -ClientWidth 800 -ClientHeight 600 -SettleMilliseconds 5000
.\tools\capture-client-window.ps1 `
    -Executable .\dist\bin\BattleSpadesClient.exe `
    -OutputPath .\out\evidence\lobby-notice-1280x720.png `
    -ArgumentList @('--debug-ui', 'create_match/notice', '--run-forever', '--pace') `
    -ClientWidth 1280 -ClientHeight 720 -SettleMilliseconds 5000
```

Repeat with `loading/scores`, `loading/hosting` and `loading/error` at both sizes.
Check caption visibility, score group expansion/scrolling, map-frame edges and
readable status text. Fixtures exercise presentation; use a real local-host smoke
to verify server readiness and connection behavior.

Windows capture/navigation tools are `tools/capture-client-window.ps1`,
`compare-client-captures.ps1`, `smoke-client-window-input.ps1`,
`smoke-client-navigation.ps1` and `smoke-client-settings.ps1`. Recreate reference
and candidate captures before comparing them; old cleanup-removed files are
not available fixtures. Put new captures under `out/evidence/`.

`tools/run-server-client-parity.ps1` and `run-server-two-client-parity.ps1`
exercise live movement/replication. Menu tests do not replace owner/observer,
late-join, adverse-network, transition or mixed retail/native match acceptance.
For an isolated local multiplayer matrix, choose unused ports and run:

```powershell
.\tools\run-server-two-client-parity.ps1 -Port 32800 -Seconds 12 -SecondClientDelaySeconds 0 `
    -EvidenceDirectory out/evidence/simultaneous
.\tools\run-server-two-client-parity.ps1 -Port 32801 -Seconds 60 -SecondClientDelaySeconds 5 `
    -EvidenceDirectory out/evidence/late-join
.\tools\run-server-client-parity.ps1 -Port 32802 -Seconds 20 `
    -TracePath out/evidence/latency.csv `
    -ClientArguments @('--uplink-ms=300','--downlink-ms=300','--jitter-ms=100')
& .\out\build\native-dev\src\RelWithDebInfo\aos_local_server_host_smoke.exe `
    .\dist\bin\server --native-bridge
```

Repeat short simultaneous runs to cover different spawn terrain. The movement
schedule runs for about 9.5 seconds; a longer run also checks idle replication.
Delay/jitter flags queue application packets after bootstrap and preserve
ordering. They do not simulate packet loss, transport reordering, or delayed
handshake packets. The native host smoke verifies real readiness, map bootstrap
and shutdown; menu cancellation and XP delivery have separate regressions.
Public service smokes can create fixture identities, lobbies and relay leases;
check their arguments and cleanup behavior before using a live service.

Record the source revision and local diff, configuration, graphics backend and
result for each release gate. Historical totals, timings and executable hashes
are not current acceptance. See [ROADMAP.md](ROADMAP.md).

## Local layout

| Path | Purpose |
| --- | --- |
| `src/`, `include/`, `tests/`, `tools/`, `scripts/`, `config/` | Source and regression fixtures |
| `assets/original/` | Imported owned retail content |
| `assets/client/` | Client resources, catalogues and attribution |
| `third_party/` | Build dependencies, not scratch output |
| `dist/bin/` | Local playable staging tree and player files |
| `out/build/native-dev/` | Development build, symbols and test binaries |
| `out/build/native-release/` | Release build and generated packages |
| `out/build/steam-bridge-x86/`, `out/steam-bridge/` | Steam helper build/output |
| `out/vcpkg_installed/`, retained bgfx source/package, `out/shaderc-build/` | Active native/shader dependencies |
| `out/package/` | Retained distributable ZIP/checksum |
| `out/evidence/`, `tmp/`, `Testing/` | Regenerable diagnostics |

Keep current build configurations and their symbols. Superseded binaries,
`.before-*` copies and old captures can be removed after checking for authored
content. No recovery ZIP is required. Preserve source fixtures, licences,
maps/settings, identity and pending results. The separate active client working
copy in the server's `validation-reports/` contains ongoing source work; its
directory name alone does not make it disposable.
