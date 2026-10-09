# Windows installer and auto-updater

This covers `BattleSpades-Setup-<version>.exe`, the auto-updating
`BattleSpadesLauncher.exe`, the Steam registration the installer can do, the
update manifest (`stable.json`) that the launcher and dedicated servers read,
and exactly how a release is published.

## Pieces

| Piece | Source | Role |
| --- | --- | --- |
| `BattleSpadesLauncher.exe` | `src/updater/windows/launcher_main.cpp` | Entry point for Steam, the Start menu and the desktop icon. Steam Play chooser (BattleSpades or the original game), first-run screen (game files / hosting), per-launch updates of every installed component from mirrors, opt-in `--install-component`, `update/hosting.json`; then starts `BattleSpadesClient.exe` and waits for it. |
| `BattleSpadesSetupHelper.exe` | `src/updater/windows/setup_helper_main.cpp` | Console tool the installer runs: `detect` (Steam root, the library holding app 224540, the Steam account), `register` / `unregister` (launch options and non-Steam shortcut). |
| `aos_updater_core` | `src/updater/*.cpp`, `include/battlespades/updater/` | Portable, unit-tested logic: update manifest, planner and mirror fallback; staged per-component apply and rollback; text/binary VDF; Steam library detection; registration state; semver; SHA-256; legacy GitHub parsing. |
| Hosting gate | `src/platform/hosting_gate.cpp` | Create Match / Map Creator: reads `hosting.json`, asks the launcher to download a missing or incompatible server, never blocks playing. |
| Server notice / `--update` | `BattleSpades/server/update_check.py` | Dedicated servers log "new server version available" and stage the server component on request. |
| Installer script | `installer/windows/BattleSpades.iss` | Inno Setup 6 wizard. |
| Build script | `installer/build-installer.ps1` | Stages the CMake install tree plus the server, runs ISCC, writes the three component packages. |
| Manifest generator | `installer/make-update-manifest.ps1` | Writes `stable.json` from the packages and mirror base URLs; `-Verify` checks every uploaded URL. |
| Retail pack builder | `installer/make-retail-assets-pack.ps1` | Builds the `retail_assets` ZIP from Kiril's own Ace of Spades folder (never uploads; see "Packaging `retail_assets`"). |

Both executables link the **static CRT and system DLLs only** (`winhttp`,
`comctl32`, `shell32`, `user32`, `gdi32`, `kernel32`, `advapi32`, `ole32`).
That is what lets the launcher replace every DLL in the install folder, and
the helper run from the installer's temp folder. Both embed an `asInvoker`
manifest: without it, Windows' installer detection would request elevation
for any executable whose name contains "setup", "update" or "install". There
is no HTTP code in the game itself; the launcher uses WinHTTP.

## What the installer does

The wizard is near one-click. Its only pages are **Ready to install** (one
**Install** click; it shows the folder) and **Finished** (with **Play
BattleSpades** ticked). There is no welcome, license, folder, Start-menu or
tasks page and no UAC prompt; everything below is decided automatically.
`/DIR="<folder>"` on the command line still picks another folder, and
`/SILENT` or `/VERYSILENT` install without any page.

1. Runs `BattleSpadesSetupHelper detect`: reads `HKCU\Software\Valve\Steam\SteamPath`
   (fallback `HKLM\SOFTWARE\WOW6432Node\Valve\Steam\InstallPath`), parses
   `steamapps\libraryfolders.vdf` (current and pre-2021 formats) and returns the
   library whose `steamapps\appmanifest_224540.acf` exists, with
   `steamapps\common\<installdir>` from that manifest.
2. Folder: `<library>\steamapps\common\aceofspades\BattleSpades`. No
   retail file is overwritten; the wizard refuses the game folder itself (a
   folder containing the retail `aos.pkg`; BattleSpades ships its own
   `aos.exe`, so that is not a marker). Without Ace of Spades installed, or
   when its folder is not writable for the user, it uses
   `%LOCALAPPDATA%\Programs\BattleSpades`. An upgrade keeps the previous
   folder.
   Installation is per user (`PrivilegesRequired=lowest`): Steam's folder is
   user-writable by default, and the updater must write the folder later
   without elevation.
