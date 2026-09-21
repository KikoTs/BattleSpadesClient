# In-game HUD — recovered specification

> **Recovery/specification reference.** Preserve the measured retail behavior and its evidence. Implementation updates, old build paths, test counts and session constraints below describe their original investigation; they are not current release or deployment status. Use the [maintained documentation index](README.md) for present operating instructions and recheck historical findings against current source.

## 2026-08-21: jetpack fuel is a 100-unit protocol value

Windows `hud.pyd` `HUD.draw_jetpack_hud` (`0x10096C10`) divides
`character.jetpack_fuel` by the active jetpack profile's
`JETPACK_MAX_FUEL`, then clamps the result to 0..1.  All four shipping
profiles define a maximum of 100 in `shared/constants.py`.  Protocol 168 and
the BattleSpades server therefore correctly carry raw 0..100 fuel; only the
presentation boundary normalizes it.  Passing raw fuel directly to the
fraction setter kept the native gauge full until the final unit.  Native now
normalizes exactly once, and `Restock(69,type=6)` restores 100 raw units before
the next authoritative WorldUpdate.

## 2026-08-21: roster, leaderboard and ViewGameStats baseline parity

The last visible drift in the leaderboard, TAB roster and end-of-match screen
shared one porting error. Retail's `draw_text_with_size_validation` scales the
already-cached glyph geometry only when text exceeds its width, and its direct
row calls keep a fixed FTGL baseline. Native was rerasterizing a smaller font,
considering rectangle height during fitting, and vertically re-centering every
row. That made Tuffy/Cyrillic names jump, allowed the zero-height leaderboard
loading label to collapse to one pixel, and floated result/footer text by a few
pixels.

Native now separates the source paths precisely:

* leaderboard helper text uses cached-glyph `retail_width_scale`; the direct
  `settings_font.draw(..., 400, 300, center=True)` loading label uses a fixed
  baseline and no fit boundary;
* `draw_player_list` headings, headers, names, scores and pings use their exact
  source baselines. Hiding a team score also takes retail's wider 180px team-name
  branch and shifted TEAM2 origin;
* `draw_game_stats` award names/labels use direct 506/522/538 baselines;
* ViewGameStats team headings, scores, instruction footer and rank-up labels use
  the ascender/descender metric-span center from
  `draw_text_with_alignment_and_size_validation`, while its mode/result labels
  remain direct baselines.

The 800x600 evidence is retained under
`out/evidence/ui-baseline-fix-20260821/`. Focused presentation tests and the full
100-test client suite pass under `/W4 /WX`.

## 2026-08-21: authored ViewGameStats world-camera pan

The result screen previously advanced only its small stock screenshot while
the live 3D view stayed attached to the player/death camera. The shipping
client does more. macOS `gameScene.so` `GameScene.set_screenshot_camera`
(`0x1AE7F0`) activates `PAN_CAMERA`, constructs the authored VXL point, calls
`pitch_yaw_to_direction_vector(radians(rotation[0]),
radians(rotation[1]))`, and uses `PanController.move_and_rotate`,
`set_start_location`, `set_end_location`, and `set_lerp_time`. Constants
`A2498/A2499` resolve to `SCREENSHOT_CAMERA_PAN_DISTANCE=-5.0` and
`SCREENSHOT_CAMERA_PAN_TIME=5.0`. `PanController.update_pan` is the ordinary
linear `start + (end-start)*(current/lerp)` path.

Native now resolves the same fixed-rotation, five-block reverse pan from the
paired StateData camera arrays and gives it priority over player/death cameras
while ViewGameStats is visible. The first-person tool is suppressed because
the active controller is no longer the character camera. Retail camera yaw is
converted to the native VXL basis with `native_yaw=-retail_yaw-90`; pitch and
roll remain authored degrees. Non-finite, unpaired, out-of-range cameras and
the revival server's historical exact `(0,0,0)/(0,0,0)` placeholder fail
closed instead of snapping the view to the map corner.

StateData's `has_map_ended` branch now also uses the same terminal overlay
boundary as packet 52: it closes unfinished text input while preserving chat
history, the active GenericVotingHUD ballot, retained awards, and rank data.
The camera resolver is characterized at start/midpoint/end, angle conversion,
time clamping, placeholder rejection, malformed values, and array bounds in
`tests/test_match_overlays.cpp`.

## 2026-08-21: terminal overlay stack survives MapEnded

The production rollover path still differed from the isolated UI oracle:
`MapEnded(52)` cleared `GenericVotingModel`, deleting an active or just-closed
map ballot even though retail freezes the existing `GameScene` and continues
to composite GenericVotingHUD and chat above ViewGameStats. Native now applies
one explicit terminal-overlay boundary: ViewGameStats is activated/preserved,
an unfinished chat edit is cancelled, retained chat lines remain, and the vote
model keeps its candidates/result until its normal timer or scene teardown.
The client therefore retains the same end-screen layering in a real map change
that the `--debug-ui endgame` oracle already demonstrates.

## 2026-08-21: MapEnded preserves the live result scene

The production UI oracle exposed a lifecycle difference that geometry tests
could not catch.  `MapEnded(52)` previously cleared `MatchResultsModel`
immediately, so a real server rollover could erase an otherwise correct
ViewGameStats frame during the disconnect/reconnect gap.  The preserved
`LoadingMenu.start_pressed` path instead treats both
`state_data.has_map_ended` and `game_statistics_active` as reasons to enter
`show_game_statistics(True)` before retiring the old scene.

Native now preserves an already-visible result at packet 52, activates the
retained packet-67/66/73 batch when packet 53 was missed, and applies the same
rule to a late bootstrap whose StateData tail already has `has_map_ended` set.
Only map-scene teardown clears the awards, result selector, rank animation and
winner.  This keeps the real end-game leaderboard intact while the standalone
client waits for the next Protocol 168 handshake.

## 2026-08-21: live result lifecycle and stock screenshot aliases

`ViewGameStats.draw` places the mode and result strings through direct
baseline-oriented `Font.draw` calls at retail bottom-left `(400,180)` and
`(400,165)`. They are therefore native top-left baselines `(400,420)` and
`(400,435)`, not height-bearing rectangles to be vertically centered or
shrunk. The native presentation now uses zero-width centered baseline commands
with `TextFit::none`, matching `center=True` without inventing a fit boundary.

`GameStats(67)` is retained before `ShowGameStats(53)`, but BattleSpades uses
the same packet without packet 53 for a safe in-place round restart. The two
authoritative `SetScore(85, TEAM)` zero updates are the observable boundary in
that path. Native now discards only an unshown result batch after both team
resets arrive; a visible `ViewGameStats` remains owned by `MapEnded(52)` and is
not dismissed by ordinary score traffic. This prevents awards from an earlier
same-map round appearing during a later real map rollover.

InitialInfo map names are not consistent across public hosts. End-screen
camera lookup resolves compact stock stems (`MayanJungle`, `LunarBase`,
`TheColosseum`) through the official map catalog before constructing the
retail asset path. `20thCenturyTown` is the one non-mechanical exception: its
authored screenshots are `WW10.png` through `WW13.png`. Unknown UGC names
remain literal and path traversal is rejected, so compatibility aliases cannot
silently substitute unrelated stock art.

The camera preview is intentionally a mixed-coordinate exception. Retail
`ViewGameStats.draw_hud` derives its bottom-left Y from `window.height`, so the
197x158 frame and 176x135 image remain in raw window pixels at `(10,10)` and
`(20,22)` after top-left conversion. Only the main result content uses the
800x600 authored canvas. Scaling the preview with that canvas made it visibly
oversized at 720p and above.

