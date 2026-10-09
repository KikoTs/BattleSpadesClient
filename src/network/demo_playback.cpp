#include "battlespades/network/demo_playback.hpp"
#include "battlespades/network/classic_protocol.hpp"

namespace battlespades::network {

struct DemoPlayback::State final {
    DemoReader reader;
    std::optional<DemoPacket> pending;
    std::unique_ptr<Protocol168Session> retail;
    std::unique_ptr<ClassicProtocolSession> classic;
    std::shared_ptr<const Protocol168InitialInfo> initial_info;
    Protocol168LoadingProgress loading;
    std::uint64_t generation{};
    bool joined{};
    bool ever_joined{};
    std::string error;
};

DemoPlayback::DemoPlayback() : state_{std::make_unique<State>()} {}
DemoPlayback::~DemoPlayback() = default;
GameProtocol DemoPlayback::protocol() const noexcept {
    return static_cast<GameProtocol>(state_->reader.protocol());
}
const std::string& DemoPlayback::error() const noexcept { return state_->error; }

bool DemoPlayback::open(const std::filesystem::path& path) {
    state_ = std::make_unique<State>();
    if (!state_->reader.open(path)) { state_->error = state_->reader.error(); return false; }
    if (is_classic_protocol(protocol())) {
        state_->classic = std::make_unique<ClassicProtocolSession>(protocol());
    } else {
        Protocol168SessionConfig config;
        config.auto_join = false;
        config.team = 0U;
        state_->retail = std::make_unique<Protocol168Session>(std::move(config));
        static_cast<void>(state_->retail->connected());
    }
    return true;
}

DemoPlaybackUpdate DemoPlayback::advance(std::uint64_t time_us, std::size_t budget) {
    DemoPlaybackUpdate result;
    auto& state = *state_;
    while (budget-- > 0U && state.error.empty()) {
        if (!state.pending) state.pending = state.reader.next();
        if (!state.pending) {
            state.error = state.reader.error();
            if (state.reader.finished() && !state.ever_joined)
                state.error = "demo ended before a complete map and player bootstrap";
            break;
        }
        if (state.pending->time_us > time_us) break;
        auto packet = std::move(state.pending->bytes);
        state.pending.reset();
        if (state.classic) {
            auto update = state.classic->ingest(packet);
            if (!update.error.empty()) { state.error = std::move(update.error); break; }
            if (update.map_started) {
                state.joined = false;
                state.initial_info.reset();
                state.loading = {};
                result.map_started = true;
                state.generation = state.classic->generation();
            }
            for (auto& event : update.events) result.packets.push_back(std::move(event));
            state.loading.sync_started = true;
            state.loading.sync_percent = static_cast<std::uint8_t>(std::min<std::size_t>(99U,
                state.classic->map_bytes() * 100U /
                std::max<std::uint32_t>(1U, state.classic->advertised_map_bytes())));
            if (update.bootstrap) {
                update.bootstrap->protocol = protocol();
                state.initial_info = std::make_shared<const Protocol168InitialInfo>(update.bootstrap->initial_info);
                result.bootstrap = std::move(update.bootstrap);
                state.loading.sync_finished = true;
                state.joined = true;
                state.ever_joined = true;
                break;
            }
        } else if (state.retail) {
            // Password prompts precede the playable stream. No replay ever
            // asks for credentials or sends an authentication response.
            if (packet.front() == std::byte{112U}) continue;
            if (packet.front() == std::byte{114U}) state.joined = false;
            if (state.joined) {
                result.packets.push_back(std::move(packet));
                continue;
            }
            auto& session = *state.retail;
            const auto ingested = session.ingest_packet(packet);
            // Follow the live adapter's treatment of optional/unknown join
            // packets. Critical malformed input sets the session to failed.
            static_cast<void>(ingested);
            if (state.generation != session.map_generation()) {
                result.map_started = true;
                state.generation = session.map_generation();
                state.initial_info.reset();
            }
            state.loading = session.loading_progress();
            if (session.initial_info() && !state.initial_info)
                state.initial_info = std::make_shared<const Protocol168InitialInfo>(*session.initial_info());
            if (session.phase() == Protocol168SessionPhase::failed) {
                state.error = std::string{session.last_error()};
                break;
            }
            if (session.bootstrap_ready()) {
                auto map = session.take_map();
                const auto id = session.local_player_id();
                if (!map || !id || !session.initial_info()) {
                    state.error = "incomplete demo bootstrap";
                    break;
                }
                auto bootstrap = std::make_unique<Protocol168WorldBootstrap>();
                bootstrap->map_generation = session.map_generation();
                bootstrap->initial_info = *session.initial_info();
                if (session.state_info()) bootstrap->state_info = *session.state_info();
                if (session.skybox_info()) bootstrap->skybox_info = *session.skybox_info();
                bootstrap->map = std::make_shared<world::VxlMap>(std::move(*map));
                bootstrap->roster = session.roster();
                bootstrap->local_player_id = *id;
                bootstrap->next_client_loop_count = session.next_client_loop_count();
                bootstrap->protocol = protocol();
                result.bootstrap = std::move(bootstrap);
                result.packets = session.take_deferred_runtime_packets();
                state.joined = true;
                state.ever_joined = true;
                break;
            }
        }
    }
    result.initial_info = state.initial_info;
    result.loading = state.loading;
    result.map_generation = state.generation;
    result.finished = state.reader.finished();
    result.error = state.error;
    return result;
}
} // namespace battlespades::network
