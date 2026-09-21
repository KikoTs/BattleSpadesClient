# Class selection, constructs, and block hotbar

The class menu previously chose the four-card background for short server
rosters but always used the five-card portrait positions, sizes, and mouse
targets. It also appended the player's current class even if the server did
not advertise that class. The model and presentation now share the recovered
four/five-card geometry, preserve the server roster order, and only expose
advertised supported classes.

Construct selection now follows retail's one-to-three selection behavior:
clicking a fourth construct drops the oldest choice, clicking a selected
construct removes it unless it is the final choice, and changing classes
preserves each class's loadout. Explicit selections are no longer refilled to
three during submission. New default loadouts still contain three constructs.
`restore_loadout` seeds the menu from authoritative equipped items when it is
reopened.

The block HUD has a separate placement-cost API. Its upper row can show a
one-block or drag-line cost, with an invalid-placement red number and team
color. The lower row continues to show authoritative stock/capacity with the
same team-colored icon, matching `draw_tools_hud` at 0x10095247 and 0x100965BA.
Native frontend integration supplies the current placement cost/validity.

The palette is now the original 64 swatches in an 8x8 grid, replacing a guessed
32-swatch table. Both input and drawing consume the same color table. Cells are
9px squares with 11px spacing and a 1px selection border.

## Original-game evidence

Source root: `G:/AoSRevival/aceofspades_source`.

- `aoslib/scenes/ingame_menus/selectClass.py`, `on_start`: server-team class
  list; four-card mode for at most four classes; five-card mode otherwise;
  construct `TableSelection` maximum 3, minimum 1.
- `aoslib/scenes/gui/horizontalListSelection.py`, `item_info`: four-card
  button size 136, interval 167; five-card button size 107, interval 134.
  Combining the supplied origin with the list offsets gives top-origin
  `(81,132)` and `(85,131)` respectively.
- The same file, `on_item_selected`: removes the oldest selected index before
  appending a new one when the maximum is reached.
- `selectClass.py`, `on_class_selected`/`create_loadout_list`: retains each
  class's choices and sends the explicitly selected prefab list.
- `aoslib/gui.py`, `CustomButton.draw_image`: adds 0.22 to a class icon's scale
  when hovered or selected. `draw_button_border` centers the separately scaled
  frame. `aoslib/image.py` truncates image dimensions during the 0.64 load
  scale, so the 251x226 frame becomes 160x144 before further scaling.
- `aoslib/hud/hud.pyd`, `Palette.draw` at `0x10030020`, read with IDA from an
  analysis copy: anchor span 88 at `0x10030251`/`0x10030378`, row stride 11 at
  `0x100305d2`, column stride 11 at `0x100307aa`, selector offset 1 at
  `0x10030a83`/`0x10030ac0`, selector dimensions 11 at `0x10030b0d`/
  `0x10030b2b`, swatch dimensions 9 at `0x10030bfd`/`0x10030c20`.
- `shared/hud_constants.py`: ordinary palette right/bottom padding 170/5;
  UGC padding 30/30.
- Exact RGB swatches were read from the supplied original-game screenshot
  `codex-clipboard-a9aa76f6-56a4-44cf-93eb-fe5b0b3dd5d4.png`, at the centers
  `(17+11*column,16+11*row)`. Rows are gray, red, orange, yellow, green, cyan,
  blue, and magenta. The screenshot's 9px squares/11px stride independently
  agree with the binary.
- `aoslib/weapons/blockToolCommon.py` initializes `block_cost = 1` and tracks
  placement validity; `blockTool.py::get_block_cost` uses the current block
  line length.

## Focused regression coverage

`aos_class_selection_menu_tests` covers 1–7 server classes, both geometries,
mouse selection, invalid/duplicate class ids, forbidden current classes,
FIFO construct replacement, minimum-one selection, exact submitted prefabs,
retained per-class choices, restored authoritative loadouts, and scrolling.

`aos_game_hud_tests` covers independent cost/stock rows, invalid-placement
color, team-colored block icon, drag-line cost updates, all 64 swatches,
sampled original RGB values, and exact palette corner positions/cell sizes.

Both targets passed in the native-dev build. The staged client was also
checked on `192.248.177.80:32887`: the four-class chooser, parachute equipment
choice, 8x8 palette and independent block cost/stock rows rendered correctly.
Captures are under `out/evidence/retail-fixes-loadout-respawn/`.

## Final local-only audit

Subsequent testing used only local BattleSpades. The four-class UI fixture is
`127.0.0.1:32889`, using the isolated server profile
`tmp/local-client-audit-20260920/four-classes.toml`. It advertises Commando,
Marksman, Miner and Engineer. No public server was contacted for this audit.

Block drag evaluation now shares one pure helper between the native ghost and
HUD cost. Existing solid cells cost zero, each new cell must touch terrain or
an earlier cell in the drag, infinite block teams bypass stock checks, and
65-cell drags are rejected rather than silently truncated. The permitted
height is 1 through 238, matching the authoritative local server. Retail
`blockToolCommon.py::get_block_line(False, False)` also omits existing solids.
`aos_prefab_placement_tests` passed the new occupied-cell, supported-chain,
reverse-order, stock, infinite-stock, map-boundary and maximum-length cases.

The repeatable server command is:

```powershell
py -3.12 tools/run_local_prefab_fixture.py --client G:/AoSRevival/BattleSpadesClient/out/build/native-dev/src/RelWithDebInfo/aos_protocol168_live_session_smoke.exe --evidence tmp/local-prefab-gate-20260920
```

This starts a bounded server on loopback port 32891 with anchored flat terrain,
runs two real native protocol peers, and shuts down. The client submits one
non-default `prefab_caltrop` before joining; its server CreatePlayer preserves
that exact one-item list. Packet 30 then creates 11 cells, charges stock from
400 to 389, sends 11 owner BlockBuild packets and PrefabComplete, and sends 11
colored cell updates to the observer. This gate passed. Earlier authored-map
smoke placement was correctly rejected as `spawn or objective zone`; it tried
to build beside the spawn. The fixture does not disable production validation.

`aos_class_selection_menu_tests`, `aos_game_hud_tests`,
`aos_retail_character_pose_tests`, `aos_local_entity_tests`, and the hidden
`aos_placement_render_tests` also passed. The pose cases retain character arms
when a held tool is hidden or unavailable; the render cases verify ghost
opacity, ordering and terrain occlusion. Human-visible spectator transitions
and exact screen appearance still require the separate native UI check.
