#include "battlespades/network/protocol168_weapons.hpp"

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

    [[nodiscard]] bool done() const noexcept { return offset_ == bytes_.size(); }
    [[nodiscard]] std::size_t offset() const noexcept { return offset_; }
    [[nodiscard]] std::size_t remaining() const noexcept {
        return bytes_.size() - offset_;
    }

    [[nodiscard]] std::optional<std::uint8_t> u8() noexcept {
        if (remaining() == 0U) {
            return std::nullopt;
        }
        return std::to_integer<std::uint8_t>(bytes_[offset_++]);
    }

    template <typename Integer>
    [[nodiscard]] std::optional<Integer> integer() noexcept {
        static_assert(std::is_integral_v<Integer>);
        if (remaining() < sizeof(Integer)) {
            return std::nullopt;
        }
        using Unsigned = std::make_unsigned_t<Integer>;
        Unsigned value{};
        for (std::size_t index{}; index < sizeof(Integer); ++index) {
            value |= static_cast<Unsigned>(
                         std::to_integer<std::uint8_t>(bytes_[offset_ + index]))
                     << (index * 8U);
        }
        offset_ += sizeof(Integer);
        return static_cast<Integer>(value);
    }

    [[nodiscard]] std::optional<float> f32() noexcept {
        const auto bits = integer<std::uint32_t>();
        if (!bits.has_value()) {
            return std::nullopt;
        }
        const float value = std::bit_cast<float>(*bits);
        return std::isfinite(value) ? std::optional<float>{value} : std::nullopt;
    }

    [[nodiscard]] std::optional<std::string> string() {
        const auto start = offset_;
        while (offset_ < bytes_.size() && bytes_[offset_] != std::byte{0U}) {
            ++offset_;
        }
        if (offset_ == bytes_.size()) {
            return std::nullopt;
        }
        std::string result;
        result.reserve(offset_ - start);
        for (auto index = start; index < offset_; ++index) {
            result.push_back(static_cast<char>(
                std::to_integer<std::uint8_t>(bytes_[index])));
        }
        ++offset_;
        return result;
    }

    [[nodiscard]] bool skip(std::size_t count) noexcept {
        if (remaining() < count) {
            return false;
        }
        offset_ += count;
        return true;
    }

private:
    std::span<const std::byte> bytes_;
    std::size_t offset_{};
};

class Writer final {
public:
    void u8(std::uint8_t value) { bytes_.push_back(static_cast<std::byte>(value)); }

    template <typename Integer>
    void integer(Integer value) {
        static_assert(std::is_integral_v<Integer>);
        using Unsigned = std::make_unsigned_t<Integer>;
        const auto raw = static_cast<Unsigned>(value);
        for (std::size_t index{}; index < sizeof(Integer); ++index) {
            u8(static_cast<std::uint8_t>(raw >> (index * 8U)));
        }
    }

    void f32(float value) { integer(std::bit_cast<std::uint32_t>(value)); }

    void string(std::string_view value) {
        for (const char character : value) {
            u8(static_cast<std::uint8_t>(character));
        }
        u8(0U);
    }

    [[nodiscard]] std::vector<std::byte> take() && { return std::move(bytes_); }

private:
    std::vector<std::byte> bytes_;
};

template <typename Value>
[[nodiscard]] bool required(std::optional<Value> input, Value& output) noexcept {
    if (!input.has_value()) {
        return false;
    }
    output = std::move(*input);
    return true;
}

[[nodiscard]] float from_fixed(std::int16_t raw) noexcept {
    const auto bits = static_cast<std::uint16_t>(raw);
    const float magnitude = static_cast<float>(bits & 0x7FFFU) / 64.0F;
    return (bits & 0x8000U) != 0U ? -magnitude : magnitude;
}

