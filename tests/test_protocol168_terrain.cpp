#include "battlespades/network/protocol168_terrain.hpp"
#include "battlespades/network/protocol168_tool_actions.hpp"
#include "battlespades/world/vxl_map.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

using battlespades::network::BlockBuildColoredPacket;
using battlespades::network::BlockBuildPacket;
using battlespades::network::BlockLinePacket;
using battlespades::network::BlockManagerStatePacket;
using battlespades::network::BuildPrefabActionPacket;
using battlespades::network::ColoredTerrainCell;
using battlespades::network::DamageFootprintCell;
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
using battlespades::network::is_block_granting_damage;
using battlespades::network::retail_damage_footprint;
using battlespades::world::BlockDamageOutcome;
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

[[nodiscard]] std::vector<std::byte> hex_bytes(const std::string& hex) {
    std::vector<std::byte> bytes;
    for (std::size_t index{}; index + 1U < hex.size(); index += 2U) {
        bytes.push_back(static_cast<std::byte>(std::stoi(hex.substr(index, 2U), nullptr, 16)));
    }
    return bytes;
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

    // The damage byte is UNSIGNED quarters: kills use 31.75, and anything up
    // to 63.75 must never decode negative. Encoding rounds to nearest.
    packet.damage = 63.75F;
    auto big = encode_packet(packet);
    expect(std::to_integer<std::uint8_t>(big[3U]) == 0xFFU,
           "Damage(37) must encode 63.75 as byte 255");
    expect(std::get<DamagePacket>(*decode_terrain_packet(big).packet).damage == 63.75F,
           "Damage(37) must decode byte 255 as +63.75, not a negative amount");
    packet.damage = 0.7F;
    expect(std::to_integer<std::uint8_t>(encode_packet(packet)[3U]) == 3U,
           "Damage(37) amount must round to the nearest quarter (0.7 -> 0.75)");
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
    expect(map.initial_health(103U, 102U, 101U) == 9.0F,
           "BlockLine cells are retail user blocks at DEFAULT_PREFAB_HEALTH 9");
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
    expect(map.initial_health(100U, 100U, 100U) == 3.0F,
           "BlockBuildColored(33) must add a user block at 3.0 (live 2026-09-26)");

    DamagePacket damage;
    damage.position = {100.0F, 100.0F, 100.0F};
    damage.damage = 2.0F;
    auto result = apply_direct_damage(map, damage);
    expect(result.accepted && !result.destroyed && map.solid(100U, 100U, 100U),
           "sublethal Damage(37) must retain collision");
    const auto damaged = map.damaged_block(100U, 100U, 100U);
    expect(damaged.has_value() && damaged->health == 1.0F &&
               damaged->original_color == VxlColor{0x78U, 0x5AU, 0x3CU, 255U},
           "sublethal Damage(37) must record DamagedBlock(remaining, first colour)");
    // server block_damage_model.dim_rgb(0x785A3C, 2) == 0x5A442D.
    const auto dimmed = map.color(100U, 100U, 100U);
    expect(dimmed.has_value() && dimmed->red == 0x5AU && dimmed->green == 0x44U &&
               dimmed->blue == 0x2DU,
           "sublethal Damage(37) must darken with shared.common.dim");

    damage.damage = 1.0F;
    damage.chunk_check = true;
    result = apply_direct_damage(map, damage);
    expect(result.destroyed && result.destroyed_cells == 1U && !map.solid(100U, 100U, 100U),
           "lethal Damage(37) must remove canonical collision");
}

void retail_dim_matches_shared_common_dim() {
    using battlespades::world::retail_dim;
    // server block_damage_model.dim_rgb(0xFF8001, 4.75) == 0x603001.
    expect(retail_dim(VxlColor{0xFFU, 0x80U, 0x01U, 7U}, 4.75F) ==
               VxlColor{0x60U, 0x30U, 0x01U, 7U},
           "dim must use Python 2 round() and keep alpha");
    expect(retail_dim(std::uint8_t{200U}, 0.25F) == 200U &&
               retail_dim(std::uint8_t{200U}, 0.5F) == 175U &&
               retail_dim(std::uint8_t{200U}, 1.0F) == 175U,
           "round(0.25)=0 leaves the colour, round(0.5)=1 takes 1/8");
    expect(retail_dim(std::uint8_t{255U}, 9.0F) == 0U,
           "dim must clamp at zero past 8 damage");

    // Darkening compounds on the current colour, once per surviving hit.
    auto map = empty_world();
    expect(map.set_voxel(50U, 50U, 50U, VxlColor{200U, 160U, 80U, 255U}),
           "dim fixture must place");
    expect(map.add_damage(50U, 50U, 50U, 1.0F) == BlockDamageOutcome::damaged &&
               map.add_damage(50U, 50U, 50U, 1.0F) == BlockDamageOutcome::damaged,
           "two bullets must leave a 5-health map voxel standing");
    const auto twice = map.color(50U, 50U, 50U);
    expect(twice.has_value() && twice->red == 154U && twice->green == 123U &&
               twice->blue == 62U,
           "two 1-damage hits must compound dim (200->175->154)");
    expect(map.damaged_block(50U, 50U, 50U)->original_color ==
               VxlColor{200U, 160U, 80U, 255U},
           "the DamagedBlock must keep the colour of the first hit");
}

