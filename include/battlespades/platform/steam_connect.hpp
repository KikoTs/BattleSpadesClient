#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace battlespades::platform {

/**
 * Where a Steam "Join Game", an accepted invite or a Steam launch sends us.
 *
 * Steam hands the game a friend's rich presence `connect` value, either as a
 * GameRichPresenceJoinRequested_t callback while the game runs or appended to
 * the command line when it launches the game. A lobby invite instead names a
 * lobby (GameLobbyJoinRequested_t, or `+connect_lobby <id>` on launch), whose
 * own `connect` value is read after joining it.
 */
enum class SteamJoinTargetKind : std::uint8_t {
    /** A player-hosted match reached over Valve's relays: `steam:<id>`. */
    steam_host,
    /** A dedicated server: `host:port`, validated by the loader. */
    endpoint,
    /** A Steam lobby whose `connect` value names the match. */
    lobby,
};

struct SteamJoinTarget final {
    SteamJoinTargetKind kind{SteamJoinTargetKind::steam_host};
    /** Host account id for steam_host, lobby id for lobby. */
    std::uint64_t steam_id{};
    /** `host:port` for endpoint. */
    std::string endpoint;
    /**
     * The server password from `+password <text>` behind the address, if the
     * link carried one. The client never publishes one in its own presence.
     */
    std::string password;

    [[nodiscard]] friend bool operator==(const SteamJoinTarget&,
                                         const SteamJoinTarget&) = default;
};

/** "steam:76561198…" (optionally behind "+connect "); zero when it is not one. */
[[nodiscard]] std::uint64_t parse_steam_host_address(std::string_view value) noexcept;

/**
 * Parse a connect string or a launch command line tail.
 *
 * Accepts `+connect steam:<id>`, `+connect host:port`, `+connect_lobby <id>`,
 * a bare `steam:<id>` (what builds before 2026-09-28 published) and a bare
 * `host:port`. Anything else is nullopt: a value Steam relays comes from
 * another player's presence and is never trusted beyond this shape check.
 * A `+password <text>` (or `--password`) pair behind a `+connect` target is
 * kept as the target's password; one that cannot be a password (over 64
 * bytes, control characters) refuses the whole value.
 */
[[nodiscard]] std::optional<SteamJoinTarget> parse_steam_join_target(std::string_view value);

/** The rich presence `connect` value for a Steam-hosted match. */
[[nodiscard]] std::string steam_host_connect_string(std::uint64_t host_steam_id);

/**
 * The rich presence `connect` value for a dedicated server.
 *
 * Nullopt for an address a friend cannot dial: empty, loopback, unspecified
 * or a zero port. A player on the local tunnel or their own local server must
 * advertise the Steam route instead.
 */
[[nodiscard]] std::optional<std::string> endpoint_connect_string(std::string_view host,
                                                                 std::uint16_t port);

/**
 * Drops the second copy of one join request.
 *
 * Both Steam processes (the in-process runtime and the 32-bit retail bridge)
 * are attached as the same application and may each be told about the same
 * click. A repeat of the last target inside the window is ignored.
 */
class SteamJoinDeduplicator final {
public:
    explicit SteamJoinDeduplicator(
        std::chrono::milliseconds window = std::chrono::milliseconds{5'000}) noexcept
        : window_{window} {}

    [[nodiscard]] bool accept(const SteamJoinTarget& target,
                              std::chrono::steady_clock::time_point now);

private:
    std::chrono::milliseconds window_;
    std::optional<SteamJoinTarget> last_;
    std::chrono::steady_clock::time_point last_at_{};
};

} // namespace battlespades::platform
