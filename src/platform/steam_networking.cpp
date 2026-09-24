#include "battlespades/platform/steam_networking.hpp"

#include "battlespades/core/diagnostics.hpp"
#include "battlespades/platform/steam_achievements.generated.hpp"

#include <steam/steam_api.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
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

/**
 * Winsock is reference counted and the discovery and local-server code release
 * theirs, so the transport starts its own for the life of the process rather
 * than borrowing a count that may already have dropped to zero.
 */
[[nodiscard]] bool sockets_ready() noexcept {
    static const bool ready = [] {
#if defined(_WIN32)
        WSADATA data{};
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
        return true;
#endif
    }();
    return ready;
}

/** One loopback datagram socket, always bound to 127.0.0.1 with no route out. */
[[nodiscard]] Socket open_loopback_socket(std::uint16_t connect_port, std::uint16_t& bound_port) {
    if (!sockets_ready()) return invalid_socket;
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
    bool(S_CALLTYPE* set_rich_presence)(ISteamFriends*, const char*, const char*){};
    void(S_CALLTYPE* clear_rich_presence)(ISteamFriends*){};
    bool(S_CALLTYPE* dispatch_call_result)(HSteamPipe, SteamAPICall_t, void*, int, int, bool*){};
    ISteamMatchmaking*(S_CALLTYPE* matchmaking)(){};
    SteamAPICall_t(S_CALLTYPE* create_lobby)(ISteamMatchmaking*, ELobbyType, int){};
    bool(S_CALLTYPE* set_lobby_data)(ISteamMatchmaking*, uint64, const char*,
                                     const char*){};
    const char*(S_CALLTYPE* get_lobby_data)(ISteamMatchmaking*, uint64, const char*){};
    bool(S_CALLTYPE* set_lobby_joinable)(ISteamMatchmaking*, uint64, bool){};
    void(S_CALLTYPE* leave_lobby)(ISteamMatchmaking*, uint64){};
    SteamAPICall_t(S_CALLTYPE* request_lobby_list)(ISteamMatchmaking*){};
    uint64(S_CALLTYPE* lobby_by_index)(ISteamMatchmaking*, int){};
    void(S_CALLTYPE* filter_result_count)(ISteamMatchmaking*, int){};
    void(S_CALLTYPE* filter_distance)(ISteamMatchmaking*, ELobbyDistanceFilter){};
    int(S_CALLTYPE* lobby_members)(ISteamMatchmaking*, uint64){};
    int(S_CALLTYPE* friend_count)(ISteamFriends*, int){};
    uint64(S_CALLTYPE* friend_by_index)(ISteamFriends*, int, int){};
    const char*(S_CALLTYPE* friend_persona)(ISteamFriends*, uint64){};
    const char*(S_CALLTYPE* friend_presence)(ISteamFriends*, uint64, const char*){};
    ISteamUserStats*(S_CALLTYPE* user_stats)(){};
    bool(S_CALLTYPE* set_achievement)(ISteamUserStats*, const char*){};
    bool(S_CALLTYPE* get_achievement)(ISteamUserStats*, const char*, bool*){};
    bool(S_CALLTYPE* set_stat_int32)(ISteamUserStats*, const char*, int32){};
    bool(S_CALLTYPE* get_stat_int32)(ISteamUserStats*, const char*, int32*){};
    bool(S_CALLTYPE* indicate_progress)(ISteamUserStats*, const char*, uint32, uint32){};
    bool(S_CALLTYPE* store_stats)(ISteamUserStats*){};
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

/** Reads as a polled fact, so it holds even if no status callback arrives. */
[[nodiscard]] const char* connection_state_name(int state) noexcept {
    switch (state) {
        case k_ESteamNetworkingConnectionState_None:
            return "none";
        case k_ESteamNetworkingConnectionState_Connecting:
            return "connecting, waiting for the host to answer";
        case k_ESteamNetworkingConnectionState_FindingRoute:
            return "finding a route";
        case k_ESteamNetworkingConnectionState_Connected:
            return "connected";
        case k_ESteamNetworkingConnectionState_ClosedByPeer:
            return "closed by the host";
        case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
            return "a problem was detected locally";
        default:
            return "an unnamed state";
    }
}

/** Steam takes this process's application id from the environment at init. */
void announce_app_id(const std::string& app_id) noexcept {
#if defined(_WIN32)
    static_cast<void>(_putenv_s("SteamAppId", app_id.c_str()));
    static_cast<void>(_putenv_s("SteamGameId", app_id.c_str()));
#else
    static_cast<void>(setenv("SteamAppId", app_id.c_str(), 1));
    static_cast<void>(setenv("SteamGameId", app_id.c_str(), 1));
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
        Binding{"SteamAPI_ISteamFriends_SetRichPresence",
                reinterpret_cast<void**>(&api.set_rich_presence)},
        Binding{"SteamAPI_ISteamFriends_ClearRichPresence",
                reinterpret_cast<void**>(&api.clear_rich_presence)},
        Binding{"SteamAPI_ManualDispatch_GetAPICallResult",
                reinterpret_cast<void**>(&api.dispatch_call_result)},
        Binding{"SteamAPI_SteamMatchmaking_v009", reinterpret_cast<void**>(&api.matchmaking)},
        Binding{"SteamAPI_ISteamMatchmaking_CreateLobby",
                reinterpret_cast<void**>(&api.create_lobby)},
        Binding{"SteamAPI_ISteamMatchmaking_SetLobbyData",
                reinterpret_cast<void**>(&api.set_lobby_data)},
        Binding{"SteamAPI_ISteamMatchmaking_GetLobbyData",
                reinterpret_cast<void**>(&api.get_lobby_data)},
        Binding{"SteamAPI_ISteamMatchmaking_SetLobbyJoinable",
                reinterpret_cast<void**>(&api.set_lobby_joinable)},
        Binding{"SteamAPI_ISteamMatchmaking_LeaveLobby",
                reinterpret_cast<void**>(&api.leave_lobby)},
        Binding{"SteamAPI_ISteamMatchmaking_RequestLobbyList",
                reinterpret_cast<void**>(&api.request_lobby_list)},
        Binding{"SteamAPI_ISteamMatchmaking_GetLobbyByIndex",
                reinterpret_cast<void**>(&api.lobby_by_index)},
        Binding{"SteamAPI_ISteamMatchmaking_AddRequestLobbyListResultCountFilter",
                reinterpret_cast<void**>(&api.filter_result_count)},
        Binding{"SteamAPI_ISteamMatchmaking_AddRequestLobbyListDistanceFilter",
                reinterpret_cast<void**>(&api.filter_distance)},
        Binding{"SteamAPI_ISteamMatchmaking_GetNumLobbyMembers",
                reinterpret_cast<void**>(&api.lobby_members)},
        Binding{"SteamAPI_ISteamFriends_GetFriendCount",
                reinterpret_cast<void**>(&api.friend_count)},
        Binding{"SteamAPI_ISteamFriends_GetFriendByIndex",
                reinterpret_cast<void**>(&api.friend_by_index)},
        Binding{"SteamAPI_ISteamFriends_GetFriendPersonaName",
                reinterpret_cast<void**>(&api.friend_persona)},
        Binding{"SteamAPI_ISteamFriends_GetFriendRichPresence",
                reinterpret_cast<void**>(&api.friend_presence)},
        Binding{"SteamAPI_SteamUserStats_v013", reinterpret_cast<void**>(&api.user_stats)},
        Binding{"SteamAPI_ISteamUserStats_SetAchievement",
                reinterpret_cast<void**>(&api.set_achievement)},
        Binding{"SteamAPI_ISteamUserStats_GetAchievement",
                reinterpret_cast<void**>(&api.get_achievement)},
        Binding{"SteamAPI_ISteamUserStats_SetStatInt32",
                reinterpret_cast<void**>(&api.set_stat_int32)},
        Binding{"SteamAPI_ISteamUserStats_GetStatInt32",
                reinterpret_cast<void**>(&api.get_stat_int32)},
        Binding{"SteamAPI_ISteamUserStats_IndicateAchievementProgress",
                reinterpret_cast<void**>(&api.indicate_progress)},
        Binding{"SteamAPI_ISteamUserStats_StoreStats",
                reinterpret_cast<void**>(&api.store_stats)},
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

    /**
     * Runs the handlers with the lock held, so remove() cannot return while one
     * is still running.
     *
     * Every handler holds a raw pointer to a host or client implementation that
     * stop() destroys immediately after removing its token. Copying the
     * handlers and releasing the lock first let a teardown on the presentation
     * thread free that object while the pump thread was inside the copy, which
     * crashed the client on any failed join. No handler adds or removes one, so
     * holding the lock cannot deadlock.
     */
    void dispatch(const SteamNetConnectionStatusChangedCallback_t& event) {
        const std::scoped_lock lock{mutex_};
        for (const auto& entry : handlers_) entry.second(event);
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
/**
 * Steam allows 10 s by default for a connection to find its route. Two peers
 * that both have to set up relay sessions were seen accepted on the host at the
 * very moment the joiner gave up; give route finding half a minute.
 */
[[nodiscard]] SteamNetworkingConfigValue_t initial_timeout_option() {
    SteamNetworkingConfigValue_t option{};
    option.SetInt32(k_ESteamNetworkingConfig_TimeoutInitial, 30'000);
    return option;
}

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
    /** The id Steam accepted, which is the fallback when the first was refused. */
    std::atomic<std::uint32_t> attached_app_id{};
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
    /** Bytes of a completed asynchronous call, keyed by its handle. */
    std::mutex results_mutex;
    std::map<SteamAPICall_t, std::vector<unsigned char>> call_results;
    std::set<SteamAPICall_t> awaited_calls;

    void await_call(SteamAPICall_t call) {
        const std::scoped_lock lock{results_mutex};
        awaited_calls.insert(call);
    }

    /** Takes the result if it has landed; nullopt while the call is in flight. */
    [[nodiscard]] std::optional<std::vector<unsigned char>> take_call_result(SteamAPICall_t call) {
        const std::scoped_lock lock{results_mutex};
        const auto found = call_results.find(call);
        if (found == call_results.end()) return std::nullopt;
        auto bytes = std::move(found->second);
        call_results.erase(found);
        awaited_calls.erase(call);
        return bytes;
    }

    void dispatch_callbacks() {
        api.dispatch_run_frame(pipe);
        CallbackMsg_t message{};
        while (api.dispatch_next(pipe, &message)) {
            if (message.m_iCallback == SteamNetConnectionStatusChangedCallback_t::k_iCallback &&
                message.m_pubParam != nullptr) {
                StatusRouter::instance().dispatch(
                    *reinterpret_cast<SteamNetConnectionStatusChangedCallback_t*>(
                        message.m_pubParam));
            } else if (message.m_iCallback == SteamAPICallCompleted_t::k_iCallback &&
                       message.m_pubParam != nullptr) {
                // An asynchronous call answers here rather than through a
                // callback, and its bytes must be read before the message is
                // freed. Without this every Steam call that returns a handle,
                // such as creating a lobby, never completes.
                collect_call_result(
                    *reinterpret_cast<SteamAPICallCompleted_t*>(message.m_pubParam));
            }
            api.dispatch_free(pipe);
        }
    }

    void collect_call_result(const SteamAPICallCompleted_t& completed) {
        if (api.dispatch_call_result == nullptr) return;
        {
            const std::scoped_lock lock{results_mutex};
            if (!awaited_calls.contains(completed.m_hAsyncCall)) return;
        }
        std::vector<unsigned char> bytes(static_cast<std::size_t>(completed.m_cubParam));
        bool failed{};
        if (!api.dispatch_call_result(pipe, completed.m_hAsyncCall, bytes.data(),
                                      static_cast<int>(completed.m_cubParam),
                                      completed.m_iCallback, &failed) ||
            failed) {
            core::diagnostic("steam", "an asynchronous Steam call failed");
            const std::scoped_lock lock{results_mutex};
            awaited_calls.erase(completed.m_hAsyncCall);
            return;
        }
        const std::scoped_lock lock{results_mutex};
        call_results.emplace(completed.m_hAsyncCall, std::move(bytes));
    }

    void run(std::stop_token stop) {
        while (!stop.stop_requested()) {
            dispatch_callbacks();
            {
                // Held across the call for the same reason as the status
                // handlers: a service borrows a host or client implementation
                // that stop() destroys as soon as remove_service() returns.
                const std::scoped_lock lock{services_mutex};
                for (const auto& entry : services) entry.second();
            }
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

    auto app_id = std::to_string(impl->config.app_id);
    announce_app_id(app_id);
    SteamErrMsg message{};
    if (impl->api.init_flat(&message) != k_ESteamAPIInitResult_OK) {
        // Steam refuses an application id the account does not own. That is the
        // ordinary case for a player without Ace of Spades, who joins the
        // Spacewar network instead of being turned away.
        const std::string refusal{message[0] != '\0' ? message : "Steam is not running"};
        const auto fallback = impl->config.fallback_app_id;
        if (fallback == 0U || fallback == impl->config.app_id) {
            error = refusal;
            close_library(impl->api.handle);
            return false;
        }
        core::diagnostic("steam", "app " + app_id + " refused (" + refusal + "), using " +
                                      std::to_string(fallback));
        app_id = std::to_string(fallback);
        announce_app_id(app_id);
        message[0] = '\0';
        if (impl->api.init_flat(&message) != k_ESteamAPIInitResult_OK) {
            error = message[0] != '\0' ? message : "Steam is not running";
            close_library(impl->api.handle);
            return false;
        }
    }
    impl->api.manual_dispatch_init();
    impl->pipe = impl->api.steam_pipe();
    impl->attached_app_id.store(static_cast<std::uint32_t>(std::stoul(app_id)));
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

std::uint32_t SteamNetworkingRuntime::app_id() const noexcept {
    return impl_ == nullptr ? 0U : impl_->attached_app_id.load();
}

bool SteamNetworkingRuntime::tracking_enabled() const noexcept {
    if (impl_ == nullptr) return false;
    const auto attached = impl_->attached_app_id.load();
    // Spacewar's statistics and achievements belong to Valve's test app, so
    // reporting ours against it would write into someone else's schema. A
    // player who does not own the game still plays; nothing is tracked.
    return attached != 0U && attached != impl_->config.fallback_app_id;
}

bool SteamNetworkingRuntime::publish_presence(const std::string& status,
                                              const std::string& connect) {
    if (impl_ == nullptr || impl_->api.set_rich_presence == nullptr) return false;
    auto* const friends = impl_->api.friends();
    if (friends == nullptr) return false;
    // "status" is what the friends list shows under view game info, and
    // "connect" is the command line Steam hands a friend who clicks Join.
    // Both are free-form, so neither needs anything defined for the app id.
    const auto published = impl_->api.set_rich_presence(friends, "status", status.c_str()) &&
                           impl_->api.set_rich_presence(friends, "connect", connect.c_str());
    core::diagnostic("steam", published ? "presence published: " + status
                                        : "presence was refused by Steam");
    return published;
}

void SteamNetworkingRuntime::clear_presence() noexcept {
    if (impl_ == nullptr || impl_->api.clear_rich_presence == nullptr) return;
    if (auto* const friends = impl_->api.friends(); friends != nullptr) {
        impl_->api.clear_rich_presence(friends);
    }
}

std::uint64_t SteamNetworkingRuntime::create_lobby(const std::string& status,
                                                  const std::string& connect,
                                                  int maximum_members,
                                                  std::chrono::seconds timeout) {
    if (impl_ == nullptr || impl_->api.create_lobby == nullptr) return 0U;
    auto* const matchmaking = impl_->api.matchmaking();
    if (matchmaking == nullptr) return 0U;
    // Friends-only: the match is reached through the friends list, and a public
    // lobby would advertise a player's machine to everyone running the app id.
    const auto call = impl_->api.create_lobby(matchmaking, k_ELobbyTypeFriendsOnly,
                                              maximum_members);
    if (call == 0) return 0U;
    impl_->await_call(call);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (const auto bytes = impl_->take_call_result(call); bytes.has_value()) {
            if (bytes->size() < sizeof(LobbyCreated_t)) return 0U;
            LobbyCreated_t created{};
            std::memcpy(&created, bytes->data(), sizeof(created));
            if (created.m_eResult != k_EResultOK) {
                core::diagnostic("steam", "Steam refused the lobby, result " +
                                              std::to_string(static_cast<int>(created.m_eResult)));
                return 0U;
            }
            const std::uint64_t lobby{created.m_ulSteamIDLobby};
            static_cast<void>(impl_->api.set_lobby_data(matchmaking, lobby, "status",
                                                        status.c_str()));
            static_cast<void>(impl_->api.set_lobby_data(matchmaking, lobby, "connect",
                                                        connect.c_str()));
            core::diagnostic("steam", "lobby " + std::to_string(lobby) + " open for friends: " +
                                          status);
            return lobby;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
    }
    core::diagnostic("steam", "Steam did not answer the lobby request in time");
    return 0U;
}

std::vector<SteamFriendMatch> SteamNetworkingRuntime::friend_matches() const {
    std::vector<SteamFriendMatch> matches;
    if (impl_ == nullptr || impl_->api.friend_count == nullptr) return matches;
    auto* const friends = impl_->api.friends();
    if (friends == nullptr) return matches;
    // k_EFriendFlagImmediate is the friends list proper, not blocked users or
    // people who merely share a chat.
    const auto count = impl_->api.friend_count(friends, k_EFriendFlagImmediate);
    for (int index{}; index < count; ++index) {
        const std::uint64_t id{impl_->api.friend_by_index(friends, index,
                                                          k_EFriendFlagImmediate)};
        if (id == 0U) continue;
        // A friend's "connect" value is what Steam would hand us if we clicked
        // Join on them, so its presence is what makes a match joinable. Steam
        // only serves rich presence for friends running the same application.
        const auto* const connect = impl_->api.friend_presence(friends, id, "connect");
        if (connect == nullptr || *connect == '\0') continue;
        SteamFriendMatch match;
        match.steam_id = id;
        match.connect = connect;
        if (const auto* const status = impl_->api.friend_presence(friends, id, "status");
            status != nullptr) {
            match.status = status;
        }
        if (const auto* const persona = impl_->api.friend_persona(friends, id);
            persona != nullptr) {
            match.persona = persona;
        }
        matches.push_back(std::move(match));
    }
    core::diagnostic("steam", "friends in a joinable match: " + std::to_string(matches.size()) +
                                  " of " + std::to_string(count));
    return matches;
}

std::vector<SteamLobbyListing> SteamNetworkingRuntime::list_lobbies(
    int maximum, std::chrono::seconds timeout) {
    std::vector<SteamLobbyListing> listings;
    if (impl_ == nullptr || impl_->api.request_lobby_list == nullptr) return listings;
    auto* const matchmaking = impl_->api.matchmaking();
    if (matchmaking == nullptr) return listings;
    impl_->api.filter_result_count(matchmaking, maximum);
    // Worldwide: the relays carry the traffic, so a distant host is reachable
    // and its ping is the relay's, not the raw distance.
    impl_->api.filter_distance(matchmaking, k_ELobbyDistanceFilterWorldwide);
    const auto call = impl_->api.request_lobby_list(matchmaking);
    if (call == 0) return listings;
    impl_->await_call(call);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (const auto bytes = impl_->take_call_result(call); bytes.has_value()) {
            if (bytes->size() < sizeof(LobbyMatchList_t)) return listings;
            LobbyMatchList_t matched{};
            std::memcpy(&matched, bytes->data(), sizeof(matched));
            const auto count = static_cast<int>(matched.m_nLobbiesMatching);
            for (int index{}; index < count; ++index) {
                const std::uint64_t lobby{impl_->api.lobby_by_index(matchmaking, index)};
                if (lobby == 0U) continue;
                SteamLobbyListing listing;
                listing.lobby_id = lobby;
                listing.status = lobby_data(lobby, "status");
                listing.connect = lobby_data(lobby, "connect");
                listing.members = impl_->api.lobby_members(matchmaking, lobby);
                // A lobby with no connect value is not one of ours to join.
                if (!listing.connect.empty()) listings.push_back(std::move(listing));
            }
            core::diagnostic("steam", "found " + std::to_string(listings.size()) +
                                          " joinable lobbies of " + std::to_string(count));
            return listings;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
    }
    core::diagnostic("steam", "Steam did not answer the lobby list in time");
    return listings;
}

std::string SteamNetworkingRuntime::lobby_data(std::uint64_t lobby,
                                               const std::string& key) const {
    if (impl_ == nullptr || impl_->api.get_lobby_data == nullptr || lobby == 0U) return {};
    auto* const matchmaking = impl_->api.matchmaking();
    if (matchmaking == nullptr) return {};
    const auto* const value = impl_->api.get_lobby_data(matchmaking, lobby, key.c_str());
    return value == nullptr ? std::string{} : std::string{value};
}

void SteamNetworkingRuntime::leave_lobby(std::uint64_t lobby) noexcept {
    if (impl_ == nullptr || impl_->api.leave_lobby == nullptr || lobby == 0U) return;
    if (auto* const matchmaking = impl_->api.matchmaking(); matchmaking != nullptr) {
        impl_->api.leave_lobby(matchmaking, lobby);
    }
}

bool SteamNetworkingRuntime::unlock_achievement(const std::string& name) {
    if (!tracking_enabled() || impl_->api.set_achievement == nullptr) return false;
    // The retail application defines the achievements, so a name outside its
    // catalogue can only be a mistake on our side. Saying so here beats a
    // silent refusal from Steam that looks identical to a network problem.
    if (!is_retail_achievement(name)) {
        core::diagnostic("steam", "refusing to unlock " + name +
                                      ", which the retail achievement list does not define");
        return false;
    }
    auto* const stats = impl_->api.user_stats();
    if (stats == nullptr) return false;
    // An achievement must exist in the attached application's schema, which
    // belongs to whoever owns that id. Steam refuses a name it does not know,
    // so a refusal here says the schema lacks it, not that the call is wrong.
    if (!impl_->api.set_achievement(stats, name.c_str())) {
        core::diagnostic("steam", "Steam does not know the achievement " + name);
        return false;
    }
    return store_statistics();
}

bool SteamNetworkingRuntime::report_achievement_progress(const std::string& name,
                                                        std::uint32_t progress,
                                                        std::uint32_t target) {
    if (!tracking_enabled() || impl_->api.indicate_progress == nullptr) return false;
    auto* const stats = impl_->api.user_stats();
    if (stats == nullptr || target == 0U || progress >= target) return false;
    return impl_->api.indicate_progress(stats, name.c_str(), progress, target);
}

bool SteamNetworkingRuntime::set_statistic(const std::string& name, std::int32_t value) {
    if (!tracking_enabled() || impl_->api.set_stat_int32 == nullptr) return false;
    auto* const stats = impl_->api.user_stats();
    return stats != nullptr && impl_->api.set_stat_int32(stats, name.c_str(), value);
}

std::optional<std::int32_t> SteamNetworkingRuntime::statistic(const std::string& name) const {
    if (!tracking_enabled() || impl_->api.get_stat_int32 == nullptr) return std::nullopt;
    auto* const stats = impl_->api.user_stats();
    if (stats == nullptr) return std::nullopt;
    int32 value{};
    if (!impl_->api.get_stat_int32(stats, name.c_str(), &value)) return std::nullopt;
    return static_cast<std::int32_t>(value);
}

bool SteamNetworkingRuntime::store_statistics() {
    if (!tracking_enabled() || impl_->api.store_stats == nullptr) return false;
    auto* const stats = impl_->api.user_stats();
    return stats != nullptr && impl_->api.store_stats(stats);
}

SteamRelayStatus SteamNetworkingRuntime::relay_status() const {
    if (impl_ == nullptr) return {};
    const std::scoped_lock lock{impl_->mutex};
    return SteamRelayStatus{impl_->relay_available.load(), impl_->relay_detail};
}

bool SteamNetworkingRuntime::wait_for_relays(std::chrono::seconds timeout) {
    if (impl_ == nullptr) return false;
    if (impl_->relay_available.load()) return true;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!impl_->relay_available.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
    }
    if (!impl_->relay_available.load()) {
        core::diagnostic("steam", "the relay network is still unavailable after waiting");
        return false;
    }
    core::diagnostic("steam", "relay network ready: " + relay_status().detail);
    return true;
}

std::string SteamNetworkingRuntime::last_error() const {
    if (impl_ == nullptr) return {};
    const std::scoped_lock lock{impl_->mutex};
    return impl_->error;
}

/**
 * First message a host sends a joiner, ahead of any game datagram: the
 * AoSPlay server id the joiner authorizes with. Sent reliably, unlike the
 * game traffic, and consumed by the joiner's tunnel rather than handed to the
 * game.
 */
constexpr std::string_view host_hello_magic{"BSP2P-HELLO\x01"};

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
        // The joiner logs every state; the host logged nothing after accept, so
        // a route that never formed and a tunnel that broke looked identical.
        if (event.m_info.m_eState == k_ESteamNetworkingConnectionState_FindingRoute) {
            core::diagnostic("steam", "finding a route to the joiner through the relays");
            return;
        }
        if (event.m_info.m_eState == k_ESteamNetworkingConnectionState_Connected) {
            core::diagnostic("steam", "relay connection established with the joiner");
            send_hello(event.m_hConn);
            return;
        }
        if (event.m_info.m_eState == k_ESteamNetworkingConnectionState_ClosedByPeer ||
            event.m_info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally) {
            core::diagnostic("steam", std::string{"joiner's relay connection ended: "} +
                                          event.m_info.m_szEndDebug + " (reason " +
                                          std::to_string(event.m_info.m_eEndReason) + ")");
            drop(event.m_hConn, "peer closed");
        }
    }

    /**
     * Sent once the route exists and before the joiner dials: the joiner's
     * tunnel holds its game back until this arrives, so the id always precedes
     * the first game datagram.
     */
    void send_hello(HSteamNetConnection connection) {
        auto& api = runtime->api;
        std::string hello{host_hello_magic};
        hello += config.server_identifier;
        const auto result =
            api.send_message(api.sockets(), connection, hello.data(),
                             static_cast<uint32>(hello.size()), k_nSteamNetworkingSend_Reliable, nullptr);
        if (result != k_EResultOK) {
            core::diagnostic("steam", "sending the hello to the joiner failed: result " +
                                          std::to_string(static_cast<int>(result)));
            return;
        }
        core::diagnostic("steam", config.server_identifier.empty()
                                      ? std::string{"told the joiner no identity is needed"}
                                      : "told the joiner to authorize for " + config.server_identifier);
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
        reap_idle_clients(api);
    }

    /**
     * Release a player Steam never reported as gone.
     *
     * last_seen was recorded on every datagram and read by nothing, and
     * client_idle_timeout was configuration that did nothing. A connection
     * Steam leaves half open therefore kept its loopback socket and its slot
     * for as long as the match ran, and enough of them turned a host with
     * nobody in it into one that answers "server full".
     *
     * Called with the lock already held, so it closes and erases in place
     * rather than going through drop(), which takes the lock itself.
     */
    void reap_idle_clients(SteamApi& api) {
        const auto now = std::chrono::steady_clock::now();
        const auto stale = std::ranges::remove_if(clients, [&](const Client& client) {
            if (now - client.last_seen < config.client_idle_timeout) return false;
            close_socket(client.socket);
            static_cast<void>(
                api.close_connection(api.sockets(), client.connection, 0, "idle", false));
            core::diagnostic("steam", "dropped a player silent for " +
                                          std::to_string(config.client_idle_timeout.count()) +
                                          " seconds");
            return true;
        });
        clients.erase(stale.begin(), stale.end());
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
    const std::array<SteamNetworkingConfigValue_t, 2> options{status_callback_option(),
                                                               initial_timeout_option()};
    if (config.direct_listen_port != 0U) {
        SteamNetworkingIPAddr address{};
        address.Clear();
        address.m_port = config.direct_listen_port;
        impl->listen = api.create_listen_ip(api.sockets(), address, 2, options.data());
    } else {
        impl->listen = api.create_listen_p2p(api.sockets(), config.virtual_port, 2, options.data());
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
                                  std::to_string(config.virtual_port) + ", relays " +
                                  (impl_->runtime->relay_available.load() ? "ready" : "unavailable"));
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
    std::atomic_bool connected{};
    std::atomic_int ping{-1};
    /** Guarded by ``mutex``: the host's hello, once it arrived. */
    bool hello_received{};
    std::string host_server_identifier;
    /** Last state seen by polling; -1 until the first poll succeeds. */
    std::atomic_int polled_state{-1};
    /** Only the service thread touches this, so it needs no lock. */
    std::chrono::steady_clock::time_point last_state_report{};
    std::string error;

    void on_status(const SteamNetConnectionStatusChangedCallback_t& event) {
        if (event.m_hConn != connection) return;
        // A connect that no host answers stays in these states until Steam's
        // own timeout, well past the Protocol 168 handshake. Without them the
        // log cannot tell a host that is not listening from a broken tunnel.
        if (event.m_info.m_eState == k_ESteamNetworkingConnectionState_FindingRoute) {
            core::diagnostic("steam", "finding a route to the host through the relays");
            return;
        }
        if (event.m_info.m_eState == k_ESteamNetworkingConnectionState_Connected) {
            connected.store(true);
            core::diagnostic("steam", "relay connection established to the host");
            return;
        }
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
            const std::string_view payload{static_cast<const char*>(message->m_pData),
                                           static_cast<std::size_t>(message->m_cbSize)};
            if (payload.starts_with(host_hello_magic)) {
                {
                    const std::scoped_lock lock{mutex};
                    host_server_identifier = std::string{payload.substr(host_hello_magic.size())};
                    hello_received = true;
                }
                api.release_message(message);
                continue;
            }
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
            // Polled, so it records the tunnel's progress even when no status
            // callback arrives: silence in the log otherwise cannot distinguish
            // a connection Steam never routes from callbacks never delivered.
            //
            // A connection still waiting repeats every few seconds, because a
            // state logged once cannot distinguish either of those from this
            // thread having stopped polling at all.
            const int state = status.m_eState;
            if (state == k_ESteamNetworkingConnectionState_Connected) connected.store(true);
            const auto now = std::chrono::steady_clock::now();
            const bool changed = state != polled_state.exchange(state);
            const bool waiting = state == k_ESteamNetworkingConnectionState_Connecting ||
                                 state == k_ESteamNetworkingConnectionState_FindingRoute;
            if (changed || (waiting && now - last_state_report >= std::chrono::seconds{5})) {
                last_state_report = now;
                core::diagnostic("steam", std::string{"tunnel state: "} +
                                              connection_state_name(state));
            }
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
    const std::array<SteamNetworkingConfigValue_t, 2> options{status_callback_option(),
                                                               initial_timeout_option()};
    if (config.direct_connect_port != 0U) {
        SteamNetworkingIPAddr address{};
        address.SetIPv4(0x7F000001U, config.direct_connect_port);
        impl->connection = api.connect_ip(api.sockets(), address, 2, options.data());
    } else {
        SteamNetworkingIdentity identity{};
        identity.SetSteamID64(config.host_steam_id);
        impl->connection =
            api.connect_p2p(api.sockets(), identity, config.virtual_port, 2, options.data());
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

bool SteamP2PClient::connected() const noexcept {
    return impl_ != nullptr && impl_->running.load() && impl_->connected.load();
}

std::optional<std::string> SteamP2PClient::wait_for_host_hello(std::chrono::seconds timeout) {
    if (impl_ == nullptr) return std::nullopt;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
        {
            const std::scoped_lock lock{impl_->mutex};
            if (impl_->hello_received) return impl_->host_server_identifier;
        }
        if (!impl_->running.load()) return std::nullopt;
        if (std::chrono::steady_clock::now() >= deadline) {
            const std::scoped_lock lock{impl_->mutex};
            impl_->error = impl_->connected.load()
                               ? "the host never said which server to authorize for"
                               : "the host did not answer through the relays";
            return std::nullopt;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
    }
}

std::string SteamP2PClient::last_error() const {
    if (impl_ == nullptr) return {};
    const std::scoped_lock lock{impl_->mutex};
    return impl_->error;
}

} // namespace battlespades::platform
