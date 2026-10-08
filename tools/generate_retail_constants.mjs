#!/usr/bin/env node
// Generates include/battlespades/shared/retail_constants.hpp from the
// authoritative retail_constants.json (extracted by executing the retail
// Python 2.7 shared/constants.py + aoslib/weapons/*.py).
//
// Usage: node tools/generate_retail_constants.mjs <retail_constants.json> <retail_constants.hpp>
//
// Output is deterministic: every section is emitted in lexicographic key
// order and numeric formatting is the shortest round-trip representation.

import fs from 'node:fs';
import path from 'node:path';

function fail(message) {
    console.error(`generate_retail_constants: ${message}`);
    process.exit(1);
}

const [jsonPath, outPath] = process.argv.slice(2);
if (!jsonPath || !outPath) {
    fail('usage: node tools/generate_retail_constants.mjs <retail_constants.json> <output.hpp>');
}

const rawText = fs.readFileSync(jsonPath, 'utf8');
const data = JSON.parse(rawText);

for (const section of ['all_named_constants', 'classes', 'teams', 'tools', 'weapons']) {
    if (!(section in data)) {
        fail(`input JSON is missing required section "${section}"`);
    }
}

// ---------------------------------------------------------------------------
// Formatting helpers
// ---------------------------------------------------------------------------

const INT64_MIN = -9223372036854775808n;
const INT64_MAX = 9223372036854775807n;
const UINT64_MAX = 18446744073709551615n;

function isIdentifier(name) {
    return /^[A-Za-z_][A-Za-z0-9_]*$/.test(name) && !/^_[A-Z]/.test(name) && !name.includes('__');
}

function escapeString(value) {
    let out = '';
    for (const ch of value) {
        const code = ch.codePointAt(0);
        if (ch === '\\') out += '\\\\';
        else if (ch === '"') out += '\\"';
        else if (code < 0x20 || code > 0x7e) {
            fail(`string literal contains non-printable/non-ASCII character: ${JSON.stringify(value)}`);
        } else out += ch;
    }
    return `"${out}"`;
}

function formatDouble(value) {
    if (typeof value !== 'number' || !Number.isFinite(value)) {
        fail(`expected finite number, got ${JSON.stringify(value)}`);
    }
    if (Number.isInteger(value) && Math.abs(value) < 1e15) {
        return `${value}.0`;
    }
    const text = String(value); // shortest round-trip repr; valid C++ double literal
    return /[.e]/.test(text) ? text : `${text}.0`;
}

function formatInt(value) {
    if (!Number.isInteger(value)) fail(`expected integer, got ${JSON.stringify(value)}`);
    return String(value);
}

// Recover the exact source literal for integers that exceed the double-safe
// range (JSON.parse rounds them). Returns a BigInt or null.
function recoverExactInteger(key) {
    const pattern = new RegExp(`"${key}"\\s*:\\s*(-?\\d+)(?=[,}\\s])`, 'g');
    const literals = new Set();
    for (const match of rawText.matchAll(pattern)) {
        literals.add(match[1]);
    }
    if (literals.size !== 1) return null;
    return BigInt([...literals][0]);
}

const sortKeys = (obj) => Object.keys(obj).sort();

// Every identifier emitted directly at battlespades::retail scope; used to
// detect collisions between sections before writing anything.
const retailScopeNames = new Set();
function claimRetailName(name) {
    if (!isIdentifier(name)) fail(`"${name}" is not a usable C++ identifier`);
    if (retailScopeNames.has(name)) fail(`duplicate identifier at retail scope: ${name}`);
    retailScopeNames.add(name);
}

// ---------------------------------------------------------------------------
// Section: named scalar constants
// ---------------------------------------------------------------------------

