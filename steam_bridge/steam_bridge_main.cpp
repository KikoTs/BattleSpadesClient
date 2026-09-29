#include <Windows.h>
#include <shellapi.h>

#include "battlespades/platform/steam_registration_windows.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

constexpr std::size_t maximum_command_bytes{8'192U};
constexpr std::size_t maximum_ticket_bytes{0x3F8U};

using SteamApiInit = bool(__cdecl*)();
using SteamApiShutdown = void(__cdecl*)();
using SteamApiRunCallbacks = void(__cdecl*)();
using SteamApiInterface = void*(__cdecl*)();
using SteamApiIsRunning = bool(__cdecl*)();

class CallbackBase;
using SteamApiRegisterCallback = void(__cdecl*)(CallbackBase*, int);
using SteamApiUnregisterCallback = void(__cdecl*)(CallbackBase*);

// k_iSteamFriendsCallbacks + 33 / + 37. The retail SteamFriends013 era SDK
// already posted both; their layouts have not changed since.
constexpr int game_lobby_join_requested{333};
constexpr int game_rich_presence_join_requested{337};
constexpr std::size_t rich_presence_value_bytes{256U};
constexpr std::size_t maximum_queued_events{8U};

struct SteamId final {
    std::uint64_t value{};
};

template <typename Function>
[[nodiscard]] Function virtual_function(void* object, std::size_t index) noexcept {
    if (object == nullptr) return nullptr;
    auto*** const instance = reinterpret_cast<void***>(object);
    if (instance == nullptr || *instance == nullptr) return nullptr;
    return reinterpret_cast<Function>((*instance)[index]);
}

[[nodiscard]] char hex_digit(std::uint8_t value) noexcept {
    return value < 10U ? static_cast<char>('0' + value)
                       : static_cast<char>('a' + value - 10U);
}

[[nodiscard]] std::optional<std::uint8_t> hex_value(char value) noexcept {
    if (value >= '0' && value <= '9') return static_cast<std::uint8_t>(value - '0');
    if (value >= 'a' && value <= 'f') return static_cast<std::uint8_t>(value - 'a' + 10);
    if (value >= 'A' && value <= 'F') return static_cast<std::uint8_t>(value - 'A' + 10);
    return std::nullopt;
}

[[nodiscard]] std::string hex_encode(std::span<const std::uint8_t> value) {
    std::string result;
    result.reserve(value.size() * 2U);
    for (const auto byte : value) {
        result.push_back(hex_digit(static_cast<std::uint8_t>(byte >> 4U)));
        result.push_back(hex_digit(static_cast<std::uint8_t>(byte & 0x0FU)));
    }
    return result;
}

[[nodiscard]] std::string hex_encode(std::string_view value) {
    return hex_encode(std::span{
        reinterpret_cast<const std::uint8_t*>(value.data()), value.size()});
}

[[nodiscard]] std::optional<std::string> hex_decode(std::string_view value) {
    if ((value.size() % 2U) != 0U || value.size() > maximum_command_bytes * 2U)
        return std::nullopt;
    std::string result(value.size() / 2U, '\0');
    for (std::size_t index{}; index < result.size(); ++index) {
        const auto high = hex_value(value[index * 2U]);
        const auto low = hex_value(value[index * 2U + 1U]);
        if (!high.has_value() || !low.has_value()) return std::nullopt;
        result[index] = static_cast<char>((*high << 4U) | *low);
    }
    return result;
}

[[nodiscard]] std::vector<std::string_view> split_fields(std::string_view value) {
    std::vector<std::string_view> result;
    std::size_t start{};
    while (start <= value.size()) {
        const auto delimiter = value.find('\t', start);
        if (delimiter == std::string_view::npos) {
            result.push_back(value.substr(start));
            break;
        }
        result.push_back(value.substr(start, delimiter - start));
        start = delimiter + 1U;
    }
    return result;
}

/**
 * The protocol pipe, captured before steam_api.dll is loaded.
 *
 * The retail DLL printf()s breakpad and minidump notices to whatever stdout
 * the process has. That stdout used to be the response pipe, so a stray
 * "Setting breakpad minidump AppID = 224540" could arrive where the client
 * expected READY and the Steam integration failed its handshake for the
 * whole session. The DLL now gets NUL, and only the bridge writes here.
 */
HANDLE protocol_output{INVALID_HANDLE_VALUE};

void response(std::string_view value) {
    std::string line{value};
    line.push_back('\n');
    DWORD written{};
    static_cast<void>(WriteFile(protocol_output,
                                line.data(),
                                static_cast<DWORD>(line.size()),
                                &written,
                                nullptr));
}

void error_response(std::string_view value) {
    response("ERROR\t" + hex_encode(value));
}

/**
 * Join requests Steam posted, held until the client asks for them.
 *
 * A client from before SUBSCRIBE existed reads exactly one line per command,
 * so an unsolicited line would be taken as the next command's answer. Events
 * are therefore only written after SUBSCRIBE; earlier ones (a Join clicked
 * while the bridge was still starting) wait here.
 */
std::deque<std::string> pending_events;
bool events_subscribed{};

void emit_event(std::string line) {
    if (events_subscribed) {
        response(line);
        return;
    }
    if (pending_events.size() >= maximum_queued_events) pending_events.pop_front();
    pending_events.push_back(std::move(line));
}

/**
 * Binary-compatible stand-in for the SDK's CCallbackBase.
 *
 * The bridge has no SDK headers for the 2013 DLL, so it declares the class the
 * way steam_api.h does: same virtual functions in the same order (MSVC groups
 * the two Run overloads exactly as it does for the SDK), then the flag byte and
 * the callback id that SteamAPI_RegisterCallback fills in.
 */
class CallbackBase {
public:
    CallbackBase() = default;
    CallbackBase(const CallbackBase&) = delete;
    CallbackBase& operator=(const CallbackBase&) = delete;

    virtual void Run(void* parameter) = 0;
    virtual void Run(void* parameter, bool io_failure, std::uint64_t call) = 0;
    virtual int GetCallbackSizeBytes() = 0;

    [[nodiscard]] bool registered() const noexcept { return (callback_flags & 0x01U) != 0U; }

protected:
    ~CallbackBase() = default;

private:
    std::uint8_t callback_flags{};
    int callback_id{};
};

/** GameRichPresenceJoinRequested_t (337) and GameLobbyJoinRequested_t (333). */
class JoinCallback final : public CallbackBase {
public:
    explicit JoinCallback(int kind) noexcept : kind_{kind} {}
    ~JoinCallback() = default;
    JoinCallback(const JoinCallback&) = delete;
    JoinCallback& operator=(const JoinCallback&) = delete;

    void Run(void* parameter) override { handle(parameter); }
    void Run(void* parameter, bool, std::uint64_t) override { handle(parameter); }
    int GetCallbackSizeBytes() override {
        return kind_ == game_rich_presence_join_requested
                   ? static_cast<int>(sizeof(std::uint64_t) + rich_presence_value_bytes)
                   : static_cast<int>(sizeof(std::uint64_t) * 2U);
    }

private:
    void handle(const void* parameter) const {
        if (parameter == nullptr) return;
        const auto* const bytes = static_cast<const char*>(parameter);
        if (kind_ == game_rich_presence_join_requested) {
            std::uint64_t friend_id{};
            std::memcpy(&friend_id, bytes, sizeof(friend_id));
            const auto* const connect = bytes + sizeof(friend_id);
            const auto length = strnlen(connect, rich_presence_value_bytes);
            emit_event("EVENT\tJOIN\t" + hex_encode(std::string_view{connect, length}) + "\t" +
                       std::to_string(friend_id));
        } else {
            std::uint64_t lobby{};
            std::uint64_t friend_id{};
            std::memcpy(&lobby, bytes, sizeof(lobby));
            std::memcpy(&friend_id, bytes + sizeof(lobby), sizeof(friend_id));
            if (lobby == 0U) return;
            emit_event("EVENT\tLOBBY\t" + std::to_string(lobby) + "\t" +
                       std::to_string(friend_id));
        }
    }

    int kind_;
};

struct Options final {
    std::filesystem::path library;
    std::uint32_t app_id{};
    std::string nonce;
};

[[nodiscard]] std::optional<Options> parse_options(int argc, wchar_t* argv[]) {
    Options result;
    for (int index = 1; index < argc; ++index) {
        const std::wstring_view argument{argv[index]};
        if ((argument != L"--steam-api" && argument != L"--app-id" &&
             argument != L"--nonce") || ++index >= argc) {
            return std::nullopt;
        }
        if (argument == L"--steam-api") {
            result.library = argv[index];
        } else if (argument == L"--app-id") {
            const std::wstring_view value{argv[index]};
            // The bridge is intentionally bound to the retail Battle Builder
            // AppID; never accept an arbitrary DLL/AppID pair from its caller.
            if (value != L"224540") return std::nullopt;
            result.app_id = 224540U;
        } else {
            const std::wstring_view value{argv[index]};
            if (value.empty() || value.size() > 128U ||
                !std::ranges::all_of(value, [](wchar_t character) {
                    return (character >= L'0' && character <= L'9') ||
                           (character >= L'a' && character <= L'f');
                })) {
                return std::nullopt;
            }
            result.nonce.clear();
            result.nonce.reserve(value.size());
            for (const auto character : value) {
                result.nonce.push_back(static_cast<char>(character));
            }
        }
    }
    if (result.library.empty() || result.app_id != 224540U || result.nonce.empty())
        return std::nullopt;
    return result;
}

struct SteamRuntime final {
    HMODULE module{};
    SteamApiShutdown shutdown{};
    SteamApiRunCallbacks callbacks{};
    void* user{};
    void* friends{};
    void* stats{};
    void* apps{};
    SteamId steam_id{};
    std::uint32_t ticket_handle{};
    SteamApiUnregisterCallback unregister_callback{};
    JoinCallback rich_presence_join{game_rich_presence_join_requested};
    JoinCallback lobby_join{game_lobby_join_requested};
    /**
     * Only a bridge that set presence clears it on exit. Presence belongs to
     * the account and application, not the process, so an unconditional
     * clear wiped what the game's own Steam runtime had published.
     */
    mutable bool presence_published{};
    /** Set when the failure is permanent: a missing or unusable DLL. */
    bool permanent_failure{};

    SteamRuntime() = default;
    SteamRuntime(const SteamRuntime&) = delete;
    SteamRuntime& operator=(const SteamRuntime&) = delete;

    ~SteamRuntime() {
        cancel_ticket();
        if (unregister_callback != nullptr) {
            if (rich_presence_join.registered()) unregister_callback(&rich_presence_join);
            if (lobby_join.registered()) unregister_callback(&lobby_join);
        }
        if (friends != nullptr && presence_published) {
            using ClearPresence = void(__thiscall*)(void*);
            if (const auto clear = virtual_function<ClearPresence>(friends, 37U);
                clear != nullptr) {
                clear(friends);
            }
        }
        if (shutdown != nullptr) shutdown();
        if (module != nullptr) FreeLibrary(module);
    }

    [[nodiscard]] bool initialize(const std::filesystem::path& path,
                                  std::string& error) {
        // Steam takes the application from SteamAppId, then from a
        // steam_appid.txt in the working directory. Launched by Steam the
        // environment carries it; launched directly (a shortcut to aos.exe,
        // a developer build) nothing did, since the imported steam/win32
        // directory has no steam_appid.txt, and SteamAPI_Init failed. Set it
        // before the DLL loads so its C runtime sees it too.
        static_cast<void>(SetEnvironmentVariableW(L"SteamAppId", L"224540"));
        static_cast<void>(SetEnvironmentVariableW(L"SteamGameId", L"224540"));
        module = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (module == nullptr) {
            error = "cannot load the original steam_api.dll";
            permanent_failure = true;
            return false;
        }
        const auto is_running = reinterpret_cast<SteamApiIsRunning>(
            GetProcAddress(module, "SteamAPI_IsSteamRunning"));
        const auto register_callback = reinterpret_cast<SteamApiRegisterCallback>(
            GetProcAddress(module, "SteamAPI_RegisterCallback"));
        unregister_callback = reinterpret_cast<SteamApiUnregisterCallback>(
            GetProcAddress(module, "SteamAPI_UnregisterCallback"));
        if (is_running != nullptr && !is_running()) {
            const auto diagnosis = battlespades::platform::diagnose_steam_registration();
            error = diagnosis.empty() ? std::string{"Steam is not running; start Steam and sign in"}
                                      : diagnosis;
            return false;
        }
        const auto init = reinterpret_cast<SteamApiInit>(
            GetProcAddress(module, "SteamAPI_Init"));
        shutdown = reinterpret_cast<SteamApiShutdown>(
            GetProcAddress(module, "SteamAPI_Shutdown"));
        callbacks = reinterpret_cast<SteamApiRunCallbacks>(
            GetProcAddress(module, "SteamAPI_RunCallbacks"));
        const auto get_user = reinterpret_cast<SteamApiInterface>(
            GetProcAddress(module, "SteamUser"));
        const auto get_friends = reinterpret_cast<SteamApiInterface>(
            GetProcAddress(module, "SteamFriends"));
        const auto get_stats = reinterpret_cast<SteamApiInterface>(
            GetProcAddress(module, "SteamUserStats"));
        const auto get_apps = reinterpret_cast<SteamApiInterface>(
            GetProcAddress(module, "SteamApps"));
        if (init == nullptr || shutdown == nullptr || callbacks == nullptr ||
            get_user == nullptr || get_friends == nullptr || get_stats == nullptr ||
            get_apps == nullptr) {
            error = "the imported steam_api.dll is not the retail Steamworks runtime";
            permanent_failure = true;
            shutdown = nullptr;
            unregister_callback = nullptr;
            return false;
        }
        if (!init()) {
            // Only a successful init may be paired with SteamAPI_Shutdown.
            shutdown = nullptr;
            const auto diagnosis = battlespades::platform::diagnose_steam_registration();
            error = diagnosis.empty()
                        ? std::string{"SteamAPI_Init failed; sign in to Steam with an account "
                                      "that owns Ace of Spades (224540)"}
                        : "SteamAPI_Init failed: " + diagnosis;
            return false;
        }
        user = get_user();
        friends = get_friends();
        stats = get_stats();
        apps = get_apps();
        if (user == nullptr || friends == nullptr || stats == nullptr || apps == nullptr) {
            error = "Steam returned an incomplete client interface set";
            return false;
        }
        using LoggedOn = bool(__thiscall*)(void*);
        const auto logged_on = virtual_function<LoggedOn>(user, 1U);
        if (logged_on == nullptr || !logged_on(user)) {
            error = "Steam user is not logged on";
            return false;
        }
        // The legacy MSVC x86 ABI returns CSteamID through a hidden output
        // pointer. Declaring an 8-byte by-value return corrupts the stack.
        using GetSteamId = void*(__thiscall*)(void*, SteamId*);
        const auto get_id = virtual_function<GetSteamId>(user, 2U);
        if (get_id == nullptr) {
            error = "SteamUser016::GetSteamID is unavailable";
            return false;
        }
        static_cast<void>(get_id(user, &steam_id));
        if (steam_id.value == 0U) {
            error = "Steam returned an invalid account identity";
            return false;
        }
        using RequestStats = bool(__thiscall*)(void*);
        if (const auto request = virtual_function<RequestStats>(stats, 0U);
            request != nullptr) {
            static_cast<void>(request(stats));
        }
        // Registered before the first RunCallbacks: a Join clicked in the
        // friends list while the game was starting is already queued in
        // Steam, and an unregistered callback is simply dropped.
        if (register_callback != nullptr) {
            register_callback(&rich_presence_join, game_rich_presence_join_requested);
            register_callback(&lobby_join, game_lobby_join_requested);
        }
        callbacks();
        return true;
    }

    [[nodiscard]] std::string persona_name() const {
        using GetPersonaName = const char*(__thiscall*)(void*);
        const auto get_name = virtual_function<GetPersonaName>(friends, 0U);
        const char* value = get_name == nullptr ? nullptr : get_name(friends);
        return value == nullptr || *value == '\0' ? std::string{} : std::string{value};
    }

    [[nodiscard]] std::string language() const {
        using GetLanguage = const char*(__thiscall*)(void*);
        const auto get_language = virtual_function<GetLanguage>(apps, 4U);
        const char* value = get_language == nullptr ? nullptr : get_language(apps);
        return value == nullptr ? std::string{} : std::string{value};
    }

    void cancel_ticket() noexcept {
        if (ticket_handle == 0U || user == nullptr) return;
        using CancelTicket = void(__thiscall*)(void*, std::uint32_t);
        if (const auto cancel = virtual_function<CancelTicket>(user, 16U);
            cancel != nullptr) {
            cancel(user, ticket_handle);
        }
        ticket_handle = 0U;
    }

    [[nodiscard]] std::optional<std::string> create_ticket() {
        cancel_ticket();
        std::array<std::uint8_t, maximum_ticket_bytes> ticket{};
        std::uint32_t size{};
        using GetTicket = std::uint32_t(__thiscall*)(
            void*, void*, std::int32_t, std::uint32_t*);
        const auto get_ticket = virtual_function<GetTicket>(user, 13U);
        if (get_ticket == nullptr) return std::nullopt;
        ticket_handle = get_ticket(user,
                                   ticket.data(),
                                   static_cast<std::int32_t>(ticket.size()),
                                   &size);
        if (ticket_handle == 0U || size == 0U || size > ticket.size()) {
            cancel_ticket();
            return std::nullopt;
        }
        std::vector<std::uint8_t> wire(sizeof(steam_id.value) + size);
        std::memcpy(wire.data(), &steam_id.value, sizeof(steam_id.value));
        std::memcpy(wire.data() + sizeof(steam_id.value), ticket.data(), size);
        return hex_encode(wire);
    }

    [[nodiscard]] bool set_presence(std::string_view connect,
                                    std::string_view status) const {
        using SetPresence = bool(__thiscall*)(void*, const char*, const char*);
        const auto set = virtual_function<SetPresence>(friends, 36U);
        if (set == nullptr) return false;
        const std::string connect_value{connect};
        const std::string status_value{status};
        presence_published = true;
        return set(friends,
                   "connect",
                   connect_value.empty() ? nullptr : connect_value.c_str()) &&
               set(friends, "status", status_value.c_str());
    }

    /** One rich presence key; an empty value deletes it. */
    [[nodiscard]] bool set_presence_key(std::string_view key, std::string_view value) const {
        using SetPresence = bool(__thiscall*)(void*, const char*, const char*);
        const auto set = virtual_function<SetPresence>(friends, 36U);
        if (set == nullptr) return false;
        const std::string key_value{key};
        const std::string text{value};
        presence_published = true;
        return set(friends, key_value.c_str(), text.empty() ? nullptr : text.c_str());
    }

    [[nodiscard]] bool clear_presence() const {
        using ClearPresence = void(__thiscall*)(void*);
        const auto clear = virtual_function<ClearPresence>(friends, 37U);
        if (clear == nullptr) return false;
        clear(friends);
        presence_published = false;
        return true;
    }

    [[nodiscard]] bool increment_stat(std::string_view name, std::int32_t amount) const {
        using GetStat = bool(__thiscall*)(void*, const char*, std::int32_t*);
        using SetStat = bool(__thiscall*)(void*, const char*, std::int32_t);
        using StoreStats = bool(__thiscall*)(void*);
        const auto get = virtual_function<GetStat>(stats, 1U);
        const auto set = virtual_function<SetStat>(stats, 3U);
        const auto store = virtual_function<StoreStats>(stats, 10U);
        if (get == nullptr || set == nullptr || store == nullptr) return false;
        const std::string stat_name{name};
        std::int32_t current{};
        if (!get(stats, stat_name.c_str(), &current) ||
            amount > std::numeric_limits<std::int32_t>::max() - current ||
            !set(stats, stat_name.c_str(), current + amount)) {
            return false;
        }
        return store(stats);
    }
};

[[nodiscard]] bool dispatch(SteamRuntime& steam,
                            std::string_view command,
                            bool& quit) {
    const auto fields = split_fields(command);
    if (fields.empty()) return false;
    if (fields[0U] == "QUIT" && fields.size() == 1U) {
        response("OK");
        quit = true;
        return true;
    }
    if (fields[0U] == "TICKET" && fields.size() == 1U) {
        const auto ticket = steam.create_ticket();
        if (!ticket.has_value()) {
            error_response("SteamUser016::GetAuthSessionTicket failed");
        } else {
            response("TICKET\t" + std::to_string(steam.ticket_handle) + "\t" + *ticket);
        }
        return true;
    }
    if (fields[0U] == "CANCEL" && fields.size() == 2U) {
        std::uint32_t handle{};
        const auto parsed = std::from_chars(fields[1U].data(),
                                            fields[1U].data() + fields[1U].size(),
                                            handle);
        if (parsed.ec != std::errc{} ||
            parsed.ptr != fields[1U].data() + fields[1U].size() ||
            handle == 0U || handle != steam.ticket_handle) {
            error_response("invalid Steam ticket cancellation handle");
        } else {
            steam.cancel_ticket();
            response("OK");
        }
        return true;
    }
    if (fields[0U] == "PRESENCE" && fields.size() == 3U) {
        const auto connect = hex_decode(fields[1U]);
        const auto status = hex_decode(fields[2U]);
        if (!connect.has_value() || !status.has_value() || status->empty() ||
            !steam.set_presence(*connect, *status)) {
            error_response("SteamFriends013::SetRichPresence failed");
        } else {
            response("OK");
        }
        return true;
    }
    if (fields[0U] == "PRESENCE_KEY" && fields.size() == 3U) {
        // Only the keys Steam itself interprets, so a compromised caller
        // cannot use the bridge to publish arbitrary account data.
        const auto key = hex_decode(fields[1U]);
        const auto value = hex_decode(fields[2U]);
        const bool known = key.has_value() &&
                           (*key == "status" || *key == "connect" ||
                            *key == "steam_player_group" ||
                            *key == "steam_player_group_size");
        if (!known || !value.has_value() || value->size() >= rich_presence_value_bytes ||
            !steam.set_presence_key(*key, *value)) {
            error_response("SteamFriends013::SetRichPresence failed");
        } else {
            response("OK");
        }
        return true;
    }
    if (fields[0U] == "SUBSCRIBE" && fields.size() == 1U) {
        // The answer comes first so the client pairs it with this command;
        // queued events follow as ordinary event lines.
        response("OK");
        events_subscribed = true;
        while (!pending_events.empty()) {
            response(pending_events.front());
            pending_events.pop_front();
        }
        return true;
    }
    if (fields[0U] == "CLEAR_PRESENCE" && fields.size() == 1U) {
        if (steam.clear_presence()) response("OK");
        else error_response("SteamFriends013::ClearRichPresence failed");
        return true;
    }
    if (fields[0U] == "SHOW_ACHIEVEMENTS" && fields.size() == 1U) {
        const auto url = L"https://steamcommunity.com/profiles/" +
                         std::to_wstring(steam.steam_id.value) +
                         L"/stats/AceofSpades?tab=achievements";
        const auto opened = reinterpret_cast<std::intptr_t>(
            ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
        if (opened > 32) response("OK");
        else error_response("could not open Steam achievements");
        return true;
    }
    if (fields[0U] == "INCREMENT" && fields.size() == 3U) {
        const auto name = hex_decode(fields[1U]);
        std::int32_t amount{};
        const auto parsed = std::from_chars(fields[2U].data(),
                                            fields[2U].data() + fields[2U].size(),
                                            amount);
        if (!name.has_value() || name->empty() || name->size() > 128U ||
            parsed.ec != std::errc{} ||
            parsed.ptr != fields[2U].data() + fields[2U].size() || amount <= 0 ||
            !steam.increment_stat(*name, amount)) {
            error_response("SteamUserStats011 update failed");
        } else {
            response("OK");
        }
        return true;
    }
    return false;
}

} // namespace

int wmain(int argc, wchar_t* argv[]) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    // Keep the protocol pipe for ourselves and give everything else NUL
    // before steam_api.dll (and its own C runtime) loads; see response().
    protocol_output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (const HANDLE nul = CreateFileW(L"NUL",
                                       GENERIC_WRITE,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE,
                                       nullptr,
                                       OPEN_EXISTING,
                                       0U,
                                       nullptr);
        nul != INVALID_HANDLE_VALUE) {
        static_cast<void>(SetStdHandle(STD_OUTPUT_HANDLE, nul));
        static_cast<void>(SetStdHandle(STD_ERROR_HANDLE, nul));
    }
    const auto options = parse_options(argc, argv);
    if (!options.has_value()) return 2;

    SteamRuntime steam;
    std::string error;
    if (!steam.initialize(options->library, error)) {
        error_response(error);
        // 6: nothing will change by retrying (missing or foreign DLL).
        // 3: Steam is not ready yet; the client tries again later.
        return steam.permanent_failure ? 6 : 3;
    }
    const auto persona = steam.persona_name();
    if (persona.empty()) {
        error_response("Steam returned an empty persona name");
        return 4;
    }
    response("READY\t" + options->nonce + "\t" + std::to_string(steam.steam_id.value) +
             "\t" + hex_encode(persona) + "\t" + hex_encode(steam.language()));

    const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    std::string command;
    bool quit{};
    while (!quit) {
        steam.callbacks();
        DWORD available{};
        if (PeekNamedPipe(input, nullptr, 0U, nullptr, &available, nullptr) == FALSE) break;
        if (available == 0U) {
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
            continue;
        }
        char value{};
        DWORD read{};
        if (ReadFile(input, &value, 1U, &read, nullptr) == FALSE || read != 1U) break;
        if (value == '\n') {
            if (!dispatch(steam, command, quit)) error_response("unknown Steam bridge command");
            command.clear();
            continue;
        }
        if (value != '\r') command.push_back(value);
        if (command.size() >= maximum_command_bytes) {
            error_response("oversized Steam bridge command");
            return 5;
        }
    }
    return 0;
}
