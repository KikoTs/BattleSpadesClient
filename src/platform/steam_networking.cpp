#include "battlespades/platform/steam_networking.hpp"

#include "battlespades/core/diagnostics.hpp"

#include <steam/steam_api.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace battlespades::platform {
namespace {

#if defined(_WIN32)
using Socket = SOCKET;
constexpr Socket invalid_socket{INVALID_SOCKET};
using BufferLength = int;
void close_socket(Socket value) noexcept {
    if (value != invalid_socket) static_cast<void>(closesocket(value));
}
#else
using Socket = int;
constexpr Socket invalid_socket{-1};
using BufferLength = std::size_t;
void close_socket(Socket value) noexcept {
    if (value != invalid_socket) static_cast<void>(close(value));
}
#endif

constexpr std::size_t maximum_datagram_bytes{2048U};
constexpr int receive_batch{32};

/** One loopback datagram socket, always bound to 127.0.0.1 with no route out. */
[[nodiscard]] Socket open_loopback_socket(std::uint16_t connect_port, std::uint16_t& bound_port) {
    const auto handle = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (handle == invalid_socket) return invalid_socket;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0U;
    if (::bind(handle, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        close_socket(handle);
        return invalid_socket;
    }
#if defined(_WIN32)
    u_long non_blocking{1U};
    if (ioctlsocket(handle, FIONBIO, &non_blocking) != 0) {
        close_socket(handle);
        return invalid_socket;
    }
#else
    const auto flags = fcntl(handle, F_GETFL, 0);
    if (flags < 0 || fcntl(handle, F_SETFL, flags | O_NONBLOCK) != 0) {
        close_socket(handle);
        return invalid_socket;
    }
#endif
    sockaddr_in assigned{};
    socklen_t assigned_size = sizeof(assigned);
    if (::getsockname(handle, reinterpret_cast<sockaddr*>(&assigned), &assigned_size) != 0) {
        close_socket(handle);
        return invalid_socket;
    }
    bound_port = ntohs(assigned.sin_port);
    if (connect_port != 0U) {
        sockaddr_in server{};
        server.sin_family = AF_INET;
        server.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        server.sin_port = htons(connect_port);
        if (::connect(handle, reinterpret_cast<const sockaddr*>(&server), sizeof(server)) != 0) {
            close_socket(handle);
            return invalid_socket;
        }
    }
    return handle;
}

/** Steamworks entry points resolved at run time so Steam stays optional. */
struct SteamApi final {
    void* handle{};
    ESteamAPIInitResult(S_CALLTYPE* init_flat)(SteamErrMsg*){};
    void(S_CALLTYPE* shutdown)(){};
    void(S_CALLTYPE* run_callbacks)(){};
    void(S_CALLTYPE* manual_dispatch_init)(){};
    HSteamPipe(S_CALLTYPE* steam_pipe)(){};
    void(S_CALLTYPE* dispatch_run_frame)(HSteamPipe){};
    bool(S_CALLTYPE* dispatch_next)(HSteamPipe, CallbackMsg_t*){};
    void(S_CALLTYPE* dispatch_free)(HSteamPipe){};
    ISteamNetworkingSockets*(S_CALLTYPE* sockets)(){};
    ISteamNetworkingUtils*(S_CALLTYPE* utils)(){};
    ISteamUser*(S_CALLTYPE* user)(){};
    ISteamFriends*(S_CALLTYPE* friends)(){};
    void(S_CALLTYPE* init_relay)(ISteamNetworkingUtils*){};
    ESteamNetworkingAvailability(S_CALLTYPE* relay_status)(ISteamNetworkingUtils*,
                                                           SteamRelayNetworkStatus_t*){};
    HSteamListenSocket(S_CALLTYPE* create_listen_p2p)(ISteamNetworkingSockets*, int, int,
                                                      const SteamNetworkingConfigValue_t*){};
    HSteamListenSocket(S_CALLTYPE* create_listen_ip)(ISteamNetworkingSockets*,
                                                     const SteamNetworkingIPAddr&, int,
                                                     const SteamNetworkingConfigValue_t*){};
    HSteamNetConnection(S_CALLTYPE* connect_ip)(ISteamNetworkingSockets*,
                                                const SteamNetworkingIPAddr&, int,
                                                const SteamNetworkingConfigValue_t*){};
    HSteamNetConnection(S_CALLTYPE* connect_p2p)(ISteamNetworkingSockets*,
                                                 const SteamNetworkingIdentity&, int, int,
                                                 const SteamNetworkingConfigValue_t*){};
    EResult(S_CALLTYPE* accept)(ISteamNetworkingSockets*, HSteamNetConnection){};
    bool(S_CALLTYPE* close_connection)(ISteamNetworkingSockets*, HSteamNetConnection, int,
                                       const char*, bool){};
    bool(S_CALLTYPE* close_listen)(ISteamNetworkingSockets*, HSteamListenSocket){};
    HSteamNetPollGroup(S_CALLTYPE* create_poll_group)(ISteamNetworkingSockets*){};
    bool(S_CALLTYPE* destroy_poll_group)(ISteamNetworkingSockets*, HSteamNetPollGroup){};
    bool(S_CALLTYPE* set_poll_group)(ISteamNetworkingSockets*, HSteamNetConnection,
                                     HSteamNetPollGroup){};
    int(S_CALLTYPE* receive_poll_group)(ISteamNetworkingSockets*, HSteamNetPollGroup,
                                        SteamNetworkingMessage_t**, int){};
    int(S_CALLTYPE* receive_connection)(ISteamNetworkingSockets*, HSteamNetConnection,
                                        SteamNetworkingMessage_t**, int){};
    EResult(S_CALLTYPE* send_message)(ISteamNetworkingSockets*, HSteamNetConnection, const void*,
                                      uint32, int, int64*){};
    EResult(S_CALLTYPE* real_time_status)(ISteamNetworkingSockets*, HSteamNetConnection,
                                          SteamNetConnectionRealTimeStatus_t*, int,
                                          SteamNetConnectionRealTimeLaneStatus_t*){};
    void(S_CALLTYPE* release_message)(SteamNetworkingMessage_t*){};
    bool(S_CALLTYPE* connection_info)(ISteamNetworkingSockets*, HSteamNetConnection,
                                      SteamNetConnectionInfo_t*){};
    uint64(S_CALLTYPE* steam_id)(ISteamUser*){};
    const char*(S_CALLTYPE* persona_name)(ISteamFriends*){};
};

[[nodiscard]] void* library_symbol(void* handle, const char* name) noexcept {
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle), name));
#else
    return dlsym(handle, name);
