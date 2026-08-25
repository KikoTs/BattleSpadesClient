# UI layout editor and external translations

BattleSpadesClient keeps the recovered C++ presentations as its safe defaults,
then applies optional user overrides from two UTF-8 JSON files beside the
executable:

- `ui-layout.json` stores element rectangles in the retail 800x600 design
  coordinate space.
- `localization.json` stores the selected language and translated strings.

Both files are ordinary text files. They are copied into a clean staged client
folder, but the staging script never overwrites an existing copy. Layout and
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

`localization.json` uses schema version 1:

```json
{
  "schema_version": 1,
  "active_locale": "bg",
  "fallback_locale": "en",
  "locales": {
    "en": {
      "native_name": "English",
      "font_asset": "",
      "strings": {
        "main_menu_join_match": "JOIN MATCH"
      }
    },
    "bg": {
      "native_name": "Български",
      "font_asset": "fonts/Mplus1Code-VariableFont_wght.ttf",
      "strings": {
        "main_menu_join_match": "ИГРАЙ"
      }
    }
  }
}
```

To switch languages, change `active_locale` to a locale present in `locales`.
The running client checks the file approximately once per second and clears
its text texture cache after a successful reload. A restart is not required.

Lookup order is:

1. exact locale, such as `zh-Hant`;
2. base locale, such as `zh`;
3. `fallback_locale`;
4. the source-recovered C++ English string.

That fallback chain permits partial community translations without blank UI.
The shipped catalog contains 1,788 recovered English keys and locale entries
for English, Bulgarian, Russian, Ukrainian, Polish, Czech, German, French,
Spanish, Mexican Spanish, Brazilian Portuguese, Italian, Turkish, Japanese,
Korean, Simplified Chinese, and Traditional Chinese. Bulgarian, Russian,
Japanese, and Chinese have starter UI translations; remaining untranslated
keys safely display English until contributors fill them in.

Files must be valid UTF-8 JSON. Locale tags accept BCP-47-style letters,
numbers, and hyphens. `font_asset` is relative to the client asset roots;
MPlus and Noto assets are selected in the shipped catalog for Cyrillic and
CJK coverage. Invalid, oversized, or malformed edits are rejected and the
last valid in-memory catalog remains active.

To regenerate the English key inventory from the recovered retail source:

```powershell
py -3.12 tools\generate-localization-catalog.py `
  --source G:\AoSRevival\aceofspades_source\aoslib\strings\english.py `
  --output config\localization.json
```

Back up community translations before regeneration because the generator
creates a fresh catalog.

