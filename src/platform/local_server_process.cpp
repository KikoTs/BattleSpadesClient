#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <crt_externs.h>
#include <spawn.h>
#else
extern char** environ;
#endif
#endif

#include "battlespades/platform/local_server_process.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <fstream>
#include <limits>
#include <locale>
#include <set>
#include <sstream>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>

namespace battlespades::platform {
namespace {

constexpr auto graceful_shutdown_timeout{std::chrono::seconds{5}};
constexpr auto forced_shutdown_timeout{std::chrono::seconds{2}};
constexpr std::size_t maximum_rule_count{256U};
constexpr std::size_t maximum_text_bytes{512U};
std::atomic<std::uint64_t> session_counter{};

[[nodiscard]] constexpr std::string_view
server_executable_name(LocalServerProgram program) noexcept {
#if defined(_WIN32)
    return program == LocalServerProgram::map_creator
               ? std::string_view{"BattleSpadesMapCreator.exe"}
               : std::string_view{"BattleSpades.exe"};
#else
    return program == LocalServerProgram::map_creator
               ? std::string_view{"BattleSpadesMapCreator"}
               : std::string_view{"BattleSpades"};
#endif
}

[[nodiscard]] bool safe_text(std::string_view value) noexcept {
    return !value.empty() && value.size() <= maximum_text_bytes &&
           std::ranges::none_of(value, [](unsigned char character) {
               return character < 0x20U && character != '\t';
           });
}

[[nodiscard]] bool safe_mode(std::string_view value) noexcept {
    constexpr std::array modes{
        std::string_view{"tdm"}, std::string_view{"ctf"}, std::string_view{"cctf"},
        std::string_view{"vip"}, std::string_view{"zom"}, std::string_view{"mh"},
        std::string_view{"tc"}, std::string_view{"dia"}, std::string_view{"dem"},
        std::string_view{"oc"}, std::string_view{"ugc"},
    };
    return std::ranges::find(modes, value) != modes.end();
}

[[nodiscard]] bool safe_terrain(std::string_view value) noexcept {
    constexpr std::array terrains{
        std::string_view{"desert"}, std::string_view{"lunar"},
        std::string_view{"mountain"}, std::string_view{"grassland"},
        std::string_view{"temple"}, std::string_view{"urban"},
        std::string_view{"marsh"}, std::string_view{"snowy"},
        std::string_view{"water"},
    };
    return std::ranges::find(terrains, value) != terrains.end();
}

[[nodiscard]] bool safe_map_creator_mode(std::string_view value) noexcept {
    constexpr std::array modes{
        std::string_view{"zom"}, std::string_view{"tdm"},
        std::string_view{"dia"}, std::string_view{"oc"},
        std::string_view{"dem"}, std::string_view{"mh"},
        std::string_view{"vip"}, std::string_view{"ctf"},
        std::string_view{"tc"},
    };
    return std::ranges::find(modes, value) != modes.end();
}

[[nodiscard]] bool safe_path(const std::filesystem::path& value) noexcept {
    const auto text = value.generic_string();
    return !value.empty() && text.size() <= 4'096U && text.find('\0') == std::string::npos;
}

[[nodiscard]] bool safe_rule_key(std::string_view value) noexcept {
    return value.starts_with("RULE_") && value.size() <= 96U &&
           std::ranges::all_of(value, [](unsigned char character) {
               return (character >= 'A' && character <= 'Z') ||
                      (character >= '0' && character <= '9') || character == '_';
           });
}

[[nodiscard]] bool safe_environment_override(std::string_view name,
                                             std::string_view value) noexcept {
    constexpr std::array allowed{
        std::string_view{"AOS_MASTER_URL"},
        std::string_view{"AOS_MASTER_WRITE_TOKEN"},
        std::string_view{"AOS_PUBLIC_HOST"},
        std::string_view{"AOS_PUBLIC_PORT"},
        std::string_view{"AOS_PUBLIC_QUERY_PORT"},
        std::string_view{"AOS_SERVER_ID"},
        std::string_view{"AOS_UGC_OWNER_ID"},
        std::string_view{"AOS_RELAY_LOBBY_ID"},
        std::string_view{"AOS_MATCH_RESULTS_DIRECTORY"},
    };
    return std::ranges::find(allowed, name) != allowed.end() &&
           !value.empty() && value.size() <= 2'048U &&
           std::ranges::none_of(value, [](unsigned char character) {
               return character == 0U || character < 0x20U;
           });
}

[[nodiscard]] std::string toml_quote(std::string_view value) {
    std::string output{"\""};
    output.reserve(value.size() + 2U);
    constexpr char hex[]{"0123456789ABCDEF"};
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        switch (character) {
        case '\\': output += "\\\\"; break;
        case '"': output += "\\\""; break;
        case '\b': output += "\\b"; break;
        case '\t': output += "\\t"; break;
        case '\n': output += "\\n"; break;
        case '\f': output += "\\f"; break;
        case '\r': output += "\\r"; break;
        default:
            if (byte < 0x20U || byte == 0x7FU) {
                output += "\\u00";
                output.push_back(hex[(byte >> 4U) & 0x0FU]);
                output.push_back(hex[byte & 0x0FU]);
            } else {
                output.push_back(character);
            }
            break;
        }
    }
    output.push_back('"');
    return output;
}

[[nodiscard]] std::filesystem::path default_session_parent() {
    std::error_code error;
    auto root = std::filesystem::temp_directory_path(error);
    if (error) root = std::filesystem::current_path(error);
    return root / "BattleSpadesClient" / "local-servers";
}

[[nodiscard]] std::filesystem::path make_session_directory(
    const std::filesystem::path& parent, std::string& error) {
    std::error_code code;
    std::filesystem::create_directories(parent, code);
    if (code) {
        error = "cannot create the local-server session root: " + code.message();
        return {};
    }
    for (std::uint32_t attempt{}; attempt < 64U; ++attempt) {
        const auto clock = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto sequence = session_counter.fetch_add(1U, std::memory_order_relaxed);
        const auto candidate =
            parent / ("session-" + std::to_string(clock) + "-" + std::to_string(sequence));
        if (std::filesystem::create_directory(candidate, code)) return candidate;
        if (code && code != std::errc::file_exists) {
            error = "cannot create a private local-server session: " + code.message();
            return {};
        }
        code.clear();
    }
    error = "cannot allocate a unique local-server session directory";
    return {};
}

#if defined(_WIN32)
class SocketRuntime final {
public:
    SocketRuntime() {
        WSADATA data{};
        ready_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }
    ~SocketRuntime() {
        if (ready_) WSACleanup();
    }
    [[nodiscard]] bool ready() const noexcept { return ready_; }

private:
    bool ready_{};
};
#endif

[[nodiscard]] bool udp_port_available(std::uint16_t port) noexcept {
#if defined(_WIN32)
    static SocketRuntime sockets;
    if (!sockets.ready()) return false;
    const auto handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (handle == INVALID_SOCKET) return false;
    BOOL exclusive{TRUE};
    static_cast<void>(setsockopt(handle, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                                 reinterpret_cast<const char*>(&exclusive),
                                 static_cast<int>(sizeof(exclusive))));
#else
    const auto handle = socket(AF_INET, SOCK_DGRAM, 0);
    if (handle < 0) return false;
#endif
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);
#if defined(_WIN32)
    const auto address_size = static_cast<int>(sizeof(address));
#else
    const auto address_size = static_cast<socklen_t>(sizeof(address));
#endif
    const auto bound = bind(handle, reinterpret_cast<const sockaddr*>(&address),
                            address_size) == 0;
#if defined(_WIN32)
    closesocket(handle);
#else
    close(handle);
#endif
    return bound;
}

