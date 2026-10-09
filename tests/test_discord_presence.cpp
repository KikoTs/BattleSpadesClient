#include "battlespades/platform/discord_presence.hpp"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace {
using namespace std::chrono_literals;
using namespace battlespades::platform;
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;

void expect(bool value, const char* message) { if (!value) throw std::runtime_error{message}; }
template<class Predicate> bool eventually(Predicate predicate) {
    const auto deadline = Clock::now() + 3s;
    do { if (predicate()) return true; std::this_thread::sleep_for(5ms); } while (Clock::now() < deadline);
    return false;
}
std::string wire(std::uint32_t opcode, std::string_view payload) {
    std::string result(8U, '\0');
    for (unsigned i = 0U; i < 4U; ++i) {
        result[i] = static_cast<char>((opcode >> (8U * i)) & 255U);
        result[i + 4U] = static_cast<char>((payload.size() >> (8U * i)) & 255U);
    }
    result.append(payload);
    return result;
}
std::uint32_t u32(const char* bytes) {
    std::uint32_t result{};
    for (unsigned i = 0U; i < 4U; ++i) result |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[i])) << (8U * i);
    return result;
}

// Isolated real IPC peer: never touches the player's Discord or publishes test
// presence. Exercises partial frames, transport shutdown and callback delivery.
class Peer final {
public:
    std::string path;
    Peer() {
#ifdef _WIN32
        path = "\\\\.\\pipe\\battlespades-discord-test-" + std::to_string(GetCurrentProcessId());
        pipe_ = CreateNamedPipeA(path.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1U, 65536U, 65536U, 0U, nullptr);
        expect(pipe_ != INVALID_HANDLE_VALUE, "Create isolated test pipe");
#else
        path = "/tmp/battlespades-discord-test-" + std::to_string(getpid());
        listener_ = socket(AF_UNIX, SOCK_STREAM, 0);
        expect(listener_ >= 0, "Create isolated test socket");
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        std::memcpy(address.sun_path, path.c_str(), path.size() + 1U);
        expect(bind(listener_, reinterpret_cast<const sockaddr*>(&address), static_cast<socklen_t>(sizeof(address))) == 0 && listen(listener_, 1) == 0,
            "Bind isolated test socket");
#endif
    }
    ~Peer() {
#ifdef _WIN32
        if (pipe_ != INVALID_HANDLE_VALUE) CloseHandle(pipe_);
#else
        if (socket_ >= 0) close(socket_);
        if (listener_ >= 0) close(listener_);
        unlink(path.c_str());
#endif
    }
    void accept_client() {
#ifdef _WIN32
        OVERLAPPED operation{};
        operation.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        const bool connected = ConnectNamedPipe(pipe_, &operation) != FALSE;
        const auto error = connected ? ERROR_SUCCESS : GetLastError();
        bool ok = connected || error == ERROR_PIPE_CONNECTED;
        if (error == ERROR_IO_PENDING) {
            ok = WaitForSingleObject(operation.hEvent, 3000U) == WAIT_OBJECT_0;
            if (!ok) CancelIoEx(pipe_, &operation);
            DWORD transferred{};
            static_cast<void>(GetOverlappedResult(pipe_, &operation, &transferred, TRUE));
        }
        CloseHandle(operation.hEvent);
        expect(ok, "Client connects without blocking main thread");
#else
        pollfd descriptor{listener_, POLLIN, 0};
        expect(poll(&descriptor, 1U, 3000) > 0, "Client connects without blocking main thread");
        socket_ = accept(listener_, nullptr, nullptr);
        expect(socket_ >= 0, "Accept client");
#endif
    }
    void send_bytes(std::string_view value) {
#ifdef _WIN32
        expect(transfer(false, const_cast<char*>(value.data()), value.size()) == static_cast<int>(value.size()), "Write fake Discord response");
#else
        expect(::send(socket_, value.data(), value.size(), 0) == static_cast<ssize_t>(value.size()), "Write fake Discord response");
#endif
    }
    void send(const Json& value) { send_bytes(wire(1U, value.dump())); }
    std::optional<std::pair<std::uint32_t, std::string>> receive(std::chrono::milliseconds timeout = 3000ms) {
        const auto deadline = Clock::now() + timeout;
        do {
            if (input_.size() >= 8U) {
                const auto length = u32(input_.data() + 4U);
                expect(length <= 65536U, "Client frames stay bounded");
                if (input_.size() >= 8U + length) {
                    auto result = std::pair{u32(input_.data()), input_.substr(8U, length)};
                    input_.erase(0U, 8U + length);
                    return result;
                }
            }
            std::array<char, 8192U> data{};
            int count{};
#ifdef _WIN32
            DWORD available{};
            if (!PeekNamedPipe(pipe_, nullptr, 0U, nullptr, &available, nullptr)) return std::nullopt;
            if (available > 0U) count = transfer(true, data.data(), std::min(data.size(), static_cast<std::size_t>(available)));
#else
            pollfd descriptor{socket_, POLLIN, 0};
            if (poll(&descriptor, 1U, 0) > 0) count = static_cast<int>(recv(socket_, data.data(), data.size(), 0));
#endif
            if (count > 0) input_.append(data.data(), static_cast<std::size_t>(count));
            else std::this_thread::sleep_for(2ms);
        } while (Clock::now() < deadline);
        return std::nullopt;
    }
    Json command() {
        const auto message = receive();
        expect(message && message->first == 1U, "Receive RPC command");
        return Json::parse(message->second);
    }
private:
    std::string input_;
#ifdef _WIN32
    HANDLE pipe_{INVALID_HANDLE_VALUE};
    int transfer(bool reading, char* data, std::size_t size) {
        OVERLAPPED operation{};
        operation.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        DWORD transferred{};
        const bool ok = (reading ? ReadFile(pipe_, data, static_cast<DWORD>(size), &transferred, &operation)
                                : WriteFile(pipe_, data, static_cast<DWORD>(size), &transferred, &operation)) != FALSE;
        bool completed = ok;
        if (!ok && GetLastError() == ERROR_IO_PENDING) {
            completed = WaitForSingleObject(operation.hEvent, 3000U) == WAIT_OBJECT_0;
            if (!completed) CancelIoEx(pipe_, &operation);
            completed = GetOverlappedResult(pipe_, &operation, &transferred, TRUE) != FALSE && completed;
        }
        CloseHandle(operation.hEvent);
        return completed ? static_cast<int>(transferred) : -1;
    }
#else
    int listener_{-1};
    int socket_{-1};
#endif
};

