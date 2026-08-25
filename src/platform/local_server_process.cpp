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
#endif

} // namespace

struct LocalServerProcess::Impl final {
    std::filesystem::path session_directory;
    std::filesystem::path log_path;
    std::uint16_t port{};
#if defined(_WIN32)
    HANDLE process{};
    HANDLE stdin_write{};
    HANDLE job{};
#else
    mutable pid_t process{-1};
    int stdin_write{-1};
#endif
};

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
            !safe_path(editor.retail_root)) {
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
           << "enabled = false\n"
           << "require_identity = false\n\n"
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
LocalServerProcess& LocalServerProcess::operator=(LocalServerProcess&&) noexcept = default;

bool LocalServerProcess::start(const LocalServerLaunchConfig& config,
                               std::string& error) {
    error.clear();
    if (running()) {
        error = "a local server process is already running";
        return false;
    }
    const auto executable = config.bundle_root / server_executable_name(config.program);
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
    const auto directory = make_session_directory(parent, error);
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
    PROCESS_INFORMATION process{};
    const auto created = CreateProcessW(
        executable.c_str(), mutable_command.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP, nullptr,
        config.bundle_root.c_str(), &startup, &process);
    CloseHandle(stdin_read);
    CloseHandle(log);
    if (!created) {
        CloseHandle(stdin_write);
        error = "cannot launch the hidden BattleSpades server (Windows error " +
                std::to_string(GetLastError()) + ')';
        std::filesystem::remove_all(directory, filesystem_error);
        return false;
    }
    CloseHandle(process.hThread);
    const auto job = CreateJobObjectW(nullptr, nullptr);
    if (job != nullptr) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation,
                                     &limits, sizeof(limits)) ||
            !AssignProcessToJobObject(job, process.hProcess)) {
            CloseHandle(job);
            impl_->job = nullptr;
        } else {
            impl_->job = job;
        }
    }
    impl_->process = process.hProcess;
    impl_->stdin_write = stdin_write;
#else
    std::array<int, 2U> control{};
    if (pipe(control.data()) != 0) {
        error = "cannot create the local-server control pipe";
        std::filesystem::remove_all(directory, filesystem_error);
        return false;
    }
    const auto log = open(log_path.c_str(), O_CREAT | O_WRONLY | O_APPEND, 0600);
    if (log < 0) {
        close(control[0U]);
        close(control[1U]);
        error = "cannot create the local-server bootstrap log";
        std::filesystem::remove_all(directory, filesystem_error);
        return false;
    }
    const auto child = fork();
    if (child == 0) {
        static_cast<void>(setsid());
        static_cast<void>(chdir(config.bundle_root.c_str()));
        static_cast<void>(dup2(control[0U], STDIN_FILENO));
        static_cast<void>(dup2(log, STDOUT_FILENO));
        static_cast<void>(dup2(log, STDERR_FILENO));
        close(control[0U]);
        close(control[1U]);
        close(log);
        execl(executable.c_str(), executable.c_str(), "--config", config_path.c_str(),
              "--control-stdin", static_cast<char*>(nullptr));
        _exit(127);
    }
    close(control[0U]);
    close(log);
    if (child < 0) {
        close(control[1U]);
        error = "cannot launch the hidden BattleSpades server";
        std::filesystem::remove_all(directory, filesystem_error);
        return false;
    }
    impl_->process = child;
    impl_->stdin_write = control[1U];
#endif
    impl_->session_directory = directory;
    impl_->log_path = log_path;
    impl_->port = resolved_port;
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
            static_cast<void>(write(impl_->stdin_write, shutdown.data(), shutdown.size()));
        }
        auto deadline = std::chrono::steady_clock::now() + graceful_shutdown_timeout;
        int status{};
        while (std::chrono::steady_clock::now() < deadline &&
               waitpid(impl_->process, &status, WNOHANG) == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds{25});
        }
        if (waitpid(impl_->process, &status, WNOHANG) == 0) {
            static_cast<void>(kill(impl_->process, SIGTERM));
            deadline = std::chrono::steady_clock::now() + forced_shutdown_timeout;
            while (std::chrono::steady_clock::now() < deadline &&
                   waitpid(impl_->process, &status, WNOHANG) == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds{25});
            }
            if (waitpid(impl_->process, &status, WNOHANG) == 0) {
                static_cast<void>(kill(impl_->process, SIGKILL));
                static_cast<void>(waitpid(impl_->process, &status, 0));
            }
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
    int status{};
    const auto result = waitpid(impl_->process, &status, WNOHANG);
    if (result == impl_->process) {
        impl_->process = -1;
        return false;
    }
    return result == 0;
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
