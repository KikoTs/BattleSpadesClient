#pragma once

#include "battlespades/core/runtime_module.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace battlespades::network {

struct Endpoint final {
    std::string host;
    std::uint16_t port{};
};

struct ReceivedPacket final {
    std::uint8_t channel{};
    std::vector<std::byte> payload;
};

/**
 * Reliable-UDP transport boundary for the future ENet adapter.
 *
 * Protocol 168 encoding remains a separate layer so recorded packets can be
 * replayed without opening a socket.
 */
class TransportPort : public core::RuntimeModule {
public:
    [[nodiscard]] virtual bool connect(const Endpoint& endpoint) = 0;
    virtual void disconnect() noexcept = 0;
    [[nodiscard]] virtual bool
    send(std::uint8_t channel, std::span<const std::byte> payload, bool reliable) = 0;
    [[nodiscard]] virtual std::span<const ReceivedPacket> received_packets() const noexcept = 0;
};

} // namespace battlespades::network