#if defined(_WIN32)
[[nodiscard]] std::wstring quote_windows_argument(std::wstring_view value) {
    if (value.find_first_of(L" \t\"") == std::wstring_view::npos) {
        return std::wstring{value};
    }
    std::wstring output{L"\""};
    std::size_t backslashes{};
    for (const auto character : value) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        if (character == L'"') {
            output.append(backslashes * 2U + 1U, L'\\');
            output.push_back(L'"');
            backslashes = 0U;
            continue;
        }
        output.append(backslashes, L'\\');
        backslashes = 0U;
        output.push_back(character);
    }
    output.append(backslashes * 2U, L'\\');
    output.push_back(L'"');
    return output;
}

[[nodiscard]] std::optional<std::wstring> utf8_to_wide(std::string_view value) {
    if (value.empty()) return std::wstring{};
    const auto count = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        nullptr, 0);
    if (count <= 0) return std::nullopt;
    std::wstring output(static_cast<std::size_t>(count), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), output.data(), count) != count) {
        return std::nullopt;
    }
    return output;
}

[[nodiscard]] std::optional<std::vector<wchar_t>> child_environment_block(
    const std::map<std::string, std::string, std::less<>>& overrides) {
    std::vector<std::wstring> entries;
    auto* source = GetEnvironmentStringsW();
    if (source == nullptr) return std::nullopt;
    for (auto* cursor = source; *cursor != L'\0';) {
        std::wstring entry{cursor};
        cursor += entry.size() + 1U;
        entries.push_back(std::move(entry));
    }
    FreeEnvironmentStringsW(source);
    for (const auto& [name_utf8, value_utf8] : overrides) {
        const auto name = utf8_to_wide(name_utf8);
        const auto value = utf8_to_wide(value_utf8);
        if (!name.has_value() || !value.has_value()) return std::nullopt;
        std::erase_if(entries, [&](const std::wstring& entry) {
            return entry.size() > name->size() && entry[name->size()] == L'=' &&
                   _wcsnicmp(entry.data(), name->data(), name->size()) == 0;
        });
        entries.push_back(*name + L"=" + *value);
    }
    std::ranges::sort(entries, [](const std::wstring& left, const std::wstring& right) {
        return _wcsicmp(left.c_str(), right.c_str()) < 0;
    });
    std::size_t size{1U};
    for (const auto& entry : entries) size += entry.size() + 1U;
    std::vector<wchar_t> block;
    block.reserve(size);
    for (const auto& entry : entries) {
        block.insert(block.end(), entry.begin(), entry.end());
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}
#else
[[nodiscard]] std::vector<std::string> child_environment_entries(
    const std::map<std::string, std::string, std::less<>>& overrides) {
    std::vector<std::string> entries;
#if defined(__APPLE__)
    auto** source = *_NSGetEnviron();
#else
    auto** source = environ;
#endif
    for (auto** cursor = source; cursor != nullptr && *cursor != nullptr; ++cursor) {
        const std::string_view entry{*cursor};
        if (!overrides.contains(entry.substr(0U, entry.find('=')))) {
            entries.emplace_back(entry);
        }
    }
    for (const auto& [name, value] : overrides) entries.push_back(name + '=' + value);
    return entries;
}

// Keep redirected descriptors separate from stdin/stdout/stderr even when a
// GUI launcher left one of those closed, and do not leak them through exec.
[[nodiscard]] bool prepare_child_descriptor(int& descriptor) noexcept {
    if (descriptor > STDERR_FILENO) return fcntl(descriptor, F_SETFD, FD_CLOEXEC) == 0;
    const auto duplicate = fcntl(descriptor, F_DUPFD_CLOEXEC, STDERR_FILENO + 1);
    if (duplicate < 0) return false;
    close(descriptor);
    descriptor = duplicate;
    return true;
}

enum class ChildProcessState { running, exited, unowned };

[[nodiscard]] ChildProcessState observe_child(pid_t process) noexcept {
    siginfo_t information{};
    int result{};
    do {
        result = waitid(P_PID, static_cast<id_t>(process), &information, WEXITED | WNOHANG | WNOWAIT);
    } while (result < 0 && errno == EINTR);
    if (result < 0) return ChildProcessState::unowned;
    // Preserve the waitable leader until cleanup: its reserved PID prevents
    // the owned process-group ID from being reused by an unrelated process.
    return information.si_pid == 0 ? ChildProcessState::running : ChildProcessState::exited;
}

#if defined(__APPLE__)
[[nodiscard]] int spawn_local_server(pid_t& child, const char* executable,
                                      const char* directory, int input, int log,
                                      char* const* arguments, char* const* environment) {
    // Forking a running Cocoa/Metal client and calling allocator-backed libc
    // functions in the child can deadlock on locks held by another thread.
    posix_spawn_file_actions_t actions;
    auto result = posix_spawn_file_actions_init(&actions);
    if (result != 0) return result;
    posix_spawnattr_t attributes;
    result = posix_spawnattr_init(&attributes);
    if (result != 0) {
        posix_spawn_file_actions_destroy(&actions);
        return result;
    }
    if ((result = posix_spawn_file_actions_addchdir_np(&actions, directory)) == 0 &&
        (result = posix_spawn_file_actions_adddup2(&actions, input, STDIN_FILENO)) == 0 &&
        (result = posix_spawn_file_actions_adddup2(&actions, log, STDOUT_FILENO)) == 0 &&
        (result = posix_spawn_file_actions_adddup2(&actions, log, STDERR_FILENO)) == 0 &&
        (result = posix_spawn_file_actions_addclose(&actions, input)) == 0 &&
        (result = posix_spawn_file_actions_addclose(&actions, log)) == 0 &&
        (result = posix_spawnattr_setpgroup(&attributes, 0)) == 0 &&
        (result = posix_spawnattr_setflags(
             &attributes, POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_CLOEXEC_DEFAULT)) == 0) {
        result = posix_spawn(&child, executable, &actions, &attributes, arguments, environment);
    }
    posix_spawnattr_destroy(&attributes);
    posix_spawn_file_actions_destroy(&actions);
    return result;
}
#endif
#endif

} // namespace

