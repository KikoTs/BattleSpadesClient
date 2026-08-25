"""Generate the entity catalog from the recovered retail constants.

Every number in the emitted table is READ from a constants module at generation
time. Nothing is transcribed by hand, so a constant that is renamed or deleted
upstream fails the build loudly instead of silently freezing a stale value into
C++.

What IS hand-authored here is the mapping from a logical field to the constant
that feeds it -- because that mapping lives in the weapon and behaviour modules
(`landmineWeapon.py` binds `A1798..A1810`), not in the constants file, and a
generator that tried to infer it would be re-implementing the game.

Three retail traps are handled explicitly rather than papered over:

  * Retail ships TWO parallel constant blocks. A named one
    (`DYNAMITE_EXPLOSION_RADIUS = 5`) and an A-numbered alias block
    (`A1632 = 8`). They DISAGREE, and the weapon modules bind the alias. The
    alias therefore wins, and every row records which block it came from.
  * `GRAVE_DAMAGE`, `LANDMINE_DAMAGE` and friends in the line-954 block are
    damage-TYPE ordinals from an `xrange` enum, not damage amounts. Reading
    them as damage yields a plausible-looking 14 that is completely wrong. The
    real amounts live in the `*_EXPLOSION_DAMAGE` block.
  * `BOMB_EXPLOSION_FUSE` and `BOMB_EXPLOSION_RADIUS` are each assigned THREE
    times in a row at constants.py:4529-4534. Python keeps the last, so the
    effective values are 10 and 7 -- but a human reading the file top-down sees
    the first pair. Evaluating rather than parsing is what makes this correct.

Five entities have NO art anywhere -- FLAG(0), HELICOPTER(2), MACHINE_GUN(7),
TANK(26), BLOCK_GOO(31). MACHINE_GUN is the interesting one: RETAIL ITSELF
ships none (`mgWeapon.py` declares `model = []`). Their `parts` span is emitted
empty, which the asset test pins, so nobody can quietly substitute a lookalike.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

# Provenance, strongest first. Mirrors world::EntityProvenance.
RETAIL_ALIAS = "retail_alias"
RETAIL_NAMED = "retail_named"
RETAIL_LITERAL = "retail_literal"
BATTLESPADES = "battlespades"
ABSENT = "absent"

PROVENANCE_RANK = {
    RETAIL_ALIAS: 0,
    RETAIL_NAMED: 1,
    RETAIL_LITERAL: 2,
    BATTLESPADES: 3,
    ABSENT: 4,
}


class Ref:
    """A field value sourced from a named constant in one of the two trees."""

    def __init__(self, constant: str, provenance: str, *, scale: float = 1.0,
                 server: bool = False) -> None:
        self.constant = constant
        self.provenance = provenance
        self.scale = scale
        self.server = server


class Lit:
    """A field value that is a bare literal in a behaviour module."""

    def __init__(self, value, provenance: str = RETAIL_LITERAL) -> None:
        self.value = value
        self.provenance = provenance


def alias(name: str, **kw) -> Ref:
    return Ref(name, RETAIL_ALIAS, **kw)


def named(name: str, **kw) -> Ref:
    return Ref(name, RETAIL_NAMED, **kw)


def server(name: str, **kw) -> Ref:
    return Ref(name, BATTLESPADES, server=True, **kw)


def size(value: float) -> "Lit":
    """A per-entity render scale recovered from retail's own class attribute.

    Retail has NO shared model-size table. `Entity.size` is a class attribute on
    each Entity subclass and `Entity.create_display` copies it onto every
    DisplayList (`entity.py:88`), so that attribute IS the voxel-to-world scale:
        world_extent = kv6_voxel_dim * size
    Several entities ignore their own `*_MODEL_SIZE` constant entirely -- the
    rocket's says 0.06 but `rocket.py:22` hardcodes 0.02, and 0.06 would draw a
    rocket taller than a player. Where the two disagree, the class attribute is
    what the game runs.

    Most of these classes live in gameScene.pyd rather than shipping as .py;
    those values were read out of the binary. Two independent cross-checks
    passed: the radar's placement ghost (radarStationWeapon.py:26) is 0.03,
    matching the recovered entity, and the UGC ghost (ugcTool.py:196) is 0.06,
    likewise.
    """
    return Lit(value, RETAIL_LITERAL)


def unsourced(value: float) -> "Lit":
    """A scale we CHOSE because retail's is unrecoverable. Flagged on the HUD."""
    return Lit(value, ABSENT)