The live `https://www.aosplay.net/leaderboard` contract was also rechecked on
this date. Its ten published presets and positional row format still match
`aosplay_scores.cpp`; the UI therefore keeps the recovered native table rather
than introducing a web-view-only path.

## 2026-08-21: ViewGameStats authored camera preview

Windows `hud.pyd` `ViewGameStats.draw_hud` (`0x1006E180`, source lines
262-275) draws a second frame independently from the main result content. When
GameScene has exactly the two StateData camera arrays and at least one loaded
map image, `screenshot_frame` is blitted at retail `(10, window.height -
frame.height - 10)` and the selected `level_screenshots/<map><index>` image is
blitted ten pixels inward and eleven pixels above the frame bottom. After the
global 0.64 load scale and integer truncation, the native 800x600 top-left
rectangles are `(10,10,197,158)` and `(20,22,176,135)`.

The Protocol 168 StateData decoder now retains both bounded camera arrays in
semantic X/Y/Z order instead of skipping them, and the index advances after
the recovered strict five-second interval. The stock asset spelling exception
`City Of Chicago` -> `City of Chicago` is preserved. A camera from a UGC map
does not imply that a stock screenshot exists; native checks the resolved file
before submitting the sprite so a missing optional image cannot terminate the
render frame.

The graphical gates `--debug-ui leaderboard`, `--debug-ui scoreboard`,
`--debug-ui endgame`, `--debug-ui chat`, and `--debug-ui vote` open the
production presentations with packet-shaped offline fixtures. They never
connect to a server and are mutually exclusive with `--debug-vfx`. The
scoreboard and endgame fixtures distinguish the TAB ViewScores screen from
ViewGameStats and freeze the latter at a visible rank-up frame for
deterministic 800x600 captures. Chat and vote exercise the live overlay stack,
including segmented player labels, active input, safe localized vote decoding,
and the F1/F2/F3 candidate rows.

The production capture also exposed a renderer defect outside the presentation
models: filtered loading of retail's uniform `png/high/white.png` selected a
single-channel texture path, so solid tinted quads retained red but lost green
and blue. The renderer now routes that asset to its startup-created RGBA8 white
pixel for every sampling request. Combined with the executable-verified
`blend_color(a,b,f) = int(a*(1-f)+b*f)` weights, ViewScores and ViewGameStats
again use the stock dark blue/green roster stripes rather than red or saturated
team-colour panels.

## 2026-08-21: packet 72/73 ViewScores message surface

Windows `hud.pyd` `ViewScores.set_message` at `0x10061280` sets
`draw_text_box=True`. Its draw path at `0x1005FBD0` resolves
`global_images.score_text_frame`, a 1098x45 source loaded at global scale 0.64,
and blits it centre-anchored at retail bottom-left `(400,75)`. It then draws
the uppercase authored message with `score_text_font` (Spades/ALDO 14) centred
at `(400,70)`. The same function draws the mode title with `title_font` centred
at `(400,463)`. In native top-left 800x600 design coordinates these become the
exact baselines `(400,530)` and `(400,137)`; the score frame itself is
`(49,511,702,28)`. These are direct baseline draws with no fit-to-box shrink.

`ForceShowScores(72)` owns forced visibility independently of the local TAB
hold. `ShowTextMessage(73)` owns the nine-entry end-result selector; its wire
duration is retained for diagnostics but the shipping GameScene does not pass
that value into the HUD setter. One resolver is shared by ViewScores and
ViewGameStats so team substitution and draw wording cannot diverge.

Every `GameStats(67)` award also retains the packet-selected wire team. The
blue/green result columns therefore remain authoritative even if a roster row
is stale or its player ID has already been reused.

## 2026-08-21: GameStats team-list semantics and end-screen layering

Windows `gameScene.pyd` `process_packet_game_stats` implementation
`0x102479C0` branches on packet 67 `team_id` and appends each decoded record to
`game_stats_team1` or `game_stats_team2`. The field is therefore an award-list
selector, not a result winner. This agrees with `aosprotocol.1x.md`, which
describes it as the team to which the stats apply.

Native retains both bounded lists until `ShowGameStats(53)`. The winning team
comes from the final `SetScore`/StateData team-score snapshot; equal scores are
a draw. The compatibility fallback for a historical one-packet BattleSpades
stream is deliberately disabled whenever both official team lists were
observed.

Retail composition paints the authored result frame and team columns before
the result and mode labels. The native list now preserves that layer order.
Live player-name projections and the UGC settings overlay are also excluded
while ViewGameStats is visible; chat, GenericVotingHUD map votes, and the TAB
scoreboard remain later overlays exactly because they are still interactive at
the end of a match.

## 2026-08-09: shared retail menu text-centering correction

The leaderboard audit exposed a renderer-wide porting trap. Retail menus call
`draw_text_with_alignment_and_size_validation(..., alignment_y='center')`,
whose `text.py:558-563` implementation centers the FTGL ascender plus negative
descender span. Native `VerticalTextAlignment::center` instead centers the
font line height, including leading, so otherwise correct text appeared to
float by a few pixels inside buttons, dropdowns, tabs and rows.

All source-backed front-end presentations now opt into
`VerticalTextAlignment::retail_center`: main menu, Join Match/server browser,
Quick Play, Create/Custom Match, identity, loading, class/team selection,
pause, settings and resolution confirmation, profile, and the UGC select,
editor, loadout and publish screens. Debug-only overlays and gameplay HUD
commands were deliberately excluded; each gameplay widget has its own recovered
baseline contract and is audited separately. Main-menu characterization pins
all six retail text commands to the metric-span branch.

## 2026-08-09: Leaderboard text-baseline and name-fitting correction

The retail leaderboard does not vertically center text from the font's line
height. `aoslib/text.py:537-573` computes its baseline as
`y + height/2 - (ascender + descender)/2` in bottom-left coordinates; FTGL's
descender is negative. In native top-left coordinates the exact equivalent is
`top + height/2 + (ascender - abs(descender))/2`. A dedicated
`VerticalTextAlignment::retail_center` now evaluates that formula from the
actual rasterized font metrics. Leaderboard title, dropdowns, list headers,
rows and navigation use it, so font leading can no longer make short rows look
like their text is floating. The connecting label is a separate source path:
`settings_font.draw(..., 400, 300, center=True)` uses baseline y=300 directly.

`LeaderboardListPanel.populate` also calls
`modify_name_to_fix_width(name, 143, small_standard_ui_font)` before ordinary
column drawing. That helper removes one Unicode character at a time and tests
the prefix plus three literal periods. Native presentation now performs the
same UTF-8-safe truncation with the shipped 11px A750 rasterizer before its
normal 122px row-interior fit. This replaces the previous visually smaller,
unellipsized long names.

Authoritative files: `aoslib/text.py:537-573,356-369`,
`aoslib/scenes/frontend/leaderboardListPanel.py:41-47`,
`leaderboardListItem.py:24-43`, `listPanelItemBase.py:158-176`, and
`LeaderboardMenu.py:draw`.

## 2026-08-09: LeaderboardListPanel row interaction correction

Retail `LeaderboardMenu.on_start` creates a `LeaderboardListPanel` but never
attaches an item-selected callback. A player-row click therefore stays inside
the table: `ListPanelBase.on_mouse_release` selects the row, plays
`menu_scrollA`, and draws the inherited green highlight. It does **not** open
`PlayerProfileMenu`. The previous native click-to-profile route was fabricated
and has been removed.

