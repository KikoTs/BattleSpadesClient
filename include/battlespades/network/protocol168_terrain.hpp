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

using TerrainPacket = std::variant<SetColorPacket, DamagePacket,
                                   BlockBuildColoredPacket, BlockLinePacket>;

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

struct TerrainApplyResult final {
    bool accepted{};
    bool destroyed{};
    std::vector<world::VoxelCell> changed_cells;
    std::vector<world::FallingComponent> falling_components;
};

/**
 * Apply a direct-cell Damage(37) after the packet-specific damage expansion
 * stage. Weapon damage uses this directly; spade/explosion damage must first
 * expand to the exact affected cells recovered for that damage type.
 */
[[nodiscard]] TerrainApplyResult apply_direct_damage(
    world::VxlMap& map, const DamagePacket& packet,
    float block_health = 5.0F);

/**
 * Apply one wire Damage(37), including the native BlockManager expansion for
 * spades, zombie hands, drill bores, and compact deployable explosions.
 * Legacy turret rockets use rounded centres and seeded radius-three falloff.
 * The server intentionally sends one packet for these shapes; treating it as
 * a single voxel leaves collision and rendering permanently desynchronized.
 */
[[nodiscard]] TerrainApplyResult apply_expanded_damage(
    world::VxlMap& map, const DamagePacket& packet,
    float block_health = 5.0F);

/** Apply an explicit-color single block placement. */
[[nodiscard]] TerrainApplyResult apply_block_build_colored(
    world::VxlMap& map, const BlockBuildColoredPacket& packet);

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
 * Packet 32 confirms one ordinary/prefab voxel; packet 40 confirms exactly the
 * newly materialized BlockLine cells. Other terrain packets never spend stock.
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
    explicit Protocol168TerrainReplica(world::VxlMap& map,
                                       float block_health = 5.0F) noexcept;

    [[nodiscard]] TerrainReplicaResult
    apply(std::span<const std::byte> payload);
    /**
     * Commit an authoritative decoded batch through the same invalidation
     * lane as wire terrain packets. Used by native packet-30 prefab expansion.
     */
    [[nodiscard]] TerrainReplicaResult
    apply_colored_cells(std::span<const ColoredTerrainCell> cells);
    /** Commit exact decoded air cells, without inventing collapse or impacts. */
    [[nodiscard]] TerrainReplicaResult
    apply_removed_cells(std::span<const world::VoxelCell> cells);
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
    float block_health_{5.0F};
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
