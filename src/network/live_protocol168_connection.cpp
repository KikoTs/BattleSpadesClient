#include "battlespades/network/live_protocol168_connection.hpp"
#include "battlespades/network/classic_protocol.hpp"
#include "battlespades/network/demo_playback.hpp"
#include "battlespades/network/demo_stream.hpp"

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
    std::atomic<bool> demo_paused{};
    LiveProtocol168Status status;
    std::unique_ptr<Protocol168WorldBootstrap> bootstrap;
    // Allow the server's full 8,192-mutation reconnect catch-up plus one
    // extra burst while the renderer meshes the VXL, within a byte budget.
    detail::Protocol168PacketQueue inbound{
        detail::live_inbound_packet_limit, detail::live_inbound_byte_limit};
    std::deque<std::vector<std::byte>> outbound;
    std::optional<ClassicMotion> classic_motion;
    bool classic_jump_pending{};
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
        state_->status.protocol = transport.protocol == GameProtocol::automatic
                                      ? GameProtocol::retail168 : transport.protocol;
        state_->bootstrap.reset();
        state_->inbound.clear();
        state_->outbound.clear();
        state_->password_answer.reset();
        state_->classic_motion.reset();
        state_->classic_jump_pending = false;
        state_->status.demo_playback = !transport.play_demo_path.empty();
        if (!transport.play_demo_path.empty() && !transport.record_demo_path.empty()) {
            state_->status.phase = LiveProtocol168Phase::failed;
            state_->status.error = "cannot record and play a demo simultaneously";
            return false;
        }
        if (transport.play_demo_path.empty() &&
            (transport.host.empty() || transport.port == 0U || transport.timeout_ms == 0U)) {
            state_->status.phase = LiveProtocol168Phase::failed;
            state_->status.error = "invalid ENet Protocol 168 endpoint";
            return false;
        }
    }
    state_->stop_requested.store(false);
    state_->demo_paused.store(false);
    if (!transport.play_demo_path.empty()) {
        state_->worker = std::thread{[this, path = std::move(transport.play_demo_path)] {
            run_demo_playback(path);
        }};
        return true;
    }
    if (!transport.record_demo_path.empty()) {
        // A replay must reconstruct the entire map without another player's
        // cache or a master server. Ask for a full snapshot when recording.
        session_config.local_map_crc = 0U;
        session_config.local_map_directory.clear();
    }
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
            auto protocol = transport.protocol == GameProtocol::automatic ? GameProtocol::retail168 : transport.protocol;
            auto* peer = enet_host_connect(host, &address, 1U, static_cast<enet_uint32>(protocol));
            if (peer == nullptr) {
                fail("enet_host_connect failed");
                return;
            }

            // Exact packet-105 payload, immutable for this connection. Retain
            // one worker-local copy rather than allocating/copying it on every
            // idle service poll merely to send no packets.
            const auto ticket_key = session_config.steam_ticket;
            // The caller enables extensions only for known BattleSpades
            // peers. Stock retail servers must receive the exact ticket.
            Protocol168Session session{std::move(session_config)};
            std::unique_ptr<ClassicProtocolSession> classic;
            if (is_classic_protocol(protocol)) classic = std::make_unique<ClassicProtocolSession>(protocol);
            bool received_application_data{};
            DemoWriter demo;
            bool demo_attempted{};
            const auto demo_started = std::chrono::steady_clock::now();
            const auto record_demo = [&](std::span<const std::byte> bytes) {
                if (transport.record_demo_path.empty()) return;
                if (!demo_attempted) {
                    demo_attempted = true;
                    static_cast<void>(demo.open(transport.record_demo_path, static_cast<std::uint16_t>(protocol)));
                }
                if (demo.active()) {
                    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - demo_started).count();
                    static_cast<void>(demo.append(static_cast<std::uint64_t>(elapsed), bytes));
                }
                if (!demo.error().empty()) {
                    const std::lock_guard lock{state->mutex};
                    state->status.demo_error = demo.error();
                }
            };
            { const std::lock_guard lock{state->mutex}; state->status.protocol=protocol; }
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
            std::optional<std::chrono::steady_clock::time_point> retail_ticket_at;
            const auto send_retail_ticket = [&] {
                retail_ticket_at.reset();
                const auto ticket = session.connected();
                if (!send_datagram(peer, ticket)) {
                    fail("cannot send Steam session ticket");
                    return false;
                }
                enet_host_flush(host);
                const std::lock_guard lock{state->mutex};
                ++state->status.sent_datagrams;
                return true;
            };
            const auto retry_classic = [&](GameProtocol next) {
                // A server that ignored connect data already assigned a slot.
                // Release it before retrying, and discard packets for that peer.
                enet_peer_disconnect_now(peer, 0U);
                retail_ticket_at.reset();
                protocol = next;
                classic = std::make_unique<ClassicProtocolSession>(protocol);
                peer = enet_host_connect(host, &address, 1U, static_cast<enet_uint32>(protocol));
                if (!peer) { fail("Cannot start classic protocol connection"); return; }
                connected = false;
                joined = false;
                received_application_data = false;
                deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{connect_ms};
                const std::lock_guard lock{state->mutex};
                state->status.protocol = protocol;
                state->status.phase = LiveProtocol168Phase::connecting;
                state->status.disconnect_reason.reset();
            };
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
                        state->status.error = std::string{connected ? "Loading timed out: " : "No response from "} +
                            transport.host + ":" + std::to_string(transport.port) +
                            " (" + std::string{protocol_name(protocol)} + ")";
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
                        if (classic) break;
                        if (transport.protocol == GameProtocol::automatic) {
                            // Legacy servers can ignore ENet's connect version.
                            // Give their initial MapStart a short listen window
                            // before sending any retail application bytes. A
                            // silent retail server still gets its required 105.
                            retail_ticket_at = std::chrono::steady_clock::now() +
                                               std::chrono::milliseconds{250};
                        } else {
                            static_cast<void>(send_retail_ticket());
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
                        if (const auto next = next_protocol_from_bootstrap(
                                transport.protocol, protocol, received_application_data, bytes)) {
                            enet_packet_destroy(event.packet);
                            retry_classic(*next);
                            break;
                        }
                        if (retail_ticket_at && !send_retail_ticket()) {
                            enet_packet_destroy(event.packet);
                            break;
                        }
                        received_application_data = true;
                        if (!transport.record_demo_path.empty()) {
                            if (classic) record_demo(bytes);
                            else {
                                std::string demo_decode_error;
                                if (const auto packet = decode_protocol168_server_datagram(bytes, demo_decode_error))
                                    record_demo(*packet);
                            }
                        }
                        if (classic) {
                            auto update=classic->ingest(bytes);
                            enet_packet_destroy(event.packet);
                            if (!update.error.empty()) { fail(update.error); break; }
                            deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds{transport.timeout_ms};
                            for (const auto& packet:update.wire) {
                                if (!send_datagram(peer,packet)) { fail("Cannot send classic handshake"); break; }
                            }
                            if (state->stop_requested.load()) break;
                            if (!update.wire.empty()) enet_host_flush(host);
                            const std::lock_guard lock{state->mutex};
                            if (update.map_started) {
                                state->inbound.clear(); state->outbound.clear(); state->bootstrap.reset(); state->classic_motion.reset(); state->classic_jump_pending = false;
                                state->status.initial_info.reset(); state->status.loading={};
                                state->status.map_generation=classic->generation(); state->status.phase=LiveProtocol168Phase::handshaking; joined=false;
                            }
                            state->status.loading.sync_started=true;
                            state->status.loading.sync_percent=static_cast<std::uint8_t>(std::min<std::size_t>(99,
                                classic->map_bytes()*100/std::max<std::uint32_t>(1,classic->advertised_map_bytes())));
                            if (update.bootstrap) {
                                update.bootstrap->protocol=protocol;
                                state->status.initial_info=std::make_shared<const Protocol168InitialInfo>(update.bootstrap->initial_info);
                                state->bootstrap=std::move(update.bootstrap); state->status.phase=LiveProtocol168Phase::ready;
                                state->status.loading.sync_finished=true; joined=true;
                            }
                            for (auto& packet:update.events) if (!state->inbound.push(std::move(packet))) {
                                state->status.phase=LiveProtocol168Phase::failed; state->status.error="Classic inbound queue overflow";state->stop_requested.store(true);break;
                            }
                            state->status.sent_datagrams+=update.wire.size();
                            break;
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
                        if (transport.protocol==GameProtocol::automatic) {
                            if (auto next=next_protocol(protocol,event.data,received_application_data)) {
                                retry_classic(*next);
                                break;
                            }
                        }
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
                if (retail_ticket_at && std::chrono::steady_clock::now() >= *retail_ticket_at &&
                    !send_retail_ticket()) break;
                if (classic && joined) {
                    std::optional<ClassicMotion> motion;
                    { const std::lock_guard lock{state->mutex}; motion=state->classic_motion;
                      if (motion && state->classic_jump_pending) motion->movement |= 16U;
                      state->classic_jump_pending = false; }
                    const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
                    if (motion) for (const auto& packet:classic->motion(*motion, seconds)) {
                        if (!send_datagram(peer,packet)) { fail("Cannot send classic movement"); break; }
                        const std::lock_guard lock{state->mutex}; ++state->status.sent_datagrams;
                    }
                    for (auto& packet : classic->advance(seconds)) {
                        const std::lock_guard lock{state->mutex};
                        if (!state->inbound.push(std::move(packet))) {
                            state->status.phase=LiveProtocol168Phase::failed;
                            state->status.error="Classic objective queue overflow";
                            state->stop_requested.store(true);
                            break;
                        }
                    }
                }
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
                    if (peer && peer->state == ENET_PEER_STATE_CONNECTED)
                        state->status.round_trip_time_ms = peer->roundTripTime;
                }
                for (const auto& packet : outgoing) {
                    if (classic) {
                        if (packet.front() == std::byte{255} &&
                            !classic->accepts_client_action(std::span{packet}.subspan(1)))
                            continue;
                        const auto translated=packet.front()==std::byte{255}
                            ? ClassicPackets{std::vector<std::byte>{packet.begin()+1,packet.end()}}
                            : classic->translate_client(packet);
                        for (const auto& wire:translated) {
                            if (!send_datagram(peer,wire)) { fail("Cannot send classic packet"); break; }
                            const std::lock_guard lock{state->mutex}; ++state->status.sent_datagrams;
                        }
                        if (state->stop_requested.load()) break;
                        continue;
                    }
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

            if (!demo.finish()) {
                const std::lock_guard lock{state->mutex};
                state->status.demo_error = demo.error();
            }
            if (peer != nullptr) {
                if (peer->state == ENET_PEER_STATE_CONNECTED ||
                    peer->state == ENET_PEER_STATE_CONNECTION_SUCCEEDED) {
                    enet_peer_disconnect(peer, 0U);
                    enet_host_flush(host);
                }
                enet_peer_reset(peer);
            }
            const std::lock_guard lock{state->mutex};
            if (state->status.phase != LiveProtocol168Phase::failed &&
                state->status.phase != LiveProtocol168Phase::disconnected) {
                state->status.phase = LiveProtocol168Phase::stopped;
            }
        }};
    return true;
}

