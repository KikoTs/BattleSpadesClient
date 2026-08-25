# ADS sight placement — recovered specification

Status: **RECOVERED**. This closes `docs/WEAPON_SECONDARY_RECOVERY.md` UNRECOVERABLE
item 11 in full.

Everything below was decompiled this pass from the retail binaries under
`G:/AoSRevival/aceofspades_source/`. Addresses are image-base relative
(character.pyd / draw.pyd base `0x10000000`, gameScene.pyd base `0x10000000`).
Every Python attribute and module-global name was resolved through the Cython
string table (20-byte `__Pyx_StringTabEntry {PyObject** p; const char* s;
Py_ssize_t n; ...}`), not guessed — Hex-Rays renders
`__Pyx_GetModuleGlobalName` as an argument-less `sub_10007F60()` because the
name object is passed in `ESI`, so every global had to be read off the
preceding `mov esi, dword_*`.

Functions decompiled:

| Symbol | Binary | Address | Source lines |
| --- | --- | --- | --- |
| `aoslib.character.Character.draw_sight` (body) | character.pyd | `0x1005B630` | character.pyx 2069–2094 |
| `Character.draw_sight` (arg wrapper) | character.pyd | `0x100869B0` | — |
| `aoslib.character.Character.draw_fps` | character.pyd | `0x1005CB20` | character.pyx 2100+ |
| `aoslib.draw.draw_scaled` | draw.pyd | `0x1000DDF0` | draw.pyx 448–453 |
| `GameScene.draw` (caller) | gameScene.pyd | `0x10138B70` | gameScene.pyx 1442–1447 |

Identity proofs: the traceback literal `"aoslib.character.Character.draw_sight"`
at `0x1008B4D8` is pushed at `0x1005B7C4`; `"aoslib.character.Character.draw_fps"`
at `0x1008B590` is pushed at `0x1005CD08`; `"aoslib.draw.draw_scaled"` at
`0x1003A268` is referenced at `0x1000E035`. The wrapper at `0x100869B0` errors
with *"draw_sight() takes exactly 2 positional arguments"* and its kwlist
`off_100976A0` resolves to `['self', 'zoom_level']` (`0x1008F0A8`, `0x1008FDFC`),
so the signature is `Character.draw_sight(self, zoom_level)`.

---

## 1. THE TRANSFORM

### 1.1 The branch (this is the load-bearing structure)

`gameScene.pyd sub_10138B70`, gameScene.pyx 1442–1447:

```python
1442  if not self.camera_manager.is_controller_active() and <A>:
1443      if <zoom>:                                     # 0x1013EF1C IsTrue, 0x1013EF3A jz
1444          glClear(GL_DEPTH_BUFFER_BIT)               # 0x1013EF40 / 0x1013EFAE
1445          character.draw_sight(self.zoom_level)      # 0x1013F004 / 0x1013F031 / 0x1013F087
1446      else:
1447          character.draw_fps()                       # 0x1013F0E7 / 0x1013F114, empty tuple
```

`draw_sight` and `draw_fps` are **mutually exclusive**: the aimed arm ends at
`0x1013F0E5` with `jmp short loc_1013F15D`, hopping straight over the else block
at `0x1013F0E7`. Names: `dword_1028C550 → 0x1026B0E4 "draw_sight"`,
`dword_1028D928 → 0x1026A160 "draw_fps"`, `dword_1028B97C → 0x1026B600
"zoom_level"`, `dword_1028E2F8 → 0x10269CD0 "glClear"`, `dword_1028E9AC →
0x1026FAE4 "GL_DEPTH_BUFFER_BIT"`. The selector local is written at
`0x10138E1F` from `getattr(character, dword_1028E290 = "zoom")` (gameScene.pyx
1275) — a **boolean**, not `zoom_level`. Retail snaps; it never interpolates the
pose.

**While aimed, retail does not draw the weapon body, the arms, or any part of
the `draw_fps` chain.**

### 1.2 `Character.draw_sight` (character.pyx 2069–2094)

```python
def draw_sight(self, zoom_level):
    if not self.weapon_object:        return          # 2070  getattr 0x1005B682, IsTrue 0x1005B6E6
    if not self.weapon_object.sight:  return          # 2070  getattr 0x1005B82F, IsTrue 0x1005B867
    glPushMatrix()                                    # 2072  name 0x1005B8AD, call 0x1005B8E7
    glLoadIdentity()                                  # 2073  name 0x1005B92C, call 0x1005B95D
    glRotatef(180, 0.0, 1.0, 0.0)                     # 2074  name 0x1005B9A2, tuple 0x1005BA7C
    MODEL_SHADER.bind()                               # 2076
    MODEL_SHADER.uniformf_loc(MODEL_SHADER_BLEND_COLOR_LOC,
                              1.0, 1.0, 1.0, zoom_level)   # 2077  arg stored 0x1005BD1A
    x, y, z = self.weapon_object.sight_pos            # 2078  getattr 0x1005BDBE, 3-unpack 0x1005BF12
    X = 0.025 + x                                     # 2079  0x1005BFAC / Add 0x1005BFDA
    Y = -0.35 + y                                     # 2080  0x1005C025 / Add 0x1005C051
    Z =  1.85 + z                                     # 2081  0x1005C094 / Add 0x1005C0C0
    draw_scaled(self.weapon_object.sight.draw, 0.05, X, Y, Z)      # 2082  0.05 @0x1005C1E1
    pin = self.weapon_object.pin                      # 2083  getattr 0x1005C2F3
    if pin:                                           # 2084  IsTrue 0x1005C32B
        draw_scaled(pin.draw, self.weapon_object.pin_scale,        # 2085/2086
                    X - 0.015,                        # 2087  0x1005C417 / Sub 0x1005C448
                    Y + 0.3,                          # 2088  0x1005C484 / Add 0x1005C4B4
                    Z + 2.1)                          # 2089  0x1005C4F9 / Add 0x1005C523
    MODEL_SHADER.uniformf_loc(MODEL_SHADER_BLEND_COLOR_LOC,
                              1.0, 1.0, 1.0, 1.0)     # 2091
    self.weapon_object.draw_sight(self.view_weapon)   # 2092  0x1005C83A / 0x1005C8CE
    MODEL_SHADER.unbind()                             # 2093
    glPopMatrix()                                     # 2094  0x1005C9E1 / call 0x1005CA17
```

