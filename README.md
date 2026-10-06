<div align="center">

# BattleSpadesClient

**A native, open-source client for _Ace of Spades_ (2012), built to keep the game playable on modern systems.**

[![License: AGPL-3.0-or-later](https://img.shields.io/badge/license-AGPL--3.0--or--later-blue.svg)](LICENSE)
[![Latest release](https://img.shields.io/github/v/release/KikoTs/BattleSpadesClient?include_prereleases&sort=semver)](https://github.com/KikoTs/BattleSpadesClient/releases)
![Platforms: Windows | Linux | macOS](https://img.shields.io/badge/platforms-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey.svg)
![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C.svg?logo=cplusplus)
[![Website: aosplay.net](https://img.shields.io/badge/website-aosplay.net-orange.svg)](https://www.aosplay.net)
[![Discord](https://img.shields.io/badge/discord-join-5865F2.svg?logo=discord&logoColor=white)](https://discord.gg/aosbb)
[![Steam app 224540](https://img.shields.io/badge/Steam-app%20224540-1b2838.svg?logo=steam)](https://store.steampowered.com/app/224540/)

</div>

BattleSpadesClient is part of the [AoS Revival](https://www.aosplay.net) project.
It is a from-scratch C++20 client (CMake, vcpkg, SDL3, bgfx, OpenAL Soft) for
_Ace of Spades_ ("Battle Builder", Steam app 224540), the voxel shooter Jagex
published in 2012 and later discontinued. It speaks the original game's network
protocol (Protocol 168) and plays on the
[BattleSpades](https://github.com/KikoTs/BattleSpades) server.

> **Unofficial fan project.** Not affiliated with, endorsed by or sponsored by
> Jagex Ltd or Valve Corporation. This repository contains no Jagex game assets
> and no retail game code. See [Trademarks and legal notice](#trademarks-and-legal-notice).

## Contents

- [What is BattleSpades?](#what-is-battlespades)
- [Goals](#goals)
- [What this repository does not ship](#what-this-repository-does-not-ship)
- [Download and install](#download-and-install)
- [Building from source](#building-from-source)
- [Project layout](#project-layout)
- [Related projects](#related-projects)
- [Contributing](#contributing)
- [License](#license)
- [Trademarks and legal notice](#trademarks-and-legal-notice)

## What is BattleSpades?

The original game was a Python 2 client built on pyglet, and it stopped being
maintained long ago. BattleSpades replaces it with a native client while
keeping compatibility with the original protocol, maps and content:

- **Client (this repository):** gameplay, voxel rendering, client-side
  prediction, HUD, menus, Map Creator, Steam integration, audio.
- **[BattleSpades server](https://github.com/KikoTs/BattleSpades):** the
  authoritative game server. It is also bundled with the client so players can
  host their own matches.
- **Game content:** the original models, maps, textures and sounds still come
  from the player's own copy of the game (see below).

The source includes the offline Tutorial and the live multiplayer path: map
loading, movement and reconciliation, classes and tools, combat, terrain
editing, entities, objectives, chat, HUD, round flow, server discovery, Quick
Play, lobbies, local hosting, Map Creator and Workshop maps, profiles,
leaderboards and the inventory. These paths are implemented; the
[roadmap](docs/ROADMAP.md) lists what still needs acceptance testing.

## Goals

- **Faithful retail parity.** Movement, weapons, HUD, menus and timings match
  the original game, verified against the original client's observable
  behaviour. See the [compatibility contract](docs/COMPATIBILITY.md).
- **Optional Enhanced graphics.** A modern renderer with an Enhanced/Classic
  toggle: Classic stays close to the original look, Enhanced adds better
  lighting and effects.
- **Modern platforms.** Windows, Linux and macOS, on x64 and arm64.
- **Steam integration.** Steam overlay, friends and invites, Workshop map
  subscriptions, and launching from the existing Steam entry.
- **Automatic updates.** A small launcher keeps the client, its assets and the
  optional server up to date ([details](docs/INSTALLER_AND_UPDATER.md)).
- **Hosting for everyone.** Create Match and Map Creator start the bundled
  BattleSpades server locally, with no separate setup.

## What this repository does not ship

- **No Jagex game assets.** Models, maps, textures, sounds, fonts and music
  from the original game are not in this repository or its source archives.
  `assets/original/` is git-ignored, and
  [`assets/catalog/original-assets.json`](assets/catalog/original-assets.json)
  lists only file paths, sizes and SHA-256 hashes.
- **No retail binaries or code.** No original executables, Python modules,
  `.pyd` files or decompiled source are included. Retail behaviour was studied
  for interoperability, and the client is an independent implementation.
- **No Steamworks SDK.** The SDK headers are fetched at build time and the
  `steam_api` runtime library is redistributed only under Valve's terms; see
  [Third-party notices](THIRD_PARTY_NOTICES.md).

Players need their own copy of _Ace of Spades_. On first launch, the client's
importer finds an existing Steam installation (or lets you pick the folder),
verifies every file against the manifest and copies it into
`assets/original/`. Your Steam installation is never modified.

## Download and install

1. Get the latest Windows build from
   [aosplay.net/download](https://www.aosplay.net/download) or the
   [Releases page](https://github.com/KikoTs/BattleSpadesClient/releases).
2. Run the installer (or extract the portable ZIP).
3. Start BattleSpades. On first launch, choose how to get the original game
   files: from your _Ace of Spades_ folder, which is detected through Steam,
   or from the download offered by the first-run screen. Both paths end with
   the same files, checked against the manifest.
4. Optionally enable hosting, which downloads the BattleSpades server for
   Create Match and Map Creator.

After that, the launcher checks for updates on each start and never blocks
play when offline. Linux and macOS builds are currently built from source
(below).

## Building from source

Requirements on all platforms: CMake 3.25 or newer, Ninja, Git, a C++20
compiler and a [vcpkg](https://github.com/microsoft/vcpkg) checkout with
`VCPKG_ROOT` set. Dependencies are pinned in [`vcpkg.json`](vcpkg.json) and
installed automatically on the first configure. Some tests replay movement on
the server's Training map, so clone
[BattleSpades](https://github.com/KikoTs/BattleSpades) next to this
repository (`../BattleSpades`) to run the full test suite.

### Windows

Use Visual Studio 2022 or newer with the C++ x64 tools. From PowerShell at the
repository root:

```powershell
$env:VCPKG_ROOT = 'C:\src\vcpkg'
.\scripts\build.ps1 -Profile Dev -Native          # configure, build and test
& .\out\build\native-dev\src\RelWithDebInfo\BattleSpadesClient.exe
```

Or use the presets directly:

```powershell
cmake --preset native-release
cmake --build --preset native-release
ctest --preset native-release --output-on-failure
```

### Linux and macOS

Install a C++20 compiler and the development packages SDL3 needs for your
desktop (X11/Wayland and audio on Linux; Xcode 26 or newer on macOS), then:

```sh
export VCPKG_ROOT=~/src/vcpkg
bash scripts/build.sh dev        # or: release
```

The script picks `native-linux-*` or `native-macos-*`, builds, runs the tests
and installs into `out/install/<preset>`.

On Linux the client prefers X11, which Wayland desktops provide through
XWayland, and falls back to native Wayland when no X server is available. Set
`SDL_VIDEO_DRIVER=wayland` to use native Wayland directly.

### Presets

| Platform | Configure / build / test presets |
| --- | --- |
| Windows | `native-dev`, `native-release`, `native-debug` (plus headless `dev`, `release`, `debug`) |
| Windows on Arm | `native-windows-arm64-dev`, `native-windows-arm64-release` |
| Linux | `native-linux-dev`, `native-linux-release` |
| macOS | `native-macos-dev`, `native-macos-release` |

All six platform builds (Windows, Linux and macOS on x64 and arm64) can also
be made on GitHub Actions; see [docs/BUILDING.md](docs/BUILDING.md).

The [runbook](docs/RUNBOOK.md) covers staging a playable folder, packaging,
shaders, hosting, smoke tests and diagnostics. The
[installer guide](docs/INSTALLER_AND_UPDATER.md) covers the Windows installer
and updater.

## Project layout

| Path | Contents |
| --- | --- |
| `src/`, `include/battlespades/` | Client source, split by subsystem: `core`, `world`, `network`, `render`, `frontend`, `audio`, `platform`, `settings`, `updater`, ... |
| `tests/` | Unit, integration and offscreen rendering tests (CTest) |
| `tools/` | Generators, capture and smoke-test tools for development |
| `assets/client/` | Project-owned UI files and fonts, plus community cosmetics with their own licences |
| `assets/catalog/` | Manifest of the original game files (paths, sizes and hashes only) |
| `config/` | Localization packs and the default UI layout |
| `steam_bridge/` | Small 32-bit Windows helper that talks to the Steam runtime on the client's behalf (session tickets) |
| `installer/` | Windows installer (Inno Setup) and update-manifest scripts |
| `third_party/` | Vendored AngelScript (zlib licence) |
| `cmake/`, `scripts/`, `packaging/` | Build helpers, wrapper scripts and platform packaging |
| `docs/` | Design notes, guides and research ([index](docs/README.md)) |

## Related projects

- [BattleSpades](https://github.com/KikoTs/BattleSpades): the authoritative
  game server (Python 3 and Cython, AGPL-3.0).
- [aceofspades_revival](https://github.com/KikoTs/aceofspades_revival): a patch
  for the original Steam client that connects it to AoS Revival servers.
- [aosplay.net](https://www.aosplay.net): downloads, server list, accounts,
  credits and community links.

## Contributing

Bug reports, fixes and parity research are welcome. Read
[CONTRIBUTING.md](CONTRIBUTING.md) before opening a pull request. Report
security problems privately as described in [SECURITY.md](SECURITY.md). Chat
with the community on [Discord](https://discord.gg/aosbb).

Thanks to the people who have contributed fixes:
[@lucasoskorep](https://github.com/lucasoskorep) (Linux XWayland and native
Wayland start-up, Classic CTF in the server browser, GCC 16 build warnings).

## License

Copyright (c) 2026 Kiril Tsanov and contributors.

BattleSpadesClient is free software under the
[GNU Affero General Public License v3.0 or later](LICENSE)
(`AGPL-3.0-or-later`), with an additional permission to link with the
Steamworks SDK. [LICENSING.md](LICENSING.md) explains what that means for
redistribution and for the bundled server. Third-party libraries, fonts and
community cosmetics keep their own terms
([THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)); several cosmetics are
non-commercial only.

## Trademarks and legal notice

- _Ace of Spades_ is a trademark of Jagex Ltd. Steam and the Steam logo are
  trademarks of Valve Corporation. All other trademarks belong to their owners.
- BattleSpades is an unofficial, non-commercial fan project. It is not
  affiliated with, endorsed by or sponsored by Jagex Ltd or Valve Corporation.
- This repository does not distribute the original game's assets, executables
  or code. The original game's behaviour was studied only to make the client
  interoperable with existing servers, maps and content; the implementation is
  original work.
- **Rights holders:** if you believe something in this repository infringes
  your rights, contact `<CONTACT EMAIL TO BE FILLED IN>` or open an issue
  titled "Takedown request", and it will be reviewed and removed promptly.
  See [LEGAL.md](LEGAL.md).
