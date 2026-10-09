# Original Ace of Spades compatibility

The existing BattleSpades classic rules are displayed as **Classic+**. Their
`cctf` playlist code, `classic` feature flag, saved settings and Protocol 168
transport are unchanged. Original servers are displayed as **Classic 0.75** or
**Classic 0.76**, using their advertised protocol and then the actual connection
protocol. CTF and Territory Control retain their separate game rules.

## Connection and isolation

### Server browser ping

Classic 0.75 and 0.76 use a connectionless UDP probe on the **game port**:
send the five ASCII bytes `HELLO` (`48 45 4c 4c 4f`), expect exactly `HI`
(`48 49`) from that same IP and port. There is no ENet header, packet ID,
terminating NUL or newline. `HELLOLAN` is a separate metadata request.
See [piqueserver's protocol documentation](https://www.piqueserver.org/aosprotocol/protocolping.html).

The browser measures full round-trip time on its discovery worker, using a
separate monotonic send timestamp for each endpoint. This follows ZeroSpades'
RTT convention and ENet's connected ping (the old protocol document describes
halving the elapsed time). The batch takes at most 900 ms for up to 512 unique
numeric IPv4 endpoints; duplicates share a measurement. Missing replies, DNS
rows and socket failures show `—`, sort after measured pings, and remain
joinable. A fresh socket per refresh rejects replies left from earlier probes.
The master server's latency is not used as the player's Classic latency.

While connected, the local player's scoreboard ping comes from ENet. Standard
Classic packets do not provide other players' RTTs, so those remain unknown.

### Version selection

`GameProtocol` uses the ENet connect data values 168, 3 (0.75), and 4 (0.76).
An unversioned direct connection tries 168, 0.75, then 0.76, normally advancing
after an explicit incompatible-version rejection **before any application
packet**. Bans, kicks, full servers, timeouts and established connections never
trigger fallback. Listed retail servers and Steam/identity connections pin 168.

Some original servers ignore the requested ENet version and immediately send a
raw MapStart instead of rejecting 168. For automatic direct connections only,
a complete, bounded five-byte MapStart as the first application packet triggers
a fresh connection explicitly requesting 0.75. A version rejection can then
advance to 0.76 as usual. This never switches a pinned protocol or an application
stream that has already begun, and malformed packets do not trigger retries.

Automatic direct connections listen for 250 ms after ENet connects before
sending the retail ticket. An initial Classic MapStart cancels that pending
send before reconnecting; a silent retail server receives its required ticket
when the window expires. Pinned retail connections send it immediately.
This is a bounded probe, so a legacy server that delays its first packet beyond
the window should be joined with an explicit `:0.75` or `:0.76` link to avoid
retail probing entirely.

Direct links may pin a version, for example
`aos://127.0.0.1:32887:0.75`. Original packed little-endian IPv4 links are also
accepted. The browser combines AoSPlay/Steam listings with the public
Build and Shoot list. Legacy endpoints retain their port and protocol when
merged, saved as favorites, or used to prefill the loading screen.

`ClassicProtocolSession` handles the original handshake, bounded zlib/VXL map
transfer, CP437/UTF-8 text, player state, CTF/TC objectives and terrain packets.
The 64-high original map is translated by +176 on Z into the shared world.
This translation does not alter collision coordinates on the wire or apply the
retail VXL chroma-key filter. 0.75's implicit WorldUpdate IDs and 0.76's explicit
IDs have separate decoders. A new map clears queued input and world state.

TC StateData accepts piqueserver's fixed 241-byte layout (16 territory slots,
13 bytes each) and the compact layout containing only active territories.
Unused slots are skipped, not turned into objectives. Partial padding, extra
tail bytes and counts above 16 are rejected. The regression fixture
`tests/fixtures/classic-tc-state-voxide.bin` is the 241-byte StateData captured
from `152.117.81.19:30002` on 2026-10-09, with eight active territories.