`180` is an int constant, not a float: `dword_10098844 = PyInt_FromLong(0xB4)`,
built at `0x1006D13E` / stored `0x1006D148`.

Resolved names (global → string-tab entry → C string):

```
0x100989D4 → 0x10094EC0 → 0x10090094 "glPushMatrix"     0x10098260 → 0x10094E98 → 0x10090604 "glLoadIdentity"
0x1009871C → 0x10094ED4 → 0x1008FB08 "glRotatef"        0x10098324 → 0x10094EAC → 0x1008FEA4 "glPopMatrix"
0x10098218 → 0x1009490C → 0x1008FE8C "draw_scaled"      0x10098CF0 → 0x10093E58 → 0x1008FFE4 "MODEL_SHADER"
0x10097D74 → 0x10091FCC → 0x1008D120 "MODEL_SHADER_BLEND_COLOR_LOC"
0x10098334 → 0x10096F68 → 0x10090554 "weapon_object"    0x10097DDC → 0x10096798 → 0x1008F478 "sight"
0x10098D54 → 0x100967AC → 0x1008FC04 "sight_pos"        0x1009809C → 0x10094880 → 0x1008F000 "draw"
0x10097B50 → 0x10095C30 → 0x1008EE58 "pin"              0x100985E4 → 0x10095C44 → 0x1008FB80 "pin_scale"
0x100984A0 → 0x1009422C → 0x1008EFD8 "bind"             0x10097A5C → 0x10096BF8 → 0x1008F600 "unbind"
0x100981C0 → 0x10096C20 → 0x10090264 "uniformf_loc"     0x10097F88 → 0x10094920 → 0x1008FCA0 "draw_sight"
0x10098890 → 0x10096EB4 → 0x1008FFB8 "view_weapon"
```

That is the **complete** attribute set. There is no twelfth. `ring` does not
occur as a standalone NUL-terminated string anywhere in character.pyd (0 hits).

`draw_sight` issues **no `glTranslatef` and no `glScalef` of its own** — proved
exhaustively, not by absence of notice: there are exactly 12 `mov esi,
dword_*; call sub_10007F60` sites in `0x1005B630..0x1005CB1D`
(`0x1005B8AD`, `0x1005B92C`, `0x1005B9A2`, `0x1005BB02`, `0x1005BBBF`,
`0x1005BC25`, `0x1005C0F3`, `0x1005C34F`, `0x1005C60C`, `0x1005C670`,
`0x1005C92C`, `0x1005C9E1`), and none is `dword_100988DC` ("glTranslatef",
record `0x10094EFC`) or `dword_100982DC` ("glScalef", record `0x10094EE8`).

### 1.3 `aoslib.draw.draw_scaled` (draw.pyx 448–453)

```python
def draw_scaled(func, scale, x=0.0, y=0.0, z=0.0):
    glPushMatrix()                # 0x1000DE00
    glTranslatef(x, y, z)         # 0x1000DF0A   <-- TRANSLATE FIRST
    glScalef(scale, scale, scale) # 0x1000E00B   <-- SCALE SECOND, same scalar x3
    func()                        # 0x1000E01E   PyObject_Call(func, empty_tuple, NULL)
    glPopMatrix()                 # 0x1000E065
```

Positional order proven by the wrapper `sub_100178E0`'s register marshalling
(`a2[3]→func`, `a2[4]→scale`, `a2[5]→x`, `a2[6]→y`, `a2[7]→z`); the three
defaults are genuinely `0.0` (`fldz → PyFloat_FromDouble` at `0x10013354` /
`0x1001337C`, stored `0x10013384`).

**Consequence:** `0.025 / -0.35 / 1.85` are *unscaled eye-space units*. Do not
multiply them by `0.05`. That is the single easiest way to get this wrong (a
20× error).

### 1.4 The composite, in OUR conventions

GL column form: `M = Ry(180) · T(X, Y, Z) · S(0.05)`, rooted at identity.

Our `mat_mul(a, b)` applies `a` first, then `b` (row-vector; documented at
`src/frontend/native_frontend_module.cpp:5380-5394`), so the same chain is
written back-to-front:

```cpp
Mat4 m = mat_scale(0.05F);                       // draw.pyx:451
m = mat_mul(m, mat_translate(X, Y, Z));          // draw.pyx:450 + character.pyx:2079-2081
m = mat_mul(m, mat_rotate_y(180.0F));            // character.pyx:2074
```

