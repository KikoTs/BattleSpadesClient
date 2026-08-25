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

/** Client-to-server input snapshot. Tool selection is carried on every row. */
struct ClientDataPacket final {
    static constexpr std::uint8_t id{4U};
    std::int32_t loop_count{};
    std::uint8_t player_id{};
    bool palette_enabled{};
    std::uint8_t tool_id{};
    std::array<float, 3U> orientation{};
    std::uint8_t opaque_state{};
    std::uint8_t movement_flags{};
    std::uint8_t action_flags{};
    float weapon_deployment_yaw{};
};

/** Stock retail's loop-derived four-bit ClientData `ooo` value. */
[[nodiscard]] constexpr std::uint8_t
protocol168_client_data_opaque_state(std::int32_t loop_count) noexcept {
    return static_cast<std::uint8_t>(
        (static_cast<std::uint32_t>(loop_count) + 7U) & 0x0FU);
}

/** Client-to-server authoritative hit proposal; never accepted from observers. */
struct ShootPacket final {
    static constexpr std::uint8_t id{6U};
    std::int32_t loop_count{};
    std::uint8_t shooter_id{};
    std::int32_t shot_on_world_update{};
    std::array<float, 3U> position{};
    std::array<float, 3U> orientation{};
    std::int16_t damage{};
    std::int16_t penetration{};
    bool affect_shooter{};
    bool secondary{};
    std::uint8_t seed{};
};

/** Server-to-client shot animation seed for every non-owning observer. */
struct ShootFeedbackPacket final {
    static constexpr std::uint8_t id{8U};
    std::int32_t loop_count{};
    std::uint8_t shooter_id{};
    std::uint8_t tool_id{};
    std::int32_t shot_on_world_update{};
    std::uint8_t seed{};
};

/** Server-to-client hit marker/blood response. */
struct ShootResponsePacket final {
    static constexpr std::uint8_t id{9U};
    std::uint8_t damage_by{};
    std::uint8_t damaged{};
    bool blood{};
    std::array<float, 3U> position{};
};

/** Bidirectional projectile/deployable launch in fixed-point world space. */
struct UseOrientedItemPacket final {
    static constexpr std::uint8_t id{10U};
    std::int32_t loop_count{};
    std::uint8_t player_id{};
    std::uint8_t tool_id{};
    float value{};
    std::array<float, 3U> position{};
    std::array<float, 3U> velocity{};
};

/** Transactional class equipment selection; strings are NUL terminated. */
struct SetClassLoadoutPacket final {
    static constexpr std::uint8_t id{13U};
    std::uint8_t player_id{};
    std::uint8_t class_id{};
    bool instant{};
    std::vector<std::uint8_t> loadout;
    std::vector<std::string> prefabs;
    std::vector<std::uint8_t> ugc_tools;
};

/** Server-to-client pickup/spawn counter reset. */
struct RestockPacket final {
    static constexpr std::uint8_t id{69U};
    std::uint8_t player_id{};
    std::uint8_t type{};
};

/** Bidirectional reload edge. is_done=false starts, true commits counters. */
struct WeaponReloadPacket final {
    static constexpr std::uint8_t id{76U};
    std::uint8_t player_id{};
    std::uint8_t tool_id{};
    bool is_done{};
};

using WeaponPacket =
    std::variant<ClientDataPacket, ShootPacket, ShootFeedbackPacket,
                 ShootResponsePacket, UseOrientedItemPacket,
                 SetClassLoadoutPacket, RestockPacket, WeaponReloadPacket>;

struct WeaponDecodeResult final {
    std::optional<WeaponPacket> packet;
    std::string error;

    [[nodiscard]] explicit operator bool() const noexcept {
        return packet.has_value();
    }
};

/** Decode exactly one complete supported Protocol 168 weapon packet. */
[[nodiscard]] WeaponDecodeResult
decode_weapon_packet(std::span<const std::byte> payload);

[[nodiscard]] std::vector<std::byte> encode_packet(const ClientDataPacket& packet);
[[nodiscard]] std::vector<std::byte> encode_packet(const ShootPacket& packet);
[[nodiscard]] std::vector<std::byte>
encode_packet(const ShootFeedbackPacket& packet);
[[nodiscard]] std::vector<std::byte>
encode_packet(const ShootResponsePacket& packet);
[[nodiscard]] std::vector<std::byte>
encode_packet(const UseOrientedItemPacket& packet);
[[nodiscard]] std::vector<std::byte>
encode_packet(const SetClassLoadoutPacket& packet);
[[nodiscard]] std::vector<std::byte> encode_packet(const RestockPacket& packet);
[[nodiscard]] std::vector<std::byte>
encode_packet(const WeaponReloadPacket& packet);