#endif
}

void close_library(void* handle) noexcept {
    if (handle == nullptr) return;
#if defined(_WIN32)
    static_cast<void>(FreeLibrary(static_cast<HMODULE>(handle)));
#else
    static_cast<void>(dlclose(handle));
#endif
}

[[nodiscard]] void* open_library(const std::filesystem::path& path) noexcept {
#if defined(_WIN32)
    return reinterpret_cast<void*>(LoadLibraryW(path.c_str()));
#else
    return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

[[nodiscard]] std::filesystem::path default_library_name() {
#if defined(_WIN32)
    return "steam_api64.dll";
#elif defined(__APPLE__)
    return "libsteam_api.dylib";
#else
    return "libsteam_api.so";
#endif
}

[[nodiscard]] bool load_steam_api(const std::filesystem::path& configured,
                                  const std::filesystem::path& search_directory, SteamApi& api,
                                  std::string& error) {
    std::vector<std::filesystem::path> candidates;
    if (!configured.empty()) candidates.push_back(configured);
    if (!search_directory.empty()) candidates.push_back(search_directory / default_library_name());
    std::error_code ignored;
    const auto working = std::filesystem::current_path(ignored);
    if (!ignored) candidates.push_back(working / default_library_name());
    candidates.push_back(default_library_name());
    for (const auto& candidate : candidates) {
        api.handle = open_library(candidate);
        if (api.handle != nullptr) break;
    }
    if (api.handle == nullptr) {
        error = "the Steamworks library is not installed beside the game";
        return false;
    }
    struct Binding final {
        const char* name;
        void** target;
    };
    const std::array bindings{
        Binding{"SteamAPI_InitFlat", reinterpret_cast<void**>(&api.init_flat)},
        Binding{"SteamAPI_Shutdown", reinterpret_cast<void**>(&api.shutdown)},
        Binding{"SteamAPI_RunCallbacks", reinterpret_cast<void**>(&api.run_callbacks)},
        Binding{"SteamAPI_ManualDispatch_Init",
                reinterpret_cast<void**>(&api.manual_dispatch_init)},
        Binding{"SteamAPI_GetHSteamPipe", reinterpret_cast<void**>(&api.steam_pipe)},
        Binding{"SteamAPI_ManualDispatch_RunFrame",
                reinterpret_cast<void**>(&api.dispatch_run_frame)},
        Binding{"SteamAPI_ManualDispatch_GetNextCallback",
                reinterpret_cast<void**>(&api.dispatch_next)},
        Binding{"SteamAPI_ManualDispatch_FreeLastCallback",
                reinterpret_cast<void**>(&api.dispatch_free)},
        Binding{"SteamAPI_SteamNetworkingSockets_SteamAPI_v013",
                reinterpret_cast<void**>(&api.sockets)},
        Binding{"SteamAPI_SteamNetworkingUtils_SteamAPI_v004",
                reinterpret_cast<void**>(&api.utils)},
        Binding{"SteamAPI_SteamUser_v023", reinterpret_cast<void**>(&api.user)},
        Binding{"SteamAPI_SteamFriends_v018", reinterpret_cast<void**>(&api.friends)},
        Binding{"SteamAPI_ISteamNetworkingUtils_InitRelayNetworkAccess",
                reinterpret_cast<void**>(&api.init_relay)},
        Binding{"SteamAPI_ISteamNetworkingUtils_GetRelayNetworkStatus",
                reinterpret_cast<void**>(&api.relay_status)},
        Binding{"SteamAPI_ISteamNetworkingSockets_CreateListenSocketP2P",
                reinterpret_cast<void**>(&api.create_listen_p2p)},
        Binding{"SteamAPI_ISteamNetworkingSockets_ConnectP2P",
                reinterpret_cast<void**>(&api.connect_p2p)},
        Binding{"SteamAPI_ISteamNetworkingSockets_CreateListenSocketIP",
                reinterpret_cast<void**>(&api.create_listen_ip)},
        Binding{"SteamAPI_ISteamNetworkingSockets_ConnectByIPAddress",
                reinterpret_cast<void**>(&api.connect_ip)},
        Binding{"SteamAPI_ISteamNetworkingSockets_AcceptConnection",
                reinterpret_cast<void**>(&api.accept)},
        Binding{"SteamAPI_ISteamNetworkingSockets_CloseConnection",
                reinterpret_cast<void**>(&api.close_connection)},
        Binding{"SteamAPI_ISteamNetworkingSockets_CloseListenSocket",
                reinterpret_cast<void**>(&api.close_listen)},
        Binding{"SteamAPI_ISteamNetworkingSockets_CreatePollGroup",
                reinterpret_cast<void**>(&api.create_poll_group)},
        Binding{"SteamAPI_ISteamNetworkingSockets_DestroyPollGroup",
                reinterpret_cast<void**>(&api.destroy_poll_group)},
        Binding{"SteamAPI_ISteamNetworkingSockets_SetConnectionPollGroup",
                reinterpret_cast<void**>(&api.set_poll_group)},
        Binding{"SteamAPI_ISteamNetworkingSockets_ReceiveMessagesOnPollGroup",
                reinterpret_cast<void**>(&api.receive_poll_group)},
        Binding{"SteamAPI_ISteamNetworkingSockets_ReceiveMessagesOnConnection",
                reinterpret_cast<void**>(&api.receive_connection)},
        Binding{"SteamAPI_ISteamNetworkingSockets_SendMessageToConnection",
                reinterpret_cast<void**>(&api.send_message)},
        Binding{"SteamAPI_ISteamNetworkingSockets_GetConnectionRealTimeStatus",
                reinterpret_cast<void**>(&api.real_time_status)},
        Binding{"SteamAPI_SteamNetworkingMessage_t_Release",
                reinterpret_cast<void**>(&api.release_message)},
        Binding{"SteamAPI_ISteamNetworkingSockets_GetConnectionInfo",
                reinterpret_cast<void**>(&api.connection_info)},
        Binding{"SteamAPI_ISteamUser_GetSteamID", reinterpret_cast<void**>(&api.steam_id)},
        Binding{"SteamAPI_ISteamFriends_GetPersonaName",
                reinterpret_cast<void**>(&api.persona_name)},
    };
    for (const auto& binding : bindings) {
        *binding.target = library_symbol(api.handle, binding.name);
        if (*binding.target != nullptr) continue;
        error = std::string{"the installed Steamworks library has no "} + binding.name;
        close_library(api.handle);
        api.handle = nullptr;
        return false;
    }
    return true;
}

/** Registered tunnels, so Steam's status callback reaches its owner. */
class StatusRouter final {
public:
    using Handler = std::function<void(const SteamNetConnectionStatusChangedCallback_t&)>;

    [[nodiscard]] static StatusRouter& instance() {
        static StatusRouter router;
        return router;
    }

    [[nodiscard]] std::uint64_t add(Handler handler) {
        const std::scoped_lock lock{mutex_};
        const auto token = ++next_token_;
        handlers_.emplace_back(token, std::move(handler));
        return token;
    }

    void remove(std::uint64_t token) {
        const std::scoped_lock lock{mutex_};
        std::erase_if(handlers_, [token](const auto& entry) { return entry.first == token; });
    }

    void dispatch(const SteamNetConnectionStatusChangedCallback_t& event) {
        std::vector<Handler> copies;
        {
            const std::scoped_lock lock{mutex_};
            copies.reserve(handlers_.size());
            for (const auto& entry : handlers_) copies.push_back(entry.second);
        }
        for (const auto& handler : copies) handler(event);
    }

private:
    std::mutex mutex_;
    std::vector<std::pair<std::uint64_t, Handler>> handlers_;
    std::uint64_t next_token_{};
};

void S_CALLTYPE on_connection_status_changed(SteamNetConnectionStatusChangedCallback_t* event) {
    if (event != nullptr) StatusRouter::instance().dispatch(*event);
}

/** Every socket and connection reports status through one process-wide hook. */
[[nodiscard]] SteamNetworkingConfigValue_t status_callback_option() {
    SteamNetworkingConfigValue_t option{};
    option.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged,
                  reinterpret_cast<void*>(&on_connection_status_changed));
    return option;
}

} // namespace