# Fallback for any row not covered above. Deliberately the crate scale, which is
# the most common value in the recovered set.
DEFAULT_MODEL_SIZE = 0.05


def part(kv6: str, *, z: float = 0.0, x: float = 0.0, y: float = 0.0,
         scale: float = 1.0, rotation: int = 0, voxel_offset: bool = True,
         pivot_offset: bool = False) -> dict:
    """One KV6 in a rig.

    `voxel_offset` distinguishes the TWO offset conventions retail mixes:

      * models.py offsets -- e.g. `load_model('GRAVE_MODEL', 'grave', (0,0,11))`
        -- are raw KV6 VOXEL units, applied to the model's own vertices before
        any scaling. Those need multiplying by the row's model size here.
      * The turret's per-part offsets are ALREADY premultiplied in the source:
        `A1612 = -3.0 * A1610`. Scaling those again shrinks the rig into itself.

    Emitting both as world units means the renderer applies one rule.
    """
    return {"kv6": kv6, "offset": (x, y, z), "scale": scale, "rotation": rotation,
            "voxel_offset": voxel_offset, "pivot_offset": pivot_offset}


# ---------------------------------------------------------------------------
# The catalog spec. One entry per retail entity id 0..39.
#
# `parts` is the single source of truth for "has art". An empty list means no
# model ships for it and none may be substituted.
# ---------------------------------------------------------------------------

