# Gameplay parity audit

> **Recovery/specification reference.** Preserve the measured retail behavior and its evidence. Implementation updates, old build paths, test counts and session constraints below describe their original investigation; they are not current release or deployment status. Use the [maintained documentation index](../README.md) for present operating instructions and recheck historical findings against current source.

Updated 2026-07-21. This document separates recovered retail behavior from
temporary native-client approximations. Do not tune gameplay by eye when an
oracle named below can answer the question.

## Evidence used

- Decompiled Python: `G:\AoSRevival\aceofspades_source\aoslib`, especially
  `weapons/tool.py`, `spadeTool.py`, `diggingTool.py`, `blockTool.py`,
  `pistolWeapon.py`, and the animation modules.
- Native retail binary: `G:\AoSRevival\AceOfSpades_no_steam_new\aoslib\character.pyd`.
  `Character.draw_fps` is the wrapper at `0x10086b40` and implementation at
  `0x1005cb20` in the IDA database used on 2026-07-21.
- Native gameplay binary:
  `G:\AoSRevival\AceOfSpades_no_steam_new\aoslib\scenes\main\gameScene.pyd`.
  It contains `BlockManager.remove_falling_blocks` and the compiled
  `FallingBlocks` methods.
- Protocol oracle: `G:\AoSRevival\BattleSpades\shared\packet.pyx` and
  `G:\AoSRevival\BattleSpades\docs\PROTOCOL.md`.
- Structural oracle: `G:\AoSRevival\BattleSpades\server\world_manager.py`.
- `aoslib.world.cube_line` was executed from the server's Python 3.12 native
  module to capture positive, negative, diagonal, and tied-axis golden paths.
- OpenSpades was used only as corroboration for generic voxel behavior. It is
  not a Protocol 168 or Battle Builder gameplay oracle.

## Inventory and tool-selection findings

The inventory is a renderer-independent `world::RetailInventory`; the
Tutorial session is only one consumer. It keeps HUD indexing separate from
the protocol tool byte because retail builds one ordered strip from class
loadout entries, prefab choices, and UGC tools.

Recovered native entry points in `gameScene.pyd`:

- `GameScene.set_current_tool_index`: `0x101542e0`;
- `GameScene.get_tool_index_on_mouse_scroll`: `0x101573e0`;
- `GameScene.on_mouse_scroll`: `0x1015bc50`.

The recovered input contract is now pinned by `aos_retail_inventory_tests`:

- number keys select their combined HUD slot directly and do not reveal the
  tool strip;
- the mouse wheel first allows the equipped character/tool to consume the
  event, then wraps through the combined loadout/prefab/UGC index;
- wheel traversal skips unavailable entries, but tools in retail's
  `SELECTABLE_ON_NO_AMMO_TOOLS` remain selectable when empty;
- dead/non-swappable states reject both paths;
- a protocol-visible tool change is immediate, while the Character pullout
  presentation is an independent 0.5-second animation;
- wheel selection opens `HUD.set_show_tool_loadout(True, 1.0)` only while its
  scale timer is idle. Further wheel notches change selection without
  restarting that timer.

The Tutorial's recovered slot order is block (tool 5), spade (tool 2), pistol
(tool 17). The HUD uses the retail 80-pixel stride and compact
`png/ui/weapons` portraits. Normal entries use the 195x195 frame at 0.25
scale and the selected frame asset keeps its authored 0.7 scale (a 136.5px
draw box, approximately 118 visible pixels). Portraits use the recovered
0.4/0.9 item scale inside the draw routine's half-scale transform, producing
effective 0.2/0.45 scales. The separate 1.3/2.0 values position the hotkey
label and must not be multiplied into the frame or portrait dimensions. The
selected scale remains for the toolbar lifetime; the earlier 0.18-second
shrink pulse was an unsupported approximation and was removed. These constants
come from `images.py` and `HUD.draw_loadout_item_hud` (`hud.pyd` core
`0x100a8030`). The strip is centered at 70 percent of the top-origin window
height. Compare
`out/parity/retail/07-toolbar.png` with
`out/parity/ours/inventory-wheel-final.png`.

## Viewmodel findings and implementation

The old C++ viewmodel was a screenshot-fitted approximation. The replacement
is `world/retail_view_model.{hpp,cpp}`, a renderer-neutral oracle consumed by
the bgfx frontend and tested independently.

