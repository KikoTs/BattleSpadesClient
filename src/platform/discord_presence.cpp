#include "battlespades/platform/discord_presence.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#endif

#ifndef AOS_DISCORD_APPLICATION_ID
#define AOS_DISCORD_APPLICATION_ID "1557497536602701824"
#endif

namespace battlespades::platform {
namespace {
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
constexpr std::size_t maximum_frame = 64U * 1024U;

std::string environment(const char* name) {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t size{};
    if (_dupenv_s(&value, &size, name) != 0 || value == nullptr) return {};
    std::string result{value};
    std::free(value);
    return result;
#else
    const auto* value = std::getenv(name);
    return value == nullptr ? std::string{} : std::string{value};
#endif
}

std::string bounded_text(std::string_view value, std::size_t limit = 128U) {
    std::string result;
    result.reserve(std::min(value.size(), limit));
    for (const char byte : value) {
        const auto ch = static_cast<unsigned char>(byte);
        if (result.size() >= limit) break;
        result.push_back(ch < 32U || ch == 127U ? ' ' : static_cast<char>(ch));
    }
    // Do not cut the final UTF-8 codepoint. Invalid source bytes are replaced
    // by the JSON serializer rather than throwing on the render thread.
    if (value.size() > result.size()) {
        while (!result.empty() && (static_cast<unsigned char>(value[result.size()]) & 0xc0U) == 0x80U) {
            result.pop_back();
        }
    }
    return result;
}

bool public_endpoint(std::string_view endpoint) {
    const auto colon = endpoint.find(':');
    if (colon == std::string_view::npos || endpoint.size() > 21U) return false;
    const auto host = endpoint.substr(0U, colon);
    const auto port = endpoint.substr(colon + 1U);
    unsigned port_value{};
    const auto parsed_port = std::from_chars(port.data(), port.data() + port.size(), port_value);
    if (port.empty() || parsed_port.ec != std::errc{} || parsed_port.ptr != port.data() + port.size() ||
        port_value < 1U || port_value > 65535U || std::to_string(port_value) != port) return false;
    std::array<unsigned, 4U> octets{};
    std::size_t offset{};
    for (std::size_t index = 0U; index < octets.size(); ++index) {
        const auto dot = host.find('.', offset);
        const auto end = dot == std::string_view::npos ? host.size() : dot;
        const auto part = host.substr(offset, end - offset);
        const auto parsed = std::from_chars(part.data(), part.data() + part.size(), octets[index]);
        if (part.empty() || part.size() > 3U || parsed.ec != std::errc{} || parsed.ptr != part.data() + part.size() ||
            octets[index] > 255U || std::to_string(octets[index]) != part ||
            (index < 3U) != (dot != std::string_view::npos)) return false;
        offset = end + 1U;
    }
    const auto [a, b, c, d] = octets;
    static_cast<void>(d);
    return !(a == 0U || a == 10U || a == 127U || a >= 224U ||
        (a == 100U && b >= 64U && b <= 127U) || (a == 169U && b == 254U) ||
        (a == 172U && b >= 16U && b <= 31U) || (a == 192U && b == 168U) ||
        (a == 192U && b == 0U && (c == 0U || c == 2U)) ||
        (a == 192U && b == 88U && c == 99U) || (a == 198U && (b == 18U || b == 19U)) ||
        (a == 198U && b == 51U && c == 100U) || (a == 203U && b == 0U && c == 113U));
}

std::uint32_t process_id() {
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return static_cast<std::uint32_t>(getpid());
#endif
}

Json activity_json(const DiscordPresenceActivity& activity, bool allow_join) {
    const auto join = discord_join_url(activity.endpoint, activity.protocol);
    const bool publish_join = allow_join && !activity.password_protected && join.has_value();
    auto details = join ? activity.server_name : std::string{"Private server"};
    if (details.empty()) details = "In a server";
    auto state = activity.mode;
    if (!activity.map.empty()) state += (state.empty() ? "" : " | ") + activity.map;
    if (state.empty()) state = "Playing BattleSpades";
    const bool known_capacity = activity.maximum_players > 0 && activity.maximum_players <= 256 &&
        activity.players >= 0 && activity.players <= activity.maximum_players;
    if (!known_capacity && activity.players >= 0 && activity.players <= 256) {
        state += " | " + std::to_string(activity.players) + " players";
    }
    Json value{{"type", 0}, {"details", bounded_text(details)}, {"state", bounded_text(state)}, {"instance", true}};
    if (activity.started_at > 0) value["timestamps"] = {{"start", activity.started_at}};
    if (known_capacity) {
        value["party"] = {{"id", publish_join ? *join : "battlespades-" + std::to_string(process_id())},
                          {"size", {activity.players, activity.maximum_players}}};
    }
    if (publish_join) {
        if (!value.contains("party")) value["party"] = {{"id", *join}};
        value["secrets"] = {{"join", *join}};
        // Discord buttons use HTTPS. The landing page validates this address
        // again and offers an explicit aosbb link, also when the game is closed.
        std::string encoded = activity.endpoint;
        encoded.replace(encoded.find(':'), 1U, "%3A");
        const auto version = activity.protocol == 3 ? "0.75" : activity.protocol == 4 ? "0.76" : "168";
        value["buttons"] = Json::array({{{"label", "Join server"},
            {"url", "https://www.aosplay.net/join?server=" + encoded + "&protocol=" + version}}});
    }
    return value;
}

std::string dump(const Json& value) {
    return value.dump(-1, ' ', false, Json::error_handler_t::replace);
}

std::string frame(std::uint32_t opcode, std::string_view payload) {
    std::string result(8U, '\0');
    for (std::size_t i = 0U; i < 4U; ++i) {
        result[i] = static_cast<char>((opcode >> (i * 8U)) & 255U);
        result[i + 4U] = static_cast<char>((payload.size() >> (i * 8U)) & 255U);
    }
    result.append(payload);
    return result;
}

std::uint32_t little_u32(const char* bytes) {
    std::uint32_t result{};
    for (unsigned i = 0U; i < 4U; ++i) result |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[i])) << (i * 8U);
    return result;
}

