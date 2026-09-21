# Class voice-over recovery

> **Recovery/specification reference.** Preserve the measured retail behavior and its evidence. Implementation updates, old build paths, test counts and session constraints below describe their original investigation; they are not current release or deployment status. Use the [maintained documentation index](README.md) for present operating instructions and recheck historical findings against current source.

## Implemented multiplayer observer path (2026-08-05)

Remote-character audio is now wired end to end in the C++ client. The protocol
boundary matters:

- `CreatePlayer(28)` starts a generation-safe life and arms retail's guaranteed
  remote spawn line after a uniform 1-2 second delay.
- The local `CreatePlayer(28)` path now arms the same Character timer. The first
  life and a changed class force the spawn bank; an ordinary same-class respawn
  preserves retail's authored `25 >= randint(0, 100)` chance. This replaced the
  old class-change-only hook that made every same-class respawn silent.
- `WorldUpdate` supplies class, health, movement flags, wade state and velocity.
  The observer derives footsteps, wading, jump/water-jump, damaging land,
  fall-hurt, jetpack landing and the zombie periodic groan from those fields.
- The authoritative alive-to-dead edge (including `KillAction`) plays the
  class-specific death bank exactly once.
- `PlayerLeft`, player-id generation reuse, and map/session teardown discard all
  cadence, voice-selection and delayed-spawn state. A replacement player cannot
  inherit a stale groan, step, or death edge.
- Every observer cue is emitted at the replicated character position through
  the shared OpenAL distance, orientation and VXL-occlusion path. Local cues
  remain head-relative.
- `PlaySound(23)`, `PlayAmbientSound(24)` and `StopSound(25)` remain the
  authoritative path for explicitly networked sounds. This change does not
  duplicate those packets.

Retail has no ordinary bullet-damage pain vocal in `CLASS_SOUNDS`. The word
"hurt" in the class table means `FALL_HURT_SOUND` / `FALL_HURT_VO`; weapon hit
and melee impact sounds continue to come from replicated combat/`PlaySound`
events. Inventing a generic pain cry would not match the original game.

The pure state machines live in `include/battlespades/world/class_voice.hpp` and
`include/battlespades/world/remote_character_audio.hpp`, covered by
`aos_class_voice_tests` and `aos_remote_character_audio_tests`. They are
fixed-capacity and allocation-free on the gameplay thread.

Measured evidence behind the per-class VO system. Recovered from
aceofspades_source/shared/constants.py CLASS_SOUNDS (18 classes x 16 slots, of
which 8 are vocal), cross-checked against the server vendored copy, and expanded
against the shipped asset tree: 374 distinct .ogg files, 0 missing.

Two retail quirks below MUST be reproduced rather than corrected -- see the
warning markers in the table. Diverging from either is a parity break.

# Per-class voice-over and death sounds — implementation plan

Recovery is complete and re-verified first-hand for this plan: I re-read the whole `CLASS_SOUNDS` block, re-expanded every spec against the shipped asset tree, re-read `aoslib/media.py`'s selection algorithm, and re-decompiled `Character.play_vo` (0x10024030) and `Character.play_sound` (0x10024730) in the live IDB session. Where the two input reports disagreed, my own reading is cited below and settles it.

Retail's whole vocal system is **one table with 16 slots per class**, of which **8 are vocal**: `DEATH_SOUND`, `PERIODIC_SOUND`, `SPAWN_VO`, `JUMP_VO`, `WATER_JUMP_VO`, `LAND_VO`, `WATER_LAND_VO`, `FALL_HURT_VO` (slot enum: `aceofspades_source/shared/constants_audio.py:33`). There is **no pain/hurt vocal**, no taunt, no kill-confirm, no reload line — the enum has no such member, and the only announcer-style lines in the game are two VIP stingers (`constants_audio.py:394-395`) that are module-level and not class-keyed. These 8 slots are the complete answer to "you know those stuff".

---

## A. THE VO TABLE

Source of truth: `G:/AoSRevival/aceofspades_source/shared/constants.py:5610-5935` (`CLASS_SOUNDS`, closing at `:5935` with `A2412 = CLASS_SOUNDS` at `:5936`). Vendored identically on the server at `G:/AoSRevival/BattleSpades/shared/constants.py:6231`.

The recovery counted **51 unique specs and 374 distinct `.ogg` files** and checked the then-existing source/staged asset roots. That historical result does not verify a current install; regenerate and verify staged assets before release. There is no equivalent of the weapon table's one known hole (`snowcan_reload`, `tests/test_weapon_audio_map.cpp:39`).

Chance and repeat behaviour are per-slot constants, identical across every class, so they are factored out of the table:

| slot | chance | repeat rule | source |
|---|---|---|---|
| `SPAWN_VO` | 25 | repeats allowed | `constants_audio.py:167` |
| `JUMP_VO` | 33 | never twice running | `:168` (`-33`) |
| `WATER_JUMP_VO` | 66 | never twice running | `:169` (`-66`) |
| `LAND_VO` | 33 | never twice running | `:170` (`-33`) |
| `WATER_LAND_VO` | 66 | never twice running | `:171` (`-66`) |
| `FALL_HURT_VO` | 100 | repeats allowed | `:172` (`FULLHURT_VO_CHANCE`, typo is real) |
| `DEATH_SOUND` | 100 | repeats allowed | hardcoded per row |
| `PERIODIC_SOUND` | 100 | repeats allowed, re-arm `uniform(3.0, 6.0)` s | per row |

