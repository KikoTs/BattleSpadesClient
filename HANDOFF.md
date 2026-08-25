# BattleSpadesClient Handoff

## 2026-08-24: complete playtester-channel audit and Steam alias

The full 2026-08-05 through 2026-08-11 playtester export is normalized in
`docs/PLAYTESTER_CHANNEL_AUDIT_2026-08-24.md`. It distinguishes deterministic
fixes, sensory items that still require a live retail A/B capture, disproven
proposals, reproduction gaps, and server-owned behavior. Do not reopen the
claim that every local projectile must be client-spawned: IDA decompilation of
retail `GameScene.send_rocket`/`send_rocket2` proves those functions only fill
and send the oriented packet. Predicting an additional rocket would duplicate
the server-created entity.

Windows post-build and install/package now create `aos.exe` as a byte-identical
alias of `BattleSpadesClient.exe`, preserving the original Steam launch name
without creating a second implementation. The RelWithDebInfo build and staged
install hashes matched (`EC07A810D0769E41E55F040ABB9FFCB52C896E2125CCC65B7EE3653947E97464`),
the installed alias completed a 120-tick headless smoke, and the complete native
suite passed 101/101. The BattleSpades server repository was not modified.

## 2026-08-21: High/Ultra particle lighting is now live

`QualityProfile::particle_lighting` was previously reported by Settings/F3 but
never reached the particle shader.  Particle billboards now pass their stable
world-space centre, and High/Ultra evaluate the same bounded eight-light array
used by terrain.  Only non-additive smoke/debris receives the bounded tint;
additive glow LUT sprites remain self-lit.  Legacy, Low, and Medium retain the
old unlit path, so the retail Legacy capture is unchanged.  Whenever this
shader changes, run `scripts/compile-shaders.ps1` and stage the complete backend
tree rather than copying only the executable.

## 2026-08-21: authoritative jetpack-fuel HUD units

Protocol 168 carries `WorldUpdate.jetpack_fuel` in the retail 0..100 fuel
units.  `HUD.draw_jetpack_hud` divides that value by the selected pack's
`JETPACK_MAX_FUEL` before clamping; every shipping pack profile uses 100.
Native previously passed the raw value into a 0..1 HUD setter, leaving the
gauge visually full for nearly the entire tank.  The roster now retains the
raw authoritative units, the HUD converts them with
`retail_jetpack_fuel_fraction`, and `Restock(69,type=6)` immediately restores
100 while the next WorldUpdate remains authoritative.  Protocol and HUD tests
pin finite-input rejection, the 50% midpoint, and both clamps.

## 2026-08-21: minigun reload integration boundary

Retail `MinigunWeapon.update` does not hard-reset the barrel on reload.  It
keeps applying the inactive `+0.075` interval ramp while
`Character.reloading` makes both fire inputs unavailable; the separate fire
loop therefore closes immediately while the spin loop audibly winds down.
The native runtime already followed that rule.  A new
`aos_tutorial_session_tests` case now pins the frontend-facing contract too:
`weapon_trigger_live()` must become false on the accepted reload frame and
remain false while `weapon_spin_fraction()` decays.  Do not replace that ramp
with an abrupt motor reset; only `Tool.on_unset` (selection/life boundary)
resets spin synchronously.

## 2026-08-21: leaderboard/result text parity checkpoint

Leaderboard, TAB ViewScores, ViewGameStats and GenericVotingHUD have fresh
800x600 evidence in `out/evidence/ui-baseline-fix-20260821/`. The important
invariant is that retail width validation geometrically scales cached glyphs;
do not replace `TextFit::retail_width_scale` with rerasterized
`shrink_to_fit`. Direct roster/award calls are baseline anchored, while the
ViewGameStats team/rank boxes use `VerticalTextAlignment::retail_center`.
`MapEnded(52)` preserves results/chat/vote until scene teardown. The full suite
is 100/100 green after this checkpoint.

## 2026-08-21: exact terrain debris and non-retail plume removal

This change is client-only. `G:/AoSRevival/BattleSpades` remained a read-only
protocol oracle and was not modified.

IDA of Windows `gameScene.pyd` recovered `GameScene.spawn_debris` at
`sub_101318A0`: it calls `ParticleEffectManager.create_particle_effect` with
exactly four map-coloured tumbling cubes, `explode_velocity=.25`, authored
size `3.0`, the ordinary two-second decay, gravity and collision. Native now
uses that one composition for both chips and destroyed blocks. The fabricated
9/12-particle destruction bursts and separate tinted dust emitter are gone.

`FallingBlocks.update` (`sub_100C8720`) owns its per-voxel particle call; its
delete path (`sub_100CC290`) only removes the display object. The native
average-colour ground-smoke plume had no retail counterpart and was removed,
while the recovered per-voxel breakup and structure sound remain intact.
Tests now reject any extra smoke batch and pin debris count, size and opacity.

All 100 native tests pass. The portable bundle completed the 120-tick headless
smoke after restaging. Current executable SHA256:
`452425DF14A49E0AFE690C3DCB5275B504B0479A738BA55026F68E1DD202E8B0`.

## 2026-08-21: integrated client parity validation

The current client-only parity tree passes all 100 native tests, including the
leaderboard, packet-driven scoreboard, end-game results, map voting, settings,
audio, entity, movement and protocol suites. The portable bundle was restaged
with `tools/stage-native-client.ps1`, which copies and hash-verifies the full
backend shader tree instead of leaving stale renderer binaries behind. The
staged executable then completed the bounded 120-tick headless smoke.

The executable hash in this historical section was superseded by the terrain
debris build documented immediately above.

`G:/AoSRevival/BattleSpades` remained read-only throughout this work.

## 2026-08-21: deployable placement sound-bank parity

Entity creation previously hard-coded `_001` for Medpack and C4 and could not
select the Mine Launcher's authored water bank. `entity_sound.hpp` now resolves
the recovered placement family and semitone range for turret, landmine,
dynamite, Medpack, Radar, projectile mine and C4. `consume_entity_events`
expands numbered families, chooses a stable per-event variant and applies exact
retail pitch while preserving CreateEntity as the authority boundary.

The OpenAL-free audio test proves every mapped placement file exists and pins
Medpack/C4's three variants plus the Mine Launcher's dry pitch.

## 2026-08-21: portable Legacy renderer staging repair

The Ancient Egypt Legacy comparison exposed a packaging split, not another fog
formula error. `fs_world.sc` and the checked-in backend binaries already use
retail GL-linear fog from half draw distance to draw distance, bypass enhanced
tonemapping, and preserve the server/map fog colour. `dist/bin`, however, still
contained older `vs_world`/`fs_world` binaries for DX11, OpenGL, ESSL, Vulkan
and Metal, so the portable client continued rendering the old washed-out path.

`tools/stage-native-client.ps1` now stages the EXE, runtime DLLs and complete
shader tree as one operation, then hash-verifies every shader. Do not copy only
the executable into a portable bundle.

## 2026-08-21: live input settings and truthful graphics restart state

This change is client-only. The settings audit found that `TutorialWorldSession`
copied mouse sensitivity and invert-mouse at map construction, so a successful
in-game Settings commit persisted correctly but left look controls unchanged
until the next map. Confirmed settings now cross an explicit bounded runtime
setter and affect the live session immediately.

Shader Quality is consumed every render frame and MSAA is applied through the
existing bgfx presentation reset. They no longer raise a false restart-required
notice; only graphics API, texture quality and model quality remain restart-only.
Tests pin both the live-session input update and restart classification.

## 2026-08-21: authored weapon pitch and complete explosive sound banks

This change is client-only. `G:/AoSRevival/BattleSpades` remained a read-only
protocol/behavior oracle and was not modified.

Retail `Character.play_sound` passes each cue's minimum/maximum semitone fields
to `MediaManager.play`; Python 2 truncates `(semitones / 12) * 1000`, samples
that inclusive integer range, and converts it back with `2 ** semitones/12`.
The generated weapon catalog now preserves shot/reload/reload-done pitch pairs,
and OpenAL applies the exact conversion with a stable client-local draw.

The impact path no longer collapses modern numbered sound banks to `_001`.
Chemical, GL, sticky, mine-launcher and modern C4 impacts use every authored
dry/water variant; classic grenades, RPG/RPG2, drill, dynamite, turret rockets
and Molotov preserve their recovered banks and pitch ranges. Molotov fuse,
block-fire and burnout loops now follow entity create/destroy/map transitions.
Chemical dissolve/burn remains intentionally unwired because the current
server stream does not create the retail BlockGoo lifecycle that owns it.

`aos_weapon_catalog_tests` and `aos_weapon_audio_map_tests` pass after the
strict `/W4 /WX` client build. The latter now verifies every impact asset and
pins the otherwise easy-to-miss Python 2 pitch quantization.

## 2026-08-21: live ViewGameStats cinematic camera parity

This change is client-only. `G:/AoSRevival/BattleSpades` remained a read-only
protocol oracle and was not modified.

The end screen's authored thumbnail already rotated every five seconds, but
the world camera still remained on the local player or grave. IDA of macOS
`gameScene.so` recovered the actual `GameScene.set_screenshot_camera` path:
the StateData camera point is the pan start, authored rotation is retained,
`pitch_yaw_to_direction_vector` supplies the look direction, and a
`PanController` moves linearly five blocks backwards over five seconds.
Native now performs that pan, converts retail yaw to the renderer's canonical
VXL yaw, hides the first-person weapon during the cinematic, and bounds every
camera input. The old server's exact all-zero placeholder is deliberately
ignored for the 3D view.

The StateData-only `has_map_ended` route now shares packet 52's overlay
boundary, so chat editing is cancelled without erasing retained chat or a map
vote. Focused overlay tests and the complete native suite pass 100/100 under
strict warnings; the rendered `--debug-ui endgame` oracle was inspected after
the rebuild. The staged executable also passes the bounded 120-tick headless
smoke and has SHA256
`870BC0439FF7C35D566A38DA34BACF1E20CDCF8FF4CF6E3264EDC3E22C808378`.

## 2026-08-21: packet 72/73 scoreboard parity and live Friends leaderboard

This change is client-only. `G:/AoSRevival/BattleSpades` remained a read-only
protocol/behavior oracle and was not modified.

Windows `hud.pyd` proves that `ViewScores.set_message` enables the authored
bottom message box and `ViewScores.draw` blits `score_text_frame` at retail
bottom-left `(400,75)`, then draws uppercase Spades-14 text at `(400,70)`.
Native now decodes `ForceShowScores(72)` and `ShowTextMessage(73)` strictly,
retains the message selector, and shares one exact nine-message resolver
between hold/forced-TAB `ViewScores` and `ViewGameStats`. The converted native
rectangles are frame `(49,511,702,28)` and text `(180,516,440,14)`.

Packet 67 awards now retain their packet-selected wire team on every entry.
This prevents a stale/reused live roster team from moving an award into the
wrong blue/green result column after both official award packets arrive.

Retail `LeaderboardMenu` submits `SteamGetFriendList()` followed by the local
account and replaces returned row names with the current local/friend persona
names. Native now does the same using accepted AoSPlay social records: numeric
legacy IDs are bounded, de-duplicated, zero-filtered, and the local account is
appended. Persona replacement prefers signed-in identity, then current friend
nickname, then the score-service name. The score contract, leaderboard UI,
scoreboard, end-result tests and the full native suite pass 100/100 under
strict warnings. The current executable is staged in `dist/bin`, passed the
120-tick bounded smoke, and has SHA256
`FD47E8C9EF2BF50878BBA75B016B63A49A93C4E7C3FEE823B5881029AF9ADCE9`.

## 2026-08-21: official two-team end-result packet repair

This change is client-only. `G:/AoSRevival/BattleSpades` remained a read-only
protocol/behavior oracle and was not modified.

IDA of the shipping Windows `gameScene.pyd` implementation behind
`process_packet_game_stats` (`0x102479C0`, wrapper `0x1024B2F0`) confirmed that
packet 67 `team_id` chooses `game_stats_team1` or `game_stats_team2`. It is not
the winning team. Native previously cleared the retained awards on every
packet and assigned `winner_team = team_id`; an official server's second team
packet therefore erased the first column and could announce a false green win.

`MatchResultsModel` now appends bounded records from both official packets and
tracks which team lists were observed. `ShowGameStats(53)` resolves the winner
from the final authoritative team scores. A one-packet fallback remains only
for older BattleSpades builds that sent one mixed award list and used this
field as the winner; it is forbidden once both retail lists were seen.

The live result composition also had two draw-order leaks. The opaque authored
frame was emitted after result/mode labels and covered them, while live player
name projections and a stale UGC settings overlay could be painted over the
result screen. The frame and team columns now precede the labels, and active
world-only overlays are suppressed while ViewGameStats owns the screen. Chat,
the server-authored map vote, and hold-TAB ViewScores remain available.

`tests/test_match_overlays.cpp` covers two-packet accumulation, explicit score
winners, draws, the legacy fallback, and frame-before-label ordering. The live
AoSPlay `/leaderboard` and `/profile` POST endpoints returned HTTP 200 with the
expected JSON contract on 2026-08-21. The navigation smoke completed and the
full native suite passed 100/100.

## 2026-08-09: exact retail numeric-health label transform

This change is client-only. `G:/AoSRevival/BattleSpades` remained a read-only
protocol/behavior oracle and was not modified.

The numeric HP label was still a fabricated rectangle centred near the middle
of the health bar, used the ammo size 26, and emitted only one draw. Windows
`hud.pyd` `HUD.draw_healthbar` at `0x1009E70B..0x1009EC92` and macOS `hud.so`
at `0x6CF9A`, `0x6D2DF`, `0x6D367`, `0x6D42D`, `0x6D8FA`, `0x6D93E`, and
`0x6D967` prove the exact replacement. The centred Spades-25 anchor is
`(window.width/2 + health_bar.width*0.23 + health_bar.anchor_x, 35)` in retail
bottom-origin space. `images.py:439` overwrites `health_bar.anchor_x` with 35,
so the 1600x900 anchor is `(889.97,35)`, not the old approximate centre.

Retail calls `health_text.draw()` and then
`draw_offset(health_text.draw_shadowed,0.2,-0.2)`. Decompiled `draw.pyd`
`sub_1000DC70` confirms draw_offset is push/translate/call/pop; `text.py`
confirms draw_shadowed adds `(2,-2)`, grey `(64,64,64,alpha)`, then redraws
the foreground. Native now emits those exact three passes through zero-size
center/center point anchors. `aos_hud_layout_tests` pins the bottom/top-origin
coordinates at 1600x900 and 1920x1080; `aos_game_hud_tests` pins font, size,
order, colours, offsets, and the independent InitialInfo numeric-HP gate.

## 2026-08-09: exact retail HeadCount draw geometry

This change is client-only. `G:/AoSRevival/BattleSpades` remained a read-only
protocol/behavior oracle and was not modified.

Saved Windows `hud.pyd` `HeadCount.draw` at `0x100328E0` and macOS `hud.so`
at `0x159170` prove that the previously centred native score cells were an
approximation. `calculate_startx` keeps retail's shadowing bug and uses the
last team's dynamic width twice for the background, but `draw` resolves the
module `HC_TEXT_WIDTH=80` for both labels. At 1600x900 the score rectangles are
exactly `(637,18,80,30)` and `(879,18,80,30)`, aligned right/left and bottom.
The 72x69 images truncate to 46x44 at load, then draw at 0.5 as 23x22 at
`(728.5,27)` and `(848.5,27)`. Regression coverage pins the layout metrics and
emitted draw commands in `tests/test_hud_layout.cpp`.

## 2026-08-09: retail leaderboard row selection, hover, and routing

The leaderboard no longer fabricates a Player Profile transition when a row is
clicked. Retail `LeaderboardMenu` never registers a row-selection callback;
`ListPanelBase` only selects the row, plays `menu_scrollA`, and leaves the user
on the table. Native rows now reproduce the exact alternating background ->
`(175,172,161,255)` hover -> green line/glow selection -> text draw order.
Selected and hovered player identity follows a row through header sorting and
is cleared when type/scope refresh recreates the row objects. Pointer hit
testing uses the same clipped grid and scroll position as rendering.

Regression coverage is in `tests/test_leaderboard_menu.cpp`; source evidence is
in `docs/HUD_RECOVERY.md`. This slice is client-only and does not modify
`G:/AoSRevival/BattleSpades`.

## 2026-08-09: complete retail scoreboard player-state branches

This change is client-only. `G:/AoSRevival/BattleSpades` remained a read-only
protocol compatibility oracle and was not modified.

The SelectTeam, ChangeTeam and hold-TAB ViewScores player rows now implement
the remaining source branches from
`aoslib/scenes/ingame_menus/__init__.py:102-209`: red dead name/death icon,
amber demo name, VIP crown, white-blended class/crown tint, Russian/Polish/
Turkish Tuffy font, domination and dominated relationship markers, and the
local hosted-lobby leader icon with its exact name offset. Original image sizes
are retained at retail's 0.64 global scale rather than normalized to fabricated
16px squares.

`RemotePlayerReplica` now retains the `CreatePlayer(28)` demo/language fields.
`Protocol168Roster::apply_kill_relationships` reproduces Windows
`gameScene.pyd` `sub_10194940` source-line transitions for domination, revenge,
ordinary death and forced/team-change death. Relationship flags survive only a
same-name/same-team CreatePlayer replacement because those bits are absent from
packet 28; a reused player id cannot inherit them.

Focused coverage is in `aos_pause_menu_tests` and
`aos_protocol168_players_tests`. Continue from source evidence for the next UI
gap; do not replace these branches with generic team colors or uniform icon
boxes.

## 2026-08-09: exact SelectTeam/ChangeTeam/ViewScores roster rows

