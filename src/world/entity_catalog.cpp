#include "battlespades/world/entity_catalog.hpp"
#include "battlespades/world/kv6_model.hpp"

#include <algorithm>
#include <vector>

namespace battlespades::world {

namespace {

constexpr EntityModelPart ugc_drop_health_parts[]{
    {"ugc_baseplate.kv6", {}, 1.0F, 0U, {}},
    {"healthcrate.kv6", {0.0F, 0.0F, -0.03F}, 0.25F, 0U, {}}};
constexpr EntityModelPart ugc_drop_ammo_parts[]{
    {"ugc_baseplate.kv6", {}, 1.0F, 0U, {}},
    {"ammocrate.kv6", {0.0F, 0.0F, -0.03F}, 0.25F, 0U, {}}};
constexpr EntityModelPart ugc_drop_blocks_parts[]{
    {"ugc_baseplate.kv6", {}, 1.0F, 0U, {}},
    {"block_crate.kv6", {0.0F, 0.0F, -0.03F}, 0.25F, 0U, {}}};
constexpr EntityModelPart ugc_occ_parts[]{
    {"ugc_baseplate.kv6", {}, 1.0F, 0U, {}},
    {"Bomb.kv6", {0.0F, 0.0F, -0.03F}, 1.0F, 0U, {}}};

#define UGC_ZONE_PARTS(name, mesh, size)                                                           \
    constexpr EntityModelPart name[]{                                                              \
        {"ugc_baseplate.kv6", {}, 1.0F, 0U, {}},                                                  \
        {mesh, {0.0F, 0.0F, -0.03F}, size, 0U, {}}}

UGC_ZONE_PARTS(ugc_spawn_small_parts, "ugc_spawn_zone.kv6", 0.25F);
UGC_ZONE_PARTS(ugc_spawn_medium_parts, "ugc_spawn_zone.kv6", 1.0F);
UGC_ZONE_PARTS(ugc_spawn_large_parts, "ugc_spawn_zone.kv6", 1.5F);
UGC_ZONE_PARTS(ugc_base_small_parts, "ugc_base_zone.kv6", 0.5F);
UGC_ZONE_PARTS(ugc_base_medium_parts, "ugc_base_zone.kv6", 1.0F);
UGC_ZONE_PARTS(ugc_base_large_parts, "ugc_base_zone.kv6", 1.75F);

#undef UGC_ZONE_PARTS

} // namespace

std::span<const EntityModelPart> ugc_entity_model_parts(std::uint8_t item_id) noexcept {
    switch (item_id) {
    case 0U: return ugc_drop_health_parts;
    case 1U: return ugc_drop_ammo_parts;
    case 2U: return ugc_drop_blocks_parts;
    case 3U: return ugc_occ_parts;
    case 4U:
    case 7U: return ugc_spawn_small_parts;
    case 5U:
    case 8U: return ugc_spawn_medium_parts;
    case 6U:
    case 9U: return ugc_spawn_large_parts;
    case 10U:
    case 13U:
    case 16U: return ugc_base_small_parts;
    case 11U:
    case 14U:
    case 17U: return ugc_base_medium_parts;
    case 12U:
    case 15U:
    case 18U: return ugc_base_large_parts;
    default: return {};
    }
}

std::uint8_t ugc_entity_team(std::uint8_t item_id) noexcept {
    if ((item_id >= 4U && item_id <= 6U) || (item_id >= 10U && item_id <= 12U)) {
        return 3U;
    }
    if ((item_id >= 7U && item_id <= 9U) || (item_id >= 13U && item_id <= 15U)) {
        return 2U;
    }
    return 0U;
}

std::uint8_t next_ugc_item_variant(std::uint8_t item_id) noexcept {
    switch (item_id) {
    case 0U: return 1U;
    case 1U: return 2U;
    case 2U: return 0U;
    case 4U:
    case 7U:
    case 10U:
    case 13U:
    case 16U: return static_cast<std::uint8_t>(item_id + 1U);
    case 5U:
    case 8U:
    case 11U:
    case 14U:
    case 17U: return static_cast<std::uint8_t>(item_id + 1U);
    case 6U:
    case 9U:
    case 12U:
    case 15U:
    case 18U: return static_cast<std::uint8_t>(item_id - 2U);
    default: return item_id;
    }
}

std::string_view ugc_tool_icon_asset(std::uint8_t item_id) noexcept {
    // shared.constants.UGC_TOOL_IMAGES, preserving the retail item-id order.
    constexpr std::string_view icons[]{
        "png/ui/ugc_tools/ugc_health_drop.png",
        "png/ui/ugc_tools/ugc_ammo_drop.png",
        "png/ui/ugc_tools/ugc_block_drop.png",
        "png/ui/ugc_tools/ugc_bomb_drop.png",
        "png/ui/ugc_tools/ugc_spawngreen_small.png",
        "png/ui/ugc_tools/ugc_spawngreen_med.png",
        "png/ui/ugc_tools/ugc_spawngreen_large.png",
        "png/ui/ugc_tools/ugc_spawnblue_small.png",
        "png/ui/ugc_tools/ugc_spawnblue_med.png",
        "png/ui/ugc_tools/ugc_spawnblue_large.png",
        "png/ui/ugc_tools/ugc_basegreen_small.png",
        "png/ui/ugc_tools/ugc_basegreen_med.png",
        "png/ui/ugc_tools/ugc_basegreen_large.png",
        "png/ui/ugc_tools/ugc_baseblue_small.png",
        "png/ui/ugc_tools/ugc_baseblue_med.png",
        "png/ui/ugc_tools/ugc_baseblue_large.png",
        "png/ui/ugc_tools/ugc_base_small.png",
        "png/ui/ugc_tools/ugc_base_med.png",
        "png/ui/ugc_tools/ugc_base_large.png",
    };
    return item_id < std::size(icons) ? icons[item_id] : std::string_view{};
}

std::string_view entity_category_name(EntityCategory category) noexcept {
    switch (category) {
    case EntityCategory::pickup:
        return "PICKUP";
    case EntityCategory::objective:
        return "OBJECTIVE";
    case EntityCategory::deployable:
        return "DEPLOYABLE";
    case EntityCategory::hazard:
        return "HAZARD";
    case EntityCategory::marker:
        return "MARKER";
    case EntityCategory::structure:
        return "STRUCTURE";
    case EntityCategory::projectile:
        return "PROJECTILE";
    case EntityCategory::unportable:
        break;
    }
    return "UNPORTABLE";
}

std::string_view entity_provenance_name(EntityProvenance provenance) noexcept {
    switch (provenance) {
    case EntityProvenance::retail_alias:
        return "RETAIL";
    case EntityProvenance::retail_named:
        return "RETAIL(NAMED)";
    case EntityProvenance::retail_literal:
        return "RETAIL(LITERAL)";
    case EntityProvenance::battlespades:
        // Deliberately shouty: this row is an invention we are choosing to
        // ship, not recovered parity, and the debug readout should say so.
        return "INVENTED";
    case EntityProvenance::absent:
        break;
    }
    return "UNSOURCED";
}

std::span<const EntityDefinition* const> spawnable_entities() noexcept {
    static const std::vector<const EntityDefinition*> menu = [] {
        std::vector<const EntityDefinition*> rows;
        for (const auto& definition : entity_catalog()) {
            if (definition.spawnable) {
                rows.push_back(&definition);
            }
        }
        return rows;
    }();
    return menu;
}

void apply_entity_team_material(Kv6Model& model, std::uint8_t type_id,
                                VxlColor team_color) noexcept {
    static_cast<void>(type_id);
    model.apply_default_color(team_color);
}

namespace {

/**
 * Tool id -> entity id for everything a player can put into the world.
 *
 * Deliberately a hand-written switch rather than a generated table: retail
 * expresses this as a Python class reference inside each weapon module
 * (`landmineWeapon.py` names LandmineEntity directly) plus one PICKUPS dict in
 * `list.py` for the objectives. There is no upstream table to generate from, so
 * generating one would be inventing a source rather than reading it.
 */
struct ToolEntityPair final {
    std::uint8_t tool_id;
    std::uint8_t entity_id;
};

// The three objectives come first; `tool_carrying_entity` only searches these.
constexpr ToolEntityPair carry_pairs[]{
    {25U, 14U}, // BOMB tool     -> BOMB_PICKUP
    {26U, 15U}, // DIAMOND tool  -> DIAMOND_PICKUP
    {30U, 16U}, // INTEL tool    -> INTEL_PICKUP
};

constexpr ToolEntityPair deploy_pairs[]{
    {16U, 8U},  // ROCKET_TURRET
    {20U, 9U},  // LANDMINE
    {21U, 10U}, // DYNAMITE
    {51U, 30U}, // MEDPACK
    {56U, 36U}, // RADAR_STATION
    {59U, 38U}, // C4
};

} // namespace

std::uint8_t entity_placed_by_tool(std::uint8_t tool_id) noexcept {
    for (const auto& pair : deploy_pairs) {
        if (pair.tool_id == tool_id) {
            return pair.entity_id;
        }
    }
    for (const auto& pair : carry_pairs) {
        if (pair.tool_id == tool_id) {
            return pair.entity_id;
        }
    }
    return 0U;
}

const DeployablePlacement* deployable_placement(std::uint8_t tool_id) noexcept {
    // Recovered from each weapon's own can_place_object call site. The far
    // radii are A1603/A1637/A1754/A1806/A1880/A1896.
    //
    // `others_min_radius` is deliberately absent: all six tools pass 0, and
    // retail's player-overlap check returns true only when `sq_dist < 0`,
    // which never holds. The rule is a dead no-op in the original -- you can
    // place a deployable inside another player -- so porting it would be
    // inventing a restriction retail does not have.
    static constexpr struct {
        std::uint8_t tool_id;
        DeployablePlacement rules;
    } table[]{
        {16U, {10.0F, 1.0F, 1.0F, false, true}},  // ROCKET_TURRET
        {20U, {5.0F, 0.0F, 1.0F, false, true}},   // LANDMINE (A1810 allows water)
        {21U, {5.0F, 0.0F, 1.0F, true, true}},    // DYNAMITE  -- sticks to walls
        {51U, {5.0F, 0.0F, 1.0F, false, true}},   // MEDPACK
        {56U, {10.0F, 1.0F, 1.0F, false, true}},  // RADAR_STATION
        {59U, {5.0F, 0.0F, 1.0F, true, true}},    // C4        -- sticks to walls
    };
    for (const auto& row : table) {
        if (row.tool_id == tool_id) {
            return &row.rules;
        }
    }
    return nullptr;
}

std::uint8_t entity_face_from_normal(int nx, int ny, int nz) noexcept {
    if (nx < 0) return 0U;
    if (nx > 0) return 1U;
    if (ny < 0) return 2U;
    if (ny > 0) return 3U;
    if (nz > 0) return 5U;
    // nz < 0 is the block's top, and it is also the safe default.
    return 4U;
}

std::uint8_t tool_carrying_entity(std::uint8_t type_id) noexcept {
    for (const auto& pair : carry_pairs) {
        if (pair.entity_id == type_id) {
            return pair.tool_id;
        }
    }
    return 0U;
}

std::size_t maximum_entity_parts() noexcept {
    static const std::size_t widest = [] {
        std::size_t maximum{1U};
        for (const auto& definition : entity_catalog()) {
            maximum = std::max(maximum, definition.parts.size());
        }
        return maximum;
    }();
    return widest;
}

} // namespace battlespades::world
