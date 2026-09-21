# VIP Client Compatibility

> **Recovery/specification reference.** Preserve the measured retail behavior and its evidence. Implementation updates, old build paths, test counts and session constraints below describe their original investigation; they are not current release or deployment status. Use the [maintained documentation index](README.md) for present operating instructions and recheck historical findings against current source.

## Recovered retail contract

VIP and Territory Control use the `mafia` UI skin selected by
`InitialInfo(114).texture_skin`. The value is server-owned; browser metadata is
only an early loading-screen prediction.

Retail applies the skin globally during loading and then switches these shared
images through an override-first, default-fallback resource path:

- the large loading/class frame;
- Choose Team;
- View Scores;
- Change Team;
- Pause;
- in-game Settings.

Primary evidence:

- `aceofspades_source/aoslib/scenes/ingame_menus/loadingMenu.py` calls
  `set_loading_image(..., packet.texture_skin)` from `InitialInfo`;
- `aceofspades_source/aoslib/image.py::set_texture_skin` inserts
  `skins/<skin>/png/ui` at the front of the resource search path;
- `aceofspades_source/aoslib/images.py::GlobalImages.set_skin` switches the
  shared frame objects listed above.

## Crash invariant

Retail does not ship `gangster_character_*` portraits. Its
`GlobalImages.class_images` table intentionally maps classes 6 through 11
(Gangster 1-4 and both VIP bodies) to `soldier_character_team1/2`. The distinct
gangster and boss art exists only in the class-card icon table.

The native generator must preserve that alias. Class-menu presentation also
skips an empty optional icon/portrait instead of passing an empty asset path to
the renderer. Missing presentation art may degrade visually, but it must never
terminate a live match.

## Validation

- `aos_protocol168_session_tests` retains `texture_skin=mafia`.
- `aos_preload_loading_tests` verifies `MODE_VIP` and mafia VIP infographic
  resolution independently of stale browser metadata.
- `aos_class_catalog_tests` verifies the retail Soldier portrait alias for
  classes 6-11.
- `aos_class_selection_menu_tests` rejects empty VIP class-menu asset paths.
- Full native suite: 81/81 passing on 2026-07-31.
- Live smoke: C++ client joined a two-bot VIP match on Alcatraz, remained in
  gameplay for 20 seconds, and opened scoreboard, pause, and Change Team with
  zero stderr. Captures are under
  `out/evidence/vip-client-regression/client-smoke-fixed/`.

The BattleSpades server repository was used only as a read-only protocol/test
target and was not modified.
