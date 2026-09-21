# AncientEgypt block shading, 20 September 2026

The reported fine speckling was reproduced in the installed client and in a
repeatable render of AncientEgypt's eastern cliff. The active settings were
Ultra, Direct3D 11, high textures, and 4x MSAA. The earlier Legacy noise-mipmap
change did not address this enhanced-lighting defect.

## Cause and correction

Egypt's resolved sun direction is approximately `(-0.866025, 0.000000032, -0.5)`.
It is effectively parallel to the map's Y-facing walls. The shader estimated
receiver-plane depth slopes using screen-space derivatives and capped them at
`+/-4`. At grazing angles this calculation either lost precision or supplied
too little slope compensation. A wall could consequently shadow itself.

The shader also multiplied its 0.28 diffuse fill floor by that unstable shadow
result. Even a wall receiving effectively zero direct sunlight therefore showed
strong speckles. A production-renderer capture with only cast shadows disabled
removed the speckling and isolated the faulty term.

`src/render/shaders/fs_world.sc` now:

- Derives shadow receiver slopes from the geometric face normal and the actual
  shadow projection, independently of pixel derivatives and mesh tessellation.
- Separates the diffuse fill floor from the direct sun contribution, and skips
  degenerate shadow comparisons for effectively parallel or back-facing faces.
- Weights sun specular by the incident light angle, so parallel/back-facing
  faces cannot amplify unstable sun visibility through a specular highlight.

Cast shadows, contact shading, voxel AO, texture quality, and the user's chosen
graphics settings remain enabled. The tiny enhanced-path banding dither was not
the cause and was not changed. This update changes no server or simulation code.

## Verification

- New `aos_block_shading_render_tests`: all six face normals, six light
  directions including Egypt's actual vector and either side of parallel, real
  voxel-sized triangles, Ultra, and 4x MSAA. The original shader fails. The
  corrected shader produces zero self-shadow outlier pixels in all 36 cases on
  Direct3D 11, Direct3D 12, Vulkan, and OpenGL.
- Existing shadow stability test: 192 walking/jumping camera frames per backend,
  Medium/Ultra and short/long draw distance. Passed on the same four backends,
  including checks that shadows remain present, correctly positioned, and
  attached at ground contact.
- Seven targeted CTest targets passed: block shading, shadow stability, shadow
  projection, quality profiles, vertex layout, ADS sight rendering, and placement
  rendering.
- Built all five shader formats, including ESSL and Metal. ESSL and Metal were
  compiled but not executed on this Windows machine.
- Repeated the map and wall tests against `dist/bin/shaders`. The installed
  AncientEgypt capture is byte-identical to the verified Direct3D 11 capture.

The GPU readback harness explicitly switches framebuffer before reading MSAA
results; GL/Vulkan require that transition to resolve the multisampled image.
This is test-harness behavior, not a change to production presentation.

## Installed files and evidence

Only `dist/bin/shaders/{dx11,glsl,essl,spirv,metal}/fs_world.bin` was replaced.
Each replacement was atomic, backed up, and SHA-256 verified. The active game
retains its already loaded shader; restart the client to load this update.

Evidence directory:
`G:/AoSRevival/BattleSpades/tmp/block-shading-20260920/`

- `comparison.html`: identical-camera before/after cliff renders, with pixel zoom.
- `regression-before.log`: original-shader failure.
- `direct3d11.log`, `direct3d12.log`, `vulkan.log`, `opengl.log`: corrected cases.
- `shadow-stability.log`, `shadow-direct3d12.log`, `shadow-vulkan.log`,
  `shadow-opengl.log`: cast-shadow placement/contact/movement checks.
- `ctest.log`, `installed.log`, `shaders.log`: targeted tests, installed rendering,
  and shader compilation.
- `shader-staging-receipt.json`, `shader-backups/`: installed hashes and originals.

These checks establish the reproduced graphics correction. They do not establish
whole-client or whole-server retail parity.