The negative sign is not a negative probability: `media.py:46-47` reads `disallow_consecutive_plays = name[2] < 0; play_chance = abs(name[2])`.

### The table (all 18 classes × 8 vocal slots)

`—` marks a **deliberate silence** (`BLANK_SOUND`, `constants_audio.py:164`), not a gap. Counts in parentheses are the number of takes in that bank.

| id | class (dict line) | spawn | jump | water_jump | land | water_land | fall_hurt | death | periodic |
|---|---|---|---|---|---|---|---|---|---|
| 0 | SOLDIER (5611) | `sol_spawn_vo_001-006` (6) | `sol_jump_vo_001-008` (8) | `sol_water_jump_vo_001-008` (8) | `sol_land_vo_001-008` (8) | `sol_water_land_vo_001-008` (8) | `sol_water_land_vo_001-008` ⚠ | `sol_death_vo_001-008` (8) | — |
| 1 | SCOUT (5629) | `sco_spawn_vo_001-006` (6) | `sco_jump_vo_001-008` | `sco_water_jump_vo_001-008` | `sco_land_vo_001-008` | `sco_water_land_vo_001-008` | `sco_water_land_vo_001-008` ⚠ | `sco_death_vo_001-008` | — |
| 2 | ROCKETEER (5647) | `roc_spawn_vo_001-005` (5) | `roc_jump_vo_001-008` | `roc_water_jump_vo_001-008` | `roc_land_vo_001-008` | `roc_water_land_vo_001-008` | `roc_water_land_vo_001-008` ⚠ | `roc_death_vo_001-008` | — |
| 3 | MINER (5683) | `min_spawn_vo_001-004` (4) | `min_jump_vo_001-008` | `min_water_jump_vo_001-008` | `min_land_vo_001-008` | `min_water_land_vo_001-008` | `min_water_land_vo_001-008` ⚠ | `min_death_vo_001-008` | — |
| 4 | ZOMBIE (5701) | — | — | — | — | — | — | `vo_zombiedeath_001-010` (10) | `vo_zombiegroan_001-016` (16) |
| 5 | CLASSIC_SOLDIER (5719) | — | — | — | — | — | `classic_fallhurt_vo` (1) | `classic_death_vo` (1) | — |
| 6 | GANGSTER_1 (5737) | `gang_spawn_vo_001-006` (6) | `gang_jump_vo_001-008` | `gang_water_jump_vo_001-008` | `gang_land_vo_001-008` | `gang_water_land_vo_001-008` | `gang_water_land_vo_001-008` ⚠ | `gang_death_vo_001-008` | — |
| 7 | GANGSTER_2 (5755) | *identical to id 6* | | | | | | | — |
| 8 | GANGSTER_3 (5773) | *identical to id 6* | | | | | | | — |
| 9 | GANGSTER_4 (5791) | *identical to id 6* | | | | | | | — |
| 10 | GANGSTER_VIP_1 (5809) | *identical to id 6* | | | | | | | — |
| 11 | GANGSTER_VIP_2 (5827) | *identical to id 6* | | | | | | | — |
| 12 | ENGINEER (5665) | `eng_spawn_vo_001-005` (5) | `eng_jump_vo_001-008` | `eng_water_jump_vo_001-008` | `eng_land_vo_001-008` | `eng_water_land_vo_001-008` | `eng_water_land_vo_001-008` ⚠ | `eng_death_vo_001-008` | — |
| 13 | UGCBUILDER (5845) | — | — | — | — | — | — | **—** | — |
| 14 | FAST_ZOMBIE (5863) | — | — | — | — | — | — | `vo_zombiedeath_001-010` | `vo_zombiegroan_001-016` |
| 15 | JUMP_ZOMBIE (5881) | — | — | — | — | — | — | `vo_zombiedeath_001-010` | `vo_zombiegroan_001-016` |
| 16 | SPECIALIST (5899) | `AoS_vox_SPECIALIST_spawn_001-005` (5) | `AoS_vox_SPECIALIST_emote_jump_001-008` (8) | `AoS_vox_SPECIALIST_emote_jump_001-008` ‡ | `AoS_vox_SPECIALIST_emote_land_001-008` (8) | `AoS_vox_SPECIALIST_emote_land_001-008` ‡ | `AoS_vox_SPECIALIST_emote_impact_fallen_001-008` (8) | `AoS_vox_SPECIALIST_emote_death_001-008` (8) | — |
| 17 | MEDIC (5917) | `AoS_vox_MEDIC_spawn_001-005` (5) | `AoS_vox_MEDIC_emote_jump_001-008` | `AoS_vox_MEDIC_emote_jump_001-008` ‡ | `AoS_vox_MEDIC_emote_land_001-008` | `AoS_vox_MEDIC_emote_land_001-008` ‡ | `AoS_vox_MEDIC_emote_impact_fallen_001-008` | `AoS_vox_MEDIC_emote_death_001-008` | — |

