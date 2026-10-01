# Score, map vote and disappearing hands fixes

## Accepted final-frame kills

`BattleSpades/server/simulation_runtime.py` now drains the bounded batch of
already accepted mode events before `mode.on_tick` checks the round clock.
Previously, a lethal hit accepted earlier in that same simulation step had
already increased the kill count and sent KillAction, but the timer could end
the round before its queued personal/team score was applied. The ended guard
then discarded those points and could declare the wrong winner.

This preserves the existing event budget and plugin callback order. It does
not change retail score amounts. The TDM regression accepts a real lethal hit
at the timeout boundary and checks personal score 100, team score 1, and the
winning team instead of a 0-0 draw.

## Map-vote result

GenericVoteMessage CLOSED uses its `title` as the six-second result panel.
The server had reused `VOTE_MAP_TITLE`, leaving only “VOTE MAP” after voting.
It now sends `('MAP_VOTED_MESSAGE', (winning_map,))`; the native vote decoder
supports the original English string “Next map will be {0}”. Map names stay
literal format arguments, including quotes/braces. The result matches the
same authoritative map consumed by the transition service.

Evidence: original string table key `MAP_VOTED_MESSAGE`, existing
`GenericVotingModel` CLOSED transition recovered from retail, and the literal
packet format in `shared.packet.GenericVoteMessage`. Packet and model tests
cover the selected map, special characters, and disabled voting after close.

## Hands and respawn

The third-person renderer previously skipped both the held tool and ordinary
class arms whenever WorldUpdate `can_display_weapon` (action bit 0x10) was
false. Retail draws those arms independently, so this made players appear
armless while spectating or watching a sprinting/tool-hidden player.

Verified in the original `aceofspades_source/aoslib/character.pyd.i64`, image
base `0x10000000`, `Character.draw` at `0x1004F120`:

- `weapon_object.needs_player_arms_drawing()` is read at `0x1005284F`, source
  `character.pyx:1891` (symbol `0x10097F70`).
- `tp_right_upper_arm.draw` is called through the attributes at `0x10052911`
  and `0x1005293C`, source line 1892; the lower arm follows at `0x10052A6E` /
  `0x10052A99`, source line 1893.
- The separate `can_display_weapon` read is later at `0x10053A66`, source
  line 1908 (symbol `0x10098148`).

The native third-person pose now suppresses only tool parts with that flag.
Zombie tools still own their own hands and do not receive duplicate class
arms. Missing held-tool geometry also preserves already available class arms.

Local CreatePlayer now completes its authoritative life, health and death
camera transition independently of unrelated remote-model upload failures.
First-person arm replacements commit both models together, and a successful
class/team change invalidates the held-tool mesh cache even when the tool ID
has not changed. A failed replacement retains the previous usable arm pair.

## Deployable placement previews

The native preview path only handled blocks, prefabs and UGC markers. It now
also handles rocket turrets, landmines, dynamite, medpacks, radar stations and
C4. Every preview uses the same `deployable_target` raw support voxel and face
as the placement packet, plus the same catalog parts, pivots, cosmetic palette,
team material and rig contact adjustment as the placed entity. The new pure
`entity_presentation_transform` is shared by both render paths.

Opacity is the original 0.3 from each weapon's `draw_ghosting` in
`aceofspades_source/aoslib/weapons`: `rocketTurretWeapon.py:68`,
`radarStationWeapon.py:56`, `landmineWeapon.py:65`, `dynamiteWeapon.py:90`,
`medPackWeapon.py:79`, and `c4Weapon.py:97`. `weapon.py:133` requires ammo in
either pool; the native preview follows that check. Invalid targets, dead
players, spectators and results cameras have no gadget ghost. The cached mesh
is rebuilt when type, team colour or cosmetic changes, and invalidated when
the gameplay lab clears all world models.

## Validation

Server: 69 tests passed across `test_tdm.py`, `test_audio_voting.py`,
`test_scoreboard.py`, `test_combat_scores.py`, and `test_simulation_order.py`.

Native targets for the combined build: `aos_match_overlays_tests`,
`aos_retail_character_pose_tests`, `aos_local_entity_tests`,
`aos_placement_render_tests`, and the client executable. The entity regression
checks every attachment face and support contact for all six gadget rigs.
All four native targets passed, and the executable was rebuilt and staged.
The rebuilt client completed live join and class-change/death/respawn smokes
against `192.248.177.80:32887`; the inspected respawn capture shows the class
arms and weapon. Evidence is in `out/evidence/retail-fixes-loadout-respawn/`.
The scripted captures do not establish every spectator animation or gadget
model's pixel-level parity. Both staged local server runtimes passed `--check`.

## Score and vote follow-up audit

