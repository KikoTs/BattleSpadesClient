# Licensing

Copyright (c) 2026 Kiril Tsanov (KikoTs)

BattleSpadesClient is free software licensed under the
**GNU Affero General Public License, version 3 or (at your option) any later
version** (SPDX: `AGPL-3.0-or-later`), the same license as the
[BattleSpades server](https://github.com/KikoTs/BattleSpades). The full,
unmodified license text is in [`LICENSE`](LICENSE).

This page is a short plain-English summary. It is not legal advice and does not
replace the license; if they disagree, `LICENSE` wins.

## What the AGPL means here

- **You may** use, build, run, study, copy, modify and share the client for
  any purpose, including commercially.
- **If you share a build** (modified or not), you must license it under
  AGPL-3.0-or-later, include the license and notices, provide the complete
  corresponding source (for example a public repository linked from the
  download page), and mark your changes.
- **Network use (section 13).** If you modify code that other people use over
  a network, such as the bundled local server behind "Create Match", a relay or
  a lobby service, you must offer those users its complete source. For the
  dedicated server this is usually done with a link in its join message; see
  the server's [`LICENSING.md`](https://github.com/KikoTs/BattleSpades/blob/main/LICENSING.md).
- **Legal notices in the UI (section 5(d)).** If a version of the client shows
  "Appropriate Legal Notices" (copyright, no warranty, AGPL license and how to
  view it) in its interface, for example on an About or credits screen,
  modified versions you distribute must keep showing them.
- There is **no warranty** (sections 15 and 16).

The client has never had another license: before the AGPL was added, the
repository had no license file (all rights reserved). AGPL-3.0-or-later now
applies to all of its source, including earlier commits and releases.

## Additional permission for Steamworks (AGPL section 7)

As an additional permission under section 7 of the GNU Affero General Public
License version 3, the copyright holder gives you permission to combine or
link BattleSpadesClient with Valve Corporation's Steamworks SDK (its headers
and runtime libraries such as `steam_api64.dll`, `libsteam_api.dylib` and
`libsteam_api.so`), and to convey the resulting combination, even though those
components are not licensed under the AGPL. You must follow the AGPL for every
part of the work other than those Valve components, and you must follow Valve's
own terms for the Valve components. If you modify BattleSpadesClient, you may
extend this permission to your version, but you are not obliged to.

## Names and trademarks

The AGPL grants no trademark rights. The names **BattleSpades**,
**BattleSpadesClient**, **Ace of Spades Revival**, **AoS Revival** and
**AoSPlay**, and their logos, are not licensed. You may say truthfully that
your client is based on BattleSpadesClient, but please use your own name for
modified builds and don't present them as official or endorsed. "Ace of
Spades" belongs to its respective owners; this project is not affiliated with
them.

## Third-party components, cosmetics and game content

- Libraries keep their own AGPL-compatible licenses; see
  [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
- **Community cosmetics** in `assets/client/cosmetics/` belong to their
  authors. Many are *non-commercial, no-derivatives* (CC BY-NC-ND 4.0 or the
  OpenSpades Non-GPL Pak License). They are separate works shipped alongside
  the program, are **not** covered by the AGPL, and may not be used
  commercially even though the client code may. Remove them from any build you
  sell.
- **Retail Ace of Spades content** is never shipped. The asset installer copies
  it from the player's own installation; it remains the property of its owners.

## Applying the license in files

New source files may carry this header:

```
// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Kiril Tsanov
```

## Contributions

By submitting a contribution you agree that:

- it is licensed to everyone under AGPL-3.0-or-later, including the section 7
  Steamworks permission above;
- you also grant Kiril Tsanov a perpetual, worldwide, royalty-free,
  irrevocable license to use, modify, distribute and relicense your
  contribution as part of BattleSpadesClient under other terms, so the project
  can keep making licensing decisions in one place. You keep your copyright;
  and
- you have the right to submit it. Point out any third-party material in the
  pull request together with its license, which must be AGPL-compatible (or,
  for art and sound, clearly separate and redistributable).

## Questions

- GitHub: [KikoTs](https://github.com/KikoTs)
- Discord: <https://discord.gg/aosbb>