This change is client-only. `G:/AoSRevival/BattleSpades` remained a read-only
protocol reference and was not modified.

The shared native roster renderer used `277/16` row height for all three team
lists and approximated both the stripe order and blend. Retail does not share
that height: `selectTeam.py` calls `draw_player_list(..., y=469, height=277)`,
`changeTeam.py` calls it with `y=443, height=263`, and macOS `hud.so`
`ViewScores.draw` at `0xfe56c..0xfe63b` / `0xfe72f..0xfe814` builds the exact
tuples `(team1,77,416,313,263,385,extras)` and
`(team2,412,416,313,263,415,extras)`.

`append_player_lists` now receives the caller's actual height. SelectTeam keeps
its `17.3125px` rows; ChangeTeam and the hold-TAB scoreboard use `16.4375px`.
The scoreboard still begins at native top-left y=219 but its sixteenth row now
ends at y=482, rather than being stretched thirteen pixels too far down.

The source loop includes the header as `i=0`, so the first player row is `i=1`
and begins with `LIST_COLOR2 (35,35,35)`, then alternates to `LIST_COLOR1`.
`shared.common.blend_color(a,b,0.2)` is `int(a*0.2+b*0.8)` with truncation;
the first two default-blue rows are therefore exactly `(42,100,150)` and
`(37,95,145)`. The old port reversed both weights and row order, producing a
much darker fabricated roster. `tests/test_pause_menu.cpp` pins all three
caller pitches, the stripe colors, and the exact scoreboard lower edge.

## 2026-08-09: retail VIP banner and shared `draw_big_text`

This change is client-only. `G:/AoSRevival/BattleSpades` remained a read-only
protocol reference and was not modified.

The native packet-50 `CHAT_BIG` path previously drew the correctly sized
backing frame but omitted retail `Label.draw_shadowed`, and the persistent
local VIP notice was absent entirely. Retail source plus Windows `hud.pyd`
establish that both use the same `aoslib.text.draw_big_text` helper. The native
presentation now shares one implementation: width wrapping with the 60px
reserve, widest line plus 40px frame width, 58px frame scaling per source
line, the retail empty-line positioning quirk, then gray shadow at converted
top-origin `(+2,+2)` followed by the foreground.

When `high_minimap_visibility` marks the local player as VIP, the HUD now draws
`You are a V.I.P! Stay safe!` at bottom-origin `(window.width/2, 90)` in the
active team's authoritative `StateData` colour. This is intentionally
independent from `enable_player_score`: disabling the SCORE box and class
portrait does not suppress the VIP banner. `tests/test_hud_layout.cpp` pins the
exact frame and label geometry, ordering, colours, font, size, and independent
gate. Continue with the next source-proven HUD contradiction; do not fork a
second big-text renderer for future packet or mode labels.

## 2026-08-09: InitialInfo HUD authority and query-port alignment

This change is client-only. `G:/AoSRevival/BattleSpades` remained a read-only
protocol reference and was not modified.

Retail protocol evidence in
`G:/AoSRevival/aceofspades_source/reversal/protocol/tooling/reversed.json`
places the `query_port` uint16 immediately after `map_is_ugc`, before the
classic/presentation flags. The native decoder declared that field but consumed
it after those flags, shifting every presentation flag by two bytes while the
later string fields happened to remain aligned. `Protocol168InitialInfo` now
consumes and retains `query_port` in its wire position and retains the server's
`enable_numeric_hp` and `enable_player_score` flags instead of discarding them.

Retail HUD evidence in `docs/recovery/hud_verification_verdicts.json` establishes
that `enable_numeric_hp` gates only the numeric health label; it does not hide
the health frame, fill, or portrait. `enable_player_score` gates the top-left
SCORE box and the health-bar class portrait, and class 13 (`CLASS_UGCBUILDER`)
hides the SCORE presentation regardless of that flag. The frontend now applies
those rules directly from the active server's InitialInfo and preserves the
authority when later SetScore packets arrive.

`tests/test_protocol168_session.cpp` pins the exact query port and both enabled
and disabled presentation flags. `tests/test_game_hud.cpp` separately proves
numeric-label gating, health-bar persistence, SCORE-box gating, and portrait
gating. Any future InitialInfo extension must be inserted at its recovered wire
offset rather than appended near a similarly named field.

## 2026-08-09: Model Quality now reproduces retail KV6 invscale

This change is client-only. `G:/AoSRevival/BattleSpades` remained a read-only
protocol reference and was not modified.

The Graphics tab already persisted and restart-gated Model Quality, but every
native KV6 path still uploaded full-resolution geometry. Retail
`aoslib/models.py` and the symbol-rich macOS `aoslib.kv6.so` establish the
complete behavior: ordinary Low/Medium/High models use inverse scales 3/2/1;
prefabs stay full resolution; weapon sights and pins request minimum detail 2
and therefore stay full resolution even on Low.

`world::Kv6Model::inverse_scaled` now reproduces native `scale_kv6`: dimensions
ceiling-divide, pivots and subsequent authored offsets divide by invscale,
occupied N-cubed source cells collapse to one voxel, RGB uses integer-average
color, and any grouped team-material marker remains the black recolor band.
The mesher uses the recovered `n` cube extent and `c = 0.5*n - 0.5` center
correction, preserving authored world size instead of shrinking low-detail
models. The startup tier is routed through class bodies/arms, held weapons,
casings/tracers, disguises, entities, projectiles, jetpacks, graves and Tutorial
models. Prefab validation/preview deliberately retains raw KV6 resolution.

`tests/test_model_quality.cpp` pins the tier/minimum/prefab mapping, ceiling
dimensions, pivot and offset scaling, team-marker preservation, reduced mesh
load, retained extents, and full-resolution sights. Continue the settings audit
with a source-backed runtime check for the remaining persisted graphics rows;
do not turn Model Quality into a live cache mutation because retail applies it
before model loading and the native settings menu correctly requires restart.

## 2026-08-09: Texture Quality is now an actual runtime setting

This change is client-only. `G:/AoSRevival/BattleSpades` remained a read-only
protocol reference and was not modified.

The Graphics tab already retained and restart-gated Texture Quality, but every
native loader still decoded `png/high`; Low and Medium therefore changed a TOML
value and nothing else. Retail `aoslib/image.py` proves the setting is a startup
resource-root choice: 0 -> `png/low`, 1 -> `png/med`, 2 -> `png/high`.

`render::texture_quality_asset` now applies that exact mapping only to the
quality-family images. It is shared by background UI preloading, on-demand UI
texture loading, and WorldRenderer's particle atlases, smoke/glow LUTs and
sniper-laser textures. Fixed `png/ui` artwork is deliberately unchanged. The
GPU cache keeps the authored draw-list key while decoding the selected file, so
menu geometry and callers do not fork by quality. The setting remains
startup-only exactly like retail and the existing restart-required notice is
therefore still correct.

`tests/test_texture_quality.cpp` pins the three path mappings, isolation from
`png/ui`, unsafe-path fail-closed behavior, and the shipped 128/256/512-pixel
Tumbling Cube atlases. Continue the settings audit with retail KV6
`model_detail`/`invscale`, which is still persisted but not yet applied.

## 2026-08-09: source-exact ordinary and Tutorial timer

This change is client-only. `G:/AoSRevival/BattleSpades` remained a read-only
protocol reference and was not modified.

The previous clock was a fabricated centered text rectangle with no shadow and
an incorrectly lifted icon. It also had no Tutorial branch. The shipping macOS
`hud.so` now supplies the complete contract:

- ordinary modes call `draw_timer(window.width*0.5 + 2, window.height - 48)`;
  Tutorial calls `draw_timer(window.width - 78, window.height - C, True)` with
  `C=190` when the minimap is enabled and `40` when it is disabled;
- `draw_timer` subtracts Python-2 integer `timer_frame.width/2`, which is 57
  for the shipped 115px art rather than 57.5;
- it draws the optional 115x40 frame, black left-baseline shadow at
  `(x+42,y-12)`, white left-baseline foreground at `(x+40,y-10)`, then the
  center-anchored 32x32 icon at `(x+24,y)` with the exact 0.8 scale;
- packet `DisplayCountdown` now selects Tutorial placement from the retained
  InitialInfo mode ordinal 10 and passes the server-owned minimap switch.

`tests/test_hud_layout.cpp` pins both call sites, every internal offset, text
alignment/color/order, optional frame behavior, and emitted top-left draw
geometry at 1600x900. Do not restore a centered text box or reuse the
HeadCount cell as timer geometry.

## 2026-08-09: TeamProgressBar retail particle lane

This change is client-only. `G:/AoSRevival/BattleSpades` remained a read-only
protocol reference and was not modified.

Packet 117's previously discarded `show_particle` bit now drives the exact
retail loss burst. macOS `hud.so` proves `HUD.__init__` creates
`HudParticleManager(100)` and `TeamProgressBar.update_team_progress_data`
starts `block128` at `x + direction*67 - 10, y`: Team 1 moves `(-1,-1)`, Team
2 and neutral move `(1,-1)`. The retained particle uses the recovered 0.5s
lifetime, 200 px/s speed, 0.5 base scale, +0.8 scale/s, 1.0 alpha and -2.0
alpha/s. Sub-threshold loss uses `(previous-current)*0.4`; healing, reset, an
invalid team, and pool overflow emit nothing. The sprite is drawn uncentred,
matching `HudParticle.draw`'s direct `image.blit(x,y)` call.

`tests/test_hud_layout.cpp` now verifies packet gating, both team directions,
small-loss scaling, motion/fade/expiry, the exact 100-particle bound, custom
team tint, reset, the 128px source asset, and the 1600x900/HeadCount-shifted
draw rectangle. Continue with the next evidence-backed UI comparison rather
than revisiting this lane from screenshots alone.

## 2026-08-09: minigun reload motor parity

This change is client-only. `G:/AoSRevival/BattleSpades` remained a read-only
protocol reference and was not modified.

The minigun used to freeze its barrel interval, rotation, and spin-loop pitch
for the entire reload because `WeaponRuntime::tick` skipped all held processing
while `reload_remaining_` was non-zero. Retail does not freeze the motor.
`aoslib/weapons/minigunWeapon.py:MinigunWeapon.update` continues running and
selects the inactive alteration whenever `character.reloading` is true, even if
LMB or RMB remains physically held.

`WeaponRuntime::update_minigun_motor` now runs on every selected-minigun tick.
Reload disables both held inputs for the motor and suppresses fire, while the
recovered `+0.075` interval-per-second curve winds the barrels and
`minigun_loop` pitch down toward rest. Normal LMB/RMB spin-up still uses the
recovered `-0.15` curve; no wire action, ammunition transfer, or reload timing
was changed.

`tests/test_weapon_runtime.cpp::the_minigun_winds_down_during_reload` starts at
full spin, fires one round, begins the recovered two-second reload, holds RMB,
and proves that one second later the motor is silent-fire-safe at exactly the
source-derived 0.625 spin fraction. The pre-fix runtime failed this assertion
because it remained at full spin.

## 2026-08-09: exact GenericVotingHUD geometry and text helpers

This change is client-only. `G:/AoSRevival/BattleSpades` remained a read-only
protocol reference and was not modified.

The previous voting presentation still centered ordinary text inside guessed
rectangles. Retail does not: it mixes cached-glyph width scaling with the
wrapping/shrinking helpers in `aoslib/text.py`, and every Y coordinate is a
bottom-origin baseline. The native renderer now has separate
`TextFit::retail_width_scale` and `TextLayout::retail_wrapped_lines` contracts
instead of approximating both with `shrink_to_fit`.

- `GenericVotingHUD.draw` in macOS `hud.so` (`retail_hud_mac2`) and the source
  helper calls establish the 300x200 panel at x=10, live vertical centering,
  the title baseline `title_y + 70/3`, and the description baseline at
  `title_y - 10`.
- The candidate loop starts at
  `description_y + (count - 1) * 30 - 115`, then subtracts 30 per row. Its
  recovered columns are x=40 for keys/unvotable candidate text, x=90 for
  votable candidate text, and x=250 for counts. Candidate text has width 150;
  key/count fields have width 25. After a non-revotable cast the key hints
  disappear and candidate labels reclaim x=40.
- The selected count is `(0,255,0,255)`; all other voting text is white. The
  Cython intern-table reference used by `draw_text_lines` resolves to the
  literal keyword `horizontal_alignment`, proving centered wrapped description
  and result text rather than the earlier inferred single line.
- `split_text_to_fit_screen` is reproduced exactly: explicit newlines survive,
  ordinary wrapping uses the strict `content_width + 10 < width` test, words
  are not broken, the font shrinks by one pixel until the authored height fits,
  every line shares the smallest recovered width scale, and later baselines
  advance by the FTGL ascender/descender span plus twice the 4px line spacing.
- The CLOSED result branch converts its source bottom rect
  `(15,315,290,80)` to native top-left `(15,325,290,80)` at 720p. Its first
  centered text baseline is y=405 and its authored width is 295.

Regression coverage in `tests/test_match_overlays.cpp` pins the baselines,
columns, row pitch, colors, hidden key prompts, wrapped-text contract, and
result rectangle. Strict `/W4 /WX` build and all 95 CTest targets pass. The
renderer executable was refreshed in `dist/bin` and passes the 120-tick
headless smoke. Do not restore the older centered-box approximation.

## 2026-08-09: source-derived ScoreLine/score-reason feed parity

This change is client-only. `G:/AoSRevival/BattleSpades` remained a read-only
protocol reference and was not modified.

The earlier native score feed was a visual approximation: 14 px text, a 2.4 s
shared timer, immediate aggregation, gray/green/red hardcoded colors, reversed
reason order, and an `(W/2 + 28, H/2 - 9)` top-left anchor. None of those
details matches retail.

- Mac `hud.so` `HUD.add_score_reason` is the Cython wrapper at `0x99280`.
  It resets an empty, five-line, or almost-faded stack; otherwise it appends a
  reason delayed by `0.25 * len(feed)`. Its comparison at `0x9ae1a` is
  `feed[0].ttl < ScoreLine.alive_before_show + ScoreLine.start_ttl`; the loop
  at `0x9b0ab` calls `set_ttl(1.75)` on every retained line.
- Mac `HUD.update` at `0x92bd5..0x93582` aggregates a positive reason score
  only after `ScoreLine.can_draw()` becomes true, rewrites the `+N` title, and
  clears that reason score. The first reason is seeded with zero so the first
  award is not counted twice.
- Mac `HUD.draw` at `0x5bfd8` translates to
  `(window.width * 0.5 + 30, window.height * 0.5 - 20)` in bottom-origin
  coordinates and advances every retained row by `content_height + 10`, even
  while a delayed row cannot draw.
- Retail `ScoreLine` uses `Spades.ttf` 20 px for the white title and 16 px for
  team-colored reasons, a 1.5 s TTL, 0.2 s fade, and maximum alpha 127. Direct
  probes of the bundled Python 2 font wrapper returned line-height/ascender
  pairs `22/15` and `17/12`; at 800x600 the native baselines are exactly
  `(430,320)`, `(430,352)`, `(430,379)`, while the normal glyph tops resolve to
  `(430,305)`, `(430,340)`, `(430,367)`, ... .
- `ScoreLine.draw` calls `Font.draw_multi_stroke` with the optional center flag
  omitted, so every line is deliberately left-aligned at `W/2+30`. The helper
  emits the recovered FTTextureFont outside-outline pass followed by the fill
  on one baseline; it does not use `text.py`'s eight translated copies. Stroke
  alpha remains `int(127 * fade^2)` and foreground alpha
  `int(127 * fade)`.

Regression coverage is in `tests/test_game_hud.cpp`: it proves the initial
`+100`, the hidden 0.5-second second reason, delayed `+200` aggregation, exact
font/color/geometry, shared 1.75-second retirement, and the five-line reset.
The focused HUD test and complete native suite must remain green.

Next UI comparison: exercise the live packet-69 score reasons at 800x600 and
capture draw geometry against retail, then continue the map-vote/end-screen
comparison and remaining gameplay/VFX/audio/settings/legacy audit.

## 2026-08-09: GenericVotingHUD state-machine correction

This is client-only. `G:/AoSRevival/BattleSpades` remained a read-only
compatibility reference.

The earlier map-vote model fabricated a two-second result immediately after a
local F-key cast, hid `hide_after_vote` ballots immediately, and treated UPDATE
like START. All three behaviors were disproved in the shipped binaries.

- `GameScene.send_cast_generic_vote_message` at Mac `0x1252c0` calls
  `on_vote_cast(index, hide_after_vote, 5.0)`, retrieves the exact candidate,
  disables `can_vote` only when changing votes is forbidden, and sends it.
- Packet START resets/configures/shows the HUD. UPDATE only replaces candidate
  rows and resets local `voted_candidate_index`/`result_text`. CLOSED uses the
  packet title in `show_voted_candidate_message(6.0, title)`.
- The safe literal decoder now implements both retail tuple forms, argument
  formatting, selected-argument localization, Python escapes, and doubled
  braces without evaluating server input.
- `GenericVotingHUD.update`'s result-first `if/elif` timer ordering and strict
  five/six-second expiry comparisons are regression tested.

Do not restore the local post-cast result. The 290x80 branch is server-authored
by CLOSED. Continue next with a headless draw-command geometry comparison, then
the remaining gameplay/VFX/audio/settings/legacy audit.

## 2026-08-09: player-chat parity correction

Normal packet-49 chat now follows the recovered retail `ChatLine` structure:
the sender prefix is a team-coloured label, the body is a separately coloured
label (white globally; `blend_color(team, white, 0.4)` for team chat), and the
body offset uses shaped A750-Sans-Medium metrics. Regression coverage lives in
`tests/test_match_overlays.cpp`; do not collapse these runs back into one
team-coloured string.