Pin (same yaw, its own scale):

```cpp
Mat4 p = mat_scale(pin_scale);                   // 0.02, weapon.py:37
p = mat_mul(p, mat_translate(X - 0.015F, Y + 0.3F, Z + 2.1F));   // character.pyx:2087-2089
p = mat_mul(p, mat_rotate_y(180.0F));
```

**No `Rx(-90)` undo.** `draw_scaled` applies no X rotation; retail draws the KV6
display list as authored, and `src/world/kv6_model.cpp` emits the same
`(x - px, -(z - pz), y - py)` space that `kv6.pyd` bakes — with each cube
CENTRED on that coordinate, which is the half-voxel CORRECTION 2 in §2 is about.
The sandbox sight branch is correct in omitting the rotation — do not add one.

**No camera term to remove.** `src/render/world_renderer.cpp:1883` already sets
an identity view matrix for the viewmodel view; that *is* our `glLoadIdentity`.
`world_renderer.cpp:1881` already does `setViewClear(..., BGFX_CLEAR_DEPTH, ...)`,
which covers gameScene.pyx:1444.

### 1.5 Eye-space result

`Ry(180)` maps `(x, y, z) → (−x, y, −z)`, so the sight model origin lands at

```
eye = ( −(0.025 + sight_pos.x),  −0.35 + sight_pos.y,  −(1.85 + sight_pos.z) )
```

i.e. `1.85 + z` units **in front** of the eye (GL looks down −Z). Pin origin:
`( −(0.010 + x), −0.05 + y, −(3.95 + z) )` — a near rear notch and a far front
post, 2.1 units apart, which is how iron sights physically line up.

### 1.6 What is drawn while aimed — the complete list

1. `weapon_object.sight.draw` at scale `0.05`, alpha `zoom_level`.
2. `weapon_object.pin.draw` at `pin_scale` (0.02), alpha `zoom_level` — **only
   the classic rifle**; `classicRifleWeapon.py:33 pin = SEMI_PIN` is the sole
   `pin =` override in the whole tree (`weapon.py:39 pin = None`).
3. The zoomed muzzle flash: character.pyx:2092 →
   `aoslib/weapons/weapon.py:209-210 def draw_sight(self, weapon_display):
   self.draw_muzzle(weapon_display, self.muzzle_flash_zoomed_view_offset)` →
   `draw_muzzle` (weapon.py:195-204), only when `muzzle_flash_view_display` and
   `muzzle_flash_timer > 0`: `glTranslatef(off)`, `glRotatef(muzzle_flash_rotation,
   0,0,1)`, `PASSTHROUGH_SHADER` around `muzzle_flash_view_display.draw()`.
   Fully opaque — the blend colour was reset at 2091. Note this is a **different**
   offset from the hip `muzzle_flash_view_offset`.

Nothing else. No weapon body, no arms, no `view_weapon` display-list draw
(`draw_muzzle` reads `weapon_display` only for `.size` and `.matrix`).

### 1.7 `zoom_level` is an alpha, never a lerp

`zoom_level` (param `a2`) is referenced exactly once in the entire function, at
`0x1005BD08/0x1005BD16/0x1005BD1A`, where it is stored into slot 4 of the
5-tuple for `uniformf_loc` — the blend colour's **alpha**. There is no
`PyNumber_Multiply` or `PyNumber_TrueDivide` anywhere in `sub_1005B630`; only
five `PyNumber_Add` (`0x1005BFD8`, `0x1005C051`, `0x1005C0C0`, `0x1005C4B4`,
`0x1005C523`) and one `PyNumber_Subtract` (`0x1005C448`). The sight and pin fade
in over the zoom ramp at a fixed, already-centred position.

---

## 2. WHY OURS IS OFF-CENTRE

