# BattleSpadesClient architecture

BattleSpadesClient is a clean C++20 client for BattleSpades. It is a custom
engine, not a Unity, Unreal, or Godot project. Low-level libraries provide
portable windows, input, graphics backends, audio, networking, and file access;
gameplay behavior, voxel rendering, movement, presentation, and tooling remain
owned by this repository.

The existing Python 3 BattleSpades server remains authoritative and is not
being ported into the client. The new client must coexist with the retail
client over Protocol 168 throughout development.

## Runtime model

```text
platform events ------> sampled input ------> fixed 60 Hz client simulation
                                                   |
ENet / Protocol 168 --> decoded events ------------+
                                                   +--> immutable render snapshot
                                                   +--> audio events
                                                   +--> outgoing player intentions

immutable render snapshot --> interpolated frame --> Classic/Enhanced renderer
```

- Simulation advances on a fixed 60 Hz clock. The current application presents
  at most once per fixed tick; overdue catch-up ticks skip presentation while
  still processing input and networking. Catch-up is bounded so loading or
  resuming does not replay an unlimited backlog of renders.
- Server WorldUpdates arrive at the retail 30 Hz cadence. Remote actors are
  interpolated; the local actor predicts and reconciles against its own row.
- The gameplay world is mutated only by the simulation owner. Rendering,
  streaming, and audio consume snapshots or bounded messages.
- Network receive, asset decoding, and voxel meshing may run asynchronously,
  but results enter simulation through generation-tagged bounded queues.
- A map or session change increments an epoch. Results from an old epoch are
  discarded instead of being installed into the new world.

The source implements the native frontend, offline Tutorial, network session,
world/prediction and replicated gameplay paths. This diagram expresses ownership
constraints, not proof of complete retail parity. See [ROADMAP.md](ROADMAP.md)
for acceptance still required.

## Subsystem boundaries

| Subsystem | Owns | Must not own |
|---|---|---|
| `app` | startup, shutdown, scene/session composition | gameplay rules |
| `platform` | window, input devices, timing, filesystem paths | renderer or game state |
| `network` | ENet, Protocol 168 framing/codecs and service adapters | rendering |
| `core`, `world` and session integration | fixed clock, prediction, reconciliation, characters and entities | graphics API ownership |
| `world` | canonical VXL cells, coordinate conversion, dirty regions | UI or packet layouts |
| `assets` | asset IDs, source streams, format importers and caches | gameplay authority |
| `render` | voxel meshes, characters, effects, camera and render profiles | collision truth |
| `audio` | music, ambience, positional effects and voice allocation | world mutation |
| `ui` and `frontend` | menus, HUD, localization, input focus and typed service effects | server gameplay authority |
| `diagnostics` | metrics, captures, overlays and crash context | production behavior |

Public interfaces use stable value types such as packet events, input frames,
simulation snapshots, asset handles, and audio cues. Native graphics, audio,
and transport handles stay private to their subsystem.

The frontend boundary and its 800 by 600 Classic compatibility canvas are
specified in [FRONTEND.md](FRONTEND.md). UI widgets emit typed intentions;
session, storefront, local-host, and configuration services execute them.

## Runtime resources and distribution

Graphical resources are located from the process image, never the current
working directory. The packaged roots are
`<executable>/assets/original` and `<executable>/shaders`. Developer builds may
fall back to their compiled source-tree asset and shader roots, independently,
when one packaged family is absent. Missing required resources fail startup
with the attempted paths.

Install and archive layouts deliberately keep resources next to the executable
so an extracted ZIP remains relocatable. Windows Release installation also
copies the executable's transitive vcpkg DLLs plus the redistributable MSVC and
app-local Universal CRT files selected by the active Visual Studio 2022-or-newer
toolchain. Linux and macOS resource layout is portable, but dependency closure,
native CI smoke tests, signing, notarization, and a macOS application bundle
remain release work. Source-level platform support is not the same claim as a
validated platform package.

## World and assets

Original data remains data. Importers read the shipped formats rather than
converting the source collection by hand:

- VXL maps and raw voxel colors;
- KV6 models and prefabs;
- PNG/TGA textures and interface art;
- OGG music, ambience, and effects;
- map metadata for fog, skybox, palette, ambient emitters, and game objects.

Importers are deterministic libraries with command-line inspection tools. A
derived-data cache may contain meshes, decoded audio, or transcoded textures,
but it is disposable and keyed by the source bytes plus importer version. The
source asset tree is never modified at runtime.