The recovered row draw order is also explicit in
`ListPanelItemMultiColumn.draw`: segmented alternating background, full-width
`hovered_colour == (175,172,161,255)`, selected `green_highlight_line` and
`green_highlight_glow`, then column text. The selection glow expands by 5% of
row width and 30% of row height. Selection and hover stay attached to their row
object across header sorting, while a type/scope refresh recreates the rows and
clears both flags. Native model, rendering, hit testing, and regression tests
now preserve those behaviors.

Authoritative files: `aoslib/scenes/frontend/LeaderboardMenu.py`,
`leaderboardListPanel.py`, `aoslib/scenes/frontend/listPanelBase.py`,
`aoslib/scenes/main/listPanelItemBase.py`, and
`listPanelItemMultiColumn.py` in `aceofspades_revival`.

## 2026-08-09: ScoreLine baseline and FTGL outline correction

`ScoreLine.draw` in the shipped macOS `hud.so` (`0x107de0`) calls
`Font.draw_multi_stroke(colored_text_runs, stroke_rgba, 0, 0)` after the HUD
has translated to `(window.width/2+30, window.height/2-20)`. The optional
`center` argument is omitted and resolves false in `Font.draw_multi_stroke`
(`font.so` `0x3dbc9..0x3dc1c`), so the score feed is intentionally left-aligned
to the right of the crosshair rather than centered or animated.

The helper performs exactly two draws on one font baseline: a black
`resize_font(stroke=True)` pass and the normal colored fill. The stroke path is
the same recovered FTTextureFont outside outline used by kill names:
`FT_Stroker_Set(140, ROUND, ROUND, 0)` followed by
`FT_Glyph_StrokeBorder(..., outside, destroy_original)`. It is not the separate
eight-offset `Label.draw_stroked` helper from `text.py`.

Native ScoreLine commands now use `VerticalTextAlignment::baseline` directly.
At 800x600 their command baselines are `(430,320)`, `(430,352)`, `(430,379)`,
while the visible normal glyph tops remain the measured `(430,305)`,
`(430,340)`, `(430,367)`. Regression coverage proves the two-command
outline/fill order, shared baseline, left alignment, delayed row pitch, and the
retail `int(127*fade^2)` / `int(127*fade)` alpha curves.

## 2026-08-09: team-list height, stripe order, and blend parity

The three callers of `aoslib.scenes.ingame_menus.draw_player_list` share the
313px width but not the list height. `selectTeam.py` supplies
`(y=469,height=277)`, `changeTeam.py` supplies `(443,263)`, and the macOS
`ViewScores.draw` binary constructs `(416,263)` for both teams. The latter is
visible at `hud.so` `0xfe56c..0xfe63b` and `0xfe72f..0xfe814`, including the
complete x/score-x tuples. Because `draw_player_list` computes
`list_y_size = height / 16.0`, SelectTeam rows are 17.3125px while ChangeTeam
and ViewScores rows are 16.4375px. ViewScores' first top-left row remains y=219
and the complete sixteen-row body ends exactly at y=482.

The loop's header is index zero. Its first player row is consequently odd and
uses `LIST_COLOR2=(35,35,35)`; the next uses `LIST_COLOR1=(10,10,10)`.
`shared.common.blend_color(a,b,f)` evaluates `int(a*(1-f)+b*f)`, truncating each
channel. This was confirmed by executing the shipped `shared.common` extension,
not inferred from the call sites. With `f=0.2` and the stock blue UI team colour,
those rows are exactly `(36,51,63)` and `(16,31,43)`. Reversing the weights made
the native scoreboard far too bright. Tests: `tests/test_pause_menu.cpp`.

## 2026-08-09: shared big-text renderer and local VIP banner

Retail does not have separate approximations for packet-50 `CHAT_BIG` and the
local VIP notice. Both labels pass through `aoslib.text.draw_big_text`, which
wraps against `window.width` with a 60px reserve, sizes the backing frame to
the widest shaped line plus 40px, and stretches its authored 58px height once
per source line. Empty lines retain that vertical frame scale but are removed
from the `frame_y = y - 19.5 * line_count + 10` positioning count. The helper
then invokes `Label.draw_shadowed`: gray `(64,64,64,alpha)` at source offset
`(+2,-2)`, followed by the foreground. In the native top-origin draw list the
shadow therefore appears at `(+2,+2)`.

`HUD.draw_healthbar` renders the persistent local VIP notice when
`player.high_minimap_visibility` is true. It sets the label to
`strings.VIP_YOU_ARE_VIP` (`"You are a V.I.P! Stay safe!"` in English),
replaces `BIG_TEXT_COLOR` with the active team's exact `StateData` colour, and
calls `draw_big_text(vip_text, window.width / 2.0, 30 + vip_text_offset,
window)`. The recovered `vip_text_offset` is `60.0`, so the bottom-origin
anchor is `(W/2, 90)`. This branch is independent of
`scene.manager.enable_player_score`; hiding SCORE and the class portrait must
not hide the VIP notice.

Evidence: retail `aoslib/text.py:584-608`, `Label.draw_shadowed:773-784`,
`aoslib/strings/english.py:166`, Windows `hud.pyd` `HUD.draw_healthbar`
`0x1009CE03..0x1009D3BF`, and the `vip_text_offset` initializer containing
double `60.0`. Tests: `tests/test_hud_layout.cpp` pins CHAT_BIG shadow order,
both exact draw rectangles/colours, the VIP frame at `(W/2,90)`, team tint,
Edo 30 typography, and independence from the score/portrait flag.

## 2026-08-09: GenericVotingHUD exact text and candidate layout

The voting HUD now reproduces the complete source helper chain rather than
using a generic centered-label approximation.

* Dimensions are `(x=10, y=0, width=300, height=200)`. At draw time retail
  adds `window.height / 2 - height / 2` to its bottom-origin Y.
* The title uses `draw_text_with_size_validation(title, x, title_y, width-10,
  70, hc_font)` with a centered baseline at `title_y + 70/3`.
* Description and CLOSED-result text use
  `get_resized_font_and_formatted_text_to_fit_boundaries` followed by
  `draw_text_lines(..., horizontal_alignment='center')`, a 4px line-spacing
  argument, and `allow_word_breaking=False`.
* Candidate baselines start at
  `description_y + (candidate_count-1)*30 - 115` and decrease by 30. The
  candidate span is 240px, yielding x=40. While voting is permitted, its key
  occupies x=40/width=25 and candidate text moves to x=90; otherwise candidate
  text starts at x=40. Candidate width is 150. Vote counts intentionally use
  x=`width-50` (250), not `self.x + width - 50`, and width 25.
* Key, candidate, and count calls pass `center_text=False`. The selected count
  tuple is green `(0,255,0,255)`; every other cached tuple used here is white.
* The source wrapping test is strictly `content_width + 10 < width`. A line
  over the height budget shrinks the font one pixel at a time. All lines then
  share the minimum width scale and advance by
  `(ascender + raw_negative_descender) * scale + line_spacing*2`. The native
  rasterizer stores descender magnitude, so that source expression maps to
  `ascender - descender_magnitude`.
* At 1280x720, the recovered first three candidate baselines are y=385, 415,
  and 445 in native top-origin space. The CLOSED result panel is
  `(15,325,290,80)` and its first text baseline is `(15,405)` with width 295.

