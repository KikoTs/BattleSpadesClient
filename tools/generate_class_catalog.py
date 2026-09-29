#!/usr/bin/env python3
"""Generate the native class/body/loadout catalogue from the retail tables."""

from __future__ import annotations

import argparse
import ast
import hashlib
import importlib
import json
import os
import pathlib
import subprocess
import sys
import types


PARTS = ("head", "torso", "arms_collision", "left_leg", "right_leg",
         "crouched_torso", "crouched_leg")
DISPLAY = {
    0: "Soldier", 1: "Scout", 2: "Rocketeer", 3: "Miner", 4: "Zombie",
    5: "Classic Soldier", 6: "Gangster 1", 7: "Gangster 2",
    8: "Gangster 3", 9: "Gangster 4", 10: "Gangster VIP 1",
    11: "Gangster VIP 2", 12: "Engineer", 13: "UGC Builder",
    14: "Fast Zombie", 15: "Jump Zombie", 16: "Specialist", 17: "Medic",
}
SYMBOLS = (
    "SOLDIER", "SCOUT", "ROCKETEER", "MINER", "ZOMBIE", "CLASSIC_SOLDIER",
    "GANGSTER_1", "GANGSTER_2", "GANGSTER_3", "GANGSTER_4",
    "GANGSTER_VIP_1", "GANGSTER_VIP_2", "ENGINEER", "UGCBUILDER",
    "FAST_ZOMBIE", "JUMP_ZOMBIE", "SPECIALIST", "MEDIC",
)
PORTRAIT = {
    0: "soldier", 1: "scout", 2: "pilot", 3: "miner", 4: "zombie",
    5: "classic",
    # Retail GlobalImages.class_images deliberately reuses the ordinary
    # soldier portrait for all four Gangsters and both VIP bodies. There are
    # no gangster_character PNGs in the shipped resource set.
    6: "soldier", 7: "soldier", 8: "soldier", 9: "soldier",
    10: "soldier", 11: "soldier",
    12: "engineer", 13: "ugcbuilder", 14: "zombie",
    15: "zombie", 16: "specialist", 17: "medic",
}
ICON = {**PORTRAIT, 6: "gangster1", 7: "gangster2", 8: "gangster3",
        9: "gangster4", 10: "boss", 11: "boss"}


def q(value: str) -> str:
    return json.dumps(value)


