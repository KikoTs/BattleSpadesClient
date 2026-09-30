#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#endif

#include "battlespades/network/server_discovery.hpp"
#include "battlespades/core/build_info.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cctype>
#include <cstring>
#include <limits>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <system_error>
#include <utility>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace battlespades::network {
namespace {

constexpr std::size_t maximum_text_field_bytes{255U};
constexpr std::size_t maximum_lan_payload_bytes{8U * 1024U};

[[nodiscard]] std::string trim(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.remove_prefix(1U);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.remove_suffix(1U);
    }
    return std::string{value};
}

[[nodiscard]] std::string lowercase(std::string value) {
    std::ranges::transform(value, value.begin(), [](char character) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    });
    return value;
}

[[nodiscard]] std::string bounded_string(const nlohmann::json& value,
                                         std::string_view key,
                                         std::string fallback = {}) {
    const auto iterator = value.find(key);
    if (iterator == value.end() || !iterator->is_string()) return fallback;
    auto result = iterator->get<std::string>();
    if (result.size() > maximum_text_field_bytes) result.resize(maximum_text_field_bytes);
    return result;
}

template <typename Integer>
[[nodiscard]] Integer bounded_integer(const nlohmann::json& value,
                                      std::string_view key,
                                      Integer fallback = {}) {
    const auto iterator = value.find(key);
    if (iterator == value.end() || !iterator->is_number_integer()) return fallback;
    const auto number = iterator->get<std::int64_t>();
    if (number < 0 || static_cast<std::uint64_t>(number) >
                          static_cast<std::uint64_t>(std::numeric_limits<Integer>::max())) {
        return fallback;
    }
    return static_cast<Integer>(number);
}

[[nodiscard]] bool boolean_value(const nlohmann::json& value,
                                 std::string_view key,
                                 bool fallback = false) {
    const auto iterator = value.find(key);
    return iterator != value.end() && iterator->is_boolean() ? iterator->get<bool>() : fallback;
}

[[nodiscard]] bool tag_present(const nlohmann::json& value, std::string_view tag) {
    const auto iterator = value.find("tags");
    if (iterator == value.end() || !iterator->is_array()) return false;
    return std::ranges::any_of(*iterator, [tag](const nlohmann::json& candidate) {
        return candidate.is_string() && lowercase(candidate.get<std::string>()) == tag;
    });
}

/**
 * The gameplay ordinal from a listing's `mode=NNNN` tags.
 *
 * Revival servers advertise their MODE_* id there, while the Steam A2S path
 * appends `mode=0001`, the SERVERMODE_PUBLIC browser category. The live
 * master (2026-09-29) derived `mode_tla` from that last tag and labelled
 * every CTF/TDM/TC/VIP/Zombie server "dem". When the tags disagree, the
 * non-category ordinal is the gameplay mode; a real Demolition server
 * advertises only 0001 and keeps its label.
 */
[[nodiscard]] std::optional<std::string> gameplay_mode_from_tags(const nlohmann::json& value) {
    const auto iterator = value.find("tags");
    if (iterator == value.end() || !iterator->is_array()) return std::nullopt;
    constexpr std::array<std::string_view, 13U> codes{
        "nor", "dem", "zom", "mh", "oc", "dia", "tdm", "vip", "ctf", "tc", "tut", "cctf", "ugc"};
    std::size_t tags_seen{};
    std::optional<std::size_t> gameplay;
    for (const auto& candidate : *iterator) {
        if (!candidate.is_string()) continue;
        const auto tag = lowercase(candidate.get<std::string>());
        if (!tag.starts_with("mode=") || tag.size() > 9U) continue;
        std::size_t ordinal{};
        bool digits = tag.size() > 5U;
        for (std::size_t index{5U}; index < tag.size(); ++index) {
            const auto character = tag[index];
            if (character < '0' || character > '9') { digits = false; break; }
            ordinal = ordinal * 10U + static_cast<std::size_t>(character - '0');
        }
        if (!digits || ordinal >= codes.size()) continue;
        ++tags_seen;
        if (ordinal != 1U && !gameplay.has_value()) gameplay = ordinal;
    }
    if (tags_seen < 2U || !gameplay.has_value()) return std::nullopt;
    return std::string{codes[*gameplay]};
}

