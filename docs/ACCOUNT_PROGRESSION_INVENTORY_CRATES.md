# Account Progression, Inventory, and Earned Crates

Status: implementation specification, not yet implemented  
Contract version: `progression-v1`  
Document copies: `G:/AoSRevival/BattleSpadesClient/docs/ACCOUNT_PROGRESSION_INVENTORY_CRATES.md`
and `G:/AoSRevival/aos_revival/docs/ACCOUNT_PROGRESSION_INVENTORY_CRATES.md`

This document is the shared contract between the native BattleSpades client and
the AoSPlay backend. The two copies must remain byte-for-byte identical while
the feature is being built. The dedicated BattleSpades game-server repository
is outside this documentation change and must not be modified as part of the
client/backend implementation.

## 1. Product goal

Add a durable, uncapped account level that makes normal play feel rewarding
without creating a paid economy or a competitive advantage. Every completed
level grants one free Supply Crate. Opening a crate costs nothing and awards a
permanent cosmetic item, primarily a KV6 weapon appearance or character skin.

The feature has three distinct layers:

1. **Retail mastery** remains exactly as recovered: individual statistic levels
   and Beginner/Intermediate/Advanced ranks for classes and modes.
2. **Account progression** is a new lifetime XP total and uncapped account
   level, computed only by AoSPlay from trusted match evidence.
3. **Cosmetic collection** contains earned crates, permanent inventory items,
   equipped appearances, and verifiable opening receipts.

These layers must never be conflated. Resetting or correcting a retail stat
must not silently remove account XP or cosmetics. Equipping a cosmetic must not
change damage, recoil, hitboxes, inventory capacity, movement, visibility, or
any other gameplay rule.

## 2. Non-negotiable principles

- There are no paid crates, paid keys, premium currency, cash-out, trading,
  wagering, or purchasable power.
- Opening a crate is always free. A level-up crate is an earned reward, not a
  sales funnel.
- Every functional base weapon and class tool remains available through the
  normal class/loadout rules. Crates unlock appearances, not stronger weapons.
- The client is untrusted. It cannot submit XP, grant a crate, choose a roll,
  claim ownership, or equip an unowned/incompatible item.
- AoSPlay is authoritative for XP, levels, crate instances, openings,
  inventory, and equipped cosmetics.
- Only active servers explicitly trusted for statistics may produce XP-bearing
  match events. Tutorial, Map Creator, debug labs, and unapproved private
  servers award zero XP.
- Every write is transactional, idempotent, bounded, audited, and safe to
  retry after a timeout.
- Published loot odds and duplicate-protection rules are visible before the
  player opens a crate.
- Content is never silently removed from an account. Disabled cosmetics remain
  owned; if unsafe, they render as the base model until repaired.
- Accessibility includes a Skip button, reduced-motion mode, no flashing
  requirement, and no fake near-miss manipulation.

## 3. Account level and XP curve

### 3.1 Level semantics

- A new account begins at **Level 1** with `0` lifetime XP.
- Level has no designed maximum. PostgreSQL `bigint` is the storage safety
  boundary, not a product cap.
- XP never decreases during normal play.
- Administrative corrections are append-only ledger events with an operator,
  reason, and before/after values. Direct counter edits are forbidden.
- All XP and level values cross JSON as decimal strings. JavaScript and the
  native client must not round a 64-bit value through a floating-point number.

### 3.2 Recommended `progression-v1` curve

XP required to advance from current level `L` is:

```text
xp_to_next(L) = min(1000 + 250 * (L - 1), 10000)
```

The cost grows gently through Level 37 and then remains 10,000 XP forever.
This keeps the system uncapped without turning later rewards into an
unreachable grind.

For a level `L`, let `n = L - 1` and `r = min(n, 36)`. Cumulative XP at the
start of the level is calculated without a loop:

```text
threshold(L) = 1000*r + 125*r*(r - 1) + 10000*(n - r)
```

Reference vectors:

