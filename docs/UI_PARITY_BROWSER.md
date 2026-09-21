# Retail UI parity browser

Press **F12** after the startup preload to open the developer parity browser
from an offline frontend screen. Developer tools are gated out of network play.
Press F12 again while the browser is active, click
Back, or press Escape to return.

The left panel is the complete recovered inventory. Use Left/Right or click
the filter to select All, Screens, Components, Widgets, or Services. Use
Up/Down, the mouse wheel, or click a row to select it. The right panel shows
the exact retail class, source line, parent composition, controls, states, and
assets. Scroll over the right panel to page long metadata independently.

`OPEN NATIVE` is enabled only for a screen with a deterministic native
fixture, including the recovered Map Creator browser and host lobby.
Catalog-only entries remain inspectable but are explicitly marked;
the browser never presents an unfinished panel as a parity-complete route.
`HITBOXES` overlays the major inspector input bounds for capture review.

The authoritative evidence is:

- `docs/research/RETAIL_FRONTEND_CATALOG.md`
- `assets/catalog/retail-frontend-screens.json`
- the preserved Python client under `G:\AoSRevival\aceofspades_decompiled`

The generated native inventory is rebuilt with:

```powershell
py -3 tools/generate-retail-ui-catalog.py `
    assets/catalog/retail-frontend-screens.json `
    src/frontend/retail_ui_catalog.generated.cpp
```

Validate schema, source coverage, parent references, uniqueness, and generated
freshness with:

```powershell
.\tools\validate-retail-frontend-catalog.ps1
```

The source catalogue records screen/component fixtures, recovered frontend
classes, widget types, navigation factories and specialized list rows. Derive
counts from `assets/catalog/retail-frontend-screens.json` and validate the
generated list after changes; old inventory totals do not prove current coverage.

## Shell and cursor fixtures

Retail begins forward/back slides at `+1`/`-1`, interpolates toward zero with
factor 10, exposes incoming controls once `abs(current_x) < 0.5`, and releases
the retained outgoing menu below `0.005`. Releasing the old layer must not snap
the incoming layer to zero; interpolation continues to subpixel rest.

Retail constructs `ImageMouseCursor(cursor.png, 6, image.height - 4)`, giving
pyglet's bottom-left hotspot `(6, 60)` for the 64×64 image. SDL uses a top-left
cursor origin, so the equivalent native operating-system hotspot is `(6, 4)`.
The cursor is owned by the window layer rather than drawn by bgfx, keeping it
responsive independently of the render frame rate.

## Graphical validation

The full real-window navigation smoke includes the halfway-transition click,
the parity browser, the hitbox overlay, and Open Native routing:

```powershell
.\tools\smoke-client-navigation.ps1 `
    -Executable .\out\build\native-dev\src\RelWithDebInfo\BattleSpadesClient.exe `
    -EvidenceDirectory .\out\evidence\frontend-parity-debug
```

The smoke requires an interactive, unlocked Windows desktop.