function emitNamedConstants(lines) {
    const constants = data.all_named_constants;
    let count = 0;
    lines.push('// -------------------------------------------------------------------------');
    lines.push('// Named scalar constants (retail shared/constants.py). Non-numeric values');
    lines.push('// (strings, lists, dicts, booleans) are intentionally omitted; structured');
    lines.push('// data lives in the classes/tools/weapons/teams sections below.');
    lines.push('// -------------------------------------------------------------------------');
    lines.push('');
    for (const key of sortKeys(constants)) {
        const value = constants[key];
        if (typeof value !== 'number') continue;
        if (!Number.isFinite(value)) fail(`named constant ${key} is not finite`);
        claimRetailName(key);
        if (Number.isInteger(value)) {
            if (Math.abs(value) > Number.MAX_SAFE_INTEGER) {
                const exact = recoverExactInteger(key);
                if (exact !== null && exact >= 0n && exact <= UINT64_MAX) {
                    lines.push(`inline constexpr std::uint64_t ${key} = ${exact}ULL;`);
                } else if (exact !== null && exact >= INT64_MIN && exact <= INT64_MAX) {
                    lines.push(`inline constexpr std::int64_t ${key} = ${exact}LL;`);
                } else {
                    lines.push(`inline constexpr double ${key} = ${formatDouble(value)};`);
                }
            } else {
                lines.push(`inline constexpr std::int64_t ${key} = ${formatInt(value)};`);
            }
        } else {
            lines.push(`inline constexpr double ${key} = ${formatDouble(value)};`);
        }
        ++count;
    }
    lines.push('');
    return count;
}

// ---------------------------------------------------------------------------
// Section: teams
// ---------------------------------------------------------------------------

function emitTeams(lines) {
    const teams = data.teams;
    const order = [
        ['spectator', 'SPECTATOR'],
        ['neutral', 'NEUTRAL'],
        ['team1', 'TEAM1'],
        ['team2', 'TEAM2'],
    ];
    lines.push('// -------------------------------------------------------------------------');
    lines.push('// Teams (retail shared/constants.py:213-245). Colors are RGB 0-255.');
    lines.push('// -------------------------------------------------------------------------');
    lines.push('');
    for (const [key, prefix] of order) {
        const team = teams[key];
        if (!team) fail(`teams section is missing "${key}"`);
        const idName = `${prefix}_ID`;
        const colorName = `${prefix}_COLOR`;
        claimRetailName(idName);
        claimRetailName(colorName);
        lines.push(`inline constexpr int ${idName} = ${formatInt(team.id)};`);
        lines.push(`inline constexpr std::array<int, 3> ${colorName}{${team.color.map(formatInt).join(', ')}};`);
    }
    claimRetailName('BASE_CONTESTED_COLOUR');
    lines.push(`inline constexpr std::array<int, 3> BASE_CONTESTED_COLOUR{${teams.base_contested_colour.map(formatInt).join(', ')}};`);
    lines.push('');
}

// ---------------------------------------------------------------------------
// Section: classes
// ---------------------------------------------------------------------------

const CLASS_DOUBLE_FIELDS = [
    'accel_multiplier',
    'crouch_sneak_multiplier',
    'damage_multiplier',
    'fall_max_damage',
    'fall_max_distance',
    'fall_min_distance',
    'fall_on_water_multiplier',
    'headshot_damage_multiplier',
    'jump_multiplier',
    'sprint_multiplier',
    'water_friction',
];
const CLASS_INT_FIELDS = ['max_blocks', 'starting_blocks'];

function emitClasses(lines) {
    const classes = data.classes;
    const names = sortKeys(classes);
    lines.push('namespace classes {');
    lines.push('');
    lines.push('// Per-class stats/multipliers (retail shared/constants.py:5333-5620).');
    lines.push('// Loadout/model lists are omitted (non-numeric).');
    lines.push('struct ClassStats final {');
    lines.push('    int id;');
    lines.push('    const char* name; // retail display_name key');
    lines.push('    bool can_sprint_uphill;');
    for (const field of CLASS_DOUBLE_FIELDS) {
        lines.push(`    double ${field};`);
    }
    for (const field of CLASS_INT_FIELDS) {
        lines.push(`    int ${field};`);
    }
    lines.push('};');
    lines.push('');
    for (const key of names) {
        const cls = classes[key];
        if (!isIdentifier(key)) fail(`class key "${key}" is not a usable identifier`);
        const fields = [
            `.id = ${formatInt(cls.id)}`,
            `.name = ${escapeString(cls.display_name)}`,
            `.can_sprint_uphill = ${cls.can_sprint_uphill ? 'true' : 'false'}`,
        ];
        for (const field of CLASS_DOUBLE_FIELDS) {
            if (typeof cls[field] !== 'number') fail(`class ${key} field ${field} is not numeric`);
            fields.push(`.${field} = ${formatDouble(cls[field])}`);
        }
        for (const field of CLASS_INT_FIELDS) {
            fields.push(`.${field} = ${formatInt(cls[field])}`);
        }
        lines.push(`inline constexpr ClassStats ${key}{`);
        for (const field of fields) {
            lines.push(`    ${field},`);
        }
        lines.push('};');
    }
    lines.push('');
    lines.push(`inline constexpr std::array<ClassStats, ${names.length}> ALL_CLASSES{`);
    for (const key of names) {
        lines.push(`    ${key},`);
    }
    lines.push('};');
    lines.push('');
    lines.push('} // namespace classes');
    lines.push('');
    return names.length;
}

