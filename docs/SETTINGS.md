# Settings

The native client implements the recovered three-tab Settings screen as a
transaction. Opening the screen creates a draft, `Defaults` resets only the
active tab, `Cancel` restores the last confirmed runtime state, and `Done`
validates and atomically saves the complete draft.

The maintained schema is in `include/battlespades/settings/client_settings.hpp`
and `src/settings/settings_store.cpp`; tier capabilities are defined by
`src/render/quality_profile.cpp` and consumed by the renderer. For executable
staging and shader updates use [RUNBOOK.md](RUNBOOK.md).

## Audio output

The executable-adjacent `settings.toml` accepts `audio_device` in `[main]`.
An empty string (the default) tries the Windows default output and then other
available outputs if initialization fails. A nonempty value is an exact OpenAL
device name, for example `"OpenAL Soft on Speakers (Echo Show 8-5WJ)"`.
An explicitly selected device never falls back to another speaker. Changes
take effect on the next launch; this option is currently file-only.

If an output rejects playback, the client remains usable and reports the
device failure in the settings warning and console. Windows error `0x88890008`
means unsupported audio format. When an endpoint rejects its own Windows mix
format, reconnect that output before restarting the client; changing game
volume or rendering settings cannot repair the endpoint.

## Option coverage

The Main tab contains every recovered retail row plus the external language selector:

| UI option | `settings.toml` key | Values and retail default | Runtime behavior |
|---|---|---|---|
| Language | `main.language` | locale found under `localization/`; `"en"` | live preview and persisted by `Done` |
| Master Volume | `main.master_volume` | `0.0`-`1.0`; `1.0` | live preview |
| Music Volume | `main.music_volume` | `0.0`-`1.0`; `1.0` | live preview |
| Fullscreen | `main.fullscreen` | boolean; `true` | live preview |
| Invert Mouse | `main.invert_mouse` | boolean; `false` | applied by `Done` |
| Favourite | not persisted here | boolean | disabled in the frontend; a live server session owns it |
| Show skins | `main.show_skins` | boolean; `true` | live local filter for character, weapon, world-object and death skins, including pack sounds |
| Show other players' skins | `main.show_other_skins` | boolean; `true` | live remote-only filter; own equipped appearance stays visible when Show skins is on |
| Weapon movement | `main.weapon_motion` | boolean; `true` | live toggle for running sway, falling lift and scripted sprint poses; firing, reloads and ADS remain animated |

The three presentation preferences are available during matches. Turning skins
off does not modify equipment, crate awards, inventory previews or what other
players see. Turning them back on reuses the saved equipment without fetching
inventory again. `Cancel` restores the previous visual preferences. Remote rigs
and props replace their meshes incrementally using the existing upload budget.

The Graphics tab contains every row supported by the active backend. Resolution
choices come from SDL's filtered, de-duplicated display-mode list. Antialiasing
and shader rows are omitted when the backend reports that capability as
unavailable.

| UI option | `settings.toml` key | Values and retail default | Runtime behavior |
|---|---|---|---|
| Resolution | `graphics.resolution` | `"WIDTHxHEIGHT"`; `"800x600"` | temporary mode after `Done`, then confirmation |
| Graphics API | `graphics.graphics_api` | `"auto"`, `"direct3d11"`, `"direct3d12"`, `"vulkan"`, `"opengl"`, `"metal"`; `"auto"` | applied after restart |
| Antialiasing | `graphics.antialiasing` | `"off"`, `"2x"`, `"4x"`; `"off"` | applied by `Done` through bgfx reset |
| Effect Quality | `graphics.effect_quality` | `"low"`, `"medium"`, `"high"`; `"medium"` | applied by `Done` |
| Draw Distance | `graphics.draw_distance` | `"low"`, `"medium"`, `"high"`; `"high"` | applied by `Done` |
| Shader Quality | `graphics.shader_quality` | `"low"`, `"medium"`, `"high"`, `"ultra"`; `"medium"` | applied on the next frame; no restart |
| Texture Quality | `graphics.texture_quality` | `"low"`, `"medium"`, `"high"`; `"medium"` | restart-sensitive |
| Graphics/Model Quality | `graphics.model_quality` | `"low"`, `"medium"`, `"high"`; `"high"` | restart-sensitive |
| VSync | `graphics.vsync` | boolean; `false` | live preview |
| Compatibility Shader | `graphics.shader_quality` | toggles `"compatibility"` | applied on the next frame |

