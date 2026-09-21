#pragma once

#include "battlespades/world/vxl_map.hpp"
#include "battlespades/world/voxel_collapse.hpp"

#include <cstddef>
#include <cstdint>
#include <array>
#include <optional>
#include <span>
#include <string_view>
#include <vector>
#include <unordered_map>

namespace battlespades::world {

/** Missing cells and ordered support checks for an ordinary block drag. */
struct BlockLinePlacementEvaluation final {
    std::size_t required_blocks{};
    bool all_in_bounds{true};
    bool all_supported{true};

    [[nodiscard]] bool can_place(std::int32_t stock, bool infinite = false) const noexcept {
        return all_in_bounds && all_supported && required_blocks != 0U &&
               (infinite || (stock >= 0 && required_blocks <= static_cast<std::size_t>(stock)));
    }
};

/** Existing solid cells cost nothing; each new cell must touch earlier support. */
[[nodiscard]] BlockLinePlacementEvaluation evaluate_block_line_placement(
    const VxlMap& map, std::span<const VoxelCell> line) noexcept;

/** One signed world-space cell from a rotated prefab footprint. */
struct PrefabPlacementCell final {
    std::int32_t x{};
    std::int32_t y{};
    std::int32_t z{};
    [[nodiscard]] friend constexpr bool operator==(const PrefabPlacementCell&,
                                                   const PrefabPlacementCell&) = default;
};

/** Protocol 168 rotates authored cells around Y (roll), X (pitch), then Z (yaw). */
[[nodiscard]] PrefabPlacementCell rotate_prefab_cell(
    PrefabPlacementCell cell, std::uint8_t yaw, std::uint8_t pitch, std::uint8_t roll) noexcept;

/** Convert pivot-relative KV6 render vertices to the exact packet-30 footprint. */
[[nodiscard]] std::array<float, 16U> prefab_preview_transform(
    const std::array<float, 3U>& pivot, PrefabPlacementCell anchor,
    std::uint8_t yaw, std::uint8_t pitch, std::uint8_t roll) noexcept;

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
    const VxlMap& map, std::span<const PrefabPlacementCell> footprint,
    PrefabPlacementCell anchor = {}) noexcept;

struct PrefabPlacementBounds final {
    std::array<std::int32_t, 3U> minimum{};
    std::array<std::int32_t, 3U> maximum{};
};

/** Reuse one rotated footprint and placement result across simulation/render ticks. */
class PrefabPlacementPreview final {
public:
    void reset(std::span<const PrefabPlacementCell> authored = {});
    [[nodiscard]] const PrefabPlacementBounds& bounds(
        std::uint8_t yaw, std::uint8_t pitch, std::uint8_t roll);
    [[nodiscard]] PrefabPlacementEvaluation evaluate(
        const VxlMap& map, PrefabPlacementCell anchor,
        std::uint8_t yaw, std::uint8_t pitch, std::uint8_t roll);
    /** Number of actual voxel scans, for performance diagnostics. */
    [[nodiscard]] std::size_t evaluations() const noexcept { return evaluations_; }

private:
    std::vector<PrefabPlacementCell> authored_;
    std::vector<PrefabPlacementCell> rotated_;
    PrefabPlacementBounds bounds_;
    std::optional<std::array<std::uint8_t, 3U>> rotation_;
    const VxlMap* evaluated_map_{};
    std::uint64_t evaluated_revision_{};
    PrefabPlacementCell evaluated_anchor_{};
    PrefabPlacementEvaluation evaluation_;
    std::size_t evaluations_{};
};

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
    // Retail landscape constructs exceed 200,000 cells. Keep a bounded budget
    // above the bundled catalogue, including overlapping streamed slices.
    static constexpr std::size_t maximum_voxels{1'048'576U};

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
    struct CellHash final {
        [[nodiscard]] std::size_t operator()(PrefabPlacementCell cell) const noexcept {
            std::size_t hash = static_cast<std::uint32_t>(cell.x);
            hash = hash * 1'000'003U ^ static_cast<std::uint32_t>(cell.y);
            return hash * 1'000'003U ^ static_cast<std::uint32_t>(cell.z);
        }
    };
    std::vector<PrefabPlacementVoxel> staged_;
    std::unordered_map<PrefabPlacementCell, std::size_t, CellHash> staged_indices_;
};

} // namespace battlespades::world
