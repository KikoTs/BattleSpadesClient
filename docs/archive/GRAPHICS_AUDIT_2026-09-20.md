# Graphics and Connect to IP audit — 2026-09-20

This pass restores specific recovered Legacy rendering behavior, preserves the
enhanced shadow repair, and fixes Connect to IP text input. It does not establish
pixel-for-pixel parity with the entire original renderer.

Build status at handoff: all checks below passed. The initial colon/paste fix
was installed earlier in the session, but the complete graphics build and the
Loading-to-Connect input restoration await installation because
`dist/bin/aos.exe` is running. No running process was terminated. See
`build-ready.json` in the evidence directory for executable hashes; the existing
staging receipt currently describes the earlier input-only installation.

## Changes and source evidence

A reference screenshot was supplied for Legacy (not retained in the repository).
Recovered sources and IDA databases are under
`<retail-source>/aoslib`.

| Defect | Correction | Original evidence |
|---|---|---|
| VXL baked light incorrectly used as the directional-light bypass, washing out faces | Keep bypass zero for ordinary terrain; average stored light over the corner's occupied neighbors, multiply and truncate RGB before interpolation | `vxl.pyd` `sub_10030B60` and `sub_100051C0` |
| Grain was moved to a separate mipmapped texture | Sample the blue channel of the same linear-filtered, clamped, unmipped AO atlas | `vxl.pyd` `sub_10014A30`; `shader_source/map_vert.py` and `map_frag.py` |
| KV6 normal byte discarded; Legacy models used cube-face shading | Preserve voxel byte 7, reconstruct the 256-entry normal table, and use recovered wrapped diffuse/specular lighting | `kv6.pyd` `sub_10001350`, `sub_100013F0`, `sub_1001AA50`; `shader_source/model_frag.py` |
| Reduced-detail normals did not follow the original reconstruction | Rebuilt reduced-detail voxels use index 1 | `kv6.pyd` `sub_100015D0` |
| Model lighting ignored the original specular light colors | Use each light's color for diffuse and specular, plus ambient RGB × intensity | `gameScene.pyd` GameScene.draw: GL light calls at `0x1013A9D1` through `0x1013B51A` |
| Legacy could inherit enhanced sky-derived fog and artistic prefab tint | Preserve StateData fog separately; apply model tint only in enhanced tiers; reset retail lighting when entering offline maps | Native environment handoff plus recovered map/model shader paths |
| Legacy fog evaluated at fragments instead of vertices | Restore per-vertex radial distance including homogeneous w, then linear fog from half to full draw distance | `shader_source/map_vert.py` and `model_vert.py` |
| A zero baked-light byte recolored ordinary blocks as water | Restrict the water fallback to all-zero voxels in the synthetic bottom layer | VXL fixture with a zero-light authored block |
| HDR/SSAO/bloom diagnostics advertised passes that do not exist | Keep reserved profile fields zero; document actual capabilities | Production renderer submission and shader audit |
| Texture/model restart behavior was absent from row descriptions | Display the existing restart-required description on both rows | Existing restart handling in settings commit |

The shared vertex is now 44 bytes. Terrain stores its baked multiplier in
TexCoord1; tagged KV6 models use otherwise unused terrain UV fields for normals.
Enhanced tiers continue to use face normals and their existing AO/shadow path.
Viewmodel lights are transformed into the held model's rendering space as the
camera turns. Executable and world shader binaries must be staged together.

## Settings coverage

