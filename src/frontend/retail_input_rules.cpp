#include "battlespades/frontend/retail_input_rules.hpp"

#include "battlespades/shared/retail_constants.hpp"

#include <array>

namespace battlespades::frontend {
namespace {

using settings::ControlAction;

constexpr std::array<std::string_view, 1U> paintbrush_tips{"UGC_HELP_PAINTBRUSH"};
constexpr std::array<std::string_view, 1U> construct_step1_tips{"UGC_HELP_CONSTRUCTTOOL_STEP1"};
constexpr std::array<std::string_view, 1U> construct_step2_tips{"UGC_HELP_CONSTRUCTTOOL_STEP2"};
constexpr std::array<std::string_view, 1U> pickaxe_tips{"UGC_HELP_PICKAXE"};
constexpr std::array<std::string_view, 1U> superspade_tips{"UGC_HELP_SPADEDUELUSE"};
constexpr std::array<std::string_view, 1U> rocket_tips{"UGC_HELP_ROCKETLAUNCHER"};
constexpr std::array<std::string_view, 1U> block_tips{"UGC_HELP_BLOCKTOOL"};
constexpr std::array<std::string_view, 1U> game_data_tips{"UGC_HELP_GAMEDATATOOL"};
constexpr std::array<std::string_view, 1U> drillgun_tips{"UGC_HELP_DRILLGUN"};
constexpr std::array<std::string_view, 1U> block_cannon_tips{"UGC_HELP_BLOCKCANNON"};

[[nodiscard]] constexpr bool is_tool(std::uint8_t tool_id, std::int64_t retail_id) noexcept {
    return static_cast<std::int64_t>(tool_id) == retail_id;
}

} // namespace

ControlKeyNames retail_control_key_names(const settings::ControlsSettings& controls,
                                         const settings::RetailStringLookup& lookup) {
    const auto name = [&](ControlAction action) {
        return settings::retail_binding_name(controls.binding(action), lookup);
    };
    ControlKeyNames names;
    names.forward = name(ControlAction::forward);
    names.backward = name(ControlAction::backward);
    names.left = name(ControlAction::left);
    names.right = name(ControlAction::right);
    names.jump = name(ControlAction::jump);
    names.crouch = name(ControlAction::crouch);
    names.change_class = name(ControlAction::change_class);
    names.view_scores = name(ControlAction::view_scores);
    names.palette_up = name(ControlAction::palette_up);
    names.palette_down = name(ControlAction::palette_down);
    names.palette_left = name(ControlAction::palette_left);
    names.palette_right = name(ControlAction::palette_right);
    names.weapon_custom = name(ControlAction::weapon_custom);
    names.cancel_prefab_placement = name(ControlAction::cancel_prefab_placement);
    names.carve_prefab = name(ControlAction::carve_prefab);
    names.tool_help = name(ControlAction::tool_help);
    names.hover = name(ControlAction::hover);
    names.sprint = name(ControlAction::sprint);
    names.ugc_settings = name(ControlAction::ugc_settings);
    names.menu = name(ControlAction::menu);
    return names;
}

std::string retail_help_string(std::string_view message_id,
                               const settings::RetailStringLookup& lookup) {
    if (lookup) {
        if (auto text = lookup(message_id); text.has_value()) {
            return std::move(*text);
        }
    }
    return std::string{tutorial_string(message_id)};
}

bool retail_aim_mouse_button(const settings::ControlsSettings& controls,
                             std::uint32_t mouse_button) noexcept {
    constexpr std::uint32_t right_mouse_button{3U};
    if (mouse_button == right_mouse_button) {
        return true;
    }
    const auto binding = controls.binding(ControlAction::aim);
    return binding.kind == settings::BindingKind::mouse_button && binding.code == mouse_button;
}

std::span<const std::string_view> ugc_tool_help_ids(std::uint8_t tool_id,
                                                    bool controlling_prefab) noexcept {
    if (is_tool(tool_id, retail::PAINTBRUSH_TOOL)) return paintbrush_tips;
    if (is_tool(tool_id, retail::UGC_PREFAB_TOOL)) {
        return controlling_prefab ? std::span<const std::string_view>{construct_step2_tips}
                                  : std::span<const std::string_view>{construct_step1_tips};
    }
    if (is_tool(tool_id, retail::UGC_PICKAXE_TOOL)) return pickaxe_tips;
    if (is_tool(tool_id, retail::UGC_SUPERSPADE_TOOL)) return superspade_tips;
    if (is_tool(tool_id, retail::UGC_RPG2_TOOL)) return rocket_tips;
    if (is_tool(tool_id, retail::BLOCK_TOOL)) return block_tips;
    if (is_tool(tool_id, retail::UGC_TOOL)) return game_data_tips;
    if (is_tool(tool_id, retail::UGC_DRILLGUN_TOOL)) return drillgun_tips;
    if (is_tool(tool_id, retail::UGC_SNOWBLOWER_TOOL)) return block_cannon_tips;
    return {};
}

std::filesystem::path retail_screenshot_path(const std::filesystem::path& directory,
                                             std::string_view map_name, std::uint32_t index) {
    std::string stem;
    stem.reserve(map_name.size() + 12U);
    for (const char character : map_name) {
        const bool reserved = character == '<' || character == '>' || character == ':' ||
                              character == '"' || character == '/' || character == '\\' ||
                              character == '|' || character == '?' || character == '*' ||
                              static_cast<unsigned char>(character) < 32U;
        stem.push_back(reserved ? '_' : character);
    }
    stem += std::to_string(index);
    stem += ".png";
    std::u8string utf8;
    utf8.reserve(stem.size());
    for (const char character : stem) {
        utf8.push_back(static_cast<char8_t>(character));
    }
    return directory / std::filesystem::path{utf8};
}

} // namespace battlespades::frontend
