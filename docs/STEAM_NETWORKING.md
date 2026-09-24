# Player-hosted matches over Valve's relay network

Create Match hosts a server on the player's own machine. Friends previously
reached it through the AoSPlay relay, which sits in one place and adds latency
for everyone far from it. Steam's relay network has points of presence next to
the players: this Mac measured 20 ms to Madrid, 39 ms to Paris, 49 ms to
Frankfurt and 67 ms to Warsaw.

The transport carries Protocol 168 datagrams unchanged. The bundled server and
the joining client both see ordinary loopback traffic, so neither the server nor
the protocol changed.

```
joiner's client ─UDP→ loopback port ─► Steam relays ─► host's client ─UDP→ bundled server
```

## Application id

The client attaches as **Ace of Spades (224540)**, so an owner appears in Steam
as playing it. Steam refuses an id the account does not own — it answers
`ConnectToGlobalUser failed.` — and that refusal selects **Spacewar (480)**,
which every account owns, so a player without the game still plays.

Peer-to-peer networking and lobbies are per application id, so the two networks
do not meet: a player on Spacewar cannot join a host running as Ace of Spades.
Setting `app_id` to `fallback_app_id` puts everyone on Spacewar instead, at the
cost of every player showing as Spacewar in their friends list.

The attach happens while the boot loader runs, with no wait for the relay
network; the pump thread warms the relays while the player is in the menus. An
attach that fails leaves the retry to the first match, because a player may
start Steam after the game.

## Falling back

Steamworks is loaded at run time and the SDK supplies headers only. A player
without Steam, or with Steam signed out, still starts the game: the runtime
fails to start, the host keeps its AoSPlay relay, and a `steam:` address
reports that Steam is unavailable.

## Checking it across two machines

The host needs no flags:

1. Create Match, then **Local Match**. The loader shows
   `Friends can join with steam:<id>`, which is also in the player's log.
2. The joining player enters `steam:<id>` in **Direct Connect**, or starts the
   client with `--connect steam:<id>`.

`aos_steam_p2p_smoke` checks the transport without the game, on Windows as well
as macOS. One side runs `aos_steam_p2p_smoke host` and prints its Steam id; the
other runs `aos_steam_p2p_smoke join <id>` and reports the round trip through
the relays.
Its `host local` and `join local` modes exercise accepting, forwarding and
teardown on a single machine, because Steam refuses a connection to your own
account. A trailing application id and fallback exercise the refusal path on an
account that owns the game: `host local 999999999 480` is refused and lands on
Spacewar.

## Diagnostics

Every step writes to `BattleSpadesClient.log` beside the executable:

```
[steam] runtime ready: app=224540 id=76561198158362762 relays=ready
[steam] hosting over the relay network on virtual port 0
[steam] player joined over the relay network, now 1 connected
[steam] tunnel state: connected
[steam] the host asks for a ticket for 204.168.157.43:40047
[steam] presence published: Hosting Ancient Egypt, TDM
[steam] lobby 109775242608886766 open for friends: Hosting Ancient Egypt, TDM
```

## Presence, lobbies, and finding a friend

Hosting publishes two Steam rich presence keys. `status` is the line the
friends list shows under view game info, naming the map and mode. `connect` is
the command line Steam hands a friend who clicks Join, which is
`steam:<host id>` — the same address Direct Connect takes. Both keys are
free-form and need nothing configured for the application id, which is what
makes them usable on an id we do not own. Steam spells the switch `+connect`
when it launches the friend, so the client accepts that as well as `--connect`.

A hosted match also opens a **friends-only** lobby carrying the same two
values. A lobby needs no public address, so it reaches players behind any NAT.

Steam's lobby *search* only returns public lobbies: creating a friends-only
lobby and listing immediately finds nothing, `0 of 0`, while it is open. A
friend's match is therefore found through their presence instead, which Steam
serves to anyone running the same application. That is what `friend_matches()`
reads, and what the browser's Friends source shows as a row per friend. Those
rows carry the host id in place of an address, and joining one dials Valve's
relays with no AoSPlay service in the path at all.

`list_lobbies()` remains for a future public playlist, where search would work.

## Preferring Steam

A listing that carries the host's Steam id is dialled over Valve's relays
first: their points of presence sit next to the players, while the AoSPlay
relay sits in one place. A tunnel that cannot be opened is not fatal — the
AoSPlay endpoint in the same listing is used instead and the reason is logged —
so a player without Steam still joins. A row that has only a Steam id fails
with a message, because there is nothing else to try.

The master must store and return `steam_host_id` from the lobby publish for a
browsed match to offer the Steam route; the client sends it and ignores its
absence.

## Statistics and achievements

Both live in the schema of the application id the process attached as, so
`tracking_enabled()` is false while attached as Spacewar: reporting ours under
Valve's test app would write into someone else's schema.

The 77 retail achievement *names* are public, and
`tools/generate-steam-achievements.py` copies them into a generated catalogue.
`unlock_achievement()` refuses a name the catalogue does not hold and says so,
so a typo is a log line rather than a silent no-op.

What each achievement *requires* is not public, and neither are the statistic
names. `tools/fetch-steam-schema.py` reads both with a Steam Web API key, and
its answer decides whether this is possible at all: a 403 means only the
application's owner can read it. Until then nothing is unlocked, because an
achievement granted on a guess lands permanently on a player's real account.

## What the tools check

| command | covers |
| --- | --- |
| `aos_local_server_host_smoke <dir> --steam-tunnel` | server, host, hello, tunnel, Protocol 168 bootstrap, on one machine |
| `aos_steam_p2p_smoke host \| join <id>` | the transport across two machines, and the round trip through the relays |
| `aos_steam_p2p_smoke host local \| join local` | accepting, forwarding and teardown without the relays |
| `python3 tools/verify-frontend-pumps.py` | that every frontend pump is driven, since one that is not compiles and passes every test |

## Not done yet

- **A measured relay session.** The transport is verified over a direct
  connection and across two machines, but the round trip through the relays was
  measured in one direction only: 20 of 20 datagrams, median 92.7 ms from a
  Windows joiner to this Mac. The reverse needs a second account here.
- **Achievement criteria.** See above: the names are in, the conditions are not
  readable without the schema, and guessing grants a player something they did
  not earn.
- **`steam_host_id` on the master.** Drafted in the AoSPlay service and not
  deployed, so a browsed match currently offers only its relay endpoint.
- **Dedicated servers keep their own path.** Relayed dedicated servers need
  Valve-side configuration that only the application's owner can request.
