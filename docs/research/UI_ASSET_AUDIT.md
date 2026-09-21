# Retail frontend UI asset audit

> **Recovery/specification reference.** Preserve the measured retail behavior and its evidence. Implementation updates, old build paths, test counts and session constraints below describe their original investigation; they are not current release or deployment status. Use the [maintained documentation index](../README.md) for present operating instructions and recheck historical findings against current source.

This audit identifies the assets and recovered behavior needed for the first
native `SelectMenu` render. It covers the immutable asset mirror in
`assets/original` and the canonical recovered client in
`G:\AoSRevival\aos-nonsteam\src`.

## Evidence and confidence

The findings use three evidence classes:

1. **Recovered source:** `aoslib/images.py`, `aoslib/text.py`,
   `aoslib/scenes/frontend/menuScene.py`, and
   `aoslib/scenes/frontend/selectMenu.py` expose asset names, layout, scales,
   and draw order directly.
2. **Recovered Python 2.7 bytecode:** `aoslib/image.pyc`, `aoslib/gui.pyc`, and
   `shared/hud_constants.pyc` were inspected with `uncompyle6`. They establish
   resource lookup, widget state machines, and the numeric HUD constants.
3. **Asset inspection:** PNG headers were inspected with Pillow, TTF name
   tables with fontTools, and Ogg streams with ffprobe.

All paths below are relative to `assets/original` unless otherwise stated.
Source dimensions are the actual file dimensions. Runtime dimensions are the
integer dimensions produced by the retail `load_texture()` scale before any
later scene transform.

## Key result

The retail frontend is not a mesh-based or atlas-driven UI. It is a fixed
800x600, bottom-left-origin logical canvas composed from loose PNG textures,
three-slice button graphics, runtime-rendered TTF glyphs, and immediate OpenGL
transforms. A faithful first native implementation therefore needs a small
renderer-neutral widget model and a sprite/text draw list, not a general HTML,
retained-mode, or 3D UI framework.

## Asset loading pipeline

- `aoslib/run.py:169` sets the Pyglet resource root to `png/ui`.
- `aoslib/images.py:32` defines `load_ui()` with a default scale of `0.6` and
  filtered sampling enabled.
- `aoslib/images.py:58` defines a second commonly used scale of `0.64`.
- Recovered `aoslib/image.pyc` shows that UI paths are assembled from path
  components and receive a `.png` suffix. Textures use clamp-to-edge. Assets
  passed with `filtered=False` switch both minification and magnification to
  nearest-neighbor sampling.
- The `png/low`, `png/med`, and `png/high` trees are world texture quality
  variants. The menu assets below come from `png/ui` and are not selected by
  world texture quality.

The native asset resolver should preserve these stable logical IDs but should
not expose filesystem paths to screens. For example,
`ui.main_menu.frame` can resolve to
`png/ui/main_menu/frame_main_menu.png` in the Classic profile.

## Main-menu composition

`MenuScene` chooses `global_images.main_image` as its background
(`menuScene.py:38`), fits it to the window, then draws the active menu inside a
uniformly scaled 800x600 canvas (`menuScene.py:118-132`). Mouse coordinates are
mapped back through the same transform (`menuScene.py:162`).

### Backgrounds and frames

| Asset | Format | Source | Retail load | Purpose |
|---|---:|---:|---:|---|
| `png/ui/ugc_splash.png` | PNG RGB | 1280x960 | 768x576, linear | Active `main_image` in `images.py:148` |
| `png/ui/main_splash.png` | PNG RGB | 1280x960 | 768x576 if loaded normally | Alternate main-menu artwork present in the retail asset set, but not referenced by the recovered active loader |
| `png/ui/splash.png` | PNG RGBA | 653x430 | 391x258, centered, linear | Ace of Spades: Battle Builder logo |
| `png/ui/main_menu/frame_main_menu.png` | PNG RGBA | 567x752 | 340x451, centered, linear | Main Select Menu frame |
| `png/ui/main_menu/frame_player_name.png` | PNG RGBA | 200x53 | 120x31, linear | Dynamically width-scaled welcome/name plate |
| `png/ui/main_menu/frame_3button_menu.png` | PNG RGBA | 566x422 | 339x253, centered, linear | Three-button child screen; not used by the first Select Menu |
| `png/ui/main_menu/frame_nav_bar_small.png` | PNG RGBA | 567x114 | 340x68, centered, linear | Small child-screen navigation frame; not used by the first Select Menu |