void test_validation() {
    expect(valid_discord_application_id("123456789012345678"), "Accept configured public application ID");
    for (const auto value : {"", "123", "123456789012345678x", "18446744073709551616", "0123456789012345678"}) {
        expect(!valid_discord_application_id(value), "Reject invalid IDs without connecting");
    }
    expect(discord_join_url("152.117.81.19:30002", 3) == "aosbb://152.117.81.19:30002:0.75", "Preserve classic protocol");
    expect(discord_join_url("94.213.171.38:32887", 4) == "aosbb://94.213.171.38:32887:0.76", "Preserve 0.76 protocol");
    expect(parse_discord_join_secret("aosbb://8.8.8.8:27015") == "aosbb://8.8.8.8:27015", "Retail round trip");
    for (const auto endpoint : {"127.0.0.1:1", "10.0.0.1:1", "192.168.1.1:1", "172.16.0.1:1", "172.31.255.255:1",
        "169.254.1.1:1", "100.64.0.1:1", "198.18.0.1:1", "192.0.2.1:1", "203.0.113.1:1", "0.0.0.0:1", "224.0.0.1:1",
        "255.255.255.255:1", "8.8.8.8:0", "8.8.8.8:65536", "8.8.8.8:00080", "08.8.8.8:80", "8.8.8.8:80?password=x",
        "example.com:80", "steam:123456789", "[::1]:80", "8.8.8.8:80:0.75", "8.8.8:80", ":80"}) {
        expect(!discord_join_url(endpoint, 168), "Never publish private or ambiguous targets or credentials");
    }
    for (const auto secret : {"aos://8.8.8.8:80", "aosbb://127.0.0.1:80", "aosbb://8.8.8.8:80 --foo", "aosbb://8.8.8.8:80:168",
        "aosbb://8.8.8.8:80:0.77", "aosbb://8.8.8.8:80?ticket=secret", "https://example.com"}) {
        expect(!parse_discord_join_secret(secret), "Only canonical public game addresses are accepted from Discord");
    }
    expect(!discord_join_url("8.8.8.8:80", 5), "Reject unknown protocol");
}

