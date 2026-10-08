#include "battlespades/platform/relay_host_tunnel.hpp"

#include "battlespades/core/diagnostics.hpp"
#include "battlespades/platform/socket_select.hpp"

#include <sodium.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstring>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
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

namespace battlespades::platform {
namespace {

constexpr std::array<unsigned char, 4U> magic{'A', 'O', 'S', 'R'};
constexpr std::uint8_t version{1U};
constexpr std::size_t header_bytes{36U};
constexpr std::size_t mac_bytes{crypto_auth_hmacsha256_BYTES};
constexpr std::size_t maximum_payload_bytes{32U * 1'024U};
constexpr std::size_t receive_batch{64U};
constexpr std::uint8_t frame_hello{1U};
constexpr std::uint8_t frame_ack{2U};
constexpr std::uint8_t frame_host_to_client{3U};
constexpr std::uint8_t frame_client_to_host{4U};
constexpr std::uint8_t frame_keepalive{5U};
constexpr std::uint8_t frame_close{6U};

#if defined(_WIN32)
using Socket = SOCKET;
using SocketLength = int;  // Winsock counts bytes and address lengths in int
using SendLength = int;
constexpr Socket invalid_socket{INVALID_SOCKET};
void close_socket(Socket value) noexcept {
    if (value != invalid_socket) static_cast<void>(closesocket(value));
}
[[nodiscard]] bool socket_would_block() noexcept {
    const auto error = WSAGetLastError();
    return error == WSAEWOULDBLOCK || error == WSAEINTR;
}
#else
using Socket = int;
using SocketLength = socklen_t;
using SendLength = std::size_t;
constexpr Socket invalid_socket{-1};
void close_socket(Socket value) noexcept {
    if (value != invalid_socket) static_cast<void>(close(value));
}
[[nodiscard]] bool socket_would_block() noexcept {
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
}
#endif

[[nodiscard]] bool initialize_network() noexcept {
    static const bool initialized = [] {
        if (sodium_init() < 0) return false;
#if defined(_WIN32)
        WSADATA data{};
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
        return true;
#endif
    }();
    return initialized;
}

[[nodiscard]] bool set_nonblocking(Socket socket) noexcept {
#if defined(_WIN32)
    u_long enabled{1U};
    return ioctlsocket(socket, FIONBIO, &enabled) == 0;
#else
    const auto flags = fcntl(socket, F_GETFL, 0);
    return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

void write_u16(std::span<unsigned char> output, std::size_t offset,
               std::uint16_t value) noexcept {
    output[offset] = static_cast<unsigned char>(value >> 8U);
    output[offset + 1U] = static_cast<unsigned char>(value);
}

void write_u32(std::span<unsigned char> output, std::size_t offset,
               std::uint32_t value) noexcept {
    for (std::size_t index{}; index < 4U; ++index) {
        output[offset + index] =
            static_cast<unsigned char>(value >> ((3U - index) * 8U));
    }
}

void write_u64(std::span<unsigned char> output, std::size_t offset,
               std::uint64_t value) noexcept {
    for (std::size_t index{}; index < 8U; ++index) {
        output[offset + index] =
            static_cast<unsigned char>(value >> ((7U - index) * 8U));
    }
}

[[nodiscard]] std::uint16_t read_u16(std::span<const unsigned char> input,
                                     std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(input[offset]) << 8U) |
        static_cast<std::uint16_t>(input[offset + 1U]));
}

[[nodiscard]] std::uint32_t read_u32(std::span<const unsigned char> input,
                                     std::size_t offset) noexcept {
    std::uint32_t value{};
    for (std::size_t index{}; index < 4U; ++index) {
        value = (value << 8U) | input[offset + index];
    }
    return value;
}

[[nodiscard]] std::uint64_t read_u64(std::span<const unsigned char> input,
                                     std::size_t offset) noexcept {
    std::uint64_t value{};
    for (std::size_t index{}; index < 8U; ++index) {
        value = (value << 8U) | input[offset + index];
    }
    return value;
}

[[nodiscard]] std::optional<std::array<unsigned char, 16U>> parse_uuid(
    std::string_view value) noexcept {
    if (value.size() != 36U || value[8U] != '-' || value[13U] != '-' ||
        value[18U] != '-' || value[23U] != '-') {
        return std::nullopt;
    }
    std::array<unsigned char, 16U> output{};
    std::size_t source{};
    for (auto& byte : output) {
        while (source < value.size() && value[source] == '-') ++source;
        if (source + 2U > value.size()) return std::nullopt;
        unsigned int parsed{};
        const auto converted = std::from_chars(value.data() + source,
                                               value.data() + source + 2U,
                                               parsed, 16);
        if (converted.ec != std::errc{} || converted.ptr != value.data() + source + 2U) {
            return std::nullopt;
        }
        byte = static_cast<unsigned char>(parsed);
        source += 2U;
    }
    return output;
}

[[nodiscard]] std::optional<std::array<unsigned char, 32U>> parse_host_key(
    std::string_view value) noexcept {
    std::array<unsigned char, 32U> output{};
    std::size_t decoded{};
    if (value.size() != 43U || sodium_base642bin(
            output.data(), output.size(), value.data(), value.size(), nullptr,
            &decoded, nullptr, sodium_base64_VARIANT_URLSAFE_NO_PADDING) != 0 ||
        decoded != output.size()) {
        return std::nullopt;
    }
    return output;
}

struct Frame final {
    std::uint8_t type{};
    std::uint64_t sequence{};
    std::uint32_t client_id{};
    std::vector<unsigned char> payload;
};

[[nodiscard]] std::vector<unsigned char> encode_frame(
    std::uint8_t type,
    std::span<const unsigned char, 16U> allocation,
    std::uint64_t sequence,
    std::uint32_t client_id,
    std::span<const unsigned char> payload,
    std::span<const unsigned char, 32U> key) {
    if (payload.size() > maximum_payload_bytes) return {};
    std::vector<unsigned char> output(header_bytes + payload.size() + mac_bytes);
    std::ranges::copy(magic, output.begin());
    output[4U] = version;
    output[5U] = type;
    std::ranges::copy(allocation, output.begin() + 6);
    write_u64(output, 22U, sequence);
    write_u32(output, 30U, client_id);
    write_u16(output, 34U, static_cast<std::uint16_t>(payload.size()));
    std::ranges::copy(payload, output.begin() + static_cast<std::ptrdiff_t>(header_bytes));
    static_cast<void>(crypto_auth_hmacsha256(
        output.data() + header_bytes + payload.size(), output.data(),
        static_cast<unsigned long long>(header_bytes + payload.size()), key.data()));
    return output;
}

[[nodiscard]] std::optional<Frame> decode_frame(
    std::span<const unsigned char> input,
    std::span<const unsigned char, 16U> allocation,
    std::span<const unsigned char, 32U> key) {
    if (input.size() < header_bytes + mac_bytes ||
        input.size() > header_bytes + maximum_payload_bytes + mac_bytes ||
        !std::ranges::equal(magic, input.first<4U>()) || input[4U] != version ||
        !std::ranges::equal(allocation, input.subspan(6U, 16U))) {
        return std::nullopt;
    }
    const auto payload_size = read_u16(input, 34U);
    if (input.size() != header_bytes + payload_size + mac_bytes) return std::nullopt;
    const auto* supplied = input.data() + header_bytes + payload_size;
    if (crypto_auth_hmacsha256_verify(
            supplied, input.data(),
            static_cast<unsigned long long>(header_bytes + payload_size), key.data()) != 0) {
        return std::nullopt;
    }
    const auto type = input[5U];
    if (type < frame_hello || type > frame_close) return std::nullopt;
    Frame output;
    output.type = type;
    output.sequence = read_u64(input, 22U);
    output.client_id = read_u32(input, 30U);
    output.payload.assign(input.begin() + static_cast<std::ptrdiff_t>(header_bytes),
                          input.begin() + static_cast<std::ptrdiff_t>(header_bytes + payload_size));
    return output;
}

} // namespace

struct RelayHostTunnel::Impl final {
    RelayHostTunnelConfig config;
    std::array<unsigned char, 16U> allocation{};
    std::array<unsigned char, 32U> key{};
    sockaddr_in relay_address{};
    sockaddr_in local_address{};
    Socket relay_socket{invalid_socket};
    struct Client final {
        Socket socket{invalid_socket};
        std::chrono::steady_clock::time_point last_received{};
    };
    std::map<std::uint32_t, Client> clients;
    std::jthread worker;
    std::atomic_bool active{};
    std::atomic_bool acknowledged{};
    mutable std::mutex mutex;
    std::condition_variable ready_condition;
    std::string error;
    std::uint64_t host_sequence{};
    std::uint64_t relay_sequence{};
    std::chrono::steady_clock::time_point last_acknowledgement{};

