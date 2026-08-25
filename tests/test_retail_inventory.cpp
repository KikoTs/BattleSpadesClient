#include "battlespades/world/retail_inventory.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

using battlespades::world::InventorySelectionOrigin;
using battlespades::world::InventorySlot;
using battlespades::world::InventorySlotKind;
using battlespades::world::RetailInventory;

void expect(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error{message};
    }
}

} // namespace

int main() {
    try {
        RetailInventory inventory;
        inventory.set_slots({
                                InventorySlot{5U},
                                InventorySlot{2U},
                                InventorySlot{17U},
                                InventorySlot{23U, InventorySlotKind::prefab, 1U,
                                              true, false, true},
                                InventorySlot{40U, InventorySlotKind::ugc_tool, 0U,
                                              false},
                            },
                            1U);
        expect(inventory.selected_tool_id() == 2U, "the supplied retail slot must equip");

        expect(inventory.select_slot(2U, InventorySelectionOrigin::direct_slot),
               "a number key must address its slot directly");
        auto event = inventory.take_selection_event();
        expect(event.has_value() && event->changed && !event->animate_toolbar,
               "number selection must switch immediately without wheel HUD animation");
        expect(!inventory.toolbar_visible(), "number selection must not open the toolbar");
        expect(std::fabs(inventory.pullout_remaining() - 0.5) < 1e-9,
               "a changed tool must start Character's 0.5 second pullout");

        expect(inventory.cycle(1), "wheel down must advance");
        event = inventory.take_selection_event();
        expect(event.has_value() && event->animate_toolbar &&
                   inventory.selected_tool_id() == 23U,
               "wheel selection must include empty-but-selectable prefab entries");
        expect(inventory.toolbar_visible(),
               "wheel selection must open the retail toolbar");

        inventory.tick(0.1);
        const auto running_toolbar = inventory.toolbar_remaining();
        expect(inventory.cycle(1) && inventory.selected_tool_id() == 5U,
               "wheel selection must skip disabled entries and wrap");
        event = inventory.take_selection_event();
        expect(std::fabs(inventory.toolbar_remaining() - running_toolbar) < 1e-9,
               "wheel input must not restart a running retail HUD scale timer");
        expect(event.has_value() && !event->animate_toolbar,
               "a running retail scale timer must suppress another animation request");
        expect(inventory.cycle(-1) && inventory.selected_tool_id() == 23U,
               "wheel-up must traverse in the reverse direction");
        expect(!inventory.select_slot(4U, InventorySelectionOrigin::direct_slot),
               "disabled number slots must be rejected");
        expect(!inventory.cycle(1, false), "a non-swappable weapon must consume no wheel step");

        inventory.tick(0.1);
        expect(inventory.toolbar_visible(),
               "the selected authored scale must remain visible until toolbar timeout");
        inventory.tick(0.81);
        expect(!inventory.toolbar_visible(), "the wheel toolbar must close after one second");

        std::cout << "retail inventory: slot, wheel, hotkey and animation checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