struct LocalServerProcess::Impl final {
    std::filesystem::path session_directory;
    std::filesystem::path log_path;
    std::uint16_t port{};
    std::string mode;
#if defined(_WIN32)
    HANDLE process{};
    HANDLE stdin_write{};
    HANDLE job{};
#else
    pid_t process{-1};
    int stdin_write{-1};
#endif
};

LocalServerState read_local_server_status(const std::filesystem::path& directory,
                                          std::uint16_t port, std::string_view mode) noexcept {
    try {
        std::error_code error;
        const auto path = directory / "host-status.json";
        const auto size = std::filesystem::file_size(path, error);
        if (error || size == 0U || size > 4096U) return LocalServerState::unavailable;
        std::ifstream input{path, std::ios::binary};
        std::string bytes(static_cast<std::size_t>(size), '\0');
        if (!input.read(bytes.data(), static_cast<std::streamsize>(size))) return LocalServerState::unavailable;
        const auto data = nlohmann::json::parse(bytes);
        if (data.at("schema_version") != 1 || data.at("session") != directory.filename().string() ||
            data.at("port") != port || data.at("mode").get<std::string>() != mode) return LocalServerState::unavailable;
        const auto state = data.at("state").get<std::string>();
        if (state == "starting") return LocalServerState::starting;
        if (state == "ready") return LocalServerState::ready;
        if (state == "stopping") return LocalServerState::stopping;
        if (state == "stopped") return LocalServerState::stopped;
        if (state == "failed") return LocalServerState::failed;
    } catch (const std::exception&) { /* Legacy servers and partial/foreign files are not readiness. */ }
    return LocalServerState::unavailable;
}

