# Contributing to BattleSpadesClient

Thanks for helping keep _Ace of Spades_ playable. Bug reports, fixes, parity
research, translations and platform work are all welcome.

## Before you start

- For anything larger than a small fix, open an issue first (or ask on
  [Discord](https://discord.gg/aosbb)) so the work is not duplicated.
- Retail parity is the default. A change that alters gameplay or presentation
  should say what the original game does and how that was verified. See the
  [compatibility contract](docs/COMPATIBILITY.md).
- Security problems go through [SECURITY.md](SECURITY.md), not public issues.

## Building and testing

Follow [Building from source](README.md#building-from-source) and the
[runbook](docs/RUNBOOK.md). Before opening a pull request:

```sh
cmake --build --preset native-dev        # or your platform's preset
ctest --preset native-dev --output-on-failure
```

All tests must pass. Add or update tests for the behaviour you change. Some
tests need the original game files imported into `assets/original/` and the
server repository cloned beside this one (`../BattleSpades`).

## Code style

- C++20. Format with the repository's [`.clang-format`](.clang-format); keep
  warnings clean (`AOS_WARNINGS_AS_ERRORS` is on in the presets).
- Follow the structure of the surrounding code. Subsystem ownership is
  described in [ARCHITECTURE.md](docs/ARCHITECTURE.md).
- Comments explain why, not what. Keep commit messages descriptive: one short
  summary line, then the reason for the change.
- Update the relevant guide in `docs/` when behaviour, commands or file formats
  change.

## Content and licensing rules

These rules protect the project. Pull requests that break them cannot be
merged.

- **No original game content.** Do not commit assets, executables, Python
  modules, `.pyd` files, decompiled source or IDA databases from the original
  game, nor files converted from them. Original content stays in the
  git-ignored `assets/original/`.
- **No copied code under incompatible licences.** OpenSpades (GPL-3.0) and
  similar projects may be studied for techniques, but their code must not be
  copied or translated into the client.
- **Third-party code and art** must come with a licence that permits
  redistribution, recorded in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)
  or the cosmetics credit files. Name the source and licence in the pull
  request.
- **No secrets.** Never commit tokens, keys, passwords, `.env` files or
  personal data.

By submitting a contribution you agree to the terms in
[LICENSING.md](LICENSING.md#contributions): your contribution is licensed
under AGPL-3.0-or-later (including the Steamworks permission), and you grant
the maintainer the additional licence described there.