Evidence: `genericVotingHUD.py`, `aoslib/text.py`, macOS `hud.so`
`GenericVotingHUD.draw`, cached-constant initialization for tuples 84-87, and
the Cython string table entry resolving `__pyx_n_s_79` to
`horizontal_alignment`. Tests: `tests/test_match_overlays.cpp`.

## 2026-08-09: retail player-chat label segmentation

`HUD.create_line` does not colour a complete `"name: message"` string as one
label. It creates a sender-prefix label (`"name: "`) in the player's team
colour followed immediately by a second message label. Global-chat bodies are
white. Team-chat bodies use `shared.common.blend_color(team, white, 0.4)`,
including its per-channel integer truncation. The native presentation now
retains these runs and advances the body by the shaped A750-Sans-Medium prefix
width; plain server/system messages remain one label.

The third value stored with each retail run is the sender's
`CreatePlayer.local_language`. `ChatLine.draw` chooses `self.tuffy_font` for a
truthy `language_requires_tuffy` result and `self.font` otherwise. The source
helper maps Russian, Polish, and Turkish (wire ordinals 6, 7, and 8) to Tuffy.
Native chat now carries that packet field into the retained line and uses
`fonts/Tuffy_Bold.ttf` for both sender and body, including shaped-width
measurement. The Cython string-table entry for the sender format resolves to
the exact five-byte literal `%s: `, confirming the retained trailing space.

Evidence: macOS retail `HUD.create_line` at `0x9c0a0`, `HUD.add_chat` at
`0x9b8f0`, and `ChatLine.draw` at `0x10a800` in the `retail_hud_mac2` IDA
database; `__pyx_kp_s_226` resolves through the string table at `0x1e5fe8` to
`0x1dce96` (`%s: `). This corrects the earlier native approximation that
coloured both sender and body with the team colour and always selected A750.

## 2026-08-09: LeaderboardMenu control and scale correction

The leaderboard was not yet source-equivalent even though its outer frame and
table coordinates were characterized. `LeaderboardMenu.py` constructs two
real `DropBoxControl` instances; clicking a title opens a 10-row type list or
3-row scope list and transfers focus between them. The previous native screen
cycled the value on every click. The model and input path now retain one
focused dropdown, draw its black/alternating-row panel above the grid, preserve
the selected row's `highlight_line` plus `highlight_glow`, and dispatch a score
request only after an option is selected.

Further source corrections:

* `title_font`, `navigation_font`, and dropdown `medium_aldo_ui_font` all map to
  `ALDO_FONT == "Spades"`, not Edo. The title is 46px, navigation is 24px,
  and the 20px dropdown chooses 14px.
* `settings_font` supplies the 18px connecting message and remains Edo.
* Red header caps and sort arrows are loaded with
  `global_images.global_scale == 0.64` and integer truncation: 25x25 caps and
  8x7 arrows. Their recovered translated bounds are preserved.
* The scrollbar bevel textures load at 0.6 and are then scaled from 16px to a
  20px runtime bar, producing 3.75px bevels and a 7px minimum thumb. The old
  native 20px vertical caps and 40px minimum were fabricated.
* Disabled `SquareButton` images use `(86,86,86,255)` while the dropdown text
  remains `MENU_FONT_COLOR`; disabling a selector does not grey its text.

Authoritative files: `aoslib/scenes/frontend/LeaderboardMenu.py`,
`leaderboardListPanel.py`, `leaderboardListItem.py`, `listPanelBase.py`,
`aoslib/scenes/gui/dropBoxControl.py`, `menuOptionControl.py`, `aoslib/gui.py`,
`aoslib/images.py`, `aoslib/image.py`, `aoslib/text.py`, and
`shared/hud_constants.py` in `aceofspades_revival`.

## 2026-08-08: retail chat and GenericVotingHUD recovery

The former native chat and voting layouts were visual approximations. This
pass re-derived both paths from the shipped Python/Cython client and its native
`aoslib.draw.draw_quad` implementation.

### Chat

* `CHAT_SHOWN_LINES = 10`, `CHAT_BUFFER_SIZE = 50`, `MESSAGE_TTL = 5`,
  `MSG_LEFT_MARGIN = 12`, `MSG_BOTTOM_MARGIN = 60`, and `MSG_PAD = 5`.
* `HUD.add_message` inserts at index zero. The newest message uses the
  bottom-left baseline `(12, 65)`. A direct probe of the retail Python 2 font
  wrapper returns `chat_font.get_line_height() == 13`, so older messages rise
  by 13px plus 10px padding, for a 23px pitch. The earlier 15/25 measurement
  was inferred from nominal font size and is refuted.
* `ChatLine.draw` uses `A750-Sans-Medium` at 12px. Its recovered opacity is
  `min(ttl / MESSAGE_TTL * 2 + extra_alpha, 1)`: messages stay solid for 2.5
  seconds, then fade for 2.5 seconds.
* Retail `draw_stroked` renders eight 1px dark-grey neighbours in the source
  order, then the foreground. The native renderer converts the bottom-left Y
  offsets exactly once.
* Active input has two independent stroked labels: the input at bottom 15 and
  `Global chat:` or `Team chat:` at bottom 40. Retail has no black input panel,
  uppercase prefix, combined line, or underscore cursor.

### Kill feed baseline and measured row height

* `HUD.draw` translates the first retained row to `(12, height - 75)` and
  `KillLine.draw_text(..., y=0)` reaches FTGL's baseline-oriented draw path.
  Native commands therefore use an exact top-origin baseline of `y=75`; they
  do not approximate that point as a top-aligned 14px rectangle.
* `KillLine` inherits `ChatLine.content_height`. The same direct retail probe
  returns 13px for that base height. Iconless kills therefore advance by
  `13 + MSG_PAD * 2 = 23` pixels. A 330px kill icon still produces the shipped
  half-height quirk `330 * 0.1 * 0.5 = 16.5`, hence a 26.5px row advance.
* Retail kill names use `Font.draw_multi_stroke`: one outlined glyph pass and
  one normal fill pass. The macOS native font path proves that
  `get_font(..., stroke=True)` sets the `FTTextureFont` stroke flag, after which
  `FTTextureGlyph` calls `FT_Get_Glyph`, `FT_Stroker_Set(140, ROUND, ROUND, 0)`,
  `FT_Glyph_StrokeBorder(..., outside, destroy_original)`, and
  `FT_Glyph_To_Bitmap(..., NORMAL)`. Native now follows that exact 140/64px
  outside-stroke path and keys outline bitmaps separately from normal glyphs;
  the old eight-translated-copy approximation has been removed from kill names.

### GenericVotingHUD

* `set_dimensions` is `(10, 0, 300, 200)` and draw-time Y is vertically
  centered from the live window height.
* The retained panel is black alpha 100 and the title, description, key,
  candidate and count all use `hc_font` (`Spades`, 26px). Keys and counts keep
  their source brackets; candidate rows have no invented backing rectangles.
* `aoslib.draw.draw_quad` was independently decompiled and confirmed to accept
  `(x, y, width, height, color[, texture])`, removing the earlier coordinate
  ambiguity.
* `GameScene.send_cast_generic_vote_message` (Mac `0x1252c0`, source lines
  3132-3145) first calls `on_vote_cast(index, hide_after_vote, 5.0)`, then gets
  the selected candidate. A valid cast disables `can_vote` only when
  `allow_vote_changing` is false and sends the untouched candidate record.
  Local input does **not** invent a result message. `hide_after_vote` schedules
  list dismissal after five seconds rather than hiding it immediately.
