"""Record original grenade physics using the installed piqueserver build.

Reference: piqueserver 3dfc0a6774fc5cf3a80eec13b7764b751a0d949b.
The client test compares every frame, including collisions and water.
"""
import io
import json
from pathlib import Path

from pyspades import vxl, world
from pyspades.common import Vertex3


def main() -> None:
    column = bytes([0, 60, 63, 0]) + bytes([0, 255, 0, 128]) * 4
    scenarios = []
    for name, position, velocity in (
        ("ground", [250.5, 250.5, 56.0], [0.8, 0.2, -0.6]),
        ("wall", [250.5, 250.5, 56.0], [1.4, 0.1, -0.2]),
        ("corner", [250.5, 250.5, 58.0], [1.2, 1.2, 0.7]),
        ("water", [250.5, 250.5, 59.0], [0.2, 0.1, 0.7]),
        ("edge", [511.8, 250.5, 58.0], [1.1, 0.2, 0.5]),
    ):
        simulation = world.World()
        simulation.map = vxl.VXLData(io.BytesIO(column * (512 * 512)))
        added, removed = [], []
        if name in ("wall", "corner"):
            added = [[255, y, z] for y in range(245, 266) for z in range(50, 60)]
            if name == "corner":
                added += [[x, 255, z] for x in range(245, 266) for z in range(50, 60)]
        if name == "water":
            removed = [[x, y, z] for x in range(245, 270)
                       for y in range(245, 270) for z in range(60, 63)]
        for cell in added:
            simulation.map.set_point(*cell, (100, 100, 100))
        for cell in removed:
            simulation.map.remove_point(*cell)
        grenade = simulation.create_object(
            world.Grenade, 20.0, Vertex3(*position), None, Vertex3(*velocity)
        )
        rows = []
        for _ in range(180):
            simulation.update(1 / 60)
            rows.append([*grenade.position.get(), *grenade.velocity.get()])
        scenarios.append(dict(name=name, position=position, velocity=velocity,
                              added=added, removed=removed, rows=rows))
    output = Path(__file__).resolve().parents[1] / "tests/fixtures/classic-grenades.json"
    output.write_text(json.dumps(scenarios, separators=(",", ":")) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
