# UGC Map Creator Recovery

This document is the implementation contract for the native Map Creator. The
authoritative BattleSpades server remains unchanged; the native client speaks
its existing Protocol 168 editor surface.

## Retail evidence

The recovered behavior is derived from these read-only retail sources:

- `aoslib/scenes/frontend/ugcSelectMenu.py`
- `aoslib/scenes/frontend/ugcSquadsMenu.py`
- `aoslib/scenes/frontend/ugcSquadLobbyMenu.py`
- `aoslib/scenes/ingame_menus/ugcSettings.py`
- `aoslib/scenes/ingame_menus/selectUGC.py`
- `aoslib/scenes/ingame_menus/selectPrefabs.py`
- `aoslib/scenes/ingame_menus/selectGameData.py`
- `aoslib/scenes/main/ugcObjectivesListPanel.py`
- `aoslib/scenes/main/ugcObjectiveListItem.py`
- `aoslib/scenes/ingame_menus/screenshotHud.py`
- `aoslib/weapons/ugcTool.py`
- `aoslib/weapons/ugcPrefabTool.py`
- `aoslib/models.py` (`UGC_ENTITY_MODELS`)
- `shared/constants.py` (`UGC_RIGHT_CLICK_GROUPS`, item and class ids)
- `shared/constants_prefabs.py` (all 448 prefab categories and size tags)
- `shared/constants_ugc_objectives.py` (marker ids and validation limits)

The server interoperability evidence is the read-only
`modes/ugc.py` implementation and its Protocol 168 tests in the BattleSpades
repository. No server file is part of this client change.

## Frontend route and local host

The route is the retail sequence:

1. Main Menu -> Map Creator.
2. Map Creator Lobbies -> New Lobby.
3. Map Creator Lobby configures privacy, maximum players, baseplate, prefab
   set, target mode, and title.
4. Start Game launches `BattleSpadesMapCreator` as a hidden child process and
   connects through the ordinary server-loading pipeline.
5. Leaving the owned session terminates the child process.

Nine authored baseplates are available:

| UI map | generator terrain |
|---|---|
| DesertBaseplate | desert |
| LunarBaseplate | lunar |
| MountainBaseplate | mountain |
| GrasslandBaseplate | grassland |
| Templebaseplate | temple |
| UrbanBaseplate | urban |
| MarshTemplate | marsh |
| SnowyBaseplate | snowy |
| WaterBaseplate | water |

Projects are written below the user's settings directory at
`hosted_ugc/maps`. A project consists of sibling `.ugc`, `.vxl`, `.txt`, and
optional `.png` files. The Publish Map browser reads this same catalog through
opaque, traversal-safe project identifiers.

## Wire contract

| Id | Direction | Layout | Native responsibility |
|---:|---|---|---|
| 12 | bidirectional | mode byte | switch the editor's target game mode |
| 51 | bidirectional | NUL string | selected skydome definition |
| 97 | client -> server | item, XYZ, place flag | place/remove one UGC marker |
| 98 | server -> clients | item, XYZ, place flag | replicate marker mutation |
| 99 | client -> server | mode, in-editor flag | request the filtered marker set |
| 100 | bidirectional | lifecycle code 0..4 | convert/validate/VXL/map-info requests |
| 101 | server -> client | 0..100 percent | host-map loading progress |
| 102 | server -> client | counted objective records | mode validation result |
| 118 | bidirectional | count then RGBZ rows | terrain palette and water color |

`InitialInfo.map_is_ugc` is a three-state role, not a boolean: none=0,
peer-host=1, collaborative client=2. A dedicated BattleSpades editor advertises
2 even to its logical owner because retail role 1 expects an in-process Steam
lobby VXL object and crashes when that object is absent. The native client
therefore exposes host settings when either role 1 is received or this process
owns the dedicated Map Creator child. It does not grant those controls to a
remote collaborative client.

Map chunks use an unsigned 16-bit payload length. Real compressed editor maps
contain chunks larger than 32767 bytes; decoding this field as signed truncates
the stream and prevents MapSync.

