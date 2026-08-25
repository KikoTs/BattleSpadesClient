"""Generate native weapon data from the server and recovered retail classes.

The server remains authoritative for accepted damage, cadence and ammunition.
The recovered Python 2 weapon subclasses are evaluated statically so local
prediction/presentation retains their exact A#### aliases, inherited defaults,
accuracy, recoil, ammo tuple and interaction flags. No retail module executes.
"""

from __future__ import annotations

import argparse
import ast
import hashlib
import importlib
import json
import operator
import re
import sys
from dataclasses import asdict, dataclass, is_dataclass
from pathlib import Path
from typing import Any


CATEGORY_CPP = {
    "melee": "WeaponCategory::melee",
    "rifle": "WeaponCategory::rifle",
    "smg": "WeaponCategory::smg",
    "shotgun": "WeaponCategory::shotgun",
    "sniper": "WeaponCategory::sniper",
    "pistol": "WeaponCategory::pistol",
    "mg": "WeaponCategory::machine_gun",
    "grenade": "WeaponCategory::grenade",
    "launcher": "WeaponCategory::launcher",
    "deployable": "WeaponCategory::deployable",
    "objective": "WeaponCategory::objective",
    "special": "WeaponCategory::special",
}

# Retail cues recovered from aoslib/weapons/*.py method bodies.
#
# Only class attributes are machine-walkable; every string below is a
# literal passed to self.play_sound(...) inside a method, or a value
# inherited from DiggingTool/Weapon. Recovered by reading all 71 retail
# weapon modules and cross-checked against the shipped sounds/ tree by
# tests/test_weapon_audio_map.cpp, which fails if any cue names a file
# that does not exist.
#
# An empty role means retail is deliberately silent there. Do not invent
# a substitute: silence is the parity-correct behaviour.
#
# Each role holds exactly one group. A few retail tools drive several
# loops from one state machine (the drill has a projectile loop and a
# drilling loop; the chemical bomb has dissolve and burn phases); only
# the primary is modelled here. See docs/WEAPON_AUDIO.md.
RECOVERED_SOUND_SETS: dict[int, dict[str, str]] = {
    # 0 PICKAXE (pickAxeTool.PickAxeTool)
    0: {"melee_miss": "woosh", "melee_hit_block": "hitground_pickaxe", "melee_hit_player": "whack_pickaxe"},
    # 1 KNIFE (knifeTool.KnifeTool)
    1: {"melee_miss": "woosh", "melee_hit_block": "hitground_knife_damage", "melee_hit_player": "whack_knife"},
    # 2 SPADE (spadeTool.SpadeTool)
    2: {"melee_miss": "woosh", "melee_hit_block": "hitground", "melee_hit_player": "whack"},
    # 3 SUPERSPADE (superSpadeTool.SuperSpadeTool)
    3: {"melee_miss": "woosh", "melee_hit_block": "hitground_super", "melee_hit_player": "whack"},
    # 4 CLASSIC_SPADE (classicSpadeTool.ClassicSpadeTool)
    4: {"melee_miss": "woosh", "melee_hit_block": "hitground", "melee_hit_player": "whack"},
    # 6 RIFLE (classicRifleWeapon.ClassicRifleWeapon)
    6: {"empty_fire": "empty"},
    # 7 SMG (smgWeapon.SMGWeapon)
    7: {"fire_loop": "smg_fire_loop", "fire_tail": "smg_fire_tail", "empty_fire": "empty"},
    # 8 MINIGUN (minigunWeapon.MinigunWeapon)
    8: {"fire_loop": "minigun_fire_loop", "fire_tail": "minigun_fire_tail", "spin_loop": "minigun_loop", "empty_fire": "empty"},
    # 9 SHOTGUN (shotgunWeapon.ShotgunWeapon)
    9: {"empty_fire": "empty"},
    # 10 SHOTGUN2 (shotgun2Weapon.Shotgun2Weapon)
    10: {"empty_fire": "empty"},
    # 11 GRENADE (grenadeTool.GrenadeTool)
    11: {"pin": "pin", "throw_release": "woosh"},
    # 12 RPG (rpgWeapon.RPGWeapon)
    12: {"empty_fire": "empty", "tool_loop": "rocket_projectile"},
    # 13 RPG2 (rpg2Weapon.RPG2Weapon)
    13: {"empty_fire": "empty", "tool_loop": "rocket_trip_projectile"},
    # 14 DRILLGUN (drillgunWeapon.DrillgunWeapon)
    14: {"empty_fire": "empty", "tool_loop": "drill_projectile", "tool_extra": "drill_drilling_exp"},
    # 15 MG (mgWeapon.MGWeapon)
    15: {"fire_loop": "smg_fire_loop", "fire_tail": "smg_fire_tail", "empty_fire": "empty"},
    # 16 ROCKET_TURRET (rocketTurretWeapon.RocketTurretWeapon)
    16: {"empty_fire": "empty", "tool_loop_start": "turret_aim_start", "tool_loop": "turret_aiming_lp", "tool_loop_stop": "turret_aim_stop", "tool_extra": "turret_lockon"},
    # 17 PISTOL (pistolWeapon.PistolWeapon)
    17: {"empty_fire": "empty"},
    # 18 SNIPER (sniperWeapon.SniperWeapon)
    18: {"empty_fire": "empty"},
    # 19 SNIPER2 (sniper2Weapon.Sniper2Weapon)
    19: {"empty_fire": "empty"},
    # 20 LANDMINE (landmineWeapon.LandmineWeapon)
    20: {"empty_fire": "empty"},
    # 21 DYNAMITE (dynamiteWeapon.DynamiteWeapon)
    21: {"empty_fire": "empty"},
    # 24 ZOMBIEHAND (zombieHandTool.ZombieHandTool)
    24: {"melee_miss": "woosh", "melee_hit_block": "hitground_zombie", "melee_hit_player": "zombiehand_hit"},
    # 25 BOMB (bombTool.BombTool)
    25: {"throw_release": "bomb_drop"},
    # 29 SNOWBLOWER (snowBlowerWeapon.SnowBlowerWeapon)
    29: {"empty_fire": "empty", "tool_loop": "snowcan_eng_lp"},
    # 31 CLASSIC_GRENADE (classicGrenadeTool.ClassicGrenadeTool)
    31: {"pin": "pin", "throw_release": "woosh"},
    # 32 ANTIPERSONNEL_GRENADE (antipersonnelGrenadeTool.AntipersonnelGrenadeTool)
    32: {"pin": "pin", "throw_release": "woosh"},
    # 33 MOLOTOV (molotovWeapon.MolotovWeapon)
    33: {"throw_release": "molotov_throw", "tool_loop": "molotov_player_on_fire_lp", "tool_loop_stop": "molotov_pl_on_fire_burnout"},
    # 34 CROWBAR (crowbarTool.CrowbarTool)
    34: {"melee_miss": "woosh", "melee_hit_block": "hitground_crowbar_damage", "melee_hit_player": "whack_crowbar"},
    # 35 TOMMYGUN (tommyGunWeapon.TommyGunWeapon)
    35: {"fire_loop": "tommygun_fire_loop", "fire_tail": "tommygun_fire_tail", "empty_fire": "empty"},
    # 36 SNUB_PISTOL (snubPistolWeapon.SnubPistolWeapon)
    36: {"empty_fire": "empty"},
    # 37 CLASSIC_SHOTGUN (classicShotgunWeapon.ClassicShotgunWeapon)
    37: {"empty_fire": "empty"},
    # 38 CLASSIC_SMG (classicSmgWeapon.ClassicSmgWeapon)
    38: {"fire_loop": "classic_smg_fire_loop", "fire_tail": "classic_smg_fire_tail", "empty_fire": "empty"},
    # 43 PAINTBRUSH (paintbrushTool.PaintbrushTool)
    43: {"tool_loop": "ugc_colour_spraying"},
    # 44 UGC_PICKAXE (ugcPickAxeTool.UGCPickAxeTool)
    44: {"melee_miss": "woosh", "melee_hit_block": "hitground_pickaxe", "melee_hit_player": "whack_pickaxe"},
    # 45 UGC_SUPERSPADE (ugcSuperSpadeTool.UGCSuperSpadeTool)
    45: {"melee_miss": "woosh", "melee_hit_block": "hitground_super", "melee_hit_player": "whack"},
    # 46 UGC_RPG2 (ugcRPG2Weapon.UGCRPG2Weapon)
    46: {"tool_loop": "rocket_trip_projectile"},
    # 47 UGC_DRILLGUN (ugcDrillgunWeapon.UGCDrillgunWeapon)
    47: {"empty_fire": "empty", "tool_loop": "drill_projectile", "tool_extra": "drill_drilling_exp"},
    # 48 UGC_SNOWBLOWER (ugcSnowBlowerWeapon.UGCSnowBlowerWeapon)
    48: {"empty_fire": "empty", "tool_loop": "snowcan_eng_lp"},
    # 49 RIOTSTICK (riotStickTool.RiotStickTool)
    49: {"melee_miss": "AoS_soundfx_PLAYER_shared_wpn_melee_swipe_001-010", "melee_hit_block": "hitground_riotstick_damage", "melee_hit_player": "whack_riotstick"},
    # 50 MACHETE (macheteTool.MacheteTool)
    50: {"melee_miss": "AoS_soundfx_PLAYER_shared_wpn_melee_swipe_001-010", "melee_hit_block": "hitground_machete_damage", "melee_hit_player": "whack_machete"},
    # 51 MEDPACK (medPackWeapon.MedPackWeapon)
    51: {"empty_fire": "empty"},
    # 52 RIOTSHIELD (riotShieldTool.RiotShieldTool)
    52: {"melee_miss": "AoS_soundfx_PLAYER_shared_wpn_melee_swipe_001-010", "melee_hit_block": "hitground_riotshield_damage", "melee_hit_player": "whack_riotshield"},
    # 53 AUTOMATIC_PISTOL (autoPistolWeapon.AutoPistolWeapon)
    53: {"fire_loop": "AoS_soundfx_PLAYER_marksman_wpn_AUTOPISTOL_fire_loop_001", "fire_tail": "AoS_soundfx_PLAYER_marksman_wpn_AUTOPISTOL_fire_end_001-006", "empty_fire": "empty"},
    # 54 CHEMICALBOMB (chemicalbombWeapon.ChemicalBombWeapon)
    54: {"throw_release": "AoS_soundfx_PLAYER_specialist_wpn_chem_bomb_throw_001-005", "tool_loop": "AoS_soundfx_PLAYER_specialist_wpn_chem_bomb_dissolve_loop_001", "tool_loop_stop": "AoS_soundfx_PLAYER_specialist_wpn_chem_bomb_dissolve_end_001"},
    # 55 GRENADE_LAUNCHER_WEAPON (grenadeLauncherWeapon.GrenadeLauncherWeapon)
    55: {"empty_fire": "empty"},
    # 56 RADAR_STATION (radarStationWeapon.RadarStationWeapon)
    56: {"empty_fire": "empty"},
    # 57 STICKY_GRENADE (stickygrenadeWeapon.StickyGrenadeWeapon)
    57: {"pin": "AoS_soundfx_PLAYER_specialist_wpn_sticky_grenade_remove_pin_001-005", "throw_release": "AoS_soundfx_PLAYER_specialist_wpn_sticky_grenade_throw_001-005", "tool_loop": "AoS_soundfx_PLAYER_specialist_wpn_sticky_grenade_countdown_001"},
    # 58 MINE_LAUNCHER (mineLauncherWeapon.MineLauncherWeapon)
    58: {"empty_fire": "empty"},
    # 59 C4 (c4Weapon.C4Weapon)
    59: {"empty_fire": "empty"},
    # 60 ASSAULT_RIFLE (assaultRifleWeapon.AssaultRifleWeapon)
    60: {"empty_fire": "empty"},
    # 61 LIGHT_MACHINE_GUN (lightMachineGunWeapon.LightMachineGunWeapon)
    61: {"fire_loop": "AoS_soundfx_PLAYER_medic_wpn_LMG_loop_fire_001-002", "fire_tail": "AoS_soundfx_PLAYER_medic_wpn_LMG_loop_end_001-004", "empty_fire": "empty"},
    # 62 AUTO_SHOTGUN (autoShotgunWeapon.AutoShotgunWeapon)
    62: {"empty_fire": "empty"},
    # 63 BLOCK_SUCKER (blockSuckerWeapon.BlockSuckerWeapon)
    63: {"tool_loop_start": "AoS_soundfx_PLAYER_miner_wpn_block_sucker_start_001", "tool_loop": "AoS_soundfx_PLAYER_miner_wpn_block_sucker_loop_001", "tool_loop_stop": "AoS_soundfx_PLAYER_miner_wpn_block_sucker_end_001", "tool_extra": "AoS_soundfx_PLAYER_miner_wpn_block_sucker_block_aquired_001-006"},
}

