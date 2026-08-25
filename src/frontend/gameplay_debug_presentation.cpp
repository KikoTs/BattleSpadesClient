#include "battlespades/frontend/gameplay_debug_presentation.hpp"

#include "battlespades/frontend/custom_match_presentation.hpp"
#include "battlespades/frontend/game_hud.hpp"
#include "battlespades/world/class_catalog.hpp"
#include "battlespades/world/weapon_catalog.hpp"

#include <string>

namespace battlespades::frontend {
namespace {

[[nodiscard]] ui::SpriteDrawCommand solid(ui::DrawRect bounds, ui::ColorRgba8 color,
                                          std::uint16_t opacity) {
    return {std::string{game_hud_assets::white_pixel}, bounds, ui::DrawSpace::design_pixels,
            ui::TextureFilter::nearest, ui::TextureAnchor::top_left, 1.0,
            ui::SpriteSizing::stretch, {color, 1'000U, opacity}};
}

[[nodiscard]] ui::TextDrawCommand text(std::string value, ui::DrawRect bounds,
                                       double size, ui::ColorRgba8 color =
                                           {244U, 236U, 187U, 255U}) {
    return {std::move(value), std::string{custom_match_assets::row_font}, bounds,
            ui::DrawSpace::design_pixels, size, 1.0, 1U,
            ui::HorizontalTextAlignment::left, ui::VerticalTextAlignment::center,
            ui::TextTransform::preserve, ui::TextFit::shrink_to_fit,
            {color, 1'000U, 1'000U}};
}

[[nodiscard]] std::string_view mode_name(world::DebugInspectionMode mode) noexcept {
    switch (mode) {
    case world::DebugInspectionMode::character: return "CHARACTER + HELD TOOL";
    case world::DebugInspectionMode::first_person: return "FIRST PERSON";
    case world::DebugInspectionMode::gallery: return "CLASS GALLERY";
    case world::DebugInspectionMode::effects: return "VFX PARITY";
    }
    return "?";
}

} // namespace

ui::DrawList GameplayDebugPresentation::build(const world::GameplayDebugLab& lab,
                                               ui::PixelExtent window) const {
    ui::DrawList list;
    if (!window.is_valid()) return list;
    const auto* klass = world::find_class_definition(lab.selected_class_id());
    const auto* weapon = world::find_weapon_definition(lab.selected_tool_id());
    const auto* ammo = lab.inventory().ammo(lab.selected_tool_id());
    list.reserve(28U);
    list.push(ui::SpriteDrawCommand{
        "png/ui/common_elements/frames/ui_frame_large.png",
        {10.0, 8.0, 382.0, 310.0}, ui::DrawSpace::design_pixels,
        ui::TextureFilter::linear, ui::TextureAnchor::center, 0.6,
        ui::SpriteSizing::stretch, {}});
    list.push(solid({16.0, 15.0, 370.0, 296.0}, {8U, 10U, 14U, 255U}, 820U));
    list.push(solid({18.0, 17.0, 366.0, 30.0}, {40U, 74U, 94U, 255U}, 930U));
    list.push(text("GAMEPLAY PARITY LAB  [F10 TO CLOSE]", {28.0, 18.0, 344.0, 28.0},
                   15.0, {232U, 207U, 78U, 255U}));
    list.push(text("CLASS  UP/DOWN: " + std::string{klass != nullptr ? klass->display_name : "?"},
                   {28.0, 52.0, 318.0, 22.0}, 13.0));
    const auto color = lab.team_color();
    const std::string team_name = lab.team_index() == 0U ? "BLUE"
                                  : lab.team_index() == 1U ? "GREEN"
                                                           : "CUSTOM";
    list.push(text("TEAM [T]: " + team_name + " RGB " +
                       std::to_string(color.red) + "," + std::to_string(color.green) +
                       "," + std::to_string(color.blue) + "   SKIN [K]: " +
                       std::string{lab.skin_id()},
                   {28.0, 75.0, 318.0, 22.0}, 12.0));
    list.push(solid({356.0, 78.0, 18.0, 16.0},
                    {color.red, color.green, color.blue, 255U}, 1'000U));
    const bool effects = lab.inspection_mode() == world::DebugInspectionMode::effects;
    std::string tool_label = weapon != nullptr
                                 ? std::to_string(weapon->tool_id) + " " +
                                       std::string{weapon->symbolic_name}
                                 : std::string{"?"};
    if (weapon != nullptr && !weapon->third_person_models.empty()) {
        const auto offset = weapon->third_person_models.front().authored_offset;
        tool_label += "  TP(" + std::to_string(static_cast<int>(offset[0U])) + "," +
                      std::to_string(static_cast<int>(offset[1U])) + "," +
                      std::to_string(static_cast<int>(offset[2U])) + ")";
    }
    const auto vfx_name = [&]() -> std::string_view {
        switch (lab.vfx_kind()) {
        case world::DebugVfxKind::player_death: return "PLAYER DEATH";
        case world::DebugVfxKind::grave: return "TOMBSTONE";
        case world::DebugVfxKind::grenade: return "GRENADE";
        case world::DebugVfxKind::rocket: return "ROCKET";
        }
        return "?";
    }();
    list.push(text(effects ? "VFX  LEFT/RIGHT: " + std::string{vfx_name}
                           : "TOOL  LEFT/RIGHT: " + tool_label,
                   {28.0, 100.0, 318.0, 22.0}, 12.0));
    const auto ammo_text = ammo == nullptr ? std::string{"INFINITE"}
                                           : std::to_string(ammo->magazine) + " / " +
                                                 std::to_string(ammo->reserve);
    list.push(text("AMMO: " + ammo_text + "    BLOCKS: " +
                       std::to_string(lab.inventory().blocks()) + " / " +
                       std::to_string(lab.inventory().maximum_blocks()),
                   {28.0, 124.0, 318.0, 22.0}, 12.0));
    list.push(text("VIEW [TAB]: " + std::string{mode_name(lab.inspection_mode())} +
                       "   Q/E ROTATE   -/= ZOOM",
                   {28.0, 149.0, 344.0, 20.0}, 9.0));
    const char* channel = lab.color_channel() == 0U ? "R" : lab.color_channel() == 1U ? "G" : "B";
    list.push(text("COLOR [C] CHANNEL " + std::string{channel} + "   [ / ] ADJUST RGB",
                   {28.0, 169.0, 344.0, 20.0}, 9.0));
    list.push(text(effects ? "ENTER/SPACE REPLAY EFFECT"
                           : "ENTER/LMB FIRE   RMB SECONDARY   R RELOAD",
                   {28.0, 190.0, 344.0, 20.0}, 10.0));
    list.push(text("A AMMO   B SPEND BLOCK   N REFILL BLOCKS", {28.0, 211.0, 344.0, 20.0}, 10.0));
    list.push(text("M CHARACTER   SHIFT+M GALLERY   Z ZOMBIE HANDS", {28.0, 232.0, 344.0, 20.0}, 9.0));
    list.push(text("MODELS: " + std::to_string(lab.spawned_classes().size()) +
                       "   ACTIONS: " + std::to_string(lab.emitted_action_count()),
                   {28.0, 257.0, 344.0, 18.0}, 10.0,
                   {118U, 222U, 145U, 255U}));
    list.push(text(std::string{lab.status()}, {28.0, 280.0, 344.0, 18.0}, 10.0,
                   {232U, 207U, 78U, 255U}));
    return list;
}

} // namespace battlespades::frontend
