#include "battlespades/world/player_inventory.hpp"
#include "battlespades/world/class_selection.hpp"
#include "battlespades/world/weapon_catalog.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error{message};
}
}

int main() {
    using namespace battlespades::world;
    try {
        PlayerInventory inventory;
        expect(inventory.spawn_as(0U), "Soldier spawn must create a loadout");
        expect(inventory.blocks() == 200U && inventory.maximum_blocks() == 1000U,
               "Soldier block wallet must match CLASS_BLOCKS");
        expect(inventory.spend_blocks(25U) && inventory.blocks() == 175U,
               "block placement must consume only block inventory");
        inventory.restock_blocks();
        expect(inventory.blocks() == 1000U, "block crate must restore the class maximum");

        expect(inventory.spawn_as(1U, PlayerLoadoutScope::all_class_tools),
               "Scout test loadout must include every class option");
        expect(inventory.select_tool(17U), "Scout pistol must be selectable");
        inventory.weapons().set_primary(true);
        inventory.tick(1.0 / 60.0);
        inventory.weapons().set_primary(false);
        inventory.tick(1.0 / 60.0);
        expect(inventory.ammo(17U)->magazine == 5U,
               "firing must consume the shared ammo inventory");
        const auto blocks_before_ammo = inventory.blocks();
        inventory.restock_ammunition();
        expect(inventory.ammo(17U)->magazine == 6U &&
                   inventory.blocks() == blocks_before_ammo,
               "ammo pickup must not refill or alter blocks");

        expect(inventory.spawn_as(12U, PlayerLoadoutScope::all_weapons),
               "debug arsenal must accept the complete weapon catalog");
        expect(inventory.toolbar().slots().size() == 65U,
               "debug arsenal must expose every Protocol 168 tool exactly once");
        expect(inventory.select_tool(64U), "last real tool must be testable");
        expect(!inventory.select_tool(65U), "protocol sentinel must never enter inventory");

        const std::vector<std::uint8_t> live_loadout{17U, 2U, 5U, 17U, 255U};
        expect(inventory.spawn_with_loadout(1U, live_loadout),
               "a normalized live loadout must be accepted");
        expect(inventory.toolbar().slots().size() == 3U &&
                   inventory.toolbar().slots()[0U].tool_id == 17U &&
                   inventory.toolbar().slots()[1U].tool_id == 2U &&
                   inventory.toolbar().slots()[2U].tool_id == 5U,
               "live loadout must preserve order while filtering duplicates/invalid ids");

        // Official servers include dormant objective capabilities and the
        // generic prefab mechanism in the wire loadout. Retail does not show
        // those as ordinary random items.
        const std::vector<std::uint8_t> official_scout{
            5U, 0U, 18U, 17U, 20U, 23U, 30U, 25U, 26U};
        expect(inventory.spawn_with_selection(1U, official_scout, {}, {}),
               "captured official Scout loadout must be accepted");
        expect(inventory.toolbar().slots().size() == 5U &&
                   inventory.toolbar().slots()[0U].tool_id == 5U &&
                   inventory.toolbar().slots()[4U].tool_id == 20U &&
                   !inventory.select_tool(23U) &&
                   !inventory.select_tool(25U) &&
                   !inventory.select_tool(26U) &&
                   !inventory.select_tool(30U),
               "dormant prefab/objective tools must not pollute the live toolbar");
        inventory.set_carried_pickup(16U);
        expect(inventory.toolbar().slots().size() == 6U &&
                   inventory.select_tool(30U),
               "carried intel must expose its objective slot");
        inventory.set_carried_pickup(0xFFU);
        expect(inventory.toolbar().slots().size() == 5U &&
                   !inventory.select_tool(30U),
               "dropping intel must remove its objective slot");

        const auto engineer = default_class_selection(12U);
        expect(engineer.prefabs.size() == 3U,
               "retail class defaults must select exactly three prefabs");
        expect(inventory.spawn_with_selection(engineer.class_id, engineer.loadout,
                                              engineer.prefabs,
                                              engineer.ugc_tools),
               "complete SetClassLoadout selection must create an inventory");
        const auto ordinary_slots = static_cast<std::size_t>(std::count_if(
            engineer.loadout.begin(), engineer.loadout.end(),
            [](std::uint8_t tool) {
                return tool != 23U && tool != 25U && tool != 26U &&
                       tool != 30U && valid_selectable_tool(tool);
            }));
        expect(inventory.toolbar().slots().size() == ordinary_slots + 3U,
               "combined index must replace the generic prefab tool with variants");
        expect(inventory.select_slot(ordinary_slots + 1U),
               "a prefab variant slot must select PREFAB_TOOL");
        expect(inventory.toolbar().selected_tool_id() == 23U &&
                   inventory.selected_prefab() == engineer.prefabs[1U],
               "combined prefab slot must preserve its concrete prefab name");

        const std::vector<std::uint8_t> zombie_loadout{24U, 28U, 23U};
        const std::vector<std::string> zombie_prefabs{
            "prefab_zombiehand", "prefab_zombiebone", "prefab_zombiehead"};
        expect(inventory.spawn_with_selection(
                   4U, zombie_loadout, zombie_prefabs, {}) &&
                   inventory.toolbar().slots().size() == 4U &&
                   inventory.toolbar().slots()[0U].tool_id == 24U &&
                   inventory.toolbar().slots()[1U].tool_id == 28U &&
                   inventory.toolbar().slots()[1U].kind ==
                       InventorySlotKind::prefab &&
                   inventory.toolbar().slots()[3U].tool_id == 28U,
               "Zombie prefab variants must use ZombiePrefabTool(28)");

        const std::vector<std::uint8_t> ugc_prefab_loadout{44U, 41U, 42U};
        const std::vector<std::string> ugc_prefabs{"prefab_platform"};
        const std::vector<std::uint8_t> ugc_prefab_tools{0U, 18U, 19U};
        expect(inventory.spawn_with_selection(
                   13U, ugc_prefab_loadout, ugc_prefabs,
                   ugc_prefab_tools) &&
                   inventory.toolbar().slots().size() == 4U &&
                   inventory.toolbar().slots()[1U].kind ==
                       InventorySlotKind::prefab &&
                   inventory.toolbar().slots()[1U].tool_id == 42U &&
                   inventory.toolbar().slots()[2U].tool_id == 41U &&
                   inventory.toolbar().slots()[2U].variant_id == 0U &&
                   inventory.toolbar().slots()[3U].variant_id == 18U &&
                   inventory.select_slot(3U) && inventory.selected_ugc_item() == 18U,
               "UGC Builder must map Game Data ids 0..18 onto tool 41 variants");

        const std::vector<std::uint8_t> ugc_cycle_tools{16U, 17U, 18U};
        expect(inventory.spawn_with_selection(13U, ugc_prefab_loadout, {}, ugc_cycle_tools) &&
                   inventory.select_slot(3U) && inventory.selected_ugc_item() == 18U &&
                   inventory.cycle_selected_ugc_item_variant() &&
                   inventory.selected_ugc_item() == 16U,
               "UGC RMB variant cycling must wrap large back to small in the same group");

        expect(prefab_preview_asset("prefab_supertower") ==
                   "prefabs/prefab_supertower.png" &&
                   prefab_preview_asset("prefab_future_bridge") ==
                       "prefabs/prefab_future_bridge.png" &&
                   prefab_preview_asset("../settings").empty(),
               "prefab previews must follow dynamic safe ids and reject paths");

        const std::vector<std::uint8_t> ugc_selection{44U, 45U};
        expect(inventory.spawn_with_selection(12U, std::vector<std::uint8_t>{17U, 2U},
                                              {}, ugc_selection) &&
                   inventory.select_tool(45U) &&
                   inventory.weapons().replication().selected_tool() == 45U,
               "UGC toolbar selection must atomically select the same runtime tool");
        std::cout << "player inventory: ammo, blocks, class and all-tool modes passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "player inventory failure: " << error.what() << '\n';
        return 1;
    }
}