/** Weapon-relevant portion of one WorldUpdate(2) player row. */
struct WorldPlayerWeaponRow final {
    std::uint8_t player_id{};
    std::array<float, 3U> position{};
    std::array<float, 3U> orientation{};
    std::array<float, 3U> velocity{};
    std::int16_t ping{};
    std::int32_t acknowledged_client_loop{};
    std::int16_t health{};
    std::uint8_t input_flags{};
    std::uint8_t action_flags{};
    /** Parachute/disguise/fire state. This is not the selected tool byte. */
    std::uint8_t state_flags{};
    std::uint8_t tool_id{};
    std::uint8_t pickup_id{};
    float jetpack_fuel{};
    float spawn_protection{};
    float weapon_deployment_yaw{};
};

/**
 * Server-to-client WorldUpdate action bits.
 *
 * These are deliberately not shared with ClientData(4): retail uses 0x04 for
 * zoom in the client input byte, while the authoritative WorldUpdate row
 * repacks zoom at 0x40 and uses 0x04 for jetpack state. Mixing the two packet
 * directions silently hides observer effects such as the sniper laser.
 */
inline constexpr std::uint8_t world_action_primary{0x01U};
inline constexpr std::uint8_t world_action_secondary{0x02U};
inline constexpr std::uint8_t world_action_jetpack{0x04U};
inline constexpr std::uint8_t world_action_display_weapon{0x10U};
inline constexpr std::uint8_t world_action_on_fire{0x20U};
inline constexpr std::uint8_t world_action_zoom{0x40U};
inline constexpr std::uint8_t world_action_weapon_deployed{0x80U};

[[nodiscard]] constexpr bool
world_player_zoomed(std::uint8_t action_flags) noexcept {
    return (action_flags & world_action_zoom) != 0U;
}

/** One full Entity record embedded in the WorldUpdate suffix. */
struct WorldEntityUpdateRow final {
    std::uint16_t entity_id{};
    std::uint8_t type{};
    std::uint8_t state{};
    std::uint8_t player_id{};
    std::array<float, 3U> position{};
    std::array<float, 3U> velocity{};
    float yaw{};
    std::array<float, 3U> color{};
    float radius{};
    std::uint8_t face{};
    float fuse{};
    std::uint8_t ugc_mode{};
    std::vector<std::int32_t> integer_properties;
    std::vector<float> float_properties;

    /**
     * Entity.color is mandatory on the wire, but all zeroes are the stock
     * sentinel for "use the entity/team material". Treating the sentinel as
     * authored black hides the team-colour pixels on mines and turrets.
     */
    [[nodiscard]] bool has_explicit_color() const noexcept {
        return color[0U] != 0.0F || color[1U] != 0.0F || color[2U] != 0.0F;
    }
};

/** Server-owned rocket-turret articulation appended after ordinary entities. */
struct WorldRocketTurretRow final {
    std::uint16_t entity_id{};
    float yaw{};
    float pitch{};
};

struct WorldUpdateTailResult final {
    std::vector<WorldEntityUpdateRow> entities;
    std::vector<WorldRocketTurretRow> rocket_turrets;
    std::string error;

    [[nodiscard]] explicit operator bool() const noexcept { return error.empty(); }
};

struct WorldWeaponRowsResult final {
    std::int32_t loop_count{};
    std::vector<WorldPlayerWeaponRow> rows;
    /** Offset at which WorldUpdate entity_count begins. */
    std::size_t consumed_bytes{};
    std::string error;

    [[nodiscard]] explicit operator bool() const noexcept { return error.empty(); }
};

/**
 * Decode WorldUpdate's complete player prefix without guessing Entity layout.
 * The session parser resumes at consumed_bytes for entities and turrets.
 */
[[nodiscard]] WorldWeaponRowsResult
decode_world_update_weapon_rows(std::span<const std::byte> payload);

/**
 * Decode WorldUpdate's entity and turret suffix beginning at the entity-count
 * offset returned by decode_world_update_weapon_rows(). Counts and variable
 * property arrays are bounded before allocation so a malformed server packet
 * fails closed.
 */
[[nodiscard]] WorldUpdateTailResult
decode_world_update_tail(std::span<const std::byte> payload,
                         std::size_t entity_count_offset);

} // namespace battlespades::network
