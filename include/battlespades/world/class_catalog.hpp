#pragma once

#include "battlespades/world/vxl_map.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace battlespades::world {

inline constexpr std::size_t retail_class_count{18U};
inline constexpr std::size_t retail_body_part_count{7U};

enum class BodyPart : std::uint8_t {
    head,
    torso,
    arms_collision,
    left_leg,
    right_leg,
    crouched_torso,
    crouched_leg,
};

enum class ClassItemGroup : std::uint8_t {
    melee,
    primary,
    secondary,
    equipment,
    prefab_sets,
    common,
    ugc_tools,
};

struct ClassBodyPartDefinition final {
    BodyPart part{BodyPart::head};
    std::string_view model_asset;
    std::array<float, 3U> authored_offset{};
    std::array<float, 3U> body_anchor{};
};

/**
 * One retail class/character row recovered from shared.constants.
 *
 * Class item ids deliberately use 16 bits. Values 0..64 are Protocol 168
 * tools; higher values are class equipment (jetpacks/parachute) and must
 * never be truncated into the weapon byte.
 */
struct ClassDefinition final {
    std::uint8_t class_id{};
    std::string_view symbolic_name;
    std::string_view display_name;
    std::array<ClassBodyPartDefinition, retail_body_part_count> body_parts;
    std::array<std::string_view, 2U> first_person_arm_assets;
    std::array<std::span<const std::uint16_t>, 7U> item_groups;
    std::uint16_t initial_blocks{};
    std::uint16_t maximum_blocks{};
    std::array<std::string_view, 2U> team_portrait_assets;
    std::array<std::string_view, 2U> team_icon_assets;
    std::string_view texture_skin;
    /**
     * Raw server HP is divided by this value for the retail numeric readout.
     * The health-bar fill itself continues to use the unscaled 0..100 pool.
     */
    double damage_multiplier{1.0};
};

struct UiSkinDefinition final {
    std::string_view id;
    /** Prefix searched before the normal png/ui asset. Empty is default. */
    std::string_view asset_prefix;
};

[[nodiscard]] std::span<const ClassDefinition> class_catalog() noexcept;
[[nodiscard]] const ClassDefinition* find_class_definition(std::uint8_t class_id) noexcept;
[[nodiscard]] std::span<const UiSkinDefinition> ui_skin_catalog() noexcept;
[[nodiscard]] const UiSkinDefinition* find_ui_skin(std::string_view id) noexcept;

/** First enabled choice per retail slot, followed by common tools. */
[[nodiscard]] std::vector<std::uint16_t>
default_class_items(const ClassDefinition& definition, bool include_flare = false,
                    bool include_prefab_tool = true);

/** Every unique 0..64 tool available to the class, suitable for a debug lab. */
[[nodiscard]] std::vector<std::uint8_t>
class_test_tools(const ClassDefinition& definition);

/**
 * Selects the equipped tool for a fresh authoritative life.
 *
 * Retail stores BLOCK_TOOL first in the combined loadout for inventory
 * construction, but spawns holding the chosen primary weapon. Falling back to
 * the first valid tool keeps malformed/incomplete server selections usable
 * without ever inventing a tool outside the acknowledged loadout.
 */
[[nodiscard]] std::optional<std::uint8_t>
preferred_spawn_tool(const ClassDefinition& definition,
                     std::span<const std::uint8_t> loadout) noexcept;

[[nodiscard]] std::string_view class_item_group_name(ClassItemGroup group) noexcept;
[[nodiscard]] std::string_view class_catalog_contract_sha256() noexcept;

} // namespace battlespades::world