# The original classes share behavior through Weapon/DiggingTool/GrenadeTool/
# PrefabTool inheritance. These mechanism rows retain the exceptional state
# machines that cannot be inferred from broad server categories alone.
MECHANISM_CPP = {
    0: "melee", 1: "melee", 2: "melee", 3: "melee", 4: "melee",
    5: "block_builder", 6: "firearm_semi", 7: "firearm_automatic",
    8: "firearm_spinup", 9: "shotgun_semi", 10: "shotgun_semi",
    11: "cooked_throwable", 12: "oriented_launcher",
    13: "oriented_launcher", 14: "oriented_launcher",
    15: "deployed_machine_gun", 16: "deployable", 17: "firearm_semi",
    18: "firearm_semi", 19: "firearm_semi", 20: "deployable",
    21: "deployable", 22: "flare_builder", 23: "prefab_builder",
    24: "melee", 25: "objective", 26: "objective",
    27: "block_builder", 28: "prefab_builder",
    29: "oriented_launcher", 30: "objective",
    31: "cooked_throwable", 32: "cooked_throwable",
    33: "charged_throwable", 34: "melee",
    35: "firearm_automatic", 36: "firearm_semi",
    37: "shotgun_semi", 38: "firearm_automatic", 39: "inert",
    40: "inert", 41: "ugc_entity", 42: "ugc_prefab_editor",
    43: "paintbrush", 44: "melee", 45: "melee",
    46: "oriented_launcher", 47: "oriented_launcher",
    48: "oriented_launcher", 49: "melee", 50: "melee",
    51: "deployable", 52: "melee", 53: "firearm_automatic",
    54: "charged_throwable", 55: "oriented_launcher",
    56: "deployable", 57: "charged_throwable",
    58: "oriented_launcher", 59: "c4", 60: "firearm_burst",
    61: "firearm_automatic", 62: "shotgun_automatic",
    63: "block_sucker", 64: "disguise",
}

