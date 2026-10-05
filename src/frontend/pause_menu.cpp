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

// Labels are the catalogue ids escapeMenu.py passes to TextButton.
constexpr std::array<ActionLayout, 9U> layout{{
    {PauseMenuAction::resume, 172, "RESUME"},
    {PauseMenuAction::change_class, 234, "CHANGE_CLASS"},
    {PauseMenuAction::constructs, 234, "UGC_CONSTRUCTS"},
    {PauseMenuAction::change_team, 296, "CHANGE_TEAM"},
    {PauseMenuAction::game_data, 296, "UGC_GAME_DATA"},
    {PauseMenuAction::settings, 358, "SETTINGS"},
    {PauseMenuAction::disconnect, 420, "DISCONNECT"},
    // The host's SAVE takes DISCONNECT's slot; QUIT sits one row below it.
    {PauseMenuAction::save, 420, "SAVE"},
    {PauseMenuAction::quit, 482, "QUIT"},
}};

// MessageBox(400, 300) with message_box_with_buttons_frame (910x510 @0.64).
// Buttons are a third of message_box_frame's 582 px width, 50 tall, Spades 18.
constexpr double message_frame_width{582.4};
constexpr double message_button_width{message_frame_width / 3.0};
constexpr double message_button_spacing{message_button_width / 3.0};
constexpr double message_button_top{331.0};
constexpr double message_button_height{50.0};

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
    result.ugc_host = state.ugc_host;
    const bool has_playing_team = state.player_team == 2U || state.player_team == 3U;
    const bool has_player_team = has_playing_team ||
                                (state.player_team == 0U && state.spectator_enabled);
    if (state.mode_type == tutorial_mode) {
        PauseMenuEnvironment tutorial{false, false, false, false};
        tutorial.ugc_host = state.ugc_host;
        return tutorial;
    }
    if (state.ugc_mode) {
        if (!has_playing_team) {
            // EscapeMenu keeps the ordinary selectors visible but disabled
            // until a UGC player exists; its two library controls stay hidden.
            PauseMenuEnvironment waiting{true, true, false, false};
            waiting.ugc_host = state.ugc_host;
            return waiting;
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
                                (state.available_class_count > 1U || state.class_has_choices);
    // EscapeMenu and GameScene.team_selection_has_choices allow an admitted
    // spectator to open the selector. Team locks belong to its actual choices.
    result.allow_team_change = result.show_team_change && has_player_team;
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
    if (action == PauseMenuAction::disconnect) {
        return !environment_.ugc_host;
    }
    if (action == PauseMenuAction::save || action == PauseMenuAction::quit) {
        return environment_.ugc_host;
    }
    if (action == PauseMenuAction::message_primary) {
        return message_.has_value();
    }
    if (action == PauseMenuAction::message_secondary) {
        return message_.has_value() && message_has_two_buttons();
    }
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
    const bool message_button = action == PauseMenuAction::message_primary ||
                                action == PauseMenuAction::message_secondary;
    if (message_.has_value() != message_button) {
        // show_message_box disables every element except the box itself.
        return false;
    }
    if (message_button) {
        return action_visible(action);
    }
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

void PauseMenuModel::show_message(PauseMenuMessage message) noexcept {
    message_ = message;
    hovered_.reset();
    pressed_.reset();
}

void PauseMenuModel::hide_message() noexcept {
    message_.reset();
    hovered_.reset();
    pressed_.reset();
}

bool PauseMenuModel::message_has_two_buttons() const noexcept {
    return message_ == PauseMenuMessage::save_error ||
           message_ == PauseMenuMessage::save_before_quit;
}

ui::Rect PauseMenuModel::message_button_bounds(PauseMenuAction button) const noexcept {
    const double left_x = message_has_two_buttons()
                              ? 400.0 - message_frame_width * 0.5 + message_button_spacing
                              : 400.0 - message_button_width * 0.5;
    const double x = button == PauseMenuAction::message_secondary
                         ? left_x + message_button_spacing + message_button_width
                         : left_x;
    return ui::Rect{static_cast<std::int32_t>(x),
                    static_cast<std::int32_t>(message_button_top),
                    static_cast<std::int32_t>(message_button_width),
                    static_cast<std::int32_t>(message_button_height)};
}

std::optional<PauseMenuAction>
PauseMenuModel::hit_test(std::optional<ui::Point> point) const noexcept {
    if (!point.has_value()) {
        return std::nullopt;
    }
    if (message_.has_value()) {
        for (const auto button :
             {PauseMenuAction::message_primary, PauseMenuAction::message_secondary}) {
            if (action_visible(button) && message_button_bounds(button).contains(*point)) {
                return button;
            }
        }
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
    list.reserve(16U + layout.size() * 4U);
    const bool host = model.environment().ugc_host;

    if (host) {
        // pause_menu_frame_big (pause_menu_frame_expanded, 530x720 @0.64)
        // blitted 30 px lower, at (400, 330) top-origin.
        list.push(ui::SpriteDrawCommand{
            "png/ui/in_game_menus/pause_menu_frame_expanded.png",
            ui::DrawRect{400.0 - 169.6, 330.0 - 230.4, 339.2, 460.8},
            ui::DrawSpace::design_pixels,
            ui::TextureFilter::linear,
            ui::TextureAnchor::center,
            0.64,
            ui::SpriteSizing::stretch,
            {},
        });
    } else {
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
    }
    // Title strings.PAUSE upper-cased, Spades 46; the host's title sits 10 px
    // lower (title_y = y + 150 instead of y + 160).
    list.push(ui::TextDrawCommand{
        "PAUSE",
        "fonts/Spades.ttf",
        ui::DrawRect{300.0, host ? 110.0 : 100.0, 200.0, 50.0},
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

    const auto append_button = [&](PauseMenuAction action, ui::Rect bounds,
                                   std::string_view label, double font_pixels) {
        const bool enabled = model.action_enabled(action);
        const bool pressed = model.pressed() == action;
        const bool hovered = model.hovered() == action;
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
        // TextButton.draw: a held button sinks its text by 2 source pixels of
        // the 97 px art (scaled 0.6), and a disabled one dims only its art.
        const double sink = pressed ? 2.0 * height / (97.0 * 0.6) : 0.0;
        list.push(ui::TextDrawCommand{
            std::string{label},
            "fonts/Spades.ttf",
            ui::DrawRect{x + 14.0, y + 4.0 + sink, width - 28.0, height - 8.0},
            ui::DrawSpace::design_pixels,
            font_pixels,
            0.0,
            1U,
            ui::HorizontalTextAlignment::center,
            ui::VerticalTextAlignment::retail_center,
            // set_text() replaces the upper-cased caption with the raw one.
            ui::TextTransform::preserve,
            ui::TextFit::shrink_to_fit,
            ui::ColorModulation{button_text_color, 1'000U, 1'000U},
        });
    };

    for (const auto& entry : layout) {
        if (!model.action_visible(entry.action)) {
            continue;
        }
        // big_button_aldo_font: Spades 36, shrunk to fit the 237x47 text box.
        append_button(entry.action, PauseMenuModel::action_bounds(entry.action), entry.label,
                      36.0);
    }

    if (const auto message = model.message(); message.has_value()) {
        // MessageBox(400, 300), DIALOG_WITH_BUTTONS: warning frame, the text
        // in big_standard_ui_font, then its one or two TextButtons (size 18).
        list.push(ui::SpriteDrawCommand{
            "png/ui/common_elements/frames/ui_frame_overlay_warning.png",
            ui::DrawRect{400.0 - 291.2, 300.0 - 163.2, 582.4, 326.4},
            ui::DrawSpace::design_pixels,
            ui::TextureFilter::linear,
            ui::TextureAnchor::center,
            0.64,
            ui::SpriteSizing::stretch,
            {},
        });
        const std::string_view text =
            *message == PauseMenuMessage::save_error         ? "UGC_MAP_SAVE_ERROR"
            : *message == PauseMenuMessage::save_before_quit ? "UGC_QUIT_WITHOUT_SAVING"
                                                             : "UGC_MAP_SAVE_SUCCESSFULLY";
        list.push(ui::TextDrawCommand{
            std::string{text},
            "fonts/A750-Sans-Medium.ttf",
            ui::DrawRect{400.0 - 291.2 + 54.0, 175.0, 582.4 - 108.0, 145.0},
            ui::DrawSpace::design_pixels,
            20.0,
            0.0,
            2U,
            ui::HorizontalTextAlignment::center,
            ui::VerticalTextAlignment::retail_center,
            ui::TextTransform::preserve,
            ui::TextFit::shrink_to_fit,
            ui::ColorModulation{menu_font_color, 1'000U, 1'000U},
        });
        const bool two = model.message_has_two_buttons();
        const std::string_view primary = !two ? "OK"
                                         : *message == PauseMenuMessage::save_error
                                             ? "RETRY"
                                             : "KICK_YES";
        const std::string_view secondary =
            *message == PauseMenuMessage::save_error ? "CANCEL" : "KICK_NO";
        append_button(PauseMenuAction::message_primary,
                      model.message_button_bounds(PauseMenuAction::message_primary), primary,
                      18.0);
        if (two) {
            append_button(PauseMenuAction::message_secondary,
                          model.message_button_bounds(PauseMenuAction::message_secondary),
                          secondary, 18.0);
        }
    }
    return list;
}

} // namespace battlespades::frontend
