"""Pick a camera vantage point for a map, for repeatable lighting captures.

Screenshot work on 27 maps needs viewpoints that actually show something. The
Tutorial's derived spawn is the middle of the map, which on the open-water maps
is the middle of the sea -- a technically valid frame with nothing in it.

This finds the tallest structure away from the map edge, then a standable spot at
a chosen distance from it, and prints the `--tutorial-spawn` / `--tutorial-look`
pair that frames it. Structure height is measured against the LOCAL terrain, not
against z=0, so a tower on a hill does not lose to the hill.

Canonical map space: z grows downward, z=239 is the indestructible bed, so a
SMALLER surface z is HIGHER ground.
"""

from __future__ import annotations

import argparse
import math
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

MAP_SIZE = 512
MAP_HEIGHT = 240
# Retail's eye offset above the feet for a standing player, matching
# player_contact_offset(False, False) in src/world/map_spawn.cpp.
EYE_ABOVE_FEET = 2.5


@dataclass(frozen=True)
class Column:
    x: int
    y: int
    surface_z: int


@dataclass(frozen=True)
class Overhang:
    """A column with solid ground and a separate solid deck above it."""

    x: int
    y: int
    deck_z: int
    floor_z: int

    @property
    def clearance(self) -> int:
        # z grows downward, so the floor has the LARGER z.
        return self.floor_z - self.deck_z


def load_columns(path: Path) -> tuple[list[list[int]], list[list[list[int]]], int, int]:
    """Every column's solid span list, in canonical coordinates.

    Returns (surface, spans, edge, shift). `spans` holds [start, end] pairs of
    solid z per column, which is what distinguishes a bridge from a hill: a
    bridge column has two solid runs with air between them, and no amount of
    topmost-surface data can tell them apart.
    """
    data = path.read_bytes()
    raw: list[list[list[int]]] = []
    offset = 0
    maximum_z = 0
    while offset < len(data):
        runs: list[list[int]] = []
        while True:
            if offset + 4 > len(data):
                raise ValueError("VXL truncated mid-column")
            span_length, top_start, top_end, air_start = struct.unpack_from(
                "<BBBB", data, offset
            )
            maximum_z = max(maximum_z, top_start, top_end, air_start)
            if top_end >= top_start:
                runs.append([top_start, top_end])
            if span_length == 0:
                top_words = top_end - top_start + 1 if top_end >= top_start else 0
                offset += 4 * (1 + top_words)
                break
            offset += span_length * 4
        raw.append(runs)

    edge = int(round(math.sqrt(len(raw))))
    if edge * edge != len(raw):
        raise ValueError(f"{len(raw)} columns is not a perfect square")
    shift = (239 - maximum_z) if maximum_z < 239 else 0
    centre = (MAP_SIZE - edge) // 2

    surface = [[MAP_HEIGHT - 1] * MAP_SIZE for _ in range(MAP_SIZE)]
    spans: list[list[list[int]]] = [[[] for _ in range(MAP_SIZE)] for _ in range(MAP_SIZE)]
    for source_y in range(edge):
        for source_x in range(edge):
            runs = raw[(source_y * edge) + source_x]
            x, y = source_x + centre, source_y + centre
            shifted = [[a + shift, b + shift] for a, b in runs]
            spans[y][x] = shifted
            if shifted:
                surface[y][x] = min(shifted[0][0], MAP_HEIGHT - 1)
    print(f"# source edge {edge}, max z {maximum_z}, z shift +{shift}, "
          f"centre offset +{centre}", file=sys.stderr)
    return surface, spans, edge, shift


def find_overhangs(
    spans: list[list[list[int]]], margin: int, min_clearance: int
) -> list[Overhang]:
    """Columns roofed by a deck with walkable clearance underneath."""
    found: list[Overhang] = []
    for y in range(margin, MAP_SIZE - margin):
        for x in range(margin, MAP_SIZE - margin):
            runs = spans[y][x]
            if len(runs) < 2:
                continue
            deck = runs[0]
            floor = runs[1]
            clearance = floor[0] - deck[1] - 1
            # Enough headroom to stand in, and a deck thin enough to be a bridge
            # or walkway rather than the roof of a solid massif.
            if clearance >= min_clearance and (deck[1] - deck[0]) <= 6:
                found.append(Overhang(x, y, deck[1], floor[0]))
    return found


