"""Regenerate the movement oracle with an installed piqueserver/pyspades build.

Reference revision: piqueserver 3dfc0a6774fc5cf3a80eec13b7764b751a0d949b.
Run from any directory; this writes only the checked-in movement fixture.
"""
import io
import json
from pathlib import Path

from pyspades import vxl, world
from pyspades.common import Vertex3


def main():
    column = bytes([0, 60, 63, 0]) + bytes([0, 255, 0, 128]) * 4
    simulation = world.World()
    simulation.map = vxl.VXLData(io.BytesIO(column * (512 * 512)))
    player = simulation.create_object(
        world.Character, Vertex3(256.5, 256.5, 57.75), Vertex3(1, 0, 0)
    )
    player.set_orientation(1, 0, 0)
    player.set_weapon(True)
    rows = []
    for frame in range(300):
        flags = 1 if frame < 240 else 0
        if 60 <= frame < 120:
            flags |= 8 | 128
        if 120 <= frame < 180:
            flags |= 32
        if frame == 210:
            flags |= 16
        aiming = 180 <= frame < 210
        player.set_walk(*(bool(flags & bit) for bit in (1, 2, 4, 8)))
        player.set_animation(*(bool(flags & bit) for bit in (16, 32, 64, 128)))
        player.secondary_fire = aiming
        simulation.update(1 / 60)
        rows.append([flags, int(aiming), *player.position.get(), *player.velocity.get()])
    output = Path(__file__).resolve().parents[1] / "tests/fixtures/classic-movement-piqueserver.json"
    output.write_text(json.dumps(rows, indent=2) + "\n", encoding="utf-8")
    scenarios = []
    for name in ("water_jump_spam", "water_exit", "held_jump", "ledge_climb"):
        simulation = world.World()
        simulation.map = vxl.VXLData(io.BytesIO(column * (512 * 512)))
        removed = []
        if name.startswith("water"):
            # A water basin with the original z=63 water bed, a z=62 shore,
            # then a z=61 step leading back to the z=60 ground.
            for x in range(246, 264):
                for y in range(246, 255):
                    surface = 63 if x < 254 else 62 if x < 258 else 61
                    for z in range(60, surface):
                        simulation.map.remove_point(x, y, z)
                        removed.append([x, y, z])
        added = []
        if name == "ledge_climb":
            for x in range(254, 265):
                for y in range(248, 254):
                    simulation.map.set_point(x, y, 59, (100, 100, 100))
                    added.append([x, y, 59])
        initial = [250.5, 250.5, 61.75 if name.startswith("water") else 57.75]
        player = simulation.create_object(world.Character, Vertex3(*initial), Vertex3(1, 0, 0))
        player.set_orientation(1, 0, 0)
        player.set_weapon(True)
        player.primary_fire = player.secondary_fire = False
        rows = []
        held = False
        for frame in range(360):
            flags = 1 if name in ("water_exit", "ledge_climb") and frame >= 30 else 0
            if name == "water_jump_spam" and (frame // 8) % 2 == 1:
                flags |= 16
            elif name == "water_exit" and frame in (40, 120, 200, 280):
                flags |= 16
            elif name == "held_jump" and frame >= 30:
                flags |= 16
            jump = bool(flags & 16) and not held and not player.airborne and 0 <= player.velocity.z < 0.017
            held = bool(flags & 16)
            player.set_walk(*(bool(flags & bit) for bit in (1, 2, 4, 8)))
            player.set_animation(jump, False, False, False)
            simulation.update(1 / 60)
            rows.append([flags, *player.position.get(), *player.velocity.get(), int(player.airborne), int(player.wade), int(jump)])
        scenarios.append(dict(name=name, initial=initial, removed=removed, added=added, rows=rows))
    output.with_name("classic-movement-water.json").write_text(json.dumps(scenarios, separators=(",", ":")) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