# Additional constants used by projectile/entity code live outside the weapon
# subclasses. Prefix ownership keeps them attached to the tool that spawns or
# controls that behavior. Longer prefixes automatically mask shorter ones.
TOOL_CONSTANT_PREFIXES: dict[int, tuple[str, ...]] = {
    0: ("PICKAXE",), 1: ("KNIFE",), 2: ("SPADE",),
    3: ("SUPERSPADE",), 4: ("CLASSIC_SPADE",), 5: ("BLOCK",),
    6: ("RIFLE",), 7: ("SMG",), 8: ("MINIGUN",), 9: ("SHOTGUN",),
    10: ("SHOTGUN2",), 11: ("GRENADE",), 12: ("RPG", "ROCKET"),
    13: ("RPG2", "ROCKET2"), 14: ("DRILLGUN", "DRILL"), 15: ("MG",),
    16: ("ROCKET_TURRET",), 17: ("PISTOL",), 18: ("SNIPER",),
    19: ("SNIPER2",), 20: ("LANDMINE",), 21: ("DYNAMITE",),
    22: ("FLAREBLOCK",), 23: ("PREFAB",), 24: ("ZOMBIEHAND",),
    25: ("BOMB",), 26: ("DIAMOND",), 27: ("SHRAPNEL",),
    28: ("ZOMBIE_PREFAB",), 29: ("SNOWBLOWER", "SNOWBALL"),
    30: ("INTEL",), 31: ("CLASSIC_GRENADE",),
    32: ("ANTIPERSONNEL_GRENADE",), 33: ("MOLOTOV", "BLOCKFIRE"),
    34: ("CROWBAR",), 35: ("TOMMYGUN",), 36: ("SNUB_PISTOL",),
    37: ("CLASSIC_SHOTGUN",), 38: ("CLASSIC_SMG",), 39: ("NULL",),
    40: ("FAKE_PISTOL",), 41: ("UGCTOOL",), 42: ("UGC_PREFAB",),
    43: ("PAINTBRUSH",), 44: ("UGC_PICKAXE",),
    45: ("UGC_SUPERSPADE",), 46: ("UGC_RPG2", "UGC_ROCKET2"),
    47: ("UGC_DRILLGUN", "UGC_DRILL"),
    48: ("UGC_SNOWBLOWER", "UGC_SNOWBALL"), 49: ("RIOTSTICK",),
    50: ("MACHETE",), 51: ("MEDPACK",), 52: ("RIOTSHIELD",),
    53: ("AUTOMATIC_PISTOL",), 54: ("CHEMICALBOMB",),
    55: ("GRENADE_LAUNCHER",), 56: ("RADAR_STATION",),
    57: ("STICKY_GRENADE",), 58: ("MINE_LAUNCHER",), 59: ("C4",),
    60: ("ASSAULT_RIFLE",), 61: ("LIGHT_MACHINE_GUN",),
    62: ("AUTO_SHOTGUN",), 63: ("BLOCK_SUCKER",), 64: ("DISGUISE",),
}

# Exact primary explosion ownership. The server profile table predates several
# recovered block-damage values and leaves them at zero. Never search constants
# by suffix here: e.g. MOLOTOV also owns BLOCKFIRE_EXPLOSION_RADIUS=2 while the
# projectile's primary MOLOTOV_EXPLOSION_RADIUS is 4.
PRIMARY_EXPLOSION_PREFIX: dict[int, str] = {
    11: "GRENADE", 12: "ROCKET", 13: "ROCKET2", 14: "DRILL",
    16: "ROCKET_TURRET", 20: "LANDMINE", 21: "DYNAMITE", 25: "BOMB",
    29: "SNOWBALL", 31: "CLASSIC_GRENADE",
    32: "ANTIPERSONNEL_GRENADE", 33: "MOLOTOV",
    46: "UGC_ROCKET2", 47: "UGC_DRILL", 48: "UGC_SNOWBALL",
    54: "CHEMICALBOMB", 55: "GRENADE_LAUNCHER",
    57: "STICKY_GRENADE", 58: "LANDMINE", 59: "C4",
}

_BINARY_OPERATORS = {
    ast.Add: operator.add,
    ast.Sub: operator.sub,
    ast.Mult: operator.mul,
    ast.Div: operator.truediv,
    ast.FloorDiv: operator.floordiv,
}
_MISSING = object()


@dataclass(frozen=True)
class RetailClass:
    module: str
    class_name: str
    attributes: dict[str, Any]
    referenced_constants: frozenset[str]


@dataclass(frozen=True)
class ModelReference:
    """A models.load_model global resolved without importing retail code."""

    stem: str
    offset: tuple[float, float, float]


def cpp_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=True)


def cpp_float(value: float) -> str:
    text = format(float(value), ".17g")
    return text if "." in text or "e" in text.lower() else text + ".0"


def cpp_bool(value: Any) -> str:
    return "true" if bool(value) else "false"


def json_default(value: Any) -> Any:
    if is_dataclass(value):
        return asdict(value)
    return list(value)


def cpp_optional_number(value: Any) -> str:
    return "std::nullopt" if not _is_number(value) else cpp_float(float(value))


def cpp_optional_integer(value: Any, cpp_type: str) -> str:
    if not _is_number(value):
        return "std::nullopt"
    return f"{cpp_type}{{{int(value)}}}"


def cpp_optional_damage(value: Any) -> str:
    if not isinstance(value, (tuple, list)) or len(value) < 5 or not all(
        _is_number(item) for item in value[:5]
    ):
        return "std::nullopt"
    items = ", ".join(cpp_float(float(item)) for item in value[:5])
    return f"std::array<double, 5U>{{{items}}}"


def primary_explosion_value(constants: Any, tool_id: int, suffix: str,
                            fallback: float) -> float:
    """Resolve one primary blast field through an exact, audited name."""
    prefix = PRIMARY_EXPLOSION_PREFIX.get(tool_id)
    if prefix is None:
        return float(fallback)
    value = getattr(constants, f"{prefix}_EXPLOSION_{suffix}", fallback)
    return float(value) if _is_number(value) else float(fallback)