## 2026-08-09: leaderboard source-parity correction

Leaderboard selectors are now retained DropBoxControls rather than click-to-
cycle shortcuts. Type and scope lists open over the grid, transfer single
focus, retain selected-row art, close on selection/click-away, and only request
the chosen cache entry. The presentation also fixes ALDO/Spades versus Edo
aliases, 0.64-truncated header/sort art, disabled-button tint, loading font, and
the scrollbar's source-derived 3.75px bevels. Regression coverage is in
`tests/test_leaderboard_menu.cpp`; source evidence is recorded in
`docs/HUD_RECOVERY.md`.

## 2026-08-09: retail UGC Construct/Game Data libraries

This change is client-only. `G:/AoSRevival/BattleSpades` was read as protocol
evidence and was not modified.

- The source `SelectPrefabs` and `SelectGameData` screens now replace the old
  placeholder editor inventory. They share retail's five-slot FIFO, exact UGC
  Builder loadout, source grid/tab/footer geometry, retail art, menu cues and
  packet-13 transaction.
- The Construct Library is backed by all 448 exact retail prefab identifiers.
  Category, English label and size are source-derived; fallback size uses the
  shipped KV6 voxel count and retail thresholds. Unknown server strings fail
  closed.
- Packet 68 now gates the Game Data marker set and drives a live Objectives
  panel with the exact twelve min/max/priority rules, stable priority order,
  red/green completion values and source list-panel geometry.
- Initial UGC join opens Construct Library. In-game Pause exposes Construct and
  Game Data Library instead of Change Class/Team; returning preserves the
  server-replicated backpack. No graphical client was launched during this
  headless verification.

Next UGC comparison: capture the retail and native libraries at 800x600 with a
real packet-68 editor session and resolve any remaining per-asset half-pixel
differences. Then continue the live map-vote/end-screen comparison described
below.

## 2026-08-09: retail UGC in-game Settings presentation

This change is client-only. `G:/AoSRevival/BattleSpades` remained read-only.

- Replaced `NativeFrontendModule`'s approximate UGC settings text overlay with
  a renderer-neutral `UgcIngameSettingsPresentation` recovered from
  `aoslib/scenes/ingame_menus/ugcSettings.py` and the shared retail settings
  controls.
- Exact 800x600 boundaries are pinned in `test_ugc_editor_menu.cpp`: integer
  0.64-scaled outer/content frames, title area, list panel, scrollbar and both
  TextButtons.
- The layer now uses the real category headers, setting-row texture, skydome and
  mode dropdowns, no-edit RGB sliders, color preview, title edit box, preview
  button, and retail one-row viewport scroll. Fabricated focus bars, `ESC
  CANCEL`, and keyboard-help copy were removed.
- Pointer input now asks the presentation for each visible field rectangle, so
  drawing, scrolling, and hit testing share one geometry source.
- Asset preloading now includes the two settings frames, setting-row texture,
  collapse/scroll/slider art required by this route.

Transaction evidence:
`.transactions/ugc-ingame-settings-presentation-parity-20260809/`.

## 2026-08-09: retail UGC Construct Builder fine placement

This change is client-only. `G:/AoSRevival/BattleSpades` remained read-only.

- Tool 42 now has the source two-click transaction rather than immediately
  building: first LMB enters fine placement and stops movement; second LMB
  commits the retained blueprint.
- `world::UgcPrefabControl` ports the `[0,.5,.1,.05]` held-input repeat curve,
  10x Sprint nudge, Jump/Crouch vertical nudge, camera-relative arrow rotation,
  ordinary RMB yaw rotation, 0.5-second exit guard, wheel zoom formula, green
  ghost pulse and 0.1-second held-C carve cadence.
- Fine placement carries yaw, pitch and roll into BuildPrefabAction(30), and C
  sends the existing ErasePrefabAction(31) layout. UGC builds now send the
  source `add_to_user_blocks=False`; ordinary competitive prefabs remain True.
- The retained center drives the prefab orbit camera and hides the FPS hands
  until commit/cancel. Slot/class changes clear the control state exactly like
  `UGCPrefabTool.on_unset`.
- Regression coverage is `tests/test_ugc_prefab_control.cpp`. Focused UGC,
  prefab, tool-action and weapon-runtime tests pass under strict `/W4 /WX`.

Next UGC live comparison: use a large asymmetrical construct and verify all 16
yaw/pitch/roll presentation states against retail, then capture stage-one,
stage-two, carve and commit packets from both clients. Do not tune the server
to compensate for a client transform mismatch.

### 2026-08-09: UGC asymmetric rotation correction

The first source port mirrored two branches that retail deliberately keeps
asymmetric. `UGCPrefabTool.py:149-155` makes WEST+left compound only at pitch
3, while lines 168-174 make EAST+right compound only at pitch 3. The native
control had tested pitch 1 in both places. `world::UgcPrefabControl` now follows
the source literally, and `tests/test_ugc_prefab_control.cpp` pins the pitch-1
and pitch-3 output triples for both camera-relative directions. This prevents
large non-symmetric constructs from jumping to the wrong yaw/roll when the
palette arrows are used in fine placement.

## 2026-08-09: retail delayed RankUps footer

This change is client-only. `G:/AoSRevival/BattleSpades` was not modified.

- `RankUps(66)` now runs the retail 5.5-second delayed LIFO queue. One item is
  shown at a time for 4.25 seconds: 0.75-second hold, 1.5-second score
  interpolation, 2.0-second continuation, and 0.5-second edge fades.
- The progression calculation is evaluated during interpolation, including the
  original strict threshold behavior. Crossing a level boundary triggers the
  recovered 0.3s-up/0.3s-down authored content-stroke glow.
- Footer geometry is pinned to Windows `draw_rank_ups` `0x10069240` and
  `draw_progress_bar` `0x1006cc60`, cross-checked with macOS `0xe0a40` and
  `0xddc20`: inner `(74,559,653,18)`, stroke `(71,556,658,24)`, score reason
  `(300,557,200,18)`, score `(81,557,98,18)`, and right-aligned `Level N`
  `(567,553,150,20)`. Retail's `ALDO_FONT` is the `Spades.ttf` face at 14px.
- `NativeFrontendModule` advances the state from the normal overlay delta; a
  results clear resets the active item, queue, timers, level flag and glow.
- The separate level number is now recovered from the shipping Windows and
  macOS HUD binaries. Retail draws `str(rank_up.level)` with the cached 72px
  `Spades.ttf` font at bottom-left baseline `(714,26)`, `center=False`, and
  `(104,173,87,255)`. The native top-left baseline is `(714,574)`.
- Its `Font.scale` curve is the exact A1075..A1078 branch: `0.3` on the
  transition frame, a discontinuous/clamped `3.0` on the first positive timer
  frame, then `3.3 - 7.5*t` down to `0.3` at 0.4 seconds. The retail
  `level_up` flag clears only when the timer is greater than 0.4 seconds.
- `TextDrawCommand` therefore carries a post-rasterization geometric scale and
  explicit baseline alignment. Do not replace this with per-frame font-size
  rasterization: `Font._draw` translates before scaling cached glyph geometry.

Next UI comparison: capture the live map-vote and chat screens. The full-map
overlay quads are complete: Windows hud.pyd proves they are nine vertical and
nine horizontal 1px white grid strips at 64px intervals with 0.5 alpha, not
letterbox bands.

## 2026-08-09: retail ViewGameStats/end-match award recovery

This change is client-only. `G:/AoSRevival/BattleSpades` was read as wire
evidence and was not modified.

- `MatchResultsPresentation` now follows the shipping
  `draw_game_stats` helper and macOS `ViewGameStats.draw` instead of the old
  fabricated full-screen veil, half-scale frame, 38px rows, and five stacked
  rank bars.
- The 1098x344 frame uses the retail 0.64 load scale and resolved centre blit at
  `(49,380,702,220)`. Deuce head base/color pairs, Spades 32 team headings,
  score flags/max score, three 16px award rows, A750 Sans Medium 11 text, fixed
  UI blue/green colors, and truncated `blend_color` backgrounds are all pinned
  by focused characterization.
- The result message (Spades 14), mode title (Spades 46), and `Press TAB to
  show scores` instruction (Spades 18) use the recovered source anchors. Empty
  award slots no longer emit fabricated row quads.
- `RankUps(66)` was subsequently restored by the entry above. Do not restore
  the disproven simultaneous stack.

Next UI recovery slice: do a live map-vote and end-screen screenshot
comparison. Continue to keep the compatibility server read-only.

## 2026-08-08: retail chat and GenericVotingHUD recovery

This change is client-only. `G:/AoSRevival/BattleSpades` remained an untouched
wire/reference server.

- Chat now matches recovered `HUD.add_message`, `HUD.draw_chat`,
  `ChatLine.draw`, and `draw_stroked`: newest-first 50-line retention, ten
  visible lines, five-second lifetime, exact 2.5-second fade, baseline anchors
  at 12/65, direct retail `get_line_height()==13`, 23px message pitch,
  A750 Sans Medium 12, eight-neighbour stroke, and separate unboxed
  input/channel labels at bottom-origin baselines 15/40.
- GenericVotingHUD now uses its recovered 300x200 vertically centred panel,
  black alpha 100, Spades 26 text, bracketed F-key/count labels, no fabricated
  candidate backgrounds, and a maximum of three candidates. The later entry
  above supersedes this pass's disproved local post-cast result behavior.
- The shipped `aoslib.draw.draw_quad` binary was independently decompiled to
  confirm `(x, y, width, height, color[, texture])`; the native geometry no
  longer relies on an inferred endpoint convention.
- Focused characterization covers geometry, order, fade, stroke, typefaces,
  safe decoding, exact wire token, hide-after-vote, and result expiry. No GUI
  client was opened.

### 2026-08-09: exact retail kill-name FTGL outline

- `Font.draw_multi_stroke` is two draws, not eight translated glyph copies:
  the stroke font followed by the normal fill font.
- macOS `get_font` (`0x39770`) sets the `FTTextureFont` stroke flag;
  `FTTextureFont::MakeGlyph` (`0x45ab0`) forwards it to `FTTextureGlyph`
  (`0x38e0`). The stroke branch calls `FT_Get_Glyph`, configures a FreeType
  stroker with radius `140` (26.6 units), round caps, round joins and miter 0,
  strokes the outside border while destroying the original glyph, then converts
  the result to a normal grayscale bitmap.
- `TextRasterizer` now exposes this as `retail_outline_stroke`, includes the
  render mode in its cache key, and keeps normal shaped advance/content metrics
  while expanding only ink bounds. Kill-feed names emit exactly the recovered
  outline pass then fill pass on the same baseline.
- Focused tests distinguish both cache entries, prove outside-ink expansion,
  preserve advances, and pin the two-command kill-name ordering. This is a
  source-path implementation claim; a retail/native screenshot-diff remains a
  separate visual acceptance step.

Next UI recovery slice: the end-game MatchResults/award presentation and live
map-vote screenshot comparison. Do not modify the compatibility server.

## 2026-08-08: retail leaderboard list-panel recovery

This change is client-only. `G:/AoSRevival/BattleSpades` remains an untouched
wire/reference server.

- `LeaderboardMenu.py`, `leaderboardListPanel.py`, `leaderboardListItem.py`,
  `listPanelBase.py`, `gui.py`, `text.py`, and `shared/hud_constants.py` were
  re-derived instead of extending the previous approximate table.
- The title now uses retail `title_font` (`Spades.ttf`, 46px); headers use
  `small_edo_ui_font` (11px); player rows use `small_standard_ui_font` (11px).
  The old Spades 44 title and shared 12px row/header face are gone.
- The list header moved to the exact top-left conversion of retail y=425
  (`y=150`), Rank returned to its source minimum width of 50, Name remains 143,
  and runtime-localized Edo measurements drive the source column-width
  algorithm.
- Rank and Name are pinned. Oversized localized stat columns now use the retail
  horizontal viewport, textured arrows/thumb, and `filter_down_white` icons on
  unsorted headers. Long lists use the recovered vertical scrollbar. Both
  controls share geometry with hit testing, so scrollbar clicks no longer open
  an unrelated player profile.
- The intentionally odd `max_index - 1` draw boundary is preserved: twelve
  player rows normally and eleven while the horizontal scrollbar is active.
  Drop-downs now reproduce `MenuOptionControl`'s 4px border, 2px gap, 12px
  SquareButton, down-arrow art, and Edo 11 label. The Back control uses the
  retail NavigationBar icon/font placement instead of centered gold text.
- Verification: strict `/W4 /WX` native build, 94/94 CTest targets, six focused
  leaderboard cases, and a 120-tick headless bootstrap all exit 0. No graphical
  window was opened. Chat and GenericVotingHUD remain the next UI recovery
  slices; do not infer their layout from this table.

## 2026-08-08: native UGC Map Creator vertical slice

Map Creator now launches the real hidden `BattleSpadesMapCreator` process,
streams the baseplate through Protocol 168, reconstructs the UGC Builder marker
inventory and exact marker models/icons/groups, renders placement ghosts,
supports live marker/prefab collaboration, and exposes the recovered host
settings for sky, RGB/Z palette water, target mode and title. Dedicated servers
advertise safe wire role 2; the spawning client is recognized as logical editor
owner without changing the server. See `docs/UGC_MAP_CREATOR.md` for packet
layouts, retail evidence, controls, storage and exact validation commands.

## Update 2026-08-06: recovered audio-zone lifetime and resolution-aware retail fonts

This change is client-only. The BattleSpades server was neither modified nor
launched, and no interactive client window was opened.

- The original Python `media.py`/`audio.py` contract was rechecked: HUD and
  music are dry head-relative sources, world sounds are positional, distant
  sounds are rejected before source creation, and world voices use the shared
  EFX reverb (`gain=.32`, `decay_time=1.49`, `gain_hf=.89`). The native mixer
  already modeled those zones, but installing its voxel-occlusion callback
  accidentally deleted the world reverb slot and effect. Reverb ownership now
  lasts until audio shutdown; changing the occlusion resolver no longer tears
  down unrelated OpenAL state.
- The frontend now maps every shipped retail face explicitly: Spades, Edo,
  A750 Sans Medium/Bold, Tuffy Bold, mplus and Noto Sans JP. In particular,
  `A750-Sans-Bold.ttf` no longer falls through to `Spades.ttf`, which caused
  the Create Match/settings typography mismatch. Unknown face identifiers fail
  closed instead of silently drawing the wrong family.
- Design-space text is rasterized at the drawable's physical scale and its
  bitmap metrics are transformed back to the recovered 800x600 layout. This
  preserves widget positions/sizes while avoiding the old low-resolution glyph
  texture being enlarged on 1080p/1440p/4K displays. Glyph sampling remains
  point-filtered for the retail hard-edged appearance.
- The menu artwork itself is not AI-upscaled: the recovered source files are
  already oversampled relative to their presentation geometry (for example,
  `ugc_splash.png` is 1280x960 and `ui_frame_large.png` is 1172x921). Upscaling
  these would invent detail and reduce parity; physical-resolution rendering
  was applied where the actual deficiency existed, in vector font rasterization.
- Tests now cover drawable-aware raster sizing and successful rendering through
  all seven retail font files. Verification: strict `/W4 /WX` native build,
  91/91 CTest targets, and a 120-tick headless bootstrap pass. The rebuilt
  executable is `out/build/native-dev/src/RelWithDebInfo/BattleSpadesClient.exe`.

## Update 2026-08-05b: continuous gravestone bounce contact

This change is client-only; BattleSpades server remains untouched.

- The measured KV6 bottom/contact adjustment now remains active throughout a
  physical entity's fall and rebound, rather than appearing only when its
  `grounded` flag becomes true. A grave therefore keeps the same ~0.55-block
  loader-pivot relationship for the complete bounce and no longer levitates
  before snapping down on its final contact.
- Wall and ceiling attachments still bypass the floor correction. The packet
  position, collision anchor, blast centre, pickup radius and death camera are
  unchanged; this remains a presentation-only transform.
- The helper was renamed to `entity_vertical_contact_adjustment` so its lifetime
  invariant is explicit. Asset-backed tests now require identical adjustment in
  airborne and grounded states for every physical entity model.
- Verification: strict `/W4 /WX` build, 91/91 CTest targets, and a 120-tick
  headless bootstrap all pass. The executable is in
  `out/build/native-dev/src/RelWithDebInfo` and was not copied to `dist/bin`.

## Update 2026-08-04b: network entity ground contact

This change is client-only. `G:/AoSRevival/BattleSpades` was inspected as a
read-only packet/placement authority and was not modified or launched.

- BattleSpades `CreateEntity(21)` coordinates remain authoritative. The client
  no longer changes them to compensate for KV6 pivots, because doing so would
  also move pickup radii, blast centres, death-camera anchors, and later
  `ChangeEntity(16)` updates.
- A grounded face-up entity now measures part zero's real decoded KV6 bottom
  after its recovered pivot, model scale, and mandatory Rx(-90) basis
  conversion, then applies one presentation-only vertical correction to the
  entire rig. Part zero is deliberately the contact mesh so a turret's aiming
  barrel cannot make its base bob.
- Superseded by the 2026-08-05b correction above: the intrinsic contact offset
  remains active during flight/bounce, while wall/ceiling attachments still
  bypass it. Client-side terrain gravity and the reliable server lifecycle keep
  their ownership.
- Asset-backed tests cover every gravity-owned entity model and pin the three
  distinct regressions: landmine shallow-pivot gap (~0.375 blocks), rocket
  turret multi-part base (~0.53), and grave loader-pivot gap (~0.55).
