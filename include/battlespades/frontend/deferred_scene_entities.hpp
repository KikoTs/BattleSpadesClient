#pragma once

#include "battlespades/network/protocol168_packet_queue.hpp"
#include "battlespades/network/protocol168_runtime.hpp"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <utility>
#include <vector>

namespace battlespades::frontend {

/** Keep entity snapshots ordered while the asynchronous world is loading. */
class DeferredSceneEntities final {
public:
    using Packets = std::vector<std::vector<std::byte>>;

    [[nodiscard]] std::optional<Packets> apply(bool world_ready, Packets packets) {
        if (world_ready) {
            auto ready = pending_.take(pending_.size());
            if (ready.empty()) return packets;
            ready.insert(ready.end(), std::make_move_iterator(packets.begin()),
                         std::make_move_iterator(packets.end()));
            return ready;
        }
        Packets other;
        for (auto& packet : packets) {
            if (packet.empty()) continue;
            const auto id = std::to_integer<std::uint8_t>(packet.front());
            if (id == network::CreateEntityPacket::id ||
                id == network::ChangeEntityPacket::id ||
                id == network::DestroyEntityPacket::id ||
                id == network::HitEntityPacket::id ||
                id == network::PickPickupPacket::id ||
                id == network::ExplodeCorpsePacket::id) {
                if (!pending_.push(std::move(packet))) return std::nullopt;
            } else {
                other.push_back(std::move(packet));
            }
        }
        return other;
    }

    void clear() noexcept { pending_.clear(); }

private:
    network::detail::Protocol168PacketQueue pending_{
        network::detail::live_inbound_packet_limit,
        network::detail::live_inbound_byte_limit};
};

} // namespace battlespades::frontend
