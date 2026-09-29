#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace battlespades::world {

/** Complete retail class transaction carried by SetClassLoadout(13). */
struct ClassSelection final {
    std::uint8_t class_id{};
    std::vector<std::uint8_t> loadout;
    std::vector<std::string> prefabs;
    std::vector<std::uint8_t> ugc_tools;
};

/** Retail FLAREBLOCK_TOOL and PREFAB_TOOL. */
inline constexpr std::uint8_t flare_block_tool{22U};
inline constexpr std::uint8_t prefab_tool{23U};

/**
 * Synthetic Constructs tile for FLAREBLOCK_TOOL. Retail SelectClass puts the
 * flare block first in the prefab TableSelection; picking it adds tool 22 to
 * the loadout instead of a prefab name (selectClass.py create_loadout_list).
 */
inline constexpr std::string_view flare_block_construct{"flareblock"};

/**
 * Server rules the retail picker applies (InitialInfo.disabled_tools and the
 * manager's mafia/UGC mode). Defaults describe an unrestricted server.
 */
struct ClassSelectionRules final {
    std::vector<std::uint8_t> disabled_tools;
    /** manager.is_in_mafia_mode(): no flare/prefab tool, 0 default constructs. */
    bool mafia{};
    /** game_mode == UGC: 0 default constructs. */
    bool ugc{};

    [[nodiscard]] bool tool_disabled(std::uint16_t tool) const noexcept;
};

/** Construct names allowed by CLASS_ITEMS/PREFAB_LISTS (server tables) for one class. */
[[nodiscard]] std::span<const std::string_view>
class_prefab_options(std::uint8_t class_id) noexcept;

/** One retail loadout row (melee/primary/secondary/equipment) minus disabled tools. */
[[nodiscard]] std::vector<std::uint16_t>
class_row_options(std::uint8_t class_id, std::size_t group, const ClassSelectionRules& rules);

/**
 * SelectClass.get_class_images(CLASS_PREFABS): the flare tile first (unless the
 * class is a zombie/Deuce or flare is disabled), then the class constructs.
 * Empty when PREFAB_TOOL is disabled.
 */
[[nodiscard]] std::vector<std::string>
class_construct_options(std::uint8_t class_id, const ClassSelectionRules& rules);

/**
 * GameClass.get_prefabs: the first three class constructs (only when at least
 * three exist); none in mafia/UGC or when PREFAB_TOOL is disabled.
 */
[[nodiscard]] std::vector<std::string>
default_class_constructs(std::uint8_t class_id, const ClassSelectionRules& rules);

/**
 * Build SetClassLoadout from explicit row items plus constructs, exactly as
 * create_loadout_list + set_common_loadout_items do: BLOCK_TOOL first, the
 * chosen rows, then CLASS_COMMON minus disabled tools; tool 22 only when the
 * flare tile is chosen.
 */
[[nodiscard]] ClassSelection make_class_selection(
    std::uint8_t class_id,
    std::span<const std::uint16_t> chosen_items,
    std::span<const std::string> constructs,
    const ClassSelectionRules& rules);

/**
 * GameClass(...).loadout for a class the player never picked (locked-class
 * teams, mafia auto-pick): the first enabled item per row, common tools, and
 * FLARE/PREFAB appended outside mafia mode (gameClass.build_class_loadout).
 */
[[nodiscard]] ClassSelection automatic_class_selection(std::uint8_t class_id,
                                                       const ClassSelectionRules& rules);

/** Build a valid one-choice-per-row selection, including three prefabs. */
[[nodiscard]] ClassSelection default_class_selection(std::uint8_t class_id);

/** Build from explicit row/construct choices; never refill deselected prefabs. */
[[nodiscard]] ClassSelection make_class_selection(
    std::uint8_t class_id,
    const std::array<std::size_t, 4U>& option_indices,
    std::span<const std::string> prefabs);

} // namespace battlespades::world
