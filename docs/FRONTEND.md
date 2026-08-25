# Frontend reconstruction

The frontend is an early engine vertical slice, not a substitute for the world
and protocol milestones. It exercises platform input, aspect handling, asset
lookup, text shaping, audio cues, scene transitions, and rendering without
mixing those responsibilities into gameplay.

## Compatibility baseline

The retail frontend renders against an 800 by 600 design canvas with its origin
at the bottom left. The canvas is uniformly scaled into the window and
letterboxed when necessary. Pointer coordinates must be transformed back into
design space before hit testing.

The native UI normalizes that evidence into a top-left coordinate system at
screen construction. It stores Classic layout in one-eighth-pixel integer
units, which exactly represents the retail Select Menu's fractional square
button coordinates while keeping hit tests deterministic.

Classic mode preserves:

- retail widget bounds and z-order;
- normal, hovered, pressed, and disabled states;
- the original PNG slicing and filtering rules;
- the original fonts, text fitting, colors, and uppercase rules;
- forward/back screen transitions and transition-time input gating;
- menu confirmation, back, scroll, and purchase audio cues.

Enhanced mode may change presentation and accessibility, but it consumes the
same screen model and action routing. It must not silently move Classic hit
targets or change what an action does.

The recovered Select Menu is pointer-first and has no evidenced keyboard or
controller focus state. Native focus navigation is retained as an
accessibility enhancement; it must not be presented as recovered retail
behavior or alter pointer behavior in Classic mode.

## Ownership

```text
platform input
    |
    v
design-canvas transform --> UI input actions
                                |
                                v
                         widget/screen model
                          |             |
                          v             v
                    frontend action   draw list
                          |             |
                          v             v
                 app/session service  UI renderer
```

- `ui` owns geometry, interaction state, focus, navigation, screen routing, and
  renderer-neutral draw data.
- `frontend` owns concrete screens and typed user intentions such as
  `join_match`, `open_settings`, or `quit`.
- `app` decides how those intentions affect a session or process.
- `render` owns textures, glyph atlases, clipping, batching, and GPU handles.
- `assets` resolves stable asset IDs. Screens never open files directly.
- `audio` maps typed UI cues to sound playback.

Widgets do not connect to a server, start a local host, call a storefront, or
mutate configuration files. A screen emits a typed action and the owning
service performs it.

## Reconstruction sequence

The initial slice is deliberately narrow:

1. Characterize the retail frontend shell, button state machine, and Select
   Menu from recovered Python and captured assets.
2. Implement deterministic design-canvas transforms, pointer interaction,
   focus navigation, and screen routing as renderer-independent C++.
3. Encode the Select Menu layout and its typed actions.
4. Produce a Classic draw list using the original main-menu background, frame,
   sliced buttons, icons, splash, and fonts.
5. Render that list through the SDL3/bgfx backend and play the original OpenAL
   menu cues.

The static Select Menu/window portion is accepted when deterministic native
captures clear the documented 800x600 and 1680x1050 RGB SSIM thresholds, real
window interaction survives resize/minimize/focus transitions, transition-time
clicks cannot activate a screen, and all referenced assets resolve. Destination
screens and their actions require separate acceptance gates.

Settings was the first completed destination slice. Join Match, Server Browser,
Public/Quick Play, Custom Match, Create Match, UGC Select and Publish Map,
Leaderboard, Player Profile, and loading surfaces now have renderer-independent
models, native presentations, route integration, and focused tests. External
service adapters remain separate milestones: a screen can be complete as
frontend code while its networking, HTTP, hosting, gameplay, editor, or
publishing action is still unavailable.

## Current implementation

The first native presentation path is live:

- design-canvas and inverse pointer transforms;
- stable widgets, state, focus navigation, and bounded screen stacks;
- forward/back frontend transition state and transition-time input gating;
- a shared bounded route stack used by the native executable: forward children
  begin one screen-width to the right, Back begins one screen-width to the
  left, both retained layers animate over one stationary background, and input
  remains blocked until the recovered threshold;
- exact Select Menu geometry and typed actions;
- explicit retail drag/release behavior, including the Navigation Bar's
  different capture rule;
- a 25-file English Select Menu asset manifest with scale, anchor, and
  filtering metadata;
- renderer-neutral sprite, three-slice, name-plate, and shaped-text draw data;
- an SDL3 window/event adapter and bgfx UI renderer;
- cached PNG textures plus bounded runtime RGBA glyph textures;
- FreeType/HarfBuzz shaping and fitting with the preserved retail fonts;
- optional OpenAL Soft menu music and confirmation cues;
- the recovered three-tab Settings presentation and interaction model;
- normalized, strictly validated, atomically replaced `settings.toml` state;
- duplicate/reserved-key-safe raw control rebinding;
- transactional Defaults/Cancel/Done behavior, live runtime previews, and a
  dedicated 15-second resolution confirmation route;
- the recovered Join Match menu and hardened Server Browser presentation,
  including generation-checked source/region refreshes, full/empty filters,
  stable sorting and indicators, model-owned scrolling, OS-counted
  double-click activation, and immutable typed connect requests;