Exact per-slot line numbers for every row are contiguous within each class dict in the documented order `DEATH_SOUND, PERIODIC_SOUND, SPAWN_VO, JUMP_VO, WATER_JUMP_VO, LAND_VO, WATER_LAND_VO, FALL_HURT_VO` — e.g. Soldier `constants.py:5620-5627`, Zombie `:5710-5717`, UGC Builder `:5854-5861`, Specialist `:5908-5915`, Medic `:5926-5933`.

**⚠ = retail copy-paste bug, reproduce verbatim.** On all eleven legacy voiced classes, `FALL_HURT_VO` points at the class's own **water-land** bank, byte-identical to the `WATER_LAND_VO` line directly above it: `:5627` (sol), `:5645` (sco), `:5663` (roc), `:5681` (eng), `:5699` (min), `:5753` / `:5771` / `:5789` / `:5807` / `:5825` / `:5843` (all six gangsters). Only Specialist (`:5915`) and Medic (`:5933`) got a dedicated fall-impact bank. Diverging here is a parity break, not a fix.

**‡ = second, structurally different alias.** Specialist and Medic have no separate water banks at all: `WATER_JUMP_VO` reuses `emote_jump` (`:5912`, `:5930`) and `WATER_LAND_VO` reuses `emote_land` (`:5914`, `:5932`). A generator that assumes a `water_` variant exists for these two classes will emit filenames that do not ship.

### Deliberate silences, explicitly

- **No spawn line at all** — 5 of 18 classes: Zombie (`:5712`), Classic Soldier (`:5730`), UGC Builder (`:5856`), Fast Zombie (`:5874`), Jump Zombie (`:5892`).
- **All six `*_VO` slots blank** — Zombie, UGC Builder, Fast Zombie, Jump Zombie. Classic Soldier is blank on five and is the one exception on fall-hurt (`:5735`).
- **UGC Builder is the only class with a blank `DEATH_SOUND`** (`:5854`) — mute for *voice* only; it keeps the full generic movement foley (`:5846-5853`), with one non-VO blank at `JETPACK_LAND_SOUND` (`:5848`).
- **Zombies are not mute**, they simply have no `*_VO`: they swap it for a death cry and the idle groan loop.

### Files we ship that the table never names — do NOT wire these up

96 VO-shaped files, verified unreferenced:

- Complete superseded `spe_*` bank (46 files: death 8, jump 8, land 8, spawn 6, water_jump 8, water_land 8) and complete `med_*` bank (46 files). These are the pre-`AoS_vox` Specialist/Medic voices; `constants.py:5899-5934` uses the `AoS_vox_SPECIALIST_*` / `AoS_vox_MEDIC_*` banks instead. A tree-wide grep of the retail Python finds no reference to either.
- Four orphan takes the declared ranges deliberately stop short of: `eng_spawn_vo_006` (table says `001-005`), `roc_spawn_vo_006` (`001-005`), `min_spawn_vo_005` and `min_spawn_vo_006` (`001-004`).

**Encode the declared range, never the disk count.** Inferring ranges from the filesystem would play four lines retail never plays.

---

## B. DATA PLUMBING

Follow the `WeaponSoundSet` precedent exactly: a generated struct hanging off the catalog row, never hand-written.

### 1. Struct — add to `include/battlespades/world/class_catalog.hpp`

Place next to `ClassDefinition` (currently `:50-62`), modelled on `WeaponSoundSet` (`include/battlespades/world/weapon_catalog.hpp:192-218`), including its habit of documenting each field's retail behaviour so an empty value reads as intentional.

```cpp
/**
 * One retail sample spec: the list `["stem_001-008", -1, chance]` from
 * CLASS_SOUNDS. Voice rows are always three elements, so unlike the movement
 * foley in the same table they carry NO pitch range and must play at 1.0.
 *
 * An empty `group` is retail's BLANK_SOUND: a deliberate silence, and a hard
 * no-op at the call site. Five of eighteen classes depend on that.
 */
struct VoiceLine final {
    std::string_view group;
    /**
     * 0..100. Retail rolls `chance >= randint(0, 100)` -- 101 outcomes -- so
     * 100 always fires and 25 is 26/101, not 25/100.
     */
    std::uint8_t chance{};
    /**
     * Retail encodes this as a NEGATIVE chance. It is stronger than "pick a
     * different take": after any successful play the very next trigger is
     * refused outright with the dice untouched.
     */
    bool no_consecutive{};
};

/** Order matches the bind sequence in OpenAlFrontendAudio::start(). */
enum class ClassVoiceCue : std::uint8_t {
    spawn, jump, water_jump, land, water_land, fall_hurt, death, periodic, count,
};

/**
 * The eight vocal slots of one CLASS_SOUNDS row.
 *
 * `fall_hurt` deliberately aliases `water_land` on the eleven legacy voiced
 * classes, and `water_jump`/`water_land` deliberately alias the dry banks on
 * Specialist and Medic. Both are retail data, not generator failures.
 */
struct ClassVoiceSet final {
    std::array<VoiceLine, static_cast<std::size_t>(ClassVoiceCue::count)> lines;
    /** PERIODIC_SOUND's (min, max) re-arm window in seconds; 0 when unused. */
    float periodic_minimum{};
    float periodic_maximum{};
};
```

