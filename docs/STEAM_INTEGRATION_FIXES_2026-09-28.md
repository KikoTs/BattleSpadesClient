# Steam integration fixes — 2026-09-28

Report: "Steam invite and Join through the Steam button weren't working — the
game doesn't register itself as a joinable session, Join Game in Steam doesn't
open the game and connect; APIs are not connected; Sign in through Steam was
sometimes visible and sometimes not, even when running through Steam."

Two Steam runtimes run side by side, both attached as Ace of Spades (224540):

| Runtime | Process | Owns |
| --- | --- | --- |
| `SteamNetworkingRuntime` (`src/platform/steam_networking.cpp`) | the game, `steam_api64.dll`, manual dispatch on a pump thread | relay transport, presence, lobbies, invites, achievements |
| `NativeSteamClient` + `BattleSpadesSteamBridge32.exe` (`steam_bridge/`) | 32-bit child, retail 2013 `steam_api.dll` | Steam sign-in identity, retail session tickets, retail stats |

Rich presence belongs to the account and application, not the process, so the
two share (and used to fight over) one set of keys.

## Root causes

### Join Game / invites

1. **Wrong connect value.** Presence published `connect = steam:<id>`. Steam
   appends the connect value verbatim to the command line of a friend whose game
   is closed; the parser only knew `+connect X`, so `aos.exe steam:7656…` failed
   with "unknown option" and the game exited instead of joining.
2. **Presence overwritten with a loopback address.** After every successful
   transport start the frontend also told the bridge
   `set_server_presence(host:port)` with the *dialled* endpoint. For a Steam
   join and for the player's own hosted match that is the loopback tunnel
   (`127.0.0.1:<port>`). Same application, same keys: a friend's Join Game
   dialled `+connect 127.0.0.1:NNNNN`, i.e. their own machine.
3. **Join requests to a running game were discarded.** The in-process runtime
   uses manual callback dispatch and only handled connection-status and
   call-result messages; `GameRichPresenceJoinRequested_t` (337),
   `GameLobbyJoinRequested_t` (333) and `NewUrlLaunchParameters_t` were freed
   unread. The bridge registered no callbacks at all.
4. `+connect_lobby <id>` (lobby invite accepted while closed) was an unknown
   option, and `steam://run/224540//…` launch parameters were never read.
5. A `--connect steam:<id>` launch opened the Steam tunnel synchronously in
   `start()` — up to 15 s relay wait + 30 s host hello before the first frame.
6. Leaving a dedicated-server match never cleared presence, so friends kept a
   Join that led into a match the player had left.

### Sign in through Steam flicker

