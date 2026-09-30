#include "battlespades/network/live_protocol168_connection.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <chrono>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

#include <enet/enet.h>

namespace battlespades::network {
namespace {

constexpr std::size_t outbound_capacity{256U};
constexpr std::size_t outbound_batch{32U};

[[nodiscard]] bool send_datagram(ENetPeer* peer,
                                 std::span<const std::byte> bytes,
                                 bool unsequenced = false) {
    if (peer == nullptr || bytes.empty()) return false;
    // NetworkClient.send_packet(packet, unreliable) (network.pyd 0x10008d50):
    // true selects PACKET_FLAG_UNSEQUENCED, false PACKET_FLAG_RELIABLE; both
    // go out on channel 0.
    auto* packet = enet_packet_create(
        bytes.data(), bytes.size(),
        unsequenced ? ENET_PACKET_FLAG_UNSEQUENCED : ENET_PACKET_FLAG_RELIABLE);
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
    // Allow the server's full 8,192-mutation reconnect catch-up plus one
    // extra burst while the renderer meshes the VXL, within a byte budget.
    detail::Protocol168PacketQueue inbound{
        detail::live_inbound_packet_limit, detail::live_inbound_byte_limit};
    std::deque<std::vector<std::byte>> outbound;
    /** The typed answer to PasswordNeeded, taken by the worker. */
    std::optional<std::string> password_answer;
};

PasswordJoinFailure
protocol168_password_failure(const LiveProtocol168Status& status) noexcept {
    if (status.password.requests == 0U || status.initial_info != nullptr ||
        status.phase != LiveProtocol168Phase::disconnected ||
        !status.disconnect_reason.has_value()) {
        return PasswordJoinFailure::none;
    }
    if (*status.disconnect_reason == 2U) return PasswordJoinFailure::wrong_password;
    if (*status.disconnect_reason == 11U) return PasswordJoinFailure::timed_out;
    return PasswordJoinFailure::none;
}

bool protocol168_client_packet_unsequenced(std::uint8_t packet_id) noexcept {
    // Of the 44 send_packet call sites in stock gameScene.pyd only
    // send_client_data (0x1016d248) and send_clock_sync (0x10181db5) pass
    // Py_True (unreliable). Every other client packet is reliable.
    return packet_id == 0U || packet_id == 4U;
}

LiveProtocol168Connection::LiveProtocol168Connection()
    : state_{std::make_unique<State>()} {}

LiveProtocol168Connection::~LiveProtocol168Connection() {
    stop();
}

bool LiveProtocol168Connection::start(EnetProtocol168Config transport,
                                      Protocol168SessionConfig session_config) {
    stop();
    {
        const std::lock_guard lock{state_->mutex};
        state_->status = {};
        state_->status.phase = LiveProtocol168Phase::connecting;
        state_->bootstrap.reset();
        state_->inbound.clear();
        state_->outbound.clear();
        state_->password_answer.reset();
        if (transport.host.empty() || transport.port == 0U || transport.timeout_ms == 0U) {
            state_->status.phase = LiveProtocol168Phase::failed;
            state_->status.error = "invalid ENet Protocol 168 endpoint";
            return false;
        }
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
            const auto canceled = [state] {
                if (!state->stop_requested.load()) return false;
                const std::lock_guard lock{state->mutex};
                state->status.phase = LiveProtocol168Phase::stopped;
                return true;
            };
            if (canceled()) return;
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
            if (canceled()) return;
            const auto resolved = enet_address_set_host(&address, transport.host.c_str());
            // System DNS is not cancellable through ENet. A retired lookup
            // must not create a peer or report a new failure when it returns.
            if (canceled()) return;
            if (resolved != 0) {
                fail("cannot resolve Protocol 168 host");
                return;
            }
            auto* peer = enet_host_connect(host, &address, 1U, 168U);
            if (peer == nullptr) {
                fail("enet_host_connect failed");
                return;
            }

            // Exact packet-105 payload, immutable for this connection. Retain
            // one worker-local copy rather than allocating/copying it on every
            // idle service poll merely to send no packets.
            const auto ticket_key = session_config.steam_ticket;
            session_config.negotiate_flight_profile = true;
            Protocol168Session session{std::move(session_config)};
            // Retail join timing: NetworkClient.timeout = 5 counts down until
            // EVENT_TYPE_CONNECT (network.pyd 0x10006010, disconnect with
            // ERROR_TIMEOUT); after that only loadingMenu's 30 s NO-PROGRESS
            // timer applies, restarted by every step of the handshake.
            const auto connect_ms = transport.connect_timeout_ms == 0U
                                        ? transport.timeout_ms
                                        : (std::min)(transport.connect_timeout_ms,
                                                     transport.timeout_ms);
            auto deadline = std::chrono::steady_clock::now() +
                            std::chrono::milliseconds{connect_ms};
            bool connected{};
            bool joined{};
            std::vector<std::vector<std::byte>> outgoing;
            outgoing.reserve(outbound_batch);
            while (!state->stop_requested.load()) {
                if (!joined && session.password_prompt().pending) {
                    // The server runs its own clock on the prompt
                    // (password_timeout_seconds); the no-progress timer must
                    // not cut the player off while they type.
                    deadline = std::chrono::steady_clock::now() +
                               std::chrono::milliseconds{transport.timeout_ms};
                    std::optional<std::string> answer;
                    {
                        const std::lock_guard lock{state->mutex};
                        answer = std::exchange(state->password_answer, std::nullopt);
                    }
                    if (answer.has_value()) {
                        const auto datagram = session.provide_password(*answer);
                        std::fill(answer->begin(), answer->end(), '\0');
                        if (!datagram.empty()) {
                            if (!send_datagram(peer, datagram)) {
                                fail("cannot send the server password");
                                break;
                            }
                            enet_host_flush(host);
                            const std::lock_guard lock{state->mutex};
                            ++state->status.sent_datagrams;
                            state->status.password = session.password_prompt();
                        }
                    }
                }
                if (!joined && std::chrono::steady_clock::now() >= deadline) {
                    const std::lock_guard lock{state->mutex};
                    state->status.phase = LiveProtocol168Phase::failed;
                    if (connected) {
                        state->status.error = "Protocol 168 handshake made no progress";
                        state->status.failure_key = "ERROR_TIMEOUT";
                    } else {
                        // DISCONNECT.ERROR_TIMEOUT (11) is raised locally.
                        state->status.error = "Protocol 168 connect timed out";
                        state->status.disconnect_reason = 11U;
                    }
                    state->stop_requested.store(true);
                    break;
                }

                ENetEvent event{};
                // A 10ms receive wait before examining outbound work added up
                // to most of a 60Hz input period to locally hosted play. An
                // active match limits that wait to 2ms; work already queued
                // never waits. ENet still owns the socket on this thread, and
                // receives retain priority so a disconnect cannot be replaced
                // with a subsequent send failure.
                enet_uint32 service_wait_ms = joined ? 2U : 10U;
                if (joined) {
                    const std::lock_guard lock{state->mutex};
                    if (!state->outbound.empty()) service_wait_ms = 0U;
                }
                const int serviced = enet_host_service(host, &event, service_wait_ms);
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
                        connected = true;
                        deadline = std::chrono::steady_clock::now() +
                                   std::chrono::milliseconds{transport.timeout_ms};
                        const auto ticket = session.connected();
                        if (!send_datagram(peer, ticket)) {
                            fail("cannot send Steam session ticket");
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
                        std::optional<std::vector<std::byte>> plain;
                        if (joined) {
                            std::string error;
                            plain = decode_protocol168_server_datagram(bytes, error);
                            if (!plain.has_value()) {
                                enet_packet_destroy(event.packet);
                                fail("malformed live Protocol 168 datagram: " + error);
                                break;
                            }
                            if (!plain->empty() && plain->front() == std::byte{114U})
                                joined = false;
                        }
                        if (!joined) {
                            auto ingested = session.ingest(bytes);
                            enet_packet_destroy(event.packet);
                            if (ingested.accepted) {
                                const std::lock_guard lock{state->mutex};
                                if (state->status.map_generation != session.map_generation()) {
                                    // Nothing queued for the previous map may
                                    // mutate or send input into its replacement.
                                    state->inbound.clear();
                                    state->outbound.clear();
                                    state->bootstrap.reset();
                                    state->status.initial_info.reset();
                                    state->status.map_generation = session.map_generation();
                                    state->status.phase = LiveProtocol168Phase::handshaking;
                                    state->status.queued_inbound = 0U;
                                    state->status.queued_outbound = 0U;
                                }
                            }
                            for (const auto& handshake_packet : ingested.outbound_datagrams) {
                                if (!send_datagram(peer, handshake_packet)) {
                                    fail("cannot send Protocol 168 handshake packet");
                                    break;
                                }
                                const std::lock_guard lock{state->mutex};
                                ++state->status.sent_datagrams;
                            }
                            if (state->stop_requested.load()) break;
                            if (ingested.accepted) {
                                deadline = std::chrono::steady_clock::now() +
                                           std::chrono::milliseconds{transport.timeout_ms};
                            }
                            {
                                // Loader milestones (loadingMenu.on_packet /
                                // client.map_percentage) while the join runs.
                                const std::lock_guard lock{state->mutex};
                                state->status.loading = session.loading_progress();
                                state->status.password = session.password_prompt();
                                if (state->status.initial_info == nullptr &&
                                    session.initial_info() != nullptr) {
                                    state->status.initial_info =
                                        std::make_shared<const Protocol168InitialInfo>(
                                            *session.initial_info());
                                }
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
                                bootstrap->map_generation = session.map_generation();
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
                                        if (!state->inbound.push(std::move(packet))) {
                                            state->status.phase = LiveProtocol168Phase::failed;
                                            state->status.error =
                                                "live Protocol 168 inbound queue overflow";
                                            state->stop_requested.store(true);
                                            state->bootstrap.reset();
                                            break;
                                        }
                                    }
                                    state->status.queued_inbound =
                                        state->inbound.size();
                                    if (!state->stop_requested.load())
                                        state->status.phase = LiveProtocol168Phase::ready;
                                }
                                joined = true;
                            }
                        } else {
                            enet_packet_destroy(event.packet);
                            const std::lock_guard lock{state->mutex};
                            if (!state->inbound.push(std::move(*plain))) {
                                state->status.phase = LiveProtocol168Phase::failed;
                                state->status.error =
                                    "live Protocol 168 inbound queue overflow";
                                state->stop_requested.store(true);
                            } else {
                                state->status.queued_inbound = state->inbound.size();
                            }
                        }
                        break;
                    }
                    case ENET_EVENT_TYPE_DISCONNECT: {
                        const std::lock_guard lock{state->mutex};
                        state->status.phase = LiveProtocol168Phase::disconnected;
                        state->status.disconnect_reason = event.data;
                        state->status.error = joined ? "server closed the match connection"
                                                     : "server disconnected during handshake";
                        state->status.error += " (reason " + std::to_string(event.data) + ")";
                        state->stop_requested.store(true);
                        break;
                    }
                    case ENET_EVENT_TYPE_NONE:
                        break;
                    }
                }

