#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace battlespades::world {

/** Why a tool selection occurred; retail treats wheel and hotkeys differently. */
enum class InventorySelectionOrigin : std::uint8_t {
    direct_slot,
    mouse_wheel,
    loadout_sync,
    automatic,
};

/** The three address spaces combined by GameScene's retail HUD index. */
enum class InventorySlotKind : std::uint8_t {
    loadout,
    prefab,
    ugc_tool,
};

/** One selectable retail HUD entry. Tool ids remain the protocol's byte ids. */
struct InventorySlot final {
    std::uint8_t tool_id{};
    InventorySlotKind kind{InventorySlotKind::loadout};
    std::uint16_t variant_id{};
    bool selectable{true};
    bool has_ammo{true};
    bool selectable_when_empty{};
};

/** Edge event consumed by presentation and, later, protocol replication. */
struct InventorySelectionEvent final {
    InventorySelectionOrigin origin{InventorySelectionOrigin::loadout_sync};
    std::optional<std::size_t> previous_index{};
    std::optional<std::size_t> selected_index{};
    bool changed{};
    bool animate_toolbar{};
    bool play_switch_sound{};
};

/**
 * Renderer-independent reconstruction of GameScene's inventory selection.
 *
 * Slots are ordered exactly as the retail combined HUD index: class loadout,
 * prefab variants, then UGC tools. Mouse-wheel selection wraps and skips
 * unavailable entries and opens the one-second toolbar with the selected
 * frame at its authored scale. Number keys address slots directly and do not start that HUD
 * animation. Both paths change the held tool immediately; the Character
 * pullout animation is presentation state, not a delayed gameplay commit.
 */
class RetailInventory final {
public:
    static constexpr double pullout_seconds{0.5};
    static constexpr double toolbar_seconds{1.0};

    void set_slots(std::vector<InventorySlot> slots,
                   std::optional<std::size_t> selected = std::nullopt) noexcept;
    /** Refresh availability without rebuilding slots or restarting selection animation. */
    void set_slot_ammunition(std::size_t index, bool has_ammo) noexcept;
    [[nodiscard]] bool select_slot(std::size_t index, InventorySelectionOrigin origin,
                                   bool can_swap = true) noexcept;
    [[nodiscard]] bool cycle(int direction, bool can_swap = true) noexcept;
    void tick(double dt) noexcept;

    [[nodiscard]] const std::vector<InventorySlot>& slots() const noexcept;
    [[nodiscard]] std::optional<std::size_t> selected_index() const noexcept;
    [[nodiscard]] std::optional<std::uint8_t> selected_tool_id() const noexcept;
    [[nodiscard]] double pullout_remaining() const noexcept;
    [[nodiscard]] bool toolbar_visible() const noexcept;
    [[nodiscard]] double toolbar_remaining() const noexcept;
    [[nodiscard]] std::optional<InventorySelectionEvent> take_selection_event() noexcept;

private:
    [[nodiscard]] bool slot_selectable(std::size_t index) const noexcept;
    void commit(std::size_t index, InventorySelectionOrigin origin) noexcept;

    std::vector<InventorySlot> slots_;
    std::optional<std::size_t> selected_index_{};
    std::optional<InventorySelectionEvent> event_{};
    double pullout_remaining_{};
    double toolbar_remaining_{};
};

} // namespace battlespades::world
