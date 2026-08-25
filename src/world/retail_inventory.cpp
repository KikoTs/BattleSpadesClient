#include "battlespades/world/retail_inventory.hpp"

#include <algorithm>
#include <utility>

namespace battlespades::world {

void RetailInventory::set_slots(std::vector<InventorySlot> slots,
                                std::optional<std::size_t> selected) noexcept {
    const auto old_tool = selected_tool_id();
    slots_ = std::move(slots);
    selected_index_.reset();

    if (selected.has_value() && *selected < slots_.size() && slot_selectable(*selected)) {
        selected_index_ = selected;
    } else if (old_tool.has_value()) {
        const auto found = std::find_if(slots_.begin(), slots_.end(), [&](const auto& slot) {
            return slot.tool_id == *old_tool && slot.selectable;
        });
        if (found != slots_.end()) {
            selected_index_ = static_cast<std::size_t>(found - slots_.begin());
        }
    }
    if (!selected_index_.has_value()) {
        for (std::size_t index{}; index < slots_.size(); ++index) {
            if (slot_selectable(index)) {
                selected_index_ = index;
                break;
            }
        }
    }
}

bool RetailInventory::select_slot(std::size_t index, InventorySelectionOrigin origin,
                                  bool can_swap) noexcept {
    if (!can_swap || !slot_selectable(index)) {
        return false;
    }
    commit(index, origin);
    return true;
}

bool RetailInventory::cycle(int direction, bool can_swap) noexcept {
    if (!can_swap || direction == 0 || slots_.empty()) {
        return false;
    }
    const auto count = static_cast<std::ptrdiff_t>(slots_.size());
    std::ptrdiff_t index = selected_index_.has_value()
                               ? static_cast<std::ptrdiff_t>(*selected_index_)
                               : (direction > 0 ? -1 : 0);
    for (std::ptrdiff_t attempt{}; attempt < count; ++attempt) {
        index = (index + (direction > 0 ? 1 : -1) + count) % count;
        if (slot_selectable(static_cast<std::size_t>(index))) {
            commit(static_cast<std::size_t>(index),
                   InventorySelectionOrigin::mouse_wheel);
            return true;
        }
    }
    return false;
}

void RetailInventory::tick(double dt) noexcept {
    const auto elapsed = std::max(0.0, dt);
    pullout_remaining_ = std::max(0.0, pullout_remaining_ - elapsed);
    toolbar_remaining_ = std::max(0.0, toolbar_remaining_ - elapsed);
}

const std::vector<InventorySlot>& RetailInventory::slots() const noexcept {
    return slots_;
}

std::optional<std::size_t> RetailInventory::selected_index() const noexcept {
    return selected_index_;
}

std::optional<std::uint8_t> RetailInventory::selected_tool_id() const noexcept {
    if (!selected_index_.has_value() || *selected_index_ >= slots_.size()) {
        return std::nullopt;
    }
    return slots_[*selected_index_].tool_id;
}

double RetailInventory::pullout_remaining() const noexcept {
    return pullout_remaining_;
}

bool RetailInventory::toolbar_visible() const noexcept {
    return toolbar_remaining_ > 0.0;
}

double RetailInventory::toolbar_remaining() const noexcept {
    return toolbar_remaining_;
}

std::optional<InventorySelectionEvent> RetailInventory::take_selection_event() noexcept {
    auto result = event_;
    event_.reset();
    return result;
}

bool RetailInventory::slot_selectable(std::size_t index) const noexcept {
    if (index >= slots_.size()) {
        return false;
    }
    const auto& slot = slots_[index];
    return slot.selectable && (slot.has_ammo || slot.selectable_when_empty);
}

void RetailInventory::commit(std::size_t index, InventorySelectionOrigin origin) noexcept {
    const auto previous = selected_index_;
    const bool changed = previous != index;
    selected_index_ = index;

    // The protocol-visible tool changes immediately. Character.draw_fps uses
    // this independent timer to raise the new model from below.
    if (changed || origin == InventorySelectionOrigin::loadout_sync) {
        pullout_remaining_ = pullout_seconds;
    }
    const bool wheel = origin == InventorySelectionOrigin::mouse_wheel;
    const bool start_toolbar_animation = wheel && toolbar_remaining_ <= 0.0;
    // Retail GameScene only calls HUD.set_show_tool_loadout while the HUD's
    // scale timer is idle. Further wheel notches still change the tool, but
    // do not restart the one-second reveal or selected-item pulse.
    if (start_toolbar_animation) {
        toolbar_remaining_ = toolbar_seconds;
    }
    event_ = InventorySelectionEvent{
        origin,
        previous,
        selected_index_,
        changed,
        start_toolbar_animation,
        changed,
    };
}

} // namespace battlespades::world