Then append one field to `ClassDefinition` (`class_catalog.hpp:50-62`), last so the generated aggregate initialisers only grow by one line:

```cpp
    /** Per-class voice-over recovered from retail CLASS_SOUNDS. */
    ClassVoiceSet voice;
```

### 2. Generator — extend `tools/generate_class_catalog.py`

The generator already imports the retail tables from the server tree (`load_constants` at `:42-47`, called from `generate()` at `:65`), and `BattleSpades/shared/constants.py:605` does `from .constants_audio import *`, so the **symbolic slot names are already in scope**: use `c.SPAWN_VO`, `c.DEATH_SOUND` etc. rather than hardcoding 8..15.

- Read `c.CLASS_SOUNDS[cid]` inside the existing `for cid in range(18)` loop (`:77` / `:103`), keyed **by class id**, never by dict-literal position. `CLASS_SOUNDS` is not in id order — `CLASS_ENGINEER` (id 12) physically precedes `CLASS_MINER` (id 3) at `constants.py:5665` vs `:5683`. Indexing by position silently swaps two voices.
- Normalise at generation time so the runtime never sees a negative: `no_consecutive = raw[2] < 0`, `chance = abs(raw[2])`.
- Handle both row shapes: the six `*_VO` slots plus `DEATH_SOUND` are 3-element lists or the bare string `""`; `PERIODIC_SOUND` is a 3-tuple `(spec, min, max)` whose `spec` is itself either a list or `""`.
- Emit rows at the existing emit site (`:118-127`), one extra `{...}` per class.
- Fold the voice rows into `digest_rows` (`:89-99`) so `class_catalog_contract_sha256()` (`class_catalog.hpp:85`, emitted at `:171`) changes whenever the table changes.
- Regenerate with `--server-root G:/AoSRevival/BattleSpades`. **Never hand-edit `src/world/class_catalog.generated.cpp`** — banner at `:70` and the `--check` mode at `:181-186` exist to enforce that.

### 3. Backend binding — `src/audio/openal_frontend_audio.cpp`

472 stems cannot become `SoundHandle` constants (the existing flat list is `menu_confirm_sound{1U}` .. `fall_hurt_sound{49U}`, `include/battlespades/audio/openal_frontend_audio.hpp:75-136`). Use the keyed pattern the weapon cues already use.

- In `Impl`, next to `weapon_sound_sets` (`:625`), add
  `std::array<std::array<std::vector<SoundHandle>, static_cast<std::size_t>(world::ClassVoiceCue::count)>, world::retail_class_count> class_voice_sets;`
- Bind in `start()` immediately after the weapon loop (`:802-831`), reusing `Impl::load_optional_group` (`:319-341`) — it already expands groups via `sound_group_stems` and caches by canonical path, so the six gangsters sharing one `gang_` bank decode it once.
- Clear it in `stop()` alongside `:1122-1127`.
- Public API on `OpenAlFrontendAudio`, mirroring `play_weapon_cue` / `has_weapon_cue` (`openal_frontend_audio.hpp:186-188`, impl `.cpp:899-913`):

```cpp
    void play_class_vo(std::uint8_t speaker_id, std::uint8_t class_id,
                       world::ClassVoiceCue cue, std::uint32_t variant,
                       SoundPosition position, bool head_relative, float gain = 1.0F);
    [[nodiscard]] bool has_class_vo(std::uint8_t class_id, world::ClassVoiceCue cue) const noexcept;
    void stop_class_vo(std::uint8_t speaker_id) noexcept;
```

`speaker_id` is load-bearing — it is what makes one-VO-per-character possible (section D).

**Do not route VO through `play_one_shot`** (`.cpp:1166-1168`): it hardcodes `relative=false` and draws from the 16-voice pool. The 2D path itself already exists in the private `Impl::play` (`:541`, honouring `relative` at `:570`, `:573`, `:577-578`, `:580-584`) and is used by the menu cues at `:1185` — the VO slot writer copies that setup.

**Do not unpin `AL_PITCH`** (`:571`). Voice rows are 3-element and carry no pitch field (`media.py:84-87` only reads `name[3]`/`name[4]` when `len > 3`; `Character.play_sound` does its own `PyObject_Size(sound) > 3` check), so VO must play at exactly 1.0. The foley jitter (±0.8 / ±1.5 semitones, `constants_audio.py:54-61`) is a separate change to a shared code path used by every existing one-shot.

### 4. Selection logic — new `src/world/class_voice.{hpp,cpp}`

Add to `aos_world` in `src/CMakeLists.txt` beside `world/footstep_audio.cpp` (`:72`) and `world/weapon_fire_sound.cpp` (`:73`). Pure, no device, dice injected:

```cpp
struct VoiceSelectorState final {
    /** Retail's mutable name[1], including the >=1000 no-repeat token. */
    std::int32_t last_played{-1};
};
struct VoiceSelection final {
    bool play{};
    std::uint32_t variant{};   // retail's 1-based index, already rerolled
};
[[nodiscard]] VoiceSelection
select_voice_line(const VoiceLine& line, std::uint32_t range_first,
                  std::uint32_t range_last, VoiceSelectorState& state,
                  std::uint32_t chance_roll, std::uint32_t variant_roll) noexcept;
```

