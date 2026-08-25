# Playtester channel audit — 2026-08-24

Source: the complete 2026-08-05 through 2026-08-11 playtester export supplied
by the project owner. This is a current-state audit, not a copy of the old bug
list. `G:/AoSRevival/BattleSpades` was inspected only as a read-only protocol
oracle; no server file was changed.

## Meaning of the verdicts

- **Verified** — the current client path is present and has a deterministic
  regression test in the 102-test native suite.
- **Implemented / live-check** — code and deterministic state tests exist, but
  sound, animation, apparent size, or feel still needs a fresh retail/client
  A/B capture. This deliberately does not claim visual or audio parity from a
  unit test.
- **Retail-corrected** — the report's proposed solution conflicts with recovered
  retail behavior; changing the client as requested would introduce a bug.
- **Needs reproduction** — the report has no stable route, packet trace, or
  current reproduction. The surrounding subsystem is tested, but the report
  cannot honestly be called fixed from that alone.
- **Server-owned / open** — the observed result is decided before the client
  receives it. It cannot be fixed client-side without lying about authority.

The validation baseline for this audit is `ctest --preset native-dev
--output-on-failure`: **102/102 tests passed** on 2026-08-24.

## Complete issue ledger

| ID | Report | Current verdict | Current evidence / remaining check |
|---:|---|---|---|
| 1 | Add a selected/manual server to Favorites | **Verified** | Server-browser add, canonical endpoint persistence, restart reload, and Favorites filtering are covered by `aos_join_match_frontend_tests` and `aos_server_discovery_tests`. |
| 2 | Gamemode selection should play `secondary_menu_bed_001` | **Implemented / live-check** | The recovered cue is routed by the frontend and covered by frontend state tests; confirm perceived level once with speakers/headphones. |
| 3 | Character selection needs retail sounds | **Implemented / live-check** | Selection/confirm cue routing is present; final evidence must be an A/B audio capture. |
| 4 | In-game sound is flat | **Implemented / live-check** | OpenAL uses listener-relative spatialization, distance rolloff, raycast occlusion, source ownership, and bounded voices. Audio mapping and remote-character suites pass; mix taste still requires live A/B. |
| 5 | Teleporting/rollback while moving | **Fixed in audit; public-network soak recommended** | The fixed 16-packet FIFO drain reproduced a 1.497-block rollback at 600 ms RTT/100 ms jitter, and the network session incorrectly discarded native boxclip displacement when a frame both jumped and climbed. Adaptive ordered draining plus jump/climb preservation were then exercised across eight independent Mayan Jungle runs at 600 ms RTT/100 ms jitter: 3,418 authoritative ACKs, two moving jumps per run, terrain placement, zero input mismatches, and 0.000 position/velocity correction in every run. Movement, frontend, and tutorial-session tests pass. |
| 6 | Weapons switch without player input | **Verified** | Unsolicited/stale tool updates are filtered by authoritative life/loadout state and selection resets are explicit. Inventory, runtime, and session tests pass. |
| 7 | Separate soft and hard landings | **Verified** | Soft landing preserves movement; hard landing applies the recovered slowdown and impact bank. Movement and footstep-audio tests cover the transition. |
| 8 | Hurt voice/hit sound is intermittent | **Implemented / live-check** | Local pain response, class voice selection, de-duplication, and remote hurt audio exist. A live combat pass is still the correct final check. |
| 9 | Landmine sits at a four-block corner | **Verified** | Fractional packet coordinates are accepted and the model snaps to one supporting voxel with the recovered pivot correction. Entity and protocol terrain tests pass. |
| 10 | Engineer jetpack death is incomplete | **Implemented / live-check** | Attachment launch, spin, exhaust, delayed sky explosion, light, and grave continuation are implemented and covered by `aos_jetpack_death_tests`; compare the whole sequence visually. |
| 11 | Prefab placement needs particles and sound | **Implemented / live-check** | Recovered smoke/sound routing exists and prefab tests pass; final particle density and mix need live A/B. |
| 12 | Block placement needs particles | **Implemented / live-check** | Normal placement emits its separate placement effect rather than destruction debris. Runtime VFX and terrain tests pass. |
| 13 | Prefab appears voxel-by-voxel | **Verified** | Incoming prefab mutations are presentation-batched and remeshed atomically. Prefab and protocol terrain tests pass. |
| 14 | RMB zoom enable/disable sounds missing | **Implemented / live-check** | Only retail-supported zoom tools emit distinct enter/leave cues. Zoom and weapon-audio tests pass. |
| 15 | Local shots too loud; remote shots too quiet | **Implemented / live-check** | Local/remote gain families and spatial remote sources are separate. Exact mix remains an audio A/B item. |
| 16 | C4 needs a fuse beep and world timer | **Retail-corrected** | Retail C4 is remote-detonated and has no fuse. Timed dynamite gets the beep/world countdown; inventing a C4 timer would be wrong. |
| 17 | Respawn needs `3, 2, 1` audio | **Verified** | HUD time transitions emit `beep2`, `beep2`, `beep1`, independently of the Zombie outbreak sound. `aos_game_hud_tests` asserts the exact sequence and suppression rules. |
| 18 | Enhanced voxels look plastic | **Implemented / live-check** | Enhanced roughness/normal response was reduced while Legacy remains unchanged. The profile split is tested; appearance is a visual check. |
| 19 | Some in-game UI buttons stop working | **Needs reproduction** | UI/navigation/input tests pass, but the report names no screen, control, or sequence. Record the screen plus preceding clicks if it recurs. |
| 20 | Dig debris falls instead of bursting | **Implemented / live-check** | Terrain debris receives radial/tumbling launch velocity and retail lifetime/scale curves. Particle tests pass; compare zombie-hand digging live. |
| 21 | Zombie hearts and heartbeat are absent | **Verified** | Survivor hearts, minimap markers, heartbeat phase/cadence, and server visibility flags are wired. HUD/objective tests pass. |
| 22 | Win/lose/end audio is missing | **Verified** | Authoritative `PlayMusic` drives ending and last-standing banks. Server-audio catalog and protocol runtime tests pass. |
| 23 | Drill explodes on every voxel instead of at the end | **Verified for client presentation** | Drill contact packets extend the bore loop/ticks and terrain bore effect; terminal destruction owns the terminal blast. Tutorial/protocol terrain tests cover this distinction. |
| 24 | Drill projectile needs ticks | **Verified** | Moving loop and contact/drilling loop are separate, bounded, spatial sources. Weapon audio and local entity tests pass. |
| 25 | Looking sharply up/down should reduce travel speed | **Verified** | Movement uses the horizontal projection of the look vector; `aos_player_movement_tests` explicitly asserts pitch-dependent horizontal speed. |
| 26 | Camera recoil/knockback on fire is missing | **Verified** | Network-authoritative shots retain local catalog recoil without predicting damage. `aos_tutorial_session_tests` asserts pitch/yaw recoil. |
| 27 | Held/inventory icons are too large | **Implemented / live-check** | Recovered normal/selected inventory scales and HUD anchors are centralized and layout-tested; confirm at the tester's resolution. |
| 28 | Skyboxes animate too fast | **Retail-corrected** | Retail adds authored UV speed once per 60 Hz draw; the client reproduces that convention. Treating the counter as seconds would slow it about 60x. A named layer capture is required for any remaining complaint. |
| 29 | Missing second shadow layer | **Verified** | Medium+ combines voxel occlusion, sun shadowing, and skylight/horizon cover. Legacy intentionally uses recovered baked shading. Quality-profile tests pass. |
| 30 | Water walking sounds missing | **Implemented / live-check** | Wade/water-jump/water-land banks and dedicated gain are present. Footstep tests pass; final audibility is a live mix check. |
| 31 | Map edit mode does not work | **Verified for deterministic flows** | UGC editor route, terrain selection, project repository, prefab controls, preview writing, save/publish state, and local-server handoff are tested. A full authored-map session remains a release smoke. |
| 32 | Block Gun looks wrong | **Implemented / live-check** | Recovered model/first-person offsets, automatic cadence, palette, shot placement, smoke, and sound paths are present. Weapon model/catalog tests pass; appearance remains visual. |
| 33 | Every owner projectile should be spawned client-side | **Retail-corrected** | IDA decompile of retail `GameScene.send_rocket`/`send_rocket2` shows only `oriented_packet.tool = ...; send_oriented(position, velocity)`. It does **not** create a local rocket. Classic thrown grenades are locally predicted; entity-backed launchers remain server-created with a short local muzzle-to-projectile presentation bridge to avoid duplicate entities. |
| 34 | Rocket/Block Gun particle offsets are wrong | **Implemented / live-check** | Source-derived muzzle anchors and projectile display offsets are used; local entity tests cover the numeric transforms. Final framing is visual. |
| 35 | Engineer flight sound/particles missing | **Implemented / live-check** | `JP_ignite -> JP_flight_lp -> JP_release` plus two-puff exhaust cadence exists for local and replicated players. Audio/VFX still needs live A/B. |
| 36 | Ping absent from UI | **Verified** | WorldUpdate ping is shown in scoreboard rows; HUD/overlay tests pass. |
| 37 | Explosion particles are too small | **Implemented / live-check** | Per-effect recovered size/lifetime and quality-tier scaling replaced the global tiny fallback. Apparent size needs captured comparison at each Effect Quality tier. |
| 38 | Player models need contrast/a distinct shader | **Implemented / live-check** | A restrained model-only contrast/gain path is active; disguises deliberately bypass it to match terrain. Model-quality tests pass. |
| 39 | Blue/green player colors are too bright | **Implemented / live-check** | Authoritative team RGB is preserved and player shading no longer substitutes hard-coded black/white/overbright colors. Visual confirmation remains. |
| 40 | Crates need retail gradient/contact shadow | **Implemented / live-check** | Health crate uses recovered `spot_shadow.tga`, size, query offset, and live VXL projection. Entity tests pass. |
| 41 | Full players still consume crates | **Server-owned / open** | Current `PickupCrateBehavior.on_touch` consumes after unconditional refill. By the time the client sees DestroyEntity, authority has already committed. Requires an approved server-side `try_refill -> bool` transaction; the server was not modified. |
| 42 | Volcano sky animation may be reversed | **Retail-corrected pending a named capture** | The authored Invasion/Volcano layers intentionally contain both positive and negative UV speeds. A global sign flip would break retail data. |
| 43 | Fog does not hide chunk loading | **Verified** | Join/reconnect stays behind the loading gate until packet catch-up and budgeted remesh/upload complete; fog reaches full opacity at chunk-cull distance. Loading and map-atmosphere tests pass. |
| 44 | Skybox should influence fog | **Verified** | Per-map atmosphere resolves authored sky/fog data and applies it consistently to world rendering. Atmosphere tests cover official-map lookup and fog parameters. |
| 45 | Friendly hit feedback is wrong | **Verified** | ShootResponse suppresses local hostile hit color/cue when the nearest authoritative target is a teammate. Replicated-shot tests pass. |
| 46 | Hovered teammate/enemy colors are wrong | **Verified** | Crosshair hover names are white for teammates and red for enemies; player-name projection tests pass. |
| 47 | Enhanced particles should emit light | **Verified** | Explosions publish a central light plus bounded moving hot-trail lights; selected firearms publish short low-energy muzzle lights. VFX/quality tests pass. |
| 48 | Voxel lighting shows bands/lines | **Verified** | Enhanced mode adds stable world-anchored sub-byte dither before fog; Legacy remains unchanged. Quality tests pass. |
| 49 | Every prefab uses the same placement origin | **Verified numerically, live-check visually** | Placement uses each asset's authored bounds/pivot and charges only committed voxels. Prefab placement tests pass. |
| 50 | End/last-standing music missing | **Verified** | Both are server-authoritative music events and mapped in the current audio catalog. |
| 51 | UI text is not centered/aligned | **Implemented / live-check** | Retail coordinate recovery, shared layout primitives, and external layout overrides are active. HUD/frontend tests pass; screenshot parity is still required screen-by-screen. |
| 52 | Fonts are blurry | **Verified** | Runtime glyph textures use point filtering at the requested pixel size. Text tests pass. |
| 53 | Map emissive voxels need lighting | **Implemented / live-check** | Per-map emissive sets and bounded static/dynamic light extraction are present. Emissive/atmosphere tests pass; map-specific coverage remains visual. |
| 54 | Landmines explode without particles | **Verified** | Damage type 15 accepts fractional centers, removes the 19-cell footprint, and emits the landmine explosion presentation. Protocol terrain/VFX tests pass. |
| 55 | Main menu is curved/pixelated; loading has side bars | **Implemented / live-check** | Retail design-space fitting, point-filtered UI art, aspect-aware background cover, and loading composition are present. Confirm on the reporter's aspect ratio. |
| 56 | Poor FOV; hands/shield clip or detach | **Implemented / live-check** | Per-tool view-model transforms, FOV/zoom behavior, synchronous hand/tool selection, and clipping-aware near plane are present and tested. Visual inspection across every class remains. |
| 57 | Explosions, digging, held blocks, and building look weak | **Implemented / live-check** | Those paths now have distinct recovered VFX/material/model handling. This is a multi-scene visual acceptance item, not one boolean bug. |
| 58 | Weapon damage/blast radii need overhaul | **Split authority** | Damage/radius is server-authoritative and the client does not override it; client effects consume the authoritative packet and use recovered presentation. Any numeric damage mismatch requires a server change with protocol evidence. |
| 59 | Zombie/CTF/CCTF objective indicators missing | **Verified** | Zombie hearts, bases/zones, intel ground/carrier markers, height arrows, VIP/final-survivor pins, and server-driven objective HUD data are implemented and tested. |
| 60 | Sniper rifles use the wrong/common shot | **Verified** | Tool 18 uses `semishoot`; tool 19 uses `semi_weak_shoot`. Weapon audio-map tests assert the distinction. |
| 61 | Explosions and selected muzzle flashes need light | **Verified** | Covered by the same bounded dynamic-light path as item 47. |
| 62 | Crates do not respawn | **Verified client lifecycle; server-dependent timing** | DestroyEntity removes the mesh and a later CreateEntity reuses/recreates the retained slot without cold upload. Entity lifecycle tests pass; the server owns the timer. |
| 63 | Crate placeholder voxels remain | **Needs reproduction/capture** | No current packet or coordinate was supplied. Capture entity id, map, and voxel coordinate to distinguish an authored marker from a missing terrain mutation. |
| 64 | Shots fail to register | **Verified deterministic client path; live soak recommended** | Orientation, fire cadence, ammo, origin, recoil, and authoritative response paths are tested. The original reports conflict (one tester saw failure; another saw smooth registration). A fresh rejected-shot trace is needed for any remaining case. |
| 65 | Drill should kill a player instead of exploding | **Server-owned / open** | Current server treats player contact as authoritative explosive damage. Hiding that effect client-side would conceal real damage, not fix it. Requires a separately approved combat-authority change. |
| 66 | Map explodes during loading | **Verified** | Historical catch-up mutations are queued/bounded and applied behind the loading/remesh gate; local speculative terrain is disabled during network authority. Protocol terrain/loading tests pass. |
| 67 | Voting UI says `1/2/3` instead of `F1/F2/F3` | **Verified** | Labels match the bound SDL scancodes and overlay tests pass. |
| 68 | All particles look too small | **Implemented / live-check** | Family-specific recovered sizes replace the old global scale. Exact apparent size is still an effect-quality screenshot check. |
| 69 | Green prefab ghost composites through players | **Verified** | Opaque world/player passes render before the translucent placement ghost. Render ordering and prefab tests pass. |
| 70 | Bots make the client/server lag | **Server-owned / needs current profile** | Bots execute in the BattleSpades server process/worker. The client cannot repair their planning cost; a current server tick profile is required because this report predates later bot work. |
| 71 | Ship an `aos.exe` for Steam play-time tracking | **Fixed in this audit** | Windows post-build and install/package now include `aos.exe` as a byte-identical alias of `BattleSpadesClient.exe`; both use the same adjacent runtime/assets. |
| 72 | Kill/score bonus canvas is missing | **Verified** | The recovered score feed stacks score/reason entries beside the crosshair and is covered by HUD/score-feed tests. |
| 73 | Switching weapons during reload permanently breaks reload | **Verified** | Unset cancels the outgoing runtime transaction and clears reload/pending edges; selecting again starts from valid authoritative ammo. `aos_weapon_runtime_tests` contains this exact regression. |
| 74 | Enemy intel cannot be picked up | **Server-owned path present; live-check** | Current CTF server automatically checks enemy-intel proximity and performs pickup/capture/drop/return; the client represents ground and carried intel plus indicators. Client objective/runtime tests pass. Re-test on the current server build because no client pickup packet exists to patch. |
| 75 | Two native clients interoperate well | **Corroborating observation** | Not a defect; retained as evidence that the protocol path already worked for another tester in the same build family. |
| 76 | Team bases are not outlined like retail | **Verified** | Server packet-43 zones drive the team-gated minimap region, base billboard/off-screen pointer, additive pulsing 3D boundary cube, and the recovered alpha-150 screen tint while the local player occupies the base. `aos_objective_indicator_tests`, HUD tests, and the renderer submission path cover the retained authority and presentation. |
| 77 | Human-to-Zombie conversion needs the global scary cue | **Verified** | The client plays the shipped non-positional `zombie_become.ogg` for either explicit `PlaySound(28)` or an authoritative human-to-Zombie `CreatePlayer` transition. A 300 ms gate deduplicates servers that send both; initial Zombie roster creation and ordinary Zombie respawn remain silent. `aos_zombie_conversion_audio_tests` covers both packet orders and all Zombie class ids. |

## What remains actionable

### Client release checks

The next playtest should focus only on verdicts marked **Implemented /
live-check** and record map, mode, class/tool, resolution, graphics backend,
effect quality, and a short video. That turns subjective reports into an exact
comparison without re-opening already disproven protocol assumptions.

For the two **Needs reproduction** reports, record the exact UI route/control or
crate entity/map coordinate. There is not enough information in the channel to
make a safe code change.

### Explicit server boundary

Two reports are confirmed current server behaviors: consuming a full crate and
drill/player contact using explosive damage. Bot performance is also server
owned. They are intentionally not hidden or contradicted in the client, and
the BattleSpades repository remains untouched.

### Retail projectile proof

The preserved macOS retail `aoslib.scenes.main.gameScene.so` was opened through
IDA. `GameScene.send_rocket` (0x14d0a0) and `send_rocket2` (0x14cab0) set the
oriented packet tool and call `send_oriented(position, velocity)`, then return.
Neither routine constructs a local rocket. This is why the native client only
predicts the retail grenade family and uses presentation bridging for
server-created rocket entities.
