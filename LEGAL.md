# Legal notice

This page summarises the legal position of BattleSpadesClient. It is not legal
advice. The licence itself is in [`LICENSE`](LICENSE); licensing details are in
[`LICENSING.md`](LICENSING.md) and third-party terms in
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

## Unofficial fan project

BattleSpadesClient is an unofficial, non-commercial fan project of the AoS
Revival community. It is **not affiliated with, endorsed by or sponsored by
Jagex Ltd or Valve Corporation**, and neither company is responsible for it or
supports it.

## Trademarks

- _Ace of Spades_ is a trademark of Jagex Ltd.
- Steam, Steamworks and the Steam logo are trademarks or registered trademarks
  of Valve Corporation.
- All other trademarks are the property of their respective owners.

These names are used only to describe compatibility with the original game and
platform. Their use does not imply any endorsement. The AGPL grants no
trademark rights; see [`LICENSING.md`](LICENSING.md#names-and-trademarks) for
the project's own names.

## No original game content

- The source tree and source archives of this repository contain **no
  assets, executables or code from the original game**.
  Original content stays on the player's machine: the asset importer copies it
  from the player's own installation into the git-ignored `assets/original/`
  folder.
- [`assets/catalog/original-assets.json`](assets/catalog/original-assets.json)
  lists only paths, sizes and SHA-256 hashes, used to verify an import.
- A few community cosmetics reused files that are byte-identical to original
  content. Those files were removed;
  [`assets/client/cosmetics/retail-substitutes.json`](assets/client/cosmetics/retail-substitutes.json)
  points each one at the player's own imported copy.
- Identifiers, constants, layouts and protocol details of the original game
  appear in the source where they are needed for interoperability. The facts
  they describe are not claimed by this project; any original text reproduced
  verbatim belongs to its owner.

## How the client was made

The client is an independent implementation. The original game's observable
behaviour, network protocol and file formats were studied so the client can
work with existing servers, maps and player-owned content. No original game
code is included or translated into this repository. Reference projects with
incompatible licences (for example OpenSpades, GPL-3.0) are used only to study
techniques; their code is not copied into the client.

## Takedown requests

If you are a rights holder and believe that material in this repository
infringes your rights, please contact:

- Email: `<CONTACT EMAIL TO BE FILLED IN>`
- or open an issue on GitHub titled **"Takedown request"** (do not include
  personal data you would not want to be public).

Please identify the material (file path or URL) and the work you believe it
infringes. Valid requests are acted on promptly, normally by removing the
material from the current tree and, where needed, from the repository history.

## AGPL and network use

BattleSpadesClient is licensed under AGPL-3.0-or-later with an additional
permission for the Steamworks SDK (see [`LICENSING.md`](LICENSING.md)).

- **Binary releases.** Every release is built from a tagged commit of this
  public repository, and the release page links to that source. If you
  redistribute a build, you must offer the complete corresponding source of
  that build in the same way.
- **Network use (AGPL section 13).** The client itself does not provide a
  network service. The BattleSpades server that the client starts for Create
  Match and Map Creator is a separate program under its own AGPL licence; its
  source is published at <https://github.com/KikoTs/BattleSpades>. If you
  modify that server and let others play on it, offer them its source (the
  server does this through its join message). The same applies to modified
  relays or lobby services.
- **Warranty.** The software is provided without any warranty (AGPL sections 15
  and 16).
