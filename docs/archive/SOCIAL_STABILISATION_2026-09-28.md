# Social stabilisation — 2026-09-28

Scope: the AoSPlay ("Revival") friends, invitations, presence and lobby layer
of the native client, and the AoSPlay social API it talks to
(`../aos_revival`, Next.js + PostgreSQL on Vercel). Steam rich
presence/invites are a separate workstream and were not changed here.

Player report: "inviting through aosplay is unstable, friend adds don't update
in real time, the whole menu is not reactive, continuous errors trying to
invite my friend".

## Architecture (as found)

| Piece | Where | Notes |
|---|---|---|
| Identity / session | `src/network/revival_identity.cpp` | `/api/auth/login`, `/register`, guest Ed25519 challenge, `/me`; bearer token (30-day session) stored DPAPI-protected in `%LOCALAPPDATA%/AoS Revival/launcher_state.json` (`AOS_REVIVAL_STATE_PATH` overrides). |
| HTTP executor | `RevivalIdentityService::social_request` | libcurl, pooled handles, per-request timeouts 4-8 s, token copied out of the lock (no UI-thread blocking). |
| Social worker | `src/network/revival_social.cpp` (`RevivalSocialClient`) | Two lanes on `std::jthread`s: *normal* (polls, reads) and *priority* (writes, ordered). Results reach the UI only via `drain()` on the UI thread. Stale-poll guard by submission sequence; lobby revision guard; exactly-once event IDs. |
| Transport | **polling only** — no push channel | `GET /api/social/sync?cursor&client_instance_id&status[&metadata]` returns the full friends/blocks/invitations/lobby snapshot plus events after `cursor`. The poll is also the presence heartbeat (30 s grace server-side). |
| Writes | `POST /api/social/friends` (request/accept/decline/remove/block/unblock), `POST /api/social/lobbies` (create [+invite_target]), `POST /api/social/lobbies/:id` (join/leave/invite/decline_invite/kick/assign_team/update/member_update/chat/start/publish/in_game/start_failed/close), `GET /api/social/friends?query=` (search), `DELETE /api/social/sync` (presence offline). |
| UI model | `src/frontend/friends_lobby_menu.cpp` | Generation-checked single operation, 12-30 s deadlines, convergence from polls for lost write responses. |
| UI glue | `native_frontend_module.cpp` `pump_social()` / `submit_social_intent()` | Called every frame from the frontend tick; maps intents to requests, results to navigation (Create Match lobby, UGC lobby, join via public list + relay). |
| Relay join | `begin_social_lobby_join` + `platform/relay_host_tunnel.cpp` | Lobby `server_id` is resolved against the public server list, then joined through the AoSPlay UDP relay with a `/api/auth/game-ticket` join code. |

Production latency (measured read-only from this PC, unauthenticated
`GET /api/social/sync` → 401): 0.39-0.99 s; function region `iad1` behind the
`fra1` edge. The client log on this machine
(`dist/bin/BattleSpadesClient.log`, 2026-09-23) shows authenticated social
writes at 1.1-2.4 s each, e.g.

```
2026-09-23T00:09:31.671Z [social] friend_action/request http=200 1760ms
2026-09-23T07:26:45.828Z [social] lobby_action/in_game lobby=2000000211 http=200 2398ms state=in_game rev=8 members=1
```

