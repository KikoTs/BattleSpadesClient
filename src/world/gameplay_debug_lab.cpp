#include "battlespades/world/gameplay_debug_lab.hpp"

#include "battlespades/world/class_catalog.hpp"
#include "battlespades/world/particle_effects.hpp"
#include "battlespades/world/terrain_effects.hpp"
#include "battlespades/world/weapon_catalog.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

namespace battlespades::world {
namespace {
[[nodiscard]] std::size_t wrapped(std::size_t value, int direction,
                                  std::size_t count) noexcept {
    if (count == 0U || direction == 0) return value;
    const auto signed_count = static_cast<std::ptrdiff_t>(count);
    const auto next = (static_cast<std::ptrdiff_t>(value) +
                       (direction > 0 ? 1 : -1) + signed_count) % signed_count;
    return static_cast<std::size_t>(next);
}
} // namespace

GameplayDebugLab::GameplayDebugLab() {
    static_cast<void>(inventory_.spawn_as(0U, PlayerLoadoutScope::all_weapons));
    select_current_tool();
    spawn_selected_class();
}

void GameplayDebugLab::set_grave_model(Kv6Model model) {
    grave_model_ = std::move(model);
}

void GameplayDebugLab::cycle_class(int direction) {
    class_index_ = wrapped(class_index_, direction, class_catalog().size());
    static_cast<void>(inventory_.spawn_as(selected_class_id(),
                                          PlayerLoadoutScope::all_weapons));
    select_current_tool();
    spawned_classes_.assign(1U, selected_class_id());
    inspection_mode_ = DebugInspectionMode::character;
    status_ = "CLASS CHANGED";
}

void GameplayDebugLab::cycle_tool(int direction) {
    tool_index_ = wrapped(tool_index_, direction, weapon_catalog().size());
    select_current_tool();
    status_ = "TOOL EQUIPPED";
}

bool GameplayDebugLab::select_class_and_tool(std::uint8_t class_id,
                                             std::uint8_t tool_id) {
    const auto class_it = std::ranges::find_if(
        class_catalog(), [class_id](const ClassDefinition& definition) {
            return definition.class_id == class_id;
        });
    const auto tool_it = std::ranges::find_if(
        weapon_catalog(), [tool_id](const WeaponDefinition& definition) {
            return definition.tool_id == tool_id;
        });
    if (class_it == class_catalog().end() || tool_it == weapon_catalog().end()) {
        return false;
    }
    class_index_ = static_cast<std::size_t>(class_it - class_catalog().begin());
    tool_index_ = static_cast<std::size_t>(tool_it - weapon_catalog().begin());
    static_cast<void>(inventory_.spawn_as(class_id, PlayerLoadoutScope::all_weapons));
    select_current_tool();
    spawned_classes_.assign(1U, class_id);
    inspection_mode_ = DebugInspectionMode::character;
    status_ = "ZOMBIE HAND PARITY PRESET";
    return true;
}

void GameplayDebugLab::cycle_skin(int direction) noexcept {
    skin_index_ = wrapped(skin_index_, direction, ui_skin_catalog().size());
    status_ = "UI SKIN CHANGED";
}

void GameplayDebugLab::toggle_team() noexcept {
    team_index_ = team_index_ == 0U ? 1U : 0U;
    team_color_ = team_index_ == 0U ? VxlColor{44U, 117U, 179U, 255U}
                                    : VxlColor{137U, 179U, 44U, 255U};
    status_ = "TEAM COLOR CHANGED";
}

void GameplayDebugLab::cycle_inspection_mode(int direction) {
    auto mode = static_cast<std::size_t>(inspection_mode_);
    mode = wrapped(mode, direction, 4U);
    inspection_mode_ = static_cast<DebugInspectionMode>(mode);
    if (inspection_mode_ == DebugInspectionMode::character) {
        spawned_classes_.assign(1U, selected_class_id());
    } else if (inspection_mode_ == DebugInspectionMode::gallery) {
        spawn_all_classes();
    } else if (inspection_mode_ == DebugInspectionMode::effects) {
        spawned_classes_.clear();
        vfx_particles_.clear();
        trigger_vfx();
    }
    status_ = "INSPECTION MODE CHANGED";
}

void GameplayDebugLab::rotate_preview(float degrees) noexcept {
    preview_yaw_ = std::fmod(preview_yaw_ + degrees, 360.0F);
    status_ = "CHARACTER ROTATED";
}

void GameplayDebugLab::zoom_preview(float delta) noexcept {
    preview_zoom_ = std::clamp(preview_zoom_ + delta, 0.55F, 2.25F);
    status_ = "PREVIEW ZOOM CHANGED";
}

void GameplayDebugLab::cycle_color_channel(int direction) noexcept {
    color_channel_ = static_cast<std::uint8_t>(wrapped(color_channel_, direction, 3U));
    status_ = "RGB CHANNEL SELECTED";
}

void GameplayDebugLab::adjust_team_color(int delta) noexcept {
    auto* channel = color_channel_ == 0U ? &team_color_.red
                  : color_channel_ == 1U ? &team_color_.green
                                         : &team_color_.blue;
    *channel = static_cast<std::uint8_t>(std::clamp(
        static_cast<int>(*channel) + delta, 0, 255));
    // 2 denotes an explicit server-defined/custom team color in the lab.
    team_index_ = 2U;
    status_ = "CUSTOM TEAM RGB CHANGED";
}

void GameplayDebugLab::set_primary(bool held) noexcept {
    inventory_.weapons().set_primary(held);
}

void GameplayDebugLab::set_secondary(bool held) noexcept {
    inventory_.weapons().set_secondary(held);
}

void GameplayDebugLab::reload() noexcept {
    const auto result = inventory_.weapons().request_reload();
    status_ = result == WeaponStateResult::accepted ? "RELOADING" : "RELOAD REJECTED";
}

void GameplayDebugLab::restock_ammunition() noexcept {
    inventory_.restock_ammunition();
    status_ = "AMMUNITION RESTOCKED";
}

void GameplayDebugLab::restock_blocks() noexcept {
    inventory_.restock_blocks();
    status_ = "BLOCKS RESTOCKED";
}

void GameplayDebugLab::spend_block() noexcept {
    status_ = inventory_.spend_blocks() ? "BLOCK SPENT" : "NO BLOCKS";
}

void GameplayDebugLab::spawn_selected_class() {
    if (std::ranges::find(spawned_classes_, selected_class_id()) ==
        spawned_classes_.end()) {
        spawned_classes_.push_back(selected_class_id());
    }
    inspection_mode_ = DebugInspectionMode::character;
    status_ = "CLASS MODEL SPAWNED";
}

void GameplayDebugLab::spawn_all_classes() {
    spawned_classes_.clear();
    for (const auto& definition : class_catalog()) {
        spawned_classes_.push_back(definition.class_id);
    }
    inspection_mode_ = DebugInspectionMode::gallery;
    status_ = "ALL CLASS MODELS SPAWNED";
}

void GameplayDebugLab::clear_spawned() noexcept {
    spawned_classes_.clear();
    status_ = "MODELS CLEARED";
}

void GameplayDebugLab::cycle_vfx(int direction) {
    auto index = static_cast<std::size_t>(vfx_kind_);
    index = wrapped(index, direction, 4U);
    vfx_kind_ = static_cast<DebugVfxKind>(index);
    trigger_vfx();
}

void GameplayDebugLab::select_vfx(DebugVfxKind kind, bool trigger) {
    vfx_kind_ = kind;
    inspection_mode_ = DebugInspectionMode::effects;
    spawned_classes_.clear();
    vfx_particles_.clear();
    status_ = "VFX ORACLE READY";
    if (trigger) {
        trigger_vfx();
    }
}

void GameplayDebugLab::set_vfx_origin(std::array<float, 3U> origin) noexcept {
    vfx_origin_ = origin;
}

void GameplayDebugLab::trigger_vfx() {
    vfx_particles_.clear();
    TerrainImpactEvent impact;
    impact.cell = {0U, 0U, 0U};
    // The frontend refreshes this from the live camera when a session exists;
    // the offline default is exactly five blocks along the fixed lab view.
    // Keeping that distance makes sprite size and dispersion comparable.
    impact.position = vfx_origin_;
    // Training sampled empty/black terrain at the capture point. This affects
    // only the ten map-colour debris particles in the offline parity fixture;
    // live effects continue to use the server/world impact colour.
    impact.color = VxlColor{0U, 0U, 0U, 255U};
    impact.normal = {0, 0, -1};
    impact.destroyed = true;
    ++vfx_sequence_;
    switch (vfx_kind_) {
    case DebugVfxKind::player_death:
        impact.kind = TerrainImpactKind::corpse_explosion;
        emit_corpse_explosion(vfx_particles_, impact);
        status_ = "PLAYER DEATH REPLAYED";
        break;
    case DebugVfxKind::grave:
        impact.kind = TerrainImpactKind::grave_explosion;
        impact.color = team_color_;
        emit_grave_explosion(vfx_particles_, impact,
                             grave_model_ ? &*grave_model_ : nullptr);
        status_ = "GRAVE DISPLAY REPLAYED";
        break;
    case DebugVfxKind::grenade:
        impact.kind = TerrainImpactKind::explosion;
        impact.radius = 3.0F;
        impact.source_tool = 11U; // GRENADE
        emit_explosion(vfx_particles_, impact);
        status_ = "GRENADE IMPACT REPLAYED";
        break;
    case DebugVfxKind::rocket:
        impact.kind = TerrainImpactKind::explosion;
        impact.radius = 4.0F;
        impact.source_tool = 12U;
        impact.source_velocity = {75.0F, 0.0F, 0.0F};
        emit_explosion(vfx_particles_, impact);
        status_ = "ROCKET IMPACT REPLAYED";
        break;
    }
}

void GameplayDebugLab::tick(double dt, const VxlMap* collision_map) {
    if (collision_map != nullptr) {
        vfx_particles_.tick(dt, *collision_map);
    } else {
        vfx_particles_.tick_unbounded(dt);
    }
    inventory_.tick(dt);
    auto actions = inventory_.weapons().take_actions();
    emitted_action_count_ += actions.size();
    if (!actions.empty()) {
        seconds_since_action_ = 0.0;
        status_ = actions.back().kind == WeaponActionKind::dry_fire
                      ? "DRY FIRE"
                      : "ACTION EMITTED";
    } else {
        seconds_since_action_ += std::max(0.0, dt);
    }
    pending_actions_.insert(pending_actions_.end(),
                            std::make_move_iterator(actions.begin()),
                            std::make_move_iterator(actions.end()));
}

std::vector<WeaponAction> GameplayDebugLab::take_actions() {
    return std::exchange(pending_actions_, {});
}

std::uint8_t GameplayDebugLab::selected_class_id() const noexcept {
    return class_catalog()[class_index_].class_id;
}

std::uint8_t GameplayDebugLab::selected_tool_id() const noexcept {
    return weapon_catalog()[tool_index_].tool_id;
}

std::string_view GameplayDebugLab::skin_id() const noexcept {
    return ui_skin_catalog()[skin_index_].id;
}

std::span<const std::uint8_t> GameplayDebugLab::spawned_classes() const noexcept {
    return spawned_classes_;
}

void GameplayDebugLab::select_current_tool() {
    static_cast<void>(inventory_.select_tool(selected_tool_id(),
                                             InventorySelectionOrigin::direct_slot));
}

} // namespace battlespades::world
