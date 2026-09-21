#include "battlespades/network/protocol168_terrain.hpp"

#include "battlespades/network/protocol168_tool_actions.hpp"
#include "battlespades/world/retail_random.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <type_traits>
#include <utility>

namespace battlespades::network {
namespace {

class Reader final {
public:
    explicit Reader(std::span<const std::byte> bytes) : bytes_{bytes} {}

    [[nodiscard]] bool done() const noexcept { return at_ == bytes_.size(); }
    [[nodiscard]] std::size_t remaining() const noexcept { return bytes_.size() - at_; }

    [[nodiscard]] std::optional<std::uint8_t> u8() noexcept {
        if (remaining() < 1U) {
            return std::nullopt;
        }
        return std::to_integer<std::uint8_t>(bytes_[at_++]);
    }

    template <typename Integer>
    [[nodiscard]] std::optional<Integer> little_integer() noexcept {
        static_assert(std::is_integral_v<Integer>);
        using Unsigned = std::make_unsigned_t<Integer>;
        if (remaining() < sizeof(Integer)) {
            return std::nullopt;
        }
        Unsigned value{};
        for (std::size_t byte{}; byte < sizeof(Integer); ++byte) {
            value |= static_cast<Unsigned>(
                         std::to_integer<std::uint8_t>(bytes_[at_ + byte]))
                     << (byte * 8U);
        }
        at_ += sizeof(Integer);
        return static_cast<Integer>(value);
    }

    [[nodiscard]] std::optional<float> f32() noexcept {
        const auto raw = little_integer<std::uint32_t>();
        return raw.has_value() ? std::optional<float>{std::bit_cast<float>(*raw)}
                               : std::nullopt;
    }

private:
    std::span<const std::byte> bytes_;
    std::size_t at_{};
};

class Writer final {
public:
    void u8(std::uint8_t value) { bytes_.push_back(static_cast<std::byte>(value)); }

    template <typename Integer>
    void little_integer(Integer value) {
        static_assert(std::is_integral_v<Integer>);
        using Unsigned = std::make_unsigned_t<Integer>;
        const auto raw = static_cast<Unsigned>(value);
        for (std::size_t byte{}; byte < sizeof(Integer); ++byte) {
            u8(static_cast<std::uint8_t>(raw >> (byte * 8U)));
        }
    }

    void f32(float value) { little_integer(std::bit_cast<std::uint32_t>(value)); }

