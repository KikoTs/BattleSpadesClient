BattleSpades Beta 0.3 (0.3.0-beta.1) adds original Classic servers, Workshop maps,
Steam account sign-in and Discord joining, with compatibility and stability fixes.

## Classic 0.75 and 0.76

- Browse and join original AoS servers alongside BattleSpades servers. Protocol
  detection keeps Classic 0.75, Classic 0.76 and BattleSpades Classic+ distinct.
- Load original VXL maps, retain custom team colors and use the game's existing
  objectives, markers, scoreboard and HUD.
- Correct weapon spread, recoil, ammunition, reload timing and tool cooldowns
  against ZeroSpades. Hold right-click for one second to dig three blocks with
  the shovel; tapping cancels, and left-click takes priority when both are held.
- Measure Classic server ping with its original UDP query and show unknown
  values honestly. Improve spawn recovery, map changes and plugin packet handling.
- Add corpse gravity, configurable ragdolls, shot reactions, blood effects and
  local music options, with further collision and stability fixes.

## Maps and Workshop

- Browse Workshop maps in game with sorting, search, filters, preview images and
  details, using the game's existing menu design.
- Download and load accessible Workshop maps, synchronize subscriptions and
  preserve the distinction between public content and Steam-only downloads.
- Import legacy VXL maps directly for local hosting and map selection.

## Accounts, Steam and Discord

- Automatically sign in through Steam when available and display the verified
  Steam name. Existing saved accounts can be explicitly linked while retaining
  their progress; usernames do not replace stable account IDs or SteamIDs.
- Save recovery information privately under Documents/BattleSpades. Guest,
  account and recovery options remain available when Steam cannot authenticate.
- Correct retail Steam ticket encoding and AGE X loadout/rule parsing. Successful
  authenticated map handshakes were verified with AGE X #1 and #2; ownership
  requirements still apply. Their Frankfurt test endpoint also rejects retail.
- Add Discord Rich Presence, server details and validated quick-join actions,
  with separate activity-sharing and joining settings.

## Platforms and updates

- Target macOS 11 and newer, with bundled-library compatibility checks.
- Publish Windows, Linux and macOS builds for x64 and ARM64, plus the Windows
  installer and component update packages. SHA256SUMS covers release downloads.
- Preserve the existing updater feed, component layout, settings and imported
  retail assets. Update metadata is published after its downloads are verified.

Protocol 168 remains unchanged. Use the matching server release for hosting:
https://github.com/KikoTs/BattleSpades/releases/tag/v0.3.0-beta.1

Classic regression tests and local piqueserver exercises cover the implemented
mechanics. The 0.76 transport fixture is not certification of every public server
or anti-cheat plugin. See docs/CLASSIC_PROTOCOL.md for validation and limits.

Unsigned builds; verify downloads with SHA256SUMS.