> **CORRECTION 2 (GPU-measured, supersedes item 1 of CORRECTION 1 below).**
> Item 1 was right about **retail** and wrong about **us**. The `-0.025` X bbox
> centre it quotes only holds under the convention where a voxel's cube is
> CENTRED on `coord - pivot`. `src/world/kv6_model.cpp` emitted cubes spanning
> `[coord - pivot, coord - pivot + 1]` instead, which puts the X bbox centre at
> `0.000`, leaves the `+0.025` uncancelled, and pushed the aimed sight
> **0.77 degrees off-axis**. That was the reported bug, and it moved every KV6
> model in the game, not just sights.
>
> `kv6.pyd sub_1001AA50` settles it. It computes exactly three per-voxel bases
> — `(x - px)*n + c` at `0x1001AE0B`–`0x1001AE33`, `c - (z - pz)*n` at
> `0x1001AE3A`–`0x1001AE4E`, `(y - py)*n + c` at `0x1001AE52`–`0x1001AE63` —
> and builds each cube from two values held live across the voxel loop:
> `flt_100212B0 = -0.5` and `flt_100212B4 = +0.5`, scaled by the voxel size `n`
> (`c = 0.5n - 0.5`, zero at the `n = 1` every shipped asset loads with). Retail
> centres the cube. Fixed at the mesher, 2026-07-27.
>
> Four independent constants cancel to exactly zero under the centred
> convention and under no other: `0.025` at sight scale `0.05`; `0.025 - 0.015
> = 0.010` at pin scale `0.02`; and `-0.35 + 0.325 = -0.025` on the Y axis for
> both `sniper_sight` (pivot z `10.5`) and `rpg_sight` (pivot z `4.5`). Measured
> on a GPU at 1920x1080 and the ADS FOV, every one now reads `0.0000%` — see
> `tests/test_ads_sight_render.cpp`, which locks it.
>
> **CORRECTION 1 (measured after this document was written).** §2.1–§2.3 below
> diagnose the in-game symptom incorrectly. Keep them for the transform
> analysis, but the causal claim is wrong. Measurements:
>
> 1. ~~**The aimed sight is already exactly centred in our code.**~~ See
>    CORRECTION 2 above: true of retail, false of ours until the mesher was
>    fixed. The rest of the item stands and is confirmed — the `+0.025`
>    constant exists **precisely to cancel the half-voxel pivot bias** at scale
>    0.05 (`0.5 * 0.05 = 0.025`), and the pin's `-0.015` is `0.025 - 0.5*0.02`,
>    the same cancellation at pin scale. Both land dead centre. This is strong
>    independent confirmation that the decompiled constants are correct.
> 2. **The §2.3 fall-through is unreachable.** `zoomed_` is only ever set inside
>    `if (pressed && aims_down_sights(behavior))`
>    (`src/world/tutorial_session.cpp:679`), and `weapon_secondary_behavior`
>    already requires `!weapon.sight_model_asset.empty()`
>    (`src/world/weapon_secondary.cpp:37`). A weapon with no sight cannot enter
>    ADS, so it can never reach the hip-pose fall-through. EDIT 4 is worth doing
>    as a defensive invariant, not as a bug fix.
> 3. **The slot budget never rejects a sight.** Every `can_zoom` weapon has
>    <= 3 first-person parts against a budget of 8, so EDIT 3's premise does not
>    apply to the sandbox path.
>
> **What is actually missing is the pin (EDIT 5).** While aimed we draw only
> `semi_sight.kv6`, whose eye-space Y range is `[-0.65, -0.05]` — entirely below
> the crosshair, with *nothing at the centre*. Retail additionally draws
> `semi_sight_pin.kv6`, whose single red `#78181C` voxel lands at eye
> `(0.000000, -0.01, -3.94)` = **0.0% lateral, 0.75% of half-height below
> centre**. That red bead is the user's "one red ironsight on the center", and
> it is the aiming mark; the receiver below it is not supposed to be centred.
>
> Priority is therefore EDIT 5 first, then EDITs 2/1 (missing branches on the
> other two paths), then EDIT 4 as a guard. `can_zoom` being true for 64/65
> tools is retail-faithful (`aoslib/weapons/tool.py:45` sets the base default);
> do not "fix" it in the generator.

### 2.1 The surviving term

`RetailViewModelPose::character_offset{-0.4, -0.55, 0.9}` —
**`include/battlespades/world/retail_view_model.hpp:92`**
(note: `:89` is `model_scale`, and `:62` is `RetailSightPose::model_scale`).

It is applied unconditionally by all three viewmodel chains, each immediately
followed by `mat_rotate_y(pose.character_yaw_degrees)` = 180:

| Path | tool translate | arms translate |
| --- | --- | --- |
| `gameplay_lab_view_model_draws()` (`:6176`) | `:6203` | `:6235` |
| `sandbox_view_model_draws()` (`:6566`) | `:6600` | `:6643` |
| `view_model_draws()` (`:6852`) | `:6919` | `:6948` |

`mat_rotate_y` (`:5436-5446`, row-vector) at 180° maps `(x,y,z) → (−x, y, −z)`,
so the tool root lands at eye `(+0.4 − sway.x, −0.55 + sway.y, −0.9 − sway.z)`:
**0.4 units to the right at 0.9 forward, ≈ 24° off-axis.** That is the reported
symptom. `pose.tool_sway.x` and `pose.tool.position.x` ride along with it.

`−0.4` is not itself wrong — it is the correct hip-fire `draw_fps` root. The bug
is that **retail deletes the whole matrix (`glLoadIdentity`, character.pyx:2073)
and skips `draw_fps` entirely (gameScene.pyx:1443) while aimed, and two of our
three paths never branch at all.**

### 2.2 Where the branch is missing

* `sandbox_view_model_draws()` — **has** the branch, at
  `src/frontend/native_frontend_module.cpp:6606`, and its transform at
  `:6610-6614` is bit-for-bit retail (`S → T → Ry(180)`), with an early
  `return draws;` at `:6616` that correctly suppresses the weapon parts and all
  three arm parts.
* `gameplay_lab_view_model_draws()` (`:6176`) — **no** `zoomed()` test, **no**
  sight slot, **no** sight upload. This is the F4 all-weapons first-person view.
* `view_model_draws()` (`:6852`) — **no** `zoomed()` test, **no** sight upload.
  It delegates to the sandbox path when `weapon_sandbox_enabled()` (`:6853-6856`);
  otherwise it only ever serves `TutorialTool::block/spade/pistol`, of which only
  the pistol aims.

`grep zoomed() src/frontend/native_frontend_module.cpp` returns exactly two
hits: `:6606` (sandbox sight) and `:6777` (crosshair visibility).

### 2.3 The sandbox fall-through