// A local pipe is still untrusted input. Bound nesting before JSON allocation.
bool shallow_json(std::string_view bytes) {
    unsigned depth{};
    bool quoted = false;
    bool escaped = false;
    for (const auto ch : bytes) {
        if (quoted) {
            if (escaped) escaped = false;
            else if (ch == '\\') escaped = true;
            else if (ch == '"') quoted = false;
        } else if (ch == '"') quoted = true;
        else if (ch == '{' || ch == '[') { if (++depth > 32U) return false; }
        else if (ch == '}' || ch == ']') { if (depth == 0U) return false; --depth; }
    }
    return depth == 0U && !quoted;
}

class IpcConnection final {
public:
    ~IpcConnection() { close(); }
    bool open(const std::string& path) {
        close();
#ifdef _WIN32
        pipe_ = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0U, nullptr, OPEN_EXISTING,
                            FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
        if (pipe_ == INVALID_HANDLE_VALUE) return false;
        event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (event_ == nullptr) { close(); return false; }
        return true;
#else
        sockaddr_un address{};
        if (path.size() >= sizeof(address.sun_path)) return false;
        struct stat info{};
        if (lstat(path.c_str(), &info) != 0 || !S_ISSOCK(info.st_mode) || info.st_uid != geteuid()) return false;
        socket_ = socket(AF_UNIX, SOCK_STREAM, 0);
        if (socket_ < 0) return false;
        if (fcntl(socket_, F_SETFL, O_NONBLOCK) != 0 || fcntl(socket_, F_SETFD, FD_CLOEXEC) != 0) { close(); return false; }
#ifdef SO_NOSIGPIPE
        const int enabled = 1;
        static_cast<void>(setsockopt(socket_, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)));
#endif
        address.sun_family = AF_UNIX;
        std::memcpy(address.sun_path, path.c_str(), path.size() + 1U);
        if (connect(socket_, reinterpret_cast<const sockaddr*>(&address), static_cast<socklen_t>(sizeof(address))) != 0) {
            if (errno != EINPROGRESS) { close(); return false; }
            pollfd descriptor{socket_, POLLOUT, 0};
            int error{};
            socklen_t length = sizeof(error);
            if (poll(&descriptor, 1U, 100) <= 0 || getsockopt(socket_, SOL_SOCKET, SO_ERROR, &error, &length) != 0 || error != 0) {
                close(); return false;
            }
        }
        return true;
#endif
    }
    void close() {
#ifdef _WIN32
        if (pipe_ != INVALID_HANDLE_VALUE) CloseHandle(pipe_);
        if (event_ != nullptr) CloseHandle(event_);
        pipe_ = INVALID_HANDLE_VALUE;
        event_ = nullptr;
#else
        if (socket_ >= 0) ::close(socket_);
        socket_ = -1;
#endif
    }
    bool connected() const noexcept {
#ifdef _WIN32
        return pipe_ != INVALID_HANDLE_VALUE;
#else
        return socket_ >= 0;
#endif
    }
    bool write(std::string_view bytes) {
#ifdef _WIN32
        // One write for the full header+JSON, as Discord's named pipe requires.
        return transfer(false, const_cast<char*>(bytes.data()), bytes.size()) == static_cast<int>(bytes.size());
#else
        const auto deadline = Clock::now() + 100ms;
        while (!bytes.empty()) {
#ifdef MSG_NOSIGNAL
            constexpr int flags = MSG_NOSIGNAL;
#else
            constexpr int flags = 0;
#endif
            const auto sent = send(socket_, bytes.data(), bytes.size(), flags);
            if (sent > 0) bytes.remove_prefix(static_cast<std::size_t>(sent));
            else if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) && Clock::now() < deadline) {
                pollfd descriptor{socket_, POLLOUT, 0};
                static_cast<void>(poll(&descriptor, 1U, 10));
            } else return false;
        }
        return true;
