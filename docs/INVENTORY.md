# Inventory and account integration

Profile's Inventory tab presents account level, owned cosmetics, sealed crates,
equipment and opening history. Retail statistic mastery, lifetime account XP
and cosmetic ownership are separate systems. The normative product contract is
[ACCOUNT_PROGRESSION_INVENTORY_CRATES.md](ACCOUNT_PROGRESSION_INVENTORY_CRATES.md);
its broader targets must not be read as proof that every rollout gate is closed.

## Runtime ownership

`src/frontend/inventory_menu.cpp` owns the menu model;
`inventory_session.cpp` owns asynchronous account requests and recovery;
`inventory_view.cpp` integrates RmlUi. The document/style are
`assets/client/ui/inventory.rml` and `inventory.rcss`. RmlUi 6.3 is pinned in
`src/CMakeLists.txt`; the existing native presentation is the fallback if its
document/runtime cannot load. This is a native UI, not a browser runtime.

The backend owns XP, crate grants, roll results, receipts and equipment. An
opening result is shown only after a committed receipt. Retry uses the same
operation key/nonce; a lost reply must not spend another crate. Equipment
mutations reconcile authoritative snapshots and stale revisions. Weapon
view/world equipment is one logical mutation, and a character change targets
the selected class without clearing other class choices.

Native receipt verification checks the backend commitment/draw against the
published catalogue. Historical pools stay immutable. Missing or invalid
cosmetic resources use the base model. The current content and capability-gated
remote appearances are documented in [COMMUNITY_COSMETICS.md](COMMUNITY_COSMETICS.md).

## Presentation and controls

Collection/crate cards display model thumbnails, ownership, rarity and original
author information. The inspector exposes class-specific equip controls and
only the sight/barrel options actually supported by the selected pack. Preview
assembly is asynchronous, uploads remain on the render thread, and model/part
hashes invalidate cached thumbnails. Selection and scrolling survive refreshes.

Thumbnail work prefetches the current and next page, prioritizes visible cards,
and uploads at most two completions per frame. Equipped models warm a bounded
verified-asset cache. Preview completions must match the current selection;
switching clears the previous hero while the replacement loads. Canceled
session results are discarded even when the account identity is unchanged.
Every request and retry pins the initiating account before its token is read.
Signing into another account during an uncertain equip response cannot retry
that mutation with the new account's credentials or cache its inventory under
the previous account. Returning from an empty/badge selection also clears the
pending hero key so selecting the same skin again requests a fresh preview.
Ordinary rejected mutations preserve a usable loaded snapshot and show the
error without forcing unrelated controls offline. See [Menu actions](MENU_ACTIONS.md)
for the complete control map.

The crate animation visualizes the committed receipt; it does not roll another
reward. Skip/Continue and Escape are supported. Reward overlays block background
actions. Four crate presentation models are UI assets, not additional rewards.
The 800x600 canvas and letterbox mapping must agree for input and rendering.

Edit RML/RCSS for Inventory layout; F11 affects the other native presentation
elements. Rebuild/stage resources and restart to load changes. Per-cosmetic
`skin-variants.json` stores local choice IDs; automatic aiming zoom is derived
from the authored sight or gameplay parent, not a manual zoom override.

## Hosted results and service boundaries

Native hosted matches retain credential-free result reports below the signed-in
identity state directory, in `hosted-results/<account-id>`. The client retries
captured reports after restart and removes them after acknowledgement. The
server also owns its durable result outbox. Preserve both across build cleanup;
a hard crash before capture is different from a queued report awaiting delivery.
Retry batches attempt at most 16 reports and examine at most 4,096 entries,
resuming at the next entry on the following pass. Rejected older reports stay
on disk without starving newer reports. Directory and bearer token are captured
together for one account; concurrent flushes do not duplicate a batch. A report
changed while its previous contents were being acknowledged is preserved.

Eligibility, reward rates, server/allocation trust, consumed-ticket proof and
feature flags belong to the backend. Local Tutorial/UGC and fixture menus do
not award account XP. Do not infer current production switches or migration
state from old deployment IDs. Confirm the intended backend's contract and
configuration before release or a live reward-bearing test.

## Validation

```powershell
ctest --preset native-dev -R 'inventory|cosmetic|weapon_variant' --output-on-failure
& .\out\build\native-dev\src\RelWithDebInfo\BattleSpadesClient.exe --debug-ui inventory --run-forever
```

Offline UI fixtures must not grant/open real crates. Test delayed requests,
lost opening/equip replies, stale revisions, receipt tampering, class-specific
equipment and missing-content fallback. GPU tests cover actual RmlUi/bgfx
textures and input; model tests alone do not prove the menu rendered correctly.
Live service, public hosting and multi-player acceptance are separate gates in
[ROADMAP.md](ROADMAP.md). See [RUNBOOK.md](RUNBOOK.md) for build/staging commands.