[[nodiscard]] std::optional<DiscoveredServer> parse_public_entry(const nlohmann::json& value) {
    if (!value.is_object()) return std::nullopt;
    auto host = bounded_string(value, "ip");
    auto port = bounded_integer<std::uint16_t>(value, "port");
    if (host.empty()) {
        ServerEndpoint parsed;
        std::string ignored;
        if (parse_server_endpoint(bounded_string(value, "identifier"), parsed, ignored)) {
            host = std::move(parsed.host);
            port = parsed.port;
        }
    }
    if (host.empty() || port == 0U) return std::nullopt;

    DiscoveredServer result;
    result.game = {std::move(host), port};
    result.query_port = bounded_integer<std::uint16_t>(value, "queryPort", port);
    result.ping_milliseconds = bounded_integer<std::uint16_t>(value, "ping", 65'000U);
    result.name = bounded_string(value, "name", result.game.identifier());
    result.map = bounded_string(value, "map", "Unknown");
    result.mode_code = lowercase(bounded_string(
        value, "mode_tla", bounded_string(value, "game_mode", "tdm")));
    if (auto tagged = gameplay_mode_from_tags(value); tagged.has_value()) {
        result.mode_code = std::move(*tagged);
    }
    result.region = lowercase(bounded_string(value, "region", "europe"));
    result.texture_skin = bounded_string(value, "texture_skin");
    result.master_identifier =
        bounded_string(value, "identifier", result.game.identifier());
    result.players = bounded_integer<std::uint16_t>(
        value, "players", bounded_integer<std::uint16_t>(value, "count"));
    result.maximum_players = bounded_integer<std::uint16_t>(
        value, "max_players", bounded_integer<std::uint16_t>(value, "max", 32U));
    result.classic = boolean_value(value, "classic") || result.mode_code == "cctf" ||
                     tag_present(value, "classic");
    result.official = boolean_value(value, "official");
    result.identity_ticket = tag_present(value, "identity=ticket-v1");
    // The server's master heartbeat carries the tag `password`; its A2S_INFO
    // visibility byte says the same (1) to anything that speaks A2S.
    result.password_protected =
        tag_present(value, "password") || boolean_value(value, "password") ||
        bounded_integer<std::uint16_t>(value, "visibility") == 1U;
    // The master sends this only for a player-hosted match that had a Steam
    // session; a listing without it stays reachable through the relay alone.
    if (const auto steam = bounded_string(value, "steam_host_id"); !steam.empty()) {
        try {
            result.steam_host_id = std::stoull(steam);
        } catch (const std::exception&) {
            result.steam_host_id = 0U;
        }
    }
    // `players` counts bots, so a bot-filled server reads as full. Keep the
    // human figure when the listing separates them.
    result.human_players = bounded_integer<std::uint16_t>(value, "human_players", result.players);
    return result;
}

struct CurlBuffer final {
    std::string bytes;
    std::size_t limit{};
    bool overflow{};
};

int curl_cancel(void* context,
                curl_off_t,
                curl_off_t,
                curl_off_t,
                curl_off_t) noexcept {
    const auto* stop = static_cast<const std::stop_token*>(context);
    return stop != nullptr && stop->stop_requested() ? 1 : 0;
}

std::size_t curl_write(char* data, std::size_t size, std::size_t count, void* context) {
    auto& buffer = *static_cast<CurlBuffer*>(context);
    if (size != 0U && count > std::numeric_limits<std::size_t>::max() / size) {
        buffer.overflow = true;
        return 0U;
    }
    const auto bytes = size * count;
    if (bytes > buffer.limit - std::min(buffer.limit, buffer.bytes.size())) {
        buffer.overflow = true;
        return 0U;
    }
    buffer.bytes.append(data, bytes);
    return bytes;
}

class SocketRuntime final {
public:
    SocketRuntime() {
#if defined(_WIN32)
        WSADATA data{};
        ready_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
        ready_ = true;
#endif
    }
    ~SocketRuntime() {
#if defined(_WIN32)
        if (ready_) WSACleanup();
#endif
    }
    [[nodiscard]] bool ready() const noexcept { return ready_; }

private:
    bool ready_{};
};

#if defined(_WIN32)
using NativeSocket = SOCKET;
constexpr NativeSocket invalid_socket{INVALID_SOCKET};
void close_socket(NativeSocket value) { closesocket(value); }
using SocketLength = int;
#else
using NativeSocket = int;
constexpr NativeSocket invalid_socket{-1};
void close_socket(NativeSocket value) { close(value); }
using SocketLength = socklen_t;
#endif

class SocketGuard final {
public:
    explicit SocketGuard(NativeSocket value) : value_{value} {}
    ~SocketGuard() {
        if (value_ != invalid_socket) close_socket(value_);
    }
    SocketGuard(const SocketGuard&) = delete;
    SocketGuard& operator=(const SocketGuard&) = delete;
    [[nodiscard]] NativeSocket get() const noexcept { return value_; }

private:
    NativeSocket value_{invalid_socket};
};

[[nodiscard]] bool resolve_ipv4(const ServerEndpoint& endpoint, sockaddr_in& address) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* result{};
    const auto service = std::to_string(endpoint.port);
    if (getaddrinfo(endpoint.host.c_str(), service.c_str(), &hints, &result) != 0 ||
        result == nullptr) {
        return false;
    }
    std::memcpy(&address, result->ai_addr, sizeof(address));
    freeaddrinfo(result);
    return true;
}

