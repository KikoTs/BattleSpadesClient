#pragma once

#include "battlespades/network/protocol168_players.hpp"
#include "battlespades/network/protocol168_ugc.hpp"
#include "battlespades/world/vxl_map.hpp"

#include <cstddef>
#include <cstdint>
#include <array>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace battlespades::network {

enum class Protocol168SessionPhase : std::uint8_t {
    disconnected,
    awaiting_initial_info,
    awaiting_map_validation,
    awaiting_map_start,
    receiving_map,
    awaiting_state,
    awaiting_own_player,
    ready,
    failed,
};

struct Protocol168SessionConfig final {
    std::string player_name{"NativeClient"};
    /** Retail wire teams: 0 spectator, 1 neutral, 2 Blue, 3 Green. */
    std::uint8_t team{2U};
    std::uint8_t class_id{1U};
    std::uint8_t local_language{};
    /** Zero deliberately forces a full authoritative MapSync. */
    std::uint32_t local_map_crc{};
    /**
     * Diagnostic clients may complete the old one-shot join automatically.
     * The interactive frontend disables this: retail does not send packet 15
     * until SelectTeam and SelectClass have both been confirmed.
     */
    bool auto_join{true};
};

struct Protocol168InitialInfo final {
    std::string server_name;
    std::string map_name;
    std::string filename;
    /**
     * Retail UI resource override selected by the server (for example
     * ``mafia`` in Territory Control and VIP). An empty wire string means the
     * default skin; consumers must still resolve this through the installed
     * skin catalog before using it as a path.
     */
    std::string texture_skin;
    std::uint32_t checksum{};
    /** Dedicated query port carried before the InitialInfo presentation flags. */
    std::uint16_t query_port{};
    std::uint8_t mode_key{};
    /** Exact host/client editor role carried by InitialInfo; never a boolean. */
    UgcRole ugc_role{UgcRole::none};
    [[nodiscard]] bool map_is_ugc() const noexcept { return is_ugc(ugc_role); }
    bool classic{};
    bool enable_minimap{};
    bool exposed_teams_always_on_minimap{};
    bool enable_minimap_height_icons{};
    bool allow_shooting_holding_intel{};
    /** Whether authoritative movement collides players with allied bodies. */
    bool same_team_collision{};
    bool enable_colour_picker{};
    bool enable_colour_palette{};
    /** Gates retail's top-left SCORE box and the health-bar class portrait. */
    bool enable_player_score{};
    /** Gates only the numeric HP label; the health frame/fill remain visible. */
    bool enable_numeric_hp{};
    /** InitialInfo rule controlling retail's grave-to-killer death camera. */
    bool enable_deathcam{};
    /** InitialInfo rule gating both retail sniper LaserAttachment variants. */
    bool enable_sniper_beam{};
    bool enable_spectator{};
    std::vector<float> movement_speed_multipliers;
    /** Initial UGC terrain palette rows in retail RGB/Z-threshold order. */
    std::vector<std::array<std::uint8_t, 4U>> ground_colors;
    std::uint8_t ugc_mode{};
};

/**
 * Resolve the exact fixed-point class scale advertised by InitialInfo.
 * The byte-array index is the class id and the value is already the factor
 * applied to native accel/sprint/crouch constants; it is not an effective
 * sprint speed and must never be divided by another class constant.
 */
[[nodiscard]] double protocol168_movement_scale(
    const Protocol168InitialInfo& info, std::uint8_t class_id) noexcept;

/** Server-owned environment, team and menu state from StateData(45). */
struct Protocol168StateInfo final {
    std::uint8_t player_id{};
    std::array<std::uint8_t, 3U> fog_color{};
    /** Signed 1/64 fixed-point world gravity selected by the current map. */
    double gravity{1.0};
    /** StateData's forward directional-light RGB in semantic (not wire BGR) order. */
    std::array<std::uint8_t, 3U> light_color{};
    /** Retail GL/render-space direction, decoded from the wire's Z,Y,X order. */
    std::array<double, 3U> light_direction{};
    std::array<std::uint8_t, 3U> back_light_color{};
    std::array<double, 3U> back_light_direction{};
    std::array<std::uint8_t, 3U> ambient_light_color{};
    double ambient_light_intensity{};
    /** Server-authored visual time scale; retained even before sky animation consumes it. */
    double time_scale{1.0};
    std::uint8_t score_limit{};
    std::uint8_t mode_type{};
    /**
     * Retail HeadCount value selector from StateData: 0=roster count,
     * 1=score, 2=four-digit score layout, 3=inactive. Unknown values remain
     * visible and use score, matching the stock client's equality checks.
     */
    std::uint8_t team_headcount_type{};
    std::string team1_name;
    std::string team2_name;
    std::array<std::uint8_t, 3U> team1_color{};
    std::array<std::uint8_t, 3U> team2_color{};
    std::int32_t team1_score{};
    std::int32_t team2_score{};
    bool team1_locked{};
    bool team2_locked{};
    bool team1_can_see_team2{};
    bool team2_can_see_team1{};
    bool team1_show_score{};
    bool team2_show_score{};
    bool team1_show_max_score{};
    bool team2_show_max_score{};
    bool team1_infinite_blocks{};
    bool team2_infinite_blocks{};
    bool team1_locked_class{};
    bool team2_locked_class{};
    bool team1_locked_score{};
    bool team2_locked_score{};
    bool lock_team_swap{};
    bool lock_spectator_swap{};
    bool has_map_ended{};
    /**
     * Retail end-of-round camera positions from StateData's first camera
     * array. Triplets are converted from wire Z,Y,X into world X,Y,Z.
     */
    std::vector<std::array<double, 3U>> screenshot_camera_points;
    /**
     * Retail end-of-round camera rotations from StateData's second camera
     * array, in the same semantic X,Y,Z order as the point list.
     */
    std::vector<std::array<double, 3U>> screenshot_camera_rotations;
    std::vector<std::uint8_t> team1_classes;
    std::vector<std::uint8_t> team2_classes;
    std::vector<std::string> prefabs;
};