struct SteamNetworkingRuntime::Impl final {
    SteamApi api;
    SteamNetworkingRuntimeConfig config;
    mutable std::mutex mutex;
    std::jthread pump;
    std::atomic_bool ready{};
    std::atomic<std::uint64_t> steam_id{};
    std::string persona;
    std::string relay_detail;
    std::atomic_bool relay_available{};
    HSteamPipe pipe{};
    std::string error;

    /** Serviced on the pump thread: message forwarding for every live tunnel. */
    std::mutex services_mutex;
    std::vector<std::pair<std::uint64_t, std::function<void()>>> services;
    std::uint64_t next_service{};

    [[nodiscard]] std::uint64_t add_service(std::function<void()> service) {
        const std::scoped_lock lock{services_mutex};
        const auto token = ++next_service;
        services.emplace_back(token, std::move(service));
        return token;
    }

    void remove_service(std::uint64_t token) {
        const std::scoped_lock lock{services_mutex};
        std::erase_if(services, [token](const auto& entry) { return entry.first == token; });
    }

    /**
     * Manual dispatch: the library is loaded at run time, so the SDK's
     * callback objects are unavailable and connection events are read
     * straight from Steam's queue.
     */
    void dispatch_callbacks() {
        api.dispatch_run_frame(pipe);
        CallbackMsg_t message{};
        while (api.dispatch_next(pipe, &message)) {
            if (message.m_iCallback == SteamNetConnectionStatusChangedCallback_t::k_iCallback &&
                message.m_pubParam != nullptr) {
                StatusRouter::instance().dispatch(
                    *reinterpret_cast<SteamNetConnectionStatusChangedCallback_t*>(
                        message.m_pubParam));
            }
            api.dispatch_free(pipe);
        }
    }