LocalServerState LocalServerProcess::status() const noexcept {
    return impl_ ? read_local_server_status(impl_->session_directory, impl_->port, impl_->mode)
                 : LocalServerState::unavailable;
}

std::optional<std::filesystem::path>
find_local_server_bundle(const std::filesystem::path& root) {
    const auto executable = server_executable_name(LocalServerProgram::game_server);
    const auto complete = [&](const std::filesystem::path& path) {
        std::error_code error;
        return std::filesystem::is_regular_file(path / executable, error) &&
               std::filesystem::is_directory(path / "_internal", error) &&
               std::filesystem::is_directory(path / "maps", error);
    };
    if (complete(root)) return root;
    if (complete(root / "server")) return root / "server";
    std::optional<std::filesystem::path> best;
    std::filesystem::file_time_type best_time{};
    const auto consider = [&](const std::filesystem::path& path) {
        if (!complete(path)) return;
        std::error_code error;
        const auto built = std::filesystem::last_write_time(path / executable, error);
        if (!error && (!best || built > best_time)) {
            best = path;
            best_time = built;
        }
    };
    consider(root / "dist" / "BattleSpades");
    std::error_code error;
    const auto options = std::filesystem::directory_options::skip_permission_denied;
    for (const auto& release : std::filesystem::directory_iterator(root, options, error)) {
        if (error) break;
        const auto name = release.path().filename().string();
        if (!release.is_directory(error) ||
            (!name.starts_with("release-dist") && !name.starts_with("local-release"))) {
            error.clear();
            continue;
        }
        for (const auto& candidate :
             std::filesystem::directory_iterator(release.path(), options, error)) {
            if (error) break;
            consider(candidate.path());
        }
        error.clear();
    }
    return best;
}