* `process_packet_generic_vote_message` (Mac `0x126710`) owns packet-state
  transitions. START resets the HUD, sets text/candidates/permission, shows it,
  and retains `hide_after_vote`/`allow_revote`. UPDATE calls only
  `set_candidates_to_vote`, which replaces rows and clears both the local voted
  index and `result_text`; it does not replay START flags or force visibility.
  CLOSED disables voting, calls `show(False)`, then displays the packet title
  through `show_voted_candidate_message(6.0, title)`.
* `GenericVotingHUD.update` (Mac `0x125820`) gives the six-second result timer
  priority over the delayed-hide timer and uses strict `>` expiration checks.
  The 290x80 result branch is therefore server-authored, never post-cast UI.
* `decode_string` (Mac `0x1281f0`, `genericVotingHUD.py:54-74`) parses
  `(identifier, arguments)`, supports the nested
  `((identifier, localized_argument_indexes), arguments)` form, resolves the
  selected arguments, and runs `format(*arguments)`. The native port mirrors
  that grammar with a bounded non-evaluating parser, Python escape handling,
  brace formatting, and exact English values from `aoslib/strings/english.py`.
* Candidate retention is capped at the three rows the retail HUD and F1/F2/F3
  input path can address. Malformed printable legacy text remains inert rather
  than reaching the retail client's `literal_eval`/tuple-index crash.

Focused tests pin ordering, TTL/fade, all chat anchors, stroke order, fonts,
absence of fabricated panels, vote panel geometry/alpha, bracket formatting,
START/UPDATE/CLOSED ownership, both strict timers, dynamic/nested localization,
and exact wire-token preservation.

For the packet-by-packet ownership of each displayed value, read
[`HUD_AUTHORITY_MATRIX.md`](HUD_AUTHORITY_MATRIX.md). It separates direct
server fields from deterministic client mirrors (notably ammo and blocks) and
presentation-only derivation.

Status: **PARTIALLY RECOVERED.** The layout and asset topics are solid; the
minimap and gamemode topics are not. Read §0 before trusting anything here.

Raw data, checked in so it survives:

* `docs/recovery/hud_findings_raw.json` — 224 findings across four topics, each
  with `claim`, `citation`, and `confidence` (`decompiled` vs `inferred`), plus
  a per-topic `unrecoverable` list.
* `docs/recovery/hud_verification_verdicts.json` — the adversarial verdicts.

## Current implementation update (2026-07-29)

The earlier status below is historical recovery context, not the current code
state. The following paths are now implemented in the client and covered by
strict decode/presentation tests:

* runtime-generated 512x512 VXL minimap, 128px crop, held-key full map, frames,
  local cone, local/remote player markers, entity markers and height arrows;
  every rotating/oversized overlay keeps a renderer-level 128px scissor, since
  geometrically cropping a rotated cone is not equivalent to retail's viewport;
* server `CreateEntity(21)` / `DestroyEntity(19)` installation into the live
  world, which is the source of pickup, base, grave and deployable markers;
* server minimap packets `MinimapBillboard(41)`,
  `MinimapBillboardClear(42)`, `MinimapZone(43)`,
  `MinimapZoneClear(44)`, `TeamMapVisibility(83)` and `TeamProgress(117)`;
* pulsing objective-zone rectangle/icon rendering, including the exact
  `MINIMAP_ZONE_ICON` ordinal table and the H/I/J-to-G territory-art reuse;
* retail world-space objective billboards for packet 43, with 2.5 scale and
  `pointer_icon.png` edge arrows outside the camera cone, plus packet-41 fixed
  and entity-tracked billboards; all locations and visibility remain
  server-authored;
* Demolition packet `LockToZone(108)` as a server-owned prediction boundary,
  including the later whole-world release packet;
* `ChangePlayer(17)` variable tails for high minimap visibility and chase cam.
  High-visibility players are revealed before ordinary enemy-visibility
  filtering, use `map_vip_player_16`, and suppress the carried-tool marker,
  matching `Player.get_map_icon` branch ordering;
* the CTF carried-intel corner icon, including opposing-team colour
  cross-mapping and its minimap-dependent top-right position;
* exact `TeamProgressBar` retained state and presentation: packet-117's
  shared denominator/show mode, inverted values, zero reset, icon retention,
  280x40 backing, 32px icons, Spades-20 labels, retail double text pass, and
  the pooled `block128` loss particles described below;
* exact `TerritoryBasesHud` retained state and presentation from packet 106:
  the eight retail actions, vertical 32px base spacing, 0.8/0.96 containing
  scale, capture overlay, contested orange pulse, and A-J marker art (H-I-J
  deliberately reuse G);
* kill feed, top-screen `CHAT_BIG`, respawn overlay and damage-direction
  indicator.

The original `G:/AoSRevival/BattleSpades` server is a read-only compatibility
target. All packet adaptation is client-side.

The full-map GL overlay quads are now recovered and implemented. They were not
letterbox/dimming bands as the first round inferred: the cached tuples prove
two 1px white, 0.5-alpha grid passes. `xrange(0, 576, 64)` draws nine vertical
lines and `xrange(512, -64, -64)` draws nine horizontal lines over the centred
512px map. The final right/bottom strips extend one pixel toward the 552px
frame, exactly matching the original immediate-mode vertices.

TeamProgress particle bursts and Zombie heart marker scaling are now covered
by source-derived regression tests.

The fixed 800×600 HUD anchors, health/class portrait and lower-right panels
were screenshot-compared on 2026-07-29. Evidence is under
`out/evidence/ui-parity/hud-scale-minimap-pass1/`; the combined bottom-HUD
comparison is `retail-current-bottom.png`.

## 0. HOW MUCH OF THIS IS TRUSTWORTHY

A recovery pass produced 224 findings. A separate adversarial pass then tried to
**refute** each one against primary sources. That second pass ran out of account
budget partway, so the coverage is uneven:

| | count |
| --- | --- |
| CONFIRMED (survived refutation) | 25 |
| CORRECTED (claim wrong, right value recovered) | 18 |
| REFUTED outright | 0 |
| **UNVERIFIED (verifier never ran)** | **226** |

Two things follow, and both matter:

1. **Of the 43 claims that were actually checked, 18 needed correction — 42%.**
   Extrapolating, roughly two in five unverified claims below are wrong in some
   detail. Treat every unverified number as a starting hypothesis, not a fact.
2. **Coverage is not uniform.** `layout` and `assets` got most of the
   verification. `minimap` got *none* — every one of its verifiers died. The
   `gamemodes` finder failed outright (a malformed tool call, not the budget),
   so there is **no gamemode matrix at all**.

Per-topic status:

| Topic | Findings | Verification | Use it? |
| --- | --- | --- | --- |
| layout | 53 | partial, incl. corrections | Yes — implemented |
| assets | 49 | partial | Yes, but re-check each path on disk |
| killfeed | 54 | none completed | Hypothesis only |
| minimap | 68 | none completed | Hypothesis only — do not implement blind |
| gamemodes | 0 | n/a | **Missing entirely** |

## 1. THE TWO LOAD-BEARING CONVENTIONS

### 1.1 Raw window pixels, never a design canvas

`GameScene.draw` sets `glOrtho(0, window.width, 0, window.height, -1, 1)`
(`gameScene.pyd` 0x1013F83F, args 0x1013F86D / 0x1013F898) and calls **no**
`glScalef` anywhere in its 0x80B8 bytes; `gameScene.pyd`'s string table contains
no `get_aspect`. Corroborated by shipped source at
`aoslib/scenes/__init__.py:50-61`.

