#pragma once

#include "battlespades/world/kv6_model.hpp"
#include "battlespades/world/particle_system.hpp"
#include "battlespades/world/player_inventory.hpp"
#include "battlespades/world/vxl_map.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace battlespades::world {

enum class DebugInspectionMode : std::uint8_t {
    character,
    first_person,
    gallery,
    effects,
};

/** Source-separated effects available in the offline F10 parity laboratory. */
enum class DebugVfxKind : std::uint8_t {
    player_death,
    grave,
    grenade,
    rocket,
};

/**
 * Renderer-neutral developer laboratory for class, skin and weapon parity.
 * It uses the same WeaponRuntime and PlayerInventory as a real session; the
 * frontend merely chooses inputs and displays the resulting state/actions.
 */
class GameplayDebugLab final {
public:
    GameplayDebugLab();

    /** Installs the same pivoted grave model used by live entity rendering. */
    void set_grave_model(Kv6Model model);

    void cycle_class(int direction);
    void cycle_tool(int direction);
    /** Jump directly to a catalog pair for repeatable visual parity captures. */
    bool select_class_and_tool(std::uint8_t class_id, std::uint8_t tool_id);
    void cycle_skin(int direction) noexcept;
    void toggle_team() noexcept;
    void cycle_inspection_mode(int direction = 1);
    void rotate_preview(float degrees) noexcept;
    void zoom_preview(float delta) noexcept;
    void cycle_color_channel(int direction = 1) noexcept;
    void adjust_team_color(int delta) noexcept;
    void set_primary(bool held) noexcept;
    void set_secondary(bool held) noexcept;
    void reload() noexcept;
    void restock_ammunition() noexcept;
    void restock_blocks() noexcept;
    void spend_block() noexcept;
    void spawn_selected_class();
    void spawn_all_classes();
    void clear_spawned() noexcept;
    void cycle_vfx(int direction);
    /** Selects one exact production emitter and enters the effects view. */
    void select_vfx(DebugVfxKind kind, bool trigger = true);
    /** Sets the live-session point used by subsequent effect replays. */
    void set_vfx_origin(std::array<float, 3U> origin) noexcept;
    void trigger_vfx();
    /**
     * Advances the offline fixture. When opened over a live Tutorial/session
     * world, particles collide with that exact VXL just as retail does; the
     * null form remains available to lightweight menu/unit-test callers.
     */
    void tick(double dt, const VxlMap* collision_map = nullptr);

    [[nodiscard]] std::uint8_t selected_class_id() const noexcept;
    [[nodiscard]] std::uint8_t selected_tool_id() const noexcept;
    [[nodiscard]] std::uint8_t team_index() const noexcept { return team_index_; }
    [[nodiscard]] VxlColor team_color() const noexcept { return team_color_; }
    [[nodiscard]] DebugInspectionMode inspection_mode() const noexcept {
        return inspection_mode_;
    }
    [[nodiscard]] std::uint8_t color_channel() const noexcept { return color_channel_; }
    [[nodiscard]] float preview_yaw() const noexcept { return preview_yaw_; }
    [[nodiscard]] float preview_zoom() const noexcept { return preview_zoom_; }
    [[nodiscard]] std::string_view skin_id() const noexcept;
    [[nodiscard]] const PlayerInventory& inventory() const noexcept { return inventory_; }
    [[nodiscard]] PlayerInventory& inventory() noexcept { return inventory_; }
    [[nodiscard]] std::span<const std::uint8_t> spawned_classes() const noexcept;
    [[nodiscard]] std::string_view status() const noexcept { return status_; }
    [[nodiscard]] std::uint64_t emitted_action_count() const noexcept {
        return emitted_action_count_;
    }
    [[nodiscard]] double seconds_since_action() const noexcept {
        return seconds_since_action_;
    }
    /** Semantic actions since the last call, for audiovisual parity playback. */
    [[nodiscard]] std::vector<WeaponAction> take_actions();
    [[nodiscard]] DebugVfxKind vfx_kind() const noexcept { return vfx_kind_; }
    [[nodiscard]] ParticleSystem& vfx_particles() noexcept { return vfx_particles_; }
    [[nodiscard]] const ParticleSystem& vfx_particles() const noexcept {
        return vfx_particles_;
    }

private:
    void select_current_tool();

    PlayerInventory inventory_;
    std::size_t class_index_{};
    std::size_t tool_index_{};
    std::size_t skin_index_{};
    std::uint8_t team_index_{};
    VxlColor team_color_{44U, 117U, 179U, 255U};
    DebugInspectionMode inspection_mode_{DebugInspectionMode::character};
    std::uint8_t color_channel_{};
    float preview_yaw_{-90.0F};
    float preview_zoom_{1.2F};
    std::vector<std::uint8_t> spawned_classes_;
    std::string status_{"READY"};
    std::uint64_t emitted_action_count_{};
    double seconds_since_action_{1.0e9};
    std::vector<WeaponAction> pending_actions_;
    DebugVfxKind vfx_kind_{DebugVfxKind::player_death};
    std::array<float, 3U> vfx_origin_{-0.7878F, 0.0F, 1.1288F};
    ParticleSystem vfx_particles_;
    std::optional<Kv6Model> grave_model_;
    std::uint32_t vfx_sequence_{};
};

} // namespace battlespades::world