The adapter translates an allowlist of semantic client actions. Retail
ClientData, flight, prefab and other unsupported gameplay packets never reach
legacy peers. Original position updates are limited to 1 Hz; orientation is
sent on change at at most 120 Hz. Input/tool changes are reliable on channel 0.
No optional extensions are advertised. Up to 128 player slots are supported.
Steam tickets and AoSPlay join credentials are not sent to legacy connections.

Server-owned block lines may edit all 64 layers and use temporary builder IDs
(including an absent slot 32 or ID 255), matching ZeroSpades's temporary colour
handling. Client terrain requests still protect the bottom two layers. TC
MoveObject changes update ownership in both existing zones and the capture HUD.
Raw action envelopes are validated again against the current local life before
sending: a death, map change or redirected player slot cannot reuse queued
grenade/build requests. Motion waits until the replacement bootstrap is adopted,
authoritative death releases controls, and respawn resends orientation.
Both versions answer the optional server challenge without advertising any
unimplemented extensions. WorldUpdate translation reserves its full row buffer
once and uses a fixed-size ID list, avoiding vector growth on the hot receive path.
The 0.76 MapStart CRC/name metadata is decoded into the existing map-name field;
five-byte fork/demo headers remain accepted. Zero/oversized map lengths, partial
CRC fields and extra 0.75 header bytes fail before retiring the current map.
Server ChangeTeam/ChangeWeapon notifications are ignored as in ZeroSpades:
piqueserver can broadcast an uninitialized ChangeWeapon, while the subsequent
KillAction/CreatePlayer supplies the authoritative team and weapon.

Optional extension support is intentionally not advertised. DamageMarkers,
Flashlight, MessageTypes, PlayerLimit, SilentPlayer, Teamplay and KickReason
extensions are outside this release's negotiated feature set. Unknown extension
packets are discarded before bootstrap buffering as well as during play, so
unsolicited extension bursts cannot fill the pending map's gameplay queue.
Ordinary chat, scripted terrain, CTF/TC objectives and server position corrections
continue through their original packets. Extra message categories received
without negotiation remain readable as normal system chat; their specialized
placement/severity styling is not claimed as supported. Gameplay messages are
limited to 512 wire bytes, deferred supported gameplay to 4096 packets/4 MiB,
and the live decoded queue to 16384 packets/16 MiB. A server that requires an
optional extension can still refuse the connection.

## Gameplay

Legacy sessions use original movement, acceleration, crouch/aim/sprint friction,
jump/climb/fall behavior, fixed player hit boxes, gun spread, recoil, ammunition and
reload rules. Weapon tables distinguish 0.75 from 0.76. Reload completion and
ammunition come from the server, including when switching tools. Original
block lines remain available because both protocols support them.

Rifle, SMG and shotgun recoil uses the original per-version magnitudes,
1024 ms horizontal triangular wave and radian camera turns. Walking doubles
hip-fire recoil, aiming suppresses that multiplier, airborne doubles it, and
grounded crouching halves it. Classic+ keeps its existing recoil. Empty legacy
magazines do not force the sights down.

Legacy jumping requires a fresh press while grounded. Wading changes friction;
it does not allow another jump in mid-air. Holding jump never enables automatic
bunny-hopping. Accepted jump edges survive the simulation/network handoff and
remain on the wire for a server physics step. Resolved crouching, including a
blocked attempt to stand, is also reflected in transmitted input.

The classic inventory contains a spade, blocks, one original gun and grenades.
It starts with 50 blocks and three grenades. Classic terrain uses face-connected
collapse and protects the bottom two layers. Placement previews use the same
classic reach checks as transmitted build actions. Observer weapon effects are
driven by the replicated trigger state, not retail firing packets.
Observer spread and pellet counts use the same per-version tables as local
shots. Their animation clock advances with simulation time, independently of
how many input packets other players send. All eight shotgun pellets retain
impact/tracer feedback even when one requests the shot's single block removal.

Local hit-box contacts drive the existing blood, hit marker and hit-confirm
sound once per shot; these are predicted feedback because the base protocol
does not acknowledge individual hits. Health, damage and kills remain server
authoritative. Gun kill icons use the killer's original gun loadout, so a
subsequent switch to the spade cannot change the cause shown in the kill feed.

