# Handoff — ADS sight placement + in-game HUD restoration

### 2026-08-09 exact HeadCount score/head placement

The top-centre HeadCount renderer now follows the saved Windows `hud.pyd`
`0x100328E0` and macOS `hud.so` `0x159170` functions instead of the previous
symmetrical approximation. The retail shadowing bug affects only background
width: actual score cells always use `HC_TEXT_WIDTH=80`. At 1600x900 their
top-left rectangles are `(637,18,80,30)` and `(879,18,80,30)`, aligned right
and left with default bottom vertical alignment. The 72x69 source heads are
truncated by the 0.64 loader to 46x44, drawn at 0.5 as exactly 23x22, and land
at x `728.5` / `848.5`, y `27`. Do not re-centre the labels or compute the head
size from an untruncated `72*0.64` float.

Written 2026-07-28. Updated after the minimap/objective slice on 2026-07-28.
Read this top to bottom before touching the HUD or the
viewmodel. Everything here is either cited to a decompile or explicitly flagged
as unverified — please preserve that distinction, it is the only thing keeping
this port honest.

**Current state: 100/100 tests green and `/W4 /WX` clean. The verified executable
is `out/build/native-dev/src/RelWithDebInfo/BattleSpadesClient.exe`; do not
assume `dist/bin` is current without running the packaging step.**

Compatibility boundary: `G:/AoSRevival/BattleSpades` is read-only. The client
must adapt to the existing server wire format; never change the server to make
this port easier.

### 2026-07-28 TeamProgressBar update

Packet 117 is now installed into retained client HUD state and rendered from
the exact retail `TeamProgressBar.draw` geometry. Important recovered traps:
`max_team_progress` and `show_as_percent` are widget-wide fields, values display
as `maximum - wire_value`, zero resets to maximum, and an unknown icon id keeps
the previous icon. The shipping draw path does **not** call
`draw_team_level_bar`; its apparently conventional progress fill helper is dead
in this build. The visible widget is the 280x40 resized HeadCount frame, 32px
base/diamond icons, and Spades-20 labels with the original foreground/shadow/
foreground double pass. Only the optional HUDParticleManager burst remains.

### 2026-07-28 TerritoryBasesHud update

Packet 106 now has a strict client decoder and retained `TerritoryBasesHud`
state. All eight retail actions are preserved: initial/capture update,
activate/deactivate, player entering/leaving, and contended/uncontended. Only
the first two actions replace team/capture details.

The exact shipping layout is a vertical strip: 70px from the right, 214px from
the top, no x interval, and 32px y interval. Each 128x32 plate uses scale 0.8,
or 0.96 while containing the local player. The capture overlay is stretched,
not cropped. Contention blends the controlling-team tint toward `(255,100,0)`
with the retail `pi/3` pulse and 80/20 blend/reset split. Letter art A-J is
installed; H, I and J intentionally reuse G. Preserve the nested letter
transform: its final scale is `base_scale^2 * 0.5`, not merely
`base_scale * 0.5`.

All changes are confined to `BattleSpadesClient`. The original
`G:/AoSRevival/BattleSpades` server remains a read-only protocol target.

---

## 0. HOW TO BUILD (read this first, it will cost you an hour otherwise)

`cmake --build --preset native-dev` from a bare shell **appears to work** when
nothing needs recompiling (`ninja: no work to do`) and then dies with
`fatal error C1083: Cannot open include file: 'cstdint'` the moment a real
translation unit is dirty. That error means the MSVC environment is missing, NOT
that anything is wrong with the include.

Working incantation (PowerShell tool):