The frontend menus are the opposite and by a *different mechanism*:
`aoslib/scenes/frontend/menuScene.py:137-140` does
`get_aspect(800, 600)` then `glScalef(ratio, ratio, 1.0)`.

> **CORRECTED.** The original finding also claimed "all HUD frames load at
> scale=1.0 while menu art uses global_scale=0.64". Both halves are false.
> `weapon_frame_selected` loads at 0.7 (`images.py:436`); `blueprint_background`
> at `global_scale` (`images.py:433`). Main-menu frames take `load_ui`'s default
> of **0.6**, not 0.64 (`images.py:34`, `:184-189`), and plenty of menu art is
> 1.0 (`images.py:469-472`). Repo-wide there are 67 `scale=1.0` and 162
> `scale=global_scale` sites — it is not a HUD-vs-menu split. `global_scale` is
> an art-downsample applied to texture dimensions at load (`image.py:100-101`),
> not a layout canvas.

The architectural conclusion survives intact: **in-game HUD positions are raw
window pixels and must not be scaled by resolution.** The only multipliers are
fixed authored constants — 1.3 ammo frame, 0.4 class icon, 0.5 head icon, 0.546
VIP variant, 0.15 jetpack tool icon, 0.8 timer icon.

### 1.2 pyglet's origin is bottom-left

Every recovered `window.height - N` means N pixels **down from the top**, and
every bare `y` is measured **up from the bottom**. Our `ui::Rect` is top-left.
`hud_layout::to_top_left_y` does the conversion; the constants are stored in
retail's space so they stay diffable against the decompile.

## 2. TEAM COLOURS

`shared/constants.py:229-242`. `TEAM_COLOURS` and `UI_TEAM_COLOURS` are equal.

| Team | RGB |
| --- | --- |
| TEAM1 (blue) | 44, 117, 179 |
| TEAM2 (green) | 137, 179, 44 |
| Spectator | 255, 255, 255 |
| Neutral | 128, 128, 128 |

TEAM1=blue / TEAM2=green is corroborated independently by the intel icon names
bound to each id (`images.py:465-466`: `intel_blue_90` / `intel_green_90`).

> **ALIAS TRAP.** The human-annotated `shared/constants.py` mis-assigns
> A47–A52. The raw decompiler output at `shared/backup/constants-copy.py:81-91`
> is the authority: `A47 = ENOUGH_AMMO_COLOR = (255,228,0,255)`,
> `A48 = NOT_ENOUGH_AMMO_COLOR = (204,28,24,255)`. This is the same class of
> trap as the enum-ordinal one in `docs/ENTITY_CONSTANTS.md` — when the named
> block and the alias block disagree, **the alias wins**.

`team.color` itself lives outside `hud.pyd` (game/world module) and was **not**
decompiled; that it is literally `TEAM_COLOURS[team.id]` is an inference.

## 3. IMPLEMENTED WIDGETS

All seven are in `include/battlespades/frontend/hud_layout.hpp` +
`src/frontend/hud_layout.cpp`, locked by `tests/test_hud_layout.cpp`.

| Widget | Anchor | Geometry |
| --- | --- | --- |
| SCORE box | top-left | `(12, H-48)`, frame 200x40 unscaled, bottom-left anchored (the only HUD frame loaded `center=False`); text at `x+10`, white, Spades 26 |
| HeadCount bar | top-centre | `y = H-48`; `start_x = W/2 - bg_width*0.5`. The dynamic 80/90/110 width affects the backing **only**. Both score cells use module `HC_TEXT_WIDTH=80`: left x `W/2-163`, right x `W/2+79`, with right/left x alignment. In the native renderer both scores, heads, timer text and clock icon are vertically centred over the actual 40px `hc_frame_long` backing; replaying the source baseline directly visibly drops the glyphs below its centre because FTGL and the native rasterizer have different baseline ownership. Loaded heads remain exactly 23x22. |
| TeamProgress | top-centre | base `y=H-40`, lowered 40 when HeadCount is visible; backing `(W/2-140, y-10, 280, 40)`; team1/team2 icons at `W/2-76` / `W/2+48`, 32x32; labels at `W/2-88` / `W/2+84`, anchor `y+14`, Spades 20 |
| Match timer | top-centre / tutorial corner | Ordinary modes call `draw_timer(W/2 + 2, H-48)` without a frame. Tutorial calls `draw_timer(W-78, H-C, True)`, where `C=190` with the minimap and `40` without it. The function subtracts Python-2 `115/2 = 57`, draws the optional 115x40 frame, shadow at `(x+42,y-12)` in black, foreground at `(x+40,y-10)` in white, then the centered 32x32 icon at `(x+24,y)` under scale 0.8. Both text draws are left-aligned baselines. |
| Health bar | bottom-centre | frame at `(W/2, 30)`, art 239x34 `center=True`; the **complete** 239px fill texture is scaled, not cropped, about `anchor_x = 35`; team-tinted. Numeric HP uses Spades 25 at `(W/2 + 239*0.23 + 35, 35)`, centred, then three passes: foreground, grey shadow at `(+2.2,-2.2)`, foreground at `(+0.2,-0.2)`. |
| Ammo / prefab / blocks | bottom-right | `ammo_x = W-80`, `x = ammo_x - 115*0.5 - 19` (= `W-156.5`); ammo or selected-prefab cost `y=60`, blocks `y=12`; frame 115x40 under `glScalef(1.3, 1.0, 0.0)`, therefore 149.5x40; normal authored 330px icon at `image_scale=0.1` gives 33px; selected `prefab_cost_icon` instead uses `image_scale=0.25` and displays `prefab_cost`; block stock remains beneath it; portraits are centred at `ammo_x-50` and team-tinted where supplied |
| Jetpack gauge | bottom-right | `x = W-50`, `y = 130*0.5 + 135 = 200`; frame at `(x, y-4)`; art 33x130, `anchor_y = 0` so the fill grows upward |

Notes that cost time to establish:

* **The vertical yellow→red bar is the JETPACK FUEL gauge** — not health, not
  weapon heat. `draw_jetpack_hud` (`hud.pyd` 0x10096C10) returns immediately
  when `player.jetpack == NO_JETPACK`.
* **The `N/200` string** is `'%i' % team.score` plus `'/%s' % team.max_score`
  when `show_max_score` is set (`HeadCount.draw`, hud.pyx:85).
* **HeadCount has two independent text-width paths.** `calculate_startx`
  shadows the module name and uses the final team's dynamic 80/90/110 value
  twice to resize the background. `draw` resolves module `HC_TEXT_WIDTH`
  again, so both actual score cells remain 80 pixels. The macOS and Windows
  functions agree on the 23x22 head geometry, `y+10` head centre, and the
  right-side `-4` label nudge.
* **The ammo readout is two separate text draws** — current ammo at Spades 26
  and the reserve from the literal format `'/ %s'` at Spades 18. That size
  difference is authored, not a bug. Their measured widths plus a 2px gap are
  centred as one group at `ammo_x+17`. Both draws share the same authored
  baseline, `y+11`; vertically centring each font in an independent box makes
  the reserve visibly drift and does not match retail.
* **Fonts are all "Spades" (`ALDO_FONT`)**: score 26 (`text.py:315`), headcount
  26 (`:320`), health 25 (`HEALTH_FONT_SIZE`), ammo 26 (`:299`), reserve 18
  (`:301`).
