#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
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
    [[nodiscard]] SteamRelayStatus relay_status() const;
    [[nodiscard]] std::string last_error() const;

    struct Impl;
    [[nodiscard]] Impl* impl() const noexcept { return impl_.get(); }

private:
    std::unique_ptr<Impl> impl_;
};

struct SteamP2PHostConfig final {
    /** The bundled server's loopback port. */
    std::uint16_t local_server_port{};
    /** Shared with joining clients; distinct services may share one Steam id. */
    int virtual_port{27015};
    std::size_t maximum_clients{24U};
    std::chrono::seconds client_idle_timeout{120};
    /**
     * Listen on this loopback port instead of the relay network. Zero keeps
     * the shipping path; the smoke check uses it to exercise accepting,
     * forwarding and teardown on one machine, where Steam refuses a
     * peer-to-peer connection to your own account.
     */
    std::uint16_t direct_listen_port{};
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
    int virtual_port{27015};
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
    [[nodiscard]] std::string last_error() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace battlespades::platform
