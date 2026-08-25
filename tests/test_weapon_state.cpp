#include "battlespades/world/weapon_state.hpp"

#include <array>
#include <iostream>
#include <stdexcept>

namespace {

using battlespades::world::WeaponReplicationState;
using battlespades::world::WeaponStateResult;

void expect(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error{message};
    }
}

void loadout_selection_and_shots_share_one_state() {
    WeaponReplicationState state;
    const std::array<std::uint8_t, 5U> loadout{2U, 17U, 9U, 17U, 65U};
    state.replace_loadout(loadout, std::uint8_t{17U});
    expect(state.loadout().size() == 3U && state.selected_tool() == 17U,
           "loadout replacement must deduplicate and reject sentinel 65");
    expect(state.ammo(17U) != nullptr && state.ammo(17U)->magazine == 6U &&
               state.ammo(17U)->reserve == 30U,
           "pistol counters must come from the generated server catalog");
    expect(state.observe_shot(9U) == WeaponStateResult::selected_tool_mismatch,
           "ShootFeedback for a non-selected tool must fail closed");
    expect(state.observe_shot(17U) == WeaponStateResult::accepted &&
               state.ammo(17U)->magazine == 5U && state.shot_sequence() == 1U,
           "accepted feedback must consume exactly one round and one event");
}

void reload_and_restock_are_deterministic() {
    WeaponReplicationState state;
    const std::array<std::uint8_t, 1U> loadout{17U};
    state.replace_loadout(loadout);
    expect(state.observe_shot(17U) == WeaponStateResult::accepted,
           "pistol must fire from a full magazine");
    expect(state.begin_reload(17U) == WeaponStateResult::accepted &&
               state.finish_reload(17U) == WeaponStateResult::accepted,
           "WeaponReload start/done edges must be accepted in order");
    expect(state.ammo(17U)->magazine == 6U && state.ammo(17U)->reserve == 29U,
           "reload must transfer only the missing rounds");
    state.restock_ammunition();
    expect(state.ammo(17U)->magazine == 6U && state.ammo(17U)->reserve == 30U &&
               !state.ammo(17U)->reloading,
           "Restock must reset clip, stock and reload edge atomically");
}

void projectile_and_unlimited_tools_use_the_same_catalog() {
    WeaponReplicationState state;
    const std::array<std::uint8_t, 3U> loadout{11U, 33U, 2U};
    state.replace_loadout(loadout, std::uint8_t{11U});
    expect(state.ammo(11U)->magazine == 2U,
           "grenade count must use the recovered retail initial count");
    expect(state.observe_shot(11U) == WeaponStateResult::accepted &&
               state.ammo(11U)->magazine == 1U,
           "projectile launch must consume its configured count");
    expect(state.select(2U) == WeaponStateResult::accepted &&
               state.observe_shot(2U) == WeaponStateResult::accepted,
           "melee tools with zero clip must remain usable");
}

} // namespace

int main() {
    try {
        loadout_selection_and_shots_share_one_state();
        reload_and_restock_are_deterministic();
        projectile_and_unlimited_tools_use_the_same_catalog();
        std::cout << "Weapon replication state tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
