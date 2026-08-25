#pragma once

#include "battlespades/network/enet_protocol168_client.hpp"
#include "battlespades/network/protocol168_players.hpp"
#include "battlespades/network/protocol168_session.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
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
    std::string error;
};

/**
 * Bound the amount of ordered protocol work applied by one presentation tick.
 *
 * WorldUpdate authority and the terrain mutations preceding it share one FIFO.
 * A fixed 16-packet drain lets a reliable terrain/entity burst hide fresh owner
 * acknowledgements for several frames, which turns an otherwise small replay
 * correction into a visible rollback.  Keep the ordinary cost at retail's
 * small tranche, but catch up a real backlog without reordering packets.
 */
[[nodiscard]] constexpr std::size_t
protocol168_inbound_apply_budget(std::size_t queued_inbound,
                                 bool world_is_playable) noexcept {
    constexpr std::size_t ordinary_budget{16U};
    constexpr std::size_t catchup_budget{64U};
    constexpr std::size_t severe_backlog_budget{128U};
    constexpr std::size_t severe_backlog_threshold{512U};

    if (!world_is_playable)
        return severe_backlog_budget;
    if (queued_inbound <= ordinary_budget)
        return ordinary_budget;
    if (queued_inbound > severe_backlog_threshold)
        return severe_backlog_budget;
    return std::min(queued_inbound, catchup_budget);
}

/** Immutable join result transferred once from the ENet worker to gameplay. */
struct Protocol168WorldBootstrap final {
    Protocol168InitialInfo initial_info;
    Protocol168StateInfo state_info;
    /** Packet 51 is independent of fog and selects the authored mesh layers. */
    Protocol168SkyboxInfo skybox_info{"User_Grassland.txt"};
    std::shared_ptr<world::VxlMap> map;
    Protocol168Roster roster;
    std::uint8_t local_player_id{};
    /** Zero before interactive selection; one after diagnostic auto-join. */
    std::uint32_t next_client_loop_count{};
};

/**
 * Long-lived Protocol 168 transport used by an active match.
 *
 * ENet, compression and the raw peer stay confined to one worker thread.
 * The render/simulation thread exchanges complete plain protocol packets via
 * bounded queues and never blocks on DNS, sockets, compression or shutdown.
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
    void stop() noexcept;

    [[nodiscard]] LiveProtocol168Status status() const;
    [[nodiscard]] std::unique_ptr<Protocol168WorldBootstrap> take_bootstrap();
    [[nodiscard]] std::vector<std::vector<std::byte>>
    take_inbound(std::size_t limit = 64U);
    [[nodiscard]] bool send(std::span<const std::byte> plain_packet);

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace battlespades::network