No invite failures were recorded on this machine (the failing sessions were
on the other player's PC; the client only logs failed syncs on transitions),
so the failures below were reproduced with a local harness instead.

## Reproduction harness

* `aos_revival/scripts/social-dev-server.mjs` — serves the **real**
  `src/lib/social/service.ts` on an in-memory PGlite (real PostgreSQL 17 in
  WASM) with a password-free auth shim, JSONL request log and fault injection
  (`--latency`, `--jitter`, `--fail-rate` → Vercel-style HTML 504s).
* `BattleSpadesClient/tests/social_live_driver.cpp` (`aos_social_live_driver`)
  — two identities, each a real `RevivalIdentityService` + `RevivalSocialClient`
  pumped on a 16 ms frame exactly like the frontend: search → request → accept →
  both lobbies → invite → accept-while-in-own-lobby → 260-message chat burst →
  restarted client first sync → leave → close. Refuses non-loopback APIs.
* The client can be pointed at the harness with `AOS_REVIVAL_API_BASE`
  (new; HTTPS or loopback only) plus a per-instance `AOS_REVIVAL_STATE_PATH`.

## Failures found (with evidence)

1. **Accept / Decline friend request always failed with HTTP 500 (backend).**
   `UPDATE aos_social_friendships SET status = $4 … CASE WHEN $4 = 'accepted'`
   makes PostgreSQL deduce `$4` as both `varchar` and `text`:
   ```
   friend_action/accept http=500 518ms FAILED code=42P08 error=inconsistent types deduced for parameter $4
   ```
   The error is raised at parse time, so every Accept and Decline rolled back.
   Friendships could only form when both players pressed *Add Friend* (the
   reciprocal-request path). The client treats a 5xx write as "may have
   committed" and kept the menu busy for 12 s, then showed "AoSPlay did not
   answer. Please retry." — the "friend adds don't update" / "continuous
   errors" report. Uncovered because no test exercised accept/decline.
2. **A busy lobby permanently broke polling (client + backend).** Every sync
   response was capped at the generic 64 KiB; a new process syncs with cursor
   `0`, which replayed up to 250 events (a week of chat). Measured 140,483
   bytes → `sync … FAILED code=invalid_response error=AoSPlay returned too much
   data.` A failed sync never advances its cursor, so it failed forever: the
   list froze and the menu sat in RECONNECTING, disabling every button.
3. **Active players were evicted from their own lobby (client + backend).**
   Polls are the presence heartbeat, but `tick()` skipped them while any write
   was queued/in flight. On a ~1.5 s link a stream of writes (chat, retries,
   settings) outlived the 30 s grace; another client's cleanup then removed
   the owner and closed the lobby. Baseline run under production-like latency:
   225 × `POST /api/social/lobbies/:id 404 lobby_not_found` after the owner was
   evicted mid-session ("Lobby not found." on every action).
4. **Accepting an invitation while in any lobby failed (client).** Lobbies
   persist while the game polls, so this is the common case:
   `lobby_action/join … http=409 FAILED code=active_lobby_exists error=Leave the current lobby before joining another one.`
   The Friends screen also offered no Accept button in that state.
5. **One failed poll greyed out the whole menu (client).** Action availability
   followed `enabled && connected`; any failed/slow sync (Vercel 504, 6 s
   timeout) set `connected=false`, which disabled every action until a later
   poll succeeded, with back-off doubling up to **30 s**.
6. **Opaque errors (client).** Gateway HTML 504s surfaced as "AoSPlay rejected
   the request."; errors stayed red until the next action even after the list
   recovered.
7. **Not real-time (client + backend).** 3 s menu poll plus 1-2 s request
   latency; incoming friend requests and invitations were invisible unless the
   player opened the right tab (no counts). Every poll also ran the global
   expiry sweep (5 statements incl. a locking transaction), inflating latency
   for everyone.
8. **Offline friends could not be invited** although AoSPlay keeps an
   invitation for 10 minutes ("FRIEND OFFLINE", disabled).

## Fixes

Backend (`aos_revival`, **not deployed**):

* `postSocialFriends` accept/decline: typed parameters (`$4::varchar`, separate
  `$5::boolean`) — fixes #1. Regression test added.
* `getSocialSync`: cursor `0` (new process) or out-of-window cursors resume at
  the newest event (state is in the snapshot); page reduced to 100 events with
  `has_more_events` — fixes #2 for old clients too. Test added.
* `postSocialLobby`: any authenticated lobby write refreshes the caller's
  membership and live presence grace (`refreshLiveness`, lock order lobby →
  member → presence preserved) — fixes #3 server-side. Test added.
* Expiry sweep throttled to once per 5 s per instance
  (`SOCIAL_CLEANUP_INTERVAL_MS`, reads already filter on their own expiry) —
  latency/contention for #7.
* Tests: `npm run test:social` 17/17, `npm run test:social:lifecycle` 14 pass /
  2 Postgres-only skipped, `npm run test:audit` 5/5, `npm run test:hosting`
  20/20, `tsc --noEmit` clean, eslint clean on changed files.

Client (`BattleSpadesClient`, uncommitted):

* `revival_identity.cpp`: social responses may be up to 2 MiB (parser still
  bounds every collection) — #2; 5xx without JSON → `service_unavailable`
  "AoSPlay is temporarily unavailable (HTTP 504). Retrying automatically.",
  429 → `rate_limited` — #6; `AOS_REVIVAL_API_BASE` override (compiled default
  only; HTTPS/loopback only) and a stored token is ignored when
  `launcher_state.json` was issued by a different API base.
* `revival_social.cpp`:
  * a poll runs even with a busy write lane once the last poll is 8 s old
    (`maximum_poll_starvation`; overlapping results are still discarded by the
    existing sequence guard) — #3 client-side;
  * `status().enabled` (account online) separate from `available`
    (last poll OK) — #5; back-off cap 30 s → 10 s;
  * idempotent writes (friend actions, join/leave/invite/decline/kick/
    assign/member_update/publish/in_game/close, attempt-scoped start) and reads
    are retried twice (400/800 ms) on transport errors or 502/503/504; chat,
    settings update and create are never repeated;
  * `set_foreground()` — 1.5 s polls while Friends / Create Match / UGC lobby is
    on screen (still 1 s inside a lobby, 3 s otherwise), immediate poll on
    opening — #7.
* `friends_lobby_menu` / presentation: buttons follow the account, a separate
  RECONNECTING indicator follows poll health; tab counts `REQUESTS (n)` /
  `INVITES (n)`; `LEAVE + JOIN` for an invitation into another lobby;
  `INVITE (OFFLINE)`; action errors auto-clear after 8 s; timeout text now
  says the list will update if the write went through.
