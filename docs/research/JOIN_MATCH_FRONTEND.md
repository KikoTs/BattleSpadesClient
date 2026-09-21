# Join Match Frontend Recovery

> **Recovery/specification reference.** Preserve the measured retail behavior and its evidence. Implementation updates, old build paths, test counts and session constraints below describe their original investigation; they are not current release or deployment status. Use the [maintained documentation index](../README.md) for present operating instructions and recheck historical findings against current source.

This note records the retail evidence used by the renderer-neutral Join Match
implementation. Coordinates are from the retail 800x600 bottom-left canvas;
the native models convert them to top-left eighth-pixels.

## Scene hierarchy and routes

```text
SelectMenu
└─ JoinMatchMenu                         forward slide
   ├─ Server Browser → ServerMenu        forward slide
   │  └─ selected server → LoadingMenu   forward slide
   ├─ Custom Match → MatchSquadsMenu     forward slide
   │  └─ create/join → MatchSquadLobbyMenu
   └─ Random Match → QuickPlayMenu       forward slide
      ├─ chosen server → LoadingMenu
      └─ no chosen server → JoiningGameMenu → LoadingMenu
```

Every child uses `set_menu(..., back=False)` and therefore enters from the
right. Every return to its parent supplies `back=True`, enters from the left,
and retains the outgoing child one screen-width to the right.

Primary source evidence:

- `aoslib/scenes/frontend/selectMenu.py:246-249` opens `JoinMatchMenu`.
- `joinMatchMenu.py:81-98` routes Server Browser, Random Match, Custom Match,
  and Back.
- `serverMenu.py:215-229` constructs the exact LoadingMenu expectation fields;
  `259-263` returns to Join Match.
- `quickPlayMenu.py:221-235` either opens LoadingMenu for an already selected
  server or the timed JoiningGameMenu search; `255-257` returns.
- `matchSquadsMenu.py:20-66` enters the lobby and returns to Join Match.

## Shared horizontal transition

`aoslib/scenes/frontend/menuScene.py` owns the animation for all nested menus:

- forward sets `current_x = +1`; back sets `current_x = -1` (`65-75`);
- each fixed UI update evaluates
  `current_x += (0 - current_x) / 10` (`91-97`), an exponential 0.9 decay;
- the previous menu is retained until `abs(current_x) < 0.005`;
- input is blocked until `abs(current_x) < 0.5` (`156-166`);
- the incoming menu is translated by `current_x * window.width`; the outgoing
  menu sits at `-window.width` for forward navigation and `+window.width` for
  back navigation (`137-150`).

`FrontendShellModel` already reproduces that contract. `JoinMatchPresentation`
and `ServerBrowserPresentation` expose design-only `build_layer()` outputs so
the shell can translate both retained layers over one stationary background.

## Join Match geometry

Recovered from `joinMatchMenu.py:18-69` and `shared/hud_constants.py:38-43`:

| Control | Retail `TextButton`/navbar input | Native top-left bounds |
|---|---:|---:|
| Server Browser | `(269, 434, 262, 58)` | `(269, 166, 262, 58)` |
| Custom Match | `(269, 371, 262, 58)` | `(269, 229, 262, 58)` |
| Random Match | `(269, 308, 262, 58)` | `(269, 292, 262, 58)` |
| Back navbar | `(248, 32, 78, 26)` | `(248, 542, 78, 26)` |

The frame is loaded at the default 0.6 scale. Its integer retail texture is
339x253 and resolves to `(231, 129.5, 339, 253)`. The small navbar frame is
340x68 at `(230, 520, 340, 68)`. The shared splash uses the same recovered
0.75 scene transform as Select Menu.

English localization proves `PUBLIC_MATCH = "Random Match"`,
`CUSTOM_MATCH = "Custom Match"`, and `SERVER_BROWSER = "Server Browser"`
(`aoslib/strings/english.py:979,1052,1991`).

## Server browser behavior