#endif
    }
    // Zero is "nothing available", negative is a closed/broken connection.
    int read(char* data, std::size_t capacity) {
#ifdef _WIN32
        DWORD available{};
        if (!PeekNamedPipe(pipe_, nullptr, 0U, nullptr, &available, nullptr)) return -1;
        if (available == 0U) return 0;
        return transfer(true, data, std::min(capacity, static_cast<std::size_t>(available)));
#else
        const auto count = recv(socket_, data, capacity, 0);
        if (count > 0) return static_cast<int>(count);
        return count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? 0 : -1;
#endif
    }
private:
#ifdef _WIN32
    HANDLE pipe_{INVALID_HANDLE_VALUE};
    HANDLE event_{};
    int transfer(bool reading, char* data, std::size_t bytes) {
        OVERLAPPED operation{};
        operation.hEvent = event_;
        ResetEvent(event_);
        DWORD transferred{};
        const auto success = reading ? ReadFile(pipe_, data, static_cast<DWORD>(bytes), &transferred, &operation)
                                     : WriteFile(pipe_, data, static_cast<DWORD>(bytes), &transferred, &operation);
        if (!success) {
            if (GetLastError() != ERROR_IO_PENDING) return -1;
            if (WaitForSingleObject(event_, 100U) != WAIT_OBJECT_0) {
                CancelIoEx(pipe_, &operation);
                // Cancellation must complete before the stack OVERLAPPED dies.
                static_cast<void>(GetOverlappedResult(pipe_, &operation, &transferred, TRUE));
                return -1;
            }
            if (!GetOverlappedResult(pipe_, &operation, &transferred, FALSE)) return -1;
        }
        return static_cast<int>(transferred);
    }
#else
    int socket_{-1};
#endif
};

