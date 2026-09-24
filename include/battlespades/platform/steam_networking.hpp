#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace battlespades::platform {

/**
 * Steam runtime used for peer-to-peer transport and for the player's presence.
 *
 * Ace of Spades comes first so owners appear in Steam as playing it. Steam
 * refuses an application id the account does not own, and that refusal selects
 * `fallback_app_id`: Spacewar (480), which every account owns, so players
 * without the game still play.
 *
 * Networking is per application id, so the two do not meet: a player on
 * Spacewar cannot join a host running as Ace of Spades. Setting `app_id` to
 * `fallback_app_id` puts everyone in the Spacewar network instead.
 */
struct SteamNetworkingRuntimeConfig final {
    std::uint32_t app_id{224540U};
    /** Used when the account does not own `app_id`; zero refuses the fallback. */
    std::uint32_t fallback_app_id{480U};
    /** Explicit library path; empty searches `search_directory` and the default name. */
    std::filesystem::path library;
    /** Usually the executable's directory, where packaged builds install it. */
    std::filesystem::path search_directory;
    std::chrono::seconds relay_timeout{20};
};

/** Live relay-network state for the loader and diagnostics. */
struct SteamRelayStatus final {
    bool available{};
    /** Valve's own summary, for example "OK. Relays: 23 valid, 1 great". */
    std::string detail;
};

/**
 * Owns SteamAPI for this process and pumps its callbacks.
 *
 * The library is loaded at run time, so a build without Steam installed still
 * starts: start() fails and the caller falls back to the AoSPlay relay.
 */
class SteamNetworkingRuntime final {
public:
    SteamNetworkingRuntime();
    ~SteamNetworkingRuntime();

    SteamNetworkingRuntime(const SteamNetworkingRuntime&) = delete;
    SteamNetworkingRuntime& operator=(const SteamNetworkingRuntime&) = delete;

    [[nodiscard]] bool start(SteamNetworkingRuntimeConfig config, std::string& error);
    void stop() noexcept;

    [[nodiscard]] bool ready() const noexcept;
    /** The player's Steam id, which a joining client uses as the address. */
    [[nodiscard]] std::uint64_t steam_id() const noexcept;
    [[nodiscard]] std::string persona_name() const;
    /** The application id Steam accepted, which may be `fallback_app_id`. */
    [[nodiscard]] std::uint32_t app_id() const noexcept;
    /**
     * False while attached as Spacewar.
     *
     * Statistics and achievements live in the schema of the application id the
     * process attached as, so reporting ours under Spacewar would write into
     * Valve's test app. A player without the game still plays; nothing about
     * their match is tracked.
     */
    [[nodiscard]] bool tracking_enabled() const noexcept;
    /**
     * Show the match in the player's friends list and let a friend join it.
     *
     * `status` is the line under "view game info". `connect` is the command
     * line Steam gives a friend who clicks Join, so `steam:<id>` reaches the
     * same loader as Direct Connect. Both keys are free-form and need nothing
     * configured for the application id.
     */
    [[nodiscard]] bool publish_presence(const std::string& status, const std::string& connect);
    void clear_presence() noexcept;
    /**
     * Statistics and achievements for the attached application.
     *
     * Every one of these is a no-op returning false unless tracking_enabled().
     * A name must exist in that application's schema, which is defined by
     * whoever owns the id, so unlock_achievement() reports a refusal rather
     * than pretending the name was written.
     */
    [[nodiscard]] bool unlock_achievement(const std::string& name);
    /** Shows Steam's progress toast; Steam ignores a completed target. */
    [[nodiscard]] bool report_achievement_progress(const std::string& name,
                                                   std::uint32_t progress,
                                                   std::uint32_t target);
    [[nodiscard]] bool set_statistic(const std::string& name, std::int32_t value);
    [[nodiscard]] std::optional<std::int32_t> statistic(const std::string& name) const;
    /** Sends everything set since the last store; unlocks store themselves. */
    [[nodiscard]] bool store_statistics();
    [[nodiscard]] SteamRelayStatus relay_status() const;
    /**
     * Block until the relay network is usable.
     *
     * A start() that passed a zero relay timeout returns before the relays are
     * up, so a host or a join made straight afterwards waits here instead of
     * opening a connection Steam cannot route yet.
     */
    [[nodiscard]] bool wait_for_relays(std::chrono::seconds timeout);
    [[nodiscard]] std::string last_error() const;