3. Copies the **main build**: the client, its project-owned assets
   (`assets\client`, shaders, localization; an edited `ui-layout.json` is
   kept), the Steam bridge, the asset importer and the launcher/helper.
   - The BattleSpades server is **not** in the installer. It is an optional
     component, downloaded on demand when the player:
     - ticks "Enable hosting" on the first-run screen, or
     - opens Create Match / Map Creator for the first time.
   - It lands in `server\`, where the client's `find_local_server_bundle`
     already looks.
   - `build-installer.ps1 -IncludeServerInInstaller` still bundles it for
     offline handouts.
4. **Retail assets are never redistributed or referenced by our scripts.**
   When Ace of Spades was found (and `assets\original` is not already there
   from an earlier install) it runs
   `BattleSpadesAssetInstaller.exe --source "<aceofspades>" --destination "<install>\assets\original" --report <file>`.
   The importer verifies the files, copies them into `assets\original`, and
   imports `steam_api.dll`. A failure shows no message box: the launcher's
   first-run screen explains it and offers the download.
   - If the installer did not import them, the launcher's first-run screen
     offers two ways (see Auto-update):
     - **Download game assets**: the `retail_assets` component Kiril hosts;
     - **Use my Ace of Spades folder**: the same importer.
5. Steam registration (below): the Ace of Spades launch options when the
   game is installed, otherwise a non-Steam shortcut when Steam is installed.
   If Steam is running, the installer asks ONE question, **"Restart Steam
   now?"**. Yes closes Steam (`steam.exe -shutdown`, waits up to 60 s),
   registers and starts Steam again. No (or a silent install, or Steam not
   closing in time) writes `update\steam-register.pending`; the launcher
   finishes the registration silently the next time it starts while Steam is
   closed. No retry loop.
6. Shortcuts: desktop **BattleSpades**, and in the Start menu
   **BattleSpades**, **BattleSpades - restore previous version**
   (`--rollback`) and, when Ace of Spades is installed, **BattleSpades -
   choose what Steam Play starts** (`--reset-launch-choice`).

Uninstall restores Steam's settings first (offering one "Restart Steam now?" while Steam runs), then
removes installed files and `update\`. It asks before deleting player data
(settings, logs, hosted maps, `assets\original`, `steam\`) and deletes only
those named items, never the whole folder. `AoS_Screenshots` is always kept.

## Steam registration

### Option A (default): Ace of Spades launch options

The helper sets `UserLocalConfigStore/Software/Valve/Steam/apps/224540/LaunchOptions`
in `<Steam>\userdata\<account>\config\localconfig.vdf` to

```
"<install>\BattleSpadesLauncher.exe" %command%
```

Steam then runs our launcher whenever the player presses Play on Ace of Spades.
`%command%` expands to the retail `...\aceofspades\aos.exe` plus Steam's own
arguments (`+connect_lobby`, `+connect`).

**Steam Play chooser.** Started with `%command%`, the launcher first asks
"What do you want to play?" (Windows task dialog, keyboard-friendly: Enter,
arrows/Tab, Alt+B / Alt+A, Esc closes without starting anything):

- **Play BattleSpades** (default): the retail exe argument is dropped and the
  rest is forwarded to `BattleSpadesClient.exe` (invites keep working).
- **Play Ace of Spades (original)**: runs Steam's `%command%` unchanged (the
  retail `aos.exe` and every argument after it, working folder = the game
  folder), so the retail game and its community patches keep working. The
  launch options the player had *before* BattleSpades (recorded in
  `steam-registration.json`) are applied the way Steam would: a value with
  `%command%` is a template, any other value is appended.
- **Remember my choice** stores `update\launch-choice.json` and skips the
  dialog. To choose again: hold **Shift** while pressing Play, start
  `BattleSpadesLauncher.exe --choose`, or use the Start-menu entry
  **BattleSpades - choose what Steam Play starts**
  (`--reset-launch-choice`, which forgets the choice).

Either way the launcher waits for the game it started, so Steam keeps the
session, playtime and overlay. Started without `%command%` (Start menu,
desktop, non-Steam shortcut) there is no chooser: BattleSpades starts.
`BattleSpadesClient.exe` also ignores a leading retail exe
(`src/core/command_line.cpp`), so launch options pointing straight at the
client work too.

Benefits: Steam treats the session as Ace of Spades (app 224540). That gives
the overlay (it follows child processes, and the launcher waits for the
client), friends see "Playing Ace of Spades", playtime is recorded, and invites
and Join Game work because Steam hands the same launch arguments through.

Costs and risks:

- **Steam must be closed** while the file is written. Steam keeps it in memory
  and rewrites it on exit, so an edit made while it runs would be lost. The
  helper refuses (exit code 2) whenever `steam.exe` is running and the file is
  under the live Steam folder. The installer then offers to restart Steam
  once, or leaves the registration to the launcher (see above).
- Per account: it is written for the Steam account `loginusers.vdf` marks
  `MostRecent`, otherwise the newest `Timestamp`, otherwise the account whose
  `localconfig.vdf` was written last. `--all-accounts` covers every account on
  the PC. Other accounts see the retail game.
- The retail client stays one click away: the chooser's "Play Ace of Spades
  (original)". Clearing the option in Steam > Ace of Spades > Properties
  removes BattleSpades from Play entirely.
- `localconfig.vdf` is an undocumented Valve format. The editor is
  format-preserving: it splices one line and leaves every other byte
  identical, which the tests check against a fixture that has the structure of
  a real file.
- **Uninstalling Ace of Spades in Steam deletes its folder, and our subfolder
  with it** (including settings and imported assets). "Verify integrity of game
  files" does not delete extra files.

### Option B (optional): non-Steam game shortcut

The helper adds an entry to `userdata\<account>\config\shortcuts.vdf` (binary
KeyValues): `AppName` "BattleSpades", `Exe` = quoted launcher path,
`StartDir`, `icon`, `AllowOverlay 1`, and `appid` = `crc32(exe + name) | 0x80000000`,
the id Steam itself uses. Other entries are preserved byte-for-byte.

This works without owning or installing Ace of Spades, and it has the overlay.
Friends see "Playing a non-Steam game: BattleSpades", playtime is not tied to
224540, and Steam has to be restarted to show the shortcut (it only reads
shortcuts.vdf on start and rewrites it on exit, so the same "Steam must be
closed" rule applies). The client's Steam bridge still initialises as app 224540
through `steam_appid.txt` when the account owns it. The wizard pre-selects this
option only when Steam is installed and Ace of Spades is not.

### Safety net

- Before its first change to a Steam file, the helper copies the whole file to
  `%LOCALAPPDATA%\BattleSpades\steam-backup\<name>-<timestamp>.vdf`. That
  folder is outside the install, so it survives uninstall.
- Every write goes to a temp file beside the target and is renamed over it, so
  a crash cannot leave a half-written Steam file.
- `%LOCALAPPDATA%\BattleSpades\steam-registration.json` records, per file, the
  previous value (or that there was none) and the value we set.
  `unregister` restores the previous value only if the current value is still
  exactly ours. If the player has changed it since, it is left alone. A
  `shortcuts.vdf` the helper created is deleted again if nothing else was
  added to it.
- Manual use (Steam closed):

  ```
  BattleSpadesSetupHelper.exe register --launcher "<install>\BattleSpadesLauncher.exe" --launch-options [--shortcut] [--account <id> | --all-accounts]
  BattleSpadesSetupHelper.exe unregister
  ```

  `--steam-root`, `--state` and `--backup-dir` point it at a copy for testing.
  Exit codes: 0 ok, 1 error, 2 Steam running, 3 not found.


## Auto-update and on-demand bundles

### Components

| Component | Install folder | In the main build? | Version read from | Contents |
| --- | --- | --- | --- | --- |
| `client` | install root | yes | `battlespades-version.json` (also its `protocol`) | `BattleSpadesClient.exe`, launcher, helper, DLLs, shaders, localization, UI layout, asset importer, Steam bridge |
| `assets` | `assets\client\` | yes | `update\installed.json`, else `components.assets` in `battlespades-version.json` | **Our own** redistributable packs (cosmetics, UI art) |
| `server` | `server\` | no, optional | `server\VERSION` | The BattleSpades server, needed only for hosting (Create Match / Map Creator) |
| `retail_assets` | `assets\original\` | no, optional | `update\installed.json` (downloads only) | The original game files, hosted by Kiril. Installed **through `BattleSpadesAssetInstaller`**, so a download and a folder import end identical and pass the same validation |

Each component is versioned and updated on its own:
- **Installed** components are checked on every launch.
- An optional component that is not installed is **offered, never forced**.
- One component's update never touches another's folder. A package carrying
  another component's files is refused, and mirror pruning skips them.
- Retail files imported from a folder have no version and are never
  "updated".
- No package may write `assets\original`, except `retail_assets` through the
  importer.

See [Update and asset download behavior](UPDATE_DOWNLOADS.md) for transfer
granularity, HTTP Range resume, cache reuse, and keeping unchanged asset packs
out of a client-only update.

### Screens (launcher; Windows-native dialogs)

There are at most two simple screens. Both use standard Windows Task
Dialog / progress-window styling. The game's own UI is not running yet, and
the game cannot start without its files.

1. **First launch, game files missing** (`assets\original` empty). ONE
   dialog, "Get the original game files", with the best choice preselected
   (Enter does it). `plan_first_run` decides what is shown:

   | Steam copy found | Last import failed | `retail_assets` published | Choices (in order; first = preselected unless noted) |
   | --- | --- | --- | --- |
   | yes | no | yes | **Use my Ace of Spades folder** (Found: path), Download game assets, Select my folder |
   | yes | yes | yes | **Download game assets**, Select my folder, Try my folder again |
   | no | - | yes | **Download game assets**, Select my folder |
   | yes | no | no | **Use my Ace of Spades folder**, Select my folder, Get the game files from aosplay.net |
   | yes | yes | no | **Select my folder**, Try my folder again, Get the game files from aosplay.net |
   | no | - | no | **Select my folder**, Get the game files from aosplay.net |

   - A failed or interrupted download comes back as **Retry download**
     ("continues where it stopped"), preselected. Partial downloads resume
     with HTTP Range.
   - Without `retail_assets` the text says "Automatic download of the game
     files is not available yet" and links https://www.aosplay.net/download
     (the link opens the browser and the dialog stays). Offline it says the
     download servers could not be reached.
   - "Use my folder" runs the importer hidden behind a progress window.
     "Select my folder" opens the importer's folder picker.
   - After a failure the dialog shows **why and what to do**, from the
     importer's `--report` (for example "...maps/Alcatraz.vxl is missing
     (350 of 3689 required files matched). Some original game files are
     missing or changed... Verify integrity of game files..."). The importer
     run by the launcher's "Select my folder" gets `--choose-folder` and never
     suggests "Download game assets" itself; the launcher's own screen does.
   - Check box **"Also enable hosting (Create Match, Map Creator): download
     the server, 64 MB"** when the manifest has a server and none is
     installed. **Cancel** quits.
   - The downloaded pack goes through the same importer validation as a
     folder import.
2. **Progress window** (460×130 px). It shows the current step ("Downloading
   server 0.1.0 (1 of 2)...", "Unpacking…", "Installing…"), a progress bar,
   "Downloading: 37 MB of 64 MB", and one button:
   - **Play without updating** for normal updates;
   - **Quit** when a required client/assets update is pending;
   - **Cancel** for opt-in downloads.

   A large update (> `large_update_bytes`, default 200 MB) first asks
   **"Update now / Later"** and shows the size. After `max_deferrals` (3)
   Laters the update is installed before the game starts.

**Hosting in the game.** When the player starts Create Match or Map Creator,
the client checks `update\hosting.json` (written by the launcher) and whether
`server\` exists. Then, depending on the state:
- **Missing, or "update_required" with a compatible server published**: the
  client starts `BattleSpadesLauncher.exe --install-component server`, which
  shows the progress window. The game shows its normal warning line, for
  example "Hosting needs the BattleSpades server. Downloading it now (64 MB);
  start again when it finishes.", or "Server update required to host
  (protocol 169 vs 168). Downloading it now…".
- **A download is already running**: "…The download is still running; start
  again when it finishes."
- **Nothing compatible published**: "…No compatible server is available for
  download yet."

Playing and joining remote servers are never affected.

### Flow (every launch)

0. Started by Steam with `%command%`: the Steam Play chooser (above). "Play
   Ace of Spades (original)" starts the retail game right away, with no
   update check.
1. Empty `update\trash`. Roll back any component whose last apply was
   interrupted (journal state `applying`). Finish a pending Steam
   registration (`update\steam-register.pending`) if Steam is closed.
2. Fetch `https://www.aosplay.net/updates/stable.json` (or the configured
   channel/URL), with a 6 s timeout. If it is offline or unreachable, the game
   starts silently with what is installed. An invalid manifest is logged and
   ignored.