[[nodiscard]] std::int16_t to_fixed(float value) noexcept {
    if (!std::isfinite(value)) {
        return 0;
    }
    // This is retail sign-magnitude 1/64 fixed, not two's complement.
    const float scaled = value * 64.0F + 0.5F;
    const auto integral = static_cast<std::int32_t>(scaled);
    const auto magnitude = static_cast<std::uint16_t>(std::min(
        std::abs(integral), static_cast<std::int32_t>(0x7FFF)));
    const auto bits = static_cast<std::uint16_t>(
        magnitude | (integral < 0 ? 0x8000U : 0U));
    return static_cast<std::int16_t>(bits);
}

[[nodiscard]] float from_orientation(std::uint16_t raw) noexcept {
    // ClientData orientation is the stock client's sign-magnitude format,
    // not an ordinary signed 3.13 integer. Retail leaves a deliberate gap:
    // magnitudes below 1 use 1/8192 directly, while 1..2 starts at raw
    // magnitude 0x4000. Captured retail examples:
    //   -1.0 -> 00 C0, -0.25 -> 00 88, +0.5 -> 00 10.
    const float sign = (raw & 0x8000U) != 0U ? -1.0F : 1.0F;
    const auto magnitude = static_cast<std::uint16_t>(raw & 0x7FFFU);
    if (magnitude >= 0x4000U) {
        return sign *
               (static_cast<float>(magnitude - 0x2000U) / 8192.0F);
    }
    return sign * (static_cast<float>(magnitude) / 8192.0F);
}

[[nodiscard]] std::uint16_t to_orientation(float value) noexcept {
    if (!std::isfinite(value)) {
        return 0;
    }
    constexpr float maximum{(32767.0F - 8192.0F) / 8192.0F};
    const float clamped = std::clamp(value, -maximum, maximum);
    const float magnitude_value = std::abs(clamped);
    const auto magnitude = static_cast<std::uint16_t>(std::lround(
        (magnitude_value >= 1.0F ? magnitude_value + 1.0F
                                 : magnitude_value) *
        8192.0F));
    return static_cast<std::uint16_t>(
        magnitude | (std::signbit(clamped) ? 0x8000U : 0U));
}

[[nodiscard]] bool read_fixed(Reader& reader, float& output) noexcept {
    std::int16_t raw{};
    if (!required(reader.integer<std::int16_t>(), raw)) {
        return false;
    }
    output = from_fixed(raw);
    return true;
}

