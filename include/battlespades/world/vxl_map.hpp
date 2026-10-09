#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace battlespades::world {

struct VxlColor final {
    std::uint8_t red{};
    std::uint8_t green{};
    std::uint8_t blue{};
    std::uint8_t alpha{255U};

    [[nodiscard]] friend constexpr bool operator==(const VxlColor&, const VxlColor&) = default;
};

struct VxlLoadResult;
enum class VxlDecodeProfile : std::uint8_t {
    retail,
    classic64,
    /** Headerless disk import: full 512-square maps at most 64 high use Classic. */
    automatic,
    /** Authoritative 240-high MapSync: coordinates and colors are already final. */
    canonical240,
};

/**
 * Retail `BlockManager.DamagedBlock`: the remaining (already health-multiplier
 * scaled) health of a partially damaged voxel and the colour it had when the
 * first hit landed. The live voxel colour is darkened separately per hit.
 */
struct DamagedBlock final {
    float health{};
    VxlColor original_color{};
};

/** Result of one retail `BlockManager.add_damage` call. */
enum class BlockDamageOutcome : std::uint8_t {
    ignored,
    damaged,
    destroyed,
};

/**
 * Retail `shared.common.dim(value, damage)` for one channel:
 * `value - ((value * int(round(damage))) >> 3)`, clamped at zero. Python 2
 * rounds halves away from zero.
 */
[[nodiscard]] std::uint8_t retail_dim(std::uint8_t value, float damage) noexcept;
/** Applies retail_dim to red, green and blue; alpha (baked light) is kept. */
[[nodiscard]] VxlColor retail_dim(VxlColor color, float damage) noexcept;

/** vxl.pyd marker table: colour & 0xF0F0F0 is pure green (slot 0) or blue (slot 1). */
[[nodiscard]] bool is_vxl_chroma_marker(std::uint32_t color) noexcept;

/**
 * Canonical Battle Builder voxel map.
 *
 * The decoder accepts raw, decompressed VXL bytes. Network framing and zlib
 * belong to the Protocol 168 transport adapter; keeping them outside this type
 * lets disk Tutorial maps and server MapSync data use the identical parser.
 */
class VxlMap final {
public:
    static constexpr std::uint32_t width{512U};
    static constexpr std::uint32_t depth{512U};
    static constexpr std::uint32_t height{240U};
    /** Retail `max_modifiable_z`: z=239 is the indestructible bed. */
    static constexpr std::uint32_t max_damageable_z{238U};
    /** DEFAULT_BLOCK_HEALTH: every map voxel without a user-block entry. */
    static constexpr float default_block_health{5.0F};
    /** DEFAULT_PREFAB_HEALTH: BlockBuild(32) type 0, BlockLine(40), prefabs. */
    static constexpr float prefab_block_health{9.0F};
    /** DEFAULT_SNOW_HEALTH: BlockBuild(32) type 1 and BlockBuildColored(33). */
    static constexpr float snow_block_health{3.0F};

    [[nodiscard]] static VxlLoadResult load(std::span<const std::byte> bytes,
                                            VxlDecodeProfile profile = VxlDecodeProfile::retail);
    /**
     * Native disk import. Automatic preserves stock-map retail semantics and
     * honors a sidecar's vxl_format (auto, retail, classic64). Legacy maps
     * retain their authored colors and gain the fixed Classic z offset of 176.
     * An explicit profile overrides the sidecar and resolves ambiguous files.
     */
    [[nodiscard]] static VxlLoadResult
    load_file(const std::filesystem::path& path,
              VxlDecodeProfile profile = VxlDecodeProfile::automatic);

