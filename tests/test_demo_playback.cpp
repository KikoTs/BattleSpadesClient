#include "battlespades/network/demo_playback.hpp"
#include "battlespades/network/demo_recorder.hpp"
#include "battlespades/network/protocol168_weapons.hpp"

#include <bit>
#include <atomic>
#include <chrono>
#include <iostream>
#include <future>
#include <stdexcept>
#include <thread>
#include <enet/enet.h>
#include <zlib.h>

using namespace battlespades::network;
namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error{message}; }
template<typename T> void integer(std::vector<std::byte>& bytes, T value) {
    for (std::size_t i{}; i < sizeof(T); ++i)
        bytes.push_back(static_cast<std::byte>(value >> (i * 8U)));
}
void number(std::vector<std::byte>& bytes, float value) { integer(bytes, std::bit_cast<std::uint32_t>(value)); }
void text(std::vector<std::byte>& bytes, std::string_view value) {
    for (const auto character : value) bytes.push_back(static_cast<std::byte>(character));
    bytes.push_back(std::byte{});
}

void make_retail_demo(const std::filesystem::path& path) {
    DemoWriter writer;
    check(writer.open(path, 168U), "retail recording opens");
    const auto record = [&writer](std::span<const std::byte> packet) {
        check(writer.append(0U, packet), "retail fixture records");
    };
    std::vector<std::byte> info{std::byte{114}};
    integer<std::uint64_t>(info, 0U); integer<std::uint32_t>(info, 0U); integer<std::uint32_t>(info, 27015U);
    for (const auto value : {"TDM", "Demo test", "One", "Two", "Three", "Training", "Training.vxl"}) text(info, value);
    integer<std::uint32_t>(info, 0x12345678U);
    info.push_back(std::byte{1}); info.push_back(std::byte{}); integer<std::uint16_t>(info, 27016U);
    for (const auto value : {1, 1, 1, 192, 1, 1, 1, 1, 1, 1, 1}) info.push_back(static_cast<std::byte>(value));
    text(info, "");
    for (const auto value : {1, 1, 1}) info.push_back(static_cast<std::byte>(value));
    integer<std::uint16_t>(info, 64U); integer<std::uint16_t>(info, 64U);
    for (std::size_t i{}; i < 4U; ++i) info.push_back(std::byte{});
    info.push_back(std::byte{1}); text(info, "Demo server");
    for (const auto value : {0, 0, 1, 0, 0, 0, 0}) info.push_back(static_cast<std::byte>(value));
    record(info);
    const std::array validation{std::byte{60}, std::byte{0x78}, std::byte{0x56}, std::byte{0x34}, std::byte{0x12}};
    record(validation); record(std::array{std::byte{55}});
    std::vector<std::byte> columns;
    columns.reserve(512U * 512U * 16U);
    for (std::uint32_t y{}; y < 512U; ++y) for (std::uint32_t x{}; x < 512U; ++x) {
        integer(columns, x); integer(columns, y);
        for (const auto value : {0, 239, 239, 0, 64, 80, 96, 127}) columns.push_back(static_cast<std::byte>(value));
    }
    uLongf size = compressBound(static_cast<uLong>(columns.size()));
    std::vector<std::byte> compressed(size);
    check(compress2(reinterpret_cast<Bytef*>(compressed.data()), &size,
                   reinterpret_cast<const Bytef*>(columns.data()), static_cast<uLong>(columns.size()), 1) == Z_OK,
          "retail map compresses");
    compressed.resize(size);
    for (std::size_t offset{}; offset < compressed.size(); offset += 1024U) {
        const auto count = std::min<std::size_t>(1024U, compressed.size() - offset);
        std::vector<std::byte> chunk{std::byte{57}, std::byte{100}};
        integer(chunk, static_cast<std::uint16_t>(count));
        chunk.insert(chunk.end(), compressed.begin() + static_cast<std::ptrdiff_t>(offset),
                     compressed.begin() + static_cast<std::ptrdiff_t>(offset + count));
        record(chunk);
    }
    record(std::array{std::byte{59}});
    std::vector<std::byte> state{std::byte{45}, std::byte{17}};
    state.insert(state.end(), 3U, std::byte{128}); integer<std::uint16_t>(state, 64U);
    state.insert(state.end(), 21U, std::byte{});
    integer<std::uint16_t>(state, 64U); integer<std::uint16_t>(state, 64U);
    state.push_back(std::byte{10}); state.push_back(std::byte{1}); state.push_back(std::byte{});
    for (const auto name : {"Blue", "Green"}) {
        text(state, name); state.insert(state.end(), 3U, std::byte{128}); integer<std::uint32_t>(state, 0U);
        state.push_back(std::byte{}); state.push_back(std::byte{1}); state.push_back(std::byte{1});
    }
    state.push_back(std::byte{}); integer<std::uint16_t>(state, 0U); integer<std::uint16_t>(state, 0U);
    state.insert(state.end(), 3U, std::byte{});
    record(state);
    check(writer.finish(), "retail file closes");
}