Grenades use original float32 flight, 32-unit gravity, endpoint collision,
axis reflection and 0.36 bounce damping, including the original water/boundary
rules. The shared grenade model, effects and sounds remain in use. Releasing
a grenade ends its held hand animation instead of replaying the pullback.

Legacy hits feed the existing block-damage shading, impact, tracer and falling
structure components. Block colour is restored after ten seconds without removing blocks
ahead of server confirmation. The shared collapse routine uses face adjacency
and the legacy support layers for these sessions. Shovel animation uses the
existing model and swing, timed to the original primary/secondary cadence.
Local death captures the current simulated position before the shared corpse
and death-camera transition; original servers omit self from WorldUpdate.

The existing palette and weapon-custom eyedropper control set the original
SetColor packet. The adapter remembers the builder's colour locally because
piqueserver does not echo that packet to its sender. Single blocks and lines
use the same colour. Server-confirmed builds spend the block wallet; original
single-block destruction refunds one, while alternate digging does not.

Objectives reuse BattleSpades components: CTF bases use its base zones and
server team colours, intel uses its existing ground/carried pickup lifecycle,
and TC uses its territory zones, ownership and capture HUD. Original progress
rates drive that HUD between server updates. No second set of marker graphics
is introduced. The existing territory HUD has ten slots; larger original TC
layouts retain their map zones but do not gain extra HUD slots.

Legacy rosters, team headings and their Deuce helmet masks use the server's
team palette, including spectator name tags. The health portrait and roster class icons reuse the existing
cached model-icon renderer with that RGB included in the cache key. Stock
Deuce models and equipped cosmetics retain their face/material colors while
team-colored parts follow StateData. Classic+ keeps its authored UI palette.
The local ping is the ENet peer's measured round trip; other players show `—`
because the base protocol does not carry their pings. Scores follow the initial
ExistingPlayer score, one point per non-self kill, and ten per intel capture.
The loading score table shows those original rules instead of retail bonuses.
Server scripts can implement additional scoring outside that base protocol.
The unsupported retail kick-vote dialog points to server chat commands instead;
commands and vote availability depend on that server's installed scripts.

Sneak remains the original input bit. For example, VOXIDE Hallway's welcome
message on 2026-10-08 explicitly assigns its charge ability to V (sneak);
server-owned movement powers must not be disabled in the client.

Scripted modes can hide CTF objectives using piqueserver's
`(infinity, infinity, 128)` sentinel. This becomes an absent objective: existing
markers/entities are removed and can return on the next finite position update.
It is never forwarded to rendering or accepted as a player position. Other
non-finite coordinates remain invalid.

BattleSpades rendering and local cosmetic choices remain available. Cosmetics
do not change collision or hit boxes and are not broadcast as legacy gameplay
extensions. Legacy loadout choices do not overwrite saved Classic+ loadouts.

## Verification

Build the optional targets with the project's normal configured CMake preset:

```text
cmake --build --preset native-dev --target aos_classic_protocol_tests aos_classic_live_smoke
ctest --test-dir out/build/native-dev -C RelWithDebInfo -R aos_classic_protocol_tests --output-on-failure
```

The codec test covers both versions, malformed input, map resets, text,
endpoints, protocol fallback, naming, native objective markers, original recoil,
ammunition/reload behavior, colour changes without server echoes,
hit-box occlusion, damage shading/recovery, falling components, shovel timing,
placement around the player's feet, and a 300-frame movement trace generated by
actual piqueserver physics. Four additional 360-frame traces cover water jump
spam, leaving water, holding jump, and climbing a ledge. Regenerate the traces
alongside five 180-frame grenade traces (ground, wall, corner, water, map edge)
with a Python environment
containing the reference piqueserver build:

```text
python tools/generate_classic_movement_fixture.py
python tools/generate_classic_grenade_fixture.py
```