`:6606` reads `if (tutorial_session->zoomed() && sandbox_sight_slot.has_value())`.
When a weapon is aimed but its sight mesh is absent — load failure, or the
8-slot budget rejection at `:6483-6491` ("sandbox sight exceeds viewmodel
slots") — the `&&` fails and execution falls through to the **full hip pose,
including `character_offset.x = −0.4`, under an ADS FOV**. Retail cannot reach
that state (character.pyx:2070 early-returns and the ADS gate already requires
`sight != None`), so the correct behaviour is to draw **nothing**.

### 2.4 What is already correct — do not "fix" it

* `src/world/retail_view_model.cpp:301-312` — `{sight[0]+0.025, sight[1]-0.35,
  sight[2]+1.85}` matches character.pyx:2079-2081 term for term.
* `include/battlespades/world/retail_view_model.hpp:61-65` — `RetailSightPose`
  defaults `model_scale 0.05`, `yaw_degrees 180.0`. Both match.
* `native_frontend_module.cpp:6610-6614` — the `S → T → Ry` order is right *because*
  `mat_mul` is row-vector and `draw_scaled` translates before it scales.
* `src/render/world_renderer.cpp:1881,1883` — depth clear and identity view.
* Catalog `sight_position` values match the retail class attributes exactly
  (`weapon_catalog.generated.cpp`; e.g. RIFLE `{0,0,0}`, PISTOL `{0,-0.1,-1}`,
  SNIPER `{0,0.325,-0.0}`, RPG `{0,0.325,-1.85}`).

### 2.5 Why retail is centred *laterally*, structurally

Every `sight_pos` in the tree has `x == 0.0` — all 20 explicit overrides plus
`weapon.py:38 sight_pos = (0, 0, 0)` for the five sighted weapons with no
override. So the X translate is the constant `0.025` for **every weapon in the
game**: `atan(0.025 / 1.85) = 0.77°` off-axis. Retail is structurally incapable
of an off-centre aimed weapon. **`sight_position[0]` is not a lateral tuning
knob — treat it as always zero.**

Vertically it is *not* the anchor that centres: `Y = −0.35 + sight_pos.y` sits
well below the axis, and the sight model's own geometry (X pivot at the exact
bbox centre in every asset; see §4) brings the notch up to the crosshair.

---

## 3. EDITS

Ordered by impact. Items marked **(RECOVERED)** follow directly from the
decompile; **(JUDGEMENT)** are engineering decisions about how to express it.

### EDIT 1 — (RECOVERED) aimed branch in `gameplay_lab_view_model_draws()`

`src/frontend/native_frontend_module.cpp`, insert after the `tool_matrix` lambda
closes (`:6208`) and before the part loop at `:6209`. Add a `lab_sight_slot`
beside `lab_arm_upper_slot` / `lab_arm_lower_slot`, and upload
`world::load_weapon_models(...).sight` where the lab uploads its tool parts.

> **CORRECTION.** `gameplay_debug_lab.preview_zoom()` is **not** an ADS flag —
> it is a model-inspection scale clamped to `[0.55, 2.25]`
> (`src/world/gameplay_debug_lab.cpp:98`) and is therefore always truthy. Using
> it as the branch condition would blank the lab viewmodel permanently. The lab
> has no ADS state today; either add an explicit one or skip EDIT 1. Do not use
> `preview_zoom()`.

```cpp
// gameScene.pyx:1443-1447 -- draw_sight and draw_fps are an if/else.
if (gameplay_debug_lab.preview_zoom()) {
    if (!lab_sight_slot.has_value()) {
        return draws;                       // character.pyx:2070 -- no sight, draw nothing
    }
    const auto sight = world::evaluate_weapon_sight(
        gameplay_debug_lab.selected_tool_id());
    Mat4 m = mat_scale(static_cast<float>(sight.model_scale));            // 0.05
    m = mat_mul(m, mat_translate(static_cast<float>(sight.position.x),
                                 static_cast<float>(sight.position.y),
                                 static_cast<float>(sight.position.z)));
    m = mat_mul(m, mat_rotate_y(static_cast<float>(sight.yaw_degrees)));  // 180
    draws.push_back({*lab_sight_slot, m});
    return draws;                           // weapon AND arms suppressed
}
```

Note what is *absent*: no `character_offset`, no `tool_sway`, no
`digging_pitch`, no animation pose. That is `glLoadIdentity`.

### EDIT 2 — (RECOVERED) aimed branch in `view_model_draws()`

Same file, insert after `:6898` (`const auto pose =
world::evaluate_retail_view_model(pose_input);`) and before the tool block at
`:6903`. Gate on the tool as well as on zoom — only the pistol of the three
tutorial tools aims, and an empty early return would blank the viewmodel:

```cpp
if (tutorial_session->zoomed() &&
    *uploaded_tool == world::TutorialTool::pistol &&
    tutorial_sight_slot.has_value()) {
    const auto sight = world::evaluate_weapon_sight(17U);   // PISTOL
    Mat4 m = mat_scale(static_cast<float>(sight.model_scale));
    m = mat_mul(m, mat_translate(static_cast<float>(sight.position.x),
                                 static_cast<float>(sight.position.y),
                                 static_cast<float>(sight.position.z)));
    m = mat_mul(m, mat_rotate_y(static_cast<float>(sight.yaw_degrees)));
    draws.push_back({*tutorial_sight_slot, m});
    return draws;
}
```

### EDIT 3 — (RECOVERED) slot budget + sight uploads