[[nodiscard]] bool read_finite_vec(Reader& reader,
                                   std::array<float, 3U>& output) noexcept {
    for (auto& value : output) {
        if (!required(reader.f32(), value)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool read_fixed_vec(Reader& reader,
                                  std::array<float, 3U>& output) noexcept {
    for (auto& value : output) {
        if (!read_fixed(reader, value)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool read_world_entity(Reader& reader,
                                     WorldEntityUpdateRow& entity) {
    if (!required(reader.integer<std::uint16_t>(), entity.entity_id) ||
        !required(reader.u8(), entity.type) ||
        !required(reader.u8(), entity.state) ||
        !required(reader.u8(), entity.player_id) ||
        !read_fixed_vec(reader, entity.position) ||
        !read_fixed_vec(reader, entity.velocity) ||
        !read_fixed(reader, entity.yaw) ||
        !read_fixed_vec(reader, entity.color) ||
        !read_fixed(reader, entity.radius) ||
        !required(reader.u8(), entity.face) ||
        !read_fixed(reader, entity.fuse)) {
        return false;
    }

    std::uint8_t integer_count{};
    std::uint8_t float_count{};
    if (!required(reader.u8(), integer_count) ||
        !required(reader.u8(), float_count) ||
        !required(reader.u8(), entity.ugc_mode) ||
        integer_count > 64U || float_count > 64U) {
        return false;
    }
    entity.integer_properties.reserve(integer_count);
    for (std::uint8_t index{}; index < integer_count; ++index) {
        std::int32_t value{};
        if (!required(reader.integer<std::int32_t>(), value)) {
            return false;
        }
        entity.integer_properties.push_back(value);
    }
    entity.float_properties.reserve(float_count);
    for (std::uint8_t index{}; index < float_count; ++index) {
        float value{};
        if (!read_fixed(reader, value)) {
            return false;
        }
        entity.float_properties.push_back(value);
    }
    return true;
}

[[nodiscard]] WeaponDecodeResult malformed(std::string message) {
    return {std::nullopt, std::move(message)};
}

} // namespace

WeaponDecodeResult decode_weapon_packet(std::span<const std::byte> payload) {
    Reader reader{payload};
    std::uint8_t id{};
    if (!required(reader.u8(), id)) {
        return malformed("empty Protocol 168 packet");
    }

    if (id == ClientDataPacket::id) {
        ClientDataPacket packet;
        std::uint8_t player_and_palette{};
        std::array<std::uint16_t, 3U> orientation{};
        std::int16_t deployment{};
        if (!required(reader.integer<std::int32_t>(), packet.loop_count) ||
            !required(reader.u8(), player_and_palette) ||
            !required(reader.u8(), packet.tool_id)) {
            return malformed("malformed ClientData(4) packet");
        }
        for (auto& value : orientation) {
                if (!required(reader.integer<std::uint16_t>(), value)) {
                return malformed("malformed ClientData(4) packet");
            }
        }
        if (!required(reader.u8(), packet.opaque_state) ||
            !required(reader.u8(), packet.movement_flags) ||
            !required(reader.u8(), packet.action_flags) ||
            !required(reader.integer<std::int16_t>(), deployment) || !reader.done()) {
            return malformed("malformed ClientData(4) packet");
        }
        packet.player_id = player_and_palette & 0x7FU;
        packet.palette_enabled = (player_and_palette & 0x80U) != 0U;
        for (std::size_t axis{}; axis < orientation.size(); ++axis) {
            packet.orientation[axis] = from_orientation(orientation[axis]);
        }
        packet.weapon_deployment_yaw = from_fixed(deployment);
        return {WeaponPacket{packet}, {}};
    }

    if (id == ShootPacket::id) {
        ShootPacket packet;
        std::uint8_t flags{};
        if (!required(reader.integer<std::int32_t>(), packet.loop_count) ||
            !required(reader.u8(), packet.shooter_id) ||
            !required(reader.integer<std::int32_t>(), packet.shot_on_world_update) ||
            !read_finite_vec(reader, packet.position) ||
            !read_finite_vec(reader, packet.orientation) ||
            !required(reader.integer<std::int16_t>(), packet.damage) ||
            !required(reader.integer<std::int16_t>(), packet.penetration) ||
            !required(reader.u8(), flags) || !required(reader.u8(), packet.seed) ||
            !reader.done()) {
            return malformed("malformed Shoot(6) packet");
        }
        packet.affect_shooter = (flags & 0x01U) != 0U;
        packet.secondary = (flags & 0x02U) != 0U;
        return {WeaponPacket{packet}, {}};
    }

    if (id == ShootFeedbackPacket::id) {
        ShootFeedbackPacket packet;
        if (!required(reader.integer<std::int32_t>(), packet.loop_count) ||
            !required(reader.u8(), packet.shooter_id) ||
            !required(reader.u8(), packet.tool_id) ||
            !required(reader.integer<std::int32_t>(), packet.shot_on_world_update) ||
            !required(reader.u8(), packet.seed) || !reader.done()) {
            return malformed("malformed ShootFeedback(8) packet");
        }
        return {WeaponPacket{packet}, {}};
    }

    if (id == ShootResponsePacket::id) {
        ShootResponsePacket packet;
        std::uint8_t blood{};
        if (!required(reader.u8(), packet.damage_by) ||
            !required(reader.u8(), packet.damaged) || !required(reader.u8(), blood)) {
            return malformed("malformed ShootResponse(9) packet");
        }
        for (auto& value : packet.position) {
            if (!read_fixed(reader, value)) {
                return malformed("malformed ShootResponse(9) packet");
            }
        }
        if (!reader.done()) {
            return malformed("malformed ShootResponse(9) packet");
        }
        packet.blood = blood != 0U;
        return {WeaponPacket{packet}, {}};
    }

    if (id == UseOrientedItemPacket::id) {
        UseOrientedItemPacket packet;
        if (!required(reader.integer<std::int32_t>(), packet.loop_count) ||
            !required(reader.u8(), packet.player_id) ||
            !required(reader.u8(), packet.tool_id) || !read_fixed(reader, packet.value)) {
            return malformed("malformed UseOrientedItem(10) packet");
        }
        for (auto* vector : {&packet.position, &packet.velocity}) {
            for (auto& value : *vector) {
                if (!read_fixed(reader, value)) {
                    return malformed("malformed UseOrientedItem(10) packet");
                }
            }
        }
        if (!reader.done()) {
            return malformed("malformed UseOrientedItem(10) packet");
        }
        return {WeaponPacket{packet}, {}};
    }

    if (id == SetClassLoadoutPacket::id) {
        SetClassLoadoutPacket packet;
        std::uint8_t instant{};
        std::uint8_t count{};
        if (!required(reader.u8(), packet.player_id) ||
            !required(reader.u8(), packet.class_id) ||
            !required(reader.u8(), instant) || !required(reader.u8(), count)) {
            return malformed("malformed SetClassLoadout(13) packet");
        }
        packet.instant = instant != 0U;
        packet.loadout.reserve(count);
        for (std::uint16_t index{}; index < count; ++index) {
            std::uint8_t tool{};
            if (!required(reader.u8(), tool)) {
                return malformed("malformed SetClassLoadout(13) loadout");
            }
            packet.loadout.push_back(tool);
        }
        if (!required(reader.u8(), count)) {
            return malformed("malformed SetClassLoadout(13) prefab count");
        }
        packet.prefabs.reserve(count);
        for (std::uint16_t index{}; index < count; ++index) {
            auto prefab = reader.string();
            if (!prefab.has_value()) {
                return malformed("malformed SetClassLoadout(13) prefab string");
            }
            packet.prefabs.push_back(std::move(*prefab));
        }
        if (!required(reader.u8(), count)) {
            return malformed("malformed SetClassLoadout(13) UGC count");
        }
        packet.ugc_tools.reserve(count);
        for (std::uint16_t index{}; index < count; ++index) {
            std::uint8_t tool{};
            if (!required(reader.u8(), tool)) {
                return malformed("malformed SetClassLoadout(13) UGC tools");
            }
            packet.ugc_tools.push_back(tool);
        }
        if (!reader.done()) {
            return malformed("malformed SetClassLoadout(13) trailing bytes");
        }
        return {WeaponPacket{std::move(packet)}, {}};
    }

    if (id == RestockPacket::id) {
        RestockPacket packet;
        if (!required(reader.u8(), packet.player_id) ||
            !required(reader.u8(), packet.type) || !reader.done()) {
            return malformed("malformed Restock(69) packet");
        }
        return {WeaponPacket{packet}, {}};
    }

    if (id == WeaponReloadPacket::id) {
        WeaponReloadPacket packet;
        std::uint8_t done{};
        if (!required(reader.u8(), packet.player_id) ||
            !required(reader.u8(), packet.tool_id) || !required(reader.u8(), done) ||
            done > 1U || !reader.done()) {
            return malformed("malformed WeaponReload(76) packet");
        }
        packet.is_done = done != 0U;
        return {WeaponPacket{packet}, {}};
    }
    return malformed("packet is not a supported Protocol 168 weapon packet");
}

std::vector<std::byte> encode_packet(const ClientDataPacket& packet) {
    Writer writer;
    writer.u8(ClientDataPacket::id);
    writer.integer(packet.loop_count);
    writer.u8(static_cast<std::uint8_t>((packet.player_id & 0x7FU) |
                                        (packet.palette_enabled ? 0x80U : 0U)));
    writer.u8(packet.tool_id);
    for (const auto value : packet.orientation) {
        writer.integer(to_orientation(value));
    }
    writer.u8(packet.opaque_state);
    writer.u8(packet.movement_flags);
    writer.u8(packet.action_flags);
    writer.integer(to_fixed(packet.weapon_deployment_yaw));
    return std::move(writer).take();
}

std::vector<std::byte> encode_packet(const ShootPacket& packet) {
    Writer writer;
    writer.u8(ShootPacket::id);
    writer.integer(packet.loop_count);
    writer.u8(packet.shooter_id);
    writer.integer(packet.shot_on_world_update);
    for (const auto value : packet.position) {
        writer.f32(value);
    }
    for (const auto value : packet.orientation) {
        writer.f32(value);
    }
    writer.integer(packet.damage);
    writer.integer(packet.penetration);
    writer.u8(static_cast<std::uint8_t>((packet.affect_shooter ? 0x01U : 0U) |
                                        (packet.secondary ? 0x02U : 0U)));
    writer.u8(packet.seed);
    return std::move(writer).take();
}

std::vector<std::byte> encode_packet(const ShootFeedbackPacket& packet) {
    Writer writer;
    writer.u8(ShootFeedbackPacket::id);
    writer.integer(packet.loop_count);
    writer.u8(packet.shooter_id);
    writer.u8(packet.tool_id);
    writer.integer(packet.shot_on_world_update);
    writer.u8(packet.seed);
    return std::move(writer).take();
}

std::vector<std::byte> encode_packet(const ShootResponsePacket& packet) {
    Writer writer;
    writer.u8(ShootResponsePacket::id);
    writer.u8(packet.damage_by);
    writer.u8(packet.damaged);
    writer.u8(packet.blood ? 1U : 0U);
    for (const auto value : packet.position) {
        writer.integer(to_fixed(value));
    }
    return std::move(writer).take();
}

std::vector<std::byte> encode_packet(const UseOrientedItemPacket& packet) {
    Writer writer;
    writer.u8(UseOrientedItemPacket::id);
    writer.integer(packet.loop_count);
    writer.u8(packet.player_id);
    writer.u8(packet.tool_id);
    writer.integer(to_fixed(packet.value));
    for (const auto value : packet.position) {
        writer.integer(to_fixed(value));
    }
    for (const auto value : packet.velocity) {
        writer.integer(to_fixed(value));
    }
    return std::move(writer).take();
}

std::vector<std::byte> encode_packet(const SetClassLoadoutPacket& packet) {
    Writer writer;
    writer.u8(SetClassLoadoutPacket::id);
    writer.u8(packet.player_id);
    writer.u8(packet.class_id);
    writer.u8(packet.instant ? 1U : 0U);
    const auto loadout_count = std::min<std::size_t>(packet.loadout.size(), 255U);
    writer.u8(static_cast<std::uint8_t>(loadout_count));
    for (std::size_t index{}; index < loadout_count; ++index) {
        writer.u8(packet.loadout[index]);
    }
    const auto prefab_count = std::min<std::size_t>(packet.prefabs.size(), 255U);
    writer.u8(static_cast<std::uint8_t>(prefab_count));
    for (std::size_t index{}; index < prefab_count; ++index) {
        writer.string(packet.prefabs[index]);
    }
    const auto ugc_count = std::min<std::size_t>(packet.ugc_tools.size(), 255U);
    writer.u8(static_cast<std::uint8_t>(ugc_count));
    for (std::size_t index{}; index < ugc_count; ++index) {
        writer.u8(packet.ugc_tools[index]);
    }
    return std::move(writer).take();
}

std::vector<std::byte> encode_packet(const RestockPacket& packet) {
    Writer writer;
    writer.u8(RestockPacket::id);
    writer.u8(packet.player_id);
    writer.u8(packet.type);
    return std::move(writer).take();
}

std::vector<std::byte> encode_packet(const WeaponReloadPacket& packet) {
    Writer writer;
    writer.u8(WeaponReloadPacket::id);
    writer.u8(packet.player_id);
    writer.u8(packet.tool_id);
    writer.u8(packet.is_done ? 1U : 0U);
    return std::move(writer).take();
}

WorldWeaponRowsResult
decode_world_update_weapon_rows(std::span<const std::byte> payload) {
    WorldWeaponRowsResult result;
    Reader reader{payload};
    std::uint8_t id{};
    std::int16_t player_count{};
    if (!required(reader.u8(), id) || id != 2U ||
        !required(reader.integer<std::int32_t>(), result.loop_count) ||
        !required(reader.integer<std::int16_t>(), player_count) || player_count < 0) {
        result.error = "malformed WorldUpdate(2) player header";
        return result;
    }
    result.rows.reserve(static_cast<std::size_t>(player_count));
    for (std::int32_t index{}; index < player_count; ++index) {
        WorldPlayerWeaponRow row;
        std::int16_t jetpack_fuel{};
        std::int16_t spawn_protection{};
        std::int16_t deployment{};
        if (!required(reader.u8(), row.player_id) ||
            !read_finite_vec(reader, row.position) ||
            !read_finite_vec(reader, row.orientation) ||
            !read_finite_vec(reader, row.velocity) ||
            !required(reader.integer<std::int16_t>(), row.ping) ||
            !required(reader.integer<std::int32_t>(), row.acknowledged_client_loop) ||
            !required(reader.integer<std::int16_t>(), row.health) ||
            !required(reader.u8(), row.input_flags) ||
            !required(reader.u8(), row.action_flags) ||
            !required(reader.u8(), row.state_flags) ||
            !required(reader.u8(), row.tool_id) ||
            !required(reader.u8(), row.pickup_id) ||
            !required(reader.integer<std::int16_t>(), jetpack_fuel) ||
            !required(reader.integer<std::int16_t>(), spawn_protection) ||
            !required(reader.integer<std::int16_t>(), deployment)) {
            result.rows.clear();
            result.error = "truncated WorldUpdate(2) player row";
            return result;
        }
        row.jetpack_fuel = from_fixed(jetpack_fuel);
        row.spawn_protection = from_fixed(spawn_protection);
        row.weapon_deployment_yaw = from_fixed(deployment);
        result.rows.push_back(row);
    }
    result.consumed_bytes = reader.offset();
    return result;
}

WorldUpdateTailResult
decode_world_update_tail(std::span<const std::byte> payload,
                         std::size_t entity_count_offset) {
    WorldUpdateTailResult result;
    if (entity_count_offset > payload.size()) {
        result.error = "WorldUpdate entity suffix offset exceeds packet size";
        return result;
    }
    Reader reader{payload.subspan(entity_count_offset)};
    std::int16_t entity_count{};
    if (!required(reader.integer<std::int16_t>(), entity_count) ||
        entity_count < 0 || entity_count > 4096) {
        result.error = "malformed WorldUpdate entity count";
        return result;
    }
    result.entities.reserve(static_cast<std::size_t>(entity_count));
    for (std::int32_t index{}; index < entity_count; ++index) {
        WorldEntityUpdateRow entity;
        if (!read_world_entity(reader, entity)) {
            result.entities.clear();
            result.error = "truncated or malformed WorldUpdate entity row";
            return result;
        }
        result.entities.push_back(std::move(entity));
    }

    std::int16_t turret_count{};
    if (!required(reader.integer<std::int16_t>(), turret_count) ||
        turret_count < 0 || turret_count > 1024) {
        result.entities.clear();
        result.error = "malformed WorldUpdate turret count";
        return result;
    }
    result.rocket_turrets.reserve(static_cast<std::size_t>(turret_count));
    for (std::int32_t index{}; index < turret_count; ++index) {
        WorldRocketTurretRow turret;
        if (!required(reader.integer<std::uint16_t>(), turret.entity_id) ||
            !read_fixed(reader, turret.yaw) ||
            !read_fixed(reader, turret.pitch)) {
            result.entities.clear();
            result.rocket_turrets.clear();
            result.error = "truncated WorldUpdate turret row";
            return result;
        }
        result.rocket_turrets.push_back(turret);
    }
    if (!reader.done()) {
        result.entities.clear();
        result.rocket_turrets.clear();
        result.error = "WorldUpdate has trailing bytes after turret rows";
    }
    return result;
}

} // namespace battlespades::network