void footprints_match_live_client_capture_fixture() {
    std::ifstream input{AOS_BLOCK_DAMAGE_FOOTPRINT_FIXTURE};
    expect(static_cast<bool>(input), "footprint fixture must be readable");
    const auto fixture = nlohmann::json::parse(input);
    const auto seed = fixture.at("seed").get<int>();
    const auto amount = fixture.at("amount").get<float>();
    const auto half = fixture.at("cube_half").get<int>();
    std::size_t compared{};
    for (const auto& [type_name, rows] : fixture.at("cells").items()) {
        const auto type = static_cast<std::uint8_t>(std::stoi(type_name));
        std::vector<DamageFootprintCell> predicted;
        for (const auto& cell : retail_damage_footprint(
                 type, {0.0F, 0.0F, 0.0F}, amount, static_cast<std::uint8_t>(seed))) {
            if (std::abs(cell.x) <= half && std::abs(cell.y) <= half &&
                std::abs(cell.z) <= half) {
                predicted.push_back(cell);
            }
        }
        expect(predicted.size() == rows.size(),
               ("footprint cell count differs for damage type " + type_name).c_str());
        for (std::size_t index{}; index < rows.size(); ++index) {
            const auto& row = rows[index];
            const auto& cell = predicted[index];
            expect(cell.x == row[0].get<int>() && cell.y == row[1].get<int>() &&
                       cell.z == row[2].get<int>() && cell.damage == row[3].get<float>(),
                   ("footprint cell differs from the live client for type " + type_name)
                       .c_str());
            ++compared;
        }
    }
    expect(compared > 2000U, "the live fixture must cover every captured cell");

    // Server block_damage_model.footprint(8, (100.6, 200.4, 50.5), 7.25, 200):
    // fractional centre floor(p + 0.5) = (101, 200, 51), R = 4, 251 cells.
    const auto rocket = retail_damage_footprint(8U, {100.6F, 200.4F, 50.5F}, 7.25F, 200U);
    expect(rocket.size() == 251U, "ROCKET_DAMAGE must be the 251-cell R4 sphere");
    expect(rocket[0U] == DamageFootprintCell{99, 199, 48, 1.0F} &&
               rocket[1U] == DamageFootprintCell{99, 200, 48, 2.0F} &&
               rocket[2U] == DamageFootprintCell{99, 201, 48, 2.5F} &&
               rocket[10U] == DamageFootprintCell{101, 200, 48, 3.75F} &&
               rocket[100U] == DamageFootprintCell{104, 200, 50, 4.5F} &&
               rocket[200U] == DamageFootprintCell{99, 202, 53, 2.5F} &&
               rocket.back() == DamageFootprintCell{103, 201, 54, 2.0F},
           "fractional rocket footprint must match the server model exactly");

    struct Radius final {
        std::uint8_t type;
        std::int32_t radius;
    };
    for (const auto entry : {Radius{22U, 2}, Radius{23U, 2}, Radius{11U, 3}, Radius{15U, 3},
                             Radius{21U, 3}, Radius{40U, 3}, Radius{7U, 4}, Radius{24U, 4},
                             Radius{37U, 4}, Radius{39U, 5}, Radius{9U, 6}, Radius{18U, 6},
                             Radius{19U, 7}, Radius{16U, 8}, Radius{41U, 8}}) {
        std::size_t expected{};
        for (std::int32_t dx{-entry.radius}; dx <= entry.radius; ++dx)
            for (std::int32_t dy{-entry.radius}; dy <= entry.radius; ++dy)
                for (std::int32_t dz{-entry.radius}; dz <= entry.radius; ++dz)
                    if (dx * dx + dy * dy + dz * dz < entry.radius * entry.radius) ++expected;
        expect(retail_damage_footprint(entry.type, {10.0F, 10.0F, 10.0F}, 5.0F, 3U).size() ==
                   expected,
               "every sphere type must use its live-fitted radius");
    }
    for (const auto none : std::array<std::uint8_t, 3U>{20U, 27U, 32U}) {
        expect(retail_damage_footprint(none, {10.0F, 10.0F, 10.0F}, 5.0F, 3U).empty(),
               "snowball and MG damage types have no terrain footprint");
    }
    const auto classic = retail_damage_footprint(4U, {10.0F, 10.0F, 10.0F}, 5.0F, 3U);
    const auto classic_rmb = retail_damage_footprint(5U, {10.0F, 10.0F, 10.0F}, 5.0F, 3U);
    const auto shield = retail_damage_footprint(36U, {10.0F, 10.0F, 10.0F}, 5.0F, 3U);
    expect(classic.size() == 1U && classic_rmb.size() == 3U && shield.size() == 3U,
           "classic spade LMB is one cell; classic RMB and riot shield are z columns");
}

