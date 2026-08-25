# Entity port recovery

> **STATUS: Stage 1-3 landed.** The catalog, the local entity runtime, the F7
> spawn menu and per-entity behaviour are implemented and shipping. The exact
> per-field constant table, with adversarial verification and provenance, now
> lives in `ENTITY_CONSTANTS.md` — read that for numbers, and this document for
> the architecture and the reasoning behind it.
>
> Corrections applied since this plan was written:
> * `HEALTHCRATE_HP` is **not** the health-crate heal amount. It is the
>   twenty-first entry of the kill-type `xrange(37)` at `constants.py:1146` and
>   equals 20 by position alone. Retail ships no heal amount at all; we use the
>   reference server's literal `+20 only below full`.
> * `GRAVE_DAMAGE` is likewise an enum ordinal (14), not damage. Grave damage is
>   `GRAVE_EXPLOSION_DAMAGE = 25`.
> * Dynamite radius 8 is now corroborated by `shared/backup/constants-copy.py`,
>   an A-number-only dump with no named block to contradict it.
> * `ROCKET_TURRET_TOLERANCE` is an angular RATE threshold in retail (x10, for
>   the aim sound), not the error gate we use it as.
> * No radar-station sound ships; the entity is deliberately silent.

Complete inventory of every retail entity (40 ids), recovered from the retail
source and the BattleSpades server, with every model path checked against the
shipped asset tree.

Five entities are NOT PORTABLE because no art ships for them -- including one,
MACHINE_GUN, for which RETAIL ITSELF ships none. Do not substitute models.

One hazard to read before writing any serialisation: BASE (id 1) is absent from
retail GameScene.ENTITIES and freezes a clean client with KeyError. It may be
rendered locally but must never be sent.

Scope when recovered: LOCAL simulation inside TutorialWorldSession. Network
packet parity deliberately deferred.

# Implementation plan: port every retail entity into the local Training simulation

Scope per the user: exhaustive inventory, a debug spawner for each, and working **local** behaviour. Network packet parity is explicitly out of scope; everything runs inside `TutorialWorldSession` so it works under `BattleSpadesClient.exe --tutorial-map TokyoNeon --tutorial-skydome Tokyo.txt`.

---

## A. THE ENTITY INVENTORY

The authoritative table is one 40-value tuple: `G:/AoSRevival/aceofspades_source/shared/constants.py:2801`. Ids 30–39 are `UNKNOWN_ENTITY1..10` in retail and are resolved at `G:/AoSRevival/BattleSpades/shared/constants.py:3399-3408`. All ids already exist in our client at `G:/AoSRevival/BattleSpadesClient/include/battlespades/shared/retail_constants.hpp` (e.g. `:62 AMMO_CRATE = 3`, `:836 HEALTH_CRATE = 4`, `:130 BLOCK_CRATE = 5`).

Model column: only paths I verified on disk under `G:/AoSRevival/BattleSpadesClient/assets/original/kv6/`. Bindings from `G:/AoSRevival/aceofspades_source/aoslib/models.py`.