Two native display options have no retail row and are edited in `settings.toml`
only (the Graphics tab's Defaults button leaves them alone):

| `settings.toml` key | Values and default | Runtime behavior |
|---|---|---|
| `graphics.fullscreen_mode` | `"borderless"` or `"exclusive"`; `"borderless"` | how `main.fullscreen` covers the display. Borderless keeps the desktop mode (instant alt-tab, never minimised by SDL) and renders at the desktop size; exclusive is the retail display-mode switch at `graphics.resolution` |
| `graphics.render_interpolation` | boolean; `true` | on displays above 75 Hz, extra render-only frames between the fixed 60 Hz ticks with the camera eye interpolated between the last two ticks (one tick of positional presentation delay; look angles stay current). Simulation, input and ClientData cadence are unchanged. `false` restores retail's one frame per tick |
| `graphics.hud_scale` | number; `1.0` | in-game HUD, chat and vote ballot magnification for 4K/Retina. `1.0` is retail's raw window-pixel HUD; `1.0`-`4.0` lays the retail HUD out in a window that many times smaller and magnifies it back, so proportions and anchors are unchanged; `0` = auto, `max(1, floor(height / 1080))`. Menus already scale with the 800x600 canvas and are not affected |

Minimising or alt-tabbing out never pauses a match: simulation, ClientData,
ClockSync and the connection watchdog keep running and only rendering stops.
With VSync on, bgfx queues at most one frame (`maxFrameLatency = 1`).

`Shader Quality` and `Compatibility Shader` are two recovered retail rows that
edit the same field, and they divide it cleanly: the toggle owns the
`"compatibility"` value (the Legacy tier) and the slider owns the four enhanced
tiers. While the toggle is on, the Shader Quality row is greyed out and reads
`LEGACY`, so the two rows can never disagree. Switching the toggle off restores
whichever tier was selected before it was turned on rather than snapping to
`"high"`.

Graphics rows are disabled when Settings is opened from an active match,
matching the recovered retail behavior.

### Graphics API selection

The Graphics API row only lists backends compiled into the running executable.
Current desktop targets are:

- Windows: Auto, Direct3D 11, Direct3D 12, Vulkan, and OpenGL when the installed
  driver exposes them.
- Linux: Auto, Vulkan, and OpenGL according to the bgfx build and display
  driver.
- macOS: Auto and Metal, plus any additional backend actually reported by the
  bgfx build.

`auto` is the recommended default. bgfx currently prefers Direct3D 12 on
Windows, Vulkan on Linux, and Metal on macOS. Changing the backend is
restart-only because bgfx owns GPU resources for the lifetime of the process.
If a portable `settings.toml` requests an API unavailable on the current
platform, the client starts with Auto and exposes a warning instead of trying
to initialize a platform-incompatible backend. If a selected driver fails
during device creation, edit the executable-adjacent file back to:

```toml
[graphics]
graphics_api = "auto"
```

The Ctrl+Shift+F3 gameplay diagnostics show the concrete API selected by bgfx. Developers
can list the backends compiled into a build with
`aos_graphics_backend_probe`.

Developer builds load shaders directly from `src/render/shaders/bin`, while a
portable client loads them from its own `bin/shaders` tree. After compiling,
stage both the executable and the checked-in backend binaries together; copying
only the EXE can leave an older renderer active:

```powershell
.\tools\stage-native-client.ps1
```

The staging script verifies every shader hash after copying it.

### Renderer quality tiers

`graphics.shader_quality` resolves to a `render::QualityProfile`
(`src/render/quality_profile.cpp`), the single source of truth for what each
tier enables. **Legacy uses the recovered retail lighting path** and is reached through the
Compatibility Shader toggle, not the slider.

| | Legacy | Low | Medium | High | Ultra |
|---|---|---|---|---|---|
| Lighting | recovered VXL/KV6 | enhanced | enhanced | enhanced | enhanced |
| Inline tone mapping | — | yes | yes | yes | yes |
| Offscreen HDR target | — | — | — | — | — |
| Sun shadow map | — | — | 1 @ 1536 | 1 @ 1536 | 1 @ 2048 |
| Shadow filtering | — | — | 9-tap PCF | 9-tap PCF | 9-tap PCF |
| Screen-space ambient occlusion | — | — | — | — | — |
| Dynamic lights | 0 | 2 | 4 | 8 | 8 |
| Lit particles | — | — | — | yes | yes |
| Bloom | — | — | — | — | — |

Legacy renders straight to the backbuffer with the recovered two-light terrain
shader, per-vertex VXL baked illumination, the original AO/edge/grain atlas,
authored KV6 normals and model lighting. It preserves server-supplied fog.
Enhanced tiers use the measured map atmosphere, face normals and their existing
shadow/occlusion path. All tiers read the same meshes; changing tier costs no
re-mesh. See [the graphics audit](archive/GRAPHICS_AUDIT_2026-09-20.md) for evidence and
remaining parity limits.

`Effect Quality` is a deliberately independent axis, sizing the reusable
particle pool (0.10 / 0.50 / 1.00) at any tier without thinning individual
authored bursts, because dense particles are affordable
on machines that cannot pay for shadows.

**Currently live:** per-pixel tier lighting, emissive map voxels, effect scale,
fog distance, one filtered sun-shadow map on Medium and above, the skylight
cover term, the bounded dynamic-light array used by explosions and selected
muzzle flashes, and High/Ultra per-particle light evaluation. Additive glow
sprites remain self-lit; ordinary smoke and debris receive a bounded tint from
the same selected world lights. **Not implemented:** an offscreen HDR target,
SSAO and bloom. Their reserved profile fields remain zero, including in the F3
diagnostics. Enhanced tone mapping happens inside the world fragment shader;
it does not use an HDR framebuffer. Terrain atlas AO and model corner AO are
separate from the unimplemented SSAO pass.

Sun shadows use a light-space texel grid and a map-bounded depth range so
walking and jumping do not move the sampling pattern over stationary surfaces.
The nine-tap filter follows the receiver plane with a small depth bias, keeping
contact close to the caster without darkening the ambient sky light. The outer
shadow-map border fades to light. Terrain submissions are culled against the
light volume, including its filter margin; camera visibility does not remove
off-screen shadow casters. This is filtered shadow mapping, not PCSS or ray tracing.

## Controls and rebinding

`Mouse Sensitivity` is stored as `controls.mouse_sensitivity` in the range
`0.0`-`1.0` and defaults to `0.1`. The two collapsible categories contain the
following complete binding set:

| Main Game row | `controls.bindings` key | Retail default |
|---|---|---|
| Forward / Backward / Left / Right | `forward`, `backward`, `left`, `right` | W / S / A / D |
| Sneak / Crouch / Sprint / Jump | `sneak`, `crouch`, `sprint`, `jump` | V / Left Ctrl / Left Shift / Space |
| Aim / Reload | `aim`, `reload` | Right Mouse / R |
| Team Chat / Global Chat | `team_chat`, `global_chat` | Y / T |
| View Map / View Scores | `show_map`, `view_scores` | M / Tab |
| Change Team / Change Class | `change_team`, `change_class` | Period / Comma |
| In-game Menu / Pick Colour | `menu`, `weapon_custom` | Escape / E |
| Map Vote 1 / 2 / 3 | `map_vote_1`, `map_vote_2`, `map_vote_3` | F1 / F2 / F3 |
| Kick Player / Toggle HUD | `kick_player`, `toggle_hud` | K / Unbound |

`Fire/Use` (left mouse), `Cycle Next Weapon` (mouse wheel), and Inventory Slots
(1-9) remain visible fixed helper rows and cannot be rebound.

| UGC row | `controls.bindings` key | Retail default |
|---|---|---|
| UGC Settings / Tool Help | `ugc_settings`, `tool_help` | X / H |
| Palette Left / Right / Up / Down | `palette_left`, `palette_right`, `palette_up`, `palette_down` | arrow keys |
| Cancel Prefab / Carve Prefab | `cancel_prefab_placement`, `carve_prefab` | Q / C |
| Jetpack Hover / Quick Save | `hover`, `quick_save` | Z / F10 |

Select a configurable row and press the desired physical input. Keyboard
bindings use layout-independent SDL/USB scancodes; persisted mouse values use
SDL's 1-based button numbering. One input cannot belong to two actions, and
number keys 0-9 are reserved for inventory, so either case is rejected without
changing the draft. Fixed helper rows never enter capture mode.

TOML binding values are `unbound`, `keyboard:<name>`, or `mouse:<button>`, for
example `keyboard:left_shift`, `keyboard:scancode-40`, or `mouse:right`.
Names are case-insensitive while parsing. Manual edits are validated by the
same duplicate, reserved-key, and range checks as the UI.

## Display safety and transaction rules

- Master volume, music volume, fullscreen, and VSync preview immediately.
- Resolution is staged until `Done`. The proposed mode then has a 15-second
  `Keep`/`Revert` screen. Timeout or `Revert` restores the previously confirmed
  mode and does not write the proposed resolution.
- Graphics API, texture, and model-quality changes are saved by `Done` and
  produce a restart-required notice. Antialiasing resets bgfx presentation and
  shader quality is consumed on the next frame, so neither shows a false
  restart warning.
- Confirmed mouse sensitivity and invert-mouse changes update the already-live
  world session; they no longer wait for the next map.
- `Cancel` discards every draft edit and rolls back any live previews.
- `Defaults` restores retail defaults for the active tab only; it remains a
  draft until `Done`.
- A save failure leaves the previous file and confirmed runtime state intact.

The retail defaults say fullscreen `800x600`. On a first launch with no settings
file, the native client intentionally starts in a safe window and makes the
draft reflect that real window. Once the user confirms a display mode, later
launches apply the saved fullscreen/resolution pair before the renderer starts.

## Persistence and portable packages

The settings path is always executable-adjacent:

```text
<BattleSpadesClient.exe directory>/settings.toml
```

For a developer build this is normally
`out/build/native-dev/src/RelWithDebInfo/settings.toml`; in the portable ZIP it
is `bin/settings.toml`. Extract the ZIP to a user-writable directory so the
client can replace this file atomically. The release intentionally does not
ship a pre-generated `settings.toml`: compiled defaults provide first-run state
and each extracted copy owns its configuration.

The file uses schema version 1 and the sections `[main]`, `[graphics]`,
`[controls]`, and `[controls.bindings]`. Recognized malformed or duplicate
values fail the complete load rather than installing partial settings. Unknown
keys and sections are ignored for forward compatibility. Files are bounded to
1 MiB and 4096 lines, normalized before save, written to a temporary sibling,
and atomically replaced.

## Build and test commands

From a PowerShell prompt at the repository root:

```powershell
$env:VCPKG_ROOT = "C:\src\vcpkg"
.\scripts\build.ps1 -Profile Dev -Native
& .\out\build\native-dev\src\RelWithDebInfo\BattleSpadesClient.exe
```

The build wrapper runs the complete native suite. To rerun only the Settings
contracts after configuration:

```powershell
ctest --preset native-dev -R "aos_(settings|settings_menu|settings_presentation|resolution_confirmation|frontend_controller)_tests"
```

The interactive Windows smoke stages a private runtime, drives the real SDL
window through all three tabs, exercises scrolling and mouse-button rebinding,
persists a draft, reopens it, and retains PNG evidence without touching the
developer's own configuration:

```powershell
.\tools\smoke-client-settings.ps1 `
    -Executable .\out\build\native-dev\src\RelWithDebInfo\BattleSpadesClient.exe `
    -EvidenceDirectory .\out\evidence\native-settings `
    -ExerciseResolutionRollback
```

Build, test, install, and package a release with:

```powershell
.\scripts\build.ps1 -Profile Release -Native
cmake --build .\out\build\native-release --config Release --target package
```

The portable archive and its SHA-256 checksum are written under
`out/build/native-release`.
