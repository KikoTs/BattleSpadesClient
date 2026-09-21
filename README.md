# BattleSpadesClient

A C++20 client for Ace of Spades: Battle Builder, compatible with the
authoritative [BattleSpades server](../BattleSpades/README.md) over Protocol 168.
SDL3, bgfx, OpenAL Soft, FreeType, HarfBuzz and ENet provide native services;
this repository owns gameplay, voxel rendering, prediction and presentation.

## Current implementation

The source contains a playable offline Tutorial and a live multiplayer path:
map loading, movement/reconciliation, classes and tools, combat events, terrain
edits, entities, objectives, chat, HUD and round transitions. The frontend has
discovery/direct join, Quick Play, social lobbies, hidden local-server hosting,
Map Creator, Revival Workshop publishing, profiles, leaderboards and Inventory.
These are implemented paths, not a claim that every retail behavior or external
service has passed current acceptance testing.

The collection supports earned cosmetics, native RmlUi inventory presentation,
validated model packs and bounded OpenSpades weapon presentation scripts.
Capability-gated appearances leave the original Protocol 168 gameplay stream
unchanged for retail clients. See the [compatibility contract](docs/COMPATIBILITY.md)
and [remaining release gates](docs/ROADMAP.md).

This documentation describes the source checkout. A retained executable or ZIP
may predate local edits. Rebuild and validate before treating it as a release of
the current tree. Historical pass totals and deployment notes do not establish
the current build's quality or live service state.

## Build and run on Windows

Install CMake 3.25+, Visual Studio 2022 or newer with C++ x64 tools, Ninja, Git
and a vcpkg checkout. From PowerShell at the repository root:

```powershell
$env:VCPKG_ROOT = 'C:\src\vcpkg'
.\scripts\build.ps1 -Profile Dev -Native
& .\out\build\native-dev\src\RelWithDebInfo\BattleSpadesClient.exe
```

The wrapper selects the installed toolchain, builds the native client and
Steam bridge, and runs CTest. For an already configured build:

```powershell
ctest --preset native-dev --output-on-failure
```

Stage the built development client into the local playable directory:

```powershell
.\tools\stage-native-client.ps1
& .\dist\bin\BattleSpadesClient.exe
```

Staging copies the executable, runtime DLLs, shaders and client assets. It
preserves existing editable layout/translation files by default; it does not
rebuild or produce a release ZIP. `aos.exe` is the identical Steam-compatible
launch alias. See the [runbook](docs/RUNBOOK.md) for packaging, other platforms,
shader updates, hosting, smoke tests and the output layout.

## Assets and player files

Retail content comes from an owned Ace of Spades installation. At graphical
startup, missing or incomplete content invokes the asset installer, which
validates the manifest before activating a complete staged tree. See
[asset ownership and layout](assets/README.md).

Keep the executable with its adjacent DLLs, shaders and assets in a writable
directory. Player files include `settings.toml`, `ui-layout.json`, `localization/`,
cosmetic preferences and authored `hosted_ugc/maps` projects. Resource lookup
uses the executable directory and developer fallbacks, not the shell's current
directory. Runtime code treats original source assets as read-only.

Windows identity uses `%LOCALAPPDATA%\AoS Revival\launcher_state.json` with
DPAPI protection. Hosted result reports live below the identity state directory
and survive disposable server sessions. Identity, saved maps and pending match
results are not build caches.

## Documentation

The [documentation index](docs/README.md) separates maintained guides from
retail recovery/specification evidence:

- [Runbook](docs/RUNBOOK.md): build, test, stage, package and diagnose.
- [Architecture](docs/ARCHITECTURE.md): runtime and subsystem ownership.
- [Frontend](docs/FRONTEND.md), [Settings](docs/SETTINGS.md) and
  [UI customization](docs/UI_CUSTOMIZATION.md): routes and configuration.
- [Map Creator](docs/UGC_MAP_CREATOR.md): editor authority and persistence.
- [Inventory](docs/INVENTORY.md), [cosmetics](docs/COMMUNITY_COSMETICS.md) and
  [weapon presentation](docs/WEAPON_SIGHTS_AND_SOUND.md): collection and skins.
- [Compatibility](docs/COMPATIBILITY.md) and [roadmap](docs/ROADMAP.md): invariants
  and acceptance still required.

Historical measurements remain useful research. Their old artifact paths,
session constraints and test results are not current operating instructions.