| Level | XP at level start | XP to next |
| ---: | ---: | ---: |
| 1 | 0 | 1,000 |
| 2 | 1,000 | 1,250 |
| 5 | 5,500 | 2,000 |
| 10 | 18,000 | 3,250 |
| 25 | 93,000 | 7,000 |
| 37 | 193,500 | 10,000 |
| 50 | 323,500 | 10,000 |
| 100 | 823,500 | 10,000 |

Level lookup uses the closed form for the ramp and integer division after the
cap. It must not loop once per level. All intermediate arithmetic uses checked
128-bit arithmetic in C++ and PostgreSQL `numeric` or checked `bigint`
operations in the backend. Responses include:

- `level`
- `lifetime_xp`
- `level_start_xp`
- `next_level_xp`
- `xp_into_level`
- `xp_required_for_level`
- `progress_basis_points` (`0..10000`)

The backend is the source of the displayed level. The client may calculate the
bar locally for animation but must reconcile to the returned values.

### 3.3 XP award policy

The game server submits match evidence and existing stat deltas. It never
submits a final XP value. AoSPlay applies a versioned ruleset so two servers
cannot invent different rewards for the same activity.

Recommended `xp-rules-v1` input per player:

- durable AoSPlay legacy player ID resolved from a consumed join ticket or a
  validated Steam identity;
- active seconds, connected seconds, and AFK seconds;
- result: `win`, `draw`, `loss`, or `unfinished`;
- playlist ID, mode ID, map CRC, and match ID;
- human and bot opponent counts;
- the existing normalized statistic `[count_delta, score_delta]` map.

AoSPlay maps recovered score-reason IDs into combat, objective, support, and
useful-construction buckets. Unknown IDs contribute zero until added to a new
ruleset. Negative deltas never award XP.

Recommended formula before playlist multiplier:

```text
activity     = min(300, floor(active_seconds / 4))
completion   = 100 when completion eligibility is met, otherwise 0
result       = win:150, draw:100, loss:50, unfinished:0
combat       = min(500, verified_combat_score)
objective    = min(600, 2 * verified_objective_score)
support      = min(300, verified_support_score)
construction = min(200, verified_construction_score)

raw_xp = activity + completion + result + combat + objective + support
         + construction
awarded_xp = min(2000, floor(raw_xp * playlist_multiplier))
```

Completion eligibility requires meaningful activity: at least 180 active
seconds, or at least half of a legitimately shorter round. AFK time, spectator
time, warmup, team-selection time, and post-match time do not count.

Playlist multipliers are backend-owned allowlist data, never server-supplied:

| Playlist type | Default multiplier |
| --- | ---: |
| Approved official/competitive play | `1.00` |
| Approved bot-heavy or cooperative play | `0.60` |
| Approved private/custom play | `0.25` |
| Tutorial, editor, debug, untrusted server | `0.00` |

An approved official bot playlist may explicitly use `1.00`; the default
reduction only protects against unattended farming. There is no daily XP cap,
login streak, or expiring boost. Per-match bounds and trust policy stop abuse
without adding fear-of-missing-out mechanics.

### 3.4 Guests and account upgrades

Signed guest installations may earn XP, crates, and inventory because they are
durable AoSPlay accounts. Their leaderboard eligibility remains separate.
Upgrading a guest to username/password keeps the same account row, progression,
and inventory. The UI must warn that losing an unregistered installation key
can make a guest collection unrecoverable.

### 3.5 Launch migration

Historical retail totals were not collected under the new anti-farm rules and
must not be converted into arbitrary XP. At launch:

- every account starts global Level 1 at 0 XP;
- recovered retail stats and mastery ranks remain untouched;
- accounts with trusted pre-launch match history may receive one clearly
  labelled, non-random **Founder Supply Crate** through an audited migration;
- no historical account receives hundreds of retroactive level crates.

## 4. Level rewards and crate ownership

- Reaching each new level grants exactly one `level_supply_v1` crate.
- Level 1 is the starting state and grants no level crate. Reaching Level 2
  grants the first one.
- Crossing several levels in one accepted event grants one crate for every
  crossed boundary.
