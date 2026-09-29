#pragma once

#include "battlespades/frontend/game_hud.hpp"
#include "battlespades/settings/client_settings.hpp"
#include "battlespades/settings/retail_key_names.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace battlespades::frontend {

/**
 * Pure retail input/help rules used by the native frontend, kept out of the
 * module so they can be unit tested.
 */

/** translate_key names for all 20 `{key_*}` controls of the live bindings. */
[[nodiscard]] ControlKeyNames
retail_control_key_names(const settings::ControlsSettings& controls,
                         const settings::RetailStringLookup& lookup);

/**
 * strings.get_by_id for a HelpPanel message id: the localized catalogue
 * text, else the recovered English tutorial table, else the id itself
 * (retail's non-debug get_by_id returns the id for a missing string).
 */
[[nodiscard]] std::string retail_help_string(std::string_view message_id,
                                             const settings::RetailStringLookup& lookup);

/**
 * GameScene.on_key_press `aim`: RMB is always ADS (the Controls row's fixed
 * key_text), and a key bound to `aim` adds a second ADS input
 * (DOUBLE_KEY_BINDING "RMB, <key>"). Mouse button numbers are SDL's.
 */
[[nodiscard]] bool retail_aim_mouse_button(const settings::ControlsSettings& controls,
                                           std::uint32_t mouse_button) noexcept;

/**
 * Native developer tools live behind Ctrl+Shift so they never shadow a
 * retail key (F3 vote, F10 quick save, F11 screenshot).
 */
[[nodiscard]] constexpr bool developer_chord(std::uint16_t modifiers) noexcept {
    constexpr std::uint16_t shift_mask{0x0003U};
    constexpr std::uint16_t control_mask{0x00C0U};
    return (modifiers & shift_mask) != 0U && (modifiers & control_mask) != 0U;
}

/**
 * ToolsHelpPanel.update_tool_text: the UGC_TOOL_HELP_TIPS ids for one tool
 * (shared/constants.py:4391-4401). The construct tool shows STEP1 until a
 * prefab is being controlled, then STEP2. Empty for tools without tips.
 */
[[nodiscard]] std::span<const std::string_view>
ugc_tool_help_ids(std::uint8_t tool_id, bool controlling_prefab) noexcept;

/**
 * GameScene.update's one-shot UGC_HELP_HOVER: shown the first time a
 * CLASS_UGCBUILDER player has held the jetpack for more than 1.0 s.
 */
inline constexpr double ugc_hover_help_jetpack_seconds{1.0};

/**
 * process_packet_play_music / process_packet_stop_music (gameScene.pyd
 * 0x1019D120 / 0x1019D700) return early while SECONDARY_MENU_MUSIC1
 * (secondary_menu_bed_001) is playing.
 */
[[nodiscard]] constexpr bool retail_server_music_allowed(bool secondary_bed_001_playing) noexcept {
    return !secondary_bed_001_playing;
}

/** Retail GameScene.take_screenshot directory. */
inline constexpr std::string_view retail_screenshot_directory{"C:\\AoS_Screenshots"};

/**
 * GameScene.take_screenshot(index): os.path.join(dir, map_name) + str(index)
 * + ".png". Characters that cannot appear in a file name are replaced.
 */
[[nodiscard]] std::filesystem::path retail_screenshot_path(const std::filesystem::path& directory,
                                                           std::string_view map_name,
                                                           std::uint32_t index);

} // namespace battlespades::frontend