* **The numeric HP anchor is now proven on both retail builds.** Windows
  `HUD.draw_healthbar` at `0x1009E70B..0x1009EC92` and macOS `hud.so` at
  `0x6CF9A`, `0x6D2DF`, `0x6D367`, `0x6D42D`, `0x6D8FA`, `0x6D93E`, and
  `0x6D967` agree on `width*0.23`, `health_bar.anchor_x`, bottom y `35`, the
  first `draw()`, and the subsequent `draw_offset(draw_shadowed,0.2,-0.2)`.
  `images.py:439` sets `anchor_x=35`; treating it as the centred texture anchor
  or as zero moves the number visibly left.
* **TeamProgress is not a conventional fill bar in this retail build.**
  `draw_team_level_bar` exists and can stretch `progress_bar.png`, but no
  runtime function calls it. `TeamProgressBar.draw` emits only the resized
  `hc_frame_long`, mode icons and labels.
* **Packet 117 values are inverted by the widget.** A non-zero wire value
  displays `max_team_progress - value`; zero resets the row to the maximum.
  `max_team_progress` and `show_as_percent` are shared across all rows, exactly
  as fields on the Python object rather than per-team dictionary values.
* **The label is deliberately heavy.** Retail calls `label.draw()`, then
  `draw_offset(label.draw_shadowed, 0.2, -0.2)`. Because
  `Label.draw_shadowed` adds its own `(2,-2)` translation and draws foreground
  again, the native renderer submits foreground, shadow at `(2.2,-2.2)`, then
  foreground at `(0.2,-0.2)`.
* **The loss burst uses the shared 100-particle HUD pool.** `HUD.__init__`
  constructs `HudParticleManager(100)`. A loss with packet `show_particle`
  starts `block128` at `(self.x + direction*67 - 10, self.y)`, where Team 1
  uses direction `(-1,-1)` and Team 2/neutral use `(1,-1)`. Lifetime is 0.5s,
  speed is 200 px/s, scale starts at 0.5 and grows by 0.8/s, and alpha starts
  at 1.0 and falls by 2.0/s. When `(previous-current)*100 < 10`, retail uses
  `(previous-current)*0.4` scale. Healing explicitly suppresses the burst.
  `HudParticle.draw` blits the resized 128px image at its position without
  centering it; this matters for the exact top-left conversion.

### 3.1 Territory Control

`TerritoryBaseState(106)` is decoded in the retail wire order:
`base_index`, `action`, `controlled_by`, `attacked_by`, then signed
fixed-point `capture_amount` at scale 64. The decoder rejects actions above 7,
team IDs above 3, truncated fixed values, and trailing bytes. The original
`G:/AoSRevival/BattleSpades` implementation was used only as read-only wire
evidence; the adapter and all retained state live in this client.

The recovered action state machine is:

| Action | Effect |
| --- | --- |
| 0 initial | create/update teams and capture amount |
| 1 capture update | update teams and capture amount |
| 2 activate | make the marker visible |
| 3 deactivate | hide the marker |
| 4 player entering | enable the containing-player scale |
| 5 player leaving | disable the containing-player scale |
| 6 contended | enable the contested pulse |
| 7 uncontended | disable the contested pulse |

Actions 2-7 deliberately do not overwrite team/capture details. The exact
retail constants recovered from `inithud` are: right inset 70, top inset 214,
x interval 0, y interval 32, base/frame size 128x32, base scale 0.8,
containing scale multiplier 1.2, letter scale 0.5, and letter offsets
(-100,-6). The zero x interval is important: the shipping marker strip is
vertical, not diagonal as an earlier unverified recovery suggested.

The letter inherits the outer base scale and then applies another
`base_scale * 0.5`, giving a final scale of `base_scale^2 * 0.5`. The attacked
plate is stretched to `capture_amount / 100` rather than source-cropped.
Contended markers blend the controlling-team tint toward `(255,100,0)` over
the first 80 percent of a `pi/3` pulse cycle, then snap back for the remaining
20 percent. The implementation preserves retail's single-subtraction timer
wrap.

### 3.2 Known inference inside the implemented set

Which `draw_tools_hud` local binds to `x` versus `ammo_x` (hud.pyx:809/810) was
**not** decompiled — the stack slots shift between assignment and call. The
chosen binding is the one that keeps the 1.3-scaled frame on screen and lands
its centre within 2 px of `ammo_x`; the reverse puts the panel off-screen.
`test_ammo_panel_stays_on_screen` locks that reasoning so a future change has to
confront it.

### 3.3 End-of-match ViewGameStats

The previous native MatchResults screen was not a recovery: it dimmed the whole
screen, scaled the content frame to 0.5, invented 38px award rows, used live
team model colors, and displayed as many as five rank bars simultaneously.
Those claims were contradicted by the shipping client and have been removed.

The authoritative layout comes from three independent retail sources:

* `aoslib/scenes/ingame_menus/__init__.py:210-278` contains the complete
  `draw_game_stats` helper;
* `aoslib/images.py:328-330,469-482` identifies the authored assets, the global
  0.64 load scale, centre anchors, and which Deuce head belongs to each team;
* macOS `aoslib.hud.hud.so`, `ViewGameStats.draw` at `0xea5d0`, supplies the
  top-level calls: frame `blit(400,110)`, left stats
  `(78,121,314,48,285)`, right stats `(410,121,314,48,410)`, message at
  `(400,165)`, mode title at `(400,180)`, and the bottom instruction rectangle
  `(300,25,200,18)`.

After converting from retail's bottom-left 800x600 canvas, the frame is
`(49,380,702,220)`. The 1098x344 source is truncated at load to
`int(source*0.64)`, not stretched from an assumed half scale. Team heads are
46x44 at `(78,445)` and `(678,445)`. Team names and optional scores use Spades
32; the score is right-aligned for blue and left-aligned for green. As in
`UI_TEAM_COLOURS`, this result widget is always blue `(44,117,179)` and green
`(137,179,44)` even when a mode gives the underlying character models different
colors.

Awards occupy at most three 314x16 rows per team at y=494/510/526. Empty rows
do not draw a background. Names and localized award labels use A750 Sans Medium
11 and the corresponding UI team color. Row backgrounds reproduce
`shared.common.blend_color(a,b,f) == int(a*(1-f)+b*f)` with `f=0.2`; the first
blue/green rows are consequently `(36,51,63)` and `(55,63,36)`. This order is
easy to reverse accidentally because the dark list color receives the 0.8
weight.

The screen no longer fabricates a black veil or `MATCH OVER` banner. It uses
Spades 14 for the result message, Spades 46 for the mode title, and Spades 18
for `Press TAB to show scores` at the recovered footer bounds.

`RankUps(66)` now follows the recovered timed state machine rather than the
disproven five-bars-at-once stack. Entries append to one queue and activate in
LIFO order after the exact 5.5-second initial delay. Each item holds for 0.75s,
interpolates linearly for 1.5s, remains for 2.0s, and fades over the first and
last 0.5s of that 4.25-second lifetime. A level boundary re-runs the retail
progression table and starts the independent 0.3s-up/0.3s-down glow pulse.

The footer geometry comes from Windows `ViewGameStats.draw_rank_ups`
`0x10069240` and `draw_progress_bar` `0x1006cc60`, cross-checked against macOS
`0xe0a40`/`0xddc20`. The inner bar is `(74,559,653,18)` in native top-left
coordinates; the authored 1029x38 stroke is loaded at 0.64 and blitted to
`(71,556,658,24)`. The score-reason label is centered in
`(300,557,200,18)`. Score text is left-aligned in `(81,557,98,18)`, while the
localized `Level N` label is right-aligned in `(567,553,150,20)`. All three use
the retail `ALDO_FONT` alias (`Spades.ttf`) at 14px. The level-up content glow
uses its authored image at `(68,553,664,30)` and the recovered flash color
`(192,255,132)`.