void LiveProtocol168Connection::run_demo_playback(const std::filesystem::path& path) {
    DemoPlayback playback;
    if (!playback.open(path)) {
        const std::lock_guard lock{state_->mutex};
        state_->status.phase = LiveProtocol168Phase::failed;
        state_->status.error = playback.error();
        return;
    }
    {
        const std::lock_guard lock{state_->mutex};
        state_->status.protocol = playback.protocol();
        state_->status.phase = LiveProtocol168Phase::handshaking;
    }
    auto last_tick = std::chrono::steady_clock::now();
    std::uint64_t elapsed_us{};
    while (!state_->stop_requested.load()) {
        const auto now = std::chrono::steady_clock::now();
        bool pause{};
        {
            const std::lock_guard lock{state_->mutex};
            // Loading and a slow renderer apply backpressure to file playback.
            // Never skip terrain updates or advance the demo while its map is
            // waiting for the presentation thread to adopt it.
            pause = state_->demo_paused.load() || state_->bootstrap != nullptr || state_->inbound.size() >= 512U;
        }
        if (!pause) {
            elapsed_us += static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(now - last_tick).count());
            auto update = playback.advance(elapsed_us);
            const std::lock_guard lock{state_->mutex};
            if (!update.error.empty()) {
                state_->status.phase = LiveProtocol168Phase::failed;
                state_->status.error = std::move(update.error);
                return;
            }
            if (update.map_started) {
                state_->inbound.clear();
                state_->status.phase = LiveProtocol168Phase::handshaking;
            }
            state_->status.map_generation = update.map_generation;
            state_->status.loading = update.loading;
            state_->status.initial_info = std::move(update.initial_info);
            if (update.bootstrap) {
                state_->bootstrap = std::move(update.bootstrap);
                state_->demo_paused.store(true);
                state_->status.phase = LiveProtocol168Phase::ready;
            }
            for (auto& packet : update.packets) {
                if (!state_->inbound.push(std::move(packet))) {
                    state_->status.phase = LiveProtocol168Phase::failed;
                    state_->status.error = "demo inbound queue overflow";
                    return;
                }
                ++state_->status.received_datagrams;
            }
            if (update.finished) {
                // Keep the final world visible until the viewer leaves it.
                state_->status.demo_finished = true;
                return;
            }
        }
        last_tick = now;
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }
    const std::lock_guard lock{state_->mutex};
    state_->status.phase = LiveProtocol168Phase::stopped;
}

