# Protocol 168 in-game HUD authority matrix

Status: audited against the recovered retail Python/Cython code, the protocol
layout, and the current native-client handlers on 2026-08-04.

This document distinguishes three different concepts that are easy to collapse
into the phrase "server authoritative":

- **Direct server state**: the server sends the displayed value or the event
  that sets it.
- **Server-owned, client-mirrored state**: the server decides whether actions
  are legal, but the stock wire protocol does not continuously send the number.
  The client maintains the same deterministic counter and reconciles it at
  server-confirmed edges.
- **Presentation state**: the client derives animation, geometry, formatting,
  and short-lived feedback from authoritative state/events.

The original `G:/AoSRevival/BattleSpades` server is a read-only compatibility
target. All compatibility work described here is implemented in this client.

## Result at a glance

Most competitive HUD facts are server-owned, but **ammo and block stock are not
literal fields in `WorldUpdate(2)`**. Protocol 168 sends HP, selected tool,
movement/action/state bits, pickup, jetpack fuel, and other world state, but no
generic current-magazine, reserve-ammo, or remaining-block fields. Retail keeps
those counters locally from the server-approved loadout and action stream.

| HUD value | Authority model | Wire source / transition | Native-client behavior |
| --- | --- | --- | --- |
| Current health | Direct server state | `WorldUpdate(2).health`; immediate `SetHP(5)` | Both paths replace the HUD value. `SetHP` also derives directional damage presentation from its source coordinates. |
| Maximum health / health-bar scale | Hybrid rule state | Class/loadout/life boundary from `CreatePlayer(28)` or `SetClassLoadout(13)`; mode/class constants | Current HP is direct. The scale and colour thresholds are derived from the active server-selected class/rules. It must not be hardcoded to 100 for zombies or other high-HP classes. |
| Selected tool | Direct server state with local input prediction | Client requests a tool in `ClientData(4)`; owner `WorldUpdate(2).tool_id` acknowledges it | The local switch animates immediately, then the acknowledged server byte wins. Retail's owner sentinel `0xff` means "keep local tool", not tool 255. |
| Class, loadout, prefabs, UGC tools | Direct server state | `CreatePlayer(28)` and transactional `SetClassLoadout(13)` | Replaces the local inventory as one selection. Icons, hands, ammo definitions, prefab choices, and spawn tool are rebuilt from it. |
| Magazine and reserve ammo | Server-owned, client-mirrored | Initial counts from the selected tool definition; local accepted fire; `WeaponReload(76)` edges; `Restock(69)` | No generic packet carries the two counters. The client predicts legal shots/reloads using the same weapon constants. Type 0 is a full spawn/general reset; type 3 is retail's per-tool partial ammo-crate top-up. |
| Count-based gadgets/explosives | Server-owned, client-mirrored | Loadout definition; oriented/deployable action; reload/restock where applicable | Displayed as tool ammo and spent deterministically on a legal local use. Server feedback/creation remains authoritative for the world object. |
| Reload state and completion | Direct event plus local prediction | Bidirectional `WeaponReload(76)`, `is_done=false/true` | Client may start the animation on input, but the replicated start/end event controls observers and the commit edge. |
| Remaining blocks | Server-owned, client-mirrored | Starting class/tool stock; server-echoed terrain build packets; `Restock(69)` type 5; team infinite-block rule | Ordinary placement is debited only from the authoritative echoed terrain mutation. This avoids spending blocks for a rejected build. There is no generic block-wallet field in `WorldUpdate`. |
| Infinite blocks | Direct server rule | Initial `StateData(45)` team flag; live `TeamInfiniteBlocks(82)` | Suppresses local affordability gates and wallet debits. The finite numeric wallet is left intact instead of inventing a replacement number. |
| Prefab cost and affordability | Hybrid | Prefab selected by server loadout; current mirrored block wallet; server-accepted prefab/terrain mutation | The cost and shape are asset/catalog facts. Affordability uses the local mirror of server-owned stock. Actual world placement comes only from the authoritative mutation. |
| Held block colour / palette selection | Direct server acceptance plus local UI | `SetColor(11)` for the accepted player colour; InitialInfo colour-picker/palette rules | The palette may preview locally, but the echoed server colour replaces the held-block material and selection. |
| Palette availability | Direct server rule | `InitialInfo` `enable_colour_picker` / `enable_colour_palette` | Server decides whether the picker/palette UI exists. Palette layout and nearest-swatch highlighting are client presentation. |
| Jetpack fuel | Direct server state | `WorldUpdate(2).jetpack_fuel` | The gauge value is copied from the local player's row. Visibility is derived from the server-selected loadout/class and active movement state. |
| Parachute, disguise, wading and action state | Direct server state | `WorldUpdate(2).state_flags` and `.action_flags`; dedicated action packets where present | Server bits drive replicated state. Animation, material swapping, and HUD visibility are local rendering. |
| Pickup currently carried | Direct server state | `WorldUpdate(2).pickup_id` (`0xff` means none) | Drives carried-object/tool presentation and mode HUD. |
| Player score | Direct server state | Initial score state plus `SetScore(85)` player update | The local score widget and scoreboard use the received value. The reason byte feeds score messaging; it does not alter the numeric value itself. |
| Team scores and score limit | Direct server state/rules | `StateData(45)` initial scores/limit; `SetScore(85)` team update | No locally inferred score. Scoreboard formatting respects the server's show-score/show-maximum flags. |
| Top-bar team values (`HeadCount`) | Direct server policy plus roster state | `StateData(45).team_headcount_type`, team scores, and authoritative roster | Type 0 displays player count, type 3 hides the value, and all other values display score. Type 2 chooses retail's four-digit layout. |
| Team HUD visibility/locks | Direct server rule | `StateData(45)` flag byte; live packets `LockTeam(79)`, `TeamLockClass(80)`, `TeamLockScore(81)`, `TeamInfiniteBlocks(82)`, `TeamMapVisibility(83)` | The client retains all flags instead of discarding them. Team/menu/score/minimap presentation reacts without requiring server changes. |
| Match clock | Direct server seed, client countdown | `DisplayCountdown(84).timer`; round/end events | The received time replaces the clock. Smooth decrement between packets is presentation only and cannot invent a round transition. |
| Team progress bars | Direct server state | `TeamProgress(117)` | Visibility, percent/fraction mode, numerator/denominator, particle flag, previous-value flag, and icon are packet-owned. Tweening and bar geometry are client presentation. |
| Territory-control bases | Direct server state | `TerritoryBaseState(106)` | Ownership, attacker, capture amount, and action are retained from packets. Pulse/overlay animation is derived locally. |
| CTF/VIP/other objective markers | Direct server/world state | `CreateEntity(21)`, `DestroyEntity(19)`, entity rows in `WorldUpdate(2)`, mode packets and minimap packets | Position, carrier/team, and lifetime are authoritative. Icon selection, arrows, pulsing, and clipping are presentation. |
| Minimap terrain | Direct server world state | Initial/full `MapSync`; subsequent terrain mutation packets | The texture is a client render of the authoritative voxel replica. It must be rebuilt/reset at the map epoch and updated after each accepted mutation. |
| Minimap player visibility | Direct server rules/state | InitialInfo exposure rules, `StateData(45)` team visibility, `TeamMapVisibility(83)`, `ChangePlayer(17)` visibility state, and `WorldUpdate(2)` positions | Marker position is replicated state. Rotation, crop transform, height arrows, and colours are presentation. |
| Crosshair spread/recoil geometry | Presentation derived from server-selected weapon | Tool/loadout plus local firing/movement/recoil state; hit confirmation is `ShootResponse(9)` | The server does not send pixel radius. The client derives it from the active weapon and predicted action state. A confirmed hit turns on the hit marker/sound. |
| Damage direction indicator | Presentation derived from direct damage | `SetHP(5)` source coordinates and damage type | The server supplies the hit source. The client converts it into a screen-relative angle; fall/environmental damage without a valid source must not fabricate a direction. |
| Kill feed and multikill presentation | Direct events plus local lifetime | `KillAction` and score/event packets | Names, killer/victim, weapon/reason and respawn facts come from events. Fade timing, text placement, and local multikill window are presentation and must reset at the proper life boundary. |
| Death, respawn and spectator HUD | Direct lifecycle state plus local camera | `KillAction` respawn time, HP/life state, `CreatePlayer(28)`, grave/entity state, InitialInfo deathcam/spectator rules | Countdown and eligibility are server-owned. Grave/chase camera interpolation and prompt layout are local, but cannot resurrect or switch targets illegally. |
| Chat, help, vote, broadcast and end screen | Direct server messages/state | Their dedicated packet families, `StateData.has_map_ended`, map/end packets, stats/rank packets | Text/templates/candidates/results are server data. Localization, wrapping, animation, and safe decoding are client presentation. |