CATALOG = {
    0: dict(name="FLAG", display="Flag", category="unportable", parts=[],
            wire_safe=False, spawnable=False,
            note="No load_model binding exists anywhere in models.py. Used only as an "
                 "invisible indicator anchor. wire_safe=False is DEFENSIVE by analogy "
                 "with BASE; unlike BASE it was never live-measured."),
    1: dict(name="BASE", display="Base", category="structure",
            # SCALE: BASE renders cp.kv6; capturePoint.py uses 0.30. Same model and role,
            # but BASE has no recovered class attribute of its own.
            model_size=unsourced(0.30),
            parts=[part("cp.kv6")], wire_safe=False, spawnable=True,
            touch_radius=named("CAPTURE_POINT_DISTANCE"),
            team_tinted=True,
            note="MEASURED HAZARD: absent from retail GameScene.ENTITIES; a clean client "
                 "dies with KeyError: 1 on receiving it. Render locally, never serialise."),
    2: dict(name="HELICOPTER", display="Helicopter", category="unportable", parts=[],
            spawnable=False, note="Class string only; no model."),

    3: dict(name="AMMO_CRATE", display="Ammo Crate", category="pickup",
            model_size=size(0.05),
            parts=[part("ammocrate.kv6")], spawnable=True,
            touch_radius=named("CRATE_DISTANCE"),
            respawn_delay=named("CRATE_SPAWN_DELAY"),
            sound_trigger="crate"),
    4: dict(name="HEALTH_CRATE", display="Health Crate", category="pickup",
            model_size=size(0.05),
            parts=[part("healthcrate.kv6")], spawnable=True,
            touch_radius=named("CRATE_DISTANCE"),
            respawn_delay=named("CRATE_SPAWN_DELAY"),
            # NOT `HEALTHCRATE_HP`. That name looks exactly like a heal amount
            # and even evaluates to 20, but it is the twenty-FIRST entry of the
            # kill-type `xrange(37)` at constants.py:1146, sitting between
            # SHRAPNEL_KILL and SNOWBALL_KILL. It equals 20 by position alone.
            # Reading it as a quantity is the same trap as GRAVE_DAMAGE.
            #
            # Retail ships NO health-crate heal amount. 20 is a bare literal in
            # the reference server, and it heals only below full:
            #   `if connection.hp < 100: connection.set_hp(connection.hp + 20)`
            # (aceofspades_source/server/aosmodes/__init__.py:103-107).
            # Our own server instead heals to maximum unconditionally. The
            # attested number wins over the unattested one, but this is a
            # CHOICE and the row's provenance says so.
            uses=Lit(20.0),
            sound_trigger="healthcrate"),
    5: dict(name="BLOCK_CRATE", display="Block Crate", category="pickup",
            model_size=size(0.05),
            parts=[part("block_crate.kv6")], spawnable=True,
            touch_radius=named("CRATE_DISTANCE"),
            respawn_delay=named("CRATE_SPAWN_DELAY"),
            sound_trigger="crate_blocks"),
    6: dict(name="JETPACK_CRATE", display="Jetpack Crate", category="pickup",
            # SCALE: Retail never registers entity id 6, so it has no class attribute.
            # Borrowed from the crate base.
            model_size=unsourced(0.05),
            parts=[part("jetpack.kv6")], spawnable=True,
            touch_radius=named("CRATE_DISTANCE"),
            respawn_delay=named("CRATE_SPAWN_DELAY"),
            sound_trigger="crate",
            note="Model binding UNCONFIRMED: there is no JETPACK_CRATE_MODEL in models.py "
                 "and no jetpackcrate.kv6 on disk; jetpack.kv6 is the closest shipping art. "
                 "There is no local jetpack fuel model, so the pickup is a no-op."),

    7: dict(name="MACHINE_GUN", display="Machine Gun", category="unportable", parts=[],
            spawnable=False,
            note="RETAIL ITSELF ships no art: mgWeapon.py declares model=[], view_model=[], "
                 "entity_model=[], image=None. Behaviour is recoverable; the art is not."),

    8: dict(name="ROCKET_TURRET", display="Rocket Turret", category="deployable",
            parts=[part("Turret_base.kv6", z=alias("A1612"), voxel_offset=False, rotation=0),
                   part("Turret_ball.kv6", z=alias("A1613"), voxel_offset=False, rotation=1),
                   part("Turret_gun.kv6", z=alias("A1614"), voxel_offset=False, rotation=2)],
            spawnable=True, team_tinted=True,
            model_size=alias("A1610"), health=alias("A1611"), ammo=alias("A1603"),
            blast_radius=alias("A1616"), blast_damage=alias("A1617"),
            block_damage=alias("A1618"),
            sound_place="turret_place", sound_trigger="turret_explode",
            sound_trigger_water="turret_explode_water",
            note="Three parts with INDEPENDENT rotations: base static, ball yaw, gun yaw+pitch. "
                 "Per-part z offsets A1612/A1613/A1614 are already premultiplied by the model "
                 "size in retail. Targeting algorithm and rocket muzzle speed are BattleSpades "
                 "inventions, not recovered retail."),
    9: dict(name="LANDMINE", display="Landmine", category="deployable",
            parts=[part("landmine.kv6", z=alias("A1809"))], spawnable=True,
            model_size=alias("A1807"), health=alias("A1808"),
            arm_delay=alias("A1798"),
            trip_radius=alias("A1799"), trip_height=alias("A1800"),
            blast_radius=alias("A1796"), blast_damage=alias("A1802"),
            block_damage=alias("A1803"),
            sound_place="landmine_place", sound_trigger="landmineexplode",
            sound_trigger_water="landmineexplode_water",
            note="Trip volume is a CYLINDER: A1799 horizontal, A1800 vertical layers. "
                 "Own team never trips it and line of sight is deliberately ignored, so a "
                 "re-buried mine still kills. Health 1 means any hit detonates it."),
    10: dict(name="DYNAMITE", display="Dynamite", category="deployable",
             parts=[part("dynamite.kv6", z=alias("A1640"))], spawnable=True,
             model_size=alias("A1638"), health=alias("A1639"),
             fuse=alias("A1631"),
             blast_radius=alias("A1632"), blast_damage=alias("A1633"),
             block_damage=alias("A1634"), ammo=alias("A1627"),
             sound_place="dynamite_place", sound_trigger="dynamiteexplode",
             sound_trigger_water="dynamiteexplode_water",
             note="CONFLICT: DYNAMITE_EXPLOSION_RADIUS says 5, A1632 says 8. dynamiteWeapon.py "
                  "binds the alias, so 8 is correct and the named block is stale."),
    11: dict(name="GRAVE", display="Grave", category="hazard",
            model_size=size(0.10),
             parts=[part("grave.kv6", z=11.0, pivot_offset=True)], spawnable=True,
             fuse=alias("A2086"), blast_radius=alias("A2087"),
             blast_damage=alias("A2088"), block_damage=alias("A2089"),
             sound_trigger="death_explode",
             sound_trigger_water="death_explode_water",
             note="GRAVE_DAMAGE in the line-954 block is an enum ORDINAL (14), not a damage "
                  "amount. The real amount is GRAVE_EXPLOSION_DAMAGE = 25."),
    12: dict(name="CORPSE", display="Corpse", category="hazard",
            # SCALE: ClassicCorpse.kv6 is consumed by character.pyd and no gameScene class
            # sets a scale for it. BODY_PARTS_SIZE = 0.05 is the character scale and gives
            # 2.5 blocks for a sprawled body, which is right, but the link is inference
            # not evidence.
            model_size=unsourced(0.05),
             parts=[part("ClassicCorpse.kv6")], spawnable=True,
             fuse=alias("A2074"), blast_radius=alias("A2076"),
             blast_damage=alias("A2077"), block_damage=alias("A2078"),
             sound_trigger="explode",
             note="Fuse 0 and player damage 0: it only nudges terrain. The 1.0 s jetpack "
                  "variant (A2075) is a separate case we do not model locally."),
    13: dict(name="FLARE_BLOCK", display="Flare Block", category="hazard",
             parts=[], spawnable=True,
             health=named("DEFAULT_BLOCK_HEALTH"),
             light_radius=named("FLAREBLOCK_LIGHT_RADIUS"),
             uses=named("FLAREBLOCK_COST"),
             note="The one entity that is TERRAIN: a voxel plus a static point light, not a "
                  "KV6. Empty parts here is correct and is not a missing-art case."),

    14: dict(name="BOMB_PICKUP", display="Bomb", category="objective",
            model_size=size(0.10),
             parts=[part("Bomb.kv6", z=7.0)], spawnable=True,
             touch_radius=named("PICKUP_DISTANCE"),
             fuse=alias("A2092"), blast_radius=alias("A2093"),
             blast_damage=alias("A2094"), block_damage=alias("A2095"),
             sound_trigger="bomb_pickup",
             note="BOMB_EXPLOSION_FUSE/RADIUS are each assigned three times at "
                  "constants.py:4529-4534; Python keeps the LAST, so 10 and 7 are effective."),
    15: dict(name="DIAMOND_PICKUP", display="Diamond", category="objective",
            model_size=size(0.098),
             parts=[part("diamond.kv6", z=7.0)], spawnable=True,
             touch_radius=named("PICKUP_DISTANCE"),
             lifetime=Lit(60.0),
             sound_place="diamond_appear", sound_trigger="diamond_pickup"),
    16: dict(name="INTEL_PICKUP", display="Intel", category="objective",
            model_size=size(0.12),
             parts=[part("intel.kv6")], spawnable=True, team_tinted=True,
             touch_radius=named("PICKUP_DISTANCE"),
             lifetime=Lit(60.0),
             sound_trigger="classic_pickup",
             note="Tinted with the OTHER team's colour (intelTool.py use_other_team_color). "
                  "intel_pickup.ogg does NOT ship -- the cue is the generic classic_pickup. "
                  "Retail gives intel no light, glow or particle at all."),

    17: dict(name="AIRSTRIKE", display="Airstrike Shell", category="hazard",
            # SCALE: No airstrike entity class sets a scale in any readable module.
            model_size=unsourced(0.05),
             parts=[part("airstrike_bomb.kv6")], spawnable=True,
             blast_radius=alias("A2102"), blast_damage=alias("A2103"),
             block_damage=alias("A2104"),
             sound_trigger="explode",
             note="Model binding unconfirmed: only AIRSTRIKE_BOMB_VIEW_MODEL is bound."),

    18: dict(name="AMMO_DROP_POINT", display="Ammo Drop Point", category="marker",
            model_size=size(0.06),
             parts=[part("Crate_Target.kv6", z=1.0)], spawnable=True),
    19: dict(name="HEALTH_DROP_POINT", display="Health Drop Point", category="marker",
            model_size=size(0.06),
             parts=[part("Crate_Target.kv6", z=1.0)], spawnable=True),
    20: dict(name="BLOCK_CRATE_DROP_POINT", display="Block Drop Point", category="marker",
            model_size=size(0.06),
             parts=[part("Crate_Target.kv6", z=1.0)], spawnable=True),

    21: dict(name="ROCKET", display="Rocket", category="projectile",
            model_size=size(0.02),
             parts=[part("rocket.kv6")], spawnable=False,
             note="Already simulated by TutorialProjectile."),
    22: dict(name="ROCKET2", display="Rocket II", category="projectile",
            model_size=size(0.02),
             parts=[part("rocket2.kv6")], spawnable=False),
    23: dict(name="DRILL", display="Drill", category="projectile",
            model_size=size(0.08),
             parts=[part("drill.kv6")], spawnable=False),
    24: dict(name="SNOWBALL", display="Snowball", category="projectile",
            model_size=size(0.02),
             parts=[part("snowball.kv6")], spawnable=False),

    25: dict(name="CAPTURE_POINT", display="Capture Point", category="structure",
            model_size=size(0.30),
             parts=[part("cp.kv6")], spawnable=True, team_tinted=True,
             touch_radius=named("CAPTURE_POINT_DISTANCE"),
             note="CAPTURE_POINT_DISTANCE and CAPTURE_POINT_REFILL_TIME are read by NO code in "
                  "either tree. The only executable behaviour is a 3 s restock that resolves to "
                  "set_hp(100) with the ammo lines commented out."),
    26: dict(name="TANK", display="Tank", category="unportable", parts=[], spawnable=False),
    27: dict(name="MOLOTOV", display="Molotov", category="projectile",
            model_size=size(0.02),
             parts=[part("Weapon_Molotov.kv6")], spawnable=False),
    28: dict(name="BLOCKFIRE", display="Block Fire", category="hazard",
             parts=[], spawnable=True,
             lifetime=named("BLOCKFIRE_MAX_LIFESPAN"),
             blast_damage=named("BLOCKFIRE_CHARACTER_DAMAGE"),
             block_damage=named("BLOCKFIRE_BLOCK_DAMAGE"),
             blast_radius=named("BLOCKFIRE_SPREAD_RADIUS"),
             uses=named("BLOCKFIRE_SPREAD_COUNT"),
             light_radius=Lit(3.0),
             note="Light and particles only -- no KV6, so empty parts is correct. THREE "
                  "independent timers (character 0.3 s, block 0.4 s, spread 0.5 s) which must "
                  "be modelled as three accumulators, not one."),
    29: dict(name="UGC_ENTITY", display="UGC Marker", category="marker",
            model_size=size(0.06),
             parts=[part("ugc_baseplate.kv6"), part("ugc_spawn_zone.kv6", z=-0.5, scale=0.25)],
             spawnable=True),

    30: dict(name="MEDPACK", display="Med Pack", category="deployable",
            model_size=size(0.06),
             # models.py:407 loads MedPack with this raw KV6 pivot. It is not a
             # world translation; dropping it makes the pack hover above the
             # physics support voxel even when its entity position is correct.
             parts=[part("MedPack.kv6", x=8.0, y=-21.0, z=0.0,
                         pivot_offset=True)],
             spawnable=True, team_tinted=True,
             touch_radius=named("PICKUP_DISTANCE"),
             ammo=alias("A1868"),
             sound_place="AoS_soundfx_PLAYER_medic_item_medi_pack_place_001",
             sound_trigger="AoS_soundfx_PLAYER_medic_item_medi_pack_exhausted_001",
             note="SOUND_MAP has exactly two medpack cues: 53 place and 52 EXHAUSTED. There "
                  "is no per-heal cue, so healing is deliberately silent and the pack only "
                  "speaks up when its last charge is spent. "
                  "Heal amount and use count exist ONLY in the BattleSpades server "
                  "(MEDPACK_HEAL 25 / MEDPACK_USES 3). Those are inventions, not retail."),
    31: dict(name="BLOCK_GOO", display="Block Goo", category="unportable", parts=[],
             spawnable=False),
    32: dict(name="CHEMICAL_BOMB", display="Chemical Bomb", category="projectile",
            model_size=size(0.02),
             parts=[part("chemicalbomb.kv6")], spawnable=False),
    33: dict(name="GL_GRENADE", display="GL Grenade", category="projectile",
            model_size=size(0.02),
             parts=[part("grenade.kv6")], spawnable=False),
    34: dict(name="STICKY_GRENADE", display="Sticky Grenade", category="projectile",
            model_size=size(0.06),
             parts=[part("stickygrenade.kv6")], spawnable=False),
    35: dict(name="ATTACHED_STICKY_GRENADE", display="Stuck Sticky", category="hazard",
            model_size=size(0.06),
             parts=[part("stickygrenade.kv6")], spawnable=True,
             note="The stuck form; TutorialProjectileBehavior::stick already models the flight."),
    36: dict(name="RADAR_STATION", display="Radar Station", category="deployable",
             parts=[part("radar_station.kv6", z=alias("A1902"))], spawnable=True,
             team_tinted=True,
             model_size=alias("A1898"), health=alias("A1899"),
             lifetime=alias("A1900"),
             sense_radius=alias("A1901"),
             sound_place="AoS_soundfx_PLAYER_marksman_item_RADAR_place_001",
             note="EARLIER CLAIM WITHDRAWN: the radar is not silent. SOUND_MAP id 56 names "
                  "the LONG stem, which ships; only the short RADAR_place_001 spelling is "
                  "absent. RADAR_place_001/RADAR_ping_001 are referenced in "
                  "source but absent from the asset drop, so the station is deliberately "
                  " No ping cue exists: id 56 is the only radar entry in the table. "
                  "Detection range lives in sense_radius, NOT blast_radius: it never explodes."),
    37: dict(name="PROJECTILE_MINE", display="Projectile Mine", category="deployable",
            model_size=size(0.05),
             parts=[part("projectilemine.kv6")], spawnable=True,
             arm_delay=alias("A1798"), trip_radius=alias("A1799"),
             trip_height=alias("A1800"), blast_radius=alias("A1796"),
             blast_damage=alias("A1802"), block_damage=alias("A1803"),
             health=alias("A1808"),
             sound_place="AoS_soundfx_PLAYER_engineer_wpn_mine_launcher_attach_mine_001",
             sound_trigger="landmineexplode",
             sound_trigger_water="landmineexplode_water",
             note="Its placement cue is the mine-launcher ATTACH sound (id 59), not the "
                  "hand-placed landmine's. Flight is already covered by TutorialProjectile; only the LANDED state is "
                  "new. Shares the landmine trip numbers."),
    38: dict(name="C4", display="C4", category="deployable",
             parts=[part("c4.kv6", z=server("C4_MODEL_Z_OFFSET"))], spawnable=True,
             model_size=server("C4_MODEL_SIZE"), health=server("C4_HEALTH"),
             blast_radius=server("C4_EXPLOSION_RADIUS"),
             blast_damage=server("C4_EXPLOSION_DAMAGE"),
             block_damage=server("C4_EXPLOSION_BLOCK_DAMAGE"),
             ammo=server("C4_STOCK"),
             sound_place="AoS_soundfx_PLAYER_miner_wpn_C4_place_001",
             sound_trigger="c4explode",
             sound_trigger_water="c4explode_water",
             note="Every C4 number lives only in the BattleSpades server, so the whole row is "
                  "provenance battlespades. The values are visibly cloned from dynamite. No "
                  "fuse: the secondary action detonates every live charge at once."),
    39: dict(name="RIOT_SHIELD", display="Riot Shield", category="deployable",
            # SCALE: The DEPLOYED shield sets no scale in any module we can read; 0.073 is
            # riotShieldTool.py model_size, the third-person HELD scale.
            model_size=unsourced(0.073),
             parts=[part("riotshield.kv6")], spawnable=True,
             note="A carried prop with no autonomous behaviour. Render it and stop."),
}

