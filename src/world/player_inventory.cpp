#include "battlespades/world/player_inventory.hpp"

#include "battlespades/world/class_catalog.hpp"
#include "battlespades/world/entity_catalog.hpp"
#include "battlespades/world/weapon_catalog.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <limits>
#include <vector>

namespace battlespades::world {
namespace {

constexpr std::uint8_t prefab_tool{23U};
constexpr std::uint8_t bomb_tool{25U};
constexpr std::uint8_t diamond_tool{26U};
constexpr std::uint8_t zombie_prefab_tool{28U};
constexpr std::uint8_t intel_tool{30U};
constexpr std::uint8_t ugc_entity_tool{41U};
constexpr std::uint8_t ugc_prefab_tool{42U};
constexpr std::uint8_t ugc_builder_class{13U};
constexpr std::uint8_t maximum_ugc_item{18U};

[[nodiscard]] bool contains(std::span<const std::uint8_t> values,
                            std::uint8_t value) noexcept {
    return std::ranges::find(values, value) != values.end();
}

[[nodiscard]] bool is_prefab_mechanism(std::uint8_t tool) noexcept {
    return tool == prefab_tool || tool == zombie_prefab_tool ||
           tool == ugc_prefab_tool;
}

[[nodiscard]] bool is_dormant_objective(std::uint8_t tool) noexcept {
    return tool == bomb_tool || tool == diamond_tool || tool == intel_tool;
}

[[nodiscard]] std::optional<std::uint8_t>
objective_tool_for_pickup(std::uint8_t pickup_id) noexcept {
    switch (pickup_id) {
    case 14U:
        return bomb_tool;
    case 15U:
        return diamond_tool;
    case 16U:
        return intel_tool;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<std::uint8_t> prefab_tool_for(
    std::span<const std::uint8_t> loadout,
    std::span<const std::uint8_t> ugc_tools) noexcept {
    // ZombiePrefabTool and UGCPrefabTool are distinct wire tools with distinct
    // hands/action semantics. Falling back to tool 23 made their variants look
    // selectable while emitting the wrong Protocol 168 tool byte.
    if (contains(loadout, zombie_prefab_tool)) return zombie_prefab_tool;
    if (contains(loadout, ugc_prefab_tool) ||
        contains(ugc_tools, ugc_prefab_tool)) {
        return ugc_prefab_tool;
    }
    if (contains(loadout, prefab_tool)) return prefab_tool;
    return std::nullopt;
}

[[nodiscard]] InventorySlot make_slot(
    const WeaponRuntime& weapons, std::uint8_t tool,
    InventorySlotKind kind = InventorySlotKind::loadout,
    std::uint16_t variant_id = 0U) noexcept {
    const auto* weapon = find_weapon_definition(tool);
    const auto* state = weapons.replication().ammo(tool);
    const bool finite =
        weapon != nullptr &&
        (weapon->clip_size != 0U ||
         weapon->retail.ammo.maximum_count.value_or(0U) != 0U ||
         weapon->retail.ammo.magazine_capacity.has_value());
    return InventorySlot{
        tool,
        kind,
        variant_id,
        true,
        !finite ||
            (state != nullptr && (state->magazine > 0U || state->reserve > 0U)),
        kind == InventorySlotKind::prefab ||
            (weapon != nullptr && weapon->selectable_when_empty),
    };
}

} // namespace

PlayerInventory::PlayerInventory(std::uint32_t random_seed) noexcept
    : weapons_{random_seed} {}

bool PlayerInventory::spawn_as(std::uint8_t class_id, PlayerLoadoutScope scope) {
    const auto* definition = find_class_definition(class_id);
    if (definition == nullptr) {
        return false;
    }
    std::vector<std::uint8_t> tools;
    if (scope == PlayerLoadoutScope::all_weapons) {
        tools.reserve(weapon_catalog().size());
        for (const auto& weapon : weapon_catalog()) {
            tools.push_back(weapon.tool_id);
        }
    } else if (scope == PlayerLoadoutScope::all_class_tools) {
        tools = class_test_tools(*definition);
    } else {
        for (const auto item : default_class_items(*definition)) {
            if (item <= 64U && valid_selectable_tool(static_cast<std::uint8_t>(item))) {
                tools.push_back(static_cast<std::uint8_t>(item));
            }
        }
    }
    if (!spawn_with_loadout(class_id, tools)) {
        return false;
    }
    if (scope == PlayerLoadoutScope::all_weapons) {
        // The F4 developer arsenal deliberately exposes protocol tools that
        // retail hides in a live match, so every catalog entry remains
        // inspectable without pretending those slots belong to a real spawn.
        expose_dormant_tools_ = true;
        rebuild_toolbar(false);
    }
    return true;
}

bool PlayerInventory::spawn_with_loadout(std::uint8_t class_id,
                                         std::span<const std::uint8_t> requested) {
    return spawn_with_selection(class_id, requested, {}, {});
}

bool PlayerInventory::spawn_with_selection(
    std::uint8_t class_id,
    std::span<const std::uint8_t> requested,
    std::span<const std::string> requested_prefabs,
    std::span<const std::uint8_t> requested_ugc_tools) {
    const auto* definition = find_class_definition(class_id);
    if (definition == nullptr) {
        return false;
    }
    std::vector<std::uint8_t> tools;
    tools.reserve(requested.size());
    for (const auto tool : requested) {
        if (valid_selectable_tool(tool) &&
            std::find(tools.begin(), tools.end(), tool) == tools.end()) {
            tools.push_back(tool);
        }
    }
    if (tools.empty()) return false;
    class_id_ = class_id;
    {
        // Player._block_wallet_max/_block_wallet_start on the server:
        // int(round(value * RULE_CHARACTER_BLOCK_WALLETS)), half-even.
        const auto scaled = [this](std::uint16_t value) {
            const double product = static_cast<double>(value) * block_wallet_multiplier_;
            return static_cast<std::uint16_t>(
                std::clamp(std::nearbyint(product), 0.0, 65535.0));
        };
        maximum_blocks_ = scaled(definition->maximum_blocks);
        blocks_ = std::min(maximum_blocks_, scaled(definition->initial_blocks));
    }
    loadout_tools_ = tools;
    prefabs_.assign(requested_prefabs.begin(), requested_prefabs.end());
    ugc_tools_.clear();
    ugc_tools_.reserve(requested_ugc_tools.size());
    for (const auto tool : requested_ugc_tools) {
        const bool valid = class_id == ugc_builder_class
                               ? tool <= maximum_ugc_item
                               : valid_selectable_tool(tool);
        if (valid &&
            std::find(ugc_tools_.begin(), ugc_tools_.end(), tool) ==
                ugc_tools_.end()) {
            ugc_tools_.push_back(tool);
        }
    }
    carried_objective_tool_.reset();
    carried_objective_locked_ = false;
    expose_dormant_tools_ = false;
    auto weapon_tools = tools;
    if (class_id != ugc_builder_class) {
        for (const auto tool : ugc_tools_) {
            if (std::find(weapon_tools.begin(), weapon_tools.end(), tool) ==
                weapon_tools.end()) {
                weapon_tools.push_back(tool);
            }
        }
    }
    // The combined HUD and weapon runtime are one transaction. Previously a
    // UGC slot could be highlighted while WeaponReplication rejected it as
    // absent, leaving the old gun active and desynchronizing packet tool ids.
    weapons_.replace_loadout(weapon_tools, tools.front());
    rebuild_toolbar(false);
    return true;
}

void PlayerInventory::rebuild_toolbar(bool preserve_selection) noexcept {
    std::optional<InventorySlot> previous_slot;
    if (preserve_selection) {
        const auto selected = toolbar_.selected_index();
        if (selected.has_value() && *selected < toolbar_.slots().size()) {
            previous_slot = toolbar_.slots()[*selected];
        }
    }

    std::vector<InventorySlot> slots;
    slots.reserve(loadout_tools_.size() + prefabs_.size() + ugc_tools_.size());
    for (const auto tool : loadout_tools_) {
        if (class_id_ == ugc_builder_class && tool == ugc_entity_tool &&
            !ugc_tools_.empty()) {
            continue;
        }
        if (!expose_dormant_tools_ && is_prefab_mechanism(tool)) {
            continue;
        }
        if (!expose_dormant_tools_ && is_dormant_objective(tool) &&
            carried_objective_tool_ != tool) {
            continue;
        }
        slots.push_back(make_slot(weapons_, tool));
    }

    if (!expose_dormant_tools_) {
        const auto mechanism = prefab_tool_for(loadout_tools_, ugc_tools_);
        if (mechanism.has_value()) {
            for (std::size_t index{}; index < prefabs_.size(); ++index) {
                slots.push_back(make_slot(
                    weapons_, *mechanism, InventorySlotKind::prefab,
                    static_cast<std::uint16_t>(index)));
            }
        }
    }

    for (const auto tool : ugc_tools_) {
        if (class_id_ == ugc_builder_class) {
            slots.push_back(make_slot(
                weapons_, ugc_entity_tool, InventorySlotKind::ugc_tool, tool));
            continue;
        }
        if (!expose_dormant_tools_ && tool == ugc_prefab_tool &&
            !prefabs_.empty()) {
            continue;
        }
        slots.push_back(
            make_slot(weapons_, tool, InventorySlotKind::ugc_tool, tool));
    }

    std::optional<std::size_t> selected;
    if (previous_slot.has_value()) {
        const auto found = std::ranges::find_if(slots, [&](const auto& slot) {
            return slot.tool_id == previous_slot->tool_id &&
                   slot.kind == previous_slot->kind &&
                   slot.variant_id == previous_slot->variant_id;
        });
        if (found != slots.end()) {
            selected = static_cast<std::size_t>(found - slots.begin());
        }
    }
    if (!preserve_selection && !slots.empty()) {
        selected = 0U;
    }
    toolbar_.set_slots(std::move(slots), selected);
    if (const auto selected_tool = toolbar_.selected_tool_id();
        selected_tool.has_value()) {
        static_cast<void>(weapons_.select(*selected_tool));
    }
}

bool PlayerInventory::select_tool(std::uint8_t tool_id,
                                  InventorySelectionOrigin origin) noexcept {
    const auto& slots = toolbar_.slots();
    const auto found = std::ranges::find_if(slots, [tool_id](const auto& slot) {
        return slot.tool_id == tool_id;
    });
    if (found == slots.end()) {
        return false;
    }
    return select_slot(static_cast<std::size_t>(found - slots.begin()), origin);
}

bool PlayerInventory::select_slot(std::size_t index,
                                  InventorySelectionOrigin origin) noexcept {
    // Retail Tool.can_swap only gates the player's own switch keys; loadout
    // sync and automatic switches are not refused by the held tool.
    const bool player_switch = origin == InventorySelectionOrigin::direct_slot ||
                               origin == InventorySelectionOrigin::mouse_wheel;
    if (player_switch && carried_objective_locked_) {
        return false;
    }
    if (!toolbar_.select_slot(index, origin, !player_switch || !weapons_.swap_locked())) {
        return false;
    }
    const auto selected = toolbar_.selected_tool_id();
    return selected.has_value() && weapons_.select(*selected) == WeaponStateResult::accepted;
}

bool PlayerInventory::cycle(int direction) noexcept {
    if (carried_objective_locked_ ||
        !toolbar_.cycle(direction, !weapons_.swap_locked())) {
        return false;
    }
    const auto selected = toolbar_.selected_tool_id();
    return selected.has_value() && weapons_.select(*selected) == WeaponStateResult::accepted;
}

void PlayerInventory::tick(double dt) noexcept {
    weapons_.tick(dt);
    toolbar_.tick(dt);
    refresh_toolbar_ammunition();
    if (weapons_.take_auto_switch_request()) {
        auto_switch_from_empty_tool();
    }
}

void PlayerInventory::auto_switch_from_empty_tool() noexcept {
    // Character.auto_switch_tool (character.pyd 0x10064D00) walks
    // AMMO_DEPLETED_SWITCH_ORDER = [CLASS_PRIMARY_WEAPONS,
    // CLASS_SECONDARY_WEAPONS, CLASS_MELEE] over the class's CLASS_ITEMS
    // (AMMO_DEPLETED_EXCEPTIONS is empty) and selects the first carried item
    // whose ammo is above zero; with none it stays put.
    const auto& slots = toolbar_.slots();
    const auto current = toolbar_.selected_index();
    const auto* definition = find_class_definition(class_id_);
    if (slots.empty() || !current.has_value() || definition == nullptr) {
        return;
    }
    const auto current_tool = slots[*current].tool_id;
    for (const auto group : {ClassItemGroup::primary, ClassItemGroup::secondary,
                             ClassItemGroup::melee}) {
        for (const auto item : definition->item_groups[static_cast<std::size_t>(group)]) {
            if (item >= selectable_tool_count || item == current_tool) continue;
            for (std::size_t index{}; index < slots.size(); ++index) {
                const auto& slot = slots[index];
                if (slot.kind == InventorySlotKind::loadout && slot.tool_id == item &&
                    slot.selectable && slot.has_ammo) {
                    static_cast<void>(select_slot(index, InventorySelectionOrigin::automatic));
                    return;
                }
            }
        }
    }
}

void PlayerInventory::set_carried_pickup(std::uint8_t pickup_id, bool equip) noexcept {
    const auto objective = objective_tool_for_pickup(pickup_id);
    const bool lock = objective.has_value() && equip;
    if (carried_objective_tool_ == objective && carried_objective_locked_ == lock) {
        return;
    }
    const bool changed = carried_objective_tool_ != objective;
    carried_objective_tool_ = objective;
    carried_objective_locked_ = false;
    if (changed) {
        // A dropped objective leaves the selection to set_slots, which falls
        // back to the first selectable slot (the spawn default).
        rebuild_toolbar(true);
    }
    if (lock) {
        static_cast<void>(select_tool(*objective, InventorySelectionOrigin::automatic));
        carried_objective_locked_ = true;
    }
}

bool PlayerInventory::spend_blocks(std::uint16_t amount) noexcept {
    if (amount > blocks_) {
        return false;
    }
    blocks_ = static_cast<std::uint16_t>(blocks_ - amount);
    return true;
}

void PlayerInventory::set_block_wallet_multiplier(double multiplier) noexcept {
    block_wallet_multiplier_ =
        std::isfinite(multiplier) && multiplier >= 0.0 ? multiplier : 1.0;
}

void PlayerInventory::add_blocks(std::uint16_t amount) noexcept {
    const auto sum = static_cast<std::uint32_t>(blocks_) + amount;
    blocks_ = static_cast<std::uint16_t>(
        std::min<std::uint32_t>(sum, maximum_blocks_));
}

void PlayerInventory::restock_blocks() noexcept { blocks_ = maximum_blocks_; }

void PlayerInventory::restock_ammunition() noexcept {
    weapons_.restock_ammunition();
    refresh_toolbar_ammunition();
}

bool PlayerInventory::restock_from_ammo_crate() noexcept {
    const bool changed = weapons_.restock_from_ammo_crate();
    refresh_toolbar_ammunition();
    return changed;
}

const ToolAmmoState* PlayerInventory::ammo(std::uint8_t tool_id) const noexcept {
    return weapons_.replication().ammo(tool_id);
}

std::string_view PlayerInventory::selected_prefab() const noexcept {
    const auto index = toolbar_.selected_index();
    if (!index.has_value() || *index >= toolbar_.slots().size()) return {};
    const auto& slot = toolbar_.slots()[*index];
    if (slot.kind != InventorySlotKind::prefab || slot.variant_id >= prefabs_.size()) {
        return {};
    }
    return prefabs_[slot.variant_id];
}

std::optional<std::uint8_t> PlayerInventory::selected_ugc_item() const noexcept {
    const auto index = toolbar_.selected_index();
    if (class_id_ != ugc_builder_class || !index.has_value() ||
        *index >= toolbar_.slots().size()) {
        return std::nullopt;
    }
    const auto& slot = toolbar_.slots()[*index];
    if (slot.kind != InventorySlotKind::ugc_tool || slot.tool_id != ugc_entity_tool ||
        slot.variant_id > maximum_ugc_item) {
        return std::nullopt;
    }
    return static_cast<std::uint8_t>(slot.variant_id);
}

bool PlayerInventory::cycle_selected_ugc_item_variant() noexcept {
    const auto current = selected_ugc_item();
    if (!current.has_value()) {
        return false;
    }
    const auto next = next_ugc_item_variant(*current);
    if (next == *current) {
        return false;
    }
    const auto& slots = toolbar_.slots();
    for (std::size_t index{}; index < slots.size(); ++index) {
        if (slots[index].kind == InventorySlotKind::ugc_tool &&
            slots[index].tool_id == ugc_entity_tool && slots[index].variant_id == next) {
            return toolbar_.select_slot(index, InventorySelectionOrigin::direct_slot);
        }
    }
    return false;
}

void PlayerInventory::refresh_toolbar_ammunition() noexcept {
    const auto& slots = toolbar_.slots();
    for (std::size_t index{}; index < slots.size(); ++index) {
        const auto& slot = slots[index];
        const auto* weapon = find_weapon_definition(slot.tool_id);
        const auto* state = ammo(slot.tool_id);
        const bool finite = weapon != nullptr &&
            (weapon->clip_size != 0U || weapon->retail.ammo.maximum_count.value_or(0U) != 0U ||
             weapon->retail.ammo.magazine_capacity.has_value());
        toolbar_.set_slot_ammunition(index, !finite || (state != nullptr &&
            (state->magazine > 0U || state->reserve > 0U)));
    }
    if (const auto tool = toolbar_.selected_tool_id(); tool.has_value() &&
        weapons_.replication().selected_tool() != tool) {
        static_cast<void>(weapons_.select(*tool));
    }
}

std::string prefab_preview_asset(std::string_view prefab_name) {
    if (prefab_name.size() < 8U || prefab_name.size() > 96U) {
        return {};
    }
    const bool safe = std::ranges::all_of(prefab_name, [](char value) {
        const auto byte = static_cast<unsigned char>(value);
        return std::isalnum(byte) != 0 || value == '_' || value == '-';
    });
    if (!safe) return {};
    std::string canonical{prefab_name};
    std::ranges::transform(canonical, canonical.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    if (canonical.starts_with("ugc_prefab_")) return "ugc/prefabs/" + canonical + ".png";
    if (!canonical.starts_with("prefab_")) return {};
    return "prefabs/" + canonical + ".png";
}

std::string prefab_preview_asset(const std::filesystem::path& asset_root,
                                 std::string_view prefab_name) {
    const auto relative = prefab_preview_asset(prefab_name);
    if (relative.empty()) return {};
    std::error_code error;
    if (!std::filesystem::is_regular_file(asset_root / relative, error) ||
        error) {
        return {};
    }
    return relative;
}

} // namespace battlespades::world
