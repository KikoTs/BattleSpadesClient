#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stop_token>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace battlespades::network {

/** Retail's own default game port. */
inline constexpr std::uint16_t retail_game_port{32887U};
/**
 * The port an address without one means. BattleSpades servers listen on 27015
 * (the server's config default and the whole public fleet), so a bare host in
 * Direct Connect, `--connect` or a join link resolves to it. An explicit
 * `host:port` is always taken as typed.
 */
inline constexpr std::uint16_t default_game_port{27015U};

/** A validated IPv4/DNS endpoint accepted by the retail direct-connect flow. */
struct ServerEndpoint final {
    std::string host;
    std::uint16_t port{default_game_port};

    [[nodiscard]] std::string identifier() const;
    [[nodiscard]] friend bool operator==(const ServerEndpoint&, const ServerEndpoint&) = default;
};

/** Bounded metadata returned by AoSPlay or the server's HELLOLAN response. */
struct DiscoveredServer final {
    ServerEndpoint game;
    std::uint16_t query_port{};
    std::uint16_t ping_milliseconds{};
    std::string name;
    std::string map;
    std::string mode_code{"tdm"};
    std::string region{"europe"};
    std::string texture_skin;
    std::string master_identifier;
    std::uint16_t players{};
    std::uint16_t maximum_players{};
    bool classic{};
    bool official{};
    bool local{};
    bool identity_ticket{};
    /**
     * The host's Steam id when the listing carries one, so a join can take
     * Valve's relays and leave the AoSPlay relay as the fallback. Zero for a
     * dedicated server and for any host without a Steam session.
     */
    std::uint64_t steam_host_id{};
    /** Humans only, when the listing separates them from bots. */
    std::uint16_t human_players{};
    /** The listing says the server asks for a password (tag `password`). */
    bool password_protected{};
};

struct DiscoveryResult final {
    std::vector<DiscoveredServer> servers;
    std::string error;

    [[nodiscard]] explicit operator bool() const noexcept { return error.empty(); }
};

struct PublicDiscoveryConfig final {
    std::string url{"https://www.aosplay.net/serverlist/"};
    std::chrono::milliseconds timeout{5'000};
    std::size_t maximum_payload_bytes{1U << 20U};
    std::size_t maximum_servers{512U};
};

struct LanDiscoveryConfig final {
    std::vector<std::uint16_t> ports{27015U, 32887U};
    std::chrono::milliseconds timeout{650};
    std::size_t maximum_servers{128U};
    bool include_broadcast{true};
};

/** Parses `local`, `host`, `host:port`, or `aos://host:port` without DNS I/O. */
[[nodiscard]] bool parse_server_endpoint(std::string_view text,
                                         ServerEndpoint& endpoint,
                                         std::string& error,
                                         std::uint16_t default_port = default_game_port);

/** Strict parser kept public so malformed web/LAN responses are unit-testable. */
[[nodiscard]] DiscoveryResult parse_public_server_list(std::string_view json,
                                                       std::size_t maximum_servers = 512U);
[[nodiscard]] DiscoveryResult parse_lan_server_response(std::string_view json,
                                                        const ServerEndpoint& source,
                                                        std::uint16_t ping_milliseconds);
/** Resolve either an opaque AoSPlay identifier or an explicit endpoint key. */
[[nodiscard]] std::optional<DiscoveredServer> find_discovered_server(
    std::span<const DiscoveredServer> servers,
    std::string_view identifier);
/** Keep only servers whose AoSPlay or endpoint identifier belongs to a friend. */
[[nodiscard]] DiscoveryResult select_discovered_servers(
    DiscoveryResult source,
    std::span<const std::string> identifiers);

/** Blocking adapters. Call them only from a bounded discovery worker. */
[[nodiscard]] DiscoveryResult discover_public_servers(const PublicDiscoveryConfig& config = {},
                                                       std::stop_token stop = {});
[[nodiscard]] DiscoveryResult discover_lan_servers(const LanDiscoveryConfig& config = {});
[[nodiscard]] DiscoveryResult probe_lan_server(
    const ServerEndpoint& endpoint,
    std::chrono::milliseconds timeout = std::chrono::milliseconds{650});

} // namespace battlespades::network