`serverMenu.py:36-123` defines the complete list layout:

- table `(65, 436, 435, 300)` with columns Name 103, Players 68, Map 100,
  Mode 132, Ping 45;
- Ping is the initial ascending sort column;
- network sources cycle in this exact order: All, Official, Community,
  Favourites, History, Friends, Local;
- Full Servers and Empty Servers both default enabled;
- Refresh `(288,130,120,30)`, Favourite `(417,130,113,30)`, and Connect
  `(543,153,190,54)`;
- selected map preview is a 192x192 draw at native `(542,162)`.

Behavioral evidence:

- source cycling wraps with modulo arithmetic (`164-175`);
- disabling Full hides `count == max`; disabling Empty hides `count == 0`
  (`177-201`);
- discovery deduplicates `(ip,port)`, filters monitor/wrong-version servers,
  and keeps favourite state (`335-344`, `412-429`);
- selecting a column toggles ascending/descending in `gui.ListGrid`;
- the active column draws `filter_arrow_up` for ascending and
  `filter_arrow_down` for descending (`gui.py:1204-1226`);
- `ListGrid` derives a 15-row viewport from the recovered 273-pixel content
  height and 18-pixel rows, clamps wheel/arrow scrolling to
  `visible_rows - 15`, and maps scrollbar dragging into that same bounded
  range (`gui.py:1071-1073,1156-1167,1274-1295`);
- a platform double-click connects only while a list row is under the pointer
  (`serverMenu.py:206-209`). The native model consumes the platform
  `click_count`; it does not invent a second timing threshold;
- Connect is shown only for compatible, owned content (`351-410`);
- a valid Connect hands identifier, expected map, mode ID, classic flag, skin,
  and previous-menu type to LoadingMenu (`215-229`).

The original `SERVER_REGIONS` is a Python 2 dictionary, so iteration order was
runtime-dependent. The recovered semantic set is US West, US East, Europe,
Australia. The presentation uses that stable order instead of reproducing an
undefined hash-table accident.

## Discovery boundary and stale-result safety

The model emits `ServerBrowserRefreshRequest` rather than invoking Steam or a
master server. Each request contains the selected source, the retained region
for All/Official/Community, and a generation. `accept_response()` and
`finish_refresh()` reject callbacks from older generations after a refresh,
source change, region change, or clear. This preserves retail source semantics
while preventing a delayed Internet callback from adding rows to Friends or
LAN results.

Favourites, History, Friends, and Local requests deliberately carry no region.
The region tabs render only for Official, matching `serverMenu.py:69,143-175`,
although the retained region is passed to all three Internet query families
by `serverMenu.py:287-310`.

## Loading handoff boundary

`LoadingMenu.on_start` consumes the browser's expectation fields and selects
the map/mode infographic before connecting
(`aoslib/scenes/ingame_menus/loadingMenu.py:83-112,187-210`). Loading itself is
three-part progress (`39-67`), times out safely when progress stalls
(`413-432`), waits for queued UGC prefab loading to finish (`442-445`), then
enables Start and routes through team/class selection (`451-481`).

Startup asset preloading is a separate retail layer in `aoslib/loadingscreen.py`:
it draws a 36-bullet progress screen during synchronous boot asset work. That
bootstrap and the in-match LoadingMenu should remain separate in the native
client: the former reports asset-pipeline readiness; the latter reports network
packs/map synchronization and game-scene preparation.

## Native files

- `include/battlespades/frontend/join_match_menu.hpp`
- `src/frontend/join_match_menu.cpp`
- `include/battlespades/frontend/join_match_presentation.hpp`
- `src/frontend/join_match_presentation.cpp`
- `tests/test_join_match_frontend.cpp`

Networking, Steam/master-server discovery, favourite persistence, and actual
scene changes remain outside these models. The models emit typed routes,
generation-checked refresh requests, and immutable `ServerConnectRequest`
handoffs; application services perform side effects.
