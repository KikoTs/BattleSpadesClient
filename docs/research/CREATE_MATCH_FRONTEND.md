# Create Match frontend recovery

> **Recovery/specification reference.** Preserve the measured retail behavior and its evidence. Implementation updates, old build paths, test counts and session constraints below describe their original investigation; they are not current release or deployment status. Use the [maintained documentation index](../README.md) for present operating instructions and recheck historical findings against current source.

This note records the retail evidence used by the renderer-neutral Create Match reconstruction. Coordinates below are expressed in the new client's top-left 800x600 canvas; retail Python used a bottom-left origin.

## Recovered route and panel hierarchy

`SelectMenu.squad_play_pressed()` creates a Steam lobby, and its success callback opens `MatchSquadLobbyMenu` (`selectMenu.py:281,287-290`). A lobby owner starts on `PANEL_SETTINGS`; a non-owner starts on `PANEL_PREVIEW` (`baseSquadLobbyMenu.py:153-154`). The normal Create Match branch contains these right-side panels:

- Match Settings (root owner panel)
- Choose Game Mode (`PANEL_PLAYLISTS`)
- Maps (`PANEL_MAPS`)
- Rules (`PANEL_RULES`)
- Game Info preview

The two additional panel types in `BaseSquadLobbyMenu`—UGC Mode and Prefab Sets—are enabled by the separate UGC lobby subclass, not ordinary Create Match. This implementation therefore does not expose them from the normal branch.

The left player list is retail `(56,505,340,270)` and the right preview is `(401,505,340,355)` in bottom-left coordinates (`baseSquadLobbyMenu.py:170,175`). Their top-left forms are `(56,95,340,270)` and `(401,95,340,355)`. Editable owner panels use `x=400`, `y=505`, `width=340`, `height=355` (`baseSquadLobbyMenu.py:183-196`).

Nested panel confirmation does not discard changes. `on_confirm()` hides the current panel and restores the previous panel, or Game Info when confirming Match Settings (`baseSquadLobbyMenu.py:791-807`). Mode, map, and rules links hide the current panel before showing their destination (`baseSquadLobbyMenu.py:809-833`). `CreateMatchMenuModel` mirrors that stack with forward/backward typed effects; the shared transition layer owns visual interpolation.

## Transition evidence

Retail scene navigation starts a forward transition at `current_x=+1` and a back transition at `current_x=-1` (`menuScene.py:65-74`). It interpolates toward zero with normalization 10 (`menuScene.py:91-97`). During forward travel the old menu is one window to the left; during back travel it is one window to the right (`menuScene.py:140-148`). In other words, a newly pushed menu enters from the right and a popped menu enters from the left.

Create Match's mode/map/rule views are panels inside one Match Lobby scene in the shipped client, so their original swap is immediate. The reconstructed model still labels their semantic depth as forward/backward, allowing the modern shared animator to give nested panels the same directional language requested for the rest of the frontend without duplicating timing inside this feature.

## Match Settings rows and controls

`MatchSettingsPanel.populate_match_settings_list()` establishes this exact ordinary-match order (`matchSettingsPanel.py:212-230`):

1. Privacy — stepped choice: Invite, Friends, Open
2. Mode — menu link to Choose Game Mode
3. Max Players — stepped values `2,4,...,24`
4. Match Length — stepped values `5,10,...,60,90` minutes
5. Map — menu link to Maps
6. Game Rules — menu link to Rules, displaying Default or Defined

The ordinary branch explicitly enables those six controls and disables UGC Mode, UGC map-title editing, and Prefab Set (`matchSettingsPanel.py:35-43`). Rows are 32 pixels high with a 2-pixel gap; the Defaults strip is 30 pixels high (`matchSettingsPanel.py:59-71`, `shared/hud_constants.py:34-35`).

The lobby-creation callback selects TDM, 12 players, TDM's 15-minute duration, and its first sorted map. That resolves to AncientEgypt. Privacy's retail default is Open (`DEFAULT_MATCH_SETTINGS` and accessibility constants in `matchSettings.py:12-16`, `constants_matchmaking.py:543-554`).

## Root Match Lobby composition

The lobby is not a generic settings page. `ListPreviewMenuBase.draw_background()` places `large_frame` at `(400,300)` and draws the title in `(120,525,560,50)` (`listPreviewMenuBase.py:45-54`). At the asset's recovered 0.64 scale, those become the top-left rectangles `(25,5,750,589)` and `(120,25,560,50)`.

`BaseSquadLobbyMenu.on_start()` and `__initialise_buttons()` establish the rest of the owner layout (`baseSquadLobbyMenu.py:111-127,165-205`):

- player list `(56,95,340,270)`, with its inner header `(66,105,320,40)`;
- editable 19-character lobby name `(76,111,210,30)` and Invite `(300,109,80,30)` inside that header;
- team/player count strip `(56,367,340,20)`;
- shared squad chat `(56,390,340,116)`, including the `Chat...` input at `(63,477,325,24)` (`squadChatLog.py:36-64`);
- right action background `(401,452,340,54)` and Start/Confirm `(405,456,332,50)`;
- large navigation bar `(54,541,695,32)`, whose owner label is `Leave Lobby` rather than a large ordinary button.

The local owner is always present in a successfully-created retail lobby. `refresh_name()` formats its initial header as `PLAYER_SQUAD`, whose English value is `{0}'s Lobby` (`baseSquadLobbyMenu.py:1024-1041`, `strings/english.py:643`). The native integration supplies the local player through `set_players()`; the renderer-neutral model derives that initial header but continues to accept an explicit validated lobby name.

