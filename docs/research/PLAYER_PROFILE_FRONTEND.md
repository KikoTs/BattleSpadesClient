# Player Profile Frontend Parity

> **Recovery/specification reference.** Preserve the measured retail behavior and its evidence. Implementation updates, old build paths, test counts and session constraints below describe their original investigation; they are not current release or deployment status. Use the [maintained documentation index](../README.md) for present operating instructions and recheck historical findings against current source.

This note records the static reconstruction of the retail Player Profile menu.
It is intentionally separate from the score-service adapter: the frontend owns
layout, controls, filtering and rendering, while an adapter converts historic
score-reason identifiers into normalized rows.

## Primary evidence

- `aoslib/scenes/frontend/playerProfileMenu.py`
- `aoslib/scenes/frontend/playerProfileListItems.py`
- `aoslib/scenes/frontend/listPanelBase.py`
- `aoslib/scenes/frontend/panelBase.py`
- `aoslib/scenes/gui/dropBoxControl.py`
- `aoslib/scenes/gui/menuOptionControl.py`
- `aoslib/scenes/main/listPanelItemBase.py`
- `aoslib/scenes/main/listPanelItemMultiColumn.py`
- `aoslib/gui.py` (`TextButton`, `SquareButton`, and `VerticalScrollBar`)
- `aoslib/images.py` and `aoslib/image.py`
- `aoslib/text.py`
- `shared/hud_constants.py`, `shared/constants.py`, and
  `shared/constants_playerprofile.py`

The asset dimensions were checked against `assets/original`. Retail's
`load_texture` multiplies by 0.6, truncates the dimensions, and then installs
Python 2 integer center anchors. The native draw rectangles therefore use the
truncated dimensions, not fractional rescaling.

## Recovered 800x600 geometry

All values below are native top-left coordinates after resolving the original
Pyglet anchors:

| Element | Rectangle |
| --- | --- |
| Small frame | `(130, 28, 540, 543)` |
| Profile content frame | `(166, 146, 464, 308)` |
| Title bounds | `(250, 11, 300, 80)` |
| Four-tab hit strip | `(152, 100, 496, 33)` |
| Player name | `(160, 147, 200, 20)` |
| K/D ratio | `(400, 147, 210, 20)` |
| Filter title | `(420, 143, 200, 30)` |
| Statistic row area | `(162, 185, 476, 260)` |
| Summary row area | `(162, 185, 476, 263)` |
| Cancel button | `(152, 492, 240, 60)` |
| Achievements button | `(405, 492, 240, 60)` |

The original code mixes coordinate conventions. In particular,
`TextButton.y` is the control's upper edge: `y=108`, `height=60` occupies retail
bottom coordinates 48 through 108, or native top-left `y=492`. Treating 108 as
the lower edge moves both bottom buttons upward by exactly 60 pixels and was
the largest visible error in the earlier approximation.

The title uses Spades at 46 pixels, tabs use Edo at 18, the player name uses
Spades at 26, rows use the 11-pixel small Standard/Edo faces, K/D uses Standard
at 11, and the two large buttons use 36-pixel Spades with dark `(20,20,20)`
text. K/D is only drawn on Player Stats and is formatted to three decimals.

## Tabs, filters, and rows

The tabs are Player Stats, Game Modes, Classes, and Equipment. Game Modes has
eleven filters, Classes nine, and Equipment two. Each dropdown adds `ALL` as
option zero.

Contrary to the previous native note, filters are **not** retained when the
user returns to a tab. `set_tab` constructs `DropBoxControl` with selected index
zero and then clears `current_filter`; the computed former-filter index is dead
code in the shipped Python. The native model now reproduces this reset.

The summary dedicates one of thirteen visible list slots to a sticky red
`CLASS / MODE` / `RANK` header, leaving twelve rank rows. Its first data row is
three pixels below the header. Other tabs show thirteen data rows. Red category
headers use the original bevel-cap textures, scalar values are left-aligned in
the second half-column, and alternating row colors advance across category
headers just as the retail list panel does. Rank progress is a dark/light green
quad with `LEVEL n` and `current / next` text; `level_bar_bg.png` is declared by
the Python class but is not used by `draw_bar`.

The dropdown has an explicit open state. Its option panel is emitted after the
profile list because the retail `DropBoxControl` is the last element and must
cover rows beneath it. The list and dropdown retain textured square buttons,
arrows, tracks, and scroll thumbs rather than text glyph approximations.

## Requests and platform effects

Opening the screen issues one generation-tagged profile request. A response
without usable profile/stats data changes `CONNECTING_PLEASE_WAIT` to
`PROFILE_NOT_FOUND`; late responses from an earlier account generation are
rejected. A successful callback returns the view to Player Stats, matching the
retail `score_manager_callback` call to `set_tab(0, False)`.

Achievements is not another frontend route. Retail calls
`SteamShowAchievements`; the native model therefore emits a
`show_achievements_overlay` effect for the platform adapter. No fake
achievements page or success state is created when that adapter is unavailable.

## Validation

`test_player_profile_menu.cpp` characterizes:

- all tab and filter orders;
- the non-persistent retail filter reset;
- dropdown open/close and every Game Modes option;
- stale profile callback rejection;
- the achievements overlay effect;
- exact frame, tab, list and bottom-button coordinates;
- recovered font sizes, summary header gap, red bevels and scrollbar assets;
- detailed level progress labels and hovered/pressed control assets.

The full native MSVC build passes with `/std:c++20 /W4 /WX`; all 23 tests pass.