- a distinct Public/Quick Play branch with all 11 recovered playlist IDs,
  content ownership, generation-checked discovery, retail server-selection
  priority, and typed direct-server/playlist/store/back handoffs;
- a distinct Custom Match branch with Open/Friends filtering, bounded and
  deduplicated lobby discovery, owner/member root state, selection-safe Join,
  and typed refresh/join/create/back handoffs;
- the Create Match lobby with ten retail game modes, mode-compatible grouped
  maps, all 98 reachable rule IDs, class/content gating, transactional defaults,
  lobby/player state, and direction-aware nested mode/map/rule slides;
- UGC Select with Create Map, Publish Map, Subscribe, invalid-data gating, and
  typed route/external-link activations, plus the complete Publish Map
  list/name/confirm/delete/dialog presentation and safe empty repository state;
- Leaderboard and Player Profile presentations with request-generation guards,
  cached leaderboard selections, tabs/filters/sorting/scrolling, and explicit
  unavailable/not-found outcomes when no endpoint adapter exists;
- distinct boot and match-loading state/presentations, including truthful
  weighted preload progress, recovered 36-bullet boot art, match status/tabs,
  timeout handling, official/fallback map art, and Start gating;
- a bounded native texture-preload adapter that decodes PNGs on worker threads,
  limits manifest and decoded-backlog sizes, preserves manifest upload order,
  and creates GPU resources only on the render thread; presentation text and
  glyph resources are then warmed one destination per render frame;
- executable-adjacent asset/shader discovery with source-tree fallbacks;
- headless characterization tests, native text/resource tests, and a graphical
  navigation smoke for settled and mid-transition destination screens.

The executable opens the Select Menu and handles pointer and semantic focus
input across the integrated destinations. Quit is connected to process
shutdown. Settings persists real local configuration. Join, Quick Play, Custom
Match, Create, UGC, Leaderboard, Profile, and loading routes render their native
destinations and produce typed intentions, but those intentions do not pretend
that an external system exists: live server/lobby discovery and Protocol 168
connection, score/profile HTTP, lobby creation/hosting, gameplay scene startup,
Tutorial gameplay, and UGC editing/publishing are not implemented. Offline and
missing-adapter states disable side effects and remain visibly honest. The
complete Settings contract and option inventory are in [SETTINGS.md](SETTINGS.md).

## Implemented route tree

```text
Select Menu
|-- Join Match
|   |-- Server Browser
|   |-- Custom Match Lobbies
|   `-- Public/Random Match (11 playlists)
|-- Create Match
|   |-- Choose Game Mode (10 recovered modes)
|   |-- Maps
|   `-- Game Rules
|-- UGC Select
|   |-- Create Map        [typed action; editor service pending]
|   |-- Publish Map       [complete UI; repository/upload services pending]
|   `-- Subscribe         [typed external-link action]
|-- Leaderboard           [safe unavailable state without score adapter]
|-- Player Profile        [safe not-found state without profile adapter]
`-- Settings
    |-- Main
    |-- Graphics
    |-- Controls
    `-- Resolution confirmation
```

Boot loading precedes this tree. Match loading is a separate session-boundary
surface and does not reuse boot progress. Route identity and rendering are kept
separate: the route stack owns current/previous screens and transition offsets,
presentations emit immutable draw lists, and the compositor applies translated
layers while leaving the shared background stationary.

The Windows desktop capture helper now launches the exact process, requests an
exact client extent, captures only that process's SDL client area, and closes
the window afterward. The comparison helper applies a full-frame RGB SSIM
threshold and can retain an absolute-difference image. The accepted idle
captures score `0.985400` at 800x600 (minimum `0.98`) and `0.967606` at
1680x1050 (minimum `0.96`); hashes, commands, and provenance are recorded in
[research/SELECT_MENU_VISUAL_BASELINE.md](research/SELECT_MENU_VISUAL_BASELINE.md).

The base real-window smoke passes at 1000x700 and verifies the 320x240 minimum,
exact sizing, minimize/restore, cancellation of held pointer capture on focus
loss and mouse leave, and a graceful exit through Quit. This accepts the
static idle Select Menu and native window/input lifecycle. Settings adds
headless transaction, persistence, presentation, routing, rebinding, and
resolution-timeout coverage.

`tools/smoke-client-navigation.ps1` drives the real window through Main, Join
Match, Server Browser, Direct Connect, Quick Play, Create Match, its nested mode
picker, UGC Select and Publish Map, Leaderboard, Player Profile, and match
loading. It captures settled and mid-slide frames to
`out/evidence/frontend-navigation`. Headless transition tests verify route
direction and transition-time input gating; the desktop smoke verifies the
composed result survives the complete real input/render path. This is a
presentation/navigation gate only; Protocol 168, master-server discovery,
HTTP, hosting, gameplay, and world work remain independent milestones rather
than implicit menu behavior.

## Evidence discipline

Recovered Python establishes behavior, not architecture. Retail coordinates,
asset names, timings, and state transitions belong in the compatibility
evidence and characterization tests. Python class shapes, global lookups, and
OpenGL immediate-mode calls are not copied into the native design.

Any intentional deviation must be named, profile-gated where appropriate, and
recorded before the corresponding golden test is changed.