There is a naming ambiguity worth preserving as evidence: the asset set has
both `main_splash.png` and `ugc_splash.png`, while the recovered active code
explicitly selects `ugc_splash` at `images.py:148`. The first compatibility
render should follow the code and use `ugc_splash`; a retail screenshot should
then confirm whether that selection reflects the target build or a recovered
build-specific promotion. Do not silently rename or discard `main_splash`.

### Text-button slices

`TextButton` uses three independently stretched parts. Every source slice is
PNG RGBA, 60x97, and loads at 36x58 with nearest-neighbor sampling. The center
slice stretches horizontally; the left and right slices preserve their aspect
relative to the requested button height.

| State | Left | Middle | Right | Select Menu use |
|---|---|---|---|---|
| Normal | `button_large_left.png` | `button_large_mid.png` | `button_large_right.png` | Yes |
| Hover | `button_large_hover_left.png` | `button_large_hover_mid.png` | `button_large_hover_right.png` | Yes |
| Pressed | `button_large_press_left.png` | `button_large_press_mid.png` | `button_large_press_right.png` | Yes |
| Ready/pulsing | `button_large_ready_left.png` | `button_large_ready_mid.png` | `button_large_ready_right.png` | No |
| Start normal | `button_large_start_left_default.png` | `button_large_start_mid_default.png` | `button_large_start_right_default.png` | No |
| Start hover | `button_large_start_left_hover.png` | `button_large_start_mid_hover.png` | `button_large_start_right_hover.png` | No |
| Start pressed | `button_large_start_left_press.png` | `button_large_start_mid_press.png` | `button_large_start_right_press.png` | No |

All files are under `png/ui/common_elements/buttons/`. The optional glow image
`button_large_glow.png` is PNG RGBA 400x130 and loads at 240x78. It belongs to
the reusable pulsing/constant-glow behavior, not the root Select Menu.

Recovered `TextButton` behavior from `aoslib/gui.pyc`:

- labels are uppercased;
- tall buttons use the 36-point Spades button font and shrink to fit;
- hover changes the three slices;
- pressed-and-still-hovered changes the slices and depresses the content by
  approximately two logical pixels;
- activation occurs only on release while still inside the button;
- disabled buttons reuse the normal slices with a 0.7 multiplier rather than
  selecting the shipped `*_desat` textures.

### Square buttons and icons

The four top-row buttons use 97x96 PNG RGBA state images with nearest-neighbor
sampling and no load-time scale:

| State | Asset |
|---|---|
| Normal | `png/ui/common_elements/buttons/mm_button_square_default.png` |
| Hover | `png/ui/common_elements/buttons/mm_button_square_hover.png` |
| Pressed | `png/ui/common_elements/buttons/mm_button_square_press.png` |
| Shipped but not selected by recovered widget code | `png/ui/common_elements/buttons/mm_button_square_deactivated.png` |

Their overlay icons are also PNG RGBA 97x96, unscaled and nearest-filtered:

| Action | Asset | Source reference |
|---|---|---|
| Tutorial | `png/ui/icons/icon_tutorial.png` | `images.py:431`, `selectMenu.py:37` |
| Achievements | `png/ui/icons/icon_achievements.png` | `images.py:430`, `selectMenu.py:42` |
| Leaderboards | `png/ui/icons/icon_leaderboards.png` | `images.py:428`, `selectMenu.py:47` |
| Options | `png/ui/icons/icon_options.png` | `images.py:429`, `selectMenu.py:52` |

Recovered `SquareButton` behavior from `aoslib/gui.pyc` is release-inside
activation, normal/hover/pressed images, a depressed vertical offset of
`floor(-4 * requested_size / source_width)`, and disabled tinting with
`(86, 86, 86, 255)`. The root menu requests a 63.75 logical-pixel square.

### Navigation icons

The navigation icons are PNG RGBA 40x40 and load centered at 0.64, yielding
25x25 runtime textures:

- `png/ui/common_elements/nav_bar/quit_icon.png`
- `png/ui/common_elements/nav_bar/main_menu_icon.png`
- `png/ui/common_elements/nav_bar/back_icon.png`
- `png/ui/common_elements/nav_bar/right_icon.png`