def cpp_vector3(value: Any) -> str:
    """Render a recovered Vector3/tuple attribute as a fixed C++ array."""
    if not isinstance(value, (tuple, list)) or len(value) < 3 or not all(
        _is_number(item) for item in value[:3]
    ):
        value = (0.0, 0.0, 0.0)
    items = ", ".join(cpp_float(float(item)) for item in value[:3])
    return f"std::array<double, 3U>{{{items}}}"


def relative_asset(path: Path | None, asset_root: Path) -> str:
    return "" if path is None else path.relative_to(asset_root).as_posix()


def case_insensitive_files(root: Path) -> dict[str, Path]:
    if not root.is_dir():
        return {}
    return {entry.name.lower(): entry for entry in root.iterdir() if entry.is_file()}


def _is_number(value: Any) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def _constant_value(value: Any) -> tuple[str, list[float]] | None:
    if isinstance(value, bool):
        return "boolean", [1.0 if value else 0.0]
    if _is_number(value):
        return "number", [float(value)]
    if isinstance(value, (tuple, list)) and 1 <= len(value) <= 4 and all(
        _is_number(item) for item in value
    ):
        return "numeric_tuple", [float(item) for item in value]
    return None


def _evaluate(node: ast.AST, environment: dict[str, Any]) -> Any:
    if isinstance(node, ast.Constant):
        return node.value
    if isinstance(node, ast.Name):
        return environment.get(node.id, _MISSING)
    if isinstance(node, (ast.Tuple, ast.List)):
        values = [_evaluate(item, environment) for item in node.elts]
        return _MISSING if _MISSING in values else tuple(values)
    if isinstance(node, ast.UnaryOp):
        value = _evaluate(node.operand, environment)
        if value is _MISSING or not _is_number(value):
            return _MISSING
        if isinstance(node.op, ast.USub):
            return -value
        if isinstance(node.op, ast.UAdd):
            return +value
        return _MISSING
    if isinstance(node, ast.BinOp):
        left = _evaluate(node.left, environment)
        right = _evaluate(node.right, environment)
        operation = _BINARY_OPERATORS.get(type(node.op))
        if operation is None or left is _MISSING or right is _MISSING:
            return _MISSING
        try:
            return operation(left, right)
        except (TypeError, ZeroDivisionError):
            return _MISSING
    # Vector3 literals are useful presentation constants and safe to retain as
    # tuples without importing pyglet/shared native modules.
    if isinstance(node, ast.Call) and isinstance(node.func, ast.Name) and node.func.id == "Vector3":
        values = [_evaluate(item, environment) for item in node.args]
        if len(values) == 3 and all(_is_number(item) for item in values):
            return tuple(values)
    return _MISSING


def _alias_names(constants_path: Path) -> dict[str, str]:
    tree = ast.parse(constants_path.read_text(encoding="utf-8-sig"))
    aliases: dict[str, str] = {}
    for statement in tree.body:
        if not isinstance(statement, ast.Assign) or len(statement.targets) != 1:
            continue
        target = statement.targets[0]
        if (isinstance(target, ast.Name) and target.id.startswith("A") and
                target.id[1:].isdigit() and isinstance(statement.value, ast.Name)):
            aliases[target.id] = statement.value.id
    return aliases


def _model_symbols(retail_root: Path) -> dict[str, ModelReference]:
    """Recover literal load_model globals, including their authored offsets."""
    path = retail_root / "aoslib" / "models.py"
    result: dict[str, ModelReference] = {}
    # models.py is valid Python 2 and contains bare print statements, so a
    # Python 3 AST cannot parse the file. load_model declarations themselves
    # have a deliberately simple, stable two-string prefix.
    pattern = re.compile(
        r"load_model\(\s*(['\"])(?P<global>[^'\"]+)\1\s*,\s*"
        r"(['\"])(?P<stem>[^'\"]+)\3"
        r"(?:\s*,\s*\((?P<offset>[^()]*)\))?"
    )
    for match in pattern.finditer(path.read_text(encoding="utf-8-sig")):
        offset = (0.0, 0.0, 0.0)
        if match.group("offset"):
            try:
                values = ast.literal_eval("(" + match.group("offset") + ")")
                if len(values) == 3 and all(_is_number(value) for value in values):
                    offset = tuple(float(value) for value in values)
            except (SyntaxError, ValueError, TypeError):
                # Dynamic offsets belong to load_weapon() and are recovered
                # separately below; never guess a partly parsed expression.
                pass
        result[match.group("global")] = ModelReference(match.group("stem"), offset)
    if not result:
        raise RuntimeError("no recovered load_model declarations found")
    return result


def _weapon_registrations(retail_root: Path) -> tuple[tuple[Any, ...], ...]:
    """Recover the argument tuples models.load_models() feeds to load_weapon()."""
    text = (retail_root / "aoslib" / "models.py").read_text(encoding="utf-8-sig")
    match = re.search(
        r"for value in \((?P<body>.*?)\):\s*\n\s*load_weapon\(\*value\)",
        text,
        re.DOTALL,
    )
    if match is None:
        raise RuntimeError("recovered load_weapon registration list was not found")
    return ast.literal_eval("(" + match.group("body") + ")")


def _offset_argument(value: tuple[Any, ...], index: int) -> tuple[float, float, float]:
    """One optional load_weapon offset argument, defaulted exactly as retail does."""
    if len(value) <= index:
        return (0.0, 0.0, 0.0)
    offset = value[index]
    if not isinstance(offset, (tuple, list)) or len(offset) < 3:
        return (0.0, 0.0, 0.0)
    return (float(offset[0]), float(offset[1]), float(offset[2]))


def _conventional_weapon_offsets(retail_root: Path) -> dict[str, tuple[float, float, float]]:
    """Recover models.load_models() load_weapon tuples and its offset formula."""
    result: dict[str, tuple[float, float, float]] = {}
    for value in _weapon_registrations(retail_root):
        extra = _offset_argument(value, 2)
        result[str(value[0]).lower()] = (6.0 + extra[0], -18.0 + extra[1], extra[2])
    return result


def _load_weapon_symbols(retail_root: Path) -> dict[str, ModelReference]:
    """Recover the model globals load_weapon() builds by string formatting.

    `load_weapon` names its globals with `'%s_SIGHT' % upper` and friends, so a
    scan for literal load_model() arguments cannot see any of them. Sixteen
    conventional guns -- every classic iron-sight weapon plus both snipers --
    register their sight ONLY this way. Without these entries `sight` resolves
    to nothing on those rows, and because retail gates aiming on `sight`, the
    recovered ADS rule then silently denies aiming to the very weapons it
    exists to serve.
    """
    result: dict[str, ModelReference] = {}
    for value in _weapon_registrations(retail_root):
        name = str(value[0])
        upper = name.upper()
        # A gun with no extra sight parts passes '' rather than (); both are
        # empty when iterated, which is why retail can spell it either way.
        sight_extra = value[1] if len(value) >= 2 else ()
        extra_offset = _offset_argument(value, 2)
        sight_offset = _offset_argument(value, 3)
        sight_extra_offset = _offset_argument(value, 4)
        result[f"{upper}_TRACER"] = ModelReference(f"{name}tracer", (0.0, 0.0, 0.0))
        result[f"{upper}_CASING"] = ModelReference(f"{name}casing", (0.0, 0.0, 0.0))
        result[f"{upper}_VIEW_MODEL"] = ModelReference(name, (0.0, 0.0, 0.0))
        result[f"{upper}_MODEL"] = ModelReference(
            name, (6.0 + extra_offset[0], -18.0 + extra_offset[1], extra_offset[2]))
        result[f"{upper}_SIGHT"] = ModelReference(f"{name}_sight", sight_offset)
        for part in sight_extra:
            result[f"{upper}_{str(part).upper()}"] = ModelReference(
                f"{name}_sight_{part}", sight_extra_offset)
    return result