* `native_frontend_module.cpp` (three surgical hunks): foreground hint and the
  enabled/connected split in `pump_social()`; `accept_lobby_invite` enqueues an
  ordered `leave` of the current lobby before the join — #4.

### After the fixes (same harness, 1.1-2.0 s latency, 8 % injected 504s)

```
{"step":"bravo_sees_incoming_request","ok":true,"ms":1916}
{"step":"bravo_accept","ok":true,"http":200}
{"step":"alpha_sees_accepted","ok":true,"ms":3190}
{"step":"bravo_sees_invitation","ok":true,"ms":404}
lobby_action/leave lobby=2000000000 http=200 1829ms
lobby_action/join lobby=2000000001 http=200 1372ms state=forming rev=2 members=2
{"step":"alpha_sees_two_members","ok":true,"ms":1122}
{"step":"restarted_client_first_sync_after_chat_flood","ok":true,"ms":1186}
{"failures":0}
```

Server log totals: baseline 225 × 404 `lobby_not_found`, 1 × 500 `42P08`,
1 × 409 `active_lobby_exists`, max sync body 140,483 B; after: none of those,
max sync body 9,148 B. Remaining errors are the injected 504s (polls recover on
the next tick; chat is deliberately not retried and shows the 504 text).

### Client verification

`out/build/social` (native-dev preset, MSVC 19.51 / VS 18, warnings as
errors): full build clean; full CTest 149/150 — the one failure,
`aos_weapon_runtime_tests` ("empty-while-held loads one shell and fires it"),
is in another workstream's uncommitted weapon-runtime edits and touches no
social code. `aos_revival_social_tests`, `aos_friends_lobby_tests`,
`aos_revival_identity_tests` pass, including the new
`stabilisation_2026_09_28_contract` cases (retry/no-retry, starvation bound,
enabled-vs-connected, LEAVE + JOIN, offline invite, error expiry, tab counts).
GUI clients were not driven by hand in this pass; the driver exercises the
same worker/executor the menu uses.

## What needs deploying

1. **AoSPlay backend** — `aos_revival/src/lib/social/service.ts` only (plus
   its tests / the dev server, which are not deployed code). No migration.
   Item 1 (accept/decline) is a production outage for every client version and
   should ship first. The `aos_revival` checkout has other unrelated dirty
   files: deploy only this file's diff on top of the currently deployed source
   (same caution as `out/evidence/FRIENDS_STABILIZATION_20260920.md`).
   Optional env: `SOCIAL_CLEANUP_INTERVAL_MS` (default 5000).
2. **Client** — the next BattleSpadesClient build (Windows/macOS/Linux).
   Everything is backward compatible with the current backend; the leave-then-
   join and payload/poll fixes work without the backend deploy, accept/decline
   needs it.

## Not changed / follow-ups

* Still polling (no WebSocket/SSE push); Vercel functions make long-lived
  connections costly. With the fixes, foreground convergence is ~1.5-3.5 s.
* No out-of-menu toast for a new friend request/invitation (tab counts only).
* The prod database was not inspected; lock contention was inferred from the
  code and addressed by the throttle.

## Test instructions for two real players (after backend deploy + new client)

1. Both players sign in (registered or online guest; offline guests cannot use
   social). Each opens **Friends** — status bottom-right shows CONNECTED.
2. A: search B's name → *ADD FRIEND*. Within ~2-4 s B's tab shows
   `REQUESTS (1)`; B opens it, selects A → *ACCEPT REQUEST*. It must succeed
   first time (before the deploy this returned HTTP 500). A sees B as ONLINE.
3. B: create a lobby (or open Create Match), go back to Friends. A: select B
   → *CREATE + INVITE* (or *INVITE TO LOBBY* if A already has a lobby).
4. B: tab shows `INVITES (1)`; select the invitation → *LEAVE + JOIN*. B lands
   in A's lobby; A's roster shows 2 players within ~2 s.
5. Chat in the lobby for a while, quit and restart one client: Friends must
   reconnect and show the lobby (no permanent RECONNECTING).
6. Pull the network cable for ~15 s: the indicator shows RECONNECTING but the
   buttons stay usable; on reconnect the list refreshes on its own.
7. Collect `BattleSpadesClient.log` (next to the exe); `[social]` lines list
   every action with HTTP status, latency and error code.

Local, no-risk variant: `node scripts/social-dev-server.mjs --port 18790
--latency 1200 --jitter 800 --fail-rate 0.08` in `aos_revival`, then
`aos_social_live_driver http://127.0.0.1:18790 <scratch-dir>`; or start two
clients with `AOS_REVIVAL_API_BASE=http://127.0.0.1:18790` and distinct
`AOS_REVIVAL_STATE_PATH` values and sign in with any names (the dev server
accepts any password).