Port `aoslib/media.py:41-79` verbatim: refuse-and-clear when `no_consecutive && last_played >= 1000` (`:48-50`); roll `chance >= randint(0,100)` (`:51-52`); reroll `while random_index == last_played` when `start != end`, else take `start` unchanged (`:60-65`); write back `last_played` (`:67`) and set the token (`:75-76`); zero-pad to 3 (`:69-70`). Range parsing itself is already done by `sound_group_stems` (`include/battlespades/audio/sound_groups.hpp:22`) — the selector works on indices and the frontend maps index → handle.

---

## C. TRIGGERS

| event | who owns it | local hook | remote hook |
|---|---|---|---|
| spawn line | **server** fires it (CreatePlayer 28); client owns delay + roll | `src/frontend/native_frontend_module.cpp:2443-2480` | same packet, `:2436-2442` → `sync_remote_player_rig` `:5327-5340` |
| jump / water-jump | **client** (local physics edge) | `native_frontend_module.cpp:1109` | none today — see F.7 |
| land / water-land | **client** | `native_frontend_module.cpp:1108` | none today — see F.7 |
| fall-hurt | **client** | `native_frontend_module.cpp:1116-1118` | none today |
| death cry | **server** (authoritative health) | `RemotePlayerReplica::dead` false→true | same signal, same place |
| zombie groan | **client** timer | new, ticked at `:7697` | new, ticked at `:7080-7083` |

### Spawn line

Retail's driver is `Character.update_spawn_sound` (0x10038100): a latch plus a countdown, firing exactly once per life.

```
if not self.played_spawn_sound:
    self.spawn_sound_timer -= dt
    if self.spawn_sound_timer <= 0.0:
        self.play_spawn_sound(); self.played_spawn_sound = True
```

and `Character.play_spawn_sound` (0x100825F0):

```
sound = CLASS_SOUNDS[self.parent.current_class.id][SPAWN_VO][:]     # slice copy
if len(sound) >= 2 and (not self.main or self.old_class_id != self.parent.get_class_id()):
    sound[2] = 100
self.play_vo(sound)
```

So: **remote players always get a spawn line; the local player gets 25% unless the class changed since the last spawn**, in which case it is also guaranteed. The `len(sound) >= 2` guard is what makes `BLANK_SOUND` (`""`, length 0) skip the override. The slice copy exists so the 100 override does not permanently corrupt the shared global row.

Delay: `self.spawn_sound_timer = random.uniform(1.0, 2.0)` at spawn (I confirmed `dbl_1008A2E8 == 0x4000000000000000 == 2.0` against the `fld1` pairing). Ship that as a named constant, not a fixed number.

**Local hook** — `native_frontend_module.cpp:2443-2480`, the block whose in-place comment at `:2455-2458` already declares it the authoritative new-life boundary. `local->class_id` is in hand at `:2453`, spawn position at `:2459-2460`. **Arm the timer here; do not play here.** Compare `local->class_id` against a stored `last_spawn_class_id` for the chance override, then update it. Place the call *after* `apply_authoritative_transform` / `prediction_history.clear()` (`:2459-2461`) and outside the `settings_warning` branches (`:2468`, `:2477`).

**Remote hook** — the same packet path: `tutorial_roster.apply(packet)` at `:2438`, `load_roster_players()` at `:2440`. The new-life edge is the `generation` bump on `RemotePlayerRenderRig` (`:912`), which `sync_remote_player_rig` (`:5327-5340`) already tests and early-returns on when nothing changed; a generation change falls through to the rebuild at `:5413`. Position comes from `RemotePlayerReplica::position` (`include/battlespades/network/protocol168_players.hpp:52`) or the interpolator at `:917`.

**Offline / Training** — `src/world/tutorial_session.cpp:407-431` (constructor, `spawn_with_selection` at `:418`) and `apply_server_selection` at `:1915`, which commits `config_.initial_class_id` at `:1925`.

**Ticking** — decrement all spawn timers where footsteps are already advanced, `native_frontend_module.cpp:7697`; remote rigs are already ticked in the same frame at `:7080-7083`.

### Jump / land / water variants / fall-hurt

All four edges already exist and are already correct. `advance_footsteps` (`native_frontend_module.cpp:1093-1119`) computes `impact.landed` (`:1108`), `impact.jumped` (`:1109`), `impact.wade` (`:1110`), and receives `sounds.fall_hurt` from `world::step_land_jump` (`:1112`, `:1116`).

The VO fires **alongside** the foley, not instead of it: `JUMP_SOUND` and `JUMP_VO` are two separate rows in the same class dict (`constants.py:5612` vs `:5623`), and the existing `play_movement_sound` calls at `:1101` / `:1114` / `:1117` cover only the foley half. Add the VO call next to each, keyed on `sounds.impact` / `sounds.fall_hurt`, with the water split taken from the same `wade` bit the foley uses.

Zombies (ids 4, 14, 15) swap the *foley* for their own bank (`constants_audio.py:73-78`) but leave every VO slot blank, so they simply select nothing — the empty-group no-op handles it with no special case.

### Death cry

