#include "battlespades/network/demo_recorder.hpp"
#include "battlespades/network/live_protocol168_connection.hpp"
#include "battlespades/network/protocol168_runtime.hpp"
#include "battlespades/network/protocol168_weapons.hpp"
#include "battlespades/network/server_discovery.hpp"

#include <charconv>
#include <algorithm>
#include <chrono>
#include <thread>

namespace battlespades::network {

std::string_view demo_recorder_usage() noexcept {
    return "Usage: BattleSpadesDemoRecorder +connect HOST:PORT --record-demo FILE\n"
           "       [--duration SECONDS] [--name NAME] [--password PASSWORD]\n"
           "       [--protocol auto|168|0.75|0.76]\n"
           "Records as a spectator without a window or master server.\n"
           "Duration starts after the initial map loads (1..86400 seconds).\n"
           "Without --duration, Ctrl+C/SIGTERM stops and finalizes the file.\n"
           "Existing files are preserved. A disconnect ends this recording.\n";
}

std::optional<DemoRecorderOptions> parse_demo_recorder_options(
    std::span<const std::string_view> arguments, std::string& error) {
    error.clear();
    DemoRecorderOptions result;
    result.transport.protocol = GameProtocol::automatic;
    bool connected{};
    std::optional<GameProtocol> protocol_override;
    for (std::size_t index{}; index < arguments.size(); ++index) {
        const auto option = arguments[index];
        if (option == "--help" || option == "-h") { result.help = true; return result; }
        if (index + 1U == arguments.size()) { error = "missing value for " + std::string{option}; return std::nullopt; }
        const auto value = arguments[++index];
        if (option == "+connect" || option == "--connect") {
            ServerEndpoint endpoint;
            if (!parse_server_endpoint(value, endpoint, error)) return std::nullopt;
            result.transport.host = endpoint.host;
            result.transport.port = endpoint.port;
            result.transport.protocol = endpoint.protocol;
            connected = true;
        } else if (option == "--record-demo") {
            std::u8string path;
            path.reserve(value.size());
            for (const auto character : value) path.push_back(static_cast<char8_t>(character));
            result.transport.record_demo_path = std::filesystem::path{path};
        } else if (option == "--duration") {
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result.duration_seconds);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
                result.duration_seconds == 0U || result.duration_seconds > 86'400U) {
                error = "duration must be an integer from 1 to 86400 seconds";
                return std::nullopt;
            }
        } else if (option == "--name") {
            if (value.empty() || value.size() > 15U || std::any_of(value.begin(), value.end(), [](char character) {
                    const auto byte = static_cast<unsigned char>(character);
                    return byte < 32U || byte == 127U;
                })) {
                error = "recorder name must be 1..15 bytes without control characters";
                return std::nullopt;
            }
            result.name = value;
        } else if (option == "--password") {
            if (encode_password_provided_packet(value).empty()) { error = "invalid server password"; return std::nullopt; }
            result.password = value;
        } else if (option == "--protocol") {
            if (value == "auto") protocol_override = GameProtocol::automatic;
            else if (value == "168") protocol_override = GameProtocol::retail168;
            else if (value == "0.75") protocol_override = GameProtocol::classic075;
            else if (value == "0.76") protocol_override = GameProtocol::classic076;
            else { error = "protocol must be auto, 168, 0.75 or 0.76"; return std::nullopt; }
        } else {
            error = "unknown recorder option: " + std::string{option};
            return std::nullopt;
        }
    }
    if (!connected || result.transport.record_demo_path.empty()) {
        error = "+connect and --record-demo are required";
        return std::nullopt;
    }
    if (protocol_override) result.transport.protocol = *protocol_override;
    return result;
}

