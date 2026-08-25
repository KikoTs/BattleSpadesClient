#pragma once

#include "battlespades/ui/draw_list.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace battlespades::frontend {

/** Which retail Settings surface is being composed. */
enum class SettingsPresentationScreen : std::uint8_t {
    settings,
    resolution_confirmation,
};

/** Retail Settings tabs in their fixed left-to-right order. */
enum class SettingsPresentationTab : std::uint8_t {
    main,
    graphics,
    controls,
};

/** Renderer-neutral row/control types recovered from the Python client. */
enum class SettingsPresentationRowKind : std::uint8_t {
    range_bar,
    choice,
    toggle,
    checkbox,
    dropdown,
    category,
    key_binding,
    scalar_slider,
};

/** Visual state supplied by the interactive Settings model. */
enum class SettingsPresentationVisualState : std::uint8_t {
    normal,
    hovered,
    pressed,
    disabled,
};

/** Snapshot of one logical Settings row; presentation never mutates it. */
struct SettingsPresentationRow final {
    SettingsPresentationRowKind kind{SettingsPresentationRowKind::choice};
    std::string label_key;
    std::string value_key;
    std::string supplementary_value_key;
    SettingsPresentationVisualState visual_state{SettingsPresentationVisualState::normal};
    double scalar_value{};
    bool checked{};
    bool expanded{true};
    bool left_enabled{true};
    bool right_enabled{true};
    bool dropdown_open{};
    std::vector<std::string> dropdown_options;
    std::size_t dropdown_selected_index{};
    bool dropdown_selection_visible{true};
};

/** State of a retail sliced TextButton. */
struct SettingsPresentationButton final {
    std::string label_key;
    SettingsPresentationVisualState visual_state{SettingsPresentationVisualState::normal};
};

/**
 * Immutable presentation boundary for SettingsMenuModel.
 *
 * The model may expose this DTO directly or adapt its own snapshot into it.
 * Dynamic strings occupy localization-key fields because the current text
 * backend deliberately falls back to drawing unknown keys verbatim.
 */
struct SettingsPresentationSnapshot final {
    SettingsPresentationScreen screen{SettingsPresentationScreen::settings};
    SettingsPresentationTab selected_tab{SettingsPresentationTab::main};
    bool in_game{};
    bool include_frontend_background{true};
    std::vector<SettingsPresentationRow> rows;
    std::size_t first_visible_row{};
    std::string tooltip_key{"SETTINGS_MESSAGE"};
    bool tooltip_is_error{};
    std::string error_key;
    SettingsPresentationButton defaults_button{"DEFAULTS"};
    SettingsPresentationButton cancel_button{"CANCEL"};
    SettingsPresentationButton done_button{"DONE"};
    double resolution_countdown_seconds{15.0};
};

/** Runtime values which do not belong to persistent Settings state. */
struct SettingsPresentationContext final {
    ui::PixelExtent window{800, 600};
    std::uint16_t background_opacity_per_mille{1'000U};
};

/** Stable geometry summary useful to input adapters and characterization tests. */
struct SettingsClassicLayout final {
    ui::DrawRect outer_frame{};
    ui::DrawRect content_frame{};
    ui::DrawRect title{};
    ui::DrawRect tab_hit_strip{};
    ui::DrawRect list_area{};
    ui::DrawRect defaults_button{};
    ui::DrawRect tooltip{};
    ui::DrawRect cancel_button{};
    ui::DrawRect done_button{};

    [[nodiscard]] friend constexpr bool operator==(const SettingsClassicLayout&,
                                                   const SettingsClassicLayout&) = default;
};

/** Returns exact top-left-origin Classic-profile rectangles at 800x600. */
[[nodiscard]] SettingsClassicLayout settings_classic_layout(bool in_game) noexcept;

/**
 * Builds the ordered retail Settings command stream.
 *
 * This class owns presentation only: it does not change configuration, play
 * sounds, resize the window, or route actions. Invalid snapshot values throw
 * before a partial draw list is returned.
 */
class SettingsPresentation final {
public:
    [[nodiscard]] ui::DrawList build(const SettingsPresentationSnapshot& snapshot,
                                     const SettingsPresentationContext& context = {}) const;
};

namespace settings_presentation_assets {

inline constexpr std::string_view background{"png/ui/ugc_splash.png"};
inline constexpr std::string_view outer_frame{
    "png/ui/common_elements/frames/ui_frame_small.png"};
inline constexpr std::string_view in_game_outer_frame{
    "png/ui/settings/in_game_settings_frame.png"};
inline constexpr std::string_view main_content{
    "png/ui/settings/settings_main/settings_main_content_frames.png"};
inline constexpr std::string_view graphics_content{
    "png/ui/settings/settings_graphics/settings_graphics_content_frames.png"};
inline constexpr std::string_view controls_content{
    "png/ui/settings/settings_controls/settings_controls_content_frames.png"};
inline constexpr std::string_view enabled_row{
    "png/ui/settings/settings_common/settings_matchsettings_frame.png"};
inline constexpr std::string_view disabled_row{
    "png/ui/settings/settings_common/settings_frame_disabled.png"};
inline constexpr std::string_view tooltip{
    "png/ui/settings/settings_common/settings_tooltip_frame.png"};
inline constexpr std::string_view in_game_tooltip{
    "png/ui/settings/settings_common/settings_tooltip_frame_ingame.png"};
inline constexpr std::string_view white_pixel{"png/high/white.png"};
inline constexpr std::string_view title_font{"fonts/Spades.ttf"};
inline constexpr std::string_view row_font{"fonts/Edo.ttf"};
inline constexpr std::string_view standard_font{"fonts/A750-Sans-Medium.ttf"};

} // namespace settings_presentation_assets

} // namespace battlespades::frontend