- The unique reward key is `(account_id, progression_track, reached_level)`.
  Retrying an event cannot grant the same level reward twice.
- Each grant creates a concrete crate UUID. A crate is not merely a counter.
- Crates do not expire and cannot be transferred.
- Every crate is pinned to a published loot-table version at grant time. Odds
  cannot be changed after the player earns it.
- Catalog assets referenced by a published table are retained indefinitely.

If a player owns every item in a crate's pinned table, opening returns
`catalog_exhausted` and leaves the crate unopened. If a pity floor has no
remaining unowned reward, the UI explains that the guarantee is banked; an
opening may use a lower available tier without resetting that pity counter.
The backend never consumes a crate while returning no item.

## 5. Cosmetic catalog

### 5.1 Initial cosmetic kinds

- `weapon_model`: a KV6 appearance bound to one base tool/weapon ID;
- `weapon_skin`: palette/material variation of a compatible weapon model;
- `character_skin`: class-compatible body appearance with team-color sockets;
- `tombstone`: cosmetic grave model;
- `profile_badge`: profile-only emblem.

Later kinds require a catalog-schema version. Gameplay items, ammo, damage
modifiers, movement modifiers, and larger hitboxes are not valid cosmetics.

### 5.2 Stable identity

Every item has an immutable, readable ID such as:

```text
cos.weapon_model.tool_12.riveted_rocket.v1
cos.weapon_skin.tool_5.arctic_camo.v1
cos.character_skin.medic.field_surgeon.v1
```

Renaming display text never changes this ID. Catalog records contain:

- cosmetic ID and catalog version;
- kind, rarity, display localization key, and description key;
- compatible tool IDs, class IDs, view type, and equip slot;
- KV6/texture/thumbnail paths and SHA-256 hashes;
- model pivot, scale, hand attachment, muzzle/ejection points, and bounds;
- team-color material/socket declarations;
- minimum client content version;
- enabled/disabled state and safe base-model fallback;
- authorship/license metadata.

### 5.3 Content validation

A catalog publishing tool must reject:

- missing, unhashable, or path-traversing assets;
- models outside configured dimensions or voxel-count budgets;
- NaN/invalid pivots, attachment points, scales, or transforms;
- a weapon model whose muzzle or hand contract is missing;
- skins that remove required team-color readability;
- executable content, scripts, shaders, or external URLs;
- duplicate IDs or an attempt to mutate an already published version.

Catalog manifests are signed and content-addressed. The client verifies the
manifest signature, file size, and SHA-256 before loading a cosmetic. Failure
falls back to the normal base model and never blocks joining a match.

### 5.4 Non-pay-to-win enforcement

Gameplay always resolves the base class/tool first. Cosmetics are a final
render-only substitution. Collision, damage, spread, recoil, cooldown, ammo,
projectiles, sounds used for gameplay awareness, and replication come from the
base catalog. A cosmetic may supply visual and local presentation assets, but
cannot override gameplay constants.

## 6. Rarity, odds, duplicate protection, and pity

Recommended launch rarity weights:

| Rarity | Color role | Nominal chance |
| --- | --- | ---: |
| Common | neutral | 55% |
| Uncommon | green | 27% |
| Rare | blue | 12% |
| Epic | purple | 5% |
| Legendary | gold | 1% |

Items are uniformly selected from the player's unowned eligible items inside
the chosen rarity unless the published table explicitly contains immutable
per-item weights. Effective odds may change as duplicate protection removes
owned items; the API exposes the current eligible count per rarity.

Bad-luck protection is per account and crate type:

- the 10th opening without Rare-or-better forces Rare-or-better;
- the 40th opening without Epic-or-better forces Epic-or-better;
- the 100th opening without Legendary forces Legendary;
- the strongest active guarantee is evaluated first;
- a counter resets only when an item at or above its tier is actually granted;
- unavailable guaranteed tiers remain banked rather than being silently reset.

The UI shows nominal odds, duplicate protection, current guarantee state, and
the exact pinned loot-table version before confirmation.

## 7. Auditable crate opening