    ~Impl() {
        stop();
        sodium_memzero(key.data(), key.size());
    }

    void set_error(std::string value) {
        std::scoped_lock lock{mutex};
        if (error.empty()) {
            core::diagnostic("relay", value);
            error = std::move(value);
        }
        ready_condition.notify_all();
    }

    [[nodiscard]] bool send_frame(std::uint8_t type, std::uint32_t client,
                                  std::span<const unsigned char> payload = {}) {
        auto frame = encode_frame(type, allocation, ++host_sequence, client, payload, key);
        if (frame.empty()) return false;
        const auto sent = send(relay_socket,
                               reinterpret_cast<const char*>(frame.data()),
                               static_cast<SendLength>(frame.size()), 0);
        return sent >= 0 && static_cast<std::size_t>(sent) == frame.size();
    }

    [[nodiscard]] Socket client_socket(std::uint32_t id) {
        const auto now = std::chrono::steady_clock::now();
        if (const auto found = clients.find(id); found != clients.end()) {
            found->second.last_received = now;
            return found->second.socket;
        }
        if (clients.size() >= config.maximum_clients) return invalid_socket;
        const auto socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (socket == invalid_socket || !set_nonblocking(socket) ||
            connect(socket, reinterpret_cast<const sockaddr*>(&local_address),
                    sizeof(local_address)) != 0) {
            close_socket(socket);
            return invalid_socket;
        }
        clients.emplace(id, Client{socket, now});
        return socket;
    }

