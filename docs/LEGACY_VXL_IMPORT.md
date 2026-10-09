# Legacy VXL maps

Native disk loading accepts original Ace of Spades 0.75 and 0.76 maps. Copy a
`.vxl` file into the verified asset root's `maps` directory, or its sibling
`user/maps` directory; the map catalog discovers it without rewriting the source.
For this development checkout those are `assets/original/maps` and
`assets/user/maps`. In a developer build, `--offline --tutorial-map MyMap`
loads `MyMap.vxl` from either directory (the asset directory takes precedence).
For hosting, install it in the server's `maps` directory and select its basename
in the rotation; see the server's `docs/LEGACY_VXL_MAPS.md`.

The original map volume is 512 × 512 × 64. Import keeps X/Y orientation, cave
spans, terrain colors and the water bed, translating Z by exactly **+176** into
BattleSpades' 240-high world. It does not align terrain to its tallest stored
span: maps with no explicit bottom voxel and empty water columns keep their
correct height. Pure green and blue blocks remain terrain, rather than becoming
retail light markers.

VXL has no version header. A full 512-square custom map whose span coordinates
fit the original 64-high volume is detected as Classic. Known bundled maps
retain retail loading, including the 64-high `20thCenturyTown` map. For an
ambiguous custom map, add a JSON sidecar beside the VXL:

```json
{"vxl_format": "classic64"}
```

Supported values are `auto`, `classic64` and `retail`. Sidecar precedence is
`name.json`, `name.ugc`, `name.txt`, then `name.vxl.json`; the first sidecar
containing `vxl_format` wins. A sibling without this key does not mask a later
sibling's value. Legacy assignment metadata also accepts the top-level quoted
literal `vxl_format = 'classic64'` (single or double quotes, with an optional
trailing comment); the last such assignment within a file wins. Comments,
docstrings and nested values do not select a format. This setting also works in
the BattleSpades server's map metadata. Metadata is read as data; Python map
scripts are not executed by the client. The C++ `VxlMap::load_file(path, profile)`
API can select an explicit profile without using sidecars. Native byte-stream
consumers keep their explicit
protocol profile, so changing a disk map's format cannot change a live Classic
connection's wire coordinates.

The server normalizes its authoritative MapSync to the 240-high volume. The
client's `canonical240` decoder keeps those coordinates and colors as sent;
retail chroma-marker cleanup belongs to the source import and is not repeated
on an already finalized world. Source maps and their CRCs remain unchanged.

Input validation checks column count, span lengths, color bounds, monotonic
cave/air ranges and complete input before allocating the terrain volume.
Truncated files, impossible spans, extra columns and oversized files fail closed.
Tests cover native file import, profile overrides, a stock-name regression,
asymmetric caves, empty water columns, vivid colors, malformed spans and a
canonical server/client coordinate round trip (`aos_legacy_vxl_tests`).