The animation never determines the result. The backend commits the result in a
transaction first; the client animates the signed receipt afterwards.

### 7.1 Seed commitment

When a crate is granted:

1. Generate a cryptographically random 32-byte `server_seed`.
2. Store it encrypted with an AEAD key and key-version identifier.
3. Store
   `seed_commitment = SHA256("aos-crate-v1" || crate_uuid || server_seed)`.
4. Return the crate UUID, commitment, crate type, and pinned table version.

When opening, the client sends a fresh 32-byte `client_nonce` and an
idempotency UUID. The roll stream is:

```text
SHA256(
  "aos-crate-roll-v1" || server_seed || client_nonce || crate_uuid ||
  account_uuid || loot_table_version || pity_snapshot_hash
)
```

Use rejection sampling, not `% weight_total`, so integer modulo bias cannot
alter odds. Additional digest blocks use a domain-separated counter.

### 7.2 Atomic opening transaction

The backend must:

1. authenticate the account and rate-limit the request;
2. return the prior receipt if the idempotency UUID already exists;
3. lock the crate, account pity row, and inventory revision;
4. verify ownership, unopened state, table version, and catalog availability;
5. snapshot the eligible unowned pool in stable cosmetic-ID order;
6. determine the pity floor, rarity, and item from the committed roll;
7. insert the unique inventory ownership row;
8. update pity counters and inventory revision;
9. mark the crate opened and store an immutable opening receipt;
10. commit, then return the result.

The receipt includes the seed commitment, revealed seed, client nonce, digest,
algorithm version, pity before/after, candidate counts, selected rarity/item,
catalog version, and timestamps. A local verifier in the client can reproduce
the roll. Free rewards do not require a public blockchain; the commitment,
append-only audit, and deterministic verifier are sufficient and testable.

If the response is lost after commit, reopening or querying the crate returns
the same receipt. Double-clicking, reconnecting, and concurrent requests cannot
consume two crates or grant two items.

## 8. Inventory and equipment

- Ownership is unique per `(account_id, cosmetic_id)` at launch. No duplicate
  item stacks are created.
- Inventory is server-paginated and ordered by acquisition time plus UUID.
- Filters: All, Weapon Models, Weapon Skins, Character Skins, Tombstones, and
  Badges; additional filters include rarity, class, weapon, and equipped state.
- Equipment uses explicit slots such as `weapon:<tool_id>:view`,
  `weapon:<tool_id>:world`, `class:<class_id>:body`, `tombstone`, and
  `profile_badge`.
- Equip requests contain `expected_inventory_revision`. A stale client gets
  `409 revision_conflict`, refreshes, and never overwrites a newer choice.
- The backend verifies ownership, compatibility, enabled state, and minimum
  content version before equipping.
- Unequipping restores the retail/base appearance.
- Offline caches are read-only. A player may inspect cached items offline but
  cannot open a crate or change authoritative equipment without AoSPlay.

Trading, gifting, dismantling, crafting currency, marketplace listings, and
item deletion are explicitly out of scope for v1. They add fraud, support, and
economic complexity without improving the free progression loop.

## 9. Native client experience

### 9.1 Profile integration

The recovered profile screen keeps its four retail mastery tabs and adds a
clearly marked fifth **INVENTORY** tab as a Revival extension. The header gains:

- account Level;
- current XP/required XP;
- a progress bar;
- unopened crate count;
- equipped profile badge.

Do not replace or reinterpret the existing per-stat level bars and class/mode
ranks. The account level is additive.

### 9.2 Inventory screen

The Inventory tab contains:

- virtualized grid/list rendering so thousands of items remain smooth;
- rarity frame, localized name, compatible class/weapon, acquired date, and
  equipped marker;
- full 3D KV6 preview with rotation, zoom, team-color preview, and first-/third-
  person toggle where appropriate;
- Equip/Unequip and Preview actions;
- unopened crate shelf and opening history/receipt viewer;
- useful empty, loading, offline, outdated-content, and error states.

### 9.3 Crate-opening screen

The presentation may use a polished CS2-style horizontal reveal, but it must be
honest:

- show the crate, pinned loot table, rarity odds, pity state, and free price;
- require one confirmation, then disable duplicate input while the request is
  in flight;
- begin the final reveal only after receiving a committed receipt;
- place the actual awarded card under the marker deterministically;
- never synthesize fake high-rarity near misses to manipulate the player;
- support Skip, reduced motion, keyboard/controller operation, and readable
  rarity labels independent of color;
- if disconnected after commit, show **Reward recovered** and replay or skip the
  same receipt on reconnect;
- preload only bounded thumbnails and the selected KV6 model; no opening may
  hitch gameplay or block the render thread.

Audio, particles, lighting, and camera intensity scale by rarity but respect
master volume, effects volume, reduced motion, and photosensitivity settings.

### 9.4 Multiplayer appearance replication

Protocol 168 gameplay packets remain unchanged. Cosmetic replication is an
optional capability and must fail safely:

1. The authenticated client obtains its equipped snapshot from AoSPlay.
2. A future server-side identity bridge may publish durable player IDs and a
   cosmetic snapshot/version to capable Revival clients.
3. Alternatively, capable clients may batch-fetch public equipped cosmetics by
   durable player ID through AoSPlay.
4. Original retail clients and servers see the normal base models.
5. Missing identity, unsupported capability, stale catalog, hash failure, or
   network failure always resolves to the base model.

Do not overload an existing Protocol 168 field or infer identity from a mutable
nickname. A capability extension needs its own documented, bounded contract.

## 10. HTTP API contract

All private endpoints accept the existing launcher bearer session. The native
identity service owns the token; menu and renderer objects never receive it.
Responses use `schema_version: 1`, decimal strings for 64-bit counters, strict
body limits, and stable machine-readable errors.

### 10.1 Read endpoints

```text
GET /api/progression/me
GET /api/inventory/me?cursor=<opaque>&limit=50&kind=<optional>
GET /api/crates/me?cursor=<opaque>&limit=50&state=unopened
GET /api/crates/openings?cursor=<opaque>&limit=50
GET /api/cosmetics/catalog?version=<optional>
GET /api/cosmetics/equipped?player_id=<id>&player_id=<id>...
GET /api/crates/{crate_uuid}/odds
GET /api/crates/{crate_uuid}/receipt
```

`GET /api/progression/me` example:

```json
{
  "schema_version": 1,
  "track": "lifetime-v1",
  "level": "37",
  "lifetime_xp": "193500",
  "level_start_xp": "193500",
  "next_level_xp": "203500",
  "xp_into_level": "0",
  "xp_required_for_level": "10000",
  "progress_basis_points": 0,
  "unopened_crates": "36",
  "inventory_revision": "82"
}
```

### 10.2 Mutation endpoints

```text
POST /api/crates/{crate_uuid}/open
PUT  /api/inventory/equipped/{slot}
DELETE /api/inventory/equipped/{slot}
```

Opening request:

```json
{
  "schema_version": 1,
  "idempotency_key": "8b15b004-b96f-4afb-a7ed-fc9d52a49cc2",
  "client_nonce": "base64url-encoded-32-bytes"
}
```

Equip request:

```json
{
  "schema_version": 1,
  "cosmetic_id": "cos.weapon_model.tool_12.riveted_rocket.v1",
  "expected_inventory_revision": "82"
}
```

Mutation responses include the new inventory revision. Rate limits are per
account and IP, but legitimate idempotent retries return the stored response
rather than consuming another limit bucket.

### 10.3 Trusted match ingestion

Extend the existing `/api/master/stats` event additively. Old servers remain
compatible and receive no account XP unless they send the new validated match
evidence. The envelope keeps the existing `event_id` idempotency key:

```json
{
  "event_id": "server:match:round",
  "server_id": "registered-server-id",
  "recorded_at": "2026-07-31T12:00:00Z",
  "match": {
    "match_id": "server-generated-uuid",
    "playlist_id": "tdm-public-v1",
    "mode_id": 6,
    "map_crc": "686617696",
    "duration_seconds": 901,
    "ruleset_hash": "sha256-hex"
  },
  "players": [
    {
      "steamid": "1000000000",
      "name": "Player",
      "total": [1, 120],
      "stats": { "1": [8, 80] },
      "progression": {
        "active_seconds": 842,
        "connected_seconds": 901,
        "afk_seconds": 0,
        "result": "win",
        "human_opponents": 6,
        "bot_opponents": 0
      }
    }
  ]
}
```

AoSPlay verifies server trust, player identity participation, event bounds,
ruleset, duration, and stat plausibility. It calculates XP; a supplied `xp`,
`level`, `crate`, `rarity`, or `item_id` field is rejected.

## 11. PostgreSQL model

Implement this as additive migration `0004_account_progression_inventory.sql`
or the next unused migration number. Recommended tables:

### `aos_player_progression`

- `account_id uuid primary key references aos_accounts`
- `track varchar(32)` fixed to `lifetime-v1`
- `lifetime_xp bigint check >= 0`
- `cached_level bigint check >= 1`
- `ruleset_version varchar(32)`
- `inventory_revision bigint check >= 0`
- timestamps

### `aos_progression_events`

- immutable UUID/event sequence;
- unique `(source_event_id, account_id, track)`;
- server ID, match ID, ruleset, XP delta, verified breakdown JSON;
- before/after XP and before/after level;
- correction actor/reason when applicable;
- recorded and received timestamps.

### `aos_progression_reward_grants`

- grant UUID;
- unique `(account_id, track, reached_level, reward_kind)`;
- source progression event;
- crate UUID and timestamps.

### Catalog tables

- `aos_cosmetic_catalog_versions`
- `aos_cosmetic_items`
- `aos_loot_table_versions`
- `aos_loot_table_entries`

Published rows are immutable. New content creates a new version.

### `aos_player_crates`

- crate UUID, owner, type, source grant;
- pinned loot-table version;
- seed commitment and AEAD-encrypted seed/key version;
- `unopened|opened` state and timestamps;
- unique source grant.

### `aos_player_crate_pity`

- primary key `(account_id, crate_type)`;
- since-Rare, since-Epic, and since-Legendary counters;
- revision and timestamps.

### `aos_crate_openings`

- opening UUID;
- unique `(account_id, idempotency_key)` and unique crate UUID;
- immutable algorithm inputs and result;
- seed reveal, commitment, digest, pity snapshot, candidate counts;
- catalog/loot version and timestamps.

### `aos_player_inventory`

- ownership UUID;
- owner and cosmetic ID;
- source opening or audited grant;
- acquired timestamp;
- unique `(account_id, cosmetic_id)`.

### `aos_equipped_cosmetics`

- primary key `(account_id, equip_slot)`;
- owned cosmetic ID;
- inventory revision and timestamp.

### `aos_progression_outbox`

- transactional notification rows for level-ups, crate grants, openings, and
  equipment changes;
- delivery attempts are bounded; gameplay and API commits never wait for a
  websocket, email, analytics service, or other external consumer.

Foreign keys should use `ON DELETE CASCADE` for private account ownership and
`RESTRICT` for published catalog versions referenced by inventory/openings.
Public deletion/anonymization policy must preserve aggregate integrity without
retaining unnecessary personal data.

## 12. Transactional invariants

### XP event

Inside one database transaction:

1. insert/claim the source event idempotency key;
2. resolve and lock the active account/progression row;
3. validate match participation and compute XP from the pinned ruleset;
4. calculate old/new levels with checked integer math;
5. append the progression ledger row;
6. update cached progression;
7. insert one unique reward grant and crate for each crossed level;
8. append outbox notifications;
9. commit.

An event may cross at most a configured number of levels (recommended 25) and
award at most 2,000 XP. Exceeding either bound rejects the event and alerts
operations; it is never partially applied.

### Reconciliation

A periodic job recomputes lifetime XP from the ledger, recomputes the level,
checks one reward per crossed boundary, verifies every opened crate has exactly
one receipt and inventory source, and compares cached counters. It reports
drift and may repair only through an audited correction transaction.

