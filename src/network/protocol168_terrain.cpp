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

/** Damage(37) centre: floor(position + 0.5) per axis for every type. */
[[nodiscard]] std::optional<std::array<std::int64_t, 3U>>
damage_center(const std::array<float, 3U>& position) noexcept {
    std::array<std::int64_t, 3U> center{};
    for (std::size_t axis{}; axis < 3U; ++axis) {
        const auto value = static_cast<double>(position[axis]);
        // Bound far-away garbage before the integer conversion; any footprint
        // (radius <= 8) of a centre this far out cannot touch the map.
        if (!std::isfinite(value) || value < -4096.0 || value > 4096.0) {
            return std::nullopt;
        }
        center[axis] = static_cast<std::int64_t>(std::floor(value + 0.5));
    }
    return center;
}

/** In-map centre cell of a Damage(37), for impact presentation. */
[[nodiscard]] std::optional<world::VoxelCell>
damage_cell(const DamagePacket& packet) noexcept {
    const auto center = damage_center(packet.position);
    if (!center.has_value()) return std::nullopt;
    const std::array<std::int64_t, 3U> limits{
        world::VxlMap::width, world::VxlMap::depth, world::VxlMap::height};
    for (std::size_t axis{}; axis < 3U; ++axis) {
        if ((*center)[axis] < 0 || (*center)[axis] >= limits[axis]) return std::nullopt;
    }
    return world::VoxelCell{static_cast<std::uint32_t>((*center)[0U]),
                            static_cast<std::uint32_t>((*center)[1U]),
                            static_cast<std::uint32_t>((*center)[2U])};
}

enum class FootprintKind : std::uint8_t { none, single, column, machete, cube, radius };

struct FootprintShape final {
    FootprintKind kind{FootprintKind::none};
    /** RADIUS: R. CUBE: the random extra E. */
    float parameter{};
};

/** BS/server/block_damage_model.py FOOTPRINTS, fitted live for all 44 types. */
[[nodiscard]] FootprintShape footprint_shape(std::uint8_t type) noexcept {
    switch (type) {
    case 0U:  // PICKAXE
    case 1U:  // KNIFE
    case 4U:  // CLASSIC_SPADE
    case 6U:  // WEAPON
    case 25U: // BLOCKFIRE
    case 26U: // CROWBAR
    case 28U: // UGC_PICKAXE
    case 29U: // UGC_SUPERSPADE
    case 34U: // RIOTSTICK
    case 42U: // BLOCK_SUCKER
    case 43U: // UNKNOWN
        return {FootprintKind::single, 0.0F};
    case 2U:  // SPADE
    case 5U:  // CLASSIC_SPADE_SECONDARY
    case 36U: // RIOTSHIELD
        return {FootprintKind::column, 0.0F};
    case 35U: // MACHETE
        return {FootprintKind::machete, 0.0F};
    case 3U:  // SUPERSPADE
        return {FootprintKind::cube, 5.0F};
    case 17U: // ZOMBIE
        return {FootprintKind::cube, 8.0F};
    case 31U: // UGC_SUPERSPADE_SECONDARY
        return {FootprintKind::cube, 0.0F};
    case 22U: // CLASSIC_GRENADE
    case 23U: // ANTIPERSONNEL_GRENADE
        return {FootprintKind::radius, 2.0F};
    case 10U: // DRILL
    case 11U: // DRILL_DESTROYED
    case 12U: // ROCKET_TURRET
    case 13U: // CORPSE
    case 14U: // GRAVE
    case 15U: // LANDMINE
    case 21U: // ROCKET_TURRET_ROCKET
    case 33U: // UGC_DRILL
    case 38U: // SOME
    case 40U: // MINE_LAUNCHER
        return {FootprintKind::radius, 3.0F};
    case 7U:  // GRENADE
    case 8U:  // ROCKET
    case 24U: // MOLOTOV
    case 30U: // UGC_ROCKET2
    case 37U: // GRENADE_LAUNCHER
        return {FootprintKind::radius, 4.0F};
    case 39U: // STICKY_GRENADE
        return {FootprintKind::radius, 5.0F};
    case 9U:  // ROCKET2
    case 18U: // AIRSTRIKE
        return {FootprintKind::radius, 6.0F};
    case 19U: // BOMB
        return {FootprintKind::radius, 7.0F};
    case 16U: // DYNAMITE
    case 41U: // C4
        return {FootprintKind::radius, 8.0F};
    default:  // 20 SNOWBALL, 27 MG, 32 UGC_SNOWBALL and unknown ids.
        return {FootprintKind::none, 0.0F};
    }
}

