#!/usr/bin/env python3
"""Recover the retail profile-stat and rank tables into a checked-in C++ include.

The generated file has no runtime dependency on the decompiled Python client.
Run this tool only when the cited retail sources change, then review the diff.
"""

from __future__ import annotations

import ast
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RETAIL = ROOT.parent / "aceofspades_source" / "shared"
CONSTANTS = RETAIL / "constants.py"
PROFILE = RETAIL / "constants_playerprofile.py"
OUTPUT = ROOT / "src" / "frontend" / "player_progression.generated.inc"

CATEGORIES = {
    "CATEGORY_GENERAL": "GENERAL",
    "CATEGORY_WEAPON_ACCURACY": "WEAPON_ACCURACY",
    "CATEGORY_WEAPON_POINTS": "WEAPON_POINTS",
    "CATEGORY_SOLDIER": "SOLDIER",
    "CATEGORY_SCOUT": "SCOUT",
    "CATEGORY_ENGINEER": "ENGINEER2",
    "CATEGORY_MINER": "MINER",
    "CATEGORY_GANGSTER": "GANGSTER",
    "CATEGORY_CLASSIC": "CLASSIC",  # Retail's A2362 localization alias.
    "CATEGORY_ZOMBIE": "ZOMBIE",
    "CATEGORY_SPECIALIST": "SPECIALIST",
    "CATEGORY_MEDIC": "MEDIC",
    "CATEGORY_TDM": "TDM_TITLE",
    "CATEGORY_VIP": "VIP_MODE_TITLE",
    "CATEGORY_TC": "TC_TITLE",
    "CATEGORY_OCC": "OCCUPATION_MODE_TITLE",
    "CATEGORY_DIA": "DIAMOND_MINE_TITLE",
    "CATEGORY_CTF": "CTF_TITLE",
    "CATEGORY_ZOM": "ZOMBIE_MODE_TITLE",
    "CATEGORY_DEM": "DEMOLITION_TITLE",
    "CATEGORY_MH": "MULTIHILL_TITLE",
    "CATEGORY_MAPS": "HOURS_PLAYED",
}

SUMMARY = [
    ("SOLDIER", "LEVEL_CRITERIA_SOLDIER"),
    ("SCOUT", "LEVEL_CRITERIA_SCOUT"),
    ("ENGINEER2", "LEVEL_CRITERIA_ENGINEER"),
    ("MINER", "LEVEL_CRITERIA_MINER"),
    ("GANGSTER", "LEVEL_CRITERIA_GANGSTER"),
    ("SPECIALIST", "LEVEL_CRITERIA_SPECIALIST"),
    ("MEDIC", "LEVEL_CRITERIA_MEDIC"),
    ("TDM_TITLE", "LEVEL_CRITERIA_TDM"),
    ("CTF_TITLE", "LEVEL_CRITERIA_CTF"),
    ("DIAMOND_MINE_TITLE", "LEVEL_CRITERIA_DIA"),
    ("DEMOLITION_TITLE", "LEVEL_CRITERIA_DEMO"),
    ("MULTIHILL_TITLE", "LEVEL_CRITERIA_MH"),
    ("OCCUPATION_MODE_TITLE", "LEVEL_CRITERIA_OCC"),
    ("TC_TITLE", "LEVEL_CRITERIA_TC"),
    ("VIP_MODE_TITLE", "LEVEL_CRITERIA_VIP"),
    ("ZOMBIE_MODE_TITLE", "LEVEL_CRITERIA_ZOM"),
    ("CLASSIC", "LEVEL_CRITERIA_CLASSIC"),
]


def score_ids(text: str) -> dict[str, int]:
    match = re.search(
        r"(?ms)^NO_SCORE_REASON,\s*(.*?)\s*=\s*xrange\(MAX_NOOF_SCORE_REASONS\)",
        text,
    )
    if not match:
        raise RuntimeError("retail score-reason tuple not found")
    names = ["NO_SCORE_REASON"] + re.findall(r"[A-Z][A-Z0-9_]*", match.group(1))
    if len(names) != 251:
        raise RuntimeError(f"expected 251 score ordinals, found {len(names)}")
    # Deliberately keep the last duplicate. Retail assigns the tuple directly,
    # so SPECIALIST_AUTOPISTOL_KILLS resolves to ordinal 250, not 238.
    return {name: index for index, name in enumerate(names)}


def score_labels(text: str, ids: dict[str, int]) -> dict[int, str]:
    block = re.search(r"(?ms)^SCORE_REASON_CODES\s*=\s*\{(.*?)^\}", text)
    if not block:
        raise RuntimeError("SCORE_REASON_CODES not found")
    labels: dict[int, str] = {}
    for name, value in re.findall(r"(?m)^\s*([A-Z][A-Z0-9_]*)\s*:\s*'([^']*)'", block.group(1)):
        if name in ids and value:
            labels[ids[name]] = value
    return labels


def literal(node: ast.AST, default: object) -> object:
    if isinstance(node, ast.Constant):
        return node.value
    return default