3. First-run screen, if the game files are missing (above).
4. **Plan** the installed components whose manifest version is newer
   (`plan_updates`):
   - **automatic**: small.
   - **ask**: large, with Later counted per `component@version`.
   - **mandatory**: `required`, or too many Laters.

   A version the player rolled back from is skipped unless it is required.
5. **Download** each update (`run_update_session`):
   - Mirrors are tried strictly in order. Any failure moves on to the next
     mirror: timeout, DNS, HTTP error, wrong size, or SHA-256 mismatch.
   - **Downloads resume.** An interrupted `update\download\<file>.partial` is
     continued with an HTTP `Range` request. A server that ignores ranges
     restarts the file from zero. The whole file is then hashed, and a
     mismatch deletes the partial.
   - Verified archives are kept until they have been applied.
6. **Stage and apply each component on its own.**
   - Extraction is in-process (Unicode-safe, CRC-checked, zip-slip refused).
   - Every replaced or removed file first moves to
     `update\rollback\<component>\`, with a journal.
   - A failing component restores itself and **never rolls back or blocks
     another**.
   - `retail_assets` is extracted, then handed to
     `BattleSpadesAssetInstaller.exe --source <stage> --destination <install>\assets\original --report <file>`, which validates the
     files against `asset-manifest.json` and installs them atomically.
7. If a required **client/assets/retail_assets** update failed, the launcher
   says so and quits. A failed server update never stops the game; only
   hosting is affected.
8. Write `update\hosting.json`, then start `BattleSpadesClient.exe` and wait.

**BattleSpades - restore previous version** (`--rollback`) undoes the
components changed by the most recent update session, each with its own
one-version rollback set. Log: `update\launcher.log`. A named mutex keeps two
launchers from updating at once. It also tells the client a server download
is already running.

Launcher switches:
- `--choose` (show the Steam Play chooser even if a choice is remembered)
- `--reset-launch-choice` (forget "Remember my choice", then exit)
- `--no-update`
- `--update-only` (does not start the game)
- `--rollback`
- `--install-component <name>` (opt-in download, then exit; repeatable)
- `--update-manifest <url>` (test mirror)
- `--update-api <url>` (legacy GitHub endpoint; also turns the fallback on)

### Compatibility: independent rollouts

Components roll out independently: a 0.2-beta client works with a 0.1-beta
server, and vice versa. Nothing ever refuses to install or start because of
the pairing.

- **Playing and joining remote servers** never depend on the local server.
  The protocol 168 handshake decides.
- **Hosting** (the bundled server) needs two things:
  1. The same network `protocol`. The client's comes from
     `battlespades-version.json` (`AOS_NETWORK_PROTOCOL`). The server's comes
     from the manifest entry whose version is installed; if that is unknown,
     the server is assumed compatible.
  2. The client's optional `hosting_server` minimum (e.g. `">=0.2.0"`), taken
     from the manifest entry that describes the installed client.

  Otherwise `hosting.json` says `update_required`, with a reason. A server is
  offered as the fix only if it satisfies both conditions.
- `required: true` marks a mandatory component (or `required` at the top
  level for every component).

`update\hosting.json`:

```json
{ "schema": 1, "state": "ready | not_installed | update_required",
  "installed_server": "0.1.0", "available_server": "0.2.0",
  "download_size": 67108864, "update_available": true,
  "reason": "hosting needs server >=0.2.0 (installed 0.1.0)" }
