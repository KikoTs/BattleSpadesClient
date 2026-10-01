#include "battlespades/platform/hosting_gate.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <iterator>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

namespace battlespades::platform {
namespace {

[[nodiscard]] std::string megabytes(std::uint64_t bytes) {
    return std::to_string((bytes + 512U * 1024U) / (1024U * 1024U)) + " MB";
}

#if defined(_WIN32)
/// The launcher holds this mutex while it downloads or updates anything.
[[nodiscard]] bool launcher_busy() noexcept {
    HANDLE mutex = OpenMutexW(SYNCHRONIZE, FALSE, L"Local\\BattleSpadesLauncherUpdate");
    if (mutex == nullptr) return false;
    CloseHandle(mutex);
    return true;
}

[[nodiscard]] bool start_server_download(const std::filesystem::path& launcher) noexcept {
    try {
        std::wstring line = L"\"" + launcher.wstring() + L"\" --install-component server";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        const auto directory = launcher.parent_path().wstring();
        if (!CreateProcessW(launcher.c_str(), line.data(), nullptr, nullptr, FALSE, 0U, nullptr, directory.c_str(),
                            &startup, &process)) {
            return false;
        }
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return true;
    } catch (...) {
        return false;
    }
}
#endif

} // namespace

HostingStatusFile read_hosting_status(const std::filesystem::path& install) {
    HostingStatusFile status;
    try {
        std::ifstream input{install / "update" / "hosting.json", std::ios::binary};
        if (!input) return status;
        const std::string text{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
        const auto json = nlohmann::json::parse(text);
        status.state = json.value("state", std::string{});
        status.reason = json.value("reason", std::string{});
        status.available_server = json.value("available_server", std::string{});
        status.download_size = json.value("download_size", std::uint64_t{0});
        status.update_available = json.value("update_available", false);
    } catch (...) {
        status = HostingStatusFile{};
    }
    return status;
}

HostingGateAction decide_hosting_gate(bool bundle_found, const HostingStatusFile& status, bool launcher_present,
                                      bool download_running) noexcept {
    const bool needs_server = !bundle_found || status.state == "update_required";
    if (!needs_server) return HostingGateAction::start;
    if (download_running) return HostingGateAction::wait_for_download;
    if (!launcher_present) return HostingGateAction::missing_launcher;
    // The launcher knows the manifest; only skip asking it when it said
    // there is nothing compatible to download.
    if (!status.state.empty() && !status.update_available) return HostingGateAction::unavailable;
    return HostingGateAction::request_download;
}

HostingGateResult check_hosting_gate(const std::filesystem::path& install, bool bundle_found) {
    HostingGateResult result;
    const auto status = read_hosting_status(install);
    const auto launcher = install / "BattleSpadesLauncher.exe";
    std::error_code code;
    const bool launcher_present = std::filesystem::is_regular_file(launcher, code);
#if defined(_WIN32)
    const bool running = launcher_present && launcher_busy();
#else
    const bool running = false;
#endif
    const bool update = bundle_found && status.state == "update_required";
    const std::string why = update ? "Server update required to host" + (status.reason.empty() ? std::string{}
                                                                                               : " (" + status.reason + ")")
                                   : std::string{"Hosting needs the BattleSpades server"};
    switch (decide_hosting_gate(bundle_found, status, launcher_present, running)) {
    case HostingGateAction::start:
        result.ready = true;
        return result;
    case HostingGateAction::wait_for_download:
        result.message = why + ". The download is still running; start again when it finishes.";
        return result;
    case HostingGateAction::missing_launcher:
        result.message = "The complete BattleSpades server bundle is missing. Put it in server/ "
                         "beside BattleSpadesClient.exe.";
        return result;
    case HostingGateAction::unavailable:
        result.message = why + ". No compatible server is available for download yet.";
        return result;
    case HostingGateAction::request_download:
        break;
    }
#if defined(_WIN32)
    if (start_server_download(launcher)) {
        result.download_started = true;
        result.message = why + ". Downloading it now" +
                         (status.download_size != 0U ? " (" + megabytes(status.download_size) + ")" : std::string{}) +
                         "; start again when it finishes.";
        return result;
    }
#endif
    result.message = why + ". Start BattleSpadesLauncher.exe to download it.";
    return result;
}

} // namespace battlespades::platform
