# Weapon audio port

How every retail gun sound reaches the client, and why some tools are silent
on purpose.

## Why the catalog looked broken

Before this port, 43 of the 65 tool ids had an empty `shoot_sound` and a
hand-maintained table in the audio adapter rescued six of them. That table was
the wrong fix for a misdiagnosed problem.

Retail stores per-shot cues as class attributes, which
`tools/generate_weapon_catalog.py` walks correctly. But it stores **sustained**
cues as string literals inside method bodies:

```python
# aoslib/weapons/smgWeapon.py
shoot_sound = BLANK_SOUND                      # :19  -- deliberately empty
...
self.looping_fire_sound = self.play_sound(     # :73
    'smg_fire_loop', position=..., loops=0, zone=media.IN_WORLD_AUDIO_ZONE)
...
self.play_sound('smg_fire_tail', position=...) # :86
```

`shoot_sound = BLANK_SOUND` on an automatic weapon is **correct retail data**,
not a generator failure: the sustained loop lives in `update()`, so there is
nothing for a per-shot cue to play. An attribute walk can never recover it.

The fix was a schema change, not a scraper. `WeaponSoundSet`
(`include/battlespades/world/weapon_catalog.hpp`) adds thirteen roles that the
generator fills from `RECOVERED_SOUND_SETS`, a table recovered by reading all
71 retail weapon modules.

## The thirteen roles

| Role | Retail source | Notes |
|---|---|---|
| `fire_loop` | `play_sound(..., loops=0)` in `update()` | infinite; one per held trigger |
| `fire_tail` | one-shot when the loop closes | a loop without a tail cuts off abruptly |
| `spin_loop` | `minigunWeapon.py:98` | pitch is a **raw ratio** (`spin_speed / 5`), not semitones |
| `melee_miss` | `DiggingTool.miss_sound` | plays on **every** swing, hit or miss |
| `melee_hit_block` | `DiggingTool.hit_block_sound` | |
| `melee_hit_player` | `DiggingTool.hit_player_sound` | retail plays this on the **victim** at gain 0.75 |
| `empty_fire` | `Weapon.use_primary` dry fire | resolves to `empty.ogg` |
| `pin` | throwable hold-start | |
| `throw_release` | throwable release | |
| `tool_loop_start` / `tool_loop` / `tool_loop_stop` | start/sustain/stop state machines | block sucker, snow blower, paintbrush, turret |
| `tool_extra` | one extra tool-specific cue | e.g. the block sucker's pickup |

`shoot_sound`, `reload_sound` and `reload_done_sound` remain class attributes
and are unchanged.

## Silence is a feature

An empty role means **retail is genuinely silent there**. Never substitute a
similar-sounding sample. Seventeen tools are deliberately silent across every
role — blocks, most deployables, C4, the radar station, disguise. Retail plays
only `build_error` on a *failed* placement; the real placement sounds arrive
server-side through `SOUND_MAP`.

Two related traps:

- `hit_wet_block_sound` is `[BLANK_SOUND, ...]` in retail. Do not substitute
  `hitwater`; that is a separate server-driven `SOUND_MAP` id.
- `snowcan_reload` is declared by `snowBlowerWeapon.py:19` but **no such OGG
  ships**. It is unreachable because `is_reloadable()` returns `False`, so
  retail never requests it. `tests/test_weapon_audio_map.cpp` pins this as the
  single documented exemption, and asserts the file still does not exist — if
  someone later "fixes" it by substituting a sample, that test is where the
  decision has to be argued.

## What the test guarantees

`aos_weapon_audio_map_tests` fails the build if:

- any catalog cue names an OGG that is not on disk (expanding `foo_001-004`
  ranges to every variant),
- any automatic, spin-up or deployed-MG weapon has neither a shoot sound nor a
  fire loop — the exact defect that made guns silent,
- any fire loop lacks a tail,
- any melee tool is missing its swing, block-hit or player-hit cue,
- every dry/water explosive family resolves all of its authored variants,
- the classic +/-0.8-semitone pitch range keeps Python 2's exact truncation,
- the recovered loop weapons lose their identities on a regenerate.