```

### `updater.json`

`updater.json` is optional and sits beside the launcher. Every key in it is
optional.

```json
{
  "enabled": true,
  "manifest_url": "",
  "channel": "stable",
  "check_timeout_ms": 6000,
  "large_update_bytes": 209715200,
  "max_deferrals": 3,
  "github_fallback": false,
  "repository": "KikoTs/BattleSpadesClient",
  "include_prereleases": false
}
```

- `channel`: any value other than `stable` reads
  `https://www.aosplay.net/updates/<channel>.json` (for example `beta`).
  `manifest_url` overrides it and must be `https://`.
- `github_fallback`: when the manifest is **unreachable**, the old GitHub
  `/releases/latest` check runs instead. It updates only the `client` with
  the legacy whole-client ZIP. GitHub ignores releases marked "Pre-release";
  set `include_prereleases` to use those.

### Update manifest: `stable.json` (schema 2)

```json
{
  "schema": 2,
  "product": "BattleSpades",
  "channel": "stable",
  "published": "2026-10-01T12:00:00Z",
  "components": {
    "client": {
      "version": "0.2.0-beta.3",
      "protocol": 168,
      "hosting_server": ">=0.1.0-beta.2",
      "package": "BattleSpades-client-0.2.0-beta.3-windows-x64.zip",
      "size": 15092466,
      "sha256": "<64 hex digits>",
      "root": "BattleSpades-client-0.2.0-beta.3-windows-x64",
      "urls": [
        "https://github.com/KikoTs/BattleSpadesClient/releases/download/v0.2.0-beta.3/BattleSpades-client-0.2.0-beta.3-windows-x64.zip",
        "https://<r2-host>/client/BattleSpades-client-0.2.0-beta.3-windows-x64.zip"
      ],
      "preserve": ["ui-layout.json"],
      "mirror_directories": ["shaders"]
    },
    "server": {
      "version": "0.1.0-beta.4",
      "protocol": 168,
      "target": "server",
      "package": "BattleSpades-server-0.1.0-beta.4-windows-x64.zip",
      "size": 63803986,
      "sha256": "<64 hex digits>",
      "root": "BattleSpades-server-0.1.0-beta.4-windows-x64",
      "urls": ["https://github.com/.../BattleSpades-server-0.1.0-beta.4-windows-x64.zip",
               "https://<r2-host>/server/BattleSpades-server-0.1.0-beta.4-windows-x64.zip"],
      "preserve": ["config.toml", "bans.json", "fleet.toml", "logs/**"],
      "mirror_directories": ["_internal"]
    },
    "assets": {
      "version": "0.2.0-beta.1",
      "target": "assets/client",
      "package": "BattleSpades-assets-0.2.0-beta.1.zip",
      "size": 171551868,
      "sha256": "<64 hex digits>",
      "root": "BattleSpades-assets-0.2.0-beta.1",
      "urls": ["https://github.com/.../BattleSpades-assets-0.2.0-beta.1.zip",
               "https://<r2-host>/assets/BattleSpades-assets-0.2.0-beta.1.zip"],
      "mirror_directories": ["cosmetics", "fonts", "png", "ui"]
    },
    "retail_assets": {
      "version": "1.0.0",
      "target": "assets/original",
      "package": "<file name Kiril chose>.zip",
      "size": 412345678,
      "sha256": "<64 hex digits>",
      "root": "<folder inside the zip holding the Ace of Spades layout>",
      "urls": ["https://<Kiril's host>/<file>.zip"]
    }
  }
}
```

