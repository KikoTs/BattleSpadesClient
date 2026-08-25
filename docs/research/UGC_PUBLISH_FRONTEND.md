# UGC Publish Frontend Recovery

This note records the retail evidence used by the renderer-neutral Publish Map
implementation. Source coordinates are from an 800x600 bottom-left canvas;
the native presentation stores top-left design pixels.

## Recovered state flow

`aoslib/scenes/frontend/ugcPublishMenu.py` extends
`ListPreviewMenuBase` and creates four panel objects:

- `UGCMapsListPanel` for hosted local map files;
- `UGCMapPreviewPanel` for per-mode publish requirements and Delete;
- `UGCNameMapPanel` for the Workshop title and local preview PNG;
- `UGCPublishedMapsPanel`, an overwrite-selection panel.

The visible bytecode path is:

```text
Map List + Preview
  Preview (enabled only when at least one mode is publishable)
    → Name Map + Preview
      Publish
        → Workshop confirmation + license text
          Yes → asynchronous upload request
             success → Select Menu + Workshop page
             failure → error dialog

Back from Name Map → Map List
Back from Map List → UGC Select
```

Delete is a separate confirmation flow from the selected local row. A row UID
is passed to `delete_ugc_file`; it is not a display title.

`UGCPublishedMapsPanel` is instantiated and populated, and selecting one of
its rows changes the primary label to Overwrite. However, no instruction in
this recovered `UGCPublishMenu` path ever makes `PANEL_PUBLISHED_MAPS` visible;
the only explicit visibility writes keep it false. The native skeleton retains
Published and Changed Since Publish states on local rows but does not invent a
route to an unreachable panel. A later trace can add that route if another
retail caller proves it.

## Local-map classification

`UGCMapsListPanel.populate_list()` obtains hosted UGC filenames and derives:

1. no publishable modes → `CANNOT_BE_PUBLISHED`;
2. previously published and modified → `UGC_CHANGED_SINCE_PUBLISH`;
3. previously published and unchanged → `PUBLISHED`;
4. otherwise → `UNPUBLISHED`.

The first UID is selected automatically. Refresh attempts to preserve the
previous selected UID. `UGCMapPreviewPanel.populate_list()` emits one row per
eligible game mode, displaying `COMPLETED` for publishable modes or the
highest-priority missing-objective reason for blocked modes.

The native local repository adapter supplies those facts as
`UgcLocalMapRecord` and `UgcPublishModeStatus`; the frontend does not parse VXL
files or manufacture metadata.

## Recovered geometry

The outer 1172x921 frame is loaded at 0.64 and integer anchored as 750x589.
Panel positions come directly from `UGCPublishMenu.on_start()` and the panel
layout helpers:

| Element | Retail input | Native top-left rectangle |
|---|---:|---:|
| Large frame | centered `(400, 300)` | `(25, 5, 750, 589)` |
| Title | `(120, 525, 560, 50)` | `(120, 25, 560, 50)` |
| Large Back navbar | `(54, 27, 695, 32)` | Back hit `(54, 541, 78, 32)` |
| Map/Name panel | `(56, 505, 340, 413)` | `(56, 95, 340, 413)` |
| Map header | inset 10, height 40 | `(66, 105, 320, 40)` |
| First map row | height 26 | `(66, 155, 320, 26)` |
| Preview panel | `(401, 505, 340, 354)` | `(401, 95, 340, 354)` |
| Delete | `(411, 186, 80, 25)` | `(411, 414, 80, 25)` |
| Name edit | `(66, 415, 320, 30)` | `(66, 155, 320, 30)` |
| Name preview image | bottom `(66, 102)` | `(66, 195, 320, 303)` |
| Primary backing | centered `(571, 121)` | `(401, 452, 340, 54)` |
| Preview/Publish | `(405, 144, 332, 50)` | `(405, 456, 332, 50)` |

The DrawList layer contains only design-space commands so the shared retail
horizontal compositor can translate it over one stationary background.

## Side-effect and safety boundary

The model never calls Steam, opens a URL, deletes a file, or uploads bytes.
Instead it emits immutable typed effects:

- `UgcPublishRequest(local_uid, workshop_title)` only after confirmation;
- `UgcDeleteRequest(local_uid)` only after confirmation;
- Return-to-parent, panel, dialog, completion, and Workshop URL effects.

Repository rows are bounded to 4,096 and deduplicated by opaque UID. Empty and
malformed records are rejected. Titles use valid UTF-8 and the retail
200-code-point input limit. Dialogs block duplicate submissions, and upload or
delete callbacks are accepted only in the matching pending state. Delete
completion also verifies the opaque UID, preventing a stale callback from
removing a newly selected map.

An empty repository is a first-class screen: it renders an honest empty state,
keeps Back working, and disables Preview/Delete. Missing preview PNGs remain
missing and render an unavailable state; no sample map or image is substituted.

## Files and validation

- `include/battlespades/frontend/ugc_publish_menu.hpp`
- `src/frontend/ugc_publish_menu.cpp`
- `include/battlespades/frontend/ugc_publish_presentation.hpp`
- `src/frontend/ugc_publish_presentation.cpp`
- `tests/test_ugc_publish_menu.cpp`

Focused tests cover empty/local repositories, selection preservation,
transactional publish and delete flows, stale callbacks, UTF-8 limits,
scrolling, exact layout, both visible panel states, dialog composition, and
invalid presentation contexts. The standalone suite compiles with MSVC C++20
`/W4 /WX` and passes all nine cases.