Recovered invariants:

- `Tool.apply_transform` is translate, rotate X, rotate Y, rotate Z.
- Viewmodel scale is `0.05`; the outer character translation is
  `(-0.4,-0.55,+0.9)` followed by a 180-degree Y flip.
- Shared arm anchor is `(0.401,-0.01,-0.801)`.
- Left upper arm: `(0.29,-0.059,0.10)`, local yaw `-25` degrees.
- Left lower arm: `(-0.12,-0.061,0.60)`, local yaw `-50` degrees.
- Right lower arm: `(-0.48,-0.06,0.48)`, local yaw `0` degrees.
- Spade rotates the animated arm pitch by `0.25`; its digging pitch moves
  from 36 to -4 degrees over the active interval.
- `AnimUseSpade.start()` intentionally has signs opposite its first update.
- Resting BlockTool position `(-0.04,0,0.3)` and arm offset
  `(0.04,0,-0.3)` cancel for the arms; its yaw is 45 degrees.
- `AnimWeaponShoot.start()` is identity. Subsequent frames use
  `f=-remaining/32`, position `(10f,10f,10f)`, pitch `280f`.

`aos_retail_view_model_tests` pins these semantic values. Matrix assembly
still belongs to the renderer, but every recovered literal now comes from the
tested oracle rather than an untested frontend constant.

### Character color and weapon attachment findings

The retail `kv6.pyd` renderer's `set_kv6_default_color` stores the supplied
RGB verbatim. Its draw core (`0x10002030`) renders three reserved magenta KV6
buffers at 1.0, 0.7, and 1.3 times that RGB. The native client now resolves
the `(128,0,128)`, `(64,0,64)`, and `(192,0,192)` authored bands with those
factors, including channel saturation. The default debug colors are the
server defaults `(44,117,179)` and `(137,179,44)`, but the lab accepts any RGB;
team color is data, not a hardcoded blue/green material.

`aoslib.models.load_weapon` supplies a distinct third-person offset per model:
the conventional base is `(6,-18,0)` plus each declaration's extra offset.
Explicit first-person `load_model` offsets are independent. The generated
weapon catalogue and model loader now preserve and bake every part's exact
authored offset. Examples pinned by tests include pistol `(6,-18,0)`, machete
`(13,-25,15)`, and medpack first person `(0,0,-3)`.
`KV6.offset_pivots` adds these values to the pivot, so emitted geometry moves
by the negative offset; treating them as a direct translation inverts the
character body stack and makes held tools float.

`Character_Arms_Collision.kv6` is only the collision hull and is no longer
drawn as visible arms. Retail creates upper/lower arm segments in
`Character.setup_tp_arms` (`0x1002fa00`). The close-up lab renders the class arm
assets separately so pose work can be inspected, but the final replicated
third-person aim/reload animation remains a later character-animator gate; do
not fold a screenshot-tuned arm pose into the weapon's recovered model offset.

## Terrain damage and structural collapse

`VxlMap` now stores sparse pre-break damage independently from solidity.
Sublethal `Damage(37)` keeps collision and darkens the block. Setting or
removing a voxel clears stale damage. The chunk mesher applies the recovered
integer darkening rule to damaged RGB channels.

`voxel_collapse.{hpp,cpp}` implements the authoritative component rule:

- face and edge adjacency (18 neighbors), excluding three-axis corners;
- only the mandatory z=239 bed proves ground support;
- a ten-million-probe default budget;
- budget exhaustion fails safe and never returns a partial component;
- colors are captured before unsupported voxels leave collision atomically.

The Tutorial session sends every dig/shoot edit through this path and
invalidates every affected chunk. IDA recovery of `FallingBlocks.initialize`,
`play_sound`, and `update` established two separate 0.75-volume sound events:
`des_split_{small,med,large}` at detach and
`des_imp_{small,med,large}` (with water variants) at breakup. Both use the
retail 15/80-block thresholds. `world/terrain_effects` keeps collision removed,
renders one internal-face-culled body with an eased horizontal tilt, and uses a
hard 2,048-voxel visual cap. IDA's recovered `FallingBlocks.update` contains the
gravity/transform/delete path but no VXL collision-kernel call; accordingly the
presentation body phases through surviving roofs, walls, and terrain instead of
freezing on its first ledge. Presentation time joins the requested
0.5/0.9/1.2-second anchors smoothly; breakup emits one map-coloured tumbling
block image per captured voxel, kicked outward and shrinking to zero. The
NONRETAIL readability smoke is resolved to the surviving VXL surface beneath
the body rather than its centre. This timed/capped presentation is an
intentional port policy rather than a claim that retail used those exact limits.