void native_damage_shapes_apply_per_cell_health() {
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
    expect(result.destroyed && result.changed_cells.size() == 3U &&
               result.destroyed_cells == 3U,
           "SPADE_DAMAGE must expand to the native three-cell z column");

    const auto fill = [&](std::int32_t half, std::array<std::int32_t, 3U> centre) {
        auto filled = empty_world();
        for (std::int32_t dx{-half}; dx <= half; ++dx)
            for (std::int32_t dy{-half}; dy <= half; ++dy)
                for (std::int32_t dz{-half}; dz <= half; ++dz)
                    static_cast<void>(filled.set_voxel(
                        static_cast<std::uint32_t>(centre[0U] + dx),
                        static_cast<std::uint32_t>(centre[1U] + dy),
                        static_cast<std::uint32_t>(centre[2U] + dz), color));
        return filled;
    };

    map = fill(1, {200, 200, 100});
    damage.type = 17U;
    result = apply_expanded_damage(map, damage);
    expect(result.changed_cells.size() == 27U,
           "ZOMBIE_DAMAGE must use the 3x3x3 cube handler");

    map = fill(3, {200, 200, 100});
    damage.type = 10U;
    result = apply_expanded_damage(map, damage);
    expect(result.changed_cells.size() == 93U,
           "DRILL_DAMAGE is the live-fitted R3 sphere (93 cells), not a flat bore");

    // Every type accepts a fractional centre: floor(p + 0.5).
    map = fill(3, {201, 201, 101});
    damage.type = 15U;
    damage.position = {200.5F, 200.5F, 100.5F};
    result = apply_expanded_damage(map, damage);
    expect(result.changed_cells.size() == 93U && !map.solid(201U, 201U, 101U),
           "LANDMINE_DAMAGE must use R3 around floor(p + 0.5)");

    map = fill(3, {101, 100, 100});
    damage.type = 7U;
    damage.position = {100.6F, 100.4F, 99.5F};
    result = apply_expanded_damage(map, damage);
    expect(result.accepted && !map.solid(101U, 100U, 100U),
           "a grenade's fractional centre must create a local crater, not be dropped");

    // A built block (9.0) survives the damage that breaks a map voxel (5.0).
    map = empty_world();
    expect(map.set_voxel(10U, 10U, 10U, color) &&
               map.add_user_block(11U, 10U, 10U, color, VxlMap::prefab_block_health),
           "health fixture must place a map voxel and a built block");
    damage.type = 6U;
    damage.damage = 5.0F;
    damage.position = {10.0F, 10.0F, 10.0F};
    expect(apply_expanded_damage(map, damage).destroyed, "5 damage breaks a map voxel");
    damage.position = {11.0F, 10.0F, 10.0F};
    expect(!apply_expanded_damage(map, damage).destroyed && map.solid(11U, 10U, 10U),
           "5 damage must not break a 9-health built block");
    damage.damage = 4.0F;
    expect(apply_expanded_damage(map, damage).destroyed,
           "a built block breaks when its accumulated damage reaches 9");

    // The bed (z > 238) is never damaged.
    damage.damage = 30.0F;
    damage.position = {10.0F, 10.0F, 239.0F};
    expect(!apply_expanded_damage(map, damage).accepted && map.solid(10U, 10U, 239U),
           "Damage(37) must never touch z=239");
}

void classic_and_ugc_user_block_health_follow_add_user_block() {
    const VxlColor color{120U, 90U, 60U, 255U};
    auto classic = empty_world();
    Protocol168TerrainReplica classic_replica{classic, 1.0F, true, false};
    expect(classic.add_user_block(20U, 20U, 20U, color, VxlMap::prefab_block_health) &&
               classic.initial_health(20U, 20U, 20U) == VxlMap::default_block_health,
           "classic mode: every user block gets DEFAULT_BLOCK_HEALTH (5), not 9");
    expect(classic.add_user_block(21U, 20U, 20U, color, VxlMap::snow_block_health) &&
               classic.initial_health(21U, 20U, 20U) == 5.0F,
           "classic mode overrides the snow health too");

    auto doubled = empty_world();
    Protocol168TerrainReplica doubled_replica{doubled, 2.0F, true, false};
    expect(doubled.add_user_block(20U, 20U, 20U, color, VxlMap::prefab_block_health) &&
               doubled.initial_health(20U, 20U, 20U) == 10.0F,
           "the health multiplier still applies after the classic override");

    auto ugc = empty_world();
    Protocol168TerrainReplica ugc_replica{ugc, 1.0F, false, true};
    expect(ugc.add_user_block(20U, 20U, 20U, color, VxlMap::prefab_block_health) &&
               ugc.solid(20U, 20U, 20U) && !ugc.user_block_health(20U, 20U, 20U).has_value() &&
               ugc.initial_health(20U, 20U, 20U) == VxlMap::default_block_health,
           "UGC mode drops the user_blocks entry");

    auto modern = empty_world();
    Protocol168TerrainReplica modern_replica{modern};
    expect(modern.add_user_block(20U, 20U, 20U, color, VxlMap::prefab_block_health) &&
               modern.initial_health(20U, 20U, 20U) == 9.0F,
           "modern modes keep the caller's DEFAULT_PREFAB_HEALTH");
}