def _audio_symbols(retail_root: Path, seed: dict[str, Any]) -> dict[str, Any]:
    """Statically recover shared.constants_audio without importing retail."""
    path = retail_root / "shared" / "constants_audio.py"
    tree = ast.parse(path.read_text(encoding="utf-8-sig"))
    environment = dict(seed)
    recovered: dict[str, Any] = {}
    for statement in tree.body:
        if not isinstance(statement, ast.Assign) or len(statement.targets) != 1:
            continue
        target = statement.targets[0]
        if not isinstance(target, ast.Name):
            continue
        value = _evaluate(statement.value, environment)
        if value is _MISSING:
            continue
        environment[target.id] = value
        recovered[target.id] = value
    return recovered


def _retail_classes(retail_root: Path, constants: Any) -> dict[int, RetailClass]:
    weapon_root = retail_root / "aoslib" / "weapons"
    if not weapon_root.is_dir():
        raise RuntimeError(f"recovered weapon source is missing: {weapon_root}")
    trees: dict[str, ast.Module] = {}
    class_nodes: dict[str, tuple[str, ast.ClassDef]] = {}
    for path in weapon_root.glob("*.py"):
        tree = ast.parse(path.read_text(encoding="utf-8-sig"))
        trees[path.stem] = tree
        for statement in tree.body:
            if isinstance(statement, ast.ClassDef):
                class_nodes[statement.name] = (path.stem, statement)

    constant_environment = dict(vars(constants))
    constant_environment.update(_audio_symbols(retail_root, constant_environment))
    # Registration order follows load_models(): the load_weapon() loop runs
    # first, then the literal load_model() calls, so a literal declaration of
    # the same global is the later binding and must win.
    constant_environment.update(_load_weapon_symbols(retail_root))
    constant_environment.update(_model_symbols(retail_root))
    cache: dict[str, dict[str, Any]] = {}

    def resolve(class_name: str, active: frozenset[str] = frozenset()) -> dict[str, Any]:
        if class_name in cache:
            return dict(cache[class_name])
        if class_name in active or class_name not in class_nodes:
            return {}
        _, node = class_nodes[class_name]
        attributes: dict[str, Any] = {}
        for base in node.bases:
            if isinstance(base, ast.Name):
                attributes.update(resolve(base.id, active | {class_name}))
        environment = dict(constant_environment)
        environment.update(attributes)
        for statement in node.body:
            if not isinstance(statement, ast.Assign):
                continue
            value = _evaluate(statement.value, environment)
            if value is _MISSING:
                continue
            # CHAINED assignments bind more than one name at once, and retail
            # leans on them: `has_secondary = delay_secondary = True` is the
            # idiom the Tool family uses to declare a right-click action.
            # Requiring exactly one target silently dropped BOTH names on about
            # two dozen rows, so those tools were emitted as having no secondary
            # at all -- the data looked complete and was simply absent.
            for target in statement.targets:
                if isinstance(target, ast.Name):
                    attributes[target.id] = value
                    environment[target.id] = value
        cache[class_name] = dict(attributes)
        return attributes

    list_tree = trees.get("list")
    if list_tree is None:
        raise RuntimeError("recovered aoslib.weapons.list.py is missing")
    imported_modules: dict[str, str] = {}
    weapons_dict: ast.Dict | None = None
    for statement in list_tree.body:
        if isinstance(statement, ast.ImportFrom) and statement.module:
            for name in statement.names:
                imported_modules[name.asname or name.name] = statement.module
        if isinstance(statement, ast.Assign):
            if any(isinstance(target, ast.Name) and target.id == "WEAPONS"
                   for target in statement.targets) and isinstance(statement.value, ast.Dict):
                weapons_dict = statement.value
    if weapons_dict is None:
        raise RuntimeError("recovered WEAPONS registry was not found")

    result: dict[int, RetailClass] = {}
    for key, value in zip(weapons_dict.keys, weapons_dict.values):
        if not isinstance(key, ast.Name) or not isinstance(value, ast.Name):
            raise RuntimeError("unexpected recovered WEAPONS registry expression")
        tool_id = int(constant_environment[key.id])
        class_name = value.id
        module = imported_modules[class_name]
        tree = trees[module]
        referenced = frozenset(
            node.id for node in ast.walk(tree)
            if isinstance(node, ast.Name) and
            (node.id.isupper() or (node.id.startswith("A") and node.id[1:].isdigit()))
        )
        result[tool_id] = RetailClass(module, class_name, resolve(class_name), referenced)
    if set(result) != set(range(int(constants.NOOF_SELECTABLE_TOOLS))):
        raise RuntimeError("recovered WEAPONS registry must map exactly ids 0..64")
    return result


def _tool_constants(tool_id: int, retail: RetailClass, constants: Any,
                    aliases: dict[str, str]) -> list[tuple[str, str, list[float]]]:
    values = vars(constants)
    collected: dict[str, tuple[str, list[float]]] = {}

    # Constants directly referenced by this recovered module win over later
    # descriptive reassignments. A#### aliases preserve the executed value.
    for source_name in sorted(retail.referenced_constants):
        if source_name not in values:
            continue
        converted = _constant_value(values[source_name])
        if converted is None:
            continue
        display_name = aliases.get(source_name, source_name)
        collected[display_name] = converted

    all_prefixes = {prefix for prefixes in TOOL_CONSTANT_PREFIXES.values()
                    for prefix in prefixes}
    for prefix in TOOL_CONSTANT_PREFIXES[tool_id]:
        shadowing = tuple(
            other + "_" for other in all_prefixes
            if other != prefix and other.startswith(prefix + "_")
        )
        for name, value in values.items():
            if not name.startswith(prefix + "_") or name.startswith(shadowing):
                continue
            converted = _constant_value(value)
            if converted is not None and name not in collected:
                collected[name] = converted
    return [(name, kind, numbers) for name, (kind, numbers) in sorted(collected.items())]