| Field | Rule |
| --- | --- |
| `schema` / `product` | `2` / `BattleSpades`. |
| `required` (top level or per component) | Mandatory. A failure blocks play only for `client`, `assets` and `retail_assets`. |
| `components.<name>` | `client`, `server`, `assets` and `retail_assets` are known. Other names matching `[a-z0-9_-]` are accepted for forward compatibility. |
| `version` | Semver precedence (`0.3.0-beta.2` < `0.3.0-rc.1` < `0.3.0`). |
| `package`, `size`, `sha256` | Mandatory. Every mirror's bytes must match the size and the SHA-256. |
| `urls` | Ordered public mirrors, at least one. `https://` only (`http://127.0.0.1` / `localhost` are allowed for tests). |
| `root` | Folder inside the ZIP that maps onto `target`. If absent, it is detected: `BattleSpadesClient.exe` (client), `BattleSpades.exe` (server), or the first folder with files. |
| `target` | Defaults: client `""`, server `server`, assets `assets/client`, retail_assets `assets/original`. Components may not overlap, and none may target `update/`. |
| `preserve` / `mirror_directories` / `remove` | Relative to `target`. A mirror may not cover another component's folder, so the client can never prune `assets/original`. |
| `protocol` | Network protocol of that build. Client and server values may differ; hosting is then disabled with an update prompt. |
| `hosting_server` (client) | Minimum server for hosting, e.g. `">=0.2.0"`. The legacy `"requires": {"server": ...}` is read as the same thing. |

