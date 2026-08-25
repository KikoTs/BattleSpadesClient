# Entity Constants — Authoritative Clean-Room Spec

Merged from the entity-recovery pass and the adversarial verification pass. Every
correction from the verifier has been applied. This file supersedes the entity tables in
`docs/ENTITY_PORT_RECOVERY.md`.

## Source priority

1. `G:/AoSRevival/aceofspades_source/shared/constants.py` — retail constants. Has a **named**
   block and an **A-alias** block. The A-alias block is what the shipped modules bind; where
   the two disagree the alias wins.
2. `G:/AoSRevival/aceofspades_source/aoslib/weapons/*.py`, `aoslib/scenes/main/*.py` — bindings.
3. `G:/AoSRevival/aceofspades_source/aoslib/models.py` — model bindings, KV6 load offsets.
4. `G:/AoSRevival/BattleSpades/**` — our server. **Not retail.** Anything found only here is
   provenance `battlespades`.

## Provenance values

| value | meaning |
|---|---|
| `retail_alias` | read from the A-number block (`A1632 = 8`) |
| `retail_named` | read from the named block (`DYNAMITE_EXPLOSION_RADIUS = 5`) |
| `retail_literal` | hardcoded in a retail `.py` behaviour file or decoded from a retail `.pyd` class body |
| `battlespades` | only exists in `G:/AoSRevival/BattleSpades` — an invention |
| `absent` | not recoverable from any source; value is `null` |

## Reading the tables

* `[UNBOUND]` in the source column: the constant is real retail data, but **no recovered code
  reads it**. The field label is inference from the constant's name. Port the number, do not
  trust the meaning.
* `[INFERRED SLOT]`: the value comes from an A-alias with no named twin and no binding. Its
  position in the cluster is what assigns it a meaning.
* `[DEAD]`: the attribute exists in retail but is never read by any recovered code.
* A `null` value is a **correct answer**. Do not fill it in.

## Entity id enum (verified)

`shared/constants.py:2801` — `xrange(40)` tuple; `:2802` `ENTITY_LIST`; `:2803-2842` aliases
`A899..A938` mapping 1:1 onto ids 0..39.

**Wire safety.** Scanning `gameScene.pyd` for each id alias: 35 of 40 are present, five are
absent — `A899` (FLAG=0), `A900` (BASE=1), `A905` (JETPACK_CRATE=6), `A911` (CORPSE_ENTITY=12),
`A925` (TANK_ENTITY=26). Sending one of those five through `CreateEntity` (packet 21) makes
`GameScene.create_entity` index `GameScene.ENTITIES` with an unsupported key. For BASE this was
live-measured: `KeyError: 1`, client freeze (`BattleSpades/docs/HANDOFF.md:741-745`).
**Never put ids 0, 1, 6, 12 or 26 on the wire.**

---

## 0 — FLAG

| field | constant | value | provenance | source |
|---|---|---|---|---|
| type_id | `FLAG` / `A899` | 0 | retail_named | `shared/constants.py:2801`, alias `:2803` |
| wire_safe | — | false | retail_literal | `A899` absent from `gameScene.pyd` — no client class |
| everything else | — | null | absent | no flag.py module, no Flag class among the 60 compiled scene modules in `gameScene.pyd` |

Not recovered beyond the id. Retail has no client-side FLAG entity.

---

## 1 — BASE

| field | constant | value | provenance | source |
|---|---|---|---|---|
| type_id | `BASE` / `A900` | 1 | retail_named | `shared/constants.py:2801`, alias `:2804` |
| wire_safe | — | false | retail_literal | `A900` absent from `gameScene.pyd`; no Base/CommandPost class among its 64 classes, no `base.py` among its 60 scene modules |
| what_breaks | — | `GameScene.create_entity` raises `KeyError: 1`, client freezes | battlespades | live-measured, `BattleSpades/docs/HANDOFF.md:740-745`; mitigation `server/entities/registry.py:72-79` (`wire_visible=False`), filtered at `:156` |
| model | — | null | absent | **no** BASE model binding exists in `models.py`. The `cp.kv6` attribution in `ENTITY_PORT_RECOVERY.md:32` is unconfirmed and could not be reproduced — `models.py:339` binds `CP_MODEL` to the capture point only |
| model_size | — | null | absent | no model |
| touch_radius | — | null | absent | no BASE distance constant. Related but different: `CLASSIC_CTF_BASE_CAPTURE_DISTANCE = 5` (`constants_gamemode.py:497`, `A2645 :498`), `CLASSIC_CTF_INTEL_MIN_RADIUS_FROM_BASE = 3` (`:499`, `A2646 :500`) — neither alias is in any `.pyd`. The generic `ENTITY_RADIUS = 5.0` (`constants.py:4980`, `A2260 :4981`) **is** referenced by `gameScene.pyd` |
| restock_behaviour | — | `set_hp(100)`, throttled to once per 3 s | absent | **reference-server only, not retail.** `aceofspades_source/server/aosserver/connection.py:1128-1131` — per-tool restock and `send_loader` both commented out at `:1130-1131`. Gated `ctf.py:84-86`; `cctf.py:143-145` **additionally requires the player not be carrying a flag** (`cctf.py:141 if carrying_flag is None:`) |
| team_only | — | null | absent | **not a BASE property.** `ctf.py:81` / `cctf.py:131` gate on `base.team != player.team`, but `dem.py:62-65` creates its CommandPosts with **no team kwarg at all**, and `types.py:240-242` (`class CommandPost(Entity): type = BASE`) carries no team field. Three handlers, three different gates |
| health / blast_radius / light_radius / respawn_delay | — | null | absent | none exist |

**CommandPost bindings (all three).** `ctf.py:37` → restock; `cctf.py:51` → restock;
**`dem.py:53` → `on_site_collide`, which plants a C4 bomb and never restocks**
(`dem.py:83-90`, gated on bomb carrier + terrorist team, 3 s `de_last_plant` throttle).
`dem.py:62-65` is the **only** place in the tree that actually instantiates a CommandPost —
`ctf.py:51-53` has both `create_entity(types.CommandPost, ...)` lines commented out. All of it
is inert anyway: `types.py:129-148` has `do_gravity()`/`do_collide()` and the radius-3 body
commented out.

**Not a BASE field but adjacent:** `BASE_ZONE_TINT_ALPHA = 150` (`constants.py:5051`,
alias `A2286 :5056`, provenance **retail_alias** — `A2286` is what the binary binds). Only
consumers are `aoslib/hud/hud.pyd`, `aoslib/hud/hud_old.pyd`, `aoslib/hud.hud.pyd` — i.e. it
is **live HUD code**, not legacy. Companions: `BASE_ZONE_DISTANCE_TOLERANCE = 0.5` (`:5052`),
`BASE_PLAYER_ZONE_DISTANCE_TOLERANCE_XY = 0.3` (`:5053`), `_ZEYES = 0.0` (`:5054`),
`_ZFEET = -0.1` (`:5055`). These describe an authored **zone volume**, not entity 1.

---

## 2 — HELICOPTER

| field | constant | value | provenance | source |
|---|---|---|---|---|
| type_id | `HELICOPTER` / `A901` | 2 | retail_named | `shared/constants.py:2801`, alias `:2805` |
| wire_safe | — | true | retail_literal | `A901` present in `gameScene.pyd`; a compiled `helicopter.py` module exists (referenced at `gameScene.pyd:0x101C0D08`, file string `off_1025D070`) |

Not recovered. Out of scope for this pass.

---

## 3 — AMMO_CRATE

Client class `AmmoCrate(Crate)`, compiled `ammoCrate.py` lines 4-6 at
`gameScene.pyd:0x101C4900-0x101C4A7E`. Base `Crate` class body at `0x101C41D4-0x101C43C9`,
`Crate.initialize` = `sub_1009EC90`.

| field | constant | value | provenance | source |
|---|---|---|---|---|
| type_id | `AMMO_CRATE` / `A902` | 3 | retail_named | `constants.py:2801`, alias `:2806`. Client registry binds the **alias**: `gameScene.pyd` `PyDict_SetItem(entity_map, A902, AmmoCrate)` |
| model | `AMMO_MODEL`, `CRATE_PARACHUTE_MODEL` | `['ammocrate','Crate_Parachute']` | retail_literal | `gameScene.pyd:0x101C494E-0x101C49E4` (ammoCrate.py:6). `PyList_New(2)`. Assets `models.py:344`, `:387` |
| icon | `AmmoCrate.icon` | null | retail_literal | `0x101C491E` — explicit `None`. `minimap_ammocrate` (`constants.py:4382`) is a UGC drop-point icon only |
| model_size | `Crate.size` | 0.05 | retail_literal | `0x101C4290` (crate.py:15), `dbl_10254B78 = 0x3FA999999999999A`. Entity default is 1.0 (`entity.py:31,34`) |
| model_z_offset | `Crate.model_position_offsets[0].z` | -0.65 | retail_literal | `sub_1009EC90` (crate.py:26), `dbl_102543F0 = 0xBFE4CCCCCCCCCCCD` |
| parachute_model_size | `Crate.parachute_model_size` | 0.12 | retail_literal | `0x101C4378` (crate.py:19), `dbl_1025B0B8`. Applied `display[1].size` at `0x1009F672-0x1009F78A` (crate.py:29) |
| parachute_model_z_offset | `Crate.model_position_offsets[1].z` | -1.35 | retail_literal | `sub_1009EC90` (crate.py:26), `dbl_102543E8` |
| sq_parachute_deployment_height | `A1040 * A1040` | **100** | retail_alias | `0x1009F889-0x1009F92D` (crate.py:32). The stored attribute is the **square**. `A1040 = CRATE_PARACHUTE_DEPLOYMENT_HEIGHT = 10` at `constants.py:3043/:3044`. Client loads the global string `"A1040"` (`0x1028FB84`) |
| sq_parachute_removal_height | `A1041 * A1041` | **4** | retail_alias | `0x1009F95C-0x1009F9FF` (crate.py:33). `A1041 = 2` at `constants.py:3046/:3047`, global string `"A1041"` (`0x1028E4E8`) |
| parachute_slowdown | `A1042` | 0.75 | retail_alias | `constants.py:3049/:3050`. `"A1042"` (`strtab:1400`) is loaded at `0x100A1627` inside `sub_1009FA40` (crate.py:70) and fed to `PyNumber_Multiply`. **Multiplicand not identified** — "velocity scale on chute deploy" is unproven |
| max_bounces | `Crate.max_bounces` | 1 | retail_literal | `0x101C4325` (crate.py:17). **[DEAD]** — `"max_bounces"` (`0x1028E208`) has exactly one code reference, the class-body `SetItem`. Nothing compares a bounce count against it |
| bounces | `Crate.bounces` | 0 | retail_literal | `0x101C426A` (crate.py:14). **[DEAD]** — never incremented or compared; the only other xref to `"bounces"` belongs to `helicopter.py` |
| vel | `Crate.vel` | 0.0 | retail_literal | `0x101C41F9` (crate.py:13). **[DEAD]** — `"vel"` (`0x1028E638`) is never read or written by `Crate.update` (`sub_1009FA40`) |
| drop_sound | `Crate.drop_sound` | null | retail_literal | `0x101C42F3-0x101C4304` (crate.py:16) — explicit `None` |
| landing_sound_threshold | `Crate.landing_sound_threshold` | 2 | retail_literal | `0x101C434C` (crate.py:18). **Confirmed live**: read at `0x100A04C7` inside `sub_1009FA40` |
| landing_sound | `CRATEDROP_LAND_SOUND` / `A2783` | `['cratedrop_land',-1,100,-0.8,+0.8]` | retail_alias | `constants_audio.py:151`, alias `:152`. **Confirmed live**: `"A2783"` read at `0x100A08A3` in `sub_1009FA40` |
| touch_radius | `CRATE_DISTANCE` / `A1019` | 2.5 | retail_named | `constants.py:2980`, alias `:2981`, alias-dump `shared/backup/constants-copy.py:1992`. **[UNBOUND]** — no `.py` reads it and `A1019` is absent from `gameScene.pyd`. `PICKUP_DISTANCE = 3.0` / `A1020` is a **different** constant whose 24 xrefs live only in `BombPickup.update`, `DiamondPickup.update`, `IntelPickup.update` |
| respawn_delay | `CRATE_SPAWN_DELAY` / `A1039` | 25 | retail_named | `constants.py:3040`, alias `:3041`. **[UNBOUND]** — `A1039` absent from `gameScene.pyd`. It is the default of match rule `RULE_CRATES_SPAWN_TIME` (`constants_matchmaking.py:413`), legal values `A2672 = NUMBERS_10_TO_60_STEP_5` (`:162`, dict `:135`). **The unit is nowhere stated in retail** — "25 s" is an assumption |
| ammo | `<per-weapon>_AMMO_RESTOCK_AMOUNT` | null | absent | There is **no** ammo number on the crate. Type id 3 is delivered to every equipped tool's `restock(type)`. See "Restock semantics" below |
| pickup_sound_id | `CRATE_SOUND_ID` | 13 | **battlespades** (binding) | Value 13 is retail: member 13 of the 61-entry tuple at `constants_audio.py:529`, `CRATE_SOUND` at `:407`, alias `A2987 :543`, table `:593`. **But no retail source binds it to AMMO_CRATE** — `CRATE_SOUND_ID` occurs in exactly one file (`constants_audio.py`) and neither `A2987` nor the name is in `gameScene.pyd`. The only source stating "13 = ammo crate pickup" is `BattleSpades/server/audio.py:35` |
| uses | — | 1 | battlespades | `BattleSpades/server/entities/behaviors.py:197-206` — `on_touch` refills, sets `ent.alive = False`, schedules `ent.respawn_at`. No retail use count exists |
| team_only | — | null | absent | The identifier `team_only` does not exist in retail **or** in BattleSpades (repo-wide grep: zero hits). `registry.py:55` is `state: int = TEAM_NEUTRAL`, a wire team index, not a pickup restriction. `PickupCrateBehavior.on_touch` has no team test |

**Descent audio.** `'cratedrop_freefall'` (`strtab:2530`) and `'cratedrop_chuteopen'` (`strtab:2529`)
exist, but their only xrefs are inside `sub_10001B60`, the module-wide string-creation routine —
**their use by `Crate` is not established from the binary.**

**Restock semantics (retail, exact).** `Weapon.restock` (`aoslib/weapons/weapon.py:77-89`):

```
max_ammo, initial_ammo, max_clip, initial_stock, restock_amount = self.ammo
if type == AMMO_CRATE:
    if max_clip == 0 or max_clip == None:
        self.current_ammo = min(self.current_ammo + restock_amount, max_ammo)
    else:
        self.current_clip = min(self.current_clip + restock_amount, max_clip)
else:
    self.current_ammo, self.current_clip = initial_ammo, initial_stock
self.update_ammo()
if type == AMMO_CRATE and self.character.main and self.character.weapon_object is self \
        and self.is_reloadable() and self.current_ammo == 0:
    self.character.reload_next_update = True
```

The tuple naming is **misleading**: modules pass
`ammo = (CLIP_SIZE, CLIP_SIZE, AMMO_MAX, INITIAL_STOCK, RESTOCK_AMOUNT)`
(`pistolWeapon.py:25` — `PISTOL_AMMO_CLIP_SIZE = 6` magazine vs `PISTOL_AMMO_MAX = 30` reserve,
`constants.py:3314/:3323`). So the unpacked `max_clip` is the **reserve pool max** and
`max_ammo` is the **magazine size**. A weapon with no reserve pool gets its magazine topped up;
everything else gets its **reserve** topped up. The magazine is never refilled directly.

`Tool.restock` (`tool.py:175-180`) is the same shape with `self.count` /
`self.default_count`; `Tool.restock_amount` defaults to 0 (`tool.py:38`). Overrides that ignore
the crate entirely: `blockSuckerWeapon.py:158`, `diggingTool.py:70`, `snowBlowerWeapon.py:96`.

---

## 4 — HEALTH_CRATE

Client class `HealthCrate(Crate)`, compiled `healthCrate.py` lines 4-8 at
`gameScene.pyd:0x101C4BBF-0x101C4E8F`.

| field | constant | value | provenance | source |
|---|---|---|---|---|
| type_id | `A903` (= `HEALTH_CRATE`) | 4 | retail_alias | Client binds the **alias**: `gameScene.pyd:0x101E9513` does `PyDict_SetItem(entity_map, A903, HealthCrate)` (`gameScene.py:219`). Named at `constants.py:2801`, alias `:2807` |
| model | `HEALTH_MODEL`, `CRATE_PARACHUTE_MODEL` | `['healthcrate','Crate_Parachute']` | retail_literal | `0x101C4C13-0x101C4CA7` (healthCrate.py:6); `models.py:343`, `:387` |
| icon | `HealthCrate.icon` | null | retail_literal | `0x101C4BE2` — explicit `None` |
| needs_shadow | `HealthCrate.needs_shadow` | true | retail_literal | `0x101C4CD4-0x101C4D07` (healthCrate.py:7). **Unique to this crate** |
| spot_shadow_pos_offset | `HealthCrate.spot_shadow_pos_offset` | `(0.5, 0.5, 0.0)` | retail_literal | `0x101C4D36-0x101C4E03` (healthCrate.py:8), `dbl_102506D0 = 0x3FE0...`. Overrides `Entity.spot_shadow_pos_offset = (0,0,0)` (`entity.py:32`). **Unique to this crate** |
| model_size | `Crate.size` | 0.05 | retail_literal | inherited, `0x101C4290` |
| model_z_offset | `Crate.model_position_offsets[0].z` | -0.65 | retail_literal | inherited |
| parachute_model_size | `Crate.parachute_model_size` | 0.12 | retail_literal | inherited |
| parachute_model_z_offset | `Crate.model_position_offsets[1].z` | -1.35 | retail_literal | inherited |
| sq_parachute_deployment_height | `A1040 * A1040` | 100 | retail_alias | inherited (crate.py:32) |
| sq_parachute_removal_height | `A1041 * A1041` | 4 | retail_alias | inherited (crate.py:33) |
| parachute_slowdown | `A1042` | 0.75 | retail_alias | `constants.py:3049/:3050` |
| max_bounces | `Crate.max_bounces` | 1 | retail_literal | inherited **[DEAD]** |
| bounces | `Crate.bounces` | 0 | retail_literal | inherited **[DEAD]** |
| vel | `Crate.vel` | 0.0 | retail_literal | inherited **[DEAD]** |
| drop_sound | `Crate.drop_sound` | null | retail_literal | inherited |
| landing_sound_threshold | `Crate.landing_sound_threshold` | 2 | retail_literal | inherited, live |
| touch_radius | `CRATE_DISTANCE` / `A1019` | 2.5 | retail_named | `constants.py:2980` **[UNBOUND]** |
| respawn_delay | `CRATE_SPAWN_DELAY` / `A1039` | 25 | retail_named | `constants.py:3040` **[UNBOUND]**, unit unstated |
| **health (heal amount)** | — | **null** | **absent** | **CRITICAL: the heal amount is not in retail.** No `HEALTH_CRATE`/`HEALTHCRATE` heal constant exists. Do not invent it |
| kill_type | `HEALTHCRATE_HP` / `A441` | 20 | retail_named | `constants.py:1146` (member 20 of the 37-entry kill enum, between `SHRAPNEL_KILL`=19 and `SNOWBALL_KILL`=21), alias `:1168`. **Despite the name this is a kill/damage-source TYPE ID, not a hit-point quantity.** **[UNBOUND]** — no consumer in any `.py`, and neither name nor alias is in `gameScene.pyd`, so "retail routes healing through the kill channel" is inference from the enum name only |
| pickup_sound_id | `HEALTHCRATE_SOUND_ID` | 14 | retail_named | member 14 of `constants_audio.py:529`; `HEALTHCRATE_SOUND` `:408`, alias `A2988 :544`, table `:594`. **[UNBOUND]** — neither name nor `A2988` in `gameScene.pyd`; the pickup role is inferred from the asset name. `sounds/healthcrate.ogg` exists |
| uses | — | 1 | battlespades | `behaviors.py:197-206` |
| team_only | — | null | absent | same as entity 3 |

---

## 5 — BLOCK_CRATE

Display class `BlockCrate(Crate)` at `gameScene.pyd:0x101C4FD6-0x101C513B` (blockCrate.py:4-6).
**The gameplay effect is bound in a different module:** `aoslib/character.pyd ::
Character.restock = sub_1001A470` (character.py:363-381). Packet entry point is
`gameScene.pyd :: GameScene.process_packet_restock = sub_1019A900`.

| field | constant | value | provenance | source |
|---|---|---|---|---|
| type_id | `A904` (= `BLOCK_CRATE`) | 5 | retail_alias | `constants.py:2801`, alias `:2808`; client registers via `A904` at `gameScene.pyd:0x101E95A0`. Do not confuse with `BLOCK_CRATE_DROP_POINT_ENTITY = 20` |
| model | `BLOCK_CRATE_MODEL`, `CRATE_PARACHUTE_MODEL` | `['block_crate','Crate_Parachute']` | retail_literal | `0x101C5029-0x101C50BB` (blockCrate.py:6); `models.py:345`, `:387` |
| icon | `BlockCrate.icon` | null | retail_literal | `0x101C4FF9` — explicit `None` |
| **blocks_granted** | `A2399` (= `CLASS_BLOCKS`) indexed by class, slot `[1]` | `int(CLASS_BLOCKS[class.id][1] * scene.manager.block_wallet_multiplier)` | **retail_alias** | **`aoslib/character.pyd :: Character.restock = sub_1001A470`.** `0x1001A5F2` loads global `"A904"` (`dword_10098754`); `0x1001A624` `PyObject_RichCompare(type, A904, Py_EQ)` at line 367. Inside the branch: `0x1001A72F` loads `"A2399"` (`dword_10097FBC`), `0x1001A74F` getattr `game_class.id`, `0x1001A77E` `PyObject_GetItem`, subscript `[1]`, `0x1001A8D4` getattr `self.scene.manager.block_wallet_multiplier`, `PyNumber_Multiply`, `int(...)`, `0x1001A9B4` setattr `self.block_count`. `A2399 = CLASS_BLOCKS` at `constants.py:5330`, table `:5311-5328`, per-class `*_MAX_BLOCKS` at `:1432-1495` |
| class_max_blocks | `<CLASS>_MAX_BLOCKS` | SOLDIER 1000, SCOUT 1000, ROCKETEER 1500, ENGINEER 3000, MINER 1000, ZOMBIE 2000, CLASSIC_SOLDIER 100, GANGSTER 1200, UGCBUILDER 1, SPECIALIST 1000, MEDIC 2000 | retail_named | `constants.py:1432-1495`; pairing table `:5311-5328` |
| model_size | `Crate.size` | 0.05 | retail_literal | inherited |
| model_z_offset | `Crate.model_position_offsets[0].z` | -0.65 | retail_literal | inherited |
| parachute_model_size | `Crate.parachute_model_size` | 0.12 | retail_literal | inherited |
| parachute_model_z_offset | `Crate.model_position_offsets[1].z` | -1.35 | retail_literal | inherited |
| sq_parachute_deployment_height | `A1040 * A1040` | 100 | retail_alias | inherited |
| sq_parachute_removal_height | `A1041 * A1041` | 4 | retail_alias | inherited |
| parachute_slowdown | `A1042` | 0.75 | retail_alias | `constants.py:3049/:3050` |
| max_bounces / bounces / vel | `Crate.*` | 1 / 0 / 0.0 | retail_literal | inherited **[DEAD]** |
| drop_sound | `Crate.drop_sound` | null | retail_literal | inherited |
| landing_sound_threshold | `Crate.landing_sound_threshold` | 2 | retail_literal | inherited, live |
| touch_radius | `CRATE_DISTANCE` / `A1019` | 2.5 | retail_named | `constants.py:2980` **[UNBOUND]** |
| respawn_delay | `CRATE_SPAWN_DELAY` / `A1039` | 25 | retail_named | `constants.py:3040` **[UNBOUND]**, unit unstated |
| pickup_sound_id | `CRATE_BLOCKS_SOUND_ID` | 15 | retail_named | member 15 of `constants_audio.py:529`; `CRATE_BLOCKS_SOUND` `:409`, alias `A2989 :545`, table `:595`. **[UNBOUND]** — `crate_blocks` is not in the `gameScene.pyd` string table |
| uses | — | 1 | battlespades | `behaviors.py:197-206` |
| team_only | — | null | absent | `registry.py:55` is a wire team index |

**Restock routing (settled).** `GameScene.process_packet_restock` is a one-line forwarder:
`self.character.restock(type=packet.type)` (getattr `character` `dword_1028D888`, getattr
`restock` `dword_1028F6A4`, kwargs `{'type': ...}`, `PyObject_Call` at `0x1019AA31`).
`Character.restock` then does `if type == A904:` → set `block_count` → **return**. The
per-tool loop (`character.py:374-379`) is the **else** branch, and a further `if type != A904`
guard gates the trailing scene call at line 380. **A block-crate restock never reaches any
`Weapon.restock`/`Tool.restock`, so no tool is reset.**

---

## 6 — JETPACK_CRATE

`JetpackCrate(Crate)` at `gameScene.pyd:0x101C997E-0x101C9AAA` (jetpackCrate.py:4-6).

**Unreachable dead content.** The class is created and stored in the module dict but **never
registered**: `"A905"` does not appear anywhere in `gameScene.pyd`, and the only two xrefs to
the `"JetpackCrate"` name string (`0x1028A478`) are the class-creation name arg (`0x101C9AAA`)
and the module-dict insert (`0x101C9AF1`). `AmmoCrate`/`HealthCrate`/`BlockCrate` each have a
third xref — the registry insert. **Retail has no id 6 → class mapping, so a network spawn of
type 6 never constructs the class.** (The `display[1]` IndexError at crate.py:29 is real but
unreachable — do not replicate a crash path retail cannot enter.)

| field | constant | value | provenance | source |
|---|---|---|---|---|
| type_id | `JETPACK_CRATE` / `A905` | 6 | retail_named | `constants.py:2801`, `ENTITY_LIST :2802`, alias `:2809`. These are its **only three** occurrences in the retail tree |
| wire_safe | — | false | retail_literal | `A905` absent from `gameScene.pyd` |
| model | `JETPACK_MODEL` | `['jetpack']` | retail_literal | `0x101C99D3-0x101C9A2E` (jetpackCrate.py:6). **`PyList_New(1)`** at `0x101C99F7` — a **one**-element list, unlike the other three crates. So the jetpack crate genuinely has **no parachute model**. `models.py:350` `load_model('JETPACK_MODEL','jetpack')`, no offset. `kv6/jetpack.kv6` exists |
| icon | `JetpackCrate.icon` | null | retail_literal | `0x101C99A1` — explicit `None` |
| model_size | `Crate.size` | 0.05 | retail_literal | inherited |
| model_z_offset | `Crate.model_position_offsets[0].z` | -0.65 | retail_literal | inherited (the list is built unconditionally, so the -1.35 entry exists but is unusable) |
| parachute_model_size | `Crate.parachute_model_size` | 0.12 | retail_literal | inherited but **inert** — crate.py:29 assigns `display[1].size` and this entity has only `display[0]` |
| sq_parachute_deployment_height | `A1040 * A1040` | 100 | retail_alias | inherited; no chute model to show |
| sq_parachute_removal_height | `A1041 * A1041` | 4 | retail_alias | inherited |
| max_bounces / bounces / vel | `Crate.*` | 1 / 0 / 0.0 | retail_literal | inherited **[DEAD]** |
| drop_sound | `Crate.drop_sound` | null | retail_literal | inherited |
| landing_sound_threshold | `Crate.landing_sound_threshold` | 2 | retail_literal | inherited |
| touch_radius | — | **null** | **absent** | No jetpack-specific radius exists **and** `Crate` has no `touch_radius` attribute at all — the compiled `Crate` class body defines only `vel`, `bounces`, `size`, `drop_sound`, `max_bounces`, `landing_sound_threshold`, `parachute_model_size`. Port 2.5 by convention if you ship this entity; it is not recovered data |
| respawn_delay | — | **null** | **absent** | `CRATE_SPAWN_DELAY` is a match-rule default, not a `Crate` attribute. BattleSpades uses 15.0 (`server/map_resources.py:95`) |
| fuel_granted | — | null | absent | Nothing in retail states what this crate gives. BattleSpades: `self.jetpack_fuel = float(_JETPACK_PROPERTIES[self.jetpack_id].get(1, 100.0))` at `server/player.py:1590` |
| pickup_sound_id | — | null | absent | No jetpack-crate id in the 61-entry tuple. BattleSpades reuses `SND_CRATE` = 13 (`map_resources.py:96`) |
| uses | — | **null** | **absent** | `behaviors.py:197-206` makes it a **respawning** pickup (`alive=False` + `respawn_at`), not a 1-use consumable. The only `uses` counter in that file belongs to `MedpackBehavior` (`:255`, default 3) |
| team_only | — | null | absent | identifier does not exist |

---

## 7 — MACHINE_GUN

**No Python entity class exists.** There is no `machineGun.py` in `aoslib/scenes/main/`; the
class exists only compiled inside `gameScene.pyd` (`'MachineGun'` at `strtab:2120`). Placement
is compiled too: `'PlaceMG'` `:2144`, `'send_place_mg'` `:3616`, `'place_mg'` `:3364`. All
behavioural data below comes from `MGWeapon` (`aoslib/weapons/mgWeapon.py:22`), the carried side.

Named block `constants.py:6497-6550` and alias block `:3800-3853` (`A1546`-`A1599`) **agree on
all 54 entries**. Backup dump `shared/backup/constants-copy.py:2520-2573` is byte-identical
to `constants.py:3800-3853`.

