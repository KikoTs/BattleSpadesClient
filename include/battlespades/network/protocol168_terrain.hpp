#pragma once

#include "battlespades/world/chunk_mesh.hpp"
#include "battlespades/world/terrain_effects.hpp"
#include "battlespades/world/voxel_collapse.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace battlespades::network {

struct SetColorPacket final {
    static constexpr std::uint8_t id{11U};
    std::uint8_t player_id{};
    /** Palette RGB integer 0xRRGGBB, serialized blue/green/red. */
    std::uint32_t color{};
};

struct DamagePacket final {
    static constexpr std::uint8_t id{37U};
    std::uint8_t player_id{};
    std::uint8_t type{};
    float damage{};
    std::uint8_t face{};
    bool chunk_check{};
    std::uint8_t seed{};
    std::int16_t causer_id{};
    std::array<float, 3U> position{};
};

struct BlockBuildColoredPacket final {
    static constexpr std::uint8_t id{33U};
    std::int32_t loop_count{};
    std::uint8_t player_id{};
    std::int16_t x{};
    std::int16_t y{};
    std::int16_t z{};
    /** Server RGB integer 0xRRGGBB, serialized low byte first. */
    std::uint32_t color{};
};

struct BlockLinePacket final {
    static constexpr std::uint8_t id{40U};
    std::int32_t loop_count{};
    std::uint8_t player_id{};
    std::array<std::int16_t, 3U> start{};
    std::array<std::int16_t, 3U> end{};
};

/**
 * BlockManagerState(38), little-endian (stock shared.packet round trip):
 * `u8 38, i32 n, n x (i16 x,y,z, u8 remaining*4, u8 b,g,r), i32 n,
 * n x (i16 x,y,z, u8 health*4), i32 n, n x (i16 x,y,z, u8 player_id)`.
 */
struct BlockManagerStatePacket final {
    static constexpr std::uint8_t id{38U};
    struct DamagedRow final {
        std::int16_t x{};
        std::int16_t y{};
        std::int16_t z{};
        /** Remaining (health-multiplier scaled) health, quarter units on the wire. */
        float health{};
        /** Colour when the cell was first hit, as 0xRRGGBB. */
        std::uint32_t original_color{};
        [[nodiscard]] friend bool operator==(const DamagedRow&, const DamagedRow&) = default;
    };
    struct UserRow final {
        std::int16_t x{};
        std::int16_t y{};
        std::int16_t z{};
        float health{};
        [[nodiscard]] friend bool operator==(const UserRow&, const UserRow&) = default;
    };
    struct OccupiedRow final {
        std::int16_t x{};
        std::int16_t y{};
        std::int16_t z{};
        std::uint8_t player_id{};
        [[nodiscard]] friend bool operator==(const OccupiedRow&, const OccupiedRow&) = default;
    };
    std::vector<DamagedRow> damaged;
    std::vector<UserRow> user;
    std::vector<OccupiedRow> occupied;
};

using TerrainPacket = std::variant<SetColorPacket, DamagePacket,
                                   BlockBuildColoredPacket, BlockLinePacket,
                                   BlockManagerStatePacket>;

struct TerrainDecodeResult final {
    std::optional<TerrainPacket> packet;
    std::string error;

    [[nodiscard]] explicit operator bool() const noexcept {
        return packet.has_value();
    }
};

/** Decode one complete Protocol 168 terrain packet, including its id byte. */
[[nodiscard]] TerrainDecodeResult
decode_terrain_packet(std::span<const std::byte> payload);

/** Golden-vector encoder used by parity tests and future outbound actions. */
[[nodiscard]] std::vector<std::byte> encode_packet(const SetColorPacket& packet);
[[nodiscard]] std::vector<std::byte> encode_packet(const DamagePacket& packet);
[[nodiscard]] std::vector<std::byte>
encode_packet(const BlockBuildColoredPacket& packet);
[[nodiscard]] std::vector<std::byte> encode_packet(const BlockLinePacket& packet);
[[nodiscard]] std::vector<std::byte>
encode_packet(const BlockManagerStatePacket& packet);

