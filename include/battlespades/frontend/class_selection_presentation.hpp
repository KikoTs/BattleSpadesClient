#pragma once

#include "battlespades/frontend/class_selection_menu.hpp"
#include "battlespades/ui/draw_list.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace battlespades::frontend {

/** Resolve one retail loadout/equipment id to its SelectClass icon. */
[[nodiscard]] std::string
class_selection_item_icon(std::uint16_t raw_item_id);

/** Retail FLAREBLOCK_COST: the flare tile's "Block usage". */
inline constexpr std::size_t retail_flare_block_cost{10U};

struct ClassSelectionAppearance {
    // Centre of the 256-square generated image, matching the 111x160 portrait slot.
    static constexpr ui::DrawRect portrait_source{39.2,0.0,177.6,256.0};
    std::map<std::uint8_t,std::string> class_icons, class_portraits;
    std::map<std::uint16_t,std::string> weapon_icons;
    /**
     * strings.get_by_id for popup text that must be split or composed
     * (TOOL_DESCRIPTIONS pros/cons lines, "Blocks: a / b"). Without it the
     * presentation submits the raw ids and lets the renderer localize them.
     */
    std::function<std::string(std::string_view)> localize;
    /** InitialInfo.block_wallet_multiplier for the class popup block counts. */
    double block_wallet_multiplier{1.0};
    /** prefab_manager.get_prefab_block_count; nullopt hides the number. */
    std::function<std::optional<std::size_t>(std::string_view)> prefab_block_count;
};

/** Asset presentation for the multiplayer SelectClass gate. */
class ClassSelectionPresentation final {
public:
    [[nodiscard]] ui::DrawList build(
        const ClassSelectionMenuModel& menu,
        ui::PixelExtent window,
        bool in_game = false,
        const ClassSelectionAppearance& appearance = {},
        ClassSelectionMenuModel::Clock::time_point now =
            ClassSelectionMenuModel::Clock::now()) const;
};

} // namespace battlespades::frontend