| id | Name | Model (verified on disk) | Category | Behaviour |
|---|---|---|---|---|
| 0 | FLAG | **NOT PORTABLE** — no `load_model` for it anywhere in `models.py` (grep returns nothing) | objective marker | Used only as an invisible indicator anchor (`aceofspades_source/server/aosmodes/dia.py:24`). No art exists; do not invent one. |
| 1 | BASE | `kv6/cp.kv6` — *unconfirmed binding* (`models.py:339 CP_MODEL`; `server/aosserver/types.py:240 class CommandPost: type = BASE`) | structure | Team base volume. **DO NOT WIRE LATER**: BASE=1 is absent from retail `GameScene.ENTITIES` and freezes a clean client with `KeyError: 1` (`BattleSpades/docs/HANDOFF.md:740-744`, enforced `BattleSpades/server/entities/registry.py:72-79`). Locally renderable; never serialize. |
| 2 | HELICOPTER | **NOT PORTABLE** — no model (`grep -i helicopter models.py` → nothing) | vehicle | Class string exists in `gameScene.pyd.strtab.json` only. |
| 3 | AMMO_CRATE | `kv6/ammocrate.kv6` (`models.py:344`) | pickup | Proximity 2.5, partial ammo top-up, consume + respawn. |
| 4 | HEALTH_CRATE | `kv6/healthcrate.kv6` (`models.py:343`) | pickup | Proximity 2.5, heal, consume + respawn. |
| 5 | BLOCK_CRATE | `kv6/block_crate.kv6` (`models.py:345`) | pickup | Proximity 2.5, refill block wallet to class max. |
| 6 | JETPACK_CRATE | `kv6/jetpack.kv6` — **UNCONFIRMED**; there is no `JETPACK_CRATE_MODEL` in `models.py` and no `jetpackcrate.kv6` on disk | pickup | Refills jetpack fuel. Network `Restock(69,type=6)` restores the roster's raw retail fuel units to 100; the HUD divides by the retail 100-unit maximum. |
| 7 | MACHINE_GUN | **NOT PORTABLE** — retail itself ships no art: `aoslib/weapons/mgWeapon.py:38-41` declares `model = []`, `view_model = []`, `entity_model = []`, `image = None` | deployable | Mounted MG. Behaviour is fully recovered; art is not. Defer or give it an explicit placeholder the user signs off on. |
| 8 | ROCKET_TURRET | `kv6/Turret_base.kv6` + `kv6/Turret_ball.kv6` + `kv6/Turret_gun.kv6` (`models.py:378-380`) — **3 parts, independent rotations** (`aoslib/scenes/main/rocketTurret.py:84-86`) | deployable | Autonomous: acquires ≤30 m, tracks ≤50 m, 180°/s, fires every 1.5 s at ≤0.1° error, 10 rockets. |
| 9 | LANDMINE | `kv6/landmine.kv6` (`models.py:315`) | deployable | 4 s arm, 2.5 m radial + 3-layer vertical trip, 100 dmg, 1 HP. |
| 10 | DYNAMITE | `kv6/dynamite.kv6` (`models.py:317`) | deployable | 7 s fuse, 300 dmg, radius **8** (see numbers note below). |
| 11 | GRAVE | `kv6/grave.kv6` (`models.py:349`, offset 0,0,11) | hazard | 7 s fuse then 25 dmg / r=3. |
| 12 | CORPSE | `kv6/ClassicCorpse.kv6` — *probable* (`models.py:388 CLASSIC_CORPSE_MODEL`; no explicit CORPSE_ENTITY binding) | hazard | Fuse 0 (1.0 s with jetpack), 0 player dmg, 1 block dmg. |
| 13 | FLARE_BLOCK | *no KV6* — it is a **terrain voxel plus a light**, tool model is the ordinary `kv6/block.kv6` (`aoslib/weapons/flareBlockTool.py:15`) | terrain-backed | 10 blocks cost, 5 HP, r=5.0 light, destroyed when unsupported. |
| 14 | BOMB_PICKUP | `kv6/Bomb.kv6` (`models.py:289`, entity offset 0,0,7) | objective | Carry via BOMB tool (id 25), throw at 10 u/s. |
| 15 | DIAMOND_PICKUP | `kv6/diamond.kv6` (`models.py:293`, entity offset 0,0,7) | objective | 60 s lifetime, carry via DIAMOND tool (26), throw at 15. |
| 16 | INTEL_PICKUP | `kv6/intel.kv6` (`models.py:342`, entity model has **no** offset) | objective | Enemy-only pickup at 3.0, burdens carrier, throw at 15, 60 s auto-return. |
| 17 | AIRSTRIKE | `kv6/airstrike_bomb.kv6` — *unconfirmed*; only `AIRSTRIKE_BOMB_VIEW_MODEL` is bound (`models.py:358`) | hazard | 400 dmg / r=6, shell speed 100. |
| 18 | AMMO_DROP_POINT | `kv6/Crate_Target.kv6` (+0,0,1.0) (`models.py:347`) | marker | Airdrop target decal; renders baseplate+crate pair in UGC (`models.py:448-456`). |
| 19 | HEALTH_DROP_POINT | `kv6/Crate_Target.kv6` (`models.py:346`) | marker | As above. |
| 20 | BLOCK_CRATE_DROP_POINT | `kv6/Crate_Target.kv6` (`models.py:348`) | marker | As above. |
| 21 | ROCKET | `kv6/rocket.kv6` (`models.py:305`) | projectile | **Already simulated** as `TutorialProjectile` (`native_frontend_module.cpp:127`). |
| 22 | ROCKET2 | `kv6/rocket2.kv6` (`models.py:306`) | projectile | Already covered (`native_frontend_module.cpp:128-129`). |
| 23 | DRILL | `kv6/drill.kv6` (`models.py:313`) | projectile | Already covered (`:130-131`). |
| 24 | SNOWBALL | `kv6/snowball.kv6` (`models.py:310`) | projectile | Already covered (`:132-133`). |
| 25 | CAPTURE_POINT | `kv6/cp.kv6` (`models.py:339`) | structure | Touch radius 3.0, restock throttle (see contested numbers). Shares one mesh with `pickup.kv6`/`ugc_base_zone.kv6` (identical md5 `5b68f3eb…`). |
| 26 | TANK | **NOT PORTABLE** — no model | vehicle | — |
| 27 | MOLOTOV | `kv6/Weapon_Molotov.kv6` (`models.py:390`) | projectile | Already covered (`native_frontend_module.cpp:135`). Its *product* is BLOCKFIRE. |
| 28 | BLOCKFIRE | *no model* — light + particles only | hazard | 2.5 dmg/0.3 s to players, 0.7/0.4 s to blocks, 4 s lifespan, spreads 5 within r=2.0 every 0.5 s. |
| 29 | UGC_ENTITY | `kv6/ugc_baseplate.kv6` + per-item pair (`models.py:394-396`, table at `:448-456`) | marker | Authoring markers: spawn/base zones (`ugc_spawn_zone.kv6`, `ugc_base_zone.kv6`), drop points. |
| 30 | MEDPACK | `kv6/MedPack.kv6` (`models.py:407`) | deployable | Team-only heal on touch at 3.0, N uses. |
| 31 | BLOCK_GOO | **NOT PORTABLE** — no model | deployable | Class string only. |
| 32 | CHEMICAL_BOMB | `kv6/chemicalbomb.kv6` (`models.py:392`) | projectile | Already covered (`native_frontend_module.cpp:136`). |
| 33 | GL_GRENADE | `kv6/grenade.kv6` — *inferred*; our client already uses it for tool 55 (`native_frontend_module.cpp:134`) | projectile | Already covered. |
| 34 | STICKY_GRENADE | `kv6/stickygrenade.kv6` (`models.py:413`) | projectile | Already covered (`:137`). |
| 35 | ATTACHED_STICKY_GRENADE | `kv6/stickygrenade.kv6` | hazard | The stuck form; our `TutorialProjectileBehavior::stick` already models it (`tutorial_session.hpp:64`). |
| 36 | RADAR_STATION | `kv6/radar_station.kv6` (`models.py:419`) | deployable | 45 HP, 250 s lifetime, 45 range. |
| 37 | PROJECTILE_MINE | `kv6/projectilemine.kv6` (`models.py:417`) | deployable | Already fired as a projectile (`native_frontend_module.cpp:131`); the *landed* mine is new. |
| 38 | C4 | `kv6/c4.kv6` (`C4_VIEW_MODEL`, `models.py:425`) — the held detonator is the separate `kv6/c4_detonator.kv6` | deployable | 2 live max, no fuse, secondary detonates, 300 dmg / r=8, sticks to face 0..5. |
| 39 | RIOT_SHIELD | `kv6/riotshield.kv6` (`models.py:409`) | deployable | Carried shield. |

**Verified missing: 5 entities.** FLAG(0), HELICOPTER(2), MACHINE_GUN(7), TANK(26), BLOCK_GOO(31) have no shipping model and none can be recovered from `models.py`. Do not substitute art for them. Everything else has a file on disk — I checked all 33 candidate paths and every one resolved.