def load_constants(root: pathlib.Path, retail_python: pathlib.Path | None = None):
    """Read the Python-2 retail tables without importing its native runtime.

    Importing ``shared.constants`` from the host Python executes
    ``shared/__init__.py`` first and attempts to load the 32-bit ``bytes.pyd``.
    Run the source file with the shipped Python 2 interpreter instead, while a
    namespace-only ``shared`` package keeps constants_audio importable without
    executing that incompatible initializer.
    """
    constants_source = root / "shared" / "constants.py"
    try:
        ast.parse(constants_source.read_text(encoding="utf-8"))
    except SyntaxError:
        pass
    else:
        # The maintained BattleSpades tables are native Python 3. Preserve the
        # original contract-check path while suppressing __pycache__ writes to
        # the strictly read-only compatibility server tree.
        prior_bytecode = sys.dont_write_bytecode
        sys.dont_write_bytecode = True
        sys.path.insert(0, str(root))
        try:
            return importlib.import_module("shared.constants")
        finally:
            sys.path.pop(0)
            sys.dont_write_bytecode = prior_bytecode

    candidates = []
    if retail_python is not None:
        candidates.append(retail_python)
    if os.environ.get("AOS_RETAIL_PYTHON"):
        candidates.append(pathlib.Path(os.environ["AOS_RETAIL_PYTHON"]))
    candidates.extend([
        root.parent / "AceOfSpades_no_steam_new" / "python" / "python.exe",
        root.parent / "aceofspades_nonsteam" / "python" / "python.exe",
    ])
    interpreter = next((path.resolve() for path in candidates if path.is_file()), None)
    if interpreter is None:
        raise RuntimeError(
            "retail Python 2 runtime not found; pass --retail-python or "
            "set AOS_RETAIL_PYTHON")
    shared = root / "shared"
    script = r"""
import imp, json, os, sys, types
root = sys.argv[1]
sys.path.insert(0, root)
package = types.ModuleType('shared')
package.__path__ = [root]
sys.modules['shared'] = package
c = imp.load_source('shared.constants', os.path.join(root, 'constants.py'))
names = (
    'BODY_PARTS_X', 'BODY_PARTS_Y', 'BODY_PARTS_Z', 'CLASS_ITEMS',
    'CLASS_NAMES', 'CLASS_BODY_PARTS_FILENAMES', 'CLASS_BODY_PARTS_OFFSETS',
    'CLASS_FPS_ARMS_FILENAMES', 'CLASS_BLOCKS', 'CLASS_DAMAGE_MULTIPLIER',
    'PREFAB_LISTS', 'CLASS_DESCRIPTIONS', 'TOOL_NAMES', 'TOOL_DESCRIPTIONS',
)
def materialize(value):
    if isinstance(value, dict):
        return dict((key, materialize(item)) for key, item in value.items())
    if isinstance(value, (list, tuple, xrange)):
        return [materialize(item) for item in value]
    return value
print(json.dumps(dict((name, materialize(getattr(c, name))) for name in names)))
"""
    try:
        completed = subprocess.run(
            [str(interpreter), "-c", script, str(shared)],
            # constants.py optionally reads ./constants.txt. The catalogue is
            # generated from retail defaults, never a source-tree override.
            cwd=str(pathlib.Path(__file__).resolve().parent),
            check=True,
            capture_output=True,
            text=True,
        )
    except subprocess.CalledProcessError as error:
        raise RuntimeError(
            "retail constants extraction failed:\n" +
            (error.stderr or error.stdout or "unknown Python 2 error")) from error
    raw = json.loads(completed.stdout.strip().splitlines()[-1])
    for name in (
        "CLASS_NAMES",
        "CLASS_BODY_PARTS_FILENAMES",
        "CLASS_BODY_PARTS_OFFSETS",
        "CLASS_FPS_ARMS_FILENAMES",
        "CLASS_BLOCKS",
        "CLASS_DAMAGE_MULTIPLIER",
        "PREFAB_LISTS",
        "CLASS_DESCRIPTIONS",
        "TOOL_NAMES",
        "TOOL_DESCRIPTIONS",
    ):
        raw[name] = {int(key): value for key, value in raw[name].items()}
    def flatten_group(values):
        result = []
        for value in values:
            if isinstance(value, list):
                result.extend(value)
            else:
                result.append(value)
        return result

    raw["CLASS_ITEMS"] = {
        int(class_id): {
            int(group): flatten_group(value)
            for group, value in groups.items()
        }
        for class_id, groups in raw["CLASS_ITEMS"].items()
    }
    return types.SimpleNamespace(**raw)


def path_pair(stem: str, suffix: str) -> tuple[str, str]:
    if not stem:
        return "", ""
    root = "png/ui/in_game_menus/select_class/"
    return (f"{root}{stem}_{suffix}_team1.png", f"{root}{stem}_{suffix}_team2.png")


def icon_pair(stem: str) -> tuple[str, str]:
    if stem == "zombie":
        path = "png/ui/in_game_menus/select_class/zombie_icon.png"
        return path, path
    return path_pair(stem, "icon")


def class_prefabs(c, cid: int) -> list[str]:
    """PREFAB_LISTS names for CLASS_ITEMS[cid][CLASS_PREFABS], de-duplicated.

    MAP_PREFABS/DEFAULT_PREFABS are empty placeholders filled at runtime by the
    map, so they contribute nothing to the static table.
    """
    names: list[str] = []
    for prefab_set in c.CLASS_ITEMS[cid].get(4, ()):
        for name in c.PREFAB_LISTS.get(int(prefab_set), ()):
            if name not in names:
                names.append(str(name))
    return names