`include/battlespades/render/world_renderer.hpp:169` is
`static constexpr std::uint32_t view_model_slot_count{8U}` and all eight are
consumed by the tutorial upload block at `:6804-6829` (block, spade, pistol,
arms_upper, arms_lower, debug_upper, debug_lower_a, debug_lower_b). Raise it to
10 and upload the pistol sight there; the lab needs one more for EDIT 1.
Raising the constant is lower-risk than reclaiming `view_model_slot_debug_lower_b`.

### EDIT 4 — (JUDGEMENT) close the sandbox fall-through

`src/frontend/native_frontend_module.cpp:6606`. Replace

```cpp
if (tutorial_session->zoomed() && sandbox_sight_slot.has_value()) {
```

with

```cpp
if (tutorial_session->zoomed()) {
    if (!sandbox_sight_slot.has_value()) {
        return draws;                       // character.pyx:2070 early return
    }
```

so a weapon aimed without a sight renders nothing instead of snapping back to
the `+0.4` hip pose. Retail's behaviour is proven; that we can reach the state
at all is ours.

### EDIT 5 — (RECOVERED) the pin: the rifle's red bead

1. `include/battlespades/world/weapon_catalog.hpp`: add
   `std::string_view pin_model_asset;` beside `sight_model_asset` (`:287`), and
   `pin_scale` (default `0.02`, weapon.py:37) to `RetailUseTuning` beside
   `sight_position` (`:161`).
2. Generator emits, for **tool id 6 (RIFLE / `semi` / classicRifleWeapon) only**:
   `pin_model_asset = "kv6/semi_sight_pin.kv6"`, `pin_scale = 0.02`,
   authored offset `(0, 0, -0.5)` (`aoslib/models.py:230` row
   `('semi', ('pin',), (0,0,0), (0,0,-0.0), (0,0,-0.5))` through
   `models.py:143`). Every other weapon keeps `pin = None`.
3. `src/world/weapon_models.cpp`: add
   `load_optional(asset_root, definition->pin_model_asset, tint, result.pin, error)`
   beside the sight load at `:99-101`. The existing `render_offset{-x, +z, -y}`
   at `:30-36` converts `(0,0,-0.5)` to `(0,-0.5,0)` correctly; no new code.
4. `src/world/retail_view_model.cpp:301-313`: extend `RetailSightPose` and append

```cpp
result.has_pin      = !weapon->pin_model_asset.empty();
result.pin_scale    = weapon->retail.use.pin_scale;      // 0.02
result.pin_position = {result.position.x - 0.015,
                       result.position.y + 0.3,
                       result.position.z + 2.1};         // character.pyx:2087-2089
```

   — relative to the **already-offset** `result.position`, exactly as retail does.
5. Emit it as a second `push_back` in all three aimed branches, same yaw,
   `mat_scale(pin_scale)` in place of `0.05`.

### EDIT 6 — (RECOVERED) the zoomed muzzle flash

character.pyx:2092 → `weapon.py:209-210` uses `muzzle_flash_zoomed_view_offset`,
a **different** offset from the hip `muzzle_flash_view_offset`. Whatever emits
our hip muzzle flash must switch offsets while aimed rather than being
suppressed along with the weapon body.

### EDIT 7 — (RECOVERED) fade the sight in with `zoom_level`

Plumb `TutorialWorldSession::zoom_level()` (`src/world/tutorial_session.cpp:2388`,
already correctly ramped by `advance_zoom_level` in `weapon_zoom.cpp`) into
`render::ViewModelDraw` as a per-draw alpha applied to the sight **and** the
pin, matching character.pyx:2077 with the reset at 2091.

### EDIT 8 — (JUDGEMENT, required by EDIT 7) blend state

`src/render/world_renderer.cpp:1911-1913` sets
`BGFX_STATE_WRITE_RGB | WRITE_A | WRITE_Z | DEPTH_TEST_LESS | MSAA` — it writes
alpha but never blends, so the fade would be a no-op. Add
`BGFX_STATE_BLEND_ALPHA` for viewmodel draws carrying alpha < 1. See §5 for the
caveat on this.

### DO NOT CHANGE

* `native_frontend_module.cpp:6610-6614` — bit-for-bit retail.
* `retail_view_model.cpp:310-311` — the `+0.025 / −0.35 / +1.85` offsets.
* `world_renderer.cpp:1881/1883` — depth clear and identity view are our
  `glClear(GL_DEPTH_BUFFER_BIT)` and `glLoadIdentity`.
* Do **not** add an `Rx(-90)` undo to the sight chain.
* Do **not** interpolate the pose by `zoom_level` — retail snaps on the boolean
  `character.zoom` (gameScene.pyx:1443).
* Do **not** treat `sight_position[0]` as a lateral tuning knob; it is 0.0 for
  every weapon in the game.

---

## 4. SIGHT ASSETS

All 31 files below are present under
`G:/AoSRevival/BattleSpadesClient/assets/original/kv6/` — **every one ships.**
Dimensions are `x × y × z` in voxels as stored in the `Kvxl` header; colours are
read back as RGB (KV6 stores BGRA). Intended draw scale is the hard-coded `0.05`
for every `sight` model and `pin_scale` (0.02) for every `pin` model — retail
never consults `view_model_size` here. The `*_sight.kv6` main models are **not**
small reticles: they are full aimed-view barrel/receiver/scope models with the
X pivot at the exact bbox centre, which is what puts the barrel on the view axis.

### 4.1 `sight` — hollow tube family (scope you look through)