Only `quit_icon.png` is needed on the first Select Menu. Recovered
`NavigationBar` behavior from `aoslib/gui.pyc` calculates each hit target from
the rendered text width, icon width, and a five-pixel pad. It dims idle items
to 70%, restores full intensity on hover, and activates on release inside.

## Exact first-screen geometry

The HUD constants recovered from `shared/hud_constants.pyc` are:

```text
MAIN_MENU_BUTTON_WIDTH = 262
MAIN_MENU_BUTTON_HEIGHT = 58
MAIN_MENU_BUTTON_FONT_SIZE = 30
MAIN_MENU_BOTTOM_BUTTON_Y_SPACE = 37
MAIN_MENU_SPACE_BETEWEEN_BUTTONS = 5
MAIN_MENU_SPACE_BETWEEN_BUTTON_GROUPS = 16
LIST_PANEL_SPACING = 10
```

Applying those constants to `selectMenu.py:24-74` gives the following 800x600
logical layout. A text button's Y value is its top edge in the recovered
widget.

| Element | Logical geometry |
|---|---|
| Main frame | centered at `(400, 236)`; loaded size `340x451` |
| Join Match | `x=269`, top `y=421`, `262x58` |
| Create Match | `x=269`, top `y=358`, `262x58` |
| Player Profile | `x=269`, top `y=295`, `262x58` |
| Map Creator | `x=269`, top `y=232`, `262x58` |
| Tutorial square | center `(298.875, 123.125)`, size `63.75` |
| Achievements square | center `(366.625, 123.125)`, size `63.75` |
| Leaderboards square | center `(434.375, 123.125)`, size `63.75` |
| Options square | center `(502.125, 123.125)`, size `63.75` |
| Quit navigation region | navigation bar `(248, 32)`, size `304x26`, middle item only |
| Logo | centered at `(412, 510)` after an additional `0.75` scene scale |
| Welcome/name plate | dynamically width-scaled and inset 10 pixels from the logical top-right safe edge |

The draw order in `selectMenu.py:186-218` is frame, name plate and text,
buttons/navigation, then logo. The surrounding `MenuScene` offsets outgoing
and incoming screens horizontally. Pointer input is transformed into design
space, and transition-time input should remain gated as documented in
`docs/FRONTEND.md`.

## Typography

All shipped fonts are TrueType. The name data below comes from each font's TTF
name table.

| File | Embedded family/style | Bytes | Recovered role |
|---|---|---:|---|
| `fonts/A750-Sans-Medium.ttf` | A750-Sans-Medium Regular | 44,256 | Default/English standard UI; welcome text at 16 pt |
| `fonts/Spades.ttf` | Aldo the Apache Regular, full name `Spades` | 62,412 | Default button/navigation font; buttons 18/36 pt, navigation 24 pt |
| `fonts/Edo.ttf` | Edo Pro Regular | 111,608 | Category/display text; Turkish display substitution |
| `fonts/Tuffy_Bold.ttf` | Tuffy Bold | 94,592 | Russian, Polish, and Turkish standard-text substitution |
| `fonts/Gen_Shin_Gothic_Monospace_Bold.ttf` | Gen Shin Gothic Monospace Bold | 4,952,384 | Cyrillic display/button substitution |
| `fonts/NotoSansJP-SemiBold.ttf` | Noto Sans JP SemiBold | 5,726,852 | Japanese/CJK standard-text substitution |
| `fonts/A750-Sans-Bold.ttf` | A750-Sans Bold | 43,418 | Present; only font-map generation was found in recovered text code, not the first screen |

The default mappings are at `aoslib/text.py:8-10`; the first-screen font
instances are at `text.py:286`, `text.py:291`, and `text.py:335-336`.
Locale substitutions are explicit in `aoslib/strings/__init__.py:36-47`.
An English-only first slice needs only A750-Sans-Medium and Spades. Full
retail-language parity needs the substitution fonts as well.

The shipped assets contain no font atlas. Glyph textures are generated at
runtime by the recovered custom font layer. The native renderer should shape
with FreeType/HarfBuzz and build a disposable runtime glyph atlas; that atlas
belongs in `assets/generated`, not `assets/original`.

## Menu audio

