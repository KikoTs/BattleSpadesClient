# Launch options, offline play and join links

These options describe the current source. Build or install a release containing
them before using them. Use `BattleSpadesClient.exe` on Windows and
`BattleSpadesClient` on Linux/macOS (in a Mac bundle,
`Contents/MacOS/BattleSpadesClient`). The Windows `BattleSpadesLauncher.exe`
forwards game options.

## Offline and local play

```powershell
BattleSpadesClient.exe --offline
BattleSpadesClient.exe --profile "LAN Player" +connect 127.0.0.1:27015
BattleSpadesClient.exe --profile "LAN Player" +connect 192.168.1.20:27015
```

`--profile NAME` implies `--offline`. Names contain 1–15 ASCII letters, digits,
spaces, underscores or hyphens. Each name has a persistent local guest identity,
separate from normal online account state. `--offline` alone selects Player.
The client skips online account requests, Steam integration, public server lists,
leaderboards and automatic launcher updates. Tutorial, local hosting, LAN browser
and direct connections remain available. Direct connections may target a remote
server too, provided it accepts unauthenticated players. Offline profiles do not
grant a registered account's rank, inventory or server privileges.

Install/import the game assets and local server component before disconnecting.
Offline startup reports missing assets instead of starting a network download.
Offline mode does not bypass server passwords or authentication policies.

Run a dedicated server with `BattleSpades.exe --offline` or
`python run_server.py --offline`; see the server's
[offline guide](https://github.com/KikoTs/BattleSpades/blob/main/docs/OFFLINE_AND_LAUNCH_OPTIONS.md).
Neither a master server nor Internet access is required for direct LAN matches.

## Choosing a master server

```powershell
BattleSpadesClient.exe --master-url https://master.example.org
BattleSpadesClient.exe --master-url http://127.0.0.1:3000
BattleSpadesClient.exe +master_server https://master.example.org +connect 192.168.1.20:27015
```

Supply an origin without `/api`, credentials, query or fragment. HTTPS is
accepted for community hosts; HTTP is accepted only for exact localhost,
127.0.0.1 or [::1]. Use HTTPS to reach a master on another LAN machine.
The service must implement the AoSPlay APIs; an arbitrary website or Classic
server-list URL is not a replacement for the account/master service.

The override selects authentication, server listing, leaderboard and profile
endpoints together. Alternate masters use separate saved login state; production
bearer tokens are never sent to an alternate master. The extra public Classic
list is disabled with a custom master. Direct game connections remain independent
of this service. Master overrides are never accepted from a join link.

See [aos_revival-master](https://github.com/KikoTs/aos_revival-master) for complete
source, migrations, relay code, local setup and hosting documentation.

## Player options

| Option | Behavior |
| --- | --- |
| `+connect ADDRESS`, `--connect ADDRESS` | Direct host/port, `aos://`/`aosbb://` URI or `steam:STEAMID`. Default direct port: 27015. |
| `+connect_lobby ID`, `--connect-lobby ID` | Steam lobby invitation. Requires Steam and online mode. |
| `+password TEXT`, `--password TEXT` | Startup server password; visible in process arguments. |
| `+language NAME`, `--language NAME` | Retail language name or locale code. |
| `-reset`, `--reset-settings` | Start from defaults without deleting the existing settings file. Saving in the menu persists the new values. |
| `--offline` | Local Player profile, online services disabled. |
| `--profile NAME` | Named offline profile. |
| `--master-url ORIGIN`, `+master_server ORIGIN` | Compatible community master/account service. |
| `--record-demo FILE` | Incoming match packet stream including map transfer. |
| `--play-demo FILE` | Offline spectator playback; cannot combine with connect/record. |
| `--shader-quality TIER` | compatibility, low, medium, high or ultra for this run. |
| `--help`, `--version` | Options or build version. |

Recovered original player switches are `+connect`, `+connect_lobby`,
`+language`, and `-reset` (`aoslib/run.py`, `aoslib/strings/__init__.py`).
Retail language names: english, german, french, spanish, italian, brazilian,
portuguese_brazil, russian, polish, turkish, mexican, spanish_mexico and japanese.
The later Python launcher's `+legacymouse` selects its patched pyglet input path;
the native SDL client does not use it. Developer unlocks, crash-dump options and
legacy launcher child-process flags are intentionally excluded from this guide.

## Click-to-join links

Examples: `aosbb://127.0.0.1:27015`, `aos://127.0.0.1:32887:0.75`.
Classic little-endian packed IPv4 links such as `aos://16777343:32887:0.75`
are supported. `aosbb` targets BattleSpades; an explicit `:0.75`/`:0.76` suffix
can still select a Classic server.

The Windows installer registers `aosbb` per user and registers `aos` only when no
existing handler owns it. Uninstall removes only associations still pointing at
that installation. For portable installations, run the script from this repo:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/register-protocols.ps1 -Executable "C:\Games\BattleSpades\BattleSpadesClient.exe"
# Explicitly choose BattleSpades for Classic aos links too:
powershell -ExecutionPolicy Bypass -File scripts/register-protocols.ps1 -Executable "C:\Games\BattleSpades\BattleSpadesClient.exe" -IncludeClassic
```

Linux desktop registration:

```sh
sh scripts/register-protocols.sh /absolute/path/BattleSpadesClient
sh scripts/register-protocols.sh /absolute/path/BattleSpadesClient --include-classic
```

Mac bundles declare both schemes in `CFBundleURLTypes`. SDL delivers activation
through its [file-drop event](https://github.com/libsdl-org/SDL/blob/main/src/video/cocoa/SDL_cocoaevents.m).
All handlers use the exclusive `--join-url URL` input: a URL selects only an
endpoint, never a master, profile, file or additional launch option.

Windows may start another client when one is running; macOS delivers links to
the existing process. Check OS registration on each release target.

See [Demo recording](DEMOS.md) and [Update downloads](UPDATE_DOWNLOADS.md).