std::vector<std::string> ipc_paths(const std::string& override_path) {
    if (!override_path.empty()) return {override_path};
    std::vector<std::string> result;
#ifdef _WIN32
    for (unsigned index = 0U; index < 10U; ++index) result.push_back("\\\\?\\pipe\\discord-ipc-" + std::to_string(index));
#else
    std::vector<std::string> roots;
    for (const auto* variable : {"XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP"}) {
        auto root = environment(variable);
        if (!root.empty() && root.front() == '/' && std::find(roots.begin(), roots.end(), root) == roots.end()) roots.push_back(std::move(root));
    }
    roots.emplace_back("/tmp");
    for (const auto& root : roots) for (unsigned index = 0U; index < 10U; ++index) result.push_back(root + "/discord-ipc-" + std::to_string(index));
#endif
    return result;
}
} // namespace

bool valid_discord_application_id(std::string_view value) noexcept {
    if (value.size() < 17U || value.size() > 20U || value.front() == '0') return false;
    std::uint64_t parsed{};
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    return result.ec == std::errc{} && result.ptr == value.data() + value.size() && parsed > 0U;
}

std::string configured_discord_application_id() {
    auto value = environment("AOS_DISCORD_APPLICATION_ID");
    if (value.empty()) value = AOS_DISCORD_APPLICATION_ID;
    // Existing CMake caches may retain the formerly empty optional default.
    if (value.empty()) value = "1557497536602701824";
    return valid_discord_application_id(value) ? value : std::string{};
}

std::optional<std::string> discord_join_url(std::string_view endpoint, int protocol) {
    if ((protocol != 168 && protocol != 3 && protocol != 4) || !public_endpoint(endpoint)) return std::nullopt;
    return "aosbb://" + std::string{endpoint} + (protocol == 3 ? ":0.75" : protocol == 4 ? ":0.76" : "");
}

std::optional<std::string> parse_discord_join_secret(std::string_view secret) {
    if (!secret.starts_with("aosbb://") || secret.size() > 40U) return std::nullopt;
    const std::string original{secret};
    secret.remove_prefix(8U);
    int protocol = 168;
    if (secret.ends_with(":0.75") || secret.ends_with(":0.76")) {
        protocol = secret.back() == '5' ? 3 : 4;
        secret.remove_suffix(5U);
    }
    auto result = discord_join_url(secret, protocol);
    return result && *result == original ? result : std::nullopt;
}

struct DiscordPresence::Impl final {
    mutable std::mutex mutex;
    std::condition_variable changed;
    DiscordPresenceConfig config{.enabled = false};
    std::optional<DiscordPresenceActivity> activity;
    std::optional<std::string> join_request;
    std::atomic<DiscordPresenceStatus> state{DiscordPresenceStatus::disabled};
    std::atomic<DiscordActivityAcknowledgement> acknowledged{DiscordActivityAcknowledgement::none};
    std::uint64_t generation{};
    std::uint64_t urgent{};
    bool stopping{};
    std::string endpoint;
    const std::string default_application_id{configured_discord_application_id()};
    std::thread worker;

    explicit Impl(std::string path) : endpoint{std::move(path)}, worker{[this] { run(); }} {}
    ~Impl() {
        { std::lock_guard lock{mutex}; stopping = true; }
        changed.notify_one();
        worker.join();
    }

