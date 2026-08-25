#pragma once

#include "battlespades/frontend/frontend_shell.hpp"
#include "battlespades/ui/screen_stack.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace battlespades::frontend {

/**
 * Stable identities for reconstructed frontend destinations.
 *
 * Values are persisted only inside the running process. Network and save-file
 * code must never serialize them; each destination emits a typed application
 * action at the boundary instead.
 */
enum class FrontendScreen : std::uint32_t {
    select_menu = 1U,
    settings,
    resolution_confirmation,
    join_match,
    server_browser,
    direct_connect,
    public_match,
    custom_match,
    create_match,
    create_match_rules,
    create_match_maps,
    create_match_lobby,
    ugc_select,
    ugc_publish,
    leaderboard,
    player_profile,
    startup_loading,
    game_loading,
    class_selection,
    ugc_editor_browser,
    ugc_editor_lobby,
    tutorial_world,
    pause_menu,
    change_team,
    parity_debug,
    gameplay_debug,
    identity,
    friends_lobby,
};

[[nodiscard]] constexpr ui::ScreenId screen_id(FrontendScreen screen) noexcept {
    return ui::ScreenId{static_cast<std::uint32_t>(screen)};
}

[[nodiscard]] constexpr std::optional<FrontendScreen>
frontend_screen(ui::ScreenId id) noexcept {
    if (id.value < static_cast<std::uint32_t>(FrontendScreen::select_menu) ||
        id.value > static_cast<std::uint32_t>(FrontendScreen::friends_lobby)) {
        return std::nullopt;
    }
    return static_cast<FrontendScreen>(id.value);
}

/**
 * Transactional route stack coupled to the retail horizontal slide model.
 *
 * Push and pop are the only operations normal nested menus need. The stack is
 * mutated before the visual transition starts and is rolled back if either
 * half cannot be applied, so input dispatch and presentation never disagree
 * about the active destination. Calls fail while a transition is already in
 * progress; retail gates those inputs until the new menu is near its resting
 * position.
 */
class FrontendNavigationModel final {
public:
    explicit FrontendNavigationModel(std::size_t maximum_depth = 16U);

    [[nodiscard]] bool start(FrontendScreen root) noexcept;
    [[nodiscard]] bool push(FrontendScreen child) noexcept;
    [[nodiscard]] bool pop() noexcept;
    [[nodiscard]] bool replace(FrontendScreen target,
                               NavigationDirection direction) noexcept;
    [[nodiscard]] bool reset(FrontendScreen root,
                             NavigationDirection direction) noexcept;

    void tick() noexcept;

    [[nodiscard]] std::optional<FrontendScreen> active() const noexcept;
    [[nodiscard]] std::optional<FrontendScreen> previous() const noexcept;
    [[nodiscard]] std::span<const ui::ScreenId> stack() const noexcept;
    [[nodiscard]] std::size_t depth() const noexcept;
    [[nodiscard]] const FrontendShellModel& shell() const noexcept;

private:
    ui::ScreenStack routes_;
    FrontendShellModel shell_{};
};

} // namespace battlespades::frontend
