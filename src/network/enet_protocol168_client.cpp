#include "battlespades/network/enet_protocol168_client.hpp"

#include <chrono>
#include <span>

#include <enet/enet.h>

namespace battlespades::network {
namespace {

[[nodiscard]] bool send_datagram(ENetPeer* peer,
                                 std::span<const std::byte> bytes) {
    if (peer == nullptr || bytes.empty()) return false;
    auto* packet = enet_packet_create(bytes.data(), bytes.size(),
                                      ENET_PACKET_FLAG_RELIABLE);
    if (packet == nullptr) return false;
    if (enet_peer_send(peer, 0U, packet) != 0) {
        enet_packet_destroy(packet);
        return false;
    }
    return true;
}

} // namespace

EnetProtocol168Result run_enet_protocol168_session(
    const EnetProtocol168Config& config, Protocol168Session& session) {
    EnetProtocol168Result result;
    if (config.host.empty() || config.port == 0U || config.timeout_ms == 0U) {
        result.error = "invalid ENet Protocol 168 endpoint";
        return result;
    }
    if (enet_initialize() != 0) {
        result.error = "enet_initialize failed";
        return result;
    }
    struct EnetGuard final {
        ~EnetGuard() { enet_deinitialize(); }
    } enet_guard;
    auto* host = enet_host_create(nullptr, 1U, 1U, 0U, 0U);
    if (host == nullptr) {
        result.error = "enet_host_create failed";
        return result;
    }
    struct HostGuard final {
        ENetHost* value{};
        ~HostGuard() {
            if (value != nullptr) enet_host_destroy(value);
        }
    } host_guard{host};
    if (enet_host_compress_with_range_coder(host) != 0) {
        result.error = "ENet range-coder initialization failed";
        return result;
    }
    ENetAddress address{};
    address.port = config.port;
    if (enet_address_set_host(&address, config.host.c_str()) != 0) {
        result.error = "cannot resolve Protocol 168 host";
        return result;
    }
    ENetPeer* peer = enet_host_connect(host, &address, 1U, 168U);
    if (peer == nullptr) {
        result.error = "enet_host_connect failed";
        return result;
    }
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds{config.timeout_ms};
    while (std::chrono::steady_clock::now() < deadline && !session.ready() &&
           session.phase() != Protocol168SessionPhase::failed) {
        ENetEvent event{};
        const int serviced = enet_host_service(host, &event, 25U);
        if (serviced < 0) {
            result.error = "enet_host_service failed";
            break;
        }
        if (serviced == 0) continue;
        switch (event.type) {
        case ENET_EVENT_TYPE_CONNECT: {
            result.connected = true;
            const auto ticket = session.connected();
            if (!send_datagram(peer, ticket)) {
                result.error = "cannot send offline Steam ticket";
            } else {
                ++result.sent_datagrams;
                enet_host_flush(host);
            }
            break;
        }
        case ENET_EVENT_TYPE_RECEIVE: {
            ++result.received_datagrams;
            const auto bytes = std::span{
                reinterpret_cast<const std::byte*>(event.packet->data),
                event.packet->dataLength};
            const auto ingested = session.ingest(bytes);
            enet_packet_destroy(event.packet);
            for (const auto& outbound : ingested.outbound_datagrams) {
                if (!send_datagram(peer, outbound)) {
                    result.error = "cannot send Protocol 168 handshake packet";
                    break;
                }
                ++result.sent_datagrams;
            }
            enet_host_flush(host);
            break;
        }
        case ENET_EVENT_TYPE_DISCONNECT:
            session.disconnected();
            result.error = "server disconnected during Protocol 168 handshake";
            break;
        case ENET_EVENT_TYPE_NONE:
            break;
        }
        if (!result.error.empty()) break;
    }
    result.ready = session.ready();
    if (!result.ready && result.error.empty()) {
        result.error = session.phase() == Protocol168SessionPhase::failed
                           ? std::string{session.last_error()}
                           : "Protocol 168 handshake timed out";
    }
    enet_peer_disconnect(peer, 0U);
    ENetEvent event{};
    const auto disconnect_deadline = std::chrono::steady_clock::now() +
                                     std::chrono::milliseconds{250};
    while (std::chrono::steady_clock::now() < disconnect_deadline &&
           enet_host_service(host, &event, 10U) > 0) {
        if (event.type == ENET_EVENT_TYPE_RECEIVE) {
            enet_packet_destroy(event.packet);
        }
        if (event.type == ENET_EVENT_TYPE_DISCONNECT) break;
    }
    enet_peer_reset(peer);
    return result;
}

} // namespace battlespades::network