Training bullseyes are compound targets: the audited VXL face contains 13
red `(228,51,52)` cells and eight white `(232,233,233)` cells. Hitting the red
trigger now captures/removes the complete 21-cell face as one falling
component while leaving its separate metal stand. Falling components and
projectile KV6s must compute fog from transformed world position. Using local
mesh coordinates reproduced the reported sky-colored overlay and is forbidden
by the shader regression comment in `vs_world.sc`.

### Retail sky and fog invariant

`SkyboxData` (packet 51) carries a NUL-terminated definition basename, not six
cube-map faces and not a guessed map-name-to-colour table. The selected JSON
definition under `mesh/<stem>/<stem>.txt` orders layered `.aos` triangle meshes
and provides per-layer scale, Euler rotation, translation and UV velocity. Each
`.aos` record stores its TGA texture name after an unindexed stream of vertices
with position, RGBA and UV (nine little-endian floats per vertex). The client
must retain packet 51 when it arrives before `CreatePlayer`, render the blended
dome camera-centred with animated UVs, and apply `StateData(45).fog_color` to
both terrain fog and the clear fallback. A malformed asset basename fails
closed and must not destroy the last valid atmosphere.

## Tutorial all-weapons integration

F4 installs all 65 protocol tools into a developer `PlayerInventory`. Selection
and ammo stay in the same inventory/runtime used by the F10 laboratory; number
keys are immediate and wheel selection retains the recovered toolbar timing.
The Tutorial action consumer now applies catalogue accuracy, zoom accuracy,
pellet count/spread, recoil, cadence, reload, melee and projectile emission.
RMB is classified per equipped tool. The macOS IDA databases preserve the
demangled `GameScene.on_mouse_press`, `Character.set_secondary_shoot`,
`Character.set_zoom`, and `Character.draw_sight` symbols and confirm that the
scene forwards RMB while Character/tool code owns its meaning. Twenty-two tools
enter aim presentation: the two snipers at their 1.5/1.2 magnification and
0.4/0.5 look sensitivity, and twenty more through their own iron sights at a
multiplier of 1.0, which is already a 2x view. Retail's gate is
`(not has_secondary) and can_zoom and self.main and sight is not None`
(`character.pyd sub_100348B0`) — it has no weapon-category term and no
magnification threshold. An earlier revision of this section claimed sniper
tools alone aim and that sight assets on ordinary guns do not grant ADS; both
claims were wrong and cost every iron-sight weapon its right click. See
`docs/WEAPON_SECONDARY_RECOVERY.md`. C4, block,
prefab, paint, UGC tools, secondary spades, and the deployable MG instead route
their actual secondary state machine. The scope in this retail build is the
`*_sight.kv6` model, not a flat scope PNG. E is retail's default
`weapon_custom`; MMB is an explicit convenience alias requested for testing.
For the minigun, either shoot input advances the recovered motor: RMB pre-spins
without spending ammunition, while held primary fires only after the 0.5 spin
threshold. Weapon-custom is unrelated. Barrel presentation consumes accumulated
rotation, not the bounded spin-speed fraction.

The exact sight oracle comes from `Character.draw_sight` at macOS `0x3c0d0`:
identity matrix, 180-degree Y rotation, 0.05 scale and position
`(sight_x+.025, sight_y-.35, sight_z+1.85)`. RMB is toggle-on-press; releasing
it does not clear aim. `--tutorial-tool ID --tutorial-aim` is an explicit local
lab automation path used to verify that state without unreliable desktop focus.

F5 and Shift+F5 cycle the full recovered class table inside the Tutorial debug
sandbox without changing the selected tool. Each switch reloads that class's
declared upper/lower FPS arms. Zombie variants intentionally load no separate
arm pair because their equipped hands tool owns the first-person model.