/** ceil4: round up to the retail 0.25 block-damage quantum. */
[[nodiscard]] float ceil4(double value) noexcept {
    return static_cast<float>(std::ceil(value * 4.0) / 4.0);
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
        return world::TerrainImpactKind::fire;
    case 25U:
        // BLOCKFIRE: one burning block per tick, not a molotov blast.
        return world::TerrainImpactKind::burn;
    case 43U:
        // Chemical Bomb goo dissolving its block.
        return world::TerrainImpactKind::dissolve;
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
        // One UNSIGNED byte in quarter units (retail shared.packet).
        packet.damage = static_cast<float>(encoded_damage) / 4.0F;
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
    if (*id == BlockManagerStatePacket::id) {
        BlockManagerStatePacket packet;
        const auto count = [&](std::size_t row_bytes) -> std::optional<std::size_t> {
            const auto value = reader.little_integer<std::int32_t>();
            if (!value.has_value() || *value < 0 ||
                static_cast<std::size_t>(*value) > reader.remaining() / row_bytes) {
                return std::nullopt;
            }
            return static_cast<std::size_t>(*value);
        };
        const auto read_cell = [&](std::int16_t& x, std::int16_t& y, std::int16_t& z) {
            return read_required(reader.little_integer<std::int16_t>(), x) &&
                   read_required(reader.little_integer<std::int16_t>(), y) &&
                   read_required(reader.little_integer<std::int16_t>(), z);
        };
        const auto damaged_count = count(10U);
        if (!damaged_count.has_value()) {
            return {std::nullopt, "malformed BlockManagerState(38) damaged table"};
        }
        packet.damaged.resize(*damaged_count);
        for (auto& row : packet.damaged) {
            std::uint8_t quarters{};
            std::uint8_t blue{};
            std::uint8_t green{};
            std::uint8_t red{};
            if (!read_cell(row.x, row.y, row.z) || !read_required(reader.u8(), quarters) ||
                !read_required(reader.u8(), blue) || !read_required(reader.u8(), green) ||
                !read_required(reader.u8(), red)) {
                return {std::nullopt, "malformed BlockManagerState(38) damaged row"};
            }
            row.health = static_cast<float>(quarters) / 4.0F;
            row.original_color = (static_cast<std::uint32_t>(red) << 16U) |
                                 (static_cast<std::uint32_t>(green) << 8U) |
                                 static_cast<std::uint32_t>(blue);
        }
        const auto user_count = count(7U);
        if (!user_count.has_value()) {
            return {std::nullopt, "malformed BlockManagerState(38) user table"};
        }
        packet.user.resize(*user_count);
        for (auto& row : packet.user) {
            std::uint8_t quarters{};
            if (!read_cell(row.x, row.y, row.z) || !read_required(reader.u8(), quarters)) {
                return {std::nullopt, "malformed BlockManagerState(38) user row"};
            }
            row.health = static_cast<float>(quarters) / 4.0F;
        }
        const auto occupied_count = count(7U);
        if (!occupied_count.has_value()) {
            return {std::nullopt, "malformed BlockManagerState(38) occupied table"};
        }
        packet.occupied.resize(*occupied_count);
        for (auto& row : packet.occupied) {
            if (!read_cell(row.x, row.y, row.z) || !read_required(reader.u8(), row.player_id)) {
                return {std::nullopt, "malformed BlockManagerState(38) occupied row"};
            }
        }
        if (!reader.done()) {
            return {std::nullopt, "malformed BlockManagerState(38) packet"};
        }
        return {TerrainPacket{std::move(packet)}, {}};
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
    writer.u8(encode_damage_quarters(packet.damage));
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

std::vector<std::byte> encode_packet(const BlockManagerStatePacket& packet) {
    Writer writer;
    writer.u8(BlockManagerStatePacket::id);
    writer.little_integer(static_cast<std::int32_t>(packet.damaged.size()));
    for (const auto& row : packet.damaged) {
        writer.little_integer(row.x);
        writer.little_integer(row.y);
        writer.little_integer(row.z);
        writer.u8(encode_damage_quarters(row.health));
        writer.u8(static_cast<std::uint8_t>(row.original_color));
        writer.u8(static_cast<std::uint8_t>(row.original_color >> 8U));
        writer.u8(static_cast<std::uint8_t>(row.original_color >> 16U));
    }
    writer.little_integer(static_cast<std::int32_t>(packet.user.size()));
    for (const auto& row : packet.user) {
        writer.little_integer(row.x);
        writer.little_integer(row.y);
        writer.little_integer(row.z);
        writer.u8(encode_damage_quarters(row.health));
    }
    writer.little_integer(static_cast<std::int32_t>(packet.occupied.size()));
    for (const auto& row : packet.occupied) {
        writer.little_integer(row.x);
        writer.little_integer(row.y);
        writer.little_integer(row.z);
        writer.u8(row.player_id);
    }
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

std::vector<DamageFootprintCell>
retail_damage_footprint(std::uint8_t damage_type, const std::array<float, 3U>& position,
                        float amount, std::uint8_t seed) {
    const auto shape = footprint_shape(damage_type);
    const auto center = damage_center(position);
    std::vector<DamageFootprintCell> cells;
    if (shape.kind == FootprintKind::none || !center.has_value() || !std::isfinite(amount)) {
        return cells;
    }
    const auto cx = static_cast<std::int32_t>((*center)[0U]);
    const auto cy = static_cast<std::int32_t>((*center)[1U]);
    const auto cz = static_cast<std::int32_t>((*center)[2U]);
    const auto push = [&](std::int32_t dx, std::int32_t dy, std::int32_t dz, float damage) {
        cells.push_back({cx + dx, cy + dy, cz + dz, damage});
    };
    switch (shape.kind) {
    case FootprintKind::single:
        push(0, 0, 0, amount);
        return cells;
    case FootprintKind::column:
        push(0, 0, -1, amount);
        push(0, 0, 0, amount);
        push(0, 0, 1, amount);
        return cells;
    case FootprintKind::machete:
        push(0, 0, 0, amount);
        push(0, 0, 1, amount);
        return cells;
    case FootprintKind::cube: {
        world::RetailRandom random{seed};
        const double extra = shape.parameter;
        for (std::int32_t dx{-1}; dx <= 1; ++dx) {
            for (std::int32_t dy{-1}; dy <= 1; ++dy) {
                for (std::int32_t dz{-1}; dz <= 1; ++dz) {
                    push(dx, dy, dz,
                         ceil4(static_cast<double>(amount) + extra * random.random()));
                }
            }
        }
        return cells;
    }
    case FootprintKind::radius: {
        world::RetailRandom random{seed};
        const auto radius = static_cast<std::int32_t>(shape.parameter);
        const auto limit = static_cast<double>(radius * radius);
        // RADIUS_BLOCK_DAMAGE_RANDOM_EXTRA.
        constexpr double extra{2.0};
        for (std::int32_t dz{-radius}; dz <= radius; ++dz) {
            for (std::int32_t dx{-radius}; dx <= radius; ++dx) {
                for (std::int32_t dy{-radius}; dy <= radius; ++dy) {
                    const auto distance_squared = dx * dx + dy * dy + dz * dz;
                    if (distance_squared >= radius * radius) continue;
                    // One CPython draw per footprint cell, solid or not.
                    const double roll = random.random();
                    push(dx, dy, dz,
                         ceil4(static_cast<double>(amount) *
                                   (1.0 - static_cast<double>(distance_squared) / limit) +
                               extra * roll));
                }
            }
        }
        return cells;
    }
    case FootprintKind::none:
        break;
    }
    return cells;
}

bool is_block_granting_damage(std::uint8_t damage_type) noexcept {
    switch (damage_type) {
    case 2U:  // SPADE
    case 4U:  // CLASSIC_SPADE
    case 3U:  // SUPERSPADE
    case 0U:  // PICKAXE
    case 1U:  // KNIFE
    case 17U: // ZOMBIE
    case 26U: // CROWBAR
    case 28U: // UGC_PICKAXE
    case 29U: // UGC_SUPERSPADE
    case 34U: // RIOTSTICK
    case 35U: // MACHETE
    case 36U: // RIOTSHIELD
    case 42U: // BLOCK_SUCKER
        return true;
    default:
        return false;
    }
}

std::uint8_t encode_damage_quarters(float amount) noexcept {
    if (!std::isfinite(amount)) return 0U;
    const auto quarters = std::floor(static_cast<double>(amount) * 4.0 + 0.5);
    return static_cast<std::uint8_t>(std::clamp(quarters, 0.0, 255.0));
}

namespace {

void apply_footprint(world::VxlMap& map, const DamagePacket& packet,
                     std::span<const DamageFootprintCell> cells,
                     TerrainApplyResult& result) {
    std::vector<world::VoxelCell> destroyed_cells;
    for (const auto& entry : cells) {
        if (entry.x < 0 || entry.y < 0 || entry.z < 0 ||
            entry.x >= static_cast<std::int32_t>(world::VxlMap::width) ||
            entry.y >= static_cast<std::int32_t>(world::VxlMap::depth) ||
            entry.z > static_cast<std::int32_t>(world::VxlMap::max_damageable_z) ||
            !(entry.damage > 0.0F)) {
            continue;
        }
        const world::VoxelCell cell{static_cast<std::uint32_t>(entry.x),
                                    static_cast<std::uint32_t>(entry.y),
                                    static_cast<std::uint32_t>(entry.z)};
        const auto outcome = map.add_damage(cell.x, cell.y, cell.z, entry.damage);
        if (outcome == world::BlockDamageOutcome::ignored) continue;
        result.accepted = true;
        result.changed_cells.push_back(cell);
        if (outcome == world::BlockDamageOutcome::destroyed) {
            result.destroyed = true;
            ++result.destroyed_cells;
            destroyed_cells.push_back(cell);
        }
    }
    // Collapse is a property of the complete wire action, not each cell;
    // running it inside the loop makes the result order-dependent.
    if (packet.chunk_check && !destroyed_cells.empty()) {
        result.falling_components =
            world::collapse_unsupported_components(map, destroyed_cells);
        for (const auto& component : result.falling_components) {
            for (const auto& voxel : component) {
                result.changed_cells.push_back(voxel.cell);
            }
        }
    }
}

[[nodiscard]] world::VxlColor unpack_rgb(std::uint32_t color) noexcept {
    return {static_cast<std::uint8_t>(color >> 16U), static_cast<std::uint8_t>(color >> 8U),
            static_cast<std::uint8_t>(color), 255U};
}

} // namespace

TerrainApplyResult apply_direct_damage(world::VxlMap& map, const DamagePacket& packet) {
    TerrainApplyResult result;
    if (!std::isfinite(packet.damage) || packet.damage <= 0.0F) return result;
    const auto center = damage_center(packet.position);
    if (!center.has_value()) return result;
    const std::array<DamageFootprintCell, 1U> cell{{
        {static_cast<std::int32_t>((*center)[0U]), static_cast<std::int32_t>((*center)[1U]),
         static_cast<std::int32_t>((*center)[2U]), packet.damage}}};
    apply_footprint(map, packet, cell, result);
    return result;
}

TerrainApplyResult apply_expanded_damage(world::VxlMap& map, const DamagePacket& packet) {
    TerrainApplyResult result;
    if (!std::isfinite(packet.damage) || packet.damage <= 0.0F) return result;
    const auto cells =
        retail_damage_footprint(packet.type, packet.position, packet.damage, packet.seed);
    apply_footprint(map, packet, cells, result);
    return result;
}

TerrainApplyResult apply_block_build_colored(
    world::VxlMap& map, const BlockBuildColoredPacket& packet) {
    TerrainApplyResult result;
    if (packet.x < 0 || packet.y < 0 || packet.z < 0) {
        return result;
    }
    const auto x = static_cast<std::uint32_t>(packet.x);
    const auto y = static_cast<std::uint32_t>(packet.y);
    const auto z = static_cast<std::uint32_t>(packet.z);
    // Stock client (live 2026-09-26): user block at 3.0, ignored on a solid.
    result.accepted = map.add_user_block(x, y, z, unpack_rgb(packet.color),
                                         world::VxlMap::snow_block_health, false);
    if (result.accepted) {
        result.changed_cells.push_back({x, y, z});
    }
    return result;
}

TerrainApplyResult apply_block_manager_state(world::VxlMap& map,
                                             const BlockManagerStatePacket& packet) {
    TerrainApplyResult result;
    result.accepted = true;
    const auto in_map = [](std::int16_t x, std::int16_t y, std::int16_t z) {
        return x >= 0 && y >= 0 && z >= 0 &&
               x < static_cast<std::int16_t>(world::VxlMap::width) &&
               y < static_cast<std::int16_t>(world::VxlMap::depth) &&
               z < static_cast<std::int16_t>(world::VxlMap::height);
    };
    // receive_block_manager_state MERGES, in wire order: damaged rows darken
    // against the initial health held at that moment, which is why the
    // server always sends user rows in earlier packets.
    for (const auto& row : packet.damaged) {
        if (!in_map(row.x, row.y, row.z)) continue;
        const auto x = static_cast<std::uint32_t>(row.x);
        const auto y = static_cast<std::uint32_t>(row.y);
        const auto z = static_cast<std::uint32_t>(row.z);
        if (map.set_damaged_block(x, y, z, row.health, unpack_rgb(row.original_color))) {
            result.changed_cells.push_back({x, y, z});
        }
    }
    for (const auto& row : packet.user) {
        if (!in_map(row.x, row.y, row.z)) continue;
        static_cast<void>(map.set_user_block_health(static_cast<std::uint32_t>(row.x),
                                                    static_cast<std::uint32_t>(row.y),
                                                    static_cast<std::uint32_t>(row.z),
                                                    row.health));
    }
    // Occupied rows reserve air cells for pending builds; BattleSpades never
    // sends them (BlockOccupy is unused) and nothing here consumes them.
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
        // Retail add_user_block(..., DEFAULT_PREFAB_HEALTH): 9.0 per cell.
        if (!map.add_user_block(cell.x, cell.y, cell.z, voxel,
                                world::VxlMap::prefab_block_health, false)) {
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
        return build != nullptr && build->player_id == local_player_id &&
                       result.mutation.accepted
                   ? 1U
                   : 0U;
    }
    if (id == BuildPrefabActionPacket::id) {
        const auto decoded = decode_tool_action_packet(payload);
        const auto* prefab = decoded
                                 ? std::get_if<BuildPrefabActionPacket>(&*decoded.packet)
                                 : nullptr;
        if (prefab == nullptr || !prefab->add_to_user_blocks ||
            prefab->player_id != local_player_id) {
            return 0U;
        }
        return static_cast<std::uint16_t>(std::min<std::size_t>(
            result.mutation.user_blocks_added, std::numeric_limits<std::uint16_t>::max()));
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
                                                     float health_multiplier,
                                                     bool classic, bool ugc) noexcept
    : map_{&map} {
    map_->set_health_multiplier(health_multiplier);
    map_->set_user_block_rules(classic, ugc);
}

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
            // BLOCK_BUILD_TYPE_STATS: type 0 (prefab/ordinary) 9.0, type 1
            // (snow) 3.0; add_user_block never replaces a solid.
            const float health = build_packet->block_type == 1U
                                     ? world::VxlMap::snow_block_health
                                 : build_packet->block_type == 0U
                                     ? world::VxlMap::prefab_block_health
                                     : world::VxlMap::default_block_health;
            if (colored.x >= 0 && colored.y >= 0 && colored.z >= 0) {
                const auto bx = static_cast<std::uint32_t>(colored.x);
                const auto by = static_cast<std::uint32_t>(colored.y);
                const auto bz = static_cast<std::uint32_t>(colored.z);
                const world::VxlColor block_color{
                    static_cast<std::uint8_t>(colored.color >> 16U),
                    static_cast<std::uint8_t>(colored.color >> 8U),
                    static_cast<std::uint8_t>(colored.color), 255U};
                result.mutation.accepted =
                    map_->add_user_block(bx, by, bz, block_color, health, false);
                if (result.mutation.accepted) {
                    result.mutation.changed_cells.push_back({bx, by, bz});
                }
            }
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
            // Paint is recolor-only (VXL.color_block). Treating it as a
            // generic set operation creates a solid collision voxel when a
            // delayed paint packet races a destroy, and would erase the
            // cell's user health and DamagedBlock, which retail keeps.
            if (!map_->solid(ux, uy, uz)) {
                return result;
            }
            const world::VxlColor color{
                static_cast<std::uint8_t>(paint_packet->color >> 16U),
                static_cast<std::uint8_t>(paint_packet->color >> 8U),
                static_cast<std::uint8_t>(paint_packet->color), 255U};
            result.mutation.accepted = map_->recolor_voxel(ux, uy, uz, color);
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
                result.mutation = apply_expanded_damage(*map_, packet);
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
            } else if constexpr (std::is_same_v<Packet, BlockManagerStatePacket>) {
                result.mutation = apply_block_manager_state(*map_, packet);
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

TerrainReplicaResult Protocol168TerrainReplica::apply_prefab_user_blocks(
    std::span<const ColoredTerrainCell> cells) {
    TerrainReplicaResult result;
    result.recognized = true;
    result.mutation.changed_cells.reserve(cells.size());
    for (const auto& mutation : cells) {
        if (map_->add_user_block(mutation.cell.x, mutation.cell.y, mutation.cell.z,
                                 mutation.color, world::VxlMap::prefab_block_health, true)) {
            result.mutation.changed_cells.push_back(mutation.cell);
            ++result.mutation.user_blocks_added;
        }
    }
    result.mutation.accepted = result.mutation.user_blocks_added != 0U;
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
    if (result.changed_cells.empty()) {
        for (const auto& component : result.falling_components) {
            falling_components_.push_back(component);
        }
        return;
    }
    // O(1) de-duplication: the old std::find over dirty_chunks_ per changed
    // cell was O(cells x dirty) on large blasts.
    std::array<bool, static_cast<std::size_t>(chunks_per_axis) * chunks_per_axis> queued{};
    for (const auto& key : dirty_chunks_) {
        if (key.x < chunks_per_axis && key.y < chunks_per_axis) {
            queued[static_cast<std::size_t>(key.y) * chunks_per_axis + key.x] = true;
        }
    }
    const auto add_chunk = [&](std::uint32_t x, std::uint32_t y) {
        auto& flag = queued[static_cast<std::size_t>(y) * chunks_per_axis + x];
        if (!flag) {
            flag = true;
            dirty_chunks_.push_back(world::ChunkKey{x, y});
        }
    };
    // Same neighbourhood as world::ChunkTracker::mark_voxel: a cell's faces
    // and AO reach one voxel into every edge- and corner-adjacent chunk, and
    // the retail diagonal sun light (retail_sun_light) shades cells up to
    // nine rows further along +y, plus one for corner averaging. Missing the
    // diagonals and the +y reach left stale shading at chunk seams after
    // remote edits.
    constexpr std::int64_t sun_reach_y{10};
    constexpr auto last = static_cast<std::int64_t>(world::VxlMap::width) - 1;
    for (const auto& cell : result.changed_cells) {
        const auto low_x = std::max<std::int64_t>(static_cast<std::int64_t>(cell.x) - 1, 0);
        const auto high_x = std::min<std::int64_t>(static_cast<std::int64_t>(cell.x) + 1, last);
        const auto low_y = std::max<std::int64_t>(static_cast<std::int64_t>(cell.y) - 1, 0);
        const auto high_y =
            std::min<std::int64_t>(static_cast<std::int64_t>(cell.y) + sun_reach_y, last);
        for (auto chunk_y = static_cast<std::uint32_t>(low_y) / chunk_edge;
             chunk_y <= static_cast<std::uint32_t>(high_y) / chunk_edge; ++chunk_y) {
            for (auto chunk_x = static_cast<std::uint32_t>(low_x) / chunk_edge;
                 chunk_x <= static_cast<std::uint32_t>(high_x) / chunk_edge; ++chunk_x) {
                add_chunk(chunk_x, chunk_y);
            }
        }
    }
    for (const auto& component : result.falling_components) {
        falling_components_.push_back(component);
    }
}

} // namespace battlespades::network