## 13. Abuse and security controls

- Require a consumed join ticket or validated Steam identity associated with
  the reporting server and match participation window.
- Award XP only from active, non-banned accounts and active, verified,
  `stats_trusted` servers.
- Bind registered-server credentials to the existing observed-host and status
  checks.
- Reject duplicate match IDs, implausible duration, future/stale timestamps,
  impossible player counts, negative counters, unknown playlist/ruleset, and
  mismatched mode/map evidence.
- Bound body size, players per event, stat rows, XP, levels crossed, catalog
  candidates, page size, and concurrent openings.
- Use PostgreSQL row locks and unique constraints as the final concurrency
  authority; in-process locks are insufficient across web instances.
- Never log bearer tokens, crate seeds before reveal, session cookies, guest
  private keys, or raw Steam tickets.
- Encrypt unopened crate seeds with versioned AEAD keys. Key rotation keeps old
  decrypt keys until every associated crate is opened or migrated.
- Keep opening receipts append-only. Administrative item grants/revocations
  require a reason and appear in the existing admin audit log.
- Detect repeated farming pairs, impossible score/minute, unattended bot loops,
  collusive private servers, and large correction rates. Automatic detection
  flags review; it should not silently destroy inventory.
- Content delivery never accepts a filesystem path from an API caller. Resolve
  cosmetic IDs through the signed manifest only.

## 14. Failure behavior

| Failure | Required behavior |
| --- | --- |
| AoSPlay unavailable after a match | Trusted server queues the idempotent event with a bounded durable retry policy. |
| Duplicate stats event | Return the original accepted result; no second XP or crate. |
| Opening response lost | Query/retry returns the same receipt and item. |
| Client closes during animation | Reward remains committed; show it next login. |
| Catalog asset missing/corrupt | Retain ownership and render base model. |
| Loot table has no unowned item | Keep crate unopened; return `catalog_exhausted`. |
| Equip revision stale | Return `409`; client refreshes without overwriting. |
| Old client | Ignore new UI/API and use base models. |
| Old/original server | Normal Protocol 168 play; no unsupported cosmetic packet. |
| Seed key unavailable | Disable opening, retain unopened crates, alert operations. |
| Reconciliation drift | Freeze affected mutations, preserve evidence, repair through audited correction. |

## 15. Observability and operations

Metrics:

- accepted/duplicate/rejected XP events by reason and server;
- XP awarded by playlist, mode, server, and ruleset;
- level distribution and level-up rate;
- crates granted/opened/unopened and opening latency;
- rarity distribution versus expected distribution;
- pity activations and catalog-exhaustion count;
- inventory/equip errors, revision conflicts, and base-model fallbacks;
- reconciliation drift and outbox backlog.

Feature flags:

- `progression_read_enabled`
- `progression_awards_enabled`
- `crate_grants_enabled`
- `crate_opening_enabled`
- `cosmetic_equip_enabled`
- `cosmetic_replication_enabled`

Flags are backend-owned kill switches. Disabling them never deletes data.
Database backups and restore drills must include seeds, receipts, catalog
versions, inventory, and ledger rows together.

## 16. Test plan

### Deterministic/unit tests

- every XP threshold reference vector and exact boundary;
- huge-level lookup without iteration, overflow, or float conversion;
- XP bucket mapping and playlist multipliers for each recovered score reason;
- malformed/negative/unknown match evidence awards zero or is rejected;
- one, many, and zero crossed-level grants;
- deterministic RNG golden vectors and rejection sampling;
- rarity selection, pity priority/reset, duplicate protection, and exhausted
  tables;
- catalog manifest signatures, hashes, compatibility, model bounds, and path
  traversal rejection;
- all 64-bit JSON values remain exact decimal strings.

### Transaction/concurrency tests

- 100 concurrent deliveries of one match event create one ledger event and one
  set of crates;
- concurrent events for one account serialize without losing XP;
- 100 concurrent Open clicks consume one crate and grant one item;
- response loss and retry return byte-equivalent opening receipts;
- equip revision conflicts cannot overwrite another session;
- a crash at every transactional step rolls back cleanly;
- reconciliation detects deliberately injected drift.

