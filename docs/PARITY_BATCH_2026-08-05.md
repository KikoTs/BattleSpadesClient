# Runtime parity batch — 2026-08-05

This note separates client presentation fixes from server-authoritative issues.
`G:/AoSRevival/BattleSpades` is a read-only compatibility oracle for this
project. No server file was changed while addressing this batch.

## Implemented and regression-tested

- Protocol 168 damage type 15 (landmine) now accepts the retail fractional
  blast centre, removes the exact radius-one 19-cell footprint and emits the
  normal explosion presentation. Previously the client rejected the packet
  before either terrain or VFX could see it.
- Damage type 41 (C4) now accepts its fractional face-centred position and
  removes the exact radius-two 81-cell footprint.
- Vote bindings display `F1`, `F2`, and `F3`, matching the configured SDL
  scancodes, rather than the misleading labels `1`, `2`, and `3`.
- Engineer flight now has `JP_ignite -> JP_flight_lp -> JP_release` audio and
  the recovered two-puff, 0.02-second jetpack exhaust cadence for both local
  and replicated players.
- Water movement banks remain selected by surface state and have a dedicated
  audible gain; the old shared 0.45 gain made wading effectively inaudible.
- ShootResponse feedback suppresses the local red hit/cue when the nearest
  authoritative hit target is a teammate. Crosshair hover names are white for
  teammates and red for enemies.
- Player models receive a restrained model-only contrast/gain correction.
  Disguise blocks deliberately do not: they must continue to match terrain.
- Opaque player/world models render before translucent green placement ghosts,
  so a ghost can no longer make a player behind it appear composited into the
  preview.
- Explosions publish a central light plus three bounded moving lights attached
  to their hot trails. Bright firearm/launcher families also publish a very
  short, low-energy muzzle light; sniper rifles remain laser-only.
- Runtime glyph textures use point filtering. They are already rasterized at
  their requested retail pixel size, so bilinear filtering only blurred small
  HUD text a second time.
- Enhanced world lighting receives a sub-byte, world-anchored dither before
  fog. It breaks up visible lighting bands without crawling with the camera,
  affecting Legacy parity, or outlining fog-culled chunks.
- The health crate now draws retail's unique `spot_shadow.tga` contact decal at
  the recovered 1.0-block size and `(0.5, 0.5, 0.0)` query offset. The client
  projects it onto live VXL, so it remains on the floor during a crate drop and
  disappears atomically when the pickup is consumed.

## Verified existing behavior (do not duplicate)

- Scoreboard rows already display each WorldUpdate player's authoritative ping.
- Medium and above already combine voxel corner/face occlusion, a sun shadow
  map, and the skylight-horizon cover term. Legacy intentionally retains only
  the recovered baked retail shading.
- Map fog reaches exactly full opacity at the chunk-cull distance. Initial map
  and reconnect terrain catch-up remain behind the loading gate until every
  affected chunk is resident.
- The two sniper tools intentionally use different assets: tool 18 uses
  `semishoot`; tool 19 uses `semi_weak_shoot`.
- PlayMusic is server-authoritative, including ending and last-standing banks.
- Pickup destruction already emits the recovered crate star burst; a later
  CreateEntity recreates the retained mesh slot without cold re-upload.
- The recovered sky shader adds authored UV speed to UV, and retail advances
  its counter once per 60 Hz draw. Invasion/Volcano intentionally contains both
  positive and negative layer speeds. Reversing every layer or treating the
  counter as seconds would contradict the recovered client and slow animation
  by roughly sixty times. A remaining subjective speed mismatch therefore
  needs a side-by-side capture of the same layer, not a global sign hack.

## Server-authoritative findings requiring a separate approved server change

### Full players consume crates

`server/entities/behaviors.py::PickupCrateBehavior.on_touch` calls its refill
unconditionally, destroys the entity, and schedules its respawn. A client
cannot safely reject that destruction: the crate has already disappeared for
every participant and the server owns its respawn timer.

The requested policy should be implemented server-side as an atomic
`try_refill(player) -> bool` transaction:

- ammo succeeds only when at least one active firearm reserve/clip can gain;
- health succeeds only below the active class maximum HP;
- blocks succeed only below maximum block stock;
- jetpack fuel succeeds only below that loadout's maximum fuel;
- a failed transaction leaves the entity alive, sends no pickup sound and
  starts no respawn timer;
- a successful transaction preserves the existing DestroyEntity, pickup sound
  and respawn scheduling order.

Required tests: full/partial/empty inventory for every crate type, two players
touching in one tick, class max-HP variance, and disconnect during refill.

### Drill projectile versus a player

The server's projectile engine treats the drill as a contact projectile. World
contact produces repeated `DrillContact` bore events while player contact goes
through the ordinary explosion path. Whether player contact should be a direct
kill rather than a 50-damage blast is combat authority and cannot be repaired
by hiding the client explosion: doing so would only conceal real server damage.

The server change needs an explicit drill/player branch before ordinary
contact explosion, authoritative kill attribution, DestroyEntity replication,
and tests for enemy, friendly-fire, spawn-protected, already-dead and
terrain/player-same-step cases.

## Still requiring a visual/runtime capture

- exact particle apparent size at each Effect Quality tier;
- shot-rejection rate with server rejection reasons enabled (origin drift,
  orientation mismatch, fire-rate or ammo rejection);
- a packet/VXL capture for any remaining crate placeholder voxel, distinguishing
  a stock-map authoring marker from a missing CreateEntity or terrain mutation;
- a frame-matched Invasion/Volcano sky capture to identify a layer-specific
  speed discrepancy, if one remains after testing this build.