```
$d="C:\Program Files\Microsoft Visual Studio\18\Enterprise\Common7\Tools\VsDevCmd.bat"; & cmd.exe /d /c "call `"$d`" -arch=amd64 -host_arch=amd64 >nul && cmake --build --preset native-dev"
```

* It is `-arch=amd64`, **not** `-arch=x64`. Wrong arch → "The system cannot find
  the path specified."
* `scripts/build.ps1 -Native` fails in this sandbox: VsDevCmd's internal
  `vswhere.exe` writes to stderr, which `$ErrorActionPreference = "Stop"` turns
  into a terminating error.
* `ctest --preset native-dev` needs **no** VS environment — run it from Bash.
* Adding new source/test files requires `cmake --preset native-dev` to
  reconfigure before the target exists.
* Python is `py -3`. Bare `python` is not on PATH. Retail python is Python 2 at
  `C:/Python27/python.exe`.
* **Never hand-edit `*.generated.cpp`** — change the generator in `tools/`.

---

## 1. WHAT LANDED THIS SESSION

### 1.1 ADS sight placement — DONE, needs visual confirmation

`docs/ADS_SIGHT_PLACEMENT.md` is the spec. Summary:

Retail does not reposition the aimed weapon — **it stops drawing it**.
`Character.draw_sight` (`character.pyd sub_1005B630`) calls `glLoadIdentity()`
at character.pyx:2073, wiping the camera and the whole `draw_fps` chain, then
rebuilds from constants: `Ry(180) · T(0.025+x, −0.35+y, 1.85+z) · S(0.05)`.
`gameScene.pyx:1443` is a hard if/else — `draw_sight` and `draw_fps` are
mutually exclusive. **While aimed, no weapon body and no arms are drawn.**

The rifle's red bead is a separate model, `semi_sight_pin.kv6`
(`classicRifleWeapon.py:33 pin = SEMI_PIN`, the only `pin =` override in the
tree), drawn at `pin_scale` 0.02 offset `(X−0.015, Y+0.3, Z+2.1)`. It is
implemented.

**The actual bug was in the KV6 mesher, and it affected every model in the
game.** `kv6.pyd sub_1001AA50` builds each cube's corners from `flt_100212B0 =
−0.5` and `flt_100212B4 = +0.5` — retail **centres** the cube on
`coord − pivot`. We were spanning `[coord − pivot, coord − pivot + 1]`. Fixed in
`src/world/kv6_model.cpp`.

Confidence anchor: four independent constants collapse to exactly zero under the
centred convention and under no other — `0.025` at sight scale 0.05,
`0.025 − 0.015 = 0.010` at pin scale 0.02, and `−0.35 + 0.325 = −0.025` on the
**Y** axis for both `sniper_sight` and `rpg_sight`. Different axes, different
constants: not the same coincidence twice.

Measured after the fix (headless GPU probe, `tests/test_ads_sight_render.cpp`):
every aimed weapon silhouette is **±0.0000%** off the screen axis. Before, the
RPG scope hole was −2.86% x / −5.09% y.

**Open:** nobody has screenshot-compared against retail. The
`sight_pos.z = −1.85` family (RPG / drillgun / sniper2, hollow 9×29×9 tube)
centres the tube *on the eye point* with half behind the near plane — a
scope-tunnel look is predicted but was never observed in retail. If that family
looks wrong in game, it is the one genuinely unverified reading, not a
regression.

### 1.2 In-game HUD and objective widgets

`docs/HUD_RECOVERY.md` is the spec. Raw findings are checked in at
`docs/recovery/hud_findings_raw.json` (round 1, 224 claims) and
`docs/recovery/hud_findings_round2.json` (round 2, 60 claims).

Code: `include/battlespades/frontend/hud_layout.hpp`,
`src/frontend/hud_layout.cpp`, wired through `GameHudModel` /
`GameHudPresentation` in `game_hud.{hpp,cpp}`, fed from
`native_frontend_module.cpp` around line 6759. Tests:
`tests/test_hud_layout.cpp` (10 cases) plus updated assertions in
`tests/test_game_hud.cpp`.

The table below records the first restoration slice and is historical context.
The current implementation also includes TeamProgressBar, minimap/objective
zones, packet-41/43 world billboards and off-camera pointers, the CTF
carried-intel marker, VIP/final-survivor high-visibility pins, Demolition's
server-owned LockToZone prediction boundary, and TerritoryBasesHud as
described above and in `docs/HUD_RECOVERY.md`.

| Widget | Anchor (retail bottom-left space) | Wired from game? |
| --- | --- | --- |
| SCORE box | `(12, H−48)`, frame 200×40, `SCORE: n` at 26px | yes |
| Health bar | frame at `(W/2, 30)`, 239×34, fill scaled about `anchor_x=35`, team-tinted; centred Spades-25 number at `W/2+239*0.23+35`, drawn foreground/shadow/foreground | yes |
| Class portrait | centred `(W/2 − 119.5 + 6.4, 36.6)`, 58.8px | yes |
| Ammo panel | `x = W−156.5`, `y = 60`, 115×40 under `glScalef(1.3, 1.0, 0.0)` → 149.5×40 | yes |
| Blocks panel | same x, `y = 12`, **always visible** | yes |
| Jetpack gauge | `(W−50, 200)`, 33×130, fill grows upward | **NO — model setter exists, never called** |
| HeadCount + clock | `y = H−48`, bar width 340 for a 3-digit max score; fixed 80x30 score cells and 23x22 heads | yes — populated from retained match state |

---

## 2. TRAPS THAT HAVE ALREADY BITTEN THIS PROJECT

Every one of these cost real time. Do not relearn them.

1. **Raw window pixels, never a design canvas.** The in-game HUD uses
   `glOrtho(0, W, 0, H, −1, 1)` (`gameScene.pyd 0x1013F83F`) with no `glScalef`.
   The 800×600 canvas belongs to the frontend menus only
   (`menuScene.py:137-140`). Do not scale HUD positions by resolution.
2. **pyglet's origin is bottom-left.** Every recovered `window.height − N` is N
   px down from the top. `hud_layout::to_top_left_y` converts.
3. **The alias trap.** Retail constants files have a named block and an
   A-aliased block. **The alias wins.** The annotated `shared/constants.py`
   mis-assigns A47–A52; `shared/backup/constants-copy.py:81-91` is the
   authority. `ENOUGH_AMMO_COLOR = (255,228,0,255)`,
   `NOT_ENOUGH_AMMO_COLOR = (204,28,24,255)`.
4. **The enum-ordinal trap.** Names inside `xrange()` blocks are POSITIONS, not
   quantities. `GRAVE_DAMAGE` = 14 is an index, not a damage figure. Kill types
   and damage types are both ordinals.
5. **`int()` truncation at load.** `image.py:100-101` does
   `tex.width = int(tex.width * scale)` **before** the anchor is taken.
   `int(230 × 0.64) = 147`, not 147.2. Round early, exactly like retail, via
   `hud_layout::loaded_image_pixels`.
6. **The 0.546 class-icon scale is NOT a VIP variant.** It is gated on
   `player.high_minimap_visibility` (hud.pyx:1040). The two coincide in the
   gangster modes, so a VIP-keyed implementation looks right in exactly the
   modes you would test it in.
7. **HeadCount has a real retail bug — reproduce it, do not fix it.**
   `calculate_startx` assigns a local that shadows the module-level
   `HC_TEXT_WIDTH`, so only the **last** team's width survives the loop, and
   line 68 then adds that value **to itself**. Both halves of the bar get the
   same width. `HC_TIMER_WIDTH` (120) is added unconditionally.
   `width_offset` is 0 and never reassigned.
8. **Two different icon scales.** The tool-strip portrait (`330 × 0.40` = 132px)
   is NOT the bottom-right panel icon. `draw_ammo_hud` takes its own
   `image_scale` parameter. Conflating them made the panel icons 5× too large.
9. **`can_zoom` is true for 64/65 tools** — `tool.py:45` sets the base default.
   That is retail-faithful; do not "fix" it in the generator. The real ADS gate
   is `sight is not None`.
10. **Recovered specs are wrong ~40% of the time on the details.** Of the claims
    that got adversarially verified this session, round 1 corrected 18 of 43 and
    round 2 corrected 16 of 35. **Measure before implementing a spec's causal
    claim about our code.** Two confident diagnoses were disproved in minutes by
    parsing the actual assets.

---

## 3. WHAT TO DO NEXT, IN ORDER

### 3.0b Retail menu centering is explicit across the front end

Do not replace `VerticalTextAlignment::retail_center` with generic `center` in
retail menu presentations. Python `text.py` centers `(ascender + negative
descender)`, while the generic native branch centers line height and therefore
adds half-leading drift. The main, Join/Quick/Create/Custom Match, identity,
loading, class/team, pause, settings/profile and UGC presentations now all use
the source formula. Gameplay HUD and debug commands remain outside this broad
migration because their recovered baseline contracts differ.

### 3.0a Leaderboard vertical metrics and long names are source-backed

All leaderboard text that retail routes through
`draw_text_with_alignment_and_size_validation(..., alignment_y='center')` or
`ListPanelItemBase.get_text_y_position` now uses the FTGL
ascender/negative-descender span, not the native font line height. Keep the
dedicated `VerticalTextAlignment::retail_center`; replacing it with generic
`center` reintroduces half-leading drift. `CONNECTING_PLEASE_WAIT` is the
exception and remains a direct Edo baseline at screen y=300.

Player names now run through the exact retail 143px, 11px A750 ellipsis pass
before ordinary column fitting. The runtime callback must continue measuring
literal row text with `measure_kill_feed_name`, not the localized Edo header
rasterizer. The implementation removes complete UTF-8 codepoints, so Cyrillic
names cannot be cut mid-sequence.

### 3.0 Chat language-font route is now source-backed

`HUD.create_line` copies the sending player's `local_language` into both the
sender-prefix and body tuples. `ChatLine.draw` switches the complete line to
Tuffy when `language_requires_tuffy` is true; the source helper identifies
wire language ordinals 6/7/8 (Russian/Polish/Turkish). The native packet path,
retained chat model, draw commands, and shaped-width callback now preserve that
route. The exact prefix literal is `%s: `, resolved from the macOS Cython string
table (`0x1e5fe8 -> 0x1dce96`). Do not regress this to per-run font guessing or
measure a Tuffy line with the A750 rasterizer.

### 3.1 Finish wiring the two dead widgets (small, mechanical)

`GameHudModel::set_jetpack_fuel`, `set_team_scores` and `set_match_clock` exist
and are covered by presentation tests, but **nothing calls them**. Add calls
next to the `set_block_state` call in `native_frontend_module.cpp` (~line 6776).
This is exactly the mistake made once already — an API that looks wired and
isn't — so verify in game, not just in tests.

Jetpack needs a per-class "has jetpack" flag we do not model yet; retail's
`draw_jetpack_hud` early-returns on `player.jetpack == NO_JETPACK`.

### 3.2 Bottom-right panel icon scale — settled

The old `ammo_icon_size{26.0}` was a screenshot fit based on a false premise:
`TOOL_IMAGES` do not pass through the UI global `0.64` load scale.
`draw_ammo_hud` receives the authored 330×330 tool image and its call-site
default `image_scale = 0.1`, producing a 33×33 destination for both the weapon
and block rows. The icon centre is `ammo_x - 50`; the block image is tinted
with the live team colour.

Current and reserve text are measured separately, separated by 2 pixels, then
centred as one group at `ammo_x + 17`. Current ammo/blocks use yellow
`(255,228,0)` above zero and red `(204,28,24)` at zero; reserve uses the retail
literal `"/ %s"` in white.

The corrected widget was captured against the retail 800×600 reference in
`out/evidence/ui-parity/hud-scale-minimap-pass1/retail-current-bottom.png`.

### 3.3 Minimap, kill feed, gamemode matrix (large, partially recovered)

* **Gamemodes** — all 13 Protocol 168 mode ordinals and all 11 canonical
  BattleSpades registry codes are covered by executable compatibility tests.
  Objective presentation is selected by authoritative packet icon/type fields,
  not by client-side mode guesses.
* **Minimap** — 68 unverified claims (round 1) + 15 more (round 2), **none
  verified**; both rounds' verifiers died on account limits. Given the ~40%
  correction rate, do NOT implement from these blind. Known open: how
  `map.minimap_texture` is generated inside `vxl.pyd`; the compare direction in
  `RadarStationEntity.can_detect_player` (`radarStation.py:92`); the full-screen
  map's `GL_QUADS` loops (`minimap.py:636-646`). `MinimapBillboard.render`, the
  packet-43 companion billboard scale, pointer texture, and diamond-dropoff
  assets were subsequently recovered and implemented.
* **Kill feed** — 54 + 15 claims, none verified. Icons live in
  `png/ui/kill_types/`.

### 3.4 Backlog carried from earlier sessions

Turret aim audio loop; entity health / chain detonation; dynamite blast radius
in the weapon catalog generator (still 5, should be 8); death camera orbit around
the grave; HDR + tonemap; dynamic lights; bloom/SSAO; water; footsteps/bob;
per-class VO; minigun spin during reload.

---

## 4. HOW THE RECOVERY WORKFLOW IS RUN

Pattern that works: a `Workflow` with parallel finders → **adversarial
verification of every claim** → synthesis. The verification is not optional; it
corrected 34 claims across two rounds this session.

Cost control matters — the account hit both session and weekly limits. Round 1
used 230 agents / 16M tokens because finders returned 49–68 shallow claims each.
Round 2 capped finders at **15 claims, depth over breadth**, and produced better
answers with 64 agents. Keep the cap.

Scripts persist under the session's `workflows/scripts/`; re-run with
`{scriptPath, resumeFromRunId}` to replay cached agents for free after editing
post-processing. **Have the workflow return the claims themselves** — an earlier
run returned only counts and the synthesis agent then died, stranding the data
in the run journal.

---

## 5. STANDING CONSTRAINTS

* OpenSpades (`G:/AoSRevival/openspades`) is **GPLv3**: study techniques, never
  copy code or transcribe constants.
* Network packet parity is explicitly deferred.
* Canonical map space: **z grows DOWNWARD**; z=239 is the indestructible bed;
  "up" is −z; face 4 is a block's top.
* `Kv6Model::mesh` bakes Rx(+90) (retail GL basis); world-space draws apply
  Rx(−90) to undo it. The ADS sight chain does **not** — do not add one there.
* No graphical/windowed smoke tests without asking; the user's desktop is not
  free by default. Headless probes only.
* Canonical client repo is `aceofspades_revival` (KikoTs, aos.pkg pipeline).