**No `KillAction` decoder is needed, and death does not belong last.** Retail has exactly one `DEATH_SOUND` per class with no siblings and no damage-type branching — the per-damage-type variation that exists is non-vocal explosion foley keyed by the killing weapon (`constants_audio.py:136-137`, `:310-311`), not by the victim's class.

The signal already exists: `bool dead` on `RemotePlayerReplica` (`protocol168_players.hpp:51`) and on the packet (`:23`), driven from authoritative health at `src/network/protocol168_players.cpp:315` (WorldUpdate rows), `:335` (SetHP 5) and `:264` (CreatePlayer). A **false→true edge is the death event**, for local and remote alike, and it is read-only and server-authoritative.

Hook it as a once-per-tick diff rather than inside a packet-apply branch: keep a `std::array<bool, 128>` of last-seen dead flags in the frontend `Impl` and compare against `tutorial_roster.players()` next to the rig tick (`:7080-7083`). That keeps the audio out of the `settings_warning` error paths at `:2385`, `:2428`, `:2497` and out of `update_health` (`:2432`). Reset the corresponding spawn latch on the same diff when `dead` goes true→false.

Death chance is 100 — it always fires. UGC Builder is silent by data.

### Zombie idle groan

`PERIODIC_SOUND` is a 3-tuple `(spec, 3.0, 6.0)` on ids 4/14/15 and `(BLANK_SOUND, 0, 0)` everywhere else. Retail's driver is `Character.setup_periodic_sound` / `update_periodic_sound` / `stop_periodic_sound` with attributes `periodic_sound_timer` and `periodic_sound_timer_settings`. Add a per-speaker timer re-armed to `uniform(min, max)` after each fire, ticked with everything else, suppressed while the speaker is dead and stopped on despawn.

---

## D. THE OVERLAP GUARD

Two distinct problems, one mechanism.

**The threat is real, not theoretical.** `Impl::acquire_voice()` (`src/audio/openal_frontend_audio.cpp:524-539`) reclaims stopped sources, then takes any inactive slot, then falls through to `std::min_element(... left.sequence < right.sequence ...)` at `:534-537` — **steal the oldest-started voice**. A spawn line is by construction the longest-running and earliest-started voice in the window right after a respawn, so under a gunfire/footstep flood it is the first thing evicted, every time. The pool is 16 (`openal_frontend_audio.hpp:16`, reserved at `.cpp:380-381`).

**Retail has no such problem and does not solve it by reservation.** I confirmed `Character.play_vo` (0x10024030) decompiles to:

```
new_vo = self.play_sound(sound, zone=media.IN_WORLD_AUDIO_ZONE)
if new_vo is not None:
    self.stop_current_vo()
    self.current_vo = new_vo
```

The new voice is created first, the previous one is then hard-stopped (`stop_current_vo`, 0x10024390: `if self.current_vo is not None and self.current_vo.is_playing(): self.current_vo.close(); self.current_vo = None`), and the new one is latched. Each Character owns exactly one VO player, reclaimed only by its own next line. **There is no cooldown** — the entire throttle is the chance roll plus the no-consecutive token. Resist adding one.

### Design

A **dedicated VO source pool keyed by speaker id**, held apart from the one-shot pool — the same shape as the existing `LoopSlot` pool (`.cpp:636-641`, `loop_source_count{4U}` at `:635`, allocated at `:368-378`, justified by the comment at `openal_frontend_audio.hpp:53-55` that one-shot stealing would cut a sustained loop off mid-burst). That comment applies verbatim to a two-second spawn line.

```cpp
    static constexpr std::size_t vo_source_count{8U};
    struct VoiceOverSlot final {
        ALuint source{};
        std::uint8_t speaker_id{};
        bool active{};
    };
    std::array<VoiceOverSlot, vo_source_count> vo_slots{};
```

`play_class_vo(speaker_id, ...)` slot policy, in order:

1. If a slot is already owned by `speaker_id`, **reuse it**: `alSourceStop` + `alSourcei(AL_BUFFER, 0)`, then rebind. This *is* the one-line-per-speaker rule — it falls out of keying by speaker, exactly as retail's `current_vo` does, and needs no separate bookkeeping.
2. Otherwise reclaim `AL_STOPPED` / `AL_INITIAL` slots first, the same sweep as `reclaim_finished_voices()` (`:510-522`), and take a free one.
3. If every slot is busy with a *different* speaker, steal the one whose speaker is **furthest from the listener**. Never steal by age (age cuts the newest line, which is backwards) and never steal the local player's slot.

Do not carve reserved voices out of the 16-voice one-shot pool: shrinking it degrades weapon and footstep feedback under load, which is a perceptible combat regression. `valid_openal_frontend_audio_config` (`:668-673`) is untouched — the VO pool is fixed-size and separate.

Additional rules on the slot writer, all mirroring `Impl::play`:

