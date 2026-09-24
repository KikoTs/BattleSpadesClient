#!/usr/bin/env python3
"""Fail when a frontend pump is written but never called.

A pump that nothing drives compiles, links, and passes every test: the feature
simply never happens. That is how 2dbf39c shipped an unused timestamp and an
unused period while its message described a ten-second refresh, and how a
half-applied edit can look like a clean build.

    python3 tools/verify-frontend-pumps.py

Each `void pump_x()` in the native frontend must be reached either from tick,
as `impl_->pump_x()`, or from another function in the same translation unit.
"""
from __future__ import annotations

import pathlib
import re
import sys

SOURCE = (
    pathlib.Path(__file__).resolve().parent.parent
    / "src/frontend/native_frontend_module.cpp"
)


def main() -> int:
    text = SOURCE.read_text(encoding="utf-8")
    defined = set(re.findall(r"\n    void (pump_\w+)\(\)", text))
    from_tick = set(re.findall(r"impl_->(pump_\w+)\(\)", text))
    internal = set(re.findall(r"(?<!impl_->)\b(pump_\w+)\(\);", text)) - from_tick
    orphans = sorted(defined - from_tick - internal)
    if orphans:
        print("pumps that nothing drives:", file=sys.stderr)
        for name in orphans:
            print(f"  {name}", file=sys.stderr)
        return 1
    print(f"{len(defined)} frontend pumps, all driven "
          f"({len(from_tick)} from tick, {len(internal)} from other pumps)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