def profile_stats(text: str, ids: dict[str, int], labels: dict[int, str]) -> list[tuple]:
    block = re.search(
        r"(?ms)^PLAYER_PROFILE_CATEGORY_STATS\s*=\s*(\[.*?\])\s*\n\s*A3084",
        text,
    )
    if not block:
        raise RuntimeError("PLAYER_PROFILE_CATEGORY_STATS not found")
    expression = ast.parse(block.group(1), mode="eval").body
    assert isinstance(expression, ast.List)
    result = []
    for node in expression.elts:
        if not isinstance(node, ast.Call) or len(node.args) < 2:
            raise RuntimeError("unexpected profile-stat expression")
        code_name = ast.unparse(node.args[0])
        category_name = ast.unparse(node.args[1])
        if code_name not in ids or category_name not in CATEGORIES:
            raise RuntimeError(f"unresolved profile stat {code_name}/{category_name}")
        options = {keyword.arg: keyword.value for keyword in node.keywords if keyword.arg}
        show_bar = bool(literal(node.args[2], False)) if len(node.args) > 2 else False
        show_bar = bool(literal(options.get("show_bar", ast.Constant(show_bar)), show_bar))
        show_score = bool(literal(options.get("show_score", ast.Constant(False)), False))
        requirement = float(literal(options.get("level1_requirement", ast.Constant(10)), 10))
        multiplier = float(literal(options.get("multiplier", ast.Constant(1.15)), 1.15))
        modifier = "none"
        value_modifier = options.get("value_modifier")
        if value_modifier is not None:
            name = ast.unparse(value_modifier)
            if name.endswith("value_modifier_mins_to_hours"):
                modifier = "minutes_to_hours"
            elif name.endswith("value_modifier_percentage"):
                modifier = "percentage"
            else:
                raise RuntimeError(f"unknown value modifier {name}")
        code = ids[code_name]
        label = labels.get(code, code_name)
        result.append(
            (code, CATEGORIES[category_name], label, show_bar, show_score,
             requirement, multiplier, modifier)
        )
    return result


def rank_tables(text: str, ids: dict[str, int]) -> list[tuple]:
    module = ast.parse(text)
    dictionaries: dict[str, list[tuple[str, list[tuple[int, int]]]]] = {}
    for statement in module.body:
        if not isinstance(statement, ast.Assign) or len(statement.targets) != 1:
            continue
        target = statement.targets[0]
        if not isinstance(target, ast.Name) or not target.id.startswith("LEVEL_CRITERIA_"):
            continue
        if not isinstance(statement.value, ast.Dict):
            continue
        levels = []
        for key, value in zip(statement.value.keys, statement.value.values):
            level_name = ast.unparse(key)
            if not isinstance(value, (ast.Tuple, ast.List)):
                raise RuntimeError(f"unexpected criteria value in {target.id}")
            criteria = []
            for call in value.elts:
                if not isinstance(call, ast.Call) or len(call.args) != 2:
                    raise RuntimeError(f"unexpected criterion in {target.id}")
                stat_name = ast.unparse(call.args[0])
                requirement = int(literal(call.args[1], 0))
                criteria.append((ids[stat_name], requirement))
            levels.append((level_name, criteria))
        dictionaries[target.id] = levels

    result = []
    for label, table_name in SUMMARY:
        levels = dictionaries[table_name]
        intermediate = next((items for key, items in levels if key == "RANK_INTERMEDIATE"), [])
        advanced = next((items for key, items in levels if key == "RANK_ADVANCED"), [])
        if table_name == "LEVEL_CRITERIA_VIP":
            # Decompiled retail contains a duplicate INTERMEDIATE key. The two
            # tuples are plainly the 5/20 intermediate/advanced progression;
            # retain both while removing the original crash at high VIP rank.
            duplicates = [items for key, items in levels if key == "RANK_INTERMEDIATE"]
            intermediate, advanced = duplicates[0], duplicates[1]
        result.append((label, intermediate, advanced))
    return result


def cpp_bool(value: bool) -> str:
    return "true" if value else "false"


def criterion_array(items: list[tuple[int, int]]) -> str:
    padded = items + [(0, 0)] * (3 - len(items))
    body = ", ".join(f"RetailRankCriterion{{{stat}U, {level}U}}" for stat, level in padded)
    return f"std::array{{{body}}}"


def generate() -> str:
    constants_text = CONSTANTS.read_text(encoding="utf-8")
    profile_text = PROFILE.read_text(encoding="utf-8")
    ids = score_ids(constants_text)
    labels = score_labels(constants_text, ids)
    stats = profile_stats(profile_text, ids, labels)
    ranks = rank_tables(profile_text, ids)
    label_rows = [labels.get(index, "") for index in range(251)]
    lines = [
        "// Generated by tools/generate_player_progression.py from the recovered retail client.",
        "// Do not hand-edit: regenerate and review the diff.",
        "constexpr std::array<std::string_view, 251U> retail_score_reason_labels{",
        "    " + ",\n    ".join(f'"{label}"' for label in label_rows),
        "};",
        "",
        f"constexpr std::array<RetailProfileStatDefinition, {len(stats)}U> retail_profile_stats{{{{",
    ]
    for code, category, label, show_bar, show_score, requirement, multiplier, modifier in stats:
        lines.append(
            "    RetailProfileStatDefinition{" +
            f'{code}U, "{category}", "{label}", {cpp_bool(show_bar)}, '
            f'{cpp_bool(show_score)}, {requirement:.17g}, {multiplier:.17g}, '
            f"RetailValueModifier::{modifier}" + "},"
        )
    lines.extend(["}};", "", f"constexpr std::array<RetailRankDefinition, {len(ranks)}U> retail_rank_definitions{{{{"])
    for label, intermediate, advanced in ranks:
        lines.append(
            f'    RetailRankDefinition{{"{label}", {criterion_array(intermediate)}, '
            f"{len(intermediate)}U, {criterion_array(advanced)}, {len(advanced)}U}},"
        )
    lines.extend(["}};", ""])
    return "\n".join(lines)


if __name__ == "__main__":
    OUTPUT.write_text(generate(), encoding="utf-8", newline="\n")
    print(f"wrote {OUTPUT}")