void make_classic_demo(const std::filesystem::path& path) {
    DemoWriter writer;
    check(writer.open(path, 3U), "classic recording opens");
    std::vector<std::byte> raw;
    raw.reserve(512U * 512U * 20U);
    for (std::size_t i{}; i < 512U * 512U; ++i) {
        for (const auto b : {0, 60, 63, 0}) raw.push_back(static_cast<std::byte>(b));
        for (std::size_t z{60U}; z < 64U; ++z)
            for (const auto b : {0, 255, 0, 128}) raw.push_back(static_cast<std::byte>(b));
    }
    uLongf size = compressBound(static_cast<uLong>(raw.size()));
    std::vector<std::byte> compressed(size);
    check(compress2(reinterpret_cast<Bytef*>(compressed.data()), &size,
                   reinterpret_cast<const Bytef*>(raw.data()), static_cast<uLong>(raw.size()), 1) == Z_OK,
          "map compresses");
    compressed.resize(size);
    std::vector<std::byte> start{std::byte{18}};
    integer(start, static_cast<std::uint32_t>(compressed.size()));
    check(writer.append(0U, start), "map start records");
    compressed.insert(compressed.begin(), std::byte{19});
    check(writer.append(0U, compressed), "full map records");
    std::vector<std::byte> state{std::byte{15}, std::byte{0}};
    for (std::size_t i{}; i < 9U; ++i) state.push_back(std::byte{128});
    for (std::size_t i{}; i < 20U; ++i) state.push_back(std::byte{0});
    for (const auto b : {0, 0, 0, 10, 0}) state.push_back(static_cast<std::byte>(b));
    for (std::size_t i{}; i < 4U; ++i) {
        number(state, 40.5F + static_cast<float>(i) * 100.0F);
        number(state, 60.5F); number(state, 59.0F);
    }
    check(writer.append(0U, state), "state records");
    const std::vector<std::byte> color{std::byte{8}, std::byte{1}, std::byte{30}, std::byte{20}, std::byte{10}};
    check(writer.append(100'000U, color) && writer.finish(), "runtime packet and footer record");
}

void live_capture_roundtrip(const std::filesystem::path& fixture, const std::filesystem::path& captured,
                            GameProtocol protocol = GameProtocol::classic075) {
    check(enet_initialize() == 0, "loopback ENet initializes");
    struct EnetLifetime { ~EnetLifetime() { enet_deinitialize(); } } enet_lifetime;
    ENetAddress address{};
    check(enet_address_set_host(&address, "127.0.0.1") == 0, "loopback address resolves");
    auto* server = enet_host_create(&address, 1U, 1U, 0U, 0U);
    check(server != nullptr, "loopback demo test server opens an ephemeral port");
    struct ServerLifetime { ENetHost* host; ~ServerLifetime() { enet_host_destroy(host); } } server_lifetime{server};
    check(enet_host_compress_with_range_coder(server) == 0, "loopback compression initializes");
    LiveProtocol168Connection connection;
    EnetProtocol168Config config;
    config.port = server->address.port;
    config.protocol = protocol;
    config.record_demo_path = captured;
    check(connection.start(config, {}), "real connection starts recording");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    std::size_t sent{};
    while (std::chrono::steady_clock::now() < deadline) {
        ENetEvent event{};
        if (enet_host_service(server, &event, 2U) > 0) {
            if (event.type == ENET_EVENT_TYPE_CONNECT) {
                DemoReader reader;
                check(reader.open(fixture), "server opens prerecorded fixture");
                while (const auto packet = reader.next()) {
                    auto wire = packet->bytes;
                    if (protocol == GameProtocol::retail168) {
                        wire.assign(1U, std::byte{0x30});
                        for (std::size_t offset{}; offset < packet->bytes.size(); offset += 32U) {
                            const auto count = std::min<std::size_t>(32U, packet->bytes.size() - offset);
                            wire.push_back(static_cast<std::byte>(count - 1U));
                            wire.insert(wire.end(), packet->bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                                        packet->bytes.begin() + static_cast<std::ptrdiff_t>(offset + count));
                        }
                    }
                    auto* outgoing = enet_packet_create(wire.data(), wire.size(), ENET_PACKET_FLAG_RELIABLE);
                    check(outgoing != nullptr && enet_peer_send(event.peer, 0U, outgoing) == 0,
                          "server sends recorded game packets");
                    ++sent;
                }
                enet_host_flush(server);
            } else if (event.type == ENET_EVENT_TYPE_RECEIVE) {
                enet_packet_destroy(event.packet);
            }
        }
        if (sent > 0U && connection.status().received_datagrams == sent) break;
    }
    check(sent > 0U && connection.status().received_datagrams == sent && connection.status().demo_error.empty(),
          "live recorder receives the whole server stream");
    connection.stop();
    DemoReader source;
    DemoReader recording;
    check(source.open(fixture) && recording.open(captured), "closed live capture is readable");
    std::size_t count{};
    while (const auto packet = source.next()) {
        const auto copied = recording.next();
        check(copied && copied->bytes == packet->bytes, "live capture preserves each incoming packet");
        ++count;
    }
    check(count == sent && !recording.next() && recording.finished(), "capture contains only incoming packets and a clean footer");
    DemoPlayback replay;
    check(replay.open(captured), "live recording opens for replay");
    DemoPlaybackUpdate replayed;
    for (std::size_t attempts{}; attempts < 100U; ++attempts) {
        replayed = replay.advance(demo_maximum_time_us);
        if (replayed.bootstrap || !replayed.error.empty()) break;
    }
    check(replayed.error.empty() && replayed.bootstrap != nullptr,
          "an actual live network capture rebuilds the world offline");
}

void companion_capture(const std::filesystem::path& fixture, const std::filesystem::path& captured,
                       GameProtocol protocol) {
    check(enet_initialize() == 0, "companion test ENet initializes");
    struct EnetLifetime { ~EnetLifetime() { enet_deinitialize(); } } enet_lifetime;
    ENetAddress address{};
    check(enet_address_set_host(&address, "127.0.0.1") == 0, "companion test uses loopback");
    auto* server = enet_host_create(&address, 1U, 1U, 0U, 0U);
    check(server != nullptr, "companion test server opens");
    struct ServerLifetime { ENetHost* host; ~ServerLifetime() { enet_host_destroy(host); } } server_lifetime{server};
    check(enet_host_compress_with_range_coder(server) == 0, "companion test compression initializes");
    DemoRecorderOptions options;
    options.transport.port = server->address.port;
    options.transport.protocol = protocol;
    options.transport.record_demo_path = captured;
    options.duration_seconds = 1U;
    std::atomic<bool> stop{};
    auto recorder = std::async(std::launch::async, [&] { return run_demo_recorder(options, [&] { return stop.load(); }); });
    struct StopGuard { std::atomic<bool>& stop; ~StopGuard() { stop.store(true); } } stop_guard{stop};
    bool spectator_join{};
    bool scene_ready{};
    bool neutral_input{};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (std::chrono::steady_clock::now() < deadline &&
           recorder.wait_for(std::chrono::milliseconds{0}) != std::future_status::ready) {
        ENetEvent event{};
        if (enet_host_service(server, &event, 2U) <= 0) continue;
        if (event.type == ENET_EVENT_TYPE_CONNECT) {
            DemoReader reader;
            check(reader.open(fixture), "companion server fixture opens");
            while (const auto packet = reader.next()) {
                auto wire = packet->bytes;
                if (protocol == GameProtocol::retail168) {
                    wire.assign(1U, std::byte{0x30});
                    for (std::size_t offset{}; offset < packet->bytes.size(); offset += 32U) {
                        const auto count = std::min<std::size_t>(32U, packet->bytes.size() - offset);
                        wire.push_back(static_cast<std::byte>(count - 1U));
                        wire.insert(wire.end(), packet->bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                                    packet->bytes.begin() + static_cast<std::ptrdiff_t>(offset + count));
                    }
                }
                auto* outgoing = enet_packet_create(wire.data(), wire.size(), ENET_PACKET_FLAG_RELIABLE);
                check(outgoing != nullptr && enet_peer_send(event.peer, 0U, outgoing) == 0, "companion fixture sends");
            }
            enet_host_flush(server);
        } else if (event.type == ENET_EVENT_TYPE_RECEIVE) {
            std::vector<std::byte> packet(reinterpret_cast<const std::byte*>(event.packet->data),
                                          reinterpret_cast<const std::byte*>(event.packet->data) + event.packet->dataLength);
            enet_packet_destroy(event.packet);
            if (protocol == GameProtocol::retail168 && !packet.empty()) packet.erase(packet.begin());
            if (protocol == GameProtocol::retail168) {
                if (packet.size() >= 3U && packet[0] == std::byte{15}) spectator_join = packet[1] == std::byte{};
                if (packet == std::vector{std::byte{110}, std::byte{0}}) scene_ready = true;
                if (!packet.empty() && packet[0] == std::byte{4}) {
                    const auto decoded = decode_weapon_packet(packet);
                    if (decoded) if (const auto* input = std::get_if<ClientDataPacket>(&*decoded.packet))
                        neutral_input = input->movement_flags == 0U && input->action_flags == 0U && input->tool_id == 0U;
                }
            } else if (packet.size() >= 3U && packet[0] == std::byte{9}) {
                spectator_join = packet[2] == std::byte{255};
                // Exercise the same stop callback used by Ctrl+C, independent
                // of the duration-completion branch exercised for retail.
                stop.store(true);
            }
        }
    }
    stop.store(true);
    const auto result = recorder.get();
    if (!result.error.empty()) std::cerr << result.error << '\n';
    check(result.recorded && result.error.empty() && spectator_join,
          "companion recorder automatically joins spectators and finalizes on duration/stop");
    check(protocol != GameProtocol::retail168 || (scene_ready && neutral_input),
          "retail recorder opens gameplay stream with neutral readiness, never actions");
    DemoReader reader;
    check(reader.open(captured), "companion capture opens");
    while (reader.next()) {}
    check(reader.finished() && reader.error().empty(), "companion shutdown closes a complete file");
}
}

int main() {
    const auto directory = std::filesystem::temp_directory_path() /
        ("battlespades-playback-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(directory);
    try {
        const auto path = directory / "classic.bsdem";
        make_classic_demo(path);
        live_capture_roundtrip(path, directory / "captured.bsdem");
        companion_capture(path, directory / "classic-companion.bsdem", GameProtocol::classic075);
        DemoPlayback playback;
        check(playback.open(path) && playback.protocol() == GameProtocol::classic075, "replay opens offline");
        auto first = playback.advance(0U);
        check(first.error.empty() && first.bootstrap && first.bootstrap->map->solid(0, 0, 236),
              "recorded map rehydrates without external terrain");
        check(!playback.advance(99'999U).finished, "future packet is not replayed early");
        auto last = playback.advance(100'000U);
        check(last.error.empty() && last.finished && !last.packets.empty(), "packet reaches gameplay at recorded time");

        LiveProtocol168Connection connection;
        EnetProtocol168Config config;
        config.host.clear(); config.port = 0U; config.timeout_ms = 0U;
        config.play_demo_path = path;
        check(connection.start(config, {}), "replay bypasses network endpoint validation");
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
        std::unique_ptr<Protocol168WorldBootstrap> bootstrap;
        while (std::chrono::steady_clock::now() < deadline && !bootstrap) {
            bootstrap = connection.take_bootstrap();
            if (!bootstrap) std::this_thread::sleep_for(std::chrono::milliseconds{2});
        }
        check(bootstrap != nullptr, "background replay publishes normal bootstrap");
        std::this_thread::sleep_for(std::chrono::milliseconds{150});
        check(!connection.status().demo_finished, "loader adoption does not start playback before START");
        connection.set_demo_paused(false);
        while (std::chrono::steady_clock::now() < deadline && !connection.status().demo_finished) {
            static_cast<void>(connection.take_inbound(128U, bootstrap->map_generation));
            std::this_thread::sleep_for(std::chrono::milliseconds{2});
        }
        const auto status = connection.status();
        check(status.demo_playback && status.demo_finished && status.phase == LiveProtocol168Phase::ready &&
              status.sent_datagrams == 0U, "replay finishes and retains scene without sending datagrams");
        const std::array outgoing{std::byte{4}, std::byte{1}};
        check(connection.send(outgoing) && connection.status().queued_outbound == 0U,
              "replay discards local outgoing input");
        connection.stop();
        config.record_demo_path = directory / "conflict.bsdem";
        check(!connection.start(config, {}), "record and replay cannot be combined");
        const auto retail = directory / "retail.bsdem";
        make_retail_demo(retail);
        live_capture_roundtrip(retail, directory / "retail-captured.bsdem", GameProtocol::retail168);
        companion_capture(retail, directory / "retail-companion.bsdem", GameProtocol::retail168);
        check(playback.open(retail), "retail replay opens");
        DemoPlaybackUpdate retail_update;
        for (std::size_t attempts{}; attempts < 100U; ++attempts) {
            retail_update = playback.advance(0U);
            if (retail_update.bootstrap || !retail_update.error.empty()) break;
        }
        if (!retail_update.error.empty()) std::cerr << retail_update.error << '\n';
        check(retail_update.error.empty() && retail_update.bootstrap &&
              retail_update.bootstrap->protocol == GameProtocol::retail168 &&
              retail_update.bootstrap->map->solid(10, 20, 239),
              "complete recorded retail map rehydrates without local map cache or server");
        check(playback.advance(0U).finished, "retail recording completes normally");
        const auto invalid = directory / "broken.bsdem";
        DemoWriter writer;
        check(writer.open(invalid, 168U) && writer.append(0U, std::array{std::byte{59}}) && writer.finish(),
              "malformed protocol fixture records");
        check(playback.open(invalid) && !playback.advance(0U).error.empty(), "out-of-order retail handshake fails closed");
        std::string parse_error;
        const std::array<std::string_view, 8U> recorder_args{
            "--protocol", "0.75", "+connect", "127.0.0.1:32887", "--record-demo", "demo.bsdem", "--duration", "2"};
        const auto recorder_options = parse_demo_recorder_options(recorder_args, parse_error);
        check(recorder_options && recorder_options->transport.protocol == GameProtocol::classic075 &&
              recorder_options->duration_seconds == 2U, "recorder options preserve explicit protocol regardless of argument order");
        check(!parse_demo_recorder_options(std::array<std::string_view, 2U>{"--duration", "0"}, parse_error),
              "invalid recorder duration is rejected");
        static_cast<void>(playback.open(directory / "missing.bsdem"));
        std::filesystem::remove_all(directory);
        std::cout << "Demo playback tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
