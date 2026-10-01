"""Generate the per-class voice-over table from the recovered retail constants.

Retail keeps one CLASS_SOUNDS dict with sixteen slots per class, eight of which
are vocal. Rather than transcribe 374 filenames by hand -- where a single typo is
a sound that silently never plays -- this evaluates the real constants module and
expands its `"stem_001-008"` range notation against the shipped asset tree.

Three retail quirks are preserved deliberately rather than corrected, because
diverging from them is a parity break:

  * On eleven classes FALL_HURT_VO points at that class's own WATER-LAND bank,
    byte-identical to the line above it. Only two classes have a dedicated fall
    bank. This is a copy-paste bug in the original data and it is audible.
  * Two classes ship no water banks at all; their water variants reuse the dry
    ones. Assuming a `water_` filename exists for them emits files that do not
    ship.
  * Several classes are deliberately silent in some slots. A blank is an
    authored choice, not a gap, and is emitted as an empty bank.

The middle `-1` field in a sound spec is NOT a probability or a flag: it is the
last-played take index (-1 = none yet); media.py never repeats a take anyway.
The third field is the 0..100 chance, and a NEGATIVE chance additionally
suppresses the trigger straight after a played line.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

# The eight vocal slots, in the order they appear in each class dict.
VOCAL_SLOTS = [
    "DEATH_SOUND",
    "PERIODIC_SOUND",
    "SPAWN_VO",
    "JUMP_VO",
    "WATER_JUMP_VO",
    "LAND_VO",
    "WATER_LAND_VO",
    "FALL_HURT_VO",
]

RANGE = re.compile(r"^(?P<stem>.+?)_(?P<first>\d+)-(?P<last>\d+)$")


def expand(spec: str) -> list[str]:
    """`"sol_jump_vo_001-008"` -> eight zero-padded stems. A plain name is one."""
    if not spec:
        return []
    match = RANGE.match(spec)
    if match is None:
        return [spec]
    stem = match.group("stem")
    first = match.group("first")
    last = match.group("last")
    width = len(first)
    return [f"{stem}_{index:0{width}d}" for index in range(int(first), int(last) + 1)]


def load_constants(source_root: Path, python2: str) -> dict:
    """Evaluate the retail constants module far enough to read CLASS_SOUNDS.

    Executed rather than parsed because the table is built from cross-referenced
    names -- slot keys, chance constants, shared GENERIC_* specs -- that a
    literal parse would have to re-implement.

    Retail is Python 2, so the evaluation happens in a Python 2 subprocess that
    dumps JSON. Running it under Python 3 fails on the first print statement, and
    porting the module would mean maintaining a fork of the thing we are trying
    to read faithfully.
    """
    dumper = r"""
import json, sys, re
path = sys.argv[1]
text = open(path).read()
# Rewrite package-qualified sibling imports to bare ones and run from the
# shared/ directory, rather than dropping them: `from shared.constants_audio
# import *` is what defines every GENERIC_* sound spec the class table refers
# to, so stripping it silently produces an empty table. Only genuinely absent
# game modules are removed.
lines = []
for l in text.split("\n"):
    if l.startswith("from aoslib") or l.startswith("import aoslib"):
        continue
    if l.startswith("from shared.") or l.startswith("import shared."):
        l = l.replace("shared.", "", 1)
    lines.append(l)
stripped = "\n".join(lines)
ns = {}
exec compile(stripped, path, "exec") in ns
slots = %s
out = {"slots": {}, "classes": {}}
for name in slots:
    out["slots"][name] = ns[name]
for class_id, entry in ns["CLASS_SOUNDS"].items():
    row = {}
    for name in slots:
        spec = entry.get(ns[name])
        if spec is None:
            row[name] = None
            continue
        if isinstance(spec, (list, tuple)):
            # PERIODIC_SOUND is the one deliberately nested retail shape:
            # ([stem, no_repeat, chance], minimum_delay, maximum_delay).
            # Treating it like the ordinary [stem, no_repeat, chance] slots
            # discarded the inner stem and left every Zombie idle bank empty.
            sound_spec = spec[0] if name == "PERIODIC_SOUND" and spec else spec
            if isinstance(sound_spec, (list, tuple)):
                stem = sound_spec[0] if len(sound_spec) > 0 else ""
                # media.get_sound_name: slot 1 is the last-played index
                # (-1 = none), NOT a flag. A NEGATIVE chance (slot 2) is what
                # disallows consecutive plays -- it suppresses the trigger
                # right after a played line. Every row starts at -1, so
                # reading slot 1 wrongly flagged 25/100-chance rows too.
                no_repeat = sound_spec[2] < 0 if len(sound_spec) > 2 else False
                chance = sound_spec[2] if len(sound_spec) > 2 else 0
            else:
                stem = sound_spec
                no_repeat = False
                chance = 0
        else:
            stem, chance, no_repeat = spec, 0, False
        row[name] = [stem if isinstance(stem, str) else "", chance, no_repeat]
    out["classes"][str(class_id)] = row