std::uint16_t allocate_local_server_port(std::uint16_t preferred,
                                         std::string& error) noexcept {
    error.clear();
    if (preferred == 0U) {
        error = "preferred local-server port must be between 1 and 65535";
        return 0U;
    }
    for (std::uint32_t offset{}; offset < 65'535U; ++offset) {
        const auto candidate =
            static_cast<std::uint16_t>(((static_cast<std::uint32_t>(preferred) - 1U + offset) %
                                        65'535U) +
                                       1U);
        if (udp_port_available(candidate)) return candidate;
    }
    error = "no local UDP port is available";
    return 0U;
}

std::string build_local_server_toml(const LocalServerLaunchConfig& config,
                                    std::uint16_t resolved_port) {
    if (resolved_port == 0U || !safe_text(config.server_name) ||
        !safe_text(config.map_name) || !safe_mode(config.mode) ||
        config.maximum_players < 2U || config.maximum_players > 24U ||
        config.match_minutes == 0U || config.match_minutes > 90U ||
        config.bot_count >= config.maximum_players ||
        config.rule_overrides.size() > maximum_rule_count) {
        return {};
    }
    if (config.program == LocalServerProgram::map_creator) {
        if (!config.map_creator.has_value() || config.mode != "ugc" || config.bot_count != 0U) {
            return {};
        }
        const auto& editor = *config.map_creator;
        if (!safe_text(editor.project) || !safe_terrain(editor.terrain) ||
            !safe_map_creator_mode(editor.target_mode) || !safe_text(editor.title) ||
            !safe_text(editor.author) || !safe_path(editor.publish_root) ||
            !safe_path(editor.retail_root) || (editor.prefab_set && *editor.prefab_set > 5U)) {
            return {};
        }
    } else if (config.map_creator.has_value()) {
        return {};
    }
    if (config.bot_difficulty != "casual" && config.bot_difficulty != "normal" &&
        config.bot_difficulty != "hard" && config.bot_difficulty != "mixed") {
        return {};
    }
    for (const auto& [key, value] : config.rule_overrides) {
        if (!safe_rule_key(key) || !safe_text(value)) return {};
    }
    for (const auto& [name, value] : config.environment_overrides) {
        if (!safe_environment_override(name, value)) return {};
    }
    constexpr std::array public_environment{
        std::string_view{"AOS_MASTER_URL"},
        std::string_view{"AOS_MASTER_WRITE_TOKEN"},
        std::string_view{"AOS_PUBLIC_HOST"},
        std::string_view{"AOS_PUBLIC_PORT"},
        std::string_view{"AOS_PUBLIC_QUERY_PORT"},
        std::string_view{"AOS_SERVER_ID"},
    };
    const auto public_match = std::ranges::all_of(public_environment, [&](auto name) {
        return config.environment_overrides.contains(name);
    });
    // A partial public identity would make the child advertise a local port,
    // reject AoSPlay tickets, or publish a listing that nobody can reach.
    if (!config.environment_overrides.empty() && !public_match) return {};

    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << "# Disposable client-owned local match. Safe to delete.\n\n"
           << "[server]\n"
           << "name = " << toml_quote(config.server_name) << '\n'
           << "port = " << resolved_port << '\n'
           << "max_players = " << config.maximum_players << '\n'
           << "tick_rate = 60\n\n"
           << "[network]\n"
           << "max_connections = " << config.maximum_players << '\n'
           << "timeout_ms = 10000\n\n"
           << "[game]\n"
           << "default_mode = " << toml_quote(config.mode) << '\n'
           << "default_map = " << toml_quote(config.map_name) << '\n'
           << "bot_count = " << config.bot_count << '\n'
           << "movement_authority = \"server\"\n"
           << "map_sync_mode = \"full\"\n\n"
           << "[lobby]\n"
           << "map_rotation = [" << toml_quote(config.map_name) << "]\n"
           << "match_length_minutes = " << config.match_minutes << '\n'
           << "end_screen_seconds = 12.0\n\n"
           << "[bots]\n"
           << "enabled = " << (config.bot_count == 0U ? "false" : "true") << '\n'
           << "population_mode = \"fixed\"\n"
           << "fill_target = " << config.bot_count << '\n'
           << "max_bots = " << config.bot_count << '\n'
           << "reserve_human_slots = 1\n"
           << "difficulty = " << toml_quote(config.bot_difficulty) << '\n'
           << "worker = \"process\"\n\n"
           << "[steam]\n"
           << "enabled = false\n"
           << "public = false\n"
           << "require_registration = false\n\n"
           << "[revival]\n"
           << "enabled = " << (public_match ? "true" : "false") << '\n'
           << "require_identity = " << (public_match ? "true" : "false") << "\n\n"
           << "[plugins]\n"
           << "enabled = false\n\n"
           << "[logging]\n"
           << "level = \"INFO\"\n"
           << "file = \"server.log\"\n"
           << "console = false\n"
           << "packet_trace = false\n";
    if (config.map_creator.has_value()) {
        const auto& editor = *config.map_creator;
        output << "\n[map_creator]\n"
               << "project = " << toml_quote(editor.project) << '\n'
               << "publish_root = " << toml_quote(editor.publish_root.generic_string()) << '\n'
               << "terrain = " << toml_quote(editor.terrain) << '\n'
               << "target_mode = " << toml_quote(editor.target_mode) << '\n'
               << "title = " << toml_quote(editor.title) << '\n'
               << "author = " << toml_quote(editor.author) << '\n'
               << "retail_root = " << toml_quote(editor.retail_root.generic_string()) << '\n';
        if (editor.prefab_set) output << "prefab_set = " << static_cast<unsigned>(*editor.prefab_set) << '\n';
    }
    if (!config.rule_overrides.empty()) {
        output << "\n[game_rules]\n";
        for (const auto& [key, value] : config.rule_overrides) {
            output << key << " = " << toml_quote(value) << '\n';
        }
    }
    return output.str();
}

LocalServerProcess::LocalServerProcess() : impl_{std::make_unique<Impl>()} {}

LocalServerProcess::~LocalServerProcess() {
    stop();
}

LocalServerProcess::LocalServerProcess(LocalServerProcess&&) noexcept = default;
LocalServerProcess& LocalServerProcess::operator=(LocalServerProcess&& other) noexcept {
    if (this != &other) {
        stop();
        impl_ = std::move(other.impl_);
    }
    return *this;
}

bool LocalServerProcess::start(const LocalServerLaunchConfig& config,
                               std::string& error) {
    error.clear();
    if (!impl_) impl_ = std::make_unique<Impl>();
    if (running()) {
        error = "a local server process is already running";
        return false;
    }
    // A previous child may have exited between launches. Release its control
    // channel, retained process ownership and session before replacing them.
    stop();
    // Resolve before changing the child's working directory. A relative bundle
    // must not become bundle/bundle/BattleSpades at exec time.
    const auto bundle_root = std::filesystem::absolute(config.bundle_root);
    const auto executable = bundle_root / server_executable_name(config.program);
    if (!std::filesystem::is_regular_file(executable)) {
        error = std::string{server_executable_name(config.program)} +
                " is missing from the local server bundle";
        return false;
    }
    if (!std::filesystem::is_directory(config.bundle_root / "_internal") ||
        !std::filesystem::is_directory(config.bundle_root / "maps")) {
        error = "the local server bundle is incomplete";
        return false;
    }
    const auto resolved_port = allocate_local_server_port(config.preferred_port, error);
    if (resolved_port == 0U) return false;
    const auto payload = build_local_server_toml(config, resolved_port);
    if (payload.empty()) {
        error = "Create Match produced an invalid local-server configuration";
        return false;
    }

    const auto parent =
        config.session_parent.empty() ? default_session_parent() : config.session_parent;
    const auto directory = make_session_directory(std::filesystem::absolute(parent), error);
    if (directory.empty()) return false;
    const auto config_path = directory / "config.toml";
    const auto temporary_path = directory / "config.toml.tmp";
    {
        std::ofstream stream{temporary_path, std::ios::binary | std::ios::trunc};
        stream.write(payload.data(), static_cast<std::streamsize>(payload.size()));
        stream.flush();
        if (!stream) {
            error = "cannot write the disposable local-server configuration";
            std::error_code ignored;
            std::filesystem::remove_all(directory, ignored);
            return false;
        }
    }
    std::error_code filesystem_error;
    std::filesystem::rename(temporary_path, config_path, filesystem_error);
    if (filesystem_error) {
        error = "cannot publish the disposable local-server configuration: " +
                filesystem_error.message();
        std::filesystem::remove_all(directory, filesystem_error);
        return false;
    }

    const auto log_path = directory / "server-bootstrap.log";
    auto child_overrides = config.environment_overrides;
    const auto status_path_utf8 = (directory / "host-status.json").u8string();
    child_overrides["AOS_NATIVE_HOST_STATUS"] = std::string{status_path_utf8.begin(), status_path_utf8.end()};
    child_overrides["AOS_NATIVE_HOST_SESSION"] = directory.filename().string();
#if defined(_WIN32)
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE stdin_read{};
    HANDLE stdin_write{};
    if (!CreatePipe(&stdin_read, &stdin_write, &security, 0U)) {
        error = "cannot create the local-server control pipe";
        std::filesystem::remove_all(directory, filesystem_error);
        return false;
    }
    static_cast<void>(SetHandleInformation(stdin_write, HANDLE_FLAG_INHERIT, 0U));
    const auto log = CreateFileW(log_path.c_str(), FILE_APPEND_DATA,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                                 OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE) {
        CloseHandle(stdin_read);
        CloseHandle(stdin_write);
        error = "cannot create the local-server bootstrap log";
        std::filesystem::remove_all(directory, filesystem_error);
        return false;
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    startup.wShowWindow = SW_HIDE;
    startup.hStdInput = stdin_read;
    startup.hStdOutput = log;
    startup.hStdError = log;
    auto command = quote_windows_argument(executable.wstring()) + L" --config " +
                   quote_windows_argument(config_path.wstring()) + L" --control-stdin";
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(L'\0');
    const auto environment = child_environment_block(child_overrides);
    if (!environment.has_value()) {
        CloseHandle(stdin_read);
        CloseHandle(stdin_write);
        CloseHandle(log);
        error = "cannot create the private local-server environment";
        std::filesystem::remove_all(directory, filesystem_error);
        return false;
    }
    PROCESS_INFORMATION process{};
    const auto created = CreateProcessW(
        executable.c_str(), mutable_command.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP | CREATE_UNICODE_ENVIRONMENT | CREATE_SUSPENDED,
        const_cast<wchar_t*>(environment->data()),
        bundle_root.c_str(), &startup, &process);
    const auto launch_error = created ? ERROR_SUCCESS : GetLastError();
    CloseHandle(stdin_read);
    CloseHandle(log);
    if (!created) {
        CloseHandle(stdin_write);
        error = "cannot launch the hidden BattleSpades server (Windows error " +
                std::to_string(launch_error) + ')';
        std::filesystem::remove_all(directory, filesystem_error);
        return false;
    }
    // Own the complete process tree before any server code can spawn helpers.
    // Assigning an already-running child leaves an unavoidable escape window.
    const auto job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (job == nullptr || !SetInformationJobObject(job, JobObjectExtendedLimitInformation,
                                                  &limits, sizeof(limits)) ||
        !AssignProcessToJobObject(job, process.hProcess) || ResumeThread(process.hThread) == DWORD(-1)) {
        const auto ownership_error = GetLastError();
        static_cast<void>(TerminateProcess(process.hProcess, 1U));
        static_cast<void>(WaitForSingleObject(process.hProcess, 2'000U));
        if (job != nullptr) CloseHandle(job);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        CloseHandle(stdin_write);
        error = "cannot establish ownership of the local server process tree (Windows error " +
                std::to_string(ownership_error) + ')';
        std::filesystem::remove_all(directory, filesystem_error);
        return false;
    }
    CloseHandle(process.hThread);
    impl_->job = job;
    impl_->process = process.hProcess;
    impl_->stdin_write = stdin_write;
#else
    std::array<int, 2U> control{};
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, control.data()) != 0) {
        error = "cannot create the local-server control pipe";
        std::filesystem::remove_all(directory, filesystem_error);
        return false;
    }
    auto log = open(log_path.c_str(), O_CREAT | O_WRONLY | O_APPEND, 0600);
    if (log < 0) {
        close(control[0U]);
        close(control[1U]);
        error = "cannot create the local-server bootstrap log";
        std::filesystem::remove_all(directory, filesystem_error);
        return false;
    }
    auto control_ready = prepare_child_descriptor(control[0U]) &&
                         prepare_child_descriptor(control[1U]) &&
                         prepare_child_descriptor(log);
#if defined(__APPLE__)
    const int no_sigpipe{1};
    control_ready = control_ready &&
                    setsockopt(control[1U], SOL_SOCKET, SO_NOSIGPIPE,
                               &no_sigpipe, sizeof(no_sigpipe)) == 0;
#endif
    if (!control_ready) {
        close(control[0U]);
        close(control[1U]);
        close(log);
        error = "cannot configure the local-server control channel";
        std::filesystem::remove_all(directory, filesystem_error);
        return false;
    }
    auto environment_entries = child_environment_entries(child_overrides);
    std::vector<char*> environment;
    environment.reserve(environment_entries.size() + 1U);
    for (auto& entry : environment_entries) environment.push_back(entry.data());
    environment.push_back(nullptr);
    std::array<std::string, 4U> argument_entries{
        executable.string(), "--config", config_path.string(), "--control-stdin"};
    std::array<char*, 5U> arguments{};
    for (std::size_t index{}; index < argument_entries.size(); ++index) {
        arguments[index] = argument_entries[index].data();
    }
    pid_t child{-1};
#if defined(__APPLE__)
    const auto launch_error = spawn_local_server(child, executable.c_str(), bundle_root.c_str(),
                                                  control[0U], log, arguments.data(), environment.data());
#else
    child = fork();
    const auto launch_error = child < 0 ? errno : 0;
    if (child == 0) {
        if (setsid() < 0 || chdir(bundle_root.c_str()) != 0 ||
            dup2(control[0U], STDIN_FILENO) < 0 ||
            dup2(log, STDOUT_FILENO) < 0 || dup2(log, STDERR_FILENO) < 0) _exit(126);
        close(control[0U]);
        close(control[1U]);
        close(log);
        execve(executable.c_str(), arguments.data(), environment.data());
        _exit(127);
    }
#endif
    close(control[0U]);
    close(log);
    if (launch_error != 0) {
        close(control[1U]);
        error = "cannot launch the hidden BattleSpades server (POSIX error " +
                std::to_string(launch_error) + ')';
        std::filesystem::remove_all(directory, filesystem_error);
        return false;
    }
    impl_->process = child;
    impl_->stdin_write = control[1U];
#endif
    impl_->session_directory = directory;
    impl_->log_path = log_path;
    impl_->port = resolved_port;
    impl_->mode = config.mode;
    return true;
}

void LocalServerProcess::stop() noexcept {
    if (impl_ == nullptr) return;
#if defined(_WIN32)
    if (impl_->process != nullptr) {
        DWORD code{};
        if (GetExitCodeProcess(impl_->process, &code) && code == STILL_ACTIVE) {
            if (impl_->stdin_write != nullptr) {
                constexpr std::array shutdown{char{'s'}, char{'h'}, char{'u'}, char{'t'},
                                               char{'d'}, char{'o'}, char{'w'}, char{'n'},
                                               char{'\n'}};
                DWORD written{};
                static_cast<void>(WriteFile(impl_->stdin_write, shutdown.data(),
                                            static_cast<DWORD>(shutdown.size()), &written,
                                            nullptr));
            }
            if (WaitForSingleObject(
                    impl_->process,
                    static_cast<DWORD>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                           graceful_shutdown_timeout)
                                           .count())) == WAIT_TIMEOUT) {
                static_cast<void>(TerminateProcess(impl_->process, 1U));
                static_cast<void>(WaitForSingleObject(
                    impl_->process,
                    static_cast<DWORD>(
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            forced_shutdown_timeout)
                            .count())));
            }
        }
        CloseHandle(impl_->process);
        impl_->process = nullptr;
    }
    if (impl_->stdin_write != nullptr) {
        CloseHandle(impl_->stdin_write);
        impl_->stdin_write = nullptr;
    }
    if (impl_->job != nullptr) {
        CloseHandle(impl_->job);
        impl_->job = nullptr;
    }
