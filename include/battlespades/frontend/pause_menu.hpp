#pragma once

#include "battlespades/ui/design_canvas.hpp"
#include "battlespades/ui/draw_list.hpp"
#include "battlespades/ui/geometry.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace battlespades::frontend {

enum class PauseMenuAction : std::uint8_t {
    resume,
    change_class,
    change_team,
    constructs,
    game_data,
    settings,
    disconnect,
    /** Map Creator host only: replaces DISCONNECT (escapeMenu.save_button). */
    save,
    /** Map Creator host only: asks UGC_QUIT_WITHOUT_SAVING first. */
    quit,
    /** MessageBox left button (OK, Yes or Retry). */
    message_primary,
    /** MessageBox right button (No or Cancel). */
    message_secondary,
};

/** escapeMenu.py MESSAGE_* ids plus the dialog each one shows. */
enum class PauseMenuMessage : std::uint8_t {
    /** UGC_MAP_SAVE_SUCCESSFULLY, OK: resume. */
    saved,
    /** UGC_MAP_SAVE_SUCCESSFULLY, OK: disconnect (MESSAGE_QUIT_AFTER_SAVE). */
    saved_then_quit,
    /** UGC_MAP_SAVE_ERROR, Retry / Cancel. */
    save_error,
    /** UGC_QUIT_WITHOUT_SAVING ("Save before quitting?"), Yes / No. */
    save_before_quit,
};

/**
 * Mode gating recovered from the retail EscapeMenu: the tutorial (mode
 * A2445) hides Change Class/Change Team entirely, and both stay visible but
 * disabled while the player has no team yet.
 */
struct PauseMenuEnvironment final {
    bool show_class_change{true};
    bool show_team_change{true};
    bool allow_class_change{true};
    bool allow_team_change{true};
    bool show_constructs{};
    bool show_game_data{};
    bool allow_constructs{};
    bool allow_game_data{};
    /**
     * game_scene.is_ugc_host(): SAVE and QUIT replace DISCONNECT and the
     * menu uses pause_menu_frame_big.
     */
    bool ugc_host{};
};

/** The server-owned subset used by retail EscapeMenu visibility/gating. */
struct PauseMenuServerState final {
    std::uint8_t mode_type{};
    std::uint8_t player_team{};
    std::uint8_t player_class{};
    std::size_t available_class_count{};
    bool active_team_locks_class{};
    bool team1_locked{};
    bool team2_locked{};
    bool lock_team_swap{};
    bool lock_spectator_swap{};
    bool spectator_enabled{};
    bool map_ended{};
    bool ugc_mode{};
    /** This client owns the Map Creator session (game_scene.is_ugc_host). */
    bool ugc_host{};
};

[[nodiscard]] PauseMenuEnvironment
pause_menu_environment_for(const PauseMenuServerState& state) noexcept;

/**
 * Retail in-game Escape menu ("Menu"), recovered from escapeMenu.py:
 * 265x55 buttons at x 267.5 with 7px padding, stacked Resume (top, y 172),
 * Change Class, Change Team, Settings, Disconnect (y 420) on the 800x600
 * canvas; pause_menu_frame centered at (400,300); Spades 46 title.
 * Escape/Resume closes; the world keeps rendering behind it.
 */
class PauseMenuModel final {
public:
    explicit PauseMenuModel(PauseMenuEnvironment environment = {});

    void pointer_move(std::optional<ui::Point> point) noexcept;
    void pointer_press(std::optional<ui::Point> point) noexcept;
    [[nodiscard]] std::optional<PauseMenuAction>
    pointer_release(std::optional<ui::Point> point) noexcept;

    [[nodiscard]] const PauseMenuEnvironment& environment() const noexcept {
        return environment_;
    }
    [[nodiscard]] std::optional<PauseMenuAction> hovered() const noexcept { return hovered_; }
    [[nodiscard]] std::optional<PauseMenuAction> pressed() const noexcept { return pressed_; }

    /** Menu buttons; message-box buttons use message_button_bounds(). */
    [[nodiscard]] static ui::Rect action_bounds(PauseMenuAction action) noexcept;
    [[nodiscard]] bool action_visible(PauseMenuAction action) const noexcept;
    [[nodiscard]] bool action_enabled(PauseMenuAction action) const noexcept;

    /** show_message_box: every other button is disabled while it is up. */
    void show_message(PauseMenuMessage message) noexcept;
    void hide_message() noexcept;
    [[nodiscard]] std::optional<PauseMenuMessage> message() const noexcept { return message_; }
    /** False for the one-button (OK) dialogs. */
    [[nodiscard]] bool message_has_two_buttons() const noexcept;
    [[nodiscard]] ui::Rect message_button_bounds(PauseMenuAction button) const noexcept;

private:
    [[nodiscard]] std::optional<PauseMenuAction>
    hit_test(std::optional<ui::Point> point) const noexcept;

    PauseMenuEnvironment environment_{};
    std::optional<PauseMenuAction> hovered_{};
    std::optional<PauseMenuAction> pressed_{};
    std::optional<PauseMenuMessage> message_{};
};

struct PauseMenuPresentationContext final {
    ui::PixelExtent window{800, 600};
};

class PauseMenuPresentation final {
public:
    [[nodiscard]] ui::DrawList build(const PauseMenuModel& model,
                                     const PauseMenuPresentationContext& context) const;
};

} // namespace battlespades::frontend
