# Protocol 168 Untrusted-Client Hardening Specification

Status: implementation specification  
Audit date: 2026-07-30  
Audited server: `G:\AoSRevival\BattleSpades`  
Client implementation: `G:\AoSRevival\BattleSpadesClient`

The BattleSpades server repository was inspected read-only for this document.
It was not modified. The C++ client's offline developer-tool lock is a safety
and UX measure only; it is not a server security boundary. A modified client
can construct any packet byte sequence and the server must remain authoritative.

## 1. Security goals

The server must guarantee all of the following:

1. A connection can act only as the `Player` object bound to that connection.
2. A player can select only a tool in the normalized loadout committed for the
   current life, with the one server-owned mounted-machine-gun exception.
3. Class, team, tool, ammo, stock, prefab, palette, entity, terrain, damage,
   position, and objective state remain server-owned.
4. No malformed, stale, out-of-phase, or unauthorized packet is broadcast.
5. One abusive connection cannot delay simulation or drop another player's
   traffic.
6. Invalid input cannot make the retail client instantiate an illegal class,
   tool, entity, resource, or packet state.
7. Every packet has an explicit direction, connection phase, exact framing
   rule, rate limit, and authorization rule.

## 2. Current audited protections

The current server already has several strong boundaries which must be
preserved:

- `server/class_selection.py` normalizes client class/loadout fragments into
  one immutable `ClassSelection`. It removes cross-class, duplicate, disabled,
  and unknown tools and restores required defaults.
- `equipped_tool_authorized()` rejects a held-tool byte unless it is globally
  enabled, belongs to the current committed loadout, and is valid for the
  player's alive/spawned state. `MG_TOOL` is allowed only while attached to the
  corresponding live server-owned entity.
- `active_tool_authorized()` additionally requires the packet tool to match
  the currently held tool. Deployables and oriented projectiles use this gate.
- ClientData packet 4 currently calls `equipped_tool_authorized()` before
  changing `Player.tool`. Rejected tool updates are counted without attacker-
  controlled per-frame logging.
- `ReplicationService` sanitizes a tool/action snapshot before it reaches
  another client. This protects observers if internal state becomes invalid.
- Packets 13 and 78 normalize and stage class/loadout changes atomically rather
  than independently changing class and inventory.
- Shoot packet 6 does not trust its claimed shooter or damage. The connection's
  player and the server weapon catalog determine damage, cadence, ammunition,
  pellets, and hit resolution.
- Block, prefab, oriented-projectile, and most deployable paths validate
  alive/spawned state, held tool, server rules, range, and normalized class or
  loadout.
- Runtime deployable decoders use exact lengths, SetClassLoadout list counts
  are bounded, and LZF decompression has an output cap.
- Client-supplied `player_id` fields are generally ignored in favor of the
  player bound to the ENet connection.

These checks close the simplest “send ClientData with any weapon ID, then
shoot” path. They do not make the remaining action layer complete.

## 3. Priority findings

### P0: weapon switching can manufacture ammunition

`Player.set_tool()` calls `_reset_ammo()` whenever the selected raw weapon
differs from `Player.weapon`. Ammunition is stored as one shared
`ammo_clip`/`ammo_reserve` pair instead of per weapon.

A player with two legitimate weapons can therefore:

1. spend ammunition on weapon A;
2. select legitimate weapon B;
3. select legitimate weapon A again;
4. receive weapon A's full default ammunition.

This does not require an out-of-loadout tool. The selection gate correctly
accepts both weapons, but the state transition grants inventory.

Required change:

- Add a per-life `WeaponAmmoState` ledger keyed by raw tool ID.
- Populate the ledger only during spawn/restock or an authoritative pickup.
- A tool switch saves the outgoing weapon state and restores the incoming
  weapon state; it never calls a stock initializer.
- Reload and firing mutate only the current ledger entry.
- Class/life transition discards the old ledger and creates the normalized new
  one exactly once.
- Add a regression that repeatedly switches A/B after firing and proves total
  ammunition never increases.

### P0: action handlers do not all re-authorize the active tool

ClientData selection is protected, but `CombatSystem.handle_shot()` and
`handle_weapon_reload()` rely on the mutable `Player.tool` and generic
`is_weapon_tool()` checks. `handle_block_destroy()` similarly accepts a
globally enabled current tool without proving it remains in the committed
loadout.

The input boundary must not be the only protection. A stale transition, mode
hook, plugin, bot gateway, or future handler can corrupt `Player.tool` without
going through ClientData.

