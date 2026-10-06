#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

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

/** A friend in a match this player can join, read from their presence. */
struct SteamFriendMatch final {
    std::uint64_t steam_id{};
    std::string persona;
    /** The host's own line: map and mode. */
    std::string status;
    /** The address to dial, `steam:<id>`. */
    std::string connect;
};

/**
 * One game server from Steam's own server list (ISteamMatchmakingServers),
 * the list the original game browsed. Steam answers it even where our web
 * services are blocked, so the browser merges it with the AoSPlay list.
 */
struct SteamListedGameServer final {
    /** IPv4 in host byte order. */
    std::uint32_t ip{};
    std::uint16_t port{};
    std::uint16_t query_port{};
    std::string name;
    std::string map;
    /** Semicolon-separated server tags (v168;region=europe;mode=0001;...). */
    std::string tags;
    std::uint16_t players{};
    std::uint16_t maximum_players{};
    std::uint16_t bots{};
    bool password{};
    std::uint64_t steam_id{};
    int ping{};
};

/** One friends-only lobby Steam offers, as the browser would show it. */
struct SteamLobbyListing final {
    std::uint64_t lobby_id{};
    /** What the host published: map and mode, the friends-list line. */
    std::string status;
    /** The address to dial, `steam:<id>`, as rich presence spells it. */
    std::string connect;
    int members{};
};

