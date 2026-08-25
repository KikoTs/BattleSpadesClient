#include "battlespades/network/protocol168_terrain.hpp"
#include "battlespades/network/protocol168_tool_actions.hpp"
#include "battlespades/world/vxl_map.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

namespace {

using battlespades::network::BlockBuildColoredPacket;
using battlespades::network::BlockBuildPacket;
using battlespades::network::BlockLinePacket;
using battlespades::network::DamagePacket;
using battlespades::network::PaintBlockPacket;
using battlespades::network::SetColorPacket;
using battlespades::network::Protocol168TerrainReplica;
using battlespades::network::apply_block_build_colored;
using battlespades::network::apply_block_line;
using battlespades::network::apply_direct_damage;
using battlespades::network::apply_expanded_damage;
using battlespades::network::cube_line_cells;
using battlespades::network::confirmed_owner_block_cost;
using battlespades::network::decode_terrain_packet;
using battlespades::network::encode_packet;
using battlespades::world::VxlColor;
using battlespades::world::VxlMap;

void expect(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error{message};
    }
}

[[nodiscard]] VxlMap empty_world() {
    std::vector<std::byte> bytes;
    bytes.reserve(static_cast<std::size_t>(VxlMap::width) * VxlMap::depth * 4U);
    for (std::size_t column{};
         column < static_cast<std::size_t>(VxlMap::width) * VxlMap::depth;
         ++column) {
        bytes.push_back(std::byte{0U});
        bytes.push_back(std::byte{1U});
        bytes.push_back(std::byte{0U});
        bytes.push_back(std::byte{0U});
    }
    auto loaded = VxlMap::load(bytes);
    expect(static_cast<bool>(loaded), "synthetic empty VXL must parse");
    return std::move(*loaded.map);
}

void damage_golden_vector_matches_python_protocol_oracle() {
    DamagePacket packet;
    packet.player_id = 7U;
    packet.type = 6U;
    packet.damage = 3.0F;
    packet.face = 2U;
    packet.chunk_check = true;
    packet.seed = 0x5AU;
    packet.causer_id = -2;
    packet.position = {10.0F, 20.0F, 30.0F};
    const std::array<std::uint8_t, 21U> expected{
        0x25U, 0x07U, 0x06U, 0x0CU, 0x02U, 0x01U, 0x5AU, 0xFEU, 0xFFU,
        0x00U, 0x00U, 0x20U, 0x41U, 0x00U, 0x00U, 0xA0U, 0x41U, 0x00U,
        0x00U, 0xF0U, 0x41U};
    const auto encoded = encode_packet(packet);
    expect(encoded.size() == expected.size(), "Damage(37) must be exactly 21 bytes");
    for (std::size_t index{}; index < expected.size(); ++index) {
        expect(std::to_integer<std::uint8_t>(encoded[index]) == expected[index],
               "Damage(37) byte differs from Python protocol oracle");
    }
    const auto decoded = decode_terrain_packet(encoded);
    expect(static_cast<bool>(decoded), decoded.error.c_str());
    const auto& round_trip = std::get<DamagePacket>(*decoded.packet);
    expect(round_trip.damage == 3.0F && round_trip.causer_id == -2 &&
               round_trip.position == packet.position,
           "Damage(37) must round-trip fixed8, signed id and float positions");
}

void colored_build_and_block_line_round_trip() {
    SetColorPacket palette{3U, 0x123456U};
    const auto palette_bytes = encode_packet(palette);
    const std::array<std::uint8_t, 5U> expected_palette{
        11U, 3U, 0x56U, 0x34U, 0x12U};
    expect(palette_bytes.size() == expected_palette.size(),
           "SetColor(11) must be exactly five bytes");
    for (std::size_t index{}; index < expected_palette.size(); ++index) {
        expect(std::to_integer<std::uint8_t>(palette_bytes[index]) ==
                   expected_palette[index],
               "SetColor must serialize blue/green/red like Python 2 retail");
    }
    const auto palette_decoded = decode_terrain_packet(palette_bytes);
    expect(static_cast<bool>(palette_decoded) &&
               std::get<SetColorPacket>(*palette_decoded.packet).color ==
                   palette.color,
           "SetColor must round-trip palette RGB");

    BlockBuildColoredPacket build;
    build.loop_count = 0x01020304;
    build.player_id = 9U;
    build.x = 100;
    build.y = 101;
    build.z = 102;
    build.color = 0x123456U;
    const auto encoded = encode_packet(build);
    expect(encoded.size() == 15U, "BlockBuildColored(33) must be 15 bytes");
    expect(std::to_integer<std::uint8_t>(encoded[12U]) == 0x56U &&
               std::to_integer<std::uint8_t>(encoded[13U]) == 0x34U &&
               std::to_integer<std::uint8_t>(encoded[14U]) == 0x12U,
           "BlockBuildColored color must be low/mid/high on the wire");
    const auto decoded = decode_terrain_packet(encoded);
    expect(static_cast<bool>(decoded), decoded.error.c_str());
    expect(std::get<BlockBuildColoredPacket>(*decoded.packet).color == 0x123456U,
           "BlockBuildColored color must round-trip as 0xRRGGBB");

    BlockLinePacket line;
    line.loop_count = -9;
    line.player_id = 3U;
    line.start = {1, 2, 3};
    line.end = {10, 20, 30};
    const auto line_bytes = encode_packet(line);
    expect(line_bytes.size() == 18U, "BlockLine(40) must be exactly 18 bytes");
    const auto line_decoded = decode_terrain_packet(line_bytes);
    expect(static_cast<bool>(line_decoded) &&
               std::get<BlockLinePacket>(*line_decoded.packet).end == line.end,
           "BlockLine(40) must round-trip all six signed coordinates");
}

