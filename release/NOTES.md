BattleSpades Client 0.3.0-beta.2 improves Classic gameplay, graphics and inventory discovery.

## Classic fixes

- Right-click digging now winds up while you hold the shovel, reaches the block at the one-second dig deadline, and recovers afterward. Primary attack timing is unchanged.
- Block-line dragging starts correctly when right-click is held through a placement cooldown. Release commits the line; cancelling or changing tools discards it. Lines use the original protocol packet and server validation.
- Fix Compatibility Shader corruption caused by a disabled Classic light with a zero direction.
- Limit Classic 0.75 visibility to 128 horizontal blocks (or your lower graphics setting), with the original squared-distance fog curve at every shader quality. Classic 0.76 and Classic+ keep their existing distance settings.

## Inventory and Classic atmosphere

- Open Inventory directly from the main menu. New hints explain skins, levels, XP and crates; player statistics remain available.
- Choose a Classic sky preset, or a random sky from a family selected using the map's surface colors.
- Classic fog defaults to gray. Settings also offer server/map fog, sky-matched fog and custom RGB. Sky and fog changes preview immediately, and Cancel restores the previous appearance.
- New settings and hints are localized in all 14 supported languages.

## Updates and validation

This is a client-only update. Protocol 168 is unchanged and the existing server 0.3.0-beta.1 remains compatible. The updater keeps its component layout and preserves player settings, custom maps and imported retail assets.

Graphics regression tests and real Classic-map captures were run on D3D11, D3D12, Vulkan and OpenGL on Windows, with Compatibility and enhanced rendering. Metal/ESSL shaders are compiled; this Windows machine cannot provide a Metal runtime visual check. Local piqueserver validates block lines, hits, builds, digs, grenades, reloads and map rotation without corrections or cheat reports. The separate 0.76 loopback fixture covers negotiation, plugin replies and redirects.

Unsigned builds; verify downloads with SHA256SUMS.