void health_multiplier_scales_initial_health() {
    auto map = empty_world();
    const VxlColor color{120U, 90U, 60U, 255U};
    expect(map.set_voxel(10U, 10U, 10U, color), "multiplier fixture must place");
    Protocol168TerrainReplica replica{map, 2.0F};
    expect(map.initial_health(10U, 10U, 10U) == 10.0F,
           "RULE_BLOCK_HEALTH 2.0 must double DEFAULT_BLOCK_HEALTH");
    DamagePacket damage;
    damage.type = 6U;
    damage.damage = 9.0F;
    damage.position = {10.0F, 10.0F, 10.0F};
    expect(!replica.apply(encode_packet(damage)).mutation.destroyed,
           "9 damage must not break a doubled map voxel");
    damage.damage = 1.0F;
    expect(replica.apply(encode_packet(damage)).mutation.destroyed,
           "the doubled voxel breaks at 10");
}

void block_manager_state_matches_server_encoder_and_merges() {
    // BS server/prefab_actions.encode_block_manager_state(
    //   [(1,2,3,9.0), (300,-1,238,3.0)],
    //   [(7,8,9,4.5,(10,20,30)), (511,511,238,0.25,(255,128,1))]).
    const auto wire = hex_bytes(
        "2602000000070008000900121e140aff01ff01ee00010180ff0200000001000200030024"
        "2c01ffffee000c00000000");
    const auto decoded = decode_terrain_packet(wire);
    expect(static_cast<bool>(decoded), decoded.error.c_str());
    const auto& state = std::get<BlockManagerStatePacket>(*decoded.packet);
    expect(state.damaged.size() == 2U && state.user.size() == 2U && state.occupied.empty(),
           "BlockManagerState(38) must decode all three tables");
    expect(state.damaged[0U] ==
                   BlockManagerStatePacket::DamagedRow{7, 8, 9, 4.5F, 0x0A141EU} &&
               state.damaged[1U] ==
                   BlockManagerStatePacket::DamagedRow{511, 511, 238, 0.25F, 0xFF8001U},
           "damaged rows are i16 xyz, u8 remaining*4, then B, G, R");
    expect(state.user[0U] == BlockManagerStatePacket::UserRow{1, 2, 3, 9.0F} &&
               state.user[1U] == BlockManagerStatePacket::UserRow{300, -1, 238, 3.0F},
           "user rows are i16 xyz, u8 health*4");
    const auto round_trip = encode_packet(state);
    expect(round_trip == wire, "BlockManagerState(38) must re-encode byte-for-byte");

    auto truncated = wire;
    truncated.pop_back();
    expect(!decode_terrain_packet(truncated), "truncated BlockManagerState must fail closed");
    auto huge = wire;
    huge[1U] = std::byte{0xFFU};
    huge[4U] = std::byte{0x7FU};
    expect(!decode_terrain_packet(huge), "an impossible row count must fail closed");

    // Merge: user rows first (earlier packet), then a damaged row darkens its
    // original colour once by initial - remaining.
    auto map = empty_world();
    expect(map.set_voxel(20U, 20U, 20U, VxlColor{0x10U, 0x10U, 0x10U, 255U}),
           "merge fixture must place");
    Protocol168TerrainReplica replica{map};
    BlockManagerStatePacket users;
    users.user.push_back({20, 20, 20, 9.0F});
    expect(replica.apply(encode_packet(users)).mutation.accepted &&
               map.initial_health(20U, 20U, 20U) == 9.0F,
           "a user row must set the cell's initial health");
    BlockManagerStatePacket damaged;
    damaged.damaged.push_back({20, 20, 20, 7.0F, 0x406080U});
    const auto merged = replica.apply(encode_packet(damaged));
    expect(merged.mutation.accepted && !replica.take_dirty_chunks().empty(),
           "a damaged row must invalidate its chunk");
    const auto shade = map.color(20U, 20U, 20U);
    // dim((0x40,0x60,0x80), 9 - 7 = 2) = (0x30, 0x48, 0x60).
    expect(shade.has_value() && shade->red == 0x30U && shade->green == 0x48U &&
               shade->blue == 0x60U,
           "a damaged row must darken its ORIGINAL colour by initial - remaining");
    DamagePacket hit;
    hit.type = 6U;
    hit.damage = 6.75F;
    hit.position = {20.0F, 20.0F, 20.0F};
    expect(!replica.apply(encode_packet(hit)).mutation.destroyed,
           "the merged remaining health (7.0) must survive 6.75");
    hit.damage = 0.25F;
    expect(replica.apply(encode_packet(hit)).mutation.destroyed,
           "and break exactly when it reaches zero");
}

