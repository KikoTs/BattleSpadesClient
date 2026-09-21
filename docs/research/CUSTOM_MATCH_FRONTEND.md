# Custom Match Frontend Recovery

> **Recovery/specification reference.** Preserve the measured retail behavior and its evidence. Implementation updates, old build paths, test counts and session constraints below describe their original investigation; they are not current release or deployment status. Use the [maintained documentation index](../README.md) for present operating instructions and recheck historical findings against current source.

This note records the retail evidence behind the renderer-neutral Custom Match
lobby-list implementation. Retail coordinates use an 800x600 bottom-left
canvas; native draw geometry is the equivalent top-left canvas.

## Retail scene and actions

`joinMatchMenu.py` routes Custom Match to `MatchSquadsMenu`. The subclass sets
the Match lobby type and the `SQUAD_LIST`, `JOIN_SQUAD`, and
`AVAILABLE_SQUADS` strings (`matchSquadsMenu.py:12-17`). A successful join
opens `MatchSquadLobbyMenu` (`20-22`), while Back returns to `JoinMatchMenu`
(`64-66`).

`BaseSquadsMenu` owns the side-effecting operations:

- Open/Friends enumeration is selected by the two-row filter
  (`baseSquadsMenu.py:188-195`).
- Refresh repeats the current enumeration (`268-269`).
- Join calls the selected opaque lobby ID, then the success/error callback
  decides routing (`110-131`).
- Create calls `SteamCreateLobby` (`280-284`), and the subclass initializes
  the lobby metadata (`matchSquadsMenu.py:24-61`).

The native model emits typed refresh, join, create, and back intents. It never
calls Steam/AoSPlay, launches a server, plays audio, or changes scenes.

## Important Create-button distinction

The Match flavor deliberately does **not** draw a Create button. At
`baseSquadsMenu.py:63-68`, `lobbyType == A2664` creates one 332 px Join button;
only the other base flavor draws adjacent 162 px Join/Create buttons. Hosting
is reached through the separate Create Match flow, although the common base
still contains the Create operation. The native reconstruction therefore
keeps the visible Match screen exact and exposes `request_create()` solely as
the host-service boundary. It does not invent a fourth visible control.

## Exact visible root geometry

`ListPreviewMenuBase.draw_background` centers the large frame, then draws its
title at retail `(120,525,frame_width-190,50)`
(`listPreviewMenuBase.py:45-54`). `BaseSquadsMenu.on_start` supplies the panel
and control geometry (`baseSquadsMenu.py:50-82`):

| Element | Native top-left rectangle |
|---|---:|
| Large frame | `(25, 5, 750, 589)` |
| Title | `(120, 25, 560, 50)` |
| Back navigation item | `(54, 541, 78, 32)` |
| Lobby panel | `(56, 95, 340, 413)` |
| Open/Friends filter | `(242, 114, 130, 24)` |
| First lobby row | `(66, 155, 320, 25)` |
| Preview panel | `(401, 95, 340, 354)` |
| Join background | `(401, 452, 340, 54)` |
| Join button | `(405, 456, 332, 50)` |

The 320 px row columns reproduce `SquadListItem.draw_name`: 145 px lobby name,
25 px ping, 90 px friend count, and 25 px member count with the recovered
15 px spacing (`squadListItem.py:48-71`). Join is enabled only after preview
details exist and the selected lobby is open and not full
(`baseSquadsMenu.py:166-185`). Native additionally fails closed when content
is unowned.

## Network and lobby-root safety

Retail sends an offline user back to Select Menu. The native model retains a
renderable root with only Back active and explicitly reports Offline, Loading,
Empty, or Error. It never creates sample lobbies. Refreshes are transactional,
bounded to one in flight, deduplicate opaque lobby IDs, cap the accepted list
at 4,096 rows, and clear stale join targets on failure/disconnect.

Only the lobby-facing route distinction is modeled here. Retail owners cancel
their lobby and return to Select Menu; joining members return to the Custom
Match list (`matchSquadLobbyMenu.py:25-37`). Full chat, team assignment, match
settings, server allocation, and invite behavior remain application services.

## Native files and validation

- `include/battlespades/frontend/custom_match_menu.hpp`
- `src/frontend/custom_match_menu.cpp`
- `include/battlespades/frontend/custom_match_presentation.hpp`
- `src/frontend/custom_match_presentation.cpp`
- `tests/test_custom_match_frontend.cpp`

`build_layer()` contains only design-space commands so the shared retail slide
compositor can translate both retained screens over one stationary background.
The isolated strict MSVC build uses C++20, `/permissive-`, `/W4`, and `/WX`;
all 7 focused tests pass.