| Asset | Codec | Rate/channels | Duration | Bytes | Use |
|---|---|---|---:|---:|---|
| `music/mainmenu.ogg` | Vorbis | 44.1 kHz stereo | 103.385 s | 4,538,485 | Started by `selectMenu.py:118` |
| `sounds/menu_confirmA.ogg` | Vorbis | 44.1 kHz stereo | 0.472 s | 26,159 | Normal Select Menu actions |
| `sounds/menu_backA.ogg` | Vorbis | 44.1 kHz stereo | 0.472 s | 26,663 | Child-screen back action |
| `sounds/menu_scrollA.ogg` | Vorbis | 44.1 kHz stereo | 0.186 s | 12,405 | Tabs, sliders, and list navigation |
| `sounds/menu_buyA.ogg` | Vorbis | 44.1 kHz stereo | 1.063 s | 50,762 | Store/purchase action |

Only `mainmenu.ogg` and `menu_confirmA.ogg` are required for an interactive
first Select Menu. The other cues belong in the shared frontend asset group so
later screens do not invent replacements.

## Atlases, shaders, and meshes

- **Texture atlas:** none was identified for the frontend. Every referenced UI
  texture is a loose PNG loaded by logical path.
- **Button slicing:** large buttons are a three-slice composition, not a
  nine-slice atlas.
- **Font atlas:** generated at runtime, not shipped.
- **Frontend mesh:** none. Menu rendering is 2D textured quads and text.
- **Frontend-specific shader:** none was found in the recovered menu path or
  asset tree. The old client routes UI through the active OpenGL pipeline and
  uses `glColor`, translation, scale, and texture blits. A native bgfx UI
  program is an implementation detail and must reproduce alpha blending,
  nearest/linear sampling choices, and bottom-left retail coordinates.

## Smallest faithful first-menu asset set

For an **interactive English Select Menu**, lazily load these 25 files:

```text
png/ui/ugc_splash.png
png/ui/splash.png
png/ui/main_menu/frame_main_menu.png
png/ui/main_menu/frame_player_name.png

png/ui/common_elements/buttons/button_large_left.png
png/ui/common_elements/buttons/button_large_mid.png
png/ui/common_elements/buttons/button_large_right.png
png/ui/common_elements/buttons/button_large_hover_left.png
png/ui/common_elements/buttons/button_large_hover_mid.png
png/ui/common_elements/buttons/button_large_hover_right.png
png/ui/common_elements/buttons/button_large_press_left.png
png/ui/common_elements/buttons/button_large_press_mid.png
png/ui/common_elements/buttons/button_large_press_right.png

png/ui/common_elements/buttons/mm_button_square_default.png
png/ui/common_elements/buttons/mm_button_square_hover.png
png/ui/common_elements/buttons/mm_button_square_press.png

png/ui/icons/icon_tutorial.png
png/ui/icons/icon_achievements.png
png/ui/icons/icon_leaderboards.png
png/ui/icons/icon_options.png
png/ui/common_elements/nav_bar/quit_icon.png

fonts/A750-Sans-Medium.ttf
fonts/Spades.ttf
music/mainmenu.ogg
sounds/menu_confirmA.ogg
```

A static golden screenshot needs only 15 of them: omit the six hover/pressed
large-button slices, the two hover/pressed square images, and both audio files.
Keep loading lazy: `GlobalImages.load_global_images()` eagerly loads far more
than the Select Menu needs, but that old ownership model should not be copied.

## Recommended first implementation boundary

1. Implement and test the 800x600 canvas transform and inverse pointer
   transform without graphics dependencies.
2. Implement renderer-neutral `TextButton`, `SquareButton`, and
   `NavigationItem` state machines with release-inside activation.
3. Add a typed asset manifest for the 25-file set above, preserving per-asset
   filter and anchor metadata.
4. Encode the Select Menu geometry as immutable Classic-profile data and emit
   a sprite/text draw list.
5. Render the list through SDL3/bgfx, then add FreeType/HarfBuzz text and
   OpenAL menu cues.
6. Validate at 800x600, 1280x720, and 2560x1440 using golden screenshots and
   pointer-hit tests before implementing the settings screen or server browser.

This boundary exercises the correct native architecture—assets, input,
widgets, text, audio, screen routing, and rendering—while remaining small
enough to compare pixel-by-pixel with the recovered client.
