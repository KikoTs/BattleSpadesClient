#include "battlespades/world/weapon_state.hpp"
#include "battlespades/world/classic_weapons.hpp"

#include <algorithm>

namespace battlespades::world {
namespace {

constexpr std::uint8_t ugc_rpg2_tool_id{46U};
constexpr std::uint8_t ugc_drillgun_tool_id{47U};

[[nodiscard]] ToolAmmoState initial_ammo(const WeaponDefinition& definition) noexcept {
    const auto& retail = definition.retail.ammo;
    if (retail.maximum_count.value_or(0U) != 0U) {
        return ToolAmmoState{retail.initial_count.value_or(0U), 0U, false};
    }
    return ToolAmmoState{
        retail.initial_magazine.value_or(definition.clip_size),
        retail.initial_reserve.value_or(definition.reserve_ammo), false};
}

} // namespace

void WeaponReplicationState::replace_loadout(
    std::span<const std::uint8_t> tool_ids,
    std::optional<std::uint8_t> selected) {
    loadout_.clear();
    for (const auto tool_id : tool_ids) {
        if (!valid_selectable_tool(tool_id) ||
            std::find(loadout_.begin(), loadout_.end(), tool_id) != loadout_.end()) {
            continue;
        }
        loadout_.push_back(tool_id);
    }
    ammunition_.fill({});
    for (const auto tool_id : loadout_) {
        const auto& definition = weapon_catalog()[tool_id];
        ammunition_[tool_id] = initial_ammo(definition);
        if (auto rules = classic_weapon_rules(classic_protocol_, tool_id))
            ammunition_[tool_id] = {rules->magazine, rules->reserve, false};
        else if (classic_protocol_ && tool_id == 31) ammunition_[tool_id] = {3, 0, false};
    }
    selected_tool_.reset();
    if (selected.has_value() && contains(*selected)) {
        selected_tool_ = selected;
    } else if (!loadout_.empty()) {
        selected_tool_ = loadout_.front();
    }
}

WeaponStateResult WeaponReplicationState::select(std::uint8_t tool_id) noexcept {
    if (!valid_selectable_tool(tool_id)) {
        return WeaponStateResult::invalid_tool;
    }
    if (!contains(tool_id)) {
        return WeaponStateResult::not_in_loadout;
    }
    selected_tool_ = tool_id;
    return WeaponStateResult::accepted;
}

WeaponStateResult
WeaponReplicationState::observe_shot(std::uint8_t tool_id) noexcept {
    if (!valid_selectable_tool(tool_id)) {
        return WeaponStateResult::invalid_tool;
    }
    if (selected_tool_ != tool_id) {
        return WeaponStateResult::selected_tool_mismatch;
    }
    auto& state = ammunition_[tool_id];
    const auto& definition = weapon_catalog()[tool_id];
    const bool finite = definition.clip_size != 0U ||
                        definition.retail.ammo.magazine_capacity.has_value() ||
                        definition.retail.ammo.maximum_count.value_or(0U) != 0U;
    if (finite) {
        if (state.magazine == 0U) {
            return WeaponStateResult::no_ammunition;
        }
        // UGCRPG2Weapon (aoslib/weapons/ugcRPG2Weapon.py): use_an_ammo is
        // `pass` and get_has_enough_ammo returns True -- the editor rocket
        // never spends its loaded round, so it never reloads either.
        if (tool_id != ugc_rpg2_tool_id) {
            --state.magazine;
        }
    }
    state.reloading = false;
    ++shot_sequence_;
    return WeaponStateResult::accepted;
}

WeaponStateResult
WeaponReplicationState::begin_reload(std::uint8_t tool_id) noexcept {
    if (!valid_selectable_tool(tool_id)) {
        return WeaponStateResult::invalid_tool;
    }
    if (!contains(tool_id)) {
        return WeaponStateResult::not_in_loadout;
    }
    auto& state = ammunition_[tool_id];
    if (state.reloading) {
        return WeaponStateResult::already_reloading;
    }
    state.reloading = true;
    return WeaponStateResult::accepted;
}

WeaponStateResult
WeaponReplicationState::finish_reload(std::uint8_t tool_id) noexcept {
    if (!valid_selectable_tool(tool_id)) {
        return WeaponStateResult::invalid_tool;
    }
    if (!contains(tool_id)) {
        return WeaponStateResult::not_in_loadout;
    }
    auto& state = ammunition_[tool_id];
    if (!state.reloading) {
        return WeaponStateResult::not_reloading;
    }
    const auto& definition = weapon_catalog()[tool_id];
    const auto capacity = definition.retail.ammo.magazine_capacity.value_or(
        definition.clip_size);
    const auto missing = static_cast<std::uint16_t>(capacity -
                                                     std::min<std::uint16_t>(
                                                         capacity,
                                                         state.magazine));
    const auto wanted = definition.retail.ammo.clip_reload
                            ? std::min<std::uint16_t>(missing, 1U)
                            : missing;
    const auto moved = std::min(wanted, state.reserve);
    state.magazine = static_cast<std::uint16_t>(state.magazine + moved);
    state.reserve = static_cast<std::uint16_t>(state.reserve - moved);
    if (tool_id == ugc_drillgun_tool_id) {
        // UGCDrillgunWeapon.get_ammo_after_reload returns (1, 1): every
        // reload leaves one round loaded and one in reserve, so the editor
        // drill never runs dry.
        state.magazine = 1U;
        state.reserve = 1U;
    }
    state.reloading = false;
    return WeaponStateResult::accepted;
}

void WeaponReplicationState::cancel_reload() noexcept {
    // Retail stores reload ownership on Character rather than permanently on
    // a weapon. Tool changes, death, and class replacement all end that one
    // global reload, so no previously selected tool may retain a stale flag.
    for (const auto tool_id : loadout_) {
        ammunition_[tool_id].reloading = false;
    }
}

void WeaponReplicationState::restock_ammunition() noexcept {
    for (const auto tool_id : loadout_) {
        if (classic_protocol_) {
            // Classic bases replenish reserve without loading the magazine
            // or interrupting the server-owned reload already in progress.
            if (const auto rules = classic_weapon_rules(classic_protocol_, tool_id))
                ammunition_[tool_id].reserve = rules->reserve;
            else if (tool_id == 31)
                ammunition_[tool_id] = {3, 0, false};
            continue;
        }
        const auto& definition = weapon_catalog()[tool_id];
        ammunition_[tool_id] = initial_ammo(definition);
    }
}

void WeaponReplicationState::set_authoritative_ammo(std::uint8_t tool, std::uint16_t magazine, std::uint16_t reserve) noexcept {
    if (contains(tool)) ammunition_[tool] = {magazine, reserve, false};
}

bool WeaponReplicationState::restock_from_ammo_crate() noexcept {
    bool changed{};
    for (const auto tool_id : loadout_) {
        const auto& definition = weapon_catalog()[tool_id];
        const auto& retail = definition.retail.ammo;
        auto& state = ammunition_[tool_id];
        const auto before = state;

        if (retail.maximum_count.value_or(0U) != 0U) {
            // Count-based tools (grenades, deployables) top up toward their
            // maximum by a per-tool restock amount, not to full.
            const auto ceiling = *retail.maximum_count;
            const auto step = retail.count_restock_amount.value_or(ceiling);
            state.magazine = std::min<std::uint16_t>(
                static_cast<std::uint16_t>(state.magazine + step), ceiling);
        } else {
            const auto step = retail.restock_amount.value_or(0U);
            // THE PREDICATE, and it is easy to get backwards: retail tests the
            // RESERVE cap, and when there is no reserve the crate tops up the
            // MAGAZINE instead. Inverting this silently breaks every weapon
            // that has a magazine but no reserve -- nine catalog rows,
            // including the turret, landmine, dynamite and C4 -- which would
            // then never restock from a crate at all.
            if (retail.reserve_capacity.value_or(0U) == 0U) {
                const auto ceiling = retail.magazine_capacity.value_or(
                    definition.clip_size);
                state.magazine = std::min<std::uint16_t>(
                    static_cast<std::uint16_t>(state.magazine + step), ceiling);
            } else {
                state.reserve = std::min<std::uint16_t>(
                    static_cast<std::uint16_t>(state.reserve + step),
                    *retail.reserve_capacity);
            }
        }
        if (state.magazine != before.magazine || state.reserve != before.reserve) {
            changed = true;
        }
    }
    return changed;
}

std::span<const std::uint8_t> WeaponReplicationState::loadout() const noexcept {
    return loadout_;
}

std::optional<std::uint8_t>
WeaponReplicationState::selected_tool() const noexcept {
    return selected_tool_;
}

const ToolAmmoState*
WeaponReplicationState::ammo(std::uint8_t tool_id) const noexcept {
    return contains(tool_id) ? &ammunition_[tool_id] : nullptr;
}

std::uint64_t WeaponReplicationState::shot_sequence() const noexcept {
    return shot_sequence_;
}

bool WeaponReplicationState::contains(std::uint8_t tool_id) const noexcept {
    return std::find(loadout_.begin(), loadout_.end(), tool_id) != loadout_.end();
}

} // namespace battlespades::world
