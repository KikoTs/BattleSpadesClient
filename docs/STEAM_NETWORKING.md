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