[[nodiscard]] std::string numeric_host(const sockaddr_in& address) {
    std::array<char, INET_ADDRSTRLEN> buffer{};
    return inet_ntop(AF_INET, &address.sin_addr, buffer.data(),
                     static_cast<socklen_t>(buffer.size())) == nullptr
               ? std::string{}
               : std::string{buffer.data()};
}

[[nodiscard]] DiscoveryResult query_lan(std::span<const ServerEndpoint> endpoints,
                                        std::chrono::milliseconds timeout,
                                        std::size_t maximum_servers,
                                        bool broadcast) {
    DiscoveryResult output;
    if (endpoints.empty() || timeout.count() <= 0 || maximum_servers == 0U) {
        output.error = "invalid LAN discovery request";
        return output;
    }
    SocketRuntime runtime;
    if (!runtime.ready()) {
        output.error = "socket runtime initialization failed";
        return output;
    }
    SocketGuard socket{::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)};
    if (socket.get() == invalid_socket) {
        output.error = "cannot create LAN discovery socket";
        return output;
    }
    if (broadcast) {
        int enabled{1};
        static_cast<void>(setsockopt(socket.get(), SOL_SOCKET, SO_BROADCAST,
                                    reinterpret_cast<const char*>(&enabled), sizeof(enabled)));
    }
    constexpr std::array<char, 8U> query{'H', 'E', 'L', 'L', 'O', 'L', 'A', 'N'};
    const auto started = std::chrono::steady_clock::now();
    for (const auto& endpoint : endpoints) {
        sockaddr_in target{};
        if (!resolve_ipv4(endpoint, target)) continue;
        static_cast<void>(sendto(socket.get(), query.data(), static_cast<int>(query.size()), 0,
                                 reinterpret_cast<const sockaddr*>(&target), sizeof(target)));
    }

    std::set<std::string, std::less<>> seen;
    const auto deadline = started + timeout;
    while (std::chrono::steady_clock::now() < deadline && output.servers.size() < maximum_servers) {
        const auto remaining = std::chrono::duration_cast<std::chrono::microseconds>(
            deadline - std::chrono::steady_clock::now());
        timeval wait{};
        const auto remaining_microseconds = std::max<std::int64_t>(0, remaining.count());
        const auto remaining_seconds = remaining_microseconds / 1'000'000;
        using TimevalSeconds = decltype(wait.tv_sec);
        wait.tv_sec = static_cast<TimevalSeconds>(std::min<std::int64_t>(
            remaining_seconds,
            static_cast<std::int64_t>(std::numeric_limits<TimevalSeconds>::max())));
        // The modulo is always in [0, 999999], including on platforms where
        // suseconds_t is a 32-bit integer rather than a long.
        wait.tv_usec = static_cast<decltype(wait.tv_usec)>(
            remaining_microseconds % 1'000'000);
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(socket.get(), &readable);
        const auto selected = select(static_cast<int>(socket.get() + 1), &readable, nullptr,
                                     nullptr, &wait);
        if (selected <= 0) break;

        std::array<char, maximum_lan_payload_bytes + 1U> bytes{};
        sockaddr_in source{};
        SocketLength source_length{sizeof(source)};
        const auto count = recvfrom(socket.get(), bytes.data(),
                                    static_cast<int>(maximum_lan_payload_bytes), 0,
                                    reinterpret_cast<sockaddr*>(&source), &source_length);
        if (count <= 0) continue;
        const auto host = numeric_host(source);
        const auto port = ntohs(source.sin_port);
        const auto key = host + ':' + std::to_string(port);
        if (host.empty() || !seen.insert(key).second) continue;
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started);
        const auto ping = static_cast<std::uint16_t>(std::clamp<std::int64_t>(
            elapsed.count(), 0, std::numeric_limits<std::uint16_t>::max()));
        auto parsed = parse_lan_server_response(
            std::string_view{bytes.data(), static_cast<std::size_t>(count)},
            ServerEndpoint{host, port}, ping);
        if (parsed && !parsed.servers.empty()) {
            auto server = std::move(parsed.servers.front());
            // A server on this machine receives both the explicit loopback
            // probe and the broadcast probe. Retail shows that host once, so
            // coalesce only aliases involving loopback; two remote machines
            // are still allowed to share a name, map, mode, and port.
            const auto alias = std::ranges::find_if(
                output.servers, [&server](const DiscoveredServer& existing) {
                    const auto has_loopback = existing.game.host == "127.0.0.1" ||
                                              server.game.host == "127.0.0.1";
                    return has_loopback && existing.game.port == server.game.port &&
                           existing.name == server.name && existing.map == server.map &&
                           existing.mode_code == server.mode_code;
                });
            if (alias == output.servers.end()) {
                output.servers.push_back(std::move(server));
            } else if (server.game.host == "127.0.0.1") {
                *alias = std::move(server);
            }
        }
    }
    return output;
}

} // namespace

