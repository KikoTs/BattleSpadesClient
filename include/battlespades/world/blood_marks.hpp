#pragma once

#include "battlespades/world/chunk_mesh.hpp"
#include "battlespades/world/voxel_raycast.hpp"

namespace battlespades::world {

/** Bounded client-only droplets and surface flecks. Never writes to the map. */
class BloodMarks final {
public:
    static constexpr std::size_t maximum_drops = 192, maximum_marks = 768;
    BloodMarks();
    void emit(std::array<float, 3> position, std::uint32_t seed);
    void tick(double dt, const VxlMap& map, double gravity = 1);
    void clear();
    [[nodiscard]] ChunkMesh mesh() const;
    [[nodiscard]] std::uint64_t revision() const {
        return revision_;
    }
    [[nodiscard]] std::size_t drop_count() const {
        return drops_.size();
    }
    [[nodiscard]] std::size_t mark_count() const {
        return marks_.size();
    }

private:
    struct Drop {
        std::array<float, 3> position, velocity;
        float age{}, size{};
        std::uint32_t seed{};
    };
    struct Mark {
        VoxelCell cell;
        std::array<std::int32_t, 3> normal;
        std::array<float, 3> minimum, maximum;
        float age{}, lifetime{};
        std::uint32_t color{};
    };
    void splatter(const VoxelRayHit& hit, const VxlMap& map, std::uint32_t seed);
    std::vector<Drop> drops_;
    std::vector<Mark> marks_;
    std::size_t next_drop_{}, next_mark_{};
    std::uint64_t revision_{};
    std::uint32_t emission_{};
};
} // namespace battlespades::world
