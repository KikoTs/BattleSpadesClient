# Map Creator frontend route

> **Recovery/specification reference.** Preserve the measured retail behavior and its evidence. Implementation updates, old build paths, test counts and session constraints below describe their original investigation; they are not current release or deployment status. Use the [maintained documentation index](../README.md) for present operating instructions and recheck historical findings against current source.

The retail Map Editor button does not enter the in-game loading screen directly. The recovered route is:

1. `UGCSelectMenu.map_editor_lobbies_pressed`
2. `UGCSquadsMenu` (`BaseSquadsMenu` lobby browser)
3. `UGCSquadLobbyMenu` (`BaseSquadLobbyMenu` host/member lobby)
4. `LoadingMenu` only after the lobby owner presses Start and a game server has been found

Evidence lives in the preserved decompile:

- `aoslib/scenes/frontend/ugcSelectMenu.py`
- `aoslib/scenes/frontend/ugcSquadsMenu.py`
- `aoslib/scenes/frontend/ugcSquadLobbyMenu.py`
- `aoslib/scenes/frontend/baseSquadsMenu.py`
- `aoslib/scenes/frontend/baseSquadLobbyMenu.py`
- `aoslib/scenes/frontend/matchSettingsPanel.py`

## Lobby browser invariants

`UGCSquadsMenu` changes the shared list-preview labels to `UGC_SQUADS_MENU_TITLE`, `UGC_OPEN_LOBBIES`, `UGC_SQUADS_MENU_JOIN`, and `UGC_SQUADS_MENU_NEW_LOBBY`. It retains the Friends/Open filter, left lobby list, right preview, Back, Join, and New Lobby controls. The native model keeps discovery explicit and bounded; no placeholder lobby may be joinable. New Lobby remains locally available when an online discovery adapter is absent so the bundled editor server can still be launched.

## Host lobby invariants

`UGCSquadLobbyMenu.on_start` enables exactly these `MatchSettingsPanel` rows:

- Privacy
- Max Players
- Map
- Prefab Set
- UGC Mode
- editable UGC map title

It disables Match Length, normal Game Rules, and playlist selection. The recovered Max Players sequence is the even values 2 through 24. The UGC playlist contains nine baseplates and defaults to 12 players, `tdm`, `DesertBaseplate`, and the Desert prefab set. A title is limited to 19 characters by the retail edit box.

The native lobby owns those settings as one `UgcEditorConfiguration`. Start emits an immutable configuration and is the only effect that may enter Loading. Back returns to the UGC lobby browser, and Invite remains an explicit platform action.

## Known adapter boundaries

Steam/AoSPlay lobby discovery, friend invites, bundled editor-server launch, and joining another host are application services. The models emit typed intentions for these operations. They do not invent a successful network operation, mutate process state, or draw a loading screen before Start.