std::string ServerEndpoint::identifier() const {
    return "aos://" + host + ':' + std::to_string(port);
}

bool parse_server_endpoint(std::string_view text,
                           ServerEndpoint& endpoint,
                           std::string& error,
                           std::uint16_t default_port) {
    error.clear();
    auto input = trim(text);
    if (input == "local") input = "127.0.0.1:" + std::to_string(default_port);
    constexpr std::string_view scheme{"aos://"};
    if (input.size() >= scheme.size() &&
        lowercase(input.substr(0U, scheme.size())) == scheme) {
        input.erase(0U, scheme.size());
    }
    if (input.empty() || input.size() > maximum_text_field_bytes ||
        input.find_first_of("/\\?#@ \t\r\n") != std::string::npos) {
        error = "Enter a host or IPv4 address, optionally followed by :port";
        return false;
    }
    if (input.front() == '[' || input.find(']') != std::string::npos) {
        error = "Protocol 168 currently accepts IPv4/DNS endpoints only";
        return false;
    }

    auto host = input;
    auto port = default_port;
    const auto colon = input.rfind(':');
    if (colon != std::string::npos) {
        if (input.find(':') != colon) {
            error = "IPv6 endpoints are not supported by the Protocol 168 transport";
            return false;
        }
        host = input.substr(0U, colon);
        const auto port_text = std::string_view{input}.substr(colon + 1U);
        unsigned parsed{};
        const auto conversion = std::from_chars(port_text.data(),
                                                port_text.data() + port_text.size(), parsed);
        if (port_text.empty() || conversion.ec != std::errc{} ||
            conversion.ptr != port_text.data() + port_text.size() || parsed == 0U ||
            parsed > std::numeric_limits<std::uint16_t>::max()) {
            error = "Port must be between 1 and 65535";
            return false;
        }
        port = static_cast<std::uint16_t>(parsed);
    }
    if (host.empty() || host.front() == '.' || host.back() == '.') {
        error = "Host cannot be empty";
        return false;
    }
    const auto valid_host_character = [](char character) {
        const auto byte = static_cast<unsigned char>(character);
        return std::isalnum(byte) != 0 || character == '.' || character == '-';
    };
    if (!std::ranges::all_of(host, valid_host_character)) {
        error = "Host contains an unsupported character";
        return false;
    }
    endpoint = ServerEndpoint{std::move(host), port};
    return true;
}

