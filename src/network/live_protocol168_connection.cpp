#include "battlespades/network/live_protocol168_connection.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

#include <enet/enet.h>

namespace battlespades::network {
namespace {

// A reconnect can legitimately deliver the server's full 8,192-mutation
// catch-up immediately after CreatePlayer. Keep one additional burst of room
// for WorldUpdates/objectives while the renderer meshes the received VXL.
constexpr std::size_t inbound_capacity{16'384U};
constexpr std::size_t outbound_capacity{256U};
constexpr std::size_t outbound_batch{32U};

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

struct LiveProtocol168Connection::State final {
    mutable std::mutex mutex;
    std::thread worker;
    std::atomic<bool> stop_requested{};
    LiveProtocol168Status status;
    std::unique_ptr<Protocol168WorldBootstrap> bootstrap;
    std::deque<std::vector<std::byte>> inbound;
    std::deque<std::vector<std::byte>> outbound;
};

LiveProtocol168Connection::LiveProtocol168Connection()
    : state_{std::make_unique<State>()} {}

LiveProtocol168Connection::~LiveProtocol168Connection() {
    stop();
}

bool LiveProtocol168Connection::start(EnetProtocol168Config transport,
                                      Protocol168SessionConfig session_config) {
    stop();
    if (transport.host.empty() || transport.port == 0U || transport.timeout_ms == 0U) {
        const std::lock_guard lock{state_->mutex};
        state_->status.phase = LiveProtocol168Phase::failed;
        state_->status.error = "invalid ENet Protocol 168 endpoint";
        return false;
    }
    {
        const std::lock_guard lock{state_->mutex};
        state_->status = {};
        state_->status.phase = LiveProtocol168Phase::connecting;
        state_->bootstrap.reset();
        state_->inbound.clear();
        state_->outbound.clear();
    }
    state_->stop_requested.store(false);
    auto* state = state_.get();
    state_->worker = std::thread{
        [state, transport = std::move(transport),
         session_config = std::move(session_config)]() mutable {
            const auto fail = [state](std::string message) {
                const std::lock_guard lock{state->mutex};
                state->status.phase = LiveProtocol168Phase::failed;
                state->status.error = std::move(message);
                state->stop_requested.store(true);
            };
            if (enet_initialize() != 0) {
                fail("enet_initialize failed");
                return;
            }
            struct EnetGuard final {
                ~EnetGuard() { enet_deinitialize(); }
            } enet_guard;
            auto* host = enet_host_create(nullptr, 1U, 1U, 0U, 0U);
            if (host == nullptr) {
                fail("enet_host_create failed");
                return;
            }
            struct HostGuard final {
                ENetHost* host{};
                ~HostGuard() { if (host != nullptr) enet_host_destroy(host); }
            } host_guard{host};
            if (enet_host_compress_with_range_coder(host) != 0) {
                fail("ENet range-coder initialization failed");
                return;
            }
            ENetAddress address{};
            address.port = transport.port;
            if (enet_address_set_host(&address, transport.host.c_str()) != 0) {
                fail("cannot resolve Protocol 168 host");
                return;
            }
            auto* peer = enet_host_connect(host, &address, 1U, 168U);
            if (peer == nullptr) {
                fail("enet_host_connect failed");
                return;
            }

            Protocol168Session session{std::move(session_config)};
            const auto deadline = std::chrono::steady_clock::now() +
                                  std::chrono::milliseconds{transport.timeout_ms};
            bool joined{};
            while (!state->stop_requested.load()) {
                if (!joined && std::chrono::steady_clock::now() >= deadline) {
                    fail("Protocol 168 handshake timed out");
                    break;
                }

                ENetEvent event{};
                const int serviced = enet_host_service(host, &event, 10U);
                if (serviced < 0) {
                    fail("enet_host_service failed");
                    break;
                }
                if (serviced > 0) {
                    switch (event.type) {
                    case ENET_EVENT_TYPE_CONNECT: {
                        {
                            const std::lock_guard lock{state->mutex};
                            state->status.phase = LiveProtocol168Phase::handshaking;
                        }
                        const auto ticket = session.connected();
                        if (!send_datagram(peer, ticket)) {
                            fail("cannot send offline Steam ticket");
                        } else {
                            const std::lock_guard lock{state->mutex};
                            ++state->status.sent_datagrams;
                        }
                        break;
                    }
                    case ENET_EVENT_TYPE_RECEIVE: {
                        const auto bytes = std::span{
                            reinterpret_cast<const std::byte*>(event.packet->data),
                            event.packet->dataLength};
                        {
                            const std::lock_guard lock{state->mutex};
                            ++state->status.received_datagrams;
                        }
                        if (!joined) {
                            auto ingested = session.ingest(bytes);
                            enet_packet_destroy(event.packet);
                            for (const auto& outgoing : ingested.outbound_datagrams) {
                                if (!send_datagram(peer, outgoing)) {
                                    fail("cannot send Protocol 168 handshake packet");
                                    break;
                                }
                                const std::lock_guard lock{state->mutex};
                                ++state->status.sent_datagrams;
                            }
                            if (session.phase() == Protocol168SessionPhase::failed) {
                                fail(std::string{session.last_error()});
                            } else if (session.bootstrap_ready()) {
                                auto map = session.take_map();
                                const auto local_id = session.local_player_id();
                                if (!map.has_value() || !local_id.has_value() ||
                                    session.initial_info() == nullptr) {
                                    fail("Protocol 168 session has incomplete chooser bootstrap");
                                    break;
                                }
                                auto bootstrap =
                                    std::make_unique<Protocol168WorldBootstrap>();
                                bootstrap->initial_info = *session.initial_info();
                                if (session.state_info() != nullptr) {
                                    bootstrap->state_info = *session.state_info();
                                }
                                if (session.skybox_info() != nullptr) {
                                    bootstrap->skybox_info = *session.skybox_info();
                                }
                                bootstrap->map = std::make_shared<world::VxlMap>(
                                    std::move(*map));
                                bootstrap->roster = session.roster();
                                bootstrap->local_player_id = *local_id;
                                bootstrap->next_client_loop_count =
                                    session.next_client_loop_count();
                                auto deferred_runtime =
                                    session.take_deferred_runtime_packets();
                                {
                                    const std::lock_guard lock{state->mutex};
                                    state->bootstrap = std::move(bootstrap);
                                    for (auto& packet : deferred_runtime) {
                                        state->inbound.push_back(
                                            std::move(packet));
                                    }
                                    state->status.queued_inbound =
                                        state->inbound.size();
                                    state->status.phase = LiveProtocol168Phase::ready;
                                }
                                joined = true;
                            }
                        } else {
                            std::string error;
                            auto plain = decode_protocol168_server_datagram(bytes, error);
                            enet_packet_destroy(event.packet);
                            if (!plain.has_value()) {
                                fail("malformed live Protocol 168 datagram: " + error);
                                break;
                            }
                            const std::lock_guard lock{state->mutex};
                            if (state->inbound.size() >= inbound_capacity) {
                                state->status.phase = LiveProtocol168Phase::failed;
                                state->status.error =
                                    "live Protocol 168 inbound queue overflow";
                                state->stop_requested.store(true);
                            } else {
                                state->inbound.push_back(std::move(*plain));
                                state->status.queued_inbound = state->inbound.size();
                            }
                        }
                        break;
                    }
                    case ENET_EVENT_TYPE_DISCONNECT: {
                        const std::lock_guard lock{state->mutex};
                        state->status.phase = LiveProtocol168Phase::disconnected;
                        state->status.error = joined ? "server closed the match connection"
                                                     : "server disconnected during handshake";
                        state->stop_requested.store(true);
                        break;
                    }
                    case ENET_EVENT_TYPE_NONE:
                        break;
                    }
                }

                std::deque<std::vector<std::byte>> outgoing;
                {
                    const std::lock_guard lock{state->mutex};
                    const auto count = outbound_batch < state->outbound.size()
                                           ? outbound_batch
                                           : state->outbound.size();
                    for (std::size_t index{}; index < count; ++index) {
                        outgoing.push_back(std::move(state->outbound.front()));
                        state->outbound.pop_front();
                    }
                    state->status.queued_outbound = state->outbound.size();
                }
                for (const auto& packet : outgoing) {
                    const auto datagram =
                        encode_protocol168_client_datagram(packet);
                    if (!send_datagram(peer, datagram)) {
                        fail("cannot send live Protocol 168 packet");
                        break;
                    }
                    const std::lock_guard lock{state->mutex};
                    ++state->status.sent_datagrams;
                }
                if (!outgoing.empty()) enet_host_flush(host);
            }

            if (peer->state == ENET_PEER_STATE_CONNECTED ||
                peer->state == ENET_PEER_STATE_CONNECTION_SUCCEEDED) {
                enet_peer_disconnect(peer, 0U);
                enet_host_flush(host);
            }
            enet_peer_reset(peer);
            const std::lock_guard lock{state->mutex};
            if (state->status.phase != LiveProtocol168Phase::failed &&
                state->status.phase != LiveProtocol168Phase::disconnected) {
                state->status.phase = LiveProtocol168Phase::stopped;
            }
        }};
    return true;
}

void LiveProtocol168Connection::stop() noexcept {
    state_->stop_requested.store(true);
    if (state_->worker.joinable()) state_->worker.join();
}

LiveProtocol168Status LiveProtocol168Connection::status() const {
    const std::lock_guard lock{state_->mutex};
    auto result = state_->status;
    result.queued_inbound = state_->inbound.size();
    result.queued_outbound = state_->outbound.size();
    return result;
}

std::unique_ptr<Protocol168WorldBootstrap>
LiveProtocol168Connection::take_bootstrap() {
    const std::lock_guard lock{state_->mutex};
    return std::move(state_->bootstrap);
}

std::vector<std::vector<std::byte>>
LiveProtocol168Connection::take_inbound(std::size_t limit) {
    std::vector<std::vector<std::byte>> result;
    const std::lock_guard lock{state_->mutex};
    const auto count = limit < state_->inbound.size() ? limit : state_->inbound.size();
    result.reserve(count);
    for (std::size_t index{}; index < count; ++index) {
        result.push_back(std::move(state_->inbound.front()));
        state_->inbound.pop_front();
    }
    state_->status.queued_inbound = state_->inbound.size();
    return result;
}

bool LiveProtocol168Connection::send(std::span<const std::byte> plain_packet) {
    if (plain_packet.empty()) return false;
    const std::lock_guard lock{state_->mutex};
    if (state_->status.phase != LiveProtocol168Phase::ready ||
        state_->outbound.size() >= outbound_capacity) {
        if (state_->outbound.size() >= outbound_capacity) {
            state_->status.phase = LiveProtocol168Phase::failed;
            state_->status.error = "live Protocol 168 outbound queue overflow";
            state_->stop_requested.store(true);
        }
        return false;
    }
    // IDA: GameScene.send_client_data passes True to send_packet, selecting
    // PACKET_FLAG_RELIABLE. The server consumes one observed packet per
    // physics frame, so dropping a ClientData frame changes simulation time.
    state_->outbound.emplace_back(plain_packet.begin(), plain_packet.end());
    state_->status.queued_outbound = state_->outbound.size();
    return true;
}

} // namespace battlespades::network