def _retail_tuning_cpp(retail: RetailClass, profile: Any) -> str:
    attrs = retail.attributes
    damage = attrs.get("damage")
    ammo = attrs.get("ammo")
    ammo_values = ammo if isinstance(ammo, (tuple, list)) and len(ammo) >= 5 else None
    scalar_damage = damage if _is_number(damage) else attrs.get("block_damage")
    return "RetailWeaponTuning{" + ", ".join([
        cpp_string(retail.module),
        cpp_string(retail.class_name),
        "RetailDamageTuning{" + ", ".join([
            cpp_optional_damage(damage),
            cpp_optional_number(scalar_damage),
            cpp_optional_number(profile.base_damage if profile.is_melee else None),
        ]) + "}",
        "RetailAimTuning{" + ", ".join([
            cpp_optional_number(attrs.get("accuracy")),
            cpp_optional_number(attrs.get("accuracy_zoom")),
            cpp_optional_number(attrs.get("accuracy_min")),
            cpp_optional_number(attrs.get("accuracy_max")),
            cpp_optional_number(attrs.get("accuracy_spread_min")),
            cpp_optional_number(attrs.get("accuracy_spread_max")),
            cpp_optional_number(attrs.get("accuracy_spread_increase_per_shot")),
            cpp_optional_number(attrs.get("accuracy_spread_reduction_speed")),
            cpp_optional_number(attrs.get("recoil_up")),
            cpp_optional_number(attrs.get("recoil_side")),
            cpp_bool(attrs.get("variable_accuracy", False)),
        ]) + "}",
        "RetailAmmoTuning{" + ", ".join([
            cpp_optional_integer(ammo_values[0] if ammo_values else None, "std::uint16_t"),
            cpp_optional_integer(ammo_values[1] if ammo_values else None, "std::uint16_t"),
            cpp_optional_integer(ammo_values[2] if ammo_values else None, "std::uint16_t"),
            cpp_optional_integer(ammo_values[3] if ammo_values else None, "std::uint16_t"),
            cpp_optional_integer(ammo_values[4] if ammo_values else None, "std::uint16_t"),
            cpp_optional_integer(attrs.get("default_count"), "std::uint16_t"),
            cpp_optional_integer(attrs.get("initial_count"), "std::uint16_t"),
            cpp_optional_integer(attrs.get("restock_amount"), "std::uint16_t"),
            cpp_bool(attrs.get("clip_reload", False)),
        ]) + "}",
        "RetailUseTuning{" + ", ".join([
            cpp_optional_number(attrs.get("shoot_interval")),
            cpp_optional_number(attrs.get("secondary_shoot_interval")),
            cpp_optional_number(attrs.get("reload_time")),
            cpp_optional_number(attrs.get("delay") if _is_number(attrs.get("delay")) else None),
            cpp_optional_number(attrs.get("range")),
            cpp_optional_number(attrs.get("fuse")),
            cpp_optional_number(attrs.get("max_fuse")),
            cpp_optional_number(attrs.get("zoom")),
            cpp_optional_number(attrs.get("zoomed_sensitivity_factor")),
            cpp_optional_number(attrs.get("model_size")),
            cpp_optional_number(attrs.get("view_model_size")),
            cpp_vector3(attrs.get("sight_pos")),
            cpp_float(attrs["pin_scale"]) if _is_number(attrs.get("pin_scale")) else "0.0",
            cpp_optional_integer(attrs.get("show_crosshair"), "std::int16_t"),
            cpp_optional_integer(attrs.get("pellets"), "std::uint8_t"),
            cpp_bool(attrs.get("can_zoom", False)),
            cpp_bool(attrs.get("has_secondary", False)),
            cpp_bool(attrs.get("can_shoot_primary_while_sprinting", False)),
            cpp_bool(attrs.get("can_shoot_secondary_while_sprinting", False)),
            cpp_bool(attrs.get("stoppable", False)),
            cpp_bool(attrs.get("delay", False) if isinstance(attrs.get("delay"), bool) else False),
            cpp_bool(attrs.get("show_crosshair_centre", False)),
            cpp_bool(attrs.get("play_shoot_animation", True)),
            cpp_bool(attrs.get("use_team_color", False)),
        ]) + "}",
    ]) + "}"


def _constant_cpp(name: str, kind: str, numbers: list[float]) -> str:
    padded = numbers + [0.0] * (4 - len(numbers))
    values = ", ".join(cpp_float(value) for value in padded)
    return (
        f"RetailWeaponConstant{{{cpp_string(name)}, RetailConstantKind::{kind}, "
        f"{{{values}}}, {len(numbers)}U}}"
    )