// ---------------------------------------------------------------------------
// Section: tools
// ---------------------------------------------------------------------------

function emitTools(lines) {
    const tools = data.tools;
    const names = sortKeys(tools);
    lines.push('namespace tools {');
    lines.push('');
    lines.push('// Tool registry (retail shared/constants.py:858-942 ids, :1691-1836 kv6/names).');
    lines.push('// show_crosshair uses the *_CROSSHAIR enum constants above; -1 means the');
    lines.push('// retail data had no value (0 is a real value, NEVER_CROSSHAIR).');
    lines.push('// SNIPER_TOOL has per-team icons in retail; the first (blue) is kept here.');
    lines.push('struct ToolInfo final {');
    lines.push('    int id;');
    lines.push('    const char* name;');
    lines.push('    const char* kv6;');
    lines.push('    const char* icon;');
    lines.push('    const char* kind;');
    lines.push('    int show_crosshair;');
    lines.push('};');
    lines.push('');
    for (const key of names) {
        const tool = tools[key];
        if (!isIdentifier(key)) fail(`tool key "${key}" is not a usable identifier`);
        let icon = tool.icon;
        if (Array.isArray(icon)) icon = icon[0];
        const crosshair = tool.show_crosshair === null ? -1 : tool.show_crosshair;
        lines.push(`inline constexpr ToolInfo ${key}{`);
        lines.push(`    .id = ${formatInt(tool.id)},`);
        lines.push(`    .name = ${escapeString(key)},`);
        lines.push(`    .kv6 = ${escapeString(tool.kv6 ?? '')},`);
        lines.push(`    .icon = ${escapeString(icon ?? '')},`);
        lines.push(`    .kind = ${escapeString(tool.kind)},`);
        lines.push(`    .show_crosshair = ${formatInt(crosshair)},`);
        lines.push('};');
    }
    lines.push('');
    lines.push(`inline constexpr std::array<ToolInfo, ${names.length}> ALL_TOOLS{`);
    for (const key of names) {
        lines.push(`    ${key},`);
    }
    lines.push('};');
    lines.push('');
    lines.push('} // namespace tools');
    lines.push('');
    return names.length;
}

// ---------------------------------------------------------------------------
// Section: weapons
// ---------------------------------------------------------------------------

