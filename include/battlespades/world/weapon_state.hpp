#pragma once

#include "battlespades/world/weapon_catalog.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace battlespades::world {

struct ToolAmmoState final {
    std::uint16_t magazine{};
    std::uint16_t reserve{};
    bool reloading{};
};

enum class WeaponStateResult : std::uint8_t {
    accepted,
    invalid_tool,
    not_in_loadout,
    selected_tool_mismatch,
    no_ammunition,
    already_reloading,
    not_reloading,
};

/**
 * Packet-neutral weapon state for one player generation.
 *
 * CreatePlayer/SetClassLoadout replace the complete loadout; ClientData or a
 * WorldUpdate row selects a tool; ShootFeedback consumes an observer round;
 * WeaponReload edges move rounds; Restock resets counters. Keeping this logic
 * independent of rendering prevents first-person animation code from becoming
 * a second source of ammo or selection truth.
 */
class WeaponReplicationState final {
public:
    void replace_loadout(std::span<const std::uint8_t> tool_ids,
                         std::optional<std::uint8_t> selected = std::nullopt);

    [[nodiscard]] WeaponStateResult select(std::uint8_t tool_id) noexcept;
    [[nodiscard]] WeaponStateResult observe_shot(std::uint8_t tool_id) noexcept;
    [[nodiscard]] WeaponStateResult begin_reload(std::uint8_t tool_id) noexcept;
    [[nodiscard]] WeaponStateResult finish_reload(std::uint8_t tool_id) noexcept;
    /** Cancel Character's single global reload state across the old loadout. */
    void cancel_reload() noexcept;
    /** Full spawn reset: every tool back to its initial magazine and reserve. */
    void restock_ammunition() noexcept;
    /**
     * Partial top-up from an ammo crate. Returns true if anything moved.
     *
     * Deliberately distinct from `restock_ammunition()` rather than a flag on
     * it: retail's crate is a PARTIAL top-up while spawn is a full reset, and
     * the two are selected by different network paths. Collapsing them into one
     * method would force the later Restock(69) handler to pick a behaviour it
     * has no business choosing.
     */
    [[nodiscard]] bool restock_from_ammo_crate() noexcept;

    [[nodiscard]] std::span<const std::uint8_t> loadout() const noexcept;
    [[nodiscard]] std::optional<std::uint8_t> selected_tool() const noexcept;
    [[nodiscard]] const ToolAmmoState* ammo(std::uint8_t tool_id) const noexcept;
    [[nodiscard]] std::uint64_t shot_sequence() const noexcept;

private:
    [[nodiscard]] bool contains(std::uint8_t tool_id) const noexcept;

    std::vector<std::uint8_t> loadout_;
    std::optional<std::uint8_t> selected_tool_;
    std::array<ToolAmmoState, selectable_tool_count> ammunition_{};
    std::uint64_t shot_sequence_{};
};

} // namespace battlespades::world