DemoRecorderResult run_demo_recorder(const DemoRecorderOptions& options,
    const std::function<bool()>& stop_requested,
    const std::function<void(std::string_view)>& report) {
    DemoRecorderResult result;
    std::error_code ec;
    if (options.transport.record_demo_path.empty() || !options.transport.play_demo_path.empty() ||
        std::filesystem::exists(options.transport.record_demo_path, ec) || ec) {
        result.error = "recording requires a new accessible output filename and cannot use playback";
        return result;
    }
    Protocol168SessionConfig session;
    session.player_name = options.name;
    session.server_password = options.password;
    session.team = 0U;
    session.class_id = 0U;
    session.auto_join = false;
    LiveProtocol168Connection connection;
    if (!connection.start(options.transport, session)) { result.error = connection.status().error; return result; }
    std::optional<std::chrono::steady_clock::time_point> started;
    std::optional<std::uint8_t> local_id;
    std::uint64_t generation{};
    std::int32_t loop{};
    auto clock_at = std::chrono::steady_clock::now();
    for (;;) {
        if (stop_requested && stop_requested()) break;
        const auto now = std::chrono::steady_clock::now();
        if (started && options.duration_seconds != 0U &&
            now - *started >= std::chrono::seconds{options.duration_seconds}) break;
        const auto status = connection.status();
        result.received_packets = status.received_datagrams;
        if (!status.demo_error.empty()) { result.error = status.demo_error; break; }
        if (status.password.pending) {
            result.error = "server requires a password or rejected the supplied password";
            break;
        }
        if (status.phase == LiveProtocol168Phase::failed || status.phase == LiveProtocol168Phase::disconnected) {
            result.error = status.error;
            break;
        }
        if (auto bootstrap = connection.take_bootstrap()) {
            if (!bootstrap->initial_info.enable_spectator) {
                result.error = "server has disabled spectators";
                break;
            }
            local_id = bootstrap->local_player_id;
            generation = bootstrap->map_generation;
            loop = 0;
            if (!connection.send(encode_protocol168_new_player_connection(session)) ||
                !connection.send(std::array{std::byte{110}, std::byte{0}})) {
                result.error = "cannot join as a recording spectator";
                break;
            }
            if (!started) started = now;
            result.recorded = true;
            if (report) report("Recording spectator connected; map transfer complete.");
        }
        for (const auto& packet : connection.take_inbound(1024U, generation)) {
            if (packet.empty()) continue;
            if (packet.front() == std::byte{52U}) {
                // MatchEnded asks the client to enter LoadingMenu before the
                // same connection is allowed to receive the next InitialInfo.
                static_cast<void>(connection.send(std::array{std::byte{110}, std::byte{1}}));
            }
            if (packet.front() == static_cast<std::byte>(CreatePlayerPacket::id)) {
                const auto player = decode_create_player(packet);
                if (player && local_id && player.packet->player_id == *local_id && player.packet->team != 0U)
                    result.error = "server assigned the recorder to a playing team instead of spectators";
            }
        }
        if (!result.error.empty()) break;
        if (local_id && status.phase == LiveProtocol168Phase::ready && !is_classic_protocol(status.protocol)) {
            ClientDataPacket input;
            input.loop_count = loop++;
            input.player_id = *local_id;
            input.orientation = {1.0F, 0.0F, 0.0F};
            input.opaque_state = protocol168_client_data_opaque_state(input.loop_count);
            if (!connection.send(encode_packet(input))) { result.error = "cannot send spectator readiness"; break; }
            if (now >= clock_at) {
                const auto token = static_cast<std::int32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - started.value_or(now)).count());
                static_cast<void>(connection.send(encode_packet(ClockSyncPacket{token, 0})));
                clock_at = now + std::chrono::seconds{1};
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{16});
    }
    connection.stop();
    const auto final = connection.status();
    result.received_packets = final.received_datagrams;
    if (result.error.empty() && !final.demo_error.empty()) result.error = final.demo_error;
    if (!result.recorded && result.error.empty()) result.error = "recording stopped before a complete map arrived";
    return result;
}
} // namespace battlespades::network
