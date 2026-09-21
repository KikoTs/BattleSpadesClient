#pragma once

#include <algorithm>
#include <cstddef>
#include <deque>
#include <span>
#include <utility>
#include <vector>

namespace battlespades::network::detail {

inline constexpr std::size_t live_inbound_packet_limit{16'384U};
inline constexpr std::size_t live_inbound_byte_limit{16U << 20U};
inline constexpr std::size_t deferred_runtime_packet_limit{64U};
inline constexpr std::size_t deferred_runtime_byte_limit{1U << 20U};

/** Ordered runtime packets, bounded independently by count and retained memory.
 * MapSync and UGC source bytes belong to their separate map stream limits.
 * A rejected push leaves existing packets intact; the caller fails the session
 * rather than silently losing authoritative gameplay.
 */
class Protocol168PacketQueue final {
public:
    Protocol168PacketQueue(std::size_t packet_limit, std::size_t byte_limit)
        : packet_limit_{packet_limit}, byte_limit_{byte_limit} {}

    Protocol168PacketQueue(const Protocol168PacketQueue&) = delete;
    Protocol168PacketQueue& operator=(const Protocol168PacketQueue&) = delete;
    Protocol168PacketQueue(Protocol168PacketQueue&& other)
        : Protocol168PacketQueue{other.packet_limit_, other.byte_limit_} {
        packets_.swap(other.packets_);
        retained_bytes_ = std::exchange(other.retained_bytes_, 0U);
    }
    Protocol168PacketQueue& operator=(Protocol168PacketQueue&& other) noexcept {
        if (this != &other) {
            clear();
            packet_limit_ = other.packet_limit_;
            byte_limit_ = other.byte_limit_;
            packets_.swap(other.packets_);
            retained_bytes_ = std::exchange(other.retained_bytes_, 0U);
        }
        return *this;
    }

    [[nodiscard]] bool push(std::vector<std::byte>&& packet) {
        // Retain capacity, not only size: a decoded vector can own unused
        // allocation beyond its payload after LZF reserve/growth.
        if (!accepts(packet.capacity())) return false;
        const auto retained = packet.capacity();
        packets_.push_back(std::move(packet));
        retained_bytes_ += retained;
        return true;
    }

    [[nodiscard]] bool push(std::span<const std::byte> packet) {
        if (!accepts(packet.size())) return false;
        return push(std::vector<std::byte>{packet.begin(), packet.end()});
    }

    [[nodiscard]] std::vector<std::vector<std::byte>> take(std::size_t limit) {
        std::vector<std::vector<std::byte>> result;
        const auto count = std::min(limit, packets_.size());
        result.reserve(count);
        for (std::size_t index{}; index < count; ++index) {
            const auto retained = packets_.front().capacity();
            result.push_back(std::move(packets_.front()));
            packets_.pop_front();
            retained_bytes_ -= retained;
        }
        return result;
    }

    void clear() noexcept {
        packets_.clear();
        retained_bytes_ = 0U;
    }

    [[nodiscard]] std::size_t size() const noexcept { return packets_.size(); }
    [[nodiscard]] std::size_t retained_bytes() const noexcept { return retained_bytes_; }

private:
    [[nodiscard]] bool accepts(std::size_t bytes) const noexcept {
        return packets_.size() < packet_limit_ && bytes <= byte_limit_ - retained_bytes_;
    }

    std::size_t packet_limit_{};
    std::size_t byte_limit_{};
    std::size_t retained_bytes_{};
    std::deque<std::vector<std::byte>> packets_;
};

} // namespace battlespades::network::detail
