#pragma once

#include "battlespades/world/vxl_map.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace battlespades::world {

/** One signed world-space cell from a rotated prefab footprint. */
struct PrefabPlacementCell final {
    std::int32_t x{};
    std::int32_t y{};
    std::int32_t z{};
};

/**
 * Retail placement facts for one already-transformed prefab footprint.
 *
 * Existing solid cells are legal overlap and cost no blocks. The placement is
 * attached when either an overlapping cell or a face-neighbour belongs to the
 * live VXL. A footprint outside the competitive build volume fails atomically.
 */
struct PrefabPlacementEvaluation final {
    bool all_in_bounds{true};
    bool touches_world{};
    std::size_t required_blocks{};

    [[nodiscard]] bool has_effect() const noexcept { return required_blocks != 0U; }
};

[[nodiscard]] PrefabPlacementEvaluation evaluate_prefab_placement(
    const VxlMap& map, std::span<const PrefabPlacementCell> footprint) noexcept;

/** One coloured cell recovered from a packet-30 prefab slice. */
struct PrefabPlacementVoxel final {
    PrefabPlacementCell cell{};
    VxlColor color{};
};

struct PrefabCommitResult final {
    bool committed{};
    std::size_t placed{};
    std::size_t overlapped{};
};

/** Authoritative construction edge whose recovered retail sound is required. */
enum class PlacementFeedbackKind : std::uint8_t {
    block,
    prefab,
    ugc_prefab,
};

/**
 * Exact extension-free audio stem used by retail for one placement edge.
 *
 * Call this only after the server-confirmed VXL mutation commits. Preview and
 * rejected placement must stay silent, and a multi-slice prefab produces one
 * `prefabbuild` edge at PrefabComplete rather than one sound per packet.
 */
[[nodiscard]] std::string_view placement_sound_stem(
    PlacementFeedbackKind kind) noexcept;

/**
 * Bounded owner for packet-30 prefab slices until PrefabComplete(29).
 *
 * Retail may send a large construct in multiple [from,to) slices, but it does
 * not reveal those partial slices as a structure growing over several frames.
 * Network code stages each decoded slice here, then calls commit() exactly on
 * packet 29.  Coordinates are validated before any VXL write, so malformed or
 * clipped input can never leave a half-built collision body behind.
 */
class PrefabPlacementTransaction final {
public:
    static constexpr std::size_t maximum_voxels{8'192U};

    [[nodiscard]] bool stage(std::span<const PrefabPlacementVoxel> voxels);
    [[nodiscard]] PrefabCommitResult commit(VxlMap& map) noexcept;
    void clear() noexcept;

    [[nodiscard]] bool empty() const noexcept { return staged_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return staged_.size(); }
    /** Read-only staged footprint for TerrainReplica/VFX integration. */
    [[nodiscard]] std::span<const PrefabPlacementVoxel> voxels() const noexcept {
        return staged_;
    }

private:
    std::vector<PrefabPlacementVoxel> staged_;
};

} // namespace battlespades::world
