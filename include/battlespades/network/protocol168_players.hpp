#pragma once

#include "battlespades/world/player_movement.hpp"
#include "battlespades/network/protocol168_weapons.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace battlespades::network {

/** Exact server-to-client CreatePlayer(28) payload. */
struct CreatePlayerPacket final {
    static constexpr std::uint8_t id{28U};
    std::uint8_t player_id{};
    bool demo_player{};
    std::uint8_t class_id{};
    std::uint8_t team{};
    bool dead{};
    std::uint8_t local_language{};
    std::array<float, 3U> position{};
    std::array<float, 3U> orientation{1.0F, 0.0F, 0.0F};
    std::string name;
    std::vector<std::uint8_t> loadout;
    std::vector<std::string> prefabs;
};

struct CreatePlayerDecodeResult final {
    std::optional<CreatePlayerPacket> packet;
    std::string error;
    [[nodiscard]] explicit operator bool() const noexcept {
        return packet.has_value();
    }
};

[[nodiscard]] CreatePlayerDecodeResult
decode_create_player(std::span<const std::byte> payload);
[[nodiscard]] std::vector<std::byte>
encode_packet(const CreatePlayerPacket& packet);

/** Generation-safe render/simulation state created only from packet 28. */
struct RemotePlayerReplica final {
    std::uint8_t player_id{};
    std::uint32_t generation{};
    std::uint8_t class_id{};
    std::uint8_t team{};
    bool dead{};
    bool demo_player{};
    std::uint8_t local_language{};
    world::Vec3 position{};
    world::Vec3 orientation{1.0, 0.0, 0.0};
    world::Vec3 velocity{};
    std::int16_t health{100};
    std::int16_t ping{};
    std::int32_t acknowledged_client_loop{-1};
    std::uint8_t input_flags{};
    std::uint8_t action_flags{0x10U};
    std::uint8_t state_flags{};
    std::uint8_t tool_id{};
    std::uint8_t pickup_id{};
    float jetpack_fuel{};
    float spawn_protection{};
    float weapon_deployment_yaw{};
    bool high_minimap_visibility{};
    bool chase_cam{};
    /** Retail KillAction relationship flags used by draw_player_list. */
    bool dominating_local_player{};
    bool dominated_by_local_player{};
    std::string name;
    std::vector<std::uint8_t> loadout;
    std::vector<std::string> prefabs;
    /** Persisted from SetClassLoadout; CreatePlayer does not carry this suffix. */
    std::vector<std::uint8_t> ugc_tools;
};

struct RemoteMotionSample final {
    world::Vec3 position{};
    world::Vec3 orientation{1.0, 0.0, 0.0};
    world::Vec3 velocity{};
};

/**
 * Presentation-only interpolation between authoritative 30 Hz WorldUpdates.
 * Collision and hit state continue to use RemotePlayerReplica directly; this
 * buffer only prevents observers from rendering each peer as 30 Hz teleports.
 */
class RemoteMotionInterpolator final {
public:
    void reset(RemoteMotionSample sample) noexcept;
    void push(RemoteMotionSample sample, double snapshot_interval) noexcept;
    void tick(double dt) noexcept;
    [[nodiscard]] const RemoteMotionSample& sample() const noexcept;

private:
    RemoteMotionSample current_{};
    RemoteMotionSample start_{};
    RemoteMotionSample target_{};
    double elapsed_{};
    double duration_{1.0 / 30.0};
    bool initialized_{};
};

/**
 * Bounded roster used by both tutorial packet fixtures and a live session.
 * Malformed or crash-sensitive records fail closed without changing an
 * existing generation. A repeated player id atomically replaces its life.
 */
class Protocol168Roster final {
public:
    [[nodiscard]] bool apply(const CreatePlayerPacket& packet,
                             std::string* error = nullptr);
    [[nodiscard]] bool apply(std::span<const std::byte> payload,
                             std::string* error = nullptr);
    void remove(std::uint8_t player_id) noexcept;
    [[nodiscard]] bool update_transform(std::uint8_t player_id,
                                        world::Vec3 position,
                                        world::Vec3 orientation) noexcept;
    /** Atomically consume every replicated WorldUpdate field for one player. */
    [[nodiscard]] bool update_world_state(
        const WorldPlayerWeaponRow& row) noexcept;
    /** Apply SetHP(5) without fabricating a complete WorldUpdate row. */
    [[nodiscard]] bool update_health(std::uint8_t player_id,
                                     std::int16_t health) noexcept;
    /**
     * Apply the Character resource changed by Restock(69, JETPACK_CRATE).
     * WorldUpdate remains authoritative and replaces this value on the next
     * snapshot; the packet edge prevents one stale HUD frame.
     */
    [[nodiscard]] bool update_jetpack_fuel(std::uint8_t player_id,
                                           float fuel) noexcept;
    /** Apply the two boolean ChangePlayer(17) mode flags without a respawn. */
    [[nodiscard]] bool update_mode_visibility(
        std::uint16_t player_id,
        std::optional<bool> high_minimap_visibility,
        std::optional<bool> chase_cam) noexcept;
    /** Apply PickPickup(70) immediately rather than waiting for WorldUpdate. */
    [[nodiscard]] bool update_pickup(std::uint8_t player_id,
                                     std::uint8_t pickup_id) noexcept;
    /**
     * Apply GameScene.process_packet_kill_action's local domination/revenge
     * transitions without fabricating fields absent from KillAction(46).
     */
    [[nodiscard]] bool apply_kill_relationships(
        std::uint8_t victim_id, std::uint8_t killer_id,
        std::uint8_t local_player_id, bool domination, bool revenge,
        bool team_change_kill) noexcept;
    [[nodiscard]] bool update_loadout(
        std::uint8_t player_id, std::uint8_t class_id,
        std::span<const std::uint8_t> loadout,
        std::span<const std::string> prefabs,
        std::span<const std::uint8_t> ugc_tools = {}) noexcept;
    void clear() noexcept;

    [[nodiscard]] const RemotePlayerReplica*
    player(std::uint8_t player_id) const noexcept;
    [[nodiscard]] std::vector<RemotePlayerReplica> players() const;

private:
    std::array<std::optional<RemotePlayerReplica>, 128U> players_{};
    std::array<std::uint32_t, 128U> generations_{};
};

/** Packet fixtures shown in Training; encoded/decoded through packet 28. */
[[nodiscard]] std::vector<CreatePlayerPacket>
tutorial_create_player_fixtures();

/**
 * Build the exact authoritative peer-body set used by local prediction.
 * Enemies always collide; allies follow InitialInfo.same_team_collision.
 */
[[nodiscard]] std::vector<world::PlayerCollisionBody>
protocol168_collision_bodies(const Protocol168Roster& roster,
                             std::uint8_t local_player_id,
                             bool same_team_collision);

} // namespace battlespades::network