def load_surface(path: Path) -> list[list[int]]:
    """Each canonical column's topmost solid z, in CANONICAL coordinates.

    Mirrors the two transforms `VxlMap::load` applies to a source map, because a
    spawn computed in source space lands in the wrong place otherwise:

      - Centring. The column count only has to be a perfect square, so a map may
        be smaller than 512 and is centred by `(512 - edge) / 2` on both axes.
      - Z shift. A map whose deepest referenced z falls short of 239 is pushed
        down by the difference so its floor rests on the indestructible bed.

    Getting the shift wrong is silent: the spawn simply falls, and the capture
    still succeeds from an unintended position.
    """
    data = path.read_bytes()
    tops: list[list[int]] = []
    offset = 0
    maximum_z = 0
    while offset < len(data):
        column_top: int | None = None
        while True:
            if offset + 4 > len(data):
                raise ValueError("VXL truncated mid-column")
            span_length, top_start, top_end, air_start = struct.unpack_from(
                "<BBBB", data, offset
            )
            maximum_z = max(maximum_z, top_start, top_end, air_start)
            # The first span's top is the column's topmost solid voxel, which is
            # all this tool needs; the rest is skipped, not decoded.
            if column_top is None:
                column_top = top_start
            if span_length == 0:
                # Last span in the column: header plus its top colours.
                top_words = top_end - top_start + 1 if top_end >= top_start else 0
                offset += 4 * (1 + top_words)
                break
            # Otherwise the length counts the whole span, header included.
            offset += span_length * 4
        tops.append(column_top if column_top is not None else MAP_HEIGHT - 1)

    edge = int(round(math.sqrt(len(tops))))
    if edge * edge != len(tops):
        raise ValueError(f"{len(tops)} columns is not a perfect square")
    shift = (239 - maximum_z) if maximum_z < 239 else 0
    centre = (MAP_SIZE - edge) // 2

    surface = [[MAP_HEIGHT - 1] * MAP_SIZE for _ in range(MAP_SIZE)]
    for source_y in range(edge):
        for source_x in range(edge):
            top = tops[(source_y * edge) + source_x] + shift
            surface[source_y + centre][source_x + centre] = min(top, MAP_HEIGHT - 1)
    print(f"# source edge {edge}, max z {maximum_z}, z shift +{shift}, "
          f"centre offset +{centre}", file=sys.stderr)
    return surface