The opt-in live test starts a fresh **loopback-only**, unlisted piqueserver with
an observer script and a second local player. The script fixes spawn positions
and records accepted actions without changing server validation. It checks
automatic 168-to-0.75 fallback, map loading, spawning, movement, a validated
headshot/kill, authoritative reload, coloured building, digging and a grenade throw.
It also checks the resulting score, holds jump through landing and requires
exactly one accepted jump. A second connection triggers a server-owned map
rotation and must download the new map and respawn with an increased generation.
It fails on position corrections or a server hack report and stops its server
afterward. It needs piqueserver and its ENet binding installed in that Python
environment; it is deliberately not a network dependency of CTest.

```text
python tests/run_classic_live_smoke.py out/build/native-dev/tests/RelWithDebInfo/aos_classic_live_smoke.exe
```

For an explicitly supplied server, the same executable has a connection-only
diagnostic. It downloads the map and decodes state without spawning or sending
gameplay actions; the endpoint may pin `:0.75` or `:0.76`:

```text
aos_classic_live_smoke.exe --probe aos://host:port:0.75
```

The loopback-only automatic-connection regression tests a version-ignoring
Classic peer without leaking a retail packet, a silent retail peer's bounded
ticket delay, and the immediate ticket on a pinned retail connection:

```text
python tests/run_automatic_protocol_smoke.py out/build/native-dev/tests/RelWithDebInfo/aos_classic_live_smoke.exe
```

A public-server reproduction on 2026-10-08 exposed hidden objective coordinates
in StateData. After the sentinel fix, `77.37.141.193:32892` completed bootstrap.
Both protocol fixtures cover hidden objectives and their hide/reveal lifecycle.

On 2026-10-08 this local 0.75 test passed with zero corrections and no hack
reports. The relevant existing movement, weapon, terrain, roster, tutorial,
join, discovery, loading and Protocol 168 session tests also passed.
The existing native-window smoke was also run on a generated classic map with
a 30-second world hold and inspected captures of the HUD, inventory, scoreboard
and team menu. This exposed and fixed a solo-match watchdog warning: empty
original WorldUpdates must still reach the shared connection watchdog. Palette
and recoil fixes subsequently passed the codec/gameplay and live colour tests.
The subsequent regression pass exercised death after walking on a generated
map and inspected the corpse position and respawn captures. The native smoke
script exposes this check through `-ExerciseClassicDeath`.

The feedback regression pass also checked live gun kill icons and custom team
colours on VOXIDE Hallway, and original score updates and map rotation on the
local piqueserver. Frame traces found recurring Steam helper restart work in
the game thread when the local Steam registration was stale. Failed background
Steam attachments now wait until leaving the live match; ready callbacks keep
running. Additional trace scopes distinguish services, observer shots, world
simulation, minimap updates and input/capture stalls. This does not establish
that every reported long-session slowdown is resolved.

### Stabilization verification, 2026-10-09

Three agents reviewed protocol handling, client VXL import, and server VXL
import; the integration pass preserved the existing death/ragdoll code.
The Windows native client and server Cython module were rebuilt. Eight core client
CTest suites passed (Classic, retail session, terrain, roster, VXL, legacy VXL,
frontend and ragdoll), plus the scripted weapon image suite after the final skin
preparation change; 329 scoped server tests passed, including all 28 stock
map loads and new import/background-load regressions.

