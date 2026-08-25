#include "battlespades/frontend/pause_menu.hpp"

#include <array>
#include <string>
#include <string_view>

namespace battlespades::frontend {
namespace {

// Recovered EscapeMenu geometry (escapeMenu.py, converted to top-origin):
// button_width 265, height 55, pad 7, x = 400 - 265/2; bottom-origin tops
// 428/366/304/242/180 for resume/class/team/settings/disconnect.
constexpr std::int32_t button_left{267};
constexpr std::int32_t button_width{265};
constexpr std::int32_t button_height{55};

struct ActionLayout final {
    PauseMenuAction action;
    std::int32_t top;
    std::string_view label;
};

constexpr std::array<ActionLayout, 7U> layout{{
    {PauseMenuAction::resume, 172, "RESUME"},
    {PauseMenuAction::change_class, 234, "CHANGE CLASS"},
    {PauseMenuAction::constructs, 234, "UGC_CONSTRUCTS"},
    {PauseMenuAction::change_team, 296, "CHANGE TEAM"},
    {PauseMenuAction::game_data, 296, "UGC_GAME_DATA"},
    {PauseMenuAction::settings, 358, "SETTINGS"},
    {PauseMenuAction::disconnect, 420, "DISCONNECT"},
}};

constexpr ui::ColorRgba8 menu_font_color{244U, 236U, 187U, 255U};
constexpr ui::ColorRgba8 button_text_color{20U, 20U, 20U, 255U};

[[nodiscard]] const ActionLayout& layout_for(PauseMenuAction action) noexcept {
    for (const auto& entry : layout) {
        if (entry.action == action) {
            return entry;
        }
    }
    return layout[0U];
}

} // namespace

PauseMenuEnvironment pause_menu_environment_for(
    const PauseMenuServerState& state) noexcept {
    constexpr std::uint8_t vip_mode{7U};
    constexpr std::uint8_t tutorial_mode{10U};

    PauseMenuEnvironment result;
    const bool has_playing_team = state.player_team == 2U || state.player_team == 3U;
    if (state.mode_type == tutorial_mode) {
        return {false, false, false, false};
    }
    if (state.ugc_mode) {
        if (!has_playing_team) {
            // EscapeMenu keeps the ordinary selectors visible but disabled
            // until a UGC player exists; its two library controls stay hidden.
            return {true, true, false, false};
        }
        result.show_class_change = false;
        result.show_team_change = false;
        result.allow_class_change = false;
        result.allow_team_change = false;
        result.show_constructs = true;
        result.show_game_data = true;
        result.allow_constructs = true;
        result.allow_game_data = true;
        return result;
    }
    // VIP owns class assignment. Its boss classes also cannot defect through
    // SelectTeam during the sub-round (retail class ids 10/11).
    if (state.mode_type == vip_mode) {
        result.show_class_change = false;
        result.show_team_change = state.player_class != 10U &&
                                  state.player_class != 11U;
    }
    result.allow_class_change = result.show_class_change && has_playing_team &&
                                !state.active_team_locks_class &&
                                state.available_class_count > 1U;
    const bool another_team_open = state.player_team == 2U
                                       ? !state.team2_locked
                                       : !state.team1_locked;
    const bool spectator_open = state.spectator_enabled &&
                                !state.lock_spectator_swap;
    result.allow_team_change = result.show_team_change && has_playing_team &&
                               !state.lock_team_swap &&
                               (another_team_open || spectator_open);
    if (state.map_ended) {
        // EscapeMenu.update disables both selectors while the server owns the
        // post-match statistics screen, but keeps the normal buttons visible.
        result.allow_class_change = false;
        result.allow_team_change = false;
    }
    return result;
}

PauseMenuModel::PauseMenuModel(PauseMenuEnvironment environment)
    : environment_{environment} {}

ui::Rect PauseMenuModel::action_bounds(PauseMenuAction action) noexcept {
    return ui::Rect{button_left, layout_for(action).top, button_width, button_height};
}

bool PauseMenuModel::action_visible(PauseMenuAction action) const noexcept {
    if (action == PauseMenuAction::change_class) {
        return environment_.show_class_change;
    }
    if (action == PauseMenuAction::change_team) {
        return environment_.show_team_change;
    }
    if (action == PauseMenuAction::constructs) {
        return environment_.show_constructs;
    }
    if (action == PauseMenuAction::game_data) {
        return environment_.show_game_data;
    }
    return true;
}

bool PauseMenuModel::action_enabled(PauseMenuAction action) const noexcept {
    if (action == PauseMenuAction::change_class) {
        return environment_.allow_class_change;
    }
    if (action == PauseMenuAction::change_team) {
        return environment_.allow_team_change;
    }
    if (action == PauseMenuAction::constructs) {
        return environment_.allow_constructs;
    }
    if (action == PauseMenuAction::game_data) {
        return environment_.allow_game_data;
    }
    return true;
}

std::optional<PauseMenuAction>
PauseMenuModel::hit_test(std::optional<ui::Point> point) const noexcept {
    if (!point.has_value()) {
        return std::nullopt;
    }
    for (const auto& entry : layout) {
        if (action_visible(entry.action) && action_enabled(entry.action) &&
            action_bounds(entry.action).contains(*point)) {
            return entry.action;
        }
    }
    return std::nullopt;
}

void PauseMenuModel::pointer_move(std::optional<ui::Point> point) noexcept {
    hovered_ = hit_test(point);
    if (pressed_.has_value() && hovered_ != pressed_) {
        pressed_.reset();
    }
}

void PauseMenuModel::pointer_press(std::optional<ui::Point> point) noexcept {
    pressed_ = hit_test(point);
    hovered_ = pressed_;
}

std::optional<PauseMenuAction>
PauseMenuModel::pointer_release(std::optional<ui::Point> point) noexcept {
    const auto released_on = hit_test(point);
    const auto activated =
        pressed_.has_value() && released_on == pressed_ ? pressed_ : std::nullopt;
    pressed_.reset();
    hovered_ = released_on;
    return activated;
}

ui::DrawList PauseMenuPresentation::build(const PauseMenuModel& model,
                                          const PauseMenuPresentationContext& context) const {
    static_cast<void>(context);
    ui::DrawList list;
    list.reserve(4U + layout.size() * 4U);

    // pause_menu_frame 530x644 @0.64 = 339.2x412.16 centered at (400,300).
    list.push(ui::SpriteDrawCommand{
        "png/ui/in_game_menus/pause_menu_frame.png",
        ui::DrawRect{400.0 - 169.6, 300.0 - 206.08, 339.2, 412.16},
        ui::DrawSpace::design_pixels,
        ui::TextureFilter::linear,
        ui::TextureAnchor::center,
        0.64,
        ui::SpriteSizing::stretch,
        {},
    });
    // Title "MENU" (strings.PAUSE upper), Spades 46, baseline near TO 140.
    list.push(ui::TextDrawCommand{
        "MENU",
        "fonts/Spades.ttf",
        ui::DrawRect{300.0, 100.0, 200.0, 50.0},
        ui::DrawSpace::design_pixels,
        46.0,
        0.0,
        1U,
        ui::HorizontalTextAlignment::center,
        ui::VerticalTextAlignment::retail_center,
        ui::TextTransform::uppercase,
        ui::TextFit::shrink_to_fit,
        ui::ColorModulation{menu_font_color, 1'000U, 1'000U},
    });

    for (const auto& entry : layout) {
        if (!model.action_visible(entry.action)) {
            continue;
        }
        const auto bounds = PauseMenuModel::action_bounds(entry.action);
        const bool enabled = model.action_enabled(entry.action);
        const bool pressed = model.pressed() == entry.action;
        const bool hovered = model.hovered() == entry.action;
        const std::string_view art_state = pressed ? "press" : hovered ? "hover" : "";
        const auto part = [&](std::string_view side) {
            std::string asset{"png/ui/common_elements/buttons/button_large_"};
            if (!art_state.empty()) {
                asset += art_state;
                asset += '_';
            }
            asset += side;
            asset += ".png";
            return asset;
        };
        const auto intensity =
            enabled ? std::uint16_t{1'000U} : std::uint16_t{700U};
        const double x = static_cast<double>(bounds.x);
        const double y = static_cast<double>(bounds.y);
        const double width = static_cast<double>(bounds.width);
        const double height = static_cast<double>(bounds.height);
        const double cap = 60.0 / 97.0 * height + 1.0;
        list.push(ui::SpriteDrawCommand{
            part("left"), ui::DrawRect{x, y, cap, height}, ui::DrawSpace::design_pixels,
            ui::TextureFilter::linear, ui::TextureAnchor::top_left, 0.6,
            ui::SpriteSizing::stretch,
            ui::ColorModulation{ui::ColorRgba8{}, intensity, 1'000U}});
        list.push(ui::SpriteDrawCommand{
            part("mid"), ui::DrawRect{x + cap, y, width - cap * 2.0, height},
            ui::DrawSpace::design_pixels, ui::TextureFilter::linear,
            ui::TextureAnchor::top_left, 0.6, ui::SpriteSizing::stretch,
            ui::ColorModulation{ui::ColorRgba8{}, intensity, 1'000U}});
        list.push(ui::SpriteDrawCommand{
            part("right"), ui::DrawRect{x + width - cap, y, cap, height},
            ui::DrawSpace::design_pixels, ui::TextureFilter::linear,
            ui::TextureAnchor::top_left, 0.6, ui::SpriteSizing::stretch,
            ui::ColorModulation{ui::ColorRgba8{}, intensity, 1'000U}});
        list.push(ui::TextDrawCommand{
            std::string{entry.label},
            "fonts/Spades.ttf",
            ui::DrawRect{x + 14.0, y + 4.0, width - 28.0, height - 8.0},
            ui::DrawSpace::design_pixels,
            30.0,
            0.0,
            1U,
            ui::HorizontalTextAlignment::center,
            ui::VerticalTextAlignment::retail_center,
            ui::TextTransform::uppercase,
            ui::TextFit::shrink_to_fit,
            ui::ColorModulation{button_text_color, intensity, 1'000U},
        });
    }
    return list;
}

} // namespace battlespades::frontend