    [[nodiscard]] bool receive_relay() {
        std::array<unsigned char, header_bytes + maximum_payload_bytes + mac_bytes> buffer{};
        for (std::size_t packet{}; packet < receive_batch; ++packet) {
            const auto bytes = recv(relay_socket, reinterpret_cast<char*>(buffer.data()),
                                    static_cast<int>(buffer.size()), 0);
            if (bytes < 0) {
                if (socket_would_block()) return true;
                set_error("The public relay socket failed while receiving.");
                return false;
            }
            if (bytes == 0) continue;
            const auto decoded = decode_frame(
                std::span<const unsigned char>{buffer.data(), static_cast<std::size_t>(bytes)},
                allocation, key);
            if (!decoded.has_value() || decoded->sequence <= relay_sequence) continue;
            relay_sequence = decoded->sequence;
            if (decoded->type == frame_ack && decoded->client_id == 0U &&
                decoded->payload.empty()) {
                last_acknowledgement = std::chrono::steady_clock::now();
                acknowledged.store(true, std::memory_order_release);
                ready_condition.notify_all();
                continue;
            }
            if (decoded->type != frame_client_to_host || decoded->client_id == 0U ||
                decoded->payload.empty()) {
                continue;
            }
            const auto local = client_socket(decoded->client_id);
            if (local == invalid_socket) continue;
            static_cast<void>(send(local,
                                   reinterpret_cast<const char*>(decoded->payload.data()),
                                   static_cast<SendLength>(decoded->payload.size()), 0));
        }
        return true;
    }

