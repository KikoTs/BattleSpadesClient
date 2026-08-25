# Retail Quick Play reconstruction

## Scope and evidence

The renderer-neutral implementation is based on these preserved retail sources:

- `aoslib/scenes/frontend/quickPlayMenu.py`
- `listPreviewMenuBase.py`, `listPanelBase.py`, `columnsListPanel.py`
- `previewPanelBase.py`, `playlistUIManager.py`
- `aoslib/scenes/main/playlistItem.py`
- the thirteen shipped `playlists/*.txt` definitions and `playlists/mapinfo.py`

Retail calls `SteamGetInternetServerList(SERVERMODE_PUBLIC, ...)` directly. The
native model deliberately replaces that dependency with a typed,
generation-checked `QuickPlaySearchIntent`; it makes no network call itself.

## Recovered behavior

- Playlist files receive IDs in sorted filename order. Tutorial (ID 10) and
  UGC (ID 11) are excluded. The remaining eleven rows are localized/sorted,
  with Random (ID 1) explicitly promoted to row zero.
- The initial row is Random. A playlist selection updates its mode letterbox,
  map rotation, default rules, Start/Buy visibility, and matchmaking ID.
- Territory Control (ID 8) and VIP (ID 12) require the Mafia content pack.
  Retail permits selecting an unowned row but replaces Start with Buy.
- Refresh clears all prior response, ping, player, and chosen-server data and
  disables itself until completion. Native generations reject late callbacks.
- Full servers never become a Quick Play target. Candidate priority is:
  populated within three times the playlist's best ping, empty within that
  threshold, populated outside it, then empty outside it. Retail uniformly
  chooses within the first non-empty bucket after each response.
- Start goes directly to LoadingMenu when a server is chosen, retaining
  identifier, name, map, mode, classic flag, and texture skin. With no chosen
  server it opens JoiningGameMenu using the selected playlist ID.
- Retail `TextButton` arms on any delivered press and activates when release is
  over the button. NavigationBar Back requires both press and release over Back.
- The menu uses the 800x600 canvas: two `340x354` panels at `(56,505)` and
  `(401,505)` in bottom-left retail coordinates, `332x50` Refresh/Start buttons
  at top coordinate `144`, an 11-row `24px` playlist table, and the centered
  `0.64`-scale large frame.

## Native boundaries

- `QuickPlayMenuModel` owns only deterministic playlist/discovery/input state.
- `QuickPlaySearchIntent`, `QuickPlayDirectStartIntent`,
  `QuickPlayPlaylistStartIntent`, `QuickPlayBuyIntent`, and
  `QuickPlayBackIntent` are the application adapter boundary.
- No discovery adapter is the explicit `unavailable` state: Refresh and Start
  fail closed, while selection, Buy, and Back remain safe.
- `QuickPlayPresentation::build_layer()` contains design-space commands only,
  so it can be translated by the shared retail slide compositor. `build()` adds
  exactly one stationary window-space background.

## Verification

`tests/test_quick_play_frontend.cpp` was compiled with MSVC C++20,
`/permissive- /W4 /WX`, and passed 6/6 tests. Coverage includes the playlist
catalog, ownership, offline state, generation safety, server-choice priority,
both Start paths, pointer activation, Back, geometry, slide composition, mode
art, and the presence of every declared preserved asset.