- Verification: strict `/W4 /WX` native build, focused protocol/entity tests,
  90/90 CTest targets, and a 120-tick headless MayanJungle bootstrap. The
  verified executable is in `out/build/native-dev/src/RelWithDebInfo`; it was
  not copied into `dist/bin`.

## Update 2026-08-04: authoritative jetpack corpse flight

This change is client-only. `G:/AoSRevival/BattleSpades` was inspected as a
read-only protocol authority and was not modified. No GUI client was launched.

- Pack-equipped deaths now retain the packet-28 Character through the server's
  one-second corpse fuse instead of hiding it at `KillAction(25)`. Dead
  `WorldUpdate(2)` rows own the rising/corkscrew translation and packet
  `ExplodeCorpse(36)` owns the final removal, explosion point and camera handoff.
- The visual spin reproduces recovered `Character.set_dead/update_dead`: one
  uniformly distributed `get_random_vector()` is chosen per life, then added to
  yaw, pitch and roll once per equivalent retail 60 Hz update. State is keyed by
  player generation so an id reuse cannot move or explode the wrong life.
- Flying corpses draw the class body/head/legs without the stale held weapon,
  use `JP_death_fly` followed by `JP_death_explode[_water]`, and keep the local
  camera on the rising Character until packet 36. The subsequent authoritative
  team-coloured grave entity retains the existing gravity/bounce camera path.
- `tests/test_jetpack_death.cpp` covers pack ids 66..69, deterministic spherical
  spin, the 60-update fuse, authoritative position updates, id-generation
  rejection, packet-36 consumption and teardown. Strict `/W4 /WX` build and
  90/90 CTest targets pass.

## Update 2026-08-02c: announcement FIFO, overlapping prefabs, pickup twinkles

This change is client-only; `G:/AoSRevival/BattleSpades` was inspected as a
read-only protocol target and was not modified. No GUI client was launched.

- `ChatMessage(49)` and `LocalisedMessage(50)` type 3 now use the recovered
  `bigMsgList_text`/`bigMsgList_time` behavior: the current message receives
  its complete four-second presentation, six later messages are retained in a
  bounded FIFO, and packet-50 override clears both lists. This packet is still
  unrelated to `ShowTextMessage(73)`: later IDA work disproved the old
  "loading-menu status" label and confirmed packet 73 selects the authored
  ViewScores/ViewGameStats end-of-round message.
- Prefab placement no longer treats an occupied authored voxel as collision.
  Existing cells attach the shape and cost zero; only in-bounds air cells count
  against the visible block stock and enter the delayed colour-ack ledger. A
  fully occupied no-op or clipped footprint still fails closed. The read-only
  server already debits committed empty cells only, but its pre-admission guard
  still compares stock with the full authored footprint; the narrower
  low-stock overlap case cannot be changed from this client without a server
  change and remains explicitly out of scope under the compatibility boundary.
- Supply entity deletion for retail types 3..6 now emits `Crate.delete`'s 25
  additive, gravity-free, non-colliding two-second particles from the retained
  authoritative entity position. The renderer uses the shipped 4x4
  `PickUp_Twinkle_anim_4x4.png` atlas, preserving its blue/white ramp.
- Verification: strict `/W4 /WX` native build and 86/86 CTest targets. The
  executable is in `out/build/native-dev/src/RelWithDebInfo`; it was not copied
  into `dist/bin`.

## Update 2026-08-02b: packet-synchronised impact particles and VXL sound cover

This change is client-only; `G:/AoSRevival/BattleSpades` was not modified.

- `ShootFeedback(8)` now follows retail's observer path: it replays the seeded
  scenery hitscan without mutating VXL, producing positional contact audio and
  block-hit particles before a block actually breaks. The local network
  shooter uses the same cosmetic path.
- `Damage(37)` remains the only terrain authority. A bounded player/cell/world-
  loop ledger suppresses duplicated sublethal particles and contact audio;
  destructive acknowledgements retain the distinct block-break burst/sample.
- Shoot seeds use CPython 2.7-compatible MT19937 seeding and three axis draws,
  matching the recovered `Character.shoot` expansion instead of an unrelated
  C++ hash.
- Positional one-shots, weapon loops and packet-created loops are attenuated
  through five read-only rays against the live VXL. Full cover uses an 18%
  transmission floor, apertures interpolate, and the retail 50-block hearing
  cutoff avoids needless long traces. UI/music/ambient-bed audio is unaffected.
- Verification: strict `/W4 /WX` native build and 85/85 CTest targets. No GUI
  client was launched because the user had an active gameplay session. The
  built executable is in `out/build/native-dev/src/RelWithDebInfo`; it was not
  copied into `dist/bin`.

## Update 2026-08-02: prefab cost HUD and retail-speed moving skies

This change is client-only; `G:/AoSRevival/BattleSpades` was not modified.

- Selecting a prefab now replaces the weapon-ammo row with its authored
  330x330 shape preview at retail `image_scale=0.25` and its true KV6 voxel
  cost. Affordability is yellow/red against the live block stock, while the
  normal block-stock panel stays visible below. The already-resident placement
  KV6 supplies the cost, so the HUD performs no per-frame disk reads.
- The layered skydome animation now matches retail timing. Native
  `SkyDome.draw` increments `time_counted` once per rendered frame; the old C++
  renderer incorrectly supplied seconds, slowing every authored UV speed by
  about 60x. The renderer now supplies equivalent 60 Hz draw ticks derived
  from a monotonic clock, preserving retail speed without refresh-rate drift.
- Verification: strict `/W4 /WX` native build, 82/82 CTest targets, staged
  executable headless smoke for 120 ticks. SHA256:
  `5F901049255251FCC38E41F66032ABD3AC429150907F14DBE5C331690EEEFA34`.

> Profile progression recovery is documented in
> `docs/PROGRESSION_RECOVERY.md`. The native client now uses authenticated
> `/api/profile/me`, the full retail stat/rank tables, and RankUps(66); do not
> replace the per-class/per-mode ranks with a total-score threshold.
>
> The proposed uncapped account XP, free level crates, cosmetic inventory,
> auditable opening, and AoSPlay API/database contract are frozen in
> `docs/ACCOUNT_PROGRESSION_INVENTORY_CRATES.md`. Keep the recovered retail
> mastery system separate from this new lifetime account level.

## Update 2026-07-30b: native identity gate, logout and game tickets

This subsystem is client-only. `G:/AoSRevival/BattleSpades` remained an
immutable compatibility target and was not modified.

- The native process now begins on `FrontendScreen::identity`. A bounded worker
  refreshes any existing launcher session; otherwise the original Select Menu
  backdrop presents Sign In, Register and Play as Guest before Main becomes
  reachable. Clear passwords remain inside the form model and never enter a
  renderer draw command or log.
- `RevivalIdentityService` implements the recovered AoSPlay register, login,
  account refresh, logout, signed guest and game-ticket contracts. Its
  version-one `%LOCALAPPDATA%/AoS Revival/launcher_state.json` is wire/storage
  compatible with the Python launcher. Windows bearer tokens and guest seeds
  are protected with DPAPI using the original credential description; writes
  replace atomically. Network responses are capped at 64 KiB and time out
  after five seconds.
- Registration validates the public username/password rules locally and
  requires the player to acknowledge the one-time recovery code before
  entering Main. Guest creation prefers the signed online flow and safely
  persists an unranked offline guest if AoSPlay is unavailable.
- Main has a distinct Logout action below the player name, horizontally at the
  same retail design-space height as Quit. It revokes the online session where
  possible, always clears local identity state and returns to the gate.
- Public discovery retains the AoSPlay server identifier and recognizes the
  explicit `identity=ticket-v1` tag. Only such a server receives a freshly
  fetched 15-byte one-use join code as the Protocol 168 name. Untagged public
  servers, LAN replies and direct endpoints keep the legacy visible nickname,
  preserving stock compatibility. Map transitions request a new ticket, and
  generation/request checks discard stale asynchronous results.
- `AOS_REVIVAL_STATE_PATH` is a diagnostic/test override used to launch a true
  first-run UI without replacing the player's real state file.
- Verification: strict `/W4 /WX` native build, 79/79 CTest targets, live
  authenticated refresh (`KikoTs`, registered/ranked), and the isolated
  first-run screenshot at `out/evidence/identity-gate.png`. The authenticated
  Main/Logout screenshot is `out/evidence/identity-main-menu.png`.

## Update 2026-07-28: HUD/minimap protocol parity

Compatibility direction is fixed: `G:/AoSRevival/BattleSpades` is a read-only
target built for the retail client. Do not alter it for this port. All
adaptation belongs in `BattleSpadesClient`.

The live client now installs/removes server entities from packets 21/19 and
uses them for minimap pickups, bases, graves and deployables. Strict decoders
and bounded retained state landed for packets 41-44, 83 and 117. Packet-43
zones render their recovered pulsing rectangle/icon, with exact zone-icon
ordinals. Packet 17's action-8/action-9 variable tails now drive high minimap
visibility/chase state; high-visibility players bypass ordinary enemy culling,
use `map_vip_player_16`, and suppress the carried-tool icon in the same branch
order as retail. CTF's opposing-team 90px carried-intel corner icon is wired at
the recovered minimap-dependent coordinates.

The HUD also has the live 512x512 VXL overview, cropped/full map frames, player
cone, entity markers, height arrows, kill feed, CHAT_BIG, respawn overlay and
damage-direction flashes. Packet 117 now drives the exact retail
TeamProgressBar: shared denominator/show mode, inverted values and zero reset,
unknown-icon retention, custom team tints, 280x40 frame, 32px icons and the
original double-drawn Spades-20 labels. Packet 106 now drives the retail
TerritoryBasesHud state machine and presentation: all eight actions, strict
team/action validation, vertical 32px spacing, containing-player scaling,
partial capture overlay, contested pulse, and A-J letter art. Exact evidence
and remaining gaps are in `docs/HUD_RECOVERY.md` and
`docs/HANDOFF_HUD_AND_ADS.md`.

Verification: `/W4 /WX` build clean; 73/73 CTest tests pass. Verified binary:
`out/build/native-dev/src/RelWithDebInfo/BattleSpadesClient.exe`.

Open next: packet-41 3D billboard rendering, full-map overlay quads, zombie
heart marker scaling, TeamProgress particle bursts, then screenshot comparison
against retail.

## Update 2026-07-25b: per-map atmosphere, quality tiers, and the KV6 normal fix

**Per-map atmosphere.** `world/map_atmosphere.{hpp,cpp}` derives a map's lighting
from its own shipped sky assets: it parses the skydome definition, decodes the
skysphere's sky-gradient TGA, and integrates it cosine-weighted over the
hemisphere for ambient, then locates the sun or moon layer and pushes its
centroid through the renderer's exact authored-transform bake for a key
direction. Pure with respect to the GPU — no bgfx, no image library — so all 27
shipped domes are verified headlessly by `aos_map_atmosphere_tests`.