    void run() noexcept {
        try { work(); }
        catch (...) { state = DiscordPresenceStatus::waiting_for_discord; }
    }
    void work() {
        IpcConnection connection;
        const auto paths = ipc_paths(endpoint);
        DiscordPresenceConfig active_config{.enabled = false};
        std::string input;
        std::string sent_activity;
        std::string desired_value{"null"};
        std::string pending_nonce;
        bool pending_has_activity{};
        std::string last_join;
        auto last_join_at = Clock::time_point::min();
        auto next_connect = Clock::time_point::min();
        auto next_update = Clock::time_point::min();
        auto handshake_deadline = Clock::time_point::max();
        auto response_deadline = Clock::time_point::max();
        std::uint64_t sent_urgent{};
        std::uint64_t nonce{};
        std::optional<std::uint64_t> serialized_revision;
        bool ready = false;
        const auto send_activity = [&](std::string_view value) {
            pending_nonce = "presence-" + std::to_string(++nonce);
            pending_has_activity = value != "null";
            const auto command = "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + std::to_string(process_id()) +
                ",\"activity\":" + std::string{value} + "},\"nonce\":\"" + pending_nonce + "\"}";
            response_deadline = Clock::now() + 10s;
            return connection.write(frame(1U, command));
        };
        const auto disconnect = [&] {
            connection.close(); ready = false; input.clear(); sent_activity.clear(); pending_nonce.clear();
            acknowledged = DiscordActivityAcknowledgement::none;
            state = DiscordPresenceStatus::waiting_for_discord;
            next_connect = Clock::now() + 30s;
        };
        while (true) {
            DiscordPresenceConfig desired_config;
            std::optional<DiscordPresenceActivity> desired_activity;
            std::uint64_t revision{};
            std::uint64_t urgent_revision{};
            bool stop{};
            {
                std::lock_guard lock{mutex};
                desired_config = config; desired_activity = activity;
                revision = generation; urgent_revision = urgent; stop = stopping;
            }
            if (stop) { if (ready) static_cast<void>(send_activity("null")); break; }
            if (serialized_revision != revision) {
                desired_value = desired_activity ? dump(activity_json(*desired_activity, desired_config.allow_join)) : "null";
                serialized_revision = revision;
            }
            if (desired_config != active_config) {
                if (ready) static_cast<void>(send_activity("null"));
                disconnect();
                active_config = desired_config;
                next_connect = Clock::time_point::min();
            }
            const bool configured = valid_discord_application_id(desired_config.application_id);
            if (!desired_config.enabled || !configured) {
                state = desired_config.enabled ? DiscordPresenceStatus::unconfigured : DiscordPresenceStatus::disabled;
            } else {
                if (!connection.connected() && Clock::now() >= next_connect) {
                    state = DiscordPresenceStatus::connecting;
                    for (const auto& path : paths) if (connection.open(path)) break;
                    if (connection.connected() && connection.write(frame(0U, dump(Json{{"v", 1}, {"client_id", desired_config.application_id}})))) {
                        handshake_deadline = Clock::now() + 3s;
                    } else disconnect();
                }
                if (connection.connected()) {
                    std::array<char, 8192U> bytes{};
                    const int count = connection.read(bytes.data(), bytes.size());
                    if (count < 0) disconnect();
                    else if (count > 0) {
                        input.append(bytes.data(), static_cast<std::size_t>(count));
                        // An endless burst of tiny frames must not outrun the
                        // per-iteration dispatch budget and grow this buffer.
                        if (input.size() > maximum_frame * 2U) disconnect();
                    }
                    for (unsigned messages = 0U; connection.connected() && messages < 8U && input.size() >= 8U; ++messages) {
                        const auto opcode = little_u32(input.data());
                        const auto length = little_u32(input.data() + 4U);
                        if (length > maximum_frame || opcode > 4U) { disconnect(); break; }
                        if (input.size() < 8U + length) break;
                        const auto payload = input.substr(8U, length);
                        input.erase(0U, 8U + length);
                        if (opcode == 3U) { if (!connection.write(frame(4U, payload))) disconnect(); continue; }
                        if (opcode == 4U) continue;
                        if (opcode != 1U || !shallow_json(payload)) { disconnect(); break; }
                        const auto message = Json::parse(payload, nullptr, false);
                        if (!message.is_object()) { disconnect(); break; }
                        const auto string_field = [&](const char* key) -> std::string {
                            const auto found = message.find(key);
                            return found != message.end() && found->is_string() ? found->get<std::string>() : std::string{};
                        };
                        const auto command = string_field("cmd");
                        const auto event = string_field("evt");
                        if (!ready && command == "DISPATCH" && event == "READY") {
                            ready = true; state = DiscordPresenceStatus::ready;
                            next_update = Clock::time_point::min();
                            if (desired_config.allow_join && !connection.write(frame(1U, dump(Json{
                                {"cmd", "SUBSCRIBE"}, {"evt", "ACTIVITY_JOIN"}, {"nonce", "join-" + std::to_string(++nonce)}})))) disconnect();
                        } else if (ready && event == "ERROR" && command != "SUBSCRIBE") {
                            disconnect();
                        } else if (ready && command == "SET_ACTIVITY" && !pending_nonce.empty() && string_field("nonce") == pending_nonce) {
                            pending_nonce.clear();
                            acknowledged = pending_has_activity ? DiscordActivityAcknowledgement::published : DiscordActivityAcknowledgement::cleared;
                        } else if (ready && command == "DISPATCH" && event == "ACTIVITY_JOIN" && desired_config.allow_join) {
                            const auto data = message.find("data");
                            if (data != message.end() && data->is_object()) {
                                const auto secret = data->find("secret");
                                const auto join = secret != data->end() && secret->is_string()
                                    ? parse_discord_join_secret(secret->get<std::string>()) : std::nullopt;
                                if (join && (*join != last_join || Clock::now() - last_join_at > 5s)) {
                                    std::lock_guard lock{mutex};
                                    if (config.enabled && config.allow_join && config.application_id == desired_config.application_id) {
                                        join_request = *join; last_join = *join; last_join_at = Clock::now();
                                    }
                                }
                            }
                        }
                    }
                    if (connection.connected() && ((!ready && Clock::now() >= handshake_deadline) ||
                        (ready && !pending_nonce.empty() && Clock::now() >= response_deadline))) disconnect();
                    if (ready) {
                        const auto& value = desired_value;
                        const bool immediate = value == "null" || sent_activity == "null" || urgent_revision != sent_urgent;
                        if (value != sent_activity && (immediate || (pending_nonce.empty() && Clock::now() >= next_update))) {
                            if (send_activity(value)) {
                                sent_activity = value; sent_urgent = urgent_revision; next_update = Clock::now() + 15s;
                            } else disconnect();
                        }
                    }
                }
            }
            std::unique_lock lock{mutex};
            changed.wait_for(lock, connection.connected() ? 100ms : 1s, [&] { return stopping || generation != revision; });
        }
    }
};