/** A Steam Join Game, accepted invite or relaunch delivered to this process. */
struct SteamJoinRequest final {
    /** A connect value or launch command line; empty for a lobby request. */
    std::string connect;
    std::uint64_t lobby_id{};
    std::uint64_t friend_id{};
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
     * line Steam gives a friend who clicks Join (appended to the launch
     * command line when the friend's game is closed, or delivered as a
     * GameRichPresenceJoinRequested_t while it runs), so it must be a whole
     * switch: "+connect steam:<id>". Both keys are free-form and need nothing
     * configured for the application id. `group` (steam_player_group) puts
     * everyone in one match together in the friends list; empty leaves it
     * unset.
     */
    [[nodiscard]] bool publish_presence(const std::string& status, const std::string& connect,
                                        const std::string& group = {});
    void clear_presence() noexcept;
    /**
     * Join requests Steam delivered to this process, oldest first.
     *
     * GameRichPresenceJoinRequested_t (a friend's Join Game or an accepted
     * invite) carries the connect value; GameLobbyJoinRequested_t carries a
     * lobby id instead, and NewUrlLaunchParameters_t (Steam relaunching the
     * running game with new arguments) the new launch command line.
     */
    [[nodiscard]] std::vector<SteamJoinRequest> take_join_requests();
    /** Arguments Steam launched this process with (steam://run/…//args). */
    [[nodiscard]] std::string launch_command_line() const;
    /**
     * Read the connect value of a lobby this player was invited to.
     *
     * Joins the lobby, reads "connect" (falling back to "+connect
     * steam:<owner>"), and leaves again. Blocks up to `timeout`; call it off
     * the presentation thread. Empty when Steam refused or did not answer.
     */
    [[nodiscard]] std::string resolve_lobby_connect(std::uint64_t lobby,
                                                    std::chrono::seconds timeout =
                                                        std::chrono::seconds{8});
    /** True when Steam's in-game overlay is running in this process. */
    [[nodiscard]] bool overlay_enabled() const;
    /**
     * True while Steam's overlay is open over the game (GameOverlayActivated_t).
     *
     * The game must not hold the mouse or read gameplay input meanwhile; see
     * SteamOverlayInputGate, which pairs this with overlay_activations().
     */
    [[nodiscard]] bool overlay_active() const noexcept;
    /** Overlay activations reported since start(); it only grows. */
    [[nodiscard]] std::uint64_t overlay_activations() const noexcept;
    /**
     * Open Steam's invite dialog for the current match.
     *
     * Uses the connect-string invite when this Steam client supports it, the
     * lobby invite otherwise. False when the overlay is not in this process
     * (the game was not launched through Steam), in which case the friends
     * list's own "Invite to Game" still works because presence carries connect.
     */
    [[nodiscard]] bool open_invite_dialog(const std::string& connect, std::uint64_t lobby);
    /**
     * Statistics and achievements for the attached application.
     *
     * Every one of these is a no-op returning false unless tracking_enabled().
     * A name must exist in that application's schema, which is defined by
     * whoever owns the id, so unlock_achievement() reports a refusal rather
     * than pretending the name was written.
     */
    /**
     * Open a friends-only lobby carrying this match, and return its id.
     *
     * A lobby needs no public address and no application configuration, so it
     * reaches players behind any NAT. `status` and `connect` are stored on it
     * with the same meaning as in rich presence, so a friend can read where to
     * go. Zero means Steam refused or did not answer in time.
     *
     * This blocks briefly: creating a lobby is asynchronous, and the pump
     * collects the answer.
     */
    [[nodiscard]] std::uint64_t create_lobby(const std::string& status,
                                             const std::string& connect,
                                             int maximum_members = 24,
                                             std::chrono::seconds timeout =
                                                 std::chrono::seconds{10});
    /**
     * Friends who are in a match right now, with the address to join them.
     *
     * This is the list a player wants, and the one that works: Steam serves a
     * friend's rich presence for anyone running the same application, so a
     * friends-only host is visible here even though no lobby search can see it.
     */
    [[nodiscard]] std::vector<SteamFriendMatch> friend_matches() const;
    /**
     * Starts a Steam internet server list query for `app_id` (Ace of Spades
     * is 224540), replacing any query in flight. Works while attached as
     * Spacewar too: the list is per queried app, not per attached app.
     */
    bool begin_internet_server_query(std::uint32_t app_id);
    /** Servers that have answered so far. */
    [[nodiscard]] std::vector<SteamListedGameServer> internet_servers() const;
    /** True once Steam finished the query (or there is none). */
    [[nodiscard]] bool internet_server_query_done() const;
    void cancel_internet_server_query();
    /**
     * The friends-only lobbies this account can see, newest offer first.
     *
     * Note that Steam's lobby search only returns public lobbies, so a
     * friends-only match never appears here; friend_matches() is the route for
     * those. Kept for a future public playlist.
     *
     * Only lobbies carrying a connect value are returned, because anything else
     * is not a match of ours. Blocks briefly while Steam answers, as creating
     * one does.
     */
    [[nodiscard]] std::vector<SteamLobbyListing> list_lobbies(
        int maximum = 50, std::chrono::seconds timeout = std::chrono::seconds{10});
    [[nodiscard]] std::string lobby_data(std::uint64_t lobby, const std::string& key) const;
    void leave_lobby(std::uint64_t lobby) noexcept;
    [[nodiscard]] bool unlock_achievement(const std::string& name);
    /**
     * The retail achievements Steam holds as unlocked for this player, each
     * with its unlock time in seconds since the Unix epoch (zero if unknown).
     *
     * This is a record of what was earned on the retail servers and nothing
     * can add to it: Steam's schema reserves every Ace of Spades achievement
     * for the publisher's game servers. A client write is refused outright and
     * a community game server's store is answered with access denied (both
     * measured against the live application). Empty unless tracking_enabled().
     */
    [[nodiscard]] std::vector<std::pair<std::string, std::int64_t>> unlocked_achievements() const;
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
    /**
     * An export of the loaded Steamworks library (for example
     * "SteamAPI_SteamUGC_v021"), so a feature can keep its flat-API calls in
     * its own file; see steam_workshop.cpp. Null before start() or when the
     * library lacks it.
     */
    [[nodiscard]] void* steamworks_symbol(const char* name) const noexcept;

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
    /**
     * How long Steam may take to find a route before giving up.
     *
     * Thirty seconds rather than Steam's default ten: two peers that both have
     * to set up relay sessions were once seen accepted on the host at the very
     * moment the joiner gave up. This is passed as the connection's initial
     * timeout, so lowering it shortens that window again.
     */
    std::chrono::seconds connect_timeout{30};
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