- **Cull before allocating.** `media.py:90-98` returns *before* creating a player when the squared distance exceeds `HEARING_DISTANCE * HEARING_DISTANCE` (`constants_audio.py:7`, `= 50`; already present client-side as `retail_hearing_distance` at `.cpp:68`). Do the same squared-distance test before taking a slot, so a distant VO consumes no source and — matching retail — does not consume selection state either.
- **2D vs 3D.** `head_relative == true` sets `AL_SOURCE_RELATIVE`, `AL_ROLLOFF_FACTOR 0`, `AL_POSITION 0,0,0` exactly as `.cpp:570-584`. See F.1 for when it is true.
- **Reverb.** `play_vo` passes `zone=IN_WORLD_AUDIO_ZONE` as an explicit kwarg, so `media.py:136-139` gives VO `AOS_EFFECT_REVERB` **even when it is playing 2D**. We have no reverb path yet; note it rather than fake it.
- `stop_class_vo(speaker_id)` on despawn/disconnect/roster clear; clear all slots in `stop()` beside `:1111-1127`.

---

## E. TESTS — off-GPU, off-audio-device

Two binaries, following the two existing shapes.

### 1. `tests/test_class_voice.cpp` — the data/asset contract

Wire it in `tests/CMakeLists.txt` exactly like `aos_weapon_audio_map_tests` (`:149-154`): link `BattleSpades::World`, define `AOS_TEST_ASSET_ROOT="${AOS_ASSET_ROOT}"`. No audio device, no window.

- **`every_named_voice_sample_resolves_on_disk()`** — the one that has already caught real problems. For all 18 classes × 8 cues, expand with `audio::sound_group_stems` (`include/battlespades/audio/sound_groups.hpp:22`) and assert the `.ogg` exists under `AOS_TEST_ASSET_ROOT/sounds`. Structure copied from `tests/test_weapon_audio_map.cpp:89-115`. Expect **374 distinct files, 0 missing, and no exemption list** — unlike the weapon table there is no `known_missing_group` here (`test_weapon_audio_map.cpp:39`), and introducing one should require an argument in review.
- **`deliberate_silences_are_pinned()`** — assert the exact blank set: spawn blank for ids {4, 5, 13, 14, 15}; all six `*_VO` blank for {4, 13, 14, 15}; death blank for {13} only; periodic non-blank for {4, 14, 15} only with window (3.0, 6.0). A regenerate that "helpfully" fills a blank fails here.
- **`the_retail_fall_hurt_alias_is_preserved()`** — for ids {0,1,2,3,6,7,8,9,10,11,12} assert `fall_hurt.group == water_land.group`; for {16,17} assert it differs and ends in `emote_impact_fallen_001-008`. This pins the copy-paste bug so a future cleanup cannot silently diverge.
- **`specialist_and_medic_reuse_their_dry_banks()`** — `water_jump == jump` and `water_land == land` for ids 16 and 17.
- **`chances_match_the_recovered_constants()`** — the eight-row table from section A, including `no_consecutive` flags.
- **`banks_are_keyed_by_class_id_not_by_position()`** — assert id 3 resolves to a `min_` group and id 12 to an `eng_` group. `CLASS_SOUNDS`'s literal order puts ENGINEER before MINER; a generator that walks the dict in order swaps two voices, and this is the only test that catches it.
- **`superseded_banks_are_not_referenced()`** — assert no class names a `spe_` or `med_` group. 92 such files ship and are dead.
- Extend `tests/test_class_catalog.cpp` (which already takes `AOS_TEST_ASSET_ROOT`, `:47`) with a pinned `class_catalog_contract_sha256()` value, and keep `tools/generate_class_catalog.py --check` (`:181-186`) in CI so a hand-edit of the generated file fails.

### 2. `tests/test_class_voice_selection.cpp` — the pure selection function

Link `BattleSpades::World` only, no asset root — same wiring as `aos_weapon_fire_sound_tests` (`tests/CMakeLists.txt:180-183`). Because `select_voice_line` takes its dice as parameters, every case is deterministic with no RNG and no seed.

- Chance 100 always fires; chance 0 never fires.
- **The off-by-one that matters**: `chance >= randint(0, 100)` is inclusive on both ends, 101 outcomes. Assert chance 25 fires at `roll == 25` and does not at `roll == 26`. Getting this wrong is a silent 1% drift nobody would ever hear.
- `no_consecutive`: a successful play sets the token; the very next call refuses **regardless of the roll** and clears the token; the call after that rolls normally. This is stronger than "pick a different take" and is what a naive port gets wrong.
- Variant reroll never returns `last_played` when `first != last`; returns `first` unchanged when `first == last`.
- An empty group is a hard no-op and leaves state untouched.
- A golden vector: a fixed dice sequence maps to a pinned sample-name sequence for one class, so a refactor of the expansion cannot drift.
- Pin the two named behavioural decisions the way `footstep_audio.hpp:66` pins `stale_suppressed_timer`: per-speaker vs shared no-repeat state (F.5), and whether jump/land VO inherits the 0.1 s foley debounce (F.3).

### 3. Slot bookkeeping, still off-device

Factor the slot decision (reuse-by-speaker / reclaim / steal-furthest) into a pure header over an array of `{speaker_id, active, distance}` and unit-test it, with the OpenAL calls layered on top. Same split the codebase already uses between `world/footstep_audio` and the frontend.

---

## F. RISKS AND UNRESOLVED ITEMS