void block_line_matches_native_cube_line_oracle() {
    BlockLinePacket line;
    line.start = {0, 0, 0};
    line.end = {3, 2, 1};
    const std::vector<battlespades::world::VoxelCell> expected{
        {0U, 0U, 0U}, {1U, 0U, 0U}, {1U, 1U, 0U}, {1U, 1U, 1U},
        {2U, 1U, 1U}, {2U, 2U, 1U}, {3U, 2U, 1U}};
    expect(cube_line_cells(line) == expected,
           "BlockLine expansion must match aoslib.world.cube_line");

    line.start = {3, 2, 1};
    line.end = {0, 0, 0};
    const std::vector<battlespades::world::VoxelCell> reverse_expected{
        {3U, 2U, 1U}, {2U, 2U, 1U}, {2U, 1U, 1U}, {2U, 1U, 0U},
        {1U, 1U, 0U}, {1U, 0U, 0U}, {0U, 0U, 0U}};
    expect(cube_line_cells(line) == reverse_expected,
           "negative BlockLine expansion must retain stock tie behavior");

    auto map = empty_world();
    line.start = {100, 100, 100};
    line.end = {103, 102, 101};
    const auto applied = apply_block_line(map, line, 0xA1B2C3U);
    expect(applied.accepted && applied.changed_cells.size() == 7U,
           "BlockLine must place every expanded empty cell");
    const auto color = map.color(103U, 102U, 101U);
    expect(color.has_value() && color->red == 0xA1U && color->green == 0xB2U &&
               color->blue == 0xC3U,
           "BlockLine must use the player's current SetColor RGB");
}

void terrain_packets_share_damage_and_collapse_world_path() {
    auto map = empty_world();
    BlockBuildColoredPacket build;
    build.x = 100;
    build.y = 100;
    build.z = 100;
    build.color = 0x785A3CU;
    const auto built = apply_block_build_colored(map, build);
    expect(built.accepted && map.solid(100U, 100U, 100U),
           "colored placement must enter canonical collision map");
    const auto color = map.color(100U, 100U, 100U);
    expect(color.has_value() && color->red == 0x78U && color->green == 0x5AU &&
               color->blue == 0x3CU,
           "wire color must become canonical RGB without channel swapping");

    DamagePacket damage;
    damage.position = {100.0F, 100.0F, 100.0F};
    damage.damage = 3.0F;
    auto result = apply_direct_damage(map, damage);
    expect(result.accepted && !result.destroyed && map.solid(100U, 100U, 100U),
           "sublethal Damage(37) must retain collision");
    expect(map.damage_fraction(100U, 100U, 100U) == 0.6F,
           "sublethal Damage(37) must update visual block health");

    damage.damage = 2.0F;
    damage.chunk_check = true;
    result = apply_direct_damage(map, damage);
    expect(result.destroyed && !map.solid(100U, 100U, 100U),
           "lethal Damage(37) must remove canonical collision");
}

