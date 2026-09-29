#pragma once

#include "battlespades/world/retail_inventory.hpp"
#include "battlespades/world/weapon_runtime.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace battlespades::world {

enum class PlayerLoadoutScope : std::uint8_t {
    retail_default,
    all_class_tools,
    all_weapons,
};

/**
 * One source of truth for toolbar selection, per-tool ammunition and blocks.
 * Health/ammo/block pickups call distinct restock methods so an ammo crate can
 * never silently refill health or construction stock.
 */
class PlayerInventory final {
public:
    explicit PlayerInventory(std::uint32_t random_seed = 0xA05B168U) noexcept;

    [[nodiscard]] bool spawn_as(std::uint8_t class_id,
                                PlayerLoadoutScope scope = PlayerLoadoutScope::retail_default);
    /** Install the exact normalized Protocol 168 loadout for a live spawn. */
    [[nodiscard]] bool spawn_with_loadout(std::uint8_t class_id,
                                          std::span<const std::uint8_t> tools);
    /** Install all three retail combined-index address spaces. */
    [[nodiscard]] bool spawn_with_selection(
        std::uint8_t class_id,
        std::span<const std::uint8_t> tools,
        std::span<const std::string> prefabs,
        std::span<const std::uint8_t> ugc_tools = {});
    [[nodiscard]] bool select_tool(std::uint8_t tool_id,
                                   InventorySelectionOrigin origin =
                                       InventorySelectionOrigin::direct_slot) noexcept;
    [[nodiscard]] bool select_slot(std::size_t index,
                                   InventorySelectionOrigin origin =
                                       InventorySelectionOrigin::direct_slot) noexcept;
    [[nodiscard]] bool cycle(int direction) noexcept;
    void tick(double dt) noexcept;
    /**
     * Apply the pickup byte from the owner's WorldUpdate row.
     *
     * Bomb, diamond and intel tools are members of the wire loadout even when
     * they are not carried. Retail exposes their toolbar slots only while the
     * corresponding pickup id is active.
     *
     * With `equip`, the Player pickup setter's retail behaviour applies: the
     * carrier is switched to the pickup tool (PICKUPS in aoslib/weapons/
     * list.py) and the player's own switch keys and wheel cannot leave it
     * until the pickup is gone (verified live against the retail client in
     * modern CTF). Classic CTF (`can_shoot_holding_intel`) passes false for
     * the intel: the carrier keeps shooting and the HUD shows the intel icon.
     */
    void set_carried_pickup(std::uint8_t pickup_id, bool equip = false) noexcept;
    [[nodiscard]] bool carried_pickup_locks_tool() const noexcept {
        return carried_objective_locked_;
    }

    [[nodiscard]] bool spend_blocks(std::uint16_t amount = 1U) noexcept;
    /**
     * InitialInfo.block_wallet_multiplier, applied at the next spawn exactly
     * like the server: max = round(max * m), start = min(max, round(start * m)),
     * Python's round-half-even.
     */
    void set_block_wallet_multiplier(double multiplier) noexcept;
    void add_blocks(std::uint16_t amount) noexcept;
    void restock_blocks() noexcept;
    void restock_ammunition() noexcept;
    /** Walk-through ammo crate: a partial top-up, not a spawn reset. */
    [[nodiscard]] bool restock_from_ammo_crate() noexcept;

    [[nodiscard]] std::uint8_t class_id() const noexcept { return class_id_; }
    [[nodiscard]] std::uint16_t blocks() const noexcept { return blocks_; }
    [[nodiscard]] std::uint16_t maximum_blocks() const noexcept { return maximum_blocks_; }
    [[nodiscard]] const ToolAmmoState* ammo(std::uint8_t tool_id) const noexcept;
    [[nodiscard]] const RetailInventory& toolbar() const noexcept { return toolbar_; }
    [[nodiscard]] RetailInventory& toolbar() noexcept { return toolbar_; }
    [[nodiscard]] const WeaponRuntime& weapons() const noexcept { return weapons_; }
    [[nodiscard]] WeaponRuntime& weapons() noexcept { return weapons_; }
    [[nodiscard]] std::string_view selected_prefab() const noexcept;
    /** Selected Map Creator Game Data item (wire ids 0..18). */
    [[nodiscard]] std::optional<std::uint8_t> selected_ugc_item() const noexcept;
    /** Select the next retail RMB variant in the current UGC item group. */
    [[nodiscard]] bool cycle_selected_ugc_item_variant() noexcept;
    [[nodiscard]] std::span<const std::string> prefabs() const noexcept {
        return prefabs_;
    }

private:
    void rebuild_toolbar(bool preserve_selection) noexcept;
    void refresh_toolbar_ammunition() noexcept;
    void auto_switch_from_empty_tool() noexcept;

    WeaponRuntime weapons_;
    RetailInventory toolbar_;
    std::uint8_t class_id_{};
    std::uint16_t blocks_{};
    std::uint16_t maximum_blocks_{};
    double block_wallet_multiplier_{1.0};
    std::vector<std::uint8_t> loadout_tools_;
    std::vector<std::string> prefabs_;
    std::vector<std::uint8_t> ugc_tools_;
    std::optional<std::uint8_t> carried_objective_tool_;
    bool carried_objective_locked_{};
    bool expose_dormant_tools_{};
};

/**
 * Resolve a safe server prefab identifier to its toolbar preview path.
 *
 * The packet-provided inventory is dynamic; restricting it to a compiled
 * allow-list made newly shipped/UGC prefabs fall back to one static icon.
 * Identifiers are still syntax-validated so a server cannot request an
 * arbitrary path.
 */
[[nodiscard]] std::string prefab_preview_asset(std::string_view prefab_name);

/** Resolve only when the safe preview is actually present below asset_root. */
[[nodiscard]] std::string
prefab_preview_asset(const std::filesystem::path& asset_root,
                     std::string_view prefab_name);

} // namespace battlespades::world
