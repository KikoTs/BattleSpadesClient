#pragma once
#include "battlespades/world/player_movement.hpp"
#include "battlespades/world/terrain_effects.hpp"
#include "battlespades/world/voxel_raycast.hpp"
#include "battlespades/world/weapon_runtime.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <unordered_map>

namespace battlespades::world {
struct ClassicHitTarget {
    std::uint8_t id{};
    Vec3 position{}, orientation{1, 0, 0};
    bool crouched{}, sprinting{};
};
struct ClassicHit {
    std::uint8_t player{}, part{};
    Vec3 position{};
};
struct ClassicBlockDamage {
    VoxelCell cell;
    int remaining{100};
    VxlColor original;
};
struct ClassicAttackResult {
    struct Tracer {
        Vec3 direction, endpoint;
    };
    std::vector<ClassicHit> hits;
    std::optional<VoxelCell> destroy;
    std::uint8_t block_action{1};
    std::vector<ClassicBlockDamage> damage;
    std::vector<TerrainImpactEvent> impacts;
    std::vector<Tracer> tracers;
};
/** Gameplay boxes always have original AoS dimensions, independent of cosmetic meshes. */
class ClassicCombat final {
public:
    ClassicCombat();
    /** Reproducible stream for reference tests; production seeds once from entropy. */
    explicit ClassicCombat(std::array<std::uint64_t, 2> seed) noexcept;
    [[nodiscard]] ClassicAttackResult attack(const VxlMap& map,
                                             const PlayerMovementState& player,
                                             std::span<const ClassicHitTarget> targets,
                                             const WeaponAction& action,
                                             std::uint8_t protocol,
                                             bool aiming,
                                             double seconds);
    [[nodiscard]] std::vector<ClassicBlockDamage> expire(double seconds);
    void reset() {
        damage_.clear();
    }
    void forget(VoxelCell cell) {
        damage_.erase(cell.x | (cell.y << 9U) | (cell.z << 18U));
    }

private:
    // ZeroSpades Core/Math.cpp LocalRNG (xorshift128+), GPL-3.0-or-later.
    // A continuing stream, independent of retail's byte-sized visual/wire seed.
    struct SpreadRandom {
        using result_type = std::uint64_t;
        std::array<result_type, 2> state;
        static constexpr result_type min() noexcept { return 0; }
        static constexpr result_type max() noexcept { return UINT64_MAX; }
        result_type operator()() noexcept;
    } random_;
    struct Damage {
        int remaining{100};
        double expires{};
        VxlColor original;
    };
    std::unordered_map<std::uint32_t, Damage> damage_;
};
[[nodiscard]] std::optional<VoxelCell> classic_build_target(
    const VxlMap&, const PlayerMovementState&, std::span<const ClassicHitTarget> targets);
} // namespace battlespades::world