void colored_build_and_paint_keep_block_damage() {
    auto map = empty_world();
    Protocol168TerrainReplica replica{map};
    const VxlColor stone{100U, 100U, 100U, 255U};
    expect(map.set_voxel(30U, 30U, 30U, stone), "fixture must place");
    DamagePacket hit;
    hit.type = 6U;
    hit.damage = 2.0F;
    hit.position = {30.0F, 30.0F, 30.0F};
    static_cast<void>(replica.apply(encode_packet(hit)));

    BlockBuildColoredPacket repair;
    repair.x = 30;
    repair.y = 30;
    repair.z = 30;
    repair.color = 0x112233U;
    const auto ignored = replica.apply(encode_packet(repair));
    expect(!ignored.mutation.accepted && map.damaged_block(30U, 30U, 30U).has_value() &&
               map.damaged_block(30U, 30U, 30U)->health == 3.0F &&
               map.color(30U, 30U, 30U) != VxlColor{0x11U, 0x22U, 0x33U, 255U},
           "BlockBuildColored(33) onto a solid voxel is ignored and keeps its damage");

    PaintBlockPacket paint;
    paint.position = {30, 30, 30};
    paint.color = 0xC08040U;
    const auto painted = replica.apply(battlespades::network::encode_packet(paint));
    const auto shown = map.color(30U, 30U, 30U);
    expect(painted.mutation.accepted && shown.has_value() && shown->red == 0xC0U &&
               shown->green == 0x80U && shown->blue == 0x40U,
           "PaintBlock(7) must set the paint colour undarkened");
    expect(map.damaged_block(30U, 30U, 30U).has_value() &&
               map.damaged_block(30U, 30U, 30U)->health == 3.0F &&
               map.damaged_block(30U, 30U, 30U)->original_color == stone,
           "PaintBlock(7) must keep the DamagedBlock health and original colour");
    hit.damage = 3.0F;
    expect(replica.apply(encode_packet(hit)).mutation.destroyed,
           "a painted damaged block still breaks at its remaining health");
}

void competitive_prefab_expansion_replaces_solids_at_prefab_health() {
    auto map = empty_world();
    Protocol168TerrainReplica replica{map};
    const VxlColor stone{10U, 10U, 10U, 255U};
    expect(map.set_voxel(60U, 60U, 60U, stone), "prefab overlap fixture must place");
    DamagePacket hit;
    hit.type = 6U;
    hit.damage = 2.0F;
    hit.position = {60.0F, 60.0F, 60.0F};
    static_cast<void>(replica.apply(encode_packet(hit)));
    const std::array cells{
        ColoredTerrainCell{{60U, 60U, 60U}, {200U, 100U, 50U, 255U}},
        ColoredTerrainCell{{61U, 60U, 60U}, {200U, 100U, 50U, 255U}},
        ColoredTerrainCell{{62U, 60U, 239U}, {200U, 100U, 50U, 255U}}};
    const auto result = replica.apply_prefab_user_blocks(cells);
    expect(result.mutation.accepted && result.mutation.user_blocks_added == 2U,
           "every in-volume model voxel is added; the bed is not modifiable");
    expect(map.color(60U, 60U, 60U) == VxlColor{200U, 100U, 50U, 255U} &&
               !map.damaged_block(60U, 60U, 60U).has_value() &&
               map.initial_health(60U, 60U, 60U) == 9.0F &&
               map.initial_health(61U, 60U, 60U) == 9.0F,
           "replace_solids overwrites the solid, clears its damage and sets health 9");

    BuildPrefabActionPacket packet;
    packet.prefab_name = "superminibunker";
    packet.player_id = 4U;
    packet.add_to_user_blocks = true;
    const auto payload = battlespades::network::encode_packet(packet);
    battlespades::network::TerrainReplicaResult echoed;
    echoed.recognized = true;
    echoed.mutation.user_blocks_added = 68U;
    expect(confirmed_owner_block_cost(payload, echoed, 4U) == 68U &&
               confirmed_owner_block_cost(payload, echoed, 5U) == 0U,
           "the owner is debited once per model voxel; observers are not");
    packet.add_to_user_blocks = false;
    expect(confirmed_owner_block_cost(battlespades::network::encode_packet(packet), echoed,
                                      4U) == 0U,
           "UGC place_prefab_in_world never spends the block wallet");
}