function emitWeapons(lines) {
    const weapons = data.weapons;
    // _BASE_Tool/_BASE_Weapon are python base-class attribute dumps with no
    // tool id; they are metadata, not weapons, and their names are reserved
    // identifiers in C++ anyway.
    const names = sortKeys(weapons).filter((key) => !key.startsWith('_BASE'));
    lines.push('namespace weapons {');
    lines.push('');
    lines.push('// Weapon stat blocks (aoslib/weapons/*.py class attributes with retail');
    lines.push('// A-constants resolved). Missing/null numeric fields are 0, except');
    lines.push('// secondary_shoot_interval where retail null is kept as -1.0.');
    lines.push('struct WeaponStats final {');
    lines.push('    int tool_id;');
    lines.push('    const char* name;');
    lines.push('    double shoot_interval;');
    lines.push('    double secondary_shoot_interval; // -1.0 when retail value is None');
    lines.push('    double reload_time;');
    lines.push('    double range;');
    lines.push('    double block_damage;');
    lines.push('    double block_penetration;');
    lines.push('    // (torso, head, arms, left_leg, right_leg); scalar retail damage is');
    lines.push('    // broadcast to all five entries.');
    lines.push('    std::array<double, 5> damage;');
    lines.push('    // (max_ammo, initial_ammo, max_clip, initial_stock) per');
    lines.push('    // aoslib/weapons/weapon.py:78.');
    lines.push('    std::array<int, 4> ammo;');
    lines.push('    double recoil_up;');
    lines.push('    double recoil_side;');
    lines.push('    double accuracy;');
    lines.push('};');
    lines.push('');
    const numberOr = (value, fallback) => (typeof value === 'number' ? value : fallback);
    for (const key of names) {
        const weapon = weapons[key];
        if (!isIdentifier(key)) fail(`weapon key "${key}" is not a usable identifier`);
        if (typeof weapon.tool_id !== 'number') fail(`weapon ${key} has no tool_id`);

        let damage = [0, 0, 0, 0, 0];
        if (typeof weapon.damage === 'number') {
            damage = Array(5).fill(weapon.damage);
        } else if (Array.isArray(weapon.damage)) {
            if (weapon.damage.length !== 5) fail(`weapon ${key} damage tuple has length ${weapon.damage.length}`);
            damage = weapon.damage;
        }

        let ammo = [0, 0, 0, 0];
        const semantics = weapon.ammo_semantics;
        if (semantics && typeof semantics === 'object') {
            ammo = ['max_ammo', 'initial_ammo', 'max_clip', 'initial_stock'].map((field) => {
                const value = semantics[field];
                if (value === null || value === undefined) return 0; // throwables have no clip
                if (!Number.isInteger(value)) fail(`weapon ${key} ammo field ${field} is not an integer`);
                return value;
            });
        }

        lines.push(`inline constexpr WeaponStats ${key}{`);
        lines.push(`    .tool_id = ${formatInt(weapon.tool_id)},`);
        lines.push(`    .name = ${escapeString(key)},`);
        lines.push(`    .shoot_interval = ${formatDouble(numberOr(weapon.shoot_interval, 0))},`);
        lines.push(`    .secondary_shoot_interval = ${formatDouble(numberOr(weapon.secondary_shoot_interval, -1))},`);
        lines.push(`    .reload_time = ${formatDouble(numberOr(weapon.reload_time, 0))},`);
        lines.push(`    .range = ${formatDouble(numberOr(weapon.range, 0))},`);
        lines.push(`    .block_damage = ${formatDouble(numberOr(weapon.block_damage, 0))},`);
        lines.push(`    .block_penetration = ${formatDouble(numberOr(weapon.block_penetration, 0))},`);
        lines.push(`    .damage = {${damage.map(formatDouble).join(', ')}},`);
        lines.push(`    .ammo = {${ammo.map(formatInt).join(', ')}},`);
        lines.push(`    .recoil_up = ${formatDouble(numberOr(weapon.recoil_up, 0))},`);
        lines.push(`    .recoil_side = ${formatDouble(numberOr(weapon.recoil_side, 0))},`);
        lines.push(`    .accuracy = ${formatDouble(numberOr(weapon.accuracy, 0))},`);
        lines.push('};');
    }
    lines.push('');
    lines.push(`inline constexpr std::array<WeaponStats, ${names.length}> ALL_WEAPONS{`);
    for (const key of names) {
        lines.push(`    ${key},`);
    }
    lines.push('};');
    lines.push('');
    lines.push('} // namespace weapons');
    lines.push('');
    return names.length;
}

// ---------------------------------------------------------------------------
// Assemble
// ---------------------------------------------------------------------------

const lines = [];
lines.push('// GENERATED FILE - DO NOT HAND-EDIT.');
lines.push('//');
lines.push('// Produced from retail_constants.json by tools/generate_retail_constants.mjs.');
lines.push('// The JSON is authoritative: it was extracted by executing the retail');
lines.push('// Ace of Spades: Battle Builder shared/constants.py and aoslib/weapons/*.py');
lines.push('// with real Python 2.7. Regenerate with:');
lines.push('//   node tools/generate_retail_constants.mjs <retail_constants.json> \\');
lines.push('//       include/battlespades/shared/retail_constants.hpp');
lines.push('#pragma once');
lines.push('');
lines.push('#include <array>');
lines.push('#include <cstdint>');
lines.push('');
lines.push('// NOLINTBEGIN');
lines.push('// clang-format off');
lines.push('');
lines.push('namespace battlespades::retail {');
lines.push('');

const namedCount = emitNamedConstants(lines);
emitTeams(lines);
const classCount = emitClasses(lines);
const toolCount = emitTools(lines);
const weaponCount = emitWeapons(lines);

lines.push('} // namespace battlespades::retail');
lines.push('');
lines.push('// clang-format on');
lines.push('// NOLINTEND');
lines.push('');

fs.mkdirSync(path.dirname(outPath), { recursive: true });
fs.writeFileSync(outPath, lines.join('\n'), 'utf8');
console.log(
    `wrote ${outPath}: ${namedCount} named constants, ${classCount} classes, ` +
    `${toolCount} tools, ${weaponCount} weapons`,
);