1. **Stale Steam registration (this machine, confirmed).** Every steam_api
   decides "is Steam running" from `HKCU\Software\Valve\Steam\ActiveProcess\pid`.
   Here it named pid 77404, which had exited, while Steam ran as 39080 (HKCU
   `SteamPath` also pointed into `AceOfSpades_no_steam_new\Steam`: the no-Steam
   retail client's emulator rewrote both). Both runtimes then fail
   `SteamAPI_Init` until Steam restarts — it depends on whether the emulator ran
   since Steam last started, hence "sometimes". Measured with the new bridge:
   `Steam is running (pid 39080) but its registration names pid 77404, which has
   exited…`. The client now says exactly that instead of "start Steam"; it never
   writes the registry (repairing it is Steam's job — **restart Steam**).
2. **2-second handshake.** `NativeSteamClient::start()` blocked the boot for at
   most `response_timeout` (2 s) waiting for READY, covering bridge launch +
   `SteamAPI_Init` + stats request. A Steam still starting, or the overlay
   injecting into the child, exceeded it on some launches: button gone for the
   session, never retried.
3. **No app id outside Steam.** The bridge relied on `SteamAppId` being
   inherited (only when Steam launched the game); the imported `steam/win32`
   directory has no `steam_appid.txt`. A direct launch never attached.
4. **Protocol pipe shared with the DLL.** The bridge's stdout (and stderr) *was*
   the response pipe; the retail DLL `printf`s breakpad/minidump notices
   (`Setting breakpad minidump AppID = …`). Any such line arriving first failed
   the handshake as "malformed".
5. **Visibility sampled once.** `set_steam_available(ready())` ran only at
   identity bootstrap; nothing refreshed it afterwards.

### "APIs are not connected"

No `TODO`/stub bodies exist; the gaps were unwired features: no join callbacks
(either runtime), no `InviteUserToGame`/overlay invite, no launch-parameter
read, no lobby-invite resolution, bridge presence clears that wiped the other
runtime's keys on exit, bridge failure never retried.

## Fixes

- `platform/steam_connect.{hpp,cpp}` (new): one parser for every Steam join
  form (`+connect steam:<id>`, `+connect host:port`, `+connect_lobby <id>`, bare
  `steam:<id>` from older builds, launch command lines), connect-string
  builders (`+connect steam:<id>`; `+connect host:port` refusing loopback /
  unspecified / port 0) and a 5 s de-duplicator (both runtimes may report one
  click).
- Presence: hosts and Steam joiners publish `+connect steam:<host>` plus
  `steam_player_group=<host>`; dedicated servers publish `+connect host:port`
  only when public, through `publish_dedicated_presence()` — one writer (the
  in-process runtime; the bridge only in builds without it). Cleared when the
  match connection is retired (never a live host's). `steam_display` is not
  used: it needs localisation tokens on the retail app's Steamworks page.
- In-process runtime: handles 337/333/`NewUrlLaunchParameters_t` in the
  dispatch loop; `take_join_requests()`, `launch_command_line()`,
  `resolve_lobby_connect()` (JoinLobby → read `connect`, fall back to the
  owner → leave), `overlay_enabled()`, `open_invite_dialog()`
  (`ActivateGameOverlayInviteDialogConnectString`, lobby dialog fallback). New
  flat bindings are optional, so an older `steam_api64` still carries matches.
  Its init failure now includes the registration diagnosis.
- Bridge: `SteamAppId/SteamGameId=224540` set before loading the DLL; stdout
  and stderr redirected to NUL before `LoadLibrary`, protocol written to a
  private handle; `SteamAPI_IsSteamRunning` checked first with a precise
  diagnosis; exit code 6 = permanent (bad DLL), 3 = transient; registers
  337/333 through `SteamAPI_RegisterCallback` with an ABI-identical
  `CCallbackBase` **before** the first `RunCallbacks`; new `SUBSCRIBE`
  (events only after it, so old clients are unaffected) and `PRESENCE_KEY`
  commands; clears presence on exit only if it published any. Fully backward
  compatible with older clients.
- `NativeSteamClient`: non-blocking `begin_start()` + per-frame `poll()`
  state machine (`starting / ready / retrying / unavailable`), 20 s startup
  bound, automatic retry every 10 s after transient failures (Steam started
  or restarted later → button appears without restarting the game), a dead
  bridge is detected and restarted, noise lines ignored, join events queued.
- Identity screen: `IdentitySteamState` (`hidden / connecting / available`).
  The button shows **CONNECTING TO STEAM...** (disabled) during the first
  attempt, **SIGN IN THROUGH STEAM** once ready, and hides only when Steam is
  genuinely unavailable; retries never flash it. `reset_form`, `set_busy`,
  `set_error` no longer override it. Synced every frame.
- Frontend `pump_steam_integration()` (every tick): polls the bridge, syncs the
  button, collects joins from both runtimes, resolves lobby invites on a
  worker, re-attaches the in-process runtime every 30 s while Steam is absent,
  and performs a queued join once the player is signed in — leaving a match or
  sub-menu first (route exit hooks tear the match down). Startup `+connect`,
  bare `steam:<id>`, `+connect_lobby` and Steam launch parameters all go
  through the same queue (no more 45 s blocked first frame).
- Invites: Steam's friends-list **Invite to Game** works whenever presence has
  `connect` (it now always does in a joinable match). `open_steam_invite_dialog()`
  opens the overlay picker; wired into Create Match → Invite Friends when
  AoSPlay is unreachable (the AoSPlay friends screen is the social agent's).
- No `SteamAPI_RestartAppIfNecessary`: the client deliberately also runs
  outside Steam (AoSPlay accounts, dev builds); both runtimes set the app id
  themselves instead.

## Verification done

- `out/build/steam` (native-dev, VS 18, `/W4 /WX`): full build clean, ctest 151/151 passed.
- `aos_steam_connect_tests` (new): parser/builder/dedup cases, plus a fake
  bridge (`tests/fake_steam_bridge.cpp`) driving the real `NativeSteamClient`:
  breakpad noise before READY and between a command and its answer, a 2.5 s
  Steam (over the old 2 s bound), transient failure → retry → ready, permanent
  failure → unavailable, JOIN/LOBBY events after SUBSCRIBE.
- `aos_identity_menu_tests`: pending/available/hidden button across
  `reset_form`, `set_busy`, `set_error`.
- `aos_core_tests`: `+connect steam:<id>`, bare `steam:<id>`, `+connect_lobby`.
- New bridge built with `/W4 /WX` (x86) and run against the retail DLL on this
  machine: returns the stale-registration diagnosis above (live Steam attach
  was impossible here without restarting Steam, which was not done).

## Verify with two real Steam accounts (A hosts, B joins)

Prerequisites: both machines run this build installed as `aos.exe` in the
Steam `aceofspades` folder (so Steam launches it), `steam_api64.dll` and
`BattleSpadesSteamBridge32.exe` beside it, retail runtime imported. On any
machine that ever ran the no-Steam retail client, **restart Steam** first (or
check the log for "registration names pid"). Each client logs `[steam]` lines.

1. **Sign-in button.** Launch from Steam. Expect "CONNECTING TO STEAM..." then
   "SIGN IN THROUGH STEAM" within a few seconds, every launch. Quit Steam,
   launch the game: button hidden, log `retail runtime will retry: …`. Start
   Steam: the button appears within ~10 s without restarting the game.
2. **Presence.** A: Create Match → host. Log: `presence published: Hosting …
   (+connect steam:<A>)`. On B's friends list A shows "Hosting <map, mode>" and
   a **Join Game** entry. A joining a public dedicated server shows
   `+connect <public ip>:<port>` — never `127.0.0.1`.
3. **Join Game, B running.** B at the Select Menu (or in another match): right-
   click A → Join Game. B log: `join requested through Steam: +connect steam:<A>`
   → `join queued` → tunnel → loader. From inside a match B is taken to the
   menu first, then joins.
4. **Join Game, B closed.** Quit B. Join Game on A from the Steam client. Steam
   launches `aos.exe +connect steam:<A>`; B signs in (or auto-refreshes) and
   joins without menu navigation.
5. **Invite.** A in the match: Shift+Tab → friends → B → Invite to Game (or
   Create Match → Invite Friends with AoSPlay offline, which opens the overlay
   picker). B accepts in Steam chat: running → callback path (3); closed →
   launch path (4).
6. **Lobby invite.** Invite B to A's lobby from the overlay's lobby dialog; B
   accepts. Log: `lobby join requested` → `lobby <id> leads to +connect
   steam:<A>` → join. Closed B launches with `+connect_lobby <id>` and does the
   same after sign-in.
7. **Leave.** B leaves the match: A's list shows B without Join Game. A stops
   hosting: A's Join Game disappears.
8. Two clients on one machine cannot test this: Steam refuses a P2P connection
   to your own account.