Locally simulated projectiles use a swept segment every 60 Hz tick rather than
testing only their final point. Their authored KV6 model follows the velocity
vector and collision enters the canonical VXL damage/collapse path. This
validates presentation and weapon feel offline; it is not authority for a
multiplayer match. A future live session must send the generated Protocol 168
tool action, accept authoritative entity/terrain state from the server, and
reconcile or retire the local prediction.

Grenade families are not one generic projectile. Normal, Classic and AP bounce;
Molotov/Chemical/grenade-launcher projectiles detonate on contact; Sticky sticks
and arms for five seconds. Gravity is 30, bounce damping is 0.36 and velocity is
capped at 511.98999. Normal/AP throws use `25 + cooked_fraction*50`, Classic is
fixed at 35, Molotov uses `35 + charge*40`, and Chemical/Sticky use
`25 + charge*50`; thrower velocity is added. The launcher uses recovered speed
75, 3-second lifespan, radius 4 and block damage 6. Normal explosion, Molotov
fire and Chemical effects dispatch distinct bounded particle events; terrain
edits enter one collapse batch and play the recovered explosion sound.

Melee footprints are explicit: single cells for Pickaxe/Knife/Crowbar, three
vertical cells for Spade/Classic, 3x3x3 for Super Spade/Zombie Hands, an
accumulating two-cell Machete column, and UGC Super Spade single LMB versus
cubic RMB. Classic RMB waits 0.8 seconds and cancels on early release.

## Protocol 168 terrain path

`network/protocol168_terrain.{hpp,cpp}` now has strict complete-payload codecs
and golden bytes for:

- `SetColor(11)`;
- `BlockBuildColored(33)`;
- `Damage(37)`;
- `BlockLine(40)`.

`Protocol168TerrainReplica` orders palette state and mutations. A BlockLine
before the relevant player's SetColor fails closed. Explicit-color cells do
not depend on palette state. Changed cells invalidate their own chunk and the
adjacent chunk when a face crosses a 16-voxel boundary. Collapse components
are drained separately for presentation.

BlockLine expansion reproduces retail `aoslib.world.cube_line`, including its
protocol-visible tie priority: Z wins ties, then Y wins an X/Y tie. Existing
solid cells are skipped rather than recolored.

This terrain replica remains the only accepted mutation path once continuous
gameplay packets are wired. It is now complemented by a real connection and
loader foundation:

- `Protocol168Session` enforces InitialInfo -> MapDataValidation ->
  MapSyncStart/chunks/end -> StateData -> own CreatePlayer ordering.
- `EnetProtocol168Client` uses protocol data 168, one ENet channel and the
  range coder used by the retail/server endpoints.
- Packet 105 is sent with a zero-length ticket. This is BattleSpades' explicit
  offline compatibility path, not an invented Steam ticket; it therefore does
  not activate ticket-derived XOR.
- CRC zero requests an authoritative full map. Each LZF-wrapped packet is
  bounded, the zlib stream is inflated, and the 512x512 ordered column stream
  is validated by `VxlMap::load` before the world becomes visible.
- StateData supplies the local player id; CreatePlayer packet 28 builds a
  generation-safe roster. Only then are NewPlayerConnection and the first
  ClientData sent.
- Unknown packets are counted/quarantined. Phase-critical malformed data fails
  the session immediately; bounded non-critical corruption fails after three
  occurrences without reaching simulation or rendering.

The live probe joined the local BattleSpades server on 2026-07-22, loaded
Mayan Jungle (10,812,149 solid voxels from 1,582,935 compressed map bytes),
received 13 roster rows and disconnected cleanly. This proves connection and
full-map synchronization, not yet a playable network match: the normal Join
Match route, WorldUpdate interpolation, entity/objective replication and
continuous terrain/action reconciliation remain to be attached.

## Reload and explosive parity tranche (2026-07-22)

IDA `Character.reload` (`0x10020da0`) cancels incompatible zoom, sets the
shared Character pullout clock to the weapon reload time, starts the reload
sound and emits WeaponReload(false). `Character.end_reload` (`0x1001fc00`)
transfers ammunition and emits WeaponReload(true). Clip-reload weapons move one
shell per cycle and immediately restart reload while another shell fits; only
the final cycle plays the optional `reload_done_sound`. The native runtime and
both weapon/hands draw roots now consume that same clock, and fire/use input is
suppressed for its duration.

