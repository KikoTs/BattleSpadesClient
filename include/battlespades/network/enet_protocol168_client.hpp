#pragma once

#include "battlespades/network/protocol168_session.hpp"
#include "battlespades/network/game_protocol.hpp"

#include <cstdint>
#include <string>

namespace battlespades::network {

struct EnetProtocol168Config final {
    std::string host{"127.0.0.1"};
    /** `default_game_port`: the port BattleSpades servers listen on. */
    std::uint16_t port{27015U};
    /**
     * Live connections: LOADING_MENU_NO_PROGRESS_TIMEOUT, restarted by every
     * accepted handshake packet. The blocking probe keeps it as one deadline.
     */
    std::uint32_t timeout_ms{30'000U};
    /** Retail NetworkClient.timeout: ENet CONNECT must arrive within 5 s. */
    std::uint32_t connect_timeout_ms{5'000U};
    GameProtocol protocol{GameProtocol::retail168};
    /** Optional incoming-packet recording, created once for this connection. */
    std::filesystem::path record_demo_path;
    /** Local spectator replay; bypasses ENet, DNS and authentication entirely. */
    std::filesystem::path play_demo_path;
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