    void receive_clients(fd_set& readable) {
        std::array<unsigned char, maximum_payload_bytes> buffer{};
        for (const auto& [id, client] : clients) {
            const auto socket = client.socket;
            if (!select_has_socket(socket, readable)) continue;
            for (std::size_t packet{}; packet < receive_batch; ++packet) {
                const auto bytes = recv(socket, reinterpret_cast<char*>(buffer.data()),
                                        static_cast<int>(buffer.size()), 0);
                if (bytes < 0) {
                    if (socket_would_block()) break;
                    break;
                }
                if (bytes == 0) break;
                static_cast<void>(send_frame(
                    frame_host_to_client, id,
                    std::span<const unsigned char>{buffer.data(),
                                                   static_cast<std::size_t>(bytes)}));
            }
        }
    }

    void run(std::stop_token stop) noexcept {
        active.store(true, std::memory_order_release);
        auto next_control = std::chrono::steady_clock::time_point{};
        while (!stop.stop_requested()) {
            const auto now = std::chrono::steady_clock::now();
            if (acknowledged.load(std::memory_order_acquire) &&
                now - last_acknowledgement >= std::max(std::chrono::seconds{3}, config.keepalive * 3)) {
                set_error("The public relay stopped acknowledging the host tunnel.");
                break;
            }
            for (auto entry = clients.begin(); entry != clients.end();) {
                if (now - entry->second.last_received >= config.client_idle_timeout) {
                    close_socket(entry->second.socket);
                    entry = clients.erase(entry);
                } else {
                    ++entry;
                }
            }
            if (now >= next_control) {
                const auto type = acknowledged.load(std::memory_order_acquire)
                                      ? frame_keepalive
                                      : frame_hello;
                if (!send_frame(type, 0U)) {
                    set_error("Could not reach the public match relay.");
                    break;
                }
                const auto interval = acknowledged.load(std::memory_order_acquire)
                                          ? std::max(std::chrono::seconds{1},
                                                     config.keepalive / 2)
                                          : std::chrono::seconds{1};
                next_control = now + interval;
            }

            fd_set readable;
            FD_ZERO(&readable);
            select_add_socket(relay_socket, readable);
            Socket maximum = relay_socket;
            for (const auto& [id, client] : clients) {
                static_cast<void>(id);
                const auto socket = client.socket;
                select_add_socket(socket, readable);
                maximum = std::max(maximum, socket);
            }
            timeval timeout{0, 100'000};
            const auto selected = select(static_cast<int>(maximum + 1), &readable,
                                         nullptr, nullptr, &timeout);
            if (selected < 0) {
                if (socket_would_block()) continue;
                set_error("The public relay select loop failed.");
                break;
            }
            if (selected > 0 && select_has_socket(relay_socket, readable) && !receive_relay()) break;
            if (selected > 0) receive_clients(readable);
        }
        if (relay_socket != invalid_socket) {
            static_cast<void>(send_frame(frame_close, 0U));
        }
        acknowledged.store(false, std::memory_order_release);
        active.store(false, std::memory_order_release);
        ready_condition.notify_all();
    }

    void stop() noexcept {
        if (worker.joinable()) {
            worker.request_stop();
            worker.join();
        }
        for (const auto& [id, client] : clients) {
            static_cast<void>(id);
            close_socket(client.socket);
        }
        clients.clear();
        close_socket(relay_socket);
        relay_socket = invalid_socket;
        active.store(false, std::memory_order_release);
        acknowledged.store(false, std::memory_order_release);
    }
};

RelayHostTunnel::RelayHostTunnel() : impl_{std::make_unique<Impl>()} {}
RelayHostTunnel::~RelayHostTunnel() = default;
RelayHostTunnel::RelayHostTunnel(RelayHostTunnel&&) noexcept = default;
RelayHostTunnel& RelayHostTunnel::operator=(RelayHostTunnel&&) noexcept = default;

bool RelayHostTunnel::start(RelayHostTunnelConfig config, std::string& error) {
    error.clear();
    if (impl_ == nullptr) impl_ = std::make_unique<Impl>();
    if (impl_->active.load(std::memory_order_acquire)) {
        error = "A public relay tunnel is already running.";
        return false;
    }
    if (!initialize_network() || config.relay_port == 0U ||
        config.local_server_port == 0U || config.maximum_clients < 2U ||
        config.maximum_clients > 24U || config.keepalive <= std::chrono::seconds::zero() ||
        config.keepalive > std::chrono::minutes{5} ||
        config.client_idle_timeout <= std::chrono::seconds::zero() ||
        config.client_idle_timeout > std::chrono::minutes{10}) {
        error = "The public relay configuration is invalid.";
        return false;
    }
    const auto allocation = parse_uuid(config.allocation_id);
    const auto key = parse_host_key(config.host_key_base64url);
    if (!allocation.has_value() || !key.has_value()) {
        error = "AoSPlay returned invalid relay credentials.";
        return false;
    }
    // A terminal worker remains joinable and still owns its UDP endpoints.
    // Drain it before reusing this owner for another local server/allocation.
    impl_->stop();

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    addrinfo* addresses{};
    const auto port = std::to_string(config.relay_port);
    if (getaddrinfo(config.relay_host.c_str(), port.c_str(), &hints, &addresses) != 0 ||
        addresses == nullptr) {
        error = "The public relay hostname could not be resolved.";
        if (addresses != nullptr) freeaddrinfo(addresses);
        return false;
    }
    const auto socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket == invalid_socket || !set_nonblocking(socket) ||
        connect(socket, addresses->ai_addr,
                static_cast<SocketLength>(addresses->ai_addrlen)) != 0) {
        close_socket(socket);
        freeaddrinfo(addresses);
        error = "The public relay UDP socket could not be opened.";
        return false;
    }
    std::memcpy(&impl_->relay_address, addresses->ai_addr,
                std::min<std::size_t>(sizeof(impl_->relay_address), addresses->ai_addrlen));
    freeaddrinfo(addresses);

