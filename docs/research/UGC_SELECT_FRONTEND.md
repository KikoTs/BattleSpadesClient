# UGC Select Frontend Recovery

This note records the retail evidence behind the renderer-neutral UGC Select
menu. Retail coordinates use an 800x600 bottom-left canvas; the native model
stores top-left coordinates in eighth-pixels.

## Scene and routes

`aoslib/scenes/frontend/ugcSelectMenu.py` defines one three-button scene:

```text
SelectMenu
└─ UGCSelectMenu                         forward slide
   ├─ Create Map   → UGCSquadsMenu       forward slide
   ├─ Publish Map  → UGCPublishMenu      forward slide
   ├─ Subscribe    → Steam Workshop page external overlay
   └─ Back         → SelectMenu          back slide
```

The source variable names are misleading: `publish_map_button` opens
`UGCSquadsMenu`, while `map_editor_lobbies_button` opens `UGCPublishMenu`.
The native API therefore exposes actions by visible behavior instead of
retaining those accidental names.

All forward actions play `menu_confirmA`; Back plays `menu_backA`. Routing,
audio playback, and opening a browser/Steam overlay remain application-owned
side effects. The model only emits a typed `UgcSelectActivation`.

## Geometry and draw order

The menu reuses the same constants and art as Join Match:

| Visible control | Retail `TextButton`/navbar | Native top-left bounds |
|---|---:|---:|
| Create Map | `(269, 434, 262, 58)` | `(269, 166, 262, 58)` |
| Publish Map | `(269, 371, 262, 58)` | `(269, 229, 262, 58)` |
| Subscribe | `(269, 308, 262, 58)` | `(269, 292, 262, 58)` |
| Back | `(248, 32, 78, 26)` | `(248, 542, 78, 26)` |

The foreground draw layer contains, in retail order, the 339x253 three-button
frame, the 340x68 navigation frame, three sliced `TextButton` controls, Back
text and icon, then the shared splash transformed by translate `(412, 510)`
and scale `0.75`. `build_layer()` intentionally excludes the background so
the shared menu compositor can translate the retained old and incoming new
screens over one stationary `ugc_splash` cover.

Retail `TextButton` activation is release-driven and does not require its own
inside press; the NavigationBar item does. The model preserves that unusual
pointer contract, strict edge-exclusive hit testing, focus navigation, the
two-pixel pressed-label offset, and disabled background-art dimming (retail
does not dim the label itself).

## Invalid-data and Workshop behavior

When `GameManager.invalid_data_error` is set, retail disables Create Map,
Publish Map, and Subscribe while leaving Back enabled. The native model does
the same and repairs keyboard/controller focus to Back immediately.

The original Ace of Spades Steam app ID is `224540`. The inspected non-Steam
compatibility tree currently defines `SPADES_GAME_APP_ID = 480 #224540`; this
is a migration override, not the retail product identity. The model defaults
to `224540`, rejects zero, and permits an application adapter to substitute an
explicit compatibility ID. Subscribe produces:

```text
http://steamcommunity.com/workshop/browse/?appid=<app-id>
```

## Native files and verification

- `include/battlespades/frontend/ugc_select_menu.hpp`
- `src/frontend/ugc_select_menu.cpp`
- `include/battlespades/frontend/ugc_select_presentation.hpp`
- `src/frontend/ugc_select_presentation.cpp`
- `tests/test_ugc_select_menu.cpp`

The focused test executable covers recovered geometry/order, typed routes and
sounds, invalid-data gating, Workshop URL ownership, pointer quirks, exact draw
composition, and invalid presentation contexts. It compiles cleanly with MSVC
C++20 `/W4 /WX` and passes all seven cases.
