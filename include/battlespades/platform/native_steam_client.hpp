#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace battlespades::platform {

/** Identity returned by the player's owned retail Steam runtime. */
struct NativeSteamIdentity final {
    std::uint64_t steam_id{};
    std::string persona_name;
    std::string language;
};

/**
 * One retail Protocol-168 authentication key and its Steam cancellation token.
 *
 * `wire_bytes` are already the original wrapper's ASCII-hex
 * `SteamID || GetAuthSessionTicket` representation. They are opaque to the
 * client and must never be logged or persisted.
 */
struct NativeSteamTicket final {
    std::uint32_t handle{};
    std::vector<std::byte> wire_bytes;
};

struct NativeSteamClientConfig final {
    std::filesystem::path bridge_executable;
    std::filesystem::path steam_api_library;
    std::uint32_t app_id{224540U};
    std::chrono::milliseconds response_timeout{2'000};
};

/**
 * Supervises the architecture-matched helper for the original Steamworks DLL.
 *
 * The Windows retail DLL is 32-bit while the renderer is 64-bit, so it cannot
 * be loaded into the game process. A hidden child owns the DLL and pumps Steam
 * callbacks; this façade exchanges bounded commands over inherited anonymous
 * pipes. No gameplay or renderer thread is created here. All public methods
 * fail closed after a broken/malformed response and never fabricate identity.
 */
class NativeSteamClient final {
public:
    explicit NativeSteamClient(NativeSteamClientConfig config);
    ~NativeSteamClient();

    NativeSteamClient(const NativeSteamClient&) = delete;
    NativeSteamClient& operator=(const NativeSteamClient&) = delete;
    NativeSteamClient(NativeSteamClient&&) noexcept;
    NativeSteamClient& operator=(NativeSteamClient&&) noexcept;

    [[nodiscard]] bool start() noexcept;
    void stop() noexcept;

    [[nodiscard]] bool ready() const noexcept;
    [[nodiscard]] const NativeSteamIdentity* identity() const noexcept;
    [[nodiscard]] std::string_view last_error() const noexcept;

    [[nodiscard]] std::optional<NativeSteamTicket> session_ticket() noexcept;
    [[nodiscard]] bool cancel_ticket(std::uint32_t handle) noexcept;
    [[nodiscard]] bool set_lobby_presence() noexcept;
    [[nodiscard]] bool set_server_presence(std::string_view endpoint) noexcept;
    [[nodiscard]] bool clear_presence() noexcept;
    [[nodiscard]] bool show_achievements() noexcept;
    [[nodiscard]] bool increment_int_stat(std::string_view name,
                                          std::int32_t amount) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/** Locate the optional runtime imported by BattleSpadesAssetInstaller. */
[[nodiscard]] NativeSteamClientConfig default_native_steam_config(
    const std::filesystem::path& executable_directory) noexcept;

} // namespace battlespades::platform