void LiveProtocol168Connection::set_demo_paused(bool paused) noexcept {
    state_->demo_paused.store(paused);
}

bool LiveProtocol168Connection::send_classic(std::span<const std::byte> packet) {
    if (!valid_classic_client_action(packet)) return false;
    const std::lock_guard lock{state_->mutex};
    if (state_->status.demo_playback) return true;
    if (!is_classic_protocol(state_->status.protocol) || state_->status.phase!=LiveProtocol168Phase::ready ||
        state_->bootstrap || state_->outbound.size()>=outbound_capacity) return false;
    std::vector<std::byte> tagged{std::byte{255}}; tagged.insert(tagged.end(),packet.begin(),packet.end());
    state_->outbound.push_back(std::move(tagged)); return true;
}
void LiveProtocol168Connection::update_classic_motion(const ClassicMotion& motion) {
    const std::lock_guard lock{state_->mutex};
    if (state_->status.demo_playback) return;
    if (is_classic_protocol(state_->status.protocol) && state_->status.phase==LiveProtocol168Phase::ready &&
        state_->bootstrap == nullptr) {
        state_->classic_jump_pending = motion.alive && (state_->classic_jump_pending || (motion.movement & 16U));
        state_->classic_motion=motion;
    }
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
    if (state_->status.demo_playback) return true;
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
