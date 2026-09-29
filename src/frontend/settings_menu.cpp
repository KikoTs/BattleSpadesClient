#include "battlespades/frontend/settings_menu.hpp"

#include "battlespades/render/quality_profile.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace battlespades::frontend {
namespace {

constexpr ui::Rect panel_bounds{152, 133, 494, 293};
constexpr ui::Rect viewport_bounds{162, 143, 474, 273};
constexpr std::int32_t row_height{32};
constexpr std::int32_t category_height{26};
constexpr std::int32_t row_spacing{2};
constexpr std::int32_t scrollbar_reserved_width{32};
constexpr std::size_t dropdown_visible_rows{10U};
constexpr std::int32_t dropdown_option_height{20};
constexpr ui::Rect scrollbar_bounds{614, 143, 22, 273};
constexpr ui::Rect scrollbar_up_bounds{614, 143, 22, 22};
constexpr ui::Rect scrollbar_track_bounds{615, 167, 20, 225};
constexpr ui::Rect scrollbar_down_bounds{614, 394, 22, 22};

constexpr std::array<ui::Rect, 3U> tab_bounds{{
    {152, 100, 164, 33},
    {316, 100, 164, 33},
    {480, 100, 166, 33},
}};

constexpr std::array<std::string_view, 3U> tab_labels{{"MAIN", "GRAPHICS", "CONTROLS"}};

constexpr std::array<SettingsRowId, 10U> main_inventory{{
    SettingsRowId::language,
    SettingsRowId::master_volume,
    SettingsRowId::music_volume,
    SettingsRowId::fullscreen,
    SettingsRowId::invert_mouse,
    SettingsRowId::favorite_server,
    SettingsRowId::show_skins,
    SettingsRowId::show_other_skins,
    SettingsRowId::weapon_motion,
    SettingsRowId::ability_hints,
}};

constexpr std::array<SettingsRowId, 39U> controls_inventory{{
    // Python 2's stable iteration order in the shipped retail build assigns
    // the Map Creator group sort_order=0 and Main Game sort_order=1.
    SettingsRowId::ugc_controls_category,
    SettingsRowId::ugc_settings,
    SettingsRowId::tool_help,
    SettingsRowId::palette_left,
    SettingsRowId::palette_right,
    SettingsRowId::palette_up,
    SettingsRowId::palette_down,
    SettingsRowId::cancel_prefab_placement,
    SettingsRowId::carve_prefab,
    SettingsRowId::jetpack_hover,
    SettingsRowId::quick_save,
    SettingsRowId::main_controls_category,
    SettingsRowId::mouse_sensitivity,
    SettingsRowId::forward,
    SettingsRowId::backward,
    SettingsRowId::move_left,
    SettingsRowId::move_right,
    SettingsRowId::sneak,
    SettingsRowId::crouch,
    SettingsRowId::sprint,
    SettingsRowId::jump,
    SettingsRowId::fire_use,
    SettingsRowId::aim,
    SettingsRowId::reload,
    SettingsRowId::cycle_next_weapon,
    SettingsRowId::inventory_slots,
    SettingsRowId::team_chat,
    SettingsRowId::global_chat,
    SettingsRowId::show_map,
    SettingsRowId::view_scores,
    SettingsRowId::change_team,
    SettingsRowId::change_class,
    SettingsRowId::in_game_menu,
    SettingsRowId::pick_colour,
    SettingsRowId::map_vote_1,
    SettingsRowId::map_vote_2,
    SettingsRowId::map_vote_3,
    SettingsRowId::kick_player,
    SettingsRowId::toggle_hud,
}};

[[nodiscard]] constexpr bool valid_tab(settings::SettingsTab tab) noexcept {
    switch (tab) {
    case settings::SettingsTab::main:
    case settings::SettingsTab::graphics:
    case settings::SettingsTab::controls:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr std::size_t tab_index(settings::SettingsTab tab) noexcept {
    switch (tab) {
    case settings::SettingsTab::main:
        return 0U;
    case settings::SettingsTab::graphics:
        return 1U;
    case settings::SettingsTab::controls:
        return 2U;
    }
    return 0U;
}

[[nodiscard]] constexpr settings::SettingsTab tab_at(std::size_t index) noexcept {
    switch (index) {
    case 0U:
        return settings::SettingsTab::main;
    case 1U:
        return settings::SettingsTab::graphics;
    default:
        return settings::SettingsTab::controls;
    }
}

[[nodiscard]] constexpr bool is_category(SettingsRowId row) noexcept {
    return row == SettingsRowId::main_controls_category ||
           row == SettingsRowId::ugc_controls_category;
}

[[nodiscard]] constexpr bool is_fixed_binding(SettingsRowId row) noexcept {
    return row == SettingsRowId::fire_use || row == SettingsRowId::cycle_next_weapon ||
           row == SettingsRowId::inventory_slots;
}

[[nodiscard]] constexpr bool is_pointer_slider(SettingsRowId row) noexcept {
    return row == SettingsRowId::master_volume || row == SettingsRowId::music_volume ||
           row == SettingsRowId::mouse_sensitivity;
}

[[nodiscard]] constexpr std::int32_t height_for(SettingsRowId row) noexcept {
    return is_category(row) ? category_height : row_height;
}

[[nodiscard]] constexpr SettingsRowKind kind_for(SettingsRowId row) noexcept {
    if (is_category(row)) {
        return SettingsRowKind::category;
    }
    if (row == SettingsRowId::master_volume || row == SettingsRowId::music_volume) {
        return SettingsRowKind::stepped_slider;
    }
    if (row == SettingsRowId::mouse_sensitivity) {
        return SettingsRowKind::continuous_slider;
    }
    if (row == SettingsRowId::show_skins || row == SettingsRowId::show_other_skins ||
        row == SettingsRowId::weapon_motion || row == SettingsRowId::ability_hints ||
        row == SettingsRowId::fullscreen || row == SettingsRowId::favorite_server ||
        row == SettingsRowId::vsync || row == SettingsRowId::compatibility_shader) {
        return SettingsRowKind::toggle;
    }
    if (row == SettingsRowId::language || row == SettingsRowId::invert_mouse ||
        row == SettingsRowId::resolution ||
        row == SettingsRowId::graphics_api || row == SettingsRowId::antialiasing ||
        row == SettingsRowId::effect_quality || row == SettingsRowId::draw_distance ||
        row == SettingsRowId::shader_quality || row == SettingsRowId::texture_quality ||
        row == SettingsRowId::model_quality) {
        return SettingsRowKind::choice;
    }
    if (is_fixed_binding(row)) {
        return SettingsRowKind::fixed_binding;
    }
    return SettingsRowKind::binding;
}

[[nodiscard]] constexpr std::string_view label_for(SettingsRowId row) noexcept {
    switch (row) {
    case SettingsRowId::language:
        return "LANGUAGE";
    case SettingsRowId::master_volume:
        return "MASTER_VOLUME";
    case SettingsRowId::music_volume:
        return "MUSIC_VOLUME";
    case SettingsRowId::fullscreen:
        return "FULLSCREEN";
    case SettingsRowId::invert_mouse:
        return "INVERT_MOUSE";
    case SettingsRowId::favorite_server:
        return "FAVORITE";
    case SettingsRowId::show_skins:
        return "SHOW_SKINS";
    case SettingsRowId::show_other_skins:
        return "SHOW_OTHER_SKINS";
    case SettingsRowId::weapon_motion:
        return "WEAPON_MOTION";
    case SettingsRowId::ability_hints:
        return "ABILITY_HINTS";
    case SettingsRowId::resolution:
        return "RESOLUTION";
    case SettingsRowId::graphics_api:
        return "GRAPHICS_API";
    case SettingsRowId::antialiasing:
        return "ANTIALIAS";
    case SettingsRowId::effect_quality:
        return "EFFECT_QUALITY";
    case SettingsRowId::draw_distance:
        return "DRAW_DISTANCE";
    case SettingsRowId::shader_quality:
        return "SHADER_QUALITY";
    case SettingsRowId::texture_quality:
        return "TEXTURE_QUALITY";
    case SettingsRowId::model_quality:
        return "GRAPHICS_QUALITY";
    case SettingsRowId::vsync:
        return "VSYNC";
    case SettingsRowId::compatibility_shader:
        // Keep the shipped localization identifier's historical misspelling.
        return "COMPATIBILTY_SHADER";
    case SettingsRowId::main_controls_category:
        return "MAIN_GAME_CONTROLS";
    case SettingsRowId::mouse_sensitivity:
        return "MOUSE_SENSITIVITY";
    case SettingsRowId::forward:
        return "FORWARD";
    case SettingsRowId::backward:
        return "BACKWARD";
    case SettingsRowId::move_left:
        return "LEFT";
    case SettingsRowId::move_right:
        return "RIGHT";
    case SettingsRowId::sneak:
        return "SNEAK";
    case SettingsRowId::crouch:
        return "CROUCH";
    case SettingsRowId::sprint:
        return "SPRINT";
    case SettingsRowId::jump:
        return "JUMP";
    case SettingsRowId::fire_use:
        return "FIRE_USE";
    case SettingsRowId::aim:
        return "AIM";
    case SettingsRowId::reload:
        return "RELOAD";
    case SettingsRowId::cycle_next_weapon:
        return "CYCLE_NEXT_WEAPON";
    case SettingsRowId::inventory_slots:
        return "INVENTORY_SLOTS";
    case SettingsRowId::team_chat:
        return "TEAM_CHAT";
    case SettingsRowId::global_chat:
        return "GLOBAL_CHAT";
    case SettingsRowId::show_map:
        return "VIEW_MAP";
    case SettingsRowId::view_scores:
        return "VIEW_SCORES";
    case SettingsRowId::change_team:
        return "CHANGE_TEAM";
    case SettingsRowId::change_class:
        return "CHANGE_CLASS";
    case SettingsRowId::in_game_menu:
        return "IN_GAME_MENU";
    case SettingsRowId::pick_colour:
        return "PICK_COLOUR";
    case SettingsRowId::map_vote_1:
        return "MAP_VOTE_1";
    case SettingsRowId::map_vote_2:
        return "MAP_VOTE_2";
    case SettingsRowId::map_vote_3:
        return "MAP_VOTE_3";
    case SettingsRowId::kick_player:
        return "KICK_PLAYER";
    case SettingsRowId::toggle_hud:
        return "TOGGLE_HUD";
    case SettingsRowId::ugc_controls_category:
        return "UGC_CONTROLS";
    case SettingsRowId::ugc_settings:
        return "UGC_GAME_SETTINGS_KEY";
    case SettingsRowId::tool_help:
        return "TOOL_HELP";
    case SettingsRowId::palette_left:
        return "PALETTE_LEFT";
    case SettingsRowId::palette_right:
        return "PALETTE_RIGHT";
    case SettingsRowId::palette_up:
        return "PALETTE_UP";
    case SettingsRowId::palette_down:
        return "PALETTE_DOWN";
    case SettingsRowId::cancel_prefab_placement:
        return "CANCEL_PREFAB_PLACEMENT";
    case SettingsRowId::carve_prefab:
        return "CARVE_PREFAB";
    case SettingsRowId::jetpack_hover:
        return "HOVER_INPUT";
    case SettingsRowId::quick_save:
        return "SAVE";
    }
    return {};
}

[[nodiscard]] constexpr ui::Rect control_bounds_for(ui::Rect bounds) noexcept {
    if (!bounds.is_valid() || bounds.width == 0 || bounds.height == 0) {
        return {};
    }
    const auto name_width = bounds.width / 3;
    return {bounds.x + name_width + 28,
            bounds.y + 4,
            bounds.width - name_width - 42,
            bounds.height - 8};
}

[[nodiscard]] constexpr std::string_view quality_text(std::size_t index) noexcept {
    constexpr std::array values{"LOW", "MEDIUM", "HIGH"};
    return values[std::min(index, values.size() - 1U)];
}

[[nodiscard]] std::string sensitivity_text(double value) {
    // SliderControl shows str(round(value, 2)): Python drops trailing zeros
    // but always keeps one decimal, so 0.1 reads "0.1" and 1.0 reads "1.0".
    const auto hundredths = std::clamp(static_cast<int>(std::lround(value * 100.0)), 0, 100);
    const auto remainder = hundredths % 100;
    if (remainder % 10 == 0) {
        return std::to_string(hundredths / 100) + "." + std::to_string(remainder / 10);
    }
    return std::to_string(hundredths / 100) + "." + (remainder < 10 ? "0" : "") +
           std::to_string(remainder);
}

/** RangeBarControl geometry: 4 px padding, square arrows, the bar between them. */
struct RangeBarGeometry final {
    ui::Rect left_arrow;
    ui::Rect right_arrow;
    std::int32_t bar_left{};
    std::int32_t bar_right{};
};

[[nodiscard]] constexpr RangeBarGeometry range_bar_geometry(ui::Rect control) noexcept {
    constexpr std::int32_t option_spacing{4};
    constexpr std::int32_t button_bar_spacing{2};
    const auto arrow = std::max(0, control.height - option_spacing * 2);
    // The hit areas take the arrow plus its padding so the control's edge
    // pixels still belong to the arrow the player aimed at.
    return {ui::Rect{control.x, control.y, option_spacing + arrow, control.height},
            ui::Rect{control.x + control.width - option_spacing - arrow,
                     control.y,
                     option_spacing + arrow,
                     control.height},
            control.x + option_spacing + arrow + button_bar_spacing,
            control.x + control.width - (option_spacing + arrow + button_bar_spacing)};
}

/** SliderControl geometry with its edit box (sliderControl.update_position). */
struct SliderGeometry final {
    double track_x{};
    double track_width{};
    ui::Rect edit_box{};
};

[[nodiscard]] SliderGeometry slider_geometry(ui::Rect control) noexcept {
    constexpr double spacing{4.0};
    const auto box_width = static_cast<double>(control.width) / 6.0;
    const auto box_x = static_cast<double>(control.x + control.width) - spacing - box_width;
    return {static_cast<double>(control.x) + spacing * 2.0,
            static_cast<double>(control.width) - box_width - spacing * 5.0,
            ui::Rect{static_cast<std::int32_t>(std::floor(box_x)),
                     control.y + static_cast<std::int32_t>(spacing),
                     static_cast<std::int32_t>(std::ceil(box_width)),
                     control.height - static_cast<std::int32_t>(spacing) * 2}};
}

/** ToggleOptionControl halves: -1 off box, +1 on box, 0 the gap between. */
[[nodiscard]] std::int32_t toggle_half_at(ui::Rect control, ui::Point point) noexcept {
    if (point.y < control.y || point.y > control.y + control.height) {
        return 0;
    }
    const auto box_width = static_cast<double>(control.width) / 2.0 - 2.0;
    const auto x = static_cast<double>(point.x);
    if (x >= control.x && x <= control.x + box_width) {
        return -1;
    }
    const auto on_x = static_cast<double>(control.x + control.width) - box_width;
    if (x >= on_x && x <= control.x + control.width) {
        return 1;
    }
    return 0;
}

/** Choice rows react only to their SquareButton arrows (gui.py 1817-1880). */
[[nodiscard]] std::int32_t choice_arrow_at(ui::Rect control, ui::Point point) noexcept {
    const auto geometry = range_bar_geometry(control);
    if (geometry.left_arrow.contains(point)) return -1;
    if (geometry.right_arrow.contains(point)) return 1;
    return 0;
}

[[nodiscard]] double snapped_volume(double value) noexcept {
    // RangeBarControl.set: clamp, then anything under 0.01 is silence.
    const auto clamped = std::clamp(value, 0.0, 1.0);
    return clamped < 0.01 ? 0.0 : clamped;
}

[[nodiscard]] std::string resolution_text(settings::Resolution resolution) {
    // ScreenResolution.resolution_string is retail's compact "%dx%d" form.
    return std::to_string(resolution.width) + "x" + std::to_string(resolution.height);
}


[[nodiscard]] constexpr SettingsVisualState
visual_state_for(SettingsMenuTarget target,
                 bool enabled,
                 const std::optional<SettingsMenuTarget>& focused,
                 const std::optional<SettingsMenuTarget>& hovered,
                 const std::optional<SettingsMenuTarget>& pressed) noexcept {
    if (!enabled) {
        return SettingsVisualState::disabled;
    }
    if (pressed == target) {
        return SettingsVisualState::pressed;
    }
    if (hovered == target) {
        return SettingsVisualState::hovered;
    }
    if (focused == target) {
        return SettingsVisualState::focused;
    }
    return SettingsVisualState::normal;
}

[[nodiscard]] std::size_t total_height_from(const std::vector<SettingsRowId>& rows,
                                            std::size_t first) noexcept {
    if (first >= rows.size()) {
        return 0U;
    }
    std::size_t result{};
    for (auto index = first; index < rows.size(); ++index) {
        result += static_cast<std::size_t>(height_for(rows[index]));
        if (index + 1U < rows.size()) {
            result += static_cast<std::size_t>(row_spacing);
        }
    }
    return result;
}

[[nodiscard]] constexpr std::size_t quality_index(settings::QualityLevel value) noexcept {
    switch (value) {
    case settings::QualityLevel::low:
        return 0U;
    case settings::QualityLevel::medium:
        return 1U;
    case settings::QualityLevel::high:
        return 2U;
    }
    return 0U;
}

[[nodiscard]] constexpr settings::QualityLevel quality_at(std::size_t index) noexcept {
    switch (index) {
    case 0U:
        return settings::QualityLevel::low;
    case 1U:
        return settings::QualityLevel::medium;
    default:
        return settings::QualityLevel::high;
    }
}

[[nodiscard]] constexpr std::size_t draw_distance_index(settings::DrawDistance value) noexcept {
    switch (value) {
    case settings::DrawDistance::low:
        return 0U;
    case settings::DrawDistance::medium:
        return 1U;
    case settings::DrawDistance::high:
        return 2U;
    }
    return 0U;
}

[[nodiscard]] constexpr settings::DrawDistance draw_distance_at(std::size_t index) noexcept {
    switch (index) {
    case 0U:
        return settings::DrawDistance::low;
    case 1U:
        return settings::DrawDistance::medium;
    default:
        return settings::DrawDistance::high;
    }
}

// Four selectable tiers. `compatibility` is deliberately not on this row: the
// recovered retail Compatibility Shader toggle owns that value, and this row is
// disabled while the toggle is on. One constant so the display and mutation
// sides cannot drift apart.
constexpr std::size_t shader_choice_count{4U};

[[nodiscard]] constexpr std::size_t shader_index(settings::ShaderQuality value) noexcept {
    switch (value) {
    // Legacy shares index 0 only so a disabled row has somewhere to point; the
    // row reads LEGACY and cannot be cycled while the toggle holds it.
    case settings::ShaderQuality::compatibility:
    case settings::ShaderQuality::low:
        return 0U;
    case settings::ShaderQuality::medium:
        return 1U;
    case settings::ShaderQuality::high:
        return 2U;
    case settings::ShaderQuality::ultra:
        return 3U;
    }
    return 1U;
}

[[nodiscard]] constexpr settings::ShaderQuality shader_at(std::size_t index) noexcept {
    switch (index) {
    case 0U:
        return settings::ShaderQuality::low;
    case 1U:
        return settings::ShaderQuality::medium;
    case 2U:
        return settings::ShaderQuality::high;
    default:
        return settings::ShaderQuality::ultra;
    }
}

[[nodiscard]] constexpr std::size_t antialiasing_index(settings::Antialiasing value) noexcept {
    switch (value) {
    case settings::Antialiasing::off:
        return 0U;
    case settings::Antialiasing::samples_2:
        return 1U;
    case settings::Antialiasing::samples_4:
        return 2U;
    }
    return 0U;
}

[[nodiscard]] constexpr settings::Antialiasing antialiasing_at(std::size_t index) noexcept {
    switch (index) {
    case 0U:
        return settings::Antialiasing::off;
    case 1U:
        return settings::Antialiasing::samples_2;
    default:
        return settings::Antialiasing::samples_4;
    }
}

[[nodiscard]] constexpr std::string_view graphics_api_text(settings::GraphicsApi value) noexcept {
    switch (value) {
    case settings::GraphicsApi::automatic:
        return "AUTO";
    case settings::GraphicsApi::direct3d11:
        return "DIRECT3D 11";
    case settings::GraphicsApi::direct3d12:
        return "DIRECT3D 12";
    case settings::GraphicsApi::vulkan:
        return "VULKAN";
    case settings::GraphicsApi::opengl:
        return "OPENGL";
    case settings::GraphicsApi::metal:
        return "METAL";
    }
    return "AUTO";
}

[[nodiscard]] std::size_t
shifted_index(std::size_t current, std::size_t count, std::int32_t direction) noexcept {
    if (count == 0U || direction == 0) {
        return current;
    }
    if (direction < 0) {
        return current == 0U ? 0U : current - 1U;
    }
    return std::min(current + 1U, count - 1U);
}

} // namespace

std::string settings_binding_text(settings::InputBinding binding,
                                  const settings::RetailStringLookup& lookup) {
    // KeyControl.draw: None/'' text draws strings.NONE ("None").
    if (binding.is_unbound()) return "NONE";
    if (!lookup) {
        // No catalogue: submit the translate_key id and let the renderer
        // localize it ("COMMA", "CTRL", LEFT -> "Left").
        const auto id = settings::retail_binding_name(binding, {});
        return id.empty() ? std::string{"NONE"} : id;
    }
    const auto text = settings::retail_binding_name(binding, lookup);
    return std::string{literal_text_prefix} + (text.empty() ? std::string{"None"} : text);
}

std::string_view settings_control_action_label(settings::ControlAction action) noexcept {
    for (const auto row : controls_inventory) {
        if (settings_row_control_action(row) == action) return label_for(row);
    }
    return {};
}

std::optional<settings::ControlAction> settings_row_control_action(SettingsRowId row) noexcept {
    using settings::ControlAction;
    switch (row) {
    case SettingsRowId::forward:
        return ControlAction::forward;
    case SettingsRowId::backward:
        return ControlAction::backward;
    case SettingsRowId::move_left:
        return ControlAction::left;
    case SettingsRowId::move_right:
        return ControlAction::right;
    case SettingsRowId::sneak:
        return ControlAction::sneak;
    case SettingsRowId::crouch:
        return ControlAction::crouch;
    case SettingsRowId::sprint:
        return ControlAction::sprint;
    case SettingsRowId::jump:
        return ControlAction::jump;
    case SettingsRowId::aim:
        return ControlAction::aim;
    case SettingsRowId::reload:
        return ControlAction::reload;
    case SettingsRowId::team_chat:
        return ControlAction::team_chat;
    case SettingsRowId::global_chat:
        return ControlAction::global_chat;
    case SettingsRowId::show_map:
        return ControlAction::show_map;
    case SettingsRowId::view_scores:
        return ControlAction::view_scores;
    case SettingsRowId::change_team:
        return ControlAction::change_team;
    case SettingsRowId::change_class:
        return ControlAction::change_class;
    case SettingsRowId::in_game_menu:
        return ControlAction::menu;
    case SettingsRowId::pick_colour:
        return ControlAction::weapon_custom;
    case SettingsRowId::map_vote_1:
        return ControlAction::map_vote_1;
    case SettingsRowId::map_vote_2:
        return ControlAction::map_vote_2;
    case SettingsRowId::map_vote_3:
        return ControlAction::map_vote_3;
    case SettingsRowId::kick_player:
        return ControlAction::kick_player;
    case SettingsRowId::toggle_hud:
        return ControlAction::toggle_hud;
    case SettingsRowId::ugc_settings:
        return ControlAction::ugc_settings;
    case SettingsRowId::tool_help:
        return ControlAction::tool_help;
    case SettingsRowId::palette_left:
        return ControlAction::palette_left;
    case SettingsRowId::palette_right:
        return ControlAction::palette_right;
    case SettingsRowId::palette_up:
        return ControlAction::palette_up;
    case SettingsRowId::palette_down:
        return ControlAction::palette_down;
    case SettingsRowId::cancel_prefab_placement:
        return ControlAction::cancel_prefab_placement;
    case SettingsRowId::carve_prefab:
        return ControlAction::carve_prefab;
    case SettingsRowId::jetpack_hover:
        return ControlAction::hover;
    case SettingsRowId::quick_save:
        return ControlAction::quick_save;
    default:
        return std::nullopt;
    }
}

std::string_view settings_row_name(SettingsRowId row) noexcept {
    switch (row) {
#define AOS_SETTINGS_ROW_NAME(name)                                                                \
    case SettingsRowId::name:                                                                      \
        return #name
        AOS_SETTINGS_ROW_NAME(language);
        AOS_SETTINGS_ROW_NAME(master_volume);
        AOS_SETTINGS_ROW_NAME(music_volume);
        AOS_SETTINGS_ROW_NAME(fullscreen);
        AOS_SETTINGS_ROW_NAME(invert_mouse);
        AOS_SETTINGS_ROW_NAME(show_skins);
        AOS_SETTINGS_ROW_NAME(show_other_skins);
        AOS_SETTINGS_ROW_NAME(weapon_motion);
        AOS_SETTINGS_ROW_NAME(ability_hints);
        AOS_SETTINGS_ROW_NAME(favorite_server);
        AOS_SETTINGS_ROW_NAME(resolution);
        AOS_SETTINGS_ROW_NAME(graphics_api);
        AOS_SETTINGS_ROW_NAME(antialiasing);
        AOS_SETTINGS_ROW_NAME(effect_quality);
        AOS_SETTINGS_ROW_NAME(draw_distance);
        AOS_SETTINGS_ROW_NAME(shader_quality);
        AOS_SETTINGS_ROW_NAME(texture_quality);
        AOS_SETTINGS_ROW_NAME(model_quality);
        AOS_SETTINGS_ROW_NAME(vsync);
        AOS_SETTINGS_ROW_NAME(compatibility_shader);
        AOS_SETTINGS_ROW_NAME(main_controls_category);
        AOS_SETTINGS_ROW_NAME(mouse_sensitivity);
        AOS_SETTINGS_ROW_NAME(forward);
        AOS_SETTINGS_ROW_NAME(backward);
        AOS_SETTINGS_ROW_NAME(move_left);
        AOS_SETTINGS_ROW_NAME(move_right);
        AOS_SETTINGS_ROW_NAME(sneak);
        AOS_SETTINGS_ROW_NAME(crouch);
        AOS_SETTINGS_ROW_NAME(sprint);
        AOS_SETTINGS_ROW_NAME(jump);
        AOS_SETTINGS_ROW_NAME(fire_use);
        AOS_SETTINGS_ROW_NAME(aim);
        AOS_SETTINGS_ROW_NAME(reload);
        AOS_SETTINGS_ROW_NAME(cycle_next_weapon);
        AOS_SETTINGS_ROW_NAME(inventory_slots);
        AOS_SETTINGS_ROW_NAME(team_chat);
        AOS_SETTINGS_ROW_NAME(global_chat);
        AOS_SETTINGS_ROW_NAME(show_map);
        AOS_SETTINGS_ROW_NAME(view_scores);
        AOS_SETTINGS_ROW_NAME(change_team);
        AOS_SETTINGS_ROW_NAME(change_class);
        AOS_SETTINGS_ROW_NAME(in_game_menu);
        AOS_SETTINGS_ROW_NAME(pick_colour);
        AOS_SETTINGS_ROW_NAME(map_vote_1);
        AOS_SETTINGS_ROW_NAME(map_vote_2);
        AOS_SETTINGS_ROW_NAME(map_vote_3);
        AOS_SETTINGS_ROW_NAME(kick_player);
        AOS_SETTINGS_ROW_NAME(toggle_hud);
        AOS_SETTINGS_ROW_NAME(ugc_controls_category);
        AOS_SETTINGS_ROW_NAME(ugc_settings);
        AOS_SETTINGS_ROW_NAME(tool_help);
        AOS_SETTINGS_ROW_NAME(palette_left);
        AOS_SETTINGS_ROW_NAME(palette_right);
        AOS_SETTINGS_ROW_NAME(palette_up);
        AOS_SETTINGS_ROW_NAME(palette_down);
        AOS_SETTINGS_ROW_NAME(cancel_prefab_placement);
        AOS_SETTINGS_ROW_NAME(carve_prefab);
        AOS_SETTINGS_ROW_NAME(jetpack_hover);
        AOS_SETTINGS_ROW_NAME(quick_save);
#undef AOS_SETTINGS_ROW_NAME
    }
    return {};
}

SettingsMenuModel::SettingsMenuModel(settings::SettingsSession& session,
                                     SettingsMenuEnvironment environment)
    : session_{&session}, environment_{std::move(environment)},
      favorite_server_{environment_.favorite_server},
      initial_favorite_server_{environment_.favorite_server},
      focused_{SettingsMenuTarget::for_tab(settings::SettingsTab::main)} {
    set_languages(std::move(environment_.languages));
    auto& modes = environment_.display_modes;
    modes.erase(std::remove_if(modes.begin(),
                               modes.end(),
                               [](settings::Resolution resolution) {
                                   // Retail hid legacy modes below 640x480 from the dropdown.
                                   return resolution.width < 640U || resolution.height < 480U;
                               }),
                modes.end());
    std::sort(
        modes.begin(), modes.end(), [](settings::Resolution left, settings::Resolution right) {
            return left.width < right.width ||
                   (left.width == right.width && left.height < right.height);
        });
    modes.erase(std::unique(modes.begin(), modes.end()), modes.end());

    auto& graphics_apis = environment_.graphics_apis;
    graphics_apis.erase(std::remove_if(graphics_apis.begin(),
                                       graphics_apis.end(),
                                       [](settings::GraphicsApi api) {
                                           return settings::parse_graphics_api(
                                                      settings::graphics_api_name(api)) != api;
                                       }),
                        graphics_apis.end());
    std::vector<settings::GraphicsApi> unique_graphics_apis;
    unique_graphics_apis.reserve(graphics_apis.size() + 1U);
    unique_graphics_apis.push_back(settings::GraphicsApi::automatic);
    for (const auto api : graphics_apis) {
        if (std::find(unique_graphics_apis.begin(), unique_graphics_apis.end(), api) ==
            unique_graphics_apis.end()) {
            unique_graphics_apis.push_back(api);
        }
    }
    graphics_apis = std::move(unique_graphics_apis);

    // Remember the tier to come back to if the player turns Compatibility
    // Shader off. Opening the menu with legacy already stored keeps the old
    // observable behaviour of landing on High.
    const auto& initial_graphics = session_->draft().graphics;
    // With the Retail tier as the new-install default (D1), switching the
    // toggle off lands on Medium: the enhanced look is one click away.
    restore_shader_quality_ = initial_graphics.compatibility_shader()
                                  ? settings::ShaderQuality::medium
                                  : initial_graphics.shader_quality;

    const auto current = session_->draft().graphics.resolution;
    if (std::find(modes.begin(), modes.end(), current) == modes.end()) {
        modes.push_back(current);
        std::sort(
            modes.begin(), modes.end(), [](settings::Resolution left, settings::Resolution right) {
                return left.width < right.width ||
                       (left.width == right.width && left.height < right.height);
            });
    }
}

void SettingsMenuModel::set_languages(std::vector<SettingsLanguageOption> languages) {
    languages.erase(std::remove_if(languages.begin(), languages.end(), [](const auto& language) {
                        return language.locale.empty() || language.native_name.empty();
                    }),
                    languages.end());
    std::ranges::sort(languages, {}, &SettingsLanguageOption::locale);
    languages.erase(std::unique(languages.begin(), languages.end(), [](const auto& left,
                                                                       const auto& right) {
                        return left.locale == right.locale;
                    }),
                    languages.end());
    if (languages.empty()) languages.push_back({"en", "English"});
    environment_.languages = std::move(languages);
}

settings::SettingsTab SettingsMenuModel::active_tab() const noexcept {
    return active_tab_;
}

void SettingsMenuModel::set_active_tab(settings::SettingsTab tab) {
    if (!valid_tab(tab) || tab == active_tab_) {
        return;
    }
    if (sensitivity_edit_.has_value()) commit_text_edit();
    active_tab_ = tab;
    close_resolution_dropdown();
    cancel_binding_capture();
    pressed_.reset();
    dragged_slider_.reset();
    pressed_dropdown_option_.reset();
    scrollbar_capture_ = ScrollbarCapture::none;
    clamp_scroll(tab);
    focused_ = SettingsMenuTarget::for_tab(tab);
    hovered_ = pointer_.has_value() ? hit_test(*pointer_) : std::nullopt;
    effects_.emplace_back(SettingsSoundEffect{SettingsMenuSound::confirm});
}

std::vector<SettingsRowId> SettingsMenuModel::inventory(settings::SettingsTab tab) const {
    if (tab == settings::SettingsTab::main) {
        return {main_inventory.begin(), main_inventory.end()};
    }
    if (tab == settings::SettingsTab::graphics) {
        std::vector<SettingsRowId> result;
        result.reserve(10U);
        result.push_back(SettingsRowId::resolution);
        result.push_back(SettingsRowId::graphics_api);
        if (environment_.multisampling_supported) {
            result.push_back(SettingsRowId::antialiasing);
        }
        result.push_back(SettingsRowId::effect_quality);
        result.push_back(SettingsRowId::draw_distance);
        if (environment_.glsl_shader_quality_supported) {
            result.push_back(SettingsRowId::shader_quality);
        }
        result.push_back(SettingsRowId::texture_quality);
        result.push_back(SettingsRowId::model_quality);
        result.push_back(SettingsRowId::vsync);
        result.push_back(SettingsRowId::compatibility_shader);
        return result;
    }
    if (tab == settings::SettingsTab::controls) {
        return {controls_inventory.begin(), controls_inventory.end()};
    }
    return {};
}

std::vector<SettingsRowId> SettingsMenuModel::expanded_rows(settings::SettingsTab tab) const {
    auto rows = inventory(tab);
    if (tab != settings::SettingsTab::controls) {
        return rows;
    }

    std::vector<SettingsRowId> result;
    result.reserve(rows.size());
    bool include_children{};
    for (const auto row : rows) {
        if (row == SettingsRowId::main_controls_category) {
            include_children = main_controls_expanded_;
            result.push_back(row);
            continue;
        }
        if (row == SettingsRowId::ugc_controls_category) {
            include_children = ugc_controls_expanded_;
            result.push_back(row);
            continue;
        }
        if (include_children) {
            result.push_back(row);
        }
    }
    return result;
}

std::size_t SettingsMenuModel::maximum_scroll_index(settings::SettingsTab tab) const {
    const auto rows = expanded_rows(tab);
    if (rows.empty() ||
        total_height_from(rows, 0U) <= static_cast<std::size_t>(viewport_bounds.height)) {
        return 0U;
    }
    for (std::size_t index{1U}; index < rows.size(); ++index) {
        if (total_height_from(rows, index) <= static_cast<std::size_t>(viewport_bounds.height)) {
            return index;
        }
    }
    return rows.size() - 1U;
}

void SettingsMenuModel::clamp_scroll(settings::SettingsTab tab) {
    const auto index = tab_index(tab);
    scroll_indices_[index] = std::min(scroll_indices_[index], maximum_scroll_index(tab));
}

std::optional<ui::Rect> SettingsMenuModel::visible_row_bounds(SettingsRowId row) const {
    const auto rows = expanded_rows(active_tab_);
    auto index = std::min(scroll_indices_[tab_index(active_tab_)], rows.size());
    auto y = viewport_bounds.y;
    const auto has_scrollbar = maximum_scroll_index(active_tab_) > 0U;
    const auto width = viewport_bounds.width - (has_scrollbar ? scrollbar_reserved_width : 0);

    for (; index < rows.size(); ++index) {
        const auto height = height_for(rows[index]);
        if (y + height > viewport_bounds.y + viewport_bounds.height) {
            break;
        }
        const ui::Rect bounds{viewport_bounds.x, y, width, height};
        if (rows[index] == row) {
            return bounds;
        }
        y += height + row_spacing;
    }
    return std::nullopt;
}

std::size_t SettingsMenuModel::visible_row_count(settings::SettingsTab tab) const {
    const auto rows = expanded_rows(tab);
    auto index = std::min(scroll_indices_[tab_index(tab)], rows.size());
    auto y = viewport_bounds.y;
    std::size_t count{};
    for (; index < rows.size(); ++index) {
        const auto height = height_for(rows[index]);
        if (y + height > viewport_bounds.y + viewport_bounds.height) {
            break;
        }
        ++count;
        y += height + row_spacing;
    }
    return count;
}

ui::Rect SettingsMenuModel::dropdown_panel_bounds() const {
    if (!resolution_dropdown_open_) {
        return {};
    }
    const auto row_bounds = visible_row_bounds(SettingsRowId::resolution);
    if (!row_bounds.has_value() || environment_.display_modes.empty()) {
        return {};
    }
    const auto control = control_bounds_for(*row_bounds);
    const auto first =
        std::min(resolution_dropdown_first_index_, environment_.display_modes.size());
    const auto count = std::min(dropdown_visible_rows, environment_.display_modes.size() - first);
    const auto height = static_cast<std::int32_t>(count) * dropdown_option_height;
    const auto y = std::min(control.y + control.height,
                            SettingsMenuPresentation::reference_height - height - 4);
    return {control.x, y, control.width, height};
}

std::optional<std::size_t> SettingsMenuModel::dropdown_option_at(ui::Point point) const {
    const auto panel = dropdown_panel_bounds();
    if (!panel.is_valid() || !panel.contains(point) || panel.height <= 0) {
        return std::nullopt;
    }
    const auto local_index = static_cast<std::size_t>(
        std::clamp(point.y - panel.y, 0, panel.height - 1) / dropdown_option_height);
    const auto index = resolution_dropdown_first_index_ + local_index;
    if (index >= environment_.display_modes.size()) {
        return std::nullopt;
    }
    return index;
}

void SettingsMenuModel::open_resolution_dropdown() {
    if (active_tab_ != settings::SettingsTab::graphics ||
        !target_enabled(SettingsMenuTarget::for_row(SettingsRowId::resolution)) ||
        environment_.display_modes.empty()) {
        return;
    }
    const auto current = session_->draft().graphics.resolution;
    const auto found =
        std::find(environment_.display_modes.begin(), environment_.display_modes.end(), current);
    const auto selected =
        found == environment_.display_modes.end()
            ? std::size_t{}
            : static_cast<std::size_t>(std::distance(environment_.display_modes.begin(), found));
    const auto maximum_first = environment_.display_modes.size() > dropdown_visible_rows
                                   ? environment_.display_modes.size() - dropdown_visible_rows
                                   : std::size_t{};
    resolution_dropdown_first_index_ = std::min(selected, maximum_first);
    resolution_dropdown_open_ = true;
    effects_.emplace_back(SettingsSoundEffect{SettingsMenuSound::scroll});
}

void SettingsMenuModel::close_resolution_dropdown() noexcept {
    resolution_dropdown_open_ = false;
    pressed_dropdown_option_.reset();
}

ui::Rect SettingsMenuModel::scrollbar_thumb_bounds() const {
    if (maximum_scroll_index(active_tab_) == 0U) {
        return {};
    }
    const auto rows = expanded_rows(active_tab_);
    const auto visible = visible_row_count(active_tab_);
    const auto total = std::max<std::size_t>(1U, rows.size());
    const auto thumb_height =
        std::clamp(static_cast<std::int32_t>(
                       std::floor(static_cast<double>(scrollbar_track_bounds.height) *
                                  static_cast<double>(visible) / static_cast<double>(total))),
                   18,
                   scrollbar_track_bounds.height);
    const auto visual_maximum = rows.size() > visible ? rows.size() - visible : std::size_t{};
    const auto scroll = scroll_indices_[tab_index(active_tab_)];
    const auto ratio = visual_maximum == 0U
                           ? 0.0
                           : static_cast<double>(std::min(scroll, visual_maximum)) /
                                 static_cast<double>(visual_maximum);
    const auto travel = scrollbar_track_bounds.height - thumb_height;
    return {scrollbar_track_bounds.x,
            scrollbar_track_bounds.y + 1 +
                static_cast<std::int32_t>(std::floor(static_cast<double>(travel) * ratio)),
            scrollbar_track_bounds.width,
            thumb_height};
}

SettingsMenuModel::ScrollbarCapture SettingsMenuModel::scrollbar_hit_test(ui::Point point) const {
    if (maximum_scroll_index(active_tab_) == 0U || !scrollbar_bounds.contains(point)) {
        return ScrollbarCapture::none;
    }
    if (scrollbar_up_bounds.contains(point)) {
        return ScrollbarCapture::up_arrow;
    }
    if (scrollbar_down_bounds.contains(point)) {
        return ScrollbarCapture::down_arrow;
    }
    if (scrollbar_thumb_bounds().contains(point)) {
        return ScrollbarCapture::thumb;
    }
    if (scrollbar_track_bounds.contains(point)) {
        return ScrollbarCapture::track;
    }
    return ScrollbarCapture::none;
}

bool SettingsMenuModel::set_scroll_index(std::size_t value, bool play_sound) {
    auto& scroll = scroll_indices_[tab_index(active_tab_)];
    const auto bounded = std::min(value, maximum_scroll_index(active_tab_));
    if (scroll == bounded) {
        return false;
    }
    scroll = bounded;
    if (play_sound) {
        effects_.emplace_back(SettingsSoundEffect{SettingsMenuSound::scroll});
    }
    hovered_ = pointer_.has_value() ? hit_test(*pointer_) : std::nullopt;
    return true;
}

void SettingsMenuModel::scroll_from_track_pointer(ui::Point point, bool play_sound) {
    const auto thumb = scrollbar_thumb_bounds();
    const auto maximum = maximum_scroll_index(active_tab_);
    const auto travel = scrollbar_track_bounds.height - thumb.height;
    if (!thumb.is_valid() || maximum == 0U || travel <= 0) {
        return;
    }
    const auto desired =
        std::clamp(point.y - scrollbar_track_bounds.y - thumb.height / 2, 0, travel);
    const auto value = static_cast<std::size_t>(std::lround(
        static_cast<double>(desired) * static_cast<double>(maximum) / static_cast<double>(travel)));
    static_cast<void>(set_scroll_index(value, play_sound));
}

bool SettingsMenuModel::category_expanded(SettingsRowId category) const noexcept {
    if (category == SettingsRowId::main_controls_category) {
        return main_controls_expanded_;
    }
    if (category == SettingsRowId::ugc_controls_category) {
        return ugc_controls_expanded_;
    }
    return false;
}

bool SettingsMenuModel::target_enabled(SettingsMenuTarget target) const {
    switch (target.kind) {
    case SettingsTargetKind::tab:
        return valid_tab(target.tab);
    case SettingsTargetKind::defaults_button:
        return !(environment_.context == SettingsMenuContext::in_game &&
                 active_tab_ == settings::SettingsTab::graphics);
    case SettingsTargetKind::cancel_button:
    case SettingsTargetKind::done_button:
        return true;
    case SettingsTargetKind::row:
        break;
    }

    const auto rows = expanded_rows(active_tab_);
    if (std::find(rows.begin(), rows.end(), target.row) == rows.end()) {
        return false;
    }
    if (is_fixed_binding(target.row)) {
        return false;
    }
    if (target.row == SettingsRowId::favorite_server) {
        return environment_.context == SettingsMenuContext::in_game &&
               environment_.favorite_server_available;
    }
    if (active_tab_ == settings::SettingsTab::graphics &&
        environment_.context == SettingsMenuContext::in_game) {
        return false;
    }
    if (target.row == SettingsRowId::shader_quality &&
        session_->draft().graphics.compatibility_shader()) {
        // The recovered Compatibility Shader toggle owns
        // ShaderQuality::compatibility. Leaving the tier row live would give one
        // field two editors with different index bases, so it greys out and
        // reads LEGACY until the toggle is switched off.
        return false;
    }
    return true;
}

SettingsMenuPresentation SettingsMenuModel::presentation() const {
    SettingsMenuPresentation result;
    result.active_tab = active_tab_;
    result.panel_bounds = panel_bounds;
    result.viewport_bounds = viewport_bounds;
    result.scroll_index = scroll_indices_[tab_index(active_tab_)];
    result.maximum_scroll_index = maximum_scroll_index(active_tab_);
    result.in_game = environment_.context == SettingsMenuContext::in_game;
    result.dirty = session_->dirty() || favorite_server_ != initial_favorite_server_;
    result.tooltip_key = result.in_game && active_tab_ == settings::SettingsTab::graphics
                             ? "SETTINGS_GRAPHICS_DISABLED_MESSAGE"
                             : "SETTINGS_MESSAGE";
    result.focused = focused_;
    result.hovered = hovered_;

    for (std::size_t index{}; index < result.tabs.size(); ++index) {
        const auto tab = tab_at(index);
        const auto target = SettingsMenuTarget::for_tab(tab);
        result.tabs[index] = {tab,
                              tab_labels[index],
                              tab_bounds[index],
                              tab == active_tab_,
                              visual_state_for(target, true, focused_, hovered_, pressed_)};
    }

    const auto current = session_->draft();
    const auto rows = inventory(active_tab_);
    result.rows.reserve(rows.size());
    for (const auto row : rows) {
        SettingsRowPresentation item;
        item.id = row;
        item.kind = kind_for(row);
        item.label_key = label_for(row);
        item.control_action = settings_row_control_action(row);
        item.expanded = is_category(row) && category_expanded(row);
        const auto bounds = visible_row_bounds(row);
        item.visible = bounds.has_value();
        item.bounds = bounds.value_or(ui::Rect{});
        item.control_bounds = item.visible ? control_bounds_for(item.bounds) : ui::Rect{};
        const auto target = SettingsMenuTarget::for_row(row);
        item.enabled = target_enabled(target);

        switch (row) {
        case SettingsRowId::language: {
            const auto found = std::ranges::find(environment_.languages,
                                                 current.main.language,
                                                 &SettingsLanguageOption::locale);
            item.choice_index = found == environment_.languages.end()
                                    ? 0U
                                    : static_cast<std::size_t>(
                                          std::distance(environment_.languages.begin(), found));
            item.choice_count = environment_.languages.size();
            item.choices.reserve(item.choice_count);
            for (const auto& language : environment_.languages) {
                item.choices.push_back(language.native_name);
            }
            item.value_text = environment_.languages.empty()
                                  ? current.main.language
                                  : environment_.languages[item.choice_index].native_name;
            break;
        }
        case SettingsRowId::master_volume:
            item.scalar_value = current.main.master_volume;
            item.value_text =
                std::to_string(static_cast<int>(std::lround(current.main.master_volume * 100.0))) +
                "%";
            break;
        case SettingsRowId::music_volume:
            item.scalar_value = current.main.music_volume;
            item.value_text =
                std::to_string(static_cast<int>(std::lround(current.main.music_volume * 100.0))) +
                "%";
            break;
        case SettingsRowId::fullscreen:
            item.choice_index = current.main.fullscreen ? 1U : 0U;
            item.choice_count = 2U;
            item.choices = {"OFF", "ON"};
            item.value_text = current.main.fullscreen ? "ON" : "OFF";
            break;
        case SettingsRowId::show_skins:
        case SettingsRowId::show_other_skins:
        case SettingsRowId::weapon_motion:
        case SettingsRowId::ability_hints: {
            const bool on = row == SettingsRowId::show_skins ? current.main.show_skins :
                            row == SettingsRowId::show_other_skins ? current.main.show_other_skins :
                            row == SettingsRowId::ability_hints ? current.main.ability_hints :
                            current.main.weapon_motion;
            item.choice_index = on ? 1U : 0U;
            item.choice_count = 2U;
            item.choices = {"OFF", "ON"};
            item.value_text = on ? "ON" : "OFF";
            break;
        }
        case SettingsRowId::invert_mouse:
            item.choice_index = current.main.invert_mouse ? 1U : 0U;
            item.choice_count = 2U;
            item.choices = {"FALSE", "TRUE"};
            item.value_text = current.main.invert_mouse ? "TRUE" : "FALSE";
            break;
        case SettingsRowId::favorite_server:
            item.choice_index = favorite_server_ ? 1U : 0U;
            item.choice_count = 2U;
            item.choices = {"OFF", "ON"};
            item.value_text = favorite_server_ ? "ON" : "OFF";
            item.description = environment_.favorite_server_description.empty()
                                   ? (result.in_game ? std::string{} : "OPTION_ONLY_IN_PLAY")
                                   : environment_.favorite_server_description;
            break;
        case SettingsRowId::resolution: {
            const auto found = std::find(environment_.display_modes.begin(),
                                         environment_.display_modes.end(),
                                         current.graphics.resolution);
            item.choice_index = found == environment_.display_modes.end()
                                    ? 0U
                                    : static_cast<std::size_t>(
                                          std::distance(environment_.display_modes.begin(), found));
            item.choice_count = environment_.display_modes.size();
            item.dropdown_open = resolution_dropdown_open_;
            item.dropdown_first_index = resolution_dropdown_first_index_;
            item.dropdown_visible_count =
                std::min(dropdown_visible_rows,
                         item.choice_count > item.dropdown_first_index
                             ? item.choice_count - item.dropdown_first_index
                             : std::size_t{});
            item.value_text = resolution_text(current.graphics.resolution);
            item.choices.reserve(environment_.display_modes.size());
            for (const auto resolution : environment_.display_modes) {
                item.choices.push_back(resolution_text(resolution));
            }
            item.description = current.main.fullscreen ? "FULLSCREEN" : "WINDOWED";
            break;
        }
        case SettingsRowId::graphics_api: {
            const auto found = std::find(environment_.graphics_apis.begin(),
                                         environment_.graphics_apis.end(),
                                         current.graphics.graphics_api);
            item.choice_index = found == environment_.graphics_apis.end()
                                    ? 0U
                                    : static_cast<std::size_t>(
                                          std::distance(environment_.graphics_apis.begin(), found));
            item.choice_count = environment_.graphics_apis.size();
            item.choices.reserve(item.choice_count);
            for (const auto api : environment_.graphics_apis) {
                item.choices.emplace_back(graphics_api_text(api));
            }
            item.value_text =
                std::string{graphics_api_text(environment_.graphics_apis[item.choice_index])};
            item.description = "RESTART_REQUIRED";
            break;
        }
        case SettingsRowId::antialiasing:
            item.choice_index = antialiasing_index(current.graphics.antialiasing);
            item.choice_count = 3U;
            item.choices = {"OFF", "2", "4"};
            item.value_text =
                item.choice_index == 0U ? "OFF" : std::to_string(item.choice_index * 2U);
            break;
        case SettingsRowId::effect_quality:
            item.choice_index = quality_index(current.graphics.effect_quality);
            item.choice_count = 3U;
            item.choices = {"LOW", "MEDIUM", "HIGH"};
            item.value_text = quality_text(item.choice_index);
            break;
        case SettingsRowId::draw_distance:
            item.choice_index = draw_distance_index(current.graphics.draw_distance);
            item.choice_count = 3U;
            item.choices = {"LOW", "MEDIUM", "HIGH"};
            item.value_text = quality_text(item.choice_index);
            break;
        case SettingsRowId::shader_quality:
            item.choice_index = shader_index(current.graphics.shader_quality);
            item.choice_count = shader_choice_count;
            item.choices = {"LOW", "MEDIUM", "HIGH", "ULTRA"};
            // Sourced from the renderer's own tier namer so the menu label and
            // the debug overlay can never disagree about which tier is active.
            // While Compatibility Shader is on the stored tier IS legacy, so a
            // disabled row reading LEGACY keeps the two rows consistent.
            item.value_text =
                std::string{render::quality_profile_name(current.graphics.shader_quality)};
            break;
        case SettingsRowId::texture_quality:
            item.choice_index = quality_index(current.graphics.texture_quality);
            item.choice_count = 3U;
            item.choices = {"LOW", "MEDIUM", "HIGH"};
            item.value_text = quality_text(item.choice_index);
            item.description = "RESTART_REQUIRED";
            break;
        case SettingsRowId::model_quality:
            item.choice_index = quality_index(current.graphics.model_quality);
            item.choice_count = 3U;
            item.choices = {"LOW", "MEDIUM", "HIGH"};
            item.value_text = quality_text(item.choice_index);
            item.description = "RESTART_REQUIRED";
            break;
        case SettingsRowId::vsync:
            item.choice_index = current.graphics.vsync ? 1U : 0U;
            item.choice_count = 2U;
            item.choices = {"OFF", "ON"};
            item.value_text = current.graphics.vsync ? "ON" : "OFF";
            break;
        case SettingsRowId::compatibility_shader:
            item.choice_index = current.graphics.compatibility_shader() ? 1U : 0U;
            item.choice_count = 2U;
            item.choices = {"OFF", "ON"};
            item.value_text = current.graphics.compatibility_shader() ? "ON" : "OFF";
            break;
        case SettingsRowId::main_controls_category:
        case SettingsRowId::ugc_controls_category:
            item.value_text = item.expanded ? "EXPANDED" : "COLLAPSED";
            break;
        case SettingsRowId::mouse_sensitivity:
            item.scalar_value = current.controls.mouse_sensitivity;
            item.value_text = sensitivity_edit_.has_value()
                                  ? std::string{literal_text_prefix} + *sensitivity_edit_ + "_"
                                  : sensitivity_text(current.controls.mouse_sensitivity);
            item.text_editing = sensitivity_edit_.has_value();
            break;
        case SettingsRowId::fire_use:
            item.value_text = "LMB";
            break;
        case SettingsRowId::cycle_next_weapon:
            item.value_text = "MOUSE_WHEEL"; // strings.MOUSE_WHEEL = "MWheel"
            break;
        case SettingsRowId::inventory_slots:
            item.value_text = "1-9";
            break;
        default:
            if (item.control_action.has_value()) {
                item.value_text = settings_binding_text(
                    current.controls.binding(*item.control_action), key_name_lookup_);
            }
            break;
        }
        item.state = visual_state_for(target, item.enabled, focused_, hovered_, pressed_);
        if (item.kind == SettingsRowKind::toggle && row != SettingsRowId::favorite_server &&
            item.enabled && item.visible && pointer_.has_value()) {
            const auto half = toggle_half_at(item.control_bounds, *pointer_);
            item.unselected_half_hovered =
                (half < 0 && item.choice_index != 0U) || (half > 0 && item.choice_index == 0U);
        }
        result.rows.push_back(std::move(item));
    }

    const auto defaults_target = SettingsMenuTarget::button(SettingsTargetKind::defaults_button);
    const auto cancel_target = SettingsMenuTarget::button(SettingsTargetKind::cancel_button);
    const auto done_target = SettingsMenuTarget::button(SettingsTargetKind::done_button);
    const auto defaults_enabled = target_enabled(defaults_target);
    const auto in_game = environment_.context == SettingsMenuContext::in_game;
    const ui::Rect cancel_bounds =
        in_game ? ui::Rect{160, 480, 232, 41} : ui::Rect{152, 492, 240, 60};
    const ui::Rect done_bounds =
        in_game ? ui::Rect{403, 480, 232, 41} : ui::Rect{405, 492, 240, 60};
    result.buttons = {{
        {SettingsTargetKind::defaults_button,
         "DEFAULTS",
         {151, 434, 80, 30},
         defaults_enabled,
         visual_state_for(defaults_target, defaults_enabled, focused_, hovered_, pressed_)},
        {SettingsTargetKind::cancel_button,
         "CANCEL",
         cancel_bounds,
         true,
         visual_state_for(cancel_target, true, focused_, hovered_, pressed_)},
        {SettingsTargetKind::done_button,
         "DONE",
         done_bounds,
         true,
         visual_state_for(done_target, true, focused_, hovered_, pressed_)},
    }};

    if (binding_capture_.has_value()) {
        result.binding_capture = BindingCapturePresentation{
            *binding_capture_, binding_rejection_,
            binding_rejection_.has_value() ? binding_rejected_input_ : std::nullopt};
    }
    return result;
}

std::optional<SettingsMenuTarget> SettingsMenuModel::focused() const noexcept {
    return focused_;
}

bool SettingsMenuModel::set_focus(SettingsMenuTarget target) {
    if (!target_enabled(target)) {
        return false;
    }
    if (target.kind == SettingsTargetKind::row) {
        const auto rows = expanded_rows(active_tab_);
        if (std::find(rows.begin(), rows.end(), target.row) == rows.end()) {
            return false;
        }
    }
    focused_ = target;
    if (target.kind != SettingsTargetKind::row || target.row != SettingsRowId::resolution) {
        close_resolution_dropdown();
    }
    if (target.kind != SettingsTargetKind::row ||
        settings_row_control_action(target.row) != binding_capture_) {
        cancel_binding_capture();
    }
    reveal_focused_row();
    return true;
}

void SettingsMenuModel::reveal_focused_row() {
    if (!focused_.has_value() || focused_->kind != SettingsTargetKind::row) {
        return;
    }
    const auto rows = expanded_rows(active_tab_);
    const auto found = std::find(rows.begin(), rows.end(), focused_->row);
    if (found == rows.end()) {
        return;
    }
    const auto row_index = static_cast<std::size_t>(std::distance(rows.begin(), found));
    auto& scroll = scroll_indices_[tab_index(active_tab_)];
    if (row_index < scroll) {
        scroll = row_index;
    }
    const auto maximum = maximum_scroll_index(active_tab_);
    while (!visible_row_bounds(focused_->row).has_value() && scroll < maximum) {
        ++scroll;
    }
}

void SettingsMenuModel::repair_focus() {
    if (focused_.has_value() && target_enabled(*focused_)) {
        reveal_focused_row();
        return;
    }
    focused_ = SettingsMenuTarget::for_tab(active_tab_);
}

std::optional<SettingsMenuTarget> SettingsMenuModel::hit_test(ui::Point point) const {
    for (std::size_t index{}; index < tab_bounds.size(); ++index) {
        if (tab_bounds[index].contains(point)) {
            return SettingsMenuTarget::for_tab(tab_at(index));
        }
    }

    const auto in_game = environment_.context == SettingsMenuContext::in_game;
    const std::array<std::pair<SettingsTargetKind, ui::Rect>, 3U> buttons{{
        {SettingsTargetKind::defaults_button, {151, 434, 80, 30}},
        {SettingsTargetKind::cancel_button,
         in_game ? ui::Rect{160, 480, 232, 41} : ui::Rect{152, 492, 240, 60}},
        {SettingsTargetKind::done_button,
         in_game ? ui::Rect{403, 480, 232, 41} : ui::Rect{405, 492, 240, 60}},
    }};
    for (const auto& [id, bounds] : buttons) {
        if (bounds.contains(point)) {
            return SettingsMenuTarget::button(id);
        }
    }

    for (const auto row : inventory(active_tab_)) {
        const auto bounds = visible_row_bounds(row);
        if (bounds.has_value() && bounds->contains(point)) {
            return SettingsMenuTarget::for_row(row);
        }
    }
    return std::nullopt;
}

void SettingsMenuModel::pointer_move(std::optional<ui::Point> point) {
    pointer_ = point;
    if (!point.has_value()) {
        hovered_.reset();
    } else if (resolution_dropdown_open_ && dropdown_option_at(*point).has_value()) {
        hovered_ = SettingsMenuTarget::for_row(SettingsRowId::resolution);
    } else if (scrollbar_hit_test(*point) != ScrollbarCapture::none) {
        hovered_.reset();
    } else {
        hovered_ = hit_test(*point);
    }
}

void SettingsMenuModel::pointer_press(ui::Point point) {
    pointer_ = point;
    pressed_range_arrow_ = 0;
    if (sensitivity_edit_.has_value()) {
        // EditBoxControl.on_mouse_press: a press inside keeps focus; anywhere
        // else drops it, which runs on_return and commits the typed value.
        const auto bounds = visible_row_bounds(SettingsRowId::mouse_sensitivity);
        if (bounds.has_value() &&
            slider_geometry(control_bounds_for(*bounds)).edit_box.contains(point)) {
            return;
        }
        commit_text_edit();
    }
    if (resolution_dropdown_open_) {
        pressed_.reset();
        dragged_slider_.reset();
        scrollbar_capture_ = ScrollbarCapture::none;
        if (const auto option = dropdown_option_at(point); option.has_value()) {
            pressed_dropdown_option_ = option;
            hovered_ = SettingsMenuTarget::for_row(SettingsRowId::resolution);
            return;
        }
        const auto resolution_bounds = visible_row_bounds(SettingsRowId::resolution);
        if (resolution_bounds.has_value() &&
            control_bounds_for(*resolution_bounds).contains(point)) {
            pressed_ = SettingsMenuTarget::for_row(SettingsRowId::resolution);
            hovered_ = pressed_;
            return;
        }
        close_resolution_dropdown();
        hovered_ = hit_test(point);
        return;
    }

    scrollbar_capture_ = scrollbar_hit_test(point);
    if (scrollbar_capture_ != ScrollbarCapture::none) {
        hovered_.reset();
        pressed_.reset();
        dragged_slider_.reset();
        pressed_dropdown_option_.reset();
        return;
    }

    hovered_ = hit_test(point);
    pressed_.reset();
    dragged_slider_.reset();
    if (!hovered_.has_value() || !target_enabled(*hovered_)) {
        return;
    }

    static_cast<void>(set_focus(*hovered_));
    pressed_ = hovered_;
    if (hovered_->kind == SettingsTargetKind::tab) {
        set_active_tab(hovered_->tab);
        pressed_ = hovered_;
        return;
    }
    if (hovered_->kind == SettingsTargetKind::row && is_pointer_slider(hovered_->row)) {
        const auto row = hovered_->row;
        const auto bounds = visible_row_bounds(row);
        if (!bounds.has_value()) return;
        const auto control = control_bounds_for(*bounds);
        if (row == SettingsRowId::mouse_sensitivity) {
            if (slider_geometry(control).edit_box.contains(point)) {
                sensitivity_edit_ = sensitivity_text(session_->draft().controls.mouse_sensitivity);
                return;
            }
        } else {
            const auto geometry = range_bar_geometry(control);
            if (geometry.left_arrow.contains(point)) {
                pressed_range_arrow_ = -1;
                return;
            }
            if (geometry.right_arrow.contains(point)) {
                pressed_range_arrow_ = 1;
                return;
            }
        }
        dragged_slider_ = row;
        static_cast<void>(set_slider_from_pointer(*dragged_slider_, point, true));
    }
}

void SettingsMenuModel::pointer_drag(ui::Point point) {
    pointer_ = point;
    if (scrollbar_capture_ == ScrollbarCapture::thumb) {
        scroll_from_track_pointer(point, false);
        hovered_.reset();
        return;
    }
    if (pressed_dropdown_option_.has_value()) {
        hovered_ = dropdown_option_at(point).has_value()
                       ? std::optional{SettingsMenuTarget::for_row(SettingsRowId::resolution)}
                       : std::nullopt;
        return;
    }
    hovered_ = hit_test(point);
    if (dragged_slider_.has_value()) {
        static_cast<void>(set_slider_from_pointer(*dragged_slider_, point, false));
    }
}

void SettingsMenuModel::pointer_release(ui::Point point) {
    pointer_ = point;
    if (pressed_dropdown_option_.has_value()) {
        const auto pressed_option = *pressed_dropdown_option_;
        const auto released_option = dropdown_option_at(point);
        if (released_option == pressed_option &&
            pressed_option < environment_.display_modes.size()) {
            const auto before = session_->draft();
            auto graphics = before.graphics;
            graphics.resolution = environment_.display_modes[pressed_option];
            session_->set_graphics(graphics);
            if (session_->draft() != before) {
                effects_.emplace_back(SettingsSoundEffect{SettingsMenuSound::scroll});
                emit_preview(SettingsRowId::resolution);
            }
        }
        close_resolution_dropdown();
        hovered_ = hit_test(point);
        pressed_.reset();
        dragged_slider_.reset();
        return;
    }

    if (scrollbar_capture_ != ScrollbarCapture::none) {
        const auto captured = scrollbar_capture_;
        const auto released = scrollbar_hit_test(point);
        if (captured == ScrollbarCapture::up_arrow && released == captured) {
            const auto current = scroll_indices_[tab_index(active_tab_)];
            static_cast<void>(set_scroll_index(current == 0U ? 0U : current - 1U, true));
        } else if (captured == ScrollbarCapture::down_arrow && released == captured) {
            static_cast<void>(set_scroll_index(scroll_indices_[tab_index(active_tab_)] + 1U, true));
        } else if (captured == ScrollbarCapture::track && scrollbar_track_bounds.contains(point)) {
            scroll_from_track_pointer(point, true);
        } else if (captured == ScrollbarCapture::thumb) {
            scroll_from_track_pointer(point, false);
        }
        scrollbar_capture_ = ScrollbarCapture::none;
        hovered_ = hit_test(point);
        return;
    }

    hovered_ = hit_test(point);
    if (pressed_range_arrow_ != 0) {
        // SquareButton fires on release, and only over the arrow it grabbed.
        const auto direction = pressed_range_arrow_;
        pressed_range_arrow_ = 0;
        if (pressed_.has_value() && pressed_->kind == SettingsTargetKind::row) {
            const auto bounds = visible_row_bounds(pressed_->row);
            if (bounds.has_value()) {
                const auto geometry = range_bar_geometry(control_bounds_for(*bounds));
                const auto& arrow = direction < 0 ? geometry.left_arrow : geometry.right_arrow;
                if (arrow.contains(point)) {
                    static_cast<void>(step_volume(pressed_->row, direction));
                }
            }
        }
    } else if (dragged_slider_.has_value()) {
        static_cast<void>(set_slider_from_pointer(*dragged_slider_, point, false));
    } else if (pressed_.has_value() && hovered_ == pressed_ && target_enabled(*pressed_)) {
        auto activate_pressed = true;
        if (pressed_->kind == SettingsTargetKind::row && !is_category(pressed_->row)) {
            const auto bounds = visible_row_bounds(pressed_->row);
            activate_pressed = bounds.has_value() && control_bounds_for(*bounds).contains(point);
            // Sliders act on press/drag only; a release in their gaps is inert.
            if (is_pointer_slider(pressed_->row)) activate_pressed = false;
        }
        if (activate_pressed && pressed_->kind == SettingsTargetKind::row &&
            kind_for(pressed_->row) == SettingsRowKind::toggle &&
            pressed_->row != SettingsRowId::favorite_server) {
            // ToggleOptionControl sets the half that was clicked, never flips.
            const auto bounds = visible_row_bounds(pressed_->row);
            const auto half = toggle_half_at(control_bounds_for(*bounds), point);
            if (half != 0) static_cast<void>(adjust_row(pressed_->row, half));
            activate_pressed = false;
        }
        if (activate_pressed) {
            if (pressed_->kind == SettingsTargetKind::row &&
                kind_for(pressed_->row) == SettingsRowKind::choice) {
                const auto bounds = visible_row_bounds(pressed_->row);
                const auto control = control_bounds_for(*bounds);
                if (pressed_->row == SettingsRowId::resolution) {
                    if (resolution_dropdown_open_) {
                        close_resolution_dropdown();
                        effects_.emplace_back(SettingsSoundEffect{SettingsMenuSound::scroll});
                    } else {
                        open_resolution_dropdown();
                    }
                } else {
                    // Retail choice rows react only to their two arrows; the
                    // value text between them is not a button.
                    const auto direction = choice_arrow_at(control, point);
                    if (direction != 0) {
                        static_cast<void>(adjust_row(pressed_->row, direction));
                    }
                }
            } else {
                static_cast<void>(activate(*pressed_));
            }
        }
    }
    pressed_.reset();
    dragged_slider_.reset();
}

void SettingsMenuModel::cancel_pointer_capture() noexcept {
    pressed_range_arrow_ = 0;
    pressed_.reset();
    dragged_slider_.reset();
    pressed_dropdown_option_.reset();
    scrollbar_capture_ = ScrollbarCapture::none;
    close_resolution_dropdown();
}

bool SettingsMenuModel::mouse_wheel(ui::Point point, std::int32_t vertical_steps) {
    if (vertical_steps == 0) {
        return false;
    }

    if (resolution_dropdown_open_ &&
        (dropdown_panel_bounds().contains(point) ||
         (visible_row_bounds(SettingsRowId::resolution).has_value() &&
          control_bounds_for(*visible_row_bounds(SettingsRowId::resolution)).contains(point)))) {
        const auto maximum_first = environment_.display_modes.size() > dropdown_visible_rows
                                       ? environment_.display_modes.size() - dropdown_visible_rows
                                       : std::size_t{};
        const auto proposed = static_cast<std::int64_t>(resolution_dropdown_first_index_) -
                              static_cast<std::int64_t>(vertical_steps);
        const auto bounded =
            std::clamp<std::int64_t>(proposed, 0, static_cast<std::int64_t>(maximum_first));
        if (resolution_dropdown_first_index_ != static_cast<std::size_t>(bounded)) {
            resolution_dropdown_first_index_ = static_cast<std::size_t>(bounded);
            effects_.emplace_back(SettingsSoundEffect{SettingsMenuSound::scroll});
        }
        return true;
    }

    if (!viewport_bounds.contains(point)) {
        return false;
    }
    const auto scroll = scroll_indices_[tab_index(active_tab_)];
    const auto proposed =
        static_cast<std::int64_t>(scroll) - static_cast<std::int64_t>(vertical_steps);
    const auto bounded = std::clamp<std::int64_t>(
        proposed, 0, static_cast<std::int64_t>(maximum_scroll_index(active_tab_)));
    static_cast<void>(set_scroll_index(static_cast<std::size_t>(bounded), true));
    return true;
}

bool SettingsMenuModel::set_category_expanded(SettingsRowId category, bool expanded) {
    bool* state{};
    if (category == SettingsRowId::main_controls_category) {
        state = &main_controls_expanded_;
    } else if (category == SettingsRowId::ugc_controls_category) {
        state = &ugc_controls_expanded_;
    } else {
        return false;
    }
    if (*state == expanded) {
        return true;
    }
    *state = expanded;
    if (!expanded && focused_.has_value() && focused_->kind == SettingsTargetKind::row) {
        const auto focused_row = focused_->row;
        const auto focused_position =
            std::find(controls_inventory.begin(), controls_inventory.end(), focused_row);
        const auto category_position =
            std::find(controls_inventory.begin(), controls_inventory.end(), category);
        const auto next_category_position = category == SettingsRowId::main_controls_category
                                                ? std::find(controls_inventory.begin(),
                                                            controls_inventory.end(),
                                                            SettingsRowId::ugc_controls_category)
                                                : controls_inventory.end();
        if (focused_position != controls_inventory.end() &&
            category_position != controls_inventory.end() && focused_position > category_position &&
            focused_position < next_category_position) {
            focused_ = SettingsMenuTarget::for_row(category);
        }
    }
    clamp_scroll(settings::SettingsTab::controls);
    repair_focus();
    effects_.emplace_back(SettingsSoundEffect{SettingsMenuSound::scroll});
    hovered_ = pointer_.has_value() ? hit_test(*pointer_) : std::nullopt;
    return true;
}

void SettingsMenuModel::begin_binding_capture(SettingsRowId row) {
    const auto action = settings_row_control_action(row);
    if (!action.has_value() || !target_enabled(SettingsMenuTarget::for_row(row))) {
        return;
    }
    binding_capture_ = action;
    binding_rejection_.reset();
    focused_ = SettingsMenuTarget::for_row(row);
}

bool SettingsMenuModel::activate(SettingsMenuTarget target) {
    if (!target_enabled(target)) {
        return false;
    }
    switch (target.kind) {
    case SettingsTargetKind::tab:
        set_active_tab(target.tab);
        return true;
    case SettingsTargetKind::defaults_button:
        activate_defaults();
        return true;
    case SettingsTargetKind::cancel_button:
        activate_cancel();
        return true;
    case SettingsTargetKind::done_button:
        activate_done();
        return true;
    case SettingsTargetKind::row:
        break;
    }

    if (is_category(target.row)) {
        return set_category_expanded(target.row, !category_expanded(target.row));
    }
    if (settings_row_control_action(target.row).has_value()) {
        begin_binding_capture(target.row);
        return true;
    }
    if (is_fixed_binding(target.row)) {
        return false;
    }

    const auto& draft = session_->draft();
    switch (target.row) {
    case SettingsRowId::fullscreen:
        return adjust_row(target.row, draft.main.fullscreen ? -1 : 1);
    case SettingsRowId::invert_mouse:
        return adjust_row(target.row, draft.main.invert_mouse ? -1 : 1);
    case SettingsRowId::show_skins:
        return adjust_row(target.row, draft.main.show_skins ? -1 : 1);
    case SettingsRowId::show_other_skins:
        return adjust_row(target.row, draft.main.show_other_skins ? -1 : 1);
    case SettingsRowId::weapon_motion:
        return adjust_row(target.row, draft.main.weapon_motion ? -1 : 1);
    case SettingsRowId::ability_hints:
        return adjust_row(target.row, draft.main.ability_hints ? -1 : 1);
    case SettingsRowId::favorite_server:
        return adjust_row(target.row, favorite_server_ ? -1 : 1);
    case SettingsRowId::vsync:
        return adjust_row(target.row, draft.graphics.vsync ? -1 : 1);
    case SettingsRowId::compatibility_shader:
        return adjust_row(target.row, draft.graphics.compatibility_shader() ? -1 : 1);
    case SettingsRowId::resolution:
        if (resolution_dropdown_open_) {
            close_resolution_dropdown();
        } else {
            open_resolution_dropdown();
        }
        return true;
    default:
        return adjust_row(target.row, 1);
    }
}

bool SettingsMenuModel::handle(ui::InputEvent event) {
    if (!event.triggers_action()) {
        return false;
    }
    if (binding_capture_.has_value()) {
        if (event.action == ui::InputAction::cancel) {
            cancel_binding_capture();
        }
        // Raw input owns the capture; semantic navigation cannot leak through it.
        return true;
    }
    if (sensitivity_edit_.has_value()) {
        if (event.action == ui::InputAction::cancel) {
            sensitivity_edit_.reset();
            activate_menu_key();
            return true;
        }
        if (event.action == ui::InputAction::activate) {
            commit_text_edit();
            return true;
        }
        if (event.action == ui::InputAction::navigate_left ||
            event.action == ui::InputAction::navigate_right) {
            return true; // caret keys stay inside the box
        }
        commit_text_edit();
    }

    std::vector<SettingsMenuTarget> order;
    order.reserve(3U + expanded_rows(active_tab_).size() + 3U);
    for (std::size_t index{}; index < 3U; ++index) {
        order.push_back(SettingsMenuTarget::for_tab(tab_at(index)));
    }
    for (const auto row : expanded_rows(active_tab_)) {
        const auto target = SettingsMenuTarget::for_row(row);
        if (target_enabled(target)) {
            order.push_back(target);
        }
    }
    for (const auto kind : {SettingsTargetKind::defaults_button,
                            SettingsTargetKind::cancel_button,
                            SettingsTargetKind::done_button}) {
        const auto target = SettingsMenuTarget::button(kind);
        if (target_enabled(target)) {
            order.push_back(target);
        }
    }

    const auto move_linear = [&](bool backwards) {
        if (order.empty()) {
            return;
        }
        const auto found =
            focused_.has_value() ? std::find(order.begin(), order.end(), *focused_) : order.end();
        std::size_t index{};
        if (found != order.end()) {
            index = static_cast<std::size_t>(std::distance(order.begin(), found));
            index = backwards ? (index == 0U ? order.size() - 1U : index - 1U)
                              : (index + 1U) % order.size();
        } else if (backwards) {
            index = order.size() - 1U;
        }
        static_cast<void>(set_focus(order[index]));
    };

    switch (event.action) {
    case ui::InputAction::focus_next:
    case ui::InputAction::navigate_down:
        move_linear(false);
        return true;
    case ui::InputAction::focus_previous:
    case ui::InputAction::navigate_up:
        move_linear(true);
        return true;
    case ui::InputAction::navigate_left:
    case ui::InputAction::navigate_right: {
        const auto direction = event.action == ui::InputAction::navigate_left ? -1 : 1;
        if (focused_.has_value() && focused_->kind == SettingsTargetKind::row) {
            if (is_category(focused_->row)) {
                return set_category_expanded(focused_->row, direction > 0);
            }
            return adjust_row(focused_->row, direction);
        }
        if (focused_.has_value() && focused_->kind == SettingsTargetKind::tab) {
            const auto current = static_cast<std::int32_t>(tab_index(focused_->tab));
            const auto selected = std::clamp(current + direction, 0, 2);
            set_active_tab(tab_at(static_cast<std::size_t>(selected)));
            return true;
        }
        move_linear(direction < 0);
        return true;
    }
    case ui::InputAction::activate:
        return focused_.has_value() && activate(*focused_);
    case ui::InputAction::cancel:
        // Escape is the default Menu binding (settingsMenu.on_key_press).
        activate_menu_key();
        return true;
    }
    return false;
}

bool SettingsMenuModel::adjust_row(SettingsRowId row, std::int32_t direction) {
    if (direction == 0 || !target_enabled(SettingsMenuTarget::for_row(row)) || is_category(row) ||
        is_fixed_binding(row) || settings_row_control_action(row).has_value()) {
        return false;
    }
    if (row == SettingsRowId::resolution) {
        close_resolution_dropdown();
    }

    const auto before = session_->draft();
    if (row == SettingsRowId::favorite_server) {
        const auto updated = direction > 0;
        if (favorite_server_ == updated) {
            return false;
        }
        favorite_server_ = updated;
        effects_.emplace_back(SettingsSoundEffect{SettingsMenuSound::scroll});
        return true;
    }

    if (row == SettingsRowId::language || row == SettingsRowId::master_volume ||
        row == SettingsRowId::music_volume ||
        row == SettingsRowId::fullscreen || row == SettingsRowId::invert_mouse ||
        row == SettingsRowId::show_skins || row == SettingsRowId::show_other_skins ||
        row == SettingsRowId::weapon_motion || row == SettingsRowId::ability_hints) {
        auto main = before.main;
        switch (row) {
        case SettingsRowId::language: {
            if (environment_.languages.empty()) return false;
            const auto found = std::ranges::find(environment_.languages,
                                                 main.language,
                                                 &SettingsLanguageOption::locale);
            const auto current_index = found == environment_.languages.end()
                                           ? 0U
                                           : static_cast<std::size_t>(std::distance(
                                                 environment_.languages.begin(), found));
            main.language = environment_.languages[shifted_index(
                current_index, environment_.languages.size(), direction)].locale;
            break;
        }
        case SettingsRowId::master_volume:
            // RangeBarControl.on_click: value + step (0.2), never re-rounded,
            // so a dragged 0.37 steps to 0.57 exactly as the arrows did.
            main.master_volume =
                snapped_volume(main.master_volume + (direction < 0 ? -0.2 : 0.2));
            break;
        case SettingsRowId::music_volume:
            main.music_volume =
                snapped_volume(main.music_volume + (direction < 0 ? -0.2 : 0.2));
            break;
        case SettingsRowId::fullscreen:
            main.fullscreen = direction > 0;
            break;
        case SettingsRowId::invert_mouse:
            main.invert_mouse = direction > 0;
            break;
        case SettingsRowId::show_skins:
            main.show_skins = direction > 0;
            break;
        case SettingsRowId::show_other_skins:
            main.show_other_skins = direction > 0;
            break;
        case SettingsRowId::weapon_motion:
            main.weapon_motion = direction > 0;
            break;
        case SettingsRowId::ability_hints:
            main.ability_hints = direction > 0;
            break;
        default:
            break;
        }
        session_->set_main(main);
    } else if (row == SettingsRowId::mouse_sensitivity) {
        auto controls = before.controls;
        controls.mouse_sensitivity =
            std::clamp(controls.mouse_sensitivity + (direction < 0 ? -0.01 : 0.01), 0.0, 1.0);
        controls.mouse_sensitivity = std::round(controls.mouse_sensitivity * 100.0) / 100.0;
        session_->set_controls(controls);
    } else {
        auto graphics = before.graphics;
        switch (row) {
        case SettingsRowId::resolution: {
            if (environment_.display_modes.empty()) {
                return false;
            }
            const auto found = std::find(environment_.display_modes.begin(),
                                         environment_.display_modes.end(),
                                         graphics.resolution);
            const auto current = found == environment_.display_modes.end()
                                     ? 0U
                                     : static_cast<std::size_t>(std::distance(
                                           environment_.display_modes.begin(), found));
            graphics.resolution = environment_.display_modes[shifted_index(
                current, environment_.display_modes.size(), direction)];
            break;
        }
        case SettingsRowId::graphics_api: {
            const auto found = std::find(environment_.graphics_apis.begin(),
                                         environment_.graphics_apis.end(),
                                         graphics.graphics_api);
            const auto current = found == environment_.graphics_apis.end()
                                     ? 0U
                                     : static_cast<std::size_t>(std::distance(
                                           environment_.graphics_apis.begin(), found));
            graphics.graphics_api = environment_.graphics_apis[shifted_index(
                current, environment_.graphics_apis.size(), direction)];
            break;
        }
        case SettingsRowId::antialiasing: {
            const auto current = antialiasing_index(graphics.antialiasing);
            graphics.antialiasing = antialiasing_at(shifted_index(current, 3U, direction));
            break;
        }
        case SettingsRowId::effect_quality: {
            const auto current = quality_index(graphics.effect_quality);
            graphics.effect_quality = quality_at(shifted_index(current, 3U, direction));
            break;
        }
        case SettingsRowId::draw_distance: {
            const auto current = draw_distance_index(graphics.draw_distance);
            graphics.draw_distance = draw_distance_at(shifted_index(current, 3U, direction));
            break;
        }
        case SettingsRowId::shader_quality: {
            const auto current = shader_index(graphics.shader_quality);
            graphics.shader_quality =
                shader_at(shifted_index(current, shader_choice_count, direction));
            break;
        }
        case SettingsRowId::texture_quality: {
            const auto current = quality_index(graphics.texture_quality);
            graphics.texture_quality = quality_at(shifted_index(current, 3U, direction));
            break;
        }
        case SettingsRowId::model_quality: {
            const auto current = quality_index(graphics.model_quality);
            graphics.model_quality = quality_at(shifted_index(current, 3U, direction));
            break;
        }
        case SettingsRowId::vsync:
            graphics.vsync = direction > 0;
            break;
        case SettingsRowId::compatibility_shader:
            if (direction > 0) {
                if (!graphics.compatibility_shader()) {
                    restore_shader_quality_ = graphics.shader_quality;
                }
                graphics.shader_quality = settings::ShaderQuality::compatibility;
            } else {
                // Retail's toggle had no recovered "off" tier, so this used to
                // hard-write High and silently discard whatever the player had
                // chosen. With four tiers that would demote Ultra every time the
                // toggle was flipped; restore what they actually picked.
                graphics.shader_quality = restore_shader_quality_;
            }
            break;
        default:
            return false;
        }
        session_->set_graphics(graphics);
    }

    if (session_->draft() == before) {
        return false;
    }
    effects_.emplace_back(SettingsSoundEffect{SettingsMenuSound::scroll});
    emit_preview(row);
    return true;
}

bool SettingsMenuModel::set_slider_from_pointer(SettingsRowId row,
                                                ui::Point point,
                                                bool play_sound) {
    const auto bounds = visible_row_bounds(row);
    if (!bounds.has_value() || !target_enabled(SettingsMenuTarget::for_row(row))) {
        return false;
    }
    const auto control = control_bounds_for(*bounds);
    if (control.width <= 0 || point.y < control.y || point.y > control.y + control.height) {
        return false;
    }
    double value{};
    if (row == SettingsRowId::mouse_sensitivity) {
        // SliderControl.update_press: the track plus one spacing either side;
        // the edit box on the right is a separate control.
        const auto geometry = slider_geometry(control);
        if (point.x < control.x - 4 ||
            point.x > geometry.track_x + geometry.track_width + 4.0 ||
            geometry.track_width <= 0.0) {
            return false;
        }
        value = std::clamp((static_cast<double>(point.x) - geometry.track_x) /
                               geometry.track_width,
                           0.0,
                           1.0);
    } else {
        // RangeBarControl.update_press maps only the bar between the arrows.
        const auto geometry = range_bar_geometry(control);
        if (point.x < geometry.bar_left || point.x > geometry.bar_right ||
            geometry.bar_right <= geometry.bar_left) {
            return false;
        }
        value = snapped_volume(static_cast<double>(point.x - geometry.bar_left) /
                               static_cast<double>(geometry.bar_right - geometry.bar_left));
    }
    const auto before = session_->draft();

    if (row == SettingsRowId::master_volume || row == SettingsRowId::music_volume) {
        auto main = before.main;
        if (row == SettingsRowId::master_volume) {
            main.master_volume = value;
        } else {
            main.music_volume = value;
        }
        session_->set_main(main);
    } else if (row == SettingsRowId::mouse_sensitivity) {
        auto controls = before.controls;
        controls.mouse_sensitivity = std::round(value * 100.0) / 100.0;
        session_->set_controls(controls);
    } else {
        return false;
    }

    if (session_->draft() == before) {
        return false;
    }
    if (play_sound) {
        effects_.emplace_back(SettingsSoundEffect{SettingsMenuSound::scroll});
    }
    emit_preview(row);
    return true;
}

void SettingsMenuModel::emit_preview(SettingsRowId source) {
    effects_.emplace_back(SettingsPreviewEffect{source, session_->draft()});
}

bool SettingsMenuModel::step_volume(SettingsRowId row, std::int32_t direction) {
    if (row != SettingsRowId::master_volume && row != SettingsRowId::music_volume) {
        return false;
    }
    // The arrows grey out at the ends (update_buttons_enabled_state).
    const auto& main = session_->draft().main;
    const auto value = row == SettingsRowId::master_volume ? main.master_volume
                                                           : main.music_volume;
    if ((direction < 0 && value <= 0.0) || (direction > 0 && value >= 1.0)) {
        return false;
    }
    return adjust_row(row, direction);
}

bool SettingsMenuModel::text_editing() const noexcept {
    return sensitivity_edit_.has_value();
}

bool SettingsMenuModel::text_input(std::string_view utf8) {
    if (!sensitivity_edit_.has_value()) {
        return false;
    }
    // The box is typed float: only characters float() can use are kept, and
    // the text stays short enough to fit the sixth-of-a-row box.
    constexpr std::size_t maximum_characters{6U};
    for (const auto character : utf8) {
        const bool digit = character >= '0' && character <= '9';
        const bool point = character == '.' &&
                           sensitivity_edit_->find('.') == std::string::npos;
        if ((digit || point) && sensitivity_edit_->size() < maximum_characters) {
            sensitivity_edit_->push_back(character);
        }
    }
    return true;
}

bool SettingsMenuModel::text_erase(bool forward) {
    if (!sensitivity_edit_.has_value()) {
        return false;
    }
    // The caret sits at the end of the text, so Delete has nothing after it
    // and Backspace removes the last character (EditBoxControl).
    if (!forward && !sensitivity_edit_->empty()) {
        sensitivity_edit_->pop_back();
    }
    return true;
}

void SettingsMenuModel::commit_text_edit() {
    if (!sensitivity_edit_.has_value()) {
        return;
    }
    const auto text = std::move(*sensitivity_edit_);
    sensitivity_edit_.reset();
    double parsed{};
    try {
        std::size_t used{};
        parsed = text.empty() || text == "." ? 0.0 : std::stod(text, &used);
    } catch (...) {
        return; // an unparsable value leaves the setting untouched
    }
    // EditBoxFloatControl.on_return: clamp to min/max, round to 2 places.
    parsed = std::round(std::clamp(parsed, 0.0, 1.0) * 100.0) / 100.0;
    const auto before = session_->draft();
    auto controls = before.controls;
    controls.mouse_sensitivity = parsed;
    session_->set_controls(controls);
    if (session_->draft() != before) {
        emit_preview(SettingsRowId::mouse_sensitivity);
    }
}

void SettingsMenuModel::activate_menu_key() {
    if (environment_.context != SettingsMenuContext::in_game) {
        activate_cancel();
        return;
    }
    close_resolution_dropdown();
    cancel_binding_capture();
    sensitivity_edit_.reset();
    session_->cancel();
    favorite_server_ = initial_favorite_server_;
    effects_.emplace_back(SettingsSoundEffect{SettingsMenuSound::back});
    effects_.emplace_back(SettingsRestoreCommand{session_->committed()});
    effects_.emplace_back(SettingsCloseCommand{false, true});
}

settings::BindingAssignmentResult SettingsMenuModel::capture_scancode(std::uint32_t scancode) {
    if (!binding_capture_.has_value()) {
        return {settings::BindingAssignmentStatus::invalid_action, std::nullopt};
    }
    const auto action = *binding_capture_;
    const auto result =
        session_->assign_binding(action, settings::InputBinding::keyboard(scancode));
    if (!result.accepted()) {
        binding_rejection_ = result;
        binding_rejected_input_ = settings::InputBinding::keyboard(scancode);
        effects_.emplace_back(
            SettingsBindingRejectedEffect{action, result.status, result.conflicting_action});
        return result;
    }

    binding_capture_.reset();
    binding_rejection_.reset();
    effects_.emplace_back(SettingsSoundEffect{SettingsMenuSound::scroll});
    for (const auto row : controls_inventory) {
        if (settings_row_control_action(row) == action) {
            emit_preview(row);
            break;
        }
    }
    return result;
}

settings::BindingAssignmentResult SettingsMenuModel::capture_mouse_button(std::uint32_t button) {
    if (!binding_capture_.has_value()) {
        return {settings::BindingAssignmentStatus::invalid_action, std::nullopt};
    }
    const auto action = *binding_capture_;
    const auto result = session_->assign_binding(action, settings::InputBinding::mouse(button));
    if (!result.accepted()) {
        binding_rejection_ = result;
        binding_rejected_input_ = settings::InputBinding::mouse(button);
        effects_.emplace_back(
            SettingsBindingRejectedEffect{action, result.status, result.conflicting_action});
        return result;
    }

    binding_capture_.reset();
    binding_rejection_.reset();
    effects_.emplace_back(SettingsSoundEffect{SettingsMenuSound::scroll});
    for (const auto row : controls_inventory) {
        if (settings_row_control_action(row) == action) {
            emit_preview(row);
            break;
        }
    }
    return result;
}

void SettingsMenuModel::cancel_binding_capture() noexcept {
    binding_capture_.reset();
    binding_rejection_.reset();
}

void SettingsMenuModel::activate_defaults() {
    if (!target_enabled(SettingsMenuTarget::button(SettingsTargetKind::defaults_button))) {
        return;
    }
    close_resolution_dropdown();
    cancel_binding_capture();
    sensitivity_edit_.reset();
    session_->reset_tab(active_tab_);
    effects_.emplace_back(SettingsSoundEffect{SettingsMenuSound::confirm});
    effects_.emplace_back(SettingsDefaultsCommand{active_tab_, session_->draft()});
    const auto rows = inventory(active_tab_);
    if (!rows.empty()) {
        emit_preview(rows.front());
    }
}

void SettingsMenuModel::activate_done() {
    close_resolution_dropdown();
    cancel_binding_capture();
    commit_text_edit();
    const auto previous = session_->committed();
    const auto draft = session_->draft();
    const auto resolution_changed = previous.graphics.resolution != draft.graphics.resolution;
    const auto restart_required =
        previous.graphics.graphics_api != draft.graphics.graphics_api ||
        previous.graphics.texture_quality != draft.graphics.texture_quality ||
        previous.graphics.model_quality != draft.graphics.model_quality;
    const auto changed = session_->commit();

    effects_.emplace_back(SettingsSoundEffect{SettingsMenuSound::confirm});
    effects_.emplace_back(SettingsCommitCommand{
        session_->committed(), changed, resolution_changed, restart_required});
    if (environment_.context == SettingsMenuContext::in_game &&
        environment_.favorite_server_available && favorite_server_ != initial_favorite_server_) {
        effects_.emplace_back(SettingsFavoriteServerCommand{favorite_server_});
        initial_favorite_server_ = favorite_server_;
    }
    // save_pressed: Done in a match goes back to the game, not EscapeMenu.
    effects_.emplace_back(
        SettingsCloseCommand{true, environment_.context == SettingsMenuContext::in_game});
}

void SettingsMenuModel::activate_cancel() {
    close_resolution_dropdown();
    cancel_binding_capture();
    sensitivity_edit_.reset();
    session_->cancel();
    favorite_server_ = initial_favorite_server_;
    // back_pressed (in game) reopens EscapeMenu silently; the frontend's
    // cancel_pressed plays menu_backA.
    if (environment_.context != SettingsMenuContext::in_game) {
        effects_.emplace_back(SettingsSoundEffect{SettingsMenuSound::back});
    }
    effects_.emplace_back(SettingsRestoreCommand{session_->committed()});
    effects_.emplace_back(SettingsCloseCommand{false});
}

std::vector<SettingsMenuEffect> SettingsMenuModel::take_effects() noexcept {
    auto result = std::move(effects_);
    effects_.clear();
    return result;
}

} // namespace battlespades::frontend
