#pragma once

#include "battlespades/world/emissive_set.hpp"
#include "battlespades/world/static_light_field.hpp"
#include "battlespades/world/vxl_map.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace battlespades::world {

/** Column-slab chunk address; each chunk spans the full 240-voxel height. */
struct ChunkKey final {
    std::uint32_t x{};
    std::uint32_t y{};

    [[nodiscard]] friend constexpr bool operator==(const ChunkKey&, const ChunkKey&) = default;
};

/**
 * One renderer-neutral terrain vertex in canonical voxel coordinates.
 *
 * `abgr` carries pure voxel albedo. Terrain additionally carries the two
 * recovered retail AO-atlas coordinates and its noise corner; detached KV6
 * meshes leave those values zero and continue through their model path.
 */
struct ChunkVertex final {
    float x{};
    float y{};
    float z{};
    std::uint32_t abgr{};
    /** 0:-x 1:+x 2:-y 3:+y 4:-z (top, toward the sky) 5:+z (bottom). */
    std::uint8_t face{};
    /** Raw corner occlusion level 0..3, retained for Enhanced lighting. */
    std::uint8_t occlusion{};
    /** Retail ao_cube512 blue-channel corner, encoded 0:(0,0)..3:(1,1). */
    std::uint8_t noise_corner{};
    /** Retail gl_Vertex.w directional-light bypass, normalized from 0..255. */
    std::uint8_t directional_influence{};
    /**
     * Baked static point light arriving here, packed ABGR with alpha unused.
     *
     * Deliberately its own attribute rather than a spare byte: the declared
     * spare bytes carry an unresolved question about whether the bgfx layout
     * and the shader agree on normalization, and light must not inherit that.
     * Last member so every existing positional initialiser stays valid.
     */
    std::uint32_t static_light{};
    /** Red-channel AO lookup coordinate from retail vxl.pyd sub_10005440. */
    float ao_u{};
    float ao_v{};
    /** Green-channel top-edge lookup coordinate from the same atlas. */
    float edge_u{};
    float edge_v{};
};

static_assert(sizeof(ChunkVertex) == 40U,
              "the bgfx vertex layout stride must match this exactly; a mismatch "
              "silently corrupts every vertex after the first");

struct ChunkMesh final {
    ChunkKey key{};
    std::vector<ChunkVertex> vertices;
    std::vector<std::uint32_t> indices;
    /** Tight bounds of emitted geometry in canonical voxel coordinates. */
    std::array<float, 3U> minimum{};
    std::array<float, 3U> maximum{};

    [[nodiscard]] bool empty() const noexcept { return indices.empty(); }
    [[nodiscard]] std::size_t face_count() const noexcept { return indices.size() / 6U; }
};

/**
 * Shading inputs for vertex colors at mesh time.
 *
 * The face and scalar-occlusion factors are compatibility fallbacks for
 * detached voxel models and self-contained test meshes. Terrain Legacy mode
 * no longer consumes them: it uses the recovered ao_cube512 atlas codes,
 * server-authored two-light StateData values and retail map shader equations.
 */
struct ChunkMesherConfig final {
    std::uint32_t chunk_edge{16U};
    /** Water-bed fallback selected by UGC Settings' final ground-color row. */
    VxlColor bed_water_color{18U, 67U, 94U, 255U};
    /**
     * Multiply the shade tables into the vertex color at mesh time.
     *
     * Off by default: baking here and shading again in the fragment stage
     * would darken the world twice, and keeping albedo pure is what lets the
     * lighting model change without re-meshing all 1024 chunks.
     */
    bool bake_shading{false};
    /**
     * Colours whose voxels light themselves.
     *
     * Empty for every map without an authored palette, which is all of them but
     * TokyoNeon, so this costs nothing by default. Held by value; the swatch
     * span points at static storage.
     */
    EmissivePalette emissive{};
    /**
     * Placed lights baked into vertex colours, as retail's flare block did.
     *
     * Null for maps and passes with none, which costs nothing.
     */
    const StaticLightField* static_lights{};
    /**
     * Indexed by ChunkVertex::face for optional baked model/effect shading.
     */
    std::array<float, 6U> face_shade{0.85F, 0.85F, 0.75F, 0.75F, 1.00F, 0.60F};
    /** Indexed by corner occlusion level for optional baked model shading. */
    std::array<float, 4U> occlusion_shade{1.00F, 0.80F, 0.65F, 0.50F};
};

/**
 * Deterministic exposed-face mesher over the canonical map.
 *
 * Neighbor queries go through the shared map, so faces between voxels in
 * adjacent chunks are culled identically on both sides and chunk borders can
 * never produce duplicated internal faces or cracks. Below-world neighbors
 * (z >= height) count as solid: the forced z=239 bed never emits downward
 * faces nobody can see. mesh() is const and safe to call from worker threads
 * as long as the map is not mutated concurrently.
 */
class ChunkMesher final {
public:
    explicit ChunkMesher(ChunkMesherConfig config = {});

    [[nodiscard]] const ChunkMesherConfig& config() const noexcept { return config_; }
    [[nodiscard]] std::uint32_t chunks_per_axis() const noexcept;

    [[nodiscard]] ChunkMesh mesh(const VxlMap& map, ChunkKey key) const;

private:
    ChunkMesherConfig config_;
};

/**
 * Dirty-chunk bookkeeping for live terrain mutation.
 *
 * Marking a voxel dirties its owning chunk and every edge/corner-adjacent
 * chunk sharing that voxel border, because exposed-face and occlusion results
 * reach one voxel into neighboring chunks. take() drains in deterministic
 * row-major order with a caller-supplied budget.
 */
class ChunkTracker final {
public:
    explicit ChunkTracker(std::uint32_t chunk_edge = 16U);

    [[nodiscard]] std::uint32_t chunks_per_axis() const noexcept { return chunks_; }
    [[nodiscard]] std::size_t dirty_count() const noexcept { return dirty_count_; }

    void mark_all();
    void mark_voxel(std::uint32_t x, std::uint32_t y);
    [[nodiscard]] std::vector<ChunkKey> take(std::size_t limit);

private:
    void mark_chunk(std::uint32_t chunk_x, std::uint32_t chunk_y);

    std::uint32_t chunk_edge_;
    std::uint32_t chunks_;
    std::vector<std::uint8_t> dirty_;
    std::size_t dirty_count_{};
};

} // namespace battlespades::world
