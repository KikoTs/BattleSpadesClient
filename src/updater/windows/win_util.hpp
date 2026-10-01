#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace battlespades::updater::win {

[[nodiscard]] std::wstring widen(std::string_view utf8);
[[nodiscard]] std::string narrow(std::wstring_view wide);

struct HttpOptions {
    std::uint32_t timeout_ms{15000U};
    std::string user_agent{"BattleSpadesLauncher"};
    std::vector<std::string> headers;   ///< "Name: value"
};

struct HttpResponse {
    unsigned status{};
    std::string body;
    std::string error;   ///< transport failure (status stays 0)
};

/// Returns false from the callback to cancel.
using ProgressCallback = std::function<bool(std::uint64_t received, std::uint64_t total)>;

/// GET into memory, capped at `max_body` bytes.
[[nodiscard]] HttpResponse http_get(const std::string& url, const HttpOptions& options,
                                    std::size_t max_body);
/// GET streamed into `file`. Follows GitHub's redirect to its asset CDN.
[[nodiscard]] HttpResponse http_download(const std::string& url, const std::filesystem::path& file,
                                         const HttpOptions& options, const ProgressCallback& progress);

/// HKCU\Software\Valve\Steam\SteamPath, else HKLM ...\WOW6432Node\Valve\Steam\InstallPath.
[[nodiscard]] std::optional<std::filesystem::path> registry_steam_root();
[[nodiscard]] bool is_process_running(std::wstring_view image_name);
[[nodiscard]] inline bool is_steam_running() { return is_process_running(L"steam.exe"); }

/// Runs a hidden child process and waits; returns its exit code or nullopt.
[[nodiscard]] std::optional<unsigned long> run_hidden(const std::wstring& application,
                                                      const std::wstring& command_line,
                                                      unsigned long timeout_ms);

/// Extracts a ZIP in-process (Unicode-safe; see zip_extract.hpp).
bool extract_zip(const std::filesystem::path& archive, const std::filesystem::path& destination,
                 std::string& error);

[[nodiscard]] std::filesystem::path current_executable();
[[nodiscard]] std::filesystem::path local_app_data();

} // namespace battlespades::updater::win