**Two contested numbers to decide before coding (do not silently pick one):**
1. **Health crate heal.** Retail-derived reference server: `+20 only when hp < 100` (`aceofspades_source/server/aosmodes/__init__.py:103-107`). Our authoritative server: heal to `MAX_HEALTH` (`BattleSpades/server/map_resources.py:83-87`). Recommend retail semantics with a debug toggle.
2. **Dynamite.** The named block says `DYNAMITE_STOCK = 3` / `DYNAMITE_EXPLOSION_RADIUS = 5` (`constants.py:6578,6583`), but the A-alias block the client actually indexes says `A1627 = 1` / `A1632 = 8` (`constants.py:3881-3894`), and `dynamiteWeapon.py:25` binds `ammo = (A1627, A1628, None, None, A1629)`. **8 is correct.** Our catalog currently carries the wrong 5.0 at `src/world/weapon_catalog.generated.cpp:1958` — fix it in the generator.

Also flag as **not retail data**: medpack heal 25 / uses 3 exist only in `BattleSpades/shared/constants.py:7311-7312`; the rocket-turret muzzle speed 75.0 and the +90° upper pitch clamp are BattleSpades inventions (`BattleSpades/server/rocket_turret.py:27,163-175`).

---

## B. THE SHARED ENTITY SYSTEM

One system, not 35. Model it exactly on `TutorialProjectile` — a POD struct in a vector owned by the session, advanced by one private update, exposed as a `std::span` for the frontend to lease GPU slots against.

### B1. Data: a generated catalog

Follow the established pattern (`tools/generate_weapon_catalog.py`, `generate_class_catalog.py`, `generate_class_voice.py`; contract-checked in CI at `tests/CMakeLists.txt:218-242`). **Never hand-edit generated output.**

New `tools/generate_entity_catalog.py` → `src/world/entity_catalog.generated.cpp` + `include/battlespades/world/entity_catalog.hpp`, evaluating `aceofspades_source/shared/constants.py` and `aoslib/models.py` from Python 3 the way `generate_class_voice.py` does. One row per id 0..39:

```
struct EntityModelPart { std::string_view kv6; std::array<float,3> offset; };
struct EntityDefinition {
    std::uint8_t   type_id;          // 0..39
    std::string_view symbolic_name;  // "AMMO_CRATE"
    EntityCategory category;         // pickup/objective/deployable/hazard/marker/structure/projectile/unportable
    std::span<const EntityModelPart> parts;   // empty == no art
    float  model_size;               // ROCKET_TURRET_MODEL_SIZE 0.06, LANDMINE 0.05, RADAR 0.03 …
    float  touch_radius;             // CRATE_DISTANCE 2.5 / PICKUP_DISTANCE 3.0 / CAPTURE_POINT_DISTANCE 3.0
    float  health, fuse, arm_delay, lifetime;
    float  blast_radius, blast_damage, block_damage, crater_radius;
    std::int16_t damage_type, kill_type;
    std::string_view sound_place, sound_trigger, sound_trigger_water, minimap_icon;
    bool   team_tinted, needs_operator, wire_safe;   // wire_safe=false for FLAG(0)/BASE(1)
};
[[nodiscard]] const EntityDefinition* find_entity_definition(std::uint8_t type_id) noexcept;
[[nodiscard]] std::span<const EntityDefinition> entity_catalog() noexcept;
```

Seed `wire_safe = false` for BASE(1) (live-verified freeze) and FLAG(0) (defensive — the FLAG half of that claim is *not* proven; only BASE was measured). The `parts` span being empty is the single source of truth for "NOT PORTABLE / no art", which section F's asset test then enforces.

Register in `src/CMakeLists.txt` next to `world/class_catalog.generated.cpp:59` and add a `--check` contract test mirroring `tests/CMakeLists.txt:224-241`.

### B2. Runtime: `world/local_entity.{hpp,cpp}`

Renderer-free and platform-free, exactly like `terrain_effects.hpp`. New file pair, added to `src/CMakeLists.txt` after `world/tutorial_session.cpp:79`.

```
struct LocalEntity final {
    std::uint64_t id{};
    std::uint8_t  type{};          // retail id, indexes the catalog
    Vec3  position{}, velocity{}, home{};
    float yaw{}, pitch{};
    std::uint8_t team{}, owner{}, face{4U};
    double health{}, fuse{}, armed_at{}, expires_at{}, respawn_at{};
    std::uint16_t ammo{}, uses{};
    std::optional<std::uint8_t> target;
    bool alive{true};
    double next_support_check{};    // 10 Hz, per BattleSpades PickupCrateBehavior
};
```

On `TutorialWorldSession`, mirroring the projectile members at `tutorial_session.hpp:403-404`:

```
std::vector<LocalEntity> entities_;
std::uint64_t next_entity_id_{1U};
std::vector<EntityEvent> entity_events_;    // consumed once, like terrain_impacts_
```

API on the session, mirroring `projectiles()` at `tutorial_session.hpp:219`:

```
std::uint64_t spawn_entity(std::uint8_t type, Vec3 position, std::uint8_t team = 0U, std::uint8_t face = 4U);
bool despawn_entity(std::uint64_t id) noexcept;
void clear_entities() noexcept;
[[nodiscard]] std::span<const LocalEntity> entities() const noexcept;
[[nodiscard]] std::vector<EntityEvent> take_entity_events();   // sound + FX + HUD cues
```

`update_entities()` is declared next to `update_projectiles()` (`tutorial_session.hpp:333`) and called from `tick()` right beside it (`src/world/tutorial_session.cpp:773`). One pass, five phases, in this order:

1. **Respawn** — `alive == false && now >= respawn_at` → re-arm at `home`.
2. **Settle** — at 10 Hz only, if the supporting voxel is gone, walk the column down to the next solid and drop. Port of `BattleSpades/server/entities/behaviors.py:112,120-195`. This is what stops a crate floating when the player digs under it in Training.
3. **Timers** — fuse / arm delay / lifetime; expiry routes to `detonate()` or `expire()`.
4. **Per-type behaviour** — a `switch` on category, then on type. Turret targeting, mine trip test, medpack/crate touch test, MG mount.
5. **Effects** — every blast goes through one shared `detonate(const LocalEntity&, const EntityDefinition&)` that reuses `damage_voxel()` (`tutorial_session.hpp:346`), `collapse_unsupported_components` and `terrain_impacts_.push_back` exactly as `explode_projectile()` already does at `src/world/tutorial_session.cpp:1010-1051`. Craters, falling structures and explosion VFX then come free.

**Player damage does not exist today.** `explode_projectile` only touches voxels (`tutorial_session.cpp:1027-1050` — no player term anywhere). `detonate()` must add the player falloff pass, and section D1 adds the health it writes into.

