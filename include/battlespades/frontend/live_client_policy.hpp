#pragma once

#include "battlespades/network/protocol168_weapons.hpp"

#include <cstdint>
#include <optional>

namespace battlespades::frontend {

/** Spectators still publish scene readiness, but never character input. */
[[nodiscard]] constexpr bool local_player_is_spectator(
    std::uint8_t team, bool spectator_enabled) noexcept {
    return team == 0U && spectator_enabled;
}

/**
 * A neutral packet keeps the post-map roster reveal progressing even before
 * a spectator has a chase target. It deliberately contains no tool actions,
 * movement, palette state, or weapon-display flag.
 */
[[nodiscard]] inline network::ClientDataPacket spectator_client_data(
    std::int32_t loop_count, std::uint8_t player_id,
    std::array<float, 3U> orientation) noexcept {
    network::ClientDataPacket packet;
    packet.loop_count = loop_count;
    packet.player_id = player_id;
    packet.orientation = orientation;
    packet.opaque_state = network::protocol168_client_data_opaque_state(loop_count);
    return packet;
}

/**
 * Whether the fixed-step world should continue below a frontend overlay.
 *
 * Multiplayer authority never pauses. Escape/settings/team screens release
 * input, but gravity, prediction, ClientData cadence, and reconciliation must
 * continue until the world route is actually removed.
 *
 * Window state is deliberately NOT an input: retail ran
 * `schedule_interval_soft(manager.update)` regardless of minimise/alt-tab, and
 * SDL minimises exclusive-fullscreen windows on focus loss. Stopping here froze
 * the body, starved the server of ClientData/ClockSync and stalled the
 * connection watchdog on every alt-tab. A minimised window only skips
 * presentation (see live_world_presentation_allowed).
 */
[[nodiscard]] constexpr bool live_world_simulation_allowed(
    bool has_session, bool world_in_navigation_stack) noexcept {
    return has_session && world_in_navigation_stack;
}

/** Only presentation stops while the platform window is minimised. */
[[nodiscard]] constexpr bool live_world_presentation_allowed(
    bool window_suspended, bool present_this_tick) noexcept {
    return !window_suspended && present_this_tick;
}

/**
 * Whether an authoritative health sample crosses a confirmed local life into
 * death. Lobby/pre-spawn rows are deliberately inert until CreatePlayer begins
 * the life, preventing a dead menu replica from fabricating a grave camera.
 */
[[nodiscard]] constexpr bool should_present_local_death(
    bool spawn_confirmed, bool was_alive, std::int16_t health) noexcept {
    return spawn_confirmed && was_alive && health <= 0;
}

/**
 * Whether one CreatePlayer packet is the local authoritative life boundary.
 *
 * Remote players may respawn in the same packet drain as the owner. Their
 * presentation/model refresh must never snap local physics or clear the local
 * death overlay. Conversely, a local CreatePlayer must complete the life even
 * when an unrelated remote mesh fails to load.
 */
[[nodiscard]] constexpr bool is_local_create_player_boundary(
    std::uint8_t created_player_id,
    std::optional<std::uint8_t> local_player_id,
    bool has_local_session) noexcept {
    return has_local_session && local_player_id.has_value() &&
           created_player_id == *local_player_id;
}

/**
 * Packet 77 changes an existing player's team. Initial SelectTeam only stages
 * packet 15 and therefore must not mutate the server before class confirmation.
 */
[[nodiscard]] constexpr bool should_send_change_team(
    bool initial_join) noexcept {
    return !initial_join;
}

/**
 * Whether developer-only UI and state mutation may run in this process.
 *
 * Debug inventory grants, entity spawning, class cycling and parity screens
 * are local-world tools, not Protocol 168 features. Deny them as soon as a
 * remote/local-server connection exists or is pending; waiting until the
 * bootstrap declares `network_match` leaves an authorization gap during
 * loading and identity-ticket acquisition.
 */
[[nodiscard]] constexpr bool offline_developer_tools_allowed(
    bool network_match, bool live_transport_exists,
    bool connection_pending) noexcept {
    return !network_match && !live_transport_exists && !connection_pending;
}

} // namespace battlespades::frontend