    void run(std::stop_token stop) {
        while (!stop.stop_requested()) {
            dispatch_callbacks();
            std::vector<std::function<void()>> copies;
            {
                const std::scoped_lock lock{services_mutex};
                copies.reserve(services.size());
                for (const auto& entry : services) copies.push_back(entry.second);
            }
            for (const auto& service : copies) service();
            SteamRelayNetworkStatus_t status{};
            const auto availability = api.relay_status(api.utils(), &status);
            relay_available.store(availability == k_ESteamNetworkingAvailability_Current);
            {
                const std::scoped_lock lock{mutex};
                relay_detail = status.m_debugMsg;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
    }
};

SteamNetworkingRuntime::SteamNetworkingRuntime() = default;

SteamNetworkingRuntime::~SteamNetworkingRuntime() {
    stop();
}

bool SteamNetworkingRuntime::start(SteamNetworkingRuntimeConfig config, std::string& error) {
    error.clear();
    if (impl_ != nullptr) {
        error = "the Steam runtime is already started";
        return false;
    }
    auto impl = std::make_unique<Impl>();
    impl->config = std::move(config);
    if (!load_steam_api(impl->config.library, impl->config.search_directory, impl->api, error)) {
        return false;
    }

    const auto app_id = std::to_string(impl->config.app_id);
#if defined(_WIN32)
    static_cast<void>(_putenv_s("SteamAppId", app_id.c_str()));
    static_cast<void>(_putenv_s("SteamGameId", app_id.c_str()));
#else
    static_cast<void>(setenv("SteamAppId", app_id.c_str(), 1));
    static_cast<void>(setenv("SteamGameId", app_id.c_str(), 1));
#endif
    SteamErrMsg message{};
    if (impl->api.init_flat(&message) != k_ESteamAPIInitResult_OK) {
        error = message[0] != '\0' ? message : "Steam is not running";
        close_library(impl->api.handle);
        return false;
    }
    impl->api.manual_dispatch_init();
    impl->pipe = impl->api.steam_pipe();
    impl->steam_id.store(impl->api.steam_id(impl->api.user()));
    if (const auto* const name = impl->api.persona_name(impl->api.friends()); name != nullptr) {
        impl->persona = name;
    }
    impl->api.init_relay(impl->api.utils());
    impl->pump = std::jthread{[raw = impl.get()](std::stop_token stop) { raw->run(stop); }};

    const auto deadline = std::chrono::steady_clock::now() + impl->config.relay_timeout;
    while (std::chrono::steady_clock::now() < deadline && !impl->relay_available.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
    }
    impl->ready.store(true);
    impl_ = std::move(impl);
    core::diagnostic("steam", "runtime ready: app=" + app_id + " id=" +
                                  std::to_string(impl_->steam_id.load()) + " relays=" +
                                  (impl_->relay_available.load() ? "ready" : "unavailable"));
    return true;
}

void SteamNetworkingRuntime::stop() noexcept {
    if (impl_ == nullptr) return;
    impl_->pump.request_stop();
    if (impl_->pump.joinable()) impl_->pump.join();
    if (impl_->api.shutdown != nullptr) impl_->api.shutdown();
    close_library(impl_->api.handle);
    impl_.reset();
}

bool SteamNetworkingRuntime::ready() const noexcept {
    return impl_ != nullptr && impl_->ready.load();
}

std::uint64_t SteamNetworkingRuntime::steam_id() const noexcept {
    return impl_ == nullptr ? 0U : impl_->steam_id.load();
}

std::string SteamNetworkingRuntime::persona_name() const {
    if (impl_ == nullptr) return {};
    const std::scoped_lock lock{impl_->mutex};
    return impl_->persona;
}

SteamRelayStatus SteamNetworkingRuntime::relay_status() const {
    if (impl_ == nullptr) return {};
    const std::scoped_lock lock{impl_->mutex};
    return SteamRelayStatus{impl_->relay_available.load(), impl_->relay_detail};
}

std::string SteamNetworkingRuntime::last_error() const {
    if (impl_ == nullptr) return {};
    const std::scoped_lock lock{impl_->mutex};
    return impl_->error;
}

struct SteamP2PHost::Impl final {
    struct Client final {
        HSteamNetConnection connection{};
        Socket socket{invalid_socket};
        std::chrono::steady_clock::time_point last_seen;
    };

    SteamNetworkingRuntime::Impl* runtime{};
    SteamP2PHostConfig config;
    HSteamListenSocket listen{k_HSteamListenSocket_Invalid};
    HSteamNetPollGroup poll_group{k_HSteamNetPollGroup_Invalid};
    std::uint64_t status_token{};
    std::uint64_t service_token{};
    mutable std::mutex mutex;
    std::vector<Client> clients;
    std::string error;
    std::atomic_bool running{};

    void on_status(const SteamNetConnectionStatusChangedCallback_t& event) {
        if (event.m_info.m_hListenSocket != listen) return;
        auto& api = runtime->api;
        if (event.m_info.m_eState == k_ESteamNetworkingConnectionState_Connecting) {
            const std::scoped_lock lock{mutex};
            if (clients.size() >= config.maximum_clients) {
                static_cast<void>(api.close_connection(api.sockets(), event.m_hConn, 0,
                                                       "server full", false));
                return;
            }
            if (api.accept(api.sockets(), event.m_hConn) != k_EResultOK) return;
            static_cast<void>(api.set_poll_group(api.sockets(), event.m_hConn, poll_group));
            std::uint16_t bound{};
            const auto socket = open_loopback_socket(config.local_server_port, bound);
            if (socket == invalid_socket) {
                static_cast<void>(api.close_connection(api.sockets(), event.m_hConn, 0,
                                                       "no local socket", false));
                return;
            }
            clients.push_back(Client{event.m_hConn, socket, std::chrono::steady_clock::now()});
            core::diagnostic("steam", "player joined over the relay network, now " +
                                          std::to_string(clients.size()) + " connected");
            return;
        }
        if (event.m_info.m_eState == k_ESteamNetworkingConnectionState_ClosedByPeer ||
            event.m_info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally) {
            drop(event.m_hConn, "peer closed");
        }
    }

    void drop(HSteamNetConnection connection, const char* reason) {
        auto& api = runtime->api;
        const std::scoped_lock lock{mutex};
        const auto found = std::ranges::find_if(
            clients, [connection](const Client& client) { return client.connection == connection; });
        if (found == clients.end()) return;
        close_socket(found->socket);
        clients.erase(found);
        static_cast<void>(api.close_connection(api.sockets(), connection, 0, reason, false));
    }

    void service() {
        auto& api = runtime->api;
        std::array<SteamNetworkingMessage_t*, receive_batch> inbound{};
        const auto received =
            api.receive_poll_group(api.sockets(), poll_group, inbound.data(), receive_batch);
        for (int index{}; index < received; ++index) {
            auto* const message = inbound[static_cast<std::size_t>(index)];
            if (message == nullptr) continue;
            {
                const std::scoped_lock lock{mutex};
                const auto found = std::ranges::find_if(clients, [message](const Client& client) {
                    return client.connection == message->m_conn;
                });
                if (found != clients.end()) {
                    found->last_seen = std::chrono::steady_clock::now();
                    static_cast<void>(::send(found->socket,
                                             static_cast<const char*>(message->m_pData),
                                             static_cast<BufferLength>(message->m_cbSize), 0));
                }
            }
            api.release_message(message);
        }

        std::array<char, maximum_datagram_bytes> buffer{};
        const std::scoped_lock lock{mutex};
        for (auto& client : clients) {
            while (true) {
                const auto bytes = ::recv(client.socket, buffer.data(),
                                          static_cast<BufferLength>(buffer.size()), 0);
                if (bytes <= 0) break;
                static_cast<void>(api.send_message(
                    api.sockets(), client.connection, buffer.data(),
                    static_cast<uint32>(bytes),
                    k_nSteamNetworkingSend_Unreliable | k_nSteamNetworkingSend_NoNagle, nullptr));
            }
        }
    }
};

SteamP2PHost::SteamP2PHost() = default;

SteamP2PHost::~SteamP2PHost() {
    stop();
}

bool SteamP2PHost::start(SteamNetworkingRuntime& runtime, SteamP2PHostConfig config,
                         std::string& error) {
    error.clear();
    if (impl_ != nullptr) {
        error = "this host tunnel is already started";
        return false;
    }
    if (!runtime.ready()) {
        error = "the Steam runtime is not ready";
        return false;
    }
    if (config.local_server_port == 0U) {
        error = "the local server port is required";
        return false;
    }
    auto impl = std::make_unique<Impl>();
    impl->runtime = runtime.impl();
    impl->config = config;
    auto& api = impl->runtime->api;
    const auto option = status_callback_option();
    if (config.direct_listen_port != 0U) {
        SteamNetworkingIPAddr address{};
        address.Clear();
        address.m_port = config.direct_listen_port;
        impl->listen = api.create_listen_ip(api.sockets(), address, 1, &option);
    } else {
        impl->listen = api.create_listen_p2p(api.sockets(), config.virtual_port, 1, &option);
    }
    if (impl->listen == k_HSteamListenSocket_Invalid) {
        error = "Steam refused the peer-to-peer listen socket";
        return false;
    }
    impl->poll_group = api.create_poll_group(api.sockets());
    impl->status_token = StatusRouter::instance().add(
        [raw = impl.get()](const SteamNetConnectionStatusChangedCallback_t& event) {
            raw->on_status(event);
        });
    impl->service_token = impl->runtime->add_service([raw = impl.get()] { raw->service(); });
    impl->running.store(true);
    impl_ = std::move(impl);
    core::diagnostic("steam", "hosting over the relay network on virtual port " +
                                  std::to_string(config.virtual_port));
    return true;
}

void SteamP2PHost::stop() noexcept {
    if (impl_ == nullptr) return;
    impl_->running.store(false);
    impl_->runtime->remove_service(impl_->service_token);
    StatusRouter::instance().remove(impl_->status_token);
    auto& api = impl_->runtime->api;
    {
        const std::scoped_lock lock{impl_->mutex};
        for (auto& client : impl_->clients) {
            static_cast<void>(
                api.close_connection(api.sockets(), client.connection, 0, "host closed", false));
            close_socket(client.socket);
        }
        impl_->clients.clear();
    }
    if (impl_->poll_group != k_HSteamNetPollGroup_Invalid) {
        static_cast<void>(api.destroy_poll_group(api.sockets(), impl_->poll_group));
    }
    if (impl_->listen != k_HSteamListenSocket_Invalid) {
        static_cast<void>(api.close_listen(api.sockets(), impl_->listen));
    }
    impl_.reset();
}

bool SteamP2PHost::running() const noexcept {
    return impl_ != nullptr && impl_->running.load();
}

std::size_t SteamP2PHost::connected_clients() const noexcept {
    if (impl_ == nullptr) return 0U;
    const std::scoped_lock lock{impl_->mutex};
    return impl_->clients.size();
}

std::string SteamP2PHost::last_error() const {
    if (impl_ == nullptr) return {};
    const std::scoped_lock lock{impl_->mutex};
    return impl_->error;
}

struct SteamP2PClient::Impl final {
    SteamNetworkingRuntime::Impl* runtime{};
    SteamP2PClientConfig config;
    HSteamNetConnection connection{k_HSteamNetConnection_Invalid};
    Socket socket{invalid_socket};
    std::uint16_t local_port{};
    sockaddr_in game{};
    bool game_known{};
    std::uint64_t status_token{};
    std::uint64_t service_token{};
    mutable std::mutex mutex;
    std::atomic_bool running{};
    std::atomic_int ping{-1};
    std::string error;

    void on_status(const SteamNetConnectionStatusChangedCallback_t& event) {
        if (event.m_hConn != connection) return;
        if (event.m_info.m_eState == k_ESteamNetworkingConnectionState_ClosedByPeer ||
            event.m_info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally) {
            const std::scoped_lock lock{mutex};
            error = event.m_info.m_szEndDebug[0] != '\0' ? event.m_info.m_szEndDebug
                                                         : "the host closed the connection";
            running.store(false);
            core::diagnostic("steam", "relay connection lost: " + error);
        }
    }

    void service() {
        auto& api = runtime->api;
        std::array<SteamNetworkingMessage_t*, receive_batch> inbound{};
        const auto received =
            api.receive_connection(api.sockets(), connection, inbound.data(), receive_batch);
        for (int index{}; index < received; ++index) {
            auto* const message = inbound[static_cast<std::size_t>(index)];
            if (message == nullptr) continue;
            {
                const std::scoped_lock lock{mutex};
                if (game_known) {
                    static_cast<void>(::sendto(socket, static_cast<const char*>(message->m_pData),
                                               static_cast<BufferLength>(message->m_cbSize), 0,
                                               reinterpret_cast<const sockaddr*>(&game),
                                               sizeof(game)));
                }
            }
            api.release_message(message);
        }

        std::array<char, maximum_datagram_bytes> buffer{};
        while (true) {
            sockaddr_in from{};
            socklen_t from_size = sizeof(from);
            const auto bytes =
                ::recvfrom(socket, buffer.data(), static_cast<BufferLength>(buffer.size()), 0,
                           reinterpret_cast<sockaddr*>(&from), &from_size);
            if (bytes <= 0) break;
            {
                const std::scoped_lock lock{mutex};
                game = from;
                game_known = true;
            }
            static_cast<void>(api.send_message(
                api.sockets(), connection, buffer.data(), static_cast<uint32>(bytes),
                k_nSteamNetworkingSend_Unreliable | k_nSteamNetworkingSend_NoNagle, nullptr));
        }

        SteamNetConnectionRealTimeStatus_t status{};
        if (api.real_time_status(api.sockets(), connection, &status, 0, nullptr) == k_EResultOK) {
            ping.store(status.m_nPing);
        }
        // Steam reports a lost peer through the status queue, but a host that
        // vanishes mid-match can also surface here first.
        SteamNetConnectionInfo_t info{};
        if (api.connection_info(api.sockets(), connection, &info) &&
            info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally &&
            running.load()) {
            const std::scoped_lock lock{mutex};
            error = info.m_szEndDebug[0] != '\0' ? info.m_szEndDebug : "the relay path failed";
            running.store(false);
            core::diagnostic("steam", "relay connection lost: " + error);
        }
    }
};

SteamP2PClient::SteamP2PClient() = default;

SteamP2PClient::~SteamP2PClient() {
    stop();
}

bool SteamP2PClient::start(SteamNetworkingRuntime& runtime, SteamP2PClientConfig config,
                           std::string& error) {
    error.clear();
    if (impl_ != nullptr) {
        error = "this client tunnel is already started";
        return false;
    }
    if (!runtime.ready()) {
        error = "the Steam runtime is not ready";
        return false;
    }
    if (config.host_steam_id == 0U && config.direct_connect_port == 0U) {
        error = "the host's Steam id is required";
        return false;
    }
    auto impl = std::make_unique<Impl>();
    impl->runtime = runtime.impl();
    impl->config = config;
    impl->socket = open_loopback_socket(0U, impl->local_port);
    if (impl->socket == invalid_socket) {
        error = "no loopback socket was available for the Steam tunnel";
        return false;
    }
    auto& api = impl->runtime->api;
    const auto option = status_callback_option();
    if (config.direct_connect_port != 0U) {
        SteamNetworkingIPAddr address{};
        address.SetIPv4(0x7F000001U, config.direct_connect_port);
        impl->connection = api.connect_ip(api.sockets(), address, 1, &option);
    } else {
        SteamNetworkingIdentity identity{};
        identity.SetSteamID64(config.host_steam_id);
        impl->connection = api.connect_p2p(api.sockets(), identity, config.virtual_port, 1, &option);
    }
    if (impl->connection == k_HSteamNetConnection_Invalid) {
        close_socket(impl->socket);
        error = "Steam refused the peer-to-peer connection";
        return false;
    }
    impl->status_token = StatusRouter::instance().add(
        [raw = impl.get()](const SteamNetConnectionStatusChangedCallback_t& event) {
            raw->on_status(event);
        });
    impl->service_token = impl->runtime->add_service([raw = impl.get()] { raw->service(); });
    impl->running.store(true);
    impl_ = std::move(impl);
    core::diagnostic("steam", "joining " + std::to_string(config.host_steam_id) +
                                  " over the relay network on loopback port " +
                                  std::to_string(impl_->local_port));
    return true;
}

void SteamP2PClient::stop() noexcept {
    if (impl_ == nullptr) return;
    impl_->running.store(false);
    impl_->runtime->remove_service(impl_->service_token);
    StatusRouter::instance().remove(impl_->status_token);
    auto& api = impl_->runtime->api;
    if (impl_->connection != k_HSteamNetConnection_Invalid) {
        static_cast<void>(
            api.close_connection(api.sockets(), impl_->connection, 0, "client closed", false));
    }
    close_socket(impl_->socket);
    impl_.reset();
}

bool SteamP2PClient::running() const noexcept {
    return impl_ != nullptr && impl_->running.load();
}

std::uint16_t SteamP2PClient::local_port() const noexcept {
    return impl_ == nullptr ? 0U : impl_->local_port;
}

int SteamP2PClient::ping_milliseconds() const noexcept {
    return impl_ == nullptr ? -1 : impl_->ping.load();
}

std::string SteamP2PClient::last_error() const {
    if (impl_ == nullptr) return {};
    const std::scoped_lock lock{impl_->mutex};
    return impl_->error;
}

} // namespace battlespades::platform
