# Community cosmetics

The bundled source of truth is `assets/client/cosmetics/catalog.json` and its
fixed-path, hash-verified resources. The inspected catalogue is `collection-v5`:
159 enabled appearances (90 weapon models, 58 character skins, nine props and
two tombstones), with 30 disabled historical items. Derive future counts from
the catalogue rather than a dated import report.

Original authors, terms, source archive hashes and provenance remain in
`CREDITS.json`, `AUDIO_CREDITS.json`, `AUDIO_MAPPING.json`, `IMPORT_REPORT.json`,
`credits/` and per-pack licence files. Keep those files. Importing a pack does
not change its author's terms or establish permission to redistribute it.

## Models and fallback

Equipment changes appearance only. Damage, ammunition, recoil, movement,
collision, hitboxes and authoritative tool IDs remain with the gameplay parent.
Unknown IDs, incompatible slots, missing files and failed content hashes fall
back to the base appearance. Remote responses cannot introduce arbitrary paths.

Multipart models, character arms, corpses/graves and supported world objects
use their maintained native assembly paths. The historical Bren equipment
mapping is translated to the Medic LMG at the client boundary by
`include/battlespades/network/cosmetic_slots.hpp`; do not rewrite immutable
catalogues/receipts to fix a presentation binding. Explicit current slots take
precedence over legacy aliases.

Some weapon packs use bounded AngelScript presentation through
`src/world/scripted_weapon.cpp`; they are not all static model replacements.
The runtime confines resource paths and exposes no filesystem, process or
network API. Resource/draw limits and execution budgets apply. Native weapons
retain their native handling. See [weapon presentation](WEAPON_SIGHTS_AND_SOUND.md).

## Inventory and crate versions

The current collection and immutable reward pools are separate concepts.
Sealed crates and receipts retain the catalogue/version that created them;
adding appearances does not silently replace old pools or award new ownership.
Disabled content remains an owned historical item and uses safe presentation
fallback. Client receipt parsing accepts supported historical versions.

The backend owns grants, opening, ownership and equipment. Native view/world
weapon slots form one logical equipment action; character selection addresses
the chosen class. See [INVENTORY.md](INVENTORY.md) and the
[progression contract](ACCOUNT_PROGRESSION_INVENTORY_CRATES.md). A local source
catalogue does not prove which service catalogue or feature switches are live.

## Multiplayer appearance envelope

The native client requests `battlespades-cosmetics-v1` on its HTTPS game ticket.
Only server-verified consumed ticket capability enables the appearance envelope;
legacy/Steam-only sessions do not implicitly opt in.

Capable, fully joined peers receive packet 240: bytes `F0 42 53 43 31` followed
by bounded UTF-8 JSON such as:

```json
{"player_id":7,"items":{"weapon:6:world":"community-lee-enfield-v2"}}
```

`items` replaces that player's appearance snapshot; an empty map clears it.
The envelope carries catalogue IDs, not scripts, paths or models. Unsupported
peers receive no envelope. Baseline gameplay IDs, loadouts and packet layouts
remain unchanged. See [server protocol](../../BattleSpades/docs/PROTOCOL.md).

The server polls equipped IDs outside simulation and retains the last verified
outfit on a failed lookup. Joining peers receive current state; departures and
slot reuse must not attach an old lookup to a new connection. Both a compatible
server and matching local content are required for native appearances.

## Maintenance and validation

Use `aos_inventory_tests`, inventory HTTP/session tests, weapon/class/entity
model tests, variant/script/image render tests and cosmetic replication probes.
Discover the configured targets with `ctest --preset native-dev -N`; use the
[runbook](RUNBOOK.md) for building/staging. `tools/test_cosmetic_replication_live.py`
accepts the native smoke executable and a complete portable server; it tests
capable and legacy ENet peers against a loopback service fixture.

The sibling website owns content intake and published reward catalogues. Its
expanded import pipeline includes `prepare-scripted-skins.mjs`,
`complete-skin-resources.mjs`, `tune-imported-skins.mjs`,
`import-expanded-collection.mjs` and `map-expanded-audio.mjs`. Check those
scripts and current manifests before regeneration; obsolete v2 generators
must not overwrite the expanded collection. Stage executable and resources
together. No current deployment or fresh runtime pass is inferred from the
historical import notes consolidated here.