/** Server-selected retail mesh environment from SkyboxData(51). */
struct Protocol168SkyboxInfo final {
    /** Safe stock basename such as `Tokyo.txt` or `User_Grassland.txt`. */
    std::string definition_name;
};

/** Standalone bounded decoders used by the live handshake and parity tests. */
[[nodiscard]] std::optional<Protocol168InitialInfo>
decode_protocol168_initial_info(std::span<const std::byte> packet,
                                std::string& error);
[[nodiscard]] std::optional<Protocol168StateInfo>
decode_protocol168_state_info(std::span<const std::byte> packet,
                              std::string& error);
[[nodiscard]] std::optional<Protocol168SkyboxInfo>
decode_protocol168_skybox_info(std::span<const std::byte> packet,
                               std::string& error);

struct Protocol168IngestResult final {
    std::vector<std::vector<std::byte>> outbound_datagrams;
    bool accepted{};
    std::string diagnostic;
};

/** Decode the server's outer prefix plus bounded LZF packet body. */
[[nodiscard]] std::optional<std::vector<std::byte>>
decode_protocol168_server_datagram(std::span<const std::byte> datagram,
                                   std::string& error);

/** Prefix one plain client packet for the server; optionally XOR after auth. */
[[nodiscard]] std::vector<std::byte>
encode_protocol168_client_datagram(std::span<const std::byte> packet,
                                   std::span<const std::byte> ticket = {});

/** Encode the plain packet-15 player announcement sent after menu selection. */
[[nodiscard]] std::vector<std::byte>
encode_protocol168_new_player_connection(
    const Protocol168SessionConfig& config);

/**
 * Strict Protocol 168 join state machine shared by the future match scene and
 * the live integration smoke. It uses an empty (offline/fake) Steam ticket and
 * forces full MapSync. Interactive sessions publish the map/state at the
 * SelectTeam boundary; diagnostic sessions may continue through packet 15 and
 * first ClientData automatically.
 *
 * Unknown packets are quarantined and reported without mutating state.
 * Malformed phase-critical packets fail closed; bounded non-critical failures
 * cannot grow queues or crash the renderer.
 */
class Protocol168Session final {
public:
    explicit Protocol168Session(Protocol168SessionConfig config = {});

    /** Call exactly once after ENet reports CONNECT. */
    [[nodiscard]] std::vector<std::byte> connected();
    [[nodiscard]] Protocol168IngestResult
    ingest(std::span<const std::byte> datagram);
    void disconnected() noexcept;

    [[nodiscard]] Protocol168SessionPhase phase() const noexcept;
    /** Map and StateData are complete, so an interactive chooser can open. */
    [[nodiscard]] bool bootstrap_ready() const noexcept;
    [[nodiscard]] bool ready() const noexcept;
    [[nodiscard]] std::string_view last_error() const noexcept;
    [[nodiscard]] const Protocol168InitialInfo* initial_info() const noexcept;
    [[nodiscard]] const Protocol168StateInfo* state_info() const noexcept;
    [[nodiscard]] const Protocol168SkyboxInfo* skybox_info() const noexcept;
    [[nodiscard]] const world::VxlMap* map() const noexcept;
    /**
     * Transfer the fully validated MapSync world to the render/simulation
     * owner.  This is only legal after the join reached ready; moving avoids
     * a second 512x512 voxel copy at the gameplay handoff.
     */
    [[nodiscard]] std::optional<world::VxlMap> take_map() noexcept;
    [[nodiscard]] const Protocol168Roster& roster() const noexcept;
    [[nodiscard]] std::optional<std::uint8_t> local_player_id() const noexcept;
    [[nodiscard]] std::size_t compressed_map_bytes() const noexcept;
    [[nodiscard]] std::size_t unknown_packets() const noexcept;
    [[nodiscard]] std::size_t malformed_packets() const noexcept;
    /**
     * Transfer bounded presentation packets that legally arrived between
     * StateData and the local CreatePlayer readiness edge.
     */
    [[nodiscard]] std::vector<std::vector<std::byte>>
    take_deferred_runtime_packets() noexcept;
    /** Next label after the handshake's mandatory first ClientData frame. */
    [[nodiscard]] std::uint32_t next_client_loop_count() const noexcept;

private:
    [[nodiscard]] Protocol168IngestResult
    ingest_packet(std::span<const std::byte> packet);
    [[nodiscard]] Protocol168IngestResult fail(std::string message);
    [[nodiscard]] bool note_malformed(std::string message,
                                      Protocol168IngestResult& result,
                                      bool critical);

    Protocol168SessionConfig config_;
    Protocol168SessionPhase phase_{Protocol168SessionPhase::disconnected};
    std::optional<Protocol168InitialInfo> initial_info_;
    std::optional<Protocol168StateInfo> state_info_;
    std::optional<Protocol168SkyboxInfo> skybox_info_;
    std::optional<world::VxlMap> map_;
    Protocol168Roster roster_;
    std::optional<std::uint8_t> local_player_id_;
    std::vector<std::byte> map_stream_;
    /** Packet 54/56/58 UGC lobby-host source stream, distinct from MapSync. */
    std::vector<std::byte> ugc_source_stream_;
    bool receiving_ugc_source_{};
    std::vector<std::vector<std::byte>> deferred_runtime_packets_;
    std::string last_error_;
    std::size_t unknown_packets_{};
    std::size_t malformed_packets_{};
    std::uint32_t client_loop_count_{};
};

} // namespace battlespades::network