VXL is the collision truth. Render meshes are derived artifacts and can lag a
bounded number of frames after an edit without changing collision. Initial map
meshing uses background jobs over immutable map data. Live edits coalesce into
a FIFO chunk queue on the presentation thread. A two-millisecond deadline,
checked between complete chunks, prevents large edit bursts from monopolizing
a frame; at least one chunk completes per presented frame. Collision and
accepted block costs remain server-authoritative.

## Rendering profiles

Both profiles consume the same simulation snapshot:

- **Legacy/compatibility** is the retail baseline: original geometry, fog, palette,
  nearest-filtered presentation, skybox, flare lighting, particles, camera,
  animations, and HUD timing.
- **Enhanced quality settings** select native presentation features such as
  shadows, lighting and particles. `settings/client_settings.*` and
  `render/quality_profile.hpp` define the available capabilities; this is not
  a promise that every proposed rendering feature is implemented.

Enhanced mode may change presentation only. It may not change collision,
hitboxes, recoil, spread, visibility distance, fog gameplay limits, movement,
or packet behavior. Every release must still validate the compatibility path.

## Build profiles

CMake presets and the build wrappers are the supported build interface:

- `native-debug` / `Debug`: Windows native debugging;
- `native-dev` / `RelWithDebInfo`: optimized Windows iteration with symbols;
- `native-release` / `Release`: Windows distribution build;
- `native-linux-dev`, `native-linux-release`, `native-macos-dev` and
  `native-macos-release`: platform-native builds;
- `debug`, `dev` and `release`: core/headless configurations.

Rendering backends and Enhanced features are compile-time capabilities with
runtime selection. Protocol 168, compatibility rendering, and deterministic tests are
never optional build features once their milestones exist.

`scripts/build.sh` selects the Linux/macOS presets. The native dependency
manifest is pinned through vcpkg. Presets and workflow definitions do not prove
current target-platform acceptance. See [RUNBOOK.md](RUNBOOK.md) for commands.

## Architectural rules

1. The server is authoritative; the client predicts intentions, never results.
2. Packet IDs and layouts live in one generated or table-driven catalog.
3. Simulation code has no dependency on the graphics or windowing API.
4. Gameplay constants are measured from retail behavior or shared deliberately
   with the server; they are not tuned to hide reconciliation errors.
5. Every asynchronous result carries a session/map epoch.
6. Hot-path allocation, unbounded queues, and blocking file I/O are release
   failures.
7. Reverse-engineering evidence is recorded in
   [COMPATIBILITY.md](COMPATIBILITY.md), not buried in implementation comments.

## Weapon contract boundary

The weapon port has one generated catalog, not independent renderer and
network tables. `tools/generate_weapon_catalog.py` imports the authoritative
`BattleSpades/server/game_constants.py` profiles, parses recovered retail
`shared/constants.py`, and statically resolves the inheritance and assignments
of every class registered by `aoslib/weapons/list.py`. It deliberately does not
execute the Python 2 retail runtime. This preserves the exact `A####` aliases
used by a class even when a later descriptive global reuses the same name with
a different value. The generator verifies the original asset tree and emits
`src/world/weapon_catalog.generated.cpp`. Tool IDs 0 through 64 are real;
Protocol 168 value 65 is the non-selectable upper sentinel. CI or a developer
changing either server profiles or recovered retail weapon/constants source
must run the generator and its `--check` mode.

Each `WeaponDefinition` has three intentional layers. The flat fields are the
server-authoritative gameplay contract. `retail` holds the recovered class's
five-region damage, ammo tuple, recoil/accuracy, timing, zoom, crosshair,
inventory and interaction flags for prediction and presentation. `constants`
is a per-tool span of specialized numeric, boolean and tuple values such as
rocket flight, drill excavation and Molotov block-fire behavior. Consumers use
`find_retail_weapon_constant()` for named specialized values; they must not
create a second constants switch.

`WeaponReplicationState` owns loadout, selected tool, magazine, stock, reload
edge, and observer-shot sequencing. `WeaponRuntime` implements all 65 tool
mechanisms: firearms, burst/spin-up/shotgun variants, melee, cooked and charged
throws, launchers, builders, prefabs, deployables, C4, objectives, paint, UGC,
Block Sucker and disguise. Presentation reads this state but cannot mutate ammo
implicitly.

The protocol boundary is split by wire responsibility. `protocol168_weapons`
owns ClientData, Shoot, feedback, hit response, oriented items, loadouts,
restock, reload and WorldUpdate weapon rows. `protocol168_tool_actions` owns the
distinct placement/editor/objective packets, preserving raw-voxel versus 1/64
fixed-point fields and the asymmetric prefab build/erase layouts.
`protocol168_weapon_action_adapter` is the only bridge from semantic runtime
actions to outbound payloads. In particular, objective tools emit
DropPickup(71), MG use emits UseCommand(86), and C4 place/detonate remain
different packets.

