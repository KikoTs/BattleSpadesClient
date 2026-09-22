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

Peer-to-peer networking and lobbies are per application id: a player running as
480 cannot reach a host running as 224540. The transport therefore uses
**Spacewar (480)**, which every Steam account owns, so players who do not own
Ace of Spades stay in the same network. The retail id remains available for
presence through the existing 32-bit bridge.

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

`aos_steam_p2p_smoke` checks the transport without the game. One side runs
`aos_steam_p2p_smoke host` and prints its Steam id; the other runs
`aos_steam_p2p_smoke join <id>` and reports the round trip through the relays.
Its `host local` and `join local` modes exercise accepting, forwarding and
teardown on a single machine, because Steam refuses a connection to your own
account.

## Diagnostics

Every step writes to `BattleSpadesClient.log` beside the executable:

```
[steam] runtime ready: app=480 id=76561198158362762 relays=ready
[steam] hosting over the relay network on virtual port 27015
[steam] player joined over the relay network, now 1 connected
[steam] joining 76561198158362762 failed: Steam refused the peer-to-peer connection
```

## Not done yet

- **Steam lobbies and invites.** Joining still needs the host's id passed by
  hand; the Steam friends list and overlay invites are the next step, and they
  would also replace the AoSPlay lobby round trips that players see as lag.
- **A measured relay session.** The transport is verified over a direct
  connection (20 of 20 datagrams, median 5.3 ms). Two accounts are needed to
  measure the relay path itself and compare it against the AoSPlay relay.
- **Dedicated servers keep their own path.** Relayed dedicated servers need
  Valve-side configuration that only the application's owner can request.