json.dump(out, sys.stdout)
""" % (repr(VOCAL_SLOTS),)

    path = source_root / "shared" / "constants.py"
    completed = subprocess.run(
        [python2, "-c", dumper, str(path)],
        cwd=str(source_root / "shared"),
        capture_output=True, text=True, check=False,
    )
    if completed.returncode != 0:
        raise SystemExit(
            "python2 failed to evaluate the retail constants:\n" + completed.stderr
        )
    return json.loads(completed.stdout)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--python2", default="C:/Python27/python.exe")
    args = parser.parse_args()

    dumped = load_constants(args.source, args.python2)
    class_sounds = dumped["classes"]
    sounds_dir = args.assets / "sounds"

    rows = []
    missing: list[str] = []
    total = 0
    for class_id in sorted(class_sounds, key=int):
        entry = class_sounds[class_id]
        slots = []
        for slot_name in VOCAL_SLOTS:
            spec = entry.get(slot_name)
            stems: list[str] = []
            chance = 0
            no_repeat = False
            if spec is not None:
                name, raw_chance, no_repeat = spec[0], spec[1], bool(spec[2])
                chance = abs(int(raw_chance))
                stems = expand(name)
            for stem in stems:
                total += 1
                if not (sounds_dir / f"{stem}.ogg").is_file():
                    missing.append(stem)
            slots.append((slot_name, stems, chance, no_repeat))
        rows.append((int(class_id), slots))

    if missing:
        print(f"{len(missing)} referenced samples do not ship:", file=sys.stderr)
        for stem in sorted(set(missing))[:20]:
            print(f"  {stem}.ogg", file=sys.stderr)
        return 1

    lines: list[str] = []
    lines.append("// GENERATED by tools/generate_class_voice.py. Do not edit.")
    lines.append("//")
    lines.append("// Per-class voice-over banks recovered from the retail CLASS_SOUNDS table.")
    lines.append("// An empty bank is a DELIBERATE silence, not a gap: several classes have no")
    lines.append("// spawn line, and one has no death voice at all.")
    lines.append('#include "battlespades/world/class_voice.hpp"')
    lines.append("")
    lines.append("namespace battlespades::world {")
    lines.append("namespace {")
    lines.append("")

    for class_id, slots in rows:
        for slot_name, stems, _chance, _no_repeat in slots:
            if not stems:
                continue
            array = ", ".join(f'"{stem}"' for stem in stems)
            lines.append(
                f"constexpr std::string_view c{class_id}_{slot_name.lower()}[]{{{array}}};"
            )
    lines.append("")
    lines.append(f"constexpr ClassVoiceSet voices[]{{")
    for class_id, slots in rows:
        fields = []
        for slot_name, stems, chance, no_repeat in slots:
            if stems:
                bank = f"c{class_id}_{slot_name.lower()}"
                fields.append(
                    f"{{{bank}, {chance}U, {'true' if no_repeat else 'false'}}}"
                )
            else:
                fields.append("{{}, 0U, false}")
        lines.append(f"    ClassVoiceSet{{{class_id}U, " + ", ".join(fields) + "},")
    lines.append("};")
    lines.append("")
    lines.append("} // namespace")
    lines.append("")
    lines.append("const ClassVoiceSet* find_class_voice(std::uint8_t class_id) noexcept {")
    lines.append("    for (const auto& entry : voices) {")
    lines.append("        if (entry.class_id == class_id) {")
    lines.append("            return &entry;")
    lines.append("        }")
    lines.append("    }")
    lines.append("    return nullptr;")
    lines.append("}")
    lines.append("")
    lines.append("std::span<const ClassVoiceSet> class_voice_table() noexcept {")
    lines.append("    return voices;")
    lines.append("}")
    lines.append("")
    lines.append("} // namespace battlespades::world")
    lines.append("")

    args.out.write_text("\n".join(lines), encoding="utf-8")
    print(f"{len(rows)} classes, {total} samples, 0 missing -> {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
