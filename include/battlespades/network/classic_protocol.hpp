#pragma once
#include "battlespades/network/game_protocol.hpp"
#include "battlespades/network/live_protocol168_connection.hpp"
#include <array>
#include <chrono>
#include <memory>

namespace battlespades::network {
/** Classic maps are translated vertically into the renderer's 240-high world.
 * All wire coordinates remain 512 x 512 x 64; water stays at the existing render plane. */
inline constexpr float classic_z_offset = 176.0F;
using ClassicPackets = std::vector<std::vector<std::byte>>;
struct ClassicIngest final {
    ClassicPackets wire;
    ClassicPackets events;
    std::unique_ptr<Protocol168WorldBootstrap> bootstrap;
    bool map_started{};
    std::string error;
};
struct ClassicMotion final {
    std::array<float, 3> position{}, orientation{1, 0, 0};
    std::uint8_t movement{}, actions{}, tool{6};
    bool alive{};
};
/** A wire adapter, not an authority: it never invents hits or server state.
 * Documented against ZeroSpades NetClient and piqueserver contained.pyx.
 * Unknown extension IDs are ignored and none are advertised until implemented. */
class ClassicProtocolSession final {
public:
    explicit ClassicProtocolSession(GameProtocol protocol, std::string name = "BattleSpades");
    [[nodiscard]] ClassicIngest ingest(std::span<const std::byte> packet);
    [[nodiscard]] ClassicPackets translate_client(std::span<const std::byte> native_packet);
    /** Validate an already-classic action against the current authoritative life. */
    [[nodiscard]] bool accepts_client_action(std::span<const std::byte> packet) const noexcept;
    [[nodiscard]] ClassicPackets motion(const ClassicMotion& motion, double seconds);
    /** Advance server-defined TC progress into the existing objective HUD events. */
    [[nodiscard]] ClassicPackets advance(double seconds);
    [[nodiscard]] std::uint64_t generation() const noexcept {
        return generation_;
    }
    [[nodiscard]] bool ready() const noexcept {
        return ready_;
    }
    [[nodiscard]] std::uint8_t local_id() const noexcept {
        return local_id_;
    }
    [[nodiscard]] std::size_t map_bytes() const noexcept {
        return compressed_.size();
    }
    [[nodiscard]] std::uint32_t advertised_map_bytes() const noexcept {
        return map_size_;
    }
    [[nodiscard]] GameProtocol protocol() const noexcept {
        return protocol_;
    }

private:
    struct Player {
        CreatePlayerPacket record;
        std::uint32_t color{0x707070};
        std::int32_t score{};
        std::uint8_t weapon{}, tool{2}, input{}, fire{};
        bool present{};
    };
    void game_packet(std::span<const std::byte>, ClassicIngest&);
    void state_packet(std::span<const std::byte>, ClassicIngest&);
    void
    world_event(ClassicIngest&, std::span<const std::uint8_t> ids, bool position_changed = false);
    void objective_event(ClassicIngest&,
                         std::uint8_t id,
                         std::uint8_t team,
                         std::optional<std::array<float, 3>> position);
    GameProtocol protocol_;
    std::string name_;
    std::string map_name_;
    std::array<Player, 128> players_{};
    std::vector<std::byte> compressed_;
    ClassicPackets deferred_;
    std::size_t deferred_bytes_{};
    std::uint64_t generation_{};
    std::int32_t world_loop_{};
    std::uint32_t map_size_{};
    std::uint32_t temporary_block_color_{0x707070U};
    std::uint8_t local_id_{}, weapon_{}, mode_{};
    bool receiving_{}, ready_{}, joined_{};
    std::array<std::int32_t, 2> scores_{};
    std::array<std::uint8_t, 2> carriers_{255, 255};
    std::array<std::optional<std::array<float, 3>>, 16> objectives_{};
    std::array<std::array<std::uint8_t, 3>, 2> team_colors_{};
    std::array<std::string, 2> team_names_{};
    struct ObjectiveZone {
        std::array<std::int16_t, 3> minimum{}, maximum{};
    };
    std::array<std::optional<ObjectiveZone>, 16> objective_zones_{};
    struct Territory {
        std::uint8_t owner{1}, attacker{1};
        float progress{}, rate{};
        bool local_inside{};
    };
    std::array<Territory, 16> territories_{};
    std::uint8_t territory_count_{};
    double last_advance_seconds_{};
    std::optional<ClassicMotion> last_motion_;
    std::optional<std::array<float, 3>> last_sent_orientation_;
    double last_position_seconds_{}, last_orientation_seconds_{};
    double jump_pulse_until_{};
};
[[nodiscard]] std::uint8_t classic_weapon_tool(std::uint8_t weapon) noexcept;
[[nodiscard]] std::optional<std::uint8_t> classic_weapon_id(std::uint8_t tool) noexcept;
[[nodiscard]] std::vector<std::byte> classic_hit_packet(std::uint8_t victim, std::uint8_t part);
[[nodiscard]] std::vector<std::byte>
classic_block_packet(std::uint8_t player, std::uint8_t action, std::array<std::int32_t, 3> cell);
[[nodiscard]] std::vector<std::byte> classic_line_packet(std::uint8_t player,
                                                         std::array<std::int32_t, 3> start,
                                                         std::array<std::int32_t, 3> end);
[[nodiscard]] std::vector<std::byte> classic_grenade_packet(std::uint8_t player,
                                                            float fuse,
                                                            std::array<float, 3> position,
                                                            std::array<float, 3> velocity);
[[nodiscard]] std::optional<std::array<float, 3>>
classic_correction(std::span<const std::byte> packet);
/** Only complete, bounded client hit, grenade and terrain requests may bypass the adapter. */
[[nodiscard]] bool valid_classic_client_action(std::span<const std::byte> packet) noexcept;
/** Parses, bounds-checks and offsets raw 64-high VXL before using the shared map decoder. */
[[nodiscard]] world::VxlLoadResult load_classic_vxl(std::span<const std::byte> raw);
} // namespace battlespades::network