Molotov uses `AnimThrowGrenade(max_charge=3, stop_on_end=False)`, pauses charge
while sprinting and releases at `35 + charge*40`. Its throw sound is
`molotov_throw`; impact uses `molotov_land_explode`. The primary blast is
radius 4 with block damage 3 and player damage 50. The former implementation
accidentally selected `MOLOTOV_BLOCKFIRE_EXPLOSION_RADIUS` (2) as the primary
radius; the generated weapon definition is now the sole primary source.

Explosion presentation creates ten ordinary debris particles and four glow
particles. Molotov adds five four-second block-fire emitters; Chemical adds a
bounded persistent cloud. Radius affects burst spread, while canonical terrain
damage/collapse remains separate from presentation. The weapon catalogue pins
the recovered primary blast/block values for grenade, Classic/AP grenade,
rocket/RPG2, drill, Sticky, Chemical, grenade launcher, dynamite, landmine and
C4 so similarly named secondary constants cannot silently replace them.

## Verification

The vcpkg SDL3/bgfx/OpenAL build links
`out/build/native-dev/src/RelWithDebInfo/BattleSpadesClient.exe` and passes
49/49 tests with warnings as errors. The graphical Tutorial smoke presses the
recovered START gate before asserting world output. `-ExerciseWeaponSandbox`
visibly equips the all-weapons sandbox and sends held primary input.
`-ExerciseAimAndClassSwitcher` uses deterministic tool/aim launch flags and
verifies the sniper one-draw sight/FOV path. `-ExerciseGrenades` starts with
tool 11, verifies the authored grenade viewmodel, consumes one grenade through
a held cook/release, and remains alive beyond its fuse. Deterministic tests
separately require the explosion event because a bounced projectile may
detonate outside the current camera frustum.

Commands:

```powershell
cmake --preset dev
cmake --build --preset dev --config Debug
ctest --preset dev -C Debug --output-on-failure
./scripts/build.ps1 -Profile Dev -Native
```

## Next parity gates

1. Attach the implemented Protocol 168 session and full-map result to the
   normal Join Match loading scene. Feed received terrain payloads to
   `Protocol168TerrainReplica`; no second map mutation path is allowed.
2. Add WorldUpdate/SetHP/ShootFeedback/ShootResponse and entity
   replication so remote bodies, tools, fire, audio, and hit feedback share one
   typed snapshot path.
3. Attach the existing 65-tool action adapter and local projectile prediction
   to that session, with server reconciliation and late-join tests.
4. Finish exact per-tool casing, tracer, muzzle, explosion/fire, and
   third-person character animation presentation. Core viewmodel families,
   catalogue audio, recoil, zoom, projectiles, falling structures and debris
   are present; do not regress them to a three-tool Tutorial switch.
5. Run the graphical Tutorial smoke only on an unlocked, unobscured desktop;
   it takes over mouse input. Capture retail and native frames at identical
   animation ticks before accepting visual parity.

## Live replica correction tranche (2026-07-22)

The first three gates above are now partially obsolete: the menu owns the live
connection, full-map/terrain catch-up, continuous ClientData, WorldUpdate player
replicas, class/loadout selection, and outbound weapon actions.

The critical movement invariant is ACK alignment. A WorldUpdate row describes
the past ClientData loop named by `acknowledged_client_loop`; comparing it with
the current predicted transform caused the observed rollback. The runtime now
journals bounded per-loop transforms, reconciles against the matching sample,
rebases later samples, and smooths only the camera representation of a small
correction. Simulation state remains authoritative immediately.

Remote character presentation now consumes StateData team RGB and the complete
WorldUpdate display prefix. The renderer uses separate standing body and leg
meshes, crouch state, selected third-person tool, and recovered arm/tool poses.
The macOS `Character.update_animation` symbol at `0x83a70` proves left/right
legs are updated independently; exact retail gait curves remain a visual parity
follow-up, but the previous rigid/bobbing mannequin path is removed.

Pause-menu state is recomputed from live mode/team/class locks and roster data.
ClientInMenu(110), ChangeTeam(77), SetClassLoadout(13), and ChangeClass(78) are
sent through typed boundaries. The remaining high-priority network work is
inbound projectile/entity, damage/death/objective, sound and particle dispatch.