    [[nodiscard]] std::vector<std::byte> take() && { return std::move(bytes_); }

private:
    std::vector<std::byte> bytes_;
};

template <typename T>
[[nodiscard]] bool read_required(std::optional<T> value, T& output) noexcept {
    if (!value.has_value()) {
        return false;
    }
    output = *value;
    return true;
}

[[nodiscard]] std::optional<world::VoxelCell>
damage_cell(const DamagePacket& packet) noexcept {
    std::array<std::uint32_t, 3U> cell{};
    const std::array<std::uint32_t, 3U> limits{
        world::VxlMap::width, world::VxlMap::depth, world::VxlMap::height};
    for (std::size_t axis{}; axis < 3U; ++axis) {
        const float value = packet.position[axis];
        if (!std::isfinite(value) || value < 0.0F || value >= limits[axis]) {
            return std::nullopt;
        }
        // Placed explosives detonate at their rendered face/centre. Their
        // authoritative Damage(37) therefore contains .5 offsets (C4 and
        // landmines), while BlockManager converts that point with int() before
        // applying the native radius handler. Direct bullet/melee damage still
        // names an exact voxel and must fail closed on fractional coordinates.
        const bool radius_damage = packet.type == 15U || packet.type == 16U ||
                                   packet.type == 21U || packet.type == 41U;
        if (!radius_damage && std::floor(value) != value) {
            return std::nullopt;
        }
        // The original turret handler receives int(round(position)) from
        // BlockManager.handle_damage (Python 2 rounds halves away from zero).
        // Keep the existing compact deployable protocol's truncation separate.
        cell[axis] = static_cast<std::uint32_t>(
            packet.type == 21U ? std::round(value) : std::floor(value));
    }
    return world::VoxelCell{cell[0U], cell[1U], cell[2U]};
}

struct TerrainCellDamage final {
    world::VoxelCell cell;
    float damage;
};

[[nodiscard]] std::vector<TerrainCellDamage>
expanded_damage_cells(const DamagePacket& packet) {
    if (!std::isfinite(packet.damage) || packet.damage <= 0.0F) return {};
    std::array<std::int64_t, 3U> center{};
    if (packet.type == 21U) {
        // A rocket just outside the map can still damage its edge. Bound the
        // signed centre before conversion, then clip individual candidates.
        constexpr std::array limits{world::VxlMap::width, world::VxlMap::depth,
                                    world::VxlMap::height};
        for (std::size_t axis{}; axis < center.size(); ++axis) {
            const auto value = packet.position[axis];
            if (!std::isfinite(value) || value < -3.0F || value > limits[axis] + 3.0F)
                return {};
            center[axis] = static_cast<std::int64_t>(std::round(value));
        }
    } else {
        const auto cell = damage_cell(packet);
        if (!cell.has_value()) return {};
        center = {cell->x, cell->y, cell->z};
    }

    enum class Shape { single, column, cube, vertical_pair, radius_one, radius_two };
    Shape shape{Shape::single};
    switch (packet.type) {
    case 2U:  // SPADE_DAMAGE: z - 1, z, z + 1.
    case 4U:  // CLASSIC_SPADE_DAMAGE uses the ordinary spade handler.
        shape = Shape::column;
        break;
    case 3U:  // SUPERSPADE_DAMAGE: centered 3 x 3 x 3.
    case 17U: // ZOMBIE_DAMAGE: the same native cube handler.
    case 31U: // UGC_SUPERSPADE_SECONDARY_DAMAGE.
        shape = Shape::cube;
        break;
    case 35U: // MACHETE_DAMAGE: hit cell and z + 1.
        shape = Shape::vertical_pair;
        break;
    case 10U: // DRILL_DAMAGE: native radius-damage handler, radius 2.
    case 33U: // UGC_DRILL_DAMAGE.
    case 16U: // DYNAMITE_DAMAGE.
    case 41U: // C4_DAMAGE.
        shape = Shape::radius_two;
        break;
    case 15U: // LANDMINE_DAMAGE: native radius-damage handler, radius 1.
        shape = Shape::radius_one;
        break;
    default:
        break;
    }

    std::vector<TerrainCellDamage> cells;
    const auto add_damage = [&](std::int32_t dx, std::int32_t dy, std::int32_t dz,
                                float damage) {
        const auto x = center[0U] + dx;
        const auto y = center[1U] + dy;
        const auto z = center[2U] + dz;
        if (x < 0 || y < 0 || z < 0 ||
            x >= static_cast<std::int64_t>(world::VxlMap::width) ||
            y >= static_cast<std::int64_t>(world::VxlMap::depth) ||
            z >= static_cast<std::int64_t>(world::VxlMap::height)) {
            return;
        }
        cells.push_back({{static_cast<std::uint32_t>(x),
                          static_cast<std::uint32_t>(y),
                          static_cast<std::uint32_t>(z)}, damage});
    };
    const auto add = [&](std::int32_t dx, std::int32_t dy, std::int32_t dz) {
        add_damage(dx, dy, dz, packet.damage);
    };

    if (packet.type == 21U) { // ROCKET_TURRET_ROCKET_DAMAGE.
        // gameScene.pyd: turret wrapper 0x10085FA0 (A1621 = 3), radius-list
        // generator 0x10079680, damage handler 0x1007C3A0. Its 93 offsets are
        // ordered Z/X/Y, with strict d^2 < 9; this is not a uniform crater.
        world::RetailRandom random{packet.seed};
        for (std::int32_t dz{-3}; dz <= 3; ++dz) {
            for (std::int32_t dx{-3}; dx <= 3; ++dx) {
                for (std::int32_t dy{-3}; dy <= 3; ++dy) {
                    const auto distance_squared = dx * dx + dy * dy + dz * dz;
                    if (distance_squared >= 9) continue;
                    // Consume one CPython random sample even for air or an
                    // out-of-map cell. Skipping it changes every later hit.
                    const double damage = static_cast<double>(packet.damage) / 9.0 *
                                              (9 - distance_squared) +
                                          random.random() * 2.0;
                    add_damage(dx, dy, dz,
                               static_cast<float>(std::ceil(damage * 4.0) / 4.0));
                }
            }
        }
        return cells;
    }

    if (shape == Shape::single) {
        add(0, 0, 0);
    } else if (shape == Shape::column) {
        for (std::int32_t dz{-1}; dz <= 1; ++dz) add(0, 0, dz);
    } else if (shape == Shape::cube) {
        for (std::int32_t dx{-1}; dx <= 1; ++dx) {
            for (std::int32_t dy{-1}; dy <= 1; ++dy) {
                for (std::int32_t dz{-1}; dz <= 1; ++dz) add(dx, dy, dz);
            }
        }
    } else if (shape == Shape::vertical_pair) {
        add(0, 0, 0);
        add(0, 0, 1);
    } else if (shape == Shape::radius_one) {
        // BlockManager tests voxel centres against radius + 0.5. Radius one
        // therefore keeps faces and edges (distance squared <= 2) but excludes
        // the eight 3-axis corners, for nineteen cells total.
        for (std::int32_t dx{-1}; dx <= 1; ++dx) {
            for (std::int32_t dy{-1}; dy <= 1; ++dy) {
                for (std::int32_t dz{-1}; dz <= 1; ++dz) {
                    if (dx * dx + dy * dy + dz * dz <= 2) add(dx, dy, dz);
                }
            }
        }
    } else {
        // The recovered BlockManager radius predicate is distance < 2.5,
        // yielding exactly 81 integer cells for radius two.
        for (std::int32_t dx{-2}; dx <= 2; ++dx) {
            for (std::int32_t dy{-2}; dy <= 2; ++dy) {
                for (std::int32_t dz{-2}; dz <= 2; ++dz) {
                    if (dx * dx + dy * dy + dz * dz <= 6) add(dx, dy, dz);
                }
            }
        }
    }
    return cells;
}

[[nodiscard]] world::TerrainImpactKind
impact_kind(std::uint8_t damage_type) noexcept {
    switch (damage_type) {
    case 0U:
    case 1U:
    case 2U:
    case 3U:
    case 4U:
    case 5U:
    case 17U:
    case 26U:
    case 28U:
    case 29U:
    case 31U:
    case 34U:
    case 35U:
    case 36U:
    case 42U:
        return world::TerrainImpactKind::melee;
    case 10U:
    case 33U:
        // A live Drill remains in flight across many Damage packets. Each
        // packet is one bore contact (`drill_drilling_exp`), not the Drill's
        // terminal detonation; DestroyEntity(19) owns `drillexplode`.
        return world::TerrainImpactKind::drill;
    case 16U:
    case 15U:
    case 41U:
        return world::TerrainImpactKind::explosion;
    case 24U:
    case 25U:
        return world::TerrainImpactKind::fire;
    default:
        return world::TerrainImpactKind::bullet;
    }
}

/**
 * Translate the Damage(37) enum into the selectable-tool enum.
 *
 * These two retail enums only happen to share their first five ordinals.
 * Passing a damage id through as a tool id made every missing/default value
 * select tool zero (Pickaxe), while Zombie/Crowbar/Machete selected unrelated
 * inventory entries. Keep this conversion beside the packet decoder so every
 * live terrain consumer receives the same semantic tool.
 */
[[nodiscard]] std::uint8_t
source_tool_for_damage(std::uint8_t damage_type) noexcept {
    switch (damage_type) {
    case 0U:  return 0U;  // PICKAXE_DAMAGE -> PICKAXE_TOOL
    case 1U:  return 1U;  // KNIFE_DAMAGE -> KNIFE_TOOL
    case 2U:  return 2U;  // SPADE_DAMAGE -> SPADE_TOOL
    case 3U:  return 3U;  // SUPERSPADE_DAMAGE -> SUPERSPADE_TOOL
    case 4U:
    case 5U:  return 4U;  // CLASSIC_SPADE primary/secondary
    case 17U: return 24U; // ZOMBIE_DAMAGE -> ZOMBIEHAND_TOOL
    case 10U: return 14U; // DRILL_DAMAGE -> DRILLGUN_TOOL
    case 15U: return 58U; // LANDMINE_DAMAGE -> mine-launcher explosion assets
    case 16U: return 21U; // DYNAMITE_DAMAGE -> DYNAMITE_TOOL
    case 41U: return 59U; // C4_DAMAGE -> C4_TOOL
    case 33U: return 47U; // UGC_DRILL_DAMAGE -> UGC_DRILLGUN_TOOL
    case 26U: return 34U; // CROWBAR_DAMAGE -> CROWBAR_TOOL
    case 28U: return 44U; // UGC_PICKAXE_DAMAGE -> UGC_PICKAXE_TOOL
    case 29U:
    case 31U: return 45U; // UGC_SUPERSPADE primary/secondary
    case 34U: return 49U; // RIOTSTICK_DAMAGE -> RIOTSTICK_TOOL
    case 35U: return 50U; // MACHETE_DAMAGE -> MACHETE_TOOL
    case 36U: return 52U; // RIOTSHIELD_DAMAGE -> RIOTSHIELD_TOOL
    case 42U: return 63U; // BLOCK_SUCKER_DAMAGE -> BLOCK_SUCKER_TOOL
    default:  return 0U;
    }
}

[[nodiscard]] std::array<std::int32_t, 3U>
impact_normal(std::uint8_t face) noexcept {
    constexpr std::array<std::array<std::int32_t, 3U>, 6U> normals{{
        {{0, 0, -1}}, {{0, 0, 1}}, {{-1, 0, 0}},
        {{1, 0, 0}},  {{0, -1, 0}}, {{0, 1, 0}},
    }};
    return face < normals.size() ? normals[face] : normals.front();
}

} // namespace

TerrainDecodeResult decode_terrain_packet(std::span<const std::byte> payload) {
    Reader reader{payload};
    const auto id = reader.u8();
    if (!id.has_value()) {
        return {std::nullopt, "empty Protocol 168 packet"};
    }
    if (*id == SetColorPacket::id) {
        SetColorPacket packet;
        std::uint8_t low{};
        std::uint8_t middle{};
        std::uint8_t high{};
        if (!read_required(reader.u8(), packet.player_id) ||
            !read_required(reader.u8(), low) ||
            !read_required(reader.u8(), middle) ||
            !read_required(reader.u8(), high) || !reader.done()) {
            return {std::nullopt, "malformed SetColor(11) packet"};
        }
        packet.color = static_cast<std::uint32_t>(low) |
                       (static_cast<std::uint32_t>(middle) << 8U) |
                       (static_cast<std::uint32_t>(high) << 16U);
        return {TerrainPacket{packet}, {}};
    }
    if (*id == DamagePacket::id) {
        DamagePacket packet;
        std::uint8_t encoded_damage{};
        std::uint8_t chunk_check{};
        if (!read_required(reader.u8(), packet.player_id) ||
            !read_required(reader.u8(), packet.type) ||
            !read_required(reader.u8(), encoded_damage) ||
            !read_required(reader.u8(), packet.face) ||
            !read_required(reader.u8(), chunk_check) ||
            !read_required(reader.u8(), packet.seed) ||
            !read_required(reader.little_integer<std::int16_t>(), packet.causer_id) ||
            !read_required(reader.f32(), packet.position[0U]) ||
            !read_required(reader.f32(), packet.position[1U]) ||
            !read_required(reader.f32(), packet.position[2U]) || !reader.done()) {
            return {std::nullopt, "malformed Damage(37) packet"};
        }
        packet.damage = static_cast<float>(static_cast<std::int8_t>(encoded_damage)) / 4.0F;
        packet.chunk_check = chunk_check != 0U;
        return {TerrainPacket{packet}, {}};
    }
    if (*id == BlockBuildColoredPacket::id) {
        BlockBuildColoredPacket packet;
        std::uint8_t low{};
        std::uint8_t middle{};
        std::uint8_t high{};
        if (!read_required(reader.little_integer<std::int32_t>(), packet.loop_count) ||
            !read_required(reader.u8(), packet.player_id) ||
            !read_required(reader.little_integer<std::int16_t>(), packet.x) ||
            !read_required(reader.little_integer<std::int16_t>(), packet.y) ||
            !read_required(reader.little_integer<std::int16_t>(), packet.z) ||
            !read_required(reader.u8(), low) || !read_required(reader.u8(), middle) ||
            !read_required(reader.u8(), high) || !reader.done()) {
            return {std::nullopt, "malformed BlockBuildColored(33) packet"};
        }
        packet.color = static_cast<std::uint32_t>(low) |
                       (static_cast<std::uint32_t>(middle) << 8U) |
                       (static_cast<std::uint32_t>(high) << 16U);
        return {TerrainPacket{packet}, {}};
    }
    if (*id == BlockLinePacket::id) {
        BlockLinePacket packet;
        if (!read_required(reader.little_integer<std::int32_t>(), packet.loop_count) ||
            !read_required(reader.u8(), packet.player_id)) {
            return {std::nullopt, "malformed BlockLine(40) packet"};
        }
        for (auto& coordinate : packet.start) {
            if (!read_required(reader.little_integer<std::int16_t>(), coordinate)) {
                return {std::nullopt, "malformed BlockLine(40) packet"};
            }
        }
        for (auto& coordinate : packet.end) {
            if (!read_required(reader.little_integer<std::int16_t>(), coordinate)) {
                return {std::nullopt, "malformed BlockLine(40) packet"};
            }
        }
        if (!reader.done()) {
            return {std::nullopt, "malformed BlockLine(40) packet"};
        }
        return {TerrainPacket{packet}, {}};
    }
    return {std::nullopt, "packet is not a supported Protocol 168 terrain packet"};
}

std::vector<std::byte> encode_packet(const SetColorPacket& packet) {
    Writer writer;
    writer.u8(SetColorPacket::id);
    writer.u8(packet.player_id);
    writer.u8(static_cast<std::uint8_t>(packet.color));
    writer.u8(static_cast<std::uint8_t>(packet.color >> 8U));
    writer.u8(static_cast<std::uint8_t>(packet.color >> 16U));
    return std::move(writer).take();
}

std::vector<std::byte> encode_packet(const DamagePacket& packet) {
    Writer writer;
    writer.u8(DamagePacket::id);
    writer.u8(packet.player_id);
    writer.u8(packet.type);
    const auto scaled = static_cast<int>(packet.damage * 4.0F);
    writer.u8(static_cast<std::uint8_t>(static_cast<std::int8_t>(
        std::clamp(scaled, static_cast<int>(std::numeric_limits<std::int8_t>::min()),
                   static_cast<int>(std::numeric_limits<std::int8_t>::max())))));
    writer.u8(packet.face);
    writer.u8(packet.chunk_check ? 1U : 0U);
    writer.u8(packet.seed);
    writer.little_integer(packet.causer_id);
    for (const auto value : packet.position) {
        writer.f32(value);
    }
    return std::move(writer).take();
}

std::vector<std::byte> encode_packet(const BlockBuildColoredPacket& packet) {
    Writer writer;
    writer.u8(BlockBuildColoredPacket::id);
    writer.little_integer(packet.loop_count);
    writer.u8(packet.player_id);
    writer.little_integer(packet.x);
    writer.little_integer(packet.y);
    writer.little_integer(packet.z);
    writer.u8(static_cast<std::uint8_t>(packet.color));
    writer.u8(static_cast<std::uint8_t>(packet.color >> 8U));
    writer.u8(static_cast<std::uint8_t>(packet.color >> 16U));
    return std::move(writer).take();
}

std::vector<std::byte> encode_packet(const BlockLinePacket& packet) {
    Writer writer;
    writer.u8(BlockLinePacket::id);
    writer.little_integer(packet.loop_count);
    writer.u8(packet.player_id);
    for (const auto value : packet.start) {
        writer.little_integer(value);
    }
    for (const auto value : packet.end) {
        writer.little_integer(value);
    }
    return std::move(writer).take();
}

TerrainApplyResult apply_direct_damage(world::VxlMap& map,
                                       const DamagePacket& packet,
                                       float block_health) {
    TerrainApplyResult result;
    const auto cell = damage_cell(packet);
    if (!cell.has_value() || !std::isfinite(block_health) || block_health <= 0.0F ||
        packet.damage <= 0.0F || !map.solid(cell->x, cell->y, cell->z) ||
        cell->z + 1U >= world::VxlMap::height) {
        return result;
    }
    result.accepted = true;
    const float accumulated =
        map.damage_fraction(cell->x, cell->y, cell->z) * block_health + packet.damage;
    if (accumulated < block_health) {
        if (map.set_damage_fraction(cell->x, cell->y, cell->z,
                                    accumulated / block_health)) {
            result.changed_cells.push_back(*cell);
        }
        return result;
    }

    result.destroyed = map.clear_voxel(cell->x, cell->y, cell->z);
    if (!result.destroyed) {
        return result;
    }
    result.changed_cells.push_back(*cell);
    if (packet.chunk_check) {
        result.falling_components =
            world::collapse_unsupported_components(map, {*cell});
        for (const auto& component : result.falling_components) {
            for (const auto& voxel : component) {
                result.changed_cells.push_back(voxel.cell);
            }
        }
    }
    return result;
}

TerrainApplyResult apply_expanded_damage(world::VxlMap& map,
                                         const DamagePacket& packet,
                                         float block_health) {
    TerrainApplyResult result;
    const auto cells = expanded_damage_cells(packet);
    std::vector<world::VoxelCell> destroyed_cells;
    destroyed_cells.reserve(cells.size());

    for (const auto& entry : cells) {
        const auto& cell = entry.cell;
        auto direct = packet;
        direct.damage = entry.damage;
        direct.position = {static_cast<float>(cell.x),
                           static_cast<float>(cell.y),
                           static_cast<float>(cell.z)};
        // Collapse is a property of the complete wire action, not each cell;
        // running it inside the loop makes the result order-dependent.
        direct.chunk_check = false;
        auto mutation = apply_direct_damage(map, direct, block_health);
        result.accepted = result.accepted || mutation.accepted;
        result.destroyed = result.destroyed || mutation.destroyed;
        if (mutation.destroyed) destroyed_cells.push_back(cell);
        result.changed_cells.insert(result.changed_cells.end(),
                                    mutation.changed_cells.begin(),
                                    mutation.changed_cells.end());
    }

    if (packet.chunk_check && !destroyed_cells.empty()) {
        result.falling_components =
            world::collapse_unsupported_components(map, destroyed_cells);
        for (const auto& component : result.falling_components) {
            for (const auto& voxel : component) {
                result.changed_cells.push_back(voxel.cell);
            }
        }
    }
    return result;
}

TerrainApplyResult apply_block_build_colored(
    world::VxlMap& map, const BlockBuildColoredPacket& packet) {
    TerrainApplyResult result;
    if (packet.x < 0 || packet.y < 0 || packet.z < 0 ||
        packet.x >= static_cast<std::int16_t>(world::VxlMap::width) ||
        packet.y >= static_cast<std::int16_t>(world::VxlMap::depth) ||
        packet.z >= static_cast<std::int16_t>(world::VxlMap::height)) {
        return result;
    }
    const auto x = static_cast<std::uint32_t>(packet.x);
    const auto y = static_cast<std::uint32_t>(packet.y);
    const auto z = static_cast<std::uint32_t>(packet.z);
    const world::VxlColor color{
        static_cast<std::uint8_t>(packet.color >> 16U),
        static_cast<std::uint8_t>(packet.color >> 8U),
        static_cast<std::uint8_t>(packet.color), 255U};
    result.accepted = map.set_voxel(x, y, z, color);
    if (result.accepted) {
        result.changed_cells.push_back({x, y, z});
    }
    return result;
}

std::vector<world::VoxelCell> cube_line_cells(const BlockLinePacket& packet,
                                               std::size_t maximum_cells) {
    using Signed = std::array<std::int32_t, 3U>;
    Signed cell{packet.start[0U], packet.start[1U], packet.start[2U]};
    const Signed target{packet.end[0U], packet.end[1U], packet.end[2U]};
    const Signed delta{target[0U] - cell[0U], target[1U] - cell[1U],
                       target[2U] - cell[2U]};
    Signed step{delta[0U] < 0 ? -1 : 1, delta[1U] < 0 ? -1 : 1,
                delta[2U] < 0 ? -1 : 1};
    Signed interval{};
    Signed distance{};
    const auto absolute = [](std::int32_t value) {
        return value < 0 ? -value : value;
    };
    constexpr std::int32_t unit{1024};
    constexpr std::int32_t disabled{0x3FFFFFFF / 512};
    const auto ratio = [&](std::int32_t dominant, std::int32_t axis) {
        return axis == 0 ? disabled : absolute(dominant * unit / axis);
    };

    if (absolute(delta[0U]) >= absolute(delta[1U]) &&
        absolute(delta[0U]) >= absolute(delta[2U])) {
        interval = {unit, ratio(delta[0U], delta[1U]),
                    ratio(delta[0U], delta[2U])};
    } else if (absolute(delta[1U]) >= absolute(delta[2U])) {
        interval = {ratio(delta[1U], delta[0U]), unit,
                    ratio(delta[1U], delta[2U])};
    } else {
        interval = {ratio(delta[2U], delta[0U]),
                    ratio(delta[2U], delta[1U]), unit};
    }
    distance = {interval[0U] / 2, interval[1U] / 2, interval[2U] / 2};
    for (std::size_t axis{}; axis < 3U; ++axis) {
        if (step[axis] >= 0) {
            distance[axis] = interval[axis] - distance[axis];
        }
    }

    std::vector<world::VoxelCell> result;
    result.reserve(std::min<std::size_t>(maximum_cells, 64U));
    while (maximum_cells != 0U && result.size() < maximum_cells) {
        if (cell[0U] < 0 || cell[1U] < 0 || cell[2U] < 0 ||
            cell[0U] >= static_cast<std::int32_t>(world::VxlMap::width) ||
            cell[1U] >= static_cast<std::int32_t>(world::VxlMap::depth) ||
            cell[2U] >= static_cast<std::int32_t>(world::VxlMap::height)) {
            break;
        }
        result.push_back({static_cast<std::uint32_t>(cell[0U]),
                          static_cast<std::uint32_t>(cell[1U]),
                          static_cast<std::uint32_t>(cell[2U])});
        if (cell == target) {
            break;
        }
        // Exact stock priority: z wins ties, then y wins an x/y tie.
        std::size_t axis{};
        if (distance[2U] <= distance[0U] &&
            distance[2U] <= distance[1U]) {
            axis = 2U;
        } else if (distance[0U] < distance[1U]) {
            axis = 0U;
        } else {
            axis = 1U;
        }
        cell[axis] += step[axis];
        distance[axis] += interval[axis];
    }
    return result;
}

TerrainApplyResult apply_block_line(world::VxlMap& map,
                                    const BlockLinePacket& packet,
                                    std::uint32_t color) {
    TerrainApplyResult result;
    const auto cells = cube_line_cells(packet);
    if (cells.empty()) {
        return result;
    }
    const world::VxlColor voxel{
        static_cast<std::uint8_t>(color >> 16U),
        static_cast<std::uint8_t>(color >> 8U),
        static_cast<std::uint8_t>(color), 255U};
    for (const auto& cell : cells) {
        // The preview/cost path omits pre-existing terrain. The receiver must
        // do the same instead of recoloring terrain crossed by the drag.
        if (map.solid(cell.x, cell.y, cell.z) ||
            !map.set_voxel(cell.x, cell.y, cell.z, voxel)) {
            continue;
        }
        result.changed_cells.push_back(cell);
    }
    result.accepted = !result.changed_cells.empty();
    return result;
}

std::uint16_t confirmed_owner_block_cost(
    std::span<const std::byte> payload,
    const TerrainReplicaResult& result,
    std::uint8_t local_player_id) noexcept {
    if (!result.recognized || !result.error.empty() || payload.empty()) {
        return 0U;
    }
    const auto id = std::to_integer<std::uint8_t>(payload.front());
    if (id == BlockBuildPacket::id) {
        const auto decoded = decode_tool_action_packet(payload);
        const auto* build = decoded
                                ? std::get_if<BlockBuildPacket>(&*decoded.packet)
                                : nullptr;
        return build != nullptr && build->player_id == local_player_id ? 1U : 0U;
    }
    if (id != BlockLinePacket::id) return 0U;

    const auto decoded = decode_terrain_packet(payload);
    const auto* line = decoded
                           ? std::get_if<BlockLinePacket>(&*decoded.packet)
                           : nullptr;
    if (line == nullptr || line->player_id != local_player_id) return 0U;
    return static_cast<std::uint16_t>(std::min<std::size_t>(
        result.mutation.changed_cells.size(),
        std::numeric_limits<std::uint16_t>::max()));
}

Protocol168TerrainReplica::Protocol168TerrainReplica(world::VxlMap& map,
                                                     float block_health) noexcept
    : map_{&map}, block_health_{block_health} {}

std::size_t Protocol168TerrainReplica::ExpectedBuildKeyHash::operator()(
    const ExpectedBuildKey& key) const noexcept {
    // FNV-1a over the protocol-visible identity. Coordinates stay separate
    // because the full key is wider than 64 bits.
    std::size_t hash =
        sizeof(std::size_t) >= 8U ? static_cast<std::size_t>(1469598103934665603ULL)
                                  : static_cast<std::size_t>(2166136261U);
    const auto mix = [&hash](std::uint32_t value) {
        constexpr std::size_t prime =
            sizeof(std::size_t) >= 8U
                ? static_cast<std::size_t>(1099511628211ULL)
                : static_cast<std::size_t>(16777619U);
        hash ^= static_cast<std::size_t>(value);
        hash *= prime;
    };
    mix(static_cast<std::uint32_t>(key.loop_count));
    mix(key.player_id);
    mix(key.x);
    mix(key.y);
    mix(key.z);
    return hash;
}

TerrainReplicaResult
Protocol168TerrainReplica::apply(std::span<const std::byte> payload) {
    if (payload.empty()) {
        return {false, {}, "empty terrain packet"};
    }

    const auto packet_id = std::to_integer<std::uint8_t>(payload.front());
    if (packet_id == BlockBuildPacket::id ||
        packet_id == PaintBlockPacket::id) {
        const auto decoded = decode_tool_action_packet(payload);
        if (!decoded) {
            return {false, {}, decoded.error};
        }

        TerrainReplicaResult result;
        result.recognized = true;
        if (const auto* build_packet =
                std::get_if<BlockBuildPacket>(&*decoded.packet);
            build_packet != nullptr) {
            // BlockBuild(32) is the authoritative owner echo for ordinary
            // blocks and every expanded competitive-prefab voxel. It carries
            // no RGB, so it must resolve through the sender's last SetColor.
            const ExpectedBuildKey expected_key{
                build_packet->loop_count, build_packet->player_id,
                static_cast<std::uint32_t>(
                    static_cast<std::uint16_t>(build_packet->position[0U])),
                static_cast<std::uint32_t>(
                    static_cast<std::uint16_t>(build_packet->position[1U])),
                static_cast<std::uint32_t>(
                    static_cast<std::uint16_t>(build_packet->position[2U]))};
            const auto expected =
                expected_owner_build_colors_.find(expected_key);
            const auto palette = player_colors_[build_packet->player_id];
            if (expected == expected_owner_build_colors_.end() &&
                !palette.has_value()) {
                result.error =
                    "BlockBuild(32) arrived before SetColor(11) for player";
                return result;
            }
            BlockBuildColoredPacket colored;
            colored.loop_count = build_packet->loop_count;
            colored.player_id = build_packet->player_id;
            colored.x = build_packet->position[0U];
            colored.y = build_packet->position[1U];
            colored.z = build_packet->position[2U];
            colored.color = expected != expected_owner_build_colors_.end()
                                ? expected->second
                                : *palette;
            if (expected != expected_owner_build_colors_.end()) {
                expected_owner_build_colors_.erase(expected);
            }
            result.mutation = apply_block_build_colored(*map_, colored);
        } else if (const auto* paint_packet =
                       std::get_if<PaintBlockPacket>(&*decoded.packet);
                   paint_packet != nullptr) {
            const auto x = paint_packet->position[0U];
            const auto y = paint_packet->position[1U];
            const auto z = paint_packet->position[2U];
            if (x < 0 || y < 0 || z < 0 ||
                x >= static_cast<std::int16_t>(world::VxlMap::width) ||
                y >= static_cast<std::int16_t>(world::VxlMap::depth) ||
                z >= static_cast<std::int16_t>(world::VxlMap::height)) {
                return result;
            }
            const auto ux = static_cast<std::uint32_t>(x);
            const auto uy = static_cast<std::uint32_t>(y);
            const auto uz = static_cast<std::uint32_t>(z);
            // Paint is recolor-only. Treating it as a generic set operation
            // creates a solid collision voxel when a delayed paint packet
            // races a destroy.
            if (!map_->solid(ux, uy, uz)) {
                return result;
            }
            const world::VxlColor color{
                static_cast<std::uint8_t>(paint_packet->color >> 16U),
                static_cast<std::uint8_t>(paint_packet->color >> 8U),
                static_cast<std::uint8_t>(paint_packet->color), 255U};
            result.mutation.accepted = map_->set_voxel(ux, uy, uz, color);
            if (result.mutation.accepted) {
                result.mutation.changed_cells.push_back({ux, uy, uz});
            }
        }
        record(result.mutation);
        return result;
    }

    const auto decoded = decode_terrain_packet(payload);
    if (!decoded) {
        return {false, {}, decoded.error};
    }

    TerrainReplicaResult result;
    result.recognized = true;
    std::visit(
        [&](const auto& packet) {
            using Packet = std::decay_t<decltype(packet)>;
            if constexpr (std::is_same_v<Packet, SetColorPacket>) {
                player_colors_[packet.player_id] = packet.color & 0xFFFFFFU;
                result.mutation.accepted = true;
            } else if constexpr (std::is_same_v<Packet, DamagePacket>) {
                const auto center = damage_cell(packet);
                const auto color = center.has_value()
                                       ? map_->color(center->x, center->y, center->z)
                                       : std::nullopt;
                result.mutation = apply_expanded_damage(
                    *map_, packet, block_health_);
                // The retail turret handler disables debris; Rocket.delete
                // (DestroyEntity) already owns its explosion effects/audio.
                if (result.mutation.accepted && center.has_value() && packet.type != 21U) {
                    const bool broad = packet.type == 10U || packet.type == 15U ||
                                       packet.type == 16U || packet.type == 33U ||
                                       packet.type == 41U;
                    impact_events_.push_back({
                        impact_kind(packet.type), *center,
                        color.value_or(world::VxlColor{110U, 92U, 72U, 255U}),
                        impact_normal(packet.face), result.mutation.destroyed,
                        broad ? 2.0F : 1.0F,
                        source_tool_for_damage(packet.type)});
                }
            } else if constexpr (std::is_same_v<Packet,
                                                BlockBuildColoredPacket>) {
                result.mutation = apply_block_build_colored(*map_, packet);
            } else if constexpr (std::is_same_v<Packet, BlockLinePacket>) {
                const auto color = player_colors_[packet.player_id];
                if (!color.has_value()) {
                    result.error =
                        "BlockLine(40) arrived before SetColor(11) for player";
                    return;
                }
                result.mutation = apply_block_line(*map_, packet, *color);
            }
        },
        *decoded.packet);
    record(result.mutation);
    return result;
}

TerrainReplicaResult Protocol168TerrainReplica::apply_colored_cells(
    std::span<const ColoredTerrainCell> cells) {
    TerrainReplicaResult result;
    result.recognized = true;
    result.mutation.changed_cells.reserve(cells.size());
    for (const auto& mutation : cells) {
        if (map_->set_voxel(mutation.cell.x, mutation.cell.y, mutation.cell.z,
                            mutation.color)) {
            result.mutation.changed_cells.push_back(mutation.cell);
        }
    }
    result.mutation.accepted = !result.mutation.changed_cells.empty();
    record(result.mutation);
    return result;
}

TerrainReplicaResult Protocol168TerrainReplica::apply_removed_cells(
    std::span<const world::VoxelCell> cells) {
    TerrainReplicaResult result;
    result.recognized = true;
    result.mutation.changed_cells.reserve(cells.size());
    for (const auto& cell : cells) {
        if (map_->clear_voxel(cell.x, cell.y, cell.z)) {
            result.mutation.changed_cells.push_back(cell);
            result.mutation.destroyed = true;
        }
    }
    result.mutation.accepted = !result.mutation.changed_cells.empty();
    record(result.mutation);
    return result;
}

void Protocol168TerrainReplica::expect_owner_build_colors(
    std::int32_t loop_count, std::uint8_t player_id,
    std::span<const ColoredTerrainCell> cells) {
    constexpr std::size_t maximum_pending_cells{8192U};
    if (cells.empty() || cells.size() > maximum_pending_cells) return;
    if (expected_owner_build_colors_.size() + cells.size() >
        maximum_pending_cells) {
        // A rejected action has no explicit negative acknowledgement. Dropping
        // old expectations is safe because their packet-32 fallback remains
        // the player's current palette; retaining an unbounded ledger is not.
        expected_owner_build_colors_.clear();
    }
    for (const auto& expected : cells) {
        const auto packed =
            (static_cast<std::uint32_t>(expected.color.red) << 16U) |
            (static_cast<std::uint32_t>(expected.color.green) << 8U) |
            static_cast<std::uint32_t>(expected.color.blue);
        expected_owner_build_colors_.insert_or_assign(
            ExpectedBuildKey{loop_count, player_id, expected.cell.x,
                             expected.cell.y, expected.cell.z},
            packed);
    }
}

std::vector<world::ChunkKey> Protocol168TerrainReplica::take_dirty_chunks() {
    return std::exchange(dirty_chunks_, {});
}

std::vector<world::FallingComponent>
Protocol168TerrainReplica::take_falling_components() {
    return std::exchange(falling_components_, {});
}

std::vector<world::TerrainImpactEvent>
Protocol168TerrainReplica::take_impact_events() {
    return std::exchange(impact_events_, {});
}

std::optional<std::uint32_t>
Protocol168TerrainReplica::player_color(std::uint8_t player_id) const noexcept {
    return player_colors_[player_id];
}

void Protocol168TerrainReplica::record(const TerrainApplyResult& result) {
    constexpr std::uint32_t chunk_edge{16U};
    constexpr std::uint32_t chunks_per_axis{world::VxlMap::width / chunk_edge};
    const auto add_chunk = [&](std::uint32_t x, std::uint32_t y) {
        const world::ChunkKey key{x, y};
        if (std::find(dirty_chunks_.begin(), dirty_chunks_.end(), key) ==
            dirty_chunks_.end()) {
            dirty_chunks_.push_back(key);
        }
    };
    for (const auto& cell : result.changed_cells) {
        const auto chunk_x = cell.x / chunk_edge;
        const auto chunk_y = cell.y / chunk_edge;
        add_chunk(chunk_x, chunk_y);
        // A boundary mutation changes the neighboring chunk's exposed face.
        if (cell.x % chunk_edge == 0U && chunk_x > 0U) {
            add_chunk(chunk_x - 1U, chunk_y);
        }
        if (cell.x % chunk_edge == chunk_edge - 1U &&
            chunk_x + 1U < chunks_per_axis) {
            add_chunk(chunk_x + 1U, chunk_y);
        }
        if (cell.y % chunk_edge == 0U && chunk_y > 0U) {
            add_chunk(chunk_x, chunk_y - 1U);
        }
        if (cell.y % chunk_edge == chunk_edge - 1U &&
            chunk_y + 1U < chunks_per_axis) {
            add_chunk(chunk_x, chunk_y + 1U);
        }
    }
    for (const auto& component : result.falling_components) {
        falling_components_.push_back(component);
    }
}

} // namespace battlespades::network