**1. RESOLVED, and the two reports contradicted each other — the local player's own VO is 2D.** I re-decompiled `Character.play_sound` (0x10024730) directly. The structure is: if no explicit `pos` was passed, `if self.main:` then call `self.scene.<camera_manager>.<is_controller_active>()`, and **if that returns false, `pos = None`**. `media.py:129-130` turns `pos is None` into `player.relative = True`. So the local player's line is head-locked 2D in normal first person and becomes positional only when a camera controller (spectator/cinematic) is active; remote lines are always positional. One recovery report asserted "VO is never 2D"; that is wrong. This single fact decides the `head_relative` parameter, so it is worth restating rather than re-deriving.

**2. RESOLVED — the hearing cull is two mechanisms, not one.** Inside `Character.play_sound` the guard is `if pos and loops == <1>: if (not self.main) and (not self.within_hearing_distance): return None` — `within_hearing_distance` is a precomputed attribute on the character, and there are two extra guards the first report omitted. `media.play` then does an **independent** squared-distance test against `HEARING_DISTANCE = 50` (`media.py:90-98`). Implement the 50-unit test (we have the data); skip the precomputed flag (we do not have that attribute). Collapsing them into one check changes which remote VO you hear — audible-parity risk, visible on the parity rig.

**3. UNRESOLVED — does retail gate jump/land VO behind the 0.1 s foley debounce?** `JUMP_SOUND_REPEAT_DELAY = 0.1` (`constants_audio.py:373`, mirrored as `world::jump_sound_repeat_delay`, `footstep_audio.hpp:47`) demonstrably applies to the foley. Both are driven from `Character.update_sounds` (0x10037570), which I did not read. Recommendation: derive the VO from the already-debounced `LandJumpSounds::impact` (a bunny-hop firing two grunts 0.1 s apart would be worse than the alternative error), and name the assumption as a header constant so a parity disagreement flips one line and fails one pinned test.

**4. RESOLVED but worth a constant — the spawn delay is not fixed.** `self.spawn_sound_timer = random.uniform(1.0, 2.0)` on spawn; a second init site sets it to 0.0. Shipping a fixed delay, or guessing, is wrong.

**5. UNRESOLVED BY DESIGN — scope of the no-repeat state.** Retail's `name[1]` lives in the *shared* `CLASS_SOUNDS` row (`media.py:67`, `:76`), so the token is **global across every player of that class**: player B's jump grunt can be swallowed outright because player A just jumped. Per-speaker is better behaviour; shared is bit-accurate. Recommend per-speaker, named and pinned. Note retail sidesteps its own quirk in exactly one place: `play_spawn_sound` slice-copies the row before writing the chance, so the spawn line's `last_played` stays `-1` and a spawn line *can* repeat back to back while a jump grunt cannot.

**6. UNRESOLVED — the local player's own death cry, 2D or 3D.** By the rule in F.1 it is 2D unless a camera controller is active — and a death camera almost certainly is one. We have no camera-controller concept at the death moment. Ship 2D and check on the rig.

**7. SCOPE, not risk — remote jump/land/fall-hurt VO has no edge source today.** `advance_footsteps` (`native_frontend_module.cpp:1093-1119`) reads exclusively from `tutorial_session`, i.e. the local player. Peers carry `position`, `velocity`, `input_flags`, `state_flags` on the replica (`protocol168_players.hpp:52-59`) but nothing derives airborne/landed for them. Remote **spawn, death and groan are all reachable today**; remote jump/land is separate work. Do not describe the jump/land hook as covering the whole feature.

**8. Gameplay authority — nothing here writes gameplay state, with two guardrails.** (i) `old_class_id` must be a client-side mirror of the last class the *server* told us we spawned as (from `local->class_id` at `:2453`), never an input to a class-change request; the spawn latch must be reset *by* the spawn event, never the other way round. (ii) The VO call at the CreatePlayer site must come after `apply_authoritative_transform` / `prediction_history.clear()` (`:2459-2461`) and must not participate in the `settings_warning` branches (`:2468`, `:2477`) — an audio failure must never abort the authoritative respawn commit. Same for the death diff: it must sit outside the WorldUpdate/SetHP apply path.

**9. Do not touch `AL_PITCH` in this change.** `openal_frontend_audio.cpp:571` pins it to 1.0 inside the shared `Impl::play` used by every existing one-shot (weapon shoot/reload, menu, footsteps). VO needs exactly 1.0. Editing that path to add foley jitter would silently re-pitch already-shipped weapon audio; same caution for `AL_ROLLOFF_FACTOR` / `AL_MAX_DISTANCE` (`:573-579`), which currently encode the retail hearing behaviour for all cues.

**10. Verify the current asset installation.** The original investigation resolved 401 referenced samples (374 vocal + 27 foley). A retained source or staged tree may differ, and old install output was removed during cleanup. Follow the asset importer and runbook instead of assuming those three historical roots still exist.

**11. Minor.** `sound_group_stems` rejects ranges wider than 33 (`sound_groups.hpp:44`). Our widest is 16 (`vo_zombiegroan_001-016`), so it is fine, but the guard exists.

**12. Expectation-setting for the user's request.** "usually on spawn the class says something" is, in retail, **25%** for your own respawn as the same class. It is guaranteed only when you change class, and guaranteed for every *other* player you can hear. If the ship-it feel is wrong, that is a tuning decision to make deliberately against a named constant — not a bug to fix by accident.