def generate(server_root: pathlib.Path,
             retail_python: pathlib.Path | None = None) -> str:
    c = load_constants(server_root, retail_python)
    anchor_x = tuple(float(x) for x in c.BODY_PARTS_X)
    anchor_y = tuple(float(x) for x in c.BODY_PARTS_Y)
    anchor_z = tuple(float(x) for x in c.BODY_PARTS_Z)
    lines = [
        '// Generated by tools/generate_class_catalog.py; do not edit.\n',
        '#include "battlespades/world/class_catalog.hpp"\n\n',
        '#include "battlespades/world/weapon_catalog.hpp"\n\n',
        '#include <algorithm>\n#include <array>\n\n',
        'namespace battlespades::world {\nnamespace {\n\n',
    ]
    digest_rows = []
    for cid in range(18):
        groups = c.CLASS_ITEMS[cid]
        normalized_items = {}
        for group in range(7):
            values = []
            for value in groups.get(group, ()):
                if isinstance(value, range):
                    values.extend(value)
                else:
                    values.append(int(value))
            normalized_items[str(group)] = values
            joined = ", ".join(f"{v}U" for v in values)
            lines.append(f"constexpr std::array<std::uint16_t, {len(values)}U> "
                         f"class_{cid}_group_{group}{{{{{joined}}}}};\n")
        digest_rows.append({
            "id": cid,
            "name": c.CLASS_NAMES[cid],
            "parts": c.CLASS_BODY_PARTS_FILENAMES[cid],
            "offsets": c.CLASS_BODY_PARTS_OFFSETS[cid],
            "arms": c.CLASS_FPS_ARMS_FILENAMES[cid],
            # Digest the expanded semantics, not Python's source container
            # type. Retail Python 2 exposes xrange while the maintained tables
            # use Python 3 range; both represent the same class contract.
            "items": normalized_items,
            "blocks": c.CLASS_BLOCKS[cid],
            "damage_multiplier": c.CLASS_DAMAGE_MULTIPLIER[cid],
            "prefabs": class_prefabs(c, cid),
        })
        prefabs = class_prefabs(c, cid)
        joined = ", ".join(f"std::string_view{{{q(name)}}}" for name in prefabs)
        lines.append(f"constexpr std::array<std::string_view, {len(prefabs)}U> "
                     f"class_{cid}_prefabs{{{{{joined}}}}};\n")
        lines.append("\n")

    lines.append("constexpr std::array<ClassDefinition, retail_class_count> classes{{\n")
    for cid in range(18):
        body = []
        for part, (asset, offset) in enumerate(zip(c.CLASS_BODY_PARTS_FILENAMES[cid],
                                                   c.CLASS_BODY_PARTS_OFFSETS[cid])):
            body.append(
                f"ClassBodyPartDefinition{{BodyPart::{PARTS[part]}, "
                f"{q('kv6/' + asset + '.kv6')}, "
                f"{{{offset[0]}F, {offset[1]}F, {offset[2]}F}}, "
                f"{{{anchor_x[part]}F, {anchor_y[part]}F, {anchor_z[part]}F}}}}"
            )
        arms = [f"kv6/{x}.kv6" if x else "" for x in c.CLASS_FPS_ARMS_FILENAMES[cid]]
        portraits = path_pair(PORTRAIT.get(cid, ""), "character")
        icons = icon_pair(ICON.get(cid, ""))
        skin = "mafia" if 6 <= cid <= 11 else ""
        block_start, block_max = c.CLASS_BLOCKS[cid]
        damage_multiplier = float(c.CLASS_DAMAGE_MULTIPLIER[cid])
        lines.append("    ClassDefinition{\n")
        lines.append(f"        {cid}U, {q(SYMBOLS[cid])}, {q(DISPLAY[cid])},\n")
        lines.append("        {{" + ",\n         ".join(body) + "}},\n")
        lines.append(f"        {{{q(arms[0])}, {q(arms[1])}}},\n")
        spans = ", ".join(f"std::span<const std::uint16_t>{{class_{cid}_group_{g}}}"
                          for g in range(7))
        lines.append(f"        {{{spans}}},\n")
        lines.append(f"        {block_start}U, {block_max}U,\n")
        lines.append(f"        {{{q(portraits[0])}, {q(portraits[1])}}},\n")
        lines.append(f"        {{{q(icons[0])}, {q(icons[1])}}}, {q(skin)}, "
                     f"{damage_multiplier!r}}},\n")
    lines.append("}};\n\n")
    lines.append("constexpr std::array<std::span<const std::string_view>, retail_class_count> "
                 "prefab_names{{\n")
    for cid in range(18):
        lines.append(f"    std::span<const std::string_view>{{class_{cid}_prefabs}},\n")
    lines.append("}};\n\n")
    for table, source in (("class_names", c.CLASS_NAMES),
                          ("class_descriptions", c.CLASS_DESCRIPTIONS)):
        values = ", ".join(f"std::string_view{{{q(str(source.get(cid, '')))}}}"
                           for cid in range(18))
        lines.append(f"constexpr std::array<std::string_view, retail_class_count> "
                     f"{table}{{{{{values}}}}};\n")
    lines.append("\n")
    for function, source in (("tool_name_key_for", c.TOOL_NAMES),
                             ("tool_description_key_for", c.TOOL_DESCRIPTIONS)):
        lines.append(f"constexpr std::string_view {function}(std::uint16_t tool) noexcept {{\n"
                     "    switch (tool) {\n")
        for tool in sorted(source):
            if source[tool]:
                lines.append(f"    case {tool}U: return {q(str(source[tool]))};\n")
        lines.append("    default: return {};\n    }\n}\n\n")
    lines.append('constexpr std::array<UiSkinDefinition, 2U> skins{{\n'
                 '    UiSkinDefinition{"default", ""},\n'
                 '    UiSkinDefinition{"mafia", "skins/mafia/"},\n'
                 '}};\n\n')
    digest = hashlib.sha256(json.dumps(digest_rows, sort_keys=True,
                                      separators=(",", ":")).encode()).hexdigest()
    lines.append("} // namespace\n\n")
    lines.append("std::span<const ClassDefinition> class_catalog() noexcept { return classes; }\n\n")
    lines.append("const ClassDefinition* find_class_definition(std::uint8_t class_id) noexcept {\n"
                 "    return class_id < classes.size() ? &classes[class_id] : nullptr;\n}\n\n")
    lines.append("std::span<const UiSkinDefinition> ui_skin_catalog() noexcept { return skins; }\n\n")
    lines.append("const UiSkinDefinition* find_ui_skin(std::string_view id) noexcept {\n"
                 "    const auto found = std::ranges::find_if(skins, [id](const auto& skin) { return skin.id == id; });\n"
                 "    return found == skins.end() ? nullptr : &*found;\n}\n\n")
    lines.append("std::vector<std::uint16_t> default_class_items(const ClassDefinition& definition, bool include_flare, bool include_prefab_tool) {\n"
                 "    std::vector<std::uint16_t> result;\n"
                 "    for (std::size_t group{}; group < static_cast<std::size_t>(ClassItemGroup::common); ++group) {\n"
                 "        if (group == static_cast<std::size_t>(ClassItemGroup::prefab_sets) || definition.item_groups[group].empty()) continue;\n"
                 "        result.push_back(definition.item_groups[group].front());\n"
                 "    }\n"
                 "    for (const auto item : definition.item_groups[static_cast<std::size_t>(ClassItemGroup::common)]) {\n"
                 "        if (item == 22U && !include_flare) continue;\n"
                 "        if (item == 23U && !include_prefab_tool) continue;\n"
                 "        if (item == 5U) result.insert(result.begin(), item); else result.push_back(item);\n"
                 "    }\n"
                 "    if (definition.class_id != 4U && definition.class_id != 5U && definition.class_id != 14U && definition.class_id != 15U) {\n"
                 "        if (include_flare && std::ranges::find(result, 22U) == result.end()) result.push_back(22U);\n"
                 "        if (include_prefab_tool && std::ranges::find(result, 23U) == result.end()) result.push_back(23U);\n"
                 "    }\n"
                 "    return result;\n}\n\n")
    lines.append("std::vector<std::uint8_t> class_test_tools(const ClassDefinition& definition) {\n"
                 "    std::vector<std::uint8_t> result;\n"
                 "    for (std::size_t group{}; group < definition.item_groups.size(); ++group) {\n"
                 "        if (group == static_cast<std::size_t>(ClassItemGroup::prefab_sets) || group == static_cast<std::size_t>(ClassItemGroup::ugc_tools)) continue;\n"
                 "        for (const auto item : definition.item_groups[group]) {\n"
                 "            if (item <= 64U && valid_selectable_tool(static_cast<std::uint8_t>(item)) && std::ranges::find(result, static_cast<std::uint8_t>(item)) == result.end()) result.push_back(static_cast<std::uint8_t>(item));\n"
                 "        }\n"
                 "    }\n"
                 "    return result;\n}\n\n")
    lines.append("std::optional<std::uint8_t> preferred_spawn_tool(const ClassDefinition& definition, std::span<const std::uint8_t> loadout) noexcept {\n"
                 "    const auto primary = definition.item_groups[static_cast<std::size_t>(ClassItemGroup::primary)];\n"
                 "    for (const auto choice : primary) {\n"
                 "        if (choice <= 64U && valid_selectable_tool(static_cast<std::uint8_t>(choice)) && std::ranges::find(loadout, static_cast<std::uint8_t>(choice)) != loadout.end()) return static_cast<std::uint8_t>(choice);\n"
                 "    }\n"
                 "    const auto fallback = std::ranges::find_if(loadout, [](std::uint8_t tool) { return valid_selectable_tool(tool); });\n"
                 "    return fallback == loadout.end() ? std::nullopt : std::optional<std::uint8_t>{*fallback};\n"
                 "}\n\n")
    lines.append("std::string_view class_item_group_name(ClassItemGroup group) noexcept {\n"
                 "    constexpr std::array names{std::string_view{\"Melee\"}, std::string_view{\"Primary\"}, std::string_view{\"Secondary\"}, std::string_view{\"Equipment\"}, std::string_view{\"Prefab sets\"}, std::string_view{\"Common\"}, std::string_view{\"UGC tools\"}};\n"
                 "    const auto index = static_cast<std::size_t>(group); return index < names.size() ? names[index] : std::string_view{};\n}\n\n")
    lines.append(f"std::string_view class_catalog_contract_sha256() noexcept {{ return {q(digest)}; }}\n\n")
    lines.append("std::span<const std::string_view> class_prefab_names(std::uint8_t class_id) noexcept {\n"
                 "    return class_id < prefab_names.size() ? prefab_names[class_id] : std::span<const std::string_view>{};\n}\n\n")
    lines.append("std::string_view class_name_key(std::uint8_t class_id) noexcept {\n"
                 "    return class_id < class_names.size() ? class_names[class_id] : std::string_view{};\n}\n\n")
    lines.append("std::string_view class_description_key(std::uint8_t class_id) noexcept {\n"
                 "    return class_id < class_descriptions.size() ? class_descriptions[class_id] : std::string_view{};\n}\n\n")
    lines.append("std::string_view tool_name_key(std::uint16_t tool) noexcept { return tool_name_key_for(tool); }\n\n")
    lines.append("std::string_view tool_description_key(std::uint16_t tool) noexcept { return tool_description_key_for(tool); }\n\n")
    lines.append("} // namespace battlespades::world\n")
    return "".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--server-root", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path,
                        default=pathlib.Path("src/world/class_catalog.generated.cpp"))
    parser.add_argument(
        "--retail-python", type=pathlib.Path,
        help="path to the shipped 32-bit Python 2 executable")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    content = generate(args.server_root.resolve(), args.retail_python)
    output = args.output.resolve()
    if args.check:
        return 0 if output.exists() and output.read_text(encoding="utf-8") == content else 1
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(content, encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