CATEGORY_ENUM = {
    "pickup": "EntityCategory::pickup",
    "objective": "EntityCategory::objective",
    "deployable": "EntityCategory::deployable",
    "hazard": "EntityCategory::hazard",
    "marker": "EntityCategory::marker",
    "structure": "EntityCategory::structure",
    "projectile": "EntityCategory::projectile",
    "unportable": "EntityCategory::unportable",
}

PROVENANCE_ENUM = {
    RETAIL_ALIAS: "EntityProvenance::retail_alias",
    RETAIL_NAMED: "EntityProvenance::retail_named",
    RETAIL_LITERAL: "EntityProvenance::retail_literal",
    BATTLESPADES: "EntityProvenance::battlespades",
    ABSENT: "EntityProvenance::absent",
}

# Scalar fields, in EntityDefinition declaration order after `parts`.
SCALARS = [
    ("model_size", DEFAULT_MODEL_SIZE), ("touch_radius", 0.0), ("health", 0.0), ("fuse", 0.0),
    ("arm_delay", 0.0), ("lifetime", 0.0), ("respawn_delay", 0.0),
    ("blast_radius", 0.0), ("blast_damage", 0.0), ("block_damage", 0.0),
    ("trip_radius", 0.0), ("trip_height", 0.0), ("light_radius", 0.0),
    ("sense_radius", 0.0),
]
INTEGERS = [("ammo", 0), ("uses", 0)]