### B3. Renderer slots — the budget, and what happens at exhaustion

Current map (`include/battlespades/render/world_renderer.hpp:175-183`):

| range | owner |
|---|---|
| 0..23 | debug |
| 24..63 | `terrain_effect_slot_base{24}` / `count{40}` |
| 64..95 | `projectile_slot_base{64}` / `count{32}` |
| 96..1247 | 128 remote-player rigs, `slot_base{96}` × `slot_stride{9}` |
| 1248..1279 | nominally free |

**Do not take 1248..1279.** The rig slot is computed from the raw `std::uint8_t` player id — `base = 96 + player_id * 9` (`src/frontend/native_frontend_module.cpp:5438-5441`) — with no clamp, so player id 128 lands exactly on 1248 and ids ≥ 132 already run past `world_model_slot_count` entirely. That is a latent bug; do not build on top of it.

**Do this instead:** raise `world_model_slot_count` from `1280U` to `1536U` and add

```
static constexpr std::uint32_t entity_slot_base{1280U};
static constexpr std::uint32_t entity_slot_count{256U};
```

Cost is 256 more `ModelSlot` entries in the fixed array at `src/render/world_renderer.cpp:477` — CPU handles only, zero GPU cost while empty. 256 slots because the turret needs **3 slots per instance** (`rocketTurret.py:20`) and UGC drop points need 2; budget per *part*, not per entity. That is ~85 turrets or 256 crates concurrently — far beyond anything the debug spawner will place.

Lease them by cloning `sync_projectile_meshes()` (`native_frontend_module.cpp:5308-5362`) into `sync_entity_meshes()`, keyed by `(entity_id, part_index)` packed into the existing `std::map<std::uint64_t, std::uint32_t>` shape (`:895`), plus `entity_draws()` cloned from `projectile_draws()` (`:5364-5392`). Clear it alongside `projectile_slots.clear()` at `:4625`.

**On exhaustion:** the projectile allocator silently `break`s when no slot is free (`:5346-5348`), which is right for a transient. For entities, silent invisibility is a debugging trap. Instead: refuse the *spawn* in `TutorialWorldSession::spawn_entity` when `entities_.size() * max_parts` would exceed the band, return id 0, and surface `"entity slots exhausted (N/256)"` in the debug HUD line. The simulation stays correct and the user is told why nothing appeared.

---

## C. THE DEBUG SPAWNER

The `gameplay_debug` lab (F10, `native_frontend_module.cpp:7420-7429`) is a **model preview lab with no world** — `GameplayDebugLab` owns only a `PlayerInventory` (`include/battlespades/world/gameplay_debug_lab.hpp:77`). Entities need a live map and a camera. So the spawner lives in the **tutorial world screen**, next to the existing F3/F4/F5/F6 developer affordances. That is also the only place that works under `--tutorial-map`.

### C1. Insertion points

**Key routing** — `src/frontend/native_frontend_module.cpp:7513`, immediately after the F6 case, inside the `screen() == FrontendScreen::tutorial_world && !event.repeated` block:

```
// F7 / SHIFT+F7  cycle the selected entity type (catalog order, skipping unportable)
// F8            spawn the selected entity at the crosshair
// SHIFT+F8      spawn one of every portable entity in a labelled row
// F9            clear all spawned entities
```

Add the scancode constants beside `scancode_f6{63U}` at `native_frontend_module.cpp:113`: `scancode_f7{64U}`, `scancode_f8{65U}`, `scancode_f9{66U}`. (F10/F12 at `:114-115` are already taken by the lab and the parity catalog; F7–F9 are unbound — verified by grep.)

**Spawn position** — reuse the existing crosshair resolver rather than inventing one: `TutorialWorldSession::placement_position(double range)` (`include/battlespades/world/tutorial_session.hpp:224`, implemented `src/world/tutorial_session.cpp:1724`) already returns the adjacent solid-face target used by block/prefab/deployable placement. Spawn at `placement_position(8.0)`; if it returns nothing solid, fall back to eye position + 3 × forward so the entity still appears in mid-air and the user sees the miss.

**Selection state** — put it on the session, not the frontend, so it is unit-testable: `debug_selected_entity_type()`, `debug_cycle_entity_type(int)`, alongside `debug_cycle_class` (`tutorial_session.hpp:270-271`).

**Row spawn** (`SHIFT+F8`) — mirror `spawn_all_classes()` (`gameplay_debug_lab.hpp:46`): lay one of every portable entity along the player's right vector at 3-unit spacing, all settled onto terrain, so a single screenshot shows the whole set.

### C2. On-screen readout

Extend the tutorial diagnostics overlay, which already draws a keybind line at `src/frontend/native_frontend_module.cpp:6616` (`"F4 ALL WEAPONS   F5/SHIFT+F5 CLASS   WHEEL SELECT   R RELOAD"`). Append:

```
F7 ENTITY <NAME>  F8 SPAWN  SHIFT+F8 ALL  F9 CLEAR   [n/256 slots]
```

and a second line for the nearest live entity: `type · health · fuse/arm remaining · armed? · target id · ammo · respawn in`. That per-entity readout is what makes turret tracking, mine arming and crate respawn testable without a second player. It is toggled by the existing F3 flag (`:929 tutorial_diagnostics_visible`).

### C3. Works in both maps

`begin_tutorial()` (`native_frontend_module.cpp:4642-4702`) loads Training by default and swaps in `--tutorial-map` at `:4657-4665`; the session is identical either way. Because the spawner is keyed off `FrontendScreen::tutorial_world` and spawns relative to the camera, it works unchanged in TokyoNeon. Reset `entities_` in `teardown_tutorial()` next to `projectile_slots.clear()` (`:4625`).

---

## D. PER-ENTITY FUNCTIONALITY, in test-satisfaction order

### D0. PREREQUISITE — player health. We do not have one.

`TutorialWorldSession` has **no** player health: a case-insensitive grep of `include/battlespades/world/tutorial_session.hpp` returns zero matches, and the only `health` in `src/world/tutorial_session.cpp` is `constexpr int default_block_health{5};` (`:31`, used at `:1080,1085,1286,1288`) — voxel durability.