    struct Impl;
    [[nodiscard]] Impl* impl() const noexcept { return impl_.get(); }

private:
    std::unique_ptr<Impl> impl_;
};

struct SteamP2PHostConfig final {
    /** The bundled server's loopback port. */
    std::uint16_t local_server_port{};
    /**
     * Shared with joining clients; distinct services may share one Steam id.
     *
     * Zero is the single-service case Valve documents. A value must match the
     * joining client's and stay below 1000; a UDP port number such as 27015 is
     * outside the range Steam routes, and a connection to it is accepted
     * locally and then never routed.
     */
    int virtual_port{};
    std::size_t maximum_clients{24U};
    std::chrono::seconds client_idle_timeout{120};
    /**
     * Listen on this loopback port instead of the relay network. Zero keeps
     * the shipping path; the smoke check uses it to exercise accepting,
     * forwarding and teardown on one machine, where Steam refuses a
     * peer-to-peer connection to your own account.
     */
    std::uint16_t direct_listen_port{};
    /**
     * AoSPlay server id the local server registered under, sent to every
     * joiner once its relay connection is up. A public match admits only
     * players holding a join ticket for that id, and the joiner has no other
     * way to learn it. Empty when the match needs no identity.
     */
    std::string server_identifier;
};

/**
 * Accepts players over Valve's relay network and feeds them to the local
 * server. One loopback socket per remote player keeps ENet's peers distinct,
 * exactly like the AoSPlay relay tunnel it replaces.
 */
class SteamP2PHost final {
public:
    SteamP2PHost();
    ~SteamP2PHost();

    SteamP2PHost(const SteamP2PHost&) = delete;
    SteamP2PHost& operator=(const SteamP2PHost&) = delete;

    [[nodiscard]] bool start(SteamNetworkingRuntime& runtime, SteamP2PHostConfig config,
                             std::string& error);
    void stop() noexcept;
    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] std::size_t connected_clients() const noexcept;
    [[nodiscard]] std::string last_error() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct SteamP2PClientConfig final {
    std::uint64_t host_steam_id{};
    /** Must match the host's; see SteamP2PHostConfig::virtual_port. */
    int virtual_port{};
    std::chrono::seconds connect_timeout{20};
    /** Loopback port of a direct_listen_port host; zero keeps the relay path. */
    std::uint16_t direct_connect_port{};
};

/**
 * Joins a host over Valve's relay network and presents it as a loopback
 * endpoint, so the ordinary Protocol 168 client connects without knowing that
 * Steam carries the datagrams.
 */
class SteamP2PClient final {
public:
    SteamP2PClient();
    ~SteamP2PClient();

    SteamP2PClient(const SteamP2PClient&) = delete;
    SteamP2PClient& operator=(const SteamP2PClient&) = delete;

    [[nodiscard]] bool start(SteamNetworkingRuntime& runtime, SteamP2PClientConfig config,
                             std::string& error);
    void stop() noexcept;
    [[nodiscard]] bool running() const noexcept;
    /** Loopback port the game connects to; zero until start() succeeds. */
    [[nodiscard]] std::uint16_t local_port() const noexcept;
    /** Round trip through the relays in milliseconds, negative when unknown. */
    [[nodiscard]] int ping_milliseconds() const noexcept;
    /** True once Steam reports the relay connection to the host established. */
    [[nodiscard]] bool connected() const noexcept;
    /**
     * Block until the host's hello arrives, at most ``timeout``.
     *
     * The hello carries the AoSPlay server id a joiner must hold a ticket for
     * (empty when the match needs none). Nullopt means the tunnel failed or
     * the host never answered; ``last_error()`` says which.
     */
    [[nodiscard]] std::optional<std::string> wait_for_host_hello(std::chrono::seconds timeout);
    [[nodiscard]] std::string last_error() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace battlespades::platform
