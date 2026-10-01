# Security policy

## Supported versions

Only the latest release and the `main` branch receive security fixes. The
launcher updates installed copies automatically, so fixes reach players
through a normal release.

## Reporting a vulnerability

Please report security problems **privately**. Do not open a public issue for
them.

- Preferred: GitHub's private vulnerability reporting, on this repository's
  **Security** tab, under **Report a vulnerability**.
- Alternatively, email `<SECURITY CONTACT TO BE FILLED IN>`, or send a direct
  message to a maintainer on [Discord](https://discord.gg/aosbb) asking for a
  private channel.

Include the affected version or commit, the platform, steps to reproduce, and
what an attacker could achieve. A proof of concept helps. You will get an
acknowledgement within a week. Once a fix is released, the issue can be
disclosed, and reporters are credited unless they prefer otherwise.

## Scope

Areas where a bug can affect players include:

- parsing of anything that comes from the network: game servers (Protocol 168
  packets and maps), the server list, lobbies and relays;
- the updater and launcher: update manifests, downloads, archive extraction
  and file replacement;
- content from other players: Workshop maps, cosmetic packs and their
  AngelScript presentation scripts, which run in a restricted sandbox;
- Steam integration: tickets, invites and the Steam bridge;
- local files: settings, saved maps and identity state.

Out of scope: bugs in the original game, in third-party services such as
Steam, and cheating that does not compromise another player's machine or
account. Report problems with the game server to the
[BattleSpades](https://github.com/KikoTs/BattleSpades) repository.