Two measured facts made this work. The skysphere UV layout is identical across
every dome and **the horizon sits at V = 0.891, not the midpoint** — the sky
occupies 89% of the V range, so sampling the middle row reads well above the
horizon. And sampling the horizon band reproduces retail's own authored
`FOG_COLORS` to within 1-2/255 on most maps, which is strong evidence the artists
eyedropped that exact band. Where it disagrees by more than ~32/255, retail is
placeholder data (`(30,30,30)`, `(0,0,0)`, WW1's colour copy-pasted onto WW2), so
the derived value is better than retail's.

Colours are stored as **unit-luminance chroma plus a separate level**. That split
is the whole trick: measured sky luminance spans 8/255 (LunarBase) to 194/255
(Classic_B), a 24x range, and applying it literally makes six maps unplayable.
Flooring the level while leaving chroma untouched keeps Tokyo blue and legible.
Fog is now exp-squared and fades toward the colour the sky paints in that
direction, so terrain dissolves into the dome instead of into a flat colour that
cut a seam along the horizon.

One deliberate NONRETAIL choice: a dome with no sun layer still gets a weak
(0.22) off-axis key. Hemispheric ambient alone separates only up from down, so on
a uniform sky like Classic_B every wall of a corridor lit identically and the
voxel geometry stopped reading.

**Quality tiers, menu-exposed.** `render::QualityProfile` (`render/quality_profile.cpp`,
compiled into `aos_settings` so it stays GPU-free and testable) is the single
source of truth for Legacy / Low / Medium / High / Ultra. `ShaderQuality` gained
`ultra`; the Graphics tab's Shader Quality row grew from three options to four,
and the recovered Compatibility Shader toggle keeps ownership of
`compatibility` = Legacy. While the toggle is on the tier row greys out and reads
`LEGACY`, so two rows editing one field can never disagree.

Row *structure* is unchanged deliberately: dropping the toggle would take
Graphics from 9 rows to 8, which flips `needs_scrollbar()` and reflows every
row's width from 442 to 474 — a whole-tab regeneration. Growing a value list
changes nothing, because a `choice` row renders one centered string and never
reads the option list.

`WorldRenderer::TerrainLighting` is replaced by the full profile. Only
`enhanced_lighting` and `effect_scale` are consumed today; shadows, SSAO, bloom
and dynamic lights are carried and reported in the F3 overlay but change no
pixels until their passes land.

**Three real bugs fixed:**

1. **KV6 models stored four of six face normals wrong.** The loader writes
   positions through retail's `(x, y, z) -> (x, -z, y)` swizzle but stored the
   face index in unswizzled KV6 axes, so `-y` decoded as `-z`. Invisible only
   because models render unlit; the moment lighting turns on, a player's flank
   would shade as though it were their head. Fixed with the permutation
   `{0,1,4,5,3,2}`, provably a no-op today, pinned by `aos_kv6_normal_tests`
   which derives the permutation from the position swizzle independently.
2. **Ultra was silently downgraded on disk.** `shader_quality_name` had no
   `ultra` case and fell through to `return "medium"`, so saving wrote the wrong
   tier with no error; `parse_shader_quality` had no `"ultra"` branch, so a
   hand-edited file failed to load entirely. `aos_settings_tests` now round-trips
   all five tiers through save and load.
3. **The Compatibility Shader toggle destroyed the tier choice.** Switching it
   off hard-wrote `high`, which would demote Ultra every time. It now restores
   the tier the player actually selected.

Also: selecting MSAA **2x silently gave 4x** — every read site collapsed
`Antialiasing` to a bool and `reset_flags` mapped any truth to `MSAA_X4`.

**View graph renumbered** to reserve ranges for what is coming: 0 backdrop,
1-4 shadow cascades, 5 world, 6 viewmodel, 7-22 post, 23 composite, 24-25 UI.
UI views sort after the composite, which is what will keep the HUD out of
tonemapping and bloom with no UI shader changes. Static asserts pin the ordering.

**Build layout:** `out/` was 21 GB across 25 directories; 6.2 GB of regenerable
build output and smoke runs removed. Kept `vcpkg`/`vcpkg_installed` (hours to
restore), `shaderc-build` (holds the shader compiler), and all evidence —
`out/parity/retail` is the retail screenshot ground truth. A self-contained
playable folder is produced at `dist/` by `cmake --install`.

Verification: 60/60 CTest tests pass on the warnings-as-errors native build.

## Update 2026-07-25: particle system, GPU lighting, and the weapon audio port

Three subsystems landed. All 57 CTest tests pass on the warnings-as-errors
native build; visual evidence is in `out/smoke/vfx-20260725-r2`.

**Particles.** `world/particle_system.{hpp,cpp}` is a 4096-slot allocation-free
ring with gravity, drag, map collision, sprite-sheet animation and
back-to-front sorting. It is headless and owns no GPU resource, so burst
geometry is unit-tested directly. `world/particle_effects.cpp` authors the
compositions: explosion, block break, structure burst and rocket trail.
Rendering is hardware-instanced billboards submitted inside `world_view_id`
after the opaque draws and before the viewmodel. This is the **first
alpha-blended and first additive draw path in the program** — previously the
only `BGFX_STATE_BLEND_*` uses anywhere were `BLEND_ALPHA` on the skydome and
UI sprites. Every view is `ViewMode::Sequential`, so submission order is the
only ordering guarantee: alpha buckets are sorted by eye distance, additive is
order-independent, and particles never write depth.

Particles occupy **no world-model slot**, so they are independent of the
24..63 effect range, which has zero headroom. `TerrainEffectSimulation` keeps
its recovered instance counts and `make_debris` unchanged; particles are a
parallel output behind an optional `set_particle_sink()`. That is why all
pre-existing effect assertions still hold.

`graphics.effect_quality` had **zero read sites** before this and is now live.

**A real bug fixed:** `make_debris` computes
`std::min(available, std::max(1, ...))` where `available = 40 - active_.size()`.
The `min` defeats the `max(1,…)` floor, so a landing structure produced
**exactly zero** debris whenever the effect budget was full. The particle burst
does not share that budget and guarantees a floor. It also walks the whole
component with `index % N` instead of striding from index 0, which biased every
burst toward the flood-fill seed corner.

**GPU lighting.** `ChunkVertex.face` and `.occlusion` were uploaded per vertex
in `Color1` and consumed by nothing — four dead bytes that are exactly what a
lighting pass needs. `varying_world.def.sc` now exposes `a_color1`, the vertex
stage reconstructs the exact face normal from a six-entry table, and shading
moved off the CPU: `ChunkMesherConfig::bake_shading` defaults to **false** so
the mesher emits pure albedo. `classic` mode reproduces the recovered tables
(`{0.85,0.85,0.75,0.75,1.00,0.60}` by face, `{1.0,0.80,0.65,0.50}` by
occlusion) per pixel, `enhanced` does key + fill + hemispheric ambient + AO +
specular with an extended-Reinhard highlight roll-off. Switching modes costs
**no re-mesh**; `graphics.shader_quality == compatibility` selects classic.

Shading is set **per draw**. Only chunks are lit: KV6 models store vertices in
retail render space (`x-px, -(z-pz), y-py`), so their `face_index` is in
pre-swizzle axes and would produce wrong normals. Effect cubes and the
viewmodel bake their own shade. Lighting world models needs the KV6 face
indices remapped (`2→4, 3→5, 4→3, 5→2`) and is deliberately not done here.

**Weapon audio.** See `docs/WEAPON_AUDIO.md`. The root cause of silent guns was
a schema gap, not a generator bug: retail sets `shoot_sound = BLANK_SOUND` on
loop-fire weapons *on purpose* because the sustained loop lives in `update()`.
`WeaponSoundSet` adds 13 roles filled from `RECOVERED_SOUND_SETS`, recovered by
reading all 71 retail weapon modules. `aos_weapon_audio_map_tests` fails the
build if any cue names an OGG that is not on disk, if an automatic weapon has
neither a shoot sound nor a fire loop, if a loop lacks a tail, or if a melee
tool is missing its swing/impact cues.

The hand-maintained six-entry `automatic_groups` table in the adapter is
**deleted** — it was data that would drift on every regenerate. Automatic fire
now drives a real loop/tail state machine over a dedicated four-source loop
pool (one-shot acquisition steals the oldest voice and would cut a sustained
loop off mid-burst). Molotov's two hard-coded special cases are gone; every
throwable carries its own recovered cue.

Two further audio bugs fixed: world weapon cues were emitted at
`SoundPosition{}` (the map origin) while the listener sat at the eye, so every
shot was attenuated by the player's distance from the map corner; and `stop()`
left `optional_sound_cache`, `next_optional_handle` and the weapon group arrays
populated while deleting every buffer they named, so a restart replayed
dangling handles.

### Still open

- Deferred/HDR pipeline proper: G-buffer, cascaded shadow maps, SSAO, a float
  framebuffer with bloom, volumetrics, SSR and TAA. The current roll-off is a
  cheap stand-in for tonemapping inside the existing forward pass.
- World-model and viewmodel lighting (needs the KV6 face-index remap above).
- Per-shot pitch randomisation (retail: `2^(semitones/12)`, gunshots ±0.8 st)
  and `alListenerfv(AL_ORIENTATION, …)`, which is never called, so stereo
  panning is locked to OpenAL's default basis.
- The recovered `des_split_*` / `des_imp_*` / `bullet_break_*` groups: the new
  `structure_split` and `block_break` sound kinds currently fall back to
  `block_debris.ogg`.
- Multi-phase tool loops (drill projectile vs drilling, chemical bomb dissolve
  vs burn); only the primary loop of each is modelled.

## Update 2026-07-22: recovered aiming, melee footprints, and explosives

RMB now follows the recovered per-tool split instead of acting as universal
ADS. A real sight is required; sniper tools use their recovered magnification
and look factors, ordinary guns do not aim, and blocks, prefabs, paint, C4,
the mounted MG, and special spades retain their real secondary actions.
`Character.draw_sight` was recovered at macOS address `0x3c0d0`: it resets the
matrix and draws only the sight KV6 at scale 0.05, offset
`(sight_x+.025, sight_y-.35, sight_z+1.85)`, after a 180-degree Y turn. The
`--tutorial-tool ID --tutorial-aim` developer options make graphical evidence
deterministic rather than relying on focus-sensitive synthetic input.

Minigun spin uses the recovered interval ramp (`0.3 -> 0.1`, active
`-0.15/s`, decay `+0.075/s`), 0.5 firing threshold, five-unit cap and
accumulated barrel rotation. RMB pre-spins without consuming ammo.
The F4/all-weapons renderer now composes pullout explicitly: the timer offsets
X/Y by `-5 * remaining` and never enters sway Z. The old positional aggregate
wired the timer into depth, which made hands jump/twist on acquisition and
every wheel selection even though the weapon runtime itself had reset. The
evaluated pose now carries that sway explicitly and both the weapon matrix and
arm hierarchy consume it, preventing the weapon from appearing before its grip.
Melee footprints are explicit: Pickaxe/Knife/Crowbar are single-cell; Spade and
Classic Spade are three-cell Z columns; Super Spade and Zombie Hands are
centered 3x3x3 cubes; Machete accumulates on two cells; UGC Super Spade is
single-cell on LMB and cubic on RMB. Classic RMB keeps its cancellable
0.8-second wind-up.

Normal, Classic and AP grenades bounce; Molotov, Chemical and the grenade
launcher explode on contact; Sticky sticks and arms for five seconds. Throws
add player velocity and use gravity 30 plus the recovered speed curves. The
launcher is pinned to speed 75, lifetime 3, crater radius 4 and block damage 6.
Projectiles render authored KV6s, use swept collision, mutate/collapse the
canonical VXL, and dispatch bounded explosion/fire/chemical particles plus
`explode.ogg`. `AnimThrowGrenade` uses the recovered x/y/pitch curve and
charged throwables retain its completed pose.

Verification: the native warnings-as-errors build succeeds and all 49 CTest
tests pass, including fuse/effect dispatch, launcher constants, all grenade
release types, scope transforms, minigun rotation, Classic/UGC spade RMB and
Zombie Hands terrain footprint. Canonical real-window PASS evidence is in
`out/smoke/aim-20260722-r4` and `out/smoke/grenades-20260722-r7`.

## Update 2026-07-21: all-tools wheel crash and minigun multipart correction

The first F4 playtest exposed two independent defects. Opening the wheel fed
the HUD all 65 developer slots, including valid retail tools with an empty
`toolbar_icon_asset`; the UI texture loader correctly rejects an empty path and
the frontend stopped, which looked like a crash. `GameHudPresentation` now
omits only the absent icon (the selection frame remains), and the developer
toolbar presents a moving ten-slot window instead of a 5,200-pixel strip. The
real `RetailInventory` selection stays global and all 65 tools remain reachable.

The minigun body/barrel catalogue offsets were correct, but the viewmodel
oracle missed `MinigunWeapon.__init__`: only part 1 receives initial position
`(0,-0.3,1.1)`. That exact recovered pose is now applied before `AnimRoll`;
the body remains fixed and only the offset barrel spins. Regression tests cover
the complete 65-row icon set and both minigun part transforms. The MSVC native
warnings-as-errors build and all 49 tests pass.

## Update 2026-07-21: Tutorial weapon sandbox, projectile presentation, and target collapse

The playable Tutorial now exposes the complete generated 65-tool catalogue.
Press **F4** once to install the developer all-weapons inventory (a second
press restocks it), use **1-0** for direct slots or the mouse wheel to traverse
all tools, **R** to reload, **RMB** to scope only on snipers (or use a tool's
own secondary action), and **E/MMB** for the recovered
weapon-custom action. The sandbox owns real magazine/reserve state through
`PlayerInventory` and `WeaponRuntime`; it does not invent a second ammunition
model. Camera recoil, zoom FOV, pellet spread, cadence, reload, and minigun
spin come from the generated retail/server catalogue. RMB can pre-spin the
minigun without firing or consuming ammunition; weapon-custom does not.

Projectile actions now create swept, fixed-tick `TutorialProjectile` objects
with recovered per-tool speed/gravity/lifetime, render the authored rocket,
grenade, drill, snowball, and mine KV6 assets in bounded world-model slots,
and damage the canonical VXL through the same impact/collapse path as hitscan
weapons. This is an offline Tutorial integration. The Protocol 168 action
adapter is wire-ready, but the live ENet session still must own multiplayer
projectile replication and server reconciliation.

Training target destruction was corrected from "remove only red voxels" to
one coherent red+white bullseye component. The metal stand remains. The
authoritative cells lose collision atomically and the captured face falls and
breaks through `TerrainEffectsPresentation`, matching the retail target
behavior. Detached effects previously fogged from local mesh coordinates,
which painted them with the sky color; `vs_world.sc` now computes fog from
`u_model * a_position`. All checked-in shader backends were regenerated.

Verification: the warnings-as-errors native build succeeds and 49/49 CTest
tests pass. The real-window smoke enters through the retail START gate, walks,
and returns safely; its automated F4 delivery is focus-sensitive in the Codex
desktop host, so the all-tools path is additionally pinned by complete-catalog
HUD/model/runtime tests. Visual evidence root for the earlier successful F4
capture: `out/smoke/weapon-sandbox-20260721-r5`.

## Update 2026-07-21: complete 65-tool runtime and wire-ready weapon port

All selectable Protocol 168 tools (IDs 0..64; 65 remains the sentinel) now
share one generated contract and one concrete `WeaponMechanism`. The generator
combines server-authoritative profiles with statically evaluated recovered
retail classes, `shared/constants.py`, `shared/constants_audio.py`, and
`aoslib/models.py`; retail Python 2 code is never imported at runtime. The
catalog includes exact ammo/reload/accuracy/damage/use tuning, 1,535 named
special constants, toolbar imagery, multipart first/third-person KV6 models,
sight/casing/tracer models, and sound-group identities. Conventional firearms
created by retail's `models.load_weapon()` loop correctly fall back to their
shared MODEL/VIEW_MODEL KV6 stem; minigun, turret and C4 retain their special
multipart/detonator compositions.

`world/weapon_runtime.{hpp,cpp}` now handles semi/automatic/shotgun fire,
three-round assault bursts, minigun spin-up, mounted-MG fire/deployment,
melee, grenade cooking, charged throws, launchers, block lines, flares,
prefabs, every deployable, C4 detonation, objectives, UGC, paint, Block Sucker,
disguise and reload. It emits semantic actions and never raycasts or mutates
the map. `world/weapon_models` loads every declared KV6 part into validated
meshes. `evaluate_weapon_view_model()` covers every tool using the recovered
weapon recoil, spade, generic melee, zombie hand, throw and place families.

`network/protocol168_tool_actions` adds strict round-trip codecs for
BlockBuild(32), BlockLiberate(35), DropPickup(71), UseCommand(86), MG/turret/
mine/dynamite/medpack/radar/C4 placement, DetonateC4(93), BlockSucker(94),
Disguise(95), flare(104), UGC(97), paint(7), and prefab build/erase(30/31).
Raw voxel and retail sign-magnitude 1/64 fields are deliberately not unified.
`protocol168_weapon_action_adapter` maps every semantic runtime edge to its
correct packet family; local-only animation/color/prefab-rotation edges emit no
network traffic and malformed/missing context fails closed.

Focused warnings-as-errors validation passes for the catalog contract, every
KV6 composite, all-tool state machines, all special packet layouts, the action
adapter and full-catalog viewmodel families. The complete native suite is
43/43 green and the paced 120-tick native executable smoke exits 0. The next integration boundary is
the live ENet session: connect player/loadout snapshots to one runtime per
player, perform session raycasts, upload the selected model parts, play catalog
audio/effects, and test owner/observer/late-join behavior against the server.

## Update 2026-07-21: retail inventory and wheel/hotkey behavior

Inventory is now an explicit renderer-independent system in
`world/retail_inventory.{hpp,cpp}`. Do not replace it with a Tutorial-only
three-tool enum: retail addresses one combined HUD index containing class
loadout entries, prefabs, and UGC tools while the selected entry separately
owns its protocol tool byte and variant.

Recovered `gameScene.pyd` entry points are `set_current_tool_index`
`0x101542e0`, `get_tool_index_on_mouse_scroll` `0x101573e0`, and
`on_mouse_scroll` `0x1015bc50`. The implemented contract is:

- keys 1-0 directly select combined slots 0-9 and never open the toolbar;
- wheel selection wraps, skips unavailable entries, respects empty-but-
  selectable tools and the character/tool wheel-consumption gate;
- wheel opens the retail one-second toolbar animation only when the existing
  HUD scale timer is idle;
- the protocol-visible selection changes immediately while Character's
  pullout animation runs independently for 0.5 seconds.

`TutorialSession` delegates selection to this inventory and exposes the
recovered block/spade/pistol order (tool IDs 5/2/17). `GameHudPresentation`
uses retail weapon frames, compact `png/ui/icons/weapons` assets, an 80-pixel
slot stride, normal frame/icon scales 0.25/1.3, selected scales 0.7/2.0, and
the captured/recovered window-relative toolbar geometry. The selected frame
does not shrink while the strip remains visible; a previous invented
0.18-second pulse was removed after direct capture comparison. Golden visual:
`out/parity/ours/inventory-wheel-final.png` against retail
`out/parity/retail/07-toolbar.png`.
Native preload includes the hidden-at-boot toolbar assets.

Verification on 2026-07-21: native build succeeds with warnings as errors,
35/35 native tests pass, and the paced 120-tick executable smoke exits 0.
The preceding headless run passed 33/33 tests.

## Update 2026-07-21: falling structures, block impacts, and catalog audio

The earlier collapse queue is now presented by
`world/terrain_effects.{hpp,cpp}`. The authoritative VXL mutation is unchanged:
unsupported colors are captured and collision disappears atomically before a
cosmetic object exists. The frontend drains every `FallingComponent`, uploads
one internal-face-culled mesh, applies recovered per-tick gravity and random
rotation, and replaces it after collision/one second with a bounded colored
voxel-debris sample. Retail limits are preserved: 8,000 source voxels, sampling
modulus 5/15, medium/large sound thresholds 15/80, and 0.125 impact chips.
Effects occupy renderer slots 24..63 and cannot exceed 40 live instances.

Tutorial block hits now emit a typed impact for every bullet or melee contact,
including the original surface normal/color and whether it destroyed the cell.
Sublethal cells still use the canonical VXL damage fraction and darken on the
next boundary-aware chunk remesh. The frontend plays the recovered four-way
`bullet_hit_001-004` group, `hitground`, and `block_debris` positionally.
OpenAL also predecodes the generated 65-tool shoot/reload groups, including
the separately recovered loop samples for SMG, Tommy Gun, Classic SMG,
Automatic Pistol, and LMG; the F10 weapon lab consumes real runtime actions to
exercise these bindings. Specialized pickaxe/super-spade/zombie/crowbar/knife/
machete/riot hit and break assets are loaded and have stable handles for the
network gameplay integration.

Verification: warnings-as-errors native build succeeds; 49/49 tests pass,
including new mesh-culling, gravity, breakup, sound-event, and flood-bound
tests. The native executable remained responsive through expanded audio
startup, loaded Training.vxl, and entered the playable Tutorial world using
real keyboard/mouse input.

## Update 2026-07-21: retail viewmodels, terrain damage/collapse, Protocol 168 terrain core

The previous fitted first-person rig has been replaced with a tested semantic
oracle in `world/retail_view_model.{hpp,cpp}`. `character.pyd` supplied the
exact arm hierarchy, shared anchor, viewmodel scale, character offset, and
180-degree handedness flip; decompiled tool/animation modules supplied block,
spade, digging-pitch, and pistol curves. `native_frontend_module.cpp` consumes
those values instead of owning fitted pivots. See
`docs/research/GAMEPLAY_PARITY_AUDIT.md` for addresses and recovered constants.

Terrain now has sparse pre-break damage, deterministic darkening, 18-neighbor
support discovery, z=239 grounding, fail-safe work budgets, atomic collision
removal, color capture, and Tutorial integration. The later update above wires
the captured components into the bounded falling/debris presentation.

The new `aos_protocol` library strictly decodes/encodes SetColor(11),
BlockBuildColored(33), Damage(37), and BlockLine(40). Its stateful terrain
replica enforces SetColor-before-BlockLine, reproduces native `cube_line` tie
ordering, mutates the canonical VxlMap, and emits boundary-aware dirty chunks
plus falling components. This is not yet attached to ENet: handshake, MapSync,
players/entities, and outbound actions remain next milestones.

Verification on 2026-07-21:

- headless dev at that tranche: 32/32 tests passed;
- native SDL3/bgfx/OpenAL dev at that tranche: 34/34 tests passed;
- linked executable:
  `out/build/native-dev/src/RelWithDebInfo/BattleSpadesClient.exe`;
- both configurations compile with warnings as errors.

The repository still has no initial commit and every source file appears
untracked. Do not use `git diff` as the work inventory and do not stage/commit
unless the user explicitly requests it.

## Update 2026-07-20 (night): pause menu, KV6 viewmodels, tool loadout, live retail reference rig

**Reference rig**: `G:\AoSRevival\aos-nonsteam\build\exe.win32-2.7-cp0-fixed`
runs the retail client (launcher -> Enter joins the local server; real input
via SendInput, synthetic WM clicks do NOT work on it) with
`server\BattleSpadesTutorial.exe`/`BattleSpades.exe` beside it. Ground-truth
captures live in `out\parity\retail\` (loading ready-state, choose team,
choose class, full in-game HUD, toolbar, escape menu); our captures in
`out\parity\ours\`. Drive/capture helper: scratchpad `drive.ps1`
(`-ClickX/-ClickY` design coords, `-Key`, `-Capture`); pose-iteration loop:
`iterate-tools.ps1`. IMPORTANT: our Tutorial square hit box is at design
(299, 477) — the model y is bottom-origin retail data (600-123).

New this session:
- **Pause menu** (`frontend/pause_menu.{hpp,cpp}`): recovered EscapeMenu —
  265x55 buttons at x 267.5, tops 172/234/296/358/420 (resume/class/team/
  settings/disconnect), pause_menu_frame 530x644 @0.64 centered (400,300),
  "MENU" Spades 46. Tutorial hides class/team (gap preserved, retail-像).
  Escape opens it (menu_backA) / Escape or Resume closes (confirm); Settings
  pushes the settings screen and `close_settings()` returns THERE (not to
  Select) when the tutorial is beneath; Disconnect resets home and the
  route-exit hook tears the world down + restores menu music. The world
  keeps rendering behind it (world pass now gates on tutorial_world being
  anywhere in the navigation stack).
- **KV6 pipeline** (`world/kv6_model.{hpp,cpp}`): byte-verified SLAB6 loader
  (Kvxl header, 8-byte voxels, xlen/ylen run tables, optional SPal suffix
  ignored; spade 2356 = 32+188*8+24+24+772 exact), meshed to the shared
  ChunkVertex cube format with pivot-relative positions.
- **Viewmodel pass**: new view id 2 (view contract now clear 0 / world 1 /
  viewmodel 2 / ui window 3 / ui canvas 4), depth-cleared, identity view +
  tight near/far, per-tool explicit rotation BASIS (Euler mtxSRT composes
  awkwardly for kv6 axes — poses in native_frontend_module
  view_model_pose_for are NONRETAIL, tuned by screenshot; fog uniform
  recentered on the tool so it never fogs).
- **Tool loadout in the session**: recovered grants (empty -> pistol at
  SHOOTING -> +block+spade at CLIMB, grant auto-equips the final item);
  keys 1/2/3 equip block/spade/pistol, crosshair shows only with a tool
  (retail per-tool enum), F4 = developer full-loadout grant for visual
  iteration (not a retail path).
- Loading screen: bullet-row progress bar (33 slots, dark unfilled), BACK
  label; health bar order fixed (opaque frame first, team-tinted fill on
  top - PROVEN by pixel-probing the PNGs) plus centered white "100".

Still open from the user's list (extraction workflow re-running in the
background for exact specs): retail constants mirror module, toolbar/
inventory HUD widget (slots 1-8 visuals), team indicator + timer + SCORE
frame + ammo/blocks counters + minimap, Choose Team/Choose Class screens,
firing + target destruction (SHOOTING stage gate), viewmodel animations.

## Update 2026-07-20 (later): 1:1 parity pass after the second playtest

The second playtest reported HUD widgets and the loading screen "way off",
menu music leaking into loading/world, and missing ambience. A per-element
audit (retail decompile + hud.pyd/gameScene.pyd IDA vs our sources) produced
exact diffs; all are now applied and 31/31 tests pass.

- **HUD is window-relative now (root cause of "widgets way off")**: retail
  draws the in-game HUD in live window pixels (hud.pyd multiplies the runtime
  window width by 0.5; helpPanel.py uses window.width/height), while we had
  laid it on the letterboxed 800x600 canvas — displaced/scaled at every
  window size except 800x600. `GameHudPresentation` now emits
  `DrawSpace::window_pixels` with the recovered formulas: health frame
  {W*0.5-119.5, H-47, 239, 34}; crosshair {W*0.5-8, H*0.5-8, 16, 16} never
  scaled; help panel max width W*0.3 with the backing hugging the widest
  shaped line (measured through a new `measure_text` context hook wired to
  the Spades rasterizer), body LEFT-aligned, Spades.ttf metrics
  (asc 1930/2048, desc 647/2048 of 20px) for row height, resting flush with
  the top edge, slide distance T+90. The invented "ESC BACK F3" caption
  moved into the F3 debug overlay (retail has no HUD caption). Tests assert
  the geometry at 800x600 AND 1920x1080.
- **Loading screen rebuilt to the retail draw list** (decompiled
  loadingMenu.py, bottom-origin converted): ugc_splash 768x576 letterboxed
  on the canvas; ui_frame_large {24.96,5.28,750.08,589.44};
  game_loading_bar_bg {60,446.76,423.04,58.24}; LOADING title Spades 46
  baseline ~60; tab_bg + `loading_mapimage_tut` {59.52,135,680.96,304};
  MAP-only 224x42 tab at x=7 stride 228 with the Edo 16 label centered 48px
  right of frame center; map title "Tutorial" white Spades 48 top-left in
  {83,163,360,60}; status Spades 24 centered in {405..720} with retail
  wording ("Receiving map Training..." etc., EMPTY when ready); START
  {492,449,246,58} (was 58px high) with dark Spades 36 text, ready art
  (button_large_ready_*) and the 0.4 s pulsing button_large_glow; progress
  drawn LAST over everything at {66,451,414,50} with the loading_bar_bullet
  head at the fill edge; BACK strip at {54,541,135,32}.
- **Retail START gate restored — no auto-enter**: the pump now completes the
  full arc (receiving -> map_progress -> syncing_map -> preload ready ->
  bar pinned at 100%%, START glowing, status empty) and stashes the uploaded
  world in `tutorial_ready_map`. Clicking START stops the menu music
  INSTANTLY (recovered loadingMenu stop_music(True)), plays menu_confirmA,
  creates the session (INTRO help reveal), starts `tutorial_music_001` and
  the amb_rural bed, and replaces to tutorial_world. BACK and tab clicks are
  hit-tested on the loading screen.
- **Audio lifecycle per the decompiled media manager**: mainmenu.ogg loops
  through the entire loading screen (retail-correct — do not "fix" this);
  Training's ambient bed = skybox Classic_B -> **amb_rural** (global,
  volume 1.0, NOT scaled by music volume), fading in at 0.03/tick; leaving
  the world stops ambience instantly and crossfades back to menu music
  (outgoing track fades at the recovered 1/6.5 volume/sec on a dedicated
  fade source); duplicate play requests for the already-playing track are
  no-ops (retail dedupe — the server's ~1 s PlayMusic replay); music_gain
  default corrected to retail 1.0. Note: the playtest's music/ambience
  symptoms predated any in-world audio wiring (stale binary) — the wiring
  now matches the recovered state machine exactly.
- Training fog color double-checked: FOG_COLORS[Classic_B.txt] ==
  (111,215,223), identical to Grassland — our world fog was already correct.
- **Retail skydomes are now rendered, not approximated by the clear colour.**
  Protocol 168 packet 51 is a NUL-terminated safe basename such as
  `Tokyo.txt`; it is retained during bootstrap and can update while ready.
  Each `mesh/<name>/<name>.txt` definition supplies an ordered `render_list`
  plus scale/rotation/translation/UV speed for layered `.aos` meshes. The
  `.aos` records contain a mesh name, unindexed 9-float vertices and a TGA
  name. The native renderer loads those records transactionally, scrolls UVs
  in the dedicated skydome shader, centers the dome on the camera and blends
  it before terrain. `StateData(45)` supplies the matching fog/clear RGB.
  Unsafe or missing live selections leave the current dome intact. Tutorial
  explicitly selects `Classic_B.txt` because it has no packet-51 sender.
  The live graphical smoke against `88.80.155.252:38888` rendered the authored
  mountain/cloud atmosphere through movement with 54/54 tests passing.

## Mission and non-negotiable boundaries

This repository is the clean-room C++ client for Ace of Spades: Battle Builder.
The goal is retail-compatible presentation and gameplay with modern native code,
not an OpenSpades 0.75 client reskin.

- BattleSpades server and its recovered Protocol 168 behavior are authoritative.
- Do not substitute OpenSpades packet IDs, layouts, physics, dimensions, or
  assumptions for Battle Builder behavior.
- OpenSpades is a useful renderer, VXL, camera, audio and engine-organization
  reference only. It was cloned beside this repository at
  `G:\AoSRevival\openspades`, commit
  `ff9b3e71b9ad26dda940923515de8b46f4bba5a5`.
- OpenSpades is GPL-3.0. Do not copy or link its implementation into this client
  without an explicit licensing decision. Reimplement behavior independently
  using the retail source and BattleSpades server as primary evidence.
- The server repository is `G:\AoSRevival\BattleSpades`.
- The retail/source client and assets are under `G:\AoSRevival`; read the
  project research documents before repeating reversal work.

## Update 2026-07-20: retail gameplay HUD + offline tutorial lessons

The Tutorial now plays with the retail HUD and the recovered lesson script:

- `frontend/game_hud.{hpp,cpp}` + `aos_game_hud_tests`: renderer-neutral
  `GameHudModel`/`GameHudPresentation` on the 800x600 canvas. Retail
  five-image crosshair (all 16x16 `png/ui/target_*.png` centered on the
  screen center) gated exactly like retail on the equipped tool — an empty
  loadout draws none, so the movement lessons are crosshair-free until the
  pistol milestone flips `set_crosshair_visible` and adds the
  accuracy-driven corner spread. Health bar + frame at the RECOVERED layout
  (hud.pyd `draw_healthbar` @0x1009b910: center-anchored 239x34 frame at
  (window.width*0.5, 30) bottom-origin -> top-left (280.5, 553) on the
  canvas; the fill scales horizontally by hp/100 via glScalef — NOT a clip —
  and is tinted with the player team color, Blue team 1 = (44,117,179);
  tutorial pins 100 so the tinted fill spans the frame), and the recovered
  HelpPanel: max width 30%% of
  the canvas, padding 20, line spacing 5, Spades 20pt, MENU_FONT_COLOR
  (244,236,187), backing quad rgba(0,0,0,150) via a runtime-generated
  1x1 white texture (`runtime/white-pixel`, created in start()), sine slide
  from the top at 8%%/frame, swap-out cue on set_text and the appear cue when
  the recovered 0.35 s delay expires (`tutorial_disapp`/`tutorial_app` now
  loaded by the OpenAL adapter). `toggle_hud` binding hides world-anchored
  widgets but never the help panel; `tool_help` (default H) toggles the panel.
- `world/tutorial_lessons.{hpp,cpp}`: line-faithful port of the server's
  tutorial stage machine — INTRO (3.0 s) -> BASIC_CONTROLS (min lane-local
  x <= 135) -> JUMP (jump seen && x <= 128, or x <= 119) -> CROUCH (crouch
  seen && x <= 108, or x <= 99) -> SHOOTING/CLIMB (blocked on
  `advance_external()` until weapons/building exist) -> COMPLETE. Minimum-x
  is sticky (backtracking never regresses), transitions are monotonic.
  `TutorialWorldSession` drives it every tick and surfaces stage entries
  through `take_entered_stage()`.
- Verbatim recovered english.py strings live in `tutorial_string()`;
  `resolve_control_placeholders()` renders retail bracketed key names from
  the live Controls bindings ("Use [W], [A], [S] and [D] to move.",
  "[H] Close").
- The frontend shows INTRO on world entry, swaps panels on lesson events,
  plays the panel cues, and the tutorial loading screen already gates to the
  MAP-only tab set for Training. Minimap and palette are correctly absent:
  the recovered tutorial InitialInfo disables both.
- Suite: 31/31 tests green (adds `aos_game_hud_tests`; session tests now
  cover the lesson-event handoff).

The compiled hud.pyd extraction (all 756 functions decompiled) recovered the
constants future HUD widgets need — record for the next milestones:
crosshair hit flash (230,40,79) for 0.25 s; per-tool crosshair enum
(NEVER=0/ZOOMED/UNZOOMED/ALWAYS/HAS_AMMO=4; pistol=ALWAYS);
low-health text threshold 20.0 blending (255,0,0) -> (255,255,255);
fonts: HUD ALDO 40, ammo ALDO 26 + reserve ALDO 18, chat STANDARD 12,
kill feed STANDARD 14, big text EDO 30 (231,74,25); chat/kill-feed layout
MSG_LEFT_MARGIN=12, MSG_TOP_MARGIN=12, MSG_BOTTOM_MARGIN=60, MSG_PAD=5,
MAX_CHAT_ENTRIES=5, MAX_FEED_ENTRIES=5, MESSAGE_TTL=5 s. Retail HUD math is
window-relative, not fixed 800x600 (our design canvas maps it). The retail
equipped-tool tip is a NO-OP stub in the shipped build — do not implement.
Still open: ammo HUD (needs weapons), kill feed/chat (needs gameplay),
minimap (disabled in tutorial anyway), DisplayCountdown for COMPLETE
(unreachable offline), accuracy-driven crosshair corner spread formula
(partially recovered; finish with the weapon milestone).

## Exact state on 2026-07-19 (evening)

The Tutorial is now a real playable 3D vertical slice. Clicking the Tutorial
square runs a bounded background load of `Training.vxl` (parse + multithreaded
chunk meshing + budgeted GPU uploads feeding the retail loading screen), then
enters a first-person world with mouse capture, WASD, sprint/sneak/crouch,
jumping, retail collision and fixed 60 Hz simulation. Escape returns to the
menu and releases every world/GPU resource. The strict native build passes
30/30 tests.

A comprehensive evidence pass (retail source + BattleSpades server + OpenSpades
architecture, synthesized with per-constant provenance) fixed every milestone
constant. Key recovered facts now embodied in code:

- Physics is a line-faithful port of the oracle-calibrated
  `BattleSpades/aoslib/world.pyx` (itself replay-verified against retail
  `world.pyd`): jump impulse -0.36*1.2 assigned before the same-frame gravity
  step, friction divisors 1+4dt ground / 1+2dt air / 1+8dt wading, accel
  multiplier selection without stacking (0.7 walk / 1.4 sprint / 0.5
  crouch-sneak, x0.5 airborne, x sqrt(0.5) diagonal), single-pass boxclipmove
  with per-axis glide passes in float32 (double changes collision branches at
  exact contact planes), clip semantics (x/y outside map solid, z row 239
  samples the 238 bed, z in (-1,0) empty), one-block climb gating, ledge-lip
  drift, severe-landing 0.5 slowdown, crouch anchor shift 0.9 with stand-up
  headroom check, grounded epsilon 0.00875, wade = landing with pz > 237.
- Coordinates: 512x512x240, +z down, z=239 forced bed, map is bounded (NOT
  toroidal). Position is the eye anchor; feet = anchor + 2.25 standing / 1.35
  crouched.
- Tutorial spawn: lane origin (0,0) + SPAWN_LOCAL = eye (140.5, 76.5, 230.75),
  feet exactly on the z=233 corridor floor, facing -x (yaw 0).
- Camera: vertical FOV 75, near 0.1, far = Draw Distance (90/128/192, default
  192), radial linear fog to the draw distance, Training fog color
  RGB(111,215,223), no view bob, eye = anchor exactly.
- Look model: degrees end-to-end; sensitivity 0.1 degrees per raw count (the
  settings slider value), pitch clamp +-89.9, invert-mouse supported, jump
  requests edge-triggered on key-down (holding SPACE never re-fires).

### New modules in this slice

- `src/world/chunk_mesher.cpp` + `include/battlespades/world/chunk_mesh.hpp`:
  deterministic exposed-face mesher over 16x16x240 column chunks (32x32 grid),
  global-map neighbor queries (no border cracks/duplicates), per-corner
  occlusion with the anti-anisotropy diagonal flip, baked face shade
  ([NONRETAIL] placeholders pending retail light extraction), map-edge columns
  treated solid (bounded world, no edge walls), plus `ChunkTracker` dirty
  bookkeeping ready for block edits.
- `src/world/player_movement.cpp`: the physics port described above.
  `VxlMap` gained `set_voxel`/`clear_voxel`/`revision()` mutators.
- `src/world/tutorial_session.cpp`: renderer-free session (spawn, held-input
  state, degree look model, fixed-step tick, diagnostics snapshot).
- `src/world/tutorial_bootstrap.cpp`: coordinator + worker-pool loader with a
  64-mesh bounded queue; the main thread polls progress and drains meshes.
- `src/render/world_renderer.cpp` +
  `include/battlespades/render/render_views.hpp`: world pass on bgfx view 1
  (UI views renumbered: clear 0, world 1, window 2, canvas 3), per-chunk
  static buffers, frustum + radial fog-distance culling, sky clear = fog
  color, `BGFX_STATE_CULL_CW`, Sequential view mode. Water plane deferred
  (Training is dry; open-water bed voxels carry the recovered water tone).
- `include/battlespades/render/camera_basis.hpp`: THE camera contract. One
  header-only basis shared by renderer and tests; forward =
  (-cos yaw cos pitch, -sin yaw cos pitch, sin pitch), camera-right ==
  the retail strafe vector s = (-oy, ox). The view matrix is built from this
  basis explicitly — never bx::mtxLookAt, whose up-vector convention mirrored
  the world in the first playtest.
- Shaders `vs_world.sc`/`fs_world.sc` (radial fog) compiled for
  dx11/glsl/essl/spirv/metal by `scripts/compile-shaders.ps1`; its pinned
  flags reproduce the pre-existing UI shader binaries byte-for-byte
  (dx11 `s_5_0 -O 3`, glsl `130`, essl `300_es`, spirv, metal/osx). shaderc
  lives at `out/shaderc-build/cmake/bgfx/shaderc.exe`.
- Frontend integration (`native_frontend_module.cpp`): loading screen fed by
  real parse/mesh/upload progress (24 chunk uploads per tick budget),
  mouse-capture reconciliation once per tick (capture only in-world +
  focused; focus loss clears held input), movement keys resolved through the
  persisted Controls bindings, F3 toggles the diagnostics overlay
  (position/velocity/speed/yaw/pitch/state/chunk stats), placeholder "+"
  crosshair, Escape (or any route exit) tears down bootstrap, session and
  chunk buffers. `WindowPort`/`SdlWindowModule` gained
  `set_relative_mouse_mode`. The dynamic-text glyph cache now evicts entries
  unused by the current frame past a 384-entry soft limit — the hard 512 cap
  previously killed the session after ~2 s of live diagnostics text.
  `main.cpp` prints `frontend stopped: <error>` when a module stops the loop
  gracefully; without it renderer-invariant failures exited 0 silently.

### Verified by tests (30/30 passing, warnings-as-errors)

`aos_chunk_mesher_tests` (face counts, sealed volume, border culling, AO
darkening, determinism, tracker), `aos_player_movement_tests` (settle epsilon,
open-water wade, exact jump-frame velocity incl. float32 rounding, wall stop
at the 0.7/4 equilibrium speed, one-block climb, crouch headroom),
`aos_tutorial_session_tests` (spawn parity, walk + retail friction glide,
look clamps, inversion, real Training.vxl bootstrap delivering all 1024 chunks
exactly once, cancel-without-hang), `aos_camera_basis_tests` (camera right ==
strafe for all yaws, orthonormality, mouse-right turns right, pitch signs),
plus the entire pre-existing frontend suite.

## Current build and test instructions

```powershell
.\scripts\build.ps1 -Profile Dev -Native
.\out\build\native-dev\src\RelWithDebInfo\BattleSpadesClient.exe
```

Headless loop (unit tests only, no window; used while the user needs the
desktop): build targets with CMake in the activated VS environment, then run
`ctest -C Debug` inside `out\build\native-dev`.

The graphical Tutorial smoke is `tools\smoke-client-tutorial.ps1
-Executable <exe> -EvidenceDirectory <dir>`: it clicks the Tutorial square,
waits through the load, walks with W, checks the world capture is neither the
menu nor black, presses Escape and verifies the menu returns. It needs an
unlocked, unobscured desktop and it takes over the mouse — DO NOT run it while
the user is actively using the PC; ask first.

Tutorial data resolution order is unchanged (config path, packaged
`maps/Training.vxl`, then the source-tree sibling
`../BattleSpades/maps/Training.vxl`).

The repository tree is still untracked; do not assume `git diff` shows all
work, and stage/commit only when the user explicitly requests it.

## Immediate next steps (in order)

1. **Re-validate controls in-game** (user-run, or the smoke on a free
   desktop). The first playtest reported look/strafe "way off"; root cause was
   the bx::mtxLookAt mirror plus the yaw sign (spec decision D1 predicted
   exactly this failure and sanctioned the sign flip). Both are fixed and
   pinned by `aos_camera_basis_tests`, but retail's exact matrix construction
   was never decompiled, so in-game confirmation (mouse-right turns right, A
   strafes left, W walks toward the crosshair, pulling back looks down) is the
   acceptance gate. If the feel is still wrong the knobs are:
   `camera_basis.hpp` (one sign) and the sensitivity scale in
   `tutorial_session.cpp` (retail evidence: 0.1 degrees per count).
2. **Port the oracle replay harness**: `BattleSpades/scripts/replay_movebox.py`
   over `BattleSpades/logs/oracle/movebox_probes.json` as a C++ test —
   byte-for-byte float32 parity is the spec's acceptance gate for the mover.
3. Walk the recovered lane gates as integration tests: the first Training
   obstacle must stop forward walking at lane-local x ~ 134.45.
4. Retail HUD (crosshair images `png/ui/target_centre.png` + four corners,
   real loading-stage machine), then block build/destroy through the `VxlMap`
   mutators + `ChunkTracker` + a bounded re-mesh queue.
5. Protocol 168 session/map pipeline (server repo `docs/PROTOCOL.md`;
   MapSyncChunk is one zlib level-6 stream over 4-row column groups; never
   send raw VXL bytes or expanded underground voxels).

## Known gaps and [NONRETAIL] placeholders

- Face shading table, AO brightness curve, fog start 0.0, sky = fog color and
  the "+" text crosshair are placeholders tagged in code pending retail
  light/atlas extraction (spec decisions D2/D6/D11).
- `VxlMap` still stores dense colors (~252 MiB). The evidence pass recommends
  a per-column solidity bitfield plus a sparse color map; defer the refactor
  until block mutation lands so it is designed once.
- Render rate == tick rate (60 Hz); no interpolation (spec D10 allows this —
  retail renders at its 60 Hz update).
- Sprint auto-cancel nuances (D5) and wire-rounded class multipliers
  (D12: 1.4 -> 1.40625 over the network) are future-milestone items; D12 MUST
  be revisited when networking lands or prediction will drift.
- Water surface rendering deferred (D13); when added it is a separate plane at
  z=239, never meshed as blocks.

## Input and state ownership rules

- SDL/window code owns raw keyboard, mouse, focus and relative-mode events.
- The frontend maps raw input to semantic actions via the persisted Controls
  bindings; the Tutorial session owns held action state and consumes it on
  fixed ticks.
- The camera consumes look deltas only while captured; UI consumes pointer
  events only when capture is released.
- Losing focus must clear held keys and buttons to avoid stuck movement/fire.
- The renderer consumes immutable snapshots and never mutates gameplay.
- Network packets propose/update replicated state through typed session logic;
  they do not call widgets or renderer internals. The loader/session foundation
  now exists, while continuous gameplay packet families remain incomplete.

## Definition of done for this milestone

1. Training is chunk-meshed and rendered as a real 3D voxel world. DONE
2. Mouse capture/look, WASD, gravity, collision and jump at fixed 60 Hz. DONE
   in code and unit tests; in-game control-feel validation PENDING (step 1).
3. Escape returns safely to the menu without leaking GPU/world resources. DONE
4. Loading shows real parse/mesh/upload progress and never blocks the window.
   DONE (bounded bootstrap + budgeted uploads).
5. Parser, mesher, movement and graphical Tutorial smoke tests pass. Unit
   suites DONE (30/30); the graphical smoke script exists but its clean-desktop
   run is PENDING (user was using the PC).
6. The full existing frontend suite remains green with warnings-as-errors. DONE

After in-game validation closes items 2 and 5, attach the implemented Protocol
168 session/map pipeline to the Join Match scene before attaching weapon
actions to live ENet. Stable world synchronization remains the foundation for
every replicated feature.

## 2026-07-21 weapon-port foundation

The first all-tool/server-sync tranche is implemented and verified:

- `tools/generate_weapon_catalog.py` produces a dense 65-row native table from
  the sibling server's `WEAPON_CATALOG`, retail filenames/image rules, all 65
  classes registered in recovered `aoslib/weapons/list.py`, and the checked-in
  original assets. It statically resolves Python class inheritance and `A####`
  aliases rather than importing the Python 2 runtime. The table includes every
  server damage/ammo/cadence/range/explosion value plus exact retail region
  damage, ammo tuple, recoil, accuracy/spread, zoom, timing, crosshair and tool
  flags. It also owns 1,535 per-tool specialized constants for projectiles,
  explosions, digging, fire, deployables and other class-specific behavior.
- Asset audit: 65 server tools, 60 tools marked by retail as first-person
  images, 31 dedicated legacy toolbar icons, 62 authored first-person image
  sets in the files, and 52 matching KV6 tool models. Missing dedicated icons
  use the authored weapon image; tools for which retail sets `TOOL_HAS_IMAGE`
  false remain explicitly marked.
- The native preload manifest now includes all catalogued toolbar and
  first-person/team images, and HUD slot lookup no longer hardcodes only block,
  spade and pistol.
- `protocol168_weapons` has strict codecs and server-Cython golden vectors for
  ClientData(4), Shoot(6), ShootFeedback(8), ShootResponse(9),
  UseOrientedItem(10), SetClassLoadout(13), Restock(69), WeaponReload(76), plus
  WorldUpdate(2)'s complete player prefix through the tool/deployment fields.
- `WeaponReplicationState` centralizes loadout selection, ammo, observer shot,
  reload and restock state. It rejects sentinel 65, tools outside the active
  loadout, and ShootFeedback whose tool does not match the replicated selected
  tool.
- Native warnings-as-errors build passes; all 39 tests pass; a paced 120-tick
  native executable smoke exits 0. Catalog staleness check passes with
  `py -3.12 tools/generate_weapon_catalog.py --check`. The contract test only
  activates when both sibling `BattleSpades` and `aceofspades_source` inputs
  are present and passes both roots explicitly.

Next weapon work must attach CreatePlayer/SetClassLoadout and WorldUpdate rows
to per-player `WeaponReplicationState` inside the future live session, then
port behavior families in this order: firearm base + observer feedback,
shotgun pellet/recoil, scoped weapons/laser, melee/digging, cooked throwables,
launchers/projectiles, deployables/UGC tools. Do not add another hardcoded tool
switch for stats or asset paths; use `weapon_catalog()`.

## 2026-07-22 live Server Browser and direct-connect update

- The native Server Browser now uses `network/server_discovery.*`, libcurl and
  nlohmann-json for the bounded AoSPlay HTTPS list. It uses the server's
  `HELLOLAN` UDP query for Local and coalesces loopback/broadcast aliases of the
  same local host. Public payloads are capped at 1 MiB/512 rows; LAN payloads
  are capped at 8 KiB and all callbacks carry the browser generation so a late
  response cannot repopulate a changed source.
- The recovered Join Match middle route is **Connect to IP**, not Custom
  Match. `DirectConnectMenuModel` accepts `local`, IPv4/DNS with an optional
  explicit port, and `aos://` identifiers through one strict endpoint parser.
  Browser selection and direct entry both feed the same typed
  `ServerConnectRequest` and the normal match-loading worker.
- Real AoSPlay evidence returned five rows spanning TDM, CTF, Classic CTF and
  Zombie. The browser maps their mode TLAs to retail title/description keys and
  normalizes compact API names such as `BlockNess`, `DoubleDragon` and
  `SpookyMansion` to checked-in authored preview assets.
- Local evidence used a private BattleSpades process on UDP 32991 with Steam,
  Revival registry and bots disabled. `aos_server_discovery_live_smoke`
  returned exactly one `MayanJungle` row and
  `aos_protocol168_live_smoke` reached ready after 1,553 datagrams, four sends,
  a 1,582,522-byte compressed map and 10,812,498 solid voxels.
- Strict MSVC `/W4 /WX` native build passes and all 52 CTest targets pass. The
  discovery parser, direct endpoint input, generation cancellation, filters,
  map preview resolver and live mode resolver have explicit tests.
- Important boundary: the current blocking ENet adapter proves handshake and
  full-map loading, then disconnects. The loading screen is real; persistent
  multiplayer ownership, Start-to-gameplay handoff and continuous packet
  replication remain the next network milestone. Do not describe this as a
  complete multiplayer client until that long-lived transport exists.

## 2026-07-22 persistent match connection and gameplay handoff

- `LiveProtocol168Connection` now owns ENet for the whole match on one worker
  thread. The main/render thread sees only bounded complete-packet queues and a
  one-shot `Protocol168WorldBootstrap`; no raw ENet object crosses the seam.
  Inbound capacity is 16,384 because the server can legally send its 8,192
  world-mutation reconnect catch-up immediately after `CreatePlayer`. Overflow
  fails explicitly and never drops gameplay packets silently.
- `Protocol168Session::take_map()` moves the validated VXL into gameplay.
  `TutorialWorldBootstrap` accepts that existing map and reuses the same
  bounded 1,024-chunk meshing/GPU-upload path as Training without parsing or
  copying it again. The loading screen retains the retail START gate.
- The existing world/camera shell now has a `network_authoritative` mode. It
  spawns from the local packet-28 transform/class/loadout, predicts native
  movement, sends measured ClientData movement/action bytes every fixed tick,
  and never applies local weapon or terrain mutations. WorldUpdate(2) decoding
  now exposes complete position/orientation/velocity/ping/ack/health/fuel rows.
  Normal local drift stays predicted; only divergence over four blocks snaps,
  avoiding the old correction-every-update rollback jitter.
- CreatePlayer and PlayerLeft maintain the rendered roster. Packet ids
  11/33/37/40 are applied through `Protocol168TerrainReplica`. Terrain packets
  arriving while the network VXL is being meshed are deferred, applied after
  workers stop touching the map, and their dirty chunks are remeshed before
  START. This preserves the high-volume Damage(37) rejoin catch-up safely.
- Hosted evidence against `88.80.155.252:38888`: Frontier joined as local id 1
  with 13 roster entries, remained connected for five seconds, sent 302
  datagrams, received 4,373, and decoded 145 WorldUpdates. The runtime burst
  included 2,834 Damage packets and was drained without loss/overflow.
- The legacy `run_enet_protocol168_session` remains intentionally one-shot for
  loader regression tests. The menu uses `LiveProtocol168Connection`. Do not
  move ENet calls onto the render thread or apply terrain to the map while its
  bootstrap workers are still meshing it.
- Current boundary: movement/look/tool selection and remote transforms are
  live. Shooting proposals, damage/death/respawn, objectives, entities, audio,
  chat and full packet-family dispatch still require authoritative adapters;
  do not simulate these locally in network mode.

## 2026-07-21 class, skin and gameplay-lab port

- `tools/generate_class_catalog.py` generates all 18 playable class rows from
  the sibling server's recovered `shared.constants`: seven body/collision
  models with authored offsets/anchors, per-class FPS arms, all seven item
  groups, starting/max blocks, class-select portraits/icons, and texture-skin
  affinity. A CTest contract check rejects stale generated data.
- `world::load_class_models` validates every declared KV6 and replaces only
  the retail `(64,0,64)` team marker. It retains individual body/crouch parts
  for the future character animator and builds a standing debug composite.
- `world::PlayerInventory` is the shared boundary for toolbar selection,
  `WeaponRuntime` ammunition, and the class block wallet. Ammo and block
  restocks are intentionally separate. Class item IDs are 16-bit because
  jetpacks/parachute equipment occupies values above the 0..64 weapon range;
  never truncate those values into a Protocol 168 tool byte.
- Press F10 from any native frontend screen for `GameplayDebugLab`. Tab selects
  character, first-person, or gallery inspection. Up/Down changes class;
  Left/Right or wheel changes among every tool; Q/E rotates and -/= zooms;
  C plus [/] edits the exact server-defined RGB. LMB/Enter and
  RMB exercise primary/secondary state machines; R reloads; A refills ammo;
  B/N spend/refill blocks; T selects default blue/green and recolors bodies and
  FPS arms using retail's 0.7/1.0/1.3 KV6 material bands; K toggles the
  default/Mafia texture skin; M spawns the selected mannequin; Shift+M shows
  all 18; Delete clears. F10/Escape closes it. `models.py` authored offsets are
  preserved independently for every first- and third-person model part; the
  overlay prints the selected tool's first TP offset for parity inspection.
- `WorldRenderer` now owns bounded world-model slots separate from immutable
  terrain chunks and first-person slots. The F10 lab uploads all 18 class
  composites once per selected team and dynamically replaces tool/arm slots
  when class or weapon changes.
- Verification: 47/47 CTest tests pass under `native-dev`. A real 480-tick
  rendered smoke posted F10, Right, Down and Enter through the Windows window
  and exited 0 after class models and the selected tool were uploaded.

## 2026-07-22 reload, explosives, CreatePlayer and live loader update

- Reload now follows retail's shared `Character.pullout` state rather than an
  isolated ammunition timer. Magazine reloads transfer at completion;
  `clip_reload` weapons transfer one shell and start another full cycle until
  full or reserve-empty. Each cycle emits packet-compatible reload edges, and
  only the final edge plays `reload_done_sound` (`cock` where authored).
- Cooked throwables emit a priming edge on press. Molotov uses its dedicated
  throw/impact sounds, three-second charge animation contract and
  `35 + charge*40` launch speed. The primary radius/block damage bug is fixed:
  generated `blast_radius`/`block_damage` values are used instead of ambiguous
  constant-name suffix searches.
- Explosive terrain effects now create the recovered ten debris plus four glow
  particles. Molotov creates five persistent four-second flames and Chemical
  creates a bounded cloud; effects are depth-tested world cubes and never
  mutate authoritative terrain.
- Tutorial mannequins are encoded and decoded as real CreatePlayer(28)
  fixtures before entering the generation-safe roster. They deliberately cover
  Blue Soldier, Green Rocketeer and Blue Miner. Retail wire teams are
  0 spectator, 1 neutral, 2 Blue, 3 Green; loadouts may include equipment ids
  above weapon 64 and prefab slots may be empty.
- `Protocol168Session` plus `EnetProtocol168Client` performs a real protocol-168
  ENet connection with range coding, sends the empty/offline packet-105 ticket,
  requests CRC-zero full sync, bounds/decodes LZF wrappers, inflates zlib
  MapSync, validates the complete VXL, consumes StateData/CreatePlayer, then
  sends NewPlayerConnection and initial ClientData. Unknown packets quarantine;
  malformed critical sequencing fails closed.
- Live evidence against BattleSpades on UDP 32887: Mayan Jungle reached ready,
  local player 12, 10,812,149 solid voxels, 1,582,935 compressed bytes, 13 roster
  rows, 1,577 received datagrams and four sent packets; clean disconnect.
- Verification is 51/51 native CTest tests plus a passing real-window Tutorial
  grenade smoke. The gameplay menu does **not** yet own this live adapter; use
  `aos_protocol168_live_smoke.exe HOST PORT NAME` until Join Match integration.

## 2026-07-22 class selection, live weapons and prefab-ID correction

- Network loading START now opens a real SelectClass gate populated from the
  team class lists in StateData(45). The player chooses one item from each of
  the four retail loadout rows and up to three class prefabs. SELECT sends one
  atomic SetClassLoadout(13), retains jetpack/equipment IDs above 64, updates
  the local roster, and builds the live inventory from that exact selection.
- Network-authoritative `TutorialWorldSession::tick()` now advances the shared
  WeaponRuntime. `native_frontend_module` translates its semantic actions with
  `protocol168_weapon_action_adapter` and sends Shoot(6), oriented items,
  reloads, deployables, paint, objectives, and BuildPrefabAction(30) while
  keeping local terrain mutation disabled.
- A recovered constant mix-up was the prefab failure: retail 22 is
  FLAREBLOCK_TOOL and retail 23 is PREFAB_TOOL. `class_selection.cpp`,
  `player_inventory.cpp`, the generated class-catalog helper, and its generator
  now agree on those IDs. The generic nameless prefab slot is omitted whenever
  concrete selected prefabs exist; the combined inventory contains the actual
  named variants and each selects tool 23.
- Strict native build passes 53/53 CTest targets. The two-client hosted probe
  against `88.80.155.252:38888` observed the shooter through packet 8 and then
  placed `prefab_caltrop`; the observer received exactly 11 authoritative
  BlockBuildColored(33) cells. Run the same proof with:
  `aos_protocol168_live_session_smoke.exe HOST PORT 12 --prefab`.
- Current boundary: outbound player weapons and prefab placement are live.
  Continue with inbound remote projectiles/entities, damage/death/respawn,
  objective packets, and their sounds/particles; do not regress the fixed
  22=flare, 23=prefab invariant.

## 2026-07-22 live player, pause-menu and movement reconciliation

- InitialInfo and StateData are retained as authoritative runtime state. Team
  names/RGB, team/class locks, spectator permission, mode id, per-team class
  lists, and class movement multipliers must never be replaced with frontend
  defaults after the handshake.
- WorldUpdate(2) now retains every remote presentation field used here:
  position, orientation, velocity, health, acknowledged client loop, input,
  action/state flags, selected tool, and resources. Remote rigs recolor the
  reserved 64/128/192 magenta material bands from StateData team RGB.
- Each remote player has a bounded nine-slot rig: standing torso/head, crouch
  composite, independent left/right legs, three weapon parts, and upper/lower
  arms. `Character.update_animation` in the symbol-rich macOS binary at
  `0x83a70` independently rotates both legs; do not restore the old whole-body
  bob. Held tools are gated by WorldUpdate `can_display_weapon` bit `0x10`.
- Local WorldUpdate position is the state for
  `acknowledged_client_loop`, not the current render frame. Reconciliation
  looks up that exact ClientData sample, rebases later prediction samples, and
  applies the delta to physics. The camera retains the inverse of an ordinary
  small correction and exponentially settles it, preventing visible packet
  step teleports without sending an interpolated state back to the server.
- Respawn/class changes reapply the exact class entry in InitialInfo's movement
  multiplier table. Reusing the previous class's scale reintroduces gradual
  walking drift.
- Escape sends ClientInMenu(110). Its Change Class/Change Team visibility and
  enabled state are rebuilt from the current StateData and roster. Change Team
  is a real server-aware screen and sends ChangeTeam(77); class selection then
  stages SetClassLoadout(13) followed by ChangeClass(78).
- `--connect HOST:PORT` is a deterministic developer/live-smoke shortcut. It
  enters the same `begin_match_loading` service as Direct Connect; it is not a
  second network implementation.
- Verification: strict native build passes 54/54 tests. A 15-second live probe
  against `88.80.155.252:38888` loaded The Colosseum with 13 players, consumed
  450 WorldUpdates/1,446 runtime packets, sent 900 ClientData/action packets,
  and exited cleanly.
- The graphical live harness is `tools/smoke-client-live-match.ps1`. It polls
  the actual loading START material until enabled, then crosses START and
  SelectClass separately; fixed sleeps/hash-only checks previously produced a
  false positive by returning to the frontend. The verified evidence set is
  `out/evidence/live-parity-smoke-current2`: Tokyo Neon class selection,
  world, a two-second W movement, server-aware pause, and Change Team all
  rendered in one connection.
- StateData stock names are localization IDs. `TEAM1_COLOR`/`TEAM2_COLOR` must
  resolve to Blue/Green before Change Team renders; custom server names remain
  untouched. First-person arms and team-tinted tools now use the local
  player's current StateData RGB and are invalidated at CreatePlayer/class/team
  transitions. The previous hardcoded-blue local path disagreed with correctly
  tinted remote rigs.
- The StateData decoder now consumes the bounded entity/camera tail and retains
  `has_map_ended`; EscapeMenu keeps Change Class/Team visible but disabled
  while the server owns the score screen. Only `mode_type == MODE_UGC (12)`
  selects UGC gating—InitialInfo's `ugc_mode` byte is not a boolean match-mode
  discriminator and must not hide normal TDM controls.
## 2026-07-22 parity repair handoff

- Do not divide `InitialInfo.movement_speed_multipliers[class_id]` by a class
  baseline. It is the direct authority scale. The former double-normalization
  was the dominant ordinary-walking rollback bug.
- Handshake ClientData owns loop zero; continuous gameplay begins at the
  session's exported next loop. ClientData is reliable in retail, proven at
  `GameScene.send_client_data` (`gameScene.pyd:0x1016AAE0`).
- Match the default server phase: previous accepted locomotion/jump/sneak/
  sprint, current crouch/orientation. Owner reconciliation is against the
  acknowledged loop; remote WorldUpdates are interpolated only for display.
- Server class/loadout packets atomically rebuild runtime inventory, including
  UGC and all named prefabs. A pending local tool edge owns display/action
  state until the server acknowledges that loop; afterward the WU tool byte
  wins.
- Damage(37) is type-expanded and produces one visible impact event for each
  live packet, including sublethal hits. Discard impact events created while
  applying historical join catch-up.
- Current gates: client 54/54, server 1,143/1,143. Raw ENet validation against
  `88.80.155.252:38888` loaded WW1 and consumed continuous WorldUpdate/Damage
  traffic. The desktop smoke's world capture is presently black because of
  DWM capture behavior; do not report it as a visual pass until the harness is
  repaired or a visible interactive run is recorded.

## 2026-07-23 turn and block-action movement repair

- `protocol/runtime_packets.py::_fromfixed_orientation` was the remaining
  turn-dependent rollback root cause. Orientation is signed two's-complement
  3.13; raw `00 E0` is `-1.0`, not `-2.0`. Keep the raw-byte regression in
  `tests/test_reversed_world_update.py` whenever packet decoding is changed.
- Preserve the retail input phase: previous packet for locomotion, jump,
  sneak, and sprint; current packet for crouch and orientation. Do not make
  jump current to make a synthetic test feel more immediate.
- `TutorialWorldSession` quantizes network-authoritative orientation to the
  signed 1/8192 wire grid. Camera yaw/pitch stay full precision. Removing this
  can reintroduce opposite voxel-corner decisions after turns.
- `tools/protocol168_movement_parity.cpp` now changes color, places a real
  packet-40 BlockLine, applies the terrain echo, and turns through a negative
  diagonal before its movement/jump/crouch checks. Run Engineer explicitly via
  `tools/run-server-client-parity.ps1 -Class 4` to cover jetpack physics.
- Verified evidence: `out/evidence/movement-turn-block-jump-final.csv`,
  `out/evidence/movement-engineer-quantized-turn-final.csv`, and
  `out/evidence/two-client-turn-block-final`. These clean runs had zero input
  mismatches, malformed packets, orphan rows, and position/velocity correction.
- Current focused gates: native client 55/55; server movement/decoder 152/152.

## 2026-07-30 authoritative death, grave and spectator lifecycle

- This subsystem is client-only. The BattleSpades server was treated as the
  immutable protocol authority and was not modified.
- `DeathCameraController` owns the presentation state for a dead or spectator
  local player. A server `KillAction(46)` starts the death life boundary,
  clears held input/fire state, sets health to zero, starts the server-provided
  respawn countdown and suppresses outgoing `ClientData` until a later
  `CreatePlayer(28)` restores the life.
- Retail recovery established camera id 5 as `DEATH_CAMERA` and camera id 1 as
  `CHASE_CAMERA`. Chase becomes available after 1.5 seconds, eight accumulated
  mouse counts or a click selects it, and a legal target is selected
  automatically after five seconds. The local death camera orbits the
  authoritative grave; spectators chase living replicated players and cycle
  targets with mouse buttons or the wheel. These choices never mutate or send
  player state to the server.
- The `InitialInfo` death-camera flag is retained and respected. If deathcam is
  disabled, the client enters a legal chase camera immediately. A spectator
  team spawn uses chase when a living replicated player exists and otherwise
  uses the free-camera fallback.
- Grave entity type 11 is bound only when its owner is the local player. Retail
  loads `grave.kv6` with pivot offset `(0,0,11)`; applying that offset in the
  KV6 pivot, instead of translating the entity, fixes the buried tombstone
  while keeping the server's dry-surface position authoritative.
- The retail grave has an exact-black material panel rather than the usual
  magenta team-color bands. `apply_entity_team_material` recolors only that
  exact-black panel from the entity packet's RGB/team state and preserves the
  near-black stone shading. Packet team ids 2/3 fall back to the current
  `StateData` team RGB only if an explicit entity color is absent.
- Retail does not draw a death-camera or chase-title overlay. The native camera
  controller owns the grave/chase transition, while HUD retains only the
  localized respawn label. Do not reintroduce the former fabricated
  `DEATH CAMERA`, `SPECTATING`, or input-hint strings.
- Automated verification is 75/75 CTest targets. The authoritative graphical
  proof is `out/evidence/death-state-authoritative-smoke-final`: it records the
  real class-change death, blue team grave, chase-delay state and server
  respawn. `out/evidence/death-state-native-chase-physical` adds a second
  normally spawned C++ client and proves the internal transition to that
  replicated chase target. Its old `SPECTATING: PLAYER` overlay is superseded
  by the 2026-08-09 source correction below. The death click must use physical
  input because SDL
  relative mode does not consume a posted window-message click; the smoke
  harness now has a separate `PhysicalClick` path for that captured state.
  Re-run the single-client lifecycle with:
  `tools/smoke-client-live-match.ps1 -Executable
  out/build/native-dev/src/RelWithDebInfo/BattleSpadesClient.exe -Endpoint
  127.0.0.1:32782 -EvidenceDirectory
  out/evidence/death-state-authoritative-smoke-final -JoinTimeoutSeconds 40
  -WorldHoldSeconds 4 -ClassIndex 0 -ExerciseDeathState`.

## 2026-07-30 client colors, AoSPlay scores and owned local hosting

- This work is client-only. `G:\AoSRevival\BattleSpades` was used only as a
  read-only portable runtime/input for live validation.
- KV6 team materials are now decoded globally instead of through
  class-specific Specialist/Medic patches. Exact black receives the base team
  RGB; the 64/128/192 magenta bands receive the recovered shaded/base/highlight
  variants. The rule applies to standing/crouching models, arms, and entities,
  so newly added classes cannot silently fall back to black.
- `network/aosplay_scores` implements the bounded recovered AoSPlay form/JSON
  contract. Leaderboard and Player Profile requests run off the render thread,
  retain generation IDs, coalesce repeated UI changes, reject stale results,
  and apply on the main thread. Leaderboard rows select in place as retail;
  Player Profile remains the separate profile-menu route. Display-name identity resolution is exact and
  case-insensitive, and ambiguous matches fail closed.
- Create Match now carries bot count, bot difficulty and preferred server port
  in the same typed configuration as mode/map/rules. `LocalServerProcess`
  allocates the first free UDP port at or above that preference, writes a
  disposable private TOML, launches only a complete portable server bundle
  hidden, and owns its graceful/forced shutdown. No persistent server config is
  overwritten.
- The loading screen does not claim success on process creation. A worker waits
  for the real HELLOLAN response, rejects the wrong mode, and hands the
  discovered endpoint to the existing Protocol 168 loader. Route cancellation,
  stale generations and client exit stop only the process owned by this client.
- Release builds can set `AOS_BUNDLED_SERVER_ROOT` to a read-only portable
  server directory; install copies it to `bin/server`. Windows launches
  `BattleSpades.exe`; Unix platforms launch `BattleSpades`.
- Verification: strict native build and all 77 CTest targets pass. The live
  AoSPlay probe returned 19 leaderboard rows and resolved `KikoTs` to
  `1000000004`. The hidden-host smoke received HELLOLAN on `127.0.0.1:27015`
  and completed a real Protocol 168 bootstrap of Ancient Egypt with 2,265,338
  solid voxels before clean shutdown.
# 2026-08-05 remote multiplayer character audio

- Implemented the missing observer path without modifying BattleSpades server.
- `remote_character_audio.{hpp,cpp}` derives remote foley/VO from packet 28 and
  WorldUpdate state: delayed forced spawn, footsteps/wade, jump/water jump,
  damaging land/fall hurt, jetpack land, zombie groans and death.
- Frontend states are per player generation and cleared on PlayerLeft and full
  session teardown. Playback is positional and uses existing distance/VXL
  occlusion; local playback stays head-relative.
- Fixed class voice selection to retail's inclusive `randint(0,100)` chance and
  negative-chance suppression-token behavior. Added zombie movement banks and
  the UGC Builder's deliberately blank jetpack-land slot.
- Important invariant: retail has no generic bullet pain VO. Fall-hurt is the
  only class `hurt` bank; combat impacts and explicit network sounds remain on
  replicated weapon events and PlaySound(23).
- Build command still requires `VsDevCmd.bat`; strict `/W4 /WX` build passes.
  New test target: `aos_remote_character_audio_tests`.

## 2026-08-06 gait, projectile, gravity, death and score parity

- Client-only change. `G:\AoSRevival\BattleSpades` remained read-only and was
  consulted only for packet construction and authoritative movement metadata.
- `Character.update_animation` in the native macOS retail binary applies the
  forward triangle wave to the legs' X rotation and the strafe wave to Z. The
  native rig had these axes swapped, which made forward-moving bots cross their
  feet. Only that mapping changed; cadence, amplitude and crouch scaling remain
  recovered retail values (`aos_retail_character_pose_tests`).
- Rocket smoke now originates at the transformed KV6 exhaust point (local +Z),
  not the raw packet pivot. Model and plume therefore remain attached through
  pitch/yaw/roll (`aos_local_entity_tests`, `aos_particle_system_tests`).
- Lunar gravity is decoded from `StateData(45).gravity`, signed 1/64 fixed
  point. LunarBase's canonical 26 decodes to 0.40625 and is passed through every
  gravity-dependent prediction/replay path; do not infer it from the map name.
- Jetpack deaths keep the red corpse voxel burst but layer a larger warm
  explosion with six explosive trails and dynamic light at the authoritative
  airborne corpse position. Death thruster flame/smoke was enlarged. Every
  ordinary death now spends the complete opening 1.5 seconds aimed at its
  tombstone, even when the server disables free deathcam. The killer selects
  the camera side and becomes the automatic chase target after that hold.
- RPG/RPG2 keep the recovered wire invariant: `rpgWeapon.py` and the native
  `GameScene.send_rocket*` pass `world_object.position` unchanged. Only the
  owning client's first-person presentation is bridged from the measured KV6
  barrel mouth to the authoritative projectile over 0.05 seconds. The muzzle
  centres are derived from the shipped model tips/pivots plus the recovered
  0.065 view scale and `Character.draw_fps` root; remote rockets and every
  non-RPG projectile retain their ordinary packet transforms. The missing
  `(0,.1,0)` RPG/RPG2/UGC-RPG2 FPS hold and cancelling arm offset are restored.
- `SetScore(85)` is an authoritative total. The HUD stores the first value as a
  baseline, computes later local deltas, aggregates rapid awards into one
  `+score` row and retains up to four localized reasons beside the crosshair.
  Profile-total reason ordinals intentionally do not leak internal labels.
- Verification: strict build clean; focused parity executables green; all
  91/91 CTest targets pass; build and refreshed `dist/bin` executables both
  complete a 120-tick headless smoke.

## 2026-08-09 retail death/chase HUD correction

- Removed the native-only `GameHudSpectatorState` and its fabricated
  `DEATH CAMERA`, `SPECTATING`, `MOVE MOUSE OR CLICK TO CHASE`, and
  `MOUSE WHEEL TO CHANGE PLAYER` draw commands. Camera movement, the 1.5-second
  grave hold, chase selection and respawn lifecycle remain in
  `DeathCameraController`; this change removes only non-retail HUD content.
- Retail evidence is explicit. `strings/english.py` owns `RESPAWNING_IN` and
  `NEVER_RESPAWN` but none of the removed phrases. In the native macOS HUD,
  `HUD.draw` references `respawn` at `0x5725D`; `spectator` is referenced only
  by `HUD.draw_tools_hud` at `0x77DAC` to suppress tools, and `chase_player` is
  referenced only by `Minimap.get_focus_player` at `0x166D7B/0x166DBF`.
- The HUD tests now fail if any of the fabricated camera labels return while
  continuing to require the source-owned `Respawning in N`/`No respawns!`
  presentation.

## 2026-08-09 retail Legacy VXL lighting recovery

- This is a client-only correction. `G:\AoSRevival\BattleSpades` remained
  read-only and supplied only the authoritative packet layout/default fixture.
- The acknowledged placeholder `{face_shade} * {occlusion_shade}` path is gone
  for terrain. Legacy now uses the shipped `ao_cube512.tga`, the exact red AO,
  green top-edge and blue noise channels, two directional lights, ambient fill,
  `max(0.3, dot)`, `pow(spec,10)*0.065`, and linear fog from retail
  `map_vert.py`/`map_frag.py`.
- IDA evidence: `vxl.pyd` `sub_10005440` writes AO.xy/edge.zw into the 40-byte
  vertex; `sub_10004510` selects the 15-cell pattern; `sub_10004800` and
  `sub_10004E50` construct face AO/edge codes; `sub_1003B100` initializes the
  atlas; `sub_10030B60` averages the VXL high-byte light factor into
  `gl_Vertex.w`. The native mesh is position4 + encoded normal/noise + RGB +
  texcoord4, stride 40.
- `StateData(45)`'s previously skipped 25-byte lighting prefix is now decoded
  exactly as BGR colors, Z/Y/X signed 1/64 directions, ambient intensity and
  time scale. `server/builders/state_data.py` defaults remain a read-only test
  oracle; the native renderer consumes whatever the connected server sends.
- Canonical terrain vertices carry a Color2-alpha material tag so detached KV6
  models keep their compatibility Legacy fallback. Atlas UV, noise corner,
  directional influence and the terrain tag are all pinned by the mesher test.
- Verification: strict `/W4 /WX` build; focused 3/3; full 98/98 CTest; built
  and staged executables both complete a 120-tick headless smoke.
