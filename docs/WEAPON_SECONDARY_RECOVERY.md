# Weapon Secondary / ADS Recovery

> **Recovery/specification reference.** Preserve the measured retail behavior and its evidence. Implementation updates, old build paths, test counts and session constraints below describe their original investigation; they are not current release or deployment status. Use the [maintained documentation index](README.md) for present operating instructions and recheck historical findings against current source.

Authoritative per-tool specification for right-mouse behaviour and aim-down-sights,
recovered from the retail decompile (`<retail-source>`) and from
`character.pyd` / `gameScene.pyd` via IDA. Every claim below carries a `file:line`
or a binary address. Anything that could not be recovered is in
[UNRECOVERABLE](#5-unrecoverable) — do not invent it.

Scope note: `../BattleSpades` (our Python 3 server) is **not** retail and
is never cited as authority here.

---

## 0. The dispatcher (read this before the table)

`aoslib.character.Character.use_weapon_secondary` — `character.pyd` body
`sub_100348B0` (arg wrapper `sub_1007C460`; qualname string `0x1008adfc`;
embedded `character.pyx` lines 1005–1019). Attribute names resolved from the
Cython `__Pyx_StringTabEntry` table, not guessed:

```
0x10098AD0 has_secondary   0x10097B04 can_zoom      0x10098CFC main
0x10097DDC sight           0x10098A98 shoot_secondary  0x10098728 zoom
0x100979CC set_zoom        0x10097EF8 can_shoot_secondary
0x10097F44 delay_secondary 0x10097C24 use_secondary
0x10097FB4 on_start_secondary  0x100989D8 is_active
```

```python
def use_weapon_secondary(self, weapon):
    if (not weapon.has_secondary) and weapon.can_zoom:      # 0x100348CF, 0x10034984
        if self.main:                                        # 0x100349E0
            if weapon.sight:                                 # 0x10034A3E
                self.shoot_secondary = False                 # 0x10034AC4
                self.set_zoom(0) if self.zoom else self.set_zoom(weapon.zoom)
        return
    if not weapon.can_shoot_secondary(): return              # 0x10034C9C
    if not weapon.delay_secondary:                           # 0x10034D36
        if not weapon.use_secondary(): return                # 0x10034DA2  (return value gates the chain)
        weapon.on_start_secondary()                          # 0x10034E37
    else:
        if weapon.is_active(): return                        # 0x10034E85
        weapon.on_start_secondary()                          # 0x10034F18
```

The `set_zoom(0)` argument is proven, not inferred: `dword_10097CC0 =
PyTuple_Pack(1, dword_10097994)` built at `0x10001C56–0x10001C64`, and
`dword_10097994 = PyInt_FromLong(0)` at `0x1006CD7A–0x1006CD81`.

**Five rules the current enum violates.**

1. **ADS is a four-term conjunction**, not a category:
   `(not has_secondary) AND can_zoom AND self.main AND sight != None`.
   `can_zoom` is load-bearing for exactly one weapon — the MINIGUN has a real
   `sight = MINIGUN_SIGHT` (`minigunWeapon.py:42`) and a real
   `minigun_sight.kv6` on disk, and only `can_zoom = False`
   (`minigunWeapon.py:50`) denies it a scope. "ADS iff sight != null" ships a
   scoped minigun.
2. **The two branches are mutually exclusive.** A tool has an alternate action
   *or* ADS, never both. One enum value cannot encode both.
3. **ADS is a press-consumed TOGGLE.** `shoot_secondary` is cleared inside the
   branch and `Character.update_weapon` (`sub_10034FF0`) contains no reference
   to `zoom`/`set_zoom` at all — release cannot un-zoom.
4. **Two distinct action shapes** split on `delay_secondary` (base default
   `False`, `tool.py:28`): immediate (`use_secondary()` truthiness gates
   `on_start_secondary`) vs deferred (`is_active()` gates it and
   `use_secondary` is never called on press).
5. **The held flag is a second channel.** Tools that fall through without
   entering the sight branch never get `shoot_secondary` cleared, so
   MG (`mgWeapon.py:300`), minigun (`minigunWeapon.py:134`) and prefab
   (`prefabTool.py:198`) poll it per frame. A port needs *both* an edge event
   and a held flag.

Input source is not "right mouse". `Character.set_secondary_shoot`
(`sub_10028680`, `character.pyx:728–733`) is driven from four sites in
`gameScene.pyd`: `on_mouse_press` `0x101568EC`, `on_mouse_release` `0x10157108`,
`on_key_press` `0x1015F4B3` (immediately after the only reference to the interned
name `'aim'`, `0x1015F41A`), and `stop_movement` `0x10165204`.
`aoslib/config.py:61` declares a bindable, default-unbound `'aim': None` control.

---

## 1. Per-tool table (ids 0–64)

Legend for **Secondary**: `NONE` = right-click has no observable effect;
`INERT-LOCK` = sets `active_secondary` only; the rest are named behaviours.
**Trigger**: `—` / `TOGGLE` (press flips state) / `HOLD` (polled per frame) /
`EDGE` (fires once per press) / `EDGE-RATE` (repeats while held, rate-limited) /
`CHARGE` (deferred to animation end, cancels on early release).

**Zoom** is a *multiplier*, never an FOV. The FOV is computed scene-side as
`fovy = 75.0 - 37.5 * zoom_level` (`gameScene.pyd sub_10114E50`,
`gameScene.pyx:258`, literals `75.0`/`37.5` inline; `gluPerspective(fovy,
w/(h or 1.0), 0.1, draw_distance)` at `:259`), where `zoom_level` ramps toward
`character.zoom`. Multiplier 1.0 → **37.5°**, 1.5 → **18.75°**, 1.2 → **30.0°**;
hip is 75.0°.

**Sensitivity** is `config.mouse_sensitivity * zoomed_sensitivity_factor` on
both axes, gated on the *instantaneous* `character.zoom`
(`gameScene.pyx:1242–1245`, `sub_10137220`). Base default 0.5 (`tool.py:46`).

**Overlay**: there is no 2D scope overlay anywhere in retail (see §4).

| id | Name (class) | Secondary | Trigger | Zoom (multiplier → FOV) | Sensitivity | Sight asset | Overlay | Provenance |
|---:|---|---|---|---|---|---|---|---|
| 0 | PICKAXE (PickAxeTool) | NONE — `Tool.use_secondary` returns None | — | — | — | none | none | pickAxeTool.py:12; tool.py:27,169-170 |
| 1 | KNIFE (KnifeTool) | NONE | — | — | — | none | none | knifeTool.py:12; tool.py:169-170 |
| 2 | SPADE (SpadeTool) | INERT-LOCK — `on_start_secondary` sets `active_secondary`; blocks `can_swap` for 1.0 s. **Not** the alternate dig | CHARGE (no effect) | — | — | none | none | spadeTool.py:17-18; tool.py:194-195,235-236 |
| 3 | SUPERSPADE (SuperSpadeTool) | NONE | — | — | — | none | none | superSpadeTool.py:12-31 |
| 4 | CLASSIC_SPADE (ClassicSpadeTool) | **ALTERNATE DIG, charged.** 0.8 s `AnimUseSpade`; fires when the animation *completes* (`update` → `on_stop_secondary` → `use_secondary` → `use_spade(True, secondary_damage)`). 3-block vertical column, 5 dmg/cell. Early release cancels | CHARGE | — | — | none | none | classicSpadeTool.py:17,18,21,24,43-47,59-79; gameScene.pyd sub_10081D90 |
| 5 | BLOCK (BlockTool) | BLOCK-LINE CANCEL. Gated on `manager.enable_colour_picker` | EDGE | — | — | none | none | blockTool.py:39-45; blockToolCommon.py:142-143 |
| 6 | RIFLE (ClassicRifleWeapon) | **IRON SIGHTS.** Swaps in `semi_sight.kv6` **plus** a second `pin` model. Only weapon with `UNZOOMED_CROSSHAIR` (reticle hidden while aimed). Only non-sniper with its own transition entry (2.5 / 6.0) | TOGGLE | 1.0 → 37.5° | 0.5 (base) | semi_sight.kv6 + semi_sight_pin.kv6 | none | classicRifleWeapon.py:27,32,33; models.py:141,230; constants.py:1256 |
| 7 | SMG (SMGWeapon) | IRON SIGHTS. `ring = SMG_RING` is declared but never drawn | TOGGLE | 1.0 → 37.5° | 0.5 | smg_sight.kv6 | none | smgWeapon.py:35,36,38,44 |
| 8 | MINIGUN (MinigunWeapon) | **SPIN-UP.** `can_zoom=False` routes to the action branch; `Tool.use_secondary` returns None so `on_start_secondary` never fires; the motor polls `character.can_shoot_secondary()` | HOLD | none (`can_zoom=False`) | — | minigun_sight.kv6 — **loaded but unreachable** | none | minigunWeapon.py:42,44,50,134-138 |
| 9 | SHOTGUN (ShotgunWeapon) | IRON SIGHTS | TOGGLE | 1.0 → 37.5° | 0.5 | shotgun_sight.kv6 | none | shotgunWeapon.py:36,43 |
| 10 | SHOTGUN2 (Shotgun2Weapon) | IRON SIGHTS | TOGGLE | 1.0 → 37.5° | 0.5 | Shotgun2_sight.kv6 (**casing mismatch**) | none | shotgun2Weapon.py:38,45; models.py:226 |
| 11 | GRENADE (GrenadeTool) | NONE — `has_secondary=False`, `sight=None`, zoom branch falls through | — | — | — | none | none | grenadeTool.py:21; tool.py:42 |
| 12 | RPG (RPGWeapon) — the bazooka | **IRON SIGHTS.** `sight=None` at :16 is overwritten by `sight=RPG_SIGHT` at :28 (later binding wins) | TOGGLE | 1.0 → 37.5° | 0.5 | rpg_sight.kv6 | none | rpgWeapon.py:16,28,29; models.py:298 |
| 13 | RPG2 (RPG2Weapon) | IRON SIGHTS (same double-assign pattern) | TOGGLE | 1.0 → 37.5° | 0.5 | RPG2_sight.kv6 | none | rpg2Weapon.py:16,28,29; models.py:301 |
| 14 | DRILLGUN (DrillgunWeapon) | IRON SIGHTS | TOGGLE | 1.0 → 37.5° | 0.5 | drillgun_sight.kv6 | none | drillgunWeapon.py:16,28,29; models.py:312 |
| 15 | MG (MGWeapon) | **DEPLOY / WITHDRAW.** Polls `weapon_custom or shoot_secondary`; works *because* `sight` is None so the flag is never cleared. Deployed forces `character.zoom = True` **directly**, bypassing `set_zoom` (no sound, no `on_zoom`) | TOGGLE (held-flag polled) | forced True (==1.0) → 37.5° | 0.5 (base) | none — `sight_pos` set but no `sight`, no `mg_sight.kv6` | none | mgWeapon.py:42,48,198-206,275-277,300; constants.py:3800-3801 |
| 16 | ROCKET_TURRET (RocketTurretWeapon) | NONE | — | — | — | none | none | rocketTurretWeapon.py:20 |
| 17 | PISTOL (PistolWeapon) | IRON SIGHTS. `ALWAYS_CROSSHAIR` | TOGGLE | 1.0 → 37.5° | 0.5 | pistol_sight.kv6 | none | pistolWeapon.py:30,34,40 |
| 18 | SNIPER (SniperWeapon) | **MAGNIFIED SCOPE.** `zoom_position_offset (0.325,0.25,0.0)`, `needs_zoom_arms_offset()=True`, `accuracy_zoom=0.0`. Does **not** hide the crosshair (inherits `HAS_AMMO_CROSSHAIR`) | TOGGLE | **1.5 → 18.75°** | **0.4** (A1356) | sniper_sight.kv6 | none | sniperWeapon.py:25,33,41,42,48,76; constants.py:3595,3608,3609 |
| 19 | SNIPER2 (Sniper2Weapon) | MAGNIFIED SCOPE. `zoomed_sensitivity_factor=0.5` restates the base default (a no-op override) | TOGGLE | **1.2 → 30.0°** | 0.5 | sniper2_sight.kv6 | none | sniper2Weapon.py:25,33,42,43,48,74; constants.py:3625,3638,3639 |
| 20 | LANDMINE (LandmineWeapon) | NONE | — | — | — | none | none | landmineWeapon.py:20 |
| 21 | DYNAMITE (DynamiteWeapon) | NONE | — | — | — | none | none | dynamiteWeapon.py:20 |
| 22 | FLAREBLOCK (FlareBlockTool) | NONE — inherits the `enable_colour_picker` gate but not BlockTool's cancel; `BlockToolCommon.use_secondary` returns None | — | — | — | none | none | flareBlockTool.py:13; blockToolCommon.py:139-143 |
| 23 | PREFAB (PrefabTool) | **CONSTRUCT ROTATE.** `prefab_yaw_override = (n+1) % NOOF_DIRECTIONS` (4). Repeats while held at 0.5 s; `update` zeroes the delay on release so taps are instant | EDGE-RATE | — | — | none | none | prefabTool.py:28,62-63,111-115,197-200; constants.py:4877 |
| 24 | ZOMBIEHAND (ZombieHandTool) | NONE | — | — | — | none | none | zombieHandTool.py:13 |
| 25 | BOMB (BombTool) | NONE — the drop is on `use_primary` | — | — | — | none | none | bombTool.py:37-39 |
| 26 | DIAMOND (DiamondTool) | NONE — drop is primary | — | — | — | none | none | diamondTool.py:34-36 |
| 27 | SHRAPNEL (BlockTool, same class as id 5) | BLOCK-LINE CANCEL, gated | EDGE | — | — | none | none | list.py:105; blockTool.py:39-45 |
| 28 | ZOMBIE_PREFAB (ZombiePrefabTool) | CONSTRUCT ROTATE (inherits PrefabTool unchanged) | EDGE-RATE | — | — | none | none | zombiePrefabTool.py:11-29 |
| 29 | SNOWBLOWER (SnowBlowerWeapon) | IRON SIGHTS. Its eyedropper is `use_custom` on the weapon-custom key, not RMB | TOGGLE | 1.0 → 37.5° | 0.5 | snowblower_sight.kv6 | none | snowBlowerWeapon.py:17,29,30,100-124; models.py:309 |
| 30 | INTEL (IntelTool) | NONE — drop is primary | — | — | — | none | none | intelTool.py:35-37 |
| 31 | CLASSIC_GRENADE (ClassicGrenadeTool) | NONE | — | — | — | none | none | classicGrenadeTool.py:12; grenadeTool.py:21 |
| 32 | ANTIPERSONNEL_GRENADE | NONE | — | — | — | none | none | antipersonnelGrenadeTool.py:12 |
| 33 | MOLOTOV (MolotovWeapon) | NONE. Its `can_shoot_secondary` override at :50 is dead code (zoom branch never calls it) | — | — | — | none | none | molotovWeapon.py:18,50 |
| 34 | CROWBAR (CrowbarTool) | NONE | — | — | — | none | none | crowbarTool.py:12 |
| 35 | TOMMYGUN (TommyGunWeapon) | IRON SIGHTS | TOGGLE | 1.0 → 37.5° | 0.5 | Weapon_TommyGun_sight.kv6 | none | tommyGunWeapon.py:36,37,39,45 |
| 36 | SNUB_PISTOL (SnubPistolWeapon) | IRON SIGHTS | TOGGLE | 1.0 → 37.5° | 0.5 | Weapon_SnubNosePistol_sight.kv6 | none | snubPistolWeapon.py:32,36,41 |
| 37 | CLASSIC_SHOTGUN | IRON SIGHTS | TOGGLE | 1.0 → 37.5° | 0.5 | classic_shotgun_sight.kv6 | none | classicShotgunWeapon.py:36,43 |
| 38 | CLASSIC_SMG | IRON SIGHTS | TOGGLE | 1.0 → 37.5° | 0.5 | classic_smg_sight.kv6 | none | classicSmgWeapon.py:35,36,38,44 |
| 39 | NULL_TOOL (NullTool) | NONE | — | — | — | none | none | nullTool.py:12-26 |
| 40 | FAKE_PISTOL (FakePistolTool) | NONE. It is a `Tool`, so it takes the **action** branch and never consults `sight`; `use_secondary` returns None | — | — | — | **none** (our catalog fabricates `pistol_sight.kv6` — see §3) | none | fakePistolTool.py:12-26; tool.py:27,169-170 |
| 41 | UGC_TOOL (UGCTool) | **UGC ITEM-VARIANT CYCLE.** `use_secondary` returns True so `on_start_secondary` runs; steps `UGC_RIGHT_CLICK_GROUPS`. Second branch re-places a targeted UGC entity as the next variant | EDGE-RATE (0.5 s) | — | — | none | none | ugcTool.py:25,220-245; constants.py:4260 |
| 42 | UGC_PREFAB (UGCPrefabTool) | CONSTRUCT ROTATE via `rotate_prefab()`. Calls `super(PrefabTool, self)` deliberately to skip PrefabTool's version. Ghost yaw is world-absolute, not facing-relative. No eyedropper (`use_custom` is `pass`) | EDGE-RATE | — | — | none | none | ugcPrefabTool.py:341-342,438-455,502-506,543-544 |
| 43 | PAINTBRUSH (PaintbrushTool) | **CONTINUOUS AREA SPRAY.** Gated on `enable_colour_picker`. `use_secondary` returns True; `on_start_secondary` fades in a looping `ugc_colour_spraying`; radius 3 area (vs single block on LMB) | HOLD | — | — | none | none | paintbrushTool.py:20,63-64,108-127; constants.py:6684-6688; constants_audio.py:517,520,521 |
| 44 | UGC_PICKAXE | NONE | — | — | — | none | none | ugcPickAxeTool.py:11 |
| 45 | UGC_SUPERSPADE (UGCSuperSpadeTool) | **ALTERNATE DIG, immediate/repeating.** `delay_secondary=False`; `use_spade(True)` → 3×3×3 cube at 0.2 s. Sends `self.damage` (7.5), never `secondary_damage` — retail bug, replicate the effect. Returns truthy only on a player hit, so `on_start_secondary` is conditional | EDGE-RATE (0.2 s) | — | — | none | none | ugcSuperSpadeTool.py:15-28; diggingTool.py:52-68; gameScene.pyd sub_10083260 |
| 46 | UGC_RPG2 | IRON SIGHTS | TOGGLE | 1.0 → 37.5° | 0.5 | ugc_RPG2_sight.kv6 | none | ugcRPG2Weapon.py:17,29,30; models.py:304 |
| 47 | UGC_DRILLGUN | IRON SIGHTS (reuses the non-UGC sight) | TOGGLE | 1.0 → 37.5° | 0.5 | drillgun_sight.kv6 | none | ugcDrillgunWeapon.py:16,28,29 |
| 48 | UGC_SNOWBLOWER | IRON SIGHTS (reuses the non-UGC sight) | TOGGLE | 1.0 → 37.5° | 0.5 | snowblower_sight.kv6 | none | ugcSnowBlowerWeapon.py:18,30,31 |
| 49 | RIOTSTICK (RiotStickTool) | NONE | — | — | — | none | none | riotStickTool.py:12,32-35 |
| 50 | MACHETE (MacheteTool) | NONE | — | — | — | none | none | macheteTool.py:12,32-35 |
| 51 | MEDPACK (MedPackWeapon) | NONE | — | — | — | none | none | medPackWeapon.py:24 |
| 52 | RIOTSHIELD (RiotShieldTool) | NONE. It is a `DiggingTool` → `Tool`, so it takes the **action** branch (not the sight branch); no `use_secondary` override. There is no shield stance | — | — | — | none | none | riotShieldTool.py:12,37-44; tool.py:169-170 |
| 53 | AUTOMATIC_PISTOL (AutoPistolWeapon) | IRON SIGHTS | TOGGLE | 1.0 → 37.5° | 0.5 | autoPistol_sight.kv6 | none | autoPistolWeapon.py:35,37,43 |
| 54 | CHEMICALBOMB | NONE (`can_shoot_secondary` at :56 is dead code) | — | — | — | none | none | chemicalbombWeapon.py:18,56 |
| 55 | GRENADE_LAUNCHER | NONE — `sight` never assigned, no asset loaded | — | — | — | none | none | grenadeLauncherWeapon.py:17; models.py:411-412 |
| 56 | RADAR_STATION | NONE | — | — | — | none | none | radarStationWeapon.py:19 |
| 57 | STICKY_GRENADE | NONE (`can_shoot_secondary` at :57 is dead code) | — | — | — | none | none | stickygrenadeWeapon.py:18,57 |
| 58 | MINE_LAUNCHER | NONE. Carries a **stale** `sight_pos (0.0,0.325,-1.85)` with `sight=None` and no asset — must not be read as ADS | — | — | — | none | none | mineLauncherWeapon.py:16,29; models.py:415-416 |
| 59 | C4 (C4Weapon) | **DETONATOR.** The only `Weapon` that re-enables `has_secondary`; `send_detonate_c4()` then clears `shoot_secondary` so it cannot repeat while held | EDGE | — | — | none | none | c4Weapon.py:20,30,56-61 |
| 60 | ASSAULT_RIFLE | IRON SIGHTS | TOGGLE | 1.0 → 37.5° | 0.5 | assaultRifle_sight.kv6 | none | assaultRifleWeapon.py:35,37,43 |
| 61 | LIGHT_MACHINE_GUN | IRON SIGHTS | TOGGLE | 1.0 → 37.5° | 0.5 | lightMachineGun_sight.kv6 | none | lightMachineGunWeapon.py:35,37,43 |
| 62 | AUTO_SHOTGUN | IRON SIGHTS | TOGGLE | 1.0 → 37.5° | 0.5 | autoShotgun_sight.kv6 | none | autoShotgunWeapon.py:35,42 |
| 63 | BLOCK_SUCKER (BlockSuckerWeapon) | NONE — all behaviour is on primary (3-state warm-up machine) | — | — | — | none | none | blockSuckerWeapon.py:18,40,55-97 |
| 64 | DISGUISE (DisguiseTool) | NONE — `has_secondary=False` explicit, `sight=None`. Eyedropper is `use_custom` on key E | — | — | — | none | none | disguiseTool.py:17,52-73; config.py:43 |

### Totals

| bucket | count | ids |
|---|---:|---|
| ADS — iron sights (zoom 1.0) | **20** | 6,7,9,10,12,13,14,17,29,35,36,37,38,46,47,48,53,60,61,62 |
| ADS — magnified scope | **2** | 18, 19 |
| Real alternate action on RMB | **12** | 4,5,8,15,23,27,28,41,42,43,45,59 |
| INERT-LOCK (state only) | **1** | 2 |
| No observable secondary | **30** | 0,1,3,11,16,20,21,22,24,25,26,30,31,32,33,34,39,40,44,49,50,51,52,54,55,56,57,58,63,64 |

65 total (id 65 `NOOF_SELECTABLE_TOOLS` is a sentinel, `constants.py:858,861`).

### Per-tool ADS transition rate

`TOOLS_ZOOM_TRANSITION_SPEED` (`constants.py:1255-1261`, alias `A459`; alias and
named blocks agree), `(in_div, out_div)`:

| key | in | out |
|---|---:|---:|
| RIFLE_TOOL (6) | 2.5 | 6.0 |
| SNIPER_TOOL (18) | 7.5 | 6.0 |
| SNIPER2_TOOL (19) | 2.5 | 6.0 |
| NOOF_SELECTABLE_TOOLS (fallback, everything else) | 5.0 | 5.0 |

The RIFLE having its own entry despite `zoom == 1.0` is itself proof that iron
sights are a first-class aim state.

Ramp: `zoom_level = interpolate(zoom_level, character.zoom, div)` with
`interpolate(old,new,div) = old + (new-old)/float(div * globals.multiplier)`
(`common.py:115-116`). `globals.multiplier` is 1.0 and never written
(`common.py:9-10`), so it is a fixed-fraction geometric approach with **no dt
term** — but the tick is fixed at 60 Hz, not per frame:
`run.py:388 schedule_interval_soft(self.manager.update, UPDATE_INTERVAL)` with
`UPDATE_FRAMERATE = 60.0` (`constants.py:2934-2937`), and `GameScene.update`
(`gameScene.pyd sub_10149CF0`) has no other caller. Retail sniper zoom-in is
~0.54 s at 60 fps **and at 144 fps**. Ticks to 99%: div 2.5 → 9.0, 5.0 → 20.6,
6.0 → 25.3, 7.5 → 32.2.

Direction select is proven: `PyObject_RichCompare(character.zoom,
self.zoom_level, Py_GT)` at `0x1014D2A8`; true → tuple item 0 (in), false → item
1 (out) at `0x1014D30D`.

---

## 2. Proposed enum

### What is wrong with the current five

`WeaponSecondaryBehavior { none, magnified_scope, spin_up, tool_action,
deploy_machine_gun }` (`include/battlespades/world/weapon_catalog.hpp:72-78`).

| current value | verdict |
|---|---|
| `none` | **Too coarse.** Conflates 30 genuinely-nothing tools with SPADE(2)'s inert 1.0 s swap lock, and — because of the resolver bug — currently swallows all 20 iron-sight weapons. |
| `magnified_scope` | **Wrong gate.** `weapon_secondary.cpp:40` requires `category == sniper` and `:45` requires `zoom_factor > 1.0`. Retail has neither term. Result: 20 of 22 ADS tools resolve to `none`. There is no "iron sights" value at all. |
| `spin_up` | **Right behaviour, wrong layer.** Retail spin-up is not a `use_weapon_secondary` outcome — it is the weapon polling the held flag. Keep the value, but drive it from a held flag, not an edge. |
| `deploy_machine_gun` | Same as above: MG polls `weapon_custom or shoot_secondary`, and forces `character.zoom` directly. Keep, but it is a held-flag poller, not a dispatcher result. |
| `tool_action` | **Far too coarse.** One value currently covers block-line cancel, prefab rotate, UGC variant cycle, continuous paint, C4 detonate, charged alternate dig and immediate alternate dig — seven behaviours with three different trigger shapes and, for the digs, different removed volumes and damage. |

Nothing in the five encodes **trigger shape** (TOGGLE / HOLD / EDGE / EDGE-RATE /
CHARGE), which is the property that actually drives input handling.

### Recommended shape: two axes, not one enum

Model the three dispatcher inputs faithfully in the catalog — `has_secondary`,
`can_zoom`, `sight` (plus `delay_secondary`) — and derive the trigger. Then a
behaviour category for presentation/gameplay:

```
enum class WeaponSecondaryBehavior : uint8_t {
    none,                  // nothing at all
    inert_lock,            // sets active_secondary; blocks swap; no effect      [2]
    iron_sights,           // ADS, zoom multiplier 1.0                           [20 tools]
    magnified_scope,       // ADS, zoom multiplier > 1.0                         [18,19]
    spin_up,               // held-flag polled barrel motor                      [8]
    deploy_machine_gun,    // held-flag polled deploy/withdraw toggle            [15]
    cancel_block_line,     // gated on enable_colour_picker                      [5,27]
    rotate_prefab,         // 90 deg step, rate-limited repeat                   [23,28,42]
    cycle_ugc_item,        // UGC_RIGHT_CLICK_GROUPS step                        [41]
    held_spray,            // continuous radius-3 paint, fade in/out loop        [43]
    detonate,              // one-shot, self-clearing                            [59]
    alternate_dig_charged, // 0.8 s wind-up, cancels on early release            [4]
    alternate_dig_instant, // immediate, repeating                               [45]
};

enum class WeaponSecondaryTrigger : uint8_t { none, toggle, hold, edge, edge_rate, charge };
```

### Mapping — every tool assigned

| new value | trigger | tool ids | from old value |
|---|---|---|---|
| `none` | none | 0,1,3,11,16,20,21,22,24,25,26,30,31,32,33,34,39,40,44,49,50,51,52,54,55,56,57,58,63,64 | `none` (correct) |
| `inert_lock` | charge | 2 | was `tool_action`/`none` — new |
| `iron_sights` | toggle | 6,7,9,10,12,13,14,17,29,35,36,37,38,46,47,48,53,60,61,62 | **had no value**; currently resolves to `none` |
| `magnified_scope` | toggle | 18,19 | `magnified_scope` (correct) |
| `spin_up` | hold | 8 | `spin_up` (correct behaviour, wrong drive) |
| `deploy_machine_gun` | hold→toggle | 15 | `deploy_machine_gun` (correct behaviour, wrong drive) |
| `cancel_block_line` | edge | 5,27 | `tool_action` |
| `rotate_prefab` | edge_rate (0.5 s) | 23,28,42 | `tool_action` |
| `cycle_ugc_item` | edge_rate (0.5 s) | 41 | `tool_action` |
| `held_spray` | hold | 43 | `tool_action` |
| `detonate` | edge | 59 | `tool_action` |
| `alternate_dig_charged` | charge (0.8 s) | 4 | `tool_action` |
| `alternate_dig_instant` | edge_rate (0.2 s) | 45 | `tool_action` |

### Prerequisites before wiring any of this

The corrected gate `!has_secondary && can_zoom && sight != null` **cannot be
implemented against today's generated catalog** — it would be worse than the
current wrong gate:

1. `tools/generate_weapon_catalog.py:502` skips every `ast.Assign` with more than
   one target. `tool.py:27` is `has_primary = has_secondary = True` and
   `tool.py:28` is `delay = delay_secondary = False`. So `has_secondary`
   (`generate_weapon_catalog.py:635`, `attrs.get("has_secondary", False)`) is
   emitted **false** for ~24 Tool-family rows that are true in retail, and
   `delay_secondary` is never captured at all. Fix the walker first.
2. `tools/generate_weapon_catalog.py:744-751` (`auxiliary_model`) falls back to
   `<stem>_sight.kv6` on disk whenever the class does not declare `sight` as a
   string. FakePistolTool declares no `sight`, yet tool 40 is emitted with
   `sight_model_asset = "kv6/pistol_sight.kv6"`. Return `""` when the class
   resolves `sight` to `None`.
3. `WeaponDefinition` has one `sight_model_asset`
   (`weapon_catalog.hpp:270`). The rifle ADS needs a **second** model
   (`pin`, scale `Weapon.pin_scale = 0.02`, offset `(-0.015, +0.3, +2.1)` from
   the rear sight). Add a sight-parts list or an explicit `pin` field.
   The `pin` field at `weapon_catalog.hpp:210` is a throwable *sound*; unrelated.

---

## 3. Why the scopes feel janky

Ranked, most impactful first.

**1. The ADS gate excludes 20 of the 22 aiming weapons.**
`src/world/weapon_secondary.cpp:40-48`:
```cpp
if (weapon.category != WeaponCategory::sniper ||
    !weapon.retail.use.can_zoom || weapon.sight_model_asset.empty()) {
    return WeaponSecondaryBehavior::none;
}
const double factor = weapon.retail.use.zoom_factor.value_or(1.0);
if (factor > 1.0) { return WeaponSecondaryBehavior::magnified_scope; }
return WeaponSecondaryBehavior::none;
```
Retail's gate has no category term and no magnitude threshold. Every iron-sight
weapon (rifle, SMGs, shotguns, pistols, the bazooka) currently has *no* right
click at all. The comment at `weapon_secondary.cpp:38` and
`docs/research/GAMEPLAY_PARITY_AUDIT.md:180-182` assert the same false rule and
must be corrected together, or the bug will be re-derived.

**2. The FOV formula is wrong — the sniper scope is ~2.7× too weak.**
`src/frontend/native_frontend_module.cpp:7356`:
```cpp
camera.fov_y_degrees = 75.0 / tutorial_session->zoom_factor();
```
Retail is a **linear lerp, not a division**: `fovy = 75.0 - 37.5 * zoom_level`
(`gameScene.pyd sub_10114E50`, `gameScene.pyx:258`). Ours gives sniper
75/1.5 = **50.0°** where retail gives **18.75°**, and sniper2 62.5° where retail
gives 30.0°. Compounding it, `TutorialWorldSession::zoom_factor()`
(`src/world/tutorial_session.cpp:2355-2364`) returns 1.0 unless
`magnified_scope()`, so even a correctly-gated iron sight would produce 75° —
retail produces 37.5°, a 2.26× magnification.

**3. There is no zoom ramp — the transition is an instant snap.**
`src/world/tutorial_session.cpp:652-657` flips a bool `zoomed_`, and the camera
reads `zoom_factor()` directly. Retail runs a per-tool geometric ramp on a
fixed 60 Hz tick (`TOOLS_ZOOM_TRANSITION_SPEED`, `constants.py:1255-1260`;
`common.py:115-116`) — sniper zoom-in takes ~32 ticks (~0.54 s), rifle ~9.
Port it as a dt-correct exponential with the 60 Hz behaviour as reference —
`zoom_level += (target - zoom_level) * (1 - pow(1 - 1/div, dt * 60))` — and snap
to target below an epsilon, because the raw recurrence is asymptotic and never
lands. Do **not** run it in the render loop at raw retail rates: that is 2.4×
too fast at 144 fps.

**4. The crosshair rule is inverted for the snipers.**
`src/frontend/native_frontend_module.cpp:6763`:
```cpp
game_hud.set_crosshair_visible(weapon != nullptr && !tutorial_session->zoomed());
```
We hide the reticle for *every* zoom. Retail hides it for exactly one weapon —
the classic rifle, `show_crosshair = UNZOOMED_CROSSHAIR`
(`classicRifleWeapon.py:27`). Both snipers inherit `HAS_AMMO_CROSSHAIR`
(`tool.py:43`) and keep the reticle over the scope; 20 other classes set
`ALWAYS_CROSSHAIR`; `ZOOMED_CROSSHAIR` is defined and unused. Drive this from
the per-weapon `show_crosshair` ordinal (`constants.py:846`), not from `zoomed`.

**5. Zoomed sensitivity defaults to 1.0 instead of 0.5.**
`src/world/tutorial_session.cpp:487-495` uses
`zoom_sensitivity_factor.value_or(1.0)`. Retail's base default is **0.5**
(`tool.py:46`); only the sniper overrides it (0.4). Any weapon whose catalog row
lacks the field aims at double the correct speed. Note also that retail does
*not* FOV-normalise: 0.5 against a 2.26× magnification is 1.13× **faster** on
screen, and the sniper is 0.4 × 4.65 = 1.86× faster. That is authentic; do not
"fix" it if parity is the goal — but it is why aiming feels twitchy.

**6. The sight model draws at full opacity with no fade.**
`src/frontend/native_frontend_module.cpp:6605-6614` draws the sight
unconditionally when `zoomed()`. Retail's `Character.draw_sight(self,
zoom_level)` (`character.pyd sub_1005B630`, `character.pyx:2069-2094`) takes
`zoom_level` as a parameter, and its recovered locals are
`('self','zoom_level','x_off','y_off','z_off','x','y','z','pin')` — position
offsets, no alpha local. Feed `zoom_level` into the sight placement; do not add
an opacity fade the retail code does not have. (The base offset and 0.05 scale
in `src/world/retail_view_model.cpp:309-311` already match retail.)

**7. The rifle's second sight model (`pin`) is not rendered.**
`semi_sight_pin.kv6` ships and retail draws it at `pin_scale = 0.02`
(`weapon.py:37`) offset `(-0.015, +0.3, +2.1)` from the rear sight
(`character.pyx:2085-2089`). Our `WeaponDefinition` has no slot for it, so the
one weapon whose ADS is a two-part iron sight renders half of it.

**8. Melee terrain: tool 4's primary digs a column, its secondary digs nothing.**
`src/world/tutorial_session.cpp:1735-1749`:
```cpp
const bool cube   = tool_id == 3 || tool_id == 24 || (tool_id == 45 && action.secondary);
const bool column = tool_id == 2 || tool_id == 4;
... damage = default_block_health;
```
Retail: CLASSIC_SPADE(4) **primary** is a single block (`gameScene.pyd`
`sub_10081910`); the column belongs to its **secondary** only (`sub_10081D90`).
RIOTSHIELD(52) has no branch at all but digs a 3-block column in retail
(`sub_1008B200`). And the `damage = default_block_health` override is invented —
retail passes the tool's own damage per cell (the classic-spade primary is 3
against `DEFAULT_BLOCK_HEALTH = 5`, `constants.py:3016,4406`), and the cube
handler applies `ceil((damage + rnd.random()*random_extra_damage)/0.25)*0.25`
from a **packet-seeded** PRNG (`sub_1007DE40`, seeding at `0x1007DEF7`). Correct
predicate: `(tool_id==2) || (tool_id==4 && secondary) || (tool_id==52)`.

**9. The alternate dig has no charge/hold semantics and no block-grant rule.**
`weapon_secondary.cpp:34-36` collapses both digs into `tool_action`.
CLASSIC_SPADE must hold 0.8 s and cancel on early release; UGC_SUPERSPADE fires
instantly and repeats at 0.2 s. Both must award **zero** blocks —
`BLOCK_GRANTING_DAMAGES` (`constants.py:1143`) omits both secondary damage
types (caveat: that tuple has no located consumer in the shipped client).

---

## 4. Assets

**There is no 2D scope overlay in retail.** Verified across the whole asset
tree, not just `png/{high,med,low}`: `png/ui/` is 830 files in 50
subdirectories and contains no scope/reticle/vignette/lens image; a whole-tree
search for `scope|reticle|vignette|lens|zoom` returns exactly two files, both
audio (`sounds/zoom_in.ogg`, `sounds/zoom_out.ogg`). `hud.pyd`, `hud.hud.pyd`
and `hud_old.pyd` contain no zoom/scope/crosshair/reticle/vignette strings, so a
procedural HUD vignette is ruled out too. The `laser_sight_beam_*.png` /
`laser_spot*.png` files belong to `LaserAttachment` (`laserAttachment.py:76-84`),
not to any scope.

All paths below are relative to
`assets/original/`, with exact on-disk casing.

### Sight models consumed by an ADS weapon (all SHIP)

| file | used by |
|---|---|
| `kv6/semi_sight.kv6` | 6 |
| `kv6/semi_sight_pin.kv6` | 6 (the only `pin` retail draws) |
| `kv6/smg_sight.kv6` | 7 |
| `kv6/shotgun_sight.kv6` | 9 |
| `kv6/Shotgun2_sight.kv6` | 10 — **casing mismatch**, `models.py:226` generates `shotgun2_sight` |
| `kv6/rpg_sight.kv6` | 12 |
| `kv6/RPG2_sight.kv6` | 13 |
| `kv6/drillgun_sight.kv6` | 14, 47 |
| `kv6/pistol_sight.kv6` | 17 |
| `kv6/sniper_sight.kv6` | 18 |
| `kv6/sniper2_sight.kv6` | 19 |
| `kv6/snowblower_sight.kv6` | 29, 48 |
| `kv6/Weapon_TommyGun_sight.kv6` | 35 |
| `kv6/Weapon_SnubNosePistol_sight.kv6` | 36 |
| `kv6/classic_shotgun_sight.kv6` | 37 |
| `kv6/classic_smg_sight.kv6` | 38 |
| `kv6/ugc_RPG2_sight.kv6` | 46 |
| `kv6/autoPistol_sight.kv6` | 53 |
| `kv6/assaultRifle_sight.kv6` | 60 |
| `kv6/lightMachineGun_sight.kv6` | 61 |
| `kv6/autoShotgun_sight.kv6` | 62 |

### Ships but never drawn

| file | why |
|---|---|
| `kv6/minigun_sight.kv6` | assigned (`minigunWeapon.py:42`) but `can_zoom=False` makes it unreachable |
| `kv6/minigun_sight_ring.kv6` | never even **loaded** — `models.py:231` is `('minigun',)`, empty `sight_extra` |
| `kv6/shotgun_sight_pin.kv6`, `kv6/shotgun_sight_ring.kv6` | loaded by `models.py:142-143`; no weapon class declares `pin`/`ring` for them |
| `kv6/Shotgun2_sight_pin.kv6`, `kv6/Shotgun2_sight_ring.kv6` | same |
| `kv6/classic_shotgun_sight_pin.kv6`, `kv6/classic_shotgun_sight_ring.kv6` | same |
| `kv6/smg_sight_ring.kv6` | `SMG_RING` is assigned (`smgWeapon.py:36`, `tommyGunWeapon.py:37`) but `Character.draw_sight` never reads `ring` — the byte string `ring` does not occur in `character.pyd` |
| `kv6/classic_smg_sight_ring.kv6` | same (`classicSmgWeapon.py:36`) |

### Absent (do not invent)

| expected name | status |
|---|---|
| `mg_sight.kv6` | **absent** — MG has `sight_pos` but no `sight`; loaded by neither `models.py:222-251` nor `:298-312` |
| `minelauncher_sight.kv6` | **absent** — id 58's `sight_pos` is stale data |
| `grenadelauncher_sight.kv6` | **absent** |
| `ugc_drillgun_sight.kv6`, `ugc_snowblower_sight.kv6` | **absent by design** — ids 47/48 reuse the base sights |
| any scope overlay PNG | **absent** — none exists in retail |

### Detail behaviour (commonly got backwards)

Every sight loads with `min_model_detail=2` (`models.py:141,143,298,301,304,309,
312`). In `load_model` (`models.py:48-68`) that parameter appears **only** in
`invscale = 3 - max(min_model_detail, __modeldetail)`; the global is created
unconditionally (`:64-66`). It is a **quality floor**, not a visibility gate:
sights render at full voxel resolution at every setting while ordinary models
decimate. The graphics slider is clamped 0..2 (`graphicsTab.py:82`). Do **not**
implement a "no sight below detail 2" rule — `weapon.sight` is always truthy,
and since `sight` is the ADS gate, such a rule would silently disable ADS.

### Audio

`sounds/zoom_in.ogg` and `sounds/zoom_out.ogg` ship. `Character.set_zoom`
(`sub_1001C580`, `character.pyx:405-416`) plays them only when the value
*changed* and `self.main`. The deployed MG bypasses `set_zoom` entirely
(`mgWeapon.py:200,203,277`), so it plays neither.

---

## 5. UNRECOVERABLE

Nobody should silently fill these in.

1. **Whether `character.zoom` stores the tool's float `zoom` magnitude or a
   plain bool.** The FOV formula `75.0 - 37.5 * zoom_level` is proven
   (`sub_10114E50`), and `zoom_level` is proven to interpolate toward
   `character.zoom` (`sub_10149CF0`). But the call site that passes `tool.zoom`
   into `set_zoom` was not decompiled: in `gameScene.pyd sub_10182900` the
   `set_zoom` fetch is at `0x10184C52` and the interned name `zoom`
   (`dword_1028E290`) has **no xref inside that function**. Supporting evidence
   for the float reading: `Tool.zoom`/`SniperWeapon.zoom`/`Sniper2Weapon.zoom`
   are read nowhere in the Python layer; `set_zoom(value)` stores `value`
   verbatim and forwards it to `weapon.on_zoom(value)`; the sniper has a
   uniquely slow 7.5 zoom-in divisor, which only makes sense over a longer
   travel. **If it is a bool, both snipers collapse to 37.5° and the 1.5/1.2
   constants are dead data.** Settle this before shipping magnification.
2. **Which enum member of `show_crosshair` maps to which predicate.** The
   consumer is in `gameScene.pyd`; the name at `.data:0x1026D9F4` is reachable
   only through the alphabetical interned-name pointer table at `0x10285978`
   and has no code xref, so the switch cannot be read statically. The
   `HAS_AMMO_CROSSHAIR` reading is from the identifier, not from behaviour.
   Related unexplored: `show_crosshair_centre` and the
   `TARGET_CROSSHAIR_{CENTRE,TOP_LEFT,TOP_RIGHT,BOTTOM_LEFT,BOTTOM_RIGHT}`
   sprite names — a 5-piece crosshair, with no matching files under
   `assets/original` (presumably atlased, unverified).
3. **The exact input-layer site that sets `Character.shoot_secondary = True`.**
   Proven to be read by `sub_1001EC00` and cleared in the zoom branch; nothing
   in the Python tree ever sets it True. The setter was not located.
4. **Whether sprint blocks or cancels ADS.** `Character.set_sprint`
   (`sub_1001D960`, `character.pyx:447-448`) contains
   `if value: if self.zoom: self.set_zoom(0)` — so sprinting *drops* ADS. But
   `Tool.can_shoot_secondary_while_sprinting = False` (`tool.py:53-54`) also
   exists and its consumer was not fully reconstructed; whether it additionally
   refuses to *start* ADS is not established.
5. **The exact conditions under which `reload`, `set_weapon`, `set_dead` and
   `shoot` unzoom.** All four hold `set_zoom` xrefs in `character.pyd`
   (`0x1002129A`, `0x100221F0`, `0x10032595`, `0x1004C681`) and reload is gated
   on `can_reload_while_zoomed` (base False, `tool.py:232-233`; only
   `MGWeapon` returns True, `mgWeapon.py:353-354`), but the surrounding
   predicates were only pattern-matched, not fully decompiled.
6. **The paintbrush spray application rule.** `PAINTBRUSH_SECONDARY_RADIUS = 3`,
   `_RADIUS_SEPARATION = 0.5`, `_RANDOM_VARIATION = 0.5`
   (`constants.py:6684-6688`) are real retail constants, but a recursive search
   over every `.py`/`.pyc`/`.pyd` finds no client-side consumer — the scatter is
   presumably server-applied and is not in our tree.
7. **`BLOCK_GRANTING_DAMAGES` consumer** (`constants.py:1143`). The tuple is
   real and omits both secondary damage types, but the string appears only in
   the stale `gameScene_old.pyd`, never in the live one. "The alternate dig
   grants no blocks" is a strong data inference, not demonstrated behaviour.
8. **`TOOLS_SECONDARY_DAMAGE_TYPE` consumer** (`constants.py:1073-1140`). No
   `.py` and no shipped `.pyd` references it, and it contains a genuine retail
   duplicate-key bug (`PICKAXE_TOOL` mapped twice at `:1074-1075`, with
   `UGC_PICKAXE_TOOL` absent). The damage-type routing is inferred from the
   `BlockManager` handler names, not proven from the client.
9. **Whether firing cancels ADS.** No evidence that it does;
   `Weapon.get_variable_accuracy` reads `accuracy_zoom` while
   `character.zoom` is true (`weapon.py:220`), which only makes sense if you
   stay zoomed through the shot. But `Character.shoot` does contain a
   `set_zoom` xref at `0x1004C681` (a dry-fire unzoom at `character.pyx:1763`),
   whose exact ordering relative to `Weapon.use_an_ammo` (`weapon.py:113`) was
   not established.
10. **`min_model_detail` interaction with the literal 0.05 sight scale** was not
    checked; the literal is proven, the interaction is not.
11. **On-screen placement of the sight** — the GL call order in `draw_sight` is
    proven, but the resulting screen position should still be confirmed against
    a retail capture (see `retail-parity-rig`).

### Retail sloppiness to replicate, not fix

* UGC_SUPERSPADE(45) declares `secondary_damage` and never passes it
  (`ugcSuperSpadeTool.py:20,28`); both values are 7.5 so the observable damage
  is unchanged.
* MINE_LAUNCHER(58) carries a `sight_pos` with no `sight`.
* MG(15) carries `sight_pos` and `can_zoom=True` with no `sight`; deployment
  works *because* `sight` is None.
* `TOOLS_SECONDARY_DAMAGE_TYPE`'s duplicate `PICKAXE_TOOL` key.
* RIOTSHIELD(52) overrides `hit_block_sound`/`hit_player_sound` but not
  `hit_wet_block_sound`, despite `hitwater_riotshield.ogg` existing.
* `models.py` generates `shotgun2_sight` while the file is `Shotgun2_sight.kv6`
  — retail rode Windows case-insensitivity; our loader must normalise.

---

## 6. Machine-readable

`zoom` is a **multiplier**; `fov_degrees` is the derived retail vertical FOV
(`75.0 - 37.5 * zoom`). `sensitivity` multiplies `config.mouse_sensitivity` on
both axes while zoomed. `overlay` is `null` for every tool — retail has none.

```json
{
  "_meta": {
    "zoom_is": "multiplier",
    "fov_formula": "fovy_degrees = 75.0 - 37.5 * zoom_level",
    "fov_source": "gameScene.pyd sub_10114E50 (gameScene.pyx:258-259)",
    "hip_fov_degrees": 75.0,
    "znear": 0.1,
    "sensitivity_base_default": 0.5,
    "sensitivity_source": "tool.py:46; gameScene.pyx:1242-1245",
    "ads_gate": "(not has_secondary) and can_zoom and self.main and sight is not None",
    "ads_gate_source": "character.pyd sub_100348B0 (character.pyx:1005-1019)",
    "ads_trigger": "toggle, consumed on press; release cannot unzoom",
    "transition_tick_hz": 60.0,
    "transition_formula": "zoom_level += (target - zoom_level) / div",
    "transition_speed_source": "constants.py:1255-1261 (A459)",
    "transition_speed": {"6": [2.5, 6.0], "18": [7.5, 6.0], "19": [2.5, 6.0], "default": [5.0, 5.0]},
    "asset_root": "assets/original/",
    "overlay_exists_anywhere": false,
    "tool_count": 65,
    "sentinel_id": 65
  },
  "0":  {"secondary": "none", "trigger": "none", "zoom": null, "fov_degrees": null, "sensitivity": null, "sight_asset": null, "overlay": null, "provenance": "pickAxeTool.py:12; tool.py:169-170"},
  "1":  {"secondary": "none", "trigger": "none", "zoom": null, "fov_degrees": null, "sensitivity": null, "sight_asset": null, "overlay": null, "provenance": "knifeTool.py:12; tool.py:169-170"},
  "2":  {"secondary": "inert_lock", "trigger": "charge", "zoom": null, "fov_degrees": null, "sensitivity": null, "sight_asset": null, "overlay": null, "provenance": "spadeTool.py:17-18; tool.py:194-195,235-236", "charge_seconds": 1.0},
  "3":  {"secondary": "none", "trigger": "none", "zoom": null, "fov_degrees": null, "sensitivity": null, "sight_asset": null, "overlay": null, "provenance": "superSpadeTool.py:12-31"},
  "4":  {"secondary": "alternate_dig_charged", "trigger": "charge", "zoom": null, "fov_degrees": null, "sensitivity": null, "sight_asset": null, "overlay": null, "provenance": "classicSpadeTool.py:17,18,21,24,43-47,59-79; gameScene.pyd sub_10081D90", "charge_seconds": 0.8, "dig_shape": "column_z_minus1_to_plus1", "dig_damage": 5, "grants_blocks": false},
  "5":  {"secondary": "cancel_block_line", "trigger": "edge", "zoom": null, "fov_degrees": null, "sensitivity": null, "sight_asset": null, "overlay": null, "provenance": "blockTool.py:39-45; blockToolCommon.py:142-143", "gated_on": "manager.enable_colour_picker"},
  "6":  {"secondary": "iron_sights", "trigger": "toggle", "zoom": 1.0, "fov_degrees": 37.5, "sensitivity": 0.5, "sight_asset": "kv6/semi_sight.kv6", "pin_asset": "kv6/semi_sight_pin.kv6", "overlay": null, "provenance": "classicRifleWeapon.py:27,32,33; models.py:141,230", "hides_crosshair": true},
  "7":  {"secondary": "iron_sights", "trigger": "toggle", "zoom": 1.0, "fov_degrees": 37.5, "sensitivity": 0.5, "sight_asset": "kv6/smg_sight.kv6", "overlay": null, "provenance": "smgWeapon.py:35,38,44"},
  "8":  {"secondary": "spin_up", "trigger": "hold", "zoom": null, "fov_degrees": null, "sensitivity": null, "sight_asset": null, "overlay": null, "provenance": "minigunWeapon.py:42,44,50,134-138", "unreachable_asset": "kv6/minigun_sight.kv6"},
  "9":  {"secondary": "iron_sights", "trigger": "toggle", "zoom": 1.0, "fov_degrees": 37.5, "sensitivity": 0.5, "sight_asset": "kv6/shotgun_sight.kv6", "overlay": null, "provenance": "shotgunWeapon.py:36,43"},
  "10": {"secondary": "iron_sights", "trigger": "toggle", "zoom": 1.0, "fov_degrees": 37.5, "sensitivity": 0.5, "sight_asset": "kv6/Shotgun2_sight.kv6", "overlay": null, "provenance": "shotgun2Weapon.py:38,45; models.py:226"},
  "11": {"secondary": "none", "trigger": "none", "zoom": null, "fov_degrees": null, "sensitivity": null, "sight_asset": null, "overlay": null, "provenance": "grenadeTool.py:21; tool.py:42"},
  "12": {"secondary": "iron_sights", "trigger": "toggle", "zoom": 1.0, "fov_degrees": 37.5, "sensitivity": 0.5, "sight_asset": "kv6/rpg_sight.kv6", "overlay": null, "provenance": "rpgWeapon.py:16,28,29; models.py:298"},
  "13": {"secondary": "iron_sights", "trigger": "toggle", "zoom": 1.0, "fov_degrees": 37.5, "sensitivity": 0.5, "sight_asset": "kv6/RPG2_sight.kv6", "overlay": null, "provenance": "rpg2Weapon.py:16,28,29; models.py:301"},
  "14": {"secondary": "iron_sights", "trigger": "toggle", "zoom": 1.0, "fov_degrees": 37.5, "sensitivity": 0.5, "sight_asset": "kv6/drillgun_sight.kv6", "overlay": null, "provenance": "drillgunWeapon.py:16,28,29; models.py:312"},
  "15": {"secondary": "deploy_machine_gun", "trigger": "hold", "zoom": 1.0, "fov_degrees": 37.5, "sensitivity": 0.5, "sight_asset": null, "overlay": null, "provenance": "mgWeapon.py:42,48,198-206,300; constants.py:3800-3801", "deploy_seconds": 3.0, "withdraw_seconds": 0.75, "bypasses_set_zoom": true},
  "16": {"secondary": "none", "trigger": "none", "zoom": null, "fov_degrees": null, "sensitivity": null, "sight_asset": null, "overlay": null, "provenance": "rocketTurretWeapon.py:20"},
  "17": {"secondary": "iron_sights", "trigger": "toggle", "zoom": 1.0, "fov_degrees": 37.5, "sensitivity": 0.5, "sight_asset": "kv6/pistol_sight.kv6", "overlay": null, "provenance": "pistolWeapon.py:30,34,40"},
  "18": {"secondary": "magnified_scope", "trigger": "toggle", "zoom": 1.5, "fov_degrees": 18.75, "sensitivity": 0.4, "sight_asset": "kv6/sniper_sight.kv6", "overlay": null, "provenance": "sniperWeapon.py:25,33,41,42,48,76; constants.py:3595,3608,3609", "accuracy_zoom": 0.0, "zoom_position_offset": [0.325, 0.25, 0.0], "needs_zoom_arms_offset": true, "hides_crosshair": false},
  "19": {"secondary": "magnified_scope", "trigger": "toggle", "zoom": 1.2, "fov_degrees": 30.0, "sensitivity": 0.5, "sight_asset": "kv6/sniper2_sight.kv6", "overlay": null, "provenance": "sniper2Weapon.py:25,33,42,43,48,74; constants.py:3625,3638,3639", "accuracy_zoom": 0.0, "zoom_position_offset": [0.325, 0.25, 0.0], "needs_zoom_arms_offset": true, "hides_crosshair": false},
  "20": {"secondary": "none", "trigger": "none", "zoom": null, "fov_degrees": null, "sensitivity": null, "sight_asset": null, "overlay": null, "provenance": "landmineWeapon.py:20"},
  "21": {"secondary": "none", "trigger": "none", "zoom": null, "fov_degrees": null, "sensitivity": null, "sight_asset": null, "overlay": null, "provenance": "dynamiteWeapon.py:20"},
  "22": {"secondary": "none", "trigger": "none", "zoom": null, "fov_degrees": null, "sensitivity": null, "sight_asset": null, "overlay": null, "provenance": "flareBlockTool.py:13; blockToolCommon.py:139-143"},
  "23": {"secondary": "rotate_prefab", "trigger": "edge_rate", "zoom": null, "fov_degrees": null, "sensitivity": null, "sight_asset": null, "overlay": null, "provenance": "prefabTool.py:28,62-63,111-115,197-200; constants.py:4877", "repeat_seconds": 0.5, "step_degrees": 90},
  "24": {"secondary": "none", "trigger": "none", "zoom": null, "fov_degrees": null, "sensitivity": null, "sight_asset": null, "overlay": null, "provenance": "zombieHandTool.py:13"},
  "25": {"secondary": "none", "trigger": "none", "zoom": null, "fov_degrees": null, "sensitivity": null, "sight_asset": null, "overlay": null, "provenance": "bombTool.py:37-39"},
  "26": {"secondary": "none", "trigger": "none", "zoom": null, "fov_degrees": null, "sensitivity": null, "sight_asset": null, "overlay": null, "provenance": "diamondTool.py:34-36"},
  "27": {"secondary": "cancel_block_line", "trigger": "edge", "zoom": null, "fov_degrees": null, "sensitivity": null, "sight_asset": null, "overlay": null, "provenance": "list.py:105; blockTool.py:39-45", "gated_on": "manager.enable_colour_picker"},
  "28": {"secondary": "rotate_prefab", "trigger": "edge_rate", "zoom": null, "fov_degrees": null, "sensitivity": null, "sight_asset": null, "overlay": null, "provenance": "zombiePrefabTool.py:11-29; prefabTool.py:111-115", "repeat_seconds": 0.5, "step_degrees": 90},
  "29": {"secondary": "iron_sights", "trigger": "toggle", "zoom": 1.0, "fov_degrees": 37.5, "sensitivity": 0.5, "sight_asset": "kv6/snowblower_sight.kv6", "overlay": null, "provenance": "snowBlowerWeapon.py:17,29,30; models.py:309"},
  "30": {"secondary": "none", "trigger": "none", "zoom": null, "fov_degrees": null, "sensitivity": null, "sight_asset": null, "overlay": null, "provenance": "intelTool.py:35-37"},
  "31": {"secondary": "none", "trigger": "none", "zoom": null, "fov_degrees": null, "sensitivity": null, "sight_asset": null, "overlay": null, "provenance": "classicGrenadeTool.py:12; grenadeTool.py:21"},
  "32": {"secondary": "none", "trigger": "none", "zoom": null, "fov_degrees": null, "sensitivity": null, "sight_asset": null, "overlay": null, "provenance": "antipersonnelGrenadeTool.py:12; grenadeTool.py:21"},
  "33": {"secondary": "none", "trigger": "none", "zoom": null, "fov_degrees": null, "sensitivity": null, "sight_asset": null, "overlay": null, "provenance": "molotovWeapon.py:18,50"},
  "34": {"secondary": "none", "trigger": "none", "zoom": null, "fov_degrees": null, "sensitivity": null, "sight_asset": null, "overlay": null, "provenance": "crowbarTool.py:12"},
  "35": {"secondary": "iron_sights", "trigger": "toggle", "zoom": 1.0, "fov_degrees": 37.5, "sensitivity": 0.5, "sight_asset": "kv6/Weapon_TommyGun_sight.kv6", "overlay": null, "provenance": "tommyGunWeapon.py:36,39,45"},
  "36": {"secondary": "iron_sights", "trigger": "toggle", "zoom": 1.0, "fov_degrees": 37.5, "sensitivity": 0.5, "sight_asset": "kv6/Weapon_SnubNosePistol_sight.kv6", "overlay": null, "provenance": "snubPistolWeapon.py:32,36,41"},
  "37": {"secondary": "iron_sights", "trigger": "toggle", "zoom": 1.0, "fov_degrees": 37.5, "sensitivity": 0.5, "sight_asset": "kv6/classic_shotgun_sight.kv6", "overlay": null, "provenance": "classicShotgunWeapon.py:36,43"},
  "38": {"secondary": "iron_sights", "trigger": "toggle", "zoom": 1.0, "fov_degrees": 37.5, "sensitivity": 0.5, "sight_asset": "kv6/classic_smg_sight.kv6", "overlay": null, "provenance": "classicSmgWeapon.py:35,38,44"},
  "39": {"secondary": "none", "trigger": "none", "zoom": null, "fov_degrees": null, "sensitivity": null, "sight_asset": null, "overlay": null, "provenance": "nullTool.py:12-26"},
  "40": {"secondary": "none", "trigger": "none", "zoom": null, "fov_degrees": null, "sensitivity": null, "sight_asset": null, "overlay": null, "provenance": "fakePistolTool.py:12-26; tool.py:27,169-170", "note": "catalog currently fabricates kv6/pistol_sight.kv6 via generator fallback"},
  "41": {"secondary": "cycle_ugc_item", "trigger": "edge_rate", "zoom": null, "fov_degrees": null, "sensitivity": null, "sight_asset": null, "overlay": null, "provenance": "ugcTool.py:25,220-245; constants.py:4260", "repeat_seconds": 0.5},
  "42": {"secondary": "rotate_prefab", "trigger": "edge_rate", "zoom": null, "fov_degrees": null, "sensitivity": null, "sight_asset": null, "overlay": null, "provenance": "ugcPrefabTool.py:341-342,438-455,502-506", "repeat_seconds": 0.5, "step_degrees": 90, "yaw_space": "world_absolute"},
  "43": {"secondary": "held_spray", "trigger": "hold", "zoom": null, "fov_degrees": null, "sensitivity": null, "sight_asset": null, "overlay": null, "provenance": "paintbrushTool.py:63-64,108-127; constants.py:6684-6688; constants_audio.py:520-521", "gated_on": "manager.enable_colour_picker", "radius": 3, "fade_in_seconds": 0.3, "fade_out_seconds": 0.5, "loop_sound": "ugc_colour_spraying"},
  "44": {"secondary": "none", "trigger": "none", "zoom": null, "fov_degrees": null, "sensitivity": null, "sight_asset": null, "overlay": null, "provenance": "ugcPickAxeTool.py:11; pickAxeTool.py:12"},
  "45": {"secondary": "alternate_dig_instant", "trigger": "edge_rate", "zoom": null, "fov_degrees": null, "sensitivity": null, "sight_asset": null, "overlay": null, "provenance": "ugcSuperSpadeTool.py:15-28; gameScene.pyd sub_10083260", "repeat_seconds": 0.2, "dig_shape": "cube_3x3x3", "dig_damage": 7.5, "grants_blocks": false},
  "46": {"secondary": "iron_sights", "trigger": "toggle", "zoom": 1.0, "fov_degrees": 37.5, "sensitivity": 0.5, "sight_asset": "kv6/ugc_RPG2_sight.kv6", "overlay": null, "provenance": "ugcRPG2Weapon.py:17,29,30; models.py:304"},
  "47": {"secondary": "iron_sights", "trigger": "toggle", "zoom": 1.0, "fov_degrees": 37.5, "sensitivity": 0.5, "sight_asset": "kv6/drillgun_sight.kv6", "overlay": null, "provenance": "ugcDrillgunWeapon.py:16,28,29"},
  "48": {"secondary": "iron_sights", "trigger": "toggle", "zoom": 1.0, "fov_degrees": 37.5, "sensitivity": 0.5, "sight_asset": "kv6/snowblower_sight.kv6", "overlay": null, "provenance": "ugcSnowBlowerWeapon.py:18,30,31"},
  "49": {"secondary": "none", "trigger": "none", "zoom": null, "fov_degrees": null, "sensitivity": null, "sight_asset": null, "overlay": null, "provenance": "riotStickTool.py:12,32-35"},
  "50": {"secondary": "none", "trigger": "none", "zoom": null, "fov_degrees": null, "sensitivity": null, "sight_asset": null, "overlay": null, "provenance": "macheteTool.py:12,32-35"},
  "51": {"secondary": "none", "trigger": "none", "zoom": null, "fov_degrees": null, "sensitivity": null, "sight_asset": null, "overlay": null, "provenance": "medPackWeapon.py:24"},
  "52": {"secondary": "none", "trigger": "none", "zoom": null, "fov_degrees": null, "sensitivity": null, "sight_asset": null, "overlay": null, "provenance": "riotShieldTool.py:12,37-44; tool.py:169-170"},
  "53": {"secondary": "iron_sights", "trigger": "toggle", "zoom": 1.0, "fov_degrees": 37.5, "sensitivity": 0.5, "sight_asset": "kv6/autoPistol_sight.kv6", "overlay": null, "provenance": "autoPistolWeapon.py:35,37,43"},
  "54": {"secondary": "none", "trigger": "none", "zoom": null, "fov_degrees": null, "sensitivity": null, "sight_asset": null, "overlay": null, "provenance": "chemicalbombWeapon.py:18,56"},
  "55": {"secondary": "none", "trigger": "none", "zoom": null, "fov_degrees": null, "sensitivity": null, "sight_asset": null, "overlay": null, "provenance": "grenadeLauncherWeapon.py:17; models.py:411-412"},
  "56": {"secondary": "none", "trigger": "none", "zoom": null, "fov_degrees": null, "sensitivity": null, "sight_asset": null, "overlay": null, "provenance": "radarStationWeapon.py:19"},
  "57": {"secondary": "none", "trigger": "none", "zoom": null, "fov_degrees": null, "sensitivity": null, "sight_asset": null, "overlay": null, "provenance": "stickygrenadeWeapon.py:18,57"},
  "58": {"secondary": "none", "trigger": "none", "zoom": null, "fov_degrees": null, "sensitivity": null, "sight_asset": null, "overlay": null, "provenance": "mineLauncherWeapon.py:16,29; models.py:415-416", "note": "stale sight_pos with no sight model"},
  "59": {"secondary": "detonate", "trigger": "edge", "zoom": null, "fov_degrees": null, "sensitivity": null, "sight_asset": null, "overlay": null, "provenance": "c4Weapon.py:20,30,56-61"},
  "60": {"secondary": "iron_sights", "trigger": "toggle", "zoom": 1.0, "fov_degrees": 37.5, "sensitivity": 0.5, "sight_asset": "kv6/assaultRifle_sight.kv6", "overlay": null, "provenance": "assaultRifleWeapon.py:35,37,43"},
  "61": {"secondary": "iron_sights", "trigger": "toggle", "zoom": 1.0, "fov_degrees": 37.5, "sensitivity": 0.5, "sight_asset": "kv6/lightMachineGun_sight.kv6", "overlay": null, "provenance": "lightMachineGunWeapon.py:35,37,43"},
  "62": {"secondary": "iron_sights", "trigger": "toggle", "zoom": 1.0, "fov_degrees": 37.5, "sensitivity": 0.5, "sight_asset": "kv6/autoShotgun_sight.kv6", "overlay": null, "provenance": "autoShotgunWeapon.py:35,42"},
  "63": {"secondary": "none", "trigger": "none", "zoom": null, "fov_degrees": null, "sensitivity": null, "sight_asset": null, "overlay": null, "provenance": "blockSuckerWeapon.py:18,55-97"},
  "64": {"secondary": "none", "trigger": "none", "zoom": null, "fov_degrees": null, "sensitivity": null, "sight_asset": null, "overlay": null, "provenance": "disguiseTool.py:17,52-73; config.py:43"}
}
```