The generated catalog also carries exact multipart first/third-person KV6
composites, sight/casing/tracer assets, and recovered sound-group names.
`load_weapon_models()` converts those assets to render-ready meshes and fails
closed on incomplete data. `evaluate_weapon_view_model()` applies the recovered
animation family to every tool ID while retaining the older Tutorial wrapper.

The live session now owns CreatePlayer/loadout state, WorldUpdate transforms,
fixed-rate ClientData, semantic outbound weapon actions, and authoritative
terrain replication. StateData drives the START-to-class-selection gate; one
atomic SetClassLoadout(13) contains the class, four selected item rows, three
prefab names, UGC tools, and equipment IDs above 64. The combined HUD inventory
expands PREFAB_TOOL(23) into named construct variants; FLAREBLOCK_TOOL(22) is a
separate mechanism and packet family.

Flight-pack prediction advances activation delay, ignition cost, native thrust
handoff, consumption, exhaustion and regeneration from the same consumed input
frames as the server. It does not wait for the active bit to return before
predicting a boost. Decoded fuel and ability rows reconcile at their acknowledged
frame, then replay the retained ability history. Movement replay uses the thrust
state consumed by each historical step. The local fuel gauge reads this predicted
resource between server updates; wire fuel quantization alone does not alter the
exhaustion frame.

Inbound combat, death/respawn, entities, objectives and result/HUD paths exist
in the session/frontend/world code. Their existence does not close
owner/observer/late-join, adverse-network or visual/audio parity gates. Use
[ROADMAP.md](ROADMAP.md), not a historical probe count, for remaining acceptance.

## Collection and online services

Identity, discovery, scores and social services use bounded asynchronous
adapters under `network`. Frontend models own revisions and visible state;
worker results must match the active request/session. Owned servers/tunnels
live under `platform`. Identity, projects and pending hosted results outlive
temporary server sessions.

The local-server owner normalizes bundle paths before launch and cleans the
previous session before a retry. macOS uses `posix_spawn` with child-only cwd,
environment and standard streams. POSIX shutdown uses a socket with SIGPIPE
suppressed for that channel. The owned leader remains waitable until its process
group is stopped, preventing helper leaks without targeting a reused process ID.
Windows creates the child suspended, assigns its kill-on-close job, then resumes
it. A job assignment failure ends the suspended launch instead of leaving an
unowned child. Move assignment stops the destination's previous process before
transferring ownership. Bundled POSIX executables keep their source permissions
when installed.

Frontend cancellation signals the live connection immediately, then transfers
its joining destructor to a bounded cleanup queue. Hosted process/tunnel/relay
retirement and map bootstrap derivation each use separate queues. Reservations cover live resources and pending
cleanup, preventing repeated Start/Back from accumulating unbounded workers.
Application shutdown drains those queues; system DNS can still delay a final
join, even though it no longer blocks ordinary menu cancellation.

Relay readiness requires valid server acknowledgements, not successful UDP
sends. Lost acknowledgement leases or terminal socket errors end the tunnel;
the frontend releases the host and unwinds the match route. Inactive client
forwarding sockets expire so successive guests can reuse the bounded capacity.

Inventory uses native RmlUi through the bgfx UI renderer, with the existing
presentation as fallback. The backend owns rewards/equipment; `InventorySession`
owns request/retry state. See [INVENTORY.md](INVENTORY.md). Catalogue hashes
confine cosmetic resources. Bounded AngelScript handles supported weapon
presentation only. Packet 240 is a separately negotiated appearance envelope,
not a change to baseline Protocol 168. See [COMMUNITY_COSMETICS.md](COMMUNITY_COSMETICS.md).

The UI renderer submits sprites and markup in their original drawing order.
Texture handles retain generations across renderer restarts; runtime textures
are mutable, cached image textures are immutable, and preview targets are
borrowed from the renderer. Empty finite clips discard draws, while invalid
coordinates and mesh indices fail before GPU submission.
Transient UI buffer exhaustion skips and counts affected draws for that frame
and resumes normally on the next frame. It does not terminate the client.
World mesh replacement validates the full new mesh and allocates replacement
buffers before releasing the previous resident mesh. Failed initialization
rolls back partially allocated renderer resources before a retry.

## Explicit non-goals

- Replacing the BattleSpades server or moving authority to clients.
- Designing a different voxel game before retail compatibility exists.
- Using a general-purpose game engine or generic rigid-body physics.
- Requiring Enhanced rendering for correct gameplay.
- Loading retail Python 2 modules or native `.pyd` files in production.
- Changing Protocol 168 to make the new client easier to implement.
- Embedding the authoritative account/reward service in the native client.
