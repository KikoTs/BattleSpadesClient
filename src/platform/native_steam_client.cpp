#include "battlespades/platform/native_steam_client.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cctype>
#include <deque>
#include <exception>
#include <limits>
#include <random>
#include <span>
#include <string>
#include <thread>
#include <utility>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

namespace battlespades::platform {
namespace {

#if defined(_WIN32)

constexpr std::size_t maximum_bridge_line{8'192U};
constexpr std::size_t maximum_ticket_bytes{2'048U};
constexpr std::size_t maximum_join_events{16U};
/** Bridge exit codes that no retry can change: bad arguments, unusable DLL. */
constexpr DWORD bridge_exit_bad_arguments{2U};
constexpr DWORD bridge_exit_unusable_runtime{6U};

[[nodiscard]] char hex_digit(std::uint8_t value) noexcept {
    return value < 10U ? static_cast<char>('0' + value)
                       : static_cast<char>('a' + (value - 10U));
}

[[nodiscard]] std::string hex_encode(std::string_view value) {
    std::string result;
    result.reserve(value.size() * 2U);
    for (const auto character : value) {
        const auto byte = static_cast<std::uint8_t>(character);
        result.push_back(hex_digit(static_cast<std::uint8_t>(byte >> 4U)));
        result.push_back(hex_digit(static_cast<std::uint8_t>(byte & 0x0FU)));
    }
    return result;
}

[[nodiscard]] std::optional<std::uint8_t> hex_value(char value) noexcept {
    if (value >= '0' && value <= '9') return static_cast<std::uint8_t>(value - '0');
    if (value >= 'a' && value <= 'f') return static_cast<std::uint8_t>(value - 'a' + 10);
    if (value >= 'A' && value <= 'F') return static_cast<std::uint8_t>(value - 'A' + 10);
    return std::nullopt;
}

[[nodiscard]] std::optional<std::string> hex_decode(std::string_view value) {
    if ((value.size() % 2U) != 0U || value.size() > maximum_bridge_line * 2U)
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

[[nodiscard]] std::vector<std::string_view> split_fields(std::string_view line) {
    std::vector<std::string_view> fields;
    std::size_t start{};
    while (start <= line.size()) {
        const auto delimiter = line.find('\t', start);
        if (delimiter == std::string_view::npos) {
            fields.push_back(line.substr(start));
            break;
        }
        fields.push_back(line.substr(start, delimiter - start));
        start = delimiter + 1U;
    }
    return fields;
}

[[nodiscard]] std::optional<std::uint64_t> parse_u64(std::string_view text) noexcept {
    std::uint64_t value{};
    const auto* const end = text.data() + text.size();
    const auto parsed = std::from_chars(text.data(), end, value);
    if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != end) return std::nullopt;
    return value;
}

/**
 * Lines the bridge protocol can produce. Anything else on the pipe is not
 * ours: the 2013 steam_api.dll printf()s breakpad notices to stdout, and an
 * older bridge handed it the response pipe as stdout.
 */
[[nodiscard]] bool protocol_response(std::string_view head) noexcept {
    return head == "READY" || head == "OK" || head == "ERROR" || head == "TICKET";
}

[[nodiscard]] std::string random_nonce() {
    std::random_device source;
    std::array<std::uint32_t, 4U> values{};
    for (auto& value : values) value = source();
    std::string result;
    result.reserve(values.size() * 8U);
    for (const auto value : values) {
        for (int shift = 28; shift >= 0; shift -= 4) {
            result.push_back(hex_digit(
                static_cast<std::uint8_t>((value >> static_cast<unsigned>(shift)) & 0xFU)));
        }
    }
    return result;
}

[[nodiscard]] std::wstring quote_windows_argument(const std::filesystem::path& value) {
    const auto source = value.wstring();
    std::wstring result{L"\""};
    std::size_t slashes{};
    for (const auto character : source) {
        if (character == L'\\') {
            ++slashes;
        } else if (character == L'\"') {
            result.append(slashes * 2U + 1U, L'\\');
            result.push_back(L'\"');
            slashes = 0U;
        } else {
            result.append(slashes, L'\\');
            slashes = 0U;
            result.push_back(character);
        }
    }
    result.append(slashes * 2U, L'\\');
    result.push_back(L'\"');
    return result;
}

[[nodiscard]] std::wstring quote_windows_argument(std::string_view value) {
    return quote_windows_argument(std::filesystem::path{
        std::wstring{value.begin(), value.end()}});
}

void close_handle(HANDLE& handle) noexcept {
    if (handle != nullptr && handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
    handle = nullptr;
}

#endif

} // namespace

struct NativeSteamClient::Impl final {
    explicit Impl(NativeSteamClientConfig source) : config{std::move(source)} {}

    NativeSteamClientConfig config;
    std::optional<NativeSteamIdentity> identity;
    std::string error;
    std::uint32_t active_ticket{};
    NativeSteamState state{NativeSteamState::unavailable};
    std::vector<NativeSteamJoinEvent> join_events;

#if defined(_WIN32)
    HANDLE process{};
    HANDLE input_write{};
    HANDLE output_read{};
    std::string nonce;
    std::string partial_line;
    std::deque<std::string> responses;
    std::chrono::steady_clock::time_point startup_deadline{};
    std::chrono::steady_clock::time_point retry_at{};

    [[nodiscard]] bool alive() const noexcept {
        if (process == nullptr) return false;
        DWORD code{};
        return GetExitCodeProcess(process, &code) != FALSE && code == STILL_ACTIVE;
    }

    [[nodiscard]] std::optional<DWORD> exit_code() const noexcept {
        if (process == nullptr) return std::nullopt;
        DWORD code{};
        if (GetExitCodeProcess(process, &code) == FALSE || code == STILL_ACTIVE)
            return std::nullopt;
        return code;
    }

    [[nodiscard]] bool write_line(std::string_view line) noexcept {
        if (!alive() || input_write == nullptr || line.size() >= maximum_bridge_line) {
            error = "Steam bridge is not running";
            return false;
        }
        std::string framed{line};
        framed.push_back('\n');
        DWORD written{};
        if (WriteFile(input_write,
                      framed.data(),
                      static_cast<DWORD>(framed.size()),
                      &written,
                      nullptr) == FALSE ||
            written != static_cast<DWORD>(framed.size())) {
            error = "Steam bridge command pipe failed";
            return false;
        }
        return true;
    }

    void accept_line(std::string line) {
        const auto fields = split_fields(line);
        if (fields.empty()) return;
        if (fields[0U] == "EVENT") {
            accept_event(fields);
            return;
        }
        if (protocol_response(fields[0U])) responses.push_back(std::move(line));
    }

    void accept_event(const std::vector<std::string_view>& fields) {
        NativeSteamJoinEvent event;
        if (fields.size() == 4U && fields[1U] == "JOIN") {
            auto connect = hex_decode(fields[2U]);
            if (!connect.has_value() || connect->empty()) return;
            event.connect = std::move(*connect);
        } else if (fields.size() == 4U && fields[1U] == "LOBBY") {
            const auto lobby = parse_u64(fields[2U]);
            if (!lobby.has_value() || *lobby == 0U) return;
            event.lobby_id = *lobby;
        } else {
            return;
        }
        event.friend_id = parse_u64(fields[3U]).value_or(0U);
        if (join_events.size() >= maximum_join_events) join_events.erase(join_events.begin());
        join_events.push_back(std::move(event));
    }

    /** Read whatever the bridge has written, without waiting. */
    [[nodiscard]] bool drain_output() noexcept {
        if (output_read == nullptr) return false;
        try {
            for (;;) {
                DWORD available{};
                if (PeekNamedPipe(output_read, nullptr, 0U, nullptr, &available, nullptr) ==
                    FALSE) {
                    // A broken pipe after exit is expected; the caller checks exit.
                    return false;
                }
                if (available == 0U) return true;
                std::array<char, 4'096U> buffer{};
                DWORD read{};
                const auto wanted = std::min<DWORD>(available,
                                                    static_cast<DWORD>(buffer.size()));
                if (ReadFile(output_read, buffer.data(), wanted, &read, nullptr) == FALSE ||
                    read == 0U) {
                    return false;
                }
                for (DWORD index{}; index < read; ++index) {
                    const char character = buffer[index];
                    if (character == '\n') {
                        accept_line(std::move(partial_line));
                        partial_line.clear();
                    } else if (character != '\r') {
                        partial_line.push_back(character);
                        if (partial_line.size() >= maximum_bridge_line) {
                            error = "Steam bridge returned an oversized response";
                            return false;
                        }
                    }
                }
            }
        } catch (...) {
            error = "Steam bridge output could not be buffered";
            return false;
        }
    }

    [[nodiscard]] std::optional<std::string> read_response() noexcept {
        const auto deadline = std::chrono::steady_clock::now() + config.response_timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            const bool drained = drain_output();
            if (!responses.empty()) {
                auto line = std::move(responses.front());
                responses.pop_front();
                return line;
            }
            if (!drained || !alive()) {
                if (error.empty()) error = "Steam bridge exited unexpectedly";
                return std::nullopt;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        error = "Steam bridge response timed out";
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::vector<std::string_view>>
    command(std::string_view value, std::string& storage) noexcept {
        // A reply left over from a command that timed out would be taken as
        // this one's; drop what is already queued first.
        static_cast<void>(drain_output());
        responses.clear();
        if (!write_line(value)) {
            fail_transient();
            return std::nullopt;
        }
        auto response = read_response();
        if (!response.has_value()) {
            // A bridge that stopped answering cannot be trusted with the next
            // command either; restart it rather than desynchronise.
            fail_transient();
            return std::nullopt;
        }
        storage = std::move(*response);
        auto fields = split_fields(storage);
        if (fields.empty()) {
            error = "Steam bridge returned an empty response";
            return std::nullopt;
        }
        if (fields[0U] == "ERROR") {
            const auto decoded = fields.size() > 1U ? hex_decode(fields[1U]) : std::nullopt;
            error = decoded.value_or("Steam bridge rejected the command");
            return std::nullopt;
        }
        return fields;
    }

    [[nodiscard]] bool launch() noexcept {
        std::error_code code;
        if (!std::filesystem::is_regular_file(config.bridge_executable, code) || code) {
            error = "32-bit Steam bridge is not installed";
            return false;
        }
        if (!std::filesystem::is_regular_file(config.steam_api_library, code) || code) {
            error = "original 32-bit steam_api.dll has not been imported";
            return false;
        }

        SECURITY_ATTRIBUTES security{};
        security.nLength = sizeof(security);
        security.bInheritHandle = TRUE;
        HANDLE child_input_read{};
        HANDLE child_output_write{};
        if (CreatePipe(&child_input_read, &input_write, &security, 0U) == FALSE ||
            CreatePipe(&output_read, &child_output_write, &security, 0U) == FALSE) {
            close_handle(child_input_read);
            close_handle(child_output_write);
            close_handle(input_write);
            close_handle(output_read);
            error = "could not create Steam bridge pipes";
            return false;
        }
        SetHandleInformation(input_write, HANDLE_FLAG_INHERIT, 0U);
        SetHandleInformation(output_read, HANDLE_FLAG_INHERIT, 0U);

        nonce = random_nonce();
        std::wstring command_line = quote_windows_argument(config.bridge_executable) +
                                    L" --steam-api " +
                                    quote_windows_argument(config.steam_api_library) +
                                    L" --app-id " +
                                    std::to_wstring(config.app_id) + L" --nonce " +
                                    quote_windows_argument(std::string_view{nonce});
        std::vector<wchar_t> mutable_command(command_line.begin(), command_line.end());
        mutable_command.push_back(L'\0');

        // Standard error goes nowhere: only the protocol belongs on the pipe.
        HANDLE null_error = CreateFileW(L"NUL",
                                        GENERIC_WRITE,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                                        &security,
                                        OPEN_EXISTING,
                                        0U,
                                        nullptr);
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = child_input_read;
        startup.hStdOutput = child_output_write;
        startup.hStdError =
            null_error != INVALID_HANDLE_VALUE ? null_error : child_output_write;
        PROCESS_INFORMATION created{};
        const auto working = config.steam_api_library.parent_path().wstring();
        const BOOL launched = CreateProcessW(config.bridge_executable.c_str(),
                                             mutable_command.data(),
                                             nullptr,
                                             nullptr,
                                             TRUE,
                                             CREATE_NO_WINDOW,
                                             nullptr,
                                             working.c_str(),
                                             &startup,
                                             &created);
        close_handle(child_input_read);
        close_handle(child_output_write);
        close_handle(null_error);
        if (launched == FALSE) {
            close_handle(input_write);
            close_handle(output_read);
            error = "could not launch the 32-bit Steam bridge";
            return false;
        }
        process = created.hProcess;
        CloseHandle(created.hThread);
        partial_line.clear();
        responses.clear();
        startup_deadline = std::chrono::steady_clock::now() + config.startup_timeout;
        state = NativeSteamState::starting;
        error.clear();
        return true;
    }

    /** Parse READY; false with `error` set when the line is not a valid one. */
    [[nodiscard]] bool accept_ready(std::string_view line) {
        const auto fields = split_fields(line);
        if (fields.size() != 5U || fields[0U] != "READY" || fields[1U] != nonce) {
            error = "Steam bridge handshake was malformed";
            return false;
        }
        const auto steam_id = parse_u64(fields[2U]);
        auto persona = hex_decode(fields[3U]);
        auto language = hex_decode(fields[4U]);
        if (!steam_id.has_value() || *steam_id == 0U || !persona.has_value() ||
            persona->empty() || !language.has_value()) {
            error = "Steam bridge identity was invalid";
            return false;
        }
        identity = NativeSteamIdentity{*steam_id, std::move(*persona), std::move(*language)};
        error.clear();
        return true;
    }

    /** Step the handshake of a starting bridge. */
    void advance_startup() noexcept {
        const bool drained = drain_output();
        while (!responses.empty()) {
            auto line = std::move(responses.front());
            responses.pop_front();
            const auto fields = split_fields(line);
            if (!fields.empty() && fields[0U] == "ERROR") {
                const auto decoded = fields.size() > 1U ? hex_decode(fields[1U]) : std::nullopt;
                error = decoded.value_or("Steam bridge could not attach to Steam");
                // The bridge exits right after; its exit code says whether
                // another attempt can succeed.
                if (WaitForSingleObject(process, 500U) == WAIT_TIMEOUT) {
                    TerminateProcess(process, 1U);
                }
                finish_failed_startup();
                return;
            }
            bool accepted{};
            try {
                accepted = accept_ready(line);
            } catch (...) {
                error = "Steam bridge identity could not be stored";
            }
            if (!accepted) {
                fail_transient();
                return;
            }
            state = NativeSteamState::ready;
            // Ask for join events. A bridge from before SUBSCRIBE answers
            // ERROR, which only means it cannot report them.
            std::string storage;
            if (!command("SUBSCRIBE", storage).has_value() && ready_process()) error.clear();
            return;
        }
        if (!drained || !alive()) {
            if (error.empty()) error = "Steam bridge exited during startup";
            finish_failed_startup();
            return;
        }
        if (std::chrono::steady_clock::now() >= startup_deadline) {
            error = "Steam did not answer within " +
                    std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
                                       config.startup_timeout)
                                       .count()) +
                    " s; is Steam still starting?";
            fail_transient();
        }
    }

    [[nodiscard]] bool ready_process() const noexcept {
        return state == NativeSteamState::ready && alive();
    }

    void finish_failed_startup() noexcept {
        const auto code = exit_code();
        if (code.has_value() &&
            (*code == bridge_exit_bad_arguments || *code == bridge_exit_unusable_runtime)) {
            close_process();
            state = NativeSteamState::unavailable;
            return;
        }
        fail_transient();
    }

    /** Tear the bridge down and schedule another attempt. */
    void fail_transient() noexcept {
        close_process();
        state = NativeSteamState::retrying;
        retry_at = std::chrono::steady_clock::now() + config.retry_interval;
    }

    void close_process() noexcept {
        identity.reset();
        active_ticket = 0U;
        if (alive()) {
            static_cast<void>(write_line("QUIT"));
            if (WaitForSingleObject(process, 500U) == WAIT_TIMEOUT) {
                // The process is our own supervised helper. A stuck DLL must
                // not keep the game alive during shutdown.
                TerminateProcess(process, 1U);
                WaitForSingleObject(process, 500U);
            }
        }
        close_handle(input_write);
        close_handle(output_read);
        close_handle(process);
        partial_line.clear();
        responses.clear();
    }

    void shutdown() noexcept {
        close_process();
        state = NativeSteamState::unavailable;
    }

    NativeSteamState poll() noexcept {
        switch (state) {
        case NativeSteamState::starting:
            advance_startup();
            break;
        case NativeSteamState::ready:
            if (!drain_output() || !alive()) {
                error = "Steam bridge exited; reconnecting to Steam";
                fail_transient();
            }
            break;
        case NativeSteamState::retrying:
            if (std::chrono::steady_clock::now() >= retry_at) {
                const auto previous = error;
                if (!launch()) {
                    // The files vanished between attempts; nothing to retry.
                    state = NativeSteamState::unavailable;
                } else if (!previous.empty()) {
                    // Keep the reason visible while the new attempt runs.
                    error = previous;
                }
            }
            break;
        case NativeSteamState::unavailable:
            break;
        }
        return state;
    }
#else
    [[nodiscard]] bool launch() noexcept {
        error = "native retail Steam runtime is not available on this platform build";
        return false;
    }
    NativeSteamState poll() noexcept { return state; }
    void shutdown() noexcept {
        identity.reset();
        state = NativeSteamState::unavailable;
    }
#endif
};

NativeSteamClient::NativeSteamClient(NativeSteamClientConfig config)
    : impl_{std::make_unique<Impl>(std::move(config))} {}

NativeSteamClient::~NativeSteamClient() { stop(); }
NativeSteamClient::NativeSteamClient(NativeSteamClient&&) noexcept = default;
NativeSteamClient& NativeSteamClient::operator=(NativeSteamClient&&) noexcept = default;

bool NativeSteamClient::begin_start() noexcept {
    stop();
    try {
        const bool started = impl_->launch();
        if (!started) impl_->shutdown();
        return started;
    } catch (const std::exception& exception) {
        impl_->error = std::string{"Steam bridge startup failed: "} + exception.what();
    } catch (...) {
        impl_->error = "Steam bridge startup failed";
    }
    stop();
    return false;
}

bool NativeSteamClient::start() noexcept {
    if (!begin_start()) return false;
    while (poll() == NativeSteamState::starting) {
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    return ready();
}

NativeSteamState NativeSteamClient::poll() noexcept {
    return impl_ == nullptr ? NativeSteamState::unavailable : impl_->poll();
}

void NativeSteamClient::stop() noexcept {
    if (impl_ != nullptr) {
        const auto error = std::move(impl_->error);
        impl_->shutdown();
        impl_->error = error;
    }
}

NativeSteamState NativeSteamClient::state() const noexcept {
    return impl_ == nullptr ? NativeSteamState::unavailable : impl_->state;
}

bool NativeSteamClient::ready() const noexcept {
#if defined(_WIN32)
    return impl_ != nullptr && impl_->state == NativeSteamState::ready &&
           impl_->identity.has_value() && impl_->alive();
#else
    return false;
#endif
}

const NativeSteamIdentity* NativeSteamClient::identity() const noexcept {
    return ready() ? &*impl_->identity : nullptr;
}

std::string_view NativeSteamClient::last_error() const noexcept {
    return impl_ == nullptr ? std::string_view{} : std::string_view{impl_->error};
}

std::vector<NativeSteamJoinEvent> NativeSteamClient::take_join_events() {
    if (impl_ == nullptr) return {};
    return std::exchange(impl_->join_events, {});
}

std::optional<NativeSteamTicket> NativeSteamClient::session_ticket() noexcept {
#if defined(_WIN32)
    if (!ready()) return std::nullopt;
    std::string storage;
    const auto fields = impl_->command("TICKET", storage);
    if (!fields.has_value() || fields->size() != 3U || (*fields)[0U] != "TICKET")
        return std::nullopt;
    std::uint32_t handle{};
    const auto parsed = std::from_chars((*fields)[1U].data(),
                                       (*fields)[1U].data() + (*fields)[1U].size(),
                                       handle);
    if (parsed.ec != std::errc{} ||
        parsed.ptr != (*fields)[1U].data() + (*fields)[1U].size() || handle == 0U ||
        (*fields)[2U].empty() || (*fields)[2U].size() > maximum_ticket_bytes ||
        !std::ranges::all_of((*fields)[2U], [](unsigned char value) {
            return std::isxdigit(value) != 0;
        })) {
        impl_->error = "Steam bridge returned an invalid session ticket";
        return std::nullopt;
    }
    NativeSteamTicket result;
    result.handle = handle;
    result.wire_bytes.reserve((*fields)[2U].size());
    for (const auto value : (*fields)[2U]) {
        result.wire_bytes.push_back(static_cast<std::byte>(value));
    }
    impl_->active_ticket = handle;
    impl_->error.clear();
    return result;
#else
    return std::nullopt;
#endif
}

bool NativeSteamClient::cancel_ticket(std::uint32_t handle) noexcept {
#if defined(_WIN32)
    if (!ready() || handle == 0U) return false;
    std::string storage;
    const auto response = impl_->command("CANCEL\t" + std::to_string(handle), storage);
    if (!response.has_value() || response->size() != 1U || (*response)[0U] != "OK")
        return false;
    if (impl_->active_ticket == handle) impl_->active_ticket = 0U;
    return true;
#else
    static_cast<void>(handle);
    return false;
#endif
}

bool NativeSteamClient::set_presence(std::string_view connect,
                                     std::string_view status) noexcept {
#if defined(_WIN32)
    if (!ready() || status.empty() || connect.size() > 255U || status.size() > 255U)
        return false;
    std::string storage;
    const auto response = impl_->command(
        "PRESENCE\t" + hex_encode(connect) + "\t" + hex_encode(status), storage);
    return response.has_value() && response->size() == 1U && (*response)[0U] == "OK";
#else
    static_cast<void>(connect);
    static_cast<void>(status);
    return false;
#endif
}

bool NativeSteamClient::set_lobby_presence() noexcept {
    return set_presence({}, "In Custom Lobby");
}

bool NativeSteamClient::set_server_presence(std::string_view endpoint) noexcept {
    if (endpoint.empty() || endpoint.size() > 200U) return false;
    return set_presence("+connect " + std::string{endpoint},
                        "Playing on " + std::string{endpoint});
}

bool NativeSteamClient::clear_presence() noexcept {
#if defined(_WIN32)
    if (!ready()) return false;
    std::string storage;
    const auto response = impl_->command("CLEAR_PRESENCE", storage);
    return response.has_value() && response->size() == 1U && (*response)[0U] == "OK";
#else
    return false;
#endif
}

bool NativeSteamClient::show_achievements() noexcept {
#if defined(_WIN32)
    if (!ready()) return false;
    std::string storage;
    const auto response = impl_->command("SHOW_ACHIEVEMENTS", storage);
    return response.has_value() && response->size() == 1U && (*response)[0U] == "OK";
#else
    return false;
#endif
}

bool NativeSteamClient::increment_int_stat(std::string_view name,
                                           std::int32_t amount) noexcept {
#if defined(_WIN32)
    if (!ready() || name.empty() || name.size() > 128U || amount <= 0) return false;
    std::string storage;
    const auto response = impl_->command(
        "INCREMENT\t" + hex_encode(name) + "\t" + std::to_string(amount), storage);
    return response.has_value() && response->size() == 1U && (*response)[0U] == "OK";
#else
    static_cast<void>(name);
    static_cast<void>(amount);
    return false;
#endif
}

NativeSteamClientConfig default_native_steam_config(
    const std::filesystem::path& executable_directory) noexcept {
    NativeSteamClientConfig result;
#if defined(_WIN32)
    result.bridge_executable = executable_directory / "BattleSpadesSteamBridge32.exe";
    result.steam_api_library = executable_directory / "steam" / "win32" / "steam_api.dll";
#else
    // Native Steam is deliberately Windows-only. Keep the cross-platform
    // façade constructible without weakening strict AppleClang diagnostics.
    static_cast<void>(executable_directory);
#endif
    return result;
}

} // namespace battlespades::platform