| Setting | Verification |
|---|---|
| Resolution/fullscreen | Existing presentation and menu tests cover selection, confirmation, transaction and rollback; settings persistence tests pass. Physical monitor mode changes were not automated in this pass. |
| Graphics API | Real GPU runs on Direct3D 11, Direct3D 12, Vulkan and OpenGL; requested backend checked against the active backend. Metal/ESSL shaders compile but were not run on a device. |
| Antialiasing | Off, 2× and 4× offscreen render/resolve on all four Windows APIs. |
| Effect Quality | All three effect profiles exercised at all five shader tiers; particle-system tests pass. |
| Draw Distance | All three stored values (90/128/192), bounds and menu transactions checked by settings tests; Legacy fog-color authority and a fully fogged pixel checked on GPU. |
| Shader Quality / Compatibility | Legacy, Low, Medium, High and Ultra cycled without remeshing; returning to Legacy reproduces the same frame. |
| Texture Quality | All three authored resource roots decoded and rendered in the API/AA matrix. |
| Model Quality | Full/reduced KV6 geometry, dimensions, pivot, team material, minimum-detail rules and full-detail sights checked. |
| VSync | On/off renderer reset succeeds and leaves the Legacy image unchanged; refresh timing and tearing are not measured. |
| Save/Cancel/Defaults | Existing settings transaction, persistence and menu tests pass; graphics remain disabled in matches. |

All 24 selected CTest suites pass, including settings, texture/model quality,
KV6 normals, terrain meshing, vertex layout, atmosphere, particles, SDL input,
join menu, UI rendering, muzzle flashes, sight alignment, placement ghosts and
shadow stability. The sight probe now supplies explicit neutral lighting to
its isolated shader setup; visibility and centering assertions are unchanged.

The GPU matrix comprises **36 runs**: four APIs × three AA levels × three texture
levels. Each run checks:

- 36 unoccluded face/light combinations, including grazing Egypt sunlight:
  zero pixels exceeded the self-shadow error threshold.
- 48 terrain/model pixels against CPU equations transcribed from the original
  shaders, covering six normals and four baked-light inputs.
- 12 camera yaw/pitch combinations through the production viewmodel pass,
  compared against lighting predicted independently in world coordinates.
- 15 shader/effect combinations, restoration of Legacy, VSync reset and
  server-fog authority.

Actual AncientEgypt terrain was rendered and visually inspected at all five
tiers. Those 512×512 captures use an offline camera and default retail lights;
they are not a matched-camera recreation of the supplied 1366×767 screenshot.

## Connect to IP

The endpoint previously interpreted raw keycodes as characters, which lost
shifted punctuation such as `:`. It now consumes SDL committed text. Ctrl+V on
Windows/Linux (Command+V on macOS) reads the clipboard only when the field has
focus. Paste trims outside whitespace and validates the complete endpoint
before appending; invalid/oversized input cannot insert a partial address.
Backspace repeat is retained. Leaving the route stops text input, and returning
from Loading restores it.

Tests cover committed colon input, `127.0.0.1:27015`, surrounding whitespace,
invalid multiline text, the 255-byte limit, focus gating and SDL clipboard
reads using the isolated dummy video driver. Desktop clipboard contents were
not changed by these tests. Physical key/paste interaction in a running native
client remains unverified: the desktop automation helper failed to launch its
window. The user's existing retail process was left alone.

## Reproduction and remaining limits

Evidence directory:
`../BattleSpades/tmp/graphics-legacy-audit-20260920`.
It includes original decompilation/disassembly evidence, build logs,
`graphics-and-input-ctest-final.log`, `matrix.json`, per-run logs, Egypt and sight
captures, and the hash-verified staging receipt with backup locations.

Run the matrix with `run-graphics-matrix.ps1` in that directory. A single probe:

```powershell
.\out\build\native-dev\tests\RelWithDebInfo\aos_block_shading_render_tests.exe `
  '<output-directory>' '-' src/render/shaders/bin direct3d11 4 2
```

Replace `'-'` with the AncientEgypt VXL path to also capture the actual map.
Shaders were compiled for DX11, GLSL, ESSL, SPIR-V and Metal.

This is scoped parity evidence, not a whole-renderer guarantee: original water
rendering, exact placed-flare baking, every original asset/state combination,
nonuniform model transforms, and matched retail camera/server lighting have not
been established by these checks. HDR framebuffers, SSAO and bloom remain
unimplemented rather than silently advertised as active.