This is what makes "every gun is mapped correctly" a checked property rather
than a claim.

## Remaining simplifications

The Drill is no longer simplified. Its projectile owns `drill_projectile` for
its complete packet-21/packet-10 lifetime. Each accepted drill `Damage(37)`
also plays `drill_drilling_exp` and refreshes the independent `drill_loop` for
the recovered 0.5 seconds (`Drill.drilling_loop_timeout`, alias A1509). The
voices follow the moving projectile and close on packet 19 or map teardown.

The Molotov is no longer simplified: its projectile fuse, placed block-fire
loop, water/dry impact, and pitched burnout cue follow the entity lifecycle and
are stopped on destroy or map teardown. Modern chemical, grenade-launcher,
sticky, mine-launcher and C4 explosions now choose from every authored numbered
bank instead of hard-coding `_001`; classic explosives retain the recovered
dry/water banks and +/-0.8-semitone variation.

The chemical bomb's *secondary* dissolve and burn phases remain incomplete.
Retail has distinct dissolve-loop/end and burn-loop/water-end cues, but the
current server protocol stream does not create the recovered BlockGoo lifecycle
that owns them. The client deliberately does not invent that authority from a
single explosion event.

Deployable creation uses the same catalog rule. Turret, landmine and dynamite
retain classic pitch; Medpack and C4 select across all three placement samples;
the Mine Launcher selects its dry pitched attach or three-sample water bank;
Radar keeps its authored single cue. These are presentation reactions to an
accepted CreateEntity lifecycle, not speculative local placement authority.

## Packet-synchronised spatial presentation

Retail does more than play the gun sample when it receives
`ShootFeedback(8)`. `GameScene.process_packet_shoot_feedback` calls the remote
character's `shoot(seed)`, and `aoslib.weapons.shoot_bullet` performs a local
world hitscan for every observer. That hitscan does not damage terrain; it owns
the positioned scenery-contact presentation.

The native client keeps the same authority boundary:

| Packet/event | Client presentation | Authority |
|---|---|---|
| local `Shoot(6)` | immediate muzzle, shot audio and cosmetic scenery raycast | server still owns damage |
| remote `ShootFeedback(8)` | remote muzzle/audio plus the seeded cosmetic scenery raycast | observer-only |
| `ShootResponse(9)` | five blood particles, hit-confirm cue and crosshair response | server-authored victim result |
| `Damage(37)` | VXL darkening/removal; break burst when destroyed | server-authored terrain result |
| `HitEntity(20)` | entity sparks at the packet position | server-authored entity result |
| `UseOrientedItem(10)` and entity packets | projectile model, trail, impact/explosion family | server lifecycle, client visuals |
| `ExplodeCorpse(36)` | grave/corpse debris and death particles | server-authored edge |

Cosmetic hits are retained for 30 world loops. When `Damage(37)` acknowledges
the same player and voxel, a sublethal acknowledgement updates only VXL damage
and does not replay the particles or contact sound. A destructive
acknowledgement still emits the larger block-break burst and break sample.
The replay uses the packet's one-byte seed with CPython 2.7's MT19937 seeding
and three axis draws, not C++'s incompatible `std::mt19937(seed)` sequence.

All positional one-shots, weapon loops and server-created loops pass through a
five-ray query against the current VXL before OpenAL distance attenuation.
Open paths keep full direct gain; partial apertures interpolate; full cover
retains an 18% low-frequency leak so an important nearby blast is muffled
rather than erased. Sources at or beyond retail's 50-block hearing range are
culled before ray traversal. UI cues, music and the non-positional ambient bed
remain head-relative and are never terrain-occluded.

The raycast is read-only. It cannot remove a block, create a hit, or affect
server reconciliation.

## Regenerating

```bash
py -3 tools/generate_weapon_catalog.py
```

Never hand-edit `src/world/weapon_catalog.generated.cpp`. Its rows use
**positional** aggregate initialisation, so a field inserted anywhere except
the documented position silently mis-assigns every string after it.