The final real-window validation uses a readiness-gated join rather than fixed
timing. `out/evidence/live-parity-smoke-current2` proves the live loading
START gate, server SelectClass, world entry, movement, five-action pause menu,
and Change Team roster in order. The stock StateData localization identifiers
are resolved in the team screen, while all body/arm/tool material tint now
comes from the same StateData RGB source and refreshes at CreatePlayer.
The decoder also retains the terminal `has_map_ended` byte after bounded
entity and screenshot-camera records, matching EscapeMenu's post-match gate.
## 2026-07-22 movement, inventory, and terrain-effect parity

The main continuous-walking rollback was a scale-interpretation bug. Each
`InitialInfo(114).movement_speed_multipliers[class_id]` value is already the
complete direct scale used by the server's native class acceleration, sprint,
and crouch calculations. It is not a speed that must be divided by the retail
class baseline. Soldier therefore uses the received `1.40625` directly; the
old client reduced it to roughly `1.004`, guaranteeing growing prediction
error during ordinary walking. `protocol168_movement_scale()` is now the only
frontend boundary for this value, and custom speed rules are composed and
wire-rounded once on the server so prediction and authority receive the same
1/64-fixed-point value.

ClientData continuity and phase are explicit protocol invariants:

- the handshake owns loop zero and exports the next loop to the gameplay
  sender, so the frontend cannot reuse loop zero after loading;
- ClientData remains reliable. IDA of `GameScene.send_client_data` at
  `gameScene.pyd:0x1016AAE0` shows `send_packet(packet, True)`, and the True
  branch in `network.pyd` selects `PACKET_FLAG_RELIABLE`;
- with the server's default one-frame input latch, locomotion, jump, sneak,
  and sprint simulate the previous accepted sample while crouch and view
  orientation apply from the current sample. The network client now predicts
  that same phase and treats jump as held input rather than a one-tick edge;
- local reconciliation still compares a WorldUpdate owner row with the
  journaled transform for its acknowledged ClientData loop. Remote rows use a
  bounded 30-to-60 Hz interpolator and snap only on semantic discontinuities
  larger than eight blocks.

Server loadout packets now rebuild the local runtime inventory. CreatePlayer
and SetClassLoadout update ordinary tools, selected prefabs, and UGC tools as
one selection. UGC toolbar entries also exist in `WeaponRuntime`; previously a
slot could highlight one tool while actions continued using the prior weapon.
Selected-tool prediction is ACK-owned: an older WorldUpdate cannot undo a
local selection edge, and the server tool byte becomes authoritative once its
acknowledged loop reaches that edge. Restock entity types remain distinct:
ammo, blocks, and health no longer refill one another.

Damage(37) now uses the recovered type footprint rather than treating every
packet as one voxel. Vertical spade columns, 3x3x3 Super Spade/Zombie volumes,
two-cell machete columns, and radius-two drill/explosive footprints are
expanded once and collapse is evaluated once per wire action. Every committed
live Damage packet emits a terrain-impact event even when the hit is
sublethal, preserving the pre-hit color for crack/debris presentation and
dispatching the matching bullet, dig, or explosion sound. Historical map
catch-up drains these events without rendering them, preventing a reconnect
from replaying an old particle storm.

Verification for this tranche: the strict native suite passes 54/54 tests;
the complete server suite passes 1,143 tests. A rebuilt raw live client joined
`88.80.155.252:38888`, loaded WW1 with 3,909,746 solid voxels and 13 roster
rows, consumed 237 WorldUpdates and 2,555 Damage packets in eight seconds, and
exited cleanly. The loading renderer produced
`out/evidence/live-loading-after-movement-fix.png`. The automated desktop
world-capture path currently receives an all-black DWM surface despite a
responsive window, so it is not accepted as visual gameplay evidence.

## 2026-07-23 deterministic server/client parity gate

`tools/run-server-client-parity.ps1` now starts an isolated real
BattleSpades server with `tools/protocol168-parity.toml`, joins through ENet
protocol 168, loads the full VXL, and drives 60 Hz walking, sprinting, two
moving jumps, crouch and strafe input. Each WorldUpdate owner row is compared
to the client prediction recorded for its exact acknowledged loop. The gate
fails on any malformed active runtime packet, input-byte mismatch, missing
owner tool sentinel, position correction above 0.5 blocks, or velocity
correction above 0.1 blocks.

The gate found four independent boundary errors:

- StateData writes colors in historical BGR wire order. Decoding them as RGB
  swapped team and fog channels before class material generation.
