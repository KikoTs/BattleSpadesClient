#include "battlespades/network/protocol168_tool_actions.hpp"

#include <algorithm>
#include <cmath>
#include <type_traits>
#include <utility>

namespace battlespades::network {
namespace {

class Reader final {
public:
    explicit Reader(std::span<const std::byte> bytes) : bytes_{bytes} {}

    [[nodiscard]] bool done() const noexcept { return offset_ == bytes_.size(); }
    [[nodiscard]] std::size_t remaining() const noexcept {
        return bytes_.size() - offset_;
    }

    [[nodiscard]] std::optional<std::uint8_t> u8() noexcept {
        if (remaining() < 1U) {
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

    [[nodiscard]] std::optional<std::string> string() {
        const auto start = offset_;
        while (offset_ < bytes_.size() && bytes_[offset_] != std::byte{0U}) {
            ++offset_;
        }
        if (offset_ == bytes_.size()) {
            return std::nullopt;
        }
        std::string value;
        value.reserve(offset_ - start);
        for (auto index = start; index < offset_; ++index) {
            value.push_back(
                static_cast<char>(std::to_integer<std::uint8_t>(bytes_[index])));
        }
        ++offset_;
        return value;
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

    void string(std::string_view value) {
        for (const auto character : value) {
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
    const auto magnitude = static_cast<float>(bits & 0x7FFFU) / 64.0F;
    return (bits & 0x8000U) != 0U ? -magnitude : magnitude;
}

[[nodiscard]] std::int16_t to_fixed(float value) noexcept {
    if (!std::isfinite(value)) {
        return 0;
    }
    const auto integral = static_cast<std::int32_t>(value * 64.0F + 0.5F);
    const auto magnitude = static_cast<std::uint16_t>(std::min(
        std::abs(integral), static_cast<std::int32_t>(0x7FFF)));
    const auto bits = static_cast<std::uint16_t>(
        magnitude | (integral < 0 ? 0x8000U : 0U));
    return static_cast<std::int16_t>(bits);
}

[[nodiscard]] bool read_fixed_position(Reader& reader,
                                       std::array<float, 3U>& output) noexcept {
    for (auto& value : output) {
        std::int16_t raw{};
        if (!required(reader.integer<std::int16_t>(), raw)) {
            return false;
        }
        value = from_fixed(raw);
    }
    return true;
}

void write_fixed_position(Writer& writer,
                          const std::array<float, 3U>& position) {
    for (const auto value : position) {
        writer.integer(to_fixed(value));
    }
}

[[nodiscard]] bool read_voxel_position(
    Reader& reader, std::array<std::int16_t, 3U>& output) noexcept {
    for (auto& value : output) {
        if (!required(reader.integer<std::int16_t>(), value)) {
            return false;
        }
    }
    return true;
}

void write_voxel_position(Writer& writer,
                          const std::array<std::int16_t, 3U>& position) {
    for (const auto value : position) {
        writer.integer(value);
    }
}

[[nodiscard]] bool read_color(Reader& reader, std::uint32_t& color) noexcept {
    std::uint8_t blue{};
    std::uint8_t green{};
    std::uint8_t red{};
    if (!required(reader.u8(), blue) || !required(reader.u8(), green) ||
        !required(reader.u8(), red)) {
        return false;
    }
    color = (static_cast<std::uint32_t>(red) << 16U) |
            (static_cast<std::uint32_t>(green) << 8U) |
            static_cast<std::uint32_t>(blue);
    return true;
}

void write_color(Writer& writer, std::uint32_t color) {
    writer.u8(static_cast<std::uint8_t>(color));
    writer.u8(static_cast<std::uint8_t>(color >> 8U));
    writer.u8(static_cast<std::uint8_t>(color >> 16U));
}

template <typename Packet>
[[nodiscard]] ToolActionDecodeResult finish(Reader& reader, Packet packet,
                                            std::string_view name) {
    if (!reader.done()) {
        return {std::nullopt,
                "malformed " + std::string{name} + " packet (trailing bytes)"};
    }
    return {ToolActionPacket{std::move(packet)}, {}};
}

[[nodiscard]] ToolActionDecodeResult malformed(std::string_view name) {
    return {std::nullopt, "malformed " + std::string{name} + " packet"};
}

template <typename Packet>
[[nodiscard]] bool read_loop_voxel_position(Reader& reader,
                                            Packet& packet) noexcept {
    return required(reader.integer<std::int32_t>(), packet.loop_count) &&
           read_voxel_position(reader, packet.position);
}

template <typename Packet>
[[nodiscard]] bool read_player_voxel_position(Reader& reader,
                                              Packet& packet) noexcept {
    return required(reader.integer<std::int32_t>(), packet.loop_count) &&
           required(reader.u8(), packet.player_id) &&
           read_voxel_position(reader, packet.position);
}

template <typename Packet>
[[nodiscard]] std::vector<std::byte>
encode_loop_voxel_position(const Packet& packet) {
    Writer writer;
    writer.u8(Packet::id);
    writer.integer(packet.loop_count);
    write_voxel_position(writer, packet.position);
    return std::move(writer).take();
}

template <typename Packet>
[[nodiscard]] std::vector<std::byte>
encode_player_voxel_position(const Packet& packet) {
    Writer writer;
    writer.u8(Packet::id);
    writer.integer(packet.loop_count);
    writer.u8(packet.player_id);
    write_voxel_position(writer, packet.position);
    return std::move(writer).take();
}

} // namespace

ToolActionDecodeResult
decode_tool_action_packet(std::span<const std::byte> payload) {
    Reader reader{payload};
    std::uint8_t id{};
    if (!required(reader.u8(), id)) {
        return {std::nullopt, "empty Protocol 168 packet"};
    }
    if (id == UseCommandPacket::id) {
        return finish(reader, UseCommandPacket{}, "UseCommand(86)");
    }
    if (id == DropPickupPacket::id) {
        DropPickupPacket packet;
        if (!required(reader.integer<std::int32_t>(), packet.loop_count) ||
            !required(reader.u8(), packet.player_id) ||
            !required(reader.u8(), packet.pickup_id) ||
            !read_fixed_position(reader, packet.position) ||
            !read_fixed_position(reader, packet.velocity)) {
            return malformed("DropPickup(71)");
        }
        return finish(reader, packet, "DropPickup(71)");
    }
    if (id == BlockBuildPacket::id) {
        BlockBuildPacket packet;
        if (!required(reader.integer<std::int32_t>(), packet.loop_count) ||
            !required(reader.u8(), packet.player_id) ||
            !read_voxel_position(reader, packet.position) ||
            !required(reader.u8(), packet.block_type)) {
            return malformed("BlockBuild(32)");
        }
        return finish(reader, packet, "BlockBuild(32)");
    }
    if (id == BlockLiberatePacket::id) {
        BlockLiberatePacket packet;
        if (!required(reader.integer<std::int32_t>(), packet.loop_count) ||
            !required(reader.u8(), packet.player_id) ||
            !read_voxel_position(reader, packet.position)) {
            return malformed("BlockLiberate(35)");
        }
        return finish(reader, packet, "BlockLiberate(35)");
    }
    if (id == BlockSuckerPacket::id) {
        BlockSuckerPacket packet;
        std::uint8_t shot{};
        if (!required(reader.integer<std::int32_t>(), packet.loop_count) ||
            !required(reader.u8(), packet.shooter_id) ||
            !required(reader.u8(), packet.state) ||
            !required(reader.u8(), shot) || packet.state > 2U ||
            shot > 1U) {
            return malformed("BlockSucker(94)");
        }
        packet.shot = shot != 0U;
        return finish(reader, packet, "BlockSucker(94)");
    }
    if (id == DetonateC4Packet::id) {
        DetonateC4Packet packet;
        if (!required(reader.integer<std::int32_t>(), packet.loop_count)) {
            return malformed("DetonateC4(93)");
        }
        return finish(reader, packet, "DetonateC4(93)");
    }
    if (id == DisguisePacket::id) {
        DisguisePacket packet;
        std::uint8_t active{};
        if (!required(reader.integer<std::int32_t>(), packet.loop_count) ||
            !required(reader.u8(), active)) {
            return malformed("Disguise(95)");
        }
        packet.active = active != 0U;
        return finish(reader, packet, "Disguise(95)");
    }
    if (id == PlaceC4Packet::id || id == PlaceDynamitePacket::id) {
        if (id == PlaceC4Packet::id) {
            PlaceC4Packet packet;
            if (!read_loop_voxel_position(reader, packet) ||
                !required(reader.u8(), packet.face)) {
                return malformed("PlaceC4(92)");
            }
            return finish(reader, packet, "PlaceC4(92)");
        }
        PlaceDynamitePacket packet;
        if (!read_loop_voxel_position(reader, packet) ||
            !required(reader.u8(), packet.face)) {
            return malformed("PlaceDynamite(1)");
        }
        return finish(reader, packet, "PlaceDynamite(1)");
    }
    if (id == PlaceFlareBlockPacket::id) {
        PlaceFlareBlockPacket packet;
        if (!read_loop_voxel_position(reader, packet)) {
            return malformed("PlaceFlareBlock(104)");
        }
        return finish(reader, packet, "PlaceFlareBlock(104)");
    }
    if (id == PlaceLandminePacket::id || id == PlaceMedPackPacket::id ||
        id == PlaceRadarStationPacket::id || id == PlaceMachineGunPacket::id ||
        id == PlaceRocketTurretPacket::id) {
        if (id == PlaceLandminePacket::id) {
            PlaceLandminePacket packet;
            if (!read_player_voxel_position(reader, packet)) {
                return malformed("PlaceLandmine(89)");
            }
            return finish(reader, packet, "PlaceLandmine(89)");
        }
        if (id == PlaceMedPackPacket::id) {
            PlaceMedPackPacket packet;
            if (!read_player_voxel_position(reader, packet) ||
                !required(reader.u8(), packet.face)) {
                return malformed("PlaceMedPack(90)");
            }
            return finish(reader, packet, "PlaceMedPack(90)");
        }
        if (id == PlaceRadarStationPacket::id) {
            PlaceRadarStationPacket packet;
            if (!read_player_voxel_position(reader, packet)) {
                return malformed("PlaceRadarStation(91)");
            }
            return finish(reader, packet, "PlaceRadarStation(91)");
        }
        if (id == PlaceMachineGunPacket::id) {
            PlaceMachineGunPacket packet;
            std::int16_t raw_yaw{};
            if (!read_player_voxel_position(reader, packet) ||
                !required(reader.integer<std::int16_t>(), raw_yaw)) {
                return malformed("PlaceMG(87)");
            }
            packet.yaw = from_fixed(raw_yaw);
            return finish(reader, packet, "PlaceMG(87)");
        }
        PlaceRocketTurretPacket packet;
        std::int16_t raw_yaw{};
        if (!read_player_voxel_position(reader, packet) ||
            !required(reader.integer<std::int16_t>(), raw_yaw)) {
            return malformed("PlaceRocketTurret(88)");
        }
        packet.yaw = from_fixed(raw_yaw);
        return finish(reader, packet, "PlaceRocketTurret(88)");
    }
    if (id == PlaceUgcPacket::id) {
        PlaceUgcPacket packet;
        std::uint8_t placing{};
        if (!required(reader.integer<std::int32_t>(), packet.loop_count) ||
            !read_voxel_position(reader, packet.position) ||
            !required(reader.u8(), packet.ugc_item_id) ||
            !required(reader.u8(), placing) || placing > 1U) {
            return malformed("PlaceUGC(97)");
        }
        packet.placing = placing != 0U;
        return finish(reader, packet, "PlaceUGC(97)");
    }
    if (id == PaintBlockPacket::id) {
        PaintBlockPacket packet;
        if (!required(reader.integer<std::int32_t>(), packet.loop_count) ||
            !read_voxel_position(reader, packet.position) ||
            !read_color(reader, packet.color)) {
            return malformed("PaintBlock(7)");
        }
        return finish(reader, packet, "PaintBlock(7)");
    }
    if (id == BuildPrefabActionPacket::id ||
        id == ErasePrefabActionPacket::id) {
        if (id == BuildPrefabActionPacket::id) {
            BuildPrefabActionPacket packet;
            std::uint8_t add{};
            if (!required(reader.integer<std::int32_t>(), packet.loop_count) ||
                !required(reader.string(), packet.prefab_name) ||
                !required(reader.u8(), packet.player_id) ||
                !required(reader.u8(), packet.yaw) ||
                !required(reader.u8(), packet.pitch) ||
                !required(reader.u8(), packet.roll) ||
                !required(reader.integer<std::int32_t>(), packet.from_block_index) ||
                !required(reader.integer<std::int32_t>(), packet.to_block_index) ||
                !read_voxel_position(reader, packet.position) ||
                !read_color(reader, packet.color) || !required(reader.u8(), add)) {
                return malformed("BuildPrefabAction(30)");
            }
            packet.add_to_user_blocks = add != 0U;
            return finish(reader, packet, "BuildPrefabAction(30)");
        }
        ErasePrefabActionPacket packet;
        if (!required(reader.integer<std::int32_t>(), packet.loop_count) ||
            !required(reader.string(), packet.prefab_name) ||
            !required(reader.u8(), packet.player_id) ||
            !required(reader.u8(), packet.yaw) ||
            !required(reader.u8(), packet.pitch) ||
            !required(reader.u8(), packet.roll) ||
            !required(reader.integer<std::int32_t>(), packet.from_block_index) ||
            !required(reader.integer<std::int32_t>(), packet.to_block_index) ||
            !read_fixed_position(reader, packet.position)) {
            return malformed("ErasePrefabAction(31)");
        }
        return finish(reader, packet, "ErasePrefabAction(31)");
    }
    return {std::nullopt,
            "packet is not a supported Protocol 168 special-tool packet"};
}

std::vector<std::byte> encode_packet(const UseCommandPacket&) {
    return {static_cast<std::byte>(UseCommandPacket::id)};
}

std::vector<std::byte> encode_packet(const DropPickupPacket& packet) {
    Writer writer;
    writer.u8(DropPickupPacket::id);
    writer.integer(packet.loop_count);
    writer.u8(packet.player_id);
    writer.u8(packet.pickup_id);
    write_fixed_position(writer, packet.position);
    write_fixed_position(writer, packet.velocity);
    return std::move(writer).take();
}

std::vector<std::byte> encode_packet(const BlockBuildPacket& packet) {
    Writer writer;
    writer.u8(BlockBuildPacket::id);
    writer.integer(packet.loop_count);
    writer.u8(packet.player_id);
    write_voxel_position(writer, packet.position);
    writer.u8(packet.block_type);
    return std::move(writer).take();
}

std::vector<std::byte> encode_packet(const BlockLiberatePacket& packet) {
    Writer writer;
    writer.u8(BlockLiberatePacket::id);
    writer.integer(packet.loop_count);
    writer.u8(packet.player_id);
    write_voxel_position(writer, packet.position);
    return std::move(writer).take();
}

std::vector<std::byte> encode_packet(const BlockSuckerPacket& packet) {
    Writer writer;
    writer.u8(BlockSuckerPacket::id);
    writer.integer(packet.loop_count);
    writer.u8(packet.shooter_id);
    writer.u8(packet.state);
    writer.u8(packet.shot ? 1U : 0U);
    return std::move(writer).take();
}

std::vector<std::byte> encode_packet(const DetonateC4Packet& packet) {
    Writer writer;
    writer.u8(DetonateC4Packet::id);
    writer.integer(packet.loop_count);
    return std::move(writer).take();
}

std::vector<std::byte> encode_packet(const DisguisePacket& packet) {
    Writer writer;
    writer.u8(DisguisePacket::id);
    writer.integer(packet.loop_count);
    writer.u8(packet.active ? 1U : 0U);
    return std::move(writer).take();
}

std::vector<std::byte> encode_packet(const PlaceC4Packet& packet) {
    Writer writer;
    writer.u8(PlaceC4Packet::id);
    writer.integer(packet.loop_count);
    write_voxel_position(writer, packet.position);
    writer.u8(packet.face);
    return std::move(writer).take();
}

std::vector<std::byte> encode_packet(const PlaceDynamitePacket& packet) {
    Writer writer;
    writer.u8(PlaceDynamitePacket::id);
    writer.integer(packet.loop_count);
    write_voxel_position(writer, packet.position);
    writer.u8(packet.face);
    return std::move(writer).take();
}

std::vector<std::byte> encode_packet(const PlaceFlareBlockPacket& packet) {
    return encode_loop_voxel_position(packet);
}

std::vector<std::byte> encode_packet(const PlaceLandminePacket& packet) {
    return encode_player_voxel_position(packet);
}

std::vector<std::byte> encode_packet(const PlaceMachineGunPacket& packet) {
    Writer writer;
    writer.u8(PlaceMachineGunPacket::id);
    writer.integer(packet.loop_count);
    writer.u8(packet.player_id);
    write_voxel_position(writer, packet.position);
    writer.integer(to_fixed(packet.yaw));
    return std::move(writer).take();
}

std::vector<std::byte> encode_packet(const PlaceMedPackPacket& packet) {
    Writer writer;
    writer.u8(PlaceMedPackPacket::id);
    writer.integer(packet.loop_count);
    writer.u8(packet.player_id);
    write_voxel_position(writer, packet.position);
    writer.u8(packet.face);
    return std::move(writer).take();
}

std::vector<std::byte> encode_packet(const PlaceRadarStationPacket& packet) {
    return encode_player_voxel_position(packet);
}

std::vector<std::byte> encode_packet(const PlaceRocketTurretPacket& packet) {
    Writer writer;
    writer.u8(PlaceRocketTurretPacket::id);
    writer.integer(packet.loop_count);
    writer.u8(packet.player_id);
    write_voxel_position(writer, packet.position);
    writer.integer(to_fixed(packet.yaw));
    return std::move(writer).take();
}

std::vector<std::byte> encode_packet(const PlaceUgcPacket& packet) {
    Writer writer;
    writer.u8(PlaceUgcPacket::id);
    writer.integer(packet.loop_count);
    write_voxel_position(writer, packet.position);
    writer.u8(packet.ugc_item_id);
    writer.u8(packet.placing ? 1U : 0U);
    return std::move(writer).take();
}

std::vector<std::byte> encode_packet(const PaintBlockPacket& packet) {
    Writer writer;
    writer.u8(PaintBlockPacket::id);
    writer.integer(packet.loop_count);
    write_voxel_position(writer, packet.position);
    write_color(writer, packet.color);
    return std::move(writer).take();
}

std::vector<std::byte> encode_packet(const BuildPrefabActionPacket& packet) {
    Writer writer;
    writer.u8(BuildPrefabActionPacket::id);
    writer.integer(packet.loop_count);
    writer.string(packet.prefab_name);
    writer.u8(packet.player_id);
    writer.u8(packet.yaw);
    writer.u8(packet.pitch);
    writer.u8(packet.roll);
    writer.integer(packet.from_block_index);
    writer.integer(packet.to_block_index);
    write_voxel_position(writer, packet.position);
    write_color(writer, packet.color);
    writer.u8(packet.add_to_user_blocks ? 1U : 0U);
    return std::move(writer).take();
}

std::vector<std::byte> encode_packet(const ErasePrefabActionPacket& packet) {
    Writer writer;
    writer.u8(ErasePrefabActionPacket::id);
    writer.integer(packet.loop_count);
    writer.string(packet.prefab_name);
    writer.u8(packet.player_id);
    writer.u8(packet.yaw);
    writer.u8(packet.pitch);
    writer.u8(packet.roll);
    writer.integer(packet.from_block_index);
    writer.integer(packet.to_block_index);
    write_fixed_position(writer, packet.position);
    return std::move(writer).take();
}

} // namespace battlespades::network