void native_damage_shapes_match_server_and_retail_block_manager() {
    const VxlColor color{120U, 90U, 60U, 255U};
    DamagePacket damage;
    damage.position = {200.0F, 200.0F, 100.0F};
    damage.damage = 20.0F;

    auto map = empty_world();
    for (std::uint32_t z{99U}; z <= 101U; ++z) {
        expect(map.set_voxel(200U, 200U, z, color),
               "spade fixture must place its target column");
    }
    damage.type = 2U;
    auto result = apply_expanded_damage(map, damage);
    expect(result.destroyed && result.changed_cells.size() == 3U,
           "SPADE_DAMAGE must expand to the native three-cell z column");

    map = empty_world();
    for (std::uint32_t x{199U}; x <= 201U; ++x) {
        for (std::uint32_t y{199U}; y <= 201U; ++y) {
            for (std::uint32_t z{99U}; z <= 101U; ++z) {
                static_cast<void>(map.set_voxel(x, y, z, color));
            }
        }
    }
    damage.type = 17U;
    result = apply_expanded_damage(map, damage);
    expect(result.changed_cells.size() == 27U,
           "ZOMBIE_DAMAGE must use the Super Spade 3x3x3 handler");

    map = empty_world();
    std::size_t expected_radius_cells{};
    for (std::int32_t dx{-2}; dx <= 2; ++dx) {
        for (std::int32_t dy{-2}; dy <= 2; ++dy) {
            for (std::int32_t dz{-2}; dz <= 2; ++dz) {
                if (dx * dx + dy * dy + dz * dz > 6) continue;
                ++expected_radius_cells;
                static_cast<void>(map.set_voxel(
                    static_cast<std::uint32_t>(200 + dx),
                    static_cast<std::uint32_t>(200 + dy),
                    static_cast<std::uint32_t>(100 + dz), color));
            }
        }
    }
    expect(expected_radius_cells == 81U,
           "radius-two oracle itself must contain 81 cells");
    damage.type = 10U;
    result = apply_expanded_damage(map, damage);
    expect(result.changed_cells.size() == 81U,
           "DRILL_DAMAGE must reproduce the compact 81-cell bore");

    map = empty_world();
    for (std::uint32_t x{199U}; x <= 201U; ++x) {
        for (std::uint32_t y{199U}; y <= 201U; ++y) {
            for (std::uint32_t z{99U}; z <= 101U; ++z) {
                static_cast<void>(map.set_voxel(x, y, z, color));
            }
        }
    }
    damage.type = 15U;
    damage.position = {200.5F, 200.5F, 100.5F};
    result = apply_expanded_damage(map, damage);
    expect(result.changed_cells.size() == 19U,
           "LANDMINE_DAMAGE must accept its rendered half-voxel centre and crater radius one");

    map = empty_world();
    for (std::int32_t dx{-2}; dx <= 2; ++dx) {
        for (std::int32_t dy{-2}; dy <= 2; ++dy) {
            for (std::int32_t dz{-2}; dz <= 2; ++dz) {
                if (dx * dx + dy * dy + dz * dz > 6) continue;
                static_cast<void>(map.set_voxel(
                    static_cast<std::uint32_t>(200 + dx),
                    static_cast<std::uint32_t>(200 + dy),
                    static_cast<std::uint32_t>(100 + dz), color));
            }
        }
    }
    damage.type = 41U;
    damage.position = {200.5F, 200.5F, 100.5F};
    result = apply_expanded_damage(map, damage);
    expect(result.changed_cells.size() == 81U,
           "C4_DAMAGE must floor its attached-face centre exactly like server BlockManager");
}

void live_damage_produces_one_drainable_impact_event() {
    auto map = empty_world();
    const VxlColor color{80U, 100U, 120U, 255U};
    expect(map.set_voxel(30U, 31U, 32U, color),
           "impact fixture must place a target block");
    Protocol168TerrainReplica replica{map};
    DamagePacket damage;
    damage.player_id = 7U;
    damage.type = 6U;
    damage.damage = 1.0F;
    damage.face = 2U;
    damage.position = {30.0F, 31.0F, 32.0F};
    const auto result = replica.apply(encode_packet(damage));
    expect(result.mutation.accepted && !result.mutation.destroyed,
           "sublethal server damage must still be a committed impact");
    const auto impacts = replica.take_impact_events();
    expect(impacts.size() == 1U && impacts.front().color == color &&
               impacts.front().cell == battlespades::world::VoxelCell{30U, 31U, 32U},
           "live server damage must retain target color/cell for particles");
    expect(replica.take_impact_events().empty(),
           "impact feedback must be delivered exactly once");
}