    [[nodiscard]] bool solid(std::uint32_t x, std::uint32_t y,
                             std::uint32_t z) const noexcept;
    [[nodiscard]] std::optional<VxlColor> color(std::uint32_t x, std::uint32_t y,
                                                std::uint32_t z) const noexcept;
    [[nodiscard]] std::uint16_t surface_z(std::uint32_t x,
                                          std::uint32_t y) const noexcept;
    [[nodiscard]] std::uint64_t solid_voxels() const noexcept;
    /** Retail vxl.pyd +0x3EB34D0: 16x16x16 chunks (of 15,360) holding any solid. */
    [[nodiscard]] std::uint32_t non_empty_chunks() const noexcept { return non_empty_chunks_; }
    /**
     * VXL.is_space_to_add_blocks (vxl.pyd 0x10019c00): false only when the
     * solid counter reaches 2,800,000 AND the non-empty chunk counter reaches
     * 3,200. BlockManager (gameScene 0x10075490) consults it only in the Map
     * Creator, so callers gate on UGC themselves.
     */
    [[nodiscard]] bool is_space_to_add_blocks() const noexcept {
        return solid_voxels_ < ugc_capacity_solid_limit ||
               non_empty_chunks_ < ugc_capacity_chunk_limit;
    }
    static constexpr std::uint64_t ugc_capacity_solid_limit{2'800'000U};
    static constexpr std::uint32_t ugc_capacity_chunk_limit{3'200U};
    [[nodiscard]] std::uint32_t source_edge() const noexcept;
    [[nodiscard]] std::uint32_t source_z_shift() const noexcept;

    /**
     * Places one solid voxel, replacing any existing color. Fails closed for
     * out-of-range coordinates. Successful mutations advance revision() so
     * chunk meshes and future MapSync consumers can invalidate precisely.
     * The cell starts fresh: any user-block health and DamagedBlock go.
     */
    [[nodiscard]] bool set_voxel(std::uint32_t x, std::uint32_t y, std::uint32_t z,
                                 VxlColor color) noexcept;

    /**
     * Removes one solid voxel and repairs the cached column surface. Fails
     * closed for out-of-range or already-empty coordinates and for the forced
     * z=239 bed, which Battle Builder never exposes to destruction.
     */
    [[nodiscard]] bool clear_voxel(std::uint32_t x, std::uint32_t y,
                                   std::uint32_t z) noexcept;

    /**
     * Fraction of the cell's initial health already lost, in [0,1). Derived
     * from the DamagedBlock; zero for undamaged cells and air.
     */
    [[nodiscard]] float damage_fraction(std::uint32_t x, std::uint32_t y,
                                        std::uint32_t z) const noexcept;
    /**
     * Legacy helper: installs a DamagedBlock holding `(1 - fraction)` of the
     * initial health without recolouring. Invalid/air/bed cells fail closed.
     */
    [[nodiscard]] bool set_damage_fraction(std::uint32_t x, std::uint32_t y,
                                           std::uint32_t z, float fraction) noexcept;
    /** Clears pre-break damage, returning true only when state changed. */
    [[nodiscard]] bool clear_damage(std::uint32_t x, std::uint32_t y,
                                    std::uint32_t z) noexcept;

    /**
     * Retail BlockManager per-cell health model.
     *
     * `get_initial_health` is `user_blocks[cell]` when present, otherwise
     * DEFAULT_BLOCK_HEALTH * health_multiplier (InitialInfo
     * block_health_multiplier, RULE_BLOCK_HEALTH).
     */
    void set_health_multiplier(float multiplier) noexcept;
    [[nodiscard]] float health_multiplier() const noexcept;
    /**
     * BlockManager.add_user_block mode rules (gameScene 0x10070530): in
     * classic mode every user block gets DEFAULT_BLOCK_HEALTH (5) instead of
     * the caller's 9/3; in UGC mode the user_blocks entry is dropped, so the
     * cell falls back to the map default.
     */
    void set_user_block_rules(bool classic, bool ugc) noexcept {
        classic_user_blocks_ = classic;
        ugc_user_blocks_ = ugc;
    }
    [[nodiscard]] float initial_health(std::uint32_t x, std::uint32_t y,
                                       std::uint32_t z) const noexcept;
    [[nodiscard]] std::optional<float> user_block_health(
        std::uint32_t x, std::uint32_t y, std::uint32_t z) const noexcept;
    /** BlockManagerState(38) user row: stores the value exactly as sent. */
    [[nodiscard]] bool set_user_block_health(std::uint32_t x, std::uint32_t y,
                                             std::uint32_t z, float health) noexcept;
    /**
     * Retail `BlockManager.add_user_block`: refuses a solid cell unless
     * `replace_solids`, writes the exact colour, records `health *
     * health_multiplier` as the cell's initial health and forgets any
     * DamagedBlock. Fails for the z=239 bed and outside the map.
     */
    [[nodiscard]] bool add_user_block(std::uint32_t x, std::uint32_t y, std::uint32_t z,
                                      VxlColor color, float health,
                                      bool replace_solids = false) noexcept;
    [[nodiscard]] std::optional<DamagedBlock> damaged_block(
        std::uint32_t x, std::uint32_t y, std::uint32_t z) const noexcept;
    /**
     * Retail `BlockManager.add_damage`: the first hit records
     * DamagedBlock(initial_health, current colour); every hit subtracts the
     * amount and, while the cell survives, darkens the CURRENT colour with
     * retail_dim(amount), so darkening compounds. Reaching zero removes the
     * voxel. Air, the bed (z > 238) and non-positive amounts are ignored.
     */
    [[nodiscard]] BlockDamageOutcome add_damage(std::uint32_t x, std::uint32_t y,
                                                std::uint32_t z, float amount) noexcept;
    /**
     * BlockManagerState(38) damaged row: installs DamagedBlock(remaining,
     * original) and repaints the voxel as retail_dim(original,
     * initial_health - remaining). Air cells are ignored.
     */
    [[nodiscard]] bool set_damaged_block(std::uint32_t x, std::uint32_t y, std::uint32_t z,
                                         float remaining, VxlColor original) noexcept;
    /**
     * PaintBlock(7) / VXL.color_block: recolours a solid voxel and keeps its
     * user-block health and DamagedBlock untouched.
     */
    [[nodiscard]] bool recolor_voxel(std::uint32_t x, std::uint32_t y, std::uint32_t z,
                                     VxlColor color) noexcept;

    /** Monotonic mutation counter; load produces revision zero. */
    [[nodiscard]] std::uint64_t revision() const noexcept;

    /**
     * Retail `VXL.generate_ground_color_table` (vxl.pyd 0x1001D860) over
     * InitialInfo `ground_colors` / SetGroundColors(118) rows (r, g, b, z).
     * The first row fills 0..z0; each later row interpolates linearly from
     * the running cursor up to its z (`t = (z - cursor) / (z - z_prev)`,
     * truncated per channel); the tail repeats the last row. Rows whose z
     * lies above the cursor write nothing but still become the previous row.
     * Entries are 0x00RRGGBB.
     */
    [[nodiscard]] static std::array<std::uint32_t, height> generate_ground_color_table(
        std::span<const std::array<std::uint8_t, 4U>> rows) noexcept;
    /**
     * Installs the ground colour table used for implicit interior voxels.
     * An empty row list restores the legacy behaviour (the column's last
     * explicit colour carried downward). Meshes pick it up on their next
     * rebuild, so a live change must re-mesh.
     */
    void set_ground_colors(std::span<const std::array<std::uint8_t, 4U>> rows) noexcept;
    [[nodiscard]] bool has_ground_color_table() const noexcept { return ground_table_set_; }
    /** The installed table's RGB at `z` (0xRRGGBB); 0 when no table is set. */
    [[nodiscard]] std::uint32_t ground_table_rgb(std::uint32_t z) const noexcept {
        return ground_table_set_ && z < height ? (ground_table_[z] & 0x00FFFFFFU) : 0U;
    }
    /**
     * True for a solid cell the VXL file did not colour explicitly (filled
     * between or below spans). Retail colours such a cell from the ground
     * table when digging exposes it (vxl.pyd 0x10029C80, `table[z] +
     * 0x010101 * (rand() & 3)`, no jitter at x == 0, y == 0 or z == 239).
     * color() resolves it lazily with a stable 0..3 grey jitter, which is
     * equivalent because the retail jitter is C rand().
     */
    [[nodiscard]] bool implicit_interior(std::uint32_t x, std::uint32_t y,
                                         std::uint32_t z) const noexcept;

private:
    std::vector<std::uint8_t> solid_bits_;
    std::vector<std::uint32_t> colors_;
    /** One bit per voxel, set while the cell holds an implicit (unauthored) colour. */
    std::vector<std::uint8_t> implicit_bits_;
    std::array<std::uint32_t, height> ground_table_{};
    bool ground_table_set_{};
    std::vector<std::uint16_t> surfaces_;
    std::unordered_map<std::uint32_t, DamagedBlock> damaged_;
    std::unordered_map<std::uint32_t, float> user_health_;
    float health_multiplier_{1.0F};
    bool classic_user_blocks_{};
    bool ugc_user_blocks_{};
    std::uint64_t solid_voxels_{};
    /** Solid count per 16x16x16 chunk (32 x 32 x 15) and the non-empty total. */
    std::vector<std::uint16_t> chunk_solids_;
    std::uint32_t non_empty_chunks_{};
    void count_chunk_solid(std::uint32_t x, std::uint32_t y, std::uint32_t z,
                           bool added) noexcept;
    std::uint64_t revision_{};
    std::uint32_t source_edge_{};
    std::uint32_t source_z_shift_{};

    void put(std::uint32_t x, std::uint32_t y, std::uint32_t z,
             std::uint32_t color) noexcept;
    /** put() for a cell the loader fills implicitly. */
    void put_implicit(std::uint32_t x, std::uint32_t y, std::uint32_t z,
                      std::uint32_t color) noexcept;
    void clear_implicit(std::size_t voxel) noexcept;
    /** The stored word, with an implicit cell resolved through the ground table. */
    [[nodiscard]] std::uint32_t color_word(std::size_t voxel, std::uint32_t x,
                                           std::uint32_t y, std::uint32_t z) const noexcept;
    void write_rgb(std::uint32_t x, std::uint32_t y, std::uint32_t z,
                   VxlColor color) noexcept;
    void remove_voxel_bits(std::uint32_t x, std::uint32_t y, std::uint32_t z) noexcept;
    /** vxl.pyd sub_10029FD0 over the explicit marker words recorded by load. */
    void remove_chroma_markers(std::vector<std::uint32_t>& candidates) noexcept;
};

struct VxlLoadResult final {
    std::optional<VxlMap> map;
    std::string error;

    [[nodiscard]] explicit operator bool() const noexcept { return map.has_value(); }
};

} // namespace battlespades::world