## UGC Builder inventory

Class 13 uses the ordinary server-authoritative loadout packet. Tool 41 is
expanded into its UGC marker variants; tool 42 is the structure/prefab editor.
Paint, pickaxe, Super Spade, RPG2, drill and snowblower remain ordinary catalog
tools and use the same action adapter as multiplayer.

The marker groups reproduce `UGC_RIGHT_CLICK_GROUPS`:

- health, ammo, block and Occupation-bomb points;
- green and blue spawn zones in small, medium and large sizes;
- green, blue and neutral base zones in small, medium and large sizes.

Each variant has its retail toolbar PNG and the exact multipart KV6 marker,
scale, Z offset and team-material treatment from `UGC_ENTITY_MODELS`.

### Construct and Game Data libraries

Joining an editor session opens `SelectPrefabs`; the Escape menu exposes both
the Construct Library and Game Data Library in place of ordinary class/team
selection. Both screens share one five-entry FIFO backpack. Selecting a sixth
entry discards the oldest selection, and clicking an already selected card or
backpack icon removes it. Switching between the two libraries preserves that
shared order.

The Construct Library admits only prefab identifiers advertised by the
server and present in retail's 448-entry `PREFABS_NAMES_WITH_TAGS` catalog.
The six source categories, English display names, preview paths, natural
numeric ordering, and Tiny/Small/Medium/Large/Huge labels are retained. A
size tag from the source takes precedence; otherwise the label is derived from
the shipped KV6 voxel count at byte 28 using the retail thresholds. Unknown
names fail closed rather than being guessed into a category.

The Game Data Library resolves packet-68 objective strings through
`UGC_OBJECTIVES_TYPES`, so only marker families legal for the current target
mode appear. Its Objectives panel is updated from live packet-68 values and
uses the exact source limits and priorities:

| Objective | Min | Max | Priority |
|---|---:|---:|---:|
| team spawn areas | 1 | 10 | 1 |
| team zones | 1 | 1 | 2 |
| TC/Multi-Hill neutral zones | 2 | 10 | 3 |
| Diamond neutral zones | 2 | 5 | 3 |
| bomb spawns | 1 | 5 | 3 |
| health/ammo/block crate spawns | 2 | 25 | 4 |
| block count | 0 | 100000 | 5 |

Selecting submits one complete packet-13 transaction for class 13. Its exact
source loadout is `[5,45,47,48,69,43,30,42,41,25,26,30]`; the duplicate tool
30 is intentional. Prefab strings and marker bytes occupy their ordinary
packet-13 fields, so the compatibility server remains the only authority.

### Marker input

- LMB on an empty valid surface places the selected item.
- LMB on an existing marker removes it.
- RMB on empty space advances only the selected item's retail group.
- RMB on a marker sends ordered remove-then-place packets for the next size or
  team variant.
- Invalid surfaces and occupied marker positions hide the placement ghost and
  emit no packet.

The ghost is rendered at alpha 0.3 with the same two model parts as the placed
marker. Packet 98 is materialized as entity type 29 so reconnect/collaborative
edits use the same render path as live local placement.

## Structure library

Server-provided prefab names become individual inventory entries backed by
tool 42. Each entry resolves its VXL preview, computes a bounded placement
candidate, displays the green/red ghost, rotates in 90-degree world-absolute
steps on RMB, and emits the existing prefab build/erase packet sequence. Packet
29 remains the atomic reveal boundary: slices from packet 30 are never exposed
as half-built structures. UGC placement uses the retail `ugc_place` feedback
bank rather than competitive `prefabbuild` audio.

### Two-stage Construct Builder control

`UGCPrefabTool` is not a one-click competitive prefab tool. Its source-visible
state machine is now retained by `world::UgcPrefabControl`:

1. The first LMB captures the approximate ghost and stops the player. It sends
   no build packet.