We *do* have a HUD display integer: `GameHudModel::health_{100}` (`include/battlespades/frontend/game_hud.hpp:126`), clamped 0..100 in `set_health` (`src/frontend/game_hud.cpp:192-194`), rendered at `:291`. Its only two callers are network-driven (`native_frontend_module.cpp:2447,2522`). The network roster also models it (`include/battlespades/network/protocol168_players.hpp:55 std::int16_t health{100};`).

**Add to the session, do not build a new HUD:**
- `double health_{100.0}` + `heal(double, HealSource)` + `apply_damage(double, std::uint8_t kill_type)` + `bool alive()`.
- Emit `EntityEvent{health_changed}` so the frontend calls the *existing* `game_hud.set_health()`.
- Use retail's `HEALTHCRATE_HP = 20` (`retail_constants.hpp:834`) as the heal source tag so the eventual networked SetHP path lines up.
- Clamp 0..100 (`INITIAL_HEALTH`, mirrored at `protocol168_players.hpp:55`).

`PlayerInventory`'s header already anticipates this: `include/battlespades/world/player_inventory.hpp:23` — *"Health/ammo/block pickups call distinct restock methods so an ammo crate can …"*.

### D1. Supply crates — ammo (3), health (4), block (5). *Highest payoff, lowest risk.*

**Trigger:** pure proximity, sphere of `CRATE_DISTANCE = 2.5` (`constants.py:2980`, ported `retail_constants.hpp:407`). No line of sight, no facing, no use key, and refill is **unconditional** — a full player still consumes the crate (`BattleSpades/server/entities/behaviors.py:104-111`, which explicitly lowered 3.0 → 2.5 because the wider radius let one walk-through eat both crates).

**Ammo (3):** fix the semantics first. Our `WeaponReplicationState::restock_ammunition()` (`src/world/weapon_state.cpp:124-129`) implements the *spawn* reset (`ammunition_[tool_id] = initial_ammo(definition)`) — that is retail's `else` branch, not the crate branch. Add `restock_from_ammo_crate()` implementing `aoslib/weapons/weapon.py:77-89` **with the correct predicate**:

```cpp
// retail: `if max_clip == 0 or max_clip == None` — max_clip is element[2], the RESERVE cap.
if (retail.reserve_capacity.value_or(0U) == 0U)
    magazine = std::min<int>(magazine + restock_amount, magazine_capacity);
else
    reserve  = std::min<int>(reserve  + restock_amount, reserve_capacity);
// tools (tool.py:175-180): count = min(count + count_restock_amount, maximum_count)
```

Getting this backwards silently breaks 9 catalog rows that have a magazine but no reserve — ROCKET_TURRET(16), LANDMINE(20), DYNAMITE(21), MOLOTOV(33), MEDPACK(51), CHEMICALBOMB(54), RADAR_STATION(56), STICKY_GRENADE(57), C4(59) — none of which would ever restock. All the data is already in `RetailAmmoTuning` (`include/battlespades/world/weapon_catalog.hpp:118-128`); no generator work needed. Then set the auto-reload flag when the held reloadable weapon ends with an empty magazine (`weapon.py:88-89`).

**Health (4):** `heal(20)` only when below 100 (retail reference) — with a debug toggle for the full-heal variant.
**Block (5):** existing `restock_blocks()` (`tutorial_session.hpp:159`) is already correct.
**Jetpack (6):** the entity-model binding remains unconfirmed, but the network
effect is implemented. `Restock(69,type=6)` restores the local roster replica
to 100 raw retail fuel units immediately; subsequent `WorldUpdate` fuel remains
authoritative. `GameHudModel` receives only the normalized fraction produced by
`retail_jetpack_fuel_fraction`, matching `HUD.draw_jetpack_hud`'s division by
the active pack's `JETPACK_MAX_FUEL` (100 for every shipping profile).

**Consume + respawn:** `alive = false`, `respawn_at = now + delay`. Retail `CRATE_SPAWN_DELAY = 25` (`constants.py:3040`); our server default is 15 (`BattleSpades/server/game_rules.py:338`). Make it a debug-adjustable field defaulting to 25.

**What the player sees/hears:** crate vanishes; 25-particle additive burst using the `CRATE_PICKUP_FX_*` block (14 of 16 constants already ported at `retail_constants.hpp:411-424`; the two missing booleans are both `False` in retail so nothing is lost) driven by the sprite sheet `assets/original/png/high/PickUp_Twinkle_anim_4x4.png` (verified present, 4×4 matches `NUM_FRAMES_X/Y = 4`); one-shot `crate.ogg` / `healthcrate.ogg` / `crate_blocks.ogg` (SOUND_IDs 13/14/15, `BattleSpades/server/audio.py:35-37`) — **all three verified on disk**. HUD ammo and health numbers move.

### D2. Landmine (9). *The most satisfying single entity to test.*

Place, walk away, walk back → boom. Numbers from the A-alias block (`constants.py:4048-4066`, which is what `landmineWeapon.py:25-26` binds): arm delay `A1798 = 4` s, trip radius `A1799 = 2.5` horizontal, `A1800 = 3` vertical layers, `A1802 = 100` damage, `A1803 = 15` block damage, blast r `A1796 = 3.0`, health `A1808 = 1` (any hit detonates it — `behaviors.py:421-424`), model size `A1807 = 0.05`, water placement legal (`A1810 = True`). Own team never trips it (`behaviors.py:391-419`); LOS is deliberately ignored so a re-buried mine still kills (`behaviors.py:359 ignore_player_los = True`). Blast centre is `(x+0.5, y+0.5, z-0.5)` (`behaviors.py:370-377`).

Sees/hears: `landmine_place.ogg` on placement, blinking arm state in the readout, then the existing explosion VFX + crater + falling structures via the shared `detonate()`, with `landmineexplode.ogg` / `landmineexplode_water.ogg` selected by `z >= MAP_Z - 2` (`rocketTurret.py:99-103`). All four files verified.

### D3. Rocket turret (8). *The showpiece — a thing that acts on its own.*

