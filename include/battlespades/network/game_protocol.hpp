#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace battlespades::network {
enum class GameProtocol : std::uint16_t {
    automatic = 0,
    classic075 = 3,
    classic076 = 4,
    retail168 = 168
};
[[nodiscard]] constexpr bool is_classic_protocol(GameProtocol protocol) noexcept {
    return protocol == GameProtocol::classic075 || protocol == GameProtocol::classic076;
}
[[nodiscard]] constexpr std::string_view protocol_name(GameProtocol protocol) noexcept {
    switch (protocol) {
    case GameProtocol::classic075:
        return "Ace of Spades 0.75";
    case GameProtocol::classic076:
        return "Ace of Spades 0.76";
    case GameProtocol::retail168:
        return "BattleSpades";
    default:
        return "Automatic";
    }
}
/** Version-refusal fallback is only allowed before any application data.
 * Never retry bans, kicks, full servers, timeouts, or a connection already accepted. */
[[nodiscard]] constexpr std::optional<GameProtocol> next_protocol(
    GameProtocol attempted, std::uint32_t reason, bool received_application_data) noexcept {
    if (received_application_data || reason != 3U)
        return std::nullopt;
    if (attempted == GameProtocol::retail168)
        return GameProtocol::classic075;
    if (attempted == GameProtocol::classic075)
        return GameProtocol::classic076;
    return std::nullopt;
}
/** Some classic servers ignore ENet's requested version and immediately send
 * MapStart. Recognize only that complete first packet, then reconnect with an
 * explicit classic version; do not reinterpret a stream already in progress. */
[[nodiscard]] constexpr std::optional<GameProtocol> next_protocol_from_bootstrap(
    GameProtocol requested, GameProtocol attempted, bool received_application_data,
    std::span<const std::byte> packet) noexcept {
    if (requested != GameProtocol::automatic || attempted != GameProtocol::retail168 ||
        received_application_data || packet.size() != 5U || packet[0] != std::byte{18})
        return std::nullopt;
    std::uint32_t map_bytes{};
    for (unsigned i{}; i < 4U; ++i)
        map_bytes |= std::to_integer<std::uint32_t>(packet[i + 1U]) << (i * 8U);
    if (map_bytes == 0U || map_bytes > 80U * 1024U * 1024U)
        return std::nullopt;
    return GameProtocol::classic075;
}
} // namespace battlespades::network
