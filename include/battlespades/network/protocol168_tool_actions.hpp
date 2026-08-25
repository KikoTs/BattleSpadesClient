#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace battlespades::network {

/** Client request to use the current command/objective tool. */
struct UseCommandPacket final {
    static constexpr std::uint8_t id{86U};
};

/** Drop a carried objective/pickup with fixed-point position and velocity. */
struct DropPickupPacket final {
    static constexpr std::uint8_t id{71U};
    std::int32_t loop_count{};
    std::uint8_t player_id{};
    std::uint8_t pickup_id{};
    std::array<float, 3U> position{};
    std::array<float, 3U> velocity{};
};

/** Client block placement request. Coordinates are raw voxel shorts. */
struct BlockBuildPacket final {
    static constexpr std::uint8_t id{32U};
    std::int32_t loop_count{};
    std::uint8_t player_id{};
    std::array<std::int16_t, 3U> position{};
    std::uint8_t block_type{};
};

/** Client request to liberate one voxel after a digging/melee action. */
struct BlockLiberatePacket final {
    static constexpr std::uint8_t id{35U};
    std::int32_t loop_count{};
    std::uint8_t player_id{};
    std::array<std::int16_t, 3U> position{};
};

/** Block Sucker warm-up/fire state. State is 0=off, 1=warm-up, 2=active. */
struct BlockSuckerPacket final {
    static constexpr std::uint8_t id{94U};
    std::int32_t loop_count{};
    std::uint8_t shooter_id{};
    std::uint8_t state{};
    bool shot{};
};

struct DetonateC4Packet final {
    static constexpr std::uint8_t id{93U};
    std::int32_t loop_count{};
};

struct DisguisePacket final {
    static constexpr std::uint8_t id{95U};
    std::int32_t loop_count{};
    bool active{};
};

/** Surface-attached C4. Position is the raw solid voxel selected by retail. */
struct PlaceC4Packet final {
    static constexpr std::uint8_t id{92U};
    std::int32_t loop_count{};
    std::array<std::int16_t, 3U> position{};
    std::uint8_t face{};
};

/** Timed charge attached to one raw solid voxel face. */
struct PlaceDynamitePacket final {
    static constexpr std::uint8_t id{1U};
    std::int32_t loop_count{};
    std::array<std::int16_t, 3U> position{};
    std::uint8_t face{};
};

/** Flare entity position in raw voxel shorts. */
struct PlaceFlareBlockPacket final {
    static constexpr std::uint8_t id{104U};
    std::int32_t loop_count{};
    std::array<std::int16_t, 3U> position{};
};

/** Ground-only landmine position in raw voxel shorts. */
struct PlaceLandminePacket final {
    static constexpr std::uint8_t id{89U};
    std::int32_t loop_count{};
    std::uint8_t player_id{};
    std::array<std::int16_t, 3U> position{};
};

/** Mounted-gun position is raw voxels; only yaw uses 1/64 fixed-point. */
struct PlaceMachineGunPacket final {
    static constexpr std::uint8_t id{87U};
    std::int32_t loop_count{};
    std::uint8_t player_id{};
    std::array<std::int16_t, 3U> position{};
    float yaw{};
};

/** Ground-only medpack position plus the supporting voxel face. */
struct PlaceMedPackPacket final {
    static constexpr std::uint8_t id{90U};
    std::int32_t loop_count{};
    std::uint8_t player_id{};
    std::array<std::int16_t, 3U> position{};
    std::uint8_t face{};
};

/** Ground-only radar position in raw voxel shorts. */
struct PlaceRadarStationPacket final {
    static constexpr std::uint8_t id{91U};
    std::int32_t loop_count{};
    std::uint8_t player_id{};
    std::array<std::int16_t, 3U> position{};
};

/** Rocket-turret position is raw voxels; only yaw uses 1/64 fixed-point. */
struct PlaceRocketTurretPacket final {
    static constexpr std::uint8_t id{88U};
    std::int32_t loop_count{};
    std::uint8_t player_id{};
    std::array<std::int16_t, 3U> position{};
    float yaw{};
};

/** UGC editor entity placement. Position is raw voxel coordinates, not fixed. */
struct PlaceUgcPacket final {
    static constexpr std::uint8_t id{97U};
    std::int32_t loop_count{};
    std::array<std::int16_t, 3U> position{};
    std::uint8_t ugc_item_id{};
    bool placing{};
};