`constants.py:6551-6577` (double-confirmed by `A1600..A1626` at `:3854-3880`): stock 4 / initial 2 / restock 2, `SHOOT_INTERVAL 1.5`, `TRACKING_RANGE 50.0` (sticky target), `DETECTION_RANGE 30.0` (acquire), `TOLERANCE 0.1`°, `AIMING_SPEED 180`°/s, `LOWER_PITCH_LIMIT 30`, `MODEL_SIZE 0.06`, `HEALTH 100`, `AMMO 10`, rocket blast `50 / r=3 / 10 block dmg`, turret death blast `100 / r=3 / 15`. Per-part z offsets are `-3/-17/-11 × 0.06` (`A1612..A1614`).

Local targeting has no live enemies in Training, so **target the debug mannequins** already present in `tutorial_roster` (`native_frontend_module.cpp:896`) plus an explicit "target me" debug toggle — that makes it demonstrable solo.

Sees/hears: three-part model, ball+gun yaw together, gun adds pitch (`rocketTurret.py:84-86`); `turret_place.ogg` → `turret_aim_start` → looping `turret_aiming_lp` → `turret_aim_stop` gated on angular rate `> A1607 * 10` with a 0.2 s tolerance timer (`rocketTurret.py:64-83`); `turret_lockon`/`turret_lockoff` on target change (`:121-132`); on death, per part, 8 glow-block particles + a (96,96,96) 10/1.5/5.0 effect + `explode_display(part, 1.0, 5)` and `turret_explode(_water).ogg` (`:99-119`). All 8 turret sounds verified on disk. Ammo billboard in team colour within 20 m (`A1626`), `ENOUGH_AMMO_COLOR (255,228,0)` / `NOT_ENOUGH (204,28,24)` (`constants.py:199-200`).

**Flag to the user:** the targeting *algorithm* (sticky-first, nearest-by-squared-distance, +90° upper pitch clamp) and rocket speed 75.0 are BattleSpades inventions (`BattleSpades/server/rocket_turret.py:27,163-175,207-229`), not recovered retail.

### D4. Intel / diamond / bomb (16/15/14). *"Base intel diamond ALL".*

Carrying is **not** a separate visual system: the ground entity is destroyed and the carrier equips the matching tool (`aoslib/weapons/list.py:140-141 PICKUPS = {BOMB_PICKUP: BOMB_TOOL, DIAMOND_PICKUP: DIAMOND_TOOL, INTEL_PICKUP: INTEL_TOOL}`). Our client already has all three rows with the exact retail offsets — `src/world/weapon_catalog.generated.cpp:1962` (BOMB 25), `:1963` (DIAMOND 26), `:1967` (INTEL 30, `kv6/intel.kv6`, third-person `{6,-12,-8}` / first-person `{0,0,-6.8}` at `:86-102`), all `WeaponMechanism::objective`, which already dispatches `WeaponActionKind::objective_use` (`src/world/weapon_runtime.cpp:502-503`).

So: pickup at `PICKUP_DISTANCE = 3.0` (`constants.py:2983`) → despawn entity, `sandbox_inventory_.select_tool(30|26|25)`, set `player_.burdened = true` (`include/battlespades/world/player_movement.hpp:65`; sprint suppression already implemented at `src/world/player_movement.cpp:590`). Note there are three places that write `burdened` today (`tutorial_session.cpp:1861`, `:1960`, `:1963-1965`, the last two network-gated) — the local path must not be clobbered by the spawn reset at `:1861`.

Drop on `objective_use` at INTEL/DIAMOND 15, BOMB 10 u/s (`constants.py:4540,4551,4554`); 2.5 s re-pickup lockout (`NO_PICKUP_AFTER_DROP_TIME`, `constants.py:2992`); 60 s auto-return (`constants_gamemode.py:495`). Intel is tinted with the **other** team's colour (`intelTool.py:19 use_other_team_color = True`) and cannot be swapped away from while carried (`:39-43`).

Sees/hears: `bomb_pickup/bomb_drop/diamond_pickup/diamond_drop/diamond_appear/diamond_disappear/diamond_dropinbase/flag_returned.ogg` — all verified. Diamond gets its own 50-particle additive burst (`constants.py:5175-5191`, incl. `ALPHA_BLEND_MODE_ADDITIVE`). Intel deliberately has **no** light, glow or particle in retail — only its minimap icon (`assets/original/png/ui/minimap_intel.png`, verified) and the >4-block height indicator it shares with the capture point (`constants.py:5235-5236`). Note `intel_pickup.ogg` does **not** ship — the intel cue is the generic `classic_pickup.ogg` / event sounds; do not invent a file.

### D5. C4 (38) + dynamite (10). Fuse vs. remote.

Dynamite: 7 s fuse (`A1631`), 300 dmg (`A1633`), **radius 8** (`A1632`), 7 block dmg, 1 HP, z offset -0.2. `dynamite_place.ogg` → ticking `dynamite_tick.ogg` → `dynamiteexplode(_water).ogg`, all verified.

C4: no fuse, 2 live max enforced at placement (`BattleSpades/server/deployable_actions.py:202-208`), secondary detonates all (`c4Weapon.py:56-61 scene.send_detonate_c4()` → locally just a direct call), 300 dmg / r=8 (`BattleSpades/shared/constants.py:7282-7294`). Sticks to a chosen voxel face with offsets `{0:(0,.5,.5), 1:(1,.5,.5), 2:(.5,0,.5), 3:(.5,1,.5), 4:(.5,.5,0), 5:(.5,.5,1)}` (`behaviors.py:22-29`) and per-face rotations (`c4Weapon.py:72-89`). Sound stems ship as numbered variants — `C4_place_001..003.ogg`, `C4_explode_001..003.ogg`, `C4_detonate_001.ogg` — plus short-name duplicates `c4_place.ogg`/`c4explode.ogg`; pick one convention deliberately and encode it in the catalog.

### D6. Medpack (30) + radar station (36).

Medpack: team-only, skipped at full health, touch radius 3.0, N uses then removed (`behaviors.py:241-271`). Retail gives the *tool* 2 charges / initial 2 / ammo-crate restock +1 (`medPackWeapon.py:35` → `A1868=2, A1869=2, A1870=1`) and a 5.0 placement radius (`A1880`). **Heal 25 / uses 3 are BattleSpades guesses** — surface them in the readout as calibration candidates.

Radar: 45 HP, 250 s lifetime driven through the entity's `fuse` field so the model self-expires (`BattleSpades/server/deployable_actions.py:288-291`), 45 range, model size 0.03, z offset -0.55 (`A1893..A1902`). `RADAR_place_001.ogg` / `RADAR_ping_001.ogg` verified.

