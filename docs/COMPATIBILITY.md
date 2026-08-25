# Compatibility contract

BattleSpadesClient is successful when the BattleSpades server cannot tell
whether a normal participant uses the retail client or the new client, except
through explicitly negotiated future capabilities. Compatibility is behavioral
as well as byte-level: timing, prediction, animation, sound, map atmosphere,
and failure handling matter.

## Sources of truth

When evidence conflicts, use this order:

1. A clean retail client observed against a controlled server.
2. Decompiled/IDA control flow and captured field access.
3. Retail packet read/write implementations and raw packet captures.
4. BattleSpades characterization tests and maintained protocol documentation.
5. Reconstructed source as a hypothesis, not as independent proof.

Every recovered invariant should record the retail build, reproduction,
capture or address, and an automated test where practical.

## Wire and session invariants

- Transport is ENet using Protocol 168, one channel, and the retail compression
  and payload-prefix behavior.
- Packet IDs, field widths, signedness, ordering, string termination, and
  reliable/unreliable delivery flags must match the existing server contract.
- The client follows the complete loader handshake, CRC/full VXL transfer,
  StateData setup, roster reveal, gameplay, end-of-round, and map-transition
  lifecycle. It must not accept gameplay packets into a partially built scene.
- The current loader deliberately advertises CRC zero so the authoritative
  server sends a complete map. `MapSyncChunk` payloads are inflated and
  consumed in order; a map is published only after the complete 512x512 column
  stream validates. Delta/cache negotiation remains a later optimization.
- Offline development uses packet 105 with a zero-length ticket. This selects
  BattleSpades' legacy/offline identity path and leaves payload XOR disabled.
  A made-up non-empty ticket is forbidden because the server derives its XOR
  key from those bytes and would desynchronize every later packet.
- Entity create/change/destroy lifetimes are generation-safe. Unknown destroys
  are ignored diagnostically rather than dereferenced.
- Packet decoding is bounded and fails closed on malformed lengths, compressed
  data, strings, entity IDs, and collection counts.
- Protocol extensions require an explicit capability handshake and cannot alter
  the baseline stream seen by retail peers.

## Time and replication invariants

- Client simulation and input sampling use fixed 60 Hz steps.
- The normal authoritative WorldUpdate stream is 30 Hz and unreliable.
- Local-player rows reconcile prediction history; remote-player rows drive
  interpolation. They are not processed as the same operation.
- Rendering interpolates between simulation snapshots and never changes the
  simulation step to follow frame rate.
- Block, prefab, collapse, projectile, sound, and tool animations preserve the
  server's ordering. A visual effect may not mutate authoritative terrain.

## Coordinate and asset invariants

- Raw packet block positions use retail VXL coordinates. Conversion to camera,
  physics, renderer, or audio coordinates occurs in named boundary functions.
- VXL solidity and RGB state are canonical. Meshing never invents or removes
  collision.
- KV6 pivot, axis, rotation, palette, and raw-color behavior must be tested
  against known retail placements.
- Map fog, density, skybox, ground/water colors, ambient loops, flare lighting,
  and resource markers come from validated map metadata and stock asset IDs.
- Missing optional art falls back safely. Missing collision/map data is a
  session load failure, not a generated substitute.

## Presentation profiles

Classic mode is the visual and input parity target. Captures compare camera
projection, voxel silhouettes, colors, fog, weapon pose, animation phase,
particles, HUD placement, sound selection, and positional attenuation.

Enhanced mode is compared against Classic for gameplay equivalence rather than
pixel identity. Switching profiles during a session must not change:

- player or projectile positions;
- collision and hit results;
- input sampling or reconciliation;
- weapon cadence, spread, recoil, or inventory;
- fog visibility rules used by gameplay.

## Executable parity gates

Compatibility tests are ordinary executables so they can run in CI without a
GPU desktop or a retail installation. The current foundation provides:

| Gate | Current responsibility |
|---|---|
| `aos_core_tests` | command-line parsing, fixed-step runtime, lifecycle and shutdown |
| `aos_resource_paths_tests` | packaged/developer precedence, independent roots and failure diagnostics |
| `aos_utf8_tests` | bounded codepoint-safe UTF-8 prefix handling |
| `aos_ui_tests` | design-canvas geometry, interaction, focus and screen-stack behavior |
| `aos_frontend_tests` | Select Menu actions, asset manifest and transition routing |
| `aos_main_menu_presentation_tests` | deterministic renderer-neutral Select Menu draw data |
| `aos_settings_*` / `aos_resolution_confirmation_tests` | settings transactions, persistence, presentation, binding, and display rollback |
| `aos_join_match_frontend_tests` | Join routing plus browser refresh generations, regions, sorting, scrolling, and double-click handoff |
| `aos_quick_play_frontend_tests` | 11-playlist discovery, ownership, server choice, and typed Quick Play handoffs |
| `aos_custom_match_frontend_tests` | bounded lobby discovery, filters, selection, offline/error state, and typed lobby handoffs |
| `aos_create_match_menu_tests` | ten modes, compatible maps, full rule catalog, lobby state, and nested navigation |
| `aos_ugc_*` | UGC route/publish models, validation, dialogs, and renderer-neutral presentations |
| `aos_leaderboard_menu_tests` / `aos_player_profile_menu_tests` | guarded async generations, filters, caches, and explicit unavailable/not-found states |
| `aos_preload_loading_tests` / `aos_loading_presentation_tests` | bounded preload state and distinct boot/match-loading composition |
| `aos_sdl_window_tests` | SDL window-event translation and capture cancellation |
| `aos_text_tests` | native FreeType/HarfBuzz shaping, fitting and raster output |
| `aos_protocol168_session_tests` | loader ordering, offline ticket, bounded LZF, full-map publication and malformed-packet policy |
| `aos_protocol168_players_tests` | exact CreatePlayer packet 28 layout and generation-safe roster replacement |

The desktop-only Select Menu gate complements those headless tests. Exact
native client-area captures pass full-frame RGB SSIM at 800x600 (`0.985400`,
minimum `0.98`) and 1680x1050 (`0.967606`, minimum `0.96`). A real 1000x700
window also passes exact sizing, the retail 320x240 minimum, minimize/restore,
focus-loss and mouse-leave capture cancellation, and graceful Quit. This is
the static idle Select Menu/window evidence. A separate 18-stage native-window
smoke covers boot loading, Join, hardened Server Browser, Direct Connect, Quick
Play, Create Match and nested mode selection, UGC Select/Publish, Leaderboard,
Profile, match loading, and retained two-layer slide frames. These are frontend
presentation/navigation gates, not evidence for networking, world simulation,
or gameplay parity.

The following names describe remaining world/protocol parity gates; the
session and player packet targets above now exist, while these targets do not:

| Gate | Responsibility |
|---|---|
| `bsc_vxl_tests` | map decode, solidity/RGB hashes, coordinate fixtures and edit round-trips |
| `bsc_kv6_tests` | model bounds, pivots, rotations, colors and prefab transforms |
| `bsc_sim_tests` | fixed-step movement, input history, prediction and reconciliation replays |
| `bsc_replay` | headless capture replay with final world/entity/audio-event hashes |
| `bsc_asset_probe` | importer validation and deterministic derived-data hashes |
| `bsc_render_probe` | deterministic Classic reference frames on the CI reference backend |

The exact target set may grow, but a compatibility rule cannot rely only on an
interactive screenshot. Golden fixtures store provenance and are versioned;
updating one requires an evidence note explaining why the old result was
wrong.

## First vertical-slice acceptance

The first playable slice is accepted only when:

1. A clean client loads one real VXL through the normal server handshake.
2. One human can join, select a team/class, walk, look, jump, crouch, and fire
   one rifle without sustained rollback.
3. A second client sees the first player's pose, tool, shots, damage, death,
   respawn, block placement, and block destruction.
4. Disconnect/reconnect reconstructs identical terrain colors and solidity.
5. Classic rendering reproduces the map fog, skybox, flare light, ambience,
   weapon view, and core HUD.
6. A map transition destroys the old scene and loads the next without stale
   entities, jobs, audio, or GPU resources.
7. Headless replay produces the same final hashes on Windows, Linux, and macOS.

Retail and new clients remain part of the same two-client matrix until all
supported gameplay has parity.

## Release rule

A feature is incomplete until it passes its unit/golden gate, a headless
session replay, and a live mixed-client observation. A crash, invisible
authoritative object, phantom collision, unexplained correction, or
profile-dependent gameplay result blocks release.