def prominence(surface: list[list[int]], x: int, y: int, radius: int) -> int:
    """How far this column rises above the median surface around it."""
    samples = []
    for dy in range(-radius, radius + 1, 4):
        for dx in range(-radius, radius + 1, 4):
            nx, ny = x + dx, y + dy
            if 0 <= nx < MAP_SIZE and 0 <= ny < MAP_SIZE:
                samples.append(surface[ny][nx])
    if not samples:
        return 0
    samples.sort()
    local = samples[len(samples) // 2]
    # Smaller z is higher, so prominence is how much SHALLOWER this column is.
    return local - surface[y][x]


def report_overhang(args) -> int:
    """Stand under the middle of the largest bridge, facing out along it.

    The interesting pixels are where the deck's cover ENDS, because that is where
    the skylight term transitions and where a hard edge is visible. So the camera
    goes under the deck and looks out toward open sky, putting the whole gradient
    across the floor in front of it.
    """
    _surface, spans, _edge, _shift = load_columns(args.vxl)
    overhangs = find_overhangs(spans, args.margin, args.min_clearance)
    if not overhangs:
        print("no bridge-like overhang found; try --min-clearance 3", file=sys.stderr)
        return 1

    # The largest connected patch, so we frame a real bridge rather than a stray
    # arch or a one-column gap in a roof.
    occupied = {(o.x, o.y): o for o in overhangs}
    seen: set[tuple[int, int]] = set()
    best: list[Overhang] = []
    for start in occupied:
        if start in seen:
            continue
        stack = [start]
        seen.add(start)
        patch: list[Overhang] = []
        while stack:
            key = stack.pop()
            patch.append(occupied[key])
            cx, cy = key
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nxt = (cx + dx, cy + dy)
                if nxt in occupied and nxt not in seen:
                    seen.add(nxt)
                    stack.append(nxt)
        if len(patch) > len(best):
            best = patch

    xs = [o.x for o in best]
    ys = [o.y for o in best]
    centre_x = (min(xs) + max(xs)) // 2
    centre_y = (min(ys) + max(ys)) // 2
    # Face along the patch's LONG axis: a bridge is longer than it is wide, and
    # looking along it keeps both the covered floor and the open end in frame.
    span_x = max(xs) - min(xs)
    span_y = max(ys) - min(ys)
    middle = min(best, key=lambda o: abs(o.x - centre_x) + abs(o.y - centre_y))
    if span_x >= span_y:
        look_x, look_y = max(xs) + 20, centre_y
    else:
        look_x, look_y = centre_x, max(ys) + 20

    print(f"map            {args.vxl.stem}")
    print(f"overhangs      {len(overhangs)} columns, largest patch {len(best)}")
    print(f"patch          x {min(xs)}..{max(xs)}  y {min(ys)}..{max(ys)}")
    print(f"stand under    ({middle.x},{middle.y}) deck_z={middle.deck_z} "
          f"floor_z={middle.floor_z} clearance={middle.clearance}")
    print()
    print(f"--tutorial-stand {middle.x},{middle.y} "
          f"--tutorial-look {look_x}.5,{look_y}.5,{middle.floor_z - EYE_ABOVE_FEET:.2f}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("vxl", type=Path)
    parser.add_argument(
        "--margin",
        type=int,
        default=64,
        help="ignore columns this close to the map edge, where the border wall lives",
    )
    parser.add_argument(
        "--distance", type=int, default=34, help="how far back to stand from the subject"
    )
    parser.add_argument("--stride", type=int, default=4)
    parser.add_argument("--min-rise", type=int, default=8)
    parser.add_argument("--max-rise", type=int, default=30)
    parser.add_argument(
        "--probe",
        default="",
        help="print the surface z at X,Y (and at Y,X) instead of searching",
    )
    parser.add_argument(
        "--overhang",
        action="store_true",
        help="frame the edge of a bridge or walkway instead of a tall structure",
    )
    parser.add_argument("--min-clearance", type=int, default=4)
    args = parser.parse_args()

    if args.overhang:
        return report_overhang(args)

    surface = load_surface(args.vxl)

    if args.probe:
        px, py = (int(part) for part in args.probe.split(","))
        print(f"surface[{py}][{px}] = {surface[py][px]}")
        print(f"surface[{px}][{py}] = {surface[px][py]}")
        return 0

    # A building, not a mountain. The tallest thing on a map is usually terrain,
    # and terrain gives a steep uphill view of a cliff face -- no ground plane in
    # frame, which is exactly where cast shadows need to be judged. Prefer
    # something that rises a storey or three out of ground the camera can see.
    best: Column | None = None
    best_score = -1.0
    for y in range(args.margin, MAP_SIZE - args.margin, args.stride):
        for x in range(args.margin, MAP_SIZE - args.margin, args.stride):
            rise = prominence(surface, x, y, radius=24)
            if not (args.min_rise <= rise <= args.max_rise):
                continue
            # Flat surroundings: the shadow needs somewhere uninterrupted to land.
            neighbours = [
                surface[y + dy][x + dx]
                for dy in range(-16, 17, 8)
                for dx in range(-16, 17, 8)
                if 0 <= x + dx < MAP_SIZE and 0 <= y + dy < MAP_SIZE
            ]
            spread = max(neighbours) - min(neighbours)
            score = rise - (spread * 0.35)
            if score > best_score:
                best_score = score
                best = Column(x, y, surface[y][x])
    if best is None:
        print(
            f"no column rises {args.min_rise}..{args.max_rise} above its "
            f"surroundings; widen the band",
            file=sys.stderr,
        )
        return 1

    # Stand back along the direction that keeps us inside the map, and prefer a
    # spot whose own surface is solid ground rather than the sea bed.
    chosen = None
    for degrees in range(0, 360, 15):
        radians = math.radians(degrees)
        sx = int(round(best.x + (math.cos(radians) * args.distance)))
        sy = int(round(best.y + (math.sin(radians) * args.distance)))
        if not (args.margin <= sx < MAP_SIZE - args.margin):
            continue
        if not (args.margin <= sy < MAP_SIZE - args.margin):
            continue
        stand_z = surface[sy][sx]
        # z=239 is the bed under open water; standing there means standing in
        # the sea, which is what the derived spawn already gives us.
        if stand_z >= MAP_HEIGHT - 1:
            continue
        if chosen is None or stand_z < chosen[2]:
            chosen = (sx, sy, stand_z)
    if chosen is None:
        chosen = (best.x + args.distance, best.y, surface[best.y][best.x])

    sx, sy, stand_z = chosen
    # Aim at the subject's BASE, not its top, so the line of sight is roughly
    # horizontal and the ground between camera and structure fills the lower
    # frame. That ground is the only place a cast shadow can be judged; aiming at
    # a rooftop tilts the camera up and fills the frame with sky.
    look_z = stand_z - EYE_ABOVE_FEET

    print(f"map            {args.vxl.stem}")
    print(f"subject        ({best.x},{best.y}) surface_z={best.surface_z} "
          f"score={best_score:.1f}")
    print(f"stand          ({sx},{sy}) surface_z={stand_z}")
    print()
    # z is left to the engine: --tutorial-stand resolves the standable height with
    # the real ground predicate, which no offline estimate reproduces reliably.
    print(f"--tutorial-stand {sx},{sy} "
          f"--tutorial-look {best.x + 0.5},{best.y + 0.5},{look_z:.2f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