DiscoveryResult parse_public_server_list(std::string_view json,
                                         std::size_t maximum_servers) {
    DiscoveryResult output;
    if (json.empty() || maximum_servers == 0U) {
        output.error = "server list response is empty";
        return output;
    }
    try {
        const auto document = nlohmann::json::parse(json.begin(), json.end());
        const nlohmann::json* rows = &document;
        if (document.is_object()) {
            const auto value = document.find("value");
            if (value != document.end()) rows = &*value;
        }
        if (!rows->is_array()) {
            output.error = "server list response must be a JSON array";
            return output;
        }
        std::set<std::string, std::less<>> seen;
        for (const auto& value : *rows) {
            if (output.servers.size() >= maximum_servers) break;
            auto server = parse_public_entry(value);
            if (!server.has_value() || !seen.insert(server->game.identifier()).second) continue;
            output.servers.push_back(std::move(*server));
        }
    } catch (const nlohmann::json::exception& exception) {
        output.error = std::string{"invalid server list JSON: "} + exception.what();
    }
    return output;
}

DiscoveryResult parse_lan_server_response(std::string_view json,
                                          const ServerEndpoint& source,
                                          std::uint16_t ping_milliseconds) {
    DiscoveryResult output;
    try {
        const auto value = nlohmann::json::parse(json.begin(), json.end());
        if (!value.is_object()) {
            output.error = "LAN response must be a JSON object";
            return output;
        }
        DiscoveredServer server;
        server.game = source;
        server.query_port = source.port;
        server.ping_milliseconds = ping_milliseconds;
        server.name = bounded_string(value, "name", source.identifier());
        server.map = bounded_string(value, "map", "Unknown");
        server.mode_code = lowercase(bounded_string(value, "game_mode", "tdm"));
        server.players = bounded_integer<std::uint16_t>(value, "players_current");
        server.maximum_players = bounded_integer<std::uint16_t>(value, "players_max", 32U);
        server.classic = server.mode_code == "cctf";
        server.local = true;
        output.servers.push_back(std::move(server));
    } catch (const nlohmann::json::exception& exception) {
        output.error = std::string{"invalid LAN response JSON: "} + exception.what();
    }
    return output;
}

std::optional<DiscoveredServer> find_discovered_server(
    std::span<const DiscoveredServer> servers,
    std::string_view identifier) {
    if (identifier.empty()) return std::nullopt;
    const auto found = std::ranges::find_if(servers, [identifier](const DiscoveredServer& server) {
        return server.master_identifier == identifier ||
               server.game.identifier() == identifier;
    });
    return found == servers.end() ? std::nullopt : std::optional{*found};
}

