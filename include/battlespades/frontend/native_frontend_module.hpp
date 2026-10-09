#pragma once

#include "battlespades/core/runtime_module.hpp"
#include "battlespades/settings/client_settings.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace battlespades::platform {
class WindowPort;
}

namespace battlespades::frontend {

enum class MainMenuAction : std::uint8_t;

/** Filesystem and user data needed by the first native frontend screen. */
struct NativeFrontendConfig final {
    /** Directory containing this executable and its optional Steam bridge. */
    std::filesystem::path executable_directory{"."};
    /** Project-owned assets shipped with every build (fonts/icons, never retail content). */
    std::filesystem::path client_asset_root{"assets/client"};
    std::filesystem::path asset_root{"assets/original"};
    std::filesystem::path shader_root{"assets/generated/shaders"};
    std::string player_name{"Player"};
    bool enable_audio{true};
    bool renderer_debug{false};
    bool offline{};
    bool reset_settings{};
    std::string offline_profile{"Player"};
    std::optional<std::string> master_url;
    std::optional<std::string> language_override;
    std::filesystem::path record_demo_path;
    std::filesystem::path play_demo_path;
    /** Explicit per-install settings file; the executable directory is used by main(). */
    std::filesystem::path settings_path{"settings.toml"};
    /** Raw Tutorial VXL used by the local world-parser test launcher. */
    std::filesystem::path tutorial_map_path{"../BattleSpades/maps/Training.vxl"};
    /** Optional deterministic tool selection used by graphical parity smokes. */
    std::optional<std::uint8_t> tutorial_debug_tool;
    std::optional<std::string> tutorial_debug_cosmetic;
    /** Enter the selected tool's recovered RMB aim state when the lab starts. */
    bool tutorial_debug_aim{false};
    /** Developer visual verification; see core::LaunchOptions for the rationale. */
    std::optional<std::string> tutorial_map;
    std::optional<std::string> tutorial_skydome;
    std::optional<std::array<double, 3U>> tutorial_spawn;
    std::optional<std::array<double, 2U>> tutorial_stand;
    std::optional<std::array<double, 3U>> tutorial_look;
    /**
     * Forces a shader tier for this run without touching the settings file.
     *
     * Deliberately NOT written back: a capture run must not leave the player on
     * whatever tier a screenshot needed.
     */
    std::optional<settings::ShaderQuality> shader_quality_override;
    /** HTTPS AoSPlay-compatible public browser endpoint. */
    std::string public_server_list_url{"https://www.aosplay.net/serverlist/"};
    /** HTTPS endpoints implementing the recovered retail score-service contract. */
    std::string leaderboard_url{"https://www.aosplay.net/leaderboard"};
    std::string player_profile_url{"https://www.aosplay.net/profile"};
    /**
     * Durable AoSPlay account identity when supplied by a future sign-in flow.
     *
     * Zero asks the score adapter to resolve the current display name through
     * an authoritative leaderboard row. Ambiguous names fail closed.
     */
    std::uint64_t player_account_id{};
    /**
     * Complete portable BattleSpades server directory used by Create Match.
     *
     * Empty enables executable-adjacent `server/`, the
     * AOS_BATTLESPADES_SERVER environment override, and the developer-tree
     * discovery path in that order.
     */
    std::filesystem::path local_server_root{};
    /** Create Match treats this as a preference and scans upward if occupied. */
    std::uint16_t preferred_local_server_port{27015U};
    /** Bounded HELLOLAN ports queried for the Local browser source. */
    std::vector<std::uint16_t> local_server_ports{27015U, 32887U};
    /** Optional automation shortcut; uses the same loader as Direct Connect. */
    std::optional<std::string> startup_endpoint;
    /** `--password`: answers the startup server's password request. */
    std::optional<std::string> startup_password;
    /** Steam lobby from "+connect_lobby <id>", resolved once Steam is attached. */
    std::optional<std::uint64_t> startup_steam_lobby;
    /**
     * Host a Local Match over Steam alone, publishing no AoSPlay relay lobby.
     * Testing only: with both doors open, a friend who arrives through the
     * relay makes a broken Steam path look like a working one.
     */
    bool steam_only{false};
    /** Offline production-renderer VFX oracle selected by --debug-vfx. */
    std::optional<std::string> debug_vfx;
    /** Exact age at which the oracle freezes for deterministic screenshots. */
    std::optional<double> debug_vfx_age;
    /** Offline production-UI oracle selected by --debug-ui. */
    std::optional<std::string> debug_ui;
    /** Editable UTF-8 catalogue loaded beside the executable. */
    std::filesystem::path localization_path{"localization"};
    /** Persistent design-pixel layout overrides loaded beside the executable. */
    std::filesystem::path ui_layout_path{"ui-layout.json"};
    /** Starts the offline F11 layout surface immediately. */
    bool ui_layout_editor{};
};

/**
 * Main-thread composition adapter for the reconstructed retail Select Menu.
 *
 * The SDL window must be registered before this module. start() attaches bgfx
 * to the borrowed native window, initializes bounded font/audio services, and
 * renders from the renderer-neutral menu model. tick() consumes only the
 * window events collected earlier in the same Application tick. Returning
 * `stop` means the user requested Quit or a native rendering invariant failed.
 */
class NativeFrontendModule final : public core::RuntimeModule {
public:
    NativeFrontendModule(platform::WindowPort& window, NativeFrontendConfig config = {});
    ~NativeFrontendModule() override;

    NativeFrontendModule(const NativeFrontendModule&) = delete;
    NativeFrontendModule& operator=(const NativeFrontendModule&) = delete;
    NativeFrontendModule(NativeFrontendModule&&) = delete;
    NativeFrontendModule& operator=(NativeFrontendModule&&) = delete;

    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] bool start() override;
    [[nodiscard]] core::TickDecision tick(const core::TickContext& context) override;
    void stop() noexcept override;
    [[nodiscard]] std::chrono::nanoseconds intermediate_frame_period() const noexcept override;
    [[nodiscard]] core::TickDecision present_intermediate(double alpha) override;
    void idle(std::chrono::steady_clock::time_point deadline) override;

    [[nodiscard]] bool is_started() const noexcept;
    [[nodiscard]] std::string_view last_error() const noexcept;
    [[nodiscard]] std::optional<MainMenuAction> last_action() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace battlespades::frontend