Required change:

- Every gameplay action calls `active_tool_authorized(player, player.tool)` or
  a stricter action-specific gate immediately before mutation.
- Shoot, reload, melee, block build/destroy/paint, palette, prefab, pickup use,
  deployable use, and entity mounting all share this rule.
- A failed action performs no ammo, stock, cooldown, terrain, entity, disguise,
  or replication mutation.
- The outbound replication sanitizer remains as independent crash containment.

### P0: pre-join class selection bypasses active class/mode authorization

Pre-join packet 13 is normalized and cached, but the join path does not apply
the same `is_class_enabled()` and `mode.allows_class_selection()` checks used
by the post-join packet 13/78 handlers. `prepare_join_selection()` is optional
and is not a general authorization boundary.

Required change:

- Introduce one `authorize_class_selection(context, selection)` service used by
  pre-join cache application, live menu changes, respawn, bots, and modes.
- Run the final authorization after team/mode identity is known and before
  `Player.apply_class_selection()`, movement profile creation, restock, spawn,
  or `CreatePlayer`.
- A forbidden class resolves to the mode/team default normalized selection.
  It must never retain any tool from the rejected selection.
- Tests must cover every disabled class and every mode-locked class through
  both packet orders and the pre-join path.

### P1: packet framing and phase are implicit

`PacketHandler` selects a loader and catches decoder exceptions, but there is
no central policy for exact payload size, legal connection phase, direction,
or rate. Some runtime decoders accept trailing bytes: ClientData accepts
payloads longer than its known layouts and PositionData accepts `>= 12`.
Generated loaders may also leave unread bytes.

Required change:

```python
@dataclass(frozen=True)
class ClientPacketPolicy:
    packet_id: int
    phases: frozenset[ConnectionPhase]
    exact_sizes: tuple[int, ...] = ()
    max_size: int = 0
    rate_per_second: float = 0.0
    burst: int = 0
    reliable_required: bool | None = None
    handler: Callable[..., Awaitable[None]] | None = None
```

- Reject a server-to-client packet ID received from a client.
- Reject a gameplay packet before `IN_GAME`, and reject handshake packets after
  joining unless the transition policy explicitly permits them.
- Require a decoder to consume the complete payload. Known variants are an
  exact-size set, not a minimum.
- Cap the outer datagram before decompression and cap decompressed gameplay
  input to the largest declared client packet. Map-size server payload limits
  must not be reused for client gameplay traffic.
- Do not continue a handshake after a malformed Steam ticket merely because
  parsing failed.

### P1: the network queue is global and unfair

The server currently appends all joined traffic to one global deque capped by
`max_pending_packets`. One peer can fill that queue and cause packets from
every other player to be dropped.

Required change:

- Maintain a small bounded queue per connection.
- Drain connections round-robin with both a global tick budget and a per-peer
  budget.
- Coalesce replaceable state packets such as ClientData and PositionData by
  connection while never coalescing actions such as Shoot or BlockLine.
- Apply token buckets per packet family.
- Disconnect a peer after sustained overflow; never drop another peer's packet
  because one queue is full.

### P1: projectile kinematics trust too much client geometry

Oriented actions authorize the tool, stock, and cadence, but accept any finite
position/velocity within very broad numeric limits in the downstream grenade
spawn path. Fuse handling is generic rather than tool-specific.

Required change:

- Validate spawn origin against the authoritative eye/tool muzzle with a small
  latency-aware envelope.
- Derive or clamp speed from the tool's throw/launch profile and validated cook
  duration.
- Validate direction against the most recent accepted orientation.
- Use tool-specific fuse minima/maxima and reject impossible negative,
  non-finite, or overcooked values.
- Perform the final line-of-fire/clearance check server-side.

### P1: deployable placement lacks one atomic stock transaction

Authorization is centralized, but stock, cooldown, support, occupancy, and
owner limits are inconsistent between deployables. C4 and disguise have
explicit stock; several other deployables can be created without a common
per-life wallet transaction.

Required change:

1. Decode primitives.
2. Authorize life, class, loadout, held tool, mode, and rule.
3. Validate face, support, occupancy, bounds, distance, and owner/team limits.
4. Reserve one unit from a server-owned per-life stock wallet.
5. Create the entity.
6. Commit stock and cooldown.
7. Replicate.

If creation or replication preparation fails, release the reservation. No
handler may create an entity first and discover empty stock afterward.

### P1: malformed input can cause log and task amplification

