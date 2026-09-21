# Weapon appearance, sights and sound

The active catalogue and attribution are documented in
[COMMUNITY_COSMETICS.md](COMMUNITY_COSMETICS.md). This guide describes native
presentation behavior; a cosmetic does not change its gameplay parent's rules.

Sprint cancels aiming immediately and blocks new aim presses until sprint is
released. This applies to both sniper scopes and iron sights, including cosmetic
optics. Holding the aim button through sprint release does not re-enter toggle
aim; release and press it again. Reload completion also cannot restore aim while
sprint remains held. The client sends the same unzoomed state to the server.

## Authored variants and aiming

Inventory -> Collection -> weapon inspector shows only the sight/barrel
choices supported by the pack. `src/world/weapon_variants.cpp` validates option
IDs and stores local choices in executable-adjacent `skin-variants.json`.
Reset restores pack defaults. These choices do not change crate ownership or
remote equipment; remote peers use the published world appearance.

Zoom is automatic from the selected authored sight or parent weapon. Old
manual zoom overrides are ignored. Kar98/MP5K option manifests retain their
real sight/reticle/attachment choices; a renderer setting exposed by a script
is not automatically a player-selectable attachment. Previews rebuild after
a real option change without saving again during rendering.

`weapon-presentation.json` keys native sight tags, eye distance and audio cues
by cosmetic ID/model hash. The sibling creator tuning UI can export this file;
install it in the playable client's `assets/client/cosmetics/` and restart.
Draft browser changes alone do not alter the client. Missing cues fall back to
the gameplay parent. `AUDIO_MAPPING.json` records source/output hashes and
fallbacks; derive current resource counts from it rather than an import log.

## Scripted presentation

`src/world/scripted_weapon.cpp` supplies the bounded OpenSpades presentation
adapter: model/image/audio registration, math, configuration defaults and
weapon state. It does not implement the whole OpenSpades engine. Resource paths
remain within each pack, file/process/network APIs are absent, and initializer
and frame execution budgets/resource limits apply.

Aim, sprint, raise, recoil, reload, ammunition and shot events come from the
parent runtime. Authored hand targets drive selected character arms through
the articulated pose. Supported classic combined-arm layouts are adapted to
that frame; unknown layouts retain fallback. Native B&S weapons keep native
poses. Remote bodies/objects retain the B&S rig.

OpenSpades skin coordinates are left/forward/down; native camera conversion is
`(-x, -z, -y)`. Apply it consistently to geometry, arms and projected sprites.
This is a rotation, not a reason to reverse mesh winding. Changing equipment,
class or team invalidates the arm upload so the next frame resolves the correct
body/arm assets. Source model hashes and author attribution remain unchanged.

## Crosshairs, flashes and lighting

`ScriptedWeaponImages` registers confined pack images before animation and
uploads them before `begin_frame`; drawing does not perform file reads or GPU
uploads. The normal HUD crosshair is available when an authored hip crosshair
is missing. An authored visible optic takes precedence over the optional
settled iron-sight aiming dot; reload, sprint and death suppress that dot.

Sight overlays retain their screen/UI path. Images under `Gfx/Flash/` use
additive first-person billboards sharing the opaque weapon depth buffer; they
do not write depth. This prevents flashes painting through the receiver/hands.
Soft-particle support is disabled for the compatibility host. Visible flash
attachments provide bounded point lights under the current quality settings,
and authored flashes suppress the duplicate parent flash light.

Sprites preserve the recovered radius conventions. Resource aliases and
explicit reticle/model fallbacks belong in pack manifests, not silent runtime
substitutions. Some supplied content is incomplete or restricted by author
terms; retain those records rather than presenting every imported file as an
enabled or redistributable item.

## Rendering invariants

Equipped HUD/class icons resolve the same model/slot as gameplay. The Bren's
legacy equipment binding maps to Medic LMG presentation through
`cosmetic_slots.hpp`. Grounded multipart objects use the visible non-pitching
parts and supporting voxel surface for contact; barrel pitch must not move
the whole rig. Falling models retain their continuous intrinsic offset.

Stable sun shadows use `include/battlespades/render/shadow_projection.hpp`:
a camera-independent light basis, translation snapped in light space and a
map-bounded depth range. The shader matrix follows bgfx clip-depth/texture-origin
capabilities. Rebuild/stage all backend shaders with the matching executable
when changing this contract; see [RUNBOOK.md](RUNBOOK.md).

## Verification

Relevant tests cover weapon variants, script budgets/resources, equipped arms,
model bounds, icons, actual UI sight images, muzzle depth/light and shadow
projection/motion. `aos_scripted_weapon_images_tests`,
`aos_muzzle_flash_render_tests` and `aos_shadow_stability_render_tests` exercise
renderer paths; inspect their supported arguments before generating captures.
`tests/test_weapon_variants.cpp` covers option persistence/fallback.

Run configured targets through CTest and retain fresh captures in disposable
`out/evidence/` or `tmp/`. Old pack counts, `.before-*` binaries, timings and
session screenshots were retired during cleanup. Native mathematical/model
tests do not establish visual parity on every graphics backend; perform target
platform checks and paired gameplay captures for a release.
