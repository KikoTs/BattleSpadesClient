#include "battlespades/platform/native_steam_client.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cctype>
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

#if defined(_WIN32)
    HANDLE process{};
    HANDLE input_write{};
    HANDLE output_read{};

    [[nodiscard]] bool alive() const noexcept {
        if (process == nullptr) return false;
        DWORD code{};
        return GetExitCodeProcess(process, &code) != FALSE && code == STILL_ACTIVE;
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

    [[nodiscard]] std::optional<std::string> read_line() noexcept {
        std::string result;
        result.reserve(512U);
        const auto deadline = std::chrono::steady_clock::now() + config.response_timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            if (!alive()) {
                error = "Steam bridge exited unexpectedly";
                return std::nullopt;
            }
            DWORD available{};
            if (PeekNamedPipe(output_read, nullptr, 0U, nullptr, &available, nullptr) == FALSE) {
                error = "Steam bridge response pipe failed";
                return std::nullopt;
            }
            if (available == 0U) {
                std::this_thread::sleep_for(std::chrono::milliseconds{2});
                continue;
            }
            char character{};
            DWORD read{};
            if (ReadFile(output_read, &character, 1U, &read, nullptr) == FALSE || read != 1U) {
                error = "Steam bridge response read failed";
                return std::nullopt;
            }
            if (character == '\n') return result;
            if (character != '\r') result.push_back(character);
            if (result.size() >= maximum_bridge_line) {
                error = "Steam bridge returned an oversized response";
                return std::nullopt;
            }
        }
        error = "Steam bridge response timed out";
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::vector<std::string_view>>
    command(std::string_view value, std::string& storage) noexcept {
        if (!write_line(value)) return std::nullopt;
        auto response = read_line();
        if (!response.has_value()) return std::nullopt;
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

        const auto nonce = random_nonce();
        std::wstring command_line = quote_windows_argument(config.bridge_executable) +
                                    L" --steam-api " +
                                    quote_windows_argument(config.steam_api_library) +
                                    L" --app-id " +
                                    std::to_wstring(config.app_id) + L" --nonce " +
                                    quote_windows_argument(std::string_view{nonce});
        std::vector<wchar_t> mutable_command(command_line.begin(), command_line.end());
        mutable_command.push_back(L'\0');

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = child_input_read;
        startup.hStdOutput = child_output_write;
        startup.hStdError = child_output_write;
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
        if (launched == FALSE) {
            close_handle(input_write);
            close_handle(output_read);
            error = "could not launch the 32-bit Steam bridge";
            return false;
        }
        process = created.hProcess;
        CloseHandle(created.hThread);

        auto line = read_line();
        if (!line.has_value()) return false;
        const auto fields = split_fields(*line);
        if (fields.size() != 5U || fields[0U] != "READY" || fields[1U] != nonce) {
            error = "Steam bridge handshake was malformed";
            return false;
        }
        std::uint64_t steam_id{};
        const auto conversion = std::from_chars(fields[2U].data(),
                                                fields[2U].data() + fields[2U].size(),
                                                steam_id);
        const auto persona = hex_decode(fields[3U]);
        const auto language = hex_decode(fields[4U]);
        if (conversion.ec != std::errc{} ||
            conversion.ptr != fields[2U].data() + fields[2U].size() ||
            steam_id == 0U || !persona.has_value() || persona->empty() ||
            !language.has_value()) {
            error = "Steam bridge identity was invalid";
            return false;
        }
        identity = NativeSteamIdentity{steam_id, std::move(*persona), std::move(*language)};
        error.clear();
        return true;
    }

    void shutdown() noexcept {
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
    }
#else
    [[nodiscard]] bool launch() noexcept {
        error = "native retail Steam runtime is not available on this platform build";
        return false;
    }
    void shutdown() noexcept { identity.reset(); }
#endif
};

NativeSteamClient::NativeSteamClient(NativeSteamClientConfig config)
    : impl_{std::make_unique<Impl>(std::move(config))} {}

NativeSteamClient::~NativeSteamClient() { stop(); }
NativeSteamClient::NativeSteamClient(NativeSteamClient&&) noexcept = default;
NativeSteamClient& NativeSteamClient::operator=(NativeSteamClient&&) noexcept = default;

bool NativeSteamClient::start() noexcept {
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

void NativeSteamClient::stop() noexcept {
    if (impl_ != nullptr) impl_->shutdown();
}

bool NativeSteamClient::ready() const noexcept {
#if defined(_WIN32)
    return impl_ != nullptr && impl_->identity.has_value() && impl_->alive();
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

bool NativeSteamClient::set_lobby_presence() noexcept {
#if defined(_WIN32)
    if (!ready()) return false;
    std::string storage;
    const auto response = impl_->command(
        "PRESENCE\t\t" + hex_encode("In Custom Lobby"), storage);
    return response.has_value() && response->size() == 1U && (*response)[0U] == "OK";
#else
    return false;
#endif
}

bool NativeSteamClient::set_server_presence(std::string_view endpoint) noexcept {
#if defined(_WIN32)
    if (!ready() || endpoint.empty() || endpoint.size() > 255U) return false;
    std::string storage;
    const auto command = "PRESENCE\t" + hex_encode("+connect " + std::string{endpoint}) +
                         "\t" + hex_encode("Playing on " + std::string{endpoint});
    const auto response = impl_->command(command, storage);
    return response.has_value() && response->size() == 1U && (*response)[0U] == "OK";
#else
    static_cast<void>(endpoint);
    return false;
#endif
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
