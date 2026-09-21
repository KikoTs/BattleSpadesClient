# Frontend and service integration

`src/frontend/native_frontend_module.cpp` composes the production routes and
effect consumers. Menu models/presentations live under `include/battlespades/frontend`
and `src/frontend`; network adapters live under `network`, and owned host/tunnel
processes under `platform`. Check these sources when a research note describes
an earlier offline-only milestone.

The [menu action reference](MENU_ACTIONS.md) maps player-facing buttons to their
handlers, availability rules and expected outcomes.

## Routes and owners

| Surface | Current implementation boundary |
| --- | --- |
| Identity gate | AoSPlay account/guest state and Windows Steam bridge; protected identity storage and ticket acquisition |
| Server Browser/direct join | HTTPS and LAN discovery, filters/favourites, typed connection requests and Protocol 168 loader |
| Quick Play | Playlist model and asynchronous discovery feeding the normal connection path |
| Match Loading | Shared in-frame Map/Mode/Scores layout, server mode captions, stock scoring reference, startup/authentication status and readiness-gated entry |
| Custom/Friends/Create Match | Social lobby state, invites/chat, owner-controlled settings and launch attempts; local child plus relay for public hosting |
| Tutorial | Playable offline lesson flow and developer sandbox |
| Map Creator | Lobby/model selection, hidden editor server, project persistence and collaborative service integration |
| Publish Map | Saved project repository and asynchronous Revival Workshop upload; result opens the returned item page |
| Profile/Leaderboard | Bounded asynchronous account/score requests, filters and explicit error/empty states |
| Inventory | Account-owned collection/receipts and RmlUi presentation with a native fallback |
| Settings | Confirmed/draft state, live preview, persistence and display rollback |

The intended Custom Match entry uses the Friends/social route. The retained
legacy `CustomMatchMenu` developer fixture still has no discovery/join/create
adapter; do not describe that fixture as a second live lobby implementation.

Implementation does not establish that a remote service is reachable, deployed
with matching contracts or accepted on every platform. Missing adapters and
failed requests must settle into visible errors; a rendered menu is not proof
of a completed account, upload or network operation.

## Presentation and input

The compatibility canvas is 800 by 600. Presentation and hit testing share the
same letterbox/scale transform. Forward navigation moves incoming content from
the right; Back moves it from the left. The compositor retains outgoing layers
over a stationary background and gates input during the transition.

Boot preload and match loading are separate lifecycles. Texture decoding uses
bounded workers; GPU creation stays on the render thread. Font/glyph warmup is
spread across screens. A session/map generation prevents stale completion work
from installing into a different route or world.

Match loading shares its tab and content geometry between rendering and input.
Map art stays inside the frame; Mode renders the three `InitialInfo` captions
with localized stock-mode fallback. Scores presents the stock score reference
for the mode and friendly-fire setting, with expandable groups and bounded
wheel/scrollbar navigation. The protocol does not transmit a custom score-value
table, so this reference is separate from live player scores.

Match Lobby notices use the free strip above navigation, while Friends keeps a
two-line notice beside its action buttons. Both use bounded readable text with
ellipsis and canvas-safe surfaces; diagnostic tails stay in logs.
Friends shares the retail frame, textured button states, tabs and selection
highlights. Relationship and presence labels distinguish requests, search
results, lobby membership and active games. Query changes discard old
search-only rows; search refreshes preserve authoritative friendship state.

RmlUi owns the Inventory document in `assets/client/ui/inventory.rml` and
`inventory.rcss`; the other native menu presentations remain in C++. The F11
layout editor edits native presentation elements, not RML/RCSS. See
[UI_CUSTOMIZATION.md](UI_CUSTOMIZATION.md) and [INVENTORY.md](INVENTORY.md).

## Hosting, ownership and recovery

Host controls are enabled only for the current owner and forming/launch state.
Input release rechecks ownership; pending edits are discarded after membership
or leadership changes. Social snapshots carry revisions, and late asynchronous
results must not restore an older lobby or launch. A launch attempt has its own
identity even when a relay reuses an endpoint.
Friends can reopen current lobby membership. Once that lobby publishes a ready
match, its main action becomes Join Game for both owners and members; it reuses
the published session and keeps non-owner settings locked.

The local server process receives private configuration and must report real
readiness before joining. Stopping an owned session closes its server and
tunnel. Persistent hosted match reports and UGC projects live outside disposable
session directories. See [RUNBOOK.md](RUNBOOK.md) and [UGC_MAP_CREATOR.md](UGC_MAP_CREATOR.md).

Current bundles publish atomic session/port/mode-bound lifecycle snapshots to
the native client's private `host-status.json` path. Ready follows complete
server initialization; stdin remains the shutdown channel. Older bundles use
LAN probing. Start queues behind pending social settings writes using the same
launch identity, and leaving during creation cleans up the arriving lobby.
Loading displays hosting and authentication progress throughout startup and
connection. Its Start action remains disabled until the session's real map,
network synchronization and assets are ready; displaying a startup status does
not complete that lifecycle.
Back during hosting queues the lobby's `start_failed` recovery before releasing
the owned session. Cancellation invalidates the pending identity ticket;
retrying the same endpoint cannot reuse a ticket for the previous server.

## Profiles, inventory and publishing

Profile counters/mastery and lifetime XP are separate contracts. The backend
owns account rewards and equipment. Native UI code displays confirmed snapshots
and retries stable operation identities after lost replies; it cannot grant a
crate or choose its result. [INVENTORY.md](INVENTORY.md) describes this boundary.

Network-hosted combat playtests are community-listed and can earn server-verified
account XP, including games with bot opponents. Offline Local Match has neither
public discovery nor account XP. Lobby visibility does not grant admission:
the companion backend checks invite/friend membership and blocks when consuming
join tickets. See the server's [hosted playtest contract](../../BattleSpades/docs/RUNBOOK.md#hosted-playtests-discovery-and-account-xp)
for eligibility, canonical modes, result retries and the separate backend
deployment requirement.

Publish Map uses `RevivalIdentityService::publish_ugc_project` through a worker,
with saved local project IDs and cancellation. This is the Revival Workshop
path; it is not a claim that the original Steam Workshop uploader is implemented.

## Validation and evidence

Use the [runbook](RUNBOOK.md) for build, CTest, desktop capture and navigation
commands. Source-backed regressions cover menu models, async revision handling,
input, settings and connection recovery. Graphical smokes test composed frames
and real window input. Live account/lobby, mixed-client gameplay and public
upload acceptance remain separate checks in [ROADMAP.md](ROADMAP.md).

Files under `research/` preserve recovered scene geometry, assets and transitions.
Their old implementation status and capture paths are historical. Current
behavior is defined by the code above, maintained guides and fresh tests.
