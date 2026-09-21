# Leaderboards, Player Profile, and Loading Reconstruction

> **Recovery/specification reference.** Preserve the measured retail behavior and its evidence. Implementation updates, old build paths, test counts and session constraints below describe their original investigation; they are not current release or deployment status. Use the [maintained documentation index](../README.md) for present operating instructions and recheck historical findings against current source.

This note records the retail behavior recovered for the Leaderboard, Player
Profile, boot splash, and match-loading surfaces. It separates confirmed
retail behavior from intentional native-client hardening.

## Evidence

Primary decompiled sources:

- `aoslib/scenes/frontend/LeaderboardMenu.py`
- `aoslib/scenes/frontend/leaderboardListPanel.py`
- `aoslib/scenes/frontend/playerProfileMenu.py`
- `aoslib/scenes/frontend/playerProfileListItems.py`
- `aoslib/scenes/ingame_menus/loadingMenu.py`
- `aoslib/loadingscreen.py`
- `aoslib/images.py`, `aoslib/image.py`, `aoslib/audio.py`, and
  `aoslib/models.py`
- `shared/hud_constants.py`, `shared/constants.py`, and
  `shared/constants_gamemode.py`

The original assets in `assets/original` confirm the source dimensions and
loader recipes used by those modules.

## Leaderboard

`LeaderboardMenu` is not a web view. It is a native 800x600 menu containing:

- a 1328x921 `ui_frame_leaderboard` texture loaded at 0.6 and centered at
  (400, 300). The loader truncates the scaled size to 796x552 before
  installing Python 2 integer anchors, yielding `(2, 24, 796, 552)`;
- an uppercase title in the 300x80 area beginning at top-left `(250, 20)`;
- a 300x20 type selector at top-left `(30, 110)`;
- a 200x20 scope selector at top-left `(340, 110)`;
- a 740x350 grid beginning at top-left `(30, 135)`;
- a Back control at top-left `(30, 525, 100, 30)`.

There are ten type filters in fixed order: General, TDM, VIP, Territory
Control, Occupation, Diamond Mine, CTF, Zombie, Demolition, and Multihill.
Each has a different recovered column set. Scope is Global, Local, or Friends.

Results are cached independently for all 30 type/scope pairs. An uncached
selection clears the grid, displays `CONNECTING_PLEASE_WAIT`, disables both
selectors, and emits one score-service request. A cached selection is shown
without another request. Late callbacks must be generation checked in the new
client because the Python implementation relied on UI locking alone.

The grid declares fourteen visible slots, but its custom panel consumes one
slot for the sortable header, leaving thirteen visible player rows. Rank and
Name stay fixed while excess score columns use a horizontal scrollbar. The
first Name sort is ascending; the first numeric sort is descending.

Native implementation:

- `LeaderboardMenuModel` owns cache, selection, requests, scrolling, and sort.
- `LeaderboardPresentation` emits the renderer-neutral Classic-profile draw
  list.
- An endpoint adapter should translate aosplay/score-service JSON into
  `LeaderboardRow`; network code never mutates menu state directly.

## Player Profile

The profile surface uses the centered 900x906 small frame at scale 0.6 and the
centered 774x514 `player_profile_stats_bg` texture. Its four tabs are fixed:

1. Player Stats
2. Game Modes
3. Classes
4. Equipment

The Game Modes tab has eleven filters: General, TDM, VIP, Occupation,
Territory Control, Diamond Mine, CTF, Zombie, Demolition, Multihill, and Hours
Played. Classes has Soldier, Scout, Engineer, Miner, Specialist, Medic,
Gangster, Classic, and Zombie. Equipment has Weapon Accuracy and Weapon
Points. Index zero in each drop-down is the synthetic `ALL` option. Returning
to a tab resets its filter to `ALL`: retail creates the replacement
`DropBoxControl` with index zero and then clears the tab's `current_filter`.

The request is made once when the screen opens. Missing `profile` or `stats`
data changes the center copy from `CONNECTING_PLEASE_WAIT` to
`PROFILE_NOT_FOUND`. The summary includes name, K/D ratio formatted to three
decimal places, and rank rows for classes and modes. Other tabs contain
category headers, scalar values, and optional rank-progress bars.

Native implementation:

- `PlayerProfileMenuModel` owns the account-generation request boundary,
  per-tab filters, rows, and scrolling.