Packet decode errors currently log a traceback while leaving the connection
alive. Pre-join packets are scheduled as independent tasks, and several social
messages have no explicit string/rate policy.

Required change:

- Keep a bounded `ConnectionSecurityState` with malformed, unauthorized,
  phase, rate, and stale counters.
- Log a compact aggregate at most once per second per connection. Never print
  one traceback per attacker packet.
- Limit pre-join outstanding work to one ordered state machine.
- Enforce UTF-8/retail encoding policy, normalized chat type, character limit,
  and chat/command/vote token buckets.
- Disconnect on repeated malformed framing or impossible state.

### P2: production diagnostic packet IDs must be unreachable

Handlers 241–243 are registered unconditionally even though diagnostics are
disabled by default and their current manager interface does not expose the
registered callback names.

Required change:

- Do not register custom diagnostic packet IDs in a production protocol table.
- If a diagnostic build needs them, require all of: development build,
  explicit config, loopback/private admin authorization, bounded payload, and
  rate limit.
- Never broadcast diagnostic state to retail clients.

## 4. Required packet policy

The exact byte sizes below must be populated from the recovered packet classes
and captures; no handler may infer permission merely because decoding worked.

| IDs | Family | Legal phase | Required authorization |
|---|---|---|---|
| 105 | Steam ticket | `CONNECTED` only | Exact framing, valid configured authentication result |
| 13, 15 | Initial class/join | `AUTHENTICATED`/`LOADING` only | Final normalized class, team, mode, name, loadout, prefab and UGC authorization |
| 0 | Clock sync | Declared handshake and in-game phases | Exact size and low rate; echo only the bound connection |
| 4, 116 | Input/position | `IN_GAME` only | Bound player, live life token, finite normalized orientation, tool selection gate, coalesced rate |
| 6, 76 | Shoot/reload | `IN_GAME` only | Active committed tool, ammo ledger, cadence, origin/orientation, alive/spawned/not transitioning |
| 7, 11 | Paint/palette | `IN_GAME` only | Active palette-capable committed tool and rule; authoritative target/color constraints |
| 10 | Oriented projectile | `IN_GAME` only | Active tool, stock, cadence, origin/speed/fuse envelope |
| 30, 31 | Prefab build/erase | `IN_GAME` only | Active prefab tool, selected prefab allowlist, block wallet, map safety and bounded operation |
| 32, 35, 40 | Block mutation | `IN_GAME` only | Active block/dig tool, stock/cadence, target reach, complete atomic cell set |
| 47–49 | Vote/chat | `IN_GAME` only | Muting/admin/vote state, normalized type/string, per-peer rate |
| 71 | Drop pickup | `IN_GAME` only | Bound carrier owns exact server pickup; physical position/velocity envelope |
| 77, 78 | Team/class | `IN_GAME` or explicit menu phase | Server rules, mode locks, atomic life transition, rate limit |
| 86–95, 104 | Deployables/entities | `IN_GAME` only | Active class/loadout/tool, per-life stock, cadence, support, owner/team limit |
| 12, 51, 97, 99, 100, 102, 118 | UGC | `UGC_EDITOR` only | UGC runtime enabled, authenticated host/editor role, exact tool and bounded content |
| 110 | Client menu state | Joined transition states only | It can acknowledge a server-armed transition; it cannot authorize a transition itself |
| 241–243 | Diagnostics | Never in production | Development-only authenticated side channel |
| all server-only IDs | Server state | Never from client | Reject and score; never route or rebroadcast |

## 5. Authoritative state model

Each connection should own:

```text
ConnectionSecurityState
  connection_phase
  bound_player_identity
  map_epoch
  mode_epoch
  life_generation
  last_accepted_input_loop
  per_family_token_buckets
  per_family_queue_depths
  violation_score
  compact_violation_counters
```

Each spawned player life should own:

```text
CommittedLifeEquipment
  class_id
  normalized_loadout
  normalized_prefabs
  normalized_ugc_tools
  selected_tool
  ammo_by_weapon
  stock_by_tool
  cooldown_by_tool
  life_generation
```

Packets never replace either record. They request transitions which the server
validates and commits.

The following invariants are mandatory:

- A packet's player ID is descriptive only. The ENet connection selects the
  acting player.
- `selected_tool` is either in `normalized_loadout` or is a verified
  server-owned mounted entity state.
- Tool selection never initializes ammo or stock.
- Client-reported ammo, damage, class inventory, entity type, block cost, and
  hit identity are ignored.