Player rows retain the 25-pixel `SquadFriendListItem` subdivisions: 120 pixels for the name, 15 for the leader icon, 42 for `[In Game]`, and 100 for the team dropdown (`squadFriendListItem.py:18-22,103-128`). The count strip renders Team 1, Neutral, Team 2, and total-player indicators exactly in the order used by `draw_player_count()` (`baseSquadLobbyMenu.py:859-913`).

The Start button is the retail constant-glow variant (`baseSquadLobbyMenu.py:115-119`), backed by `button_large_start_*`; Confirm and Invite use ordinary button slices. Invite now emits `CreateMatchPlatformActionEffect::invite_friends`, keeping the Steam/platform overlay outside the model. A platform adapter may consume this effect without granting the model direct Steam access.

The previous reconstruction put Start/Confirm at `y=406`, Defaults at `y=385`, shortened chat to 60 pixels, used a small fake outer frame, and rendered Leave Lobby as a large button. Those were coordinate-conversion mistakes: `TextButton.y` is the top edge in retail's bottom-left space, so `y=144,height=50` converts to top-left `y=456`, not `406` (`gui.py:533-635`).

Widget hit behavior is also recovered rather than treating an entire settings row as a button. `SliderOption` reacts only through its left/right squares and clamps at the first/last value (`gui.py:1738-1837`). `MenuOptionControl` opens only from its right edit square (`menuOptionControl.py:12-78`). Pointer tests now lock both invariants; keyboard/gamepad semantic actions remain available independently.

## Game modes and maps

`PlayListUIManager` excludes multi-mode lists when used by Create Match, excludes Tutorial, and excludes UGC from non-UGC lobbies (`playlistUIManager.py:55,78`). It sorts the remaining rows by localized display name (`playlistUIManager.py:75-89`). In English the result is:

1. Capture the Flag
2. Classic CTF
3. Demolition!
4. Diamond Mine!
5. Multi-Hill!
6. Occupation!
7. Team Deathmatch!
8. Territory Control
9. VIP
10. Zombie!

Playlist IDs, mode keys, family flags, compatible maps, player suggestions, config overrides, and durations come directly from `playlists/*.txt`, `playlists/__init__.py`, and `matchSettings.get_default_match_length_for_playlist_in_minutes()` (`matchSettings.py:131-140`). `CreateMatchModeDefinition` preserves retail playlist IDs because they are lobby wire metadata.

Maps are grouped beneath the expandable Standard, Classic (`A2362`), or Mafia Pack category exactly as `MapsPanel.populate_playlist()` builds its categories (`mapsPanel.py:114-165`). Changing mode repairs an incompatible map and changes Match Length only when it still equals the previous playlist's default, matching `PlayListUIManager.update_lobby_data()`.

## Game Rules

The complete ordinary Create Match catalog is recovered from:

- category membership: `shared/constants_matchmaking.py:16-129`
- allowed value tables: `shared/constants_matchmaking.py:131-151`
- rule defaults and class restrictions: `shared/constants_matchmaking.py:180-442`
- playlist overrides: `playlists/*.txt`

There are 98 distinct rules reachable through the category table. Rules renders the active mode category first, followed by General, Classes, Weapons, and Equipment (`gameRulesPanel.py:193-227`). Every category starts expanded and uses a 26-pixel category row. Boolean rules use a checkbox; enumerated rules use the stepped slider/arrow control (`gameRulesPanel.py:214-221`).

Class-dependent weapons and equipment remain visible but disabled when all owning enabled classes are disabled (`gameRulesPanel.py:110-127,142-157`). Content unavailable to the selected family is omitted: Classic CTF exposes Classic Soldier equipment, Mafia modes expose Gangster equipment, and standard modes expose the normal six-class roster. The legacy Rocketeer class is not in `DEFAULT_TEAM_CLASSES`, so its class rule and two Rocketeer-only jetpack rows are absent in this retail menu despite remaining in the shared constants.

Classic CTF reuses the CTF wire rule IDs but overrides Shoot With Intel to On and Intel Auto Return to Off. The model stores each wire rule once and projects it beneath the Classic CTF category; this avoids divergent duplicate state.

Defaults on Match Settings restores the default playlist and all six primary rows. Defaults inside Rules clears explicit overrides and resolves values from the selected playlist. This follows `MatchSettingsPanel.reset_to_defaults()` and `GameRulesPanel.on_defaults_button_clicked()`.

## Native interfaces and validation

- `create_match_menu.hpp/.cpp` — catalog, transactional local lobby metadata, nested route effects, keyboard/pointer focus, retail control hit targets, expandable lists, class-aware rules, scrolling, player list, lobby-name validation, and typed platform actions.
- `create_match_presentation.hpp/.cpp` — translates the complete lobby, chat, team counts, settings, and action strips to the shared `DrawList`; no renderer, SDL, Steam, network, or server dependency.
- `test_create_match_menu.cpp` — eight focused characterization tests covering catalog completeness, exact lobby composition/defaults, route direction, mode/map repair, categories/scrolling, class-dependent rules, semantic input, retail pointer hit targets and clamping, typed Invite dispatch, lobby data, and DrawList validation.

Standalone MSVC validation command (the project build normally wires these sources through CMake):

```powershell
cl /std:c++20 /EHsc /W4 /permissive- /Iinclude `
  tests/test_create_match_menu.cpp `
  src/frontend/create_match_menu.cpp `
  src/frontend/create_match_presentation.cpp `
  src/ui/draw_list.cpp src/ui/geometry.cpp
```

The current suite contains eight focused characterization tests. In addition to the original catalog, routing, rule, scrolling, and DrawList coverage, it locks the complete lobby geometry, retail slider/menu-link pointer hit targets, non-wrapping slider ends, and typed Invite dispatch.

Result: `8/8` Create Match tests and `23/23` project tests passed after the complete native target compiled under strict MSVC `/W4 /WX` settings.
