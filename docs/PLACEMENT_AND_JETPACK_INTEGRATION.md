# Placement and jetpack presentation integration

> **Recovery/specification reference.** Preserve the measured retail behavior and its evidence. Implementation updates, old build paths, test counts and session constraints below describe their original investigation; they are not current release or deployment status. Use the [maintained documentation index](README.md) for present operating instructions and recheck historical findings against current source.

This note covers the isolated client-side parity helpers. It intentionally does
not change `src/frontend/native_frontend_module.cpp`; that composition root must
wire the helpers at its existing packet/audio/render boundaries.

## Recovered invariants

- A ground landmine is centred on one supporting voxel: `x + 0.5, y + 0.5`.
  Its packet contains a raw support cell, not the already-centred display point.
- Ordinary block placement uses the `build` audio stem.
- Competitive prefab placement uses `prefabbuild`; UGC placement uses
  `ugc_place`. One completed prefab produces one sound edge.
- Packet 30 may split one prefab into multiple `[from,to)` slices. Those slices
  must not become visible independently. Packet 29 (`PrefabComplete`) is the
  atomic reveal boundary.
- Dynamite (entity 10) owns the seven-second fuse and `dynamite_tick` loop.
  C4 (entity 38) has no retail timer; it is remotely detonated. A C4 countdown
  would be a fabricated mechanic, even if a malformed ChangeEntity supplies a
  fuse value.
- Equipment ids 66, 67, 68 and 69 use `jetpack.kv6`, `Jetpack2.kv6`,
  `JetpackEngineer.kv6` and `JetpackUGCBuilder.kv6`, respectively. Retaining
  only a boolean loses the engineer/glider model during the death flight.

## Required frontend hooks

1. In `apply_live_prefab_mutation`, expand build packet 30 into
   `PrefabPlacementVoxel` records and call `PrefabPlacementTransaction::stage`.
   Do not call `TerrainReplica::apply_colored_cells` yet. Erase-prefab packets
   remain immediate because packet 29 is the build-tool completion latch.
2. In the `PrefabCompletePacket` branch, copy `transaction.voxels()` into the
   replica's coloured-cell format and submit the complete vector through one
   `apply_colored_cells` call. Then emit `emit_prefab_placement`, play
   `placement_sound_stem(prefab)` once, and clear the transaction. Clear and
   fail closed on decode/range errors, disconnect, map epoch change, and match
   teardown. Historical catch-up commits silently.
3. After an accepted packet 32/33 ordinary build is applied, emit
   `emit_block_placement` and play `placement_sound_stem(block)` once. Never
   present feedback for preview, rejected input, or historical catch-up.
4. In KillAction and dead-WorldUpdate handling, pass `retail_jetpack_id` to the
   id-preserving `JetpackDeathPresentation::begin` overload. While retained,
   draw `retail_jetpack_model(snapshot.jetpack_id)` on the corpse and feed the
   authoritative corpse point to `emit_jetpack_death_thruster`. Packet 36 still
   owns the final body explosion; particles here are exhaust, not damage.
5. For local-entity HUD/audio, call `timed_explosive_presentation`. When visible,
   draw its `seconds` above `label_position` and attach its `ticking_sound` loop
   to the entity. Stop the loop on entity deletion/detonation. Entity 38 always
   returns invisible by design.

## Focused verification

Build and run:

```powershell
cmake --build --preset native-dev --target aos_local_entity_tests aos_prefab_placement_tests aos_particle_system_tests aos_jetpack_death_tests
ctest --preset native-dev -R "local_entity|prefab_placement|particle_system|jetpack_death" --output-on-failure
```

The tests pin landmine centring, packet-slice atomicity, legal overlap colour
preservation, placement VFX bounds, exact placement stems, dynamite-vs-C4 fuse
semantics, and exact jetpack model selection.
