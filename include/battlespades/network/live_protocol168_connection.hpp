#pragma once

#include "battlespades/network/enet_protocol168_client.hpp"
#include "battlespades/network/protocol168_players.hpp"
#include "battlespades/network/protocol168_session.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace battlespades::network {

enum class LiveProtocol168Phase : std::uint8_t {
    idle,
    connecting,
    handshaking,
    ready,
    disconnected,
    failed,
    stopped,
};

struct LiveProtocol168Status final {
    LiveProtocol168Phase phase{LiveProtocol168Phase::idle};
    std::size_t received_datagrams{};
    std::size_t sent_datagrams{};
    std::size_t queued_inbound{};
    std::size_t queued_outbound{};
    /** InitialInfo begins another map without replacing the ENet peer. */
    std::uint64_t map_generation{};
    std::string error;
    /**
     * Raw retail ENet disconnect data, including ERROR_MATCH_ENDED (18). A
     * connect that never completes within NetworkClient.timeout (5 s) reports
     * DISCONNECT.ERROR_TIMEOUT (11) here, as retail raises it locally.
     */
    std::optional<std::uint32_t> disconnect_reason;
    /** Retail string key for a local failure without a disconnect reason. */
    std::string failure_key;
    /** Handshake milestones driving the loader bar before the bootstrap. */
    Protocol168LoadingProgress loading;
    /** InitialInfo as soon as it decodes (loadingMenu CHECKING_MAP). */
    std::shared_ptr<const Protocol168InitialInfo> initial_info;
    /**
     * PasswordNeeded(112) state. It survives the disconnect, so a kick during
     * the exchange can be told apart from an ordinary one.
     */
    Protocol168PasswordPrompt password;
    GameProtocol protocol{GameProtocol::retail168};
    /** Measured ENet RTT for this client; Classic does not publish other players' pings. */
    std::optional<std::uint32_t> round_trip_time_ms;
    bool demo_playback{};
    bool demo_finished{};
    /** Recording failures do not interrupt a live game. */
    std::string demo_error;
};

/**
 * What a handshake that ended during the password exchange means. The server
 * has no "wrong password" reason: it kicks (2, ERROR_KICKED) after too many
 * wrong answers or while the address is locked out, and times out (11,
 * ERROR_TIMEOUT) a prompt left unanswered.
 */
enum class PasswordJoinFailure : std::uint8_t { none, wrong_password, timed_out };
[[nodiscard]] PasswordJoinFailure
protocol168_password_failure(const LiveProtocol168Status& status) noexcept;

/**
 * NetworkClient.send_packet(packet, unreliable): true only for ClockSync(0)
 * and ClientData(4), which retail sends ENet UNSEQUENCED; all else reliable.
 */
[[nodiscard]] bool protocol168_client_packet_unsequenced(std::uint8_t packet_id) noexcept;

/** Loading clients can receive ERROR_MATCH_ENDED without packet 52 first. */
[[nodiscard]] inline bool protocol168_should_reconnect_after_map_change(
    const LiveProtocol168Status& status, bool map_transition_armed) noexcept {
    if (status.phase == LiveProtocol168Phase::disconnected && status.disconnect_reason) {
        if (*status.disconnect_reason == 18U) return true;
        // An explicit rejection (ban, kick, incompatible data, etc.) remains final,
        // even if the previous scene had already announced a map change.
        if (*status.disconnect_reason != 0U) return false;
    }
    return map_transition_armed &&
           (status.phase == LiveProtocol168Phase::disconnected ||
            status.phase == LiveProtocol168Phase::failed);
}

/**
 * Bound the amount of ordered protocol work applied by one presentation tick.
 *
 * Retail NetworkClient.update (network.pyd) drains both event queues
 * completely on every update, so a playable world applies everything queued:
 * a partial drain delays that tick's WorldUpdate (and the terrain mutations
 * preceding it) by whole ticks after any ENet burst. While the world is still
 * being built retail blocks network reads (block_network_read); the native
 * loader keeps applying a bounded tranche instead.
 */
[[nodiscard]] constexpr std::size_t
protocol168_inbound_apply_budget(std::size_t queued_inbound,
                                 bool world_is_playable) noexcept {
    constexpr std::size_t minimum_budget{16U};
    constexpr std::size_t loading_budget{128U};

    if (!world_is_playable)
        return loading_budget;
    return std::max(queued_inbound, minimum_budget);
}

/** Immutable result transferred once per map from the ENet worker to gameplay. */
struct Protocol168WorldBootstrap final {
    /** Identifies the map transfer independently of a status snapshot. */
    std::uint64_t map_generation{};
    Protocol168InitialInfo initial_info;
    Protocol168StateInfo state_info;
    /** Packet 51 is independent of fog and selects the authored mesh layers. */
    Protocol168SkyboxInfo skybox_info{"User_Grassland.txt"};
    std::shared_ptr<world::VxlMap> map;
    Protocol168Roster roster;
    std::uint8_t local_player_id{};
    /** Zero before interactive selection; one after diagnostic auto-join. */
    std::uint32_t next_client_loop_count{};
    GameProtocol protocol{GameProtocol::retail168};
};

/**
 * Long-lived Protocol 168 transport used by an active match.
 *
 * ENet, compression and the raw peer stay confined to one worker thread.
 * The render/simulation thread exchanges complete plain protocol packets via
 * bounded queues (16 MiB / 16,384 live inbound packets). request_stop()
 * signals cancellation without waiting; stop()
 * and destruction join the worker, including any in-flight system DNS lookup,
 * so the UI should retire connections on its background cleanup executor.
 * Queue overflow fails the connection explicitly; gameplay packets are never
 * silently discarded because that would manufacture client/server desync.
 */
class LiveProtocol168Connection final {
public:
    LiveProtocol168Connection();
    ~LiveProtocol168Connection();

    LiveProtocol168Connection(const LiveProtocol168Connection&) = delete;
    LiveProtocol168Connection& operator=(const LiveProtocol168Connection&) = delete;
    LiveProtocol168Connection(LiveProtocol168Connection&&) = delete;
    LiveProtocol168Connection& operator=(LiveProtocol168Connection&&) = delete;

    [[nodiscard]] bool start(EnetProtocol168Config transport,
                             Protocol168SessionConfig session);
    /** Signal cancellation immediately; stop() later joins and releases ENet. */
    void request_stop() noexcept;
    void stop() noexcept;

    [[nodiscard]] LiveProtocol168Status status() const;
    [[nodiscard]] std::unique_ptr<Protocol168WorldBootstrap> take_bootstrap();
    /** Wait for bootstrap adoption; an older scene cannot consume a new map's packets. */
    [[nodiscard]] std::vector<std::vector<std::byte>>
    take_inbound(std::size_t limit = 64U,
                 std::optional<std::uint64_t> expected_generation = std::nullopt);
    [[nodiscard]] bool send(std::span<const std::byte> plain_packet);
    /** Classic packets only; never available on a retail connection. */
    [[nodiscard]] bool send_classic(std::span<const std::byte> packet);
    void update_classic_motion(const struct ClassicMotion& motion);
    /**
     * Answer the pending password prompt with PasswordProvided(113). False
     * when no prompt is pending or the text cannot be a password.
     */
    [[nodiscard]] bool provide_password(std::string password);
    /** Replay pauses at each new map until its viewer is ready. */
    void set_demo_paused(bool paused) noexcept;

private:
    void run_demo_playback(const std::filesystem::path& path);
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace battlespades::network
