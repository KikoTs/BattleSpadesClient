# UI layout editor and external translations

The Inventory profile tab uses native RmlUi markup. Edit its layout and styling
in `assets/client/ui/inventory.rml` and `inventory.rcss`; the F11 editor below
applies to the C++ presentations. See [INVENTORY.md](INVENTORY.md) for the
inventory renderer, packaging and validation details.

BattleSpadesClient keeps the recovered C++ presentations as its safe defaults,
then applies optional user overrides from UTF-8 JSON files beside the executable:

- `ui-layout.json` stores element rectangles in the retail 800x600 design
  coordinate space.
- `localization/en.json`, `localization/ru.json`, and the other files each own
  one language. The selected locale is stored as `main.language` in
  `settings.toml`.

These are ordinary text files. They are copied into a clean staged client
folder; existing copies are preserved unless localization refresh is explicitly
requested with `-RefreshLocalization`. Layout and
translation edits therefore survive later client builds and do not require a
new executable.

## Visual layout editor

The editor is deliberately offline-only. It cannot be combined with
`--connect`, it closes before a hosted or network match, and it cannot emit
gameplay/debug actions to a server.

Start directly on a screen fixture with:

```powershell
BattleSpadesClient.exe --debug-ui leaderboard --ui-editor
```

For other screens, launch normally, navigate to the screen, and press **F11**.

Editor controls:

| Input | Action |
|---|---|
| F11 | Open or close the editor |
| Click | Select the topmost UI element under the cursor |
| Drag | Move the selected element |
| Drag the gold lower-right handle | Resize the selected element |
| Arrow keys | Move one design pixel |
| Shift + arrow keys | Resize by one design pixel |
| Delete | Remove the selected override and restore its C++ default |
| Ctrl+S | Save `ui-layout.json` |
| Ctrl+R | Discard unsaved edits and reload `ui-layout.json` |

The selected element's stable semantic ID is shown at the top. Saved JSON uses
absolute rectangles so it remains deterministic at every output resolution:

```json
{
  "schema_version": 1,
  "coordinate_space": "retail_design_pixels_800x600",
  "overrides": {
    "leaderboard/sprite.png_high_ui_common_panel.png#0": {
      "x": 112.0,
      "y": 54.0,
      "width": 576.0,
      "height": 492.0
    }
  }
}
```

Only sprites and text expressed in design pixels are selectable. Window-space
backgrounds and gameplay geometry are intentionally excluded. Deleting the
file, clearing `overrides`, or deleting a selected override restores the
source-backed presentation immediately.

## External language packs

Each `localization/<locale>.json` file uses schema version 1:

```json
{
  "schema_version": 1,
  "locale": "bg",
  "native_name": "Български",
  "font_asset": "fonts/Tuffy_Bold.ttf",
  "strings": {
    "main_menu_join_match": "ИГРАЙ"
  }
}
```

To switch languages, open Settings → Main → Language. The running client
checks every language file approximately once per second and clears its text
texture cache after a successful edit. A restart is not required.

Lookup order is:

1. exact locale, such as `zh-Hant`;
2. base locale, such as `zh`;
3. English (`en`), or the first valid pack when English is absent;
4. the source-recovered C++ English string.

That fallback chain permits partial community translations without blank UI.
The shipped catalog contains more than 1,800 recovered and native-client keys. German, French,
Spanish, Mexican Spanish, Brazilian Portuguese, Italian, Russian, Polish,
Turkish, and Japanese use their complete recovered retail translations.
Bulgarian has a starter frontend translation; Ukrainian and Czech are exposed
as community overlays and safely display English for keys not translated yet.
The revival-only identity, friends, lobby, direct-connect, matchmaking,
recovery, and map-creator controls are initially translated for English,
Bulgarian, German, French, Spanish, Russian, and Japanese.

Files must be valid UTF-8 JSON. Locale tags accept BCP-47-style letters,
numbers, and hyphens. `font_asset` is relative to the client asset roots;
Tuffy is selected for Cyrillic, Polish, and Turkish just as it was in retail.
Japanese uses the OFL-licensed Noto face bundled under `assets/client/fonts`
on every supported platform. It no longer depends on an optional Steam
language depot. The renderer checks glyph coverage before shaping and falls
back per text run. If no loaded face covers a symbol (for example, an emoji
in a legacy server's player name), the live UI displays the font's missing-glyph
placeholder instead of stopping the client. Font and translation tests retain
strict coverage checks. Invalid, oversized, or malformed edits are
rejected and the last valid in-memory catalog remains active.

The recovered Japanese source contained sixteen labels polluted with six
Simplified-Chinese characters that its own font cannot render. Generation
repairs only those known typos to idiomatic Japanese; the regression suite
rejects their reintroduction.

To regenerate the English key inventory from the recovered retail source:

```powershell
py -3.12 tools\generate-localization-catalog.py `
  --source G:\AoSRevival\aos-nonsteam\src\aoslib\strings `
  --output-dir config\localization
```

The generator overwrites only configured language filenames. Back up edits to
those files first; unrelated community JSON files remain untouched.