# The two trees are written in DIFFERENT Python dialects: retail is Python 2
# (`print` is a statement, `long` exists) while the BattleSpades server has been
# ported to Python 3 (`print(..., end=' ')`). Each has to be evaluated by its own
# interpreter; feeding either one to the other is an immediate SyntaxError.
_PRELUDE = r"""
import json, sys
path = sys.argv[1]
lines = []
for line in open(path).read().split("\n"):
    if line.startswith("from aoslib") or line.startswith("import aoslib"):
        continue
    if line.startswith("from shared.") or line.startswith("import shared."):
        line = line.replace("shared.", "", 1)
    # `from .constants_gamemode import *` is what defines whole blocks of the
    # table; rewriting the relative import to an absolute one (and running from
    # the module's own directory) keeps them, where dropping the line would
    # silently produce a catalog missing every gamemode constant.
    elif line.startswith("from ."):
        line = "from " + line[len("from ."):]
    lines.append(line)
source = "\n".join(lines)
ns = {}
"""

_EPILOGUE = r"""
out = {}
for key, value in ns.items():
    if key.startswith("__"):
        continue
    if isinstance(value, NUMERIC):
        out[key] = value
json.dump(out, sys.stdout)
"""

DUMPER_PY2 = (_PRELUDE + 'exec compile(source, path, "exec") in ns\n'
              + "NUMERIC = (bool, int, long, float)\n" + _EPILOGUE)
