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

- Simulation advances on a fixed 60 Hz clock. Rendering may run at any rate.
- Server WorldUpdates arrive at the retail 30 Hz cadence. Remote actors are
  interpolated; the local actor predicts and reconciles against its own row.
- The gameplay world is mutated only by the simulation owner. Rendering,
  streaming, and audio consume snapshots or bounded messages.
- Network receive, asset decoding, and voxel meshing may run asynchronously,
  but results enter simulation through generation-tagged bounded queues.
- A map or session change increments an epoch. Results from an old epoch are
  discarded instead of being installed into the new world.

The current executable uses this runtime for the native frontend and headless
tests. Network, world, prediction, and snapshot branches in the model above are
architectural requirements, not completed features.

## Subsystem boundaries

| Subsystem | Owns | Must not own |
|---|---|---|
| `app` | startup, shutdown, scene/session composition | gameplay rules |
| `platform` | window, input devices, timing, filesystem paths | renderer or game state |
| `protocol` | ENet transport, Protocol 168 framing and packet codecs | gameplay mutation |
| `simulation` | fixed clock, prediction, reconciliation, characters and entities | graphics API calls |
| `world` | canonical VXL cells, coordinate conversion, dirty regions | UI or packet layouts |
| `assets` | asset IDs, source streams, format importers and caches | gameplay authority |
| `render` | voxel meshes, characters, effects, camera and render profiles | collision truth |
| `audio` | music, ambience, positional effects and voice allocation | world mutation |
| `ui` | menus, HUD, localization and input focus | protocol-specific branching |
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
bounded number of frames after an edit without changing collision. Dirty
columns rebuild in background jobs and install atomically for the current map
epoch.

## Rendering profiles

Both profiles consume the same simulation snapshot:

- **Classic** is the compatibility baseline: original geometry, fog, palette,
  nearest-filtered presentation, skybox, flare lighting, particles, camera,
  animations, and HUD timing.
- **Enhanced** is optional: modern antialiasing, shadows, ambient occlusion,
  improved water, higher-quality lighting, and particles.

Enhanced mode may change presentation only. It may not change collision,
hitboxes, recoil, spread, visibility distance, fog gameplay limits, movement,
or packet behavior. Every release must still build and pass in Classic mode.

## Build profiles

CMake presets are the supported Windows build interface:

- `debug`: assertions, sanitizable code, protocol validation and diagnostics;
- `dev`: optimized iteration with symbols and bounded debug overlays;
- `release`: optimized, diagnostics off by default, reproducible packaging.

Rendering backends and Enhanced features are compile-time capabilities with
runtime selection. Protocol 168, Classic rendering, and deterministic tests are
never optional build features once their milestones exist.

Linux and macOS currently use ordinary CMake configure/build/install commands
until platform presets and CI runners are checked in. The native dependency
manifest is pinned through vcpkg on every platform.

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

Remaining Phase 3 work is complete inbound combat presentation: hit/death and
respawn ownership, zoom/laser/muzzle effects for remote actors, projectile and
entity lifetimes, mode objectives, and broader owner/observer/late-join tests.
The current live probes prove firearm ShootFeedback and prefab block
replication, but do not claim every inbound weapon family is presented yet.

## Explicit non-goals

- Replacing the BattleSpades server or moving authority to clients.
- Designing a different voxel game before retail compatibility exists.
- Using a general-purpose game engine or generic rigid-body physics.
- Requiring Enhanced rendering for correct gameplay.
- Loading retail Python 2 modules or native `.pyd` files in production.
- Changing Protocol 168 to make the new client easier to implement.
- Shipping an asset editor, matchmaking replacement, or account service in the
  first playable milestone.