### Statistical tests

Use fixed-seed offline simulations over millions of openings to verify rarity
frequencies, pity maximum droughts, item weighting, and absence of modulo bias.
Do not use flaky random assertions in the normal unit suite.

### Client tests

- profile level and XP bars at boundaries and huge values;
- all Inventory filters, pagination, preview, equip, empty/offline/error states;
- crate reveal, Skip, reduced motion, controller/keyboard, resize, and reconnect;
- corrupted/missing cosmetic falls back without a crash;
- base gameplay constants remain identical with every cosmetic equipped;
- original server/client compatibility and capable-client appearance fallback;
- no network request, file load, or KV6 parse blocks the render/gameplay thread.

### Security/load tests

- forged client XP, level, crate, rarity, ownership, and equip requests fail;
- untrusted/blocked/spoofed server events fail;
- replay, oversized body, enumeration, rate-limit, and IDOR tests;
- 50,000-item catalog and 10,000-item inventory pagination;
- level-up burst, catalog launch, and crate-opening traffic under production
  database connection limits.

## 17. Delivery sequence

1. **Contract freeze:** approve XP curve, award mapping, rarity odds, catalog
   schema, and no-pay/no-power rules.
2. **Database foundation:** additive migration, ledger, catalog, inventory,
   constraints, reconciliation command, and admin read-only views.
3. **Shadow XP:** compute XP for trusted events without persisting rewards;
   compare distributions and abuse signals for at least one week.
4. **Progression read path:** persist XP/levels, expose `/api/progression/me`,
   and render the account level beside untouched retail mastery.
5. **Crate grants:** create committed crate instances on level boundaries while
   opening remains disabled; reconcile counts.
6. **Catalog pipeline:** publish a small signed launch set across every rarity,
   validate KV6 transforms in first and third person, and retain base fallback.
7. **Opening API:** commitment, pity, duplicate protection, receipts,
   concurrency/load tests, verifier, and operational key rotation.
8. **Inventory UI:** fifth profile tab, collection preview, crate shelf,
   opening presentation, history, accessibility, and offline states.
9. **Local cosmetics:** equip and render for the owning client without changing
   gameplay or Protocol 168.
10. **Multiplayer cosmetics:** add a separately reviewed capability contract,
    public equipped snapshots, and original-client/server fallback.
11. **Gradual rollout:** staff, test accounts, 1%, 10%, 50%, 100%, with feature
    flags and distribution/reconciliation gates at each step.

Do not launch crate opening before the ledger, catalog immutability, duplicate
protection, receipt recovery, and reconciliation paths are proven.

## 18. Acceptance criteria

- A trusted match awards the same XP exactly once across retries and concurrent
  delivery.
- Account level remains correct at every boundary and has no designed cap.
- Each crossed level grants exactly one permanent, free crate.
- One crate can produce exactly one permanent cosmetic and one verifiable
  receipt, even under concurrency or disconnects.
- Published odds, pity state, and duplicate protection match the implementation.
- No cosmetic affects authoritative gameplay or lets a player bypass class,
  loadout, inventory, damage, movement, or visibility rules.
- A player can inspect, filter, preview, equip, unequip, and recover rewards
  without blocking the render thread.
- Missing assets, unsupported clients/servers, and backend outages fall back
  safely without losing ownership or crashing a match.
- Guests retain collection through account upgrade; registered accounts retain
  collection across devices.
- The feature contains no purchase, key, trade, cash-out, expiring reward, or
  pay-to-win path.

## 19. Decisions intentionally deferred

- player-to-player trading or gifting;
- crafting, duplicate currency, or dismantling;
- seasonal resets or paid battle passes;
- user-uploaded cosmetics;
- marketplace valuation;
- mobile/web crate opening;
- competitive prestige rewards beyond cosmetic badges.

Each would require its own threat model, abuse controls, migration, and product
approval. They must not be smuggled into the v1 schema or UI.
