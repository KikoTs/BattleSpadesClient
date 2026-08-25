# Client parity fix pass — 2026-08-05

This checklist is the acceptance ledger for the current gameplay/UI parity pass.
The retail/decompiled client is the behavioral reference and the existing
BattleSpades server is a read-only protocol oracle.  A server change is out of
scope unless a captured packet sequence proves that the client cannot implement
the behavior from the existing wire data.

Each item requires three forms of evidence before it is marked complete:

1. A retail source/decompile citation or a captured packet invariant.
2. A focused automated test for deterministic behavior.
3. A full build plus headless smoke; visual/audio items remain `visual-check`
   until compared in a live client session.

| ID | Area | Required behavior | Primary owner | Status |
|---:|---|---|---|---|
| P01 | Server browser | Show the retail Add control and accept a selected/manual endpoint | UI/audio | implemented; live-check |
| P02 | Server browser | Persist selected endpoints and display them on Favorites | UI/audio | implemented; live-check |
| P03 | Menu audio | Gamemode selection plays `secondary_menu_bed_001` | UI/audio | implemented; audio-check |
| P04 | Menu audio | Character selection uses recovered retail selection cues | UI/audio | implemented; audio-check |
| P05 | Spatial audio | Replace flat mix with stable listener-relative attenuation/occlusion | integration | implemented; audio-check |
| P06 | Movement | Eliminate false local reconciliation teleports | movement | implemented; live-check |
| P07 | Inventory | Ignore unsolicited/stale tool changes; never switch without input or authoritative loadout transition | movement | implemented; live-check |
| P08 | Movement/audio | Soft landing preserves speed; hard landing slows and emits the recovered impact cue | movement | implemented; live-check |
| P09 | Damage audio | Local hero pain voice and hit response are reliable without duplicate spam | movement | implemented; audio-check |
| P10 | Landmine | Snap the placed entity to one voxel support, not a four-voxel corner | deployables | implemented; visual-check |
| P11 | Engineer death | Complete rocketpack launch/rotation/explosion particle sequence | deployables | implemented with recovered attachment; visual-check |
| P12 | Prefab VFX | Emit recovered placement smoke and placement sound | deployables | implemented; visual/audio-check |
| P13 | Block VFX | Emit recovered normal block-placement particles | deployables | implemented; visual-check |
| P14 | Prefab replication | Commit a received prefab atomically rather than visibly voxel-by-voxel | deployables | implemented; live-check |
| P15 | Zoom audio | Emit distinct RMB activate/deactivate cues where the retail weapon supports zoom | UI/audio | implemented; audio-check |
| P16 | Weapon mix | Reduce first-person gunfire while restoring audible, directional remote gunfire | integration | implemented; audio-check |
| P17 | Timed explosive | Periodic beep plus camera-facing world countdown above timed dynamite; C4 remains remote-detonated | deployables | implemented; source-corrected from request |
| P18 | Mode countdown audio | Never play the Zombie outbreak cue for TDM deaths; consume it only from authoritative `PlaySound(23)` | UI/audio | implemented; protocol-verified |
| P19 | Enhanced voxels | Improve normal response/roughness while preserving the legacy graphics profile | integration | implemented; visual-check |

## Cross-cutting invariants

- Network packets may update inventory only through their documented direction
  and state transition. A late spawn/loadout packet must not masquerade as user
  scroll input.
- Local prediction is reconciled against the acknowledged historical sample,
  not the current pose. Only semantic teleports may bypass smoothing.
- Prefab terrain commits are atomic at presentation level even when the protocol
  transports multiple voxel records.
- Named UI/voice/countdown cues are protected from generic voice-pool eviction.
- Enhanced voxel material changes are isolated behind the enhanced graphics
  profile; legacy rendering remains a parity target.

## Final validation matrix

- Focused unit tests for every deterministic state transition.
- Full `ctest --preset native-dev --output-on-failure`.
- Installed headless smoke for at least 120 ticks.
- Two-client live session: movement/jump/landing, weapon switching, damage,
  prefab/block placement, mine/C4, engineer death, and first/third-person audio.
- Server browser live session: add endpoint, restart client, verify Favorites,
  connect, disconnect, and reconnect.
