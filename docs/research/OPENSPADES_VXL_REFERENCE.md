# OpenSpades VXL reference boundary

> **Recovery/specification reference.** Preserve the measured retail behavior and its evidence. Implementation updates, old build paths, test counts and session constraints below describe their original investigation; they are not current release or deployment status. Use the [maintained documentation index](../README.md) for present operating instructions and recheck historical findings against current source.

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

For the native shadow refinement, `Resources/Shaders/Shadow/Common.fs` and
`MapSoft.fs` at the same reference commit were inspected for the separation of
ambient/direct light and soft-shadow organization. No shader source was copied.
The native implementation uses its own bgfx projection, receiver-plane depth
adjustment and fixed nine-tap filter; it does not implement OpenSpades' blocker
search or PCSS. See [Settings](../SETTINGS.md) for current capabilities and
[Runbook](../RUNBOOK.md) for backend verification commands.
