# Steam Workshop map subscriptions

Maps a player subscribes to on the Ace of Spades Workshop (app 224540)
download by themselves and show up under **Subscribed Maps** in Create Match.
Nothing needs clicking in the game. The Subscribe button still opens the
Workshop page.

## What retail did

Retail's native `shared.steam.pyd` (2012, decompiled from the copy in the
retail install) used the legacy `ISteamRemoteStorage` UGC API:

* A map was published as one cloud file, `<name>.aos`, plus a separate
  `<name>.png` preview (`sub_10017360`).
* The `.aos` holds chunks. Each chunk is a 4-byte NUL-terminated tag
  (`"VXL\0"` or `"UGC\0"`), then a 4-byte little-endian length, then that many
  bytes. The publisher writes VXL first (`sub_10003200`). The reader accepts
  either order and stops once it has both (`sub_10001DF0`).
* On download the reader wrote `%s/Subscribed_%llu.vxl` and
  `%s/Subscribed_%llu.ugc`, and the preview went to `%s/Subscribed_%llu.png`
  (`sub_100020C0`). `%llu` is the full 64-bit published file id. Retail's
  `SteamGetSubscribedContentList` printed only the low 32 bits
  (`Subscribed_%u`), which made no difference for 2012-era ids.
* An item counted as current when both files' modification times were at
  least the item's `m_rtimeUpdated` (`sub_10001D20`).
* The title in the map list is the `.ugc` sidecar's `"title"`.

## What the client does

`platform::WorkshopSyncService` (`src/platform/steam_workshop.cpp`) runs on its
own worker thread. It starts the first time the in-process Steam runtime is
attached, and runs again each time Create Match opens (at most once every 15 s).
Each pass:

1. Skips, with a log line, when Steam is not attached as 224540. That covers
   Steam not running, and the Spacewar (480) fallback for accounts that don't
   own the game: Spacewar has no Ace of Spades subscriptions.
2. Lists subscriptions with `ISteamUGC::GetSubscribedItems` and fetches
   details (title, `m_rtimeUpdated`, file and preview handles) with
   `CreateQueryUGCDetailsRequest`, 50 ids per page.
3. Compares them with the index (below): new items install, items with a newer
   `m_rtimeUpdated` or with missing files update, and items no longer
   subscribed are removed.
4. Downloads each item with `ISteamUGC::DownloadItem` and polls
   `GetItemState`/`GetItemDownloadInfo`. For a legacy item,
   `GetItemInstallInfo` returns the file itself:
   `steamapps\workshop\content\224540\<id>\<file handle>_legacy.bin`. If that
   path fails, it falls back to `ISteamRemoteStorage::UGCDownload` + `UGCRead`
   of the file handle, which is retail's own path. The preview always comes
   through `UGCDownload` of the preview handle.
5. Splits the `.aos` and writes into `<asset root>/ugc/maps/`:
   `Subscribed_<id>.vxl`, `.png` (only if it really is a PNG), `.txt`, and
   `.ugc` last. Each file goes to `.<name>.partial` first and is then renamed
   into place, so the scanner (which keys on `.ugc`) never sees half a map.
   `.txt` is a byte copy of the `.ugc`. The local server reads a map's
   metadata from it, Create Match won't host a custom map without
   `.vxl`/`.txt`/`.ugc`, and existing retail installs have the same four files.
6. Bumps a generation counter. If a Create Match screen is open, the frontend
   rescans and refreshes the map list.

The UI is never blocked. All Steam calls run on the worker thread, and async
calls are read with `SteamAPI_ManualDispatch_GetAPICallResult` once
`ISteamUtils::IsAPICallCompleted` says they are done. The runtime's pump only
collects results that someone registered, so these results wait in Steam until
the worker reads them. Progress such as "Syncing Workshop maps 2/5" is in
`WorkshopSyncStatus` and in the `[workshop]` log lines. There is no on-screen
indicator yet.

### The index

`ugc/maps/.battlespades_workshop_index` is a text file with one line per item:
`<id> <time_updated> <files...>`. The leading dot keeps it out of the
map-stem list. Only files named in the index are ever removed, and only names
of the form `Subscribed_<that id>.{vxl,ugc,png,txt}` that pass the path-safety
check. `Subscribed_*` files the sync did not write, such as a retail download
or a hand-copied map, are never touched. If such a copy of a subscribed item
is at least as new as Steam's version (retail's freshness test), it is left
alone and not adopted.

### Failure handling

* Steam not running, or not owning 224540: the pass is skipped and logged.
  When Steam appears later, the runtime's 30 s re-attach starts the sync.
* Offline, or the details query fails: the pass fails, and the next Create
  Match retries.
* An item that fails to download or parse: logged, the other items continue,
  and nothing is written for it. The next pass retries it because it is not
  in the index.
* An unwritable maps directory (for example an install under Program Files):
  logged per item.

## Verified against a real account (2026-10-01)

`aos_workshop_sync_probe` was run against the five subscriptions on Kiril's
account, with Steam running:

| id | title | Steam file | VXL | UGC | preview |
|---|---|---|---|---|---|
| 185279489 | Paintball | custommap_6.aos, 2,839,047 B | 2,835,632 | 3,399 | 470,326 |
| 187262961 | Battlefield 4 - BF4 - Siege of Shanghai | custommap_5.aos, 5,141,447 B | 5,125,788 | 15,643 | 177,320 |
| 184772195 | Post Apocalyptia | custommap_2.aos, 3,609,091 B | 3,597,072 | 12,003 | 389,665 |
| 183365919 | de_dust2 | de_dust2.aos, 2,253,379 B | 2,242,212 | 11,151 | 326,069 |
| 697436431 | Urban-1 | Custommap_2.aos, 2,814,420 B | 2,809,640 | 4,764 | 65,630 |

* All five are legacy items (state `legacy|installed` after download, `0`
  before). Each `.aos` is exactly VXL + UGC.
* The `ISteamUGC` path delivered all five. `--remote` (`UGCDownload`) also
  works.
* The Paintball and de_dust2 VXL sizes match the copies retail ships in
  `ugc/maps`.
* Running `--sync` a second time reports "Workshop maps are up to date"
  without downloading anything.

## Tools and tests

* `aos_workshop_sync_probe` lists subscriptions and their state. It downloads
  nothing by default.
  * `--download <dir>` downloads each item, keeps the raw `.aos` under
    `<dir>/raw`, and installs into `<dir>/maps`.
  * `--sync <dir>` runs the real service against `<dir>/maps`.
  * `--only <id>` limits the run to one item, and `--remote` prefers the
    legacy download path.
  * The probe needs `steam_api64.dll` beside it. Point it at a scratch
    directory, not the game.
* `aos_workshop_sync_tests` covers stem naming, container parsing (both chunk
  orders, truncation, unknown or unterminated tags, non-JSON sidecars), path
  safety, plan diffing (new, changed, missing files, removed, duplicates,
  no downgrade), index round-trip and hostile input, atomic install, and
  removal that cannot leave its own files.

Nothing in this feature subscribes or unsubscribes. `DownloadItem` lets Steam
install the item into its own `steamapps\workshop` folder, as any Workshop
game does.