void drill_damage_is_a_bore_tick_not_a_terminal_explosion() {
    auto map = empty_world();
    const VxlColor color{88U, 76U, 64U, 255U};
    expect(map.set_voxel(80U, 81U, 82U, color),
           "drill fixture must place a target voxel");
    Protocol168TerrainReplica replica{map};
    DamagePacket damage;
    damage.player_id = 3U;
    damage.type = 10U;
    damage.damage = 20.0F;
    damage.position = {80.0F, 81.0F, 82.0F};
    const auto result = replica.apply(encode_packet(damage));
    const auto impacts = replica.take_impact_events();
    expect(result.mutation.accepted && impacts.size() == 1U &&
               impacts.front().kind ==
                   battlespades::world::TerrainImpactKind::drill &&
               impacts.front().source_tool == 14U,
           "DRILL_DAMAGE must emit one Drillgun bore tick, never an explosion");
}

void melee_damage_ids_translate_to_canonical_tools() {
    struct Mapping final {
        std::uint8_t damage_type;
        std::uint8_t tool_id;
    };
    constexpr std::array mappings{
        Mapping{0U, 0U},   Mapping{1U, 1U},   Mapping{2U, 2U},
        Mapping{3U, 3U},   Mapping{4U, 4U},   Mapping{5U, 4U},
        Mapping{17U, 24U}, Mapping{26U, 34U}, Mapping{28U, 44U},
        Mapping{29U, 45U}, Mapping{31U, 45U}, Mapping{34U, 49U},
        Mapping{35U, 50U}, Mapping{36U, 52U}, Mapping{42U, 63U},
    };
    for (const auto mapping : mappings) {
        auto map = empty_world();
        expect(map.set_voxel(40U, 41U, 42U, {90U, 80U, 70U, 255U}),
               "melee mapping fixture must place its target");
        Protocol168TerrainReplica replica{map};
        DamagePacket damage;
        damage.type = mapping.damage_type;
        damage.damage = 1.0F;
        damage.position = {40.0F, 41.0F, 42.0F};
        const auto result = replica.apply(encode_packet(damage));
        expect(result.mutation.accepted,
               "known melee damage must commit against a solid target");
        const auto impacts = replica.take_impact_events();
        expect(impacts.size() == 1U &&
                   impacts.front().kind ==
                       battlespades::world::TerrainImpactKind::melee &&
                   impacts.front().source_tool == mapping.tool_id,
               "Damage enum must not be mistaken for selectable-tool enum");
    }
}

void stateful_replica_orders_palette_mutation_and_chunk_invalidation() {
    auto map = empty_world();
    Protocol168TerrainReplica replica{map};
    BlockLinePacket line;
    line.player_id = 42U;
    line.start = {15, 15, 100};
    line.end = {16, 15, 100};
    auto result = replica.apply(encode_packet(line));
    expect(result.recognized && !result.mutation.accepted && !result.error.empty(),
           "BlockLine without prior palette state must fail closed");
    expect(!map.solid(15U, 15U, 100U),
           "rejected BlockLine must not mutate collision");

    const SetColorPacket palette{42U, 0x224466U};
    result = replica.apply(encode_packet(palette));
    expect(result.recognized && result.mutation.accepted &&
               replica.player_color(42U) == palette.color,
           "SetColor must commit per-player replica palette state");
    const auto line_payload = encode_packet(line);
    result = replica.apply(line_payload);
    expect(result.mutation.accepted && result.mutation.changed_cells.size() == 2U,
           "palette-ready BlockLine must enter the canonical map");
    expect(confirmed_owner_block_cost(line_payload, result, 42U) == 2U &&
               confirmed_owner_block_cost(line_payload, result, 41U) == 0U,
           "only the owning client must debit accepted BlockLine cells");
    const auto dirty = replica.take_dirty_chunks();
    expect(dirty.size() == 4U,
           "line crossing both x and y chunk borders must invalidate four chunks");
    expect(replica.take_dirty_chunks().empty(),
           "dirty chunk delivery must be drain-once");
}