DiscoveryResult select_discovered_servers(
    DiscoveryResult source,
    std::span<const std::string> identifiers) {
    std::erase_if(source.servers, [&](const DiscoveredServer& server) {
        return std::ranges::none_of(identifiers, [&](const std::string& identifier) {
            return server.master_identifier == identifier ||
                   server.game.identifier() == identifier;
        });
    });
    return source;
}

DiscoveryResult discover_public_servers(const PublicDiscoveryConfig& config,
                                        std::stop_token stop) {
    DiscoveryResult output;
    if (config.url.empty() || config.timeout.count() <= 0 ||
        config.maximum_payload_bytes == 0U || config.maximum_servers == 0U) {
        output.error = "invalid public discovery configuration";
        return output;
    }
    static std::once_flag curl_once;
    static CURLcode curl_initialization{CURLE_FAILED_INIT};
    std::call_once(curl_once, [] { curl_initialization = curl_global_init(CURL_GLOBAL_DEFAULT); });
    if (curl_initialization != CURLE_OK) {
        output.error = "curl global initialization failed";
        return output;
    }
    auto* handle = curl_easy_init();
    if (handle == nullptr) {
        output.error = "cannot create public discovery request";
        return output;
    }
    struct CurlGuard final {
        CURL* value{};
        ~CurlGuard() { curl_easy_cleanup(value); }
    } guard{handle};
    CurlBuffer buffer{{}, config.maximum_payload_bytes, false};
    curl_easy_setopt(handle, CURLOPT_URL, config.url.c_str());
    // The public list is metadata, but it still controls endpoints rendered
    // by the client. Never allow configuration or redirects to downgrade the
    // request to an unencrypted transport.
    curl_easy_setopt(handle, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(handle, CURLOPT_REDIR_PROTOCOLS_STR, "https");
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(handle, CURLOPT_MAXREDIRS, 2L);
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(config.timeout.count()));
    curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS, static_cast<long>(config.timeout.count()));
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(handle, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(handle, CURLOPT_USERAGENT, "BattleSpadesClient/" AOS_VERSION_STRING " Protocol168/1");
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, &curl_write);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, &curl_cancel);
    curl_easy_setopt(handle, CURLOPT_XFERINFODATA, &stop);
    const auto performed = curl_easy_perform(handle);
    long status{};
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
    if (performed != CURLE_OK) {
        if (stop.stop_requested()) {
            output.error = "public server list request cancelled";
            return output;
        }
        output.error = buffer.overflow ? "public server list exceeded its size limit"
                                       : std::string{"public server list request failed: "} +
                                             curl_easy_strerror(performed);
        return output;
    }
    if (status != 200L) {
        output.error = "public server list returned HTTP " + std::to_string(status);
        return output;
    }
    return parse_public_server_list(buffer.bytes, config.maximum_servers);
}

DiscoveryResult discover_lan_servers(const LanDiscoveryConfig& config) {
    std::vector<ServerEndpoint> endpoints;
    const auto count = std::min<std::size_t>(config.ports.size(), 64U);
    endpoints.reserve(count * (config.include_broadcast ? 2U : 1U));
    for (std::size_t index{}; index < count; ++index) {
        if (config.ports[index] == 0U) continue;
        endpoints.push_back(ServerEndpoint{"127.0.0.1", config.ports[index]});
        if (config.include_broadcast) {
            endpoints.push_back(ServerEndpoint{"255.255.255.255", config.ports[index]});
        }
    }
    return query_lan(endpoints, config.timeout, config.maximum_servers,
                     config.include_broadcast);
}

DiscoveryResult probe_lan_server(const ServerEndpoint& endpoint,
                                 std::chrono::milliseconds timeout) {
    const std::array endpoints{endpoint};
    return query_lan(endpoints, timeout, 1U, false);
}

} // namespace battlespades::network
