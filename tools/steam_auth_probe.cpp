// Manual Steam callback probe. Never prints or persists ticket bytes.
#include "battlespades/platform/steam_networking.hpp"
#include "battlespades/platform/native_steam_client.hpp"
#include "battlespades/network/live_protocol168_connection.hpp"
#include "battlespades/network/server_discovery.hpp"
#include <enet/enet.h>

#include <charconv>
#include <chrono>
#include <fstream>
#include <iostream>
#include <string_view>
#include <thread>
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif

namespace {
#if defined(_WIN32)
// The comparison child receives the opaque ticket only through an anonymous
// pipe. Keep the parent Steam session alive until its no-spawn probe finishes.
bool original_network(const char* python, const char* script,
                      const battlespades::network::ServerEndpoint& endpoint,
                      const std::vector<std::byte>& ticket) {
    if (ticket.empty() || ticket.size() > 2048U) return false;
    const auto quote = [](std::wstring_view value) {
        std::wstring result{L"\""};
        std::size_t slashes{};
        for (const auto ch : value) {
            if (ch == L'\\') { ++slashes; continue; }
            result.append(slashes * (ch == L'"' ? 2U : 1U), L'\\');
            slashes = 0U;
            if (ch == L'"') result.push_back(L'\\');
            result.push_back(ch);
        }
        result.append(slashes * 2U, L'\\');
        result.push_back(L'"');
        return result;
    };
    const auto executable = std::filesystem::absolute(python).wstring();
    auto command = quote(executable) + L" " + quote(std::filesystem::absolute(script).wstring()) +
        L" " + quote(std::wstring{endpoint.host.begin(), endpoint.host.end()}) + L" " +
        std::to_wstring(endpoint.port);
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE reader{}, writer{};
    if (!CreatePipe(&reader, &writer, &attributes, 0U)) return false;
    SetHandleInformation(writer, HANDLE_FLAG_INHERIT, 0U);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = reader;
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION process{};
    const auto created = CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
    CloseHandle(reader);
    if (!created) { CloseHandle(writer); return false; }
    CloseHandle(process.hThread);
    const auto size = static_cast<std::uint32_t>(ticket.size());
    const unsigned char header[]{static_cast<unsigned char>(size), static_cast<unsigned char>(size >> 8U),
                                 static_cast<unsigned char>(size >> 16U), static_cast<unsigned char>(size >> 24U)};
    DWORD written{};
    const bool sent = WriteFile(writer, header, sizeof(header), &written, nullptr) && written == sizeof(header) &&
        WriteFile(writer, ticket.data(), size, &written, nullptr) && written == size;
    CloseHandle(writer);
    const auto waited = WaitForSingleObject(process.hProcess, 15000U);
    if (waited != WAIT_OBJECT_0) {
        TerminateProcess(process.hProcess, 1U);
        WaitForSingleObject(process.hProcess, 2000U);
    }
    DWORD code{1U};
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    std::cout << "Original network child exit=" << code << " input_sent=" << sent << '\n';
    return sent && waited == WAIT_OBJECT_0 && code == 0U;
}
#endif

void inspect_runtime(const battlespades::platform::SteamNetworkingRuntime& runtime) {
    using Interface = void*(*)();
    using LoggedOn = bool(*)(void*);
    using Subscribed = bool(*)(void*, std::uint32_t);
    const auto user = reinterpret_cast<Interface>(runtime.steamworks_symbol("SteamAPI_SteamUser_v023"));
    auto apps = reinterpret_cast<Interface>(runtime.steamworks_symbol("SteamAPI_SteamApps_v009"));
    if (apps == nullptr) apps = reinterpret_cast<Interface>(runtime.steamworks_symbol("SteamAPI_SteamApps_v008"));
    const auto logged_on = reinterpret_cast<LoggedOn>(runtime.steamworks_symbol("SteamAPI_ISteamUser_BLoggedOn"));
    const auto subscribed = reinterpret_cast<Subscribed>(runtime.steamworks_symbol("SteamAPI_ISteamApps_BIsSubscribedApp"));
    if (user && logged_on) std::cout << "steam_logged_on=" << logged_on(user()) << '\n';
    if (apps && subscribed) std::cout << "retail_subscribed=" << subscribed(apps(), 224540U) << '\n';
#if defined(_WIN32)
    for (const auto* name : {L"steam_api64.dll", L"steamclient64.dll"}) {
        wchar_t path[32768]{};
        if (const auto module = GetModuleHandleW(name); module != nullptr && GetModuleFileNameW(module, path, 32768U) != 0U)
            std::wcout << L"runtime_module=" << path << L'\n';
    }
#endif
}

// One original packet-105 exchange, with no capability trailer or join.
bool stock_handshake(const battlespades::network::ServerEndpoint& endpoint,
                     std::vector<std::byte> ticket, bool compressed,
                     const std::filesystem::path& capture = {}) {
    if (enet_initialize() != 0) return false;
    struct EnetGuard { ~EnetGuard() { enet_deinitialize(); } } guard;
    auto* host = enet_host_create(nullptr, 1U, 1U, 0U, 0U);
    if (host == nullptr) return false;
    struct HostGuard { ENetHost* host; ~HostGuard() { enet_host_destroy(host); } } host_guard{host};
    if (compressed && enet_host_compress_with_range_coder(host) != 0) return false;
    ENetAddress address{};
    address.port = endpoint.port;
    if (enet_address_set_host(&address, endpoint.host.c_str()) != 0) return false;
    auto* peer = enet_host_connect(host, &address, 1U, 168U);
    if (peer == nullptr) return false;
    battlespades::network::Protocol168SessionConfig config;
    config.auto_join = false;
    config.steam_ticket = std::move(ticket);
    battlespades::network::Protocol168Session session{std::move(config)};
    bool accepted{};
    bool connected{};
    bool disconnected{};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{8};
    while (std::chrono::steady_clock::now() < deadline) {
        ENetEvent event{};
        if (enet_host_service(host, &event, 20U) <= 0) continue;
        if (event.type == ENET_EVENT_TYPE_CONNECT) {
            connected = true;
            std::cout << "Stock ENet connected compression=" << compressed << '\n';
            const auto bytes = session.connected();
            auto* packet = enet_packet_create(bytes.data(), bytes.size(), ENET_PACKET_FLAG_RELIABLE);
            if (packet == nullptr) break;
            if (enet_peer_send(peer, 0U, packet) != 0) { enet_packet_destroy(packet); break; }
            enet_host_flush(host);
        } else if (event.type == ENET_EVENT_TYPE_RECEIVE) {
            const auto bytes = std::span<const std::byte>{
                reinterpret_cast<const std::byte*>(event.packet->data), event.packet->dataLength};
            std::string decode_error;
            const auto decoded = battlespades::network::decode_protocol168_server_datagram(bytes, decode_error);
            std::cout << "Stock response bytes=" << bytes.size() << " packet="
                      << (decoded && !decoded->empty() ? std::to_integer<int>(decoded->front()) : -1)
                      << " decode=" << decode_error << '\n';
            // Optional server-metadata fixture. Never capture outgoing packets
            // or arbitrary responses: packet 114 contains no client ticket.
            if (!capture.empty() && decoded && !decoded->empty() &&
                std::to_integer<int>(decoded->front()) == 114) {
                std::ofstream output{capture, std::ios::binary};
                output.write(reinterpret_cast<const char*>(bytes.data()),
                             static_cast<std::streamsize>(bytes.size()));
                std::cout << "InitialInfo fixture saved=" << output.good() << '\n';
            }
            const auto result = session.ingest(bytes);
            if (!result.diagnostic.empty()) std::cout << "Stock parse: " << result.diagnostic << '\n';
            enet_packet_destroy(event.packet);
            if (session.initial_info() != nullptr) {
                accepted = true;
                std::cout << "Stock ticket accepted: InitialInfo received (no map/spawn requested)\n";
                break;
            }
        } else if (event.type == ENET_EVENT_TYPE_DISCONNECT) {
            disconnected = true;
            std::cout << "Stock ticket refused: reason=" << event.data << '\n';
            break;
        }
    }
    if (!accepted && !disconnected) std::cout << "Stock timeout connected=" << connected
        << " sent=" << host->totalSentPackets << " received=" << host->totalReceivedPackets << '\n';
    enet_peer_disconnect_now(peer, 0U);
    return accepted;
}
} // namespace

