# Workshop browser and map subscriptions

**Map Creator > Browse Workshop** opens the native RmlUi browser. It uses
the inventory's existing bgfx renderer, RmlUi context and `inventory.rcss` game
skin (frame, heading, tabs and buttons). `workshop.rml` and `workshop.rcss` define
the browser's content layout; the normal menu backdrop remains visible. Steam Workshop,
AoSPlay and My Subscriptions are separate tabs. Search accepts map text or an
HTTPS Steam Workshop item link / decimal published-file ID. Enter submits the
search; Escape/Back closes the browser; downloads run on a cancellable worker.

The Steam tab supports Most popular (week/month/three months/year/all time),
Top rated, Most subscribed, Newest and Recently updated, plus game-mode tags.
These filters apply to Steam's whole catalog. Its supported 30-item page size
is preserved so paging never skips the remainder of a Steam page. Archive
ordering stays newest-first; Steam-specific filters are disabled on other tabs.
Selecting a map opens Screenshots and Map details in place, including dates,
size, modes, description and public subscriber/favorite counts.

Catalog metadata appears before images. Four cancellable media workers stream
PNG/JPEG thumbnails and fetch additional screenshots only for the selected map.
Changing pages or selection cancels obsolete media without waiting on the UI
thread. Metadata pages cache for 60 seconds (16 pages), gallery lists for five
minutes (64 maps), and normalized images on disk. Progress changes do not rebuild
the result grid or steal search focus. Unavailable images never block downloads.

Public Steam discovery reads item links from the public Steam Community browse
page, then obtains authoritative metadata and download URLs with Valve's
`ISteamRemoteStorage/GetPublishedFileDetails/v1` API. Steam's key-requiring
QueryFiles API is not used. If Steam changes the browse-page markup, direct
item-ID lookup still works. Public legacy `.aos` downloads need no Steam login;
private, banned, wrong-app and collection items are rejected. Items without a
public file URL report an error instead of trying to bypass Steam access.

AoSPlay catalog entries use `/api/workshop/items` and `/api/workshop/items/<uuid>`.
Downloads validate the archive's SHA-256 and length, or the Steam API's file
length, then parse VXL with the actual game parser and require a JSON sidecar.
Only supported HTTPS CDN hosts are fetched, without redirects or account
credentials. Assets install under `<asset root>/ugc/maps/` with the prefix
`Subscribed_Web_<source>_<id>`. The `.ugc` marker is published last, failed
writes restore the previous revision, and the Create Match map list refreshes.
These names cannot be owned or deleted by the separate Steam subscription sync.
Downloaded maps appear in **Create Match > Subscribed Maps**.

Subscribe/Unsubscribe use the signed-in AoSPlay identity, with up to 256 entries
per account and full 64-bit Steam IDs preserved as strings. Subscribing downloads
immediately; account sign-in and a five-minute timer check for updates. Manual
Sync Library retries failures. Unsubscribe keeps the local files; Remove Files
is enabled after unsubscribing and only removes files backed by a browser receipt.
Steam account subscriptions and AoSPlay subscriptions remain independent.

Backend rollout: deploy `aos_revival` migration `0018_workshop_subscriptions.sql`
and its subscription/item-detail routes together. Public Steam browse/download
does not require that deployment. Account sync and the AoSPlay detail endpoint do.

Checks: `aos_public_workshop_tests` covers IDs, URL boundaries and metadata;
`aos_workshop_view_tests` renders the screen and exercises search, escaping and
Inventory/Workshop transitions on Windows. For an opt-in real download into a
scratch directory, run `aos_public_workshop_tests --live 185279489 <scratch>`.

## Steam account subscriptions

Maps a player subscribes to on the Ace of Spades Workshop (app 224540)
download by themselves and show up under **Subscribed Maps** in Create Match.
Nothing needs clicking in the game for this existing Steam-managed sync.

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