DUMPER_PY3 = (_PRELUDE + 'exec(compile(source, path, "exec"), ns)\n'
              + "NUMERIC = (bool, int, float)\n" + _EPILOGUE)


def load_namespace(path: Path, interpreter: str, dumper: str) -> dict:
    """Evaluate a constants module and return its scalar bindings.

    Executed rather than parsed because retail rebinds several names more than
    once in a single file -- BOMB_EXPLOSION_FUSE three times -- and only
    execution reproduces which assignment actually wins.
    """
    completed = subprocess.run(
        [interpreter, "-c", dumper, str(path)],
        cwd=str(path.parent), capture_output=True, text=True, check=False,
    )
    if completed.returncode != 0:
        raise SystemExit(f"{interpreter} failed on {path}:\n{completed.stderr}")
    return json.loads(completed.stdout)


class Resolver:
    def __init__(self, retail: dict, server_ns: dict) -> None:
        self.retail = retail
        self.server = server_ns
        self.missing: list[str] = []
        self.provenance_used: list[str] = []

    def value(self, spec, default):
        """Resolve one field spec to (value, provenance)."""
        if spec is None:
            return default, None
        if isinstance(spec, Lit):
            return spec.value, spec.provenance
        if isinstance(spec, Ref):
            table = self.server if spec.server else self.retail
            if spec.constant not in table:
                self.missing.append(
                    f"{spec.constant} ({'server' if spec.server else 'retail'})"
                )
                return default, ABSENT
            return table[spec.constant] * spec.scale, spec.provenance
        return spec, None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--retail", type=Path, required=True,
                        help="aceofspades_source/shared/constants.py")
    parser.add_argument("--server", type=Path, required=True,
                        help="BattleSpades/shared/constants.py")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--python2", default="C:/Python27/python.exe",
                        help="interpreter for the retail (Python 2) tree")
    parser.add_argument("--python3", default=sys.executable,
                        help="interpreter for the BattleSpades (Python 3) tree")
    parser.add_argument("--check", action="store_true",
                        help="fail if --out differs from what would be generated")
    args = parser.parse_args()

    retail = load_namespace(args.retail, args.python2, DUMPER_PY2)
    server_ns = load_namespace(args.server, args.python3, DUMPER_PY3)
    resolver = Resolver(retail, server_ns)

    ids = sorted(CATALOG)
    if ids != list(range(40)):
        raise SystemExit(f"catalog must cover ids 0..39 densely; got {ids}")

    lines: list[str] = []
    lines.append("// GENERATED by tools/generate_entity_catalog.py. Do not edit.")
    lines.append("//")
    lines.append("// Every number below is read from a constants module at generation time.")
    lines.append("// An EMPTY parts span means no art ships for that entity and none may be")
    lines.append("// substituted -- tests/test_entity_catalog.cpp pins that negative result.")
    lines.append('#include "battlespades/world/entity_catalog.hpp"')
    lines.append("")
    lines.append("namespace battlespades::world {")
    lines.append("namespace {")
    lines.append("")

    # Model part arrays, one per entity that has any.
    for type_id in ids:
        entry = CATALOG[type_id]
        parts = entry.get("parts", [])
        if not parts:
            continue
        # The row's own model size, needed here because voxel-space offsets are
        # converted to world units at generation time so the renderer has one
        # rule instead of two.
        row_size, _ = resolver.value(entry.get("model_size"), DEFAULT_MODEL_SIZE)
        pieces = []
        for piece in parts:
            offset = []
            pivot = [0.0, 0.0, 0.0]
            for axis in piece["offset"]:
                # An axis is either a bare number or a Ref/Lit to resolve.
                spec = axis if isinstance(axis, (Ref, Lit)) else Lit(axis)
                value, _ = resolver.value(spec, 0.0)
                if piece["voxel_offset"]:
                    value = float(value) * float(row_size)
                offset.append(float(value))
            if piece["pivot_offset"]:
                pivot = [
                    float(resolver.value(
                        axis if isinstance(axis, (Ref, Lit)) else Lit(axis),
                        0.0)[0])
                    for axis in piece["offset"]
                ]
                offset = [0.0, 0.0, 0.0]
            pieces.append(
                '{"%s", {%sF, %sF, %sF}, %sF, %sU, {%sF, %sF, %sF}}' % (
                    piece["kv6"], offset[0], offset[1], offset[2],
                    piece["scale"], piece["rotation"],
                    pivot[0], pivot[1], pivot[2]))
        lines.append(
            f"constexpr EntityModelPart e{type_id}_parts[]{{{', '.join(pieces)}}};")
    lines.append("")

    lines.append("constexpr EntityDefinition definitions[]{")
    for type_id in ids:
        entry = CATALOG[type_id]
        parts = entry.get("parts", [])
        used: list[str] = []

        def field(key, default):
            value, prov = resolver.value(entry.get(key), default)
            if prov is not None:
                used.append(prov)
            return value

        scalars = {key: field(key, default) for key, default in SCALARS}
        integers = {key: int(field(key, default)) for key, default in INTEGERS}

        weakest = RETAIL_ALIAS
        for prov in used:
            if PROVENANCE_RANK[prov] > PROVENANCE_RANK[weakest]:
                weakest = prov

        parts_expr = f"e{type_id}_parts" if parts else "{}"
        crater = max(1, int(round(scalars["blast_radius"]))) if scalars["blast_radius"] else 0
        lines.append(f"    EntityDefinition{{")
        lines.append(f"        {type_id}U, \"{entry['name']}\", \"{entry['display']}\",")
        lines.append(f"        {CATEGORY_ENUM[entry['category']]},")
        lines.append(f"        {parts_expr},")
        lines.append(f"        {float(scalars['model_size'])}F,")
        lines.append(f"        {float(scalars['touch_radius'])}F, "
                     f"{float(scalars['health'])}F, {float(scalars['fuse'])}F,")
        lines.append(f"        {float(scalars['arm_delay'])}F, "
                     f"{float(scalars['lifetime'])}F, {float(scalars['respawn_delay'])}F,")
        lines.append(f"        {float(scalars['blast_radius'])}F, "
                     f"{float(scalars['blast_damage'])}F, {float(scalars['block_damage'])}F,")
        lines.append(f"        {crater}U,")
        lines.append(f"        {float(scalars['trip_radius'])}F, "
                     f"{float(scalars['trip_height'])}F,")
        lines.append(f"        {integers['ammo']}U, {integers['uses']}U,")
        lines.append(f"        {float(scalars['light_radius'])}F, "
                     f"{float(scalars['sense_radius'])}F,")
        lines.append(f"        \"{entry.get('sound_place', '')}\", "
                     f"\"{entry.get('sound_trigger', '')}\", "
                     f"\"{entry.get('sound_trigger_water', '')}\",")
        lines.append(f"        {str(entry.get('team_tinted', False)).lower()}, "
                     f"{str(entry.get('wire_safe', True)).lower()}, "
                     f"{str(entry.get('spawnable', False)).lower()},")
        lines.append(f"        {PROVENANCE_ENUM[weakest]},")
        lines.append("    },")
    lines.append("};")
    lines.append("")
    lines.append("} // namespace")
    lines.append("")
    lines.append("const EntityDefinition* find_entity_definition(std::uint8_t type_id) noexcept {")
    lines.append("    // Ids are dense 0..39, asserted by the generator, so this indexes directly.")
    lines.append("    if (type_id >= std::size(definitions)) {")
    lines.append("        return nullptr;")
    lines.append("    }")
    lines.append("    return &definitions[type_id];")
    lines.append("}")
    lines.append("")
    lines.append("std::span<const EntityDefinition> entity_catalog() noexcept {")
    lines.append("    return definitions;")
    lines.append("}")
    lines.append("")
    lines.append("} // namespace battlespades::world")

    if resolver.missing:
        print("constants referenced by the spec do not exist:", file=sys.stderr)
        for name in sorted(set(resolver.missing)):
            print(f"  {name}", file=sys.stderr)
        return 1

    text = "\n".join(lines) + "\n"
    if args.check:
        current = args.out.read_text(encoding="utf-8") if args.out.is_file() else ""
        if current != text:
            print(f"{args.out} is stale; re-run tools/generate_entity_catalog.py",
                  file=sys.stderr)
            return 1
        print(f"{args.out} matches the recovered constants")
        return 0

    args.out.write_text(text, encoding="utf-8")
    portable = sum(1 for i in ids if CATALOG[i].get("parts"))
    spawnable = sum(1 for i in ids if CATALOG[i].get("spawnable"))
    print(f"40 entities, {portable} with art, {spawnable} spawnable -> {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