void digging_credits_block_granting_damage() {
    for (const auto type : std::array<std::uint8_t, 13U>{0U, 1U, 2U, 3U, 4U, 17U, 26U, 28U,
                                                         29U, 34U, 35U, 36U, 42U}) {
        expect(is_block_granting_damage(type), "BLOCK_GRANTING_DAMAGES member");
    }
    for (const auto type : std::array<std::uint8_t, 9U>{5U, 6U, 7U, 10U, 16U, 21U, 31U, 41U, 43U}) {
        expect(!is_block_granting_damage(type), "not a BLOCK_GRANTING_DAMAGES member");
    }
    auto map = empty_world();
    for (std::uint32_t z{99U}; z <= 100U; ++z) {
        static_cast<void>(map.set_voxel(5U, 5U, z, {90U, 80U, 70U, 255U}));
    }
    DamagePacket spade;
    spade.type = 2U;
    spade.damage = 5.0F;
    spade.position = {5.0F, 5.0F, 100.0F};
    const auto result = apply_expanded_damage(map, spade);
    expect(result.destroyed_cells == 2U,
           "a spade column destroying two solid cells grants exactly two blocks");
}

void chroma_markers_follow_vxl_pyd_cleanup() {
    // Columns: source (x, y) in y-major order, 512 x 512.
    std::vector<std::vector<std::uint8_t>> columns(
        static_cast<std::size_t>(VxlMap::width) * VxlMap::depth,
        std::vector<std::uint8_t>{0U, 1U, 0U, 0U});
    const auto column = [&](std::uint32_t x, std::uint32_t y, std::uint8_t top,
                            std::vector<std::uint32_t> colors) {
        std::vector<std::uint8_t> bytes{0U, top,
                                        static_cast<std::uint8_t>(top + colors.size() - 1U), 0U};
        for (const auto value : colors) {
            for (std::uint32_t shift{}; shift < 32U; shift += 8U) {
                bytes.push_back(static_cast<std::uint8_t>(value >> shift));
            }
        }
        columns[x + y * VxlMap::width] = std::move(bytes);
    };
    constexpr std::uint32_t green{0x7F01FF02U};  // & F0F0F0 == 0x00F000
    constexpr std::uint32_t blue{0x7F0A0BFAU};   // & F0F0F0 == 0x0000F0
    constexpr std::uint32_t brown{0x7F604020U};
    constexpr std::uint32_t team_art{0x7F0028BEU};
    column(511U, 511U, 239U, {brown});  // pins source_z_shift to zero
    column(10U, 10U, 100U, {green, brown});  // exposed marker
    column(10U, 11U, 101U, {0x7F112233U});  // +y neighbour at z=101
    column(20U, 20U, 99U, {brown, blue});   // embedded: z-1 is solid
    column(30U, 30U, 100U, {team_art, brown});
    column(40U, 40U, 100U, {blue, brown});  // exposed blue, no neighbours
    std::vector<std::byte> bytes;
    for (const auto& entry : columns) {
        for (const auto value : entry) bytes.push_back(static_cast<std::byte>(value));
    }
    const auto loaded = VxlMap::load(bytes);
    expect(static_cast<bool>(loaded), loaded.error.c_str());
    const auto& map = *loaded.map;
    expect(map.source_z_shift() == 0U, "marker fixture must not shift");
    expect(!map.solid(10U, 10U, 100U), "an exposed green marker must be removed");
    const auto below = map.color(10U, 10U, 101U);
    expect(below.has_value() && below->red == 0x11U && below->green == 0x22U &&
               below->blue == 0x33U,
           "the exposed voxel below takes the +y neighbour's colour");
    expect(map.solid(20U, 20U, 100U) && map.color(20U, 20U, 100U)->blue == 0xFAU,
           "an embedded marker (solid above) must stay");
    expect(map.solid(30U, 30U, 100U) && map.color(30U, 30U, 100U)->blue == 0xBEU,
           "team-art #0028BE is not a marker");
    expect(!map.solid(40U, 40U, 100U) && map.solid(40U, 40U, 101U),
           "an exposed blue marker must be removed without touching the voxel below");
    expect(map.color(40U, 40U, 101U)->red == 0x60U,
           "without a solid neighbour the exposed voxel keeps its colour");
    expect(map.revision() == 0U, "the load-time cleanup is part of revision zero");
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

void legacy_turret_rocket_removes_support_and_invalidates_visible_chunks() {
    auto map = empty_world();
    expect(map.set_voxel(15U, 31U, 100U, {90U, 100U, 110U, 255U}),
           "turret fixture must contain a support voxel on two chunk borders");
    const battlespades::world::ChunkMesher mesher;
    const auto visible_before = mesher.mesh(map, {0U, 1U}).vertices.size();
    Protocol168TerrainReplica replica{map};
    DamagePacket damage;
    damage.player_id = 7U;
    damage.causer_id = 180;
    damage.type = 21U; // ROCKET_TURRET_ROCKET_DAMAGE, not direct WEAPON_DAMAGE.
    damage.damage = 10.0F;
    damage.position = {15.25F, 31.5F, 100.75F};
    const auto result = replica.apply(encode_packet(damage));
    expect(result.recognized && result.error.empty() && result.mutation.destroyed,
           "legacy turret damage must accept a fractional rocket impact position");
    expect(!map.solid(15U, 31U, 100U),
           "the legacy turret blast must remove the player's canonical support voxel");
    expect(mesher.mesh(map, {0U, 1U}).vertices.size() < visible_before,
           "the destroyed support must also disappear from the rebuilt visible mesh");
    const auto dirty = replica.take_dirty_chunks();
    for (const auto key : {battlespades::world::ChunkKey{0U, 1U},
                           battlespades::world::ChunkKey{1U, 1U},
                           battlespades::world::ChunkKey{0U, 2U}}) {
        expect(std::find(dirty.begin(), dirty.end(), key) != dirty.end(),
               "turret destruction must invalidate both terrain and neighboring faces");
    }
    expect(replica.take_dirty_chunks().empty(),
           "turret remesh invalidation must drain once");
    expect(replica.take_impact_events().empty(),
           "turret terrain damage must not duplicate DestroyEntity explosion effects");
}

void legacy_turret_blast_matches_recovered_python2_seed_fixtures() {
    // Independent CPython 2.7 reconstruction of gameScene.pyd's radius-list
    // generator and damage loop: D=10, block health=5, radius=3, 93 candidates.
    struct Fixture { std::uint8_t seed; std::size_t destroyed; };
    for (const auto fixture : {Fixture{0U, 65U}, Fixture{1U, 58U},
                                Fixture{123U, 56U}, Fixture{255U, 57U}}) {
        auto map = empty_world();
        for (std::uint32_t x{97U}; x <= 103U; ++x)
            for (std::uint32_t y{97U}; y <= 103U; ++y)
                for (std::uint32_t z{97U}; z <= 103U; ++z)
                    static_cast<void>(map.set_voxel(x, y, z, {80U, 90U, 100U, 255U}));
        DamagePacket packet;
        packet.type = 21U;
        packet.damage = 10.0F;
        packet.seed = fixture.seed;
        // floor(99.5 + 0.5) = 100, as Python 2 round() also gives.
        packet.position = {99.5F, 99.75F, 100.25F};
        Protocol168TerrainReplica replica{map};
        const auto result = replica.apply(encode_packet(packet));
        expect(result.mutation.changed_cells.size() == 93U,
               "the retail turret radius-three list must contain exactly 93 cells");
        std::size_t destroyed{};
        for (const auto& cell : result.mutation.changed_cells)
            destroyed += map.solid(cell.x, cell.y, cell.z) ? 0U : 1U;
        expect(destroyed == fixture.destroyed && result.mutation.destroyed_cells == destroyed,
               "turret destruction must match CPython2 seed-dependent falloff fixtures");
        for (const auto cell : {battlespades::world::VoxelCell{97U, 100U, 100U},
                                battlespades::world::VoxelCell{103U, 100U, 100U},
                                battlespades::world::VoxelCell{100U, 97U, 100U},
                                battlespades::world::VoxelCell{100U, 100U, 103U}}) {
            expect(map.solid(cell.x, cell.y, cell.z) &&
                       !map.damaged_block(cell.x, cell.y, cell.z).has_value(),
                   "cells on/outside the strict blast boundary must remain untouched");
        }
        if (fixture.seed == 0U) {
            expect(map.damaged_block(98U, 100U, 98U)->health == 2.0F &&
                       !map.solid(99U, 99U, 98U) && !map.solid(99U, 100U, 98U) &&
                       map.damaged_block(99U, 101U, 98U)->health == 1.0F &&
                       map.damaged_block(100U, 98U, 98U)->health == 2.75F,
                   "turret RNG ordering and quarter-damage rounding must match the original");
        }
    }
}

void turret_rng_does_not_skip_air_or_out_of_map_candidates() {
    auto full = empty_world();
    auto sparse = empty_world();
    const VxlColor stone{80U, 90U, 100U, 255U};
    for (std::uint32_t x{0U}; x <= 4U; ++x)
        for (std::uint32_t y{0U}; y <= 4U; ++y)
            for (std::uint32_t z{0U}; z <= 4U; ++z)
                static_cast<void>(full.set_voxel(x, y, z, stone));
    // The final radius-list offset is (2,0,2), after both air and invalid
    // negative offsets. Seed 0 gives it 2.25 damage, independent of solids.
    static_cast<void>(sparse.set_voxel(2U, 0U, 2U, stone));
    DamagePacket packet;
    packet.type = 21U;
    packet.damage = 10.0F;
    packet.seed = 0U;
    packet.position = {0.0F, 0.0F, 0.0F};
    const auto full_result = apply_expanded_damage(full, packet);
    const auto sparse_result = apply_expanded_damage(sparse, packet);
    expect(full_result.accepted && sparse_result.accepted &&
               full.damaged_block(2U, 0U, 2U)->health == 2.75F &&
               sparse.damaged_block(2U, 0U, 2U)->health == 2.75F,
           "empty/out-of-map offsets must still consume the original random stream");
    static_cast<void>(apply_expanded_damage(sparse, packet));
    expect(sparse.solid(2U, 0U, 2U) && sparse.damaged_block(2U, 0U, 2U)->health == 0.5F,
           "outer blast damage must accumulate without prematurely deleting terrain");
    static_cast<void>(apply_expanded_damage(sparse, packet));
    expect(!sparse.solid(2U, 0U, 2U),
           "successive turret blasts must eventually destroy weakened outer terrain");
}

void turret_blast_centres_can_straddle_map_edges_but_never_break_bedrock() {
    auto map = empty_world();
    const VxlColor stone{80U, 90U, 100U, 255U};
    static_cast<void>(map.set_voxel(0U, 100U, 100U, stone));
    static_cast<void>(map.set_voxel(VxlMap::width - 1U, 100U, 100U, stone));
    static_cast<void>(map.set_voxel(100U, 100U, VxlMap::height - 1U, stone));
    DamagePacket packet;
    packet.type = 21U;
    packet.damage = 10.0F;
    packet.position = {-0.25F, 100.0F, 100.0F};
    expect(apply_expanded_damage(map, packet).destroyed && !map.solid(0U, 100U, 100U),
           "a slightly negative rocket centre must still damage the valid map edge");
    packet.position[0U] = static_cast<float>(VxlMap::width);
    expect(apply_expanded_damage(map, packet).destroyed &&
               !map.solid(VxlMap::width - 1U, 100U, 100U),
           "a rocket centred beyond the positive edge must clip its per-cell stencil");
    packet.position = {100.0F, 100.0F, static_cast<float>(VxlMap::height - 1U)};
    static_cast<void>(apply_expanded_damage(map, packet));
    expect(map.solid(100U, 100U, VxlMap::height - 1U),
           "legacy turret damage must preserve the unmodifiable bottom layer");
    const auto revision = map.revision();
    for (const float invalid : {std::numeric_limits<float>::quiet_NaN(),
                                std::numeric_limits<float>::infinity(), -1.0e30F}) {
        packet.position[0U] = invalid;
        expect(!apply_expanded_damage(map, packet).accepted && map.revision() == revision,
               "nonfinite or far-outside blast coordinates must not mutate the map");
    }
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
    expect(map.initial_health(31U, 47U, 100U) == 9.0F,
           "BlockBuild(32) type 0 must add a 9.0 user block");
    expect(confirmed_owner_block_cost(owner_payload, result, 7U) == 1U &&
               confirmed_owner_block_cost(owner_payload, result, 8U) == 0U,
           "one accepted owner BlockBuild must cost exactly one block");

    // add_user_block refuses an already solid cell, so no block is spent.
    build.position = {35, 47, 100};
    expect(map.set_voxel(35U, 47U, 100U,
                         VxlColor{0x21U, 0x43U, 0x65U, 255U}),
           "occupied-cell fixture must materialize its voxel");
    const auto occupied_owner =
        battlespades::network::encode_packet(build);
    result = replica.apply(occupied_owner);
    expect(result.recognized && result.error.empty() && !result.mutation.accepted &&
               map.solid(35U, 47U, 100U) &&
               confirmed_owner_block_cost(occupied_owner, result, 7U) == 0U,
           "BlockBuild onto a solid cell must neither place nor debit");
    const auto placed = map.color(31U, 47U, 100U);
    expect(placed.has_value() && placed->red == 0x21U &&
               placed->green == 0x43U && placed->blue == 0x65U,
           "BlockBuild(32) must resolve RGB through the sender palette");

    // Snow (block_type 1) enters at DEFAULT_SNOW_HEALTH 3.
    build.position = {36, 47, 100};
    build.block_type = 1U;
    result = replica.apply(battlespades::network::encode_packet(build));
    expect(result.mutation.accepted && map.initial_health(36U, 47U, 100U) == 3.0F,
           "BlockBuild(32) snow must add a 3.0 user block");
    build.block_type = 0U;

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
    expect(map.initial_health(31U, 47U, 100U) == 9.0F,
           "PaintBlock(7) must keep the user-block health");

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
        retail_dim_matches_shared_common_dim();
        footprints_match_live_client_capture_fixture();
        native_damage_shapes_apply_per_cell_health();
        health_multiplier_scales_initial_health();
        classic_and_ugc_user_block_health_follow_add_user_block();
        block_manager_state_matches_server_encoder_and_merges();
        colored_build_and_paint_keep_block_damage();
        competitive_prefab_expansion_replaces_solids_at_prefab_health();
        digging_credits_block_granting_damage();
        chroma_markers_follow_vxl_pyd_cleanup();
        live_damage_produces_one_drainable_impact_event();
        legacy_turret_rocket_removes_support_and_invalidates_visible_chunks();
        legacy_turret_blast_matches_recovered_python2_seed_fixtures();
        turret_rng_does_not_skip_air_or_out_of_map_candidates();
        turret_blast_centres_can_straddle_map_edges_but_never_break_bedrock();
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