int main(int argc, char* argv[]) {
    const bool bridge_mode = argc > 1 && std::string_view{argv[1]} == "--bridge";
    battlespades::network::ServerEndpoint endpoint;
    std::string error;
    if (argc > 2 && !battlespades::network::parse_server_endpoint(argv[2], endpoint, error)) {
        std::cerr << error << '\n';
        return 2;
    }
    battlespades::platform::SteamNetworkingRuntimeConfig config;
    config.relay_timeout = std::chrono::seconds{0};
    if (argc > 1 && !bridge_mode) {
        const std::string_view value{argv[1]};
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), config.app_id);
        if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
            (config.app_id != 224540U && config.app_id != 480U)) return 2;
        config.fallback_app_id = 0U;
    }
    battlespades::platform::SteamNetworkingRuntime runtime;
    if (!bridge_mode && !runtime.start(config, error)) {
        std::cerr << "Steam unavailable: " << error << '\n';
        return 1;
    }
    if (!bridge_mode) std::cout << "attached_app=" << runtime.app_id() << '\n';
    if (!bridge_mode) inspect_runtime(runtime);
    const auto web = bridge_mode ? 0U : runtime.begin_web_api_ticket(error);
    if (web == 0U && !bridge_mode) std::cout << "WebAPI unavailable: " << error << '\n';
    const auto session = bridge_mode ? 0U : runtime.begin_session_ticket(error);
    if (session == 0U && !bridge_mode) std::cout << "Retail unavailable: " << error << '\n';
    bool web_done = web == 0U;
    bool session_done = session == 0U;
    bool web_ok = bridge_mode;
    bool session_ok{};
    std::vector<std::byte> session_wire;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{8};
    while ((!web_done || !session_done) && std::chrono::steady_clock::now() < deadline) {
        if (!web_done) {
            if (const auto ticket = runtime.take_web_api_ticket(web)) {
                web_done = true;
                web_ok = ticket->error.empty();
                std::cout << "WebAPI callback=" << (web_ok ? "OK" : "failed")
                          << " hex_bytes=" << ticket->ticket_hex.size() << '\n';
            }
        }
        if (!session_done) {
            if (const auto ticket = runtime.take_session_ticket(session)) {
                session_done = true;
                session_ok = ticket->error.empty();
                if (argc > 2) session_wire = ticket->wire_bytes;
                std::cout << "Retail callback=" << (session_ok ? "OK" : "failed")
                          << " wire_bytes=" << ticket->wire_bytes.size() << '\n';
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    runtime.cancel_auth_ticket(web);
    auto bridge_config = battlespades::platform::default_native_steam_config(
        std::filesystem::absolute(argv[0]).parent_path());
    battlespades::platform::NativeSteamClient bridge{std::move(bridge_config)};
    if (bridge_mode) {
        if (!bridge.start()) {
            std::cout << "Retail bridge unavailable: " << bridge.last_error() << '\n';
        } else if (auto ticket = bridge.session_ticket()) {
            session_wire = std::move(ticket->wire_bytes);
            session_ok = true;
            std::cout << "Retail bridge ticket wire_bytes=" << session_wire.size() << '\n';
            // The historical bridge pumps callbacks itself but lacks a
            // callback completion API; allow Steam to publish this ticket.
            std::this_thread::sleep_for(std::chrono::milliseconds{250});
        } else {
            std::cout << "Retail bridge ticket unavailable: " << bridge.last_error() << '\n';
        }
    }
    bool connection_ok = argc <= 2;
    const bool capture_initial = argc == 5 && std::string_view{argv[3]} == "--stock-capture-initial-info";
    const bool compressed_stock = capture_initial || (argc > 3 && std::string_view{argv[3]} == "--stock");
    const bool stock = compressed_stock || (argc > 3 && std::string_view{argv[3]} == "--stock-uncompressed");
    const bool original = argc > 3 && std::string_view{argv[3]} == "--original-network";
    if (original) {
#if defined(_WIN32)
        connection_ok = argc == 6 && session_ok && original_network(argv[4], argv[5], endpoint, session_wire);
#else
        std::cout << "Original network comparison requires Windows\n";
        connection_ok = false;
#endif
    } else if (argc > 2 && session_ok && stock) {
        connection_ok = stock_handshake(endpoint, std::move(session_wire), compressed_stock,
                                        capture_initial ? std::filesystem::path{argv[4]} : std::filesystem::path{});
    } else if (argc > 2 && session_ok) {
        battlespades::network::EnetProtocol168Config transport;
        transport.host = endpoint.host;
        transport.port = endpoint.port;
        transport.protocol = battlespades::network::GameProtocol::retail168;
        battlespades::network::Protocol168SessionConfig options;
        options.auto_join = false;
        options.steam_ticket = std::move(session_wire);
        if (argc == 5 && std::string_view{argv[3]} == "--maps") options.local_map_directory = argv[4];
        battlespades::network::LiveProtocol168Connection connection;
        if (!connection.start(transport, std::move(options))) return 1;
        const auto connection_deadline = std::chrono::steady_clock::now() + std::chrono::seconds{30};
        auto bootstrap_at = connection_deadline;
        while (std::chrono::steady_clock::now() < connection_deadline) {
            const auto status = connection.status();
            if (!status.error.empty() || status.phase == battlespades::network::LiveProtocol168Phase::disconnected ||
                status.phase == battlespades::network::LiveProtocol168Phase::failed) {
                std::cout << "Server refused: " << status.error << " reason="
                          << status.disconnect_reason.value_or(0U) << " received=" << status.received_datagrams
                          << " sent=" << status.sent_datagrams << '\n';
                break;
            }
            if (connection.take_bootstrap()) {
                bootstrap_at = std::chrono::steady_clock::now();
                std::cout << "Authenticated map bootstrap received; observing without spawning\n";
            }
            static_cast<void>(connection.take_inbound());
            if (std::chrono::steady_clock::now() - bootstrap_at >= std::chrono::seconds{3}) {
                connection_ok = true;
                std::cout << "Server handshake OK received=" << status.received_datagrams
                          << " sent=" << status.sent_datagrams << '\n';
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
        }
        connection.stop();
        if (!connection_ok) std::cout << "Server handshake did not complete\n";
    }
    runtime.cancel_auth_ticket(session);
    if (!web_done || !session_done) std::cout << "Steam ticket callback timed out\n";
    return connection_ok && web_ok && (runtime.app_id() == 480U ? session == 0U : session_ok) ? 0 : 1;
}
