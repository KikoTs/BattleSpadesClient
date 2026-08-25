# BattleSpadesClient roadmap

Development proceeds in small compatibility-gated slices. The retail client
stays usable while the replacement grows; there is no big-bang switchover.

Current status: the Windows x64 native executable opens and renders the Select
Menu, Settings, Join/Browser/Quick Play/Custom Match, Create Match, UGC,
Leaderboard, Profile, and loading destinations from packaged resources. Phase
0 is still open because Linux/macOS native CI and package validation, worker
coverage, and several planned executable parity gates are not complete. The
Windows frontend visual, navigation, window-lifecycle, and interaction gates
are complete for the implemented offline surfaces; their live networking,
HTTP, lobby, editor, and gameplay adapters remain later milestones.

## Phase 0 — reproducible foundation

- Establish C++20 CMake presets for Windows, Linux, and macOS.
- Add platform/window, logging, jobs, tests, and clean shutdown.
- Pin low-level dependencies and produce deterministic Debug, Dev, and Release
  builds.
- Copy or reference the preserved source asset tree read-only.
- Create the executable parity gates described in
  [COMPATIBILITY.md](COMPATIBILITY.md).

Exit gate: a packaged executable opens a window, runs a fixed 60 Hz headless
clock, exercises worker shutdown, and passes CI on all three platforms.

Windows status: graphical startup, fixed-step headless operation, native unit
tests, installation, ZIP creation, and execution from an unrelated working
directory have been exercised. This is evidence for one platform, not the
three-platform exit gate.

## Phase 1 — offline world

- Implement VXL and KV6 importers with golden fixtures.
- Render a real map with the Classic camera, palette, fog, water, skybox, and
  flare lighting.
- Add deterministic voxel collision, grounded movement, jump, crouch, sprint,
  ray selection, and background dirty-mesh rebuilds.
- Add input bindings, basic HUD diagnostics, positional audio, and ambience.

Exit gate: an offline movement replay has identical final state at different
render frame rates, and reconnect-free map edits preserve VXL solidity/RGB
hashes.

### Early frontend slice

The original early slice was the small shell described in
[FRONTEND.md](FRONTEND.md): reference-canvas transforms, widget interaction,
screen routing, and the retail Select Menu. It established the engine boundaries
for input, assets, text, audio, and rendering. The user subsequently pulled the
offline presentation/model portion of the destination menus forward; external
server, lobby, HTTP, Tutorial, UGC-editor, and gameplay services remain in their
original phases.

Implementation status: SDL3 input/window ownership, bgfx rendering,
FreeType/HarfBuzz text, OpenAL menu audio, bounded preload/warmup, portable
resource discovery, executable-adjacent settings persistence, and the offline
frontend route tree are live. Exact native client-area capture and full-frame
RGB comparison pass at 800x600 (`0.985400`, minimum `0.98`) and 1680x1050
(`0.967606`, minimum `0.96`). The real-window lifecycle smoke passes at
1000x700, and the 18-stage navigation smoke covers boot, nested forward/back
slides, the destination screens, and match loading. This closes the offline
frontend presentation/navigation gate, not Protocol 168, hosting, online
services, UGC authoring, or playable-client milestones.

## Phase 2 — first vertical slice

This is the first playable target, deliberately limited to:

- Protocol 168 connection and normal map/session handshake;
- one map, two teams, one class, and one rifle;
- spawn, walk, look, jump, crouch, sprint, aim, shoot, damage, death, respawn;
- one block tool with palette, placement, destruction, and reconnect catch-up;
- CreatePlayer, WorldUpdate prediction/reconciliation, remote interpolation,
  chat, scoreboard, fog, skybox, flare light, core particles and sounds;
- clean disconnect, reconnect, same-map restart, and one full map transition.

Exit gate: two new clients plus a mixed retail/new-client pair complete the
[first vertical-slice acceptance](COMPATIBILITY.md#first-vertical-slice-acceptance)
without crash, phantom terrain, invisible players, sustained rollback, or
profile-dependent simulation.

## Phase 3 — core Battle Builders parity

- Complete firearms, melee, recoil/spread, reloads, hit feedback and audio.
- Complete class movement profiles, jetpacks, glider/parachute, inventory,
  pickups, palette, prefabs, painting, collapse, and terrain repair behavior.
- Complete explosives, projectiles, deployables, turrets, disguise, drill,
  Block Cannon, flare blocks, corpses, hitboxes, effects, and late-join state.
- Implement TDM, CTF, Classic CTF, Arena, VIP, and Zombie presentation.

Exit gate: every supported server action has owner, observer, late-join, death,
round-reset, and map-transition coverage in headless and mixed-client tests.

## Phase 4 — frontend and creation tools

- Attach discovery, direct-connect, lobby, localization, score/profile, and
  platform-overlay services to the completed offline frontend models; then add
  class/loadout screens and controller/accessibility polish.
- Rebuild Tutorial as a distinct local-server launch path.
- Rebuild Map Creator/UGC editing, validation, previews, persistence, publish
  catalog, and hidden local-server lifecycle.
- Preserve original file formats so projects remain portable.

Exit gate: a user can install, configure, join, host, complete Tutorial, author
and reopen a map, and exit without orphaned processes or corrupted content.

## Phase 5 — Enhanced presentation

- Add optional modern antialiasing, shadows, ambient occlusion, improved water,
  higher-resolution effects, scalable UI, and expanded graphics settings.
- Profile GPU/CPU/memory budgets and build graceful quality tiers.
- Keep Classic as a fully supported selectable profile.

Exit gate: deterministic replay hashes for gameplay are identical between
Classic and every Enhanced quality tier.

## Phase 6 — replacement readiness

- Performance and soak testing across representative hardware.
- Crash reporting with privacy-safe local diagnostics.
- Signed/notarized cross-platform packages, migration documentation, and
  reproducible release artifacts.
- Long mixed-client public tests before considering the retail client optional.

## Work order inside every phase

1. Capture or document the retail/server behavior.
2. Add a failing golden, replay, or characterization test.
3. Implement the smallest complete behavior.
4. Validate owner, observer, late join, and transition paths.
5. Profile and remove unbounded or blocking work.
6. Update compatibility evidence before expanding scope.

## Deferred by design

The following do not block the first vertical slice:

- Enhanced graphics beyond the Classic baseline;
- all classes, modes, weapons, deployables, bots, and UGC authoring;
- new gameplay, protocol extensions, accounts, progression, or matchmaking;
- a general scripting API or third-party mod marketplace;
- replacing the authoritative Python 3 server;
- automatic conversion of every recovered Python 2 implementation.

The immediate priority is a small client that is demonstrably correct. Breadth
comes only after its connection, clock, world, and rendering boundaries survive
the executable parity gates.