- `PlayerProfilePresentation` owns only the recovered geometry and draw order.
- `PlayerProfileData` intentionally accepts normalized rows. Translating raw
  historic score-reason IDs belongs in the score-service adapter, not UI code.
- Exact coordinate, list, dropdown, font, and achievements-overlay evidence is
  recorded in `PLAYER_PROFILE_FRONTEND.md`.

## Boot splash and preload lifecycle

The retail boot splash is distinct from the match loader. It draws:

- `ugc_splash.png` as the background;
- `title_and_copyright.png` over a 1280x960 logical surface;
- 36 dark progress bullets starting at `(316, 166.5)` in that 1280x960 space;
- the corresponding number of bright bullets over them.

Retail progress is `floor(progress_calls * 36 / 975)`. Every image/model load
increments the global counter and every third audio buffer increments it. At
shutdown it warns if the count is not exactly 975. This is presentation parity,
not a safe modern loading contract: adding or removing an asset makes progress
wrong and synchronous loading can stall event processing.

`PreloadService` replaces the call counter with an explicit weighted manifest:

1. A worker claims a bounded decode job and retains its payload by token.
2. The worker completes decode; the decoded backlog is bounded.
3. The render/audio thread claims uploads in manifest order and creates the
   runtime resource.
4. Required failures finish the batch as Failed. Optional failures finish as
   Ready With Warnings. No job or completion is silently dropped.

The queue has explicit maximum manifest size, decode concurrency, and decoded
backlog. Tokens contain batch generation and ordinal, so completions from a
cancelled or replaced load are rejected. Progress is completed weight divided
by total weight. `boot_loading_snapshot` projects that truthful value back onto
the retail 36-bullet presentation.

The service deliberately owns no file decoder, bgfx handle, OpenAL buffer, or
worker thread. Those are platform/runtime adapters. This keeps GPU creation on
the render thread and permits the same scheduler to preload textures, fonts,
audio, KV6 models, shaders, and map data.

## Match loading

The retail `LoadingMenu` uses the large frame, map art, mode infographic,
loading-bar frame, and up to three tabs. Normal matches show Map, Mode, and
Scores. Training shows Map only. Map Creator shows Map and Mode.

Packet/status transitions are:

- connect: `CONNECTING_TO_SERVER`;
- InitialInfo: `CHECKING_MAP` and select official/default map and mode art;
- pack transfer: `RECEIVING_SERVER_PACKS`;
- MapDataStart: `RECEIVING_MAP`;
- MapSyncStart: `SYNCING_MAP`;
- MapSyncEnd: `INITIALISING_MAP`, skin expansion, and UGC prefab loading;
- all prefabs/resources ready: `MAP_READY`, then enable Start.

The retail progress formula divides loading into three equal parts. It also
times out after 30 seconds without progress. Map/Mode/Scores tabs rotate every
3 seconds until the player clicks, scrolls, or presses a key, after which
automatic cycling stops.

`MatchLoadingModel` retains those status, timeout, tab, and official-art rules.
Its third progress segment is driven by the explicit preload manifest instead
of pretending that `MapSyncEnd` means every texture/model is ready. Start is
enabled only after required resources have uploaded. This is the key guarantee
needed to avoid first-use frame spikes when gameplay rendering arrives.

Official map image aliases (including both Chicago capitalizations and the
historic Mount Rushmoore/Mayan Jungle alias), Classic CTF art, Mafia VIP/TC
art, Tutorial, UGC, and the unknown-map fallback are encoded in
`select_loading_textures` and covered by tests.

`BootLoadingPresentation` and `MatchLoadingPresentation` turn these immutable
snapshots into renderer-neutral draw lists. The boot list retains the original
1280x960 bullet coordinates projected onto the contained 800x600 design
surface; the match list retains the large-frame, loading-bar, tab, official
art, status, and Start-control geometry.

## Validation

The isolated components compile with MSVC 19.51 under `/std:c++20 /W4 /WX`.
The characterization executables cover:

- all leaderboard types/columns, cache locking, stale callbacks, and sorting;
- all profile tabs/filters, per-tab memory, stale account callbacks, and draw
  geometry;
- manifest rejection, queue bounds, out-of-order worker completion,
  deterministic upload order, duplicate completion, weighted progress,
  optional failure, and all 36 boot bullets;
- official/fallback map art, Classic/Mafia infographic selection, three-phase
  progress, Start gating, and the 30-second timeout.

The component tests are intentionally not added to CMake in this changeset;
the frontend integration owner can wire the new sources and tests atomically
with the route/controller work.
