#pragma once

#include "battlespades/frontend/inventory_menu.hpp"
#include "battlespades/frontend/workshop_session.hpp"
#include "battlespades/render/bgfx_ui_renderer.hpp"
#include "battlespades/ui/input.hpp"
#include "battlespades/world/weapon_variants.hpp"
#include <filesystem>
#include <memory>

namespace battlespades::frontend {
/** Native markup screen. Receipts and mutations remain owned by InventorySession. */
class InventoryView final {
public:
    InventoryView(render::BgfxUiRenderer& renderer, std::filesystem::path asset_root,
                  world::SkinVariantPreferences* variants = nullptr);
    ~InventoryView();
    InventoryView(const InventoryView&) = delete;
    InventoryView& operator=(const InventoryView&) = delete;
    void prepare(InventoryMenuModel& model, bool fixture = false, float pixel_scale = 1.0F);
    /** Workshop shares the inventory's RmlUi context and renderer. */
    void prepare_workshop(WorkshopMenuModel& model, float pixel_scale = 1.0F);
    [[nodiscard]] WorkshopAction take_workshop_action();
    void text_input(std::string_view text);
    void select_search_text();
    void edit_key(bool backspace);
    [[nodiscard]] bool draw();
    /** Coordinates are 800x600 design pixels, after retail subpixel conversion. */
    void pointer_move(ui::Point point);
    void pointer_button(bool down);
    void cancel_pointer_capture();
    void wheel(int steps);
    void input(ui::InputEvent event);
    [[nodiscard]] InventoryAction take_action();
    [[nodiscard]] int take_navigation(); // -1 none, 0 back, 1..4 profile tabs.
    [[nodiscard]] int take_sound(); // 1 reel tick, 2 award, 3 button.
    [[nodiscard]] std::string_view error() const;
    [[nodiscard]] bool ready() const;
    /** Queued thumbnails and the selected model have reached the renderer. */
    [[nodiscard]] bool previews_ready() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace battlespades::frontend