All `9×29×9`, pivot `(4.5, 14.5, 4.5)`, 580 voxels, 20 voxels per y-slice
forming a 9×9 circular ring outline over 29 slices; 25 slices of colour 1 +
4 slices of colour 2.

| File | Colours | md5 note |
| --- | --- | --- |
| `rpg_sight.kv6` | `#20280C` ×500 / `#0C1400` ×80 | identical to drillgun (`2cfb9e9b`) |
| `drillgun_sight.kv6` | same | identical to rpg |
| `RPG2_sight.kv6` | `#801000` ×500 / `#A41004` ×80 | identical to ugc (`45b402d6`) |
| `ugc_RPG2_sight.kv6` | same | identical to RPG2 |
| `snowblower_sight.kv6` | `#DCF4FC` ×500 / `#ACECF4` ×80 | — |
| `sniper2_sight.kv6` | `#141414` ×500 / `#0C1400` ×80 **+ 4 red `#78181C`** at y=26, (x,z) = (1,4),(4,1),(4,7),(7,4) — a centred red four-tick crosshair | 584 vox |

These are the weapons with `sight_pos.z = -1.85`, which exactly cancels the
`+1.85` constant, putting the tube's centre on the eye point. The tube is
`29 × 0.05 = 1.45` units long, so half of it sits behind the near plane. A
scope-tunnel look follows geometrically — but that is **inference**, see §5.

### 4.2 `sight` — receiver / barrel family

| File | Dims | Pivot | Voxels | Colours |
| --- | --- | --- | --- | --- |
| `semi_sight.kv6` | 6×68×13 | (3, 34, 6) | 1512 | 19; `#6C5438` ×742 walnut, `#545454` ×182. **No red voxel.** |
| `shotgun_sight.kv6` | 6×56×12 | (3, 28, 5.5) | 983 | 7; `#443424` ×619, `#343434` ×327 |
| `classic_shotgun_sight.kv6` | 6×56×12 | — | 983 | byte-identical to `shotgun_sight` (`ffb58f63`) |
| `Shotgun2_sight.kv6` | 10×56×12 | (5, 28, 6) | 1506 | 5; `#747478` ×754, `#5C6060` ×428, `#7C4C0C` ×228 |
| `smg_sight.kv6` | 6×50×20 | (3, 25, 10) | 1092 | 5; `#343434` ×1062 |
| `classic_smg_sight.kv6` | 6×50×20 | — | 1092 | byte-identical to `smg_sight` (`93cf2643`) |
| `minigun_sight.kv6` | 6×50×20 | — | 1092 | byte-identical to `smg_sight`; **unreachable** (`minigun can_zoom = False`) |
| `pistol_sight.kv6` | 6×22×14 | (3, 11, 7) | 414 | 2; `#543800` ×270, `#606064` ×144 |
| `Weapon_SnubNosePistol_sight.kv6` | 3×12×9 | (1.5, 5.5, 6.5) | 93 | 7 greys |
| `Weapon_TommyGun_sight.kv6` | 9×38×11 | (4.5, 28, 9.5) | 361 | 10; `#5C2014`/`#44140C` wood + `#202024` |
| `assaultRifle_sight.kv6` | 5×52×15 | (2.5, 26, 7.5) | 441 | 5; `#343430` ×223, `#2C2C28` ×160, one `#CCA460` brass |
| `autoPistol_sight.kv6` | 3×20×18 | (1.5, 10, 9) | 215 | 5; `#5C5C5C` ×90, `#787878` ×65 |
| `autoShotgun_sight.kv6` | 7×38×13 | (3.5, 19, 6.5) | 332 | 6; `#88886C` ×167, `#40403C` ×142 |
| `lightMachineGun_sight.kv6` | 13×42×13 | (6.5, 21, 6.5) | 477 | 6; `#545C44` ×227, `#2C2C24` ×155 |
| `sniper_sight.kv6` | 21×61×21 | (10.5, 30.5, 10.5) | 3436 | 28; largest, full scope body |

### 4.3 `pin` — the front bead (scale 0.02)

All `1×1×5`, pivot `(0.5, 0.5, 2.5)`, 5 voxels: a one-voxel-square post whose
`z = 0` tip is **red `#78181C` = RGB(120, 24, 28)**, the rest grey.

| File | Drawn? |
| --- | --- |
| `semi_sight_pin.kv6` | **YES** — the only pin retail ever draws (`classicRifleWeapon.py:33`) |
| `shotgun_sight_pin.kv6` | loaded as `SHOTGUN_PIN`, never assigned to any class — dead |
| `Shotgun2_sight_pin.kv6` | dead |
| `classic_shotgun_sight_pin.kv6` | dead |

`semi_sight_pin.kv6` carries a load-time offset `(0, 0, -0.5)` (models.py:230 →
`KV6.offset_pivots`, kv6.pyd `sub_1000E900`, kv6.pyx:90-98), so its z pivot
becomes `2.0`. Every other sight/pin/ring loads with offset `(0,0,0)`.

At `pin_scale 0.02` the post is 0.10 units tall at depth 3.95; at the ADS
`FOV_y` of 37.5° the half-height there is `3.95·tan(18.75°) = 1.3404`, so the
whole post is 3.7% of screen height (~40 px at 1080p) and the **single red
voxel is 0.75% (~8 px), 0.7% left of centre and 3.7% below it.** That is the
user's "ONE small RED iron sight at the centre of the screen", exactly.