struct TerrainApplyResult final {
    bool accepted{};
    bool destroyed{};
    std::vector<world::VoxelCell> changed_cells;
    std::vector<world::FallingComponent> falling_components;
    /** Cells a Damage(37) removed itself (collapse debris excluded). */
    std::size_t destroyed_cells{};
    /** Voxels an add_user_block expansion (prefab packet 30) committed. */
    std::size_t user_blocks_added{};
};

/** One cell of a retail Damage(37) footprint, before any map filtering. */
struct DamageFootprintCell final {
    std::int32_t x{};
    std::int32_t y{};
    std::int32_t z{};
    float damage{};
    [[nodiscard]] friend bool operator==(const DamageFootprintCell&,
                                         const DamageFootprintCell&) = default;
};

/**
 * `BlockManager.handle_damage` footprint, ported verbatim from the server's
 * live-fitted model (BS/server/block_damage_model.py). Centre =
 * floor(position + 0.5) per axis for every type. Single, column (z-1..z+1)
 * and machete (z, z+1) types apply `amount`; cube types draw one
 * `random()` per cell in x-major order and apply ceil4(amount + E*r);
 * sphere types draw one `random()` per cell with d^2 < R^2 in z-major (z, x,
 * y) order and apply ceil4(amount * (1 - d^2/R^2) + 2*r). The RNG is a
 * CPython `Random(seed & 0xFF)`. Every footprint cell is returned (solid or
 * not, in or out of the map) in the client's order.
 */
[[nodiscard]] std::vector<DamageFootprintCell>
retail_damage_footprint(std::uint8_t damage_type, const std::array<float, 3U>& position,
                        float amount, std::uint8_t seed);

/** BLOCK_GRANTING_DAMAGES: destroying a cell with these credits one block. */
[[nodiscard]] bool is_block_granting_damage(std::uint8_t damage_type) noexcept;

/** Damage.damage wire byte: unsigned quarter units, rounded to nearest. */
[[nodiscard]] std::uint8_t encode_damage_quarters(float amount) noexcept;

/**
 * Apply `add_damage(amount)` to the single centre cell floor(position+0.5).
 */
[[nodiscard]] TerrainApplyResult apply_direct_damage(
    world::VxlMap& map, const DamagePacket& packet);

/**
 * Apply one wire Damage(37) through retail_damage_footprint and the per-cell
 * BlockManager health model (VxlMap::add_damage). Only solid cells with
 * z <= 238 inside the map take damage. Collapse runs once for the complete
 * action when `chunk_check` is set.
 */
[[nodiscard]] TerrainApplyResult apply_expanded_damage(
    world::VxlMap& map, const DamagePacket& packet);

/**
 * BlockBuildColored(33): `add_user_block(..., 3.0)` without replace_solids,
 * so a solid target is ignored (and keeps its damage).
 */
[[nodiscard]] TerrainApplyResult apply_block_build_colored(
    world::VxlMap& map, const BlockBuildColoredPacket& packet);

/** Merge a BlockManagerState(38) into the map (damaged rows, then user rows). */
[[nodiscard]] TerrainApplyResult apply_block_manager_state(
    world::VxlMap& map, const BlockManagerStatePacket& packet);

/**
 * Reproduce `aoslib.world.cube_line`, the retail face-connected voxel walk.
 * Its tie order is protocol-visible because BlockLine transmits endpoints,
 * not expanded cells; changing it produces client/server ghost blocks.
 */
[[nodiscard]] std::vector<world::VoxelCell>
cube_line_cells(const BlockLinePacket& packet, std::size_t maximum_cells = 64U);

/** Apply an echoed BlockLine using the player's current SetColor palette. */
[[nodiscard]] TerrainApplyResult apply_block_line(
    world::VxlMap& map, const BlockLinePacket& packet, std::uint32_t color);

struct TerrainReplicaResult final {
    bool recognized{};
    TerrainApplyResult mutation;
    std::string error;
};

/**
 * Resolve the local block-wallet debit represented by an accepted server echo.
 * Packet 32 confirms one ordinary voxel; packet 40 confirms exactly the newly
 * materialized BlockLine cells; a competitive BuildPrefabAction(30)
 * (add_to_user_blocks) debits one block per model voxel added, even over
 * existing solids (retail on_single_block_added). Other packets never spend.
 */