2. A prefab-centered orbit camera takes over while W/A/S/D nudge the blueprint,
   Jump/Crouch move it vertically, Sprint multiplies a nudge by the recovered
   factor 10, and the mouse wheel applies
   `zoom -= scroll_y * prefab_radius * 0.1`.
3. The palette-arrow bindings rotate yaw/pitch/roll in camera-relative quarter
   turns using the exact compound branches from
   `UGCPrefabTool.apply_prefab_rotation`. The source-asymmetric WEST+left and
   EAST+right pitch-3 branches are retained rather than mirrored from their
   opposite directions; regression fixtures pin both pitch-1 and pitch-3
   outcomes. RMB retains the ordinary world-Z quarter turn.
4. Q cancels and arms the source 0.5-second re-entry guard. C emits
   `ErasePrefabAction(31)` immediately and then at the recovered 0.1-second
   cadence while held.
5. The second LMB validates and sends one `BuildPrefabAction(30)` containing
   the retained anchor plus all three rotations. Only a successfully queued
   build exits fine placement.

The held ghost uses the source green 0.9..2.0 pulse at alpha 0.5; invalid
placement remains red at alpha 0.4. UGC packet 30 always carries
`add_to_user_blocks = false`, exactly as `UGCPrefabTool.confirm_prefab_placement`
requests. Competitive prefab builders retain `true` and therefore keep their
normal stock accounting. The existing BattleSpades server is only a packet
oracle and was not changed.

## Host settings

The X binding opens the recovered host-only in-game Settings panel. It supports
keyboard and mouse selection for:

- skydome;
- water red, green, and blue channels;
- target game mode;
- bounded 19-character title;
- overhead map-preview capture to the project's PNG sidecar;
- Cancel and Apply.

The presentation now follows `ugcSettings.py` rather than the former debug-like
text overlay. At the 800x600 design boundary the outer frame is
`(133,28,534,524)`, the content frame is `(112,10,552,579)`, the title area is
`(250,26,300,80)`, and the Cancel/Apply TextButtons are `(160,439,232,41)` and
`(403,439,232,41)`. These sizes include retail's integer truncation after
`global_scale = 0.64`; scaling the source PNGs with a floating rectangle gives
different anchors and visible seams.

The expanded list retains the source 26-pixel category rows, 32-pixel setting
rows, two-pixel gaps, category-green headers, textured setting rows, dropdown,
RGB sliders, color preview, edit box, map-preview TextButton, and 331-pixel
scrollbar. Only ten of the eleven expanded rows fit initially. Focusing Map
Preview advances the viewport by one row, and pointer routing consumes the same
presentation bounds used to draw it, preventing the old render/hitbox drift.

Apply sends the skydome, complete RGB/Z-threshold ground palette and target
mode, then updates local rendering. Water changes remesh all resident chunks so
the editor view and the authoritative project agree immediately.

Activating Map Preview writes the same square overhead form supported by
retail's Screenshot HUD RMB path. It is encoded from the current authoritative
minimap, committed through a temporary file, and cached immediately for loading
and Publish Map presentation. Project title lookup is repository-based; an
authored title is never treated as a filesystem path.

## Validation

Run the unit and protocol suite:

```powershell
ctest --test-dir out/build/native-dev -C RelWithDebInfo --output-on-failure
```

Run the real hidden-server bootstrap against an existing portable bundle:

```powershell
out/build/native-dev/src/RelWithDebInfo/aos_ugc_local_server_host_smoke.exe `
  G:/AoSRevival/BattleSpades/release-alpha8-final/BattleSpades-0.0.3-alpha.8-windows-x86_64 `
  G:/AoSRevival/BattleSpadesClient/assets/original
```

The smoke requires a ready ENet session, a decoded UGC world, mode `ugc`, the
Desert baseplate, the dedicated-safe role 2, and all three authored project
files before it passes.

## External boundary

Steam Workshop upload and friend invitation are external platform adapters,
not map-authoring protocol. The UI retains typed requests and fails visibly
when no adapter is installed; it never reports a false successful upload.