DiscordPresence::DiscordPresence(std::string ipc_endpoint) : impl_{std::make_unique<Impl>(std::move(ipc_endpoint))} {}
DiscordPresence::~DiscordPresence() = default;

void DiscordPresence::configure(DiscordPresenceConfig config) {
    if (config.application_id.empty()) config.application_id = impl_->default_application_id;
    if (!valid_discord_application_id(config.application_id)) config.application_id.clear();
    {
        std::lock_guard lock{impl_->mutex};
        if (impl_->config == config) return;
        impl_->config = std::move(config); impl_->join_request.reset(); ++impl_->generation; ++impl_->urgent;
    }
    impl_->changed.notify_one();
}

void DiscordPresence::update(const DiscordPresenceActivity& activity) {
    auto bounded = activity;
    bounded.server_name = bounded_text(activity.server_name);
    bounded.map = bounded_text(activity.map);
    bounded.mode = bounded_text(activity.mode);
    if (bounded.endpoint.size() > 40U) bounded.endpoint.clear();
    {
        std::lock_guard lock{impl_->mutex};
        // Revoke stale joins immediately on a server/privacy transition.
        if (impl_->activity && (impl_->activity->endpoint != bounded.endpoint || impl_->activity->protocol != bounded.protocol ||
            impl_->activity->password_protected != bounded.password_protected)) ++impl_->urgent;
        impl_->activity = std::move(bounded); ++impl_->generation;
    }
    impl_->changed.notify_one();
}

void DiscordPresence::clear() {
    {
        std::lock_guard lock{impl_->mutex};
        if (!impl_->activity) return;
        impl_->activity.reset(); ++impl_->generation; ++impl_->urgent;
    }
    impl_->changed.notify_one();
}

std::optional<std::string> DiscordPresence::poll_join_request() {
    std::lock_guard lock{impl_->mutex};
    auto result = std::move(impl_->join_request);
    impl_->join_request.reset();
    return result;
}

DiscordPresenceStatus DiscordPresence::status() const noexcept { return impl_->state.load(); }
DiscordActivityAcknowledgement DiscordPresence::activity_acknowledgement() const noexcept { return impl_->acknowledged.load(); }

} // namespace battlespades::platform