The separate level number is also source-backed. Windows `hud.pyd`
`ViewGameStats.draw_rank_ups` (`0x10069240`) maps the Cython globals for
`level_up` to `0x1011177c`, `level_up_timer` to `0x10111adc`, and the displayed
progression object's `level` attribute to `0x101127a8`. The draw call converts
that level to a string and uses cached `level_up_font` (`Spades.ttf`, 72px) at
retail bottom-left baseline `(714,26)`, color `(104,173,87,255)`, with
`center=False`. macOS `hud.so` corroborates the scale assignment and draw.

The recovered A1075..A1078 scale constants are `0.3`, `3.0`, `0.0s`, and
`0.4s`. Consequently the transition frame is scale `0.3`; the first positive
timer frame clamps to `3.0`; subsequent frames follow `3.3 - 7.5*t`, reaching
`0.3` at 0.4 seconds; and `self.level_up` clears for `t > 0.4`. The native
renderer uses baseline `(714,574)` and post-rasterization geometric scaling.
This is important: retail `Font._draw` translates before calling `glScalef`, so
re-rasterizing a differently hinted font every frame is not equivalent.

### 3.4 SelectTeam, ChangeTeam and ViewScores player states

The shared roster rows now reproduce every branch in retail
`aoslib/scenes/ingame_menus/__init__.py:102-209`, rather than treating every
living player as the same class icon. `CreatePlayer(28)` supplies the demo flag
and `local_language`; `ChangePlayer(17)` supplies high-minimap/VIP visibility;
and `KillAction(46)` drives the two relationship markers relative to the local
player. These values remain presentation state on the client and do not alter
the server protocol.

Dead names and the 32x32 death icon use `(255,0,0)`. Demo names use
`(255,194,81)`. A living VIP replaces the class portrait with the 28x24 crown.
All living primary icons blend the active StateData team colour toward white
using the source helper's exact `int(team*0.4 + 255*0.6)` truncation. Original
asset dimensions retain the global 0.64 scale: death 20.48x20.48, crown
17.92x15.36, 230x230 class icons at the local 0.11 scale become
16.192x16.192, domination markers are 10.24x10.24, and the 14x14 lobby leader
icon becomes 8.96x8.96. The leader icon advances the name origin by its width
plus one source pixel.

The roster has two deliberately separate colour domains. Team heads, names,
scores, ordinary player text, alternating row bands and domination markers use
fixed `UI_TEAM_COLOURS` blue/green. A living class or VIP portrait alone blends
the active StateData `Team.color` toward white. This matters in VIP/custom-color
modes: applying the server character tint to the complete scoreboard is not
retail behavior.

Russian, Polish and Turkish (`LocalLanguage` ordinals 6, 7 and 8 from
`aoslib/strings/__init__.py:9,53-54`) use `fonts/Tuffy_Bold.ttf`; other roster
rows retain A750 Sans Medium. The domination icon is white, while the dominated
icon uses the opposing UI team's colour.

Windows `gameScene.pyd` `GameScene.process_packet_kill_action`
(`sub_10194940`, traceback source lines 3666-3720) proves the relationship
state machine. Forced/team-change kills (types 8/9) clear both flags on the
changing player. Every ordinary death clears both victim flags; a local
domination marks the victim `dominatedByLocalPlayer`, an enemy domination marks
the killer `dominatingLocalPlayer`, and the corresponding revenge transitions
clear those flags. CreatePlayer does not carry either field, so a same-identity
respawn retains them, matching the persistent retail player object.

`tests/test_pause_menu.cpp` pins every asset, exact top-left geometry, tint,
font and host-name offset. `tests/test_protocol168_players.cpp` pins the packet
fields, all domination/revenge transitions, team-change reset, and respawn
retention.

## 4. STILL UNRECOVERABLE / NOT DONE

This section was written before the implementation update above. Items 2 and 3
are superseded: the minimap core, live markers, zones and kill feed are now
implemented. Packet-41/43 world billboards and off-camera pointers are now
implemented from the recovered renderer contract. The full-map overlay quads,
zombie heart scaling, and TeamProgress HUDParticleManager burst have since been
recovered and regression tested. Visual comparison remains. Also, both
diamond-dropoff assets exist; the missing-file statement below is a refuted
historical finding.

Do not invent values for any of these.

1. **The whole gamemode × element matrix.** The finder never ran. This is what
   the "different elements per gamemode" requirement depends on.
2. **The minimap**, in full — 68 findings exist but *none* were verified, so
   none are implemented. Specifically open: how `map.minimap_texture` is
   generated inside `vxl.pyd` (which column/topmost-block rule, what GL format);
   the compare direction in `RadarStationEntity.can_detect_player`
   (`radarStation.py:92`); `MinimapBillboard.render` internals; the two
   `GL_QUADS` loops on the full-screen map (`minimap.py:636-646`); the value of
   `MinimapZone.icon.scale`; per-entity `icon_scale` for anything but the bomb.
   `MODE_ZONE_ICONS` references a `diamond_dropoff` billboard with **no matching
   file in either tree** — unresolved.
3. **The kill feed** — 54 findings, none verified, none implemented.
4. **Recovered and implemented:** the alternate top-right timer is Tutorial's
   branch. macOS `hud.so` at `0x65724` proves `C=190` when the minimap is
   enabled and `C=40` otherwise, with `draw_bg=True`. `draw_timer` at `0x80800`
   proves the complete frame, two text baselines/colors, and centered icon
   transform. Focused layout and draw-list tests pin both callers at 1600x900.
5. The `clamp()` bounds in `draw_healthbar` (hud.pyx:1059) and
   `draw_jetpack_hud` (:912). 0.0/1.0 is overwhelmingly likely from the ratio
   semantics and is what we implement, but it is **not** cited as decompiled.
6. **Recovered and implemented:** the HP-number base x is
   `W/2 + health_bar.width*0.23 + health_bar.anchor_x`, with `anchor_x=35` from
   `images.py:439`. Windows and macOS binaries also prove bottom y `35`, Spades
   25, and the three-pass foreground/shadow/foreground sequence. Focused tests
   pin both resolutions and all emitted draw commands.
   (`draw_timer`'s default is now proven false by its ordinary caller; Tutorial
   passes true explicitly.)
7. **No visual comparison against retail has been done.** Nothing in §3 has been
   screenshot-diffed. See the `retail-parity-rig` memo.
## 2026-08-09: Death/chase overlay correction

The retail client does **not** draw a `DEATH CAMERA`, `SPECTATING`, chase-input,
or player-cycle title over the world. Those strings never occur in either the
Windows or macOS retail HUD binary and do not exist in the English localization
table. The source-owned death HUD is the centered `RESPAWNING_IN` or
`NEVER_RESPAWN` label; the camera controller changes grave/chase views without
adding another HUD widget.

Native macOS xrefs provide the positive ownership check: `HUD.draw` reads the
`respawn` member at `0x5725D`; `HUD.draw_tools_hud` reads `spectator` at
`0x77DAC` only to hide the equipped tools; and `Minimap.get_focus_player` reads
`chase_player` at `0x166D7B/0x166DBF` only to select map focus. The C++ client
therefore keeps death-camera behavior in `DeathCameraController` and emits no
separate spectator overlay.