Note `classicRifleWeapon.py:27` sets `show_crosshair = UNZOOMED_CROSSHAIR`, so
the HUD crosshair is *hidden* while aiming the rifle — precisely so the red pin
is the only aim mark on screen.

### 4.4 `ring` — dead art, DO NOT WIRE

All `9×1×9`, pivot `(4.5, 0.5, 4.5)`, 24 voxels, one voxel thick, flat hollow
circle in the x/z plane.

| File | Colours |
| --- | --- |
| `smg_sight_ring.kv6`, `classic_smg_sight_ring.kv6`, `minigun_sight_ring.kv6` | all 24 `#3C3C3C` (byte-identical, `14a4de0a`) |
| `shotgun_sight_ring.kv6`, `classic_shotgun_sight_ring.kv6`, `Shotgun2_sight_ring.kv6` | 4 greys `#3C3C3C/#343434/#2C2C2C/#242424` (byte-identical, `dd58162e`) |

`ring = SMG_RING` (smgWeapon.py:36), `ring = SMG_RING` (tommyGunWeapon.py:37)
and `ring = CLASSIC_SMG_RING` (classicSmgWeapon.py:36) are the **only three
assignments in the codebase, with zero reads**. The string `"ring"` does not
occur in character.pyd at all. `minigun_sight_ring.kv6` is not even loaded
(models.py:231 passes no `sight_extra`).

### 4.5 No PNG reticle exists

An exhaustive search of `assets/original/png/` (844 files) for
`sight|retic|crosshair|hair|aim` returns only
`{high,med,low}/laser_sight_beam_{blue,green,small}.png` — consumed by
`LaserAttachment` (laserAttachment.py:75-83) as a world beam/dot, not an
overlay — plus `ui/common_elements/frames/ui_frame_overlay_disclaimer.png`
(matched on "aim" inside "disclaimer"). The HUD crosshair is coloured geometry:
`shared/constants.py:843-846` defines `NORMAL_CROSSHAIR_COLOUR = (255,255,255)`,
`HIT_CROSSHAIR_COLOUR = (230,40,79)` and the five-value mode enum, and
gameScene.pyd draws five named elements `TARGET_CROSSHAIR_{TOP_LEFT,TOP_RIGHT,
BOTTOM_LEFT,BOTTOM_RIGHT,CENTRE}` with no crosshair texture name anywhere.

---

## 5. STILL UNRECOVERABLE

Do not invent values for any of these.

1. **The outer guard at gameScene.pyx:1442.** It is a conjunction —
   `not self.camera_manager.is_controller_active()` (globals `0x1028C3A8 →
   "camera_manager"`, `0x1028DAA0 → "is_controller_active"`, IsTrue `0x1013EEB5`,
   inverted branch `0x1013EEE1`) **and** a second precomputed local at
   `0x1013EEF0`. The second conjunct's provenance was not traced. The
   *selector* between draw_sight and draw_fps (`character.zoom`, written at
   `0x10138E1F` from gameScene.pyx:1275) **is** proven.
2. **GL blend state.** `sub_1005B630` contains no `glEnable(GL_BLEND)` and no
   `glBlendFunc` in its `0x14ED` bytes. `zoom_level` provably reaches a shader
   uniform; whether that produces a visible fade depends on blend state set by
   the caller and by the GLSL `MODEL_SHADER`, neither of which was decompiled.
   EDIT 8 rests on this unverified premise — treat "the sight fades in" as
   plausible-but-unverified.
3. **The scope-tunnel reading** for the `sight_pos.z = -1.85` family. The
   geometry (tube centred on the eye, 1.45 units long) is proven; that it
   *looks* like a vignette in retail is inference. Nobody observed it.
4. **The HUD crosshair renderer.** gameScene.pyd's crosshair draw was not
   decompiled. "Geometry, not sprite" rests on the RGB-tuple constants and the
   total absence of a crosshair texture — strong, but inference.
5. **Whether `ring` is read by another `.pyd`.** Proven never read by
   `Character.draw_sight` and by every `.py` in `aoslib/`; gameScene.pyd and
   draw.pyd were not exhaustively searched for a `ring` attribute read. Given
   that `draw_sight` is the only sight renderer, a read elsewhere is
   implausible — but it is not formally excluded.
6. **Runtime visual confirmation.** Nobody has screenshot-compared an aimed
   weapon against retail. The eye-space arithmetic and the asset pivots make
   the result predictable, but §2.5's claim that the sight model's own geometry
   lifts the notch to the crosshair deserves a check against the
   retail-parity rig (`retail-parity-rig` memo) once EDITS 1–5 land.
7. **`Character.draw_fps` internals.** `sub_1005CB20` was located and its two
   eye-space collapses identified (weapon block `glPushMatrix 0x1005E60E /
   glLoadIdentity 0x1005E68D / glRotatef(180) 0x1005E70F / glTranslatef
   0x1005EA1C`; arms block `0x1005EDD8 / 0x1005EE5F / 0x1005EED9 /
   get_arms_position 0x1005F077 / get_arms_orientation 0x1005F129 /
   rotate_arm_ratio 0x1005F43D / glRotatef 0x1005F3BE`), but it was not fully
   reconstructed. It is not needed: the aimed path never reaches it, and our
   existing hip chain is left untouched. See the `drawfps-chain-resolution`
   memo.