void test_transport() {
    Peer peer;
    DiscordPresence presence{peer.path};
    presence.configure({.application_id = "123456789012345678"});
    DiscordPresenceActivity activity{.server_name = "Test Server", .map = "Hallway", .mode = "Classic 0.75 / babel",
        .endpoint = "152.117.81.19:30002", .protocol = 3, .players = 4, .maximum_players = 32, .started_at = 1'700'000'000};
    presence.update(activity);
    peer.accept_client();
    const auto handshake = peer.receive();
    expect(handshake && handshake->first == 0U && Json::parse(handshake->second).at("client_id") == "123456789012345678", "Handshake carries configured ID only");
    const auto ready = wire(1U, R"({"cmd":"DISPATCH","evt":"READY","data":{}})");
    peer.send_bytes(ready.substr(0U, 3U));
    peer.send_bytes(ready.substr(3U));
    expect(peer.command().at("evt") == "ACTIVITY_JOIN", "Subscribe to native Join Game");
    auto command = peer.command();
    const auto& value = command.at("args").at("activity");
    expect(value.at("details") == "Test Server" && value.at("state") == "Classic 0.75 / babel | Hallway", "Presence carries server/map/mode");
    expect(value.at("party").at("size") == Json::array({4, 32}), "Presence carries player counts");
    expect(value.at("secrets").at("join") == "aosbb://152.117.81.19:30002:0.75", "Join secret has explicit protocol");
    expect(value.at("buttons").at(0).at("url") == "https://www.aosplay.net/join?server=152.117.81.19%3A30002&protocol=0.75", "HTTPS button opens safe landing route");
    peer.send({{"cmd", "SET_ACTIVITY"}, {"nonce", command.at("nonce")}, {"data", Json::object()}});
    expect(eventually([&] { return presence.activity_acknowledgement() == DiscordActivityAcknowledgement::published; }),
           "Published state requires Discord's matching acknowledgement");
    activity.players = 5;
    presence.update(activity);
    expect(!peer.receive(250ms), "Rapid count changes coalesce instead of flooding Discord");
    peer.send_bytes(wire(3U, "ping"));
    const auto pong = peer.receive();
    expect(pong && pong->first == 4U && pong->second == "ping", "Heartbeat payload is echoed");
    peer.send({{"cmd", "DISPATCH"}, {"evt", "ACTIVITY_JOIN"}, {"data", {{"secret", "aosbb://127.0.0.1:1234"}}}});
    std::this_thread::sleep_for(150ms);
    expect(!presence.poll_join_request(), "Private join event is ignored");
    const Json join{{"cmd", "DISPATCH"}, {"evt", "ACTIVITY_JOIN"}, {"data", {{"secret", "aosbb://94.213.171.38:32887:0.76"}}}};
    peer.send(join);
    std::optional<std::string> request;
    expect(eventually([&] { request = presence.poll_join_request(); return request.has_value(); }), "Validated join is delivered to main thread");
    expect(request == "aosbb://94.213.171.38:32887:0.76", "Join request preserves exact validated target");
    peer.send(join);
    std::this_thread::sleep_for(150ms);
    expect(!presence.poll_join_request(), "Duplicate join cannot repeatedly reconnect a player");
    activity.endpoint = "192.168.1.2:32887";
    activity.server_name = "192.168.1.2 private machine";
    presence.update(activity);
    command = peer.command();
    const auto private_value = command.at("args").at("activity");
    expect(private_value.at("details") == "Private server" && !private_value.contains("secrets") && !private_value.contains("buttons"), "Private transition immediately revokes join and hides address");
    expect(private_value.dump().find("192.168") == std::string::npos, "Private party IDs do not leak an address");
    activity.endpoint = "8.8.8.8:80";
    activity.password_protected = true;
    presence.update(activity);
    command = peer.command();
    expect(!command.at("args").at("activity").contains("secrets"), "Password servers never expose join credentials");
    presence.clear();
    command = peer.command();
    expect(command.at("args").at("activity").is_null(), "Disconnect clears presence despite rate limit");
    peer.send({{"cmd", "SET_ACTIVITY"}, {"nonce", command.at("nonce")}, {"data", nullptr}});
    expect(eventually([&] { return presence.activity_acknowledgement() == DiscordActivityAcknowledgement::cleared; }),
           "Clear proof requires Discord's matching acknowledgement");
    // Malformed lengths must close transport without allocating their claim.
    peer.send_bytes(std::string{"\x01\x00\x00\x00\xff\xff\xff\x7f", 8U});
    expect(eventually([&] { return presence.status() == DiscordPresenceStatus::waiting_for_discord; }), "Oversized Discord frame is rejected");
    presence.configure({.enabled = false, .application_id = "123456789012345678"});
    expect(eventually([&] { return presence.status() == DiscordPresenceStatus::disabled; }), "Setting disables background transport");
}
}

int main() {
    try {
        test_validation();
        test_transport();
        DiscordPresence unconfigured{"unused-discord-test-endpoint"};
        unconfigured.configure({.application_id = "invalid"});
        expect(eventually([&] { return unconfigured.status() == DiscordPresenceStatus::unconfigured; }), "Missing ID never tries a fabricated application");
        std::cout << "Discord presence validation and IPC integration tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