- Dead, unspawned, spectator, in-menu, loading, class-changing, and map-changing
  players cannot perform gameplay actions.
- Delayed packets from an old map/mode/life cannot act on a new life even when
  it happens to use the same tool ID.
- Unknown or invalid state is sanitized before serialization to any retail
  client.

## 6. Rejection behavior

| Condition | Server response |
|---|---|
| Benign stale action or exhausted stock | Silent reject; optionally send one authoritative owner repair |
| Unauthorized tool/class/entity/action | Reject without mutation or broadcast; increment compact counter |
| Malformed size, unread/trailing bytes, non-finite primitive | Reject and add violation score |
| Wrong direction or connection phase | Reject and add a stronger violation score |
| Repeated malformed/rate abuse | Disconnect with a stable data/protocol error |
| Internal invalid tool/entity state | Quarantine the action, sanitize replication, emit one bounded server diagnostic |

Owner repair packets must contain only canonical server state. They must be
rate-limited so a malicious client cannot turn rejection into amplification.

## 7. Required tests

### Equipment and ammunition

- For every class and normalized loadout, try every tool byte `0..255` in
  ClientData. Only committed tools are accepted.
- Try the same matrix while dead, spectator, loading, changing class, and after
  a life-generation change.
- Fire weapon A, switch A/B hundreds of times, and prove ammo never increases.
- Forge `Player.tool` internally and prove shoot, reload, block, palette,
  deployable, and replication boundaries independently reject/sanitize it.
- Confirm mounted MG works only while attached to its exact live entity.

### Class and join

- Test packet 13 then 78, 78 then 13, duplicate packets, missing slots,
  cross-class tools, disabled tools/classes, mode-locked classes, malformed
  counts, and delayed old-life selection.
- Run the same cases before packet 15 and after joining.
- Assert `CreatePlayer` contains only the final committed selection.

### Framing and fuzzing

- For every client packet, test empty, truncated, exact, trailing-byte, maximum,
  and oversized payloads.
- Fuzz packet IDs, list counts, strings, floats, fixed-point fields, LZF streams,
  and direction/phase transitions with deterministic seeds.
- Assert no decoder exception escapes, no unbounded traceback is produced, no
  invalid packet is broadcast, and memory remains bounded.

### Actions

- Projectiles: origin, speed, direction and fuse just inside/outside each
  tool-specific envelope.
- Deployables: empty stock, cooldown, unsupported face, occupied cell, range,
  owner/team cap, death/class change during reservation, and creation failure.
- Terrain/prefabs: atomic rejection, reach, block wallet, maximum cell count,
  map edge, protected zones, and stale map epoch.
- Social/admin/UGC: role, phase, string limit, rate limit, and production
  rejection of diagnostics.

### Queue and retail safety

- Flood one peer while a second sends ordinary 60 Hz movement and actions.
  The second peer must not lose gameplay packets or exceed the tick budget.
- Reconnect, respawn, class change, map change, and round restart while stale
  action packets remain queued.
- Observe with two clean retail clients and the C++ client. No invalid class,
  tool, entity, animation, terrain, or palette state may be replicated and no
  client crash may occur.

## 8. Migration order

1. Add characterization tests for current packet policy and the ammunition
   switch exploit.
2. Implement per-weapon ammo state and remove all ammo initialization from
   `set_tool()`.
3. Add action-level `active_tool_authorized()` checks to shoot, reload, block,
   palette, and remaining paths.
4. Unify pre-join and post-join class authorization.
5. Introduce the declarative packet policy with exact framing and phases.
6. Replace the global packet deque with fair per-peer queues and token buckets.
7. Make deployable/projectile actions atomic and tool-profile constrained.
8. Add bounded violation accounting, fuzzing, flood tests, and retail soak.

The first four stages should be completed before treating public servers as
safe from modified-client equipment abuse.

## 9. Acceptance criteria

- No client packet can select, fire, reload, display, place, or replicate a
  tool outside the current committed life.
- Switching tools cannot create ammunition, stock, blocks, or cooldown resets.
- Disabled/mode-locked classes cannot enter through pre-join packet ordering.
- All known client packets have exact framing, direction, phase, rate, and
  authorization policies.
- One abusive peer cannot cause another peer's queue to drop.
- Invalid input produces no partial world mutation and no packet to observers.
- Production diagnostics are unreachable from gameplay transport.
- Malformed-packet fuzzing and a two-client retail soak produce no crash,
  unbounded logs, gameplay-thread blocking, or memory growth.