Public targets came from the [Acorn list](https://aos.acornserver.com/) and the
supplied endpoints. These were brief, passive post-spawn observations, not full
gameplay certification of each plugin. No chat or combat actions were sent.

| Server/mode | Endpoint | Result |
| --- | --- | --- |
| aloha Arena | `74.91.124.129:32020` | Spawn and 348 world updates; initial run had exposed a rejected server BlockLine |
| aloha Babel | `74.91.124.129:32000` | Spawn and 355 updates |
| Voxide Bots / TC | `152.117.81.19:30002` | Spawn and 726 updates; rebuilt graphical client also spawned with custom team colors and TC markers |
| Voxide FFA | `152.117.81.19:30013` | Spawn and 720 updates |
| Voxide Push | `152.117.81.19:30014` | Spawn and 719 updates |
| Voxide Hallway | `152.117.81.19:30007` | Automatic detection, spawn and 1203 updates |
| YARR Kraken | `161.97.110.170:32888` | Baseline spawn and 118 updates |
| Spicy CTF | `82.67.159.230:32889` | Spawn and 725 updates; server reports missing optional extensions listed above |
| Supplied 0.76 server | `94.213.171.38:32887` (`aos://648795486:32887`) | No ENet handshake response from this machine; public 0.76 interoperability remains unverified |

The [Build and Shoot feed](https://services.buildandshoot.com/serverlist.json)
contained 36 entries marked 0.75 and one marked 0.76 when checked. That single
0.76 entry was the supplied Dodo's Nest endpoint, with `last_updated=1600042938`
(2020-09-14 00:22:18 UTC). Its presence in the feed is not evidence that it is
currently reachable.

The owned piqueserver gameplay test passed accepted hits/kills, colored builds,
digging, grenades, reloads, one jump per press and map rotation with **zero
correction packets and no hack reports**. Public plugin weapon effects were not
exercised by that test.

`tests/run_classic076_transport_smoke.py` adds a real ENet loopback wire fixture.
It checks explicit version rejection/fallback 168→3→4, both cache misses,
CRC/name MapStart, sparse player 95, challenge/version/extension replies, and a
server-owned second map changing local ID from 0 to 65 with exactly one join.
It validates every outgoing packet it receives. This fixture is not an
independent 0.76 server implementation.

The automatic-handshake ENet regression reproduced an eager retail packet on
the previous executable. The rebuilt client sent no application packets to the
version-ignoring Classic fixture before its delayed MapStart and 0.75 reconnect.
Silent retail received its initial ticket after 250 ms; pinned retail received
it immediately. The 0.76 redirect fixture and both protocol suites passed again
after that change.

One graphical reconnect was closed by the public bot server during map transfer
with reason 0. No decoding error was reported, and both a fresh passive probe
and the final graphical automatic connection spawned successfully afterward.
Its cause remains undetermined; the client does not hide such disconnects by
retrying established sessions or overriding server policy.

```text
python tests/run_classic076_transport_smoke.py out/build/native-dev/tests/RelWithDebInfo/aos_classic_live_smoke.exe
```

An isolated MSVC/O2 packet-buffer benchmark (100,000 constructions, equal output
checksums) reduced 32-player WorldUpdate construction from 30 allocations to one
and from 205 to 105 ms; the 128-player case changed from 624 to 408 ms. These
measure buffer construction, not frame rate. Classic map loading also skips
unused chroma-marker allocations and clipped interior-fill iterations. Server
background Classic import tests measured worst event-loop gaps of 3.6–5.5 ms.

The graphical trace exposed a 343 ms failed Steam helper retry in the join
menus. Retry deferral now covers connection/loading/class selection and local
worlds, while ready callbacks continue running. A separate cold custom viewmodel
load took 321 ms at first spawn. Scripted weapons now begin preparation in the
class chooser, using the selected class, server team color, body and variant as
the same cache key used at spawn. At most eight speculative picker jobs can be
pending; obsolete jobs are retired only when finished, and gameplay takes
ownership without adding ongoing picker work to normal frames. Character asset
resolution also runs in the existing preparation worker. GPU upload and weapon
animation remain on their existing path.

On the same server and equipped custom SMG, a fresh process with time in the
chooser measured **87.762 ms** for first-spawn `local_viewmodel`, versus
**321.205 ms** before this change. The complete spawn tick changed from
362.941 to 104.621 ms. The new `scripted_skin_wait` scope stayed below its 2 ms
logging threshold, and the displayed weapon/arms/team colors were inspected.
This leaves one-time model upload/audio work; an immediate selection can also
outrun preparation. An earlier separate 356 ms menu transition was not reproduced
in the final run and is not attributed to this fix. No claim is made that all
frame spikes or long-session slowdowns are fixed.

Native disk import instructions are in [LEGACY_VXL_IMPORT.md](LEGACY_VXL_IMPORT.md)
and the server's `docs/LEGACY_VXL_MAPS.md`. Deploy the coordinated client/server
MapSync changes together; marker behavior on older Protocol 168 implementations
has not been exhaustively retested.

**Limits:** 0.76 has codec/gameplay and local ENet wire fixture coverage, but no
successful test against a public 0.76 implementation. Server-specific scripts and anti-cheat
policies may impose additional rules; the local test does not establish parity
with every server. Cosmetic models/effects still use the BattleSpades
renderer. Large TC layouts and interactive map-transition presentation need
broader playtesting. This change does not add a 0.75/0.76 server
host to BattleSpades: its hosting menu continues to host Classic+; piqueserver
or another original-protocol server hosts legacy matches.

## Client audio and observer effects

Deuce uses sixteen existing Soldier/Miner death recordings with no immediate
repeat. Other classes retain their authored voice banks. **Settings → Main →
Music on silent servers** opts into a retail gameplay track, chosen from the
four `last_man_standing` tracks without immediately repeating the previous
choice. It uses Music Volume and yields to server PlayMusic/StopMusic commands.

Remote Classic gunfire tests player contacts at their displayed positions and
uses the normal blood particles, terrain impacts and full shotgun tracer count.
Terrain blocks those contacts; confirmed remote gun/melee kills also produce an
impact. Observer contacts are cosmetic estimates because original servers do
not broadcast hit confirmations or pellet seeds. They never send hit reports,
change health, edit terrain or trigger the local player's hit marker.

Pack-less corpses in Classic 0.75/0.76 and Classic+ have client-side gravity and
voxel collision. **Settings → Main → Ragdoll corpses**, on by default, lets a
dead player go down as a ragdoll: the player's own model, skin and hat
included, let go where it stood. With the setting off, Deuce leaves the
authored `ClassicCorpse` death-soldier model lying where the player fell, as
retail does; the other classes have no authored corpse and always use the
ragdoll. Respawn, disconnect, map changes and server corpse cleanup remove the
local simulation. Authoritative jetpack deaths retain their existing
server-driven lifecycle. The death camera follows the simulated body until a
server grave takes over.

A ragdoll is ten rigid parts: trunk, head, two upper arms, two forearms, two
thighs and two shins, joined at the neck, shoulders, elbows, hips and knees.
The legs are the class's own leg models cut at the knee. Each part has a mass
(trunk 34 kg, head 6, thigh 8, shin 5, upper arm 2.2, forearm 1.9; 74 kg in
all) and the inertia of a box its size, and every correction is shared out by
mass and inertia: a blow to the head whips the head and only rocks the trunk,
and a joint stop that turns a forearm one way turns the upper arm the other.
Shoulders, hips and the neck have separate limits for pitch, sideways swing
and twist; elbows and knees are one-way hinges. Collision balls sized to the
art cover every part and every joint, so a body rests on the ground where its
voxels do. Parts that are not jointed neighbours collide with one another.

The body starts in the living pose: upright with the retail walking stride, or
squatting, with the arms where the third-person tool pose held them. It keeps
the velocity it died with and takes the killing shot as an impulse at the head
or chest. Rifles, SMGs, shotguns and machine guns are released as a separate
rigid body that falls, tumbles and settles with the player's momentum; it is
never a pickup.

It does not go limp in that instant. Death is a sequence, the same in every
direction because nothing in it is authored: the joints are driven toward
targets by springs whose strength drains away (an active ragdoll), and the
body is physics throughout.

- **The shot.** The struck part takes the impulse. The trunk is still held
  toward upright by a spring, so a hit to the head or chest rocks the upper
  body back and it partly recovers.
- **The stagger** (0.55–1.25 s from standing, 0.25–0.45 s from a squat, drawn
  from the death's seed). The legs keep stepping the way the body is actually
  moving, forward, backward or sideways: stride and cadence follow the speed of
  the hips and chest, so a trunk thrown back by a shot draws the feet back
  under it. The hips are pushed after a leaning trunk as a catching step would
  take them, and the legs still carry a share of the weight at a height that
  sinks as the strength goes, so the body drops as it steps.
- **Going over.** Once the trunk leans past about 45 degrees the legs stop
  walking and trail in line with it; the body comes down full length with the
  momentum it has, not onto its knees.
- **Limp.** A little spring is left in each joint through the fall so limbs
  land loosely bent rather than folded flat, and goes once the body is down.

Nothing in the sequence delays the server's death or the input lock; it is
drawn by the client alone. Over a drop there is nothing to stand on and the
stagger carries nothing.

The simulation is position-based rigid-body dynamics at a fixed 120 Hz with six
substeps, independent of the frame rate. What keeps it from misbehaving:

- A death inside or against terrain is first set clear of it, and that
  correction is never turned into motion.
- What one substep's corrections may add to a part's speed is bounded, parts
  pushed out of one another are not thrown apart, and the ground returns none
  of an impact. A block built into a body moves it; it does not launch it.
- A struck part leaves at no more than twice the speed of the blow, however
  light it is.
- A body left kneeling, hips held up on its own folded legs with its head on
  the ground, or sat folded forward with its chest in the air, is drawn over
  onto its side instead of staying propped up.
- A body that has gone nowhere for 1.25 s sleeps even if its parts are still
  trading corrections where it is wedged. Shots and terrain edits wake it.

Local and observed gunfire tests the current corpse capsules, wakes sleeping
bodies and applies a bounded impulse at the struck part, with existing blood
effects. Terrain and nearer living players clip these cosmetic hits. Only the
nearest corpse reacts per pellet; no corpse hit packet, damage, ammo award or
pickup is invented. Body and dropped-gun state share the existing death cleanup.

`aos_classic_corpse_tests` covers the collapse, momentum, per-part mass,
terrain, sleep and frame-rate independence. `aos_ragdoll_capture` renders one
death at several moments side by side into a PNG, offscreen, for judging the
motion without running the game.

**Settings → Main → Lingering blood** enables cosmetic voxel droplets and
irregular surface marks in both Classic and Standard. Marks stick to exposed
block faces for 20–32 seconds, shrink away over their final five seconds, and
disappear immediately if their supporting face is destroyed or covered.
The effect has bounded droplet/mark pools, follows the existing hit feedback,
and never edits terrain, damage or network state. Turning it off or leaving
the map clears it. The preference defaults off and supports live preview,
Cancel and persistent saving through the existing settings menu.

Custom weapon scripts receive reload progress and ready-state timing from the
active Classic weapon rules, including per-shell shotgun reloads. Their visual
timeline spans the actual reload duration without changing ammo or server timing.

## ZeroSpades gameplay comparison (2026-10-09)

This audit uses ZeroSpades revision
[`6a56dc8444b0380eb77f677ba029d83a9c78d29a`](https://github.com/zerospades/zerospades/tree/6a56dc8444b0380eb77f677ba029d83a9c78d29a),
with its default `cg_classicWeaponRecoil=1`. Compatibility does not establish
identical behavior in every situation. The following **0.75 base values** in
`classic_weapons.hpp` match that revision's `Sources/Client/Weapon.cpp`:

| Weapon | Shot interval | Magazine / reserve | Reload | Spread parameter | Upward recoil | Pellets |
| --- | --- | --- | --- | --- | --- | --- |
| Rifle | 0.5 s | 10 / 50 | 2.5 s | 0.006 | 0.05 rad | 1 |
| SMG | 0.1 s | 30 / 120 | 2.5 s | 0.012 | 0.0125 rad | 1 |
| Shotgun | 1.0 s | 6 / 48 | 0.5 s per shell | 0.024 | 0.1 rad | 8 |

Spread is a direction-vector perturbation parameter, not an angle in degrees.
The perturbation formula, accumulated shotgun pellet directions, aiming half
spread and crouching half spread (except shotgun) follow ZeroSpades. Recoil
uses its 1024 ms horizontal triangle, per-weapon side coefficients, walking
hip-fire and airborne multipliers, grounded crouch reduction and 89-degree
pitch limit. The movement acceleration/friction equations follow the same
approach, including pitch-dependent forward speed and diagonal normalization.
Existing movement and grenade fixtures additionally compare against piqueserver.

The three combat/runtime differences found in the audit are now corrected:

- **Spread randomness:** Classic uses ZeroSpades' xorshift128+ generator and
  uniform integer sampling over `[0,32767]`, continuing between shots. It no
  longer restarts from the retail visual seed, which has only 255 values.
  Direction perturbations and normalization use float arithmetic, including
  accumulated shotgun pellets. Production streams start from entropy;
  explicit seeds make reference tests reproducible. Resetting block damage
  does not rewind the random stream. Random outcomes are not expected to be
  identical between separate clients or standard-library implementations.
- **Tool-switch cooldowns:** gun, spade, dig, block and grenade deadlines are
  independent and advance while inactive. Switching cannot borrow another
  tool's cooldown or erase its own. Gunfire advances the previous deadline,
  matching `Weapon::FrameNext` cadence across uneven frames, with at most
  one shot per update. New-life resets clear all deadlines.
- **Simultaneous shovel buttons:** primary suppresses secondary locally and
  in outgoing weapon input. Releasing primary while still holding secondary
  starts a full new dig charge. Releasing/re-pressing secondary between ticks
  and cancelling input also discard the previous charge.

Right-click digging requires holding the button for one second; tapping and
releasing cancels it in both clients. `shovel_dig_tests` now exercises the real
session input and weapon runtime, combat trace, original BlockAction packet,
decoded confirmation and terrain removal for both 0.75 and 0.76. It verifies
the target plus its vertical neighbors are removed, connected lower terrain
remains, tapping cancels, releasing stops further digs, and simultaneous
buttons retain primary priority. This is an
in-process regression, not evidence of server-specific plugin behavior.

After these fixes, the Classic protocol/gameplay, weapon runtime, tutorial
session and Classic corpse suites pass. Spread tests check 8,192 successive
shots for diversity, centering and reference variance, plus stance/pellet
behavior in both versions. Timing tests cover switching, new lives, uneven
frames and dig cancellation. Movement fixtures passed in the preceding audit.
Local piqueserver accepts hits, kills, colored builds, digs, grenades and
reloads, with zero corrections or hack reports during the scripted exercise;
map rotation and respawn also pass. The loopback 0.76 ENet fixture passes
fallback, cache misses, sparse player IDs, plugin replies and redirect with
one join. That fixture is not a public 0.76 server test.

These checks establish the covered combat/runtime behavior, not full-client
or server-specific anti-cheat certification. Client presentation/input gates,
custom server scripts, network delay and public 0.76 interoperability require
separate validation.

For comparison, OpenSpades revision `ff9b3e71b9ad26dda940923515de8b46f4bba5a5`
has the same base 0.75 gun values, but its crouched spread, recoil application,
vertical-look movement scaling and shotgun block damage differ from this
ZeroSpades baseline. Those two clients are not interchangeable parity targets.

## Reference provenance and licensing

Protocol layouts and original gameplay behavior were checked against:

- [ZeroSpades](https://github.com/zerospades/zerospades/tree/6a56dc8444b0380eb77f677ba029d83a9c78d29a),
  particularly `Sources/Client/NetClient.cpp`, `Player.cpp`, and weapon classes.
- [piqueserver](https://github.com/piqueserver/piqueserver/tree/3dfc0a6774fc5cf3a80eec13b7764b751a0d949b),
  particularly `pyspades/contained.pyx`, `player.py`, `weapon.py`, and `world_c.cpp`.

The adapted movement/hit-box and spread RNG routines retain their OpenSpades/ZeroSpades
(Copyright 2013 yvt) and pyspades (Copyright 2011–2012 Mathias Kaerlev) attribution.
Those routines are GPL-3.0-or-later; see `LICENSES/GPL-3.0.txt`. They are included
in the combined AGPL-3.0 BattleSpades client. No reference-repository game assets
are bundled by this change.