| field | constant | value | provenance | source |
|---|---|---|---|---|
| type_id | `MACHINE_GUN` / `A906` | 7 | retail_named | `constants.py:2801`, alias `:2810` |
| health | `MG_HEALTH` / `A1598` | 100 | retail_named | `:6549` / `:3852`. Placed entity. **[UNBOUND]** |
| ammo (entity) | `MG_AMMO` / `A1599` | 999 | retail_named | `:6550` / `:3853`. The **placed entity's** ammo, distinct from the carried weapon. **[UNBOUND]** |
| clip_size | `MG_AMMO_CLIP_SIZE` / `A1586` | 100 | retail_alias | `:3840` / `:6537`. Slots 0+1 of `mgWeapon.py:35` |
| reserve_ammo_max | `MG_AMMO_MAX` / `A1583` | 400 | retail_alias | `:3837` / `:6534`. Slot 2. Initial stock `A1584` and restock `A1585` also 400 (`:3838-3839` / `:6535-6536`). `clip_reload = False` (`mgWeapon.py:36`) |
| blast_radius | `MG_EXPLOSION_RADIUS` / `A1593` | 3.0 | retail_named | `:6544` / `:3847`. Death blast. **[UNBOUND]** |
| blast_damage | `MG_EXPLOSION_DAMAGE` / `A1594` | 100 | retail_named | `:6545` / `:3848` |
| block_damage (blast) | `MG_EXPLOSION_BLOCK_DAMAGE` / `A1595` | 5 | retail_named | `:6546` / `:3849`. **Do not confuse with `MG_DAMAGE_BLOCK = 2`** |
| blast_knockback_max | `MG_EXPLOSION_KNOCKBACK_MAX` / `A1596` | 1.0 | retail_named | `:6547` / `:3850` |
| blast_knockback_min | `MG_EXPLOSION_KNOCKBACK_MIN` / `A1597` | 0.2 | retail_named | `:6548` / `:3851` |
| model_size | `MG_MODEL_SIZE` / `A1589` | 0.06 | retail_alias | `:3843` / `:6540`. Bound as `entity_size` at `mgWeapon.py:60` |
| model_z_offset_base | `MG_BASE_MODEL_OFFSET_Z` / `A1590` | -1.5 | retail_named | `:6541` / `:3844` = `-25.0 * MG_MODEL_SIZE`. **[UNBOUND]** — belongs to the compiled entity class |
| model_z_offset_top | `MG_TOP_MODEL_OFFSET_Z` / `A1591` | -1.5 | retail_named | `:6542` / `:3845`. Base and top share the same offset (unlike the turret's three) |
| model_position_offset_xyz | — | `(0.0, 1.0, 0.5)` | retail_literal | `mgWeapon.py:96`. Manned-view offset applied to every entity part |
| placement_far_radius | `MG_FAR_RADIUS` / `A1592` | 5.0 | retail_named | `:6543` / `:3846`. **[UNBOUND]** — inferred by analogy with `rocketTurretWeapon.py:58`, which passes `far_radius=A1603` (the alias, not the named constant) |
| yaw_range | `MG_HORIZONTAL_ANGLE_RANGE` / `A1588` | 45 | retail_alias | `:3842` / `:6539`; clamp at `mgWeapon.py:234-237` |
| pitch_range | `MG_VERTICAL_ANGLE_RANGE` / `A1587` | 45 | retail_alias | `:3841` / `:6538`; clamp at `mgWeapon.py:238-241` |
| deployment_time | `MG_DEPLOYMENT_TIME` / `A1546` | 3.0 | retail_alias | `:3800` / `:6497`; `mgWeapon.py:61,87,115,192,204,351` |
| withdrawal_time | `MG_WITHDRAWAL_TIME` / `A1547` | 0.75 | retail_alias | `:3801` / `:6498`; `mgWeapon.py:113,201,261,349` |
| range | `MG_RANGE` / `A1548` | 300 | retail_alias | `:3802` / `:6499`; deployed twin `MG_DEPLOYED_RANGE` `A1549` = 300 (`:3803`) |
| reload_time | `MG_RELOAD_TIME` / `A1550` | 4.0 | retail_alias | `:3804` / `:6501`; deployed twin `A1551` = 4.0 |
| shoot_interval | `MG_SHOOT_INTERVAL` / `A1553` | 0.5 | retail_alias | `:3807` / `:6504`. **Deployed: `MG_DEPLOYED_SHOOT_INTERVAL` `A1554` = 0.1** (`:3808` / `:6505`) |
| shoot_delay | `MG_DELAY` / `A1552` | 0.11 | retail_named | `:6503` / `:3806`. **[UNBOUND]** — `mgWeapon.py:28` binds `A1553`, never `A1552`. Consumer unidentified. `SMG_DELAY` and `CLASSIC_SMG_DELAY` share 0.11 |
| damage_torso | `MG_DAMAGE_TORSO` / `A1571` | 30 | retail_alias | `:3825` / `:6522`; `mgWeapon.py:68-69` |
| damage_head | `MG_DAMAGE_HEAD` / `A1573` | 20 | retail_alias | `:3827` / `:6524`. **Head (20) is lower than torso (30) — that is retail data** |
| damage_arms | `MG_DAMAGE_ARMS` / `A1575` | 20 | retail_alias | `:3829` / `:6526` |
| damage_legs | `MG_DAMAGE_LEGS` / `A1577` | 20 | retail_alias | `:3831` / `:6528`. Occupies slots 3 **and** 4, so `MG_DAMAGE_ENTITY` (`A1579` = 20) is unbound. Standard retail pattern (`smgWeapon.py:16` does the same) |
| damage_block | `MG_DAMAGE_BLOCK` / `A1581` | 2 | retail_alias | `:3835` / `:6532`; `mgWeapon.py:24,134`. Deployed twin `A1582` = 2 |
| accuracy | `MG_ACCURACY` / `A1555` | 0.01 | retail_alias | `:3809` / `:6506`; range `A1557` = 0.05. Deployed `A1556` = 0.01 with range `A1558` = 0.005 |
| accuracy_spread | `MG_ACCURACY_SPREAD_INITIAL` / `A1559` | 1 | retail_alias | `:3813` / `:6510`; max `+A1561` (5), per-shot `A1563` (0.2), recovery `A1565` (0.6). Deployed twins identical |
| recoil_up | `MG_RECOIL_UP` / `A1567` | -0.007 | retail_alias | `:3821` / `:6518`; `recoil_side` `A1569` = 0 |
| shoot_sound | `MG_SHOOT_SOUND` / `A2843` | `["semishoot", -1, 100, -0.8, +0.8]` | retail_named | **`shared/constants_audio.py:296`**, alias `:298`. Bound at `mgWeapon.py:26,215`. Asset `sounds/semishoot.ogg` exists. Deployed firing overrides to `BLANK_SOUND` (`""`, `constants_audio.py:164`) and loops `'smg_fire_loop'` (`mgWeapon.py:212`) with `'smg_fire_tail'` on stop (`:229`). Reload sound is the literal `'smgreload'` (`mgWeapon.py:27`) |
| muzzle_flash_model | `MUZZLE_FLASH_MG` | `muzzleflash_default` | retail_literal | `models.py:376`, view variant `:377`. Duration 0.01, scale 0.75, offsets at `mgWeapon.py:51-58` |
| art_present (3D) | — | **null** | **absent** | No MG kv6 exists on disk and `models.py` loads none. But `mgWeapon.py:38-41` (`entity_model = []`) is **contradicted by the same file**: `__init__` indexes `self.entity_display[0]` at `:83-86` and `update`/`draw_manned` index `[1]` at `:187,:243,:260,:263`. With an empty list the constructor would raise `IndexError` before anything drew. `rocketTurretWeapon.py:22-24` proves the decompiler does not strip populated model lists, so the discrepancy is unexplained. The `MG_BASE_/MG_TOP_MODEL_OFFSET_Z` pair implies a ≥2-part rig |
| art_present (2D) | `TOOL_FILE_NAMES[MG_TOOL]` | `'mg'` | retail_named | `constants.py:1726`. **MG-specific 2D art does exist**: `png/ui/icons/weapons/mg.png` (via `load_weapon_icon`, `aoslib/weapons/__init__.py:25-26`) and `png/ui/marker_machinegun_16.png`. `TOOL_HAS_IMAGE[MG_TOOL] = False` (`:1938`) suppresses only the `load_weapon_image` path (`png/*/weapons/mg.png`, which indeed does not exist) |
| damage_type | `TOOLS_DAMAGE_TYPE[MG_TOOL]` (`A418`) | `WEAPON_DAMAGE` = 6 | retail_named | dict header `constants.py:1003`, entry `:1026`, alias `:1070`. `TOOLS_SECONDARY_DAMAGE_TYPE` (header `:1073`, alias `A419 :1140`) entry at `:1096` — a **separate secondary table, not a duplicate**. A dedicated `MG_DAMAGE` = 27 exists (`A401` at **retail** `constants.py:983`) but nothing maps `MG_TOOL` to it |
| kill_type | `TOOLS_KILL_TYPE[MG_TOOL]` (`A458`) | `WEAPON_KILL` = 0 | retail_named | dict header `:1187`, entry `:1209`, alias `:1252`. **There is no `MG_KILL`.** The kill credited when the placed MG explodes is absent from retail; BattleSpades substitutes `ENTITY_KILL` = 11 (`server/entities/machine_gun.py:87`) |
| crater_radius | — | 1 | battlespades | `server/entities/machine_gun.py:88` |
| team_only | — | null | absent | No constant, no readable entity class. BattleSpades stores a team (`machine_gun.py:27`) but its mount handler does **not** check it (`server/handlers/deployables.py:186-205`) |
| touch_radius | — | null | absent | BattleSpades invents `MOUNT_RADIUS = 3.0`, `MOUNT_BREAK_RADIUS = 4.0`, `MOUNT_INPUT_GRACE = 0.25` (`machine_gun.py:13-15`), plus `hit_radius = 1.5`, `hit_center_offset = (0,0,-0.75)` (`:22-23`) |
| respawn_delay | — | null | absent | No MG stock/restock/respawn constants at all — deliberate contrast with `ROCKET_TURRET_STOCK`. BattleSpades enforces one live MG per owner with no timer (`deployable_actions.py:318-324`) |
| water_legal | — | null | absent | No `mg_explode_water` string exists in source or in `gameScene.pyd` (contrast the turret, which has one) |
| light_radius | — | null | absent | none in either block |

**Deployment.** `check_deploying` refuses while crouching or without clearance
(`mgWeapon.py:299-309`); `check_available_space_for_deployment` (`:311-337`) walks a `cube_line`
2 units along the yaw direction and demands three empty z layers with a **solid** block at
`delta_z == 3`. During the 3.0 s deploy, jump/walk/crouch are force-cleared and yaw is pinned to
`weapon_deployment_yaw` (`:176-195`). Deployed, `update_properties` (`:129-165`) swaps the whole
stat block; any positional drift re-triggers `deploy()` to withdraw (`:246-248`).

**Suspect decompile.** `mgWeapon.py:204-206` unconditionally resets `deployment_time = A1546`,
`is_deploying_weapon = False`, `show_crosshair = ALWAYS_CROSSHAIR` immediately after the
deployed branch at `:198-203` sets `deployment_time = A1547`. May be an uncompyle6 artefact —
check the bytecode before porting literally.

---

## 8 — ROCKET_TURRET_ENTITY

`aoslib/scenes/main/rocketTurret.py:14` (class `RocketTurret`) +
`aoslib/weapons/rocketTurretWeapon.py:17` (placer). Named block `constants.py:6551-6577` and
alias block `:3854-3880` (`A1600`-`A1626`) **agree on all 27 entries**, including the three
derived offsets.

| field | constant | value | provenance | source |
|---|---|---|---|---|
| type_id | `ROCKET_TURRET_ENTITY` / `A907` | 8 | retail_named | `constants.py:2801`, alias `:2811` |
| health | `ROCKET_TURRET_HEALTH` / `A1611` | 100 | retail_named | `:6562` / `:3865`. **[UNBOUND]** |
| blast_radius | `ROCKET_TURRET_EXPLOSION_RADIUS` / `A1616` | **3.0** (float) | retail_named | `:6567` / `:3870`. Turret death blast |
| blast_damage | `ROCKET_TURRET_EXPLOSION_DAMAGE` / `A1617` | 100 | retail_named | `:6568` / `:3871` |
| block_damage | `ROCKET_TURRET_EXPLOSION_BLOCK_DAMAGE` / `A1618` | 15 | retail_named | `:6569` / `:3872` |
| blast_knockback_max | `ROCKET_TURRET_EXPLOSION_KNOCKBACK_MAX` / `A1619` | 1.0 | retail_named | `:6570` / `:3873` |
| blast_knockback_min | `ROCKET_TURRET_EXPLOSION_KNOCKBACK_MIN` / `A1620` | 0.2 | retail_named | `:6571` / `:3874` |
| model_size | `ROCKET_TURRET_MODEL_SIZE` / `A1610` | 0.06 | retail_alias | `:3864` / `:6561`. Bound `RocketTurret.size = A1610` (`rocketTurret.py:18`). Tool ghost uses a separate literal 0.06 (`rocketTurretWeapon.py:25`) |
| model_z_offset_base | `ROCKET_TURRET_BASE_MODEL_OFFSET_Z` / `A1612` | `-3.0 * 0.06` = -0.18 | retail_alias | `:3866` / `:6563`; bound `rocketTurret.py:35` |
| model_z_offset_ball | `ROCKET_TURRET_BALL_MODEL_OFFSET_Z` / `A1613` | `-17.0 * 0.06` = **-1.02 exactly** | retail_alias | `:3867` / `:6564`; bound `rocketTurret.py:36`. `repr(-17.0*0.06) == '-1.02'` and `-17.0*0.06 == -1.02` is **True** — the literal is bit-exact here |
| model_z_offset_gun | `ROCKET_TURRET_GUN_MODEL_OFFSET_Z` / `A1614` | `-11.0 * 0.06` = **-0.6599999999999999** | retail_alias | `:3868` / `:6565`; bound `rocketTurret.py:37`. **`-11.0*0.06 == -0.66` is False** — the product is 1 ulp smaller in magnitude. **Keep the multiply, do not write a literal** |
| ammo | `ROCKET_TURRET_AMMO` / `A1615` | 10 | retail_alias | `:3869` / `:6566`; class default `rocketTurret.py:27`. Server streams live ammo via `set_ammo` (`:134`) |
| stock_max | `ROCKET_TURRET_STOCK` / `A1600` | 4 | retail_alias | `:3854` / `:6551`; slot 0 of `rocketTurretWeapon.py:28` |
| stock_initial | `ROCKET_TURRET_INITIAL_STOCK` / `A1601` | 2 | retail_alias | `:3855` / `:6552`; slot 1 |
| stock_restock_amount | `ROCKET_TURRET_RESTOCK_AMOUNT` / `A1602` | 2 | retail_alias | `:3856` / `:6553`; slot 4. Slots 2,3 literal `None`. `ROCKET_TURRET_TOOL` is in `SELECTABLE_ON_NO_AMMO_TOOLS` (`:948`) |
| placement_far_radius | `ROCKET_TURRET_FAR_RADIUS` / `A1603` | 10 | retail_alias | `:3857` / `:6554`. Bound as `far_radius=A1603` at `rocketTurretWeapon.py:58`, with `player_min_radius=1, entity_min_radius=1, others_min_radius=0` |
| shoot_interval | `ROCKET_TURRET_SHOOT_INTERVAL` / `A1604` | 1.5 | retail_alias | `:3858` / `:6555`. Bound twice: tool cooldown and `AnimPlaceBlock` length (`rocketTurretWeapon.py:29,50`) |
| tracking_range | `ROCKET_TURRET_TRACKING_RANGE` / `A1605` | 50.0 | retail_named | `:6556` / `:3859`. **[UNBOUND]** |
| detection_range | `ROCKET_TURRET_DETECTION_RANGE` / `A1606` | 30.0 | retail_named | `:6557` / `:3860`. **[UNBOUND]** |
| aim_tolerance | `ROCKET_TURRET_TOLERANCE` / `A1607` | 0.1 | retail_alias | `:3861` / `:6558`. Client uses it **only** as an angular-**rate** gate for the aim sound: `rocketTurret.py:65` compares `abs(delta)/dt` against `A1607 * 10` = 1.0 deg/s |
| aiming_speed | `ROCKET_TURRET_AIMING_SPEED` / `A1608` | 180 | retail_named | `:6559` / `:3862`. Deg/s slew. **[UNBOUND]** — the client only receives yaw/pitch (`rocketTurret.py:93-97`) |
| lower_pitch_limit | `ROCKET_TURRET_LOWER_PITCH_LIMIT` / `A1609` | 30 | retail_named | `:6560` / `:3863`. **[UNBOUND]**; `rocketTurret.py:33` sets `self.pitch_delta = -30` as a bare literal, written and never read |
| aiming_tolerance_timer | — | 0.2 | retail_literal | `rocketTurret.py:24`. Hysteresis window keeping the aim loop alive 0.2 s after slewing stops. **Distinct from `ROCKET_TURRET_TOLERANCE`** |
| ammo_text_radius | `ROCKET_TURRET_AMMO_TEXT_RADIUS` / `A1626` | 20 | retail_alias | `:3880` / `:6577`. `rocketTurret.py:49` `display_ammo_text = (A1626 == 0)`; `:55` `radius_squared = A1626*A1626`, squared compare `:54,:56` |
| ammo_text_z_offset | — | -1.0 | retail_literal | `rocketTurret.py:61` `Vector3(x, y, z - 1.0)`. **AoS z is down, so this renders 1.0 unit ABOVE the turret.** Text3D scale 0.005, `disable_depth_test=True` (`entity.py:55`), font `aoslib.text.ammo_font` (`:138`) |
| ammo_text_color_ok | `ENOUGH_AMMO_COLOR` / `A49` | `(255,228,0,255)` | retail_named | `constants.py:199`, alias `:207`; used `rocketTurret.py:140` |
| ammo_text_color_empty | `NOT_ENOUGH_AMMO_COLOR` / `A50` | `(204,28,24,255)` | retail_named | `constants.py:200`, alias `:208`; used `rocketTurret.py:142` |
| rocket_blast_radius | `ROCKET_TURRET_ROCKET_EXPLOSION_RADIUS` / `A1621` | **3** (int) | retail_named | `:6572` / `:3875`. **Preserve the int/float split vs the 3.0 death blast** |
| rocket_blast_damage | `ROCKET_TURRET_ROCKET_EXPLOSION_DAMAGE` / `A1622` | 50 | retail_named | `:6573` / `:3876` |
| rocket_block_damage | `ROCKET_TURRET_ROCKET_EXPLOSION_BLOCK_DAMAGE` / `A1623` | 10 | retail_named | `:6574` / `:3877` |
| rocket_knockback_max | `ROCKET_TURRET_ROCKET_EXPLOSION_KNOCKBACK_MAX` / `A1624` | 0.3 | retail_named | `:6575` / `:3878` |
| rocket_knockback_min | `ROCKET_TURRET_ROCKET_EXPLOSION_KNOCKBACK_MIN` / `A1625` | 0.1 | retail_named | `:6576` / `:3879` |
| rocket_muzzle_speed | `ROCKET_SPEED` / `A1397` | **75** | **retail_alias** | `constants.py:6346` / `:3650`. Referenced at `aoslib/scenes/main/rocket.py:29` `create_object(GenericMovement, Vector3(0,0,0), Vector3(0,0,0) * A1397)`. The client multiplies a **zero** vector, so the turret-specific binding is unproven — but 75 is retail, **not** an invention |
| rocket_gravity_multiplier | `ROCKET_GRAVITY_MULTIPLIER` / `A1398` | **0.05** | **retail_alias** | `constants.py:3651` / `:6348`. Bound at `rocket.py:30` `self.world_object.set_gravity_multiplier(A1398)`, inside `Rocket.__init__` **before** the rpg/rocket_turret branch — so it governs the turret's rocket too. `rocket_type` is set to `'rocket_turret'` at `rocket.py:39` for every non-RPG rocket |
| rocket_shoot_sound | `ROCKET_TURRET_SHOOT_SOUND` / `A2825` | `["turr_rocketshoot", -1, 100, -0.8, +0.8]` | retail_named | `constants_audio.py:243`, alias `:251`; bound `rocket.py:37` |
| rocket_projectile_loop | — | `'turr_rocket_projectile'` | retail_literal | `rocket.py:74-75` |
| rocket_explode_sound | `TURRET_ROCKET_EXPLODE_SOUND` / `A2766` | `["turr_rocketexplode"]` | retail_named | `constants_audio.py:112`, alias `:114`; `rocket.py:87-96` |
| rocket_explode_water_sound | `TURRET_ROCKET_WATER_EXPLODE_SOUND` / `A2767` | `["turr_rocketexplode_water"]` | retail_named | `constants_audio.py:113`, alias `:115`. **Threshold is `MAP_Z - 1`, not `MAP_Z - 2`** — different from the turret body |
| rocket_spawn_offset | — | 1.0 | battlespades | `server/rocket_turret.py:237` — turret origin + 1.0 * unit direction. No retail muzzle offset recovered |
| damage_type | `TOOLS_DAMAGE_TYPE` (`A418`) → `ROCKET_TURRET_DAMAGE` / `A386` | 12 | retail_named | enum `constants.py:954`, alias `:968`. **`TOOLS_DAMAGE_TYPE[ROCKET_TURRET_TOOL]` is `None`** (`:1027`) because the tool deals no direct damage; `TOOLS_SECONDARY_DAMAGE_TYPE` entry at `:1097`. A separate `ROCKET_TURRET_ROCKET_DAMAGE` = 21 (`A395 :977`) exists for the projectile |
| kill_type | `TOOLS_KILL_TYPE[ROCKET_TURRET_TOOL]` → `ROCKET_KILL` | 4 | retail_named | dict header `:1187`, entry `:1210`, enum `:1146`. **Conflicts with `ROCKET_TURRET_KILL` = 18** (`A439 :1166`), which is in `ONE_HIT_KILL_WEAPONS` (`:5260`). See conflicts |
| team_only | — | true | retail_literal | `rocketTurret.py:145-149` — `draw_on_minimap` returns `super()` only when `self.team is player.team`. Ammo billboard likewise gated (`:50`). Minimap icon `image.load('marker_turret_16', center=True)`, `icon_scale` 1.0 (`:16-17`) |
| position_xy_bias | — | -0.5 | retail_literal | `rocketTurret.py:91` — `super().set_position(x - 0.5, y - 0.5, z)`. On top of the generic per-face handling in `entity.py:104-132` |
| rest_pose_yaw | — | 45.0 | retail_literal | `rocketTurret.py:38-39` — `display[1]` and `display[2]` `set_rotation(0.0, 45.0, 0.0)`. Overwritten on the first `update()` |
| rest_pose_pitch | — | -45.0 | retail_literal | `rocketTurret.py:40` — `display[2].add_rotation(-45.0, 0, 0)` (gun only) |
| death_glow_particle_count | — | 8 | retail_literal | `rocketTurret.py:114`, issued once **per display part** → 3 × 8 = 24 |
| death_smoke_color | — | `(96,96,96)` | retail_literal | `rocketTurret.py:115` `create_particle_effect(None, pos, None, (96,96,96), 10, 1.5, 5.0)`. Per `particleEffectManager.py:34` the positional args are **numparticles=10, explode_velocity=1.5, size=5.0**; `lifetime` is **not** passed and keeps its default 2.0 |
| death_explode_display_args | — | `(1.0, 5)` | retail_literal | `rocketTurret.py:118` `scene.explode_display(display, 1.0, 5)` per part |
| water_death_sound_threshold_z | `Z_ABOVE_WATERPLANE` / `A2215` | **238** | retail_named | `constants.py:4854` `Z_ABOVE_WATERPLANE = MAP_Z - 2`, with `MAP_Z = 240` (`:4853`, alias `A2214 :4862`), alias `:4864`. `rocketTurret.py:106` selects `'turret_explode_water'` over `'turret_explode'` |
| crater_radius | — | 1 | battlespades | `server/rocket_turret.py:111` |
| pitch_clamp_upper | — | 90.0 | battlespades | `server/rocket_turret.py:166`. Only the -30 lower bound has a retail counterpart |
| touch_radius | — | null | absent | not a pickup. BattleSpades invents `hit_radius = 1.25`, `hit_center_offset = (0,0,-0.55)` (`rocket_turret.py:85-86`) |
| water_legal | — | null | absent | Only water-awareness is the death-sound switch; placement legality is inside the compiled `can_place_object` |
| light_radius | — | null | absent | none in either block |

**Rig.** `display[0]` `Turret_base` is **static**; `display[1]` `Turret_ball` takes **yaw only**;
`display[2]` `Turret_gun` takes yaw then pitch additively (`rocketTurret.py:85-87`). Models
`models.py:378-380` (entity variants, no load offset); **tool** variants `:384-386` bake
`(0,-18,3)` / `(0,-18,17)` / `(0,-18,11)`.

**Aim audio state machine** (`rocketTurret.py:64-84`), runs only when `self.player` is set. Rising
edge fires one-shot `'turret_aim_start'` + looping `'turret_aiming_lp'` (`loops=0` = infinite);
falling edge plays `'turret_aim_stop'` and closes the loop. Target changes are pure audio on the
client: `'turret_lockon'` / `'turret_lockoff'` (`:122-132`).

---

## 9 — LANDMINE_ENTITY

`aoslib/weapons/landmineWeapon.py:14-73`. Alias slice `constants.py:4048-4066` =
**`A1792`-`A1810`, 19 entries**, matching the 19 named `LANDMINE_*` constants at `:6627-6645`
1:1 and value-for-value. **The alias→named correspondence is inference** — the alias lines are
bare literals with no symbolic right-hand side. Two entries are directly bound
(`A1806` at `landmineWeapon.py:54`, `A1810` at `:59`), which anchors the alignment.

| field | constant | value | provenance | source |
|---|---|---|---|---|
| type_id | `LANDMINE_ENTITY` / `A908` | 9 | retail_named | `constants.py:2801`, alias `:2812` |
| health | `A1808` | 1 | retail_alias | `:4064`; named twin `LANDMINE_HEALTH` `:6643`. **[UNBOUND]** |
| arm_delay | `A1798` | 4 | retail_alias | `:4054`; named twin `LANDMINE_ACTIVATION_TIMER` `:6633`. Seconds. **[UNBOUND]**. Note `ProximityMineBehavior`'s own default is 1.0 (`behaviors.py:324`) but the call site passes 4 (`deployable_actions.py:164`) — do not port the 1.0 |
| blast_radius | `A1796` | 3.0 | retail_alias | `:4052`; named twin `LANDMINE_EXPLOSION_RADIUS` `:6631`. `A1796` **is** present in `gameScene.pyd` (`strtab:1451`) |
| blast_wave_radius | `A1797` | 6.0 | retail_alias | `:4053`; named twin `LANDMINE_EXPLOSION_BLAST_WAVE_RADIUS` `:6632`. **[UNBOUND — no consumer anywhere]**: `A1797` appears only in `constants.py:4053` and `backup/constants-copy.py:2773`, and is **not** in `gameScene.pyd` (unlike its siblings `A1796`/`A1807`/`A1809`). **At least six retail entities have a `*_BLAST_WAVE_RADIUS`** — `CLASSIC_GRENADE` 9.0 (`:4458`), `ROCKET` 6.0 (`:6352`), `ROCKET2` 4.0 (`:6375`), `UGC_ROCKET2` 4.0 (`:6397`), `LANDMINE` 6.0 (`:6632`), `ANTIPERSONNEL_GRENADE` 6.0 (`:6675`) — it is a general explosion parameter, not a landmine special case |
| blast_damage | `A1802` | 100 | retail_alias | `:4058`; twin `:6637` |
| block_damage | `A1803` | 15 | retail_alias | `:4059`; twin `:6638` |
| trip_radius_horizontal | `A1799` | 2.5 | retail_alias | `:4055`; twin `LANDMINE_DETECTION_RANGE` `:6634`. **Retail does not state that this axis is horizontal-only** — that is our server's reading (`behaviors.py:391-417`) |
| trip_radius_vertical | `A1800` | 3 | retail_alias | `:4056`; twin `LANDMINE_DETECTION_LAYERS` `:6635` — a **layer count**, not a radius. Our server treats it as ±3 blocks of vertical tolerance (`behaviors.py:416`). Number retail, semantics ours |
| trip_detection_vertical_offset | `A1801` | -0.5 | retail_alias | `:4057`; twin `LANDMINE_EXPLOSION_AND_DETECTION_VERTICAL_OFFSET` `:6636`. Applied to **both** detection centre and explosion origin per the name. **[UNBOUND]** |
| knockback_max | `A1804` | 0.75 | retail_alias | `:4060`; twin `:6639`. **max == min, so knockback does not fall off with distance** |
| knockback_min | `A1805` | 0.75 | retail_alias | `:4061`; twin `:6640` |
| placement_far_radius | `A1806` | 5.0 | retail_alias | `:4062`; twin `LANDMINE_FAR_RADIUS` `:6641`. **Directly bound**: `landmineWeapon.py:54` passes it to `can_place_object` with `can_place_vertical=False` (floors only) |
| model_size | `A1807` | 0.05 | retail_alias | `:4063`; twin `LANDMINE_MODEL_SIZE` `:6642`. Present in `gameScene.pyd` (`strtab:1452`). **Distinct** from the placement-ghost `DisplayList` scale, the bare literal 0.06 at `landmineWeapon.py:15` |
| model_z_offset | `A1809` | 0.0 | retail_alias | `:4065`; twin `LANDMINE_MODEL_Z_OFFSET` `:6644`. Present in `gameScene.pyd` (`strtab:1453`). Ghost draw at `:63` uses no z term. **There is no `C4_MODEL_Z_OFFSET` in retail** — only `DYNAMITE_MODEL_Z_OFFSET = -0.2` (`:6591`) among the placeables |
| model | `LANDMINE_MODEL` / `LANDMINE_VIEW_MODEL` | `landmine`, load offset `(5, -13, 0.0)` | retail_literal | `models.py:315` (world, with offset), `:316` (view, no offset). `kv6/landmine.kv6` present |
| ammo | `A1792` / `A1793` / `A1794` | max 5, initial 3, restock 5 (max_clip `None`, initial_stock `None`) | retail_alias | `:4048-4050`; bound `landmineWeapon.py:25` as `(A1792, A1793, None, None, A1794)`. `A1794 = A1792` is a source-level reference. Named twins `LANDMINE_STOCK`/`_INITIAL_STOCK`/`_RESTOCK_AMOUNT` `:6627-6629` agree |
| shoot_interval | `A1795` | 1.0 | retail_alias | `:4051`; bound `landmineWeapon.py:26`, also drives `AnimPlaceBlock` (`:40`); twin `:6630` |
| water_legal | `A1810` | true | retail_alias | `:4066`; twin `LANDMINE_CAN_PLACE_ON_WATER` `:6645`. **Directly bound**: `landmineWeapon.py:59` reads `if not A1810 and position.z > Z_ABOVE_WATERPLANE: return`. Because it is `True` the guard never fires. **This is the ONLY `*_CAN_PLACE_ON_WATER` constant in retail** — the projectile mine has none |
| damage_type | `LANDMINE_DAMAGE` / `A389` | 15 | retail_named | enum `constants.py:954`, alias `:971` |
| kill_type | `LANDMINE_KILL` / `A435` | 14 | retail_named | enum `:1146`, alias `:1162`. In `ONE_HIT_KILL_WEAPONS` (`:5260`) |
| crater_radius | — | 1 | battlespades | `server/deployable_actions.py:161`. No retail crater constant exists for any placeable |
| team_only | — | true | battlespades | `behaviors.py:394` `if player.team == self.team: continue`. No retail constant expresses friendly immunity |
| touch_radius | — | null | absent | `ProximityMineBehavior` inherits `touch_radius = 0.0`, so the registry proximity path is skipped; triggering is its own `on_tick` scan |
| fuse | — | null | absent | Proximity-triggered, not timed. `arm_delay` is the only timer in the 19-entry block |
| lifetime | — | null | absent | No expiry constant. **(Note: `RADAR_STATION_LIFETIME` is NOT a retail constant — it exists only at `BattleSpades/shared/constants.py:7303`. Retail's only `*_LIFETIME` constants are `DIAMOND_LIFETIME :3034`, `ROCKET_SMOKE_LIFETIME :4779`, `SNOWBALL_SMOKE_LIFETIME :4803`, `BLOCK_SMOKE_TRAIL_LIFETIME :4825`, `SMOKE_RING_LIFETIME :5089`, `CRATE_PICKUP_FX_LIFETIME :5148`, `DIAMOND_PICKUP_FX_LIFETIME :5183`.)** |
| respawn_delay | — | null | absent | one-shot placed deployable |
| light_radius | — | null | absent | retail defines `*_LIGHT_RADIUS` only for `FLAREBLOCK` (5.0) and `BLOCKFIRE` (3.0) |

Scout equipment (`CLASS_SCOUT CLASS_EQUIPMENT`, `constants.py:1513`). Placement sends
`send_place_landmine(ghost_position)` (`landmineWeapon.py:48`) with **no face byte**, unlike
dynamite/C4.

**No named-vs-alias value conflict.** All 19 alias entries match their named twins exactly.

---

## 10 — DYNAMITE_ENTITY

`aoslib/weapons/dynamiteWeapon.py:14-98`. Alias slice `constants.py:3881-3894` =
`A1627`-`A1640`, 14 entries, against 14 named `DYNAMITE_*` constants at `:6578-6591`.
Four constants are directly bound, which pins the alignment: `A1627/A1628/A1629` (`:25`),
`A1630` (`:26`), `A1637` (`:58`), `A1640` (`:88`).

| field | constant | value | provenance | source |
|---|---|---|---|---|
| type_id | `DYNAMITE_ENTITY` / `A909` | 10 | retail_named | `constants.py:2801`, alias `:2813` |
| fuse | `A1631` | 7 | retail_alias | `:3885`; named twin `DYNAMITE_EXPLOSION_FUSE` `:6582` **agrees**. Seconds is inference (bare int, no `.py` consumer) |
| **blast_radius** | `A1632` | **8** | retail_alias | `:3886`. **CONFLICT — named `DYNAMITE_EXPLOSION_RADIUS = 5` at `:6583`. Use 8.** See conflicts |
| blast_damage | `A1633` | **300.0** (float) | retail_alias | `:3887` reads `A1633 = 300.0`; twin `:6584` |
| block_damage | `A1634` | 7 (int) | retail_alias | `:3888`; twin `:6585` |
| knockback_max | `A1635` | 0.15 | retail_alias | `:3889`; twin `:6586`. Very low for a 300-damage charge — dynamite kills without launching bodies |
| knockback_min | `A1636` | 0.1 | retail_alias | `:3890`; twin `:6587` |
| placement_far_radius | `A1637` | **5.0** (float) | retail_alias | `:3891` reads `A1637 = 5.0`; twin `:6588`. **Directly bound** at `dynamiteWeapon.py:58` as the `can_place_object` range, `can_place_vertical=True` (walls and ceilings legal) |
| model_size | `A1638` | 0.06 | retail_alias | `:3892`; twin `:6589`. The ghost `DisplayList` also uses 0.06 (bare literal `dynamiteWeapon.py:15`) — here they coincide, unlike the landmine |
| health | `A1639` | 1 | retail_alias | `:3893`; twin `:6590`. Shootable |
| model_z_offset | `A1640` | -0.2 | retail_alias | `:3894`; twin `DYNAMITE_MODEL_Z_OFFSET` `:6591`. **Directly bound** at `dynamiteWeapon.py:88` as `glTranslatef(0, -A1640, 0)` — applied along the GL up axis **after** the per-face rotation, so it lifts the charge off whatever face it is stuck to |
| model | `DYNAMITE_MODEL` / `DYNAMITE_VIEW_MODEL` | `dynamite`, world load offset `(5, -13, 0.0)`; view model **no offset** | retail_literal | `models.py:317-318`; loader signature `:48`, path `join('kv6', name + '.kv6')`. `kv6/dynamite.kv6` present. **Do not conflate this KV6 load offset with `model_z_offset = -0.2`, which is the placement-ghost lift** |
| **ammo** | `A1627` / `A1628` / `A1629` | **max 1, initial 1, restock 1** (max_clip `None`, initial_stock `None`) | retail_alias | `:3881-3883` read `A1627 = 1`, `A1628 = A1627`, `A1629 = A1627`. **Directly bound** at `dynamiteWeapon.py:25`. **CONFLICT — named `DYNAMITE_STOCK = 3` and `DYNAMITE_RESTOCK_AMOUNT = 3` at `:6578,:6580`; only `DYNAMITE_INITIAL_STOCK = 1` (`:6579`) matches. Use 1/1/1.** The reference form `A1628 = A1627` is genuine source structure, not de-duplication — the neighbouring C4 block writes `A1745 = 2` and `A1746 = 2` as separate literals |
| shoot_interval | `A1630` | **1.0** (float) | retail_alias | `:3884` reads `A1630 = 1.0`; bound `dynamiteWeapon.py:26`, drives `AnimPlaceBlock` (`:41`); twin `:6581` |
| default_face | `DynamiteWeapon.face` | 4 | retail_literal | `dynamiteWeapon.py:28`. **Vestigial** — like C4, the send path uses `self.ghost_face` (validated 0..5 at `:47`); `grep 'self\.face'` over `aoslib/` returns zero hits |
| per_face_offset_table | — | `{0:(0.0,0.5,0.5), 1:(1.0,0.5,0.5), 2:(0.5,0.0,0.5), 3:(0.5,1.0,0.5), 4:(0.5,0.5,0.0), 5:(0.5,0.5,1.0)}` | retail_literal | `dynamiteWeapon.py:65-82`, identical to C4 (`c4Weapon.py:72-89`). Applied as `glTranslatef(position.x + x, -position.z - z, position.y + y)` (`:86`) — tuple is (x,y,z) in AoS map space with **z down** |
| per_face_rotation_table | — | `{0:(90,0,0,1), 1:(-90,0,0,1), 2:(-90,1,0,0), 3:(90,1,0,0), 4:(0,0,0,0), 5:(180,1,0,0)}` | retail_literal | `dynamiteWeapon.py:65-82`, fed to `glRotatef` at `:87`. Face 4 is the identity case (zero axis = no-op) |
| damage_type | `DYNAMITE_DAMAGE` / `A390` | 16 | retail_named | enum `:954`, alias `:972` |
| kill_type | `DYNAMITE_KILL` / `A436` | 15 | retail_named | enum `:1146`, alias `:1163`. In `ONE_HIT_KILL_WEAPONS` (`:5260`) |
| crater_radius | — | 2 | battlespades | `server/deployable_actions.py:126` |
| water_legal | — | null | absent | **Cannot determine.** The dynamite slice has no water flag and `draw_ghosting` has no `Z_ABOVE_WATERPLANE` guard (contrast `landmineWeapon.py:59`). Any restriction lives inside the compiled `scene.can_place_object` |
| arm_delay / trip_radius_horizontal / lifetime / light_radius | — | null | absent | purely timed charge; the fuse is the lifetime |

Miner equipment (`CLASS_MINER CLASS_EQUIPMENT`, `constants.py:1540`). Placement sends
`send_place_dynamite(ghost_position, ghost_face)` after validating the face 0..5
(`dynamiteWeapon.py:47-52`).

---

## 11 — GRAVE_ENTITY

Client class `GraveEntity` inside `gameScene.pyd`, compiled from `grave.py` (source-path string
at `strtab:355`; methods `initialize`/`create_display`/`draw`/`update`/`set_position`/
`set_velocity`/`on_delete` at `strtab:356,357,358,360,361,362,363` — **note `:359` is
`hover_scoreboard_blue`, an unrelated interleaved string; the table is address-ordered, not
class-ordered**).

Named block `:4516-4521` and alias block `:4522-4527` agree on every value; the raw obfuscated
dump `shared/backup/constants-copy.py:3147-3152` agrees too.

| field | constant | value | provenance | source |
|---|---|---|---|---|
| type_id | `A910` (= `GRAVE_ENTITY`) | 11 | retail_alias | `constants.py:2814`, enum `:2801`. `A910` is referenced by `gameScene.pyd` |
| fuse | `A2086` | 7 | retail_alias | `:4522`; named `:4516`. **[UNBOUND]** — `A2086` is absent from `gameScene.pyd` and no retail `.py` binds it; "seconds from spawn to detonation" is inferred from the name |
| blast_radius | `A2087` | 3 | retail_alias | `:4523`; named `:4517`. **`A2087` IS referenced inside `gameScene.pyd`** (`strtab:1479`) — the client predicts the blast geometry |
| blast_damage | `A2088` | 25 | retail_alias | `:4524`; named `:4518`. **[UNBOUND]** — server-applied |
| block_damage | `A2089` | 3 | retail_alias | `:4525`; named `:4519`. **[UNBOUND]** |
| knockback_max | `A2090` | 1.0 | retail_alias | `:4526`; named `:4520`. **[UNBOUND]** |
| knockback_min | `A2091` | 0.5 | retail_alias | `:4527`; named `:4521`. **[UNBOUND]** |
| damage_type | `A388` (= `GRAVE_DAMAGE`) | 14 | retail_alias | `constants.py:970`, enum `:954`. Client dispatch `BlockManager.handle_grave_damage` at **`gameScene.pyd.strtab.json:156`** (`:154` is `handle_corpse_damage`, `:155` is `handle_radar_station_damage`) |
| kill_type | `A434` (= `GRAVE_KILL`) | 13 | retail_alias | `constants.py:1161`, enum `:1146`. In `ONE_HIT_KILL_WEAPONS` (`:5260`) |
| model | `GRAVE_MODEL` | `grave` | retail_literal | `models.py:349`. `kv6/grave.kv6` present, matching case |
| kv6_load_offset | — | `(0.0, 0.0, 11.0)` | retail_literal | `models.py:349` third positional arg, forwarded to `KV6(path, USE_BILLBOARDS, offset, ...)` at `:61`. **This is a KV6 loader offset, NOT an entity-space `model_z_offset`.** Unit convention is unreadable (`kv6.pyd` is compiled) |
| model_z_offset | — | **null** | **absent** | There is no `GRAVE_MODEL_Z_OFFSET`. The retail `*_MODEL_Z_OFFSET` family is only `ROCKET :6358`, `ROCKET2 :6383`, `DRILL :6463`, `UGC_DRILL :6495`, `DYNAMITE :6591`, `LANDMINE :6644` — all in the ±0.2 range. **Do not put 11 in this field**; that would translate the grave ~11 blocks into the sky |
| model_size | — | null | absent | No `GRAVE_MODEL_SIZE` (the `*_MODEL_SIZE` family is `:6357,:6382,:6462,:6494,:6540,:6561,:6589,:6642`). `GraveEntity.create_display` is overridden in `gameScene.pyd` and unreadable. `Entity.size` defaults to 1.0 (`entity.py:31,34`) |
| health | — | null | absent | No `GRAVE_HEALTH`. BattleSpades gives graves none either — `GraveBehavior` extends the inert `EntityBehavior` whose `takes_damage` is `False` (`behaviors.py:51-52,209`), so graves are indestructible until the fuse expires |
| enable_rule | `RULE_ENABLE_GRAVESTONES` | default `"ON"` | **retail_named** | **`shared/constants_matchmaking.py:22`** (name in `GAME_RULES_NAMES["GENERAL"]`) and **`:400`** (`{"default": "ON", "values": ON_OFF_VALUES}`). Honoured at `BattleSpades/server/player.py:1441` |
| explosion_rule | `RULE_ENABLE_CORPSE_EXPLOSION` | default `"ON"` | **retail_named** | **`constants_matchmaking.py:23`, `:401`.** Honoured at `BattleSpades/server/player.py:1459-1461`. **This IS retail — an earlier pass wrongly called it a BattleSpades invention** |
| crater_radius | — | 1 | battlespades | `behaviors.py:225` |
| team_only | — | false | battlespades | The grave carries the dead player's team as wire state and colour (`player.py:1465`), but the blast goes through the unfiltered `_apply_blast` (`behaviors.py:521`). No retail constant gates grave damage by team |

BattleSpades spawn path: gates at `player.py:1440-1447` (`RULE_ENABLE_GRAVESTONES`,
`not uses_classic_corpse`, `entities_wire_ready`), `world.dry_surface_anchor` at
**`:1452-1456`**, `reg.place(` at `:1462`, broadcast through `:1487`.

---

## 12 — CORPSE_ENTITY

**Not a wire entity.** The id exists in the enum but the retail client has no corpse entity
class: `gameScene.pyd.strtab.json` contains 29 `*Entity` class names (including `GraveEntity`)
and **no `CorpseEntity` of any kind**. The corpse is the dead `Character` object, removed by
the `ExplodeCorpse` packet. Corpse physics live in `aoslib/character.pyd` (which also contains
the raw strings `'classic_corpse'` and `'explode_corpse'`).

| field | constant | value | provenance | source |
|---|---|---|---|---|
| type_id | `A911` (= `CORPSE_ENTITY`) | 12 | retail_alias | `constants.py:2815`, enum `:2801` |
| wire_safe | — | false | retail_literal | `A911` absent from `gameScene.pyd`; no corpse entity class exists |
| fuse | `A2074` | 0 | retail_alias | `:4494`; named `:4487`; dump `backup/constants-copy.py:3135`. Zero fuse — a normal corpse explodes immediately when triggered. **[UNBOUND]** |
| fuse_jetpack | `A2075` | **1.0** (float) | retail_alias | `:4495`; named `:4488`; dump `:3136`. **[UNBOUND]** — "used when the dead player wore a jetpack" rests entirely on the constant name |
| blast_radius | `A2076` | 3 | retail_alias | `:4496`; named `:4489`; dump `:3137`. **`A2076` IS referenced inside `gameScene.pyd`** |
| blast_damage | `A2077` | **0** | retail_alias | `:4497`; named `:4490`; dump `:3138`. **Zero is the real retail value** — an ordinary corpse explosion hurts nobody, it only digs. Do not "fix" this |
| block_damage | `A2078` | 1 | retail_alias | `:4498`; named `:4491`; dump `:3139` |
| knockback_max | `A2079` | 0.1 | retail_alias | `:4499`; named `:4492` |
| knockback_min | `A2080` | 0.05 | retail_alias | `:4500`; named `:4493` |
| vip_blast_radius | `A2081` | 10 | retail_alias | `:4509`; named `:4504`. VIP mode uses a completely separate, lethal set |
| vip_blast_damage | `A2082` | **75.0** (float) | retail_alias | `:4510`; named `:4505` |
| vip_block_damage | `A2083` | 20 | retail_alias | `:4511`; named `:4506`. VIP knockback is 2→3 (`A2085`/`A2084`, `:4507-4508`, `:4512-4513`) |
| bounce | `A2291` | 0.1 | retail_alias | `:5066`; named `:5063`; dump `:3380`. **`A2291` is referenced by `aoslib/character.pyd`** — corpse ragdoll restitution |
| bounce_sound_threshold | `A2292` | 2 | retail_alias | `:5067`; named `:5064`; dump `:3381`. **[UNBOUND]** — absent from `character.pyd`, `gameScene.pyd`, `player.pyd`, `world.pyd` |
| move_threshold | `A2293` | 0.5 | retail_alias | `:5068`; named `:5065`; dump `:3382`. **Referenced by `character.pyd`** — below this the corpse is settled |
| damage_type | `A387` (= `CORPSE_DAMAGE`) | 13 | retail_alias | `constants.py:969`, enum `:954`. Client dispatch `BlockManager.handle_corpse_damage` at `strtab:154` |
| kill_type | `A433` (= `CORPSE_KILL`) | 12 | retail_alias | `constants.py:1160`, enum `:1146`. **Deliberately NOT in `ONE_HIT_KILL_WEAPONS`** (`:5260`), consistent with damage 0 |
| model (classic) | `CLASSIC_CORPSE_MODEL` | `ClassicCorpse` | retail_literal | `models.py:388`, **no offset argument**. `kv6/ClassicCorpse.kv6` present with matching case. The non-classic corpse is the dead player's normal body-part models |
| model_size | — | null | absent | The omitted third argument of `load_model` is `offset`, **not** size — scale is governed by `min_model_detail`/`invscale` (`models.py:49,59,61`), which also defaults. No `CORPSE_MODEL_SIZE` exists. `Entity.size` default is 1.0 |
| model_z_offset | — | null | absent | `models.py:388` passes no offset |
| explosion_rule | `RULE_ENABLE_CORPSE_EXPLOSION` | default `"ON"` | retail_named | `constants_matchmaking.py:23`, `:401`; server sets `initial_info.enable_corpse_explosion = 1` at `aceofspades_source/server/aosserver/connection.py:259`. Mirrored at `BattleSpades/server/corpse_lifecycle.py:131-134,176-179` |
| health | — | null | absent | none |
| crater_radius | — | 1 | battlespades | `corpse_lifecycle.py:189` (with `force_destroy=False` at `:190`) |

Packet path: `GameScene.process_packet_explode_corpse` (`strtab:817`, `:928`), packet name
`ExplodeCorpse` (`strtab:2012`); related strings `explode_corpse_packet` **`:595`**,
`enable_corpse_explosion` **`:612`**, `explode_corpse` **`:2696`**.

**Do not model CORPSE as a spawnable entity.** `BattleSpades/server/corpse_lifecycle.py:4-5`
records the rule: *"it is not a packet-21 entity and therefore must never consume an
entity-registry id."*

---

## 13 — FLARE_BLOCK

Tool `aoslib/weapons/flareBlockTool.py` (extends `BlockToolCommon`). Entity class
`FlareBlockEntity` compiled into `gameScene.pyd` from `flareBlock.py`
(**source-path string at `gameScene.pyd.strtab.json:402`**, methods around `:403-416`).

| field | constant | value | provenance | source |
|---|---|---|---|---|
| type_id | `A912` (= `FLARE_BLOCK`) | 13 | retail_alias | `constants.py:2816`, enum `:2801`. Note the entity is `FLARE_BLOCK` (underscore) while every other constant is `FLAREBLOCK_*` |
| tool_id | `FLAREBLOCK_TOOL` / `A318` | 22 | retail_alias | tool enum `constants.py:861`, alias `:885`. In `NON_HUD_TOOLS` and `PREFAB_TOOLS` (`:4901,:4903`), `SELECTABLE_ON_NO_AMMO_TOOLS` (`:948`), `CLASS_COMMON_TOOLS` (`:1396`) |
| block_cost | `A2258` (= `FLAREBLOCK_COST`) | 10 | retail_alias | `:4975`; named `:4974`; dump `backup/constants-copy.py:3347`. Bound three times in `flareBlockTool.py`: `block_cost` `:26`, deduction `:47`, affordability gate `get_has_enough_ammo` `:68`. Both the deduction and the gate are skipped when `player.team.infinite_blocks` |
| light_radius | `A2261` (= `FLAREBLOCK_LIGHT_RADIUS`) | **5.0** (float) | retail_alias | `:4984`; named `:4983`; dump `:3350`. **Consumed by `FlareBlockEntity.post_initialize` ONLY** — the two xrefs to the `"A2261"` object (`0x1028BA20`) are `sub_100DE910` at `0x100DEF63` (feeding the 7th arg of `light_manager.add_static_point_light`) and one data table. **`FlareBlockEntity.delete` (`sub_100DF070`) does not reference `A2261`** — it calls `remove_static_point_light` (`0x100DF34C`) |
| **health** | `A1031` (= `DEFAULT_BLOCK_HEALTH`) | 5 | **retail_alias** | `constants.py:3017`; named `:3016`; dump `:2004`. **The binding IS retail**, proven at instruction level: `FlareBlockEntity.post_initialize` = `sub_100DE910` loads global `"A1031"` (`dword_1028F468`, `strtab:1393`) at `0x100DECAC` and passes it as the 6th positional arg to `scene.block_manager.add_user_block(x, y, z, (r,g,b), A1031, False)` |
| shoot_interval | `FlareBlockTool.shoot_interval` | 0.5 | retail_literal | `flareBlockTool.py:17`, hardcoded (not a named constant) |
| max_place_distance | `MAX_BLOCK_DISTANCE` / `A1017` | **10** normal mode | retail_alias | `constants.py:2975`; named `:2974`. Bound in the inherited base: `blockToolCommon.py:29` and `:152` — `max_block_distance = MAX_BLOCK_DISTANCE if not character.scene.manager.classic else CLASSIC_MAX_BLOCK_DISTANCE` |
| max_place_distance_classic | `CLASSIC_MAX_BLOCK_DISTANCE` / `A1012` | **5** classic mode | retail_alias | `constants.py:2960`; named `:2959`. **Retail halves the flare block's reach in classic mode.** BattleSpades range-gates packet 104 with `MAX_BLOCK_DISTANCE` unconditionally (`deployables.py:67`) |
| model | `BLOCK_MODEL` / `BLOCK_VIEW_MODEL` | `block`, world load offset `(5, -13, 0.0)`; view model no offset | retail_literal | `models.py:319-320`. `kv6/block.kv6` present. The flare block is **terrain**, not a KV6 entity — `post_initialize` re-adds a real coloured voxel |
| model_size | — | null | absent | `load_model` takes no size parameter; no `FLAREBLOCK_MODEL_SIZE` exists. The view-model transforms `Vector3(-0.04, 0.0, 0.3)` / `Vector3(0.0, 45.0, 0.0)` (`flareBlockTool.py:29-30`) are **view** transforms, not entity offsets |
| tool_file_name | `TOOL_FILE_NAMES[FLAREBLOCK_TOOL]` / `A557` | `'glowblock'` | retail_named | dict header `constants.py:1691`, entry `:1729`, alias `:1763`. **There is no constant named `TOOL_ICONS` anywhere in retail.** Feeds `load_weapon_image()` (`aoslib/weapons/__init__.py:20-23`); `png/ui/weapons/glowblock.png` exists |
| tool_display_name | `TOOL_NAMES[FLAREBLOCK_TOOL]` | key `FLARE_BLOCK_TOOL` → `u'Flare Block'` | retail_named | dict header `constants.py:1765`, entry `:1803`; string at `aoslib/strings/english.py:175` |
| damage_type | `TOOLS_DAMAGE_TYPE[FLAREBLOCK_TOOL]` (`A418`) | **null** (explicit `None`) | retail_named | dict header `constants.py:1003`, entry **`:1033`**; secondary table `TOOLS_SECONDARY_DAMAGE_TYPE` (`:1073`) entry `:1103` also `None`. **Retail explicitly decided this, it is not a gap** |
| kill_type | `TOOLS_KILL_TYPE[FLAREBLOCK_TOOL]` | `WEAPON_KILL` = 0 | retail_named | `:1216`, enum `:1146`. A flare block cannot kill |
| water_legal | — | **false in retail** | retail_literal | **Retail rejects placement past the water plane** in the inherited base: `blockToolCommon.py:81-84` — `if z > self.character.scene.block_manager.max_modifiable_z:` → `strings.BLOCK_PLACE_FAIL_WATER`, `self.valid_placement = False`; and `:157` `if hit_block.z > Z_ABOVE_WATERPLANE: return False`. `Z_ABOVE_WATERPLANE = MAP_Z - 2 = 238` (`constants.py:4853-4854`). BattleSpades' gate `0 <= z <= 238` (`deployables.py:76-77,90-94`) lands on the same boundary |
| support_rule_neighbours | — | 6 | battlespades | `server/entities/flare_block.py:9-13` — six face neighbours, satisfied by solid terrain **or another live flare block**, re-checked every tick with self-destruction (`:42-53,:70-77`). **Retail is not silent on attachment**: `blockToolCommon.py:50-61` runs `scan_bridge_placement` and raises `BLOCK_PLACE_FAIL_NOT_ATTACHED` on `floating_blocks`, then sets `valid_placement = bridge_valid`; `:63-70` runs `map.has_neighbors(...)` which tints the ghost red at `:94`; `:78` calls the native `block_manager.valid_to_add`. What is invented is the specific 6-neighbour set, the flare-supports-flare clause, and the per-tick re-check |
| team_only | — | false | battlespades | Entity carries the placer's team as wire state and colour (`deployables.py:108-115`), but nothing restricts who may destroy or stand on it |

---

## 14 — BOMB_PICKUP

Carried form `aoslib/weapons/bombTool.py:13` (`BombTool`); pickup→tool binding
`aoslib/weapons/list.py:140`. Ground entity `BombPickup` is compiled into `gameScene.pyd`
(methods `initialize`/`update`/`set_fuse`/**`delete`**/`set_position`/`set_velocity`/
`update_fuse_fx`/`explode`/`create_fuse_fx`, plus a scene-level `players_holding_bombs` set and
`smoke_spawn_interval`). **The source module name is not proven** — no `bomb.py`/`bomb.pyc`
exists anywhere under `aceofspades_source`. An **unobfuscated** sibling build,
`aoslib/scenes/main/gameScene_old.pyd`, exposes the named constants the entity binds:
`BOMB_DAMAGE`, `BOMB_ENTITY_MODEL`, `BOMB_EXPLOSION_RADIUS`, `BOMB_PICKUP`,
`BOMB_SMOKE_GENERATION_*`, `BOMB_SMOKE_X/Y/Z_OFFSET`, `BOMB_THROW_SPEED`, `BOMB_VIEW_MODEL`,
`PICKUP_DISTANCE`.

| field | constant | value | provenance | source |
|---|---|---|---|---|
| type_id | `BOMB_PICKUP` / `A913` | 14 | retail_named | `constants.py:2801`, alias `:2817`. `A913` present in `gameScene.pyd` |
| touch_radius | `PICKUP_DISTANCE` / `A1020` | **3.0** (float) | retail_named | `:2983`, alias `:2984`. `A1020` is present in `gameScene.pyd`, `PICKUP_DISTANCE` is not — the client binds the alias. Its 24 xrefs live in `BombPickup.update` (`sub_100CFEA0`), `DiamondPickup.update` (`sub_100D7C20`), `IntelPickup.update` (`sub_100DB300`). A generic `ENTITY_RADIUS = 5.0` (`:4980`, `A2260 :4981`) also exists and is referenced by `gameScene.pyd` — which of the two `Pickup.pick` uses is unresolved |
| fuse | `BOMB_EXPLOSION_FUSE` / `A2092` | **10** | retail_named | **Defined three times: 10.0 (`:4530`), 7.0 (`:4532`), 10 (`:4534`). Last wins → 10, and the alias at `:4541` captures 10.** The 7.0 line is dead. **[UNBOUND]** — `A2092` is in no `.pyd`; the fuse arrives over the wire via `BombPickup.set_fuse` |
| blast_radius | `BOMB_EXPLOSION_RADIUS` / `A2093` | **7** | retail_alias | **Also defined three times: 7 (`:4531`), 8.0 (`:4533`), 7 (`:4535`). Effective value 7.** Reported as alias because `A2093` is what `gameScene.pyd` **and** `shared/explosionDamageManager.pyd` reference |
| blast_damage | `BOMB_EXPLOSION_DAMAGE` / `A2094` | 500 | **retail_alias** | `:4536`, alias `:4543`. **`A2094` IS present in `shared/explosionDamageManager.pyd`** — the client-side module that applies explosion damage. It is not a server-only number |
| block_damage | `BOMB_EXPLOSION_BLOCK_DAMAGE` / `A2095` | 20 | retail_named | `:4537`, alias `:4544`. `A2095` is genuinely absent from every `.pyd` |
| knockback_max | `BOMB_EXPLOSION_KNOCKBACK_MAX` / `A2096` | **3.0** (float) | **retail_alias** | `:4538`, alias `:4545`. `A2096` present in `explosionDamageManager.pyd` |
| knockback_min | `BOMB_EXPLOSION_KNOCKBACK_MIN` / `A2097` | **2.0** (float) | **retail_alias** | `:4539`, alias `:4546`. `A2097` present in `explosionDamageManager.pyd` |
| throw_speed | `BOMB_THROW_SPEED` / `A2098` | 10 (int) | retail_named | `:4540`; bound at `bombTool.py:38` as `orientation * BOMB_THROW_SPEED`. **`bombTool.pyc` contains the literal string `BOMB_THROW_SPEED`, so the tool binds the NAMED constant**; alias `A2098` (`:4547`) is separately referenced by `gameScene.pyd` (the ground entity applies the same speed) |
| no_pickup_after_drop_time | `NO_PICKUP_AFTER_DROP_TIME` / `A1023` | 2.5 | retail_named | `:2992`, alias `:2993`. `A1023` occurs only in `aoslib/scenes/main/player.pyd` — the lockout is enforced by the compiled `Player` class. Shared by all three objective pickups |
| model | `BOMB_ENTITY_MODEL` | `Bomb`, KV6 load offset `(0.0, 0.0, 7.0)` | retail_literal | `models.py:289`. Held `BOMB_MODEL` uses `(5.0,-13.0,2.0)` (`:287`); `BOMB_VIEW_MODEL` has no offset (`:288`, redundantly reloaded at `:290`). `kv6/Bomb.kv6` present |
| fuse_smoke_emitter_offset | `BOMB_SMOKE_X/Y/Z_OFFSET` | `(-0.05, -0.05, -1.2)` | retail_named | **`constants.py:4716-4718` — this is the GROUND ENTITY's emitter offset**, referenced by `BombPickup.create_fuse_fx` (visible in `gameScene_old.pyd`) and **not** by `BombTool`. The tool uses `BOMB_TOOL_SMOKE_FP|TP_*_OFFSET` (`:4710-4715`) instead |
| fuse_smoke_params | `BOMB_SMOKE_GENERATION_*` | rate 25 Hz + decay/lifespan/size/velocity set | retail_named | `constants.py:4703-4709`; driven by `BombTool.update` / `create_fuse_fx` (`bombTool.py:50-97`) for the carried form |
| damage_type | `TOOLS_DAMAGE_TYPE[BOMB_TOOL]` → `BOMB_DAMAGE` / `A393` | 19 | retail_named | enum `:954` position 19, mapping `:1036`, alias `:975`. `A393` occurs in `gameScene.pyd` |
| kill_type | `TOOLS_KILL_TYPE[BOMB_TOOL]` → `BOMB_KILL` / `A438` | 17 | retail_named | enum `:1146` position 17, mapping `:1219`, alias `:1165`. **`A438` IS present in `aoslib/hud/hud.pyd` and `shared/explosionDamageManager.pyd`** — it drives the HUD kill-feed entry and kill attribution. It is not a server-authored-only field |
| pickup_tool_id | `BOMB_TOOL` | 25 | retail_named | tool enum `:861` position 25; `PICKUPS = {BOMB_PICKUP: BOMB_TOOL, DIAMOND_PICKUP: DIAMOND_TOOL, INTEL_PICKUP: INTEL_TOOL}` at `list.py:140-141`. Carrying destroys the ground entity and equips tool 25 |
| model_size | — | null | absent | No `BOMB_MODEL_SIZE`. `Entity.size` default 1.0 (`entity.py:31,34`) but any `BombPickup` override is inside `gameScene.pyd` |
| crater_radius | — | null | absent | No separate crater constant; block falloff uses radius + block damage |
| team_only / health / arm_delay / lifetime / respawn_delay / light_radius / ammo | — | null | absent | `BombTool` sets `draw_ammo = False` (`:23`) and declares no ammo tuple. No `BOMB_LIFETIME` (compare `DIAMOND_LIFETIME = 60`, which does exist) |

`BombTool` is a no-crosshair, no-ammo prop (`NEVER_CROSSHAIR`, `bombTool.py:20-23`) held at
`Vector3(0.0,0.18,0.0)` with arms offset `Vector3(0.0,-0.18,0.0)` (`:31-34`). Only selectable
while carried and un-swappable: `is_available()` returns `self.carried`, `can_swap()` returns
`not self.carried` (`:41-45`).

---

## 15 — DIAMOND_PICKUP

Carried form `aoslib/weapons/diamondTool.py:12`; ground entity `DiamondPickup` compiled into
`gameScene.pyd` (methods `initialize`/`on_delete`/`update`/`set_packet`/`set_position`/
`set_velocity` — **no `set_fuse`**, unlike bomb and intel).

| field | constant | value | provenance | source |
|---|---|---|---|---|
| type_id | `DIAMOND_PICKUP` / `A914` | 15 | retail_named | `constants.py:2801`, alias `:2818` |
| touch_radius | `PICKUP_DISTANCE` / `A1020` | 3.0 | retail_named | `:2983`, alias `:2984`. Corroborated by the reference server's `sq_distance <= 9` (`server/aosmodes/dia.py:37`) |
| lifetime | `DIAMOND_LIFETIME` / `A1037` | 60 | retail_named | `:3034`, alias `:3035`. **Neither name nor alias occurs in ANY `.pyd`** — server-enforced only |
| throw_speed | `DIAMOND_THROW_SPEED` / `A2099` | 15 | retail_named | `:4551`; bound `diamondTool.py:35`. `diamondTool.pyc` contains the literal `DIAMOND_THROW_SPEED`, so the tool binds the named constant; alias `:4552` is separately referenced by `gameScene.pyd` |
| no_pickup_after_drop_time | `NO_PICKUP_AFTER_DROP_TIME` / `A1023` | 2.5 | retail_named | `:2992`, alias `:2993` (only in `player.pyd`) |
| respawn_delay | `DIA_TIME_BETWEEN_DIAMOND_SPAWN` / `A2613` | 15 | retail_named | `shared/constants_gamemode.py:433`, alias `:434`. **This is the Diamond Mine mode's spawn cadence, not a per-entity respawn timer** |
| model | `DIAMOND_ENTITY_MODEL` | `diamond`, KV6 load offset `(0.0, 0.0, 7.0)` | retail_literal | `models.py:293`. Third-person `DIAMOND_MODEL` `(5.0,-13.0,2.0)` (`:291`), view model no offset (`:292`). Identical entity offset to the bomb |
| model_size | — | null | absent | no `DIAMOND_MODEL_SIZE`; override is inside `gameScene.pyd` |
| damage_type | `TOOLS_DAMAGE_TYPE[DIAMOND_TOOL]` | **null** (explicit `None`) | retail_named | `constants.py:1037`. Recovered value, not a gap |
| kill_type | `TOOLS_KILL_TYPE[DIAMOND_TOOL]` → `WEAPON_KILL` | 0 | retail_named | `:1220`, enum `:1146` |
| pickup_tool_id | `DIAMOND_TOOL` | 26 | retail_named | tool enum `:861` position 26; `list.py:140` |
| fx_particle_count | `DIAMOND_PICKUP_FX_NOOF` / `A2343` | 50 | retail_named | `:5176`, alias `:5192`. **Alias referenced in `gameScene.pyd`** — twice the crate burst (`CRATE_PICKUP_FX_NOOF = 25`, `:5141`) |
| fx_vertical_speed | `..._FX_VERTICAL_SPEED` / `A2344` | 0.08 | retail_named | `:5177`, alias `:5193` (in `gameScene.pyd`) |
| fx_explosion_speed | `..._FX_EXPLOSION_SPEED` / `A2345` | 0.07 | retail_named | `:5178`, alias `:5194` (in `gameScene.pyd`) |
| fx_particle_size | `..._FX_PARTICLE_SIZE` / `A2346` | 5 | retail_named | `:5179`, alias `:5195` |
| fx_initial_rotation | `..._FX_INITIAL_ROTATION` / `A2347` | 0 | retail_named | `:5180`, alias `:5196` |
| fx_rotation_speed | `..._FX_ROTATION_SPEED` / `A2348` | 180 | retail_named | `:5181`, alias `:5197` |
| fx_decay_rate | `..._FX_DECAY_RATE` / `A2349` | 1 | retail_named | `:5182`, alias `:5198` |
| fx_lifetime | `..._FX_LIFETIME` / `A2350` | 2 | retail_named | `:5183`, alias `:5199` |
| fx_start_frame | `..._FX_START_FRAME` / `A2351` | 0 | retail_named | `:5184`, alias `:5200` |
| fx_num_frames_x | `..._FX_NUM_FRAMES_X` / `A2352` | 4 | retail_named | `:5185`, alias `:5201`. 4×4 sheet |
| fx_num_frames_y | `..._FX_NUM_FRAMES_Y` / `A2353` | 4 | retail_named | `:5186`, alias `:5202` |
| fx_loop | `..._FX_LOOP` / `A2354` | 0 | retail_named | `:5187`, alias `:5203` |
| fx_framerate | `..._FX_FRAMERATE` / `A2355` | 30 | retail_named | `:5188`, alias `:5204` |
| fx_collides | `..._FX_COLLIDES` / `A2356` | false | retail_named | `:5189`, alias `:5205` |
| fx_gravity | `..._FX_GRAVITY` / `A2357` | false | retail_named | `:5190`, alias `:5206` |
| fx_alpha_blend_mode | `..._FX_ALPHA_BLEND_MODE` / `A2358` | 2 (`ALPHA_BLEND_MODE_ADDITIVE`) | retail_named | `:5191`; enum `:5134`. **`A2358` is the ONE member of the 16-constant FX block absent from every `.pyd`** — the compiled burst may hardcode additive blending |
| health / fuse / blast_radius / light_radius / team_only | — | null | absent | The diamond does not explode and has no fuse; the entire `DIAMOND` block is two lines (`:4551-4552`) |

---

## 16 — INTEL_PICKUP

Carried form `aoslib/weapons/intelTool.py:12`; ground entity `IntelPickup` compiled into
`gameScene.pyd` (`initialize`/`on_delete`/`update`/`set_fuse`/`set_position`/`set_velocity`,
plus an entity-specific `floating_slowdown_factor`).

| field | constant | value | provenance | source |
|---|---|---|---|---|
| type_id | `INTEL_PICKUP` / `A915` | 16 | retail_named | `constants.py:2801`, alias `:2819` |
| touch_radius | `PICKUP_DISTANCE` / `A1020` | **3.0** (float) | retail_named | `:2983`, alias `:2984`. Our server uses the same constant for pickup and return-on-touch (`BattleSpades/modes/ctf.py:382`, `:392`) |
| throw_speed | `INTEL_THROW_SPEED` | 15 | retail_named | `:4554`; bound `intelTool.py:36`. `intelTool.pyc` contains the literal name. **Alias `A2100` (`:4556`) appears in NO `.pyd`** — unlike the bomb's `A2098` and the diamond's `A2099`, the compiled entity never re-applies the intel throw speed |
| auto_return_time | `CTF_INTEL_RETURN_TIME` / `A2644` | 60 | retail_named | `constants_gamemode.py:495`, alias `:496`. Neither occurs in any client `.pyd` — purely a server rule. Consumed at `BattleSpades/modes/ctf.py:364` |
| no_pickup_after_drop_time | `NO_PICKUP_AFTER_DROP_TIME` / `A1023` | 2.5 | retail_named | `:2992`, alias `:2993`. Mirrored as `pickup_cooldown` at `BattleSpades/modes/ctf.py:135` |
| use_other_team_color | `IntelTool.use_other_team_color` | true | retail_literal | `intelTool.py:23`. The `Tool` base default is `False` (`tool.py:33`) and **`IntelTool` is the only tool in the tree that overrides it** — the carried intel is tinted with the ENEMY team's colour |
| no_swap_rule | `can_swap` | `return not self.carried` | retail_literal | `intelTool.py:42-43`, paired with `is_available(): return self.carried` (`:39-40`). While carrying, the player **cannot switch tools at all**. Base default is `return not self.is_active()` (`tool.py:235-236`) |
| model | `INTEL_ENTITY_MODEL` | `intel` | retail_literal | `models.py:342` — `load_model('INTEL_ENTITY_MODEL', 'intel')`, **two arguments** |
| model_z_offset | — | **null** | **absent** | `models.py:342` passes **no offset argument**; the signature default is `offset=None` (`:49`) and it is forwarded to `KV6(...)` unchanged (`:61`). What `KV6()` does with `None` is compiled. **Do not report 0** — nothing was read. Contrast the held models: `INTEL_MODEL (6,-12,-8.0)` (`:340`), `INTEL_VIEW_MODEL (0,0,-6.8)` (`:341`) |
| model_size | — | null | absent | no `INTEL_MODEL_SIZE`; override inside `gameScene.pyd` |
| floating_slowdown_factor | `IntelPickup.floating_slowdown_factor` | **null** | absent | The attribute exists in the `gameScene.pyd` qualname table and is **IntelPickup-specific** (BombPickup instead has `players_holding_bombs` + `smoke_spawn_interval`; DiamondPickup has none). So the dropped intel has bespoke floating/drift physics with a numeric factor that exists only in compiled code. No FLOATING/SLOWDOWN constant is in `constants.py` |
| minimap_exposure_time | `INTEL_MINIMAP_EXPOSURE_TIME` / `A2101` | 30 | retail_named | `:4555`, alias `:4557`. Absent from every `.pyd` — server-side timer for how long the carrier stays exposed |
| minimap_height_icon_threshold | `MINIMAP_HEIGHT_ICON_THRESHOLD` / `A2371` | 4 | retail_named | `:5235`. `MINIMAP_HEIGHT_ICON_ENTITIES = [CAPTURE_POINT_ENTITY, INTEL_PICKUP]` (`:5236`) — **the only two entities with the height indicator**. Aliases `A2371`/`A2372` (`:5237-5238`) are referenced in `aoslib/hud/hud.pyd` |
| damage_type | `TOOLS_DAMAGE_TYPE[INTEL_TOOL]` | **null** (explicit `None`) | retail_named | `constants.py:1042` |
| kill_type | `TOOLS_KILL_TYPE[INTEL_TOOL]` → `WEAPON_KILL` | 0 | retail_named | `:1225`, enum `:1146` |
| pickup_tool_id | `INTEL_TOOL` | 30 | retail_named | tool enum `:861` position 30; `list.py:141`. **Entity 16 maps to tool 30 — the id spaces are not contiguous** |
| minimap_icon | — | `minimap_intel` | retail_literal | `intelTool.py:13`. `png/ui/minimap_intel.png` present |
| team_only | — | null | absent | The enemy-only pickup rule is **not recoverable from retail client code**. The only executable statement of it (`aceofspades_source/server/aosmodes/ctf.py:72-74`) is in a Python-3 rewrite whose collide dispatcher is commented out (`aosserver/types.py:141-148`), so it never runs. Our server enforces it at `BattleSpades/modes/ctf.py:388-395` |
| fuse | — | null | absent | `IntelPickup.set_fuse` exists (unlike DiamondPickup), so the entity does receive a wire fuse field, but **no `INTEL_*_FUSE` constant exists anywhere**. Most plausibly the 60 s return countdown — not confirmed, not asserted |
| lifetime | — | null | absent | The intel does not expire, it auto-**returns**. Do not model it as a despawn |
| health / blast_radius / light_radius | — | null | absent | The intel block is three lines (`:4554-4557`). No particle FX block either |

---

## 17 — AIRSTRIKE_ENTITY

Class `AirStrikeEntity` compiled into `gameScene.pyd` from `airStrikeEntity.py`
(build-path string at `strtab:418`; methods `initialize`/`set_position`/`set_velocity`/
`on_delete`/`draw_on_minimap`/`update` at `strtab:419, 421, 422, 423, 424, 425` — **`:420` is
`sq_crate_parachute_removal_height`, an unrelated interleaved string**). Named block
`:4560-4566`, alias block `:4567-4573`; they **agree on all seven values**, and the raw dump
`shared/backup/constants-copy.py:3167-3173` agrees.

| field | constant | value | provenance | source |
|---|---|---|---|---|
| type_id | `A916` (= `AIRSTRIKE_ENTITY`) | 17 | retail_alias | `constants.py:2820`, enum `:2801`. `A916` appears once in `gameScene.pyd` |
| blast_radius | `A2102` | 6 (int) | retail_alias | `:4567`; named `:4560`; dump `:3167`. **`A2102` IS referenced inside `gameScene.pyd`** |
| blast_damage | `A2103` | **400.0** (float) | retail_alias | `:4568`; named `:4561` reads `= 400.0`; dump `:3168`. **[UNBOUND in client]** — server-applied. 400 vs `INITIAL_HEALTH = 100.0` (`:4834`) is why it is unconditionally lethal |
| block_damage | `A2104` | 15 (int) | retail_alias | `:4569`; named `:4562`; dump `:3169`. Against `DEFAULT_BLOCK_HEALTH = 5` (`:3016`) this one-shots ordinary terrain |
| knockback_max | `A2105` | **2.0** (float) | retail_alias | `:4570`; named `:4563`; dump `:3170` |
| knockback_min | `A2106` | **1.0** (float) | retail_alias | `:4571`; named `:4564`; dump `:3171` |
| **shell_speed** | `A2107` (= `AIRSTRIKE_SHELL_SPEED`) | 100 (int) | retail_alias | `:4572`; named `:4565`; dump `:3172`. **`A2107` IS referenced inside `gameScene.pyd`** — the client simulates the falling shell locally. **This is a descent/travel speed, NOT a throw impulse.** Retail's genuine throw constants are named `*_THROW_SPEED`; no `AIRSTRIKE_THROW_SPEED` exists. Do not wire 100 into a player-throw path |
| gravity_multiplier | `A2108` | 100 (int) | retail_alias | `:4573`; named `:4566`; dump `:3173`. Also referenced inside `gameScene.pyd`. Compare `ROCKET_GRAVITY_MULTIPLIER = 0.05` (`:6348`) and `MOLOTOV_GRAVITY_MULTIPLIER = 1.0` (`:6599`) — the shell falls under 100× gravity, i.e. essentially straight down |
| model | `AIRSTRIKE_BOMB_VIEW_MODEL` | `airstrike_bomb`, **offset = None** | retail_literal | `models.py:358` — the **only** airstrike `load_model` call in the entire file. Its single reference in the binary is `gameScene.pyd.strtab.json:417`, immediately adjacent to the `airStrikeEntity.py` path at `:418`. Despite the `_VIEW_` substring this is the AirStrikeEntity world model; there is no separate entity model. `kv6/airstrike_bomb.kv6` present, lowercase |
| model_z_offset | — | **null (no offset passed)** | absent | `models.py:358` omits the offset argument |
| model_size | — | null | absent | No airstrike `*_MODEL_SIZE` string exists in `gameScene.pyd.strtab.json` |
| damage_type | `A392` (= `AIRSTRIKE_DAMAGE`) | 18 | retail_alias | `constants.py:974`, enum `:954`. Client dispatch `BlockManager.handle_airstrike_damage` exists |
| kill_type | `A437` (= `AIRSTRIKE_KILL`) | 16 | retail_alias | `constants.py:1164`, enum `:1146`. In `ONE_HIT_KILL_WEAPONS` (`:5260`) |
| debug_trigger | `DEBUG_COMMAND_7` / `A31` | `'/airstrike'` | retail_named | `constants.py:160`, alias `:170`. The retail debug command that fires a test strike |
| **cadence** | — | **null** | **absent** | **No retail constant for shell interval or shell count exists.** The whole AIRSTRIKE block is the seven constants at `:4560-4566`. An exhaustive sweep of `constants.py`, `constants_gamemode.py`, `constants_audio.py`, `constants_playerprofile.py` and all of `aoslib/**/*.py` finds nothing. The MultiHill strike is scheduled by the retail game server, which is **not** in this tree — `aceofspades_source/server/aosmodes/mh.py` is a Python-3 asyncio re-implementation with zero airstrike code (case-insensitive grep for `airstrike` over `aceofspades_source/server` returns nothing). **`DEM_TIME_TO_WAIT_FOR_AIRSTRIKE = 5.0` (`constants_gamemode.py:467`, `A2630 :468`) is a Demolition-mode pre-strike delay and MUST NOT be repurposed as the cadence** |
| lifetime | — | null | absent | The shell detonates on impact (`AirStrikeEntity.on_delete`), not on a fuse |
| team_only | — | null | absent | No retail constant team-filters it. The MH achievement string `u'Trigger an airstrike and survive without leaving the hill'` (`aoslib/strings/english.py:242`) implies it damages everyone including the triggering team |

Audio: `airstrike_siren_oneshot` / `airstrike_flyby` / `airstrike_explode(_water)`
(`constants_audio.py:141-142, 399-401`); all assets present. Announcement string
`u'Hill depleted! \nAirstrike incoming!'` (`english.py:149` — note the embedded newline).

---

## 18 — AMMO_DROP_POINT_ENTITY

Class `AmmoDropPointEntity` compiled into `gameScene.pyd` from `ammoDropPoint.py`
(display string `'Ammo drop point'` at `strtab:264`, model symbol at `:265`, build path at
`:266`; **the class-name string is at `strtab:1935`, not in that triple**). A whole-file grep
for `AmmoDropPointEntity.` returns **zero** qualified method names (compare `UGCEntity.` →
10 hits), so the class overrides nothing on `Entity`.

| field | constant | value | provenance | source |
|---|---|---|---|---|
| type_id | `AMMO_DROP_POINT_ENTITY` / `A917` | 18 | retail_named | `constants.py:2801`, `ENTITY_LIST :2802`, alias `:2821` |
| model | `AMMO_DROP_POINT_MODEL` | `Crate_Target` | retail_literal | `models.py:347`. Path is synthesised as `join('kv6', name + '.kv6')` at `models.py:53` with `KV6_PATH = 'kv6'` (`:15`) → `kv6/Crate_Target.kv6`, present, 3018 bytes. **Byte-identical to `ugc_baseplate.kv6`** (both md5 `50cf45bf5df28414c983ffe714bcff44`) — one mesh, two names |
| kv6_load_offset | — | `(0.0, 0.0, 1.0)` | retail_literal | `models.py:347` third positional arg. A **load-time pivot**, distinct from `Entity.model_position_offsets` (`entity.py:104-132`). Axis/sign convention unreadable (`kv6.pyd` compiled) |
| model_size | — | null | absent | Any `AmmoDropPointEntity.size` override is inside `gameScene.pyd`. `Entity.size` defaults to 1.0 (`entity.py:31,34`) but **every** shipped subclass overrides it (Drill 0.08, GLGrenade 0.02), so do not assume 1.0 |
| ugc_item_id | `UGC_ITEM_AMMO_DROP_POINT` / `A483` | 1 | **retail_named** | Value comes from the named tuple unpack at `constants.py:1318`; `A483` at `:1320` merely re-exports the same object, and every consumer binds the named symbol |
| ugc_tool_image | `UGC_TOOL_IMAGES[UGC_ITEM_AMMO_DROP_POINT]` | `ugc_ammo_drop` | retail_named | table `constants.py:4268`, entry `:4270`. `png/ui/ugc_tools/ugc_ammo_drop.png` present |
| ugc_minimap_icon | `UGC_MINIMAP_ICON_NAMES[...]` / `A2034` | `minimap_ammocrate` | retail_named | table `:4380`, entry `:4382`, alias `:4386`. `png/ui/minimap_ammocrate.png` present |
| ugc_objective | `UGC_OBJECTIVE_AMMOCRATE_SPAWNS` | min 2, max 25 | retail_named | `shared/constants_ugc_objectives.py:50-55` |
| respawn_delay | — | **null** | **absent** | `CRATE_SPAWN_DELAY = 25` is real (`:3040`, `A1039 :3041`) but its **only** consumers are the import at `constants_matchmaking.py:5` and the game-rule default `RULE_CRATES_SPAWN_TIME` at `:413`. It is a **match-level crate cadence**, not a property of the marker. Nothing binds it to entity 18 |
| icon (entity attribute) | `Entity.icon` | null | retail_literal | `entity.py:25`; `Entity.minimap = False` (`:24,:30`). The UGC icon above is a separate authoring-path asset |
| touch_radius / health / fuse / lifetime / blast_radius / light_radius / team_only | — | null | absent | Nothing in retail defines any of these for id 18, and our server registers no behaviour for it |

**Verdict: render-only, no simulation.** UGC path caveat: `models.py:451-453` renders the UGC
**item** (id 1) as `[ugc_baseplate, ammocrate]` at scale 0.25, z −0.5 — a **different visual
from the entity's `Crate_Target`**. Do not merge the two.

---

## 19 — HEALTH_DROP_POINT_ENTITY

Class `HealthDropPointEntity`, compiled `healthDropPoint.py` (display string `:259`, model
symbol `:260`, class name `:261`, build path `:263`; **`:262` inside that range is
`end_multiblock_damage`, unrelated**). No `HealthDropPointEntity.*` qualified method names.

| field | constant | value | provenance | source |
|---|---|---|---|---|
| type_id | `HEALTH_DROP_POINT_ENTITY` / `A918` | 19 | retail_named | `constants.py:2801`, `:2802`, alias `:2822` |
| model | `HEALTH_DROP_POINT_MODEL` | `Crate_Target` | retail_literal | `models.py:346`. Same mesh as 18 and 20 |
| kv6_load_offset | — | `(0.0, 0.0, 1.0)` | retail_literal | `models.py:346`. **Identical to 18 and 20** — `ENTITY_PORT_RECOVERY.md:50` shows no offset for 19, which is a transcription artefact |
| model_size | — | null | absent | override inside `gameScene.pyd` |
| ugc_item_id | `UGC_ITEM_HEALTH_DROP_POINT` / `A482` | 0 | **retail_named** | `constants.py:1318` (first element of the `xrange(19)` unpack); alias `:1319`. Also the `UGCTool` default selection (`ugcTool.py:30`) |
| ugc_tool_image | `UGC_TOOL_IMAGES[...]` | `ugc_health_drop` | retail_named | `constants.py:4268-4269`. `png/ui/ugc_tools/ugc_health_drop.png` present |
| ugc_minimap_icon | `UGC_MINIMAP_ICON_NAMES[...]` | `minimap_healthcrate` | retail_named | `:4380-4381`. Asset present |
| ugc_objective | `UGC_OBJECTIVE_HEALTHCRATE_SPAWNS` | min 2, max 25 | retail_named | `constants_ugc_objectives.py:56-61` |
| respawn_delay | — | **null** | **absent** | same as 18 |
| everything else | — | null | absent | no touch radius, health, fuse, lifetime, blast, light or team gating exists for id 19 |

**Verdict: render-only.** The UGC item (id 0) renders as `[ugc_baseplate, healthcrate]` at
0.25 / −0.5 (`models.py:448-450`) — again not the entity's mesh. Note also that
`BattleSpades/server/map_metadata.py:119` maps `ugc_health_drop` to `HEALTH_CRATE` (entity 4),
**not** to entity 19, so the UGC-item→entity link is an inference, not a recovered binding.

---

## 20 — BLOCK_CRATE_DROP_POINT_ENTITY

Class `BlockCrateDropPointEntity`, compiled `blockCrateDropPoint.py` (`strtab:267` display
string, `:268` model symbol, `:269` class name, `:270` build path). No qualified method names.

| field | constant | value | provenance | source |
|---|---|---|---|---|
| type_id | `BLOCK_CRATE_DROP_POINT_ENTITY` / `A919` | 20 | retail_named | `constants.py:2801`, `:2802`, alias `:2823`. Mirrored at `BattleSpades/shared/constants.py:492` and `include/battlespades/shared/retail_constants.hpp:131` |
| model | `BLOCK_CRATE_DROP_POINT_MODEL` | `Crate_Target` | retail_literal | `models.py:348`. Third consumer of the same mesh — a port can share one KV6 instance across 18/19/20 |
| kv6_load_offset | — | `(0.0, 0.0, 1.0)` | retail_literal | `models.py:348` |
| model_size | — | null | absent | override inside `gameScene.pyd` |
| ugc_item_id | `UGC_ITEM_BLOCK_DROP_POINT` / `A484` | 2 | **retail_named** | `constants.py:1318`, alias `:1321`. **Naming mismatch is real**: the UGC item is `UGC_ITEM_BLOCK_DROP_POINT`, the entity is `BLOCK_CRATE_DROP_POINT_ENTITY` |
| ugc_tool_image | `UGC_TOOL_IMAGES[...]` | `ugc_block_drop` | retail_named | `constants.py:4268-4271`. Asset present |
| ugc_minimap_icon | `UGC_MINIMAP_ICON_NAMES[...]` | `minimap_blockcrate` | retail_named | `:4380-4383`. Asset present |
| ugc_objective | `UGC_OBJECTIVE_BLOCKCRATE_SPAWNS` | min 2, max 25, priority 4 | retail_named | `constants_ugc_objectives.py:62-67` |
| respawn_delay | — | **null** | **absent** | same as 18 and 19 |
| everything else | — | null | absent | no gameplay constant exists for id 20; no BattleSpades behaviour either |

**Verdict: render-only.** UGC item 2 renders as `[ugc_baseplate, block_crate]` at 0.25 / −0.5
(`models.py:454-456`).

---

## 21 — ROCKET_ENTITY

| field | constant | value | provenance | source |
|---|---|---|---|---|
| type_id | `ROCKET_ENTITY` / `A920` | 21 | retail_named | `constants.py:2801`, alias `:2824` |
| wire_safe | — | true | retail_literal | `A920` present in `gameScene.pyd` |
| speed | `ROCKET_SPEED` / `A1397` | 75 | retail_alias | `:6346` / `:3650`; `aoslib/scenes/main/rocket.py:29` |
| gravity_multiplier | `ROCKET_GRAVITY_MULTIPLIER` / `A1398` | 0.05 | retail_alias | `:6348` / `:3651`; `rocket.py:30` |
| model_z_offset | `ROCKET_MODEL_Z_OFFSET` | 0.0 | retail_named | `:6358` |
| model_size | `ROCKET_MODEL_SIZE` | — | retail_named | `:6357` (value not read this pass) |
| health | `ROCKET_HEALTH` | — | retail_named | `:6359` (value not read this pass) |
| blast_wave_radius | `ROCKET_EXPLOSION_BLAST_WAVE_RADIUS` | 6.0 | retail_named | `:6352` |

**Not fully recovered in this pass.** The `ROCKET_*` named block runs `:6339-6360` with alias
twins around `A1390`-`A1400`. `rocket.py` is readable source — a follow-up pass should be cheap.

---

## 22 — ROCKET2_ENTITY

| field | constant | value | provenance | source |
|---|---|---|---|---|
| type_id | `ROCKET2_ENTITY` / `A921` | 22 | retail_named | `constants.py:2801`, alias `:2825` |
| wire_safe | — | true | retail_literal | `A921` present in `gameScene.pyd` |
| model_size | `ROCKET2_MODEL_SIZE` | — | retail_named | `:6382` |
| model_z_offset | `ROCKET2_MODEL_Z_OFFSET` | 0.0 | retail_named | `:6383` |
| health | `ROCKET2_HEALTH` | — | retail_named | `:6384` |
| blast_wave_radius | `ROCKET2_EXPLOSION_BLAST_WAVE_RADIUS` | 4.0 | retail_named | `:6375` |

**Not recovered in this pass.** A parallel `UGC_ROCKET2_*` block exists at `:6390-6400`.

---

## 23 — DRILL_ENTITY

| field | constant | value | provenance | source |
|---|---|---|---|---|
| type_id | `DRILL_ENTITY` / `A922` | 23 | retail_named | `constants.py:2801`, alias `:2826` |
| wire_safe | — | true | retail_literal | `A922` present in `gameScene.pyd` |
| model_size | `DRILL_MODEL_SIZE` | — | retail_named | `:6462` |
| model_z_offset | `DRILL_MODEL_Z_OFFSET` | 0.0 | retail_named | `:6463` |
| health | `DRILL_HEALTH` | — | retail_named | `:6464` |

**Not recovered in this pass.** Client class exists (`aoslib/scenes/main/drill.py`, `size = 0.08`
at `:20`). A parallel `UGC_DRILL_*` block exists at `:6490-6496`.

---

## 24 — SNOWBALL_ENTITY

| field | constant | value | provenance | source |
|---|---|---|---|---|
| type_id | `SNOWBALL_ENTITY` / `A923` | 24 | retail_named | `constants.py:2801`, alias `:2827` |
| wire_safe | — | true | retail_literal | `A923` present in `gameScene.pyd` |
| kill_type | `SNOWBALL_KILL` | 21 | retail_named | kill enum `:1146` (immediately after `HEALTHCRATE_HP` = 20) |
| smoke_lifetime | `SNOWBALL_SMOKE_LIFETIME` | — | retail_named | `:4803` |

**Not recovered in this pass.**

---

## 25 — CAPTURE_POINT_ENTITY

Class `CapturePointEntity` compiled into `gameScene.pyd` from `capturePoint.py`. Its only
qualified methods in the string table are `set_color` (`:289`), `draw` (`:290`) and
`draw_on_minimap` (`:291`) — **no `update`, no `initialize` override, no touch handler**, the
signature of a purely visual entity.

| field | constant | value | provenance | source |
|---|---|---|---|---|
| type_id | `CAPTURE_POINT_ENTITY` / `A924` | 25 | retail_named | `constants.py:2801`. **Four occurrences in the retail tree**: `:2801` (enum), **`:2802` (`ENTITY_LIST` — entity 25 is a first-class entry, not just an enum slot)**, `:2828` (`A924`), `:5236` (`MINIMAP_HEIGHT_ICON_ENTITIES`). `A924` occurs in `gameScene.pyd`, so the class is registered |
| model | `CP_MODEL` | `cp` | retail_literal | `models.py:339` — `load_model('CP_MODEL', 'cp')`, **no offset argument**. `kv6/cp.kv6` present, CRC-registered as `'cp'` at `model_crcs.py:123`. `cp.kv6` is byte-identical to `pickup.kv6` and `ugc_base_zone.kv6` (sha256 `c5c458eb…`, 2638 bytes) |
| model_z_offset | — | **null** | **absent** | `models.py:339` passes no offset. `CP_MODEL` names the **model asset**, not any offset constant |
| model_size | — | null | absent | override inside `gameScene.pyd`; `Entity.size` default 1.0 |
| minimap_height_icon_threshold | `MINIMAP_HEIGHT_ICON_THRESHOLD` / `A2371` | 4 | retail_named | `:5235`; `MINIMAP_HEIGHT_ICON_ENTITIES = [CAPTURE_POINT_ENTITY, INTEL_PICKUP]` (`:5236`). Aliases referenced in `aoslib/hud/hud.pyd` — **the one provably live capture-point constant** |
| touch_radius | `CAPTURE_POINT_DISTANCE` / `A1021` | 3.0 | retail_named | `:2986`, alias `:2987`. **DEAD CONSTANT** — recursive grep over all `.py` outside `constants.py` returns nothing, and a byte scan of every shipped `.pyd`/`.pyc` finds `A1021` **only inside `shared/constants.pyc` itself**, never in a consumer (contrast `A1020` → `gameScene.pyd`, `A1023` → `player.pyd`). Port only as a documented-unused number |
| refill_time | `CAPTURE_POINT_REFILL_TIME` / `A1022` | 10.0 | retail_named | `:2989`, alias `:2990`. **DEAD CONSTANT**, same evidence |
| restock_behaviour | — | **null** | **absent** | **There is NO restock behaviour attached to entity 25 anywhere.** The restock-at-the-point behaviour belongs to `CommandPost`, whose type is `BASE` = 1 (`aceofspades_source/server/aosserver/types.py:240-241`) — see entity 1 |
| team_only / health / respawn_delay / blast_radius / light_radius | — | null | absent | none exist. `set_color` + no update is consistent with a team-tinted marker |

---

## 26 — TANK_ENTITY

| field | constant | value | provenance | source |
|---|---|---|---|---|
| type_id | `TANK_ENTITY` / `A925` | 26 | retail_named | `constants.py:2801`, alias `:2829` |
| wire_safe | — | **false** | retail_literal | **`A925` is absent from `gameScene.pyd`** — one of the five ids with no client class. Do not send it |
| everything else | — | null | absent | not recovered |

---

## 27 — MOLOTOV_ENTITY

| field | constant | value | provenance | source |
|---|---|---|---|---|
| type_id | `MOLOTOV_ENTITY` / `A926` | 27 | retail_named | `constants.py:2801`, alias `:2830` |
| wire_safe | — | true | retail_literal | `A926` present in `gameScene.pyd` |
| gravity_multiplier | `MOLOTOV_GRAVITY_MULTIPLIER` / `A1648` | 1.0 | retail_alias | `:6599` / `:3902`. Bound as `ExplodeOnImpactEntity.gravity_multiplier` at **`aoslib/scenes/main/explodeOnImpactEntity.py:19`** (`:18` is `icon = None`), applied at `:30` via `set_gravity_multiplier`. `A1648` is in `gameScene.pyd.strtab.json:1432` |
| alias block | `A1641`-`A1657` | — | retail_alias | `constants.py:3895-3911`, matching the named `MOLOTOV_*` block `:6592-6608` |

**Not fully recovered in this pass.** Molotov impact spawns BLOCKFIRE (28), which is fully
recovered below.

---

## 28 — BLOCKFIRE

Class `BlockFireEntity` compiled into `gameScene.pyd` from `blockFire.py`
(**source-path string at `gameScene.pyd.strtab.json:437`** — the file is 4141 lines, so any
citation past that is fabricated). Methods `initialize`/`post_initialize`/`update`/
`set_position`/`delete`/`create_display`/`draw`/`draw_display`/`set_fuse`/`calculate_colour`/
`update_smoke`/`create_fire_smoke`/`draw_on_minimap` at `strtab:438-453`.

**Particle + light only — no KV6 model.** `grep -i fire` over all 522 lines of `models.py`
returns zero hits, and no `BLOCKFIRE_*MODEL*` constant exists.

**Alias mapping.** The BLOCKFIRE gameplay block sits in the **un-aliased tail** section
(`constants.py:6022-6690`), so there is no `A#### = BLOCKFIRE_*` line. The aliases were
recovered positionally: `A1758`-`A1774` at `:4012-4029` matches `:6609-6626` 17-for-17, in
order. Confirmed in the binary: `A1764, A1767, A1771, A1772, A1773, A1774` occur exactly once
each at byte offsets 2517112…2517152 — a contiguous 8-byte-stride run in one code object's
name pool. **A decoy block `A1775`-`A1791` (`:4030-4047`) has the identical 17-value shape
(5, 0.5, 3, 5, 0.8, 0.2, 6.0, 5, 0.5, 2.0, 0.3, -1, 1.0, 2, then the same three colours) —
it is a different fire type (most likely blockgoo/chemical bomb; `blockgooEntity.py` at
`strtab:518`, `chemicalbombEntity.py` at `:532`) and NONE of `A1775`-`A1791` appears in
`gameScene.pyd`.** Do not mistake it for BLOCKFIRE.

| field | constant | value | provenance | source |
|---|---|---|---|---|
| type_id | `A927` (= `BLOCKFIRE`) | 28 | retail_alias | `constants.py:2831`, enum `:2801`. `A927` appears once in `gameScene.pyd` |
| lifetime | `BLOCKFIRE_MAX_LIFESPAN` / `A1764` | **4.0** (float) | retail_named | `:6615` / `:4018`. `A1764` **is** referenced inside `gameScene.pyd` |
| player_damage_per_tick | `BLOCKFIRE_CHARACTER_DAMAGE` / `A1758` | 2.5 | retail_named | `:6609` / `:4012` |
| player_damage_tick | `BLOCKFIRE_CHARACTER_DAMAGE_TIMER` / `A1759` | 0.3 | retail_named | `:6610` / `:4013`. **TIMER 1 of 3.** **[UNBOUND in client]** — player damage is server-side |
| player_ignite_range | `BLOCKFIRE_CHARACTER_SPREAD_RANGE` / `A1760` | 3 | retail_named | `:6611` / `:4014`. **[UNBOUND]** — the Euclidean-3D reading comes only from `BattleSpades/server/fire.py:316-317`; retail could use a Chebyshev/voxel range |
| player_burn_duration | `BLOCKFIRE_CHARACTER_DURATION` / `A1761` | 10 | retail_named | `:6612` / `:4015`. Deliberately much longer than the 4.0 s fire lifespan |
| block_damage | `BLOCKFIRE_BLOCK_DAMAGE` / `A1762` | 0.7 | retail_named | `:6613` / `:4016`. Against `DEFAULT_BLOCK_HEALTH = 5` a block survives ~7 ticks ≈ 2.9 s |
| block_damage_tick | `BLOCKFIRE_BLOCK_DAMAGE_TIMER` / `A1763` | 0.4 | retail_named | `:6614` / `:4017`. **TIMER 2 of 3** — deliberately different from the 0.3 player tick |
| spread_count | `BLOCKFIRE_SPREAD_COUNT` / `A1765` | 5 | retail_named | `:6616` / `:4019`. **[UNBOUND]** — BattleSpades treats it as one **shared per-impact budget** (`fire.py:32-47`) and separately as the initial cluster cap (`:119`); retail's own use is not recoverable |
| spread_tick | `BLOCKFIRE_SPREAD_TIMER` / `A1766` | 0.5 | retail_named | `:6617` / `:4020`. **TIMER 3 of 3** |
| spread_radius | `BLOCKFIRE_SPREAD_RADIUS` / `A1767` | **2.0** (float) | retail_named | `:6618` / `:4021`. `A1767` **is** referenced inside `gameScene.pyd`. **Retail deliberately writes this as a float while writing `INITIAL_SPREAD_RADIUS` as an int — do not flatten** |
| spread_chance | `BLOCKFIRE_MAX_RANDOM_CHANCE` / `A1768` | 0.3 | retail_named | `:6619` / `:4022`. **[UNBOUND]** — "probability roll per spread attempt" rests on `BattleSpades/server/fire.py:365`; the retail name says *MAX* random chance, which could equally be a ceiling |
| blocks_to_attempt_to_light | `BLOCKFIRE_BLOCKS_TO_ATTEMPT_TO_LIGHT` / `A1769` | -1 | retail_named | `:6620` / `:4023`. `-1` is the retail sentinel (same idiom as the smoke decay). "Unlimited" is a guess. **BattleSpades never reads this** |
| max_falling_distance | `BLOCKFIRE_MAX_FALLING_DISTANCE` / `A1770` | **1.0** (float) | retail_named | `:6621` / `:4024`. **BattleSpades never reads this** |
| initial_spread_radius | `BLOCKFIRE_INITIAL_SPREAD_RADIUS` / `A1771` | **2** (int) | retail_named | `:6622` / `:4025`. `A1771` **is** referenced inside `gameScene.pyd`. Radius of the initial cluster lit by a Molotov impact |
| colour_ramp_hot | `BLOCKFIRE_HOT_COLOUR` / `A1772` | `(255,255,255)` | retail_named | `:6623` / `:4026-4027`. `A1772` **is** in `gameScene.pyd` — consumed by `BlockFireEntity.calculate_colour` (`strtab:450`), driven by `set_fuse` (`:449`), i.e. the ramp is a function of remaining fuse over 4.0 s |
| colour_ramp_mid | `BLOCKFIRE_MID_COLOUR` / `A1773` | `(255,255,0)` | retail_named | `:6625` / `:4028`. In `gameScene.pyd` |
| colour_ramp_cold | `BLOCKFIRE_COLD_COLOUR` / `A1774` | `(255,0,0)` | retail_named | `:6626` / `:4029`. In `gameScene.pyd`. Ramp is white → yellow → red as the fire ages |
| light_radius | `BLOCKFIRE_LIGHT_RADIUS` / `A2262` | **3.0** (float) | retail_alias | `:4987`; named `:4986`; dump `backup/constants-copy.py:3351`. `A2262` **is** referenced inside `gameScene.pyd` — each block fire is a real dynamic point light |
| smoke_rate_min | `BLOCKFIRE_SMOKE_GENERATION_MIN_RATE` / `A2129` | **1.0** (float) | retail_alias | `:4669`; named `:4661`. **All eight smoke aliases `A2129`-`A2136` (`:4669-4676`) are referenced inside `gameScene.pyd`**, consumed by `update_smoke`/`create_fire_smoke` (`strtab:451-452`) |
| smoke_rate_max | `..._MAX_RATE` / `A2130` | 2.0 | retail_alias | `:4670` / named `:4662` |
| smoke_particle_decay | `..._PARTICLE_DECAY` / `A2131` | -1 | retail_alias | `:4671` / `:4663` |
| smoke_particle_lifespan | `..._PARTICLE_LIFESPAN` / `A2132` | 3 | retail_alias | `:4672` / `:4664` |
| smoke_particle_min_size | `..._PARTICLE_MIN_SIZE` / `A2133` | 4 | retail_alias | `:4673` / `:4665` |
| smoke_particle_max_size | `..._PARTICLE_MAX_SIZE` / `A2134` | 8 | retail_alias | `:4674` / `:4666` |
| smoke_min_velocity | `..._MIN_VELOCITY` / `A2135` | 0.0 | retail_alias | `:4675` / `:4667` |
| smoke_max_velocity | `..._MAX_VELOCITY` / `A2136` | 0.1 | retail_alias | `:4676` / `:4668` |
| damage_type | `A399` (= `BLOCKFIRE_DAMAGE`) | 25 | retail_alias | `constants.py:981`, enum `:954`. Client dispatch `BlockManager.handle_blockfire_damage` |
| kill_type | `A446` (= `BLOCKFIRE_KILL`) | 25 | retail_alias | `constants.py:1173`, enum `:1146`. **NOT in `ONE_HIT_KILL_WEAPONS`**, consistent with damage-over-time. **Coincidentally equal to the damage type — different enums, do not conflate** |
| model | — | null | absent | particle-only by design |
| health | — | null | absent | expires on lifespan or when its host voxel is destroyed |
| face (wire) | `FACE_TOP` = 4 | 4 | **battlespades** | `FACE_TOP = 4` itself is retail (`constants.py:146`, alias `A23 :151`), but **no retail source says a BLOCKFIRE entity is spawned with face 4** — that choice is `BattleSpades/server/fire.py:209`. It is nevertheless **load-bearing**: `Entity.set_face` rotates for faces 0,1,2,3,5 (`entity.py:91-102`) and `Entity.rotate` iterates `self.model` (`:200-207`), so any face other than 4 on a model-less entity tears down the retail `GameScene` |
| team_only | — | false | battlespades | `fire.py:313-318` ignites every alive player in range with no team check; the entity still carries the owner's team as wire state (`:200`) |

---

## 29 — UGC_ENTITY

Class `UGCEntity` compiled into `gameScene.pyd` from `ugcEntity.py` (display string `strtab:272`,
build path `:273`). **Ten** overridden methods, at `strtab:274, 276, 277, 278, 280, 281, 282,
283, 285, 286` — `set_packet`, `set_position`, `on_delete`, `create_zone`, `adjust_zone`,
`delete_zone`, `get_bounds`, `update`, `draw_on_minimap`, `get_ugc_mode`. **The range is not
contiguous**: `:275` `handle_single_block_damage`, `:279` `handle_block_distance_damage`,
`:284` `handle_radius_damage` are unrelated free functions.

**This is the one entity in the marker group with real behaviour** — a live zone volume, team
ownership, minimap presence and packet-driven state.

| field | constant | value | provenance | source |
|---|---|---|---|---|
| type_id | `UGC_ENTITY` / `A928` | 29 | retail_named | `constants.py:2801`, alias `:2832` |
| model_kv6_baseplate | `UGC_ENTITY_BASEPLATE_MODEL` | `ugc_baseplate`, load offset `(0.0,0.0,1.0)` | retail_literal | `models.py:394`. **Model index 0 of every one of the 19 `UGC_ENTITY_MODELS` entries.** Asset present (sha256 `29003329…`, 3018 bytes) |
| model_kv6_spawn_zone | `UGC_SPAWN_ZONE_MODEL` | `ugc_spawn_zone`, load offset `(0.0,0.0,1.0)` | retail_literal | `models.py:395`. Index-1 model for all six spawn-zone items. Asset present (sha256 `5eddb855…`, 9636 bytes) |
| model_kv6_base_zone | `UGC_BASE_ZONE_MODEL` | `ugc_base_zone`, load offset `(0.0,0.0,1.0)` | retail_literal | `models.py:396`. Index-1 model for all nine base-zone items. **Byte-identical to `cp.kv6` and `pickup.kv6`** — sha256 `c5c458eba26f5a4dda19032c6ae864045defd9c757d2157c8924c69fd10996a9`, 2638 bytes, per `BattleSpadesClient/assets/catalog/original-assets.json:913-915, 1068-1070, 1748-1750` (that file stores **sha256, not md5**) |
| model_z_offset | — | **not an entity-wide value** | retail_literal | The `+1.0` above is a **per-mesh `load_model` argument** applying only to the three UGC meshes. The index-1 **item** meshes have different offsets: `healthcrate` (`:343`), `ammocrate` (`:344`), `block_crate` (`:345`) are loaded with **no offset at all**, and `BOMB_ENTITY_MODEL` carries **`(0.0,0.0,7.0)`** (`:289`). **Do not apply a single +1 to the whole entity** |
| model_z_offset_item_overlay | — | **-0.5** | retail_literal | Third element of every one of the 19 `UGC_ENTITY_MODELS` rows (`models.py:448-504`), applied to model index 1 only at `ugcTool.py:199`. **This is a DisplayList world-space z; the +1.0 above is in KV6 model units. Do not add them** |
| model_size | — | 0.06 **(ghost only)** | retail_literal | `ugcTool.py:196` sets both models to 0.06, then `:198` multiplies index 1 by the per-item scale. **This is `UGCTool.create_ugc_entity_display` (`:191-200`), assigned to `self.ghost_display` (`:189`) and drawn only in `draw_ghosting` (`:274-275`).** The **entity's** size is unrecoverable: `gameScene.pyd` has **no `UGCEntity.create_display` and no `UGCEntity.draw`**, so `UGCEntity` inherits `Entity.create_display` (`entity.py:83-89`), which sets `display[i].size = self.size` **uniformly with no per-model scale and no z**, default 1.0. Treat 0.06 as ghost-path only |
| ugc_pair_table (drop points) | `UGC_ENTITY_MODELS` | HEALTH(0)→`[baseplate, healthcrate]` 0.25/−0.5; AMMO(1)→`[baseplate, ammocrate]` 0.25/−0.5; BLOCK(2)→`[baseplate, block_crate]` 0.25/−0.5; OCC_BOMB(3)→`[baseplate, Bomb]` **1.0**/−0.5 | retail_literal | `models.py:448-459` |
| ugc_pair_table (spawn zones) | `UGC_ENTITY_MODELS` | GREEN/BLUE SMALL(4,7) 0.25; MEDIUM(5,8) 1.0; LARGE(6,9) 1.5 — all `[baseplate, ugc_spawn_zone]`, z −0.5 | retail_literal | `models.py:460-465, 475-480, 490-495`. Green and blue rows of the same size are byte-identical; colour comes from team tinting |
| ugc_pair_table (base zones) | `UGC_ENTITY_MODELS` | GREEN/BLUE/NEUTRAL SMALL(10,13,16) **0.5**; MEDIUM(11,14,17) 1.0; LARGE(12,15,18) **1.75** — all `[baseplate, ugc_base_zone]`, z −0.5 | retail_literal | `models.py:466-474, 481-489, 496-504`. **Base zones are scaled larger than spawn zones at small and large but identical at medium. This asymmetry is real — verified line by line. Do not regularise it** |
| zone_size_table | `UGC_ZONE_SIZES` / `A2009` | SMALL `(-5,5,-5,5,-8,2)`; MEDIUM `(-12,12,-12,12,-21,3)`; LARGE `(-20,20,-20,20,-36,4)` — order `(x_min,x_max,y_min,y_max,z_min,z_max)` in blocks | retail_named | `constants.py:4291-4307`, alias `:4308`, raw dump `backup/constants-copy.py:3002-3021`. **15 entries — 6 spawn zones AND 9 base zones**; spawn and base of the same size class share identical extents (the visual difference is model scale only). The "WTF" fixup loop at `:4323-4331` (`A2024 = A2018 + ((A2019-A2018) & ~1)`, `-` binds tighter than `&`) is a **no-op**: spans are 10/10/10, 24/24/24, 40/40/40, already even. Z is asymmetric — much deeper below than above |
| zone_team_owner | `UGC_ENTITY_TEAMS` / `A2027` | `GREEN_* → TEAM2 (3)`, `BLUE_* → TEAM1 (2)`, `NEUTRAL_BASE_* → TEAM_NEUTRAL (1)`; 15 entries, zone items only | retail_named | `constants.py:4333-4349`, alias `:4350`, raw dump `backup/constants-copy.py:3044-3058`. Team enum `constants.py:213` (`TEAM_SPECTATOR/TEAM_NEUTRAL/TEAM1/TEAM2 = xrange(4)`), confirmed against the obfuscated `backup:92-96`. **The GREEN→TEAM2 / BLUE→TEAM1 mapping is counter-intuitive but correct.** The four drop/bomb-point items are **absent** from this dict, i.e. team-agnostic; our server reads it with a `TEAM_NEUTRAL` default (`BattleSpades/server/map_metadata.py:651`). **This is a 3-valued owner mapping, not a boolean team gate.** Companion `UGC_ZONE_TYPES` (`:4360-4376`, alias `A2033 :4377`) maps the same 15 items to `UGC_ZONE_TYPE_SPAWN1/SPAWN2/BASE1/BASE2/BASE` (0..4, `:4352`) |
| base_zone_tint_alpha | `BASE_ZONE_TINT_ALPHA` / `A2286` | 150 (0-255, ≈0.588) | retail_alias | `constants.py:5051`, alias `:5056`, raw dump `backup/constants-copy.py:3375`. **No Python consumer.** Three binaries reference it: `aoslib/hud/hud.pyd`, `aoslib/hud/hud_old.pyd`, `aoslib/hud.hud.pyd` — **including the current `hud.pyd`, so it is live, not legacy**. Which surfaces it tints is not recoverable |
| placement_max_distance | `A2006` | 10.0 | retail_alias | `constants.py:4261` — **alias-only, there is no named counterpart anywhere**. Sits in the unnamed tail `A1998`-`A2006` before `#END Weapon Constants` (`:4262`); confirmed at `backup:2981`. Consumed as the second **positional** arg of `can_place_object` at `ugcTool.py:262`; the parameter name is `far_radius`, proven by the keyword form at `radarStationWeapon.py:46` and `rocketTurretWeapon.py:58` |
| placement_entity_min_radius | — | 1 | retail_literal | `ugcTool.py:262` — `player_min_radius=0, entity_min_radius=1, others_min_radius=0, can_place_vertical=False` |
| ghost_blend_alpha | — | 0.3 | retail_literal | `ugcTool.py:273` `MODEL_SHADER` blend `(1.0,1.0,1.0,0.3)`, reset to 1.0 at `:277`; `frustum_check=False` at `:275` |
| touch_radius | — | null | absent | No touch semantics. The nearest recoverable radii are placement parameters; `ugcTool.py:257` probes for an existing `UGC_ENTITY` with radius 1.0 via `is_object_on_entity_of_class` |
| health | — | null | absent | UGC entities are removed by re-placing/cycling with the tool (`ugcTool.py:216, 239-247`), not by damage |

Right-click cycles within a `UGC_RIGHT_CLICK_GROUPS` (`constants.py:1348-1355`) by deleting the
current item and placing the next (`ugcTool.py:239-247`).

**Id-space collision warning.** `UGC_ITEM_*` ids (0..18, `constants.py:1318`) are a completely
different space from ENTITY ids (0..39, `:2801`). `UGC_ITEM_HEALTH_DROP_POINT = 0` is **not**
`FLAG = 0`; `UGC_ITEM_AMMO_DROP_POINT = 1` is **not** `BASE = 1`. Both spaces are already
emitted into `include/battlespades/shared/retail_constants.hpp` (entities `:63, :131, :837,
:2048`; UGC items `:2056, :2057, :2070, :2074`) and agree with retail.

---

## 30 — MEDPACK_ENTITY (retail: `UNKNOWN_ENTITY1`)

Tool `aoslib/weapons/medPackWeapon.py:21`. Entity class `MedPackEntity` compiled from
`aoslib/scenes/main/medPackEntity.py`, only inside `gameScene.pyd` (`strtab:463-470`).
**It overrides only `initialize`/`create_display`/`draw`/`on_delete`/`draw_on_minimap` — no
`update()`, no `set_fuse()`** (contrast `RadarStationEntity`, which has both at `:477, :480`).
`Entity.set_fuse` is a no-op (`entity.py:209`), so a placed medpack never ticks and has no
timed expiry client-side.

**Retail has no named `MEDPACK_*` constant at all** (`grep '^MEDPACK'` over `shared/constants.py`
returns nothing). Everything below comes from the alias cluster `A1865, A1868`-`A1880`
(`constants.py:4124-4137`).

| field | constant | value | provenance | source |
|---|---|---|---|---|
| type_id | `UNKNOWN_ENTITY1` / `A929` | 30 | retail_named | `constants.py:2801`, alias `:2833`. The name `MEDPACK_ENTITY` is **ours** (`BattleSpades/shared/constants.py`) |
| ammo (carried charges) | `A1868` / `A1869` / `A1870` | max 2, initial 2, restock 1 (max_clip `None`, initial_stock `None`) | retail_alias | `:4124-4126`; **directly bound** at `medPackWeapon.py:35` as `(A1868, A1869, None, None, A1870)`; tuple order per `weapon.py:157`. `clip_reload = False` (`:27`) |
| shoot_interval | `A1865` | **1.0** | retail_alias | **`A1865` is assigned TWICE — `0.2` at `:4121` and `1.0` at `:4127`.** Python executes top-down, so the surviving binding at import time is **1.0**, and that is what the shipped client runs; the 0.2 is dead (obfuscator alias collision). Bound at `medPackWeapon.py:28`, also feeds `AnimPlaceBlock` (`:46`). **Port 1.0** |
| reload_time | `A1860` | 1.5 | retail_alias | `:4116`; **directly bound** `medPackWeapon.py:26` |
| **heal_amount** | `A1871` | 25 | retail_alias **(meaning INFERRED)** | `:4128`. **The value 25 is genuine retail — it is NOT a BattleSpades invention.** But **no retail `.py` binds `A1871`, and the string `"A1871"` does not appear in `gameScene.pyd`** (only `A1873` and `A1875` from this cluster do). The label rests on positional analogy, and **that analogy is weaker than previously claimed**: in the landmine (`A1806/A1807/A1808`) and C4 (`A1754/A1755/A1756`) clusters the far_radius sits **immediately before** model_size, whereas here the alias immediately before model_size is `A1872 = 3` and the directly-bound far_radius `A1880 = 5.0` sits **five slots after** the model tail. Per-tool parameter counts also differ per cluster (landmine 10, C4 5, radar 4, medpack claimed 2). **Value retail, meaning inferred** |
| **uses** | `A1872` | 3 | retail_alias **(meaning INFERRED)** | `:4129`. Same caveat as `heal_amount` — `"A1872"` is absent from `gameScene.pyd`. Independently, `BattleSpades/shared/constants.py:7312` states `MEDPACK_USES = 3`. This is the number of heals the **placed pack** grants, distinct from the medic's 2 carried charges |
| model_size | `A1873` | 0.06 | retail_alias | `:4130`. **Corroborated three ways**: the shipped `gameScene.pyd` looks the name up at runtime (`strtab:1456`); the ghost uses the same literal (`ghost_medpack.size = 0.06`, `medPackWeapon.py:19`); and it occupies the `MODEL_SIZE` slot of the landmine/C4 tail pattern. **Separate from the held-tool literals** `model_size = 0.04` (`:31`) and `view_model_size = 0.07` (`:32`) |
| health | `A1874` | 1 | retail_alias **[INFERRED SLOT]** | `:4131`. `HEALTH` slot between `MODEL_SIZE` `A1873` and `MODEL_Z_OFFSET` `A1875`, matching landmine `A1808 = 1` and C4 `A1756 = 1`. **The tail pattern is now PROVEN by retail's named block** — `LANDMINE_MODEL_SIZE = 0.05` (`:6642`), `LANDMINE_HEALTH = 1` (`:6643`), `LANDMINE_MODEL_Z_OFFSET = 0.0` (`:6644`) are three consecutive named constants matching `A1807/A1808/A1809`. **[UNBOUND]** — "one point of damage destroys a placed pack" is a consequence, not a read fact |
| model_z_offset | `A1875` | -0.5 | retail_alias | `:4132`. **Directly bound**: `glTranslatef(0, -A1875, 0)` at `medPackWeapon.py:77`. Also looked up by name from `gameScene.pyd` (`strtab:1457`), so the placed entity applies the same offset. **Note the GL translate negates it** |
| placement_radius | `A1880` | 5.0 | retail_alias | `:4137`. **Directly bound** at `medPackWeapon.py:65` as the `far_radius` positional, with retail-literal constraints `player_min_radius=0, entity_min_radius=1, others_min_radius=0, can_place_vertical=False` |
| model | `MEDPACK_MODEL` / `MEDPACK_VIEW_MODEL` | `MedPack`; world offset `(8, -21, 0.0)`, view offset `(0.0, 0.0, -3.0)` | retail_literal | `models.py:407-408`. Same kv6, different offsets. The **ghost uses `MEDPACK_VIEW_MODEL`** (`medPackWeapon.py:18`) — the catalog does not state which the placed entity binds. `kv6/MedPack.kv6` present with matching case |
| damage_type | `TOOLS_DAMAGE_TYPE[MEDPACK_TOOL]` / `A418` | **null** (Python `None`) | retail_named | dict header `constants.py:1003`, entry `:1056`, alias `:1070`. `TOOLS_SECONDARY_DAMAGE_TYPE` (header `:1073`, alias `A419 :1140`) entry `:1126` is **also `None`** — **that is the secondary table, not a duplicate**. The medpack deals no damage of any kind; this is a found value, not a gap |
| kill_type | — | null | absent | **`MEDPACK_TOOL` has NO entry at all** in the tool→kill-type map (`:1187-1252`); `RIOTSHIELD_TOOL` and `RADAR_STATION_TOOL` both appear, `MEDPACK_TOOL` does not |
| minimap_marker | — | `marker_medpack_16` | retail_literal | `png/ui/marker_medpack_16.png` present |
| lifetime | — | **null** | **absent** | `MedPackEntity` has no `update()` and no `set_fuse()`, so no client-side timed expiry. **Do not state "despawns on exhausting its uses" as retail** — that is BattleSpades policy (`behaviors.py:268-270`). **Undisclosed retail candidate: `A1879 = 300` (`:4136`)**, an unassigned alias in the medpack cluster; the radar station puts its lifetime at the analogous mid-cluster position (`A1900 = 250`) |
| fuse | — | null | absent | no `set_fuse` override; nothing in the cluster is a fuse |
| touch_radius | — | **null** | **absent** | No retail constant is bound to a medpack pickup/heal radius, and `MedPackEntity` has no `update()`, so the trigger is server-side and unreadable. BattleSpades uses 3.0 (`behaviors.py:250`) — **calibration, not data**. Unassigned retail candidates in the cluster: `A1876 = 3`, `A1877 = -0.5`, `A1878 = 5`, `A1879 = 300` (`:4133-4136`); the pair `(3, -0.5)` mirrors the retail-**named** `LANDMINE_DETECTION_LAYERS = 3` / `LANDMINE_EXPLOSION_AND_DETECTION_VERTICAL_OFFSET = -0.5` (`:6635-6636`), hinting at a detection sub-block — but nothing pins which is the radius |
| water_legal | — | null | absent | `draw_ghosting` (`medPackWeapon.py:63-84`) has **no** waterplane gate, unlike `landmineWeapon.py:59`. The medpack cluster contains no boolean at all |
| team_only | — | true | battlespades | `behaviors.py:262-264`. Retail evidence is circumstantial only: the entity carries a team and `Entity.draw` tints by team (`entity.py:170-182`) |
| respawn_delay / light_radius | — | null | absent | restock is via the ammo tuple's `restock_amount = 1` |

Placement sends `GameScene.send_place_medpack(position, face)` (`medPackWeapon.py:56`), wire
packet 90 `PlaceMedPack` (`reversal/packet_spec.md:578`). Tool description
`u'+ Heals nearby players \n- Low ammo'` (`aoslib/strings/english.py:2009`) confirms a proximity
heal exists but pins no number.

---

## 31-35 — UNKNOWN_ENTITY2 … UNKNOWN_ENTITY6

| id | constant | alias | wire_safe | status |
|---|---|---|---|---|
| 31 | `UNKNOWN_ENTITY2` | `A930` (`constants.py:2834`) | true | not recovered |
| 32 | `UNKNOWN_ENTITY3` | `A931` (`:2835`) | true | not recovered |
| 33 | `UNKNOWN_ENTITY4` | `A932` (`:2836`) | true | not recovered |
| 34 | `UNKNOWN_ENTITY5` | `A933` (`:2837`) | true | not recovered |
| 35 | `UNKNOWN_ENTITY6` | `A934` (`:2838`) | true | not recovered |

All five are unnamed in retail and were not investigated in this pass. Their aliases **are**
present in `gameScene.pyd`, so each maps to some registered client class.

---

## 36 — RADAR_STATION_ENTITY (retail: `UNKNOWN_ENTITY7`)

Tool `aoslib/weapons/radarStationWeapon.py:16`. Entity `RadarStationEntity` compiled from
`aoslib/scenes/main/radarStationEntity.py`, only inside `gameScene.pyd` (`strtab:473-483`).
Unlike the medpack it is an **active** entity: `__init__`, `initialize`, `update`,
`set_position`, `set_packet`, `set_fuse`, `on_delete`, `draw_on_minimap`, `can_detect_player`.

**Retail has no named `RADAR_STATION_*` constant** (`grep '^RADAR'` returns nothing). Cluster is
`A1893`-`A1902` (`constants.py:4150-4159`), with four direct anchors.

| field | constant | value | provenance | source |
|---|---|---|---|---|
| type_id | `UNKNOWN_ENTITY7` / `A935` | 36 | retail_named | `constants.py:2801`, alias `:2839`. The `RADAR_STATION_ENTITY = 36` identification is **ours** (`BattleSpades/shared/constants.py:3405`) |
| ammo | `A1893` / `A1894` / `A1895` | max 1, initial 1, restock 1 | retail_alias | `:4150-4152`; **directly bound** `radarStationWeapon.py:29`. `is_available()` returns `get_ammo()[0] > 0` (`:74-75`) |
| placement_radius | `A1896` | 10 | retail_alias | `:4153`; **directly bound** as `far_radius=A1896` at `radarStationWeapon.py:46`, with `player_min_radius=1` (stricter than the medpack's 0), `entity_min_radius=1`, `others_min_radius=0`. **`can_place_vertical` is not passed** — the default lives in the compiled `can_place_object` |
| shoot_interval | `A1897` | 1.5 | retail_alias | `:4154`; **directly bound** `radarStationWeapon.py:30`, drives `AnimPlaceBlock` (`:40`) |
| model_size | `A1898` | 0.03 | retail_alias | `:4155`. Corroborated: `gameScene.pyd` looks up `"A1898"` at runtime (`strtab:1458`) and the ghost uses the same literal (`radarStationWeapon.py:26`). Separate from held-tool literals `model_size = 0.0175` (`:23`) and `view_model_size = 0.03` (`:24`) |
| health | `A1899` | 45 | retail_alias **[INFERRED SLOT]** | `:4156`. `"A1899"` is **not** in `gameScene.pyd` |
| **lifetime** | `A1900` | **250** (unit unstated) | retail_alias **[INFERRED SLOT — CONTESTED]** | `:4157`. **`A1900` IS looked up by name by the shipped client** (`strtab:1459`) and `RadarStationEntity` overrides `set_fuse`/`update`, so it runs a visible countdown. **But the label "lifetime" comes only from `BattleSpades/shared/constants.py:7303`, and retail's own tool text contradicts a 250 s lifetime**: `RADAR_STATION_TOOL_DESCRIPTION = u'+ Shows enemies on the radar \n- Low health \n- Low lifetime'` (`aoslib/strings/english.py:2033`). See conflicts |
| **detection_range** | `A1901` | **45** | retail_alias **[INFERRED SLOT — CONTESTED]** | `:4158`. `"A1901"` is **not** in `gameScene.pyd`, even though `can_detect_player` is client-side (`strtab:483`) — so the range test is evaluated server-side or with an inlined literal. **The slot order of `A1900` vs `A1901` is unproven and they are NOT the same number** — swapping them changes a ported value 5.5× |
| model_z_offset | `A1902` | -0.55 | retail_alias | `:4159`; **directly bound** `radarStationWeapon.py:54` as `glTranslatef(position.x + 0.5, -(position.z + A1902), position.y + 0.5)`. Also looked up by name (`strtab:1460`). **Sign convention differs from the medpack's** — here it is ADDED to z inside a negation. Do not copy the medpack's handling |
| fuse (wire) | `A1900` (same value) | 250 | **battlespades** | Not an independent constant. `RadarStationEntity.set_fuse` exists (`strtab:480`), so the server must populate the wire fuse field — **if you send 0 the client keeps the model alive forever** (`BattleSpades/server/deployable_actions.py:288-291`). The number itself is imported from the contested lifetime slot |
| model | `RADAR_STATION_BASE_ENTITY_MODEL` / `_VIEW_MODEL` / `_TOOL_MODEL` | `radar_station`; entity **no offset**, view **no offset**, **tool offset `(20.0, -40.0, 28.0)`** | retail_literal | `models.py:419-421`; bound `radarStationWeapon.py:21-22, 25`. `TOOL_FILE_NAMES` entry `'radar_station'` (`constants.py:1752`). Assets `kv6/radar_station.kv6` and `png/ui/weapons/radar_station.png` present |
| place_sound | `A2923` | `AoS_soundfx_PLAYER_marksman_item_RADAR_place_001` | retail_alias | `shared/constants_audio.py:469`. Asset present |
| ping_sound | `A2924` | `AoS_soundfx_PLAYER_marksman_item_RADAR_ping_001` | retail_alias | `shared/constants_audio.py:470`. Asset present |
| minimap_marker | — | `marker_radar_station_16` | retail_literal | string at `gameScene.pyd.strtab.json:471`, adjacent to `RADAR_STATION_BASE_ENTITY_MODEL` and `RadarStationEntity.draw_on_minimap`. `png/ui/marker_radar_station_16.png` present |
| damage_type | `TOOLS_DAMAGE_TYPE[RADAR_STATION_TOOL]` / `A418` | **null** (`None`) | retail_named | header `constants.py:1003`, entry `:1060`; secondary table (header `:1073`, alias `A419 :1140`) entry `:1130`, also `None` |
| kill_type | `TOOLS_KILL_TYPE[RADAR_STATION_TOOL]` → `RADAR_STATION_KILL` / `A454` | 33 | retail_named | map entry `:1242`, enum `:1146`; derived from the sequential alias run (`A421 = WEAPON_KILL = 0` at `:1148`, `A454` at `:1181`, 454−421 = 33). Matches `BattleSpades/shared/constants.py:465`. **[UNBOUND]** — the map entry exists, but no readable retail code produces such a kill |
| team_only | — | true | battlespades | Per-team refcounted reveal, `BattleSpades/server/main.py:1039-1055` + `behaviors.py:474-505`. Retail supports team-scoped detection in principle (`can_detect_player` exists) but no constant or readable code states the policy |
| respawn_delay / light_radius | — | null | absent | restock is the ammo tuple's `restock_amount = 1` |

---

## 37 — PROJECTILE_MINE_ENTITY (retail: `UNKNOWN_ENTITY8`)

Weapon side `aoslib/weapons/mineLauncherWeapon.py:13-54`; the entity slice
`constants.py:3983-3998` is **unbound**.

**The NAME is retail-attested** even though the slot index is ours: `models.py:417-418` loads
`PROJECTILE_MINE_MODEL` / `PROJECTILE_MINE_VIEW_MODEL` from `kv6/projectilemine.kv6`, and the
`gameScene.pyd` string table contains `ProjectileMine`, `ProjectileMineEntity` and
`PROJECTILE_MINE_VIEW_MODEL`.

| field | constant | value | provenance | source |
|---|---|---|---|---|
| type_id | `UNKNOWN_ENTITY8` / `A936` | 37 | retail_named | `constants.py:2801`, alias `:2840`. `PROJECTILE_MINE_ENTITY = 37` is **ours** (`BattleSpades/shared/constants.py:3406`) |
| reload_time | `A1718` | 2.0 | retail_alias | `:3972`; **directly bound** `mineLauncherWeapon.py:22`, with `clip_reload = False` (`:23`) |
| shoot_interval | `A1723` | 0.35 | retail_alias | `:3977`; **directly bound** `mineLauncherWeapon.py:24` |
| ammo | `A1727` / `A1727` / `A1724` / `A1725` / `A1726` | max_ammo 1, initial 1, max_clip 5, initial_stock 3, restock 5 | retail_alias | `:3978-3981`; **directly bound** `mineLauncherWeapon.py:31-32` as `(A1727, A1727, A1724, A1725, A1726)`. `A1727` is reused for both — a one-in-the-tube launcher with a 5-round reserve. **Note `BattleSpades/shared/constants.py:7276-7279` inverts "clip" and "max" relative to `weapon.py`'s tuple order — read carefully before porting** |
| throw_speed | `A1728` | 75 | retail_alias | `:3982`; **directly bound** — `mineLauncherWeapon.py:53` sends `send_mine_projectile(player.position, fp3 * A1728)`. Identical to the grenade launcher's `A1705 = 75` (`:3959`) |
| blast_radius | `A1729` | 3.0 | retail_alias **[INFERRED SLOT]** | `:3983`. **`"A1729"` IS in `gameScene.pyd.strtab.json:1440`** — the client resolves it by name |
| blast_wave_radius | `A1730` | 6.0 | retail_alias **[INFERRED SLOT]** | `:3984` |
| arm_delay | `A1731` | 4 | retail_alias **[INFERRED SLOT]** | `:3985` |
| trip_radius_horizontal | `A1732` | 2.5 | retail_alias **[INFERRED SLOT]** | `:3986` |
| trip_radius_vertical | `A1733` | 3 | retail_alias **[INFERRED SLOT]** | `:3987`. A layer **count**, mirroring `LANDMINE_DETECTION_LAYERS` |
| trip_detection_vertical_offset | `A1734` | -0.5 | retail_alias **[INFERRED SLOT]** | `:3988` |
| blast_damage | `A1735` | 100 | retail_alias **[INFERRED SLOT]** | `:3989` |
| block_damage | `A1736` | 15 | retail_alias **[INFERRED SLOT]** | `:3990` |
| knockback_max | `A1737` | 0.75 | retail_alias **[INFERRED SLOT]** | `:3991`. Flat 0.75/0.75, no distance falloff |
| knockback_min | `A1738` | 0.75 | retail_alias **[INFERRED SLOT]** | `:3992` |
| placement_far_radius | `A1739` | 5.0 | retail_alias **[INFERRED SLOT]** | `:3993`. Probably vestigial for a launched mine |
| model_size | `A1740` | 0.05 | retail_alias **[INFERRED SLOT]** | `:3994`. **`"A1740"` IS in `gameScene.pyd.strtab.json:1441`** |
| health | `A1741` | 1 | retail_alias **[INFERRED SLOT]** | `:3995` |
| model_z_offset | `A1742` | 0.0 | retail_alias **[INFERRED SLOT]** | `:3996`. **Not** referenced by the client |
| water_legal | `A1743` | true | retail_alias **[INFERRED SLOT]** | `:3997`. **Caution: `LANDMINE_CAN_PLACE_ON_WATER` is the only `*_CAN_PLACE_ON_WATER` constant in retail** — this label rests entirely on the positional alignment, and no readable `.py` consumes it |
| **unlabelled trailing constant** | `A1744` | 1.0 | retail_alias **(role UNKNOWN)** | `:3998`. **`"A1744"` IS referenced by the shipped client** (`gameScene.pyd.strtab.json:1442`) — it is live data, not dead. The projectile-mine slice runs **one entry longer** than the landmine slice, and the client-side triple here is `A1729/A1740/A1744` where the landmine's is `A1796/A1807/A1809` (radius / model_size / model_z_offset) — i.e. **`A1744` sits where the landmine has its z-offset**, while `A1742` (the assumed z-offset slot) is *not* referenced. Candidate by analogy: a gravity multiplier (`ExplodeOnImpactEntity.gravity_multiplier = A1648 = 1.0`, `explodeOnImpactEntity.py:19`, also in `strtab:1432`). **Reported, not asserted.** 1.0 is the no-op value either way |
| model | `PROJECTILE_MINE_MODEL` / `_VIEW_MODEL` | `projectilemine`, load offset `(6, -9, 0.0)` | retail_literal | `models.py:417-418`. **Differs from the landmine's `(5, -13, 0.0)` (`:315`) — they are distinct models despite sharing the numeric block.** `kv6/projectilemine.kv6` present |
| damage_type | `MINE_LAUNCHER_DAMAGE` / `A414` | 40 | retail_named | enum `constants.py:954`, alias `:996`. **Distinct from `LANDMINE_DAMAGE` = 15** even though the deployed object is mechanically a mine |
| kill_type | `TOOLS_KILL_TYPE[MINE_LAUNCHER_TOOL]` → `MINE_KILL` / `A456` | 35 | retail_named | dict header **`constants.py:1187`** (the symbol is `TOOLS_KILL_TYPE`, **not** `TOOL_KILL_TYPES`), entry `:1244`, alias `:1252`; enum `:1146`, alias `:1183`. **Not** in `ONE_HIT_KILL_WEAPONS`, unlike `LANDMINE_KILL` |
| accuracy / recoil_up | `A1720` / `A1721` | 0.01 / -0.15 | retail_alias | `:3974-3975`; bound `mineLauncherWeapon.py:19-21` |
| crater_radius | — | 1 | battlespades | `BattleSpades/server/main.py:514` |
| team_only | — | true | battlespades | inherited from `ProximityMineBehavior` (`behaviors.py:394`) |
| lifetime / fuse / touch_radius / light_radius | — | null | absent | no expiry for either flight or deployed phase; proximity-triggered, not timed; impact only deploys it |

**Boundary evidence (why the slice is `A1729`-`A1744`).** The weapon block `A1718`-`A1728` has
**10 of 11** entries directly bound by `mineLauncherWeapon.py` (`:19-24, 31-32, 53`; only
`A1719 = 1.0` is unbound), and the C4 block `A1745`-`A1757` has six bound by `c4Weapon.py`
(`:25, 26, 65, 95`). Both boundaries are pinned by real bindings. Independently, the grenade
launcher's weapon block is an exact 23-offset parallel of `A1718`-`A1728`, and its projectile
block begins immediately after at `A1706`. Additionally `A1729`-`A1743` is value-for-value
identical, in order, to the 15-entry landmine block `A1796`-`A1810` (3.0, 6.0, 4, 2.5, 3, −0.5,
100, 15, 0.75, 0.75, 5.0, 0.05, 1, 0.0, True).

**The alias block is order-preserving but NOT 1:1 with the named block.** The named block has no
counterpart at all for the ~100-entry run `A1658`-`A1757` (classic/AP/sticky grenade, grenade
launcher, mine launcher, projectile mine, C4): named order jumps MOLOTOV (`:6592-6608` =
`A1641`-`A1657`) straight to BLOCKFIRE (`:6609` = `A1758`). So the labelling rests on the
**value identity with the landmine block**, not on a name mapping — and the named block does
diverge elsewhere in this region (`DYNAMITE_EXPLOSION_RADIUS`). Anchors verified against four
independently-checked runs: `A1620`-`A1626` → `:6571-6577` (**not** `:6570`, which is `A1619`),
`A1641`-`A1649` → `:6592-6600`, `A1790`-`A1791` → `:6625-6626`, `A1811`-`A1819` → `:6646-6654`.

---

## 38 — C4_ENTITY (retail: `UNKNOWN_ENTITY9`)

`aoslib/weapons/c4Weapon.py:14-102`. **Retail has no named `C4_*` block at all**
(`grep '^C4_'` returns nothing), so the alias slice `A1745`-`A1757` (`constants.py:3999-4011`)
is the sole retail source and no named-vs-alias conflict is possible.

**Only 6 of the 13 are actually bound**: `A1745/A1746/A1747` (`:25`), `A1748` (`:26`),
`A1754` (`:65`), `A1757` (`:95`). The other seven are **positional inference**, though tightly
constrained: `A1754` at offset +9 and `A1757` at +12 force exactly five entries between
shoot_interval and far_radius where dynamite has six, and the observed 5-tuple
`(8, 300.0, 7, 0.15, 0.1)` is a byte-for-byte match for dynamite's `A1632`-`A1636`, with the
dropped entry being the fuse.

| field | constant | value | provenance | source |
|---|---|---|---|---|
| type_id | `UNKNOWN_ENTITY9` / `A937` | 38 | retail_named | `constants.py:2801`, alias `:2841`. **Retail never names slot 38.** `C4_ENTITY = 38` is **ours** (`BattleSpades/shared/constants.py:3407`), made against the client's `GameScene.ENTITIES` wire table — and that file's own comment (`:3395-3398`) records that a previous C4/Medpack mix-up came from guessing it from class-registration order. **Verified-by-us, not retail-named** |
| ammo | `A1745` / `A1746` / `A1747` | max 2, initial 2, restock **1** | retail_alias | `:3999-4001`; **directly bound** `c4Weapon.py:25`. **The restock is 1, not 2** — an ammo crate gives back one charge at a time. `A1745` and `A1746` are separate literals (both 2), unlike dynamite's `A1628 = A1627` reference form |
| shoot_interval | `A1748` | **1.0** (float) | retail_alias | `:4002` reads `A1748 = 1.0`; **directly bound** `c4Weapon.py:26`, drives `AnimPlaceBlock` (`:42`). `BattleSpades/shared/constants.py:7285` correctly has 1.0 |
| blast_radius | `A1749` | 8 | retail_alias **[INFERRED SLOT]** | `:4003`. Equals dynamite's true radius 8 |
| blast_damage | `A1750` | **300.0** (float) | retail_alias **[INFERRED SLOT]** | `:4004` reads `= 300.0`. `BattleSpades/shared/constants.py:7287` agrees |
| block_damage | `A1751` | 7 | retail_alias **[INFERRED SLOT]** | `:4005` |
| knockback_max | `A1752` | 0.15 | retail_alias **[INFERRED SLOT]** | `:4006` |
| knockback_min | `A1753` | 0.1 | retail_alias **[INFERRED SLOT]** | `:4007` |
| placement_far_radius | `A1754` | **5.0** (float) | retail_alias | `:4008` reads `= 5.0`; **directly bound** `c4Weapon.py:65` with `can_place_vertical=True`. **This one matters most for typing** — it is passed straight into `scene.can_place_object` as a distance |
| model_size | `A1755` | 0.06 | retail_alias **[INFERRED SLOT]** | `:4009`. **C4 has THREE distinct sizes — do not conflate**: (a) 0.06 here for the placed world entity; (b) **0.04** third-person held-tool scale, bare literal `C4Weapon.model_size = 0.04` (`c4Weapon.py:31`) — C4Weapon is the only one of the three placeables that declares a class-level `model_size`; (c) 0.06 for the ghost `DisplayList` (`c4Weapon.py:15`) |
| health | `A1756` | 1 | retail_alias **[INFERRED SLOT]** | `:4010`. Shootable off a wall |
| model_z_offset | `A1757` | -0.2 | retail_alias | `:4011`; **directly bound** `c4Weapon.py:95` as `glTranslatef(0, -A1757, 0)`, applied along GL up **after** the per-face rotation. Same value as dynamite |
| model | `C4_MODEL` / `C4_VIEW_MODEL` | held `c4_detonator` offset `(8, -21, 2.0)`; placed/ghost `c4` | retail_literal | `models.py:424-425`. `kv6/c4.kv6` and `kv6/c4_detonator.kv6` both present |
| live_charge_cap | `A1745` | 2 | **retail number, battlespades mechanism** | `:3999`. **Retail expresses 2 as an AMMO STOCK, not a live-entity cap.** The cap in `BattleSpades/server/deployable_actions.py:202-208` walks `player._c4_entity_ids`, counts live ones and refuses placement at `>= C4_STOCK`. In retail the cap falls out of ammo economics instead: place 2, ammo hits 0, and `use_secondary` detonates **all** of them at once. **The two models diverge if a charge is shot off a wall or the player restocks with charges still live** — retail would let you exceed 2 |
| default_face | `C4Weapon.face` | **null (dead attribute)** | retail_literal | `c4Weapon.py:28` declares `face = 4` but **it is never read** — placement uses `self.ghost_face`, assigned from `can_place_object`'s return at `:71` and sent at `:52`; `grep 'self\.face'` over `aoslib/` returns **zero** hits. **There is no default**: if `can_place_object` does not return, `ghost_position` stays `None` and `shoot()` bails at `:47`. The 0..5 guard at `:47` validates `ghost_face`, not `face`. **Do not seed face=4 as a fallback — retail refuses the placement instead** |
| per_face_offset_table | — | `{0:(0.0,0.5,0.5), 1:(1.0,0.5,0.5), 2:(0.5,0.0,0.5), 3:(0.5,1.0,0.5), 4:(0.5,0.5,0.0), 5:(0.5,0.5,1.0)}` | retail_literal | `c4Weapon.py:72-89`, byte-for-byte identical to dynamite's. Applied as `glTranslatef(position.x + x, -position.z - z, position.y + y)` at `:93` — (x,y,z) in AoS map space with **z down**, added to the SUPPORTING voxel coordinate. Mirrored at `BattleSpades/server/entities/behaviors.py:22-29` (`_ATTACHMENT_FACE_OFFSETS`) for both hit centre and explosion origin |
| per_face_rotation_table | — | `{0:(90,0,0,1), 1:(-90,0,0,1), 2:(-90,1,0,0), 3:(90,1,0,0), 4:(0,0,0,0), 5:(180,1,0,0)}` | retail_literal | `c4Weapon.py:72-89`, `glRotatef` at `:94`. Faces 0/1 rotate ±90 about GL Z; 2/3/5 about GL X; face 4 is the identity no-op. **BattleSpades has NOT ported the rotations** — `behaviors.py` carries only the offsets, so placed C4 orientation is currently unimplemented on our side |
| damage_type | `C4_DAMAGE` / `A415` | 41 | retail_named | enum `constants.py:954` (token 42 of 44, 1-based), alias `:997` |
| kill_type | `C4_KILL` / `A457` | 36 | retail_named | enum `:1146` — the **last** entry, alias `:1184`. Matches `deployable_actions.py:214`. **Not** in `ONE_HIT_KILL_WEAPONS`, unlike `LANDMINE_KILL` and `DYNAMITE_KILL` |
| crater_radius | — | 2 | battlespades | `deployable_actions.py:213` |
| fuse | — | null | absent | **Correctly absent, and load-bearing**: the C4 slice has 13 entries where dynamite has 14, and the missing one is exactly the fuse. C4 is remote-detonated — `use_secondary()` calls `scene.send_detonate_c4()` with **no arguments** (`c4Weapon.py:56-61`), so there is no client-side per-charge selection. `has_secondary = True` (`:30`) |
| arm_delay / trip_radius_horizontal / lifetime / light_radius | — | null | absent | no arming phase, no proximity trigger, no expiry |
| water_legal | — | null | absent | **Cannot determine.** Like dynamite and unlike the landmine, the C4 block has no water flag and `draw_ghosting` has no `Z_ABOVE_WATERPLANE` guard. Any restriction lives in the compiled `scene.can_place_object` |

Miner equipment (`CLASS_MINER CLASS_EQUIPMENT`, `constants.py:1540`). Uniquely, C4 has **no
`is_available()` override** — unlike `landmineWeapon.py:72` and `dynamiteWeapon.py:97` it never
hides itself when out of ammo.

---

## 39 — RIOT_SHIELD_ENTITY (retail: `UNKNOWN_ENTITY10`)

Tool `aoslib/weapons/riotShieldTool.py:12` (subclasses `DiggingTool`). Entity
`RiotShieldEntity` compiled from `aoslib/scenes/main/riotShieldEntity.py`, only inside
`gameScene.pyd` (`strtab:541-552`).

**Direct answer: the entity has NO autonomous behaviour.** Its full override set is
`initialize`, `create_display`, `draw`, `set_target`, `update`, `get_position`, `set_position`,
`set_velocity`, `draw_display`, `set_packet`, `hit`. The presence of `set_target` (base
`Entity.set_target` is a no-op, `entity.py:215-216`) plus the position/velocity overrides is the
signature of a **follower** slaved to its wielder's transform — the same shape as
`AttachedStickyGrenadeEntity` (`strtab:509`). No `set_fuse`, no lifetime, no proximity trigger,
no `draw_on_minimap`, no tick-driven state; `update()` exists only to track the target.

| field | constant | value | provenance | source |
|---|---|---|---|---|
| type_id | `UNKNOWN_ENTITY10` / `A938` | 39 | retail_named | `constants.py:2801`, alias `:2842`. `RIOT_SHIELD_ENTITY = 39` is ours (`BattleSpades/shared/constants.py:3408`) |
| autonomous_behaviour | — | **false** | **absent** (inference) | Derived from the `gameScene.pyd` method list above, not read from any `.py` or constant. Provenance is `absent`, not `retail_literal` |
| shoot_interval | `A1881` | 1 (int) | retail_alias | `:4138`; **directly bound** `riotShieldTool.py:19`, drives `AnimUseRiotShield` (`:35`) |
| damage (melee) | `A1882` | 2 | retail_alias | `:4139`; **directly bound** `riotShieldTool.py:20`. `use_primary` calls `use_spade(False)` (`:37-40`) |
| damage_absorption_percent | `A1883` | 50 | retail_alias **[INFERRED SLOT — weak]** | `:4140`. `RiotShieldEntity.hit()` (`strtab:552`) is the absorption hook and this is the only percentage-shaped value in the cluster. Label from `BattleSpades/shared/constants.py:4761`, whose own comment (`:4756-4758`) concedes the ordering is asserted from the pyc. **Corroborated by our own live implementation** (see below) |
| knockback | `A1884` | 0.5 | retail_alias **[INFERRED SLOT — weak]** | `:4141`. Label from `BattleSpades/shared/constants.py:4762` |
| model_size | `A1885` | 0.06 | retail_alias **[INFERRED SLOT — weakest]** | `:4142`. Label from `BattleSpades/shared/constants.py:4763`, where it is **defined but never read**. **`"A1885"` is absent from the shipped client's name table** — unlike `A1873` (medpack), `A1898` (radar) and the landmine control `A1807`, which are all present. Either the scale is inlined as a C literal or `A1885` is not the model size. **Unresolved.** Held-tool sizes are separate retail literals: `model_size = 0.073` (`riotShieldTool.py:16`), `view_model_size = 0.18` (`:17`) |
| arm_pitch_min | `A1886` | -80 | retail_alias | `:4143`; **directly bound** — `get_arm_pitch_range()` returns `(A1886, A1887)` at `riotShieldTool.py:44`. Degrees. **Clamps the WIELDER's arm, not the entity** |
| arm_pitch_max | `A1887` | 0 | retail_alias | `:4144`; same call |
| model | `RIOTSHIELD_MODEL` | `riotshield`, load offset `(0.5, -11.5, -4.0)` | retail_literal | `models.py:409`. `kv6/riotshield.kv6` present. Tool view offsets are retail literals: `initial_position Vector3(0.45, -0.6, -0.2)` (`riotShieldTool.py:31`), `arms_position_offset Vector3(0.05, -0.03, 0.05)` (`:34`) |
| damage_type | `RIOTSHIELD_DAMAGE` / `A410` | 36 | retail_named | **directly bound** `riotShieldTool.py:21`; maps agree (`constants.py:1057`, secondary `:1127`); enum `:954`, alias `:992` (derived: `A374 = PICKAXE_DAMAGE = 0` at `:956`, 410−374 = 36). Also in `BLOCK_GRANTING_DAMAGES` (`:1143`) — shield bashes grant blocks back, consistent with subclassing `DiggingTool`. In `ALL_MELEE_WEAPONS` (`:946`) |
| kill_type | `TOOLS_KILL_TYPE[RIOTSHIELD_TOOL]` → `WEAPON_KILL` | 0 | retail_named | `:1239`, enum `:1146` — no bespoke kill type |
| health | — | null | absent | The shield cluster has **no HEALTH slot** — it does not follow the placeable `<MODEL_SIZE, HEALTH, MODEL_Z_OFFSET>` tail. Absorption is a percentage, not a durability |
| model_z_offset | — | null | absent | No z-offset slot. **Unassigned neighbours**: `A1888 = 0.8`, `A1889 = -0.1`, `A1890 = -0.6`, `A1891 = 1.9`, `A1892 = 1.4` (`:4145-4149`) sit between the shield and radar clusters, shaped like an attach offset plus body dimensions — **nothing binds them; not assigned** |
| lifetime / fuse / team_only | — | null | absent | held weapon, exists while the tool is held |

Medic secondary weapon (`CLASS_SECONDARY_WEAPONS`, `constants.py:1656`).

**BattleSpades DOES implement the shield** — on the combat path, keyed off `RIOTSHIELD_TOOL`
rather than entity 39: `server/combat_runtime.py:1816` `_apply_riot_shield_mitigation` (reads
`C.RIOTSHIELD_DAMAGE_ABSORPTION_PERCENT` at `:1849`) and `:1856` `_apply_riot_shield_knockback`
(reads `C.RIOTSHIELD_KNOCKBACK` at `:1867`), called from `:1153, :1155, :1174`. Covered by
passing tests: `tests/test_reversed_combat.py:703, :726, :751, :769` and
`tests/test_reversed_world_update.py:228`. What is **not** implemented is a type-39 entity —
nothing spawns it and `behaviors.py` has no `RiotShieldBehavior`.

---

# CONFLICTS AND INVENTIONS

Ordered by how much they cost us if we get them wrong.

## A. Retail disagrees with itself — alias vs named

### A1. Dynamite blast radius — **USE 8**

`A1632 = 8` (`constants.py:3886`) vs `DYNAMITE_EXPLOSION_RADIUS = 5` (`:6583`).

`dynamiteWeapon.py` binds neither directly, so this is resolved structurally: the 14-entry alias
slice `A1627`-`A1640` aligns 1:1 with the 14 named `DYNAMITE_*` constants, pinned by the four
constants the weapon **does** bind (`A1627`-`A1629` ammo `:25`, `A1630` interval `:26`, `A1637`
far radius `:58`, `A1640` z-offset `:88`). Decisive corroboration: `shared/backup/constants-copy.py`
— the untouched A-number-only dump with **no named block whatsoever** — also carries `A1632 = 8`
at `:2606`. The named block is a later hand reconstruction that got this wrong. Independent
support: the C4 tail `A1749`-`A1757` is the same shape and also carries radius 8, and there is no
named `C4_*` block to contradict it.

**Decision: 8.** BattleSpades currently ships 5 (`shared/constants.py:7204`) and
`deployable_actions.py:128` passes it straight into `blast_radius` — **our dynamite is
under-radius today.**

### A2. Dynamite ammo stock — **USE 1 / 1 / 1**

`A1627 = 1`, `A1628 = A1627`, `A1629 = A1627` (`:3881-3883`) vs `DYNAMITE_STOCK = 3` and
`DYNAMITE_RESTOCK_AMOUNT = 3` (`:6578, :6580`); only `DYNAMITE_INITIAL_STOCK = 1` (`:6579`)
matches. **No inference needed** — `dynamiteWeapon.py:25` binds `A1627/A1628/A1629` directly
into the ammo tuple. You carry exactly **one** dynamite.

**Decision: 1/1/1.** Note: `BattleSpades/shared/constants.py:7199` has `DYNAMITE_STOCK = 3` but
it is **dead data** — nothing reads it. `_reset_equipment_state` (`server/player.py:1618-1652`)
has no `DYNAMITE_TOOL` key at all, so server-side dynamite stock is simply **unimplemented**,
not "over-stocked". Fixing the constant alone changes nothing.

### A3. Bomb fuse and radius — triple definition

`BOMB_EXPLOSION_FUSE` is assigned three times: 10.0 (`:4530`), 7.0 (`:4532`), **10** (`:4534`).
`BOMB_EXPLOSION_RADIUS` likewise: 7 (`:4531`), 8.0 (`:4533`), **7** (`:4535`). Python executes
top-down so the **last** wins, and the alias block is assigned after all redefinitions so it
captures the same finals.

**Decision: fuse 10, radius 7.** Anyone reading the middle line would wrongly port 8.0.

### A4. Medpack `A1865` assigned twice — **USE 1.0**

`A1865 = 0.2` at `:4121`, then `A1865 = 1.0` at `:4127`. `medPackWeapon.py:28` binds `A1865` and
is the only consumer in the tree, so at import time it resolves to **1.0**. The 0.2 is a dead
obfuscator alias collision.

**Decision: 1.0.**

## B. Retail is silent — the number exists only in BattleSpades

### B1. Health-crate heal amount — **UNRESOLVED, DECIDE EXPLICITLY**

**No retail constant exists.** Candidates, both non-retail:

* Reference server (hand-written Python-3 rewrite, `aceofspades_source/server/aosmodes/__init__.py:103-107`):
  `if connection.hp < 100: connection.set_hp(connection.hp + 20); crate.destroy()` — i.e. **+20,
  and no consumption at full health.**
* BattleSpades (`server/map_resources.py:84`): heal to `MAX_HEALTH`, **unconditionally**
  (choice documented at `server/entities/behaviors.py:104-105`).

`HEALTHCRATE_HP = 20` is **not** the heal amount — it is a kill/damage-source type id
(`constants.py:1146`, between `SHRAPNEL_KILL` 19 and `SNOWBALL_KILL` 21). Do not use it as a
quantity. **Kiril's call. The +20 number is at least attested somewhere; "heal to full" is not.**

### B2. Airstrike cadence — **DO NOT INVENT**

Genuinely unrecoverable. The whole AIRSTRIKE block is seven constants; the MultiHill strike
scheduler lives in the retail game server, which is not in this tree.
**`DEM_TIME_TO_WAIT_FOR_AIRSTRIKE = 5.0` is a Demolition pre-strike delay and must NOT be
repurposed.** If we ship airstrikes, the cadence has to come from live observation.

### B3. Jetpack-crate everything — **fuel, sound, and whether to ship it at all**

`JETPACK_CRATE` appears in exactly three places in retail (`:2801`, `:2802`, `:2809`) and the
client **never registers id 6**. Fuel amount, sound and pickup rules are all BattleSpades
inventions (`server/player.py:1586-1596`, `map_resources.py:93-96`).
**Recommendation: treat as unused retail content.** If we do ship it, give it a chute or skip
`crate.py:29` for one-model crates, and never put id 6 on the wire to a retail client.

### B4. Block-crate grant — **RESOLVED IN RETAIL'S FAVOUR, no longer a conflict**

Previously flagged as unverified. It is not: `Character.restock` (`character.pyd sub_1001A470`,
character.py:367-372) sets `block_count = int(CLASS_BLOCKS[class.id][1] * block_wallet_multiplier)`
— refill the wallet to the **class max**, scaled by the wallet multiplier. BattleSpades'
`add_blocks(self._block_wallet_max())` (`server/player.py:1562-1585`) is the same formula.
**No decision needed — we already match retail.**

Also retired: the claim that a Restock packet of type 5 falls through to every tool's `else`
branch and resets their ammo. It does not — the block branch returns before the tool loop.

## C. Same number, incompatible meaning

### C1. `ROCKET_TURRET_TOLERANCE = 0.1`

Retail client uses it **only** as an angular-**rate** threshold, multiplied by 10, to drive the
aim sound (`rocketTurret.py:65` → 1.0 deg/s). BattleSpades uses the raw 0.1 as an angular-**error**
gate that must be satisfied before firing (`server/rocket_turret.py:173`).
**Decision: keep ours for fire permission (retail's server-side rule is unrecoverable), but do
NOT reuse it for the aim-sound gate — that one needs `* 10`.**

### C2. `LANDMINE_DETECTION_LAYERS = 3`

Retail names it a **layer count**; we consume it as ±3 blocks of vertical tolerance on the
player's feet (`behaviors.py:416`). Same number, invented interpretation. Same applies to the
projectile mine's `A1733`. **Low risk, but label it in code.**

### C3. C4 live-charge cap of 2

The **number** 2 is retail (`A1745`) but retail expresses it as an **ammo stock**, not a live
cap. Our `deployable_actions.py:202-208` counts live entities and refuses at >= 2. **The models
diverge whenever a charge is shot off a wall or the player restocks with charges live — retail
would let you exceed 2.** Decide whether we want retail's ammo-economics model or our stricter one.

### C4. Radar station `A1900 = 250` vs `A1901 = 45` — slot order unproven

We label `A1900` lifetime and `A1901` range. Evidence **for**: `A1900` is the only extra of the
cluster the client resolves by name, and `RadarStationEntity` runs a `set_fuse` countdown.
Evidence **against**: retail's own tool text says `- Low lifetime`
(`aoslib/strings/english.py:2033`), and 250 s is not low; our playtest team measured ~35 s from
the shipped server (`BattleSpades/server/config.py:209-213`). `can_detect_player` is client-side
and would need a range, yet `A1901` is *not* in the name table.
**Recommendation: keep the labels but ship the measured 35 s, and flag it.** Note our own
`config.py` already silently diverges 7× from `RadarStationBehavior`'s own 250.0 default
(`behaviors.py:484`) and from `BattleSpades/shared/constants.py:7303`.

## D. Pure inventions (retail has nothing)

| value | where | note |
|---|---|---|
| `crater_radius` for **every** placeable — landmine 1, dynamite 2, C4 2, projectile mine 1, grave 1, corpse 1, MG 1, turret 1 | `deployable_actions.py:126,161,213`, `main.py:514`, `behaviors.py:225`, `corpse_lifecycle.py:189`, `machine_gun.py:88`, `rocket_turret.py:111` | Retail derives the crater from the explosion radius inside compiled code. All of these are our tuning |
| `team_only = true` on mines | `behaviors.py:394` | No retail constant expresses friendly immunity |
| `uses = 1` on crates | `behaviors.py:197-206` | No retail use count. Note: for the **jetpack** crate our own code makes it respawning, not single-use |
| `respawn_delay = 15.0` on crates | `map_resources.py:80,85,90,95` | Retail's `CRATE_SPAWN_DELAY = 25` is a match-rule default, and even its **unit is unstated** |
| MG mounting (`MOUNT_RADIUS 3.0`, `MOUNT_BREAK_RADIUS 4.0`, `MOUNT_INPUT_GRACE 0.25`, `0xFF` unmounted id) | `machine_gun.py:13-15`, `handlers/deployables.py:181-205` | Retail routes mounting through the compiled `UseCommand` path |
| MG entity kill type `ENTITY_KILL = 11` | `machine_gun.py:87` | There is no `MG_KILL` in retail |
| `MG_SHOOT_RATE = 0.2` | `BattleSpades/shared/constants.py:580` | **Not a retail symbol.** Sits next to the typo'd `rockET_SPEED = 45.0` (`:577`). Contradicts both `MG_SHOOT_INTERVAL` 0.5 and `MG_DEPLOYED_SHOOT_INTERVAL` 0.1 |
| Turret target selection (nearest enemy, LOS-gated, sticky) and yaw/pitch convention | `rocket_turret.py:165-166, 207-229` | The retail client is a pure receiver of yaw/pitch, so the source proves nothing about the convention |
| Turret `pitch_clamp_upper = 90` | `rocket_turret.py:166` | Only the −30 lower bound has a retail counterpart |
| Turret `rocket_spawn_offset = 1.0` | `rocket_turret.py:237` | No retail muzzle offset recovered |
| Flare-block 6-neighbour support rule + per-tick self-destruction | `flare_block.py:9-13, 42-53, 70-77` | Retail **does** have an attachment concept (`BLOCK_PLACE_FAIL_NOT_ATTACHED`, `has_neighbors`) but not this rule |
| `MAX_ACTIVE_BLOCK_FIRES = 96`, shared spread budget | `fire.py:32-47, 64` | No retail basis |
| BLOCKFIRE spawn face = 4 | `fire.py:209` | `FACE_TOP` is retail; the binding is ours. **Still mandatory** — any other face crashes a retail client on a model-less entity |
| Medpack `touch_radius = 3.0`, heal refusal at >= 100 hp, use consumed regardless of amount healed | `behaviors.py:250, 265-266` | No retail source. `MedpackBehavior`'s `heal_amount` **default of 100** (`behaviors.py:255`) and its docstring claim that the heal amount "is not in the constant catalog" (`:245-247`) are both **wrong** — it is `A1871 = 25`, and the call site does pass 25 (`deployable_actions.py:91`). Fix the default and the comment |
| Radar station per-team refcounted reveal | `main.py:1039-1055`, `behaviors.py:474-505` | No retail policy statement |

## E. Things previously mislabelled — now corrected

Recorded so nobody re-derives the old error.

1. **`RULE_ENABLE_CORPSE_EXPLOSION` and `RULE_ENABLE_GRAVESTONES` ARE retail**
   (`constants_matchmaking.py:22-23, :400-401`, both default `"ON"`). Earlier notes called the
   first a BattleSpades invention.
2. **`ROCKET_SPEED = 75` and `ROCKET_GRAVITY_MULTIPLIER = 0.05` ARE retail**
   (`:6346/:3650`, `:6348/:3651`, both referenced in `rocket.py:29-30`). Earlier notes called
   the turret's use of them inventions.
3. **`A401 = MG_DAMAGE` IS retail** (`constants.py:983`) — not BattleSpades-only.
4. **`RADAR_STATION_LIFETIME` is NOT retail** — it exists only at
   `BattleSpades/shared/constants.py:7303`. It was used as the "control case" proving the
   landmine's missing lifetime was meaningful; that argument is void.
5. **`MEDPACK_MODEL_SIZE` / `RADAR_STATION_MODEL_SIZE` / `C4_MODEL_SIZE` are NOT retail** — the
   named `*_MODEL_SIZE` block has exactly 13 lines and stops at the landmine.
6. **BattleSpades DOES implement the riot shield** (absorption + knockback in
   `combat_runtime.py:1816, 1856`, with passing tests). Only the type-39 entity is missing.
7. **The blast-wave radius is not rare** — six retail entities have one.
8. **`LANDMINE_CAN_PLACE_ON_WATER` is the only water flag in retail** — the projectile mine has
   none; `A1743` is a positional guess.
9. **Constant table names**: retail has `TOOLS_DAMAGE_TYPE` (`:1003`, alias `A418`),
   `TOOLS_SECONDARY_DAMAGE_TYPE` (`:1073`, alias `A419` — a **separate secondary table**, not a
   duplicate) and `TOOLS_KILL_TYPE` (`:1187`, alias `A458`). `TOOL_DAMAGE_TYPES`,
   `TOOL_KILL_TYPES` and `TOOL_ICONS` **do not exist**.
10. **`ENTITY_PORT_RECOVERY.md` errors**: `:32` attributes `cp.kv6` to BASE (unconfirmed, and
    `models.py:339` binds `CP_MODEL` to the capture point only); `:317` files the 3-second
    `restock()` under "Capture point (25)" when it belongs to BASE (1) via `CommandPost`;
    `:49/:60` cite the UGC pair table as `models.py:448-456` when it runs `:448-504` (19 rows).

## F. Float vs int fidelity

Retail's int/float split is deliberate in places and must survive into C++ typing. Do **not**
flatten these to ints:

`DYNAMITE`: damage 300.0, interval 1.0, far radius 5.0 (but radius **8** and block damage **7**
are ints). `C4`: damage 300.0, interval 1.0, far radius 5.0. `AIRSTRIKE`: damage 400.0,
knockback 2.0/1.0 (but radius **6**, block damage **15**, shell speed **100**, gravity **100**
are ints). `BOMB`: knockback 3.0/2.0, touch radius 3.0 (but throw speed **10** is an int).
`BLOCKFIRE`: lifetime 4.0, spread radius **2.0** while initial spread radius is int **2**;
light radius 3.0; max falling distance 1.0; smoke min rate 1.0. `CORPSE`: jetpack fuse 1.0, VIP
damage 75.0. `TURRET`: death blast radius **3.0** while rocket blast radius is int **3**.

**Turret derived offsets — keep the multiply, do not write literals:**
`-17.0 * 0.06 == -1.02` exactly (the literal is bit-exact), but
`-11.0 * 0.06 == -0.6599999999999999`, which is **not** equal to `-0.66`.

---

# DOES NOT SHIP

Every asset referenced by a recovered binding was checked against
`G:/AoSRevival/aceofspades_source/`.

## Referenced assets that are missing on disk

**None.** Every `kv6`, `.ogg` and `.png` named by a recovered binding in this document exists,
with matching case. Verified present: `ammocrate`, `healthcrate`, `block_crate`,
`Crate_Parachute`, `Crate_Target`, `jetpack`, `grave`, `ClassicCorpse`, `airstrike_bomb`,
`intel`, `diamond`, `Bomb`, `cp`, `pickup`, `block`, `landmine`, `projectilemine`, `dynamite`,
`c4`, `c4_detonator`, `MedPack`, `radar_station`, `riotshield`, `Turret_base`, `Turret_ball`,
`Turret_gun`, `ugc_baseplate`, `ugc_spawn_zone`, `ugc_base_zone` (all `.kv6`); `crate`,
`crate_blocks`, `healthcrate`, `cratedrop_land`, `cratedrop_freefall`, `cratedrop_chuteopen`,
`turret_place`, `turret_aim_start`, `turret_aim_stop`, `turret_aiming_lp`, `turret_lockon`,
`turret_lockoff`, `turret_explode`, `turret_explode_water`, `turr_rocketshoot`,
`turr_rocket_projectile`, `turr_rocketexplode`, `turr_rocketexplode_water`, `semishoot`,
`smg_fire_loop`, `smg_fire_tail`, `smgreload`, `airstrike_siren_oneshot`, `airstrike_flyby`,
`airstrike_explode`, `airstrike_explode_water`, `AoS_soundfx_PLAYER_marksman_item_RADAR_place_001`,
`AoS_soundfx_PLAYER_marksman_item_RADAR_ping_001` (all `.ogg`); `mg.png`,
`marker_machinegun_16.png`, `marker_turret_16.png`, `marker_radar_station_16.png`,
`marker_medpack_16.png`, `glowblock.png`, `radar_station.png`, `minimap_intel.png`,
`minimap_ammocrate.png`, `minimap_healthcrate.png`, `minimap_blockcrate.png`,
`ugc_ammo_drop.png`, `ugc_health_drop.png`, `ugc_block_drop.png`.

## Symbols with no asset — retail ships no art for these

| entity | what is missing | evidence |
|---|---|---|
| **7 MACHINE_GUN** | **No 3D model of any kind.** No `mg.kv6`/`machinegun.kv6` exists, `models.py` loads no MG model, and `gameScene.pyd.strtab.json` — which lists ~20 `*_MODEL` names including `TURRET_BASE/BALL/GUN_ENTITY_MODEL` — contains **no MG model name at all**. The only MG art is 2D (`mg.png`, `marker_machinegun_16.png`) and the shared `muzzleflash_default` | `mgWeapon.py:38-41`, `constants.py:1938` |
| **1 BASE** | **No model binding.** `models.py` binds no model to BASE. In retail a base is authored VXL geometry plus a zone volume plus a minimap icon (`minimap_base.png` exists), not an entity model | grep over `models.py` |
| **28 BLOCKFIRE** | **No model by design** — particle + light only | `grep -i fire models.py` → 0 hits |
| **6 JETPACK_CRATE** | `jetpack.kv6` exists but the entity is never registered; and the crate carries no `Crate_Parachute` (one-element model list) | `PyList_New(1)` at `0x101C99F7` |

## Missing bindings (asset present, retail says nothing)

* **Capture point → `cp.kv6`**: both strings live in `gameScene.pyd` but nothing in readable
  source binds them. `models.py:339` only proves `CP_MODEL` loads `cp.kv6`.
* **Jetpack-crate pickup sound**: no id exists in the 61-entry sound tuple. We reuse
  `SND_CRATE = 13`.
* **MG water explosion**: no `mg_explode_water` string anywhere (the turret has one).
* **Ammo/block-crate pickup sounds**: `CRATE_SOUND_ID = 13` and `CRATE_BLOCKS_SOUND_ID = 15` are
  real, but nothing in retail binds them to those crates — the binding is ours.

## Duplicate meshes (share one instance)

* `Crate_Target.kv6` == `ugc_baseplate.kv6` (md5 `50cf45bf…`, 3018 bytes)
* `cp.kv6` == `pickup.kv6` == `ugc_base_zone.kv6` (sha256 `c5c458eb…`, 2638 bytes)

---

# Machine-readable spec

`type_id -> { field -> { constant, value, provenance } }`. `provenance` is one of
`retail_alias`, `retail_named`, `retail_literal`, `battlespades`, `absent`. A `null` value with
`absent` provenance means **not recoverable — do not fill in**. Fields carrying `"unbound": true`
have a real retail value but no recovered consumer; the label is inference. Fields carrying
`"inferred_slot": true` are positional guesses within an unnamed alias cluster.

```json
{
  "0": {
    "type_id": {"constant": "FLAG", "value": 0, "provenance": "retail_named"},
    "wire_safe": {"constant": null, "value": false, "provenance": "retail_literal"}
  },
  "1": {
    "type_id": {"constant": "BASE", "value": 1, "provenance": "retail_named"},
    "wire_safe": {"constant": null, "value": false, "provenance": "retail_literal"},
    "model": {"constant": null, "value": null, "provenance": "absent"},
    "model_size": {"constant": null, "value": null, "provenance": "absent"},
    "touch_radius": {"constant": null, "value": null, "provenance": "absent"},
    "restock_behaviour": {"constant": null, "value": "set_hp(100), 3s throttle", "provenance": "absent", "note": "reference-server rewrite only, not retail"},
    "team_only": {"constant": null, "value": null, "provenance": "absent"},
    "health": {"constant": null, "value": null, "provenance": "absent"},
    "blast_radius": {"constant": null, "value": null, "provenance": "absent"},
    "light_radius": {"constant": null, "value": null, "provenance": "absent"},
    "respawn_delay": {"constant": null, "value": null, "provenance": "absent"},
    "zone_tint_alpha": {"constant": "A2286", "value": 150, "provenance": "retail_alias", "note": "zone volume, not the entity"}
  },
  "2": {
    "type_id": {"constant": "HELICOPTER", "value": 2, "provenance": "retail_named"},
    "wire_safe": {"constant": null, "value": true, "provenance": "retail_literal"}
  },
  "3": {
    "type_id": {"constant": "AMMO_CRATE", "value": 3, "provenance": "retail_named"},
    "model": {"constant": "AMMO_MODEL,CRATE_PARACHUTE_MODEL", "value": ["ammocrate", "Crate_Parachute"], "provenance": "retail_literal"},
    "icon": {"constant": "AmmoCrate.icon", "value": null, "provenance": "retail_literal"},
    "model_size": {"constant": "Crate.size", "value": 0.05, "provenance": "retail_literal"},
    "model_z_offset": {"constant": "Crate.model_position_offsets[0].z", "value": -0.65, "provenance": "retail_literal"},
    "parachute_model_size": {"constant": "Crate.parachute_model_size", "value": 0.12, "provenance": "retail_literal"},
    "parachute_model_z_offset": {"constant": "Crate.model_position_offsets[1].z", "value": -1.35, "provenance": "retail_literal"},
    "sq_parachute_deployment_height": {"constant": "A1040*A1040", "value": 100, "provenance": "retail_alias"},
    "sq_parachute_removal_height": {"constant": "A1041*A1041", "value": 4, "provenance": "retail_alias"},
    "parachute_slowdown": {"constant": "A1042", "value": 0.75, "provenance": "retail_alias"},
    "max_bounces": {"constant": "Crate.max_bounces", "value": 1, "provenance": "retail_literal", "dead": true},
    "bounces": {"constant": "Crate.bounces", "value": 0, "provenance": "retail_literal", "dead": true},
    "vel": {"constant": "Crate.vel", "value": 0.0, "provenance": "retail_literal", "dead": true},
    "drop_sound": {"constant": "Crate.drop_sound", "value": null, "provenance": "retail_literal"},
    "landing_sound_threshold": {"constant": "Crate.landing_sound_threshold", "value": 2, "provenance": "retail_literal"},
    "touch_radius": {"constant": "CRATE_DISTANCE", "value": 2.5, "provenance": "retail_named", "unbound": true},
    "respawn_delay": {"constant": "CRATE_SPAWN_DELAY", "value": 25, "provenance": "retail_named", "unbound": true},
    "ammo": {"constant": null, "value": null, "provenance": "absent"},
    "pickup_sound_id": {"constant": "CRATE_SOUND_ID", "value": 13, "provenance": "battlespades", "note": "value 13 is retail; the AMMO_CRATE binding is ours"},
    "uses": {"constant": null, "value": 1, "provenance": "battlespades"},
    "team_only": {"constant": null, "value": null, "provenance": "absent"}
  },
  "4": {
    "type_id": {"constant": "A903", "value": 4, "provenance": "retail_alias"},
    "model": {"constant": "HEALTH_MODEL,CRATE_PARACHUTE_MODEL", "value": ["healthcrate", "Crate_Parachute"], "provenance": "retail_literal"},
    "icon": {"constant": "HealthCrate.icon", "value": null, "provenance": "retail_literal"},
    "needs_shadow": {"constant": "HealthCrate.needs_shadow", "value": true, "provenance": "retail_literal"},
    "spot_shadow_pos_offset": {"constant": "HealthCrate.spot_shadow_pos_offset", "value": [0.5, 0.5, 0.0], "provenance": "retail_literal"},
    "model_size": {"constant": "Crate.size", "value": 0.05, "provenance": "retail_literal"},
    "model_z_offset": {"constant": "Crate.model_position_offsets[0].z", "value": -0.65, "provenance": "retail_literal"},
    "parachute_model_size": {"constant": "Crate.parachute_model_size", "value": 0.12, "provenance": "retail_literal"},
    "parachute_model_z_offset": {"constant": "Crate.model_position_offsets[1].z", "value": -1.35, "provenance": "retail_literal"},
    "sq_parachute_deployment_height": {"constant": "A1040*A1040", "value": 100, "provenance": "retail_alias"},
    "sq_parachute_removal_height": {"constant": "A1041*A1041", "value": 4, "provenance": "retail_alias"},
    "parachute_slowdown": {"constant": "A1042", "value": 0.75, "provenance": "retail_alias"},
    "max_bounces": {"constant": "Crate.max_bounces", "value": 1, "provenance": "retail_literal", "dead": true},
    "bounces": {"constant": "Crate.bounces", "value": 0, "provenance": "retail_literal", "dead": true},
    "vel": {"constant": "Crate.vel", "value": 0.0, "provenance": "retail_literal", "dead": true},
    "drop_sound": {"constant": "Crate.drop_sound", "value": null, "provenance": "retail_literal"},
    "landing_sound_threshold": {"constant": "Crate.landing_sound_threshold", "value": 2, "provenance": "retail_literal"},
    "touch_radius": {"constant": "CRATE_DISTANCE", "value": 2.5, "provenance": "retail_named", "unbound": true},
    "respawn_delay": {"constant": "CRATE_SPAWN_DELAY", "value": 25, "provenance": "retail_named", "unbound": true},
    "health": {"constant": null, "value": null, "provenance": "absent", "note": "HEAL AMOUNT IS NOT IN RETAIL"},
    "kill_type": {"constant": "HEALTHCRATE_HP", "value": 20, "provenance": "retail_named", "unbound": true},
    "pickup_sound_id": {"constant": "HEALTHCRATE_SOUND_ID", "value": 14, "provenance": "retail_named", "unbound": true},
    "uses": {"constant": null, "value": 1, "provenance": "battlespades"},
    "team_only": {"constant": null, "value": null, "provenance": "absent"}
  },
  "5": {
    "type_id": {"constant": "A904", "value": 5, "provenance": "retail_alias"},
    "model": {"constant": "BLOCK_CRATE_MODEL,CRATE_PARACHUTE_MODEL", "value": ["block_crate", "Crate_Parachute"], "provenance": "retail_literal"},
    "icon": {"constant": "BlockCrate.icon", "value": null, "provenance": "retail_literal"},
    "blocks_granted": {"constant": "A2399[class][1] * block_wallet_multiplier", "value": "CLASS_BLOCKS[class][1]*multiplier", "provenance": "retail_alias"},
    "model_size": {"constant": "Crate.size", "value": 0.05, "provenance": "retail_literal"},
    "model_z_offset": {"constant": "Crate.model_position_offsets[0].z", "value": -0.65, "provenance": "retail_literal"},
    "parachute_model_size": {"constant": "Crate.parachute_model_size", "value": 0.12, "provenance": "retail_literal"},
    "parachute_model_z_offset": {"constant": "Crate.model_position_offsets[1].z", "value": -1.35, "provenance": "retail_literal"},
    "sq_parachute_deployment_height": {"constant": "A1040*A1040", "value": 100, "provenance": "retail_alias"},
    "sq_parachute_removal_height": {"constant": "A1041*A1041", "value": 4, "provenance": "retail_alias"},
    "parachute_slowdown": {"constant": "A1042", "value": 0.75, "provenance": "retail_alias"},
    "max_bounces": {"constant": "Crate.max_bounces", "value": 1, "provenance": "retail_literal", "dead": true},
    "bounces": {"constant": "Crate.bounces", "value": 0, "provenance": "retail_literal", "dead": true},
    "vel": {"constant": "Crate.vel", "value": 0.0, "provenance": "retail_literal", "dead": true},
    "drop_sound": {"constant": "Crate.drop_sound", "value": null, "provenance": "retail_literal"},
    "landing_sound_threshold": {"constant": "Crate.landing_sound_threshold", "value": 2, "provenance": "retail_literal"},
    "touch_radius": {"constant": "CRATE_DISTANCE", "value": 2.5, "provenance": "retail_named", "unbound": true},
    "respawn_delay": {"constant": "CRATE_SPAWN_DELAY", "value": 25, "provenance": "retail_named", "unbound": true},
    "pickup_sound_id": {"constant": "CRATE_BLOCKS_SOUND_ID", "value": 15, "provenance": "retail_named", "unbound": true},
    "uses": {"constant": null, "value": 1, "provenance": "battlespades"},
    "team_only": {"constant": null, "value": null, "provenance": "absent"}
  },
  "6": {
    "type_id": {"constant": "JETPACK_CRATE", "value": 6, "provenance": "retail_named"},
    "wire_safe": {"constant": null, "value": false, "provenance": "retail_literal"},
    "model": {"constant": "JETPACK_MODEL", "value": ["jetpack"], "provenance": "retail_literal"},
    "icon": {"constant": "JetpackCrate.icon", "value": null, "provenance": "retail_literal"},
    "model_size": {"constant": "Crate.size", "value": 0.05, "provenance": "retail_literal"},
    "model_z_offset": {"constant": "Crate.model_position_offsets[0].z", "value": -0.65, "provenance": "retail_literal"},
    "parachute_model_size": {"constant": "Crate.parachute_model_size", "value": 0.12, "provenance": "retail_literal", "note": "inert - no display[1]"},
    "sq_parachute_deployment_height": {"constant": "A1040*A1040", "value": 100, "provenance": "retail_alias"},
    "sq_parachute_removal_height": {"constant": "A1041*A1041", "value": 4, "provenance": "retail_alias"},
    "max_bounces": {"constant": "Crate.max_bounces", "value": 1, "provenance": "retail_literal", "dead": true},
    "bounces": {"constant": "Crate.bounces", "value": 0, "provenance": "retail_literal", "dead": true},
    "vel": {"constant": "Crate.vel", "value": 0.0, "provenance": "retail_literal", "dead": true},
    "drop_sound": {"constant": "Crate.drop_sound", "value": null, "provenance": "retail_literal"},
    "landing_sound_threshold": {"constant": "Crate.landing_sound_threshold", "value": 2, "provenance": "retail_literal"},
    "touch_radius": {"constant": null, "value": null, "provenance": "absent"},
    "respawn_delay": {"constant": null, "value": null, "provenance": "absent"},
    "fuel_granted": {"constant": null, "value": null, "provenance": "absent"},
    "pickup_sound_id": {"constant": null, "value": null, "provenance": "absent"},
    "uses": {"constant": null, "value": null, "provenance": "absent"},
    "team_only": {"constant": null, "value": null, "provenance": "absent"}
  },
  "7": {
    "type_id": {"constant": "MACHINE_GUN", "value": 7, "provenance": "retail_named"},
    "health": {"constant": "MG_HEALTH", "value": 100, "provenance": "retail_named", "unbound": true},
    "ammo": {"constant": "MG_AMMO", "value": 999, "provenance": "retail_named", "unbound": true},
    "clip_size": {"constant": "A1586", "value": 100, "provenance": "retail_alias"},
    "reserve_ammo_max": {"constant": "A1583", "value": 400, "provenance": "retail_alias"},
    "blast_radius": {"constant": "MG_EXPLOSION_RADIUS", "value": 3.0, "provenance": "retail_named", "unbound": true},
    "blast_damage": {"constant": "MG_EXPLOSION_DAMAGE", "value": 100, "provenance": "retail_named", "unbound": true},
    "block_damage": {"constant": "MG_EXPLOSION_BLOCK_DAMAGE", "value": 5, "provenance": "retail_named", "unbound": true},
    "blast_knockback_max": {"constant": "MG_EXPLOSION_KNOCKBACK_MAX", "value": 1.0, "provenance": "retail_named", "unbound": true},
    "blast_knockback_min": {"constant": "MG_EXPLOSION_KNOCKBACK_MIN", "value": 0.2, "provenance": "retail_named", "unbound": true},
    "model_size": {"constant": "A1589", "value": 0.06, "provenance": "retail_alias"},
    "model_z_offset_base": {"constant": "MG_BASE_MODEL_OFFSET_Z", "value": -1.5, "provenance": "retail_named", "unbound": true},
    "model_z_offset_top": {"constant": "MG_TOP_MODEL_OFFSET_Z", "value": -1.5, "provenance": "retail_named", "unbound": true},
    "model_position_offset_xyz": {"constant": null, "value": [0.0, 1.0, 0.5], "provenance": "retail_literal"},
    "placement_far_radius": {"constant": "MG_FAR_RADIUS", "value": 5.0, "provenance": "retail_named", "unbound": true},
    "yaw_range": {"constant": "A1588", "value": 45, "provenance": "retail_alias"},
    "pitch_range": {"constant": "A1587", "value": 45, "provenance": "retail_alias"},
    "deployment_time": {"constant": "A1546", "value": 3.0, "provenance": "retail_alias"},
    "withdrawal_time": {"constant": "A1547", "value": 0.75, "provenance": "retail_alias"},
    "range": {"constant": "A1548", "value": 300, "provenance": "retail_alias"},
    "reload_time": {"constant": "A1550", "value": 4.0, "provenance": "retail_alias"},
    "shoot_interval": {"constant": "A1553", "value": 0.5, "provenance": "retail_alias"},
    "shoot_interval_deployed": {"constant": "A1554", "value": 0.1, "provenance": "retail_alias"},
    "shoot_delay": {"constant": "MG_DELAY", "value": 0.11, "provenance": "retail_named", "unbound": true},
    "damage_torso": {"constant": "A1571", "value": 30, "provenance": "retail_alias"},
    "damage_head": {"constant": "A1573", "value": 20, "provenance": "retail_alias"},
    "damage_arms": {"constant": "A1575", "value": 20, "provenance": "retail_alias"},
    "damage_legs": {"constant": "A1577", "value": 20, "provenance": "retail_alias"},
    "damage_block": {"constant": "A1581", "value": 2, "provenance": "retail_alias"},
    "accuracy": {"constant": "A1555", "value": 0.01, "provenance": "retail_alias"},
    "accuracy_spread": {"constant": "A1559", "value": 1, "provenance": "retail_alias"},
    "recoil_up": {"constant": "A1567", "value": -0.007, "provenance": "retail_alias"},
    "shoot_sound": {"constant": "MG_SHOOT_SOUND", "value": ["semishoot", -1, 100, -0.8, 0.8], "provenance": "retail_named"},
    "muzzle_flash_model": {"constant": "MUZZLE_FLASH_MG", "value": "muzzleflash_default", "provenance": "retail_literal"},
    "art_present_3d": {"constant": null, "value": null, "provenance": "absent"},
    "damage_type": {"constant": "TOOLS_DAMAGE_TYPE[MG_TOOL]", "value": 6, "provenance": "retail_named"},
    "kill_type": {"constant": "TOOLS_KILL_TYPE[MG_TOOL]", "value": 0, "provenance": "retail_named"},
    "crater_radius": {"constant": null, "value": 1, "provenance": "battlespades"},
    "team_only": {"constant": null, "value": null, "provenance": "absent"},
    "touch_radius": {"constant": null, "value": null, "provenance": "absent"},
    "respawn_delay": {"constant": null, "value": null, "provenance": "absent"},
    "water_legal": {"constant": null, "value": null, "provenance": "absent"},
    "light_radius": {"constant": null, "value": null, "provenance": "absent"}
  },
  "8": {
    "type_id": {"constant": "ROCKET_TURRET_ENTITY", "value": 8, "provenance": "retail_named"},
    "health": {"constant": "ROCKET_TURRET_HEALTH", "value": 100, "provenance": "retail_named", "unbound": true},
    "blast_radius": {"constant": "ROCKET_TURRET_EXPLOSION_RADIUS", "value": 3.0, "provenance": "retail_named", "unbound": true},
    "blast_damage": {"constant": "ROCKET_TURRET_EXPLOSION_DAMAGE", "value": 100, "provenance": "retail_named", "unbound": true},
    "block_damage": {"constant": "ROCKET_TURRET_EXPLOSION_BLOCK_DAMAGE", "value": 15, "provenance": "retail_named", "unbound": true},
    "blast_knockback_max": {"constant": "ROCKET_TURRET_EXPLOSION_KNOCKBACK_MAX", "value": 1.0, "provenance": "retail_named", "unbound": true},
    "blast_knockback_min": {"constant": "ROCKET_TURRET_EXPLOSION_KNOCKBACK_MIN", "value": 0.2, "provenance": "retail_named", "unbound": true},
    "model_size": {"constant": "A1610", "value": 0.06, "provenance": "retail_alias"},
    "model_z_offset_base": {"constant": "A1612", "value": -0.18, "provenance": "retail_alias", "expr": "-3.0*0.06"},
    "model_z_offset_ball": {"constant": "A1613", "value": -1.02, "provenance": "retail_alias", "expr": "-17.0*0.06"},
    "model_z_offset_gun": {"constant": "A1614", "value": -0.6599999999999999, "provenance": "retail_alias", "expr": "-11.0*0.06"},
    "ammo": {"constant": "A1615", "value": 10, "provenance": "retail_alias"},
    "stock_max": {"constant": "A1600", "value": 4, "provenance": "retail_alias"},
    "stock_initial": {"constant": "A1601", "value": 2, "provenance": "retail_alias"},
    "stock_restock_amount": {"constant": "A1602", "value": 2, "provenance": "retail_alias"},
    "placement_far_radius": {"constant": "A1603", "value": 10, "provenance": "retail_alias"},
    "shoot_interval": {"constant": "A1604", "value": 1.5, "provenance": "retail_alias"},
    "tracking_range": {"constant": "ROCKET_TURRET_TRACKING_RANGE", "value": 50.0, "provenance": "retail_named", "unbound": true},
    "detection_range": {"constant": "ROCKET_TURRET_DETECTION_RANGE", "value": 30.0, "provenance": "retail_named", "unbound": true},
    "aim_tolerance": {"constant": "A1607", "value": 0.1, "provenance": "retail_alias"},
    "aiming_speed": {"constant": "ROCKET_TURRET_AIMING_SPEED", "value": 180, "provenance": "retail_named", "unbound": true},
    "lower_pitch_limit": {"constant": "ROCKET_TURRET_LOWER_PITCH_LIMIT", "value": 30, "provenance": "retail_named", "unbound": true},
    "aiming_tolerance_timer": {"constant": null, "value": 0.2, "provenance": "retail_literal"},
    "ammo_text_radius": {"constant": "A1626", "value": 20, "provenance": "retail_alias"},
    "ammo_text_z_offset": {"constant": null, "value": -1.0, "provenance": "retail_literal"},
    "ammo_text_color_ok": {"constant": "ENOUGH_AMMO_COLOR", "value": [255, 228, 0, 255], "provenance": "retail_named"},
    "ammo_text_color_empty": {"constant": "NOT_ENOUGH_AMMO_COLOR", "value": [204, 28, 24, 255], "provenance": "retail_named"},
    "rocket_blast_radius": {"constant": "ROCKET_TURRET_ROCKET_EXPLOSION_RADIUS", "value": 3, "provenance": "retail_named"},
    "rocket_blast_damage": {"constant": "ROCKET_TURRET_ROCKET_EXPLOSION_DAMAGE", "value": 50, "provenance": "retail_named"},
    "rocket_block_damage": {"constant": "ROCKET_TURRET_ROCKET_EXPLOSION_BLOCK_DAMAGE", "value": 10, "provenance": "retail_named"},
    "rocket_knockback_max": {"constant": "ROCKET_TURRET_ROCKET_EXPLOSION_KNOCKBACK_MAX", "value": 0.3, "provenance": "retail_named"},
    "rocket_knockback_min": {"constant": "ROCKET_TURRET_ROCKET_EXPLOSION_KNOCKBACK_MIN", "value": 0.1, "provenance": "retail_named"},
    "rocket_muzzle_speed": {"constant": "A1397", "value": 75, "provenance": "retail_alias"},
    "rocket_gravity_multiplier": {"constant": "A1398", "value": 0.05, "provenance": "retail_alias"},
    "rocket_shoot_sound": {"constant": "ROCKET_TURRET_SHOOT_SOUND", "value": ["turr_rocketshoot", -1, 100, -0.8, 0.8], "provenance": "retail_named"},
    "rocket_spawn_offset": {"constant": null, "value": 1.0, "provenance": "battlespades"},
    "damage_type": {"constant": "ROCKET_TURRET_DAMAGE", "value": 12, "provenance": "retail_named"},
    "kill_type": {"constant": "TOOLS_KILL_TYPE[ROCKET_TURRET_TOOL]", "value": 4, "provenance": "retail_named", "note": "ROCKET_TURRET_KILL=18 also exists"},
    "team_only": {"constant": null, "value": true, "provenance": "retail_literal"},
    "position_xy_bias": {"constant": null, "value": -0.5, "provenance": "retail_literal"},
    "rest_pose_yaw": {"constant": null, "value": 45.0, "provenance": "retail_literal"},
    "rest_pose_pitch": {"constant": null, "value": -45.0, "provenance": "retail_literal"},
    "death_glow_particle_count": {"constant": null, "value": 8, "provenance": "retail_literal"},
    "death_smoke_color": {"constant": null, "value": [96, 96, 96], "provenance": "retail_literal"},
    "death_smoke_args": {"constant": null, "value": {"numparticles": 10, "explode_velocity": 1.5, "size": 5.0, "lifetime": 2.0}, "provenance": "retail_literal"},
    "death_explode_display_args": {"constant": null, "value": [1.0, 5], "provenance": "retail_literal"},
    "water_death_sound_threshold_z": {"constant": "Z_ABOVE_WATERPLANE", "value": 238, "provenance": "retail_named"},
    "crater_radius": {"constant": null, "value": 1, "provenance": "battlespades"},
    "pitch_clamp_upper": {"constant": null, "value": 90.0, "provenance": "battlespades"},
    "touch_radius": {"constant": null, "value": null, "provenance": "absent"},
    "water_legal": {"constant": null, "value": null, "provenance": "absent"},
    "light_radius": {"constant": null, "value": null, "provenance": "absent"}
  },
  "9": {
    "type_id": {"constant": "LANDMINE_ENTITY", "value": 9, "provenance": "retail_named"},
    "health": {"constant": "A1808", "value": 1, "provenance": "retail_alias", "unbound": true},
    "arm_delay": {"constant": "A1798", "value": 4, "provenance": "retail_alias", "unbound": true},
    "blast_radius": {"constant": "A1796", "value": 3.0, "provenance": "retail_alias"},
    "blast_wave_radius": {"constant": "A1797", "value": 6.0, "provenance": "retail_alias", "unbound": true},
    "blast_damage": {"constant": "A1802", "value": 100, "provenance": "retail_alias", "unbound": true},
    "block_damage": {"constant": "A1803", "value": 15, "provenance": "retail_alias", "unbound": true},
    "trip_radius_horizontal": {"constant": "A1799", "value": 2.5, "provenance": "retail_alias", "unbound": true},
    "trip_radius_vertical": {"constant": "A1800", "value": 3, "provenance": "retail_alias", "unbound": true},
    "trip_detection_vertical_offset": {"constant": "A1801", "value": -0.5, "provenance": "retail_alias", "unbound": true},
    "knockback_max": {"constant": "A1804", "value": 0.75, "provenance": "retail_alias", "unbound": true},
    "knockback_min": {"constant": "A1805", "value": 0.75, "provenance": "retail_alias", "unbound": true},
    "placement_far_radius": {"constant": "A1806", "value": 5.0, "provenance": "retail_alias"},
    "model_size": {"constant": "A1807", "value": 0.05, "provenance": "retail_alias"},
    "model_z_offset": {"constant": "A1809", "value": 0.0, "provenance": "retail_alias"},
    "kv6_load_offset": {"constant": "LANDMINE_MODEL", "value": [5, -13, 0.0], "provenance": "retail_literal"},
    "ammo": {"constant": "A1792/A1793/A1794", "value": {"max_ammo": 5, "initial_ammo": 3, "max_clip": null, "initial_stock": null, "restock": 5}, "provenance": "retail_alias"},
    "shoot_interval": {"constant": "A1795", "value": 1.0, "provenance": "retail_alias"},
    "water_legal": {"constant": "A1810", "value": true, "provenance": "retail_alias"},
    "damage_type": {"constant": "LANDMINE_DAMAGE", "value": 15, "provenance": "retail_named"},
    "kill_type": {"constant": "LANDMINE_KILL", "value": 14, "provenance": "retail_named"},
    "crater_radius": {"constant": null, "value": 1, "provenance": "battlespades"},
    "team_only": {"constant": null, "value": true, "provenance": "battlespades"},
    "touch_radius": {"constant": null, "value": null, "provenance": "absent"},
    "fuse": {"constant": null, "value": null, "provenance": "absent"},
    "lifetime": {"constant": null, "value": null, "provenance": "absent"},
    "respawn_delay": {"constant": null, "value": null, "provenance": "absent"},
    "light_radius": {"constant": null, "value": null, "provenance": "absent"}
  },
  "10": {
    "type_id": {"constant": "DYNAMITE_ENTITY", "value": 10, "provenance": "retail_named"},
    "fuse": {"constant": "A1631", "value": 7, "provenance": "retail_alias"},
    "blast_radius": {"constant": "A1632", "value": 8, "provenance": "retail_alias", "conflict": "named block says 5"},
    "blast_damage": {"constant": "A1633", "value": 300.0, "provenance": "retail_alias"},
    "block_damage": {"constant": "A1634", "value": 7, "provenance": "retail_alias"},
    "knockback_max": {"constant": "A1635", "value": 0.15, "provenance": "retail_alias"},
    "knockback_min": {"constant": "A1636", "value": 0.1, "provenance": "retail_alias"},
    "placement_far_radius": {"constant": "A1637", "value": 5.0, "provenance": "retail_alias"},
    "model_size": {"constant": "A1638", "value": 0.06, "provenance": "retail_alias"},
    "health": {"constant": "A1639", "value": 1, "provenance": "retail_alias"},
    "model_z_offset": {"constant": "A1640", "value": -0.2, "provenance": "retail_alias"},
    "kv6_load_offset": {"constant": "DYNAMITE_MODEL", "value": [5, -13, 0.0], "provenance": "retail_literal"},
    "ammo": {"constant": "A1627/A1628/A1629", "value": {"max_ammo": 1, "initial_ammo": 1, "max_clip": null, "initial_stock": null, "restock": 1}, "provenance": "retail_alias", "conflict": "named block says stock 3 / restock 3"},
    "shoot_interval": {"constant": "A1630", "value": 1.0, "provenance": "retail_alias"},
    "default_face": {"constant": "DynamiteWeapon.face", "value": 4, "provenance": "retail_literal", "dead": true},
    "per_face_offset_table": {"constant": null, "value": {"0": [0.0, 0.5, 0.5], "1": [1.0, 0.5, 0.5], "2": [0.5, 0.0, 0.5], "3": [0.5, 1.0, 0.5], "4": [0.5, 0.5, 0.0], "5": [0.5, 0.5, 1.0]}, "provenance": "retail_literal"},
    "per_face_rotation_table": {"constant": null, "value": {"0": [90, 0, 0, 1], "1": [-90, 0, 0, 1], "2": [-90, 1, 0, 0], "3": [90, 1, 0, 0], "4": [0, 0, 0, 0], "5": [180, 1, 0, 0]}, "provenance": "retail_literal"},
    "damage_type": {"constant": "DYNAMITE_DAMAGE", "value": 16, "provenance": "retail_named"},
    "kill_type": {"constant": "DYNAMITE_KILL", "value": 15, "provenance": "retail_named"},
    "crater_radius": {"constant": null, "value": 2, "provenance": "battlespades"},
    "water_legal": {"constant": null, "value": null, "provenance": "absent"},
    "arm_delay": {"constant": null, "value": null, "provenance": "absent"},
    "trip_radius_horizontal": {"constant": null, "value": null, "provenance": "absent"},
    "lifetime": {"constant": null, "value": null, "provenance": "absent"},
    "light_radius": {"constant": null, "value": null, "provenance": "absent"}
  },
  "11": {
    "type_id": {"constant": "A910", "value": 11, "provenance": "retail_alias"},
    "fuse": {"constant": "A2086", "value": 7, "provenance": "retail_alias", "unbound": true},
    "blast_radius": {"constant": "A2087", "value": 3, "provenance": "retail_alias"},
    "blast_damage": {"constant": "A2088", "value": 25, "provenance": "retail_alias", "unbound": true},
    "block_damage": {"constant": "A2089", "value": 3, "provenance": "retail_alias", "unbound": true},
    "knockback_max": {"constant": "A2090", "value": 1.0, "provenance": "retail_alias", "unbound": true},
    "knockback_min": {"constant": "A2091", "value": 0.5, "provenance": "retail_alias", "unbound": true},
    "damage_type": {"constant": "A388", "value": 14, "provenance": "retail_alias"},
    "kill_type": {"constant": "A434", "value": 13, "provenance": "retail_alias"},
    "model": {"constant": "GRAVE_MODEL", "value": "grave", "provenance": "retail_literal"},
    "kv6_load_offset": {"constant": null, "value": [0.0, 0.0, 11.0], "provenance": "retail_literal"},
    "model_z_offset": {"constant": null, "value": null, "provenance": "absent"},
    "model_size": {"constant": null, "value": null, "provenance": "absent"},
    "health": {"constant": null, "value": null, "provenance": "absent"},
    "enable_rule": {"constant": "RULE_ENABLE_GRAVESTONES", "value": "ON", "provenance": "retail_named"},
    "explosion_rule": {"constant": "RULE_ENABLE_CORPSE_EXPLOSION", "value": "ON", "provenance": "retail_named"},
    "crater_radius": {"constant": null, "value": 1, "provenance": "battlespades"},
    "team_only": {"constant": null, "value": false, "provenance": "battlespades"}
  },
  "12": {
    "type_id": {"constant": "A911", "value": 12, "provenance": "retail_alias"},
    "wire_safe": {"constant": null, "value": false, "provenance": "retail_literal"},
    "fuse": {"constant": "A2074", "value": 0, "provenance": "retail_alias", "unbound": true},
    "fuse_jetpack": {"constant": "A2075", "value": 1.0, "provenance": "retail_alias", "unbound": true},
    "blast_radius": {"constant": "A2076", "value": 3, "provenance": "retail_alias"},
    "blast_damage": {"constant": "A2077", "value": 0, "provenance": "retail_alias", "unbound": true},
    "block_damage": {"constant": "A2078", "value": 1, "provenance": "retail_alias", "unbound": true},
    "knockback_max": {"constant": "A2079", "value": 0.1, "provenance": "retail_alias", "unbound": true},
    "knockback_min": {"constant": "A2080", "value": 0.05, "provenance": "retail_alias", "unbound": true},
    "vip_blast_radius": {"constant": "A2081", "value": 10, "provenance": "retail_alias", "unbound": true},
    "vip_blast_damage": {"constant": "A2082", "value": 75.0, "provenance": "retail_alias", "unbound": true},
    "vip_block_damage": {"constant": "A2083", "value": 20, "provenance": "retail_alias", "unbound": true},
    "bounce": {"constant": "A2291", "value": 0.1, "provenance": "retail_alias"},
    "bounce_sound_threshold": {"constant": "A2292", "value": 2, "provenance": "retail_alias", "unbound": true},
    "move_threshold": {"constant": "A2293", "value": 0.5, "provenance": "retail_alias"},
    "damage_type": {"constant": "A387", "value": 13, "provenance": "retail_alias"},
    "kill_type": {"constant": "A433", "value": 12, "provenance": "retail_alias"},
    "model_classic": {"constant": "CLASSIC_CORPSE_MODEL", "value": "ClassicCorpse", "provenance": "retail_literal"},
    "model_size": {"constant": null, "value": null, "provenance": "absent"},
    "model_z_offset": {"constant": null, "value": null, "provenance": "absent"},
    "explosion_rule": {"constant": "RULE_ENABLE_CORPSE_EXPLOSION", "value": "ON", "provenance": "retail_named"},
    "health": {"constant": null, "value": null, "provenance": "absent"},
    "crater_radius": {"constant": null, "value": 1, "provenance": "battlespades"}
  },
  "13": {
    "type_id": {"constant": "A912", "value": 13, "provenance": "retail_alias"},
    "tool_id": {"constant": "A318", "value": 22, "provenance": "retail_alias"},
    "block_cost": {"constant": "A2258", "value": 10, "provenance": "retail_alias"},
    "light_radius": {"constant": "A2261", "value": 5.0, "provenance": "retail_alias"},
    "health": {"constant": "A1031", "value": 5, "provenance": "retail_alias"},
    "shoot_interval": {"constant": "FlareBlockTool.shoot_interval", "value": 0.5, "provenance": "retail_literal"},
    "max_place_distance": {"constant": "A1017", "value": 10, "provenance": "retail_alias"},
    "max_place_distance_classic": {"constant": "A1012", "value": 5, "provenance": "retail_alias"},
    "model": {"constant": "BLOCK_MODEL", "value": "block", "provenance": "retail_literal"},
    "kv6_load_offset": {"constant": "BLOCK_MODEL", "value": [5, -13, 0.0], "provenance": "retail_literal"},
    "model_size": {"constant": null, "value": null, "provenance": "absent"},
    "damage_type": {"constant": "TOOLS_DAMAGE_TYPE[FLAREBLOCK_TOOL]", "value": null, "provenance": "retail_named"},
    "kill_type": {"constant": "TOOLS_KILL_TYPE[FLAREBLOCK_TOOL]", "value": 0, "provenance": "retail_named"},
    "water_legal": {"constant": null, "value": false, "provenance": "retail_literal"},
    "support_rule_neighbours": {"constant": null, "value": 6, "provenance": "battlespades"},
    "team_only": {"constant": null, "value": false, "provenance": "battlespades"}
  },
  "14": {
    "type_id": {"constant": "BOMB_PICKUP", "value": 14, "provenance": "retail_named"},
    "touch_radius": {"constant": "PICKUP_DISTANCE", "value": 3.0, "provenance": "retail_named"},
    "fuse": {"constant": "BOMB_EXPLOSION_FUSE", "value": 10, "provenance": "retail_named", "unbound": true},
    "blast_radius": {"constant": "A2093", "value": 7, "provenance": "retail_alias"},
    "blast_damage": {"constant": "A2094", "value": 500, "provenance": "retail_alias"},
    "block_damage": {"constant": "BOMB_EXPLOSION_BLOCK_DAMAGE", "value": 20, "provenance": "retail_named", "unbound": true},
    "knockback_max": {"constant": "A2096", "value": 3.0, "provenance": "retail_alias"},
    "knockback_min": {"constant": "A2097", "value": 2.0, "provenance": "retail_alias"},
    "throw_speed": {"constant": "BOMB_THROW_SPEED", "value": 10, "provenance": "retail_named"},
    "no_pickup_after_drop_time": {"constant": "NO_PICKUP_AFTER_DROP_TIME", "value": 2.5, "provenance": "retail_named"},
    "model": {"constant": "BOMB_ENTITY_MODEL", "value": "Bomb", "provenance": "retail_literal"},
    "kv6_load_offset": {"constant": "BOMB_ENTITY_MODEL", "value": [0.0, 0.0, 7.0], "provenance": "retail_literal"},
    "fuse_smoke_emitter_offset": {"constant": "BOMB_SMOKE_X/Y/Z_OFFSET", "value": [-0.05, -0.05, -1.2], "provenance": "retail_named"},
    "damage_type": {"constant": "BOMB_DAMAGE", "value": 19, "provenance": "retail_named"},
    "kill_type": {"constant": "BOMB_KILL", "value": 17, "provenance": "retail_named"},
    "pickup_tool_id": {"constant": "BOMB_TOOL", "value": 25, "provenance": "retail_named"},
    "model_size": {"constant": null, "value": null, "provenance": "absent"},
    "crater_radius": {"constant": null, "value": null, "provenance": "absent"},
    "team_only": {"constant": null, "value": null, "provenance": "absent"},
    "health": {"constant": null, "value": null, "provenance": "absent"},
    "lifetime": {"constant": null, "value": null, "provenance": "absent"},
    "light_radius": {"constant": null, "value": null, "provenance": "absent"},
    "ammo": {"constant": null, "value": null, "provenance": "absent"}
  },
  "15": {
    "type_id": {"constant": "DIAMOND_PICKUP", "value": 15, "provenance": "retail_named"},
    "touch_radius": {"constant": "PICKUP_DISTANCE", "value": 3.0, "provenance": "retail_named"},
    "lifetime": {"constant": "DIAMOND_LIFETIME", "value": 60, "provenance": "retail_named", "unbound": true},
    "throw_speed": {"constant": "DIAMOND_THROW_SPEED", "value": 15, "provenance": "retail_named"},
    "no_pickup_after_drop_time": {"constant": "NO_PICKUP_AFTER_DROP_TIME", "value": 2.5, "provenance": "retail_named"},
    "respawn_delay": {"constant": "DIA_TIME_BETWEEN_DIAMOND_SPAWN", "value": 15, "provenance": "retail_named"},
    "model": {"constant": "DIAMOND_ENTITY_MODEL", "value": "diamond", "provenance": "retail_literal"},
    "kv6_load_offset": {"constant": "DIAMOND_ENTITY_MODEL", "value": [0.0, 0.0, 7.0], "provenance": "retail_literal"},
    "model_size": {"constant": null, "value": null, "provenance": "absent"},
    "damage_type": {"constant": "TOOLS_DAMAGE_TYPE[DIAMOND_TOOL]", "value": null, "provenance": "retail_named"},
    "kill_type": {"constant": "TOOLS_KILL_TYPE[DIAMOND_TOOL]", "value": 0, "provenance": "retail_named"},
    "pickup_tool_id": {"constant": "DIAMOND_TOOL", "value": 26, "provenance": "retail_named"},
    "fx_particle_count": {"constant": "A2343", "value": 50, "provenance": "retail_named"},
    "fx_vertical_speed": {"constant": "A2344", "value": 0.08, "provenance": "retail_named"},
    "fx_explosion_speed": {"constant": "A2345", "value": 0.07, "provenance": "retail_named"},
    "fx_particle_size": {"constant": "A2346", "value": 5, "provenance": "retail_named"},
    "fx_initial_rotation": {"constant": "A2347", "value": 0, "provenance": "retail_named"},
    "fx_rotation_speed": {"constant": "A2348", "value": 180, "provenance": "retail_named"},
    "fx_decay_rate": {"constant": "A2349", "value": 1, "provenance": "retail_named"},
    "fx_lifetime": {"constant": "A2350", "value": 2, "provenance": "retail_named"},
    "fx_start_frame": {"constant": "A2351", "value": 0, "provenance": "retail_named"},
    "fx_num_frames_x": {"constant": "A2352", "value": 4, "provenance": "retail_named"},
    "fx_num_frames_y": {"constant": "A2353", "value": 4, "provenance": "retail_named"},
    "fx_loop": {"constant": "A2354", "value": 0, "provenance": "retail_named"},
    "fx_framerate": {"constant": "A2355", "value": 30, "provenance": "retail_named"},
    "fx_collides": {"constant": "A2356", "value": false, "provenance": "retail_named"},
    "fx_gravity": {"constant": "A2357", "value": false, "provenance": "retail_named"},
    "fx_alpha_blend_mode": {"constant": "A2358", "value": 2, "provenance": "retail_named"},
    "health": {"constant": null, "value": null, "provenance": "absent"},
    "fuse": {"constant": null, "value": null, "provenance": "absent"},
    "blast_radius": {"constant": null, "value": null, "provenance": "absent"},
    "light_radius": {"constant": null, "value": null, "provenance": "absent"},
    "team_only": {"constant": null, "value": null, "provenance": "absent"}
  },
  "16": {
    "type_id": {"constant": "INTEL_PICKUP", "value": 16, "provenance": "retail_named"},
    "touch_radius": {"constant": "PICKUP_DISTANCE", "value": 3.0, "provenance": "retail_named"},
    "throw_speed": {"constant": "INTEL_THROW_SPEED", "value": 15, "provenance": "retail_named"},
    "auto_return_time": {"constant": "CTF_INTEL_RETURN_TIME", "value": 60, "provenance": "retail_named"},
    "no_pickup_after_drop_time": {"constant": "NO_PICKUP_AFTER_DROP_TIME", "value": 2.5, "provenance": "retail_named"},
    "use_other_team_color": {"constant": "IntelTool.use_other_team_color", "value": true, "provenance": "retail_literal"},
    "model": {"constant": "INTEL_ENTITY_MODEL", "value": "intel", "provenance": "retail_literal"},
    "model_z_offset": {"constant": null, "value": null, "provenance": "absent"},
    "model_size": {"constant": null, "value": null, "provenance": "absent"},
    "floating_slowdown_factor": {"constant": "IntelPickup.floating_slowdown_factor", "value": null, "provenance": "absent"},
    "minimap_exposure_time": {"constant": "INTEL_MINIMAP_EXPOSURE_TIME", "value": 30, "provenance": "retail_named", "unbound": true},
    "minimap_height_icon_threshold": {"constant": "MINIMAP_HEIGHT_ICON_THRESHOLD", "value": 4, "provenance": "retail_named"},
    "damage_type": {"constant": "TOOLS_DAMAGE_TYPE[INTEL_TOOL]", "value": null, "provenance": "retail_named"},
    "kill_type": {"constant": "TOOLS_KILL_TYPE[INTEL_TOOL]", "value": 0, "provenance": "retail_named"},
    "pickup_tool_id": {"constant": "INTEL_TOOL", "value": 30, "provenance": "retail_named"},
    "team_only": {"constant": null, "value": null, "provenance": "absent"},
    "fuse": {"constant": null, "value": null, "provenance": "absent"},
    "lifetime": {"constant": null, "value": null, "provenance": "absent"},
    "health": {"constant": null, "value": null, "provenance": "absent"},
    "light_radius": {"constant": null, "value": null, "provenance": "absent"}
  },
  "17": {
    "type_id": {"constant": "A916", "value": 17, "provenance": "retail_alias"},
    "blast_radius": {"constant": "A2102", "value": 6, "provenance": "retail_alias"},
    "blast_damage": {"constant": "A2103", "value": 400.0, "provenance": "retail_alias", "unbound": true},
    "block_damage": {"constant": "A2104", "value": 15, "provenance": "retail_alias", "unbound": true},
    "knockback_max": {"constant": "A2105", "value": 2.0, "provenance": "retail_alias", "unbound": true},
    "knockback_min": {"constant": "A2106", "value": 1.0, "provenance": "retail_alias", "unbound": true},
    "shell_speed": {"constant": "A2107", "value": 100, "provenance": "retail_alias"},
    "gravity_multiplier": {"constant": "A2108", "value": 100, "provenance": "retail_alias"},
    "model": {"constant": "AIRSTRIKE_BOMB_VIEW_MODEL", "value": "airstrike_bomb", "provenance": "retail_literal"},
    "model_z_offset": {"constant": null, "value": null, "provenance": "absent"},
    "model_size": {"constant": null, "value": null, "provenance": "absent"},
    "damage_type": {"constant": "A392", "value": 18, "provenance": "retail_alias"},
    "kill_type": {"constant": "A437", "value": 16, "provenance": "retail_alias"},
    "cadence": {"constant": null, "value": null, "provenance": "absent"},
    "lifetime": {"constant": null, "value": null, "provenance": "absent"},
    "team_only": {"constant": null, "value": null, "provenance": "absent"}
  },
  "18": {
    "type_id": {"constant": "AMMO_DROP_POINT_ENTITY", "value": 18, "provenance": "retail_named"},
    "model": {"constant": "AMMO_DROP_POINT_MODEL", "value": "Crate_Target", "provenance": "retail_literal"},
    "kv6_load_offset": {"constant": null, "value": [0.0, 0.0, 1.0], "provenance": "retail_literal"},
    "model_size": {"constant": null, "value": null, "provenance": "absent"},
    "ugc_item_id": {"constant": "UGC_ITEM_AMMO_DROP_POINT", "value": 1, "provenance": "retail_named"},
    "ugc_tool_image": {"constant": "UGC_TOOL_IMAGES", "value": "ugc_ammo_drop", "provenance": "retail_named"},
    "ugc_minimap_icon": {"constant": "UGC_MINIMAP_ICON_NAMES", "value": "minimap_ammocrate", "provenance": "retail_named"},
    "respawn_delay": {"constant": null, "value": null, "provenance": "absent"},
    "icon": {"constant": "Entity.icon", "value": null, "provenance": "retail_literal"},
    "touch_radius": {"constant": null, "value": null, "provenance": "absent"},
    "health": {"constant": null, "value": null, "provenance": "absent"},
    "team_only": {"constant": null, "value": null, "provenance": "absent"}
  },
  "19": {
    "type_id": {"constant": "HEALTH_DROP_POINT_ENTITY", "value": 19, "provenance": "retail_named"},
    "model": {"constant": "HEALTH_DROP_POINT_MODEL", "value": "Crate_Target", "provenance": "retail_literal"},
    "kv6_load_offset": {"constant": null, "value": [0.0, 0.0, 1.0], "provenance": "retail_literal"},
    "model_size": {"constant": null, "value": null, "provenance": "absent"},
    "ugc_item_id": {"constant": "UGC_ITEM_HEALTH_DROP_POINT", "value": 0, "provenance": "retail_named"},
    "ugc_tool_image": {"constant": "UGC_TOOL_IMAGES", "value": "ugc_health_drop", "provenance": "retail_named"},
    "ugc_minimap_icon": {"constant": "UGC_MINIMAP_ICON_NAMES", "value": "minimap_healthcrate", "provenance": "retail_named"},
    "respawn_delay": {"constant": null, "value": null, "provenance": "absent"},
    "touch_radius": {"constant": null, "value": null, "provenance": "absent"},
    "health": {"constant": null, "value": null, "provenance": "absent"},
    "team_only": {"constant": null, "value": null, "provenance": "absent"}
  },
  "20": {
    "type_id": {"constant": "BLOCK_CRATE_DROP_POINT_ENTITY", "value": 20, "provenance": "retail_named"},
    "model": {"constant": "BLOCK_CRATE_DROP_POINT_MODEL", "value": "Crate_Target", "provenance": "retail_literal"},
    "kv6_load_offset": {"constant": null, "value": [0.0, 0.0, 1.0], "provenance": "retail_literal"},
    "model_size": {"constant": null, "value": null, "provenance": "absent"},
    "ugc_item_id": {"constant": "UGC_ITEM_BLOCK_DROP_POINT", "value": 2, "provenance": "retail_named"},
    "ugc_tool_image": {"constant": "UGC_TOOL_IMAGES", "value": "ugc_block_drop", "provenance": "retail_named"},
    "ugc_minimap_icon": {"constant": "UGC_MINIMAP_ICON_NAMES", "value": "minimap_blockcrate", "provenance": "retail_named"},
    "respawn_delay": {"constant": null, "value": null, "provenance": "absent"},
    "touch_radius": {"constant": null, "value": null, "provenance": "absent"},
    "health": {"constant": null, "value": null, "provenance": "absent"},
    "team_only": {"constant": null, "value": null, "provenance": "absent"}
  },
  "21": {
    "type_id": {"constant": "ROCKET_ENTITY", "value": 21, "provenance": "retail_named"},
    "speed": {"constant": "A1397", "value": 75, "provenance": "retail_alias"},
    "gravity_multiplier": {"constant": "A1398", "value": 0.05, "provenance": "retail_alias"},
    "model_z_offset": {"constant": "ROCKET_MODEL_Z_OFFSET", "value": 0.0, "provenance": "retail_named"},
    "blast_wave_radius": {"constant": "ROCKET_EXPLOSION_BLAST_WAVE_RADIUS", "value": 6.0, "provenance": "retail_named"}
  },
  "22": {
    "type_id": {"constant": "ROCKET2_ENTITY", "value": 22, "provenance": "retail_named"},
    "model_z_offset": {"constant": "ROCKET2_MODEL_Z_OFFSET", "value": 0.0, "provenance": "retail_named"},
    "blast_wave_radius": {"constant": "ROCKET2_EXPLOSION_BLAST_WAVE_RADIUS", "value": 4.0, "provenance": "retail_named"}
  },
  "23": {
    "type_id": {"constant": "DRILL_ENTITY", "value": 23, "provenance": "retail_named"},
    "model_z_offset": {"constant": "DRILL_MODEL_Z_OFFSET", "value": 0.0, "provenance": "retail_named"},
    "model_size": {"constant": "Drill.size", "value": 0.08, "provenance": "retail_literal"}
  },
  "24": {
    "type_id": {"constant": "SNOWBALL_ENTITY", "value": 24, "provenance": "retail_named"},
    "kill_type": {"constant": "SNOWBALL_KILL", "value": 21, "provenance": "retail_named"}
  },
  "25": {
    "type_id": {"constant": "CAPTURE_POINT_ENTITY", "value": 25, "provenance": "retail_named"},
    "model": {"constant": "CP_MODEL", "value": "cp", "provenance": "retail_literal"},
    "model_z_offset": {"constant": null, "value": null, "provenance": "absent"},
    "model_size": {"constant": null, "value": null, "provenance": "absent"},
    "minimap_height_icon_threshold": {"constant": "MINIMAP_HEIGHT_ICON_THRESHOLD", "value": 4, "provenance": "retail_named"},
    "touch_radius": {"constant": "CAPTURE_POINT_DISTANCE", "value": 3.0, "provenance": "retail_named", "dead": true},
    "refill_time": {"constant": "CAPTURE_POINT_REFILL_TIME", "value": 10.0, "provenance": "retail_named", "dead": true},
    "restock_behaviour": {"constant": null, "value": null, "provenance": "absent"},
    "team_only": {"constant": null, "value": null, "provenance": "absent"},
    "health": {"constant": null, "value": null, "provenance": "absent"},
    "respawn_delay": {"constant": null, "value": null, "provenance": "absent"},
    "light_radius": {"constant": null, "value": null, "provenance": "absent"}
  },
  "26": {
    "type_id": {"constant": "TANK_ENTITY", "value": 26, "provenance": "retail_named"},
    "wire_safe": {"constant": null, "value": false, "provenance": "retail_literal"}
  },
  "27": {
    "type_id": {"constant": "MOLOTOV_ENTITY", "value": 27, "provenance": "retail_named"},
    "gravity_multiplier": {"constant": "A1648", "value": 1.0, "provenance": "retail_alias"}
  },
  "28": {
    "type_id": {"constant": "A927", "value": 28, "provenance": "retail_alias"},
    "lifetime": {"constant": "BLOCKFIRE_MAX_LIFESPAN", "value": 4.0, "provenance": "retail_named"},
    "player_damage_per_tick": {"constant": "BLOCKFIRE_CHARACTER_DAMAGE", "value": 2.5, "provenance": "retail_named", "unbound": true},
    "player_damage_tick": {"constant": "BLOCKFIRE_CHARACTER_DAMAGE_TIMER", "value": 0.3, "provenance": "retail_named", "unbound": true},
    "player_ignite_range": {"constant": "BLOCKFIRE_CHARACTER_SPREAD_RANGE", "value": 3, "provenance": "retail_named", "unbound": true},
    "player_burn_duration": {"constant": "BLOCKFIRE_CHARACTER_DURATION", "value": 10, "provenance": "retail_named", "unbound": true},
    "block_damage": {"constant": "BLOCKFIRE_BLOCK_DAMAGE", "value": 0.7, "provenance": "retail_named", "unbound": true},
    "block_damage_tick": {"constant": "BLOCKFIRE_BLOCK_DAMAGE_TIMER", "value": 0.4, "provenance": "retail_named", "unbound": true},
    "spread_count": {"constant": "BLOCKFIRE_SPREAD_COUNT", "value": 5, "provenance": "retail_named", "unbound": true},
    "spread_tick": {"constant": "BLOCKFIRE_SPREAD_TIMER", "value": 0.5, "provenance": "retail_named", "unbound": true},
    "spread_radius": {"constant": "BLOCKFIRE_SPREAD_RADIUS", "value": 2.0, "provenance": "retail_named"},
    "spread_chance": {"constant": "BLOCKFIRE_MAX_RANDOM_CHANCE", "value": 0.3, "provenance": "retail_named", "unbound": true},
    "blocks_to_attempt_to_light": {"constant": "BLOCKFIRE_BLOCKS_TO_ATTEMPT_TO_LIGHT", "value": -1, "provenance": "retail_named", "unbound": true},
    "max_falling_distance": {"constant": "BLOCKFIRE_MAX_FALLING_DISTANCE", "value": 1.0, "provenance": "retail_named", "unbound": true},
    "initial_spread_radius": {"constant": "BLOCKFIRE_INITIAL_SPREAD_RADIUS", "value": 2, "provenance": "retail_named"},
    "colour_ramp_hot": {"constant": "BLOCKFIRE_HOT_COLOUR", "value": [255, 255, 255], "provenance": "retail_named"},
    "colour_ramp_mid": {"constant": "BLOCKFIRE_MID_COLOUR", "value": [255, 255, 0], "provenance": "retail_named"},
    "colour_ramp_cold": {"constant": "BLOCKFIRE_COLD_COLOUR", "value": [255, 0, 0], "provenance": "retail_named"},
    "light_radius": {"constant": "A2262", "value": 3.0, "provenance": "retail_alias"},
    "smoke_rate_min": {"constant": "A2129", "value": 1.0, "provenance": "retail_alias"},
    "smoke_rate_max": {"constant": "A2130", "value": 2.0, "provenance": "retail_alias"},
    "smoke_particle_decay": {"constant": "A2131", "value": -1, "provenance": "retail_alias"},
    "smoke_particle_lifespan": {"constant": "A2132", "value": 3, "provenance": "retail_alias"},
    "smoke_particle_min_size": {"constant": "A2133", "value": 4, "provenance": "retail_alias"},
    "smoke_particle_max_size": {"constant": "A2134", "value": 8, "provenance": "retail_alias"},
    "smoke_min_velocity": {"constant": "A2135", "value": 0.0, "provenance": "retail_alias"},
    "smoke_max_velocity": {"constant": "A2136", "value": 0.1, "provenance": "retail_alias"},
    "damage_type": {"constant": "A399", "value": 25, "provenance": "retail_alias"},
    "kill_type": {"constant": "A446", "value": 25, "provenance": "retail_alias"},
    "model": {"constant": null, "value": null, "provenance": "absent"},
    "health": {"constant": null, "value": null, "provenance": "absent"},
    "face": {"constant": "FACE_TOP", "value": 4, "provenance": "battlespades", "note": "mandatory - any other face crashes a retail client"},
    "team_only": {"constant": null, "value": false, "provenance": "battlespades"}
  },
  "29": {
    "type_id": {"constant": "UGC_ENTITY", "value": 29, "provenance": "retail_named"},
    "model_kv6_baseplate": {"constant": "UGC_ENTITY_BASEPLATE_MODEL", "value": "ugc_baseplate", "provenance": "retail_literal"},
    "model_kv6_spawn_zone": {"constant": "UGC_SPAWN_ZONE_MODEL", "value": "ugc_spawn_zone", "provenance": "retail_literal"},
    "model_kv6_base_zone": {"constant": "UGC_BASE_ZONE_MODEL", "value": "ugc_base_zone", "provenance": "retail_literal"},
    "kv6_load_offset_ugc_meshes": {"constant": null, "value": [0.0, 0.0, 1.0], "provenance": "retail_literal"},
    "model_z_offset_item_overlay": {"constant": null, "value": -0.5, "provenance": "retail_literal"},
    "model_size_ghost": {"constant": null, "value": 0.06, "provenance": "retail_literal", "note": "UGCTool ghost path only; the entity has no create_display override"},
    "item_scales": {"constant": "UGC_ENTITY_MODELS", "value": {"0": 0.25, "1": 0.25, "2": 0.25, "3": 1.0, "4": 0.25, "5": 1.0, "6": 1.5, "7": 0.25, "8": 1.0, "9": 1.5, "10": 0.5, "11": 1.0, "12": 1.75, "13": 0.5, "14": 1.0, "15": 1.75, "16": 0.5, "17": 1.0, "18": 1.75}, "provenance": "retail_literal"},
    "zone_size_small": {"constant": "UGC_ZONE_SIZES", "value": [-5, 5, -5, 5, -8, 2], "provenance": "retail_named"},
    "zone_size_medium": {"constant": "UGC_ZONE_SIZES", "value": [-12, 12, -12, 12, -21, 3], "provenance": "retail_named"},
    "zone_size_large": {"constant": "UGC_ZONE_SIZES", "value": [-20, 20, -20, 20, -36, 4], "provenance": "retail_named"},
    "zone_team_owner": {"constant": "UGC_ENTITY_TEAMS", "value": {"green": 3, "blue": 2, "neutral": 1}, "provenance": "retail_named"},
    "base_zone_tint_alpha": {"constant": "A2286", "value": 150, "provenance": "retail_alias"},
    "placement_max_distance": {"constant": "A2006", "value": 10.0, "provenance": "retail_alias"},
    "placement_entity_min_radius": {"constant": null, "value": 1, "provenance": "retail_literal"},
    "ghost_blend_alpha": {"constant": null, "value": 0.3, "provenance": "retail_literal"},
    "touch_radius": {"constant": null, "value": null, "provenance": "absent"},
    "health": {"constant": null, "value": null, "provenance": "absent"}
  },
  "30": {
    "type_id": {"constant": "UNKNOWN_ENTITY1", "value": 30, "provenance": "retail_named"},
    "ammo": {"constant": "A1868/A1869/A1870", "value": {"max_ammo": 2, "initial_ammo": 2, "max_clip": null, "initial_stock": null, "restock": 1}, "provenance": "retail_alias"},
    "shoot_interval": {"constant": "A1865", "value": 1.0, "provenance": "retail_alias", "note": "A1865 is assigned twice; 1.0 wins"},
    "reload_time": {"constant": "A1860", "value": 1.5, "provenance": "retail_alias"},
    "heal_amount": {"constant": "A1871", "value": 25, "provenance": "retail_alias", "unbound": true, "inferred_slot": true},
    "uses": {"constant": "A1872", "value": 3, "provenance": "retail_alias", "unbound": true, "inferred_slot": true},
    "model_size": {"constant": "A1873", "value": 0.06, "provenance": "retail_alias"},
    "health": {"constant": "A1874", "value": 1, "provenance": "retail_alias", "unbound": true, "inferred_slot": true},
    "model_z_offset": {"constant": "A1875", "value": -0.5, "provenance": "retail_alias"},
    "placement_radius": {"constant": "A1880", "value": 5.0, "provenance": "retail_alias"},
    "model": {"constant": "MEDPACK_MODEL", "value": "MedPack", "provenance": "retail_literal"},
    "kv6_load_offset_world": {"constant": "MEDPACK_MODEL", "value": [8, -21, 0.0], "provenance": "retail_literal"},
    "kv6_load_offset_view": {"constant": "MEDPACK_VIEW_MODEL", "value": [0.0, 0.0, -3.0], "provenance": "retail_literal"},
    "damage_type": {"constant": "TOOLS_DAMAGE_TYPE[MEDPACK_TOOL]", "value": null, "provenance": "retail_named"},
    "kill_type": {"constant": null, "value": null, "provenance": "absent"},
    "lifetime": {"constant": null, "value": null, "provenance": "absent", "note": "A1879=300 is an unassigned candidate"},
    "fuse": {"constant": null, "value": null, "provenance": "absent"},
    "touch_radius": {"constant": null, "value": null, "provenance": "absent"},
    "water_legal": {"constant": null, "value": null, "provenance": "absent"},
    "team_only": {"constant": null, "value": true, "provenance": "battlespades"},
    "respawn_delay": {"constant": null, "value": null, "provenance": "absent"},
    "light_radius": {"constant": null, "value": null, "provenance": "absent"}
  },
  "31": {"type_id": {"constant": "UNKNOWN_ENTITY2", "value": 31, "provenance": "retail_named"}},
  "32": {"type_id": {"constant": "UNKNOWN_ENTITY3", "value": 32, "provenance": "retail_named"}},
  "33": {"type_id": {"constant": "UNKNOWN_ENTITY4", "value": 33, "provenance": "retail_named"}},
  "34": {"type_id": {"constant": "UNKNOWN_ENTITY5", "value": 34, "provenance": "retail_named"}},
  "35": {"type_id": {"constant": "UNKNOWN_ENTITY6", "value": 35, "provenance": "retail_named"}},
  "36": {
    "type_id": {"constant": "UNKNOWN_ENTITY7", "value": 36, "provenance": "retail_named"},
    "ammo": {"constant": "A1893/A1894/A1895", "value": {"max_ammo": 1, "initial_ammo": 1, "max_clip": null, "initial_stock": null, "restock": 1}, "provenance": "retail_alias"},
    "placement_radius": {"constant": "A1896", "value": 10, "provenance": "retail_alias"},
    "shoot_interval": {"constant": "A1897", "value": 1.5, "provenance": "retail_alias"},
    "model_size": {"constant": "A1898", "value": 0.03, "provenance": "retail_alias"},
    "health": {"constant": "A1899", "value": 45, "provenance": "retail_alias", "inferred_slot": true},
    "lifetime": {"constant": "A1900", "value": 250, "provenance": "retail_alias", "inferred_slot": true, "note": "CONTESTED - retail tool text says 'Low lifetime'; measured ~35s"},
    "detection_range": {"constant": "A1901", "value": 45, "provenance": "retail_alias", "unbound": true, "inferred_slot": true},
    "model_z_offset": {"constant": "A1902", "value": -0.55, "provenance": "retail_alias"},
    "fuse": {"constant": null, "value": 250, "provenance": "battlespades", "note": "must be non-zero or the client never removes the model"},
    "model": {"constant": "RADAR_STATION_BASE_ENTITY_MODEL", "value": "radar_station", "provenance": "retail_literal"},
    "kv6_load_offset_tool": {"constant": "RADAR_STATION_BASE_TOOL_MODEL", "value": [20.0, -40.0, 28.0], "provenance": "retail_literal"},
    "place_sound": {"constant": "A2923", "value": "AoS_soundfx_PLAYER_marksman_item_RADAR_place_001", "provenance": "retail_alias"},
    "ping_sound": {"constant": "A2924", "value": "AoS_soundfx_PLAYER_marksman_item_RADAR_ping_001", "provenance": "retail_alias"},
    "damage_type": {"constant": "TOOLS_DAMAGE_TYPE[RADAR_STATION_TOOL]", "value": null, "provenance": "retail_named"},
    "kill_type": {"constant": "RADAR_STATION_KILL", "value": 33, "provenance": "retail_named", "unbound": true},
    "team_only": {"constant": null, "value": true, "provenance": "battlespades"},
    "respawn_delay": {"constant": null, "value": null, "provenance": "absent"},
    "light_radius": {"constant": null, "value": null, "provenance": "absent"}
  },
  "37": {
    "type_id": {"constant": "UNKNOWN_ENTITY8", "value": 37, "provenance": "retail_named"},
    "reload_time": {"constant": "A1718", "value": 2.0, "provenance": "retail_alias"},
    "shoot_interval": {"constant": "A1723", "value": 0.35, "provenance": "retail_alias"},
    "ammo": {"constant": "A1727/A1724/A1725/A1726", "value": {"max_ammo": 1, "initial_ammo": 1, "max_clip": 5, "initial_stock": 3, "restock": 5}, "provenance": "retail_alias"},
    "throw_speed": {"constant": "A1728", "value": 75, "provenance": "retail_alias"},
    "blast_radius": {"constant": "A1729", "value": 3.0, "provenance": "retail_alias", "inferred_slot": true},
    "blast_wave_radius": {"constant": "A1730", "value": 6.0, "provenance": "retail_alias", "inferred_slot": true},
    "arm_delay": {"constant": "A1731", "value": 4, "provenance": "retail_alias", "inferred_slot": true},
    "trip_radius_horizontal": {"constant": "A1732", "value": 2.5, "provenance": "retail_alias", "inferred_slot": true},
    "trip_radius_vertical": {"constant": "A1733", "value": 3, "provenance": "retail_alias", "inferred_slot": true},
    "trip_detection_vertical_offset": {"constant": "A1734", "value": -0.5, "provenance": "retail_alias", "inferred_slot": true},
    "blast_damage": {"constant": "A1735", "value": 100, "provenance": "retail_alias", "inferred_slot": true},
    "block_damage": {"constant": "A1736", "value": 15, "provenance": "retail_alias", "inferred_slot": true},
    "knockback_max": {"constant": "A1737", "value": 0.75, "provenance": "retail_alias", "inferred_slot": true},
    "knockback_min": {"constant": "A1738", "value": 0.75, "provenance": "retail_alias", "inferred_slot": true},
    "placement_far_radius": {"constant": "A1739", "value": 5.0, "provenance": "retail_alias", "inferred_slot": true},
    "model_size": {"constant": "A1740", "value": 0.05, "provenance": "retail_alias", "inferred_slot": true},
    "health": {"constant": "A1741", "value": 1, "provenance": "retail_alias", "inferred_slot": true},
    "model_z_offset": {"constant": "A1742", "value": 0.0, "provenance": "retail_alias", "inferred_slot": true},
    "water_legal": {"constant": "A1743", "value": true, "provenance": "retail_alias", "inferred_slot": true},
    "unlabelled_trailing_constant": {"constant": "A1744", "value": 1.0, "provenance": "retail_alias", "note": "referenced by gameScene.pyd; role UNKNOWN, candidate gravity_multiplier"},
    "model": {"constant": "PROJECTILE_MINE_MODEL", "value": "projectilemine", "provenance": "retail_literal"},
    "kv6_load_offset": {"constant": "PROJECTILE_MINE_MODEL", "value": [6, -9, 0.0], "provenance": "retail_literal"},
    "accuracy": {"constant": "A1720", "value": 0.01, "provenance": "retail_alias"},
    "recoil_up": {"constant": "A1721", "value": -0.15, "provenance": "retail_alias"},
    "damage_type": {"constant": "MINE_LAUNCHER_DAMAGE", "value": 40, "provenance": "retail_named"},
    "kill_type": {"constant": "MINE_KILL", "value": 35, "provenance": "retail_named"},
    "crater_radius": {"constant": null, "value": 1, "provenance": "battlespades"},
    "team_only": {"constant": null, "value": true, "provenance": "battlespades"},
    "lifetime": {"constant": null, "value": null, "provenance": "absent"},
    "fuse": {"constant": null, "value": null, "provenance": "absent"},
    "touch_radius": {"constant": null, "value": null, "provenance": "absent"},
    "light_radius": {"constant": null, "value": null, "provenance": "absent"}
  },
  "38": {
    "type_id": {"constant": "UNKNOWN_ENTITY9", "value": 38, "provenance": "battlespades", "note": "retail never names slot 38; C4_ENTITY=38 is our identification"},
    "ammo": {"constant": "A1745/A1746/A1747", "value": {"max_ammo": 2, "initial_ammo": 2, "max_clip": null, "initial_stock": null, "restock": 1}, "provenance": "retail_alias"},
    "shoot_interval": {"constant": "A1748", "value": 1.0, "provenance": "retail_alias"},
    "blast_radius": {"constant": "A1749", "value": 8, "provenance": "retail_alias", "inferred_slot": true},
    "blast_damage": {"constant": "A1750", "value": 300.0, "provenance": "retail_alias", "inferred_slot": true},
    "block_damage": {"constant": "A1751", "value": 7, "provenance": "retail_alias", "inferred_slot": true},
    "knockback_max": {"constant": "A1752", "value": 0.15, "provenance": "retail_alias", "inferred_slot": true},
    "knockback_min": {"constant": "A1753", "value": 0.1, "provenance": "retail_alias", "inferred_slot": true},
    "placement_far_radius": {"constant": "A1754", "value": 5.0, "provenance": "retail_alias"},
    "model_size": {"constant": "A1755", "value": 0.06, "provenance": "retail_alias", "inferred_slot": true},
    "health": {"constant": "A1756", "value": 1, "provenance": "retail_alias", "inferred_slot": true},
    "model_z_offset": {"constant": "A1757", "value": -0.2, "provenance": "retail_alias"},
    "held_model_size": {"constant": "C4Weapon.model_size", "value": 0.04, "provenance": "retail_literal"},
    "model": {"constant": "C4_VIEW_MODEL", "value": "c4", "provenance": "retail_literal"},
    "held_model": {"constant": "C4_MODEL", "value": "c4_detonator", "provenance": "retail_literal"},
    "live_charge_cap": {"constant": "A1745", "value": 2, "provenance": "battlespades", "note": "number is retail ammo stock; the live-entity cap mechanism is ours"},
    "default_face": {"constant": "C4Weapon.face", "value": null, "provenance": "retail_literal", "dead": true},
    "per_face_offset_table": {"constant": null, "value": {"0": [0.0, 0.5, 0.5], "1": [1.0, 0.5, 0.5], "2": [0.5, 0.0, 0.5], "3": [0.5, 1.0, 0.5], "4": [0.5, 0.5, 0.0], "5": [0.5, 0.5, 1.0]}, "provenance": "retail_literal"},
    "per_face_rotation_table": {"constant": null, "value": {"0": [90, 0, 0, 1], "1": [-90, 0, 0, 1], "2": [-90, 1, 0, 0], "3": [90, 1, 0, 0], "4": [0, 0, 0, 0], "5": [180, 1, 0, 0]}, "provenance": "retail_literal"},
    "damage_type": {"constant": "C4_DAMAGE", "value": 41, "provenance": "retail_named"},
    "kill_type": {"constant": "C4_KILL", "value": 36, "provenance": "retail_named"},
    "crater_radius": {"constant": null, "value": 2, "provenance": "battlespades"},
    "fuse": {"constant": null, "value": null, "provenance": "absent"},
    "arm_delay": {"constant": null, "value": null, "provenance": "absent"},
    "trip_radius_horizontal": {"constant": null, "value": null, "provenance": "absent"},
    "lifetime": {"constant": null, "value": null, "provenance": "absent"},
    "water_legal": {"constant": null, "value": null, "provenance": "absent"},
    "light_radius": {"constant": null, "value": null, "provenance": "absent"}
  },
  "39": {
    "type_id": {"constant": "UNKNOWN_ENTITY10", "value": 39, "provenance": "retail_named"},
    "autonomous_behaviour": {"constant": null, "value": false, "provenance": "absent"},
    "shoot_interval": {"constant": "A1881", "value": 1, "provenance": "retail_alias"},
    "damage": {"constant": "A1882", "value": 2, "provenance": "retail_alias"},
    "damage_absorption_percent": {"constant": "A1883", "value": 50, "provenance": "retail_alias", "inferred_slot": true},
    "knockback": {"constant": "A1884", "value": 0.5, "provenance": "retail_alias", "inferred_slot": true},
    "model_size": {"constant": "A1885", "value": 0.06, "provenance": "retail_alias", "inferred_slot": true, "note": "weakest label - A1885 absent from the client name table"},
    "arm_pitch_min": {"constant": "A1886", "value": -80, "provenance": "retail_alias"},
    "arm_pitch_max": {"constant": "A1887", "value": 0, "provenance": "retail_alias"},
    "model": {"constant": "RIOTSHIELD_MODEL", "value": "riotshield", "provenance": "retail_literal"},
    "kv6_load_offset": {"constant": "RIOTSHIELD_MODEL", "value": [0.5, -11.5, -4.0], "provenance": "retail_literal"},
    "damage_type": {"constant": "RIOTSHIELD_DAMAGE", "value": 36, "provenance": "retail_named"},
    "kill_type": {"constant": "TOOLS_KILL_TYPE[RIOTSHIELD_TOOL]", "value": 0, "provenance": "retail_named"},
    "health": {"constant": null, "value": null, "provenance": "absent"},
    "model_z_offset": {"constant": null, "value": null, "provenance": "absent"},
    "lifetime": {"constant": null, "value": null, "provenance": "absent"},
    "fuse": {"constant": null, "value": null, "provenance": "absent"},
    "team_only": {"constant": null, "value": null, "provenance": "absent"}
  }
}
```