Unknown keys, such as a `_comment`, are ignored.

### BattleSpadesAssetInstaller on its own (macOS, Linux, Windows without the launcher)

The client starts `BattleSpadesAssetInstaller` when no verified game files
exist. Its flow mirrors the launcher's first-run screen (`plan_first_run`):

1. It looks for Ace of Spades in every Steam installation it knows
   (Windows: registry + Program Files; Linux: `~/.local/share/Steam`,
   `~/.steam/steam`, Flatpak and Snap Steam, Steam Play/Proton libraries,
   `~/.wine`, Bottles; macOS: native Steam, CrossOver and Whisky bottles,
   `~/.wine`). Each Steam root is resolved through `libraryfolders.vdf` and
   `appmanifest_224540.acf` `installdir` (Windows `C:\...` paths inside a
   Wine prefix are mapped into its `drive_c`/`dosdevices`). Only folders that
   really contain the game are proposed.
2. It reads the release manifest (`updater.json` beside it, else
   `https://www.aosplay.net/updates/stable.json`).
3. One dialog, "Get the original game files": **Use the found folder**,
   **Download game assets (N MB)** when the manifest has `retail_assets`,
   **Choose folder...**, or, without `retail_assets`, the note "Automatic
   download isn't available yet - choose your Ace of Spades folder" with
   **Open aosplay.net/download**. Nothing found is not an error: the dialog
   comes first.
4. The download (libcurl, HTTPS) resumes `<assets>/.retail-download/*.partial`
   with HTTP Range, checks size and SHA-256 (`verify_package_file`, shared
   with the launcher), extracts with the updater's ZIP reader and imports the
   result through the same `find_asset_source` + `install_asset_tree_atomic`
   as a folder. Errors keep the partial file and offer **Retry download**.
5. A picked folder may be the game folder, Steam's `common`, `steamapps`, a
   library root, a Steam root, a Wine prefix / bottle, its `drive_c`, or a
   `Bottles` folder.

Destination: `<executable>/assets/original` when that folder is writable,
otherwise the user data folder (`%LOCALAPPDATA%\BattleSpades`,
`~/Library/Application Support/BattleSpades`,
`$XDG_DATA_HOME/BattleSpades`) as `<data>/assets/original`, with
`<data>/assets/client` linked to the packaged `assets/client` (re-linked on
every start, so a moved or translocated `.app` keeps working). The client
checks the packaged root, then the user data root.

Automation: `--source <folder>` imports a folder, `--download
[--manifest-url <url>]` runs the download path without a window; both write
`--report <file>`.

### Packaging `retail_assets` (Kiril)