    impl_->config = std::move(config);
    impl_->allocation = *allocation;
    impl_->key = *key;
    impl_->relay_socket = socket;
    impl_->local_address = {};
    impl_->local_address.sin_family = AF_INET;
    impl_->local_address.sin_port = htons(impl_->config.local_server_port);
    impl_->local_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    impl_->host_sequence = 0U;
    impl_->relay_sequence = 0U;
    impl_->acknowledged.store(false, std::memory_order_release);
    {
        std::scoped_lock lock{impl_->mutex};
        impl_->error.clear();
    }
    // Publish the starting state before launching the worker. Otherwise the
    // acknowledgement wait can observe the default false value, satisfy its
    // shutdown predicate immediately, and tear down a healthy tunnel before
    // the worker has sent HELLO.
    impl_->active.store(true, std::memory_order_release);
    impl_->worker = std::jthread{[implementation = impl_.get()](std::stop_token stop) {
        implementation->run(stop);
    }};

    std::unique_lock lock{impl_->mutex};
    static_cast<void>(impl_->ready_condition.wait_for(
        lock, std::chrono::seconds{4}, [this] {
            return impl_->acknowledged.load(std::memory_order_acquire) ||
                   !impl_->error.empty() ||
                   !impl_->active.load(std::memory_order_acquire);
        }));
    if (!impl_->acknowledged.load(std::memory_order_acquire)) {
        error = impl_->error.empty() ? "The public relay did not acknowledge the host tunnel."
                                     : impl_->error;
        lock.unlock();
        impl_->stop();
        return false;
    }
    return true;
}

void RelayHostTunnel::stop() noexcept {
    if (impl_ != nullptr) impl_->stop();
}

bool RelayHostTunnel::running() const noexcept {
    return impl_ != nullptr && impl_->active.load(std::memory_order_acquire);
}

bool RelayHostTunnel::ready() const noexcept {
    return impl_ != nullptr && impl_->acknowledged.load(std::memory_order_acquire);
}

std::string RelayHostTunnel::last_error() const {
    if (impl_ == nullptr) return {};
    std::scoped_lock lock{impl_->mutex};
    return impl_->error;
}

} // namespace battlespades::platform
