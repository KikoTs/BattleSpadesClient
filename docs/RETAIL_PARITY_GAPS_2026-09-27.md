# Retail parity gaps: native client vs retail AoS 1.x (2026-09-27)

This document merges five read-only audits of the BattleSpades native client (network, audio, HUD, gameplay, render) into one ranked list. Nothing was built or run for it.

The IDA MCP server was down during the audits, so any item marked **VERIFY** needs a headless-IDA check of the stock `.pyd` files before it is implemented.

## Abbreviations

| Short | Path or meaning |
|---|---|
| nfm | `src/frontend/native_frontend_module.cpp` |
| ts | `src/world/tutorial_session.cpp` |
| term | `src/network/protocol168_terrain.cpp` (hpp: `include/battlespades/network/protocol168_terrain.hpp`) |
| rt | `src/network/protocol168_runtime.cpp` |
| pl | `src/network/protocol168_players.cpp` |
| wpn | `src/network/protocol168_weapons.cpp` |
| hud | `src/frontend/game_hud.cpp` |
| mo | `src/frontend/match_overlays.cpp` |
| pe / te | `src/world/particle_effects.cpp` / `src/world/terrain_effects.cpp` |
| wr | `src/world/weapon_runtime.cpp` |
| ecat / wcat | `src/world/entity_catalog.generated.cpp` / `src/world/weapon_catalog.generated.cpp` |
| BS/ | `G:\AoSRevival\BattleSpades\` (server repo) |
| BDM | `BS/server/block_damage_model.py` |
| Audit tags | **N** network, **A** audio, **H** HUD, **G** gameplay, **R** render. For example "N P0-2" is item P0-2 of the network audit, and "G C6" is row C6 of the gameplay audit. |

---

## 1. Summary

### What is already at retail parity

| Area | Evidence |
|---|---|
| Movement physics | boxclipmove, glide and climb, acceleration, friction and gravity order, jump, crouch, sprint, fall damage, wade, player contact, jetpack thrust for all 4 packs, parachute physics. It is a line-for-line float32 port of `world.pyx`, and the class tables match all 18 classes (G A1-A12, A15, A18). |
| Packet parsing | Every decoded packet matches the `shared/packet.pyx` writers byte for byte: field order, widths, fixed16/float32 and BGR order (N header). |
| Sound assets and engine | All 941 sounds, 12 music tracks and 21 ambients are on disk. OpenAL with EFX reverb, the 50-block cull, a 6.5 s music crossfade, the ambience fade, class voice banks, footsteps, weapon fire/reload/tails, melee and explosions are in place. All 48 SOUND_MAP ids are wired through PlaySound(23) (A §P2). |
| Kill feed | Rows, colours, the 5-row stack, stroke font, and the icon table for types 0-26 and 31-36 (H present list). |
| Scoreboard | 16 rows per column, sorting, spectators, ping, VIP crown, domination markers, and the ShowTextMessage(73) headline. |
| End screen | ViewGameStats, all 30 GameStats(67) award labels, the RankUps timeline and MapEnded layering. |
| Ballots | GenericVotingHUD for map and kick votes: F1-F3, nested literal decoding, 5 s and 6 s timers. |
| Chat, big messages, LocalisedMessage | All present, including per-parameter localisation. |
| Minimap | Zones, billboards, VIP high visibility, carrier icons and StateData visibility. Radar is the exception (P1-03). |
| Skies, fog and lighting | Per-map skydome layers, the fog distances 90/128/192, StateData lights and ambient, retail AO and grain, KV6 normals. This holds in the Legacy tier. |
| Particles | Explosion LUT particles, blood, block debris, falling structures, rocket smoke, jetpack exhaust and view-model animations. |
| Weapons (mechanics) | Pellet seed and spread RNG, ADS FOV and sensitivity, minigun spin, burst and projectile tables. |

### Gap themes

1. **Terrain model divergence.** Damage(37) footprints, per-cell health, BlockManagerState(38), prefab(30) and the side effects of packets 33 and 7 differ from retail. The client shows holes and walls the server does not have. This is the largest source of desync and invisible collision.
2. **Stale weapon catalog.** The catalog still carries modded tail values (fire rates, swing rates, recoil), so the server rejects the extra shots and swings.
3. **Prediction rules the server enforces but the client does not predict.** These cover the jetpack exhaustion tail, parachute deploy rules, reload interrupts, melee RMB, the dig wallet and the remote-player view delay.
4. **Event hooks that never fire.** The client decodes the data, but nothing plays or shows it: kill banners and stingers, the match-start stinger, radar detection, burn and sudden-death feedback, POIFocus, spawn protection, and about 25 gameplay sounds.
5. **Entities with no presentation.** Flares (type 13), goo (31), block fire (28), crate parachutes, tracers and the KV6 muzzle flash.
6. **HUD polish.** Disconnect reasons, localisation, the votekick UI, death hints and invented hint text.

### Item counts

| Tier | Meaning | Count |
|---|---|---|
| P0 | Desync, fairness or invisible collision | 12 |
| P1 | Very visible missing feature | 19 |
| P2 | Polish | 20 |
| P3 | Minor or unverified | 24 |
| **Total** | | **75** |

---

## 2. Master list

Effort: **S** is under a day, **M** is 1-3 days, **L** is more than 3 days.

### P0: desync, fairness and invisible collision

| ID | Title | Retail behaviour (evidence) | Client status (file:line) | Fix sketch | Effort | Deps | Found by |
|---|---|---|---|---|---|---|---|
| P0-01 | **Damage(37) footprints, centre and amount byte**<br>**Status: DONE (wave 1).** `retail_damage_footprint` in term is a verbatim port of BDM (R table, x-major cubes, z-major spheres, `ceil4`, `RetailRandom(seed)`, `floor(p+0.5)` centre for every type, z>238 skipped); the amount byte is unsigned quarters (encoder rounds to nearest). Gated by `tests/fixtures/block_damage_footprints_live.json` (copied from BS/tests, every captured cell compared) plus a fractional-centre server vector in `aos_protocol168_terrain_tests`. | Per-type footprints (BDM:60-211; PROTOCOL row 37). Spheres use `d²<R²` with per-cell damage `ceil4(amt·(1−d²/R²)+2r)`, with R=2 (types 22, 23), 3 (11-14, 38, 40), 4 (7, 8, 24, 30, 37), 5 (39), 6 (9, 18) and 7 (19). Drill and landmine use R3; dynamite and C4 use R8. Cube types 3/17/31 use `ceil4(amt+E·r)` with E=5/8/0. Type 4 hits a single cell; types 5 and 36 hit a column. Types 20, 27 and 32 do no terrain damage. The centre is `floor(p+0.5)` for every type. The RNG is `Random(seed&0xFF)` in z-major order. The amount byte is u8 in quarters. | term:90-260 (`damage_cell`, `expanded_damage_cells`). Spheres collapse to a single cell (:168-169). Fractional centres are rejected except for types 15/16/21/41 (:104-113). Drill, landmine, dynamite and C4 use flat radii (:159-167, 228-249). Cubes have no random term (:219-224). Types 4 and 5/36 are swapped (:147-150). The amount byte is read as signed (:375). Only type 21 is correct (:192-212). | Port BDM verbatim: an R table, `ceil4`, `world::RetailRandom`, a `floor(p+.5)` centre, fractional centres accepted for every type, z>238 skipped, u8 amount. Gate it with `BS/tests/fixtures/block_damage_footprints_live.json` (the server matched about 1,450 cells with 0 mismatches). | M | none | N P0-2; G rank 2, C1-C5, C9, D6, D9 |
| P0-02 | **Per-cell block health and BlockManagerState(38)**<br>**Status: DONE (wave 1).** `VxlMap` now holds retail `user_blocks` and `DamagedBlock(health, original_color)`; `add_damage`/`add_user_block`/`initial_health` follow gameScene.pyd (IDA 2026-09-27: `get_initial_health` = `user_blocks[cell]` else `DEFAULT_BLOCK_HEALTH * health_multiplier`; `add_user_block` stores `health * multiplier`). 32 type 0 / 40 / prefab = 9, 32 type 1 and 33 = 3, map = 5. The multiplier comes from InitialInfo `block_health_multiplier` (session decoder now reads it). Packet 38 is decoded, re-encoded byte-for-byte against `encode_block_manager_state` and merged in wire order (damaged rows, then user rows; occupied rows parsed and ignored). Note: 38 user rows are stored exactly as sent (unscaled), which only matters when RULE_BLOCK_HEALTH != 1 (**VERIFY** the receive path's scaling then). | Initial health is 5 for map voxels, 9 for built blocks (32 type 0, 40, prefab) and 3 for snow (32 type 1) and Block Cannon (33), times `RULE_BLOCK_HEALTH`. Packet 38 is `u8 38` followed by three tables, each an `i32` count plus rows: damaged `(i16 x,y,z, u8 remaining×4, u8 b,g,r)`, user `(xyz, u8 health×4)` and occupied `(xyz, u8 pid)`. The client merges it on join, catch-up and after builds (PROTOCOL row 38, live-verified). | term hpp:206 sets one `block_health_{5.0F}` for every voxel. Damage is stored as a fraction of 5 (`VxlMap::set_damage_fraction`). No 38 decoder exists (declared ids jump from 37 to 40), and the dispatch chain has no branch for it. | Add a per-cell `user_blocks` health map and a `DamagedBlock(remaining, original_color)` map. Set health on 32 (by `block_type`), 33, 40 and 30. Decode 38 and **merge** its rows. Add a round-trip test vector from the server's `encode_block_manager_state`. | M-L | P0-01 (same replica code) | N P0-3; G rank 3, C6, C7, C11; R #5 |
| P0-03 | **Competitive BuildPrefabAction(30) is rejected: other players' prefabs are invisible but solid**<br>**Status: DONE (wave 1).** nfm `apply_live_prefab_mutation` branches on `add_to_user_blocks`: the whole model is expanded immediately on every client (range 0..0 accepted) through `apply_prefab_user_blocks` (`add_user_block(..., 9, replace_solids=True)`), colour = `retail_prefab_blend` (truncating 50/50, matches common.pyd `blend_color_component`), the owner is debited `user_blocks_added` via `confirmed_owner_block_cost`, 29 is a no-op for it, and the builder plays `prefabbuild` locally (the server excludes the builder). Smoke: one `create_smoke_ring` per model voxel on the `get_max_z_size` layer at cell+(0,0,1), radius (x_size-1)/2, 8 particles tinted by the voxel under each point (`prefab_smoke_rings` / `emit_prefab_smoke_ring`); ring particle size/velocity mapping is **VERIFY** (decompile only shows the call shape). UGC keeps the staged index-range path. The preview wallet gate now uses `model_blocks` (len(model_points)). | The server sends one packet 30 to every client with `from=to=0`, `add_to_user_blocks=1` and `color` = the blend base (`BS/server/prefab_actions.py:716-737`). Packet 29 goes to the owner only (:1253). Retail then expands the whole KV6 immediately: `add_user_block(..., 9.0, replace_solids=True)`, a 50/50 `blend_color` with the low 2 bits re-randomised, one smoke ring per top-layer voxel (`SMOKE_RING_*`), and a wallet debit of one per model voxel, even over solids. A live check showed 45 solids replaced and 32 rings for a 68-voxel bunker. | nfm:11694 rejects `last<=first`. `add_to_user_blocks` and `color` are never read. Valid ranges are staged until packet 29 (nfm:11737-11749), so observers stage forever. `prefab_placement.cpp:233-239` skips solids and uses the raw colour. The wallet counts missing voxels only and is never debited for 30 (`prefab_placement.cpp:164-170`, term:673-698). Smoke is at most 24 invented puffs, owner only (pe:245-263). | Branch on `add_to_user_blocks`: expand every voxel, blend the colour and jitter the low bits, replace solids, set health 9, apply immediately, and debit the owner by the model voxel count. Keep the index-range path for UGC only, and make 29 a no-op for competitive play. Smoke rings (8 particles, `smoke_trail` atlas; the exact call is **VERIFY**) can land in a later step. | M | P0-02 (health 9) | N P0-1; G rank 4, C16-C19; R #6 |
| P0-04 | **Packets 33 and 7 erase damage**<br>**Status: DONE (wave 1).** 33 goes through `add_user_block(..., 3.0)` without replace_solids, so a solid target is ignored and keeps its damage; 32 and 40 likewise never replace a solid (32 onto a solid no longer debits). PaintBlock(7) uses `VxlMap::recolor_voxel`, which keeps user health and the DamagedBlock. | BlockBuildColored(33) on a solid cell is ignored. On an empty cell it creates a user block at health 3. PaintBlock(7) only recolours and keeps health and damage. The server uses 7 to pin damaged shades after joins. | Both packets call `set_voxel`, which recolours and **clears damage** (`vxl_map.cpp:236`). For 33 see term:556-577; for 7 see term:797-804. Canonical repair and join catch-up therefore heal damage on the client, which later breaks blocks at a different hit from the server. | 33: return not-accepted when `map.solid()`. 7: add a recolour-only VxlMap setter that keeps the damage entry. | S | P0-02 | N P1-1, P1-2; G C13, C14 |
| P0-05 | **DONE (wave 2)** **Weapon catalog carries modded values** | Stock values (`BS/docs/WEAPONS_RETAIL.md`): pistol interval 0.4 s and reload 0.6 s; spade, pickaxe and knife intervals 0.8/0.6/0.5 s; crowbar 0.5 s; pistol recoil −0.05; the stock damage, range and explosive radii tables. | wcat (generated fb7a923, 2026-08-25) holds the modded tail: pistol 0.3 s (:1954), melee 0.4/0.4/0.25 (:1937-1939), crowbar 0.6 (:1971), pistol recoil −0.005 (:804), radii (:1949-1995). The server rejects about every fourth pistol shot and every other dig swing, and the client's ammo is never corrected. **Wave 2:** Regenerated from the uncommitted server working tree (stock restore): pistol 0.4 s / reload 0.6 / range 550 / head 45 / recoil -0.05; spade/pickaxe/knife 0.8/0.6/0.5; crowbar 0.5; pickaxe 7/40, knife 80; SMG range 350; RPG2 40; per-gun accuracy_min spread. The generator now takes blast radius and damage from the server stock catalog row, not the stale named *_EXPLOSION_RADIUS block: RPG/RPG2/AP/landmine/mine launcher 6, classic grenade 9, dynamite 8, turret rocket 50. It also attaches A1663/A1664 and A1682/A1683 (chemical and sticky throw speeds 50 + 25). Radar lifetime and range are un-swapped in both catalogs (entity generator: lifetime=A1901, sense=A1900). **Dynamite stays r8, not 5**: dynamiteWeapon binds A1632 = 8, and the stock handler radius is 8 (BS/docs/WEAPONS_RETAIL.md). The "r5" is the stale named constant, and audit B7 itself lists retail r8. Gate: tests/test_weapon_catalog.cpp `stock_restore_values_replace_the_nonsteam_mod`, test_entity_catalog `radar_and_dynamite_use_the_stock_aliases`, and the two `*_catalog_contract_check` ctests. Re-run the generator only if the server restore changes before it is committed. | After the server stock restore is committed (note 3b), run `py -3 tools/generate_weapon_catalog.py`. Also fix the entity-catalog values in the same pass: the dynamite presentation radius should be 5, not 8 (ecat:213). The radar values are covered in P1-03. | S | Server commit of `BS/shared/constants.py` STOCK RESTORE | G rank 1, B1-B7, D7 |
| P0-06 | **Networked FlareBlockEntity (type 13) creates no voxel and no light** | Every VXL chroma marker and every player flare (packet 104) becomes CreateEntity type 13. The client restores a solid voxel in the packet RGB at 5 hp and calls `add_static_point_light` with radius 5 (`BS/docs/VXL_MARKERS.md`; PROTOCOL rows 21/104). A live check on Training showed 24 lit cells. | **Wave 7: DONE.** Type 13 now restores the voxel in the packet colour (slot-0 (255,255,82) fallback) and a radius-5 static light, outside the entity part budget; DestroyEntity removes both, and a light whose voxel the terrain replica destroys is dropped. `StaticLightField` is bucketed and capped at 2048 (20thCenturyTown has 524). Local type-13 spawns use slot 0 instead of the invented (255,214,150). Test: `test_tutorial_session` flare block. Before: `apply_server_entity` (ts:2952-3001) only records the entity. The voxel-and-light path exists only for local spawns (ts:2894-2929). nfm:10536-10567 passes `color`, but nothing reads it. Local flares hard-code (255,214,150) (ts:2901, 2907). The light falloff `(1−d/r)²` is invented (`static_light_field.cpp:52-78`); see P3-13. | Route type 13 into the flare path: the voxel with `entity.color` (fallback slot 0 (255,255,82) or slot 1 (250,250,200)), a static light, and dirty chunks within the radius. Remove both on DestroyEntity or when the cell is destroyed. | S-M | none | G D4, D5; R #2, #17 |
| P0-07 | **DONE (wave 6)** **Jetpack exhaustion tail** | The server keeps thrust for `jetpack_exhaustion_tail_frames = 3` after fuel runs out (`BS/config.toml:641`, `player.py:61`), tuned for retail owners. | The client predicts a tail of 1 (ts:4044). Every burn to empty ends in a 0.03-0.2 block divergence and an ADJUST. **Wave 6:** Retail value is 3: the 60 Hz Rocketeer captures (BS/docs/RETAIL_JUMP_RESTORE.md) show the stock owner thrusting 2-3 frames after the exhausted row, and the server was calibrated to 3. The client tail is now `jetpack_exhaustion_tail_frames = 3` (with `jetpack_activation_defer_frames = 2`) in `flight_profile.hpp`. The server pins BSCF (native) owners to the 2/3 defaults in `Player._jetpack_boundary_frames`, so a host `[debug]` override only retunes the retail handoff. Gate: the balanced-flight fixture in test_tutorial_session is regenerated from the real FlightFlow ClientData trace (pack 66 at frame 360: x 148.0269, z 5.2720; the tail-1 values reproduce the old fixture exactly). Server tests: test_jetpack `test_native_owner_boundary_frames_ignore_the_retail_override`. | Make the server tail profile-aware for native clients (defer 2, tail 1), or set the client tail to 3. Decide which side owns it. | S | Server coordination | G rank 7, A14 |
| P0-08 | **DONE (wave 6)** **Parachute prediction lacks the server's deploy rules** | The server requires at least 6 blocks of clearance (centre plus 4 corners), allows one deploy per fall, closes the canopy after 30 s (1800 frames) or when `vz<−0.05` (lifted), and forbids a canopy with any jetpack (`BS/server/player.py:2864-2992`). | Prediction checks only airborne, not wading, the press edge and `vz≥0` (ts:674-693; replay at 4084-4113). A refused deploy floats locally at 0.05 g for about one RTT, then corrects. The timeout and lift closes are not predicted. **Wave 6:** `advance_parachute_rules` / `settle_parachute_after_move` / `parachute_ground_clearance` (player_movement) port `_update_parachute`, `_parachute_after_move` and `_parachute_ground_clearance` (including get_z's z=239 for out-of-map/empty columns). They cover 6-block clearance on the centre and four corners, one deploy per fall, the 1800-frame timeout, the `vz < -0.05` lift close, no canopy with any jetpack, and descent-only opening (now always, as on the server, instead of only when the profile flag is set). `tick()` and `replay_parachute_prediction` both run them. The replay re-runs the rules from each sample's own pre-move state. A closed ACK against a local canopy means the server never opened it, so the fall's deploy stays unused. The used/open-frame state is journalled in PlayerMovementState and carried through position replays. Gravity x0.05 and bit 0x01 authority are unchanged. | Port `_parachute_ground_clearance`, `used_this_fall`, `open_frames≥1800`, the lift close and the jetpack exclusion into both `tick()` and `replay_parachute_prediction`. | M | P1-19 (key change touches the same code) | G rank 8, A17 |
| P0-09 | **DONE (wave 6)** **Remote-player view vs lag compensation** | Retail snaps to the newest row and **extrapolates**. The server rewinds by `min(RTT + view_delay(0), …)` (`BS/docs/LAG_COMPENSATION.md`). | pl:489-563 lerps from the current row to the newest over one snapshot interval, with no extrapolation. Peers render about 17-33 ms older than the server assumes, so shots at strafing targets land behind. **Wave 6:** Retail extrapolation on the client, with no server change (the server keeps `view_delay = 0`). `RemoteMotionInterpolator` now snaps to each new row, including orientation, and keeps its own airborne, wade, climb and fall state across snaps. Every fixed tick it advances the peer with `step_player` on the live map and StateData gravity, using the peer's replicated buttons (input flags, hover 0x80), pack enum from the loadout, jetpack bit 0x04, canopy bit 0x01, class and InitialInfo speed scale (`remote_motion_sample`). Dead peers hold their row. Collision bodies and hit state still use the raw replica. | Either extrapolate like retail, or negotiate a native `view_delay` (profile flag) so the server rewinds one extra snapshot interval. | M | Server change if negotiated | G rank 9, A21 |
| P0-10 | **DONE (wave 2)** **Shell-reload interrupt** | Firing during a clip_reload stops after the current shell and fires with the shells loaded so far (`BS/server/player.py:1514-1538`). This applies to the shotgun, snub and RPG2. | Input is ignored while reloading, and the weapon always refills to full (wr:242-282). Native players are at a disadvantage against retail players. **Wave 2:** weapon_runtime latches a primary press during a clip_reload cycle. At that cycle end it stops the chain (final reload_completed, value 1) and replays the press on the same update, so it fires with the shells loaded so far. **VERIFY (IDA)**: whether stock end_reload also stops on a trigger that was held (not pressed) through an auto-reload. The native client only latches a press edge. | Latch the primary press, stop re-arming after the current shell, then let the shot through. | S | none | G rank 5, B8 |
| P0-11 | **DONE (wave 2)** **Melee RMB sends phantom secondaries** | Tools with `has_secondary` but no override do nothing. The spade's RMB is an inert 1 s lock (`tool.py:165`; `docs/WEAPON_SECONDARY_RECOVERY.md`). | wr:418-420 activates the secondary and sends a `secondary=1` melee ShootPacket for the spade, pickaxe, knife, crowbar and the rest. These phantom digs and hits reach the server. **Wave 2:** The has_secondary fallback is gone. RMB now activates only tools 4 and 45 (45 on the press edge only, as before; the 0.2 s repeat in WEAPON_SECONDARY_RECOVERY is unverified), UGC (41), block, prefab, paint, C4 and MG. The spade RMB is the inert can_swap lock: it lasts while RMB is held, up to 1.0 s, and gates PlayerInventory direct/wheel switches. Melee ShootPacket seed is 0 (adapter). | Activate only real overrides: tools 4 and 45, block, prefab, paint, C4 and UGC. | S | none | G rank 6, B9 |
| P0-12 | **Digging does not credit the block wallet**<br>**Status: DONE (wave 1).** Every local-player Damage(37) of a `BLOCK_GRANTING_DAMAGES` type credits `destroyed_cells` blocks (collapse debris excluded) in nfm; the old Block Sucker-only `grant_blocks()` call was folded into this path. The prefab debit is in P0-03. | Destroying a cell with a `BLOCK_GRANTING_DAMAGES` type (spade, pickaxe, knife, zombie, crowbar and so on) credits the wallet (SC:1734; `combat_runtime.py:1803+`). | Only the Block Sucker calls `grant_blocks()` (nfm:9441-9444). The mirrored wallet drifts low, and the local gate refuses builds the server would accept. | Credit the wallet on local Damage packets of the granting types. The prefab debit is in P0-03. | S | P0-01 (cell counts) | G rank 10, C10 |

### P1: very visible missing features

| ID | Title | Retail behaviour (evidence) | Client status (file:line) | Fix sketch | Effort | Deps | Found by |
|---|---|---|---|---|---|---|---|
| P1-01 | **DONE (wave 3)** **Kill banners and stingers (multikill, domination, revenge)** | `GameScene.process_packet_kill_action` (gameScene.pyd 0x10194940; `BS/docs/KILLFEED_RETAIL.md`) behaves differently depending on who the local player is. When the local player is the killer, it shows the big message KILL2-KILL5 or KILLM "{0} x Multi Kill", plus YOU_ARE_DOMINATING or YOU_GOT_REVENGE with the `ks_domination` (1.84 s) or `ks_revenge` (2.03 s) stinger. When the local player is the victim, it shows THEY_ARE_DOMINATING or THEY_GOT_REVENGE. It also updates `running_local_player_kills` and `set_killer_info` for the death cam. Relationships reset only on team-change kills. The server already sends the right fields (fixed 2026-09-26). | The KillAction handler (nfm:10868-10955) decodes `kill_count` (rt:757) and the flags (rt:751-764) but uses the flags only for scoreboard bits (pl:393-430). There are no banners and no sounds. Relationships are cleared on every ordinary death. **Wave 3:** `announce_kill_action` (nfm, KillAction branch) raises the retail banners through `kill_action_announcements` (new `include/battlespades/frontend/retail_announcer.hpp`): local killer (not a suicide) gets YOU_GOT_REVENGE then YOU_ARE_DOMINATING (victim name) and KILL2-KILL5 / KILLM (`{0}` = kill_count); local victim gets THEY_GOT_REVENGE / THEY_ARE_DOMINATING (killer name); nothing when the killer is unknown. `ks_revenge` / `ks_domination` play 2D and voice-protected in BOTH branches (capstone disassembly: four cached tuples at 0x101958f6/0x10195cce/0x10196ce8/0x101970b6); multikill banners are silent. Strings are localised through `localized_text`. `RunningLocalKills` keeps `running_local_player_kills` (reset on a local kill, +1 on a local death, forgotten on PlayerLeft); the death camera does not consume it yet (no DEATHCAM_STREAK behaviour exists natively). `apply_kill_relationships` no longer clears markers on ordinary deaths: only forced/team-change kills reset them, on killer and victim; the local-kill branch needs killer != victim. Tests: `aos_retail_announcer_tests`, `test_protocol168_players` (ordinary death keeps markers). | In the KillAction branch, raise `game_hud.set_big_message(localized_text(...))` for killer and victim, and play the `ks_*` sounds in 2D. Track running kills for the death cam. Reset relationships only on team-change kills. The strings are already in `config/localization`. | S | none | N P1-6; A P0 #1, #3; H #1 |
| P1-02 | **DONE (wave 3)** **Match-start stinger** | `SelectTeam.on_start` plays `mu_start_game` (3.00 s) and then the secondary bed. In classic mode it plays `classic_mu_start_game` (4.92 s) with no bed (`selectTeam.py:22,27`). | There are zero references. Only `start_secondary_selection_music()` runs (nfm:6394; called at 15200/15254/15262/15281/15740). **Wave 3:** A `ScreenEntryEdge` in the frontend tick fires on every entry into the team menu (`FrontendScreen::change_team` in a network match, including back-navigation from class select, as SelectTeam.on_start does) and calls `play_match_start_stinger`: `mu_start_game` then the secondary bed, or `classic_mu_start_game` with no bed in classic mode, 2D and scaled by the music slider (MUSIC_AUDIO_ZONE). The world-entry path no longer starts the bed for classic SelectTeam (it still does for ForceTeamJoin, which goes straight to SelectClass). Test: `aos_retail_announcer_tests`. **UI pass (UI-C):** the edge also fired for the in-game ChangeTeam menu (`.` / pause menu), which retail's `changeTeam.py` never does. `MatchStartStingerGate` now fires only while the local player has not yet been in the world this match (initial SelectTeam, including back-outs from the first SelectClass) and re-arms on the next match. Test: `aos_retail_input_rules_tests`. | Play the stinger at team-select entry. In classic mode, play the classic stinger and skip the bed. | S | none | A P0 #2 |
| P1-03 | **Radar station detection (Scout)**<br>**Status: DONE (wave 5).** nfm minimap filter collects the local team's live type-36 entities and reveals every enemy `retail_radar_detects` (3D squared distance < 250², the `sq_distance` of `can_detect_player`, so no longer VERIFY). Not done here: the catalog lifetime/range swap is wave 2's `entity_catalog` file; the RADAR_ping cue has no recovered trigger timing (A1903/A1904 unverified), so it is not played. | `hud.Minimap.draw` shows every enemy within 250 blocks of a friendly type-36 entity (`can_detect_player`, squared distance < 250²). There is no packet for it, and the server stopped sending 83 for radar on 2026-09-26. The radar lifetime is 45 s. The radar sweep plays `RADAR_ping` (A2924). | Minimap filter nfm:17630-17641 has no radar clause, so a placed radar reveals nothing. The catalog swaps lifetime 250 and range 45 (ecat:628, 633). There is no ping sound. | Add a filter clause: an enemy is visible if a live same-team type-36 entity is within 250 blocks (**VERIFY** 2D vs 3D). Swap the catalog values. Count the packet-21 fuse down for the label. Play the ping. | S | none | N P1-5; H #2; G rank 11, D1; A #16 |
| P1-04 | **Progressive damage darkening**<br>**Status: DONE (wave 1).** `VxlMap::add_damage` rewrites the stored RGB with `retail_dim(current, amount)` per surviving hit (compounds; alpha kept) and keeps the first-hit colour in the DamagedBlock; 38 damaged rows repaint `dim(original, initial - remaining)`. The mesher's halving is gone (emissive fixtures still classify from the DamagedBlock original colour). The offline tutorial `damage_voxel` now uses the same `add_damage`. | `shared.common.dim`: `v −= (v·round(dmg))>>3` per channel, once per hit, compounding on the stored colour (BDM:215-247). A bullet takes about 1/8 off; a spade hit takes 5/8. Packet 38 darkens against the initial health. | chunk_mesher.cpp:440-448 halves the colour once as soon as `damage_fraction > 0`. VxlMap stores only a fraction (`vxl_map.cpp:266-300`). This shows on every sub-lethal hit in every match. | On Damage(37) (term:480-494; ts:2361 offline), rewrite the stored RGB with `dim` and keep the original colour for debris. Apply 38 rows as `initial − remaining`. Remove the halving in the mesher. | S-M | P0-01, P0-02 | N P1-3; G C8; R #1 |
| P1-05 | **Burn, sudden-death and heal feedback (SetHP 2/3/4, action bit 0x20)** | SetHP type 3 plays the **burn** sound and flashes `burn_time` for `BURN_INDICATOR_TIME` (1.2 s); it is used for fire and goo ticks. Type 4 sets `sudden_death_damage_time` for a 1.2 s r/g/b tint. Type 2 sets `heal_hp_added` (effect **VERIFY**) and plays the medpack healing loop and end (A2908/A2909). WorldUpdate action bit 0x20 is `set_is_on_fire`: `molotov_player_ignite`, the on-fire loop and smoke, then burnout, or `_water_stop` when submerged (`BS/docs/WEAPONS_RETAIL.md` "Molotov fire and Chemical Bomb goo"). | **Wave 7: DONE, with a correction.** Headless IDA of `gameScene.process_packet_set_hp` shows types 1, 3 and 4 all play the same `hitplayer` (the tuple arg is one string), so the "burn sound" above is wrong and the existing cue is kept. Type 3 now starts a 1.2 s burn tint, type 4 a 1.2 s sudden-death tint (HUD art/colour **VERIFY**), type 2 plays the medpack healing cue (`heal_hp_added` loop/end split **VERIFY**). Action 0x20 drives `molotov_player_ignite`, the `molotov_player_on_fire_lp` loop, CHARACTER_MOLOTOV smoke (0.02 s), then burnout or `_water_stop`, for every roster player. `world::set_hp_feedback`/`status_tint` are tested in `test_retail_effects`. Before: SetHP (nfm:9853-9890) plays `hitplayer` for types 1, 3 and 4 (`SetHpPacket::plays_local_hit_sound`, `protocol168_runtime.hpp:44-46`) and ignores type 2. `world_action_on_fire` (0x20) is declared in `protocol168_weapons.hpp:170` and never read. Only the constant `SUDDEN_DEATH_INDICATOR_TIME` exists (`retail_constants.hpp:1882`). | Add `burn_time_` and `sudden_death_time_` to GameHudModel with a 1.2 s decay (overlay art and colour **VERIFY** in HUD.draw). Map type 3 to the burn sound. Drive the ignite, loop, burnout and water-stop cues and the character smoke from bit 0x20 for local and remote players. Add the medpack heal cues on type 2. | M | none | N P2-1; A #11, #14; H #5, #12; G E4 |
| P1-06 | **Chemical-bomb goo is invisible, and WU state bit 0x08 is misread** | The server now creates entity 31 (BlockGooEntity) patches on up to 48 exposed voxels for 4 s, with a white → (20,255,50) → green colour ramp and a static light (no smoke, no sound). WU state bit 0x08 means **touching goo only**, not water: `chem_bomb_burn_loop` (A2920), ended by A2921 (A2922 in water) (`BS/docs/WEAPONS_RETAIL.md`). | **Wave 7: DONE.** pl now derives remote wade from z>237. State 0x08 drives the chem-bomb burn loop, ended by `burn_end` or `burn_end_water_001-003`. Entity 31 draws a colour-ramped patch on its voxel (white -> (20,255,50) -> green over fuse/4) with a baked radius-3 static light recoloured 4x a second (retail `update_static_light_colour`). Damage type 43 is now a silent `dissolve` impact with green chemical debris (tint **VERIFY**). Before: Type 31 has no parts, step or presentation (ecat:543-557). pl:593 reads `state_flags & 0x08` as **wade** when it builds remote collision bodies, so a player standing in goo gets a wading body height in local contact checks. The chem burn loop and end have zero references. | Render goo as a coloured patch with the ramp and light (share the BlockFire display, P1-07). Derive wade from position (z>237), not from bit 0x08. Drive the chem burn loop and end from 0x08. | M | P1-07 | R #10; A #20; this merge (pl:593 found while checking note 3c) |
| P1-07 | **Molotov block fire looks like a stream of explosions** | `BlockFireEntity` (type 28) has a display, `calculate_colour` (hot to cold over `fuse / BLOCKFIRE_MAX_LIFESPAN` 4.0), a static light, `create_fire_smoke` (`BLOCKFIRE_SMOKE_*`: 1-2/s, lifespan 3, size 4-8) and a looping sound. The server now spawns up to 5 patches with spread and falling. | **Wave 7: DONE.** BlockFire patches use the hot->mid->cold ramp, a baked radius-3 light and BLOCKFIRE smoke at a random 1-2 puffs/s (LUT smoke, size 4-8, lifespan 3). Damage type 25 is now a silent `burn` impact (it was routed to the explosion compositor every 0.4 s in live play), and offline fire no longer pushes explosions. The patch display geometry (`create_display`) is **VERIFY**: an additive cube hugging the voxel is used. Before: ts:1792-1833 pushes a full `TerrainImpactKind::fire` explosion every 0.25 s: a glow burst, debris, invented cube flames and a dynamic light (te:390-605). | Build the patch display, colour ramp, smoke emitter and light from the constants. Stop routing fire ticks through the explosion compositor. | M | none | R #10 |
| P1-08 | **POIFocus(18) camera lock**<br>**Status: DONE (wave 5).** `PoiFocusPacket` (3× fixed16) is decoded in rt and dispatched; a living, non-spectator local player gets `poi_focus_target`, `apply_poi_focus_lock` aims `TutorialWorldSession::set_look_angles` at it every frame and `retail_gameplay_input_locked` drops movement, look, fire and tool keys. Released by death (death camera / not alive), the next local CreatePlayer, or teardown. | Reads 3× fixed16 target and activates `LookAtController`. There is no timeout; the lock is released by death, respawn or map change. The server sends it in Demolition at the objective airstrike. | **Implemented by wave 5** (decoder `PoiFocusPacket` plus the LookAtController lock in nfm); not a wave-7 change. Before: No decoder (`decode_runtime_packet`) and no handler, so it is dropped silently. | Decode it and lock the local yaw and pitch toward the point, ignoring look input, until death, the next local CreatePlayer or teardown. Never lock spectators. | S | none | N P1-4; H #6 |
| P1-09 | **Crate airdrop: no parachute, wrong fall, no sounds** | The server now airdrops respawned crates (types 3-6) from the top of the world with a falling CreateEntity state (see note 3a). The client runs `GenericMovement` with g=30. Below 10 blocks it opens `Crate_Parachute.kv6` and plays `cratedrop_chuteopen`; it multiplies v by 0.75 per move until 2 blocks up, then drops the chute. The crate bounces twice. `cratedrop_freefall` loops while falling, and `cratedrop_land` plays on impact. A 214-block drop takes 5.1 s (`BS/docs/CRATES_CLASSES_RETAIL.md` §1). | **Wave 7: DONE.** Crates 3-6 created in the air run the retail drop: g=30 at a fixed 1/60 s, chute below 10 blocks, x0.75 after each move until 2 blocks, then two half-strength bounces (`world::step_crate_drop`, tested against the 5.1 s / 214-block live trace). `Crate_Parachute.kv6` is drawn while deployed (size/lift **VERIFY**), with `cratedrop_freefall` loop, `cratedrop_chuteopen` and `cratedrop_land`. Late joiners resume from the packet height/speed. Entity gravity is now 30 for graves, corpses and bombs too. Before: Free fall only, at g=32 with restitution 0 (`local_entity.cpp:14,59-65`; ts:1623-1633), so it lands in 3.7 s while the server still has it airborne. Crates have a single part (ecat:12-14). The parachute constants exist (`retail_constants.hpp:408`) but are unused. The sounds have zero references. The server's positioned fly-by PlaySound (24/25/26) should already play through the generic path. | Build a crate state machine: g=30, a chute part between 10 and 2 blocks up, the 0.75 slowdown and bounces. Add the freefall loop, chute-open and land sounds. A late joiner resumes from the packet height and speed. Also move grave, corpse and bomb gravity from 32 to 30 (G D6, D8). | M | none | N P2-5; A #21; G D2, D3, D8; R #11 |
| P1-10 | **Disconnect reasons**<br>**Status: DONE (wave 5).** `retail_disconnect_status_key` (`retail_hud_rules.cpp`): 1→A940, 3→SERVER_OUT_OF_DATE, 4→SERVERFULL_ERROR, 9→YOU_HAVE_BEEN_VAC_BANNED, 10→CLIENT_OUT_OF_DATE, 13→INVALID_DATA, 14→A953, 15→A954, 16→A955, 17→CUSTOM_CONNECTION, 22→A961, 23/24/25→A958 formatted with KICK_REASON_*; 18 stays the reconnect path. Keys are the ones stock gamemanager.pyd references (string scan). **VERIFY** (IDA workers were full): the 23-25 template (A958 is the only kick string with a slot; english.py has no A962-A964) and 19 (kept ERROR_TEMP_BANNED_OLD). 20 CONTROL_ALREADY_BOUND keeps the generic text: its only string is the two-argument key-binding message. Beta.1 servers that send 3 when full now read "The server is out of date". | 3 is ERROR_SERVER_OUT_OF_DATE. 16 is AFK (A955 "Kicked due to being idle"), sent by `server/conduct.py:458`. 18 means the match ended. 20 is A959 and 22 is A961 "Host has left". 23/24/25 are kicks with a reason: A958 formatted with KICK_REASON_GRIEFING/HACKING/ABUSE (template **VERIFY**). | `disconnect_status_key` (nfm:184-206) maps 3 to SERVERFULL. Reasons 9, 16, 17, 20 and 22 fall back to "connection failed" (nfm:8546). 23/24/25 show the bare KICKED_ERROR. AFK and match-end look like crashes. | Edit the mapping table. Route 18 to the reconnect or match-ended path rather than a failure. Confirm the 19 and 23-25 templates in gamemanager.pyd. | S | none | N P2-6; H #4 |
| P1-11 | **Spawn-protection flash** | character.pyd has `flash_with_spawn_protection`, `is_spawn_protected` and `spawn_protection_timer`: protected characters flash, including zombie hands. `RULE_SPAWN_PROTECTION_TIME` is 3 s and ends early on attack or pickup. There is no HUD timer widget. | **Superseded by Round 2 E6** (retail spawn_color_blink_timer, team voxels only). Wave 7 had remote characters blink bright on a 0.2 s square wave while `spawn_protection>0`. Local first-person hands are not flashed: the view-model draw has no tint path. Before: `spawn_protection` is decoded per WU row (wpn:562-584 → pl:338) and never read. | Modulate the tint or alpha of remote models and local hands while `spawn_protection>0`. The flash period and which parts flash are **VERIFY**. Do not add a timer widget (retail had none). | S-M | none | H #7; G E1 |
| P1-12 | **ForceShowScores(72) does not stop movement**<br>**Status: DONE (wave 5).** forced=1 calls `clear_input()` (stop_movement) and `retail_gameplay_input_locked()` blocks movement/tool keys, mouse look and mouse buttons until forced=0. | `force_show_scores(True)` opens ViewScores, stops movement and locks the menu to the scene. `forced=0` releases it. | nfm:11050 sets `scoreboard_forced` for rendering only. Movement and fire keep working under the scoreboard. | While forced, suppress local movement and action input and zero the ClientData keys. | S | none | N P1-7 |
| P1-13 | **Bullet tracers missing** | Each gun has `tracer = <X>_TRACER` (`<name>tracer.kv6`). gameScene.pyd has a `Tracer` class and a `tracers` list; `WEAPON_TRACER_SPEED` is 200. | **Wave 7: DONE.** A Tracer flies at 200 blocks/s from the muzzle to the first replicated contact (or 128 blocks) for local and remote shots, drawn with the weapon's `*tracer.kv6` from a shared effect slot band (1920-1983). One per shot; per-pellet and the draw size (0.065) are **VERIFY**. Before: `weapon_models.cpp:145-151` loads `result.tracer`, but nothing uses it. | Add a Tracer entity: spawn it at the muzzle on each local shot and on ShootFeedback, move it at 200/s toward the hit, and draw the KV6 along the shot. One per shot or one per pellet is **VERIFY**. | M | none | R #7 |
| P1-14 | **Muzzle flash is particles, not the retail KV6** | `Weapon.draw_muzzle` draws `muzzleflash_default.kv6` for 0.05 s at `weapon_size·muzzle_flash_scale` with a random roll. It has separate view and zoomed offsets (`weapons/weapon.py:46-54, 176-210`). | **DONE (Wave 7 + first-person pass, see "First-person muzzle flash" below).** `muzzleflash_default.kv6` is drawn for the per-weapon `muzzle_flash_duration` (0.05; 0.01 for MG, tommy gun, classic SMG) with a random roll: remote shots at the recovered third-person attachment/scale on the held-weapon matrix; local shots in the view-model pass at each weapon's `muzzle_flash_view_offset` (hip) or `muzzle_flash_zoomed_view_offset` (aimed), unlit and back-face culled. The old world-space flash 0.72 ahead of the eye is gone. The sprite flash and light now run only on enhanced tiers. Before: pe:513-566 emits invented additive sprites plus smoke, and te:608-647 adds a light. The recovered offsets (`retail_character_pose.cpp:78-114`) only place particles (nfm:4693-4785). The KV6 is unreferenced. | Render the KV6 at the recovered offsets for 0.05 s with a random roll. The particles and light could stay as an enhanced-tier extra (decision D2 in section 4). | M | none | R #8 |
| P1-15 | **DONE (wave 4)** **Projectile and explosive audio** | `grenadebounce` on every bounce (A2805). The airstrike uses `airstrike_explode` or `_water` (A2779/A2780) plus the `airstrike_drop` shell whistle. Rockets have flight loops: `rocket_projectile`, `rocket_trip_projectile` and `turr_rocket_projectile` (`rocket.py:33-36`, `rocket2.py:30`). The turret plays `turr_rocketshoot` (`rocket.py:71`) and the aim start, loop and stop cues (`rocketTurret.py:74-79`). | `grenadebounce`, `airstrike_drop` and `turr_rocketshoot` have zero references. Entity 17 uses the generic `"explode"` (ecat:319-330). `tool_loop` is started only for the block sucker and snow blower (nfm:4373, 4411), and only the drill has a projectile loop (nfm:4430-4499). Only turret lock-on and lock-off play (nfm:5686). **Wave 4:** `grenadebounce`: the session queues `ProjectileBounceEvent`s from the bounce branch (`take_projectile_bounces`); nfm plays GRENADE_BOUNCE_SOUND (+-0.8) when a pre-restitution velocity component exceeds 3.2 blocks/s (BOUNCE_SOUND_THRESHOLD 0.1 per 1/32 s from the OpenSpades lineage; retail keeps the test inside world.pyd, **inferred**). Only thrown grenades 11/31/32 bounce, as in retail. Airstrike (entity 17): `advance_airstrike_audio` loops the positional `airstrike_drop` whistle while the shell exists and plays `airstrike_explode[_water]` when it is deleted (AirStrikeEntity.update/on_delete); the catalog `explode` detonation cue is suppressed for 17. Rockets: `advance_rocket_audio` gives entities 21/22 and local projectiles 12/13/46 their loops=0 flight loop (`rocket_projectile` / `rocket_trip_projectile` / `turr_rocket_projectile`); entity 21 is an RPG rocket iff its owner holds RPG_TOOL (Rocket.__init__), else a turret rocket, which also plays `turr_rocketshoot` at its first position. Turret aim: `TurretAimDetector` ports RocketTurret.update (>1 deg/s = aiming, 0.2 s tolerance) for `turret_aim_start` + `turret_aiming_lp` / `turret_aim_stop`. **Left:** a turret rocket still explodes with the RPG bank, because `destroy_server_entity` maps entity 21 to tool 12 (needs an owner-tool hint in the session). Tests: `aos_retail_event_cues_tests`. | Mirror the drill path with `start_named_voice_loop` for the rocket loops. Switch entity 17's samples. Add the bounce hook, the turret shoot and the aim cues. | M | none | A #4-#8, #13 |
| P1-16 | **DONE (wave 4)** **Pickup and build-error audio** | `bomb_pickup` and `diamond_pickup` play client-locally at the carrier (A2844/A2846; player.pyd 0x1000F150); the server never sends them. `build_error` plays on any rejected placement: block, C4, dynamite, landmine, medpack, prefab, flare and UGC tools. | PickPickup (nfm:10106-10118) updates the roster only. The catalog trigger is played only offline (ts:1774). `build_error` has zero references. **Wave 4:** PickPickup plays `bomb_pickup` / `diamond_pickup` (+-0.4) at the carrier, 2D for the local carrier (`pickup_carry_cue`). `build_error` (2D, unpitched): WeaponRuntime reports a new presentation-only `WeaponActionKind::placement_rejected` (no seed drawn, never sent) when a C4 / dynamite / landmine / medpack / radar / turret press has no ghost target; nfm also plays it for the deployable, prefab and UGC 'not valid here' refusals and for a block line that is missing an end, unsupported, out of bounds or unaffordable (blockTool.py:54-56 -- that line is now not sent, as in retail). Flare blocks have no live refusal path. Test: `test_weapon_runtime` (one build_error edge per press, none while held). | Play the pickup cue from PickPickup. Play `build_error` from every local placement rejection. | S | none | A #9, #10 |
| P1-17 | **Votekick initiation UI**<br>**Status: DONE (wave 5).** The `kick_player` binding (K) opens `KickVoteSelectModel`: SELECT_PLAYER_TO_KICK list, then the three KICK_REASON_* rows (digits, arrows+Enter, Esc backs out), drawn after the vote HUD. Denials before any packet, shown in chat: KICK_DENIED_FOR_SPECTATOR, KICK_NOT_ENOUGH_PLAYERS (<3 on the local team), KICK_DENIED_REASON_VOTE_IN_PROGRESS (a ballot is open), KICK_DENIED_REASON_VOTE_TOO_SOON with {0} seconds (server VOTE_COOLDOWN 60 s). Sends `encode_initiate_kick` = 48, player_id, target_id, reason (0-2), which `server/handlers/social.py` accepts. **VERIFY**: the retail overlay art/layout and the exact denial order were not decompiled. **UI pass (UI-B): REPLACED.** IDA showed retail validates nothing locally; the text overlay, the two-stage flow, the digit/arrow keys and the client denials were invented. It is now the retail KickVotePlayerSelect screen (`kick_vote_menu.{hpp,cpp}`); see "UI pass: in-game menus". | hud.pyd has `KickVotePlayerSelect`, `SELECT_PLAYER_TO_KICK`, `kick_reasons` (GRIEFING/HACKING/ABUSE) and client-side denials (KICK_DENIED_FOR_SPECTATOR, KICK_NOT_ENOUGH_PLAYERS, VOTE_IN_PROGRESS, VOTE_TOO_SOON), and it sends InitiateKickMessage(48). | `ControlAction::kick_player` exists (`settings_menu.cpp:87,566`; `client_settings.hpp:259`), but nothing consumes it and there is no packet-48 encoder. Players can vote in a kick but can never start one. | Build a player-select and reason overlay bound to `kick_player`, with the retail denials, and send packet 48 to the server's `server/voting.py`. | M | none | H #3 |
| P1-18 | **VXL chroma-marker cleanup missing**<br>**Status: DONE (wave 1).** `VxlMap::load` records explicit colour words matching `& 0xF0F0F0` = green/blue (z <= 238) and, after the bed (same order as the finaliser 0x1002A380), runs sub_10029FD0 in its y/x/z in-place order: remove when z-1 and z-2 are air, then copy the first solid neighbour at z+1 in +y, -y, +x, -x order into the exposed cell below (IDA 2026-09-27). Applies to disk and MapSync loads. Team-art #0028BE/#00BE2A untouched; pinned by a synthetic VXL test (full-map inventory pin not added: no map fixtures in the client tests). | `vxl.pyd` `sub_10029FD0` masks each colour with `0x00F0F0F0` and matches it against pure green (slot 0) and pure blue (slot 1). It removes a match only when the two cells above are air, and the exposed voxel below takes a neighbour's colour. Embedded markers and team-art colours stay. The server recreates the lights as type 13 (`BS/docs/VXL_MARKERS.md`). | `vxl_map.cpp:95-180` has no such pass. Pure green and blue marker blocks render on Training, TheColosseum, GreatWall, Frontier, ArcticBase, 20thCenturyTown, AncientEgypt and others. | Add a post-load pass in `VxlMap::load` (and on MapSync loads) that follows the rules exactly. Pin it with the VXL_MARKERS inventory. | S-M | P0-06 (lights come back via type 13) | R #3; G D5 |
| P1-19 | **DONE (wave 6)** **Parachute deploy key** | The stock client deploys with **SPACE mid-air**. Canopy state is server-owned (WU state bit 0x01) (`BS/docs/PARACHUTE.md`). | Native deploys on Z/hover only (ts:674). The server ignores native SPACE for the chute (`BS/server/player.py:2961-2965`). **Wave 6:** An airborne SPACE press edge (the consumed, latched jump sequence, not the held jump) is a deploy request. D3 is taken as recommended: the Z/hover edge stays as an extra binding. Both edges feed the P0-08 rules. Server: `_update_parachute` now accepts the jump edge from native owners (bots still excluded), so retail owners are unchanged. BS/docs/PARACHUTE.md is updated. Tests: server test_parachute (native SPACE opens, Z still opens, SPACE and Z share one deploy per fall); client test_tutorial_session (airborne SPACE deploys, a low hop never floats, jetpack+chute never opens). | Map an airborne SPACE *press* (not the held jump) to a deploy request, and let the server accept it from native owners. Whether Z stays is decision D3. | S | P0-08; server change | G A16 |

### P2: polish

| ID | Title | Retail / evidence | Client status (file:line) | Fix sketch | Effort | Deps | Found by |
|---|---|---|---|---|---|---|---|
| P2-01 | Map rollover on the same peer<br>**Status: NOT DONE (wave 5), by decision.** The stock client does not do this: its GameScene only freezes on 52 and reconnects (the ClientInMenu ack is the BattleSpades compatibility hook in `client_patches/session_transition_patch.py`). The server retires a peer that does not ack with reason 18 and the native client's existing reconnect keeps working, so this was left alone. | After MapEnded(52), the hook opens LoadingMenu and sends ClientInMenu(110). The server then streams InitialInfo, MapSync and StateData on the live peer. | nfm:11016 arms `map_transition_armed` but sends no 110. The server retires the peer with reason 18, and the client reconnects with a full join. It works, but it is slower. | Send `ClientInMenu(1)` after 52 and accept 114 plus the loader sequence live. | M | P1-10 (reason 18) | N P1-8 |
| P2-02 | TeamLockScore(81) not enforced<br>**Status: DONE (wave 5).** Team SetScore rows (type 0, team 2/3) are dropped while that team's `locked_score` is set; player rows still apply. | Retail ignores team SetScore rows while the team is locked. | The flag is stored (nfm:10729-10733), but SetScore (nfm:10801) never checks it. | Check the flag in SetScore. | S | none | N P2-2 |
| P2-03 | TeamMapVisibility(83) index inverted<br>**Status: DONE (wave 5).** The minimap now reads `server_team_map_visibility[local_team]`. | `teams[seeing_team].can_see_other_team` is keyed by the seeing team. | nfm:17639 indexes by the seen team. The server does not send 83 today. | Index by the local (seeing) team. | S | none | N P2-3 |
| P2-04 | MinimapBillboard(41)<br>**Status: DONE (wave 5).** The key filter is gone; every billboard also draws a 16 px minimap icon (following its tracked entity); `objective_packet_billboard_asset` keeps the aliases and accepts any other bare identifier as `png/ui/<name>.png` (paths/extensions still rejected). | Retail never reads `key`, draws a minimap icon as well as the world marker, and has no name whitelist. | Filters by `key` (nfm:20099), draws the world marker only, and whitelists `icon_name`. Sent through the API only. | Drop the key filter, add the minimap icon, and relax the whitelist. | S | none | N P2-4 |
| P2-05 | Owner WorldUpdate row health applied<br>**Status: DONE (wave 5).** The owner row no longer touches HUD health, `set_server_health` or the death presentation; SetHP(5) and KillAction(46) own them. | The retail owner-row handler skips health, orientation, input and tool. | nfm:9619 applies the owner's `health` to the HUD and death state. | Skip health on the owner row and rely on SetHP. | S | none | N P2-7 |
| P2-06 | Death-screen hints<br>**Status: DONE (wave 5).** DEATH_CLASS_CHANGE_HINT (with the change-class key) under the respawn label while a respawn is pending, and VIP_DEAD_CAM_INSTRUCTION in VIP's no-respawn dead camera. **VERIFY**: placement is ours (not decompiled). | Retail shows DEATH_CLASS_CHANGE_HINT, and VIP_DEAD_CAM_INSTRUCTION in the VIP dead cam (character.pyd, gameScene.pyd). | The respawn overlay draws only the respawn label (hud:2409-2430). | Decompile the set_dead path for placement (**VERIFY**), then add both hints. | S | none | H #8 |
| P2-07 | Hardcoded English HUD and wrong labels<br>**Status: DONE (wave 5).** `GameHudPresentationContext::localize` resolves RESPAWNING_IN {0}, NEVER_RESPAWN, VIP_YOU_ARE_VIP and SCORE_REASON_CODES keys (`retail_score_reason_key`); ViewGameStats awards draw GAME_STAT_TYPES keys. English fallbacks fixed to "Contest hill" (25) and "Carry diamond" (40). The localization generator now keeps retail's mixed-case keys (TDM_Kill, MOST_Kills, …: +289 per language), so these and the rank-up reason labels translate. | Retail resolves labels through `strings`: RESPAWNING_IN, NEVER_RESPAWN, SCORE_REASON_CODES, GAME_STAT_TYPES, VIP_YOU_ARE_VIP. TC_Contend is "Contest hill" and DIA_Carry is "Carry diamond" (english.py:796). | English literals at hud:136-212, 1717, 2414-2419 and mo:508-545. hud:165 has "Contend Territory" and hud:179 has "Carry Diamond". | Route the labels through `localized_text` with the key tables. | M | none | H #9, #10 |
| P2-08 | Invented ability hint text<br>**Status: DONE (wave 5), decision D4.** The hint is behind Settings > Main > "Ability key hints" (`ability_hints`, default off, persisted) and its five strings are localized in every shipped language (`PARITY_HUD_OVERLAYS`). | Not in retail english.py or hud.pyd. Retail shows only the parachute and disguise icons. | nfm:19325-19339, drawn at hud:1925-1934 ("Z: deploy parachute in air", …). English only. | Remove it, or put it behind an option (decision D4). | S | D4 | H #11 |
| P2-09 | **DONE (wave 4, partial)** Remaining equipment and menu audio | Parachute open, loop and close (A2957-A2959). `JP_lowthrust_lp`. Riot-shield bullet and melee impacts (A2952/A2953). Sticky grenade attach and attach_water (A2929/A2930). `bomb_fuse_lp` and `bomb_tick`. `build_light`. `secondary_menu_bed_002` on ViewGameStats. The second barrel `shotgun_double_fire02`. `ugc_colour_select`. | All have zero references. The double shotgun catalog has only `_fire01` (wcat:1947). Only bed `_001` is used (`openal_frontend_audio.cpp:2233`). The comment at ts:1728 about medpack cues is wrong. **Wave 4:** Parachute: `advance_character_equipment_audio` plays PARACHUTE_open / _close on the canopy edge (local prediction, remote WU bit 0x01) and loops PARACHUTE_loop while it is out, 2D for the local player. `bomb_fuse_lp` loops on any character holding BOMB_TOOL (25), and an armed ground bomb (raw CreateEntity fuse > 0) loops it and plays `bomb_tick` (0.5 s, faster under 2 s; formula approximate). `secondary_menu_bed_002` starts 2 s in when ViewGameStats opens. Double shotgun: the local shooter hears fire01 with two shells and fire02 with one (Shotgun2Weapon.update_ammo); observers always hear fire01, a retail quirk kept. `build_light` plays at the cube after a live flare placement is sent. Radar stations loop RADAR_ping (0.75). Sticky attach / attach_water plays once when a local sticky projectile sticks. Medpack: SetHP type 2 re-arms a 1 s window that holds the 2D healing cue, and reaching 100 HP plays healing_end (heal_hp_added). Supply crates: `cratedrop_freefall` while falling, `cratedrop_chuteopen` within 10 blocks of the ground, `cratedrop_land` on a fast touchdown (threshold inferred). `ugc_colour_select` on UGC palette picks (which callers suppress it is unrecovered, **VERIFY**). **Left:** riot-shield impacts (no client path calls an entity hit for entity 39), live sticky attach (the server never creates entity 35), and `JP_lowthrust_lp`: retail plays it only for `jetpack_passive and not jetpack_active`, and the glide policy makes passive imply active, so it never fires. | Hook each cue to its event. Alternate the shotgun barrels. Play bed 002 on the stats screen. | M | P1-19 (parachute events) | A #12, #15, #17-#19, #22-#25 |
| P2-10 | Default look is not retail | Retail uses 2 directional lights plus ambient, radial fog and baked AO, with no shadows, emissive voxels or local atmosphere. | **Wave 7: D1 and D2 applied.** A new install (no settings file) starts on the Compatibility tier, now shown as RETAIL; turning the Compatibility Shader toggle off lands on Medium. Server StateData fog is authoritative in every tier; the local atmosphere only tints sun/ambient/horizon. Before: Default `ShaderQuality::medium` (`client_settings.hpp:185`) enables shadows, emissive palettes and dynamic lights (`quality_profile.cpp:44-60`). Enhanced tiers replace server fog on official maps (nfm:8708-8715). | Product decisions D1 and D2. | S | D1, D2 | R #4 |
| P2-11 | Water and sea rendering (**VERIFY**) | `draw_sea` plus `sea_vert`/`sea_frag`: 2 lights, fresnel floor 0.3, specular 10, AO, noise and fog. The scene has a water colour. Skydomes add wave layers. | **Wave 7: NOT DONE.** Needs the `draw_sea` reversal (L). Before: z=239 is a flat `bed_water_color` (`chunk_mesher.cpp:315-317, 428`; nfm:8747-8752). There is no sea pass and nothing beyond the map edge. | Reverse `draw_sea` headlessly and port the sea shader as its own pass. | L | IDA | R #9 |
| P2-12 | In-flight smoke for molotov, chemical and GL projectiles and the bomb fuse | `ExplodeOnImpactEntity.update` calls `create_fire_smoke`, with Molotov and Chemical overrides. `BombTool.create_fuse_fx` uses `BOMB_SMOKE_*`. | **Wave 7: DONE.** Molotov (27) and chemical (32) projectiles emit LUT fire smoke every 1/60 s (MOLOTOV_SMOKE size 3-5, lifespan 2; speed range **VERIFY**). A held bomb (tool 25) emits fuse smoke at 25/s with the BOMB_TOOL_SMOKE FP/TP offsets. GL grenades emit nothing, matching the base `create_fire_smoke` (pass). Before: Trails exist only for types 12, 13, 46, 21 and 22 and the block cannon (nfm:22593-22640). | Port the smoke parameters (**VERIFY**) using the LUT smoke path. | M | none | R #12 |
| P2-13 | Entity-hit and placement particles invented | `Entity.hit` emits grey (127,127,127), 5 particles, velocity 0.25, size 2 (`entity.py:76-79`). The placement effect is **VERIFY** (`GameScene.add_block`). | **Wave 7: entity hit DONE** (grey (127,127,127), 5 cubes, 0.25, size 2). The single-block placement puff is unchanged pending the `GameScene.add_block` reversal. Before: Yellow `glow_cube` sparks (pe:150-182). A `soft_round` placement bloom (pe:217-243); that atlas does not exist in retail. | Use the grey 5-cube burst. Reverse `add_block`. | S | none | R #15 |
| P2-14 | Snowke ring and atlas | `create_snowke_ring` and `SnowkeTrail_anim_8x8` (`images.py:920`) for snowballs and the snowblower. | **Wave 7: DONE.** `ParticleAtlas::snowke_trail` (SnowkeTrail_anim_8x8) and `emit_smoke_ring` (SMOKE_RING constants) exist; snowball impacts emit the snowke ring and the block-cannon trail uses the snowke sheet. Prefab smoke rings (P0-03) can call the same emitter. Before: No snowke atlas (`particle_system.hpp:57-63`). Snow effects reuse `smoke_trail`. | Add the atlas and the ring. | S | Smoke-ring code from P0-03 | R #16 |
| P2-15 | Official maps ignore server SkyboxData(51) | Retail uses the server's dome name. | **Wave 7: DONE.** The server's packet-51 dome is used when it loads; the official-map table is only the fallback. Before: nfm:8697-8699 prefers the local table for official names (`map_catalog.cpp:34-65`). | Prefer the server's dome when it is valid; use the table as a fallback. | S | none | R #19 |
| P2-16 | **DONE (wave 6)** LockToZone clamp zeros velocity | Native `lock_box` clamps position only, inside `update` (world.pyx:1569-1573). | `constrain_player_to_bounds` (`player_movement.cpp:458-492`) also zeros outward velocity and runs after the step (ts:~716). This causes small corrections at the boundary. **Wave 6:** `constrain_player_to_bounds` now clamps position only, `min(max(p, lo), hi)` stored as float32. It runs inside `step_player` (new `lock_box` argument) right after the fall-delta bookkeeping, both in `tick()` and in the owner-row replay (which previously never clamped). `set_server_movement_bounds` only stores the box, like `set_locked_to_box`. Note: the server itself never applies lock_box to players; retail clamps client-side the same way. | Clamp position only, right after the fall-delta bookkeeping. | S | none | G A11 |
| P2-17 | **DONE (wave 2)** Spread bloom missing in shot FX | Retail FX use the current `accuracy_spread`. | `replicated_shot.cpp:54` always uses the base accuracy. **Wave 2:** WeaponAction.accuracy carries the prep_shoot accuracy (bloom before the per-shot increase). The local FX pass it to replicated_hitscan_impacts. Remote shots use a per-shooter `observe_hitscan_bloom` replica (decay plus per-shot increase). Zoomed shots still use accuracy_zoom. | Pass the current accuracy. | S | none | G B11 |
| P2-18 | Single pellet resolves dead-centre on the server | Retail scatters `pellets=1` too. | Client FX scatter, but the **server** resolves dead-centre (`BS/server/combat_runtime.py:640`). | Server fix: seed-expand single pellets. Retail server behaviour is **VERIFY**. | S | Server | G B12 |
| P2-19 | **DONE (wave 2)** Empty auto-switch and restock auto-reload | `auto_switch_tool` when dry with no reserve. An empty magazine auto-reloads after a crate. | Neither is implemented (`weapon_state.cpp:140-179`). **Wave 2:** A crate restock with an empty magazine and reserve sets reload_next_update, so the reload starts on the next tick. A dry pull with nothing to reload, or an empty grenade/molotov/sticky press, requests an auto switch. PlayerInventory then moves to the next toolbar slot with ammo (origin automatic). **VERIFY (IDA)**: Character.auto_switch_tool target choice (character.pyd). | Call `request_reload()` after a restock, and add the auto-switch. | S | none | G B17, B18 |
| P2-20 | **DONE (wave 4)** Mixer voice cap | Retail had 128 OpenAL sources. | 48 one-shot voices (`OpenAlFrontendAudioConfig::max_one_shot_voices`). Cues can drop during explosion bursts. **Wave 4 (D5):** The one-shot pool defaults to 128 voices (`retail_one_shot_voices`); OpenAL Soft's default context has 256 sources. If the device runs out earlier, start keeps whatever it got once 64 voices exist. Stealing already skips protected voices; stingers, the win/lose sting and the match-start stinger are protected. | Decision D5. | S | D5 | A mix notes |

### P3: minor or unverified

| ID | Title | Detail (client file:line) | Found by |
|---|---|---|---|
| P3-01 | Spectator join ordering (**VERIFY**) | The client sends ClientInMenu(0) before NewPlayerConnection. Spectator → team uses ChangeTeam(77); retail re-sends 13+15. Verify that the server accepts 77 from spectators with the same locks (nfm:14892, 14913). **DONE (wave 5):** retail order now: NewPlayerConnection(15) then ClientInMenu(0) for both the spectator and the class join (the server's spectator admission edge). Spectator → team no longer sends 77: the class choice sends SetClassLoadout(13, instant) + NewPlayerConnection(15), which the server routes through `handle_spectator_rejoin` (same `change_team` locks). Live check pending. | N P2-8 |
| P3-02 | CreatePlayer drops `high_minimap_visibility` (**VERIFY**) | pl:~250. It is fine if the server replays packet 17 after respawn. Check VIP bosses. | N P2-9 |
| P3-03 | Restock type 0 does not restore health | nfm:10075. SetHP covers it, so it is harmless. | N P2-10 |
| P3-04 | ClockSync takes max() and never rewinds | rt:208, nfm:10299. Deliberate. | N table |
| P3-05 | Kill-feed icons for UGC types 27-30 | nfm:580-585. Retail shows no icon. A deliberate improvement; keep it or drop it. **DONE (wave 5):** kill types 27-30 now draw no icon, like HUD.add_kill. | H #13 |
| P3-06 | Teabag reason 221 blank popup (**VERIFY**) | `score_reason_label_impl` returns blank. Check retail `add_score_reason` with an empty code. **Unchanged (wave 5):** still blank; IDA workers were full, so retail `add_score_reason` with an empty code stays **VERIFY**. | H #14 |
| P3-07 | `draw_weapon_deployment_hud` and `draw_equipped_tool_tip` (**VERIFY**) | Both exist in hud.pyd with no named native equivalent. Decompile and compare. **Unchanged (wave 5):** not decompiled (IDA workers full); still **VERIFY**. | H #15 |
| P3-08 | **DONE (wave 4)** `chat` cue plays for every LocalisedMessage (**VERIFY**) | nfm:10986, 11009. Bursts may spam the cue. **Wave 4:** Retail plays `chat` only in HUD.on_key_press when the local player submits a non-empty line (hud.pyd L210-213); gameScene has no `chat` reference. Received chat and LocalisedMessage lines are now silent and `close_chat(true)` plays the cue. | A mix notes |
| P3-09 | **DONE (wave 4)** No win or lose sting on a draw or for spectators (**VERIFY**) | mo:17-25, nfm:15008. **Wave 4:** ViewGameStats.play_win_loose_sound: only the losing team hears `mu_lose_game`; a draw and spectators fall through to `mu_win_game`. It plays once when the stats screen is visible, scaled by the music slider. (ViewScores' variant stays silent on a draw; the native client has one results overlay, which follows ViewGameStats.) | A table |
| P3-10 | Achievement sounds | `achievement_unlock` and `_notyou`. Depends on having achievements. | A #26 |
| P3-11 | Interior colour of dug terrain (**VERIFY**) | **Wave 7: open (VERIFY).** Before: `vxl_map.cpp:137, 159` use the last surface colour. Retail `generate_ground_color_table` is unknown. | R #13 |
| P3-12 | Low-2-bit colour jitter on single-block placement (**VERIFY**)<br>**Status: VERIFIED - no change (wave 1).** Headless IDA 2026-09-27: vxl.pyd `make_color`/`set_point` (sub_10019780/sub_10029DA0) store the exact colour and common.pyd `blend_color` is a pure truncating blend; nothing re-randomises low bits. The low-bit spread seen on live prefabs comes from the KV6 voxel colours, so neither single blocks nor prefabs get jitter. | term:556-577, 648-670, 804. Confirmed for prefabs (P0-03) only. | R #14 |
| P3-13 | Static flare light kernel (**VERIFY**) | **Wave 7: kernel unchanged (VERIFY);** the field is now spatially bucketed so hundreds of map flares stay cheap. Before: `static_light_field.cpp:52-78` uses an invented `(1−d/r)²`. | R #18 |
| P3-14 | z=239 bed overwrites authored colour (**VERIFY**)<br>**Status: VERIFIED - no change (wave 1).** Headless IDA 2026-09-27: vxl.pyd sub_10029900 (first step of the finaliser) sets every z=239 cell solid and overwrites its colour with one map-wide value, so retail discards authored z=239 colours too; the client's bed overwrite already matches. | `vxl_map.cpp:172-176`. | R #20 |
| P3-15 | **DONE (wave 6)** `RULE_ENABLE_FALL_ON_WATER_DAMAGE` ignored | `player_movement.cpp:666-688`. Only the landing sound is affected. **Wave 6:** InitialInfo `enable_fall_on_water_damage` is decoded into `TutorialSessionConfig::fall_on_water_damage`. `movement_config_for_class(..., fall_on_water_damage=false)` zeroes the water multiplier, as retail GameClass does. | G A6 |
| P3-16 | **DONE (wave 6)** Jetpack fuel values hard-coded | ts:3986-4052. The values match but are not read from props. **Wave 6:** Added `retail_jetpack_properties` (all nine JETPACK_PROPERTIES fields for packs 66-69) in flight_profile.hpp. The prediction reads the start delay, activation cost, damage refill delay and max fuel from it, and static asserts pin the values. | G A13 |
| P3-17 | **DONE (wave 6)** Canopy landing damage predicted as 0 | Wrong land sound only. **Wave 6:** `parachute_landing_damage` ports `_parachute_speed_damage` (free fall that reaches the same landing speed, class curve, z>237 water multiplier). A landing on a fall that touched a canopy now reports that damage for the land cue. | G A19 |
| P3-18 | **CLOSED, no gap (wave 6)** Fixed-point gravity from StateData | `protocol168_session.cpp:376-385`. Drift only in non-1.0 gravity modes. **Wave 6:** The server simulates with `canonical_gravity` = floor(g*64+0.5)/64 (BS/server/map_metadata.py) and sends that same value, so the client's 1/64 fixed-point decode is exact. There is no drift and no change was needed. | G A22 |
| P3-19 | **DEFERRED (wave 6)** Lost ClientData button edges | `live_protocol168_connection.cpp:364`. A native-only improvement: piggy-back the previous buttons. **Wave 6:** Not implemented. It needs a ClientData wire trailer (previous frame's buttons) on both sides, plus a change in the server's `_synthesize_missing_frame` so it uses the piggy-backed flags and updates `_pending_packet_flags`. Two existing mitigations make the residual rare and small: the server never stamps a self row with a refilled label, and it refills gaps with the held input (BS/docs/RETAIL_INPUT_LOSS.md). Left for a dedicated netcode pass with a lossy-proxy gate. | G A23 |
| P3-20 | Recoil crouch/zoom modifiers and side RNG (**VERIFY**) | ts:2183-2193. Needs `Character.shoot` in IDA. | G B13 |
| P3-21 | **DONE (wave 2)** ShootPacket damage and penetration fields | nfm:12093-12101. The server ignores them today. **Wave 2:** nfm now sends: guns damage = trunc(block_damage), penetration 2 (int of 2.5); melee damage = trunc(tool block damage), penetration 0, seed 0. Classic spade RMB still sends the primary block damage rather than secondary_damage (the server ignores the field). | G B19 |
| P3-22 | **VERIFY, partly researched (wave 6)** Spectator free-fly (**VERIFY**) | `death_camera.cpp:89-145`: `spectator_free` is a static anchor. **Wave 6:** Stock gameScene.pyd has `aoslib/camera/controllers/flyController.py` (`FlyController.update` sub_10037670, `on_mouse_move`, reading `FLYCAMERA_TRAVEL_SPEED`) and a spectator `TankController` (`start_chase`, `chase_next_player`, `find_chase_player`). That is strong evidence of retail WASD free-fly. The travel-speed value and the key mapping were not recovered: the 46 KB Cython body is too large for the headless decompile output, and the constant is set in module init. The native free view is still the static anchor. It only occurs when nobody can be chased. | G E7 |
| P3-23 | MG `AnimSlowPullout` (**VERIFY**) | **Wave 7: open (VERIFY).** `animSlowPullout.py` is in the retail source (0.5 s, last 10% drops y/z by 10*dt/length); wiring it needs the MG deployment view-model state. Before: `retail_view_model.cpp:142-149` uses a generic sway. | R minor |
| P3-24 | StopMusic(27) stops in-game music (**VERIFY**) | Mapped to `stop_menu_music`. | N table |

---

## 3. Notes and corrections to the audits

**a) Supply drops now exist; the audio audit is out of date here.** Audio audit #21 says "the server also has no supply-drop system". That is no longer true. The BattleSpades server now airdrops respawned crates:
- The crate is re-created at the drop altitude with a falling CreateEntity state and parachutes down.
- The server models 30 blocks/s², the 0.75 slowdown between 10 and 2 blocks, and a positioned fly-by cue (ids 24/25/26).
- Late joiners receive the current height and fall speed.

See `BS/docs/CRATES_CLASSES_RETAIL.md` §1. The crate parachute model, the corrected fall and the freefall, chute-open and land sounds are therefore real, player-visible gaps: they are merged into **P1-09**, not left "blocked on the server".

**b) Regenerate the weapon catalog only after the server restore is committed.** The server's stock-value restore is still **uncommitted**: the STOCK RESTORE block in `BS/shared/constants.py:7408-7427`, `game_constants.py`, and the untracked `docs/WEAPONS_RETAIL.md`. **P0-05** must wait until that lands. Then run `py -3 tools/generate_weapon_catalog.py` and check the diff against the WEAPONS_RETAIL tables. Regenerating from the working tree before the commit risks baking in values that are not final.

**c) Chemical-bomb goo and molotov fire changed on the server.** See `BS/docs/WEAPONS_RETAIL.md` "Molotov fire and Chemical Bomb goo".

The server now does the following:
- It spawns entity 31 `BlockGooEntity` patches: up to 48 exposed voxels for 4 s, dissolving with Damage type 43.
- It uses WorldUpdate **state bit 0x08 = touching goo only**, not a water flag.
- It spawns up to five spreading BlockFire (28) patches per molotov.
- It uses action bit 0x20 for "on fire".
- It sends SetHP type 3 for both fire and goo ticks.

On the client:
- Goo is **invisible**: type 31 has no presentation.
- The client reads state bit 0x08 as *wade* in `protocol168_collision_bodies` (`pl:593`), which is wrong now.
- Action bit 0x20 is declared but never read.

These are covered by **P1-05**, **P1-06** and **P1-07**.

**d) Items marked VERIFY need IDA.** The IDA MCP server was down for all five audits. Before implementation, confirm each VERIFY item with the headless-IDA recipe (the server repo's `scratchpad/crates_ida/` scripts and the Cython recipe in the retail-announcements notes). The main ones are:
- burn and sudden-death overlay art;
- spawn-protection flash period;
- `heal_hp_added`;
- tracer per shot vs per pellet;
- smoke-ring call and projectile smoke parameters;
- `draw_sea`;
- ground colour table and placement jitter scope;
- disconnect templates 19 and 23-25;
- spectator free-fly;
- recoil modifiers.

**e) Where the audits disagreed:**
- The C4, landmine and dynamite centre follows the server's 2026-09-26 live fit, `floor(p+0.5)`, not the older `int()` reading in client comments (G unverified list).
- Darkening appears in three audits: N P1-3 describes it as 0.125 per damage point, while G C8 and R #1 give the `dim` formula. They agree. Use the BDM `dim` port.

---

## 4. Product decisions for the user

| # | Decision | Options | Recommendation |
|---|---|---|---|
| D1 | Default shader tier | Keep **Medium** (shadows, emissive voxels, dynamic lights; not retail), or default to **Legacy** or a new "Retail" preset that matches the audited retail equations | Add a "Retail" preset (Legacy equations) as the default for new installs, and keep Medium one click away. |
| D2 | Local horizon-fog override on official maps | Enhanced tiers replace server fog with local horizon fog (nfm:8708-8715), or server fog stays authoritative in every tier | Keep server fog authoritative in all tiers; any local atmosphere should only tint on top of it. |
| D3 | Parachute key | Retail SPACE only, or SPACE **plus** the native Z key | Keep Z as an extra binding (harmless once SPACE works, P1-19), unless strict 1:1 controls are wanted. |
| D4 | Extra on-screen jetpack and parachute hints (non-retail, English only) | Remove, keep, or make an option that is off by default | Option, off by default and localised if kept. |
| D5 | Mixer one-shot voice cap | 48 (current) or 128 (retail OpenAL sources) | 128 if the target hardware copes; otherwise 64 with priority stealing that protects stingers and announcer cues. |

---

## 5. Recommended fix waves

The waves are grouped by code area so that parallel agents do not edit the same files. Waves 1 and 2 are sequential inside themselves. Waves 3-7 can run in parallel with each other once wave 1 has merged, because P1-04 and P1-06 touch the terrain replica.

| Wave | Area and main files | Items | Notes |
|---|---|---|---|
| 1 | **Terrain, damage model, health, packet 38 and prefab 30**: `protocol168_terrain.*`, `vxl_map.*`, `chunk_mesher.cpp`, `prefab_placement.cpp`, the prefab section of nfm (11620-11780), and the wallet (nfm:9441) | P0-01 → P0-02 → P0-04 → P1-04 → P0-03 → P0-12; then P1-18, P3-12, P3-14 | One agent. Gate with `block_damage_footprints_live.json` and a packet-38 round-trip vector. |
| 2 | **Weapons catalog, reload and melee RMB**: `weapon_catalog.generated.cpp`, `weapon_runtime.cpp`, `weapon_state.cpp`, `replicated_shot.cpp`, `entity_catalog.generated.cpp` (radar and dynamite values) | P0-05 (after the server commit), P0-10, P0-11, P2-17, P2-19, P3-21 | Blocked on note 3b for P0-05 only. |
| 3 | **Kill stingers, banners and announcer**: the KillAction branch of nfm (10868-10955), `game_hud` big messages, team-select entry | P1-01, P1-02 | Small; quick visible win. |
| 4 | **Audio linking**: nfm audio hooks (4373-4499, 5600-5715, 10106), `openal_frontend_audio.cpp`, `weapon_catalog` sound slots | P1-15, P1-16, P2-09, P2-20, P3-08, P3-09 | Coordinate with wave 7 for fire and goo cues (P1-05 and P1-06 own those sounds). |
| 5 | **HUD, disconnect and localisation**: `game_hud.cpp`, `match_overlays.cpp`, nfm:184-206, the minimap filter (nfm:17620-17685), `settings_menu.cpp` | P1-10, P1-03, P1-12, P1-17, P2-02 to P2-08, P3-05 to P3-07 | The radar clause lives in the minimap filter, so it sits here. |
| 6 | **Parachute and jetpack prediction, and fairness**: `tutorial_session.cpp` flight code (600-700, 3930-4113), `player_movement.cpp`, `protocol168_players.cpp` interpolation | P0-07, P0-08, P1-19, P0-09, P2-16, P3-15 to P3-19 | P0-07, P1-19 and P0-09 need matching server changes. |
| 7 | **Rendering and effects, plus SetHP and entity presentation**: `particle_effects.cpp`, `terrain_effects.cpp`, `weapon_models.cpp`, `local_entity.cpp`, `static_light_field.cpp`, `apply_server_entity` (ts:2890-3001), the SetHP handler (nfm:9853), `pl:593` | P0-06, P1-05, P1-06, P1-07, P1-08, P1-09, P1-11, P1-13, P1-14, P2-11 to P2-15, P3-11, P3-13, P3-23 | P0-06 first (collision). P2-11 and the other VERIFY items wait for IDA. |

Decisions D1-D5 (section 4) can be taken at any time; they only unblock P2-08, P2-10 and P2-20.

---

## Integration 2026-09-27

One pass after waves 1-7 merged in the main checkout (nothing committed).

### Audit of the shared files

Every hook claimed in the status notes above was checked by symbol in nfm, ts, pl, rt, hud, mo and wr: the kill announcer (`announce_kill_action`, `ks_*` via `retail_announcer.hpp`), the match-start stinger, `placement_rejected` / `build_error`, airstrike and rocket loops, the bomb fuse and tick, the chat cue only in `close_chat(true)`, stats-screen bed 002, per-shooter bloom (`observe_hitscan_bloom`), ShootPacket damage and penetration, POIFocus decode and lock, the ForceShowScores input lock, `retail_disconnect_status_key`, the radar minimap clause, packet-83 local team, the billboard filter, the owner-row health skip, death hints, `localize`, `ability_hints`, the votekick (K + packet 48), spectator join order, `RemoteMotionInterpolator` push/reset/tick, packet 38 dispatch, prefab 30 `add_to_user_blocks` with smoke rings, the Damage wallet credit, the `model_blocks` gate, the block-health multiplier, type-13 flares, SetHP tints and the medpack cue, on-fire 0x20, goo 0x08, the crate drop, the spawn-protection blink, tracers, the KV6 muzzle flash, grey entity-hit cubes, molotov/chemical smoke, `bomb_smoke_clock` (present again; it had been re-applied after the 01:37 clobber) and the server skybox preference. All were present. `tests/test_protocol168_terrain.cpp` is complete (packet-38 vector and live-footprint fixture present).

Fixed in this pass:
- nfm set the local `initial_wade` from WorldUpdate state bit 0x08. That bit is `set_touching_goo` (P1-06), so it now uses the water plane (`position.z > 237`), like the remote bodies in pl.
- `aos_revival_identity_live_smoke` now links `nlohmann_json::nlohmann_json` (`src/CMakeLists.txt`).

### Server (BS)

The full-server refusal was already `DISCONNECT.ERROR_FULL` (4) in the working tree. f3518e2 replaced the old `disconnect(reason=3)`, so Beta.1 hosts still send 3. The server never sends 3 (`ERROR_SERVER_OUT_OF_DATE`). Added `tests/test_bot_human_slots.py::test_full_server_refuses_with_retail_error_full_not_out_of_date` and a disconnect-reason note in `BS/docs/PROTOCOL.md` (Transport).

### Build and tests

- `out/build/parity-integration`: preset native-dev, VCPKG_ROOT=C:\vcpkg, shared `out/vcpkg_installed`, warnings as errors, RelWithDebInfo. The build was clean: 703/703 steps, 0 warnings.
- ctest: 129/129 passed.
- Server pytest (`py -3.12 -m pytest -q -p no:cacheprovider`, in 3 parts): files before `test_surface_corridor.py` 9376 passed and 1 skipped; the rest 447 passed; `test_bot_map_matrix.py` 36 passed. No faults and no reruns were needed.

### Live test (partial; the rest is owed)

The live test was cut short because the user needed the PC. What was seen:
- A local isolated server on UDP 27090 (TDM ArcticBase, 6 bots, revival and Steam off).
- The first-run identity gate is skipped with `AOS_REVIVAL_STATE_PATH` pointing at a launcher_state.json whose account has `"offline": true`. That account restores as an offline guest: identity, then main_menu, then the `--connect HOST:PORT` join. The offline identity path uses packet 105 with an empty ticket and joins as `identity=legacy`.
- Join, team select, class select and the in-world HUD all worked: kill feed rows from bot kills, bot chat, minimap, timer, and a server GL grenade explosion.
- There was no `[ui]` decode warning in the client log and no crash dump.
- One session ended at about 45 s with no client-side error. That was most likely the user closing the window.

Not observed: kill banners or stingers for the local player, bot prefabs on screen, crater parity, packet 38 on join, the parachute or jetpack loop, flare lights, and a map rollover with the end screen. **Live test owed.** Driver: a copy of `tools/smoke-client-live-match.ps1` with the env var above and a hold loop.

### Live test (2026-09-27, full pass)

Rig: the parity-integration build against isolated servers on UDP 27090/27100 (TDM ArcticBase first, then every `configs/official-*.toml` plus Arena), 6 bots. The server script adds a localhost eval console, so kills, teleports, prefabs, blasts and crate drops were driven server-side while the client was watched. Evidence came from window captures (burst captures for motion), the client `[nav]`/`[ui]` log, the server log, and a WASAPI loopback recording of the client's audio. Each recording was matched against the shipped Oggs by normalised cross-correlation; 0.9 and above counts as a certain match. The screenshots, audio reports and per-mode notes are in the session scratchpad (`ui_modes/`, `lt/e1..e3`).

| # | Item | Result | Evidence |
|---|---|---|---|
| 1 | TDM join with bots, no crash, no decode-warning flood | ✔ | The TDM/ArcticBase join and four map rollovers (Atlantis, BlockNess, DoubleDragon, 20thCenturyTown) ran with no crash dump. The only `[ui]` lines over about 3 h: rollover `reason 18`, the kick `reason 23`, and one cosmetic `remote held-tool art unavailable ... outside the selectable catalog` per session. The source of that last one is unknown: a 0.1 s server watch never saw a tool of 65 or more. |
| 2 | Multikill, domination and revenge banners and stingers | ✔ | Three kills credited 0.7 s apart gave the DOUBLE KILL banner and a "KILL / PAYBACK / KILL / KILL" score pop. Four kills on FrostyWolf gave "YOU ARE DOMINATING FROSTYWOLF", with `ks_domination` at 0.96. As the victim: "FROSTYWOLF GOT REVENGE ON YOU" and "FROSTYWOLF IS DOMINATING YOU". Killing him back gave "YOU GOT REVENGE ON FROSTYWOLF" plus DEATH REVENGE, with `ks_revenge` at 0.92. The victim-side stingers were not detected under the music (best correlation 0.5), so the victim stinger is unconfirmed. |
| 3 | Bot prefabs (packet 30), smoke rings, no invisible walls | ✔ | A bot `prefab_fort_wall` and a `supersmallwall` built in view appeared whole in one frame with puff rings. Walking into the wall stopped the player at the voxels the server lists. Server `pos=0` corrections showed no desync. |
| 4 | Craters match the server; packet 38 on a late join | ✔ | A type-9 blast on a flat plain matches the server's ASCII depth map cell for cell. A type-7 grenade crater on Atlantis was then viewed by a second client that joined later, from the same pose. The mean absolute pixel difference between the two views was 0.04/255, and both show the ring of darkened damaged cells, so packet 38 was applied on join. |
| 5 | Parachute; jetpack exhaustion | ✔ | The parachute was equipped (Commando). An airborne SPACE press from z 140 opened it. The server shows a smooth canopy descent (vz 0.69 falling to 0.14, z monotonic) and a close on landing. Jetpack 66 was burnt to empty: it rose 113 blocks, the fuel hit 0, and the player fell back and landed. There was no snap: 0 position corrections in the server input stats, frame-to-frame phase correlation of a 9 Hz burst capture found no discontinuity, and there was no rubber-band loop. Holding SPACE afterwards only repeats ground jumps. |
| 6 | Flare lights | inconclusive | On 20thCenturyTown the server placed 524 static flares, and the client joined and rendered the map without warnings. Captures inside a flare room (218,218), `ui_modes/flares/`, show no clearly distinguishable light pool. Without an A/B capture (flares off) this can't be judged. The light kernel is also still VERIFY (P3-13). |
| 7 | Molotov fire, chemical goo, crate airdrop | ✔ | Chemical bomb (Specialist, TDM): explosion, green goo cubes over the grid that dissolve in about 8 s, a crater under the goo, and `chem_bomb_explode_001` at 0.93. Molotov (Gangster, TC): fireball, block-fire particles for about 4 s. Its fire sounds were not confirmed; the pitched variants defeat the matcher. Crate airdrop: the health crate drops under a red-and-white parachute, which is removed near the ground. |
| 8 | Rollover, end screen, final-minute music | ✔ | Final minute: the ONE MINUTE LEFT banner and the F1-F3 map vote. Voting F1 picked the map, "NEXT MAP WILL BE ..." appeared, and the recording shows `last_man_standing_003` then `game_ending_002`. End: the forced scoreboard, then the results strip ("GREEN WINS!", awards, headline), `mu_win_game` 0.97 or `mu_lose_game` 0.95, and the stats bed `secondary_menu_bed_002`. The next map then loaded and play continued, in every mode. Multi-Hill is the exception: its win sting was not heard (0.36). |
| 9 | TC join (106), radar, votekick, disconnect text | ✔ / partial | TC on CityOfChicago: the join had no decode warning, and the A-E bars, capture banners and full-map tiles all rendered. **Radar minimap reveal: not observed** (it needs a Scout radar station near an enemy; not arranged). Votekick with two clients: K opened the player list, then the reason list, then the VOTE KICK panel on both clients (F1 YES [1] / F2 NO [0]). The vote passed and the kicked client saw "You have been kicked for Griefing until the end of the current match."; reconnecting was refused with the same text. |

Bugs found and fixed (client, rebuilt `--parallel 4`):
- **The `,` (change_class) and `.` (change_team) keys did nothing in the world.** Their bindings existed and the death hint says "PRESS [,] TO CHANGE CLASS", but no handler used them. They now open SelectClass / SelectTeam from the world through the pause-menu routes (`open_selection_from_world`). ClientInMenu goes to 1 on open, and back to 0 when Back lands on the world (`leave_selection_to_world`). Verified live: `in_menu` goes True then False, and choosing a class while dead respawns you as that class.
- **A mid-round dead joiner (Arena) never respawned.** The client sent no ClientData while dead. The server treats the first ClientData as the scene-ready edge, so `connection.in_game` stayed False, the round-start CreatePlayer never arrived, and the client showed "RESPAWNING IN n" forever. Retail keeps sending ClientData on the death screen, and the server discards those frames (`record_input_frame`). The client now sends the neutral spectator sample while dead: no prediction, movement or actions. Retest: `in_game` goes True while dead, and the player respawns at the next round.

Tests after the fixes: ctest 130/131. The one failure is `aos_weapon_catalog_contract_check` ("weapon catalog is stale"). It is caused by a concurrent, uncommitted BS edit at 05:59: `shared/constants.py` STOCK RESTORE sets `DYNAMITE_STOCK`/`DYNAMITE_RESTOCK_AMOUNT` 3 → 1. The client catalog has not been regenerated for it. That is left to whoever owns the change: run `tools/generate_weapon_catalog.py`. BS was not touched in this pass, so the server suite was not rerun.

UI-only findings were recorded for the follow-up fix pass, not fixed here (scratchpad `ui_modes/observations.md`):
- The forced scoreboard covers the map-vote panel.
- The results winner is taken from team scores rather than the server's winner. This only shows on an admin force-end.
- Modern CTF and Diamond Mine have no carried-objective HUD icon.
- The Gangster class preview shows the Soldier model.
- The TC capture bar does not visibly fill while holding, and the TC minimap tiles overlap the frame.
- The Demolition end scoreboard has blank team totals.
- The Zombie class-screen framing differs, and its first SELECT click was swallowed.
- The class-list scrollbar thumb does not track, and dragging it does not scroll.
- Multi-Hill has no win sting.
- K with only bots opens a list that cannot kick anyone and shows no denial.

Environment note: a stray Windows Firewall prompt for `aos_local_server_process_tests.exe` (from another build's ctest) covered some captures. It was dismissed with Cancel.

### Still open

- The **VERIFY** list in section 3d.
- P2-11 water/sea rendering.
- P2-09 left-overs: riot-shield impacts (entity 39 hits), live sticky attach (needs the server to create entity 35), and `JP_lowthrust_lp` (cannot be reached under the glide policy).
- P1-15 turret-rocket explosion bank.
- P3-22 spectator free-fly.
- P3-19 lost ClientData button edges.
- P2-01 same-peer rollover (not done, by decision).
- P3-06, P3-07, P3-11, P3-13, P3-20, P3-23 (VERIFY).

## View models and held tools (2026-09-27)

Every tool's first-person view model and third-person held tool/arms now follow the retail `aoslib/weapons/*.py` classes and stock `character.pyd` (`Character.draw_fps` 0x1005CB20, `Character.draw` 0x1004F120, `Character.update_weapon` 0x10034FF0, decoded headless in IDA). Code: `src/world/retail_view_model.cpp`, `src/world/retail_character_pose.cpp` and their headers. Before/after offscreen captures for all 65 tools: `out/evidence/viewmodels-2026-09-27/{before,after}` (`aos_ads_sight_render_tests.exe --viewmodel N --png DIR`, Medic arms).

Checked and already at parity (no change): `view_model_size` and third-person `model_size` for all 65 tools, `sight_pos` for all 65 tools, the draw_fps arm rig (anchor, per-part offsets, `get_arms_orientation().x * rotate_arm_ratio`) and the pullout sway.

### First person (`retail_tool_hold`, `evaluate_retail_tool_animation`)

- **Holds for all tools.** One table now holds the main-character `initial_position`, `initial_orientation` and `arms_position_offset` of every class. The test `every_tool_uses_its_retail_first_person_hold` checks all 65 rows against values taken from the scripts. These tools were at the origin before and now get their retail hold: knife 1 (-0.05,0.03,-0.05), shotgun2 10, RPG2 13, drillgun 14 / UGC drillgun 47, UGC snowblower 48, sniper 18, sticky grenade 57 (all (0,0.1,0)), sniper2 19 (0,0.2,0.1), landmine 20 (0,0.18,0; yaw 70), dynamite 21 (0,0.18,0; pitch -20, yaw 200, so the hands also tilt 20°), prefab 23 (block hold), bomb 25 / diamond 26 / intel 30 (0,0.18,0), AP grenade 32 (0,0.2,0), tommy gun 35 / snub pistol 36 (0,0.12,0), UGC prefab 42 (-0.04,0,0.3; no yaw), riot stick 49 / machete 50 (hands (0,0.03,-0.05) only), medpack 51 (-0.1,-0.18,0.45), auto pistol 53, chemical bomb 54, grenade launcher 55, radar station 56 (-0.1,0.6,0.2), mine launcher 58, C4 59, assault rifle 60, LMG 61 (0,0,0.25), auto shotgun 62, and the rocket turret 16 (three parts at ((-15, 0/14/8, 10)+(0,0.18,0))*0.05 instead of stacked at the origin).
- **Animation families by class** instead of by mechanism. C4, dynamite, landmine, medpack, radar and turret now add `AnimWeaponShoot` and `AnimPlaceBlock` together, because `Weapon.use_primary` starts both. The molotov charge runs over A1645 = 3 s (it was 1 s). An idle molotov, sticky or chemical bomb no longer stays drawn back. The melee swings (pickaxe, knife, crowbar, machete, riot stick) draw the rest pose on their `start()` frame, like the shield.
- **Hands follow the tool.** `get_arms_position/orientation` read model 0, and return zero for tools with no models (MG 15, null 39).
- **DiggingTool pitch removed from first person.** `draw_fps` never reads `get_pitch()`. `RetailViewModelPose::digging_pitch_degrees` is gone, along with its three uses in nfm and the capture.

### Third person (`evaluate_retail_third_person_pose`)

- **Arm pitch** follows Character.draw (pyx 1833-1840): `pitch + shoot_pitch` when the tool is shown, **`pitch + 50`** when it is hidden (for example while sprinting; the constant is PyInt 50 at 0x10098244), then clamped to `get_arm_pitch_range()`. The test that expected the aim pitch to be kept when the tool is hidden was changed to expect the +50.
- **`shoot_pitch` = `Tool.get_pitch()`** (`retail_tool_pitch`). It is 0 for ordinary tools. It rests at -4 for every DiggingTool and for grenades. During a DiggingTool swing it follows `use_spade`'s from→to curve (the old shield-only code, now for all of them). The zombie hand holds -4.
- **Arm pitch range** (`retail_tool_arm_pitch_range`): -90..70 by default. For a DiggingTool the upper limit is 70 minus `pitch_increase` at rest (pickaxe, crowbar and UGC pickaxe 40; knife, riot stick and machete 20; spades 30) and 70 during the swing. The riot shield is -80..0. The zombie hand keeps -90..70.
- **Observer animations.** The generic branch calls `apply_transform(i)` with animations on, so remote tools now show `AnimWeaponShoot` recoil, all the melee and spade swings, and prefab placement. Block, flare, UGC and deployable placement and grenade throws stay local-only, as in retail. The minigun keeps its barrel pose, because its special branch uses only the barrel roll.
- **Remote `initial_position`:** RPG2 and UGC RPG2 lift the model 0.3. The rocket-turret parts sit at (-0.75, 0.009, 0.5).

### Still open (recovered, not implemented)

- **Zombie-class ZombiePrefabTool** (draw pyx 1910-1940): a special branch sets size 0.04 and three hard-coded parts: (-0.95,-0.2,0.65) rotated (0,0,180), (-0.72,0.5,-0.25) rotated (45,45,0), and (0.65,0.5,0.3) rotated (90,-90,·). We still use the generic three-part draw.
- **Minigun observer barrel roll** (MINIGUN branch): it rotates around a `size*(·,-6.5,27.5)` pivot. We have no remote spin.
- **Sniper zoom arm offset** (pyx 1859-1864): when `zoom and needs_zoom_arms_offset()` (sniper 18/19), the right arms get add_yaw 25 and 25, the left upper arm gets 30, the left upper x moves -0.1 and the left lower x moves +0.3. Remote zoom is not replicated.
- **MG deployment:** the arms yaw to `weapon_deployment_yaw` while deploying or deployed, and `AnimSlowPullout` (P3-23) does not move the arms, because MG `view_model` is empty.
- **Grenade cook tilt** (-4 - 90·cooked fraction) happens only between press and release. Observers never see it, so it is not modelled.
- **Block sucker** `AnimBlockSucker` jitter in first person is state-driven and random, and is still not applied.
- **VERIFY:** the nonsteam `shared/constants.py` redefines SPADE/PICKAXE/KNIFE/CROWBAR shoot intervals near line 6040 (0.4/0.4/0.25/0.6). The catalog keeps the stock values (0.8/0.6/0.5/0.5), which set the swing length.

## UI pass: in-game HUD and overlays (2026-09-27)

Checked against the stock `hud.pyd`, `gameScene.pyd` and `character.pyd`, decoded headless in IDA (evidence and scripts in the session scratchpad `ui_modes/uia`). Code: `src/frontend/game_hud.cpp`, `match_overlays.cpp`, `change_team_menu.cpp` (the result headline only) and surgical edits in `native_frontend_module.cpp`.

### Changed to match retail

- **Chat lanes.** `process_packet_chat_message` (0x1017cf90) and `process_packet_localised_message` (0x1017dda0) choose the colour. SYSTEM goes through `HUD.add_server_message` (255,100,100). BIG goes to big text. ALL is white. TEAM is `blend_color(local team colour, white, 0.4)`. A ChatMessage from player -1 on ALL/TEAM uses `add_server_message(value, True)` (150,150,255). Unknown chat types draw nothing. Before this, every LocalisedMessage lane was gold, and a ChatMessage without a sender was added with an alpha-0 colour, so it never showed. The colour tuples are cached constants in hud.pyd, and the `flag=False` default comes from its CyFunction defaults. Helpers: `localised_message_lane_color`, `chat_message_lane`.
- **Big-text queue** (`HUD.add_big_message`, `start_big_message`, `HUD.update` 0x100edd30). While a line is queued, the current line gives way after `min(duration, BIG_TEXT_MIN_DURATION=1.5)`. The next line is taken with `list.pop()`, which is the newest queued one. On overflow (`len > 5`), the newest queued row is dropped. The old code was a 4-second FIFO.
- **Background big text** (`add_big_messageBackGround`, default `BIG_TEXT_TIME`). New `set_background_big_message` / `hide_big_message_if`. The line shows when the lane is idle, and HUD.update restarts it once if the lane empties while its time is still running.
- **Death hints are big text, not extra labels.** `DEATH_CLASS_CHANGE_HINT` is posted once at death by `character.pyd` 0x100462d0 as `add_big_message(hint, respawn_time - 2.0)`. It is posted only when the team has a class choice and `respawn_time - 2 > 0`. `VIP_DEAD_CAM_INSTRUCTION` is re-posted every frame through `add_big_messageBackGround` while `never_respawn` is set (`Character.update_respawn_time`), and GameScene hides it when that stops. The invented help-font lines under the respawn label (+34 px) and at 0.85H were removed.
- **Personal score label:** `strings.SCORE + ': '` (draw_player_score), localised. It used to be the English literal "SCORE: ".
- **HeadCount is hidden for spectators.** `HUD.draw` skips `head_count.draw()` for the spectator class, just as it does the health bar, intel icon and score.
- **Scoreboard / end-screen title** (`set_mode_text` 0x1005e080 / 0x10066870). The title comes from `MODE_TITLE[current_mode]` through the string table. `InitialInfo.classic` is never read, so Classic CTF (wire mode 8) shows `CTF_TITLE`. MODE_NORMAL, 11 and unknown ids fall back to SCORES. `retail_scoreboard_mode_title` now returns the key, and the text pass localises it. The invented "Classic CTF" title was removed.
- **Result headlines** (`ViewScores/ViewGameStats.set_message`): END_OF_MAP, TEAM_DEFEAT, GAME_DRAWN, BASE_DESTROYED, ZOMBIE_WIN and SURVIVOR_WIN now come from the active language pack (`retail_match_result_message(..., localize)`), not English literals. `LEVEL` on the rank-up bar is localised too.
- **End-screen footer:** `translate_controls_in_message(strings.SHOW_SCORES)` gives "Press [TAB] to show scores" with the live `view_scores` binding. It was the literal "Press TAB to show scores".
- **Vote ballot:** `GenericVotingHUD.decode_string` ids and localised parameters now resolve through the active language (`GenericVotingModel::set_localizer`). The English table is only the fallback.
- **Timed explosive fuse number** (`Entity.create_3dText`: `Text3D('.', pos, 0.005, disable_depth_test=True)`, default white, `text3d_font` = Edo 22, `Text3DRenderer` scale `0.005*d^0.7`, range 80). It is now white Edo, sized by distance and visible through walls. Before, it was a yellow 24 px fixed label with a line-of-sight cull.

### Checked, no change needed

- **Equipped-tool tip:** `HUD.draw_equipped_tool_tip` (0x10096c00) is a stub that returns None, so retail never draws the `EQUIPPED_TOOL_TIP_*` box. It was not ported, and must not be.
- **Already at retail parity:** the crosshair (5 sprites), health bar and portrait, ammo/blocks panels, jetpack gauge, disguise/parachute icons (`draw_disgusie_hud` / `draw_parachute_hud` at w-50, 135), intel icon, TeamProgressBar, TerritoryBasesHud, HeadCount geometry, timer placement, kill feed, score-reason feed, minimap/full map, respawn label, palette, help panel and the vote ballot layout.
- **Native-only widget:** the ability key hints (`ABILITY_HINT_*`) are already behind the off-by-default `ability_hints` setting.

### Still open / VERIFY

- `HUD.draw_weapon_deployment_hud` (machine gun) is not ported. Retail shows a DEPLOYING/WITHDRAWING MACHINEGUN background big message with `BIG_TEXT_TIME_1FRAME`, plus a progress icon driven by `get_deployment_progress()`. The native MG deploy is an instant toggle with no progress timer, so this needs the weapon/view-model owner first.
- Retail may show the VIP dead-cam line for one more `BIG_TEXT_TIME` after GameScene hides it, because the background time is still live. The model reproduces this, but it has not been seen live.
- The ballot CLOSED result duration stays at 6 s. Retail may use packet-supplied seconds (not decoded).
- Other text-3D users are not drawn natively: rocket-turret ammo, radar-station lifetime, the attached sticky charge.
- The retail title `mode_text` is drawn without `.upper()`. Native still uppercases it. This is invisible with Spades (a caps-only Latin face), but it can differ for Cyrillic packs.
- No live capture was taken in this pass, because another agent's client window was open. The changes are covered by `aos_match_overlays_tests` (lanes, titles, headlines, ballot), `aos_hud_layout_tests` (big-text dwell and pop order), `aos_retail_hud_rules_tests` (background big text, death hint) and `aos_game_hud_tests` (spectator HeadCount, score label).

## UI pass: input, key names, help text, F-keys and music (UI-C, 2026-09-27)

Evidence: retail `aoslib/gui.py:250-268` (`translate_key`, `KEY_TRANSLATIONS`), `aoslib/aosKeys.py`, `aoslib/config.py:30-68`, `aoslib/text.py:649-661`, `hud/helpPanel.py`, `scenes/ingame_menus/{selectTeam,selectClass,changeTeam,screenshotHud}.py`, `scenes/main/ugcObjectivesListPanel.py`, `shared/constants.py:4391-4401`, and headless IDA traces of `gameScene.pyd` (`on_key_press` 0x1015c9d0, `on_key_release` 0x101608c0, `take_screenshot` 0x10147780, `save_ugc_map` 0x1015b8b0, `process_packet_help_message` 0x1017d9d0, `update` 0x10149cf0) and `hud.pyd` (`HUD.on_key_press` 0x10080c70, `ViewGameStats.on_key_press` 0x100e5700, `ToolsHelpPanel.update_tool_text` 0x1003b920, `HUD.__init__`). Code: `include/battlespades/settings/retail_key_names.hpp` + `src/settings/retail_key_names.cpp` (shared key names), `include/battlespades/frontend/retail_input_rules.hpp` + `src/frontend/retail_input_rules.cpp` (pure rules), `MatchStartStingerGate` in `retail_announcer.hpp`, surgical edits in `native_frontend_module.cpp`, `game_hud.{hpp,cpp}` (placeholder resolver, `set_help_open`), `openal_frontend_audio` (`is_playing_named_music`), `bgfx_ui_renderer` (backbuffer capture) and `ugc_preview_writer` (`write_screenshot_png`). Tests: `aos_retail_input_rules_tests` (new) and `aos_game_hud_tests`.

### Changed to match retail

- **One key-name function (note for UI-B).** `settings::retail_key_name` / `retail_binding_name` (`retail_key_names.hpp`) is `translate_key`: the id comes from client_settings' `retail_key_name_id` table, is looked up in the active language pack, and falls back to the id itself (never the English humaniser). Two retail facts are applied on top of that table: right Ctrl/Shift are aliases of the left keys in retail's pyglet (`aosKeys.py` RCTRL = LCTRL_REAL), so they read CTRL/SHIFT and also *match* a Ctrl/Shift binding (`retail_key_matches`); and the grave key's id is QUOTELEFT ("QUOTE LEFT" in english.py's key block; GRAVE = "Grave" is the tombstone string). **UI-B:** `settings_menu.cpp` still formats with `retail_binding_name_id` + `localized_text`, which humanises ids missing from the pack (ESCAPE -> "Escape") and shows GRAVE/RCTRL; switching the value text to `settings::retail_binding_name(binding, lookup)` gives both screens identical text.
- **In-game key names.** `,` now reads [COMMA], `.` [PERIOD], Ctrl [CTRL], Shift [SHIFT], Escape [ESCAPE], arrows [Left]/[Right]/[UP]/[DOWN] (localised), and every other key has a name instead of a literal fallback. Used by the death hint, the vote-key captions (`keys_to_vote = translate_key(config.map_vote_N)`), help text and ability hints. The old `scancode_display_name` is gone.
- **`{key_*}` resolver:** all 20 retail controls (forward, backward, left, right, jump, crouch, change_class, view_scores, palette_up/down/left/right, weapon_custom, cancel_prefab_placement, carve_prefab, tool_help, hover, sprint, ugc_settings, menu). Like retail, any other brace text (`{0}`, unknown keys) stays literal instead of becoming "[?]". The SHOW_SCORES footer now resolves `{key_view_scores}` (it rendered "[?]" before).
- **Rebinding honoured.** Reload, Pick Colour (press and release), In-Game Menu and Aim now read their bindings (`binding_matches`); R and E no longer fire after being rebound, and Escape only opens the menu while it is the Menu key. Aim follows `GameScene.on_key_press` + the Controls row (`key_text` RMB, DOUBLE_KEY_BINDING): RMB always aims, and a key or mouse button bound to Aim aims as well. The native-only "middle mouse = pick colour" alias was removed.
- **Help text is localised.** Tutorial lessons, server HelpMessage (any id, via `get_by_id`: catalogue, then the recovered English table, then the id) and the "{key_tool_help} Close" footer go through the active language pack (`retail_help_string`, `show_retail_help`).
- **Map-creator help.** `HUD.__init__` in UGC mode: `UGC_HELP_WELCOME` (delay 0.0, override) on entering the editor. `ToolsHelpPanel.update_tool_text`: the equipped tool's `UGC_TOOL_HELP_TIPS` on each tool change, construct tool STEP1 -> STEP2 while a prefab is being controlled; tools without tips close the panel; the tips wait until the welcome is dismissed with H, and stay closed after H closed them. `GameScene.update`: one-shot `UGC_HELP_HOVER` once a CLASS_UGCBUILDER has had the jetpack active for more than 1.0 s (`fld1` constant). `UGCObjectivesListPanel`: the host sees "Press [X] to open additional map settings."
- **F-keys.** F11 is the retail `screenshot` key: on the end-of-match statistics screen (ViewGameStats is its only consumer) it saves `C:\AoS_Screenshots\<map><camera>.png` (`os.path.join(dir, map_name) + str(index) + '.png'`) and steps to the next screenshot camera. F3 with no vote on screen does nothing. Developer tools moved to Ctrl+Shift: F3 diagnostics, F10 gameplay lab, F11 UI layout editor (F4-F9 and F12 are not retail keys and are unchanged).
- **Music.** The match-start stinger and bed play only for the initial SelectTeam of a match, never for the in-game ChangeTeam (`.`); see P1-02. PlayMusic(26)/StopMusic(27) are ignored while `secondary_menu_bed_001` plays (`process_packet_play_music`/`stop_music` return when `is_playing_music(SECONDARY_MENU_MUSIC1)`), and world entry always stops that bed (retail `selectClass.py:275` stops music on the class pick).

### Checked, no change needed

- **Equipped-tool tips:** `HUD.draw_equipped_tool_tip` is an empty method (wrapper 0x10096c00 returns None), so `EQUIPPED_TOOL_TIP_*` never draws in retail. Not ported, on purpose.
- **Map key is hold, not toggle.** `Minimap.draw` shows the big map while `window.keyboard[config.show_map]` is held (hud.pyd trace), as native already does.
- **camera_pan (P)** drives a CameraManager pan controller (`controllers[A989]`, start/end locations) while held; nothing in the game sets those locations for players, so it is a capture/dev camera. Not implemented. **voice_record (B)** is scrapped (Steam voice).

### Still open / VERIFY

- **F10 quick save** (`on_key_press quick_save -> if is_ugc_host: save_ugc_map`, which writes the .ugc/.vxl/.png client-side and posts `UGC_MAP_SAVE_SUCCESSFULLY`). F10 is no longer taken by the lab, but native has no client-side UGC serializer and the protocol has no save request (the hosted Map Creator server saves on shutdown), so the key does nothing yet. Needs a server-side save request or a client VXL/sidecar writer.
- **Map-preview HUD** (`ScreenshotHud`, `SCREENSHOT_HUD_TEXT`): retail opens it from UGC settings; LMB captures a centred square of the view (scaled to 512), RMB uses the overhead map, the Menu key returns to the settings, and a 30%-height preview sits bottom-right. Native still captures the overhead map directly from the settings panel. The new backbuffer capture (`BgfxUiRenderer::request_backbuffer_capture`) is the missing building block.
- The UGC objectives list panel itself is not drawn natively, so the settings hint sits where retail puts it for an empty list.
- `hover` (Z) stays live outside the editor: the retail key handler calls `toggle_hover` without a mode check that the trace can show, and native uses Z for the parachute (decision D3). VERIFY `Character.toggle_hover`.
- The screenshot file index follows the shown StateData camera; whether retail's `current_screenshot_camera` wraps is not visible in the trace. The Map Creator category order in Settings is UI-B's.
- An Aim key is released on key-up natively; retail's `on_key_release` has no `aim` branch (VERIFY whether `set_secondary_shoot` toggles).
- The screenshot callback replaces bgfx's default stub (fatal still aborts). Not tried live: no game window was opened in this pass.

## UI pass: in-game menus (UI-B, 2026-09-27)

Evidence: retail `aoslib/scenes/ingame_menus/{selectClass,selectTeam,changeTeam}.py`, `scenes/main/gameClass.py`, `scenes/gui/{dropBoxControl,menuOptionControl}.py`, `scenes/frontend/controlsTab.py`, `scenes/main/settingsKeyControlListItem.py`, `aoslib/gui.py` (ScrollBar 1385-1541, HorizontalScrollBar 1644-1741, SquareButton, KeyControl, `translate_key`), `aoslib/image.py` (load_texture truncation and anchors), `ingame_menus/__init__.py` (`get_highlighted_team_member`), `shared/hud_constants.py`, and headless IDA traces of `hud.pyd` (KickVotePlayerSelect draw / get_hover_item / packet_received / update, HUD.on_key_press) and `gameScene.pyd` (`class_selection_has_choices` 0x101678b0, traced in this pass). Code: `class_selection_menu.{hpp,cpp}`, `class_selection_presentation.{hpp,cpp}`, `world/class_selection.{hpp,cpp}`, `tools/generate_class_catalog.py` (+ regenerated `class_catalog.generated.cpp`), new `kick_vote_menu.{hpp,cpp}`, `settings_menu.{hpp,cpp}`, `settings_presentation.cpp` (key value transform), `client_settings.{hpp,cpp}` (key-name table), `protocol168_session` (InitialInfo), `change_team_menu` (a public roster wrapper only) and surgical edits in `native_frontend_module.cpp`. Tests: new `aos_retail_menus_tests`; updated `aos_class_selection_menu_tests` and `aos_settings_menu_tests`.

### Changed to match retail

- **Class scrollbar** (the "awful and buggy" one). The track used to be the gold thumb texture stretched over 616 px, with a stretched bevelled square as the thumb (gold on gold). Now it is a black 660x22 frame at (70,249), a (73,63,7) 612x20 channel at (94,250) and a 3-slice thumb `scrollbar_left/hmid/right` at the retail anchors (caps 3.75 px, span [coord+0.625, coord+L+0.625]), with `L = floor(612*min(1,5/N))` and `bar_coord = 93 + pos*(612-L)/(N-5)` on the float position. The arrow buttons are `button_square` (+hover/press art, 1 px sink) with the 22 px glyph, tinted (86,86,86) at the ends; they fire on release and step one class. Dragging centres the thumb on the cursor (`floor(x - L/2)`), follows the float position smoothly and steps the list through retail's `set_as_int=False` mapping (one sound per step). A release on the channel jumps the thumb there. The mouse wheel stays as a native convenience (retail's HorizontalScrollBar ignores it) and steps like an arrow.
- **Absolute class keys.** Key n (0 = tenth) is class n-1 of the server list, even when that card is scrolled out of view. It shows `key_press<n>` while held and selects on release, scrolling just far enough to reveal the card. Visible cards show their absolute number (`key<abs+1>`). Before, the digits were page-relative, so after scrolling "1" selected a different class. The invented left/right arrow class cycling was removed (retail SelectClass has no arrow navigation).
- **The class data is the server's, not hand-written.** Constructs come from the generated `class_prefab_names` (server `PREFAB_LISTS` via `CLASS_ITEMS[class][CLASS_PREFABS]`). Engineer offers the stock 7 (caltrop, supertower, ultrabarrier, platform, superminibunker, superdome, fort_wall), matching the server's STOCK change. `InitialInfo.disabled_tools`, `disabled_classes` and `block_wallet_multiplier` are now decoded (they were skipped). Disabled tools vanish from the rows and from the constructs table (a disabled PREFAB_TOOL hides the whole table); disabled classes leave the team list. The flare block is the first construct whenever the server enables tool 22 (never for zombies or Deuce), and picking it sends tool 22 in the loadout. The loadout is BLOCK first, then the chosen rows, then CLASS_COMMON minus disabled tools (`set_common_loadout_items`); the invented forced PREFAB_TOOL append is gone. Default constructs follow `GameClass.get_prefabs`: the first three, and none in mafia/UGC or when fewer than three exist.
- **Names, labels and popups.** Class names use CLASS_NAMES (Commando, Marksman, Rocketeer, Engineer, "Baby-face Lou", Deuce, ...). Row labels use CLASS_ITEMS_NAME (Melee / Primary Weapons / Secondary Weapons / Equipment / Constructs). The title CHOOSE_CLASS, SELECT and BACK are string ids. The 0.5 s hover popups are drawn:
  - weapon: `item_info_frame`, the icon at 0.35x, the TOOL_NAMES title, the TOOL_DESCRIPTIONS pros and cons with the 65 px jump to the cons, and `frame_arrow`;
  - construct: `prefab_info_frame`, the name, BLOCK_USAGE and the KV6 block count (flare = 10);
  - class: `class_info_frame_left/right`, CLASS_NAMES, CLASS_DESCRIPTIONS, and the block icon with "Blocks: a / b" x block_wallet_multiplier.

  The Scout sniper icon is team coloured.
- **Screen geometry.** Black 378x48 row strips and the 126x207 constructs frame are drawn. The loadout/construct selected borders are 48x49 / 43x43 (they were 52x50 / 48x46). Construct paging arrows and the page number appear with more than 12 constructs. The in-game BACK TextButton (338,522 125x45) sits on the stretched `ugc_select_bg`. SELECT is disabled (0.7) while the team is locked_class, and Enter then does nothing.
- **Locked-class and no-choice modes.** This ports `selectTeam/changeTeam.on_select` and `class_selection_has_choices` (IDA: false in mafia mode, in UGC, or for a locked_class team). VIP, Classic CTF, Territory Control (mafia) and Zombie spawn straight after the team pick, with the single class or a random mafia/locked pick (`automatic_class_selection` = `GameClass.build_class_loadout`). `,` does nothing there; on a locked Zombie-mode team it shows the ZOMBIE_OUTBREAK_CLASS_SELECT big message, and a spectator's `,` opens ChangeTeam. The ForceTeamJoin path follows the same rule.
- **The same key closes.** In-game SelectClass closes on `,` (change_class) or the menu key, and ChangeTeam on `.` or the menu key, straight to GameScene with menu_backA (selectClass.py:357-363, changeTeam.py:113-119), even when they were reached through ChangeTeam or the pause menu. BACK still returns to the previous menu.
- **Vote kick = KickVotePlayerSelect** (IDA hud.pyd):
  - `change_team_content_frames` at (400,300) and the title SELECT_PLAYER_TO_KICK at y 489;
  - both `draw_player_list` rosters (77/412, list y 443, 313x263, score x 385/415), with spectators in the spare rows (the ViewScores rule);
  - `hover_scoreboard_*` / `highlight_scoreboard_*` art by the row's team;
  - the reason DropBoxControl (84,89 200x30, down-arrow button, 20 px rows opening below);
  - the KICK PLAYER TextButton (334,123 130x39), disabled until a row is picked;
  - K or the menu key closes it, and the mouse is released while it is open.

  There are no client-side denials: the server's KICK_DENIED_* LocalisedMessage shows in its normal lane and closes the menu 0.5 s later (`TIME_TO_SHOW_MENU_IF_CANCELLED`). Opening is refused only in the tutorial or without a player (HUD.on_key_press). Removed: the text list, the "n." prefixes, the digit/arrow/Enter keys, the reason stage, the local 60 s cooldown and the gold chat denials.
- **Settings > Controls values.** The KeyControl text is `translate_key`: COMMA, PERIOD, CTRL, SHIFT, Left/Right/UP/DOWN, ESCAPE, RETURN, F1-F24, every letter, digit and keypad key, MWheel for the wheel row, and None (strings.NONE) when unbound, drawn without upper-casing. The value uses UI-C's shared `settings::retail_binding_name` (RCTRL/RSHIFT alias, QUOTELEFT) resolved against the language pack and submitted verbatim (`LITERAL|` prefix), so an id a pack lacks is never humanised. "SCANCODE N", "UNBOUND", "LEFT CTRL" and "MOUSE WHEEL" are gone. The conflict text is ERROR_CONTROL_ALREADY_BOUND (A959) "{key} is already bound to {row}" (INVENTORY_SLOTS for the digit keys), localised, instead of the English literal.
- **Map vote.** Retail has no map-vote screen. The map vote is the GenericVotingHUD ballot over GameScene/ViewScores/ViewGameStats (F1-F3). Its text localisation landed in UI-A's pass and F3-without-ballot in UI-C's; nothing invented remains on the native side.

### Checked, no change needed

- **Controls category order.** The retail 32-bit Python 2.7 runtime iterates `keys_info` as [Map Creator Controls, Main Game Controls] (evaluated with the shipped `python.exe`), so the native order (Map Creator first) is right for English. Other languages hash their localised category names differently; that is not reproduced.
- The card geometry, the +0.22 hover scale, the class selected frame, the portrait, the SELECT position and the frontend navbar already matched.

### Still open / VERIFY

- `class_has_loadout_choices` is compiled. The native rule counts a class as having choices when any loadout row has more than one enabled item; constructs are not counted.
- Popup text layout: the renderer wraps the descriptions, so the class popup's block row uses an estimated line count, and the pros/cons line advance is 16 px (STANDARD 11 line height assumed to be 13). The construct block cost is the KV6 voxel count (prefab_manager VERIFY). The kick dropdown's row font and highlight and its open/close/kick cues use the generic menu cues (the hud.pyd cue names were not recovered).
- Not reproduced: retail's 15 px key-display shift after a page change (a retail bug), the map prefabs (MAP_PREFABS) in the constructs table, per-class loadouts persisted across sessions (retail saves them in config), and DLC greying (Specialist/Medic and 13 DLC tools; the revival has no DLC).
- Not captured live: no game window was opened in this pass. The visuals are covered by the draw-list tests only.


## Final integration 2026-09-27

One pass after every 2026-09-27 client wave (waves 1-7, integration, live-test fixes, riot shield, view models, graphics, UI-A/B/C) landed in the main checkout. Nothing was committed. Evidence (screenshots, client logs, loopback recordings) is in the session scratchpad `final/ev/<session>/`.

### Audit of claimed changes

UI-B's two whole-file rewrites (`native_frontend_module.cpp`, `settings_menu.cpp`) did not erase anything. Every hook listed in the sections above was found by symbol:
- **UI-A:** `localised_message_lane_color`, `chat_message_lane`, `set_background_big_message`, `hide_big_message_if`, `show_death_class_change_hint`, the spectator HeadCount gate (`player_widgets_visible`), the MODE_TITLE title (`retail_scoreboard_mode_title`), the SHOW_SCORES footer, `GenericVotingModel::set_localizer` and the white Edo fuse text (`create_3dText`).
- **UI-C:** `retail_key_names`, `retail_input_rules`, the rebinding of reload, pick colour, menu and aim (`binding_matches`), `show_retail_help`, the 20 `{key_*}` placeholders, `sync_ugc_editor_help` / `toggle_tool_help`, the Ctrl+Shift developer keys, F11 on the stats screen (`request_backbuffer_capture`), `MatchStartStingerGate` and the `is_playing_named_music` guard.
- **UI-B:** `kick_vote_menu.*`, the class scrollbar, absolute keys, popups, server class data, `class_selection_has_choices` / `automatic_class_selection`, same-key close (`close_selection_menu_to_world`), `settings::retail_binding_name` in Settings, and the A959 conflict text.
- **View models:** `retail_tool_hold`, `retail_tool_pitch`, `retail_tool_arm_pitch_range`, the +50 hidden-tool pitch. `digging_pitch_degrees` is gone.
- **Graphics:** AO atlas `bottom_row_first`, the recomputed light byte in `chunk_mesher` (`mark_voxel` +10 rows), bed water colour, the `draw_sea` quad (Retail tier), skydome -35 under Legacy/Retail.
- **Riot shield and live-test fixes:** `arm_model_scale` and the tool-52 hold; `open_selection_from_world` / `leave_selection_to_world`; the neutral ClientData sample while dead.
- **Earlier waves:** `announce_kill_action`, packet 38, the prefab-30 branch, POIFocus, the radar clause, the type-13 flare path, goo bit 0x08 and `bomb_smoke_clock`.

`,` / `.` and UI-B's same-key close work together. In the world, `,` opens SelectClass and `.` opens ChangeTeam, and the same key closes them to the world. The close path ignores key repeats and the initial-join menus (`in_game_selection_menu`). Checked live on TDM: `[nav]` class_selection, then game_hud, then change_team, then game_hud, with ClientInMenu going 1 and back to 0.

The live-test "localhost eval console" exists only in the scratchpad (`lt/srv.py`). Nothing like it is in either repo (`eval(compile` / `exec(compile` have no hits in BS).

### Catalogs

- `tools/generate_weapon_catalog.py` was rerun against the BS working tree. Only `DYNAMITE_STOCK` / `DYNAMITE_RESTOCK_AMOUNT` changed (3 → 1), plus the contract hash. `aos_weapon_catalog_contract_check` passes.
- The class catalog is current: its check passes, and Engineer offers the 7 stock constructs, shown live in class select.
- The entity catalog check passes (run by hand with the Python 2.7 retail dumper).

### Fixes in this pass

| Finding | Retail evidence | Fix |
|---|---|---|
| The forced end scoreboard covered the map-vote panel. | ViewScores.draw_hud draws chat plus `generic_voting.draw` over the frame. | Root cause: the UI renderer sent every window-pixel sprite to view 25, which sits under the design canvas (26) no matter the draw-list order. A window-pixel sprite queued after canvas content now goes to a new overlay view 27 (`ui_window_overlay_view_id`, `bgfx_ui_renderer.cpp`), so draw-list order holds. Live: the VOTE MAP ballot draws over the TDM scoreboard. |
| The results winner came from team scores, not the server's winner. | `set_message` id 1 computes the winner from scores (hud.pyd 0x1006ED20), and 6/7 name team1/team2. The client was already faithful. | Server side: `BaseMode.end_message_id` sends 6/7 when the winner is not the score leader. Live: `_end_by_score(Green)` with Blue ahead 8-5 now reads "GREEN WINS!". |
| Modern CTF and Diamond Mine had no carried objective. | Live against the retail client: on pickup it equips INTEL_TOOL 30 or DIAMOND_TOOL 26, shows only the blocks panel, and number keys and the wheel cannot switch away (verified in CTF and Diamond Mine). No HUD icon appears unless `can_shoot_holding_intel` (Classic CTF). | `PlayerInventory::set_carried_pickup(pickup, equip)` selects and locks the pickup tool. `TutorialWorldSession` passes `equip` except for the intel when InitialInfo allows shooting. Live: the native carrier holds the briefcase or diamond and key 2 does nothing, matching retail. Test: `aos_player_inventory_tests`. |
| The Gangster class preview showed the Soldier model. | Retail `GlobalImages.class_images` reuses the soldier portrait for all Gangster and VIP bodies (class_select_spec). The mafia modes also skip SelectClass (`class_selection_has_choices` is false). | No change; this is retail. |
| The TC capture bar did not fill. | TerritoryBasesHud stretches the attacker plate to `capture_amount/100`. | Server side: packet 106 now carries a 0..100 attacker percentage (`_wire_capture`); it used to send the internal 0..1 position. Live next to the retail client: the C bar fills in both. |
| TC `[ui] malformed TerritoryBaseState(106)` | Retail `TerritoryBaseState.read` uses the short base+action form for actions 3, 4, 6 and 7. | The native decoder rejected the short form, so the ENTERING/LEAVING player scale and the contested pulse were never applied. It now decodes it (capture 0.5). Test: `aos_protocol168_runtime_tests`. |
| TC minimap tiles overlapped the frame. | Retail captures show zone icons (TC letters, Diamond drop-off) spilling over the minimap frame. | Retail does this too. The native icons are no longer clipped to the map rect, and an icon is drawn while any part of it reaches the view (`game_hud.cpp`). The hud-layout test was updated. |
| The Demolition end scoreboard had blank team totals. | `draw_player_list` draws the team total only when `team.show_score` (`aoslib/scenes/ingame_menus/__init__.py`), and Demolition sends show_score False. | No change; this is retail. |
| Zombie class screen framing and a swallowed first SELECT | Old captures predate UI-B. | Rechecked live: Zombie uses the same frontend frame as TDM, and one SELECT click spawns. Also fixed: the SelectTeam button read "JOIN SURVIVOR_TEAM". `team_roster_state` now resolves string-id team names through the language pack, as `strings.get_by_id` does. |
| The class-list scrollbar thumb did not move or drag. | — | Rechecked live after UI-B's rewrite: the arrow moves the thumb and a drag scrolls back. No change needed. |
| K with only bots gave no denial. | The server answers with KICK_DENIED_* LocalisedMessages. | Server side: bots no longer count toward the 3-player team minimum. Live: picking a bot shows "There must be at least 3 players on a team to initiate a kick" and the menu closes. |
| Multi-Hill had no win sting. | — | Not reproduced. `mu_win_game` was detected at 0.96 on a forced MH end. A failed sting now logs `result sting ... could not play`. |
| `[ui] remote held-tool art unavailable ... outside the selectable catalog` | The server writes tool 0xFF in some remote rows, and retail rejects ids outside 0..64 and keeps the held tool. | `Protocol168Roster::update_world_state` now keeps the previous tool for ids ≥ 65 (test: `aos_protocol168_players_tests`). The log line now names the player and tool. No longer seen in a Zombie infection round. |

### Build and tests

- `out/build/parity-integration` was wiped and rebuilt fresh: preset native-dev, VCPKG_ROOT=C:\vcpkg, `out/vcpkg_installed`, `--parallel 4`, warnings as errors, RelWithDebInfo. It built 712/712 with 0 warnings, including `aos_retail_scene_capture`. The incremental rebuilds after each fix were also clean.
- ctest: **131/131** after the last change.
- Server pytest (`py -3.12 -m pytest -q -p no:cacheprovider`, in 3 parts): files before `test_surface_corridor.py` 9399 passed and 1 skipped; the rest 458 passed; `test_bot_map_matrix.py` 36 passed. No access violations and no reruns.

### Live verification (native client, parity-integration build)

| Item | Result | Evidence |
|---|---|---|
| TDM HUD, scoreboard, `,`/`.` open and close, death hint | ✔ | `ev/tdm` 04-08, 18. "PRESS [COMMA] TO CHANGE CLASS" shows as big text. |
| Class select: scrollbar arrow and drag, Engineer 7 constructs | ✔ | `ev/tdm` 02, 03-class-* |
| Vote kick screen; bot-only denial | ✔ | `ev/tdm` 09-12 |
| Settings > Controls key names (PERIOD, COMMA, ESCAPE, CTRL, SHIFT, Left/Right/UP/DOWN) | ✔ | `ev/tdm` 15-17 |
| End screen: ballot over scoreboard, server winner, rollover | ✔ | `ev/tdm` 20-end-*, 21-after |
| CTF / Diamond carry vs retail | ✔ (fixed) | `ev/ctf` r02/r03 (retail), `ev/ctf2` 01, `ev/dia` 01 / r01 |
| TC capture bar and minimap vs retail | ✔ (fixed) | `ev/tc` 03 / r03 |
| VIP HUD (V.I.P label, 0/3, no class select) | ✔ | `ev/vip` 02 |
| Zombie team and class flow, infection round | ✔ | `ev/zom`, `ev/zom2` |
| Multi-Hill end and win sting | ✔ | `ev/mh` 20-end-*; `mu_win_game` 0.96 |
| Radar minimap reveal (friendly station, enemy 30 blocks away) | ✔ | `ev/radar` 01-no-radar-map (no enemy) / 02-radar-map (green marker) |
| Victim-side domination / revenge | ✔ / partial | "YOU GOT REVENGE ON FROSTYWOLF" banner. `ks_revenge` 0.92, and the victim `ks_domination` 0.62 under the music bed. The "IS DOMINATING YOU" banner is queued behind the death hint in the 1.2 s capture. |
| Molotov block-fire sound | ✔ | `molotov_blocks_loop` 0.77 on a server-lit fire (`ev/mol`) |
| Flare lights (20thCenturyTown flare room, retail next to native) | **gap** | `ev/flare` 01/r01, 02/r02. Retail shows warm radial light pools near the flare cells, and the native Retail tier shows none: `u_emissiveParams.y` is 0 below Low, so the `v_placed` static-light term is off. Retail `map_vert/map_frag` (decompiled from `aoslib/shader_source`) has no point light, so the retail pool is baked into the VXL vertex colour and `gl_Vertex.w` by `vxl.pyd add_static_light`. The kernel is still unrecovered (P3-13), and a retail VBO dump found no chunk buffers on this map. Not changed, to avoid an invented kernel. |

### Still open

- **P3-13 flare light in the Retail tier** (above): recover `vxl.pyd add_static_light` / `update_static_light_colour` (headless IDA), then bake the light into the retail vertex colour and w.
- A server quirk: some remote WorldUpdate rows carry tool 0xFF (bots around death, spawn and infection). The client now ignores them as retail does; the server source was not chased.
- The in-game ChangeTeam/SelectTeam "JOIN {team}" label is still the English "JOIN " prefix, not `JOIN_TEAM` from the pack.
- Stray untracked files from earlier agents were left alone: `BattleSpadesClient/x.png` (a sky capture) and `BattleSpades/p.out` (a cProfile dump).

## First-person muzzle flash (2026-09-27)

Bug report: "minigun muzzle flash happens in front of my eyes, a problem on most guns". Cause: Wave 7 drew the local flash in world space at eye + forward·0.72, centred on the crosshair at 0.065·scale, under the view model. Depending on the gun it covered 7% (pistol) to 61% (shotguns) of the screen, and 32-100% while aimed.

### Recovered retail chain

- `aoslib/weapons/weapon.py`: `shot_weapon` sets `muzzle_flash_timer = muzzle_flash_duration` and `muzzle_flash_rotation = random()*360`. `update` counts it down. `draw_fps` calls `draw_muzzle(view_weapon, muzzle_flash_view_offset)` and `draw_sight` calls it with `muzzle_flash_zoomed_view_offset`. `draw_muzzle` does `glTranslatef(offset)`, then `glRotatef(roll, 0,0,1)`, then `PASSTHROUGH_SHADER` around `muzzle_flash_view_display.draw()` with `size = view_weapon.size·muzzle_flash_scale` and `matrix = view_weapon.matrix`.
- `character.pyd Character.draw_fps` (0x1005CB20, seq dump): the order is `glPushMatrix`, `glLoadIdentity`, `glRotatef(180,0,1,0)`, `glTranslatef(sx-0.4, sy-0.55, sz+0.9)` (0x1005EA1C), the arms block, then each tool part in its own push/`apply_transform`/`view_weapon.draw_scaled`/pop. Then `weapon_object.draw_fps(view_weapon)` (0x10062131) runs before the outer `glPopMatrix` (0x1006222D). So the flash rides the sway/bob/pullout root but not `Tool.apply_transform`: the class hold (`initial_position`) and recoil never move it. `draw_fps` also clears depth first (0x1005D4DE), so the flash is depth-tested only against the view model. `Character.draw_sight`: identity, then `R_y(180)`, then the sight, then `weapon_object.draw_sight(view_weapon)` (character.pyx:2092).
- `draw.pyd DisplayList.draw` (0x1000E460, headless IDA this pass): `glPushMatrix`, `glTranslatef(x,-z,y)`, `glScalef(size)`, `gl.multiply_matrix(matrix)`, polygon offset (`z_offset`), `glTranslatef(offset_x,-offset_z,offset_y)`, `glRotatef(extra_yaw,0,1,0)`, `glRotatef(extra_roll,0,0,1)`, `handle.draw()`, `glPopMatrix`. `DisplayList.draw_scaled` is only `glScalef(size)` plus `handle.draw()`. `__init__` sets `matrix` to identity, and nothing sets `view_weapon.matrix`. The view display's x/y/z/offset stay 0.
- Net transform (row-vector): `S(view_model_size·muzzle_flash_scale)·R_z(roll)·T(pos)·R_y(180)`, where `pos` is `(-0.4,-0.55,0.9) + sway + muzzle_flash_view_offset` at the hip and `muzzle_flash_zoomed_view_offset` when aimed. Code: `retail_view_muzzle_flash` / `evaluate_retail_view_muzzle_flash` in `src/world/retail_view_model.cpp`.
- GL_CULL_FACE is on in retail (`laserAttachment.py:196/227` turns it off and back on). The flash is drawn with `BGFX_STATE_CULL_CW`. Outside views are byte-identical with and without the cull (checked). It matters for Sniper and Sniper 2: they never override the zoomed offset, so the aimed flash sits at (0,0,0), the eye is inside the KV6, and every face is a back face. Unculled, that one aimed shot painted 89-100% of the scope. Culled, it is invisible, as in retail.

### Per weapon (hip offset / aimed offset / scale / duration; view_model_size)

| Tool | Hip `muzzle_flash_view_offset` | Aimed offset | Scale | Duration | Before → after (flash px at 1280×720, hip / aimed) |
|---|---|---|---|---|---|
| 7 SMG | (0,0.12,0.5) | (0,-0.1,3) | 0.5 | 0.05 | 61080 centred → 7397 at the muzzle / 296252 → 5983 |
| 8 Minigun | (0,-0.18,2.0) | never aims | 0.75 | 0.05 | 218546 centred → 3435 at the barrel tip / – |
| 9 Shotgun, 10 Shotgun 2, 37 Classic shotgun | (-0.05,0.12,0.8) | (0,-0.1,3) | 1.0 | 0.05 | 562242 → 22699 / 921600 (whole screen) → 26807 |
| 15 MG (undeployed hip) | (0,0.18,2.0) | no sight (draw_sight returns) | 0.75 | 0.01 | 218546 → 3203 / – |
| 17 Pistol | (-0.05,0.12,0.35) | (0,-0.1,1.5) | 0.5 | 0.05 | 61080 → 9870 / 296252 → 27126 |
| 18 Sniper, 19 Sniper 2 | (0,0.12,0.9) | (0,0,0), culled | 0.5 | 0.05 | 61080 → 4037 / 735110 and 436868 → 0 |
| 35 Tommy gun | (0,0.35,1.2) | (0,-0.1,3) | 0.5 | 0.01 | 61080 → 2686 / 296252 → 5983 |
| 36 Snub pistol | (0,0.34,0.5) | (0,-0.2,2.5) | 0.5 | 0.05 | 61080 → 6847 / 296252 → 8989 |
| 38 Classic SMG | (0,0.12,0.5) | (0,-0.1,3) | 0.5 | 0.01 | same as SMG |
| 53 Auto pistol, 60 Assault rifle | (0,0.12,0.5) | (0,-0.1,3) | 0.5 (size 0.035) | 0.05 | 61080 → 3394 / 296252 → 2839 |
| 61 LMG | (0,0.12,0.5) | (0,-0.1,3) | 0.5 (size 0.055) | 0.05 | 61080 → 9165 / 296252 → 7314 |
| 62 Auto shotgun | (-0.05,0.12,0.8) | (0,-0.1,3) | 0.6 | 0.05 | 104616 → 6941 / 460140 → 8802 |
| 6 Classic rifle | none (only a third-person `muzzle_flash_display`) | – | – | – | 61080 → no first-person flash |
| RPG/RPG2/grenade and mine launchers, drill, snowblower, throwables, melee, deployables | none | – | – | – | no flash (unchanged) |

After the fix, every hip flash lands right of and below the crosshair at the barrel (centroid x +5 to +15 %, y -7 to -25 % of the frame). Aimed flashes are small and just under the sight. Evidence: `out/evidence/muzzleflash-2026-09-27/{before,after}/toolN_flash_{before,after}_{hip,ads}.png` plus `measurements.txt` (`aos_ads_sight_render_tests --viewmodel N --png DIR`). Before = the old world-space transform replayed under the view model.

### Code and tests

- `src/world/retail_view_model.cpp` + header: `retail_view_muzzle_flash`, `evaluate_retail_view_muzzle_flash`.
- `render::ViewModelDraw::unlit` (fs_world lighting mode 0 plus CW cull) and `WorldRenderer::view_model_mesh_resident`.
- nfm: `arm_local_view_muzzle_flash` (every local hitscan, tutorial included), `append_local_view_muzzle_flash` (last viewmodel slot 63, lazily uploaded). It is appended in both branches of `sandbox_view_model_draws`, the tutorial pistol, and scripted skins without their own flash sprites. The world-space local `push_muzzle_flash` is removed. The timer counts down in `advance_retail_status_effects`.
- Tests: `aos_retail_view_model_tests` checks every weapon's offsets, zoomed offsets, scale, duration and view size, that the hip flash is off-axis, that the aimed flash ignores sway, and that 15 non-flash tools have no flash. 16 new ctest `aos_view_muzzle_flash_capture_<tool>` GPU captures assert that the hip flash is at the muzzle (off-axis, under 5% of the frame) and that the aimed flash covers under 10%.

### Still open / VERIFY

- The deployed MG's flash (`MGWeapon.draw_manned`, `muzzle_flash_deployed_offset` (0,2.4,0) in world space) is not ported. Stock `MGWeapon` has empty `model`/`view_model`/`entity_model` lists, so its `__init__` would IndexError on `entity_display[0]`, and the deployed path is unreachable with stock data.
- The enhanced-tier dynamic light for local shots (`terrain_effects.spawn_weapon_flash`) still sits at eye + forward·0.72. It is an illumination-only extra with no retail equivalent.
- No retail side-by-side capture yet (no game windows during this pass). The offsets are straight from the class attributes. Weapons with a class hold (for example the assault rifle's `initial_position` (0,0.2,0) and the auto pistol's (-0.15,-0.05,0.2)) show the flash slightly off the barrel line, because retail's flash ignores `Tool.apply_transform`. That is retail behaviour, not a port error.

## Round 2: frontend (2026-09-27)

Source: the round-2 out-of-match frontend audit (26 ranked items). Built in `out/build/fix-frontend`; the full ctest there passes except `aos_class_catalog_tests`, which fails on another agent's in-progress hip-seam work and touches no frontend code.

| # | Item | Status | Where |
|---|---|---|---|
| 1 | Browser 10 s auto-refresh wiped rows, selection, preview and scroll | Fixed. The auto-refresh is gone; the list refreshes on entry, source/region change and Refresh, as in `serverMenu.py` | nfm `pump_server_browser_refresh` |
| 2 | Volume arrows gave about 0 or 1 | Fixed. The arrows step ±0.2 on release without rounding, grey out at 0 and 1, and values under 0.01 snap to 0. Clicks and drags map only the bar between the arrows | `settings_menu.cpp` `range_bar_geometry`, `step_volume` |
| 3 | Sensitivity mapped over the whole control | Fixed. The track runs from x+8 and is w−w/6−20 wide, with ±4 px of slack. The right sixth is the edit box | `slider_geometry` |
| 4 | In-game Favourite was dead | Fixed. The row is enabled on non-custom live servers, labelled with the InitialInfo server name and checked from the favourites. Done adds or removes the server through `FavoriteServerStore`. Custom means a server this client hosts, a Steam relay, or the Map Creator | nfm `live_favourite`, `apply_live_favourite` |
| 5 | Main-tab Defaults reset everything | Fixed. It resets only master volume, music volume, fullscreen and invert mouse. Language, audio device and skins are kept | `settings_session.cpp` |
| 6 | Map Creator host Esc menu | Fixed in the UI. SAVE and QUIT replace DISCONNECT, the menu uses `pause_menu_frame_expanded`, and the MessageBox flow matches retail: Save → saved (OK resumes) or error (Retry/Cancel both resume, as retail's MESSAGE_ON_SAVE does); Quit → "Save before quitting?" Yes (save, then OK disconnects) / No (disconnects). **TODO/VERIFY:** the client save writes only the overhead preview png. The VXL and the .ugc sidecar belong to the dedicated editor server, which checkpoints the sidecar every second and serialises the VXL when the session stops. No mid-session VXL flush request exists yet (see `save_ugc_map`) | `pause_menu.*`, nfm `consume_pause_message_button` |
| 7 | Loader MAP tab had no map preview | Fixed. `minimap_bg` is drawn 258×258 at (459,184) bottom-origin, with the browser's map preview inside at 238×238. The UGC png preview is not drawn yet; retail's 90° tex-coord rotation is not applied, to match the native browser | `loading_presentation.cpp` |
| 8 | CUSTOM GAME RULES panel | Added. The rules are grouped by GAME_RULES_NAMES category; mode buckets use their title and the rest are upper-cased ids. The panel sits at (83,384), 366 wide, up to 200 tall, on the MAP tab only, never in the tutorial or UGC. **Note:** Protocol 168 InitialInfo carries no rules (the BattleSpades `packet.pyx` writer and the native decoder agree), so the rows come from the Match Lobby that launched the match (its `rule_overrides`) | `MatchLoadingModel::set_custom_game_rules`, nfm `custom_match_rules` |
| 9 | English status literals | Fixed. `loading_status_text` localises CONNECTING_TO_SERVER, RECEIVING_SERVER_PACKS ("Connected, receiving server packs..."), the five map stages with `{0}` = map name, and ERROR_TIMEOUT | `loading_presentation.cpp` |
| 10 | Chat capped at 90 | Fixed. The cap is MAX_CHAT_MESSAGE_LENGTH (200) characters, still bounded by the 200-byte ChatMessage encoder. Feed lines wrap at MAX_CHAT_SIZE (90), breaking at the last space, with sender/body colours kept per line | `match_overlays.*` |
| 11 | Toggle rows flipped on any click | Fixed. A click sets the half that was clicked, and the unselected half takes TOGGLE_OPTION_HOVERED_COLOUR on hover. Favourite stays a checkbox | `toggle_half_at`, `settings_presentation.cpp` |
| 12 | In-game Done/Cancel/Menu all went to the Esc menu | Fixed. Done goes to the game. Cancel goes silently to the Esc menu. The Menu key (Esc) restores, plays menu_backA and goes to the game. In the frontend, Esc is Cancel | `activate_menu_key`, `SettingsCloseCommand::return_to_game`, nfm `close_settings(to_game)` |
| 13 | Sensitivity box | Fixed. It is typeable (digits and one '.'); Enter or a click elsewhere commits, clamped to 0..1 and rounded to 2 places. It shows `str(round(v,2))` ("0.1") | `text_input`/`commit_text_edit`, nfm text-input routing |
| 14 | 30 s timeout stayed on the loader | Fixed. It pops back to the previous menu (the route exit hook disconnects) and shows the localised ERROR_TIMEOUT there: the direct-connect error, the browser status, or else the settings warning | nfm `leave_timed_out_loading` |
| 15 | Tagline | Fixed. Hiesville_TagLine and Trenches_TagLine are drawn in Spades 20 at (83,372), and the title drops from 48 to 38 | |
| 16 | Tab cycling | Fixed. The 3 s cycle continues after the map is ready, a ready loader never times out, and any press or key stops the cycle | `MatchLoadingModel::tick`, `interrupt_tab_cycle` |
| 17 | Browser status | Fixed. RECEIVED_N_SERVERS counts every row before filtering, and an empty list no longer falls back to NO_SERVERS_FOUND. Retail's SERVER_LIST_ERROR id never existed in its string table, so a failed list shows the localised SERVER_SEARCH_FAILED and the transport reason goes to stderr | nfm |
| 18 | Region | Fixed. It defaults to US West and is saved on every tab click (`server_region.txt` beside the settings) | `join_match_menu.hpp`, nfm `save_server_region` |
| 19 | MODE sort | Fixed. It sorts on the localised title through `ServerBrowserModel::set_mode_title_lookup` | |
| 20 | Direct connect | The placeholder is the catalogue's localised "IP:PORT OR HOSTNAME" (hostnames are kept as an improvement), and the invented English example line is removed | `join_match_presentation.cpp` |
| 21 | Quick Play START | Fixed. It is enabled as soon as the selected row has a chosen server, even while the search runs | `quick_play_menu.cpp` |
| 22 | Esc-menu text | Fixed. Labels are catalogue ids (PAUSE, RESUME, CHANGE_CLASS, CHANGE_TEAM, SETTINGS, DISCONNECT, SAVE, QUIT) in Spades 36 as written. A held button sinks its text 2 source px, and disabled buttons dim only the art | `pause_menu.cpp` |
| 23 | Chat input | Fixed. Characters no loaded chat face covers are dropped (all characters are accepted when no face is loaded). Delete acts like Backspace. The label is `'%s:' % TEAM_CHAT/GLOBAL_CHAT` in the active language | nfm `chat_glyph_supported`, `GameChatPresentation::set_channel_labels` |
| 24 | Options polish | The restart notice is the localised CHANGE_MSAA_SETTINGS: big text for 5 s in a match, the settings warning in the frontend. It still fires only for API/texture/model, because AA and the shader tier apply live (a kept improvement). The resolution countdown truncates (15, then 14..0). Choice rows react only to their arrows. The Graphics Defaults resolution (retail: smallest mode) is unchanged | `resolution_confirmation.cpp` |
| 25 | Misc | The tutorial loader passes TUTORIAL_MODE_TITLE, and the Training title draws that key. Disconnect now calls `audio->stop_all()` before the back cue, like `media.stop_sounds()`. The teardown already stopped loops and ambience, but world one-shots used to ring on over the menu. **VERIFY (unchanged):** retail shows disconnect errors as big text over the menu; native keeps them on the loader strip | nfm `disconnect_from_pause_menu` |
| 26 | Your call | Kept as is: FRIENDS stays on the main-menu square, and no profanity filter | |

Tests: `aos_settings_menu_tests` (range-bar arrows and bar mapping, the sensitivity track and typed box, toggle halves, choice arrows, in-game Done/Cancel/Menu), `aos_settings_tests` (Main Defaults keeps native options), `aos_pause_menu_tests` (catalogue labels, font, sink, host SAVE/QUIT, big frame, message box), `aos_loading_presentation_tests` (localised status with `{0}`, timeout, preview frame, tagline, rules grouping, tutorial title, cycling after ready), `aos_match_overlays_tests` (200-character input, byte ceiling, glyph filter, 90-character wrap, localised label), `aos_join_match_frontend_tests` (US West default, MODE sort by title), `aos_resolution_confirmation_tests` (14..0), plus updated `aos_quick_play_frontend_tests` and `aos_frontend_controller_tests`.

## Round 2: players & entities (2026-09-27)

Source: the round-2 third-person/entity audit (E1-E14, headless IDA of stock `character.pyd`, `gameScene.pyd`, `kv6.pyd`, plus the `aoslib.*` pyc bytecode). Built in `out/build/fix-entities`. Offscreen before/after renders are in `out/evidence/entities-2026-09-27/` (`aos_character_scene_capture mode=before|after view=back|front`; the "before" mode reproduces the old presentation on the same meshes). No retail capture was taken: the retail client window could not be launched during the session, so "retail" in the renders means the recovered Character.draw constants on the retail KV6s.

| # | Item | Status | Where |
|---|---|---|---|
| E1 | Living players never showed their jetpack | Fixed. Every living wearer of pack 66..69 draws `JETPACK_MODELS[id]` (z_offset 6, y -0.6, z 0.8, size 0.075) with the corpse-branch child transform, standing and crouched, in the current default colour (flashed with the body; other_color right after a back intel, as the retail draw order does). The mesh lives in a new shared **character accessory** slot band (1984..2239, keyed by model+colour, LRU) instead of a per-rig slot, so the 12-slot rig stride and the entity band are unchanged | nfm `tutorial_player_draws`, `character_accessory_slot`, `character_attachment_mesh`; `world_renderer.hpp` |
| E2 | Team colour at full intensity plus an invented 0.90 gain / 1.08 contrast | Fixed. `retail_character_color` = team × 0.5 (Character.set_team pyx 667-672) for bodies, `use_team_color` tools, packs, the classic corpse and the back intel; kv6's ×1.0/×0.7/×1.3 bands apply to that. The albedo fudge is removed. Entities keep the full colour | `jetpack_death.cpp`, nfm `sync_remote_player_rig` |
| E3 | Classic CTF carried intel not drawn | Fixed. With InitialInfo `allow_shooting_holding_intel` and `pickup_id == 16`, `intel.kv6` (size 0.1, z_offset 6) is drawn on the back in the other team's half colour. set_crouch offsets: standing z 0.9, y -0.38 / -1.1 (pack 66) / -1.0 (67-69); crouched y -0.65 z 0.5 without a pack, y -1.15 **z 1.0** with any pack (the audit table said z 0.5; every pack branch loads `fld1`) | `retail_back_intel_attachment` |
| E4 | Classic corpses vanished | Fixed. In `classic` matches a dead player without a pack draws `ClassicCorpse.kv6` (size 0.05, z 2.0, team half colour) under the yaw root until ExplodeCorpse marks the life exploded. **VERIFY:** whether a retail classic server ever sends ExplodeCorpse for a non-pack corpse (explode_corpse pyx 1563 tests `manager.classic`) | nfm `exploded_corpse_generations` |
| E5 | Remote held tools ignored use_color / use_other_team_color | Fixed. BlockTool (5, 27), FlareBlockTool (22), PrefabTool (23) + ZombiePrefabTool (28), PaintbrushTool (43) and DisguiseTool (64) are multiplied by the holder's SetColor block colour (UGCPrefabTool 42 resets `use_color`); IntelTool (30) takes the other team's half colour. The tool re-uploads (two-snapshot debounce) when the block colour changes | `retail_tool_color_path`, `load_weapon_models(force_default_color)` |
| E6 | Spawn flash was a whole-model 1.85 white-out on a 0.2 s wave | Replaced (resolves P1-11 VERIFY). `RetailSpawnBlink`: bright (default colour × SPAWN_COLOR_MULTIPLIER 2.0 = full team colour) every frame except the one where `spawn_color_blink_timer` ran out, which draws plain and re-arms with `remaining / 3.0`, so the plain blips accelerate. Only team voxels change (pre-built flash-colour body meshes in the accessory band); held tools flash only for ZombieHandTool (24). `spawn_protection_flash_on` is deleted | `jetpack_death.cpp`, nfm `character_flash_slots` |
| E7 | No 3D labels except dynamite | Fixed. `entity_world_label`: dropped intel return timer (z-1.3), diamond lifetime (always, z-1.3), armed bomb fuse (z-1.5), radar lifetime (z-1.0), friendly turret ammo within 20 blocks (z-1.0, ammo_font = Spades 26, A47 yellow / A48 red at 0). ceil-formatted like Entity.update_3dText. In a live match those packet fuses now count down locally between packets | `local_entity.cpp`, nfm `append_timed_explosive_labels`, `tutorial_session.cpp` |
| E8 | Crates/intel static, no water float | Fixed. Crates 3-6 (also under the chute) and intel turn 10°/s about the vertical; intel at z ≥ Z_ABOVE_WATERPLANE (238) rises by 0.5·dt up to **floating_range 0.7** (recovered from the IntelPickup class body in initgameScene, intel.py:21) and resets out of the water. It is a rise-and-hold, not a bob | `advance_entity_presentation`, `entity_presentation_transform` |
| E9 | Snowballs rendered raw magenta | Fixed. Block Cannon projectiles (29/48) and CreateEntity 24 take the thrower's block colour (local selection or SetColor replica) at creation | nfm `projectile_thrower_block_color`, `sync_entity_meshes` |
| E10 | Death/spectator overhead names white, 14 px | Fixed. UI_TEAM_COLOURS (TEAM1 44,117,179 / TEAM2 137,179,44) and big_name_font = `load_font(STANDARD_FONT, 20)` (text.py set_fonts:202) | nfm `append_player_name_labels`, `player_name_projection.cpp` |
| E11 | No diamond pickup twinkle | Fixed. DiamondPickup.on_delete burst: 50 twinkles, vertical .08, explosion .07, size 5, rotation 180, 2 s, 4x4 @30 fps, additive | `emit_diamond_pickup` |
| E12 | Armed ground bomb had no fuse smoke | Fixed. BOMB_SMOKE at position + (-0.05, -0.05, -1.2), 25/s while the fuse runs | nfm entity particle pass |
| E13 | +0.1 standing head/torso lift | Removed. set_crouch places head/torso at BODY_PARTS_Z 0.3 in both poses; the standing head now equals the crouch head (test) and the head pitches about 0.3. See the `before/after` renders | `class_models.cpp`, nfm |
| E14 | Neutral team colour 160 vs TEAM_COLOURS[1] 128 | **Unchanged (VERIFY).** gameScene reads TEAM_COLOURS only for the spectator team (gameScene.pyx:3046, `TEAM_COLOURS[TEAM_SPECTATOR]`) and the UGC entity table; nothing found assigns TEAM_COLOURS[TEAM_NEUTRAL] to a client Team, so there is no evidence to change 160 | nfm `remote_team_color` |

Tests: `aos_jetpack_death_tests` (half colour, flash colour, blink timer acceleration, tool colour paths, back-intel offsets, corpse attachment), `aos_local_entity_tests` (all five label kinds and their gates, spin rate and axis, intel float), `aos_particle_system_tests` (diamond twinkle), `aos_class_catalog_tests` (no standing lift), `aos_character_scene_capture_smoke` (offscreen render of the whole lineup). Full ctest in `out/build/fix-entities`: 149/149 passed (log beside the renders). **Server note (fixed in Round 2 integration):** the BattleSpades server used to create diamonds with fuse 0, so every diamond showed "0"; it now sends the lifetime (RULE_DIAMOND_LIFETIME) as the packet fuse, like retail.

## Round 2 integration (2026-09-27)

The five round-2 agents (muzzle flash, camera/feel/building, players & entities, frontend, server rules) built in separate directories while editing shared files, mostly `native_frontend_module.cpp`. This pass audited the merged tree, rebuilt from scratch and ran every suite. The camera/feel/building agent left no section in this doc, so its changes are summarised below from the code and `tests/test_retail_feel.cpp`.

### Audit of claimed changes

Each claimed symbol was checked for its definition **and** its live call site. Nothing was missing, duplicated or conflicting, and nothing had to be restored. The `sed -i` edit left the file's line endings consistent, and the 14 localisation files parse with no duplicate keys.

- **Muzzle flash:** `retail_view_muzzle_flash`/`evaluate_retail_view_muzzle_flash`, `arm_local_view_muzzle_flash`, and `append_local_view_muzzle_flash` (6 call sites) are present. The only remaining `push_muzzle_flash` call is the third-person remote flash.
- **Camera/feel/building:**
  - Recoil: `retail_recoil.hpp` (`retail_recoil_kick`, the 512 ms side sawtooth, the stance multipliers) is used by `TutorialWorldSession::apply_weapon_recoil` for hitscan and projectile shots.
  - Death camera: rewritten as DeathController (camera 5), ChaseController and FlyController. `DeathKillerInfo` is fed from `running_local_player_kills`. The camera switches to chase unless the killer's streak is ≥ 2, flies toward the killer from a streak of 3, and LMB/RMB cycling is gated on `locked`.
  - `chase_camera_eye` (validate_position) is used for the own-body, target and fallback eyes. The fly keys are mapped from the forward/back/left/right/jump/crouch controls.
  - Block placement: `block_placement.*` provides bridge scan, body overlap, water and wallet refusals, and the `BLOCK_PLACE_FAIL_*` hints, which are in all 14 languages. The classic ghost uses `placement_preview_wire_cube`.
  - `InitialInfo.block_wallet_multiplier` now reaches the sandbox inventory.
  - `retail_crosshair_visible` implements the NEVER/ZOOMED/UNZOOMED/ALWAYS/HAS_AMMO rules.
  - `zoom_dropped` un-zooms when the last round is fired.
  - Blast push: `retail_blast.*` and `apply_blast_push` run on Damage(37) for the stock `damage_functions` types.
- **Players & entities (E1–E13):** the checks covered:
  - Characters: `retail_character_color` (team ×0.5), the accessory band (`character_accessory_slot_base` 1984, `world_model_slot_count` 2240), `RetailSpawnBlink`/`character_flash_slots`, `retail_back_intel_attachment`, `exploded_corpse_generations` and `retail_tool_color_path`.
  - Entities: `entity_world_label`/`append_timed_explosive_labels`, and `advance_entity_presentation`/`entity_presentation_transform` for spin and intel float. The snowball colour comes from `projectile_thrower_block_color`.
  - Other: the UI_TEAM_COLOURS big name font, `emit_diamond_pickup`, BOMB_SMOKE, and the class_models standing lift, which is gone with no `*_lift` references left.
- **Frontend (1–25):**
  - Server browser: the auto-refresh is gone, and `pump_server_browser_refresh` only drains the worker.
  - Settings: `range_bar_geometry`/`step_volume`, `slider_geometry`, `live_favourite`/`apply_live_favourite`, Main Defaults, and `toggle_half_at`.
  - Pause menu: the SAVE/QUIT and message-box flow (`consume_pause_message_button`).
  - Loader: map preview, `set_custom_game_rules`/`custom_match_rules`, localised status, tagline, and `interrupt_tab_cycle`.
  - Chat: MAX_CHAT_MESSAGE_LENGTH 200 with a 90-character wrap, and `chat_glyph_supported`/`set_channel_labels`.
  - Also checked: `leave_timed_out_loading`, `save_server_region`, `set_mode_title_lookup`, and `audio->stop_all()` in `disconnect_from_pause_menu`.

### Server

- **Diamond fuse.** `modes/diamond_mine.py` now passes `fuse=diamond_lifetime` when it creates the diamond entity, and each tick it keeps the entity's fuse at the remaining lifetime so late joiners count down from the live value. Test: `tests/test_diamond_mine_fixes.py::test_diamond_create_entity_carries_lifetime_as_fuse`. It is listed in PARITY_CHANGES section 24, "Rules audit fixes".
- **Full server suite,** in three parts (`py -3.12 -m pytest -q -p no:cacheprovider`):

  | Part | Result |
  |---|---|
  | Files sorted before `test_surface_corridor.py`, without the bot map matrix | 9446 passed, 1 skipped |
  | The remaining files, without the bot map matrix | 458 passed |
  | `test_bot_map_matrix.py` | 36 passed |

  There were no access violations and no reruns.

### Build and tests

- Fresh configure and build in `out/build/round2`: native-dev preset, RelWithDebInfo, warnings as errors, `--parallel 4`. All 718 steps ran with no warnings or errors.
- `ctest -C RelWithDebInfo`: **149/149 passed**. That includes the 16 `aos_view_muzzle_flash_capture_*` GPU captures, `aos_ads_sight_render_tests`, `aos_character_scene_capture_smoke`, `aos_placement_render_tests`, `aos_block_shading_render_tests` and `aos_retail_feel_tests`.
- Headless sanity, with no game windows: `aos_retail_scene_capture` rendered Alcatraz, and `aos_character_scene_capture mode=after view=back|front` rendered the lineup: packs, back intel, classic corpse and spawn flash. Every run exited 0 and the images looked correct.
- Executable: `out/build/round2/src/RelWithDebInfo/BattleSpadesClient.exe`.

### Still open

- **Live play-test of round 2.** Nothing here was played live, because the user's session was running from `parity-integration`. Still to check live:
  - the hip and ADS muzzle flash;
  - recoil feel;
  - the death, killer, chase and fly camera flow;
  - bridge placement and hints;
  - the blast push against the server's 3rd-frame push;
  - jetpacks on living players, spawn blink and the 3D labels (the diamond now counts down);
  - the frontend items (volume and sensitivity controls, favourite, timeout, SAVE/QUIT).
- These VERIFY items are carried over unchanged:
  - the deployed MG flash and the dynamic light for local shots (muzzle flash);
  - no mid-session VXL flush for Map Creator SAVE (frontend 6);
  - the UGC png preview on the loader;
  - disconnect errors shown as big text;
  - ExplodeCorpse for classic non-pack corpses (E4);
  - the neutral team colour (E14);
  - no retail side-by-side captures for either the entities or the muzzle flash.
- The server's HeadCount type, VIP rounds and the TC capture rates remain **VERIFY** with a retail capture (PARITY_CHANGES section 24).

## Round 3: model lighting, muzzle flash anchor, shot light and tracers (2026-09-28)

User report (Normal graphics): (1) indoors under a light source the hands and player models stay unlit; (2) the minigun flash "still looks off and isn't quite at the muzzle"; (3) the enhanced flash should light the scene; (4) tracers "come out of my crosshair" instead of the gun.

### 1. Map light on models (enhanced tiers)

- **Cause.** Terrain receives placed flare/fire light from the mesher's per-vertex bake (`a_color2`, `StaticLightField`) and emissive spill from the 3D volume probe (`EmissiveVolume`). KV6 models have neither: their vertices carry no bake. The first-person view model is also drawn in view space, so `world_renderer.cpp` switches its volume probe off (the probe would read an unrelated cell). Under a lamp the room lit up while the hands and players only got ambient × the 0.45 interior skylight, which is dark.
- **Retail check.** Retail `model_frag` lights KV6s from `gl_LightSource[0..1]` (the packet-45 directional lights) and the ambient only. `character.pyd` asks `light_manager.get_free_dynamic_light` only in `Grenade.initialize` (headless IDA token dump, `scratchpad/mz3/chlight.txt`). Retail never samples map light for models, so the Retail tier is unchanged.
- **Fix.** `world::sample_model_light` / `model_light_rgb` (`emissive_volume.hpp`) sample the placed-light field and a new trilinear `EmissiveVolume::sample_filtered` (matching the GPU's GL_LINEAR fetch) at a world position. `WorldRenderer::set_model_light_sources` takes non-owning pointers, which nfm sets every frame and clears around session teardown. `submit()` adds the result through a new `u_modelLight` uniform, using terrain's own gains: 1.6 × placed and `emissive_cast_gain` × spill.
  - World models (players, entities) sample at their part origin, placed light only; they already probe the volume in-shader.
  - The view model samples at the eye, placed plus spill.
  - Terrain and the unlit flash get zero.
  - `fs_world` adds `albedo * u_modelLight.rgb` after occlusion, like the placed-light term.
  - Dynamic lights already reached models (the view model gets view-space light positions), so they now pick up the shot light below.
- Test: `aos_emissive_set_tests` `models_sample_the_map_light_terrain_receives`. It covers a flare near a model, spill beside a fixture, a dark tunnel receiving nothing, null sources, trilinear = nearest at a cell centre, and the Retail gains (0) giving black.

### 2. Muzzle flash on the barrel mouth

- **Measured.** Retail's `muzzle_flash_view_offset` is applied in the draw_fps root, outside `Tool.apply_transform`. Against the real KV6 geometry (barrel mouth = front voxel slab of the front-most part, projected) that leaves the flash well off the barrel. Distances are from the flash-pixel centroid to the mouth, in % of a 1280×720 frame:

| Tool | Retail offset | Anchored (enhanced) |
|---|---|---|
| 8 Minigun | 4.26 (flash centre 0.375 units past the tip and 0.12 above the barrel axis; its rear floats 0.15 clear of the barrel) | 0.17 |
| 60 Assault rifle | 12.23 (flash under the receiver: `initial_position` (0,0.2,0) and size 0.035 are ignored) | 0.08 |
| 61 LMG | 13.20 | 0.11 |
| 53 Auto pistol | 6.95 | 0.16 |
| 62 Auto shotgun | 7.22 | 0.17 |
| 19 Sniper 2 | 7.08 | 0.14 |
| 10 Shotgun 2 | 4.01 | 0.58 |
| 7/38 SMG, 9/37 shotgun, 17 pistol, 18 sniper, 35 tommy gun, 36 snub pistol | 0.8 - 2.8 | 0.08 - 0.69 |

The numbers come from `measurements.txt`; tool 15 (MG) has no first-person model.

- **Fix (enhanced tiers).** nfm computes `world::view_model_muzzle_tip` for each uploaded first-person part (and the tutorial pistol). Each frame it picks the part whose mouth is furthest ahead (the minigun barrel, not its body). It draws the flash as `S(muzzle_flash_scale)·R_z(roll)·T(tip + 5 flash voxels)·part_matrix`, which keeps retail's size (view_model_size × scale) but rides the hold, recoil and barrel spin. The flash's rear overlaps the mouth by one flash voxel.
- The Retail tier and ADS (`draw_sight`) keep retail's offsets exactly.
- Evidence is in `out/evidence/muzzleflash-2026-09-28/`:
  - `before/`: retail placement, the previous build's behaviour on every tier;
  - `after/`: the anchored enhanced flash;
  - `raw/`: every capture plus `toolN.log` with the numbers.
- Test: the 16 `aos_view_muzzle_flash_capture_<tool>` GPU captures now also render the anchored flash. They assert that it covers the projected barrel mouth and that its centroid is within 2% of the frame from it.
- Round 3 build: `out/build/round3`, native-dev, RelWithDebInfo, warnings as errors. `ctest` passed **149/149**. `aos_terrain_effects_tests` was updated for the new muzzle-light policy: snipers now flash too, and the radius is 2.5-4.5 blocks.

### 3. Shot light at the muzzle (enhanced tiers)

- **Cause.** The local light sat at eye + forward·0.72, i.e. on the crosshair. `spawn_weapon_flash` also whitelisted the wrong tool ids (20 "SMG", 48 "shotgun"), so most guns cast no light at all.
- **Fix.** `weapon_flash_casts_light` covers every gun with a retail flash plus the launchers. `muzzle_light_enabled(retail_look, tool)` is false in the Retail tier (retail has no shot light). The light is warm (1.0, 0.80, 0.50), cools to orange, has a radius of 3.2 blocks (shotguns 3.8, launchers 4.2) and dies in 65 ms (80 ms for launchers).
- Placement:
  - Local shots: at `local_muzzle_point()`, the drawn view-model mouth (`local_view_muzzle_point`, recorded each frame from the anchor) carried through the camera basis.
  - Remote shots: at the recovered third-person muzzle, as before.
- The light reaches terrain, player models and, through the view-space light transform, the hands and gun.
- Test: `aos_retail_effects_tests` `muzzle_light_spawns_at_the_muzzle_on_enhanced_tiers`.

### 4. Tracers

- **Retail (headless IDA, gameScene.pyd).** `Character.shoot` builds, per pellet, `Tracer(world_object.position.copy(), orientation.copy()*A1092, weapon.tracer, prestep=16, ttl=...)`. `Tracer.initialize` (0x100969C0, source lines 10-27) then:
  1. sets `velocity = direction * prestep` and `ttl = 2`;
  2. calls `self.update(1)`: the constant tuple at 0x1028D234 is `PyTuple_Pack(1, …)`;
  3. restores `ttl` and the real velocity.
  
  `Tracer.update` deletes the tracer when the hitscan contact lies within that step. So retail tracers are born 16 blocks down the eye line and never visibly leave the gun. Our tracer started at eye + 0.72, which is exactly the "out of the crosshair" look.
- **Fix.** `world::plan_tracer_launch`:
  - Retail tier: retail exactly. Eye + 16, and no tracer when the hit is nearer than 16 blocks.
  - Enhanced tiers: from the real muzzle to the contact (or 128 blocks along the aim line). For the local player that muzzle is the first-person barrel mouth in world space; for remote players it is the third-person muzzle. No tracer is drawn when the wall is already behind the muzzle.
  - Hit registration is untouched; only the visual changed.
- Test: `aos_retail_effects_tests` `tracers_launch_from_the_muzzle_on_enhanced_tiers`.
- Per-pellet tracers for shotguns remain **VERIFY**. Retail spawns one per pellet; we still spawn one per shot.

### Still open

- No live play-test. Still to check live: the hands under a lamp or near neon, the minigun flash while spinning and firing, the shot light, and tracers.
- The deployed MG flash is still unported (unchanged).

## Round 4: audio (2026-09-28)

Source: audit3 `audio.md` (items 1-15) plus `verify.md` V5 and V7, all taken from the stock pyds with headless IDA and from the readable `aoslib/media.py`, `aoslib/audio.py`, `weapons/tool.py` and `scenes/main/entity.py`. The pure mix model now lives in `include/battlespades/audio/retail_mix.hpp`, covered by the new `aos_retail_audio_mix_tests`. `aos_server_audio_catalog_tests` now pins the retail weapon gains.

| # | Item | Status |
|---|------|--------|
| 1 | Dynamic reverb | **Fixed.** `update_environment_audio` ports `GameScene.update_audio_effects` (gameScene 0x101446f0) and runs once per fixed update from the listener eye. It fires 5 upward `world.hitscan` rays; if at least 4 hit, it fires 8 lateral rays, and each hit under 40 blocks counts as a wall. With 6 or more walls the gain target is 0.1·(walls−5)/3 (0.033, 0.067 or 0.1) and the decay is min(0.1 × mean wall distance, 2.2) s. Otherwise, including every outdoor case, the gain target is 0 with size 1. Both are smoothed at 0.03 per update. The EFX effect is rewritten and the slot re-bound whenever the value changes. The static 0.32/1.49 s bus is gone. |
| 1 | Reverb routing | **Fixed.** Sends now follow the retail zone, not head-relativity. Every local or remote in-world cue feeds the slot: 2D weapon, foley, VO, zoom and loops. Server PlaySound(23), PlayAmbientSound(24) and server loops stay dry, as do the menu, tutorial, respawn-beep, skin and heal-end HUD cues. |
| 2 | Gunshot level | **Fixed.** `local_weapon_report_gain` and `remote_weapon_report_gain` are 0.5 (`Tool.play_sound`). Every world cue uses rolloff 0.15; the `weapon_report` 0.065 profile is gone. Throws, pins, melee swings and dry-fire are 0.5 in the live and lab paths. Reload stays at 1.0. |
| 3 | Reference distance | **Fixed.** Every source uses 1.0, and `AL_MAX_DISTANCE` is left at the OpenAL default. The 50-block allocation pre-cull is the only range limit. |
| 4 | Occlusion | **Removed.** The VXL spatial-gain resolver, `spatial_sound_gain`, and the 0.35/0.70 transmission floors are deleted. `raycast_acoustic_path` remains in world/ but is unused by audio. |
| 5 | Foley | **Fixed.** Footstep, wade, jump, land and fall-hurt play at 1.0. Steps and wading are pitched ±1.5 semitones; jump, land and fall-hurt ±0.8, for both named class foley and the generic banks. The jetpack landing is a plain string row and stays unpitched. Variants are drawn at random, never repeating the last (`MediaManager.get_sound_name`). |
| 6 | Explosions | **Fixed.** The ×1.6 explosion gain and ×1.25 death-explosion gain are removed. Death explosions get ±0.8. Bullet scenery hits get ±1.2. `play_weapon_cue` now applies the row pitch: woosh is ±0.4 as a swing and ±0.8 as a throw; molotov_throw and whack are ±0.8; the hitground family ±0.4. |
| 7 | StopMusic | **Fixed.** `stop_menu_music()`, which also backs packet 27, is now `media.stop_music()`: the track moves to the fade source and loses 1/6.5 volume per second. A later `play_music` zeroes a still-fading track, as retail does. Only teardown (`stop()`, `stop_all()`) cuts instantly. |
| 8 | Select-bed fade | **Fixed.** Fade speed is stored per track. `play_secondary_menu_music()` uses 1/1.5 (`SECONDARY_MUSIC_BED_FADE_TIME`); everything else uses 1/6.5. The outgoing track's speed applies. |
| 9 | Ambience | **Fixed.** `AmbientSound.update` is ported: ≤1 point is a bed at its volume; 2 points are positional at the closest point on the segment; ≥3 points are non-positional at `vol/(1+att·(max(d,1)−1))`. Beds and ≥3-point emitters are ducked by `1−0.8·ducking`. Running loops are no longer gain-gated at 50 blocks; a positioned stream is culled only when it starts. The fallback bed starts at full volume, replacing the old 0.03-per-tick fade-in, and is ducked the same way. Packet-22 points are no longer pre-offset, because the mixer applies the +0.5. |
| 10 | Server PlaySound falloff | **Not changed (VERIFY).** The retail server's attenuation was never recovered, so `server/audio.py` still defaults to rolloff 1.0. |
| 11 | Voice cap | **Fixed.** nfm passes `audio::retail_one_shot_voices` (128) instead of 48. |
| 12 | Entity volumes | **Fixed.** Entity cues play at 0.75, the crate chute-open at 4.0 (OpenAL clamps after attenuation) and entity detonations at 1.0. |
| 13 | VO | **Fixed.** 1.0 instead of 0.9. |
| 14 | Source offset | **Fixed.** Every positional source, one-shot or loop, and every reposition sits at (+0.5, +0.5, −0.5 z) in AoS space (`GameSound.set_position`). The listener has no offset. |
| 15 | Direct channels | **Fixed.** `AL_DIRECT_CHANNELS_SOFT` is set on every source when `AL_SOFT_direct_channels` exists (audio.py:411-412). |
| V5 | Heal cue | **Fixed.** The extra `medi_pack_healing_001` one-shot on each heal packet is removed; the 1 s loop and `healing_end` remain. |
| V7 | Placement build cue | **Fixed (audio part).** The per-packet `build` one-shot is removed; observers hear only the server's PlaySound 46. The snowke ring and puff removal belong to the visuals pass. |

Notes:

- Two ambience choices are assumptions.
  - `AmbientSound.volume` and `attenuation` are taken from the bound packet 24. Packet 22 carries neither, and the constructor defaults were not recovered.
  - A 1-point emitter starts on its authored point rather than at the BattleSpades server's listener bootstrap, since retail never moves a 1-point stream.
- Unchanged, with no evidence either way:
  - the airstrike explosion's ×1.6 (`advance_airstrike_audio`);
  - per-entity place-cue pitch. `Entity.play_sound` calls `media.play` without pitch arguments; that is worth a check.
- `Character.play_sound` routes local cues to IN_WORLD even with `pos=None`, per the audit. `Tool.play_sound` itself passes `zone=DEFAULT`, so confirm this in character.pyd if the local weapon reverb sounds wrong.
- Live listening test still owed: indoor versus outdoor reverb, gunfire balance at range, and the StopMusic fade.

## Round 4: network/world/gameplay (2026-09-28)

Sources: the round-3 audit notes `network.md` (H1-L10), `world.md` (W1, W7, W8) and `verify.md` (V1-V11), plus new headless-IDA reads of stock `gameScene.pyd`. Built in `out/build/r4-net` (native-dev, RelWithDebInfo, warnings as errors).

### Network and join

| Item | Status |
|------|--------|
| H1 ClientData/ClockSync reliability | **Fixed.** `protocol168_client_packet_unsequenced`: packets 0 and 4 go out `ENET_PACKET_FLAG_UNSEQUENCED`, everything else stays reliable (retail `send_packet(packet, unreliable)`, true only at `send_client_data` and `send_clock_sync`). The lpc comment that called this RELIABLE is corrected. Server: unchanged; it keys input on loop labels, not ENet flags, and its gap refill / stale drop (`input_gap_fill_limit`, `tests/test_movement_jitter.py`, 41 passed) is exactly the unsequenced path retail clients use. |
| L6 ClockSync | **Fixed.** `client_time = time % MAX_PING` with MAX_PING = 10000 (`PyInt_FromLong(10000)` at gameScene 0x101abb6b). The reply computes ping, a half-RTT lead of `int(ping*0.001*0.5*60)` loops, and relabels only outside the ±10 dead band (`network::clock_sync_relabel`). The P3-04 no-rewind rule still applies to the relabel. Cadence stays one request per 60 fixed loops. |
| H2 Connection watchdog | **Fixed.** `RetailConnectionWatchdog`: `last = max(last ClockSync reply, last WorldUpdate)`. After 300 silent loops, `CONNECTION_PROBLEMS` ("Disconnect in {0:.1f}") is re-posted to the big-text lane every loop. Past 20 s with no response ever, 30 s after one, or 60 s in UGC, it logs "Connection to server lost" and fails the match connection with `ERROR_TIMEOUT`. |
| M3 Local map CRC | **Fixed.** `Protocol168SessionConfig::local_map_directory` (set to `<assets>/maps`). On InitialInfo the session reads `<filename>.vxl` and answers MapDataValidation with its zlib crc32. Only a full 512×512, 240-high map qualifies; UGC or a missing file answers 0. When the server's own CRC matches, MapSyncEnd applies the (x, y, spans) records onto the local file (`protocol168_apply_map_records`), so an empty dirty-column delta is valid. A mismatch still requires a complete ordered snapshot. Server: it honours the CRC only with `map_sync_mode = "auto"`. The shipped default is `"full"` (a stock-client limitation noted in `config.toml`), so it still streams everything, which the overlay path also accepts. The client/server stock maps are byte-identical (checked all 27). |
| M4 Join timeouts | **Fixed.** `EnetProtocol168Config::connect_timeout_ms = 5000` covers only the ENet CONNECT, and a miss reports DISCONNECT 11. After connect, `timeout_ms` (30 s) is a no-progress timer restarted by every accepted handshake packet (failure key `ERROR_TIMEOUT`). The old absolute 30 s deadline is gone. |
| M5 Loader bar | **Fixed.** The worker publishes `LiveProtocol168Status::loading` and InitialInfo as they arrive. The loader shows CHECKING_MAP with its tabs at InitialInfo, LOADING_MAP at MapDataValidation, RECEIVING_MAP with the UGC MapDataChunk percent, SYNCING_MAP with the MapSyncChunk percent, and INITIALISING_MAP at MapSyncEnd, in thirds as `loadingMenu`. The local mesh build fills the last third (`world_build_progress`). |
| L8 Apply budget | **Fixed.** A playable world drains the whole inbound queue per tick (retail `NetworkClient.update`). Loading keeps the bounded 128 tranche. |
| L10 Connect data | **Checked.** Native connects with data 168 (`test_connection_recovery` asserts it). SERVER/CLIENT_OUT_OF_DATE (3/10) map to their strings. The server still ignores the value (server-side L10, not changed). |
| V3 Disconnect table | **Fixed.** 11 → UNABLE_TO_CONNECT_TO_SERVER, 19 → A958 unformatted (the retail bug, "{0}" shows), 21 → INVALID_SESSION_TICKET, and 0/20/31 or anything else → CONNECTION_CLOSED. 18 stays the reconnect. |

### World

| Item | Status |
|------|--------|
| W1 Falling chunks | **Fixed.** `retail_falling_blocks_step` ports world.pyd sub_10007DC0. It applies `v.z += g·dt` (StateData gravity), `pos += v·dt·32`, and a solid probe at `floor(origin)`: z = 239 probes 238, z ≥ 240 always hits, and outside x/y never does. On a hit it restores the old position, reflects the axis whose cell changed and halves the velocity. The origin is the bounding-box centre. The body tumbles about a retail random unit axis (sub_100027F0) at 50°/s. It breaks on the first contact: every `int(5 + size/8000·10)`-th voxel emits 5 particles (explode 0.125, size 5.0, lifetime 2), carrying the negated bounced velocity per draw.pyd. There is no size cap and no timer; a 60 s safety retires a body that can never land (gravity ≤ 0, not retail). The impact bank plays at the restored origin, so the water tier follows z ≥ 238. |
| W7 Hitscan water | **At parity.** Native traces are plain solid lookups on a map whose z = 239 bed is solid, which is retail's default `water_is_solid=True`. A test pins it. |
| W8 beach_z_modifiable | **Fixed.** InitialInfo `beach_z_modifiable` is decoded, and block targeting/ghosting uses `max_modifiable_z = 238` if set, else 237. |
| W8 BlockNess dome | **Fixed.** The native fallback now matches the server (`User_Grassland.txt`, the identical twin of Classic_B). |
| V8 Classic block health | **Fixed (client).** `VxlMap::set_user_block_rules`: in classic mode `add_user_block` stores DEFAULT_BLOCK_HEALTH (5) × multiplier, and in UGC mode it drops the user_blocks entry. Wired from InitialInfo `classic` / UGC through `Protocol168TerrainReplica`. |

### Gameplay visuals

| Item | Status |
|------|--------|
| V1 Shell reload | **Fixed.** `end_reload` also stops the chain on a trigger held at the shell boundary. `shoot_primary_held` (set by the shot that empties the magazine while held) stops the chain and resumes fire once rounds are back, for magazine weapons too. The "wait for weapon_shoot to finish" gate is not ported. |
| V2 Auto-switch | **Fixed.** The switch walks AMMO_DEPLETED_SWITCH_ORDER (primary, secondary, melee) over the class CLASS_ITEMS and picks the first carried loadout item with ammo. |
| V4 Burn / sudden death | **Fixed.** Both are full-screen `inside_zone_texture` quads: burn is (255, 0, 0, a), sudden death is (team colour, a), with a = remaining/1.2·255. Both draw when both run (burn first). |
| V6 BlockFire | **Fixed.** No display model in the Retail tier (the additive cube stays an enhanced extra). The ramped static light already follows HOT/MID/COLD. Smoke now draws uniform(0, 0.1) per axis. |
| V7 Placement puff | **Fixed.** The invented 4-puff bloom is removed. A BlockBuild of type 1 (snow) emits the snowke ring; ordinary blocks get nothing. |
| V9 Tracers | **Fixed.** One tracer per pellet (`replicated_hitscan_pellets`), ending at `min(contact, 16 + min(range/200, 0.5)·200)` blocks. |
| V10 Smoke ring | **Fixed.** Uses the caller radius (default SMOKE_RING_SIZE), `x += sin(a)·r`, `y += cos(a)·r`, and one static puff per point (velocity None) coloured by the map voxel. Other parameters: rotation 180, speed 0, decay −1, lifetime 1, start frame 1, 60 fps, no loop, no gravity, no collision (IDA gameScene 0x10189350 kwargs). The snowke ring's own angle step and z lift are still **VERIFY**. |
| V11 Death hint | **Already at parity.** It is one `set_big_message(DEATH_CLASS_CHANGE_HINT, respawn − 2)` at death (`show_death_class_change_hint`), with no dead-screen label. |

### Tests

- New or updated tests:
  - `aos_connection_recovery_tests`: connect timeout → 11; no-progress → ERROR_TIMEOUT; the unsequenced table; the ClockSync maths; the watchdog limits.
  - `aos_protocol168_session_tests`: CRC answer, local base, delta overlay, mismatch, UGC/missing → 0, beach byte.
  - `aos_terrain_effects_tests`, `aos_particle_system_tests`, `aos_retail_effects_tests`, `aos_game_hud_tests`, `aos_weapon_runtime_tests`, `aos_player_inventory_tests`, `aos_protocol168_terrain_tests`, `aos_replicated_shot_tests`, `aos_retail_feel_tests`, `aos_retail_hud_rules_tests`, `aos_frontend_tests` (budget).
- Full `ctest` in `out/build/r4-net`: 150/150 passed.
- Server: `tests/test_movement_jitter.py` passed 41/41, and `-k "map_sync or crc or clock_sync or map_validation"` passed 31/31. No server code was changed.

### Still open

- No live play-test for any of the above. Still to check live: the watchdog countdown on a stalled server, a CRC-matched join against a server in `map_sync_mode = "auto"`, falling chunks landing on ledges, and shotgun tracer fans.
- The server's WorldUpdate and ClockSync reply are still sequenced / reliable (L7). The server does not refuse a wrong connect version (L10).

## Round 4 integration (2026-09-28)

Four round-4 agents worked in the same tree at the same time: audio (`r4-audio`), network/world/gameplay (`r4-net`), social (`social`) and Steam (`steam`). All of them made surgical edits to `native_frontend_module.cpp`. This pass audited the merged tree, rebuilt it from scratch in `out/build/round4`, and ran every suite. Nothing was committed or deployed.

### Audit of claimed changes

Each claimed symbol was checked for its definition **and** its live call site. Nothing was missing, duplicated or conflicting, and nothing had to be restored.

- **Audio:**
  - `update_environment_audio` runs per fixed update from the listener eye.
  - `local_weapon_report_gain`/`remote_weapon_report_gain`, `retail_one_shot_voices` (128), `SECONDARY_MUSIC_BED_FADE_TIME` and `AL_DIRECT_CHANNELS_SOFT` are present.
  - `spatial_sound_gain` has no references left.
  - The only remaining `medi_pack_healing_001` reference is the 1 s heal loop.
  - `retail_mix.hpp` is covered by `aos_retail_audio_mix_tests`.
- **Network/world:**
  - `protocol168_client_packet_unsequenced` is used on the `LiveProtocol168Connection` send path.
  - Wired: `clock_sync_relabel`/`protocol168_clock.hpp`, `RetailConnectionWatchdog`/`advance_connection_watchdog` (in tick after `send_live_clock_sync`), `connect_timeout_ms`, `failure_key`, `present_handshake_progress`, `protocol168_map_crc32`/`protocol168_apply_map_records` and `protocol168_inbound_apply_budget`.
  - `retail_falling_blocks_step` is called from `terrain_effects.cpp`.
  - `set_user_block_rules` is called from `Protocol168TerrainReplica`.
  - Also checked: `beach_z_modifiable`, `shoot_primary_held`, `AMMO_DEPLETED_SWITCH_ORDER`, the burn/sudden-death `inside_zone_texture` quads in `game_hud.cpp`, `replicated_hitscan_pellets` and `show_death_class_change_hint`.
- **Social:**
  - Present: `set_foreground` (in `pump_social`), `set_service_status(status.enabled)` plus `set_connected(status.available)`, the leave-before-accept in `submit_social_intent`, `revival_social_write_is_idempotent`/`_failure_is_transient`, `AOS_REVIVAL_API_BASE` and `incoming_request_count`.
  - The one aggregate initialiser of the reordered `RevivalSocialClientStatus` (`revival_social.cpp`) matches the new field order (`available, enabled, closing, …`).
- **Steam:**
  - `steam_connect.{hpp,cpp}` is registered in `src/CMakeLists.txt`, and `aos_steam_connect_tests` in `tests/CMakeLists.txt`.
  - Present: `parse_steam_host_address`, `pending_steam_join`/`queue_steam_join`/`consume_pending_steam_join`, `steam_join_deduplicator`, `steam_lobby_resolve_worker` (waited in `~Impl`), `publish_dedicated_presence`/`clear_match_presence`, `open_steam_invite_dialog`, `sync_identity_steam_button`, and `NativeSteamClient::begin_start/take_join_events`.
  - `IdentitySteamState`/`set_steam_state` are present.
  - `pump_steam_integration()` runs every frame after `pump_steam_lobby()`.
- **Join paths and `local_map_directory`:** only one place builds a live transport session: `start_match_transport`, the single `LiveProtocol168Connection::start` call in the client. It sets `session.local_map_directory = asset_root/maps` and resets the loader-progress flags. Every other join reaches it through `begin_match_loading` → `begin_match_identity` → `start_match_transport`:
  - the Steam `+connect steam:<id>` / `+connect_lobby` / friends-list join (`consume_pending_steam_join` builds a `"steam:<id>"` `ServerConnectRequest`);
  - the AoSPlay lobby start and invite accept;
  - the server browser;
  - map transitions.

  The two other `Protocol168SessionConfig` instances (`send_initial_player` and the spectator rejoin) only encode the NewPlayerConnection(15) packet, so no transport reads them. The ClientData/ClockSync unsequenced flag lives inside `LiveProtocol168Connection`, so every join path gets it.
- Also checked:
  - All 14 localisation files parse, with no duplicate keys.
  - Every `tests/test_*.cpp` is registered, three of them through the `foreach(frontend_test …)` block.
  - A stray untracked `x.png` sits in the client repo root. It was left in place.

### Build and tests

- Fresh configure and build in `out/build/round4`: native-dev preset, VS 18 (MSVC 14.51) with Ninja Multi-Config, RelWithDebInfo, warnings as errors, `--parallel 4`. All 727 steps ran with no warnings or errors, and nothing needed a rerun.
- `ctest -C RelWithDebInfo`: **151/151 passed**. That includes `aos_weapon_runtime_tests`: its earlier red in `out/build/social` came from weapon edits still in progress, and it now passes on the merged tree. Also covered: `aos_retail_audio_mix_tests`, `aos_connection_recovery_tests`, `aos_protocol168_session_tests`, `aos_steam_connect_tests`, the revival social and friends-lobby tests, and all GPU captures.
- Headless sanity, with no game windows:
  - `aos_character_scene_capture mode=after view=back|front` rendered the lineup correctly.
  - `aos_retail_scene_capture` rendered Alcatraz, identical to the round-2 capture.
- `aos_social_live_driver` ran against the local `social-dev-server.mjs`, which serves the real `service.ts` on PGlite. The full script ran: search, request, accept, lobbies, invite, accept-while-in-lobby, 260-message chat, restarted-client first sync, leave and close.
  - Plain run: 0 failures.
  - `--latency 1200 --jitter 800 --fail-rate 0.08`: 0 failures. The injected HTTP 504s were retried as `service_unavailable`.
- Backend (`aos_revival`): `npm run test:social` passed 17/17. `test:social:lifecycle` passed 14, with 2 skipped because they need `AOS_SOCIAL_TEST_POSTGRES=1`. `tsc --noEmit` and eslint on the social files are clean.
- Executable: `out/build/round4/src/RelWithDebInfo/BattleSpadesClient.exe`.

### Server

No server code was changed in round 4. The full suite ran in three parts (`py -3.12 -m pytest -q -p no:cacheprovider`):

| Part | Result |
|---|---|
| Files sorted before `test_surface_corridor.py`, without the bot map matrix | 9517 passed, 1 skipped |
| The remaining files, without the bot map matrix | 465 passed |
| `test_bot_map_matrix.py` | 36 passed |

There were no access violations and no reruns.

### Still open

- Nothing from round 4 has been play-tested live. The user's session runs from `round3`/`parity-integration`. The live checks owed by each round-4 section above still apply:
  - reverb, gunfire balance and the StopMusic fade;
  - the watchdog countdown;
  - a CRC-matched join with `map_sync_mode = "auto"`;
  - falling chunks and shotgun tracer fans;
  - a Steam `+connect` / friends join between two machines;
  - a two-identity AoSPlay invite against production after the backend is deployed.
- The backend social fix (`src/lib/social/service.ts`) is **not deployed**. Production is a Vercel project linked in `aos_revival/.vercel`. Earlier releases were deployed with `vercel deploy --prod`, sometimes from an isolated `.transactions/<name>` copy (`tmp/*-deploy.log`), and aliased to www.aosplay.net. No git-push CI deploys the web app; the only workflow is `relay-release.yml`, which runs on tags. The patch is the scratchpad `social/service_fix.patch`.
- The two PostgreSQL-only lifecycle tests (concurrent joins, lock inversion) have not been run against a real PostgreSQL.

## Round 5: platform/audio/caches (2026-09-29)

Sources: audit4 `robustness_perf.md` (items 1, 2, 3, 5, 9, 12, 13, 15), `open_items.md` (1.1, 1.4, 1.6, 1.8, 1.9, 1.13) and headless-IDA notes on `Character.draw` (ch.pyd 0x1004F120). Built in `out/build/r5-platform` (native-dev, RelWithDebInfo, warnings as errors).

### Platform and frame feel

| Item | Status |
|------|--------|
| Minimise / alt-tab stopped the match | **Fixed.** `live_world_simulation_allowed(has_session, in_stack)` no longer takes the window state. The local sim, `send_live_client_data`, `send_live_clock_sync` and `advance_connection_watchdog` keep running while minimised, and remote rigs keep moving. Only presentation stops (`live_world_presentation_allowed`). `window_suspended` is now also cleared on `focus_gained`, `drawable_resized` and a new `WindowEventType::maximized` (SDL_EVENT_WINDOW_MAXIMIZED), because minimise-while-maximised never sends RESTORED. |
| Borderless fullscreen | **Added.** `WindowPort::apply_display_mode(extent, fullscreen, FullscreenKind)`. Borderless is `SDL_SetWindowFullscreenMode(nullptr)` + fullscreen, so the desktop mode is kept, alt-tab is instant and SDL never auto-minimises. Exclusive (the retail mode switch) is still available. The setting is `graphics.fullscreen_mode = "borderless" \| "exclusive"` in `settings.toml`, default borderless. There is no menu row, and the retail Defaults button leaves it alone. |
| VSync latency | **Fixed.** `bgfx::Init::resolution.maxFrameLatency = 1` (`render::bgfx_max_frame_latency`). The default of 3 let the queue fill 2-3 frames because 60.000 Hz ticks never match a 59.94 Hz panel. |
| High-refresh judder | **Added (render-only).** `RuntimeModule::intermediate_frame_period` / `present_intermediate(alpha)`. The fixed 60 Hz loop schedules extra frames between ticks (`core::next_intermediate_frame`, with a quarter-period of slack before the next tick) only when the display reports more than 75 Hz and `graphics.render_interpolation` (default true) is on. An intermediate frame re-submits the tick's retained scene: world draws, particles, lights, view model and the UI draw list. The camera eye is interpolated between the last two ticks (`CameraEyeInterpolator`, which snaps on jumps over 4 blocks), and yaw/pitch stay current. Simulation, input and ClientData are untouched. `render_interpolation = false`, or a display of 75 Hz or less, gives retail's one frame per tick. Limitation: remote players, projectiles and particles still step at 60 Hz; only the camera is smoothed. |

### Audio

| Item | Status |
|------|--------|
| Music/ambience PCM never released (~530 MB) | **Fixed (LRU).** Decoded `music/` and `ambients/` buffers are budgeted at 128 MiB (`music_ambience_pcm_budget_bytes`). Past the budget, the least-recently-used buffer that no source holds is deleted (never the playing track or bed) and re-decoded off-thread on its next use. `aos_audio_cache_tests` touches all 21 retail beds: peak resident 126 MiB, where the old code held every bed. Streaming was not needed. |
| `is_regular_file` on every named cue | **Fixed.** A per-tree (sounds/ambients/music) stem → handle cache is checked before any path is built or the filesystem is touched. |
| Cold Vorbis decode on the main thread | **Fixed.** Every named asset now goes through the worker decode (`request_named_buffer_async`, extended to `sounds/`). A cold one-shot is queued and plays on the tick its upload lands, or is dropped after 300 ms. A cold named loop (`start_named_voice_loop`) reserves its slot and starts at the latest position and gain. Beds, map-ambience preload (loading gate), server loops and ambient one-shots are all asynchronous. The startup base catalogue is unchanged. |
| Local tool cues wet | **Fixed.** `tool_cue_reverb_send(local, cue)`: the local player's own shoot, reload, reload-done, swing/throw/pin, double-shotgun barrel, cosmetic tool cues and weapon loops are dry (retail `Tool.play_sound(pos=None)` → HUD zone). Dry-fire (`empty_fire`) and all remote cues stay IN_WORLD (wet). Named presentation loops (jetpack, parachute, fuses) stay wet. |
| Turret rocket explosion bank | **Fixed.** DestroyEntity of entity 21/22 passes `rocket_explosion_tool(kind)` into `destroy_server_entity`. The kind is the one fixed at launch in `rocket_audio`, falling back to the owner's tool. The blast plays `turr_rocketexplode` (tool 20) for turret rockets. Crater and damage presentation keep the RPG row. |
| Airstrike explosion gain | **Fixed.** 1.0 (`airstrike_explosion_volume`), not ×1.6; the ±0.8 pitch is kept. |
| Entity type-28 cue pitch | **Fixed.** The ±0.4 override is removed; `Entity.play_sound` → `media.play` has no pitch. |

### Caches and robustness

| Item | Status |
|------|--------|
| Remote weapon cache key collision | **Fixed.** `RemoteWeaponModelKey` struct (tool, colour path, tool colour, body colour, skin). The old `tool<<56 \| tool_colour<<32` key let the 0x01/0x02 flag hit the tool bits (22/23, 28/29, 64/65). |
| Remote block-colour tools reloaded KV6 per palette pick | **Fixed.** use_color tools are cached untinted and recoloured in memory with `world::tinted_weapon_models`, which is bit-identical to a tinted load (test over tools 5/22/23/27/28). The block colour is no longer part of the key. |
| Unbounded remote/entity caches | **Fixed.** The weapon cache is LRU-bounded (96). The class, entity, jetpack and disguise caches are cleared past 128 entries between ticks. All of them are released in `teardown_tutorial`; previously they were never cleared on map change, and jetpack/disguise never at all. |
| `VxlMap` `inspect()` heap over-read | **Fixed.** `if (bytes.size() - position < 4U) return std::nullopt;` after a non-terminal span. `aos_vxl_tests` pins the fuzzer payload `01 f9 03 00 0a 00` and every 1..3-byte tail. The audit crash input no longer reproduces under ASan. The map fuzzer was re-run on the fixed loader: see the build and tests section below. |
| Per-frame heap churn | **Reduced.** `Protocol168Roster::present_players()` is an allocation-free view that replaces all 19 per-frame `players()` copies in the frontend (each copied names, loadouts and prefab strings). The cosmetic slot keys for rig sync and player draws are interned (`cosmetic_slot_key`). The ~10 per-frame draw vectors are unchanged. |

### Remote held-tool poses

| Item | Status |
|------|--------|
| Minigun observer barrel roll | **Added.** `advance_retail_remote_minigun_spin` runs MinigunWeapon.update for observers from WorldUpdate bit 0x01: ratio +0.75/s while held and −0.375/s otherwise, with the barrel at ratio·5 rev/s. `evaluate_retail_third_person_pose(..., mechanism_phase)` rolls only the barrel (part 1) about GL z through the recovered pivot `size·(−7, −6.5, 27.5)` (dword_100989B4 = −7, dbl_1008B3F8/F0). RMB spool is not replicated, so observers see the spin only while firing. **VERIFY** the pivot sign/axis against a retail capture. |
| ZombiePrefabTool special draw | **Added.** Size 0.04 and three fixed parts: (−0.95, −0.2, 0.65) rot (0, 0, 180); (−0.72, 0.5, −0.25) rot (45, 45, 0); (0.65, 0.5, 0.3) rot (90, 0, −90). The third tuple was re-read from the decompile: the earlier note's "(90, −90, ·)" was really (90, 0, −90). There is no AnimPlaceBlock. The old generic-loop test expectations were replaced. |

### Server

- P2-18 single-pellet spread: see `BattleSpades/docs/PARITY_CHANGES_2026-09.md` section 24, "Round 5: single-pellet seeded spread". Single-pellet hit-scan shots resolve the seeded direction, the same one the client predicts.
- The server also set `SNUB_PISTOL` `spread` to 0.01 (stock A1138), so `src/world/weapon_catalog.generated.cpp` was regenerated with `tools/generate_weapon_catalog.py`. The client's replicated shot already used `RetailAimTuning.accuracy` = 0.01, so client behaviour is unchanged.

### Build and tests

- **New or updated tests:**
  - `aos_core_tests`: intermediate-frame scheduling and the paced/unpaced Application path.
  - `aos_pause_menu_tests`: the minimise policy.
  - `aos_settings_tests`: the round-trip of `fullscreen_mode` and `render_interpolation`.
  - `aos_vxl_tests`: the fuzzer payload and short tails.
  - `aos_retail_audio_mix_tests`: dry local tool cues.
  - `aos_retail_event_cues_tests`: the airstrike volume.
  - `aos_tutorial_session_tests`: the turret versus RPG rocket bank.
  - `aos_weapon_models_tests`: in-memory recolour equals a tinted load.
  - `aos_retail_character_pose_tests`: the ZombiePrefab special branch, the minigun pivot roll and the remote spin model.
  - `aos_protocol168_players_tests`: the `present_players` view.
  - New `aos_audio_cache_tests`: runs against a real OpenAL device and skips without one. It covers cold named one-shots, missing assets, and all 21 beds under the budget.
- **Full `ctest`** in `out/build/r5-platform`: 153/154 passed. The one failure is `aos_hud_layout_tests` ("status tool uses truncated global image scale"), which belongs to the parallel R5-HUD scale work and not to this round. Earlier runs also showed transient reds from the other in-flight round-5 agents: particles, debug lab, shadow stability, chat wrap and `aos_ugc_round5_tests` not compiling. All of these were green in the final run. The catalogue contract check went red because of the server snub change; the catalogue was regenerated as described in the Server section.
- **Map-loader fuzz** (audit4 `fuzz_map.cpp`, rebuilt with ASan and libFuzzer against the fixed `vxl_map.cpp` and the r5-platform `aos_world.lib`; scratchpad `r5p/fuzz`):
  - The audit crash input `7e 01 f9 03 00 0a 00` now runs cleanly.
  - A fresh 901 s campaign ran 41,842 executions, reached 2,813 coverage features (the audit's patched-copy run reached 2,444) and grew the corpus to 226 entries. It found no crash, no ASan report and no timeout; peak RSS was 633 MB.

### Still open

- Nothing in this round has been play-tested live. To check live:
  - alt-tab and minimise during a match, in both exclusive and borderless fullscreen (the body should keep simulating and the server should see no input gap);
  - VSync feel on a 60 Hz panel;
  - interpolation smoothness on a 120/144 Hz panel;
  - a first-use cue arriving up to one tick late;
  - dry local gunfire indoors;
  - the turret rocket bank;
  - the minigun barrel pivot and the ZombiePrefab hands against a retail capture.
- Render interpolation smooths only the camera; remote players, projectiles and particles still step at 60 Hz. The fullscreen mode has no menu row (it is `settings.toml` only).
- The first-person view-model draw vectors and the remaining per-frame `std::vector<WorldModelDraw>` builders still allocate every frame.

## Round 5: Map Creator & tutorial (2026-09-29)

Source: `audit4/ugc_tutorial.md`, `audit4/tutorial_part.md` and `audit4/open_items.md` (Map Creator rows), plus new headless idalib passes on private copies of the stock `gameScene.pyd`, `hud.pyd` and `vxl.pyd`. The server half is in BattleSpades `docs/PARITY_CHANGES_2026-09.md` §24 ("Round 5: Map Creator & tutorial") and `docs/PROTOCOL.md`. The retail design where the BattleSpades server owns the editor project is kept.

### New retail evidence

- **UGC markers on the stock client:** UGCEntity sets `ugc_item_id = packet.int_properties[0]` (gameScene 0x100a2fd0) and `create_entity` sets `mode_placed_in = packet.ugc_mode` (0x10178b80).
- **Map capacity:** `vxl.pyd` recount 0x10005600 counts non-zero bytes of the 512×512×240 **solid** grid, including the implicit interior, and the non-empty 16³ chunks of the 15,360. `set_point` 0x10029da0 keeps both counters current. `BlockManager.is_space_to_add_blocks` (gameScene 0x10075490) asks the VXL only when `is_in_ugc_mode()`, so the Alcatraz-sized solid count (16.8M) never blocks normal play. For scale, the Desert baseplate is 2,820,321 solids in 1,137 chunks.
- **Editor minimap zones:** `UGCEntity.zone_colours` = `TEAM_COLOURS[UGC_ENTITY_TEAMS[item]]` and `zone_icon_ids` = `ZONE_ICON_SPAWN` (17) for spawns, `ZONE_ICON_DEMOLITION` (1) for bases (gameScene module init 0x101c7200-0x101c87f7).
- **Status Tab (`ObjectivesPlayersList`, hud.pyd 0x10079c00):** `ugc_tab_frame` at 0.8 alpha under `UGC_TAB_TITLE`. Panels are Players `initialise_ui(None, 404, 451, 327, 341)`, Map Config `(None, 68, 451, 327, 120)` and `UGCObjectivesListPanel(68, 352, 327, 242)`. The arg tuples are built at hud.pyd 0x10002cf7/0x10002d54.
- **HUD objectives list:** HUD `__init__` (0x1007e549) builds `UGCObjectivesListPanel(10, height-10, 320, 300, row_height=30, has_header, enable_background_resizing, frame_padding_height=0, transparent_items, show_game_settings_text, only_show_incomplete)`.

### Map Creator (UGC)

| # | Change | Where |
|---|---|---|
| UGC-1 | The server now also sends CreateEntity(21) type 29 for every marker, so stock clients can see, point at and remove them. The native client swallows type-29 CreateEntity (and its DestroyEntity) because it still draws markers from 97/98. | nfm `ignored_ugc_entity_ids` |
| UGC-2 | The lobby Map row lists SAVED_MAPS (the `hosted_ugc` catalog, reopened by file stem with their own baseplate) and TEMPLATES. A template gets the retail `<Name>-N` title and a `Custommap_N` stem, so a new map no longer reopens or clobbers a project and a baseplate change no longer fails. | `ugc_editor_menu.*`, `ugc_project_repository.*` (`generate_ugc_map_title`/`generate_ugc_map_filename`), nfm `begin_ugc_editor_loading` |
| UGC-3 | Create Match has SAVED_MAPS and SUBSCRIBED_MAPS categories, filtered by the selected mode's code in the sidecar `tags`, with the author shown. Hosting copies the `.vxl/.txt/.ugc/.png` into the session and writes `[world] maps_path`. | `create_match_menu.*`, `create_match_presentation.cpp`, `local_server_process.*` (`custom_map_files`) |
| Save | F10 `quick_save` (host) and the Esc SAVE / Save-before-quit now send UGCMessage(100) `UGC_CONVERT_TO_GAME`. The server flushes VXL + sidecar and answers LocalisedMessage `UGC_MAP_SAVE_SUCCESSFULLY`/`UGC_MAP_SAVE_ERROR`, which opens the pause-menu message box (a 20 s timeout counts as an error). A project with no preview yet uploads the overhead map first (packet 102). | nfm `request_ugc_save`, `finish_ugc_save`, `pump_ugc_save` |
| Preview | Map Preview "Set" opens the ScreenshotHud: SCREENSHOT_HUD_TEXT, LMB captures the view on a HUD-less frame (centre square box-filtered to 512), RMB takes the overhead map, the preview shows bottom-right (30% of height, black 100/255 box, 5 px padding), and Esc returns to the settings with the draft kept. **Deviation:** retail parks the player behind a fly camera; native keeps first-person movement but hides the held tool and HUD. | nfm `capture_ugc_overhead_preview`, `ugc_screenshot_hud_draw_list`, `consume_ugc_preview_capture`; `ugc_preview_writer` (`ugc_screenshot_preview_rgba`, `encode_png_rgba8`, `write_ugc_preview_png`); `TutorialWorldSession::set_view_model_suppressed` |
| UGC-13 | The preview is pending until Apply (upload 102 + local mirror) and discarded on Cancel or when the settings reopen. | nfm `commit_pending_ugc_preview` |
| Loader | The editor host's loader MAP tab shows the project's own png (from the local catalog). A remote guest keeps the baseplate art, because packet 102 only arrives after spawn. | `MatchLoadingModel::set_map_preview_override`, nfm `apply_ugc_loader_preview` |
| UGC-4 | Tab in the Map Creator draws the retail Status screen (Players, Map Config: MODE + UGC_MAP_NAME, Objectives). The HUD draws the top-left "Incomplete Objectives" list (30 px transparent rows, sized to its rows), and the host's settings hint now sits 25 px under it. | new `ugc_status_presentation.{hpp,cpp}`; nfm `tutorial_world_draw_list`, `append_ugc_game_settings_hint` |
| UGC-5 | Markers are drawn and pointable only in the edited mode or MODE_NORMAL. The mode of a 97 echo follows `get_ugc_mode` (crates → normal, bomb → OCC, zones → current mode). A removal prefers a marker visible in the edited mode. | `world::ugc_item_edit_mode`/`ugc_item_visible_in_mode` (entity_catalog.hpp); nfm 97 handler, `sync_ugc_item_presentations` |
| UGC-6 | Packet 12 no longer overwrites `ugc_mode`, and Apply no longer sets it locally. Packet 68 with a new mode runs the retail sequence: adopt the mode, clear the UGC tools and resend packet 13 with an empty list, show "Game mode changed: X" (UGC_GAMEMODE_CHANGED + MODE_MAP_TITLES), and refresh an open Game Data screen. | nfm `apply_ugc_mode_change` |
| UGC-7 | No client change needed: the server now sends FogColor(74) to the host too, and the existing 74 path applies it. | server |
| UGC-8 | `VxlMap` keeps per-16³-chunk solid counts and `is_space_to_add_blocks()` (≥ 2,800,000 solids AND ≥ 3,200 chunks), consulted only in the Map Creator. The block ghost posts BLOCK_PLACE_UGC_CAPACITY. The construct ghost turns red and its commit plays build_error. The UGC snowblower (48) refuses to fire and shows the message while held. | `vxl_map.*`, nfm `ugc_space_to_add_blocks` |
| UGC-9 | The paintbrush RMB spray loop (`ugc_colour_spraying`, tool 43 `tool_loop`) starts at volume 0, fades in over 0.3 s and out over 0.5 s, and closes silently when the tool is put away; remote players are driven by the WorldUpdate secondary bit. The single-block cue is the server's PlaySound 47. | nfm `advance_paint_spray_audio` |
| UGC-10 | The editor minimap shows each visible spawn/base marker as a zone of its UGC_ZONE_SIZES footprint (±5/12/20) in its team colour with the spawn or base icon. | nfm minimap sync |
| UGC-11 | The marker ghost and placement need the top face of the pointed block (a side or bottom face refuses with build_error); pointing at an existing marker still works from any face. | `TutorialWorldSession::ugc_marker_cell` |
| UGC-12 / UGC-14 | Server side (removal tolerance; `Untitled UGC`/`Undescribed UGC`, and tags = every publishable mode). The client publish flow reads the sidecar and falls back to "Undescribed UGC". | server; `revival_identity.cpp` |
| Crash fix | Entering the Map Creator crashed the frontend (`single-line text rasterization does not accept line breaks`) on the multi-line UGC_HELP_WELCOME. `GameHudModel::set_help_messages` now splits messages on their newlines like HelpPanel.set_text. The crash was already present in the round-4 build. | `game_hud.cpp` |

### Tutorial (offline session)

| # | Change |
|---|---|
| T1 | Completing the lesson plays `tutorial_complete` (PlaySound 27), runs the tutorial-corner 10 s countdown, and returns to the menu at zero, like Esc > Disconnect. |
| T2 | Every destroyed target shows "Target destroyed. {0} left." (TUTORIAL_DESTROY_TARGET, override). |
| T3 | The colour picker is off offline (the retail tutorial playlist sets `RULE_ENABLE_COLOUR_PICKER OFF`). |
| T4 | Hands are empty until SHOOTING; the pistol comes at SHOOTING; pistol, block tool and spade come at CLIMB, with the spade equipped (the server equips the last item the same way). |
| T5 | CLIMB completes on the tower top: within 9.5 horizontally of lane-local (118.5, 51.5) and z ≤ 200 (Training.vxl dome top z 193, ledge ring z 207, the same in all 12 lanes). This replaces the "two built blocks" rule on both sides. **Reconstructed.** |
| T6 | The invented offline SCORE box is hidden. |
| T7 | The one-hit disc rule is kept and is now shared with the server. **Reconstructed.** |
| T9 | `tests/data/tutorial_script.json` is a byte-identical copy of the server's `tests/fixtures/tutorial_script.json`, pinned by `aos_tutorial_script_tests`. |

Code: `tutorial_lessons.{hpp,cpp}` (`on_tower_top`, `loadout(stage)`, named gate constants), `tutorial_session.cpp`, nfm `pump_offline_tutorial`.

### Tests

- New: `aos_ugc_round5_tests` (marker modes, capacity counters, ScreenshotHud crop/scale + PNG encode, Status Tab and HUD panel) and `aos_tutorial_script_tests`.
- Extended: `aos_ugc_editor_menu_tests`, `aos_ugc_project_repository_tests`, `aos_create_match_menu_tests`, `aos_local_server_process_tests` and `aos_tutorial_session_tests`.

### Build, tests and live check

- Build: `out/build/r5-ugc` (native-dev preset, VS 18 / MSVC 14.51, Ninja Multi-Config, RelWithDebInfo, warnings as errors, `--parallel 3`). It builds clean.
- `ctest -C RelWithDebInfo`: 153/154 passed. The one failure is `aos_hud_layout_tests` ("status tool uses truncated global image scale"). That is the parallel R5-HUD pass's work in progress and has nothing to do with this round. An earlier run also showed reds in `match_overlays`, `particle_system`, `gameplay_debug_lab`, `weapon_catalog_contract_check` and `shadow_stability`, all in other agents' areas; they were green on this run.
- Live check against an isolated `run_map_creator.py` on UDP 27150 (Desert, CTF), with offline identity `ABTester`:
  - the loader, Construct Library and editor join all work;
  - the HUD "Incomplete Objectives" panel draws the seven CTF rows;
  - Tab shows the Status screen (Map Config Mode = Capture the Flag, Players, Objectives).
  - The round-4 build crashed on the same join with the UGC_HELP_WELCOME line-break error; this build does not.
- Not checked live: host-only paths (F10/SAVE flush, ScreenshotHud, the settings Apply that triggers the mode-change banner and fog) and marker placement. Those need the client to launch its own editor from the Map Creator lobby. They are covered by unit tests here and by server tests (`tests/test_ugc_round5.py`).
- Diagnostics: the two `failed to rasterize` errors in nfm now name the font, position and size.

## Round 5: rendering (2026-09-29)

Source evidence: the live A/B of 2026-09-29 (`scratchpad/audit4/ab_live.md`, `vr/shots/`), the recovered vxl.pyd kernels (`audit4/ida_vxl.md`) and fresh IDA reads of `vxl.pyd` (0x10030B60 mesher normal codes), `kv6.pyd` (sub_1000DC40 / sub_1000E120), `draw.pyd` (sub_10001730 quad corners) and `gameScene.pyd` (Grenade.update, GameScene.update spot shadows). Build `out/build/r5-render`. Before/after evidence: `scratchpad/vr/shots_before_r5/` (round-4 native vs retail) and `scratchpad/r5evidence/` (offline `aos_retail_scene_capture` renders of the r5 build at the A/B poses, next to the retail frames).

| Item | Status |
|------|--------|
| World face shading inverted (A/B #1) | **Fixed.** The light was right; the NORMALS were wrong. `vxl.pyd` sub_10030B60 writes the side-face normal codes with x and y exchanged: +y face -> code 88 (GL +x), -y -> 80 (GL -x), +x -> 148 (GL +z), -x -> 20 (GL -z); top 100 (GL +y), bottom 68. `map_vert` decodes the code as-is, so the key light (-0.69, 0.30, 0) lights the -y faces, the same side the diagonal sun-shadow walk (y-1, z-1) comes from. `fs_world` retail terrain branch now uses `(n.y, -n.z, n.x)` instead of `(n.x, -n.z, n.y)`. A voxel that holds a static light has its codes inverted (colour-entry flag +4 = `v275`): the mesher tags it with colour2 alpha 0xC0 and `fs_world` negates the normal. KV6 models keep the plain basis. Verified offline at the four compass poses (`r5evidence/pair_compass_0.png`, `_90`): the lit/dark faces now match retail. `aos_block_shading_render_tests` oracle updated to the swapped terrain basis. |
| Map/flare static light (P3-13, A/B #2) | **Fixed (Retail tier).** New `StaticLightField::strongest()` = retail 0x10022360 selection (single light, `att = clamp(1 - d^2/r^2)` from the light voxel's centre, later light wins ties). With `ChunkMesherConfig::retail_static_light_kernel` the mesher bakes `rgb * att * max(0, N.L)` (N = retail's swapped normal code, L to the light centre, at the vertex itself) into colour2 RGB and writes `w = att` (0 if N.L <= 0) into `directional_influence`. `vs_world` adds it to the truncated baked colour (`floor(min(baked + light, 1) * 255) / 255`), and the existing `mix(dirLit, colour, w)` in the retail branch fades the directional lights out at lit vertices. Enhanced tiers keep the old summed field. The live remesher picks the kernel from `retail_look_active()`, and a tier switch re-meshes the chunks that differ (`requeue_chunks_for_tier_change`). Test: `aos_chunk_mesher_tests` (att, N.L, w, swapped side normal). **Live re-check owed** in the MayanJungle temple (`tdm_indoor`): the harness could not be re-run (see below). |
| Night-map neon too bright (A/B #20) | **Fixed (Retail tier).** The Retail tier no longer meshes with the authored emissive palette (which re-tinted TokyoNeon/CityOfChicago fixtures to a saturated "surface" colour and suppressed their shading). `TutorialWorldBootstrap::set_retail_look` and the live remesher both honour it; the emissive spill volume is still built for the enhanced tiers. Street lamps are static lights and now use the retail kernel above. |
| Dug terrain colour (P3-11) | **Fixed.** `VxlMap::generate_ground_color_table` ports 0x1001D860 exactly (first row fills 0..z0, later rows interpolate from the cursor, the tail writes cursor-1..238, 239 keeps its value, rows z >= 240 are bounded). Loader-filled interior cells are marked in a per-voxel implicit bitset (+7.8 MB). With a table installed, `color()` of an implicit cell is `table[z] + 0x010101 * j` (unclamped carry, j = stable position hash & 3, none at x == 0, y == 0, z == 239); writing a colour makes the cell explicit. The frontend installs the InitialInfo rows when the map is handed over and on SetGroundColors(118) (`apply_ground_color_table`). No table (offline tutorial): legacy inherited colour. Test: `aos_vxl_tests`. |
| Explosions (A/B #9) | **Fixed.** (1) Particle quads were half size: draw.pyd sub_10001730 builds corners `(c - 0.5) * 2 * size`, so `size` is a half-extent; the shared quad is now [-1, 1]. Eye-tuned enhanced-only emitters (block placement bloom, jetpack-death thruster, weapon muzzle, block-sucker debris, the asset-failure chunk fallback) were halved to keep their look. (2) Hand grenades emit 8 glow blocks (Grenade.update 0x100AE790); only the GLGrenade (tool 55, ExplodeOnImpactEntity) emits 4. With 8 parents the glow smoke-trail children double too, which is most of retail's fluffy fire along the arcs. (3) Air-burst debris is black (get_point on air = 0), not grey. The rocket trail keeps its remaining-life alpha (pinned by the particle tests; the research note that particle_frag never fades was not adopted). |
| View-model per-voxel noise / embossed cross (A/B #13) | **Fixed.** kv6.pyd writes the byte-7 table normal but every non-billboard path then calls sub_1000DC40 (or sub_1000E120 for team-colour groups), which overwrites each corner normal with `normalize(normalize(P - C) + F)`: P the render-space corner, F the face normal, C the unscaled model centre `((xsiz>>1) - px, -((zsiz>>1) - pz), (ysiz>>1) - py)`. `Kv6Model::mesh` now writes that, so held blocks/spades read flat with retail's gentle radial gradient; the slab6 per-voxel noise and the 255-sentinel cross on block.kv6 are gone. Enhanced tiers ignore this attribute. `aos_model_quality_tests` updated. |
| Player drop shadow (A/B #8) | **Implemented.** Retail does draw spot shadows: GameScene.update feeds every live non-spectator character (local player included) into `spot_shadow_pos_list`; vxl.pyd create_spot_shadows lowers the point by `SPOT_SHADOW_RAY_CAST_CHARACTER_HEIGHT - 1`, scans at most 10 cells down and fades with the drop; `spot_shadow.tga` is drawn depth-tested with no depth write. `world::character_spot_shadow` + `entity_spot_shadow_draws` now add one 1-block decal per character (tutorial and network). Simplification: one quad at the centre column; retail splits the square into up to four per-column sub-quads at ledges. |
| Snowke ring (V10) | **Fixed.** `emit_snowke_ring`: 6 puffs at 60 degrees, `floor(pos) + (sin*r + 0.5, cos*r + 0.5, +1)`, size uniform(4, 7) x 0.1, caller colour (snow block colour / impact colour). `emit_smoke_ring(snowke=true)` forwards to it. Test: `aos_particle_system_tests`. |
| Live remesh on the main thread (perf) | **Improved.** `pump_live_chunk_remeshes` meshes a batch in parallel (head on the main thread + up to 3 `std::async` lanes, hw-2 capped at 4) while the map is quiescent, then uploads FIFO on the main thread; a started batch is always uploaded before the 2 ms deadline check. The mesher itself memoises retail sun light per chunk (`SunLightCache`, lazily filled 18x18x240) and computes AO/edge atlas codes only for emitted faces. A true off-thread remesh with a map snapshot was not attempted. |
| Network dirty-chunk marking (perf/visual) | **Fixed.** `Protocol168TerrainReplica::record` marks the ChunkTracker neighbourhood: x +-1, y -1 .. +10 (diagonal chunks and the +y sun-light reach), with an O(1) queued bitmap instead of `std::find` per cell. |
| World-model draws (perf) | **Improved.** Each world-model slot stores a model-space bounding sphere; the main pass skips parts entirely behind the eye plane or past the fog end. `WorldRenderer::set_model_culling(false)` for harnesses that replace the world view after submit (the shadow-stability, block-shading and placement render tests). Per-draw uniforms are unchanged. |
| Particle draw list (perf) | **Fixed.** `build_draw_list` buckets the pool once instead of 54 full 4096-slot scans; batch and painter order are unchanged. |
| Compact terrain vertex / 16-bit indices | **Not done.** Needs a layout + shader migration across all three vertex producers; left for a dedicated pass. |

Tests: full ctest on `out/build/r5-render` (see the report for the final count; other rounds' in-progress tests may fail independently). Live A/B re-run: **blocked this round** - another agent was running the same visual harness (shared retail client, `vr/state.json` and UDP 27140) at the same time, so the retail player never spawned. Re-run `vr/r5_run.py` (TDM world scenes + CTF/VIP night maps) once the harness is free; `vr/shots_before_r5/` holds the round-4 baseline. Steam's registration is overwritten whenever the retail client runs.

## Round 5: HUD & menus (2026-09-29)

Evidence: the live retail-vs-native A/B (`scratchpad/audit4/ab_live.md`, `vr/shots/`), the retail spec (`ui_modes/retail_ui_spec.md`), headless idalib on private copies of `hud.pyd` / `gameScene.pyd`, and the retail tracer console. Verified afterwards with native-only captures against an isolated server (`scratchpad/vr_r5/n_check.py`: focused input + PrintWindow, no retail client needed) plus a partial A/B re-run (clicks were unreliable while other agents shared the harness).

| # | Item | Result |
|---|---|---|
| 1 | Team/class select backdrop | **Fixed.** Retail's SelectTeam/SelectClass/LoadingMenu are menus over the live GameScene; before `create_player` the camera keeps CameraManager's constructor pose, read live from the retail console: position (256, 256, 0), orientation (0, 0, 0). The native renders the loaded map from that pose behind `game_loading`, `change_team` and `class_selection` (new branch in `render_frame`); the flat cyan sprites are gone. |
| 1b | Loader backdrop | **Fixed.** MenuScene blits its splash over the whole window (no 16/12 px black strips) and fades `background_alpha` out over 1 s once the map is shown, revealing the same world pose. |
| 2 | Minimap/full-map water | **Fixed.** Open water is the z=239 bed, which the VXL finaliser gives one map-wide colour: the ground table's last row (stock (40, 54, 64)). `minimap_overview.cpp` uses `VxlMap::ground_table_rgb(239)` for bed columns, and the overview is rebuilt whenever the ground table is (re)applied. Live: slate sea on Tokyo Neon, Mayan Jungle and Dragon Island. |
| 2b | Full map above the HUD | **Fixed.** Retail `HUD.draw` calls `minimap.draw()` after the health bar, tools, HeadCount, score and feeds. The full map is now appended after the kill feed (the corner minimap keeps its place); the A-H letters cover the top bar. |
| 3 | Loader MODE title / captions | **Fixed.** `mode_text` = `strings.get_by_id(mode_name with CLASSIC_ prefix).upper()`, Spades 38 white in (240, 436, 320x50), two black drop shadows (2 and 3 px) and a 2 px outline. Captions use `retail_wrapped_lines` (Spades 20, shrunk until 35 px, retail boxes), so stock captions are one smaller line. |
| 3b | BACK, START glow, SCORES headers, map title | **Fixed:** BACK draws `back_icon` plus the yellow (180, 165, 75) label like SelectTeam; the SCORES `red_header` caps keep their full 40 px torn ends; the map title has retail's 3 px shadow and 2 px outline. **START glow was already retail:** `TextButton` toggles `add_glow` every 0.4 s and the A/B frames caught opposite phases. |
| 4 | Death screen HUD | **Fixed.** New `GameHudPresentationContext::character_widgets_visible` (false while the local character is dead or the death/jetpack-corpse camera runs) hides the health bar, tool/ammo panels, intel icon, crosshair, tool strip and palette; score, HeadCount, minimap and feeds stay. |
| 5 | End screen | **Fixed.** The title is ViewGameStats' class title `GameStats` upper-cased (IDA: `draw` uses `mode_text` if set, else `title.upper()`; `per_game_initilize` clears `mode_text`). The framed level screenshot is shown only for a real authored StateData camera (our servers send the (0,0,0) placeholder, which drew an invented thumbnail). **Rollover was client-side.** The server sends 72 -> 53/73 -> dwell -> 52 identically to both clients; the maintained retail client opens LoadingMenu on 52 (session_transition_patch) while the native held ViewGameStats until the server's ack timeout closed ENet. The native now switches to the loader on packet 52 (reconnect and teardown unchanged); live: loader at 52, reconnect about 1.3 s later. No server change. |
| 6 | CTF world markers | **Fixed.** The base zones (43, icon 6) were received, but their billboards were 12 px glyphs (the 256 px canvases hold a ~74 px icon) and off-view ones went to a screen-edge pointer under the minimap. Now, per `MinimapBillboard.render`, off-view objectives are clamped onto the pi/6 cone (`clamp_point_to_cone`) and draw icon plus pointer there; size follows retail's distance scale (initial within 20 blocks, 0.6 beyond 100) at a ~40 px visible glyph. The A/B "intel pins" were these base pins; our CTF server sends no packet-41 intel billboards. |
| 7 | Tool crosshair | **Fixed.** `get_accuracy` exists on `Weapon` only; plain `Tool` subclasses (spade and other DiggingTools, BlockTool, GrenadeTool, PrefabTool) keep the 1 px minimum, retail's small square (18 px extent). Corners are pixel-snapped (fractional radii turned the 1 px art grey). |
| 8 | Gangster arm / portrait | **Not a bug.** VIP/TC classes are random per player: the native was Gangster 1 (team-colour sleeve, blond face) and retail Gangster 3 (white sleeve, blue cap); each client's arms and portrait match its own class. |
| 9 | Roster row colours | **Fixed.** `draw_player_list` reuses its `color` local: the first player row is the UI team colour, then each living player overwrites it with `blend(team.color, white, 0.4)`, so later rows are whitened. Applies to SelectTeam, ChangeTeam, ViewScores and the kick screen. |
| 10 | Server browser "Demolition!" | **Fixed in the client; master bug noted.** The live AoSPlay list sends `mode_tla: "dem"` for every server because the master reads the trailing Steam `mode=0001` (SERVERMODE_PUBLIC) tag, while the revival `mode=0008` etc. is also in `tags`. `server_discovery.cpp` prefers the non-category gameplay tag when the tags disagree (a real Demolition server advertises only 0001). The master (aos_revival) should derive `mode_tla` from the revival tag. |
| 11 | Parachute / prefab icons, NOT ATTACHED, empty magazine | **Fixed.** TOOL_IMAGES load at scale 1.0 (parachute/disguise icons 49.5 px); `prefab_cost_icon` loads at 0.64 (211 px x 0.25). Floating prefab ghosts post `BLOCK_PLACE_FAIL_NOT_ATTACHED` (`get_prefab_ghost_position(check_world_touch)`). `HUD.update_ammo` (IDA 0x1008A930) picks yellow when `get_has_enough_ammo()` **or** the reserve is > 0, so RPG 0/3 stays yellow. |
| 12 | Minor | **Fixed:** the palette selector box shows only while `selector_active` (arrow-key pick; a world colour pick hides it; the 2 Hz blink colours are unrecovered, white kept); ballot text is MENU_FONT cream; inactive settings tabs are full-strength cream (they were dimmed to 70%, which read as a greyed GRAPHICS tab; retail's in-game graphics rows stay disabled via SETTINGS_GRAPHICS_DISABLED_MESSAGE); KeyDisplay 1/2 hides with its locked team button (Zombie); CONNECT VIA IP draws no caret over the placeholder. |
| 13 | JOIN_TEAM / mode title case | **Fixed.** Team buttons use `strings.JOIN_TEAM.format(name)`; the ViewScores mode title is drawn raw (only the SCORES fallback is upper-cased). |
| 14 | Chat length | **Fixed.** ChatMessage(49) accepts up to 1024 bytes; chat lines are bounded at 800 bytes (200 UTF-8 characters) instead of 200 bytes. |
| 15 | HUD scale | **Added.** `settings.toml [graphics] hud_scale` (default 1.0 = retail raw pixels; 1-4; 0 = auto `floor(height/1080)`): the HUD, chat and ballot are laid out in a window `scale` times smaller and magnified back, so retail proportions and anchors hold on 4K/Retina. settings.toml only (no menu row); Defaults preserves it. |
| 16 | Construct choices | **Fixed.** `class_selection_has_choices` also returns true when the construct list (flare tile included) is longer than the default constructs. |

Tests: `out/build/r5-hud` (native-dev, VS 18, warnings as errors): **154/154 passed**. Updated or added expectations: loading captions and mode title, JOIN_TEAM label, 1 px melee reticle, gameplay mode tags, 200-character chat, prefab icon 52.75 px, status icon 49.5 px, objective cone clamp and distance scale.

Still open / VERIFY:
- Team-select/loader world backdrop: the camera pose is exact (console), but whether retail darkens the scene behind LoadingMenu was not isolated; the side-by-side CTF/Zombie captures look alike.
- Billboard pixel size is calibrated from the capture (~40 px glyph at 800x600), not from the recovered `0.02 * scale` world transform; the pointer is drawn centred on the icon.
- End screen title: observed on TDM only; applied to every mode because `per_game_initilize` is mode-independent.
- Palette selector blink colours (two module tuples in `Palette.draw`) are not resolved.
- One native end-of-match run stalled for about 25 s (PrintWindow blocked; the server logged input starvation). It did not reproduce with `BATTLESPADES_FRAME_TRACE=1` or in the round-4 A/B, and it coincided with another agent killing harness processes on the shared port. Watch for it.

## Round 5: hotkeys & gadgets (2026-09-29)

Report under test: "the parachute and disguise hotkeys don't work, no hotkey was valid; disguise blocks are team coloured, they should be standard grey". Evidence: retail `aoslib/config.py` (CONTROL_DEFAULT), `scenes/frontend/controlsTab.py`, `weapons/{disguiseTool,dynamiteWeapon,c4Weapon,intelTool}.py`, `shared/constants.py` (CLASS_ITEMS, tool ids, A1637/A1754 ranges), and headless IDA of `character.pyd` (`DisguiseBlocks.__init__` 0x1000FAF0, `DisguiseBlocks.draw` 0x10010020, `Character.set_disguise_model` 0x100152F0, `Character.set_team` 0x10025770, `Character.set_block_color` 0x100263D0) and `hud.pyd` (`Palette.set_selector` 0x100DBED0). Built in `out/build/r5-keys` (native-dev, VS 18, warnings as errors), live against `run_validation_server.py --port 27160` with an offline identity.

### Key map (retail vs native)

Retail binds: W/S/A/D, V sneak, LCtrl crouch, LShift sprint, SPACE jump, LMB fire/use (fixed), RMB aim (fixed; `aim` row is DOUBLE_KEY_BINDING), R reload, wheel = next tool (fixed), 1-9/0 = inventory slots (fixed), Y team chat, T global chat, M map (hold), TAB scores (hold), `.` change team, `,` change class, Esc menu, E pick colour (`weapon_custom`), F1-F3 map vote, K kick, toggle_hud unbound, F11 screenshot; UGC: X, H, arrows, Q, C, Z hover, F10. Native `retail_default_settings()` matches all of them, and the settings.toml files written by today's builds (round3, r5-hud) load with every binding intact (the R5 graphics keys did not disturb `[controls.bindings]`). There is **no** retail disguise or parachute key: disguise is a tool (select its slot, LMB; `DisguiseTool.use_custom` on E only picks the colour), and the parachute opens on an airborne SPACE press (Z is the UGC hover key, which native also accepts, decision D3). Grenades and other throwables have no quick keys in retail.

### Live check of every in-match key and gadget

The desktop foreground was held all session by a Windows Firewall prompt raised by another agent's ctest (`aos_local_server_process_tests.exe`), so SendInput could not reach the client. Input was driven instead through a new developer hook, `BATTLESPADES_INPUT_SCRIPT=<file>` (`parse_input_script`, `sdl_window_module.hpp`): scripted key/mouse/window events are appended after the SDL queue each tick, so they take the same frontend path as real input. Results (server packet trace + client captures):

| Area | Result |
|---|---|
| Slots 1-9 | Select the combined HUD index (toolbar labels 1..8 match: e.g. Engineer 1 block, 2 pickaxe, 3 SMG, 4 turret, 5 disguise, 6-8 constructs); ClientData tool id follows. |
| All 5 TDM classes, default and alternative loadouts | Every tool fires or places: ShootPacket (pickaxe, spade, superspade, knife, machete, all guns incl. shotguns, auto shotgun, auto pistol), UseOrientedItem (grenade, RPG, drill, mine launcher, grenade launcher, sticky grenade), BlockLine, BuildPrefabAction (server refuses in spawn zones, by design), PlaceLandmine, PlaceRocketTurret, PlaceRadarStation, PlaceDynamite, PlaceC4 + DetonateC4 (RMB), BlockSuckerPacket, DisguisePacket. Dynamite/C4 need a face within 5 blocks (retail A1637/A1754); an aim 25 degrees down from a tower is out of range, as in retail. Miner starts with 0 blocks (retail MINER_STARTING_BLOCKS). |
| Disguise | Slot 5 + LMB: DisguisePacket(95), server "DISGUISE activated", WorldUpdate state bit 0x02, HUD disguise icon at (w-50, 135). |
| Parachute (Commando, equipment 72) | SPACE presses while walking off the tower: server `parachute open ... deploy`. Z sends the hover bit. Opening needs a 6-block drop and descent (server policy, docs/PARACHUTE.md). |
| R, E, RMB zoom | WeaponReload(76) sent; E picks the aimed block colour (SetColor); RMB zooms/secondary. |
| M, TAB, T/Y, Esc, `,` `.`, K, F1-F3 | Full map while held, scoreboard while held, chat opens/cancels, class and team menus open and close on the same key, kick list opens, vote ballot shows. |
| Focus transitions | Keys, LMB and tool switches still work after focus_lost/focus_gained, minimise then MAXIMIZED only, and minimise then focus only (the R5-PLATFORM `window_suspended` fix). |
| Tutorial | Lesson 1 grants no tools (number keys do nothing, as retail); M works. |

**Root cause of "no hotkey works": not reproducible in the current tree.** No key or gadget is broken on the frontend path, in settings persistence or on the server. The most likely cause (inferred, not reproduced on those binaries) is pre-R5: the round3/round4 builds the user tested stopped the local simulation and ClientData while `window_suspended`, and that flag was only cleared by RESTORED, so an alt-tab or minimise out of the then-default *exclusive* fullscreen that came back through MAXIMIZED/focus left the character frozen with every key dead. R5-PLATFORM fixed it (simulation keeps running; suspended clears on focus_gained/drawable_resized/maximized; borderless is now the default) and this pass verified the transitions above. **VERIFY with a real keyboard** once the firewall prompts are dismissed: physical SendInput was never delivered this session.

### Disguise colour: retail tints by the block colour (no change)

`DisguiseBlocks.__init__` builds `DisplayList(CHARACTER_DISGUISE_STANDING/CROUCHED, z_offset)` at size 0.1; `DisguiseBlocks.draw` calls `set_kv6_default_color(0, 0, 0)`, then `MODEL_SHADER.uniformf_loc(MODEL_SHADER_BLEND_COLOR_LOC, *character_owner.block_color_float)`, draws, and restores (1, 1, 1, 1). Both KV6s are all FCFCFC voxels, so the blocks show the owner's **block colour**. `Character.set_team` (pyx 667-672) calls `self.set_block_color(team.color, silent=True)` and only then halves `color`/`other_color` for the body, and `Palette.set_selector` is reached only from the arrow keys. So in retail a player who has not picked a colour (E eyedropper or palette) is disguised in the full **team** colour, and one who picked grey terrain is grey. Native does the same (`load_disguise_models` × SetColor block colour; E2's half intensity is not applied to the blocks). The team-coloured blocks the user saw are retail behaviour for players who never picked a colour (bots never do). Left unchanged pending the user's call. If the user still wants grey, the change is one line in `sync_remote_player_rig`: pass the grey 0x707070 instead of `player_color`. Note that this would also stop the E-picked terrain colour from reaching the blocks, which is the purpose of `DisguiseTool.use_custom`.

### Changes

- `include/battlespades/platform/sdl_window_module.hpp`, `src/platform/sdl_window_module.cpp`: `ScriptedInputStep`, `parse_input_script` and the `BATTLESPADES_INPUT_SCRIPT` injector (key, tap, click, button, look, wheel, window, quit). Off unless the variable is set.
- `tests/test_input_script.cpp` (`aos_input_script_tests`): delay accumulation, tap holds, click expansion, scripted-cursor buttons, look/wheel deltas, window transitions, unknown verbs, time ordering.

Tests: `out/build/r5-keys` **155/155 passed** (154 in the main run, plus `aos_local_server_process_tests` run separately because it raises a firewall prompt).

Still open / VERIFY:
- Physical keyboard/mouse input on a focused window (blocked this session, see above).
- Crate pickup, intel/bomb/diamond pickup and LMB drop (DropPickup 71) were not driven live; the adapter mapping (25/26/30 -> pickup 14/15/16) is covered by `aos_protocol168_weapon_action_adapter_tests`.
- `hud_scale`/borderless defaults from R5 apply to new installs; an old settings.toml keeps `fullscreen = true` and now gets borderless, which avoids the minimise path entirely.

## Round 5 integration (2026-09-29)

Five round-5 agents worked in the same tree at the same time: rendering, HUD & menus, Map Creator & tutorial, platform/audio/caches (with the server pellet spread), and hotkeys & gadgets. This pass audited the merged tree, rebuilt it from scratch in `out/build/round5`, ran every suite and tried to re-run the visual A/B. Nothing was committed or deployed.

### Audit of claimed changes

Each claimed symbol was checked for its definition **and** its live call site or registration. Nothing was missing, duplicated or conflicting, and nothing had to be restored.

- **Rendering:**
  - `fs_world.sc` uses the swapped retail terrain basis `(normal.y, -normal.z, normal.x)`.
  - The mesher tags static-light voxels with colour2 alpha `0xC0`.
  - `StaticLightField::strongest` is used by `chunk_mesher.cpp`.
  - `retail_static_light_kernel` is set from `retail_look_active()` in the live remesher, next to `requeue_chunks_for_tier_change`.
  - `generate_ground_color_table` is present. `apply_ground_color_table` is called on map hand-over and on packet 118.
  - The particle quad is [-1, 1] (`world_renderer.cpp`, sub_10001730 note), and grenades emit 8 glow blocks.
  - The KV6 radial normal (`kv6_model.cpp`, sub_1000DC40) is in place.
  - `character_spot_shadow` and `entity_spot_shadow_draws` are wired.
  - `emit_snowke_ring` is called from `terrain_effects.cpp` and the frontend.
  - Also present and wired: `pump_live_chunk_remeshes`, `SunLightCache`, and `set_model_culling(false)` in the three render tests.
- **HUD & menus:**
  - `character_widgets_visible` runs from the frontend into `game_hud.cpp`.
  - `ground_table_rgb` is used by `minimap_overview.cpp`.
  - The cone clamp is in `objective_indicator.cpp`.
  - `retail_wrapped_lines` is present.
  - `hud_scale` is wired through the settings store, the session and the frontend.
  - The prefab path (`BLOCK_PLACE_FAIL_NOT_ATTACHED` / `check_world_touch`) and `class_selection_has_choices` are present.
- **Map Creator & tutorial:**
  - `ugc_status_presentation.cpp` is registered in `src/CMakeLists.txt`.
  - The `UGC_CONVERT_TO_GAME` save path (`request_ugc_save`/`finish_ugc_save`/`pump_ugc_save`) is wired.
  - Capacity: `is_space_to_add_blocks` and `ugc_space_to_add_blocks`.
  - Mode filter: `ugc_item_visible_in_mode` and `apply_ugc_mode_change`.
  - Also present: `advance_paint_spray_audio`, `ignored_ugc_entity_ids`, the preview capture/commit, `set_map_preview_override`, `generate_ugc_map_title` and `custom_map_files`.
  - `on_tower_top` is present.
  - `tests/data/tutorial_script.json` is byte-identical to the server's `tests/fixtures/tutorial_script.json`.
- **Platform, audio and caches:**
  - Window and frame: `live_world_presentation_allowed`/`live_world_simulation_allowed`, `FullscreenKind` (borderless), `bgfx_max_frame_latency`, `intermediate_frame_period`/`present_intermediate` and `CameraEyeInterpolator`.
  - Audio: `music_ambience_pcm_budget_bytes` (LRU), `request_named_buffer_async`, `tool_cue_reverb_send`, `rocket_explosion_tool` and `airstrike_explosion_volume`.
  - Caches: the `RemoteWeaponModelKey` struct key, `tinted_weapon_models`, and `present_players` (20 frontend call sites).
  - Also present: `advance_retail_remote_minigun_spin`, the ZombiePrefab branch and the `inspect()` tail check in `vxl_map.cpp`.
  - Server: `SNUB_PISTOL` spread is 0.01, and the single-pellet seeded spread is in PARITY_CHANGES §24.
- **Hotkeys:** the `BATTLESPADES_INPUT_SCRIPT` hook (`parse_input_script`) is present and `aos_input_script_tests` is registered.
- **Test registration:** every new test is registered: `aos_ugc_round5_tests`, `aos_tutorial_script_tests`, `aos_audio_cache_tests`, `aos_input_script_tests` and `aos_ugc_preview_writer_tests`.

### Build and tests

- **Build:** fresh configure and build in `out/build/round5`. Settings: native-dev preset, VS 18 (MSVC 14.51.36231), Ninja Multi-Config, RelWithDebInfo, `AOS_WARNINGS_AS_ERRORS=ON`, `--parallel 4`. All 736 steps built with no warnings or errors and needed no rerun. No access violations this pass.
- **ctest:** `ctest -C RelWithDebInfo -E aos_local_server_process_tests` gave **154/154 passed**. That includes `aos_hud_layout_tests`, which was red mid-round.
- **Firewall test:** `aos_local_server_process_tests` ran separately and last, and passed. Total: **155/155**.
  - It raises a Windows Firewall prompt because `udp_port_available` binds a UDP socket on `INADDR_ANY`. The prompt it raised was closed straight away.
  - The probe was left unchanged: a 127.0.0.1 bind would not match what the hosted server binds.
- **Executable:** `out/build/round5/src/RelWithDebInfo/BattleSpadesClient.exe`.

### Server

The full suite ran in three parts (`py -3.12 -m pytest -q -p no:cacheprovider`):

| Part | Result |
|---|---|
| Files sorted before `test_surface_corridor.py`, without the bot map matrix | 9544 passed, 1 skipped |
| The remaining files, without the bot map matrix | 485 passed |
| `test_bot_map_matrix.py` | 36 passed |

### Visual regression re-run: blocked

- **What blocked it:** two Windows Security (firewall) prompts left open by earlier round-5 test runs (PickerHost PIDs 9480 and 79416, raised at 06:26 and 08:11) held the foreground.
  - While they are in front, Windows UIPI rejects all synthetic input: `SendInput` returned 0, and `mouse_event`/`keybd_event` were dropped.
  - Neither `SetForegroundWindow` nor a title-bar click could move the foreground.
  - Neither client reacted to `PostMessage` clicks: not the native (SDL) window, and not the retail window.
  - Without input the harness cannot join, pose or open menus, so the full `run_all.py` was not run.
- **Harness changes** (`scratchpad/vr/vrlib.py`):
  - An input guard: synthetic key and click input is sent only while a game window is in the foreground, so keys can never land on a firewall prompt.
  - A PrintWindow capture mode (`VR_PRINTWINDOW=1`).
- **Re-captured scene:** only `menu_main` needs no input. It was re-captured with the round-5 exe through PrintWindow: **2.71% -> 1.09%**. What remains are the known native additions: the FRIENDS icon, LOGOUT and the welcome name.
- **Discarded captures:** the HUD agent's mid-round captures of the loader, team and class scenes were corrupted by another harness window overlapping the capture (they read 48-58%). They were moved to `vr/shots_r5hud_partial_corrupt/`, and those scenes were restored to their round-4 frames.
- **Reports:**
  - The round-4 report is kept as `vr/report_round4.html`, with its results in `vr/results_round4.json`.
  - The new `vr/report.html` carries a banner about the block and a round 4 -> round 5 table that lists only re-captured scenes.
- **To finish:** dismiss the two firewall prompts (or let the user choose), then:
  1. Run `py -3.12 scratchpad\vr\run_all.py --exe out\build\round5\src\RelWithDebInfo\BattleSpadesClient.exe`.
  2. Run `findings.py`, then `report.py`. The per-scene round-4 comparison is built in.

  Running the retail client overwrites Steam's registration.

### Still open

- **Full visual A/B on the round-5 exe** (see above). Until it runs, the round-5 visual fixes rest only on offline evidence and each agent's own partial checks:
  - face shading, static light, ground colours, explosions, KV6 normals and spot shadows: `scratchpad/r5evidence/`;
  - HUD items: the `vr_r5/n_check.py` native-only captures.
- **Live checks** owed by every round-5 section above still apply. Among them:
  - the MayanJungle temple static light;
  - alt-tab and minimise in borderless and exclusive fullscreen;
  - interpolation on a high-refresh panel;
  - physical keyboard input;
  - Map Creator host save and preview;
  - the minigun pivot.
- **Stale pytest:** a run started at 04:16 by an earlier agent (PID 87640/101668, about 880 MB) is still running and looks hung. It was not started by this pass and was left alone.

## Round 6: render (2026-09-29)

Source evidence: the round-5 A/B (`scratchpad/audit5/vr_round5.md`, `vr/shots/`, `nd_retail_grid.png` / `nd_native_grid.png`) and fresh headless-IDA reads of `vxl.pyd` (sub_10030B60, sub_10022360, sub_1000C5F0), `draw.pyd`, `gameScene.pyd`, `character.pyd`, `kv6.pyd` and `common.pyd`. Build `out/build/r6-render` (native-dev, VS 18, warnings as errors).

| Item | Status |
|------|--------|
| MayanJungle indoor static light (`tdm_indoor` 35%, `tdm_indoor_b` 43%) | **Fixed.** The round-5 kernel took N from map_vert's swapped (y, -z, x) reading of the normal code. The static-light kernel does not: sub_10022360 decodes the code with sub_1000C5F0 as x = (c>>6)&3, y = (c>>4)&3, z = (c>>2)&3 (minus 1), and under that decoder sub_10030B60's codes are the TRUE face normals (+y face 88 = GL +z = +y, +x face 148 = GL +x, +y top 100). So N.L = nx*dx + ny*dy + nz*dz, still negated for a light-source voxel. With the swap, walls facing ±x read the light's y offset: the temple wall facing the camera got N.L <= 0, so w = 0 and only its dark baked colour showed (black), and the lit walls picked up the wrong N.L (weaker). `chunk_mesher.cpp` fixed; `aos_chunk_mesher_tests` now checks that a light due west lights the -x face. Offline, same tool, retail-kernel tier with the four map flares (`aos_retail_scene_capture` new `flares=` option): `tdm_indoor` 59.9% -> 16.4%, `tdm_indoor_b` 59.0% -> 21.8% against the retail frames (the offline frames have no HUD or view model, which accounts for most of what remains). The wall facing the camera is now cream (100,101,80), where retail is (100,100,78). |
| Explosions | **Fixed.** (1) Per-source recipes from gameScene: hand grenade (Grenade.update 0x100AE790) 8 glow, 10 debris, velocity 1.3, size 5.0, lifetime 2.0. Landmine, dynamite, drill and C4: 8 / 10 / 1.5 / 5.0 / 2.0. Bomb (BombPickup.explode): 12 / 15 / 1.3 / 10 / 2.0. Rocket (Rocket.delete) and GL grenade: 8 or 4 / 10 / 1.0 / 10 / 1.0. Native had given hand grenades the rocket's size 10, which doubled the debris size. (2) The GLOW_SMOKE_TRAIL_SPAWN_POINT child (draw.pyd sub_10033C00, kwlist 0x1003F4E8) draws rand() in the order size (/5458.17 + 3), rotation (/3.835 + 160), start frame (%64 + 1), forward (!= 0). Its decay is -1, so the puff grows to 2x over its 1 s life, at 60 fps, spawned at the parent's pre-move position. Native had drawn a shrinking decay from rand(), run at 30 fps and picked forward with `&1`: half the puffs ran backwards and parked on a tiny frame, which read as thin straight lines (the "8-way star") and smoke that thinned early. (3) The rocket trail (which is also the RPG back-blast: rpgWeapon.shoot sends only the rocket) no longer fades vertex alpha. draw.pyd sub_1000BDD0 stores the colour once and never fades it. (4) Billboards are lifted by their half-extent (sub_10015770: GL y = -(z - 0.5 - size)). Test: `aos_particle_system_tests` (debris size, recipe counts, `glow_trail_children_grow_like_the_spawn_point`). |
| First-person sleeve band teal, not navy (constant red patch in every gameplay diff) | **Fixed.** Character.set_team (character.pyd 0x10025F37) stores `self.color = team * 0.5` (and `other_color` the same). draw_fps calls `set_kv6_default_color(*self.color)` before the fps arms (0x1005F76C) and before `view_weapon.draw_scaled` (0x10060BB1). Native passed the full team colour to the arm and view-weapon KV6s, so the band was exactly 2x retail: (25,70,84) where retail is (12,35,42). Now `world::retail_character_color(local_player_team_color())` in `load_tutorial_class_arms`, the scripted skin arms and `sync_sandbox_view_model`. `aos_ads_sight_render_tests --viewmodel` expects (22,59,90) for the default blue. |
| Gangster sleeve unshaded / too bright | **Not a bug.** The random gangster variant (`modes/vip.py` `random.choice`) differed again. This round retail was Gangster2, with sleeve KV6 colour (28,32,32), and native was Gangster3/4 with white (252,252,252). The same per-channel light factor (0.787, 0.848, 0.657, measured on the skin) predicts both clients' rendered sleeves from their own KV6 colours. |
| Prefab ghost shape (`tdm_vm_key6`) | **Fixed.** The ghost mesh and transform were right; the anchor was not. `world::prefab_ghost_position` ports PrefabManager.get_prefab_ghost_position (shared/prefabManager.py:211 = gameScene sub_1005B2A0): the PREFAB_DISTANCES band nearest the radius, the aim z capped at 239 before floor, `scan -= int(world_size / 2.0)` on the signed rotated size, the even-size compensation for player-orientation placement, `scan.z += int(-0.8 * size_z / 2.0)`, then lift one step at a time (move_point_towards_face BOTTOM, common.pyd sub_10007BE0) while the model intersects the world, stopping at the player's floor. `prefab_center = scan + int(ws / 2)`. For the even-sized ultrabarrier (6x14x6) the old centring put the anchor 1 block off horizontally at most yaws. The UGC editor path keeps rotate_prefab_centered. `ghost_model.z_offset = -1` is glPolygonOffset, not geometry. Test: `aos_prefab_placement_tests`. |

Tests: `out/build/r6-render`, full ctest **155/155 passed**.

Live A/B: **blocked.** A Windows Security (firewall) prompt (PickerHost PID 46400, raised 13:37 during this round's ctest run) held the foreground, so the harness's input guard suppressed every key. `run_all.py --only tdm` captured only the loader/team/class scenes, all of them invalid, and the world scenes failed (no character). Those frames are in `vr/shots_r6render_blocked/`. `vr/shots/` and `results.json` were restored to round 5 (backup `vr/shots_before_r6render/`, `results_before_r6render.json`). All harness processes exited. The figures above are offline.

### Still open

- Live re-run of `tdm_indoor*`, `tdm_nade*`, `tdm_fire_key4`, `tdm_vm_key6` and the arm band. Dismiss the firewall prompt first (the user's choice).
- Grenade debris colour: retail samples the block below (`get_point(x, y, z + 1)`); native samples the explosion cell (`tutorial_session.cpp`).
- Particle gravity should follow StateData gravity (`set_particle_gravity`); native uses 1.0.
- Airstrike recipe (3 / 5 / 1.5 / 5.0): no tool id in the impact. Sticky grenade (57) and mine launcher (58) recipes were not recovered and fall back to the rocket's.
- Rocket trail anchor: retail uses rocket position + 0.5 z; native uses the exhaust point.
- Other tinted emitters (block chips, crate and diamond twinkles, grave chunks) still fade alpha, which retail never does.
- First-person `use_other_team_color` tools (`self.other_color`) are not wired.
- Prefab "attached" check: retail judges it before the intersect lift; native judges it at the final anchor.
- UGC prefab yaw: retail uses the override only; native still adds the facing.

## Round 6: HUD & input (2026-09-29)

Evidence: round-5 A/B frames (`scratchpad/vr/shots/`), retail bytecode (`pyz/aoslib.text`, `aoslib.gui`, `aoslib.images`, `aoslib.scenes.frontend.serverMenu`, `aoslib.scenes.ingame_menus.loadingMenu`), and headless idalib on private copies of `aoslib.font.pyd`, `aoslib.draw.pyd` and `hud.pyd`.

The full VR harness could not be re-run. A foreground-locked "Windows Security" dialog (PickerHost, open since 13:37) blocked every SendInput step, and it was not ours to dismiss. Verification used two native-only rigs that need no foreground:

- `vr/r6_chute_script.py` and `r6_chute_script0.py`: `BATTLESPADES_INPUT_SCRIPT` taps, plus a server-side hook that counts jump frames.
- `vr/r6_ncap.py`: scripted clicks, PrintWindow captures, and the harness `diff_images` metric against the round-5 retail frames.

| # | Item | Result |
|---|---|---|
| 1 | Parachute short tap | **Fixed (input latch).** A key-down whose release lands in the same event poll was lost. `held_` is level-sampled, and the old code only latched jump offline. `TutorialWorldSession` now latches every action press (`press_latch_`) and the primary press until the next fixed tick consumes it. ClientData movement/action flags send the latched value for that one tick (`sent_held`); the offline primary edge is no longer cleared on release. On the server, `_update_parachute` receives the one-frame jump edge and deploys. **Measured** on the TDM server with a z=90 teleport. Old build, same-poll tap (0 ms hold): 0/8 deployed, 0 jump frames reached the server. New build: 8/8 deployed, 1 frame each. 16/50/80 ms taps: 12/12 on both builds (1/3/5 server frames). Harness diagnosis: the 80 ms SendInput tap came 0.1 s after `front()` activated the window, and that is where a frame stall merges down and up into one poll. The 150 ms press that worked spans several polls. |
| 2 | Loader captions | **Fixed.** `Font.get_char_height` is FTGL `FTSize::Height` (font.pyd `0x10001A70`): `y_ppem * (bbox.yMax - bbox.yMin) / units_per_EM`. That is the face's global box: 22.3 px for Spades 20, where the face line height is 17.5 px. It is exposed as `TextMetrics::retail_char_height_pixels`, and `retail_wrapped_lines` fitting uses it, so stock captions shrink onto one line as in retail. |
| 2b | START glow | **Fixed.** `TextButton.draw` blits `button_glow` after the button art, so the glow washes over it (retail's rivets fade). It was drawn underneath and hidden. The retail ring lands on the button edges: 1.25 x width and 2.6 x height, measured. The recovered `TextButton` formula (1.2 x width, 1.75 x height, centred `text_height / 2` below the top) does not reproduce the loader capture. Unresolved. |
| 2c | Loader layout | **Fixed, measured.** Tabs y 95..140 (46 px, both sprites); tab labels at (-5.5, -3.5) from the frame centre; LOADING title and the CHOOSE CLASS / CHOOSE TEAM / browser titles at retail's ink centre y = 43; MODE title 9 px lower. SCORES: rows at (83, 157) on a 26 px stride, 600 px wide; scroll column x 695, arrows at y 157 and 395. The black 125-alpha dimming box is removed; retail shows the map art at full brightness. |
| 2d | Map preview | **Fixed.** `images.load` shifts every `map_previews` texture's `tex_coords` by one vertex (`tex_coords[3:] + tex_coords[:3]`), which turns it 90 degrees clockwise. Native now does the same in the loader and the browser; Map Creator pngs are drawn as authored. `reset_map_previews` maps "City Of Chicago" to `midtownmassacre.png` (Chicago had no preview) and the UGC baseplates by short name. |
| 2e | Next-map loader backdrop | **Not reproduced.** `tdm2_*` backdrop samples equal `tdm_*`, and on TC/VIP the native is slightly brighter, not darker. |
| 3 | CTF billboards | **Recovered formula.** `MinimapZone.change_details` builds `MinimapBillboard(..., scale=2.5)`. `render` sets `s = 0.02 * scale * f`, where f = `initial_scale` within 20 blocks, 0.6 beyond 100, and linear in between. It then calls `set_variables(x, y, z - s / initial_scale, s)`. `aoslib.draw` spans the quad +-s on the camera axes (`0x10001730`) at 0.25 block. Screen size is therefore `2 s / (0.25 cos) * focal`, lifted `s / initial_scale` focal lengths. This replaces the 0.36 x focal capture calibration. It also explains the "inverted layers": the near base is much larger and higher than the far one, so the two icons no longer stack at one size. Colour is the local team's own base, not a layering bug. **VERIFY live** (CTF capture needs the harness). |
| 4 | Gangster portrait | **Not a HUD bug.** `images.class_icons` maps GANGSTER_1..4 (6..9) to `gangster1..4_icon_team*` exactly like native. Retail was Gangster 2 in both captures; native was Gangster 3 (VIP) and Gangster 4 (TC). The variant comes from the server's class assignment. **VERIFY** by reading both players' `class_id` in a VIP run. |
| 5a | BUILDING NOT ATTACHED / big text | **Fixed.** `big_text_frame` is loaded at `global_scale` 0.64 with `center=True`, and `draw_big_text` only scales y by the line count. One line is 37.1 px tall, not 58 (retail frame measured at 37 px, native 58). |
| 5b | TextButton labels | **Fixed.** Text box = width - 2 x `TEXT_BACKGROUND_SPACING` (14) by height - 2 x `UI_CONTROL_SPACING` (4). The face is 36 px above a 30 px box and 18 px otherwise, then `get_resized_font_and_formatted_text_to_fit_boundaries(..., 2)`: wrap and shrink with `get_char_height`. Applied to team-select buttons (JOIN SURVIVOR / JOIN BLUE now match retail width), settings CANCEL/DONE/DEFAULTS (the in-game 41 px buttons drop to 27 px as in retail), and browser REFRESH/FAVOURITE (17 px). New renderer support: `retail_wrapped_lines` + `retail_center` centres the wrapped block. |
| 5c | Server browser | **Fixed.** `ListGrid.draw` scales an over-wide cell down to its column width (`draw_text_with_alignment_and_size_validation`), so long names no longer spill into PLAYERS. FAVOURITE is `TextButton(image=favorite_star_button)`: the star is at `x + w/2 - text_width/2 - 2` and the text moves right by half the star. |
| 5d | Settings rows | **Intentional divergence.** Retail MAIN has no LANGUAGE or skin rows; these are native features (localisation, cosmetics). That accounts for most of `menu_options_main` 14% and `options_ingame`. Class screens: the remaining difference is world pose and capture timing, not chrome. |

Native-only diffs against the round-5 retail frames (same capture method for both builds, harness metric, threshold 40):

| Scene | Round 5 | Round 6 |
|---|---:|---:|
| `tdm_loader_map` | 14.7 | 11.8 |
| `tdm_loader_mode` | 16.0 | 13.8 |
| `tdm_loader_scores` | 17.3 | 10.4 |
| `menu_server_browser` | 9.8 | 8.9 |
| `tdm_team` | 6.9 | 6.6 |
| `menu_options_main` | 14.0 | 14.0 |

Tests: `out/build/r6-hud` (native-dev, VS 18, warnings as errors): **155/155 passed**. New or updated tests:

- sub-tick jump and primary taps are latched into exactly one tick and one ClientData;
- tab label offset;
- big-text frame height 58 x 0.64;
- billboard size and lift from the recovered quad.

Still open / VERIFY:

- A full `run_all.py --only menus,ctf,tdm,chute` once the Windows Security dialog is gone, especially:
  - the `tdm2_chute` 80 ms SendInput tap;
  - the CTF billboard size.
- The START glow geometry: the measured value differs from the recovered `TextButton` formula.

## Round 6 integration (2026-09-29)

This pass audited the merged round-6 tree, stopped the firewall prompt from the test suite, rebuilt from scratch in `out/build/round6` and re-ran the visual checks that need no foreground. Nothing was committed.

### Firewall prompt fix

- **Cause:** `aos_local_server_process_tests` starts an owned child, and `start()` calls `allocate_local_server_port`. Its probe `udp_port_available` binds UDP on `INADDR_ANY`. Every new build directory's copy of the test exe raised a "Windows Security" prompt, and that prompt took the foreground.
- **Fix:** a process-wide probe switch, `set_local_server_port_probe(LocalServerPortProbe)` in `local_server_process.hpp`.
  - The default, `all_interfaces`, keeps the product behaviour: probe `0.0.0.0`, which is what the hosted server binds. The product never calls the switch.
  - The test's `main` selects `loopback_only` (127.0.0.1). A loopback bind never prompts.
  - The fixture child path returns before the switch and binds nothing.
- **Result:** the test is back in the default `ctest` run, with no `-E` exclusion. No new PickerHost/"Windows Security" window appeared: the only two present were the ones from 13:37 and 13:40, before this pass.

### Audit of claimed changes

Every round-6 item is present in the merged tree and wired to a live call site:

- **Chunk mesher:** static-light N·L now uses the true decoded normal, `n0*dx + n1*dy + n2*dz` (sub_1000C5F0 note).
- **Particles:**
  - Per-source explosion recipes (`particle_effects.cpp`: Grenade.update, BombPickup).
  - The GLOW_SMOKE_TRAIL_SPAWN_POINT child draws rand() in the order /5458.17 + 3, /3.835 + 160, %64 + 1.
  - Billboards are lifted by their half-extent (`world_renderer.cpp`, sub_10015770).
- **First-person arms:** `retail_character_color(local_player_team_color())` at `native_frontend_module.cpp` 23846, 23996 and 24092.
- **Prefab ghost:** `world::prefab_ghost_position` is defined and used by the frontend.
- **Input latch:** `press_latch_`, `primary_press_latch_` and `sent_held` in `tutorial_session`.
- **Text:** `TextMetrics::retail_char_height_pixels` is used by `retail_wrapped_lines`.
- **Loader:**
  - The START glow uses 1.25 x 2.6 and is drawn over the button.
  - Map previews get the `tex_coords` rotation in the loader and the browser.
  - The `midtownmassacre` alias is in place.
- **CTF billboards:** the `initial_scale` formula is in `objective_indicator.cpp`.
- **Buttons:** labels are sized with `TEXT_BACKGROUND_SPACING` (change-team, settings, UGC).
- **Server browser:** `draw_text_with_alignment_and_size_validation` squash and `favourite_star_button`.
- **NOT ATTACHED frame:** `big_text_frame_height = 58 * 0.64`.

### Build and tests

- **Build:** fresh `out/build/round6`. Settings: native-dev, VS 18 (MSVC 14.51.36231), `VCPKG_INSTALLED_DIR=out/vcpkg_installed`, `AOS_WARNINGS_AS_ERRORS=ON`, `--parallel 4`. All 736 steps built with 0 warnings and no retries.
- **ctest:** full default run, `ctest -C RelWithDebInfo -j 4` with no exclusions: **155/155 passed**. `aos_local_server_process_tests` passed in 0.22 s.
- **Executable:** `out/build/round6/src/RelWithDebInfo/BattleSpadesClient.exe`.

### Visual regression

- **Full harness still blocked.** The two "Windows Security" prompts (PickerHost 46400 from 13:37 and 35460 from 13:40) are still open, and 46400 holds the foreground. So `run_all.py` was not run, and `vr/shots` and `results.json` still hold round 5.
- **PrintWindow rigs instead.** These need no foreground. Each drove the round-6 exe, then the round-5 exe with the same method, so the two columns compare like with like.
  - `vr/r6_ncap.py`: loader, team, class and menus.
  - New `vr/r6i_world.py`: open-loop world poses. `look` deltas are precomputed from a calibration run (`r6i_cal.py`: spawn yaw 0 / pitch 0, 0.1 deg per count, server orientation checked at every shot, all within 0.4 deg). It reproduces the round-5 harness figures exactly on the round-5 exe.
  - New `vr/r6i_nade_bot.py`: the grenade is thrown by a bot. With the native as thrower, its own grenade is never shown.
- **Frames:** `vr/r6cap/{round6,w_round6,w_round5,nb_round6,nb_round5}/`.

| Scene | Round 5 (same rig) | Round 6 |
|---|---:|---:|
| `tdm_indoor` | 35.1 | **0.6** |
| `tdm_indoor_b` | 43.1 | **0.6** |
| `tdm_compass_0/90/180/270` | 1.4 / 1.6 / 1.6 / 1.6 | 0.7 / 0.6 / 0.6 / 0.9 |
| `tdm_view_wall / vista_b / down / sky / spawn` | 1.5 / 2.2 / 1.5 / 1.7 / 1.6 | 0.5 / 1.1 / 0.5 / 1.3 / 0.7 |
| `tdm_vm_key1..6` | 3.4 / 1.3 / 1.3 / 1.0 / 1.6 / 7.2 | 1.4 / 0.8 / 0.8 / 0.8 / 1.1 / **2.8** |
| `tdm_nade` (approx., random particles) | 20.6 | 17.8 |
| `tdm_nade_after` (approx.) | 25.0 | 24.2 |
| `tdm_loader_map / mode / scores` | 14.9 / 16.2 / 17.4 | 11.8 / 13.8 / 10.4 |
| `tdm_team` | 7.0 | 6.6 |
| `tdm_class` (world pose behind, rig artefact) | 18.3 | 17.8 |
| `tdm_options_ingame` (world behind, native LANGUAGE/skin rows) | 38.2 | 32.5 |
| `menu_options_main` | 14.1 | 14.1 |
| `menu_server_browser` | 9.8 | 8.9 |

- **Parachute (`vr/r6_chute_script*.py`, round-6 exe):**
  - 16, 50 and 80 ms taps: 12/12 deployed (1, 3 and 5 server jump frames).
  - Same-poll 0 ms taps: 7/7 deployed (1 frame each).
- **Explosion look:** grenade smoke now forms round puffs along the trails. The round-5 thin "star" lines are gone.

### Still open

- **Grenade debris:** native pieces are larger and more numerous near the burst, and more of them are black and white. Retail shows a few small dark chips and bright glow cubes. The other round-6 render leftovers also still apply: debris colour sampled from the block below, and particle gravity.
- **Fire scenes (`tdm_fire_key3/4`) unverified.** A scripted `button left down` in the match fired no shot on either exe: ammo was unchanged and there was no RPG back-blast. The fire frames need the real harness, or an input-script fix for in-game primary buttons.
- **`tdm2_chute` 80 ms SendInput tap, CTF billboards and the other modes' scenes** (VIP/TC/zombie, loaders, end-game) still need the full `run_all.py`.
  - To run it: close the two Windows Security prompts, then run `py -3.12 scratchpad\vr\run_all.py --exe out\build\round6\src\RelWithDebInfo\BattleSpadesClient.exe`, then `findings.py` and `report.py`.
  - Running the retail client overwrites Steam's registration.
- **Settings title:** in-game settings has no dark title plate behind SETTINGS, which retail draws.

## Round 7 (2026-09-29)

Evidence: the round-6 A/B (`scratchpad/audit6/vr_round6.md`, `vr/shots/`), headless idalib on private copies of the stock `hud.pyd`, `aoslib.draw.pyd`, `gameScene.pyd` and `world.pyd` (scripts in `scratchpad/r7/`), and a retail runtime probe through the tracer console (`vr/r7_bbprobe.py`). Build `out/build/round7` (native-dev, VS 18, warnings as errors).

| # | Item | Result |
|---|---|---|
| 1 | Objective billboards 2.5x too large (CTF bases, TC letters) | **Fixed.** `MinimapBillboard.__init__` (hud.pyd 0x10019CA0) stores `scale` and `initial_scale` from the same argument (2.5 for a zone, the 1.8 default for `Minimap.add_billboard`), so the round-6 formula looked right. But `MinimapZone.update` (0x1001FAC0) sets `billboard.scale = 1.0` once the zone is FULLSIZE (it animates `spawn_scale` while APPEARING). The runtime probe confirmed `scale 1.0, initial_scale 2.5` on every TC zone. `render` (0x1001B040) then uses `s = max(0.02 * scale * factor, min_scale 0.02)` with `factor` from `initial_scale`, lifted `s / initial_scale`. `project_objective_indicator` now takes `scale` and `initial_scale`: zones 1.0 / 2.5, packet billboards 1.8 / 1.8, and it applies `min_scale`. `ctf_carrying` pin: same size and position as retail. `tests/test_objective_indicator.cpp` is updated to the verified values. The zone's APPEARING/VANISHING scale animation is not ported. |
| 2 | Grenade explosion | **Partly fixed.** Particle gravity now follows StateData gravity (`ParticleSystem::set_gravity`, from `GameScene.process_packet_state_data` -> `set_particles_gravity(world.get_gravity())`; draw.pyd sub_1000BED0 `v.z += dt * g`). Live explosion debris takes the colour of the block below (`get_point(x, y, z + 1)`). The "fountain" arcs are **not a systematic gap**: a fresh retail airburst this round was radial like native's. The draw.pyd update, sphere sampler (sub_10003080) and particle constructor (sub_1000BDD0, `v = dir * ev - base`) match native. Remaining: native clouds sit a little lower and wider. |
| 3 | RPG back-blast | **Fixed (anchor).** Rocket.update (gameScene 0x100B2A60) spawns each trail puff at `world_object.position` with `z += 0.5`, not the rendered exhaust point. Both the packet-10 rockets and the CreateEntity(21/22) movers use it. The cloud is now as large as retail's and on the line of fire; `tdm_fire_key4` 17.4 -> 15.3. The rest is pose and timing. |
| 4 | VIP/TC sleeves and portrait | **Not a bug.** VIP and TC on the gangster map use `MAFIA_TEAM_CLASSES`, and the server picks the variant with `random.choice`. The class icons prove it: retail's VIP portrait is Gangster1 (orange quiff) and native's blue-cap portrait is Gangster2/3. Gangster3/4 wear white (252) sleeves. This round's `tc_zone` shows retail with the same pale sleeves. Pin the class in the harness to compare arms. |
| 5 | TC zone tiles on the minimap and full map | **Fixed.** `MinimapZone.draw` (hud.pyd 0x10021FA0) keeps `icon.scale` (packet icon_scale), sets `icon.opacity = int((sin(phase) * 0.4 + 0.6) * 255)` and clamps the icon centre into the map view (`clamp(centre, view_min, view_max)`), unclipped. Native had pulsed the icon SIZE with `sin(1.5 * phase)` and culled zones outside the view. Off-view letters now stack translucently on the minimap edge like retail. `tc_zone` 18.9 -> 15.8, `tc_hud` 5.4 -> 1.5. |
| 6 | Grenade explosion 1.4-3 dB quiet | **Open.** Retail `Grenade.update` calls `media.play_pitched(explode, pos=...)` with default volume and attenuation, which is exactly the native path (`explode.ogg`, DEFAULT_ATTENUATION 0.15, reference distance 1, inverse-clamped). The grenade is the only distant cue in `s_audio`, so the landing distance or the listener position is the likely cause. Needs a fixed-distance audio probe. |
| 7 | Loader START glow phase; BACK icon hue; other BACK buttons | **Fixed.** The TextButton glow (aoslib.gui) stamps `glow_timer` at `set_glow(True)` with `add_glow` False, then toggles on the first frame more than 0.4 s later. Native now starts dark when START enables and toggles every 25 frames, instead of running on a free frame counter. The BACK arrow takes the label's glColor, MENU_FONT_COLOR2 x 0.7: retail's arrow measures (149,115,16) = back_icon (235,204,77) x (162,145,55)/255, and native had drawn it grey-dimmed (148,130,44). This applies to the loader, team, class, UGC-loadout, create-, quick- and custom-match BACK buttons. UGC loadout and create match also get x+2.5, Spades 24 and the label at x+30. |
| 8 | Server: teleport kept `wade` | **Fixed (BattleSpades).** `Player.set_position` calls `_clear_transient_movement_state`. It clears `wade` and the per-fall canopy latch, and masks the mover's held wade until the mover next reports ground contact (`_wade_stale_after_teleport`). An armed deploy press survives. Test: `tests/test_parachute.py::test_teleport_out_of_water_does_not_refuse_the_next_canopy`. `aoslib/world.pyx` is untouched, because its pyd is loaded by the running 27015 server. |

Tests: full ctest **155/155** (`out/build/round7`). Server suite in three parts: 9545 passed / 1 skipped, 485 passed, 36 passed (`test_bot_map_matrix`).

VR (`run_all.py --only ctf,tc,vip,tdm`, then a fresh grenade burst capture), round 6 -> round 7:

| scene | r6 | r7 |
|---|---:|---:|
| ctf_carrying | 6.25 | 5.06 |
| ctf_hud | 0.92 | 0.60 |
| tc_hud | 5.38 | 1.54 |
| tc_zone | 18.86 | 15.81 |
| tc_fullmap | 3.68 | 3.06 |
| tc_scoreboard | 3.68 | 0.70 |
| vip_hud | 2.66 | 1.34 |
| tdm_fire_key4 | 17.42 | 15.28 |
| tdm_nade (approx) | 8.08 | 6.43 |
| tdm_nade_after (approx) | 22.93 | 25.13 |
| ctf/tc/vip/tdm_loader_mode | 14.7/16.2/15.3/17.5 | 12.5/12.1/11.1/17.3 |
| vip_fullmap / vip_scoreboard | 1.74 / 3.47 | 3.49 / 5.75 (1 px grid shift, harness) |

Harness note: two burst captures taken right after `run_all` showed the native blast far away and low. A fresh `s_start` + burst reproduced round 6's framing, and both round-6 and round-7 exes render the same there.
