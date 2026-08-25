#pragma once

#include "battlespades/network/protocol168_session.hpp"

#include <cstdint>
#include <string>

namespace battlespades::network {

struct EnetProtocol168Config final {
    std::string host{"127.0.0.1"};
    std::uint16_t port{32887U};
    std::uint32_t timeout_ms{30'000U};
};

struct EnetProtocol168Result final {
    bool connected{};
    bool ready{};
    std::size_t received_datagrams{};
    std::size_t sent_datagrams{};
    std::string error;
};

/**
 * Blocking integration adapter used by loading workers and the live smoke.
 * It owns ENet globally for the duration of the call, enables the same range
 * coder as the retail/server endpoints, and never exposes raw ENet pointers.
 */
[[nodiscard]] EnetProtocol168Result
run_enet_protocol168_session(const EnetProtocol168Config& config,
                             Protocol168Session& session);

} // namespace battlespades::network