- A legal VXL sentinel with `maximum_z == 240` underflowed the source-height
  alignment expression and shifted the entire synchronized terrain upward by
  one voxel.
- InitialInfo's movement multiplier is already the direct native class scale;
  dividing it by the class sprint multiplier made local movement much slower
  than the authority.
- On a grounded jump, Character restores the latest ACK-ordered owner-row XYZ
  only when that anchor is within 0.25 blocks of its pre-physics position. It
  retains native launch velocity and airborne state. Mirroring only Z, always
  restoring XYZ, or using an unbounded stale anchor all create visible jumps.

The owner WorldUpdate tool value `0xFF` is also a sentinel meaning “retain the
local selected tool”; it is never an equipment id. Runtime packet ids 5, 19,
21, 22, 23, 24, 26, 27, 64 and 84 now have strict complete-payload decoders,
although entity/audio presentation remains a separate unfinished subsystem.

Three fresh 12-second runs produced 720 client frames each, 303-315 matched
owner acknowledgements per run, zero input mismatches, zero malformed packets,
zero position correction and zero velocity correction. The native suite passes
55/55 tests and the server suite passes 1,142/1,142 tests. The real SDL smoke
also passed loading, class selection, world entry, walking, pause, and Change
Team, with evidence under `out/evidence/live-local-parity-final`.

## 2026-07-23 paired-client delivery and rendered-player gate

The original paired-client harness could consume a late `CreatePlayer(28)`
while waiting for the initial world row and then report a false invisible
peer. The server roster was intact. The harness now preserves every packet
during that wait and asserts both dynamic lifecycle packets and remote
WorldUpdate rows. Simultaneous and four-second-staggered joins each ran for
720 frames with zero input mismatches, malformed packets, orphan world rows,
position corrections, or velocity corrections. Evidence is under
`out/evidence/final-simultaneous-parity` and
`out/evidence/final-staggered-parity`.

IDA and 448/448 retail ClientData captures disprove the owner-receipt
hypothesis for the `ooo` byte. Stock computes exactly `(loop + 7) & 15`; the
byte is not a WorldUpdate acknowledgement and no high-bit extension is sent.
BattleSpadesClient now emits that stock four-bit sequence for the handshake,
gameplay sender, and live parity tools. Jump anchoring remains ACK-ordered from
received owner rows and applies the existing stale-anchor teleport guard.

The remaining 0.075-block slope/landing wobble was below the retail
`Character.update` adjustment threshold of 0.1 blocks, with identical input
flags and zero velocity difference. Reconciliation now applies that recovered
vector threshold before its fixed-point component deadband, so camera and
simulation do not chase sub-retail voxel-edge noise.

The rendered-player gate uses `tools/protocol168-render-parity.toml`, a real
Python 2 protocol peer, normal `ChatMessage(49)` admin routing, and a live
class-1 `CreatePlayer`/WorldUpdate stream. The peer is teleported beside the
native camera without a production test hook and remains connected for the
capture. `out/evidence/render-parity-final/client` shows the
networked Soldier body using StateData team RGB `44:117:179`; it is neither
black nor white. The overlay also reports live roster/rig counts and the
nearest remote class/team/color/distance so a future material or lifecycle
regression is visible in the same screenshot.

## 2026-07-23 negative-orientation and block-action parity repair

The remaining turn-dependent rollback was a server wire-decoder defect, not a
movement tuning problem. ClientData orientation components are signed
two's-complement 3.13 fixed point. The runtime fast path interpreted values
with bit 15 set as a non-native extended/sign-magnitude format, so the valid
wire value `00 E0` (`-8192`, or `-1.0`) became `-2.0`. Axis-aligned vectors hid
the defect after normalization; negative diagonal look vectors changed the
server acceleration direction enough to create the reported walking and jump
rollback. `protocol/runtime_packets.py` now decodes the signed 16-bit value
directly, with raw-byte regressions for `-1.0`, `-0.25`, and `0.5`.

Retail phase ordering was also restored exactly: locomotion, jump, sneak, and
sprint use the previous accepted ClientData packet; crouch and orientation use
the current packet. The native predictor quantizes authoritative orientation
to the same 1/8192 wire grid it transmits, preventing opposite voxel-edge
choices caused by sub-wire floating-point differences while retaining
full-precision camera motion.