#else
    if (impl_->process > 0) {
        if (impl_->stdin_write >= 0) {
            constexpr std::string_view shutdown{"shutdown\n"};
#if defined(__APPLE__)
            constexpr int send_flags{}; // SO_NOSIGPIPE is set on this socket only.
#else
            constexpr int send_flags{MSG_NOSIGNAL};
#endif
            // The server may close stdin or exit between running() and stop().
            // Its broken control channel must never raise SIGPIPE in the client.
            static_cast<void>(send(impl_->stdin_write, shutdown.data(), shutdown.size(), send_flags));
        }
        auto deadline = std::chrono::steady_clock::now() + graceful_shutdown_timeout;
        auto state = observe_child(impl_->process);
        while (std::chrono::steady_clock::now() < deadline &&
               state == ChildProcessState::running) {
            std::this_thread::sleep_for(std::chrono::milliseconds{25});
            state = observe_child(impl_->process);
        }
        if (state == ChildProcessState::running) {
            static_cast<void>(kill(-impl_->process, SIGTERM));
            deadline = std::chrono::steady_clock::now() + forced_shutdown_timeout;
            while (std::chrono::steady_clock::now() < deadline &&
                   state == ChildProcessState::running) {
                std::this_thread::sleep_for(std::chrono::milliseconds{25});
                state = observe_child(impl_->process);
            }
        }
        if (state != ChildProcessState::unowned) {
            // Also collect helpers left behind by an early leader exit. The
            // leader is still waitable, so this group cannot be a reused ID.
            static_cast<void>(kill(-impl_->process, SIGKILL));
            int status{};
            while (waitpid(impl_->process, &status, 0) < 0 && errno == EINTR) {}
        }
        impl_->process = -1;
    }
    if (impl_->stdin_write >= 0) {
        close(impl_->stdin_write);
        impl_->stdin_write = -1;
    }
#endif
    impl_->port = 0U;
    if (!impl_->session_directory.empty()) {
        std::error_code ignored;
        std::filesystem::remove_all(impl_->session_directory, ignored);
    }
    impl_->session_directory.clear();
    impl_->log_path.clear();
}

bool LocalServerProcess::running() const noexcept {
    if (impl_ == nullptr) return false;
#if defined(_WIN32)
    if (impl_->process == nullptr) return false;
    DWORD code{};
    return GetExitCodeProcess(impl_->process, &code) != FALSE && code == STILL_ACTIVE;
#else
    if (impl_->process <= 0) return false;
    return observe_child(impl_->process) == ChildProcessState::running;
#endif
}

std::uint16_t LocalServerProcess::port() const noexcept {
    return impl_ == nullptr ? 0U : impl_->port;
}

const std::filesystem::path& LocalServerProcess::session_directory() const noexcept {
    static const std::filesystem::path empty;
    return impl_ == nullptr ? empty : impl_->session_directory;
}

const std::filesystem::path& LocalServerProcess::log_path() const noexcept {
    static const std::filesystem::path empty;
    return impl_ == nullptr ? empty : impl_->log_path;
}

} // namespace battlespades::platform
