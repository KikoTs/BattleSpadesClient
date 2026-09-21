# Remaining acceptance and development work

The source has playable multiplayer, Tutorial, hosting, online frontend
adapters, Map Creator, Revival publishing, progression/inventory and cosmetic
replication. Do not restart these as unimplemented features based on an old
handoff. [README](../README.md) and the [documentation index](README.md) describe
their maintained contracts. Implementation is distinct from verified parity.

## Release gates

| Area | Acceptance still required for a release |
| --- | --- |
| Gameplay parity | Fresh owner/observer/late-join coverage across tools, classes, objectives, death/respawn, round reset and map transition; mixed retail/native matches |
| Movement and terrain | Deterministic delayed/jittered turning, jumping, flight, edits and rejoin traces on representative maps; investigate measured corrections without changing retail rules to hide them |
| Hosting and social | Multiple-machine public relay/NAT sessions, lost replies, owner departure/rejoin, changed launch attempts and cleanup of owned children/tunnels |
| Map Creator | Simultaneous collaborative editing, authoritative host controls, interrupted sessions, save/reopen and published archive integrity |
| Accounts and results | Complete recovered profile event coverage, persistent result retry/restart, duplicate ingestion and account/identity boundaries against the intended backend deployment |
| Inventory and cosmetics | Immutable receipts, ownership/revisions, lost mutation replies, fallback for missing/invalid content, owner/observer cosmetic state and actual UI/GPU paths |
| Presentation | Paired retail/native captures and audio checks; texture/shader/backend behavior at different resolutions and quality profiles |
| Platforms/packages | Clean extracted-package runs on every advertised OS/backend, native runtime closure, install/import flow and platform-specific signing/packaging |
| Stability/performance | Long sessions, bounded queues/memory, frame-time measurements and graceful shutdown/recovery under adverse network conditions |

Historical pass totals, recordings and deployment IDs establish only the
specific experiment that produced them. This list does not claim a fresh
Windows, Linux, macOS or live-service acceptance run was completed during a
documentation update. Use the [runbook](RUNBOOK.md) to record new results.

## Separate future work

- Full achievements: establish conditions and Steam/Revival account behavior.
- Map cache/delta optimization: the existing full-map handshake remains the
  compatibility baseline until an optimized path has equivalent coverage.
- General community pack distribution/moderation: bundled validated cosmetics
  and a bounded presentation script adapter do not imply an unrestricted mod
  loader or automatic trust in uploaded content.
- Additional presentation features: preserve gameplay/collision/visibility
  contracts and test every supported quality profile.

Use [COMPATIBILITY.md](COMPATIBILITY.md) for evidence ordering. Retail recovery
documents and the playtester audit contain hypotheses and historical findings;
reproduce an issue against the current source before treating it as an open bug.
Update the relevant maintained guide when a gate closes rather than adding
another dated status document.
