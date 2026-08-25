# OpenSpades VXL reference boundary

OpenSpades was inspected at commit `ff9b3e71b9ad26dda940923515de8b46f4bba5a5`.
Its `Sources/Client/GameMap.cpp` span traversal and renderer organization are
useful independent evidence, but its source is GPL-3.0 and its network client
implements the classic 0.75 protocol. No OpenSpades source was copied or linked.

BattleSpades remains authoritative for this client:

- Protocol 168 owns packet framing and compressed MapSync delivery.
- `aoslib/vxl.pyx` defines the 512x512x240 canonical world, source-map vertical
  normalization, underground fill, color representation, and forced z=239 bed.
- `world::VxlMap` consumes only the decompressed VXL span stream, allowing the
  Tutorial disk loader and future network adapter to share one validated path.
- Malformed spans fail closed before a partial world can become playable.

The next renderer stage will consume this map through bounded chunk meshes;
OpenSpades' render code remains an architectural reference, not a dependency.