The broader recheck found one additional timeout edge: a bounded mode-event
drain could leave an accepted kill behind its death event or a terrain-event
backlog. `BattleSpades/modes/base_mode.py` now captures the fixed number of
pending events at the first expired-clock check. `server/simulation_runtime.py`
drains only that remainder within the existing budget before finalizing the
time-limit winner. Normal mode ticks continue; later queued events cannot
prolong the deadline or add kill points before the result. With the default
8192-event queue limit and 512-event budget, even a saturated captured batch
settles within 16 ticks (about 0.27 seconds at 60 Hz). The real-player
TDM regression covers the normal path, a one-event budget, and the default
512-event budget with an existing backlog. Both backlog cases failed before
the fix and now retain the kill's personal/team points and correct winner.
A further regression continuously adds twice as much new work as the budget
can drain and verifies the fixed cutoff, winner and next-round reset.

The follow-up passed 100 server tests covering scoring, voting, simulation,
round resets, match transitions and recovered objective modes. The current
`native-dev/RelWithDebInfo` HUD, match-overlay, protocol168 player/runtime and
AoSPlay score tests all passed (five targets). Existing vote tests cover exact
wire tokens, invalid/duplicate voters, expired ballots, deterministic ties,
disconnects, replacement ballots and the closed winning-map announcement.

The final guard was rebuilt and staged in both local server copies. All 202
runtime files per copy match the fresh build; archive extraction matches the
current `modes.base_mode`, `server.simulation_runtime`, `server.voting`,
`server.scoreboard`, `server.combat_scores` and `modes.tdm` code objects. Both
staged `--check` runs passed with 28 maps, 40 prefabs, eight native imports and
the full-map worker. All 153/133 retained configuration/state files in the
client bundle/server release remained byte-identical. The audit is
`BattleSpades/tmp/runtime-stage-timeout-20260920.json`; the server executable
SHA-256 is `e851a287c75fb413f1f6ed062f0f1953195bee9c8501db739f50741e5c32a288`.
These server-side changes apply to the local BattleSpades server
packages; they do not modify the independently hosted legacy server at
`192.248.177.80:32887`. That server remains authoritative for awarded scores,
vote candidates, vote deadlines and the next map; the client displays and
transmits its protocol messages.

## Loopback gameplay gate

`tools/protocol168_local_gameplay_smoke.cpp` and the server repository's
`tools/run_local_gameplay_fixture.py` exercise the real local ENet connection,
MapSync, runtime packet decoder, terrain replica, chunk mesher, voxel collision
ray, voting model and map rejoin. The native tool accepts only a port and fixes
the destination to `127.0.0.1`. The server profile disables public registration,
Steam, plugins and ordinary bots; it never writes operator configuration.

Build `aos_protocol168_local_gameplay_smoke`, then run from `BattleSpades`:

```powershell
py -3.12 tools/run_local_gameplay_fixture.py --client out/build/native-dev/src/RelWithDebInfo/aos_protocol168_local_gameplay_smoke.exe --evidence tmp/local-gameplay-gate
```

The fixture creates an anchored test wall, places a real server turret and
fires one production projectile into it. It directly invokes turret fire to
make the collision deterministic; automatic targeting and line of sight are
covered separately by `test_rocket_turret.py`. It verifies that the native
terrain replica removes the crater cells, dirties/rebuilds their chunk, clears
the previous collision ray, and retains the supported player floor away from
the blast. This checks production mesh geometry; it is not a GPU screenshot.

The native voting model sends the exact wire choice back through ENet and
checks the closed winner announcement. A real lethal hit is accepted behind
queued work with a one-event drain budget, then the round clock expires. The
gate requires personal score 100, team score 1, the correct winner, received
KillAction/SetScore packets, MapEnded and a fresh MapSync for the elected map.
It also verifies the negotiated native flight profile on both joins. Evidence
is written to `server-result.json`, `server.log`, `client.stdout.log` and
`client.stderr.log` under the supplied evidence directory, and the fixture
cleans up both its client and server after completion or failure.

### Local completion, 2026-09-20 20:05

The command above exited 0 against `127.0.0.1:32890`. The native result was:

```text
next_map=Spooky Mansion
PASS local turret packets=9 removed_cells=9 dirty_chunk=1 rebuilt_mesh=1 collision_ray_clear=1 personal_score=100 team_score=1 vote_closed=1 map_ended=1 rejoined=1
```

`BattleSpades/tmp/local-gameplay-gate/server-result.json` records
`passed: true`, the nine authoritative crater cells, retained player floor,
winner team 2, and the change from `MayanJungle` to `SpookyMansion`. The adjacent
`server.log` shows both real loopback ENet joins and full MapSync transfers;
`client.stderr.log` is empty. The native probe also asserted the negotiated
Rocket/Glider/Engineer drains 30/9/7.5, grounded refill 20 after one idle second,
and descending-only parachutes on both joins. Its server and client were
stopped by the fixture afterwards.

Separately, `py -3.12 -m pytest tests/test_rocket_turret.py tests/test_projectiles.py
tests/test_combat.py tests/test_tdm.py tests/test_audio_voting.py
tests/test_match_transitions.py -q` passed all 173 tests in 40.66 seconds.
This completion run contacted no public or legacy server.