                // A disconnect (including map rotation reason 18) is final for
                // this peer. Sending its queued gameplay now would fail and
                // overwrite the authoritative disconnect with a send error.
                if (state->stop_requested.load()) break;
                outgoing.clear();
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
                        encode_protocol168_client_datagram(packet, ticket_key);
                    const bool unsequenced =
                        protocol168_client_packet_unsequenced(
                            std::to_integer<std::uint8_t>(packet.front()));
                    if (!send_datagram(peer, datagram, unsequenced)) {
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

void LiveProtocol168Connection::request_stop() noexcept {
    state_->stop_requested.store(true);
}

void LiveProtocol168Connection::stop() noexcept {
    request_stop();
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
LiveProtocol168Connection::take_inbound(
    std::size_t limit, std::optional<std::uint64_t> expected_generation) {
    const std::lock_guard lock{state_->mutex};
    // Status and the render tick are independent of the worker. Keep a new
    // map's packets queued until its bootstrap is adopted by the right scene.
    if (state_->bootstrap != nullptr ||
        (expected_generation.has_value() &&
         *expected_generation != state_->status.map_generation)) {
        return {};
    }
    auto result = state_->inbound.take(limit);
    state_->status.queued_inbound = state_->inbound.size();
    return result;
}

bool LiveProtocol168Connection::provide_password(std::string password) {
    if (encode_password_provided_packet(password).empty()) return false;
    const std::lock_guard lock{state_->mutex};
    if (state_->status.phase != LiveProtocol168Phase::handshaking ||
        !state_->status.password.pending) {
        return false;
    }
    state_->password_answer = std::move(password);
    // The prompt closes at once; a wrong answer reopens it with the next 112.
    state_->status.password.pending = false;
    return true;
}

bool LiveProtocol168Connection::send(std::span<const std::byte> plain_packet) {
    if (plain_packet.empty()) return false;
    const std::lock_guard lock{state_->mutex};
    if (state_->status.phase != LiveProtocol168Phase::ready || state_->bootstrap != nullptr ||
        state_->outbound.size() >= outbound_capacity) {
        if (state_->outbound.size() >= outbound_capacity) {
            state_->status.phase = LiveProtocol168Phase::failed;
            state_->status.error = "live Protocol 168 outbound queue overflow";
            state_->stop_requested.store(true);
        }
        return false;
    }
    // IDA: GameScene.send_client_data and send_clock_sync pass True as
    // send_packet's `unreliable` argument, selecting PACKET_FLAG_UNSEQUENCED
    // (see protocol168_client_packet_unsequenced). A lost ClientData is never
    // retransmitted: the server refills the missing loop label with one held
    // frame (BattleSpades input_gap_fill_limit), instead of a reliable
    // retransmit stalling channel 0 and ratcheting the input delay up.
    state_->outbound.emplace_back(plain_packet.begin(), plain_packet.end());
    state_->status.queued_outbound = state_->outbound.size();
    return true;
}

} // namespace battlespades::network