void owner_build_and_paint_echoes_mutate_the_canonical_map() {
    auto map = empty_world();
    Protocol168TerrainReplica replica{map};

    BlockBuildPacket build;
    build.loop_count = 19;
    build.player_id = 7U;
    build.position = {31, 47, 100};
    build.block_type = 0U;
    auto result = replica.apply(
        battlespades::network::encode_packet(build));
    expect(result.recognized && !result.mutation.accepted &&
               !result.error.empty(),
           "owner BlockBuild without palette state must fail closed");
    expect(!map.solid(31U, 47U, 100U),
           "rejected owner echo must not create collision");

    constexpr std::uint32_t build_color{0x214365U};
    result = replica.apply(battlespades::network::encode_packet(
        SetColorPacket{7U, build_color}));
    expect(result.recognized && result.mutation.accepted,
           "owner palette must initialize");
    const auto owner_payload = battlespades::network::encode_packet(build);
    result = replica.apply(owner_payload);
    expect(result.recognized && result.mutation.accepted &&
               map.solid(31U, 47U, 100U),
           "BlockBuild(32) owner echo must create the authoritative voxel");
    expect(confirmed_owner_block_cost(owner_payload, result, 7U) == 1U &&
               confirmed_owner_block_cost(owner_payload, result, 8U) == 0U,
           "one accepted owner BlockBuild must cost exactly one block");

    // Competitive prefab packet 30 can expand the voxel before its packet-32
    // owner acknowledgement arrives. The latter still represents one server
    // wallet debit even though the voxel already exists locally.
    build.position = {35, 47, 100};
    expect(map.set_voxel(35U, 47U, 100U,
                         VxlColor{0x21U, 0x43U, 0x65U, 255U}),
           "prefab pre-expansion fixture must materialize its voxel");
    const auto settled_prefab_owner =
        battlespades::network::encode_packet(build);
    result = replica.apply(settled_prefab_owner);
    expect(result.recognized && result.error.empty() &&
               map.solid(35U, 47U, 100U) &&
               confirmed_owner_block_cost(settled_prefab_owner, result, 7U) == 1U,
           "prefab owner acknowledgement must debit after packet-30 expansion");
    const auto placed = map.color(31U, 47U, 100U);
    expect(placed.has_value() && placed->red == 0x21U &&
               placed->green == 0x43U && placed->blue == 0x65U,
           "BlockBuild(32) must resolve RGB through the sender palette");

    const std::array expected_prefab{
        battlespades::network::ColoredTerrainCell{
            {33U, 47U, 100U}, {0x70U, 0x50U, 0x30U, 255U}}};
    replica.expect_owner_build_colors(21, 7U, expected_prefab);
    build.loop_count = 22;
    build.position = {34, 47, 100};
    result = replica.apply(battlespades::network::encode_packet(build));
    const auto unrelated = map.color(34U, 47U, 100U);
    expect(unrelated.has_value() && unrelated->red == 0x21U &&
               unrelated->green == 0x43U && unrelated->blue == 0x65U,
           "an unrelated owner build must retain the current palette");
    build.loop_count = 21;
    build.position = {33, 47, 100};
    result = replica.apply(battlespades::network::encode_packet(build));
    const auto prefab = map.color(33U, 47U, 100U);
    expect(result.mutation.accepted && prefab.has_value() &&
               prefab->red == 0x70U && prefab->green == 0x50U &&
               prefab->blue == 0x30U,
           "prefab owner echo must consume its loop/cell-specific blended RGB");

    PaintBlockPacket paint;
    paint.loop_count = 20;
    paint.position = {31, 47, 100};
    paint.color = 0xC08040U;
    result = replica.apply(
        battlespades::network::encode_packet(paint));
    const auto painted = map.color(31U, 47U, 100U);
    expect(result.recognized && result.mutation.accepted &&
               painted.has_value() && painted->red == 0xC0U &&
               painted->green == 0x80U && painted->blue == 0x40U,
           "PaintBlock(7) echo must recolor an existing voxel");

    paint.position = {32, 47, 100};
    result = replica.apply(
        battlespades::network::encode_packet(paint));
    expect(result.recognized && !result.mutation.accepted &&
               !map.solid(32U, 47U, 100U),
           "a late paint echo must not resurrect an empty voxel");
}

void malformed_packets_fail_closed() {
    const std::array<std::byte, 2U> short_damage{
        std::byte{DamagePacket::id}, std::byte{1U}};
    expect(!decode_terrain_packet(short_damage), "truncated Damage must fail closed");
    const std::array<std::byte, 1U> unknown{std::byte{0xFFU}};
    expect(!decode_terrain_packet(unknown), "unknown packet must not be misdecoded");
}

} // namespace

int main() {
    try {
        damage_golden_vector_matches_python_protocol_oracle();
        colored_build_and_block_line_round_trip();
        block_line_matches_native_cube_line_oracle();
        terrain_packets_share_damage_and_collapse_world_path();
        native_damage_shapes_match_server_and_retail_block_manager();
        live_damage_produces_one_drainable_impact_event();
        drill_damage_is_a_bore_tick_not_a_terminal_explosion();
        melee_damage_ids_translate_to_canonical_tools();
        stateful_replica_orders_palette_mutation_and_chunk_invalidation();
        owner_build_and_paint_echoes_mutate_the_canonical_map();
        malformed_packets_fail_closed();
        std::cout << "Protocol 168 terrain parity tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