## Corrections made during this audit

1. `Restock(69)` type 0 and type 3 are no longer treated as the same full
   refill. Type 3 now performs retail's partial per-tool ammo-crate top-up.
2. `StateData(45)` no longer drops `team_headcount_type` or the seven relevant
   per-team rule/visibility bits.
3. The top HUD resolves `HeadCount` according to the server-selected type rather
   than always treating it as score.
4. Scoreboard team values obey `show_score` and `show_max_score` independently.
5. Initial and live enemy-minimap visibility obey `StateData` and packet 83.
6. Packets 79-83 now have strict canonical decoders and update retained client
   state.
7. Infinite-block teams no longer fail local placement gates or lose blocks
   from their mirrored wallet.

## Known limits and rules for future work

- Do not add a new "ammo correction" or "block count" packet to make the HUD
  easier. A stock-compatible client has to reproduce the retail deterministic
  mirror.
- A server-rejected local action must not permanently spend a resource. Prefer
  committing ordinary block costs from echoed terrain mutations. For weapon
  ammo, keep validation, cadence, reload, and restock constants exactly aligned
  with retail because the protocol has no generic rollback number.
- The selected tool's owner `WorldUpdate` byte can be `0xff`; preserve the
  predicted local selection in that case.
- `locked_score` is retained and live-updated. Its full retail menu/game-mode
  effect is separate from basic score visibility and still merits a dedicated
  UI behavior audit.
- Jetpack **fuel value** is directly authoritative. The exact rule deciding
  whether every legacy jetpack variant shows the gauge should continue to be
  checked per server-selected loadout.
- Visual geometry, animation, interpolation, audio, particles, and localization
  can be wrong even when the underlying value is authoritative. Packet parity
  and presentation parity are separate test obligations.

## Verification

Focused native tests cover:

- `StateData(45)` headcount and team flag decoding;
- strict packet 79-83 decoding, invalid booleans/teams, and trailing bytes;
- retail HeadCount resolution;
- scoreboard show-score/show-maximum behavior;
- partial ammo-crate versus full-spawn restock;
- finite server-confirmed block debit and infinite-block suppression.

The relevant suites are:

```text
aos_protocol168_session_tests
aos_protocol168_runtime_tests
aos_hud_layout_tests
aos_pause_menu_tests
aos_tutorial_session_tests
```