Build the pack from your own installation with
`installer/make-retail-assets-pack.ps1`. It never uploads anything and
refuses to write inside the repository except under `out\`:

```powershell
./installer/make-retail-assets-pack.ps1 -Version 1.0.0 -TestImport
# or: -GameDir 'D:\SteamLibrary\steamapps\common\aceofspades' -OutputDir 'D:\packs'
```

- Finds Ace of Spades through Steam (or `-GameDir`), checks every file in
  `assets/catalog/original-assets.json` for its exact size and SHA-256 (a
  modded or missing file stops it, with the list), and writes
  `<OutputDir>\BattleSpades-retail-assets-<version>.zip` (default
  `out\retail-assets\`) plus a `.sha256` file.
- Layout: one top folder (the `root`, `BattleSpades-retail-assets-<version>`)
  holding exactly the catalogued files at their relative paths, the optional
  Japanese fonts when your copy has the exact retail ones, and
  `steam_api.dll` / `steam_appid.txt`. No executables, configs, logs, mods or
  the BattleSpades subfolder.
- `-TestImport` extracts the ZIP and runs `BattleSpadesAssetInstaller.exe` on
  it (the newest one under `out\build`, an installed BattleSpades, or
  `-Importer`), the same validation the launcher runs after a download.
- Runs on a gaming PC without this repository: copy the script anywhere and
  run `powershell -ExecutionPolicy Bypass -File .\make-retail-assets-pack.ps1
  -Version 1.0.0 -PublicUrl <where you will host it> -TestImport`. The catalog
  then comes from an installed BattleSpades (`asset-manifest.json`) or the
  public repository, and the pack goes to `.\retail-assets\`.
- It prints the size, SHA-256, root and version, and the matching
  `make-update-manifest.ps1` arguments (`-RetailAssetsVersion`,
  `-RetailAssetsSize`, `-RetailAssetsSha256`, `-RetailAssetsRoot`,
  `-RetailAssetsUrls`).

The launcher downloads the ZIP (resumable, size + SHA-256 checked), extracts
it, finds `root` and passes it to the same importer a player uses for "Use my
Ace of Spades folder". Files that do not match `asset-manifest.json` are
rejected, and nothing is installed. Only the metadata goes into
`stable.json`; you host the ZIP yourself.

### Dedicated servers

`BattleSpades.exe` (server repository) reads the same manifest:
- At startup it logs `New BattleSpades server version X available (current Y)`.
  The `[updates]` keys control this.
- `BattleSpades --update` downloads and stages the server, but never replaces
  files.

See the server's `docs/ADMIN_GUIDE.md`.

### Unicode (Cyrillic and other non-ASCII) paths

The install folder, the Steam library, the user profile and every file name
may contain Cyrillic (e.g. `C:\Users\Тодор\Игры\BattleSpades`). Every path is
handled as `std::filesystem::path` (UTF-16 on Windows) and wide Win32 APIs.
Paths become UTF-8 only at text boundaries: VDF, JSON, TOML and log text.

- Steam's files are UTF-8.
- The installer reads the helper's UTF-8 report correctly: Inno Setup 6
  `LoadStringsFromFile` was checked with and without a BOM.
- ZIP extraction no longer uses `tar.exe`. It cannot open an archive below a
  Cyrillic folder ("Failed to open '…\????? ????\?????.zip'").

Tests run without the UTF-8 code-page manifest, on a machine whose ANSI code
page is 1252, so they catch any narrow conversion:
- `aos_updater_unicode_tests`
- `aos_client_unicode_tests`
- the server's `tests/test_unicode_paths.py`

The game executables also declare `activeCodePage=UTF-8`. Before that, those
narrow calls were broken only on Windows builds older than 1903, or when the
manifest is ignored.

## Building

Prerequisites:
- The native Release tree with all targets
  (`scripts/build.ps1 -Native -Profile Release`).
- The packaged server: the newest complete
  `..\BattleSpades\release-dist\BattleSpades-<v>-windows-x86_64` bundle, or
  `-ServerBundle`.

```powershell
./installer/build-installer.ps1                                   # main build installer + component packages
./installer/build-installer.ps1 -IncludeServerInInstaller         # also bundle the server into the installer
./installer/build-installer.ps1 -SkipServer                       # no server package this time
```

Output in `out/installer/`:
- `BattleSpades-Setup-<v>.exe`: the main build, without the server and
  without retail files.
- The component packages:
  - `BattleSpades-client-<v>-windows-x64.zip`
  - `BattleSpades-server-<server v>-windows-x64.zip`
  - `BattleSpades-assets-<assets v>.zip`

The script refuses a stage that contains `assets\original`.

Component versions:
- client: `AOS_RELEASE_VERSION`;
- network protocol: `AOS_NETWORK_PROTOCOL` (CMake cache, default 168);
- server: its `VERSION`;
- assets: `AOS_ASSETS_VERSION`.

Keep `-StageDir` short: ISCC is not long-path aware. `tar.exe` is used only to
*create* ZIPs, and only ever sees relative ASCII names.

Inno Setup: an installed `ISCC.exe` (or `$env:ISCC`) is used. Otherwise the
script downloads the pinned `Tools.InnoSetup` NuGet package (SHA-256 checked)
into `out/tools`, with no admin rights and no registry entries.

There is **no code signing** (Kiril declined it). Integrity rests on
HTTPS-only URLs plus the size and SHA-256 that `stable.json` states for every
file. Whoever can change `stable.json` controls what players install, so
protect the site repository and its Vercel project.

## Publishing a release (Kiril)

1. **Build.** Run `./installer/build-installer.ps1`. Only re-upload components
   whose version changed: each component rolls out on its own, and the
   manifest may keep pointing at older packages.
2. **Upload** (you do this) each new package unchanged to every mirror, using
   the same file name:
   - the GitHub release `v<version>`;
   - R2 `client/`, `server/`, `assets/`.
   Upload the retail assets ZIP to wherever you host it.
3. **Write the manifest** and check every mirror. `-Verify` sends HEAD
   requests and checks the exact size:

   ```powershell
   ./installer/make-update-manifest.ps1 `
     -ClientPackage out/installer/BattleSpades-client-0.2.0-beta.3-windows-x64.zip `
     -ServerPackage out/installer/BattleSpades-server-0.1.0-beta.4-windows-x64.zip `
     -AssetsPackage out/installer/BattleSpades-assets-0.2.0-beta.1.zip `
     -MirrorBaseUrls 'https://github.com/KikoTs/BattleSpadesClient/releases/download/{tag}', 'https://<r2-host>/{component}' `
     -Tag v0.2.0-beta.3 -Protocol 168 -HostingServer '>=0.1.0-beta.2' `
     -RetailAssetsVersion 1.0.0 -RetailAssetsUrls 'https://<your host>/<file>.zip' `
     -RetailAssetsSize 412345678 -RetailAssetsSha256 <hex> -RetailAssetsRoot '<folder in zip>' `
     -Output out/installer/stable.json -Verify
   ```

   - The `{tag}` mirror base is substituted the same for every component,
     with `-Tag` or the component's own version. Upload the server and assets
     ZIPs to the release whose tag the manifest URL names, or give them a
     mirror base without `{tag}`.
   - `-ServerProtocol` sets a server protocol that differs from the client's.
   - `-Required` makes everything mandatory; `-RequiredComponents client`
     makes only the listed components mandatory.
   - `-Channel beta -Output out/installer/beta.json` writes a test channel.
   - Without the `-RetailAssets*` options, players get only "Use my Ace of
     Spades folder".
4. **Publish.** Copy the file to
   `../aos_revival\public\updates\stable.json`, then commit and
   deploy the site.
   - Next.js serves `public/` files unchanged, ahead of the `[slug]` route.
   - Vercel's default `Cache-Control` for them is
     `public, max-age=0, must-revalidate`, and a deploy refreshes the edge
     cache.
   - The committed placeholder uses version `0.0.0`, so it never causes an
     install.
5. **Check.** Start the game once. `update\launcher.log` lists the mirror
   used and lines like `installed client 0.2.0-beta.3`, and `update\hosting.json`
   shows the hosting state.

## Tests

`ctest -R "aos_updater|aos_client_unicode"`:

- `aos_updater_manifest_tests`:
  - schema 2 parsing, including `retail_assets`, `hosting_server`, legacy
    `requires`, and differing protocols;
  - rejection cases (the client may not prune `assets/original`, among
    others);
  - constraints;
  - the planner:
    - installed components only, and optional components never forced;
    - client 0.3 + server 0.1 and client 0.1 + server 0.3, each updating on
      its own;
    - opt-in installs;
    - the Later counter;
    - `required`, where a failed required server update never blocks play;
  - hosting status: missing, ready, old server, newer server, protocol
    mismatch with no compatible offer, offline;
  - mirror order; config keys and switches.
- `aos_updater_apply_tests`: per-component apply and rollback, failed-apply
  undo, interrupted-journal recovery, foreign-component protection.
- `aos_updater_unicode_tests`, under `C:\…\Пользователи\Тодор\…`:
  - in-process ZIP extraction from and into Cyrillic folders: stored, fixed
    and dynamic deflate blocks, UTF-8 member names, directories, CRC
    mismatch, zip slip, non-ZIP input;
  - staged apply and rollback with Cyrillic file names;
  - hashing, atomic writes, backups and UTF-8 error messages;
  - Steam library detection in a Cyrillic library;
  - the launch-options round trip;
  - shortcut Exe UTF-8; command lines.
- `aos_updater_launch_tests`:
  - Steam Play chooser arguments: with `%command%` (kept verbatim, Steam's
    arguments still forwarded), without it (no chooser), `--choose`,
    `--reset-launch-choice`;
  - the decision: no `%command%` means BattleSpades whatever is remembered;
    first launch asks; remembered choice; Shift and `--choose` ask again;
  - remembering, resetting (twice), damaged choice files;
  - the original command line: unchanged, previous launch options appended,
    a `%command%` template substituted, our own launcher line never reused;
  - the first-run decision table: Steam copy found or not, import failed,
    `retail_assets` published or not, offline, download retry.
- `aos_client_unicode_tests`:
  - importer source detection, installation and verification with a Cyrillic
    Steam library;
  - regression for 0.2.1-beta.2: importing into
    `<aceofspades>\BattleSpades\assets\original`, i.e. a destination inside
    the source folder (the default install layout), re-import, refusal of a
    destination that contains the source and of a catalogued file inside the
    destination, and actionable error text;
  - server bundle discovery, custom-map validation, and UTF-8 `maps_path` in
    the child server's TOML;
  - the hosting gate decision table and reading `hosting.json` from a
    Cyrillic install.
- `aos_updater_core_tests`, `aos_updater_steam_tests`: as before.

Server repository:
- `tests/test_update_check.py`;
- `tests/test_unicode_paths.py` (config, runtime paths, VXL map, KV6 prefab,
  logs and update staging from a Cyrillic root);
- `tests/test_config_keys.py`.
