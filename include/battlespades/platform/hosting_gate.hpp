#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace battlespades::platform {

/**
 * Whether Create Match / Map Creator may start the bundled server.
 *
 * The launcher writes <install>/update/hosting.json from the update manifest
 * (state: ready | not_installed | update_required). Hosting never blocks
 * playing: when the server is missing or incompatible, this asks the
 * launcher to download it (`BattleSpadesLauncher.exe --install-component
 * server`, its own small progress window) and returns a message for the
 * client to show; the player starts the match again when it finishes.
 */
struct HostingGateResult {
    bool ready{};
    bool download_started{};
    std::string message;   ///< empty when ready
};

struct HostingStatusFile {
    std::string state;                 ///< "" when the file is absent or unreadable
    std::string reason;
    std::string available_server;
    std::uint64_t download_size{};
    bool update_available{};
};

[[nodiscard]] HostingStatusFile read_hosting_status(const std::filesystem::path& install);

/// Pure decision used by check_hosting_gate (exposed for tests).
enum class HostingGateAction { start, request_download, wait_for_download, unavailable, missing_launcher };
[[nodiscard]] HostingGateAction decide_hosting_gate(bool bundle_found, const HostingStatusFile& status,
                                                    bool launcher_present, bool download_running) noexcept;

[[nodiscard]] HostingGateResult check_hosting_gate(const std::filesystem::path& install, bool bundle_found);

} // namespace battlespades::platform
