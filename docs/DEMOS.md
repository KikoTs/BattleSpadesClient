# Demo recording and playback

BattleSpades can record the packets received during a connection and replay them
locally through the normal map and game-state decoders. This is useful for match
reviews, moderation evidence, bug reports, and community videos. A demo is game
state, not a video: playback still needs the game's installed graphics and audio
assets. It does not need the original server or a master server.

Create the destination directory, choose a new filename, and record from before
joining:

```powershell
New-Item -ItemType Directory -Force demos
.\BattleSpadesClient.exe +connect 127.0.0.1:27015 --record-demo demos\match.bsdem
```

Join as a spectator when you want a match recording without a player's missing
local movement history. Leave the server normally to finish the file. To watch:

```powershell
.\BattleSpadesClient.exe --play-demo demos\match.bsdem
```

Playback opens the normal loading screen. Press **START** after the map loads;
the demo clock stays paused while the map is loading or waiting for START. The
viewer uses the spectator camera and its normal movement/target-selection
controls. Leaving the replay returns to the menus, where normal server joins
work again. The last scene remains visible at the end of the file.

Recording and playback are mutually exclusive. An existing output file is
preserved; choose another name before recording again. A recording covers one
connection, including map changes that reuse that connection. If a server
requires a disconnect/reconnect for rotation, the first recording closes and the
reconnect cannot overwrite it: select a fresh output name for a new recording.
Recording errors are reported through the client diagnostic log and warning
state without interrupting the live game.

## What is recorded

The client stores incoming server packets and monotonic timestamps, including
the complete join/map transfer. Recording requests a full map sync, so playback
does not depend on a map cache from the recording computer. This can increase
the map download when recording is enabled. Disk writing occurs on the network
worker, outside the presentation thread; writes are buffered and flushed about
once a second. Playback streams the file with bounded packet queues.

Outgoing authentication tickets, passwords, and inputs are not recorded. Incoming
chat, player names and game identifiers can be present, including private
messages visible to the recording player. Review that content before publishing
a demo.

There is no server installation requirement. Native builds also include a
console companion, **BattleSpadesDemoRecorder**, for unattended server operators:

```powershell
.\BattleSpadesDemoRecorder.exe +connect 127.0.0.1:27015 --record-demo demos\server-match.bsdem --duration 3600
```

It connects automatically as a spectator, writes the initial map and subsequent
traffic, and needs no window, renderer or installed game graphics/audio assets.
Keep it with the native package's shared libraries. Omit `--duration` to record
until **Ctrl+C** or **SIGTERM**; both stop the connection and finalize the file.
`--duration` is 1..86400 seconds after the first map loads. `--name`, `--password`
and `--protocol auto|168|0.75|0.76` are optional; `--help` lists them.

The companion occupies a spectator slot and follows that server's spectator
visibility rules. It sends neutral scene-readiness updates, never gameplay
actions. It does no master-server/Steam login, so servers requiring verified
identity must be recorded with the regular authenticated client instead. A
disabled spectator rule or failed admission is reported. Same-peer map rotations
continue in the file; disconnections end it and require a new filename on the
next run. Exit code 0 means a requested duration/interrupt completed after a map
was received, 1 means an operational error, and 2 means invalid command arguments.
The dedicated server itself still does not record without a connected recorder.
Simply dumping server broadcasts would lack its initial world and visibility
context.

## Compatibility and current limits

- Native `.bsdem` recordings support the active retail 168 and Classic 0.75/0.76
  transports. Protocol selection is stored in the file.
- The reader also accepts ZeroSpades/aos_replay version-1 `.dem` files for
  protocols 3 and 4. This follows the documented packet stream layout in the
  [ZeroSpades source](https://github.com/zerospades/zerospades/tree/main/Sources/Client).
  The implementation here is independent; no ZeroSpades source was copied.
- Playback is sequential at the recorded rate. There is no demo browser,
  timeline seeking, speed control, video export, or input-perfect first-person
  playback yet. The recorded player's own rig can be absent because the normal
  renderer reserves that slot for the local player; spectator recordings avoid
  that limitation. Client-only effects/prediction not represented by server
  packets cannot be reconstructed exactly.
- Each file is bounded to 1 GiB, 24 hours and 4 MiB per packet. A normal size/time-limit
  stop writes a valid end marker. Truncated native recordings (including an
  interrupted/crashed writer) are reported as incomplete, not silently treated
  as complete. Invalid versions, lengths and backwards/non-finite timestamps
  are rejected. Existing network decoders validate replay payloads.
- Capture should start at connection creation. Starting halfway through a match
  cannot rebuild the missing initial world, so there is no mid-match record
  toggle in this version.

## File format for tool authors

All integer fields are little endian. Native files start with the eight bytes
`42 53 44 45 4d 4f 01 00` (`BSDEMO`, version 1, zero), a `uint16` protocol
(`3`, `4`, or `168`), and `uint16` reserved flags (must be zero).

Each record is a `uint64` time in microseconds since recording started, a
`uint32` payload length, and that many payload bytes. Retail payloads are plain
protocol packets after transport decompression, beginning with the packet ID;
Classic payloads are the plain ENet application packet. The final record has
the last timestamp and length zero, with no trailing bytes. No client-to-server
packets, host address, user profile, ticket or arbitrary metadata are included
by the format.

Legacy files begin with bytes `01 03` or `01 04`, followed by records containing
an IEEE-754 little-endian float timestamp in seconds, `uint16` length and packet
bytes. Their record-aligned EOF is the legacy completion marker.

Tests: `aos_demo_stream_tests` checks framing, round trips, legacy import and
malformed input; `aos_demo_playback_tests` reconstructs complete compressed
retail and Classic maps and exercises the background connection's offline replay, timing,
START pause, completion and suppression of outgoing packets. It also starts
ephemeral loopback ENet servers for both protocols, records their real incoming
traffic, checks every saved packet, and reconstructs the captured maps offline.
Companion tests verify spectator admission, neutral retail readiness, duration
completion and interruption with a clean file footer.