[[nodiscard]] std::uint16_t confirmed_owner_block_cost(
    std::span<const std::byte> payload,
    const TerrainReplicaResult& result,
    std::uint8_t local_player_id) noexcept;

/** One already-decoded authoritative solid voxel mutation. */
struct ColoredTerrainCell final {
    world::VoxelCell cell{};
    world::VxlColor color{};
};

/**
 * Stateful Protocol 168 terrain dispatcher.
 *
 * Owns only remote palette state and presentation invalidations. The map
 * remains authoritative session state supplied by the caller. Each complete
 * ENet payload is decoded once, applied through the canonical VxlMap path,
 * and converted to a bounded set of chunk rebuilds/falling presentations.
 * Malformed packets and BlockLine from a player without SetColor fail closed.
 */
class Protocol168TerrainReplica final {
public:
    /**
     * `health_multiplier` is InitialInfo block_health_multiplier; `classic`
     * and `ugc` select BlockManager.add_user_block's mode rules.
     */
    explicit Protocol168TerrainReplica(world::VxlMap& map,
                                       float health_multiplier = 1.0F,
                                       bool classic = false, bool ugc = false,
                                       bool classic_wire = false) noexcept;

    [[nodiscard]] TerrainReplicaResult
    apply(std::span<const std::byte> payload);
    /**
     * Commit an authoritative decoded batch through the same invalidation
     * lane as wire terrain packets. Used by native packet-30 prefab expansion.
     */
    [[nodiscard]] TerrainReplicaResult
    apply_colored_cells(std::span<const ColoredTerrainCell> cells);
    /**
     * Competitive BuildPrefabAction(30): retail `add_user_block(...,
     * DEFAULT_PREFAB_HEALTH, replace_solids=True)` for every model voxel,
     * applied immediately. `user_blocks_added` counts every accepted voxel.
     */
    [[nodiscard]] TerrainReplicaResult
    apply_prefab_user_blocks(std::span<const ColoredTerrainCell> cells);
    /** Commit exact decoded air cells, without inventing collapse or impacts. */
    [[nodiscard]] TerrainReplicaResult
    apply_removed_cells(std::span<const world::VoxelCell> cells);
    /** Local classic damage only shades blocks; removal stays server-owned. */
    void set_classic_block_damage(world::VoxelCell cell, int remaining, world::VxlColor original);
    /**
     * Remember the exact colors expected in compact owner BlockBuild(32)
     * acknowledgements for one prefab action. Packet 32 omits RGB, while the
     * competitive prefab authority blends the player's color with each KV6
     * voxel. The loop/cell key prevents an unrelated later build from
     * consuming a stale color.
     */
    void expect_owner_build_colors(
        std::int32_t loop_count, std::uint8_t player_id,
        std::span<const ColoredTerrainCell> cells);
    [[nodiscard]] std::vector<world::ChunkKey> take_dirty_chunks();
    [[nodiscard]] std::vector<world::FallingComponent>
    take_falling_components();
    /** Drain committed live hit feedback exactly once. */
    [[nodiscard]] std::vector<world::TerrainImpactEvent> take_impact_events();
    [[nodiscard]] std::optional<std::uint32_t>
    player_color(std::uint8_t player_id) const noexcept;

private:
    struct ExpectedBuildKey final {
        std::int32_t loop_count{};
        std::uint8_t player_id{};
        std::uint32_t x{};
        std::uint32_t y{};
        std::uint32_t z{};

        [[nodiscard]] bool operator==(
            const ExpectedBuildKey&) const noexcept = default;
    };

    struct ExpectedBuildKeyHash final {
        [[nodiscard]] std::size_t
        operator()(const ExpectedBuildKey& key) const noexcept;
    };

    world::VxlMap* map_{};
    bool classic_wire_{};
    std::array<std::optional<std::uint32_t>, 256U> player_colors_{};
    std::unordered_map<ExpectedBuildKey, std::uint32_t,
                       ExpectedBuildKeyHash>
        expected_owner_build_colors_;
    std::vector<world::ChunkKey> dirty_chunks_;
    std::vector<world::FallingComponent> falling_components_;
    std::vector<world::TerrainImpactEvent> impact_events_;

    void record(const TerrainApplyResult& result);
};

} // namespace battlespades::network