### D7. Flare block (13) + blockfire (28). Light and fire.

Flare block is the one entity that is *terrain*: costs 10 blocks (`FLAREBLOCK_COST`, `constants.py:4974`), 5 HP (`DEFAULT_BLOCK_HEALTH`), r=5.0 light. We already have the machinery — `place_block(bool emits_light)` (`tutorial_session.hpp:345`) registers into `StaticLightField` (`:239-241,325`) which the mesher bakes. Add the six-neighbour support rule (`BattleSpades/server/entities/flare_block.py:33-53`), **including its water carve-out** at the z=238 plane, or flare blocks on water are wrongly rejected.

Blockfire: 2.5 dmg / 0.3 s to players, 0.7 / 0.4 s to blocks, 4.0 s lifespan, spread 5 within r=2.0 every 0.5 s at ≤0.3 chance, colours HOT (255,255,255) → MID (255,255,0) → COLD (255,0,0), light r=3.0 (`constants.py:6609-6626`, `:4986`). Three independent timers — model them as three separate accumulators, not one. Its source is the molotov (`constants.py:6592-6608`), which our projectile path already throws.

### D8. Capture point (25), bases, spawn zones, drop points, markers.

Capture point renders `cp.kv6`. **Do not implement "base restock" from the constants:** `CAPTURE_POINT_DISTANCE = 3.0` and `CAPTURE_POINT_REFILL_TIME = 10.0` (`constants.py:2986-2989`) are read by **no code in either tree** (recursive grep excluding `constants.py`: zero hits). The only executable behaviour is a **3-second heal to 100 with the ammo lines commented out** — `aosmodes/cctf.py:142-144` and `aosmodes/ctf.py:83-85` call `player.restock()`, which resolves to `server/aosserver/connection.py:1128-1131 def restock(self): self.set_hp(100)`. Implement that, and note the constants as unused.

Bases and spawn points are **volumes, not entities**: there is no BASE entity worth wiring and no tent KV6 anywhere (`ls kv6 | grep -iE 'flag|tent'` → nothing). A base is authored VXL geometry + an axis-aligned box + a translucent zone at `BASE_ZONE_TINT_ALPHA = 150` (`constants.py:5051`) + a minimap icon pair. Spawn zones come in three fixed sizes (`constants.py:4291-4307`) with fixed team ownership (`:4333-4349`) and render `ugc_spawn_zone.kv6`. Our `resolve_map_spawn` is team-blind, probing outward from (256,256) (`include/battlespades/world/map_spawn.hpp:43-45`, `src/world/map_spawn.cpp:67-116`) — extend it with an optional zone list rather than replacing it. The UGC ids are **already ported** (`retail_constants.hpp:2050-2062`, and again as `RetailWeaponConstant` rows at `weapon_catalog.generated.cpp:1439-1447`), so only the volume table and selection logic are new.

Drop points (18/19/20) render `Crate_Target.kv6` at +0,0,1.0 and, in UGC, a baseplate+crate pair at scale 0.25 / z −0.5 (`models.py:448-456`).

### D9. Grave (11), corpse (12), airstrike (17), projectile mine (37), riot shield (39).

Pure fuse-then-blast, straight into the shared `detonate()`: grave 7 s / 25 dmg / r=3 (`constants.py:4516-4521`); corpse fuse 0, 0 player damage, 1 block damage (`:4487-4493`); airstrike 400 dmg / r=6 / 15 block (`:4560-4566`). Projectile mine's flight is already covered by `TutorialProjectile`; only the landed state is new. Riot shield is a carried prop with no autonomous behaviour — render it and stop there.

---

## E. STAGING

Six stages. Each compiles, each is testable, each is independently landable.

**Stage 1 — precisely: the crate.** Ship *one* entity end to end so the whole spine is proven.
1. `tools/generate_entity_catalog.py` + generated pair + `src/CMakeLists.txt:59` registration + the `--check` contract test.
2. `world/local_entity.{hpp,cpp}`; `entities_` vector, `spawn_entity`/`despawn_entity`/`clear_entities`/`entities()`; `update_entities()` called from `tick()` beside `update_projectiles()` (`src/world/tutorial_session.cpp:773`) with phases 1–3 only (respawn, 10 Hz settle, timers).
3. Player health on the session (D0) wired into the existing `GameHudModel::set_health`.
4. `WeaponReplicationState::restock_from_ammo_crate()` with the corrected predicate.
5. Renderer band: `world_model_slot_count 1280 → 1536`, `entity_slot_base{1280}`, `entity_slot_count{256}`; `sync_entity_meshes()` + `entity_draws()` cloned from `native_frontend_module.cpp:5308-5392`; cleared at `:4625`.
6. Debug spawner: F7 cycle / F8 spawn / SHIFT+F8 all / F9 clear at `native_frontend_module.cpp:7513`, readout appended at `:6616`.
7. Only three catalog rows enabled: AMMO_CRATE(3), HEALTH_CRATE(4), BLOCK_CRATE(5).

**Acceptance:** launch with `--tutorial-map TokyoNeon --tutorial-skydome Tokyo.txt`, press F8 three times, walk through each crate, watch ammo/health/blocks change, hear three distinct sounds, see the burst, watch them respawn after 25 s, dig underneath one and watch it settle.

**Stage 2 — deployables with fuses and blasts.** Shared `detonate()` with player falloff; landmine, dynamite, C4, grave, corpse, airstrike. This is where explosions start hurting the player.

**Stage 3 — autonomous and stateful.** Rocket turret (3-part rig, tracking, aim audio state machine), radar station, medpack, projectile mine.

**Stage 4 — objectives.** Intel/diamond/bomb carry-drop-return through the existing objective tools and the burden flag.

**Stage 5 — terrain-backed and volumes.** Flare block support rule, blockfire, capture point, base/spawn zones, drop-point markers, UGC markers.

**Stage 6 — polish and the deferred set.** Crate parachute airdrop (`CRATE_PARACHUTE_DEPLOYMENT_HEIGHT 10` → `SLOWDOWN 0.75` → `REMOVAL_HEIGHT 2`, `retail_constants.hpp:408-410,426`, with `cratedrop_freefall/chuteopen/land.ogg` — all verified), minimap icons, and a decision on the art-less mounted MG.