struct PaintBlockPacket final {
    static constexpr std::uint8_t id{7U};
    std::int32_t loop_count{};
    std::array<std::int16_t, 3U> position{};
    /** Conventional 0xRRGGBB; the wire stores B, G, R. */
    std::uint32_t color{};
};

struct BuildPrefabActionPacket final {
    static constexpr std::uint8_t id{30U};
    std::int32_t loop_count{};
    std::string prefab_name;
    std::uint8_t player_id{};
    std::uint8_t yaw{};
    std::uint8_t pitch{};
    std::uint8_t roll{};
    std::int32_t from_block_index{};
    std::int32_t to_block_index{};
    /** Build uses raw voxel shorts. */
    std::array<std::int16_t, 3U> position{};
    std::uint32_t color{};
    bool add_to_user_blocks{};
};

struct ErasePrefabActionPacket final {
    static constexpr std::uint8_t id{31U};
    std::int32_t loop_count{};
    std::string prefab_name;
    std::uint8_t player_id{};
    std::uint8_t yaw{};
    std::uint8_t pitch{};
    std::uint8_t roll{};
    std::int32_t from_block_index{};
    std::int32_t to_block_index{};
    /** Erase uniquely uses sign-magnitude 1/64 fixed-point. */
    std::array<float, 3U> position{};
};

using ToolActionPacket =
    std::variant<UseCommandPacket, DropPickupPacket, BlockBuildPacket,
                 BlockLiberatePacket,
                 BlockSuckerPacket, DetonateC4Packet, DisguisePacket,
                 PlaceC4Packet, PlaceDynamitePacket,
                 PlaceFlareBlockPacket, PlaceLandminePacket,
                 PlaceMachineGunPacket, PlaceMedPackPacket,
                 PlaceRadarStationPacket, PlaceRocketTurretPacket,
                 PlaceUgcPacket, PaintBlockPacket, BuildPrefabActionPacket,
                 ErasePrefabActionPacket>;

struct ToolActionDecodeResult final {
    std::optional<ToolActionPacket> packet;
    std::string error;
    [[nodiscard]] explicit operator bool() const noexcept {
        return packet.has_value();
    }
};

/** Decode exactly one complete special-tool packet; trailing bytes fail closed. */
[[nodiscard]] ToolActionDecodeResult
decode_tool_action_packet(std::span<const std::byte> payload);

[[nodiscard]] std::vector<std::byte> encode_packet(const UseCommandPacket& packet);
[[nodiscard]] std::vector<std::byte> encode_packet(const DropPickupPacket& packet);
[[nodiscard]] std::vector<std::byte> encode_packet(const BlockBuildPacket& packet);
[[nodiscard]] std::vector<std::byte> encode_packet(const BlockLiberatePacket& packet);
[[nodiscard]] std::vector<std::byte> encode_packet(const BlockSuckerPacket& packet);
[[nodiscard]] std::vector<std::byte> encode_packet(const DetonateC4Packet& packet);
[[nodiscard]] std::vector<std::byte> encode_packet(const DisguisePacket& packet);
[[nodiscard]] std::vector<std::byte> encode_packet(const PlaceC4Packet& packet);
[[nodiscard]] std::vector<std::byte> encode_packet(const PlaceDynamitePacket& packet);
[[nodiscard]] std::vector<std::byte> encode_packet(const PlaceFlareBlockPacket& packet);
[[nodiscard]] std::vector<std::byte> encode_packet(const PlaceLandminePacket& packet);
[[nodiscard]] std::vector<std::byte> encode_packet(const PlaceMachineGunPacket& packet);
[[nodiscard]] std::vector<std::byte> encode_packet(const PlaceMedPackPacket& packet);
[[nodiscard]] std::vector<std::byte> encode_packet(const PlaceRadarStationPacket& packet);
[[nodiscard]] std::vector<std::byte> encode_packet(const PlaceRocketTurretPacket& packet);
[[nodiscard]] std::vector<std::byte> encode_packet(const PlaceUgcPacket& packet);
[[nodiscard]] std::vector<std::byte> encode_packet(const PaintBlockPacket& packet);
[[nodiscard]] std::vector<std::byte> encode_packet(const BuildPrefabActionPacket& packet);
[[nodiscard]] std::vector<std::byte> encode_packet(const ErasePrefabActionPacket& packet);

} // namespace battlespades::network