The live parity harness now turns through a negative diagonal, changes color,
places a real packet-40 BlockLine into supported terrain, receives and applies
the authoritative terrain echo, then runs, jumps, crouches, strafes, and (for
Engineer) exercises jetpack movement. Two simultaneous retail-protocol clients
also assert dynamic CreatePlayer visibility and each other's block events.
The following clean runs had zero input mismatches, malformed packets, orphan
rows, position corrections, or velocity corrections:

- `out/evidence/movement-turn-block-jump-final.csv`
- `out/evidence/movement-engineer-quantized-turn-final.csv`
- `out/evidence/two-client-turn-block-final`

The focused server movement/decoder suite passes 152/152 tests and the native
client suite passes 55/55. One repeated Engineer run encountered a terrain
corner correction of 0.1065 blocks; two other Engineer runs and the paired
client run were zero. This is close to the recovered retail 0.1 adjustment
threshold and is not the former multi-block orientation rollback.

## 2026-07-29 direction-aware packet census

The BattleSpades server repository is a read-only protocol oracle for the
native client. Coverage is classified by direction and by effect: decoding a
payload is not the same as applying it to retained state, and retaining state
is not the same as presenting it visibly.

All packet IDs currently emitted by the production server now cross a strict
client framing boundary. This tranche added the missing runtime codecs and
state transitions for `ClockSync(0)`, `ChangeEntity(16)`, `HitEntity(20)`,
`PrefabComplete(29)`, `ExplodeCorpse(36)`, `UGCObjectives(68)`,
`FogColor(74)`, `InitialUGCBatch(98)`, `UGCMapInfo(102)`, and
`SetGroundColors(118)`, plus the planned tutorial `HelpMessage(109)`.
Previously typed but unconsumed `DropPickup(71)`, `WeaponReload(76)`,
`BlockSucker(94)`, and `PlaceUGC(97)` now update the live match. The UGC
source-map prevalidation
sequence `MapDataStart(54) -> MapDataChunk(56)* -> MapDataEnd(58)` is bounded,
zlib/VXL-validated, and cannot splice into normal `MapSync(55/57/59)`.

Block colour has four related wire paths:

- `SetColor(11)` stores the selected `0xRRGGBB` palette value per player;
- `PaintBlock(7)` recolours an existing solid voxel with explicit RGB;
- `BlockBuildColored(33)` places one voxel with explicit RGB;
- `BlockBuild(32)` and `BlockLine(40)` carry no RGB and therefore resolve
  through the sender's most recent `SetColor(11)`.

The local palette, held block, placement preview, remote palette, authoritative
terrain mutation, chunk invalidation, and reconnect map state all use that same
canonical colour. A packet-32/40 mutation arriving before the sender's
SetColor fails closed instead of inventing cyan or reusing another player.

The remaining registry entries are not active server-to-client gaps. They are
server-planned, reversed-but-unused, client-to-server-only, or have no verified
wire contract yet. Notable future contracts are `EntityUpdates(3)`,
`BlockManagerState(38)`, resource packs `61-63`, `ProgressBar(65)`,
`RankUps(66)`, `ForceShowScores(72)`, `TimeScale(75)`, team-rule packets
`79-82`, `DisableEntity(96)`, `VoiceData(103)`, `DebugDraw(107)`,
`LockToZone(108)`, password packets `111-113`, and server-side
`TeamProgress(117)`. They must not be guessed into production decoding merely
to make an ID checklist appear complete.

Some active UGC packets are now safe and state-complete but still
presentation-partial: validation rows and placed Game Data are retained
without the final stock editor marker widgets; the packet-102 PNG is retained
without its final preview panel; packet-118 palette rows are retained without
the final UGC ground-palette renderer. `ExplodeCorpse(36)` emits the confirmed
effect and cleanup state, while exact Classic corpse/grave model parity remains
a presentation task.

Verification: the Release build is warnings-as-errors clean and all 74 native
tests pass. A ten-second live census against the production endpoint loaded
Great Wall, retained 13 roster rows, consumed 901 runtime packets and 300
WorldUpdates, and saw only implemented IDs:
`2,5,11,19,21,22,23,24,26,27,28,33,37,64,69,84`. The smoke executable
returned its expected non-zero result only because that passive interval did
not observe an owner-shot feedback packet; packet framing and the census were
clean.