---

## F. TESTS, OFF-GPU

Follow `world/footstep_audio` and `world/class_voice`: the behaviour is free functions and PODs over explicit state, so it tests without a device. Register in `tests/CMakeLists.txt` next to `aos_footstep_audio_tests:180-185`.

**`tests/test_entity_catalog.cpp`** (links `BattleSpades::World`, `AOS_TEST_ASSET_ROOT`) — the asset-existence test, modelled on `test_map_catalog.cpp:52-70` which collects every failure and throws one report rather than dying on the first:
- Every `EntityDefinition` with a non-empty `parts` span resolves to a file that exists under `assets/original/kv6/`, and `Kv6Model::load_file` succeeds on it.
- Every non-empty `sound_place` / `sound_trigger` expands through `audio::sound_group_stems` (`include/battlespades/audio/sound_groups.hpp:22`) to `.ogg` files that exist. This is the check that would have caught `intel_pickup.ogg` not shipping.
- Every non-empty `minimap_icon` exists under `png/ui/`.
- The five NOT PORTABLE ids (0, 2, 7, 26, 31) have an **empty** `parts` span — pinning the negative result so nobody quietly substitutes art.
- `wire_safe == false` for BASE(1).
- Ids are dense 0..39 and the catalog is indexable by id.

**`tests/test_local_entity.cpp`** (links `BattleSpades::World`, no assets) — pure behaviour:
- Crate: outside 2.5 → no trigger; at 2.499 → trigger; refill fires even at full ammo; entity goes `alive=false` and returns at `now + delay`.
- Ammo restock: table-driven over the real catalog. Reserve-capacity weapons top the reserve; the 9 magazine-only rows (16/20/21/33/51/54/56/57/59) top the magazine. **This is the regression test for the inverted predicate.**
- Landmine: no trip before 4 s; no trip for own team; trips at 2.5 horizontal only within 3 vertical layers; detonates when destroyed.
- Turret: acquires within 30, holds a sticky target out to 50, drops past 50, yaw approaches at exactly 180°/s, fires only when both errors ≤ 0.1° and the 1.5 s interval has elapsed, stops at 0 ammo.
- Blockfire: the three timers advance independently; spread count and lifespan are bounded.
- Slot exhaustion: spawning past the band returns id 0 and leaves `entities_` unchanged.
- Fuse/lifetime/respawn arithmetic is exact at the fixed 1/60 dt.

**`tests/test_tutorial_session.cpp`** (existing, `:31-36`, already has `AOS_TRAINING_VXL`) — add integration cases: spawn a crate through the session API and confirm health/ammo/blocks move; confirm a blast damages the player and carves a crater; confirm `clear_entities()` empties both the vector and the event queue.

**Contract check** — add `aos_entity_catalog_contract_check` mirroring `tests/CMakeLists.txt:224-232` so a drift between the retail constants and the checked-in table fails CI.

---

## G. RISKS

**Renderer slot budget.** The documented "free" tail 1248..1279 is *not* free: `base = 96 + player_id * 9` with an unclamped `uint8_t` (`native_frontend_module.cpp:5438-5441`) maps player id 128 onto 1248 and ids ≥ 132 past the array end entirely. Taking that band would produce a corruption bug that only appears on a full server. Mitigation: raise `world_model_slot_count` to 1536 and site the entity band at 1280 (above every rig), and separately add a bounds guard on the rig index. Budget per **part** — the turret is 3 and UGC drop points are 2. Refuse the spawn and say so in the HUD when the band is full; never silently drop the mesh.

**16-voice audio pool.** `max_one_shot_voices{16U}` (`include/battlespades/audio/openal_frontend_audio.hpp:16`). `SHIFT+F8` spawning one of every entity, then a chain detonation, will blow straight through it and steal voices from gunfire and footsteps. Mitigations: (a) rate-limit identical entity cues to one per ~50 ms per type; (b) collapse simultaneous blasts within ~2 units into a single explosion voice; (c) route the turret aim loop through the existing loop-voice path (`update_weapon_loop`, `:202`) rather than one-shots, and hard-cap it to one live aiming loop; (d) never emit a cue for an entity outside audible range. Add an assertion in the debug readout showing `active_one_shot_voices()` (`:250`) so starvation is visible while testing rather than mysterious.

**Not painting ourselves into a corner on packet parity.** Four concrete guards:
1. **Type ids are the wire ids.** Key the catalog by the retail 0..39 id, never by a local enum. `CreateEntityPacket` already decodes the full record (`include/battlespades/network/protocol168_runtime.hpp:21-38`) — `LocalEntity`'s fields should be a superset of it (id, type, state, player_id, position, velocity, yaw, colour, radius, face, fuse, ugc_mode) so a future adapter is a field copy, not a redesign.
2. **`wire_safe`.** BASE(1) freezes a clean retail client with `KeyError: 1`. Carry the flag from day one so the later network stage physically cannot serialize it. FLAG(0) is defensively flagged, but be honest in the code comment that only BASE was live-measured.
3. **Keep authority seams named.** Split `spawn_entity` (local) from a future `apply_server_entity` so the local spawner is never mistaken for the authoritative path — exactly how `apply_authoritative_transform` (`tutorial_session.hpp:284`) and `config_.network_authoritative` already separate movement.
4. **Restock semantics diverge between retail and both servers.** The partial ammo top-up is a retail *client* behaviour; retail's own server calls `tool.restock()` untyped and takes the full-reset branch (`aosmodes/__init__.py:113`), and our server does `_reset_ammo()` (`BattleSpades/server/player.py:1529`). Keep `restock_ammunition()` (spawn reset) and `restock_from_ammo_crate()` (partial) as two distinct methods so the network path can pick per `Restock(69)` type without either behaviour being rewritten.

**Data provenance.** Three numbers in flight are BattleSpades inventions, not recovered retail: medpack heal 25 / uses 3, rocket speed 75.0, and the +90° turret pitch clamp. Two more are outright wrong in the named constants block (dynamite stock and radius) and wrong in *both* `shared/constants.py` files — worth telling Kiril, since the server side will disagree with a fixed client. Have the generator emit a `provenance` field per row (`retail_alias` / `retail_named` / `battlespades`) so the readout can mark a guessed number on screen instead of it hardening into fake parity.
