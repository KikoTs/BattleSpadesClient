#pragma once

#include "battlespades/frontend/frontend_shell.hpp"
#include "battlespades/frontend/main_menu.hpp"
#include "battlespades/frontend/resolution_confirmation.hpp"
#include "battlespades/frontend/settings_menu.hpp"
#include "battlespades/settings/settings_store.hpp"
#include "battlespades/ui/screen_stack.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace battlespades::frontend {

enum class FrontendRoute : std::uint8_t {
    select_menu,
    settings,
    resolution_confirmation,
};

enum class RuntimeAudioEffectKind : std::uint8_t {
    menu_confirm,
    menu_back,
    menu_scroll,
    set_master_volume,
    set_music_volume,
};

struct RuntimeAudioEffect final {
    RuntimeAudioEffectKind kind{RuntimeAudioEffectKind::menu_confirm};
    double value{};

    [[nodiscard]] friend constexpr bool operator==(const RuntimeAudioEffect&,
                                                   const RuntimeAudioEffect&) = default;
};

enum class RuntimeDisplayEffectKind : std::uint8_t {
    set_window_mode,
    set_resolution,
    set_vsync,
};

struct RuntimeDisplayEffect final {
    RuntimeDisplayEffectKind kind{RuntimeDisplayEffectKind::set_window_mode};
    bool enabled{};
    settings::Resolution resolution{};
    settings::WindowMode window_mode{settings::WindowMode::borderless};

    [[nodiscard]] friend constexpr bool operator==(const RuntimeDisplayEffect&,
                                                   const RuntimeDisplayEffect&) = default;
};

/** Applies non-window Graphics preferences to the active presentation profile. */
struct RuntimePresentationEffect final {
    settings::GraphicsSettings graphics{};

    [[nodiscard]] friend constexpr bool operator==(const RuntimePresentationEffect&,
                                                   const RuntimePresentationEffect&) = default;
};

/** Applies committed mouse inversion, sensitivity, and binding preferences. */
struct RuntimeInputEffect final {
    bool invert_mouse{};
    settings::ControlsSettings controls{};

    [[nodiscard]] friend constexpr bool operator==(const RuntimeInputEffect&,
                                                   const RuntimeInputEffect&) = default;
};

enum class RuntimeSettingsNoticeKind : std::uint8_t {
    restart_required,
    persistence_failed,
    binding_rejected,
};

struct RuntimeSettingsNoticeEffect final {
    RuntimeSettingsNoticeKind kind{RuntimeSettingsNoticeKind::restart_required};
    std::string message{};
    std::optional<settings::ControlAction> control_action{};
    std::optional<settings::ControlAction> conflicting_action{};

    [[nodiscard]] friend bool operator==(const RuntimeSettingsNoticeEffect&,
                                         const RuntimeSettingsNoticeEffect&) = default;
};

/** Transient favourite-server mutation; intentionally absent from TOML. */
struct RuntimeFavoriteServerEffect final {
    bool favorite{};

    [[nodiscard]] friend constexpr bool operator==(const RuntimeFavoriteServerEffect&,
                                                   const RuntimeFavoriteServerEffect&) = default;
};

using RuntimeSettingsEffectPayload =
    std::variant<RuntimeAudioEffect,
                 RuntimeDisplayEffect,
                 RuntimePresentationEffect,
                 RuntimeInputEffect,
                 RuntimeSettingsNoticeEffect,
                 RuntimeFavoriteServerEffect>;

struct RuntimeSettingsEffect final {
    RuntimeSettingsEffectPayload payload{};
};

struct FrontendControllerConfig final {
    /** Explicit writable path; tests never depend on a process-global user directory. */
    std::filesystem::path settings_path{};
    SettingsMenuEnvironment settings_environment{};
};

/**
 * Headless coordinator for frontend routes and Settings transactions.
 *
 * This class owns no window, renderer, audio device, or operating-system state.
 * It translates screen-model intentions into ordered RuntimeSettingsEffect
 * records for a native adapter to execute. Pointer coordinates use raw
 * top-left 800x600 retail pixels; the future native adapter divides the
 * DesignCanvas's eighth-pixel points before dispatching here.
 */
class FrontendController final {
public:
    explicit FrontendController(FrontendControllerConfig config);
    ~FrontendController();

    FrontendController(const FrontendController&) = delete;
    FrontendController& operator=(const FrontendController&) = delete;
    FrontendController(FrontendController&&) noexcept;
    FrontendController& operator=(FrontendController&&) noexcept;

    /** Loads settings safely and starts on Select Menu. May be called once. */
    [[nodiscard]] bool start();
    void tick(std::chrono::nanoseconds elapsed);

    [[nodiscard]] bool started() const noexcept;
    [[nodiscard]] std::optional<FrontendRoute> route() const noexcept;
    [[nodiscard]] const ui::ScreenStack& routes() const noexcept;
    [[nodiscard]] const FrontendShellModel& shell() const noexcept;

    [[nodiscard]] const MainMenuModel& main_menu() const noexcept;
    [[nodiscard]] const SettingsMenuModel& settings_menu() const noexcept;
    [[nodiscard]] const ResolutionConfirmationModel& resolution_confirmation() const noexcept;
    [[nodiscard]] const settings::SettingsSession& settings_session() const noexcept;

    void pointer_move(std::optional<ui::Point> retail_point);
    void pointer_press(std::optional<ui::Point> retail_point);
    void pointer_drag(std::optional<ui::Point> retail_point);
    void pointer_release(std::optional<ui::Point> retail_point);
    [[nodiscard]] bool mouse_wheel(ui::Point retail_point, std::int32_t vertical_steps);
    [[nodiscard]] bool handle(ui::InputEvent event);
    [[nodiscard]] settings::BindingAssignmentResult capture_scancode(std::uint32_t scancode);
    [[nodiscard]] settings::BindingAssignmentResult capture_mouse_button(std::uint32_t button);
    void cancel_pointer_capture() noexcept;

    /** Effects remain ordered exactly as their source UI events. */
    [[nodiscard]] std::span<const RuntimeSettingsEffect> effects() const noexcept;
    [[nodiscard]] std::vector<RuntimeSettingsEffect> take_effects() noexcept;

    /** Main-menu actions other than Settings remain available to application services. */
    [[nodiscard]] std::span<const MainMenuAction> unhandled_main_actions() const noexcept;
    [[nodiscard]] std::vector<MainMenuAction> take_unhandled_main_actions() noexcept;

    /** Non-fatal load warnings and the most recent persistence failure. */
    [[nodiscard]] std::string_view last_error() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace battlespades::frontend