def render(server_root: Path, client_root: Path, retail_root: Path) -> str:
    sys.path.insert(0, str(server_root))
    constants = importlib.import_module("shared.constants")
    game_constants = importlib.import_module("server.game_constants")

    selectable_count = int(constants.NOOF_SELECTABLE_TOOLS)
    if selectable_count != 65 or int(constants.NUMBER_OF_WEAPONS) != 66:
        raise RuntimeError("retail Protocol 168 tool sentinel changed")
    catalog = game_constants.WEAPON_CATALOG
    if set(catalog) != set(range(selectable_count)):
        raise RuntimeError("server WEAPON_CATALOG must contain exactly ids 0..64")
    if set(MECHANISM_CPP) != set(range(selectable_count)):
        raise RuntimeError("every selectable tool needs one native mechanism")

    retail_classes = _retail_classes(retail_root, constants)
    aliases = _alias_names(server_root / "shared" / "constants.py")
    asset_root = client_root / "assets" / "original"
    images = case_insensitive_files(asset_root / "png" / "ui" / "weapons")
    icons = case_insensitive_files(asset_root / "png" / "ui" / "icons" / "weapons")
    models = case_insensitive_files(asset_root / "kv6")
    conventional_offsets = _conventional_weapon_offsets(retail_root)
    selectable_when_empty = {int(value) for value in constants.SELECTABLE_ON_NO_AMMO_TOOLS}

    rows: list[str] = []
    constant_arrays: list[str] = []
    model_arrays: list[str] = []
    signature_rows: list[dict[str, object]] = []
    for tool_id in range(selectable_count):
        profile = catalog[tool_id]
        retail = retail_classes[tool_id]
        stem = str(constants.TOOL_FILE_NAMES[tool_id])
        image_mode = constants.TOOL_HAS_IMAGE[tool_id]
        image = images.get((stem + ".png").lower())
        icon = icons.get((stem + ".png").lower())
        model = models.get((stem + ".kv6").lower())
        team_colored = image_mode == 2
        blue = images.get((stem + "_blue.png").lower()) if team_colored else None
        green = images.get((stem + "_green.png").lower()) if team_colored else None
        neutral = images.get((stem + "_neutral.png").lower()) if team_colored else None
        first_person = neutral if team_colored else image
        # Retail HUD.draw_loadout_item_hud blits weapon.image.  Every weapon
        # class assigns that field from TOOL_IMAGES, whose source is
        # png/ui/weapons, not the separate 32x32 icon atlas used by menus and
        # notifications.  Using the compact atlas here made only the subset
        # with an icon entry look plausible and silently gave the rest a
        # different visual language.
        toolbar = first_person
        if bool(image_mode) and first_person is None:
            raise RuntimeError(f"tool {tool_id} ({stem}) is missing its retail image")
        if team_colored and (blue is None or green is None or neutral is None):
            raise RuntimeError("sniper team image set is incomplete")

        def model_part_list(attribute: str) -> list[tuple[str, tuple[float, float, float]]]:
            declared = retail.attributes.get(attribute, ())
            if isinstance(declared, (str, ModelReference)):
                declared = (declared,)
            if not isinstance(declared, (tuple, list)):
                return []
            resolved: list[tuple[str, tuple[float, float, float]]] = []
            for model_reference in declared:
                if isinstance(model_reference, ModelReference):
                    model_stem = model_reference.stem
                    offset = model_reference.offset
                elif isinstance(model_reference, str):
                    model_stem = model_reference
                    offset = (0.0, 0.0, 0.0)
                else:
                    continue
                path = models.get((model_stem + ".kv6").lower())
                if path is None:
                    raise RuntimeError(
                        f"tool {tool_id} {attribute} model is missing: {model_stem}.kv6"
                    )
                resolved.append((relative_asset(path, asset_root), offset))
            return resolved

        third_person_models = model_part_list("model")
        first_person_models = model_part_list("view_model")
        # Conventional firearms are registered by models.load_weapon() inside
        # a recovered Python-2 loop rather than literal load_model() calls.
        # Retail MODEL and VIEW_MODEL both point at the same KV6 stem there.
        fallback_model = relative_asset(model, asset_root)
        if not third_person_models and fallback_model:
            third_person_models = [(
                fallback_model,
                conventional_offsets.get(stem.lower(), (6.0, -18.0, 0.0)),
            )]
        if not first_person_models and fallback_model:
            first_person_models = [(fallback_model, (0.0, 0.0, 0.0))]

        def auxiliary_model(attribute: str, fallback_suffix: str) -> str:
            declared = retail.attributes.get(attribute)
            if declared is None:
                # The weapon (and everything it inherits from) declares no such
                # model, so it HAS none. Falling through to the stem-derived
                # name here invented assets: FAKE_PISTOL shares the pistol's
                # model stem, so it was handed the real pistol's sight and
                # appeared to support aiming that retail never gave it.
                return ""
            if isinstance(declared, ModelReference):
                # `sight = SEMI_SIGHT` binds a models.py global, not a stem.
                declared = declared.stem
            if isinstance(declared, str):
                path = models.get((declared + ".kv6").lower())
                if path is not None:
                    return relative_asset(path, asset_root)
            # Declared but unresolvable: the stem-derived name is the intended
            # asset under a different spelling.
            path = models.get((stem + fallback_suffix + ".kv6").lower())
            return relative_asset(path, asset_root)

        def auxiliary_part(attribute: str,
                           fallback_suffix: str) -> tuple[str, tuple[float, float, float]]:
            """One optional model plus the offset models.py loaded it with.

            Only the classic rifle's pin needs this today: load_weapon()
            registers SEMI_PIN with sight_extra_offset (0, 0, -0.5), and that
            offset moves the red bead by half a voxel at pin_scale -- the
            difference between the recovered eye-space tip and a wrong one.
            Every sight/casing/tracer offset in the tree is (0, 0, 0), which is
            why the plain auxiliary_model() path can keep dropping it.
            """
            declared = retail.attributes.get(attribute)
            offset = (0.0, 0.0, 0.0)
            if isinstance(declared, ModelReference):
                offset = declared.offset
            return auxiliary_model(attribute, fallback_suffix), offset

        def sound_name(attribute: str) -> str:
            cue = retail.attributes.get(attribute)
            if isinstance(cue, str):
                return cue
            if isinstance(cue, (tuple, list)) and cue and isinstance(cue[0], str):
                return cue[0]
            return ""

        def sound_pitch(attribute: str) -> tuple[float, float]:
            """Recover Character.play_sound cue slots 3/4 as semitones."""
            cue = retail.attributes.get(attribute)
            if isinstance(cue, (tuple, list)) and len(cue) > 4:
                minimum = cue[3]
                maximum = cue[4]
                if isinstance(minimum, (int, float)) and isinstance(maximum, (int, float)):
                    return float(minimum), float(maximum)
            return 0.0, 0.0

        shoot_sound = sound_name("shoot_sound")
        reload_sound = sound_name("reload_sound")
        reload_done_sound = sound_name("reload_done_sound")
        shoot_sound_pitch = sound_pitch("shoot_sound")
        reload_sound_pitch = sound_pitch("reload_sound")
        reload_done_sound_pitch = sound_pitch("reload_done_sound")
        recovered_sounds = RECOVERED_SOUND_SETS.get(tool_id, {})
        sound_set = [
            recovered_sounds.get(role, "")
            for role in (
                "fire_loop", "fire_tail", "spin_loop",
                "melee_miss", "melee_hit_block", "melee_hit_player",
                "empty_fire", "pin", "throw_release",
                "tool_loop_start", "tool_loop", "tool_loop_stop", "tool_extra",
            )
        ]
        sight_model = auxiliary_model("sight", "_sight")
        pin_model, pin_offset = auxiliary_part("pin", "_sight_pin")
        casing_model = auxiliary_model("casing", "casing")
        tracer_model = auxiliary_model("tracer", "tracer")
        model_arrays.append(
            f"constexpr std::array<WeaponModelPartDefinition, {len(third_person_models)}U> "
            f"third_person_models_{tool_id}{{{{"
            + ", ".join(
                "WeaponModelPartDefinition{" + cpp_string(path) + ", {" +
                ", ".join(cpp_float(value) + "F" for value in offset) + "}}"
                for path, offset in third_person_models
            )
            + "}};\n"
            + f"constexpr std::array<WeaponModelPartDefinition, {len(first_person_models)}U> "
            f"first_person_models_{tool_id}{{{{"
            + ", ".join(
                "WeaponModelPartDefinition{" + cpp_string(path) + ", {" +
                ", ".join(cpp_float(value) + "F" for value in offset) + "}}"
                for path, offset in first_person_models
            )
            + "}};"
        )

        extras = _tool_constants(tool_id, retail, constants, aliases)
        constant_arrays.append(
            f"constexpr std::array<RetailWeaponConstant, {len(extras)}U> constants_{tool_id}{{{{\n"
            + "\n".join(f"    {_constant_cpp(*item)}," for item in extras)
            + "\n}};"
        )
        damage_type = constants.TOOLS_DAMAGE_TYPE.get(tool_id)
        kill_type = constants.TOOLS_KILL_TYPE.get(tool_id)
        primary_damage = primary_explosion_value(
            constants, tool_id, "DAMAGE", profile.base_damage)
        primary_block_damage = primary_explosion_value(
            constants, tool_id, "BLOCK_DAMAGE", profile.block_damage)
        primary_blast_radius = primary_explosion_value(
            constants, tool_id, "RADIUS", profile.blast_radius)
        values = {
            "tool_id": tool_id,
            "symbolic_name": str(profile.name),
            "asset_stem": stem,
            "category": str(profile.category),
            "mechanism": MECHANISM_CPP[tool_id],
            "base_damage": primary_damage,
            "head_damage": float(profile.head_damage),
            "block_damage": primary_block_damage,
            "fire_interval": float(profile.fire_interval),
            "maximum_range": float(profile.max_range),
            "clip_size": int(profile.clip_size),
            "reserve_ammo": int(profile.reserve_ammo),
            "reload_time": float(profile.reload_time),
            "pellet_count": int(profile.pellet_count),
            "spread": float(profile.spread),
            "blast_radius": primary_blast_radius,
            "fuse_time": float(profile.fuse_time),
            "damage_type": -1 if damage_type is None else int(damage_type),
            "kill_type": -1 if kill_type is None else int(kill_type),
            "melee": bool(profile.is_melee),
            "projectile": bool(profile.is_projectile),
            "selectable_when_empty": tool_id in selectable_when_empty,
            "retail_image": bool(image_mode),
            "team_image": team_colored,
            "toolbar": relative_asset(toolbar, asset_root),
            "first_person": relative_asset(first_person, asset_root),
            "blue": relative_asset(blue, asset_root),
            "green": relative_asset(green, asset_root),
            "neutral": relative_asset(neutral, asset_root),
            "model": relative_asset(model, asset_root),
            "third_person_models": third_person_models,
            "first_person_models": first_person_models,
            "shoot_sound": shoot_sound,
            "shoot_sound_pitch": shoot_sound_pitch,
            "reload_sound": reload_sound,
            "reload_sound_pitch": reload_sound_pitch,
            "reload_done_sound": reload_done_sound,
            "reload_done_sound_pitch": reload_done_sound_pitch,
            "recovered_sounds": recovered_sounds,
            "sight_model": sight_model,
            "pin_model": pin_model,
            "pin_offset": pin_offset,
            "casing_model": casing_model,
            "tracer_model": tracer_model,
            "retail_module": retail.module,
            "retail_class": retail.class_name,
            "retail_attributes": retail.attributes,
            "constants": extras,
        }
        signature_rows.append(values)
        rows.append(
            "    WeaponDefinition{" + ", ".join([
                f"{tool_id}U", cpp_string(values["symbolic_name"]), cpp_string(stem),
                CATEGORY_CPP[str(profile.category)],
                f"WeaponMechanism::{MECHANISM_CPP[tool_id]}",
                cpp_float(values["base_damage"]),
                cpp_float(profile.head_damage), cpp_float(values["block_damage"]),
                cpp_float(profile.fire_interval), cpp_float(profile.max_range),
                f"{int(profile.clip_size)}U", f"{int(profile.reserve_ammo)}U",
                cpp_float(profile.reload_time), f"{int(profile.pellet_count)}U",
                cpp_float(profile.spread), cpp_float(values["blast_radius"]),
                cpp_float(profile.fuse_time), str(values["damage_type"]),
                str(values["kill_type"]), cpp_bool(profile.is_melee),
                cpp_bool(profile.is_projectile), cpp_bool(tool_id in selectable_when_empty),
                cpp_bool(bool(image_mode)), cpp_bool(team_colored),
                cpp_string(values["toolbar"]), cpp_string(values["first_person"]),
                cpp_string(values["blue"]), cpp_string(values["green"]),
                cpp_string(values["neutral"]), cpp_string(values["model"]),
                f"third_person_models_{tool_id}",
                f"first_person_models_{tool_id}",
                cpp_string(shoot_sound),
                "{" + ", ".join(cpp_float(value) + "F" for value in shoot_sound_pitch) + "}",
                cpp_string(reload_sound),
                "{" + ", ".join(cpp_float(value) + "F" for value in reload_sound_pitch) + "}",
                cpp_string(reload_done_sound),
                "{" + ", ".join(cpp_float(value) + "F" for value in reload_done_sound_pitch) + "}",
                "WeaponSoundSet{" + ", ".join(
                    cpp_string(value) for value in sound_set) + "}",
                cpp_string(sight_model),
                "WeaponModelPartDefinition{" + cpp_string(pin_model) + ", {" +
                ", ".join(cpp_float(value) + "F" for value in pin_offset) + "}}",
                cpp_string(casing_model), cpp_string(tracer_model),
                _retail_tuning_cpp(retail, profile), f"constants_{tool_id}",
            ]) + "},"
        )

    signature = hashlib.sha256(
        json.dumps(signature_rows, sort_keys=True, separators=(",", ":"),
                   default=json_default).encode("utf-8")
    ).hexdigest()
    return """// Generated by tools/generate_weapon_catalog.py. DO NOT EDIT.\n\
#include \"battlespades/world/weapon_catalog.hpp\"\n\n\
#include <algorithm>\n\
#include <array>\n\n\
namespace battlespades::world {\n\n\
namespace {\n\n""" + "\n\n".join(model_arrays) + "\n\n" + "\n\n".join(constant_arrays) + "\n\n" + """constexpr std::array<WeaponDefinition, selectable_tool_count> catalog{{\n""" + "\n".join(rows) + "\n}};\n\n" + f'constexpr std::string_view contract_sha256{{"{signature}"}};\n' + """\n\
static_assert(catalog.front().tool_id == 0U);\n\
static_assert(catalog.back().tool_id + 1U == no_selectable_tool);\n\n\
} // namespace\n\n\
std::span<const WeaponDefinition> weapon_catalog() noexcept { return catalog; }\n\n\
const WeaponDefinition* find_weapon_definition(std::uint8_t tool_id) noexcept {\n\
    return tool_id < catalog.size() ? &catalog[tool_id] : nullptr;\n\
}\n\n\
bool valid_selectable_tool(std::uint8_t tool_id) noexcept {\n\
    return find_weapon_definition(tool_id) != nullptr;\n\
}\n\n\
std::string_view weapon_catalog_contract_sha256() noexcept {\n\
    return contract_sha256;\n\
}\n\n\
const RetailWeaponConstant*\n\
find_retail_weapon_constant(const WeaponDefinition& weapon,\n\
                            std::string_view name) noexcept {\n\
    const auto found = std::ranges::find(weapon.constants, name,\n\
                                         &RetailWeaponConstant::name);\n\
    return found == weapon.constants.end() ? nullptr : &*found;\n\
}\n\n\
} // namespace battlespades::world\n"""


def main() -> int:
    parser = argparse.ArgumentParser()
    client_root = Path(__file__).resolve().parents[1]
    parser.add_argument("--server-root", type=Path, default=client_root.parent / "BattleSpades")
    parser.add_argument("--retail-source-root", type=Path,
                        default=client_root.parent / "aceofspades_source")
    parser.add_argument(
        "--output", type=Path,
        default=client_root / "src" / "world" / "weapon_catalog.generated.cpp",
    )
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    generated = render(args.server_root.resolve(), client_root,
                       args.retail_source_root.resolve())
    if args.check:
        if not args.output.is_file() or args.output.read_text(encoding="utf-8") != generated:
            print("weapon catalog is stale; regenerate it", file=sys.stderr)
            return 1
        return 0
    args.output.write_text(generated, encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())


