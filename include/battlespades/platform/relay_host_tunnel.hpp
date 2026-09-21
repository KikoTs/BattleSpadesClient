#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

namespace battlespades::platform {

/** Credentials returned by AoSPlay's authenticated `/api/lobbies` endpoint. */
struct RelayHostTunnelConfig final {
    std::string allocation_id;
    std::string relay_host;
    std::uint16_t relay_port{};
    std::string host_key_base64url;
    std::uint16_t local_server_port{};
    std::uint16_t maximum_clients{24U};
    std::chrono::seconds keepalive{10};
    /** Retire disconnected relay IDs; live ENet peers exchange regular traffic. */
    std::chrono::seconds client_idle_timeout{120};
};

/**
 * Authenticated UDP bridge between one local BattleSpades process and the
 * public AoSPlay relay. One loopback socket is owned per remote client slot so
 * ENet still observes distinct peers without changing the dedicated server.
 */
class RelayHostTunnel final {
public:
    RelayHostTunnel();
    ~RelayHostTunnel();

    RelayHostTunnel(const RelayHostTunnel&) = delete;
    RelayHostTunnel& operator=(const RelayHostTunnel&) = delete;
    RelayHostTunnel(RelayHostTunnel&&) noexcept;
    RelayHostTunnel& operator=(RelayHostTunnel&&) noexcept;

    [[nodiscard]] bool start(RelayHostTunnelConfig config, std::string& error);
    void stop() noexcept;
    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] bool ready() const noexcept;
    [[nodiscard]] std::string last_error() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace battlespades::platform
