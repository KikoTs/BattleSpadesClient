#include "battlespades/network/protocol168_session.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <vector>

namespace {

void expect(bool value, const char* message) {
    if (!value) throw std::runtime_error{message};
}

template <typename Integer>
void integer(std::vector<std::byte>& bytes, Integer value) {
    static_assert(std::is_integral_v<Integer>);
    using Unsigned = std::make_unsigned_t<Integer>;
    const auto raw = static_cast<Unsigned>(value);
    for (std::size_t index{}; index < sizeof(Integer); ++index) {
        bytes.push_back(static_cast<std::byte>(raw >> (index * 8U)));
    }
}

void string(std::vector<std::byte>& bytes, std::string_view value) {
    for (const char character : value) {
        bytes.push_back(static_cast<std::byte>(character));
    }
    bytes.push_back(std::byte{});
}

std::vector<std::byte> server_datagram(std::span<const std::byte> packet,
                                       std::byte prefix = std::byte{0x31U}) {
    std::vector<std::byte> wire{prefix};
    for (std::size_t start{}; start < packet.size(); start += 32U) {
        const auto count = std::min<std::size_t>(32U, packet.size() - start);
        wire.push_back(static_cast<std::byte>(count - 1U));
        wire.insert(wire.end(), packet.begin() + static_cast<std::ptrdiff_t>(start),
                    packet.begin() + static_cast<std::ptrdiff_t>(start + count));
    }
    return wire;
}

std::vector<std::byte> initial_info(
    battlespades::network::UgcRole role = battlespades::network::UgcRole::none,
    bool enable_numeric_hp = true, bool enable_player_score = true) {
    std::vector<std::byte> packet{std::byte{114U}};
    integer<std::uint64_t>(packet, 0U);
    integer<std::uint32_t>(packet, 0U);
    integer<std::uint32_t>(packet, 32887U);
    for (const auto value : {"TDM", "Description", "One", "Two", "Three"}) {
        string(packet, value);
    }
    string(packet, "Training");
    string(packet, "Training.vxl");
    integer<std::uint32_t>(packet, 0x12345678U);
    packet.push_back(std::byte{1U}); // mode key
    packet.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(role))); // exact UGC role
    integer<std::uint16_t>(packet, 32888U);
    for (const auto value : {1U, 1U, 1U, 192U, 1U, 1U, 1U, 1U, 1U, 1U}) {
        packet.push_back(static_cast<std::byte>(value));
    }
    packet.push_back(enable_numeric_hp ? std::byte{1U} : std::byte{});
    string(packet, "mafia");
    packet.push_back(std::byte{}); // beach z modifiable
    packet.push_back(std::byte{1U}); // minimap height icons
    packet.push_back(std::byte{}); // fall-on-water damage
    integer<std::int16_t>(packet, 64);
    integer<std::int16_t>(packet, 64);
    packet.push_back(std::byte{}); // disabled tools
    packet.push_back(std::byte{}); // disabled classes
    packet.push_back(std::byte{2U}); // speed multipliers
    integer<std::int16_t>(packet, 90); // 1.40625
    integer<std::int16_t>(packet, 93); // 1.453125
    packet.push_back(std::byte{}); // prefab sets
    packet.push_back(enable_player_score ? std::byte{1U} : std::byte{});
    string(packet, "BattleSpades Test");
    packet.push_back(std::byte{2U}); // ground color count
    for (const auto value : {59U, 58U, 55U, 238U, 5U, 85U, 156U, 255U}) {
        packet.push_back(static_cast<std::byte>(value));
    }
    packet.push_back(std::byte{}); // ground color terminator
    packet.push_back(std::byte{1U}); // allow shooting while carrying intel
    for (std::size_t index{}; index < 3U; ++index) packet.push_back(std::byte{});
    packet.push_back(std::byte{7U}); // UGC/mode mirror
    return packet;
}

std::vector<std::byte> state_data() {
    std::vector<std::byte> packet{std::byte{45U}, std::byte{17U}};
    // shared.packet.write_color emits B,G,R on the wire. These three bytes
    // represent semantic RGB (56,34,12), not RGB (12,34,56).
    packet.insert(packet.end(), {std::byte{12U}, std::byte{34U}, std::byte{56U}});
    integer<std::int16_t>(packet, 26); // LunarBase gravity: 26/64
    // Each color is wire B,G,R; each direction is fixed-point Z,Y,X.
    packet.insert(packet.end(), {std::byte{220U}, std::byte{192U}, std::byte{180U}});
    integer<std::uint16_t>(packet, 0x8010U); // z = -0.25 (wire sign-magnitude)
    integer<std::int16_t>(packet, 51);  // y = 0.796875
    integer<std::int16_t>(packet, 13);  // x = 0.203125
    packet.insert(packet.end(), {std::byte{64U}, std::byte{48U}, std::byte{32U}});
    integer<std::int16_t>(packet, 19);  // z = 0.296875
    integer<std::uint16_t>(packet, 0x8025U); // y = -0.578125
    integer<std::uint16_t>(packet, 0x8005U); // x = -0.078125
    packet.insert(packet.end(), {std::byte{64U}, std::byte{56U}, std::byte{52U}});
    integer<std::int16_t>(packet, 13);  // ambient = 0.203125
    integer<std::int16_t>(packet, 96);  // time scale = 1.5
    packet.push_back(std::byte{10U}); // score limit
    packet.push_back(std::byte{7U}); // VIP mode
    packet.push_back(std::byte{6U});
    string(packet, "Blue Team");
    packet.insert(packet.end(), {std::byte{33U}, std::byte{22U}, std::byte{11U}});
    integer<std::int32_t>(packet, 4);
    packet.push_back(std::byte{0x2DU}); // locked + score flags + locked class
    packet.push_back(std::byte{2U});
    packet.push_back(std::byte{10U});
    packet.push_back(std::byte{12U});
    string(packet, "Green Team");
    packet.insert(packet.end(), {std::byte{66U}, std::byte{55U}, std::byte{44U}});
    integer<std::int32_t>(packet, 6);
    packet.push_back(std::byte{0x0CU});
    packet.push_back(std::byte{1U});
    packet.push_back(std::byte{11U});
    packet.push_back(std::byte{0x03U}); // lock team and spectator swap
    integer<std::uint16_t>(packet, 1U);
    string(packet, "Bunker");
    integer<std::uint16_t>(packet, 0U); // entities
    packet.push_back(std::byte{2U}); // screenshot points
    // Wire order is Z,Y,X and fixed values are sign-magnitude / 64.
    integer<std::int16_t>(packet, 320);  // z = 5
    integer<std::int16_t>(packet, 1280); // y = 20
    integer<std::int16_t>(packet, 640);  // x = 10
    integer<std::uint16_t>(packet, 0x80C0U); // z = -3
    integer<std::int16_t>(packet, 2560);     // y = 40
    integer<std::int16_t>(packet, 1920);     // x = 30
    packet.push_back(std::byte{2U}); // screenshot rotations
    integer<std::int16_t>(packet, 32);  // z = 0.5
    integer<std::uint16_t>(packet, 0x8010U); // y = -0.25
    integer<std::int16_t>(packet, 8);   // x = 0.125
    integer<std::int16_t>(packet, 64);  // z = 1
    integer<std::int16_t>(packet, 96);  // y = 1.5
    integer<std::uint16_t>(packet, 0x8020U); // x = -0.5
    packet.push_back(std::byte{1U}); // map ended
    return packet;
}

void offline_ticket_and_initial_sequence_are_exact() {
    using namespace battlespades::network;
    Protocol168Session session;
    const auto ticket = session.connected();
    expect(ticket.size() == 6U && ticket[0U] == std::byte{0x30U} &&
               ticket[1U] == std::byte{105U},
           "connect must send a raw-prefixed zero-length Steam ticket");
    const auto info_result = session.ingest(server_datagram(initial_info()));
    expect(info_result.accepted && info_result.outbound_datagrams.size() == 1U &&
               session.phase() == Protocol168SessionPhase::awaiting_map_start,
           "InitialInfo must atomically advance and answer MapDataValidation");
    const auto* info = session.initial_info();
    expect(info != nullptr, "InitialInfo must be retained");
    expect(info->map_name == "Training", "InitialInfo map name must be exact");
    expect(info->checksum == 0x12345678U,
           "InitialInfo checksum must be exact");
    expect(info->query_port == 32888U,
           "InitialInfo query port must be consumed before presentation flags");
    expect(info->server_name == "BattleSpades Test",
           "InitialInfo server name must be exact");
    expect(info->texture_skin == "mafia",
           "InitialInfo must retain the server-owned UI skin");
    expect(info->classic && info->enable_minimap && info->same_team_collision &&
                info->enable_numeric_hp && info->enable_player_score &&
                info->enable_deathcam && info->enable_sniper_beam &&
               info->enable_spectator &&
               info->exposed_teams_always_on_minimap &&
               info->enable_minimap_height_icons &&
               info->allow_shooting_holding_intel &&
               info->movement_speed_multipliers.size() == 2U &&
               info->ground_colors ==
                   std::vector<std::array<std::uint8_t, 4U>>{{59U, 58U, 55U, 238U},
                                                             {5U, 85U, 156U, 255U}} &&
               info->ugc_mode == 7U,
           "InitialInfo presentation and movement rules must not be discarded");
    expect(protocol168_movement_scale(*info, 0U) == 1.40625 &&
               protocol168_movement_scale(*info, 1U) == 1.453125 &&
               protocol168_movement_scale(*info, 17U) == 1.0,
           "InitialInfo class values are direct movement scales, not sprint speeds");
    const auto& validation = info_result.outbound_datagrams.front();
    expect(validation.size() == 6U && validation[0U] == std::byte{0x30U} &&
               validation[1U] == std::byte{60U},
           "the full-sync request must use packet 60 with local CRC zero");
}

void native_steam_ticket_and_xor_sequence_are_exact() {
    using namespace battlespades::network;
    Protocol168SessionConfig config;
    config.steam_ticket = {std::byte{'a'}, std::byte{'1'}, std::byte{'B'}};
    Protocol168Session session{config};
    const auto ticket = session.connected();
    const std::vector<std::byte> expected_ticket{
        std::byte{0x30U}, std::byte{105U}, std::byte{3U}, std::byte{0U},
        std::byte{0U}, std::byte{0U}, std::byte{'a'}, std::byte{'1'}, std::byte{'B'}};
    expect(ticket == expected_ticket,
           "packet 105 must carry the native Steam ASCII-hex ticket unencrypted");

    const auto info_result = session.ingest(server_datagram(initial_info()));
    expect(info_result.accepted && info_result.outbound_datagrams.size() == 1U,
           "native Steam handshake must proceed to map validation");
    const std::array<std::byte, 5U> validation{
        std::byte{60U}, std::byte{}, std::byte{}, std::byte{}, std::byte{}};
    const auto expected_validation =
        encode_protocol168_client_datagram(validation, config.steam_ticket);
    expect(info_result.outbound_datagrams.front() == expected_validation,
           "every post-ticket client packet must use the retail repeating XOR key");
}

void initial_info_retains_disabled_hud_presentation_flags() {
    using namespace battlespades::network;
    std::string error;
    const auto info = decode_protocol168_initial_info(
        initial_info(UgcRole::none, false, false), error);
    expect(info.has_value() && error.empty(),
           "disabled HUD presentation fixture must still decode");
    expect(!info->enable_numeric_hp,
           "InitialInfo must retain a disabled numeric-HP gate");
    expect(!info->enable_player_score,
           "InitialInfo must retain a disabled player-score gate");
}

void initial_info_preserves_host_and_client_ugc_roles() {
    using namespace battlespades::network;
    for (const auto role : {UgcRole::host, UgcRole::client}) {
        std::string error;
        const auto decoded = decode_protocol168_initial_info(initial_info(role), error);
        expect(decoded.has_value() && decoded->ugc_role == role &&
                   decoded->map_is_ugc() && error.empty(),
               "InitialInfo must retain the exact UGC host/client role");
    }
    auto malformed = initial_info(static_cast<UgcRole>(3U));
    std::string error;
    expect(!decode_protocol168_initial_info(malformed, error).has_value() && !error.empty(),
           "InitialInfo must reject roles outside none/host/client");
}

void new_player_announcement_is_explicit_and_exact() {
    using namespace battlespades::network;
    Protocol168SessionConfig config;
    config.player_name = "Chooser";
    config.team = 3U;
    config.class_id = 6U;
    config.local_language = 4U;
    config.auto_join = false;
    const auto packet = encode_protocol168_new_player_connection(config);
    const std::vector<std::byte> expected{
        std::byte{15U}, std::byte{3U}, std::byte{6U}, std::byte{0U},
        std::byte{4U}, std::byte{'C'}, std::byte{'h'}, std::byte{'o'},
        std::byte{'o'}, std::byte{'s'}, std::byte{'e'}, std::byte{'r'},
        std::byte{0U}};
    expect(packet == expected,
           "packet 15 must carry only the explicitly chosen team/class");
}

void state_data_retains_server_owned_team_and_menu_state() {
    using namespace battlespades::network;
    std::string error;
    const auto state = decode_protocol168_state_info(state_data(), error);
    expect(state.has_value() && error.empty(), "StateData fixture must decode");
    expect(state->player_id == 17U && state->mode_type == 7U &&
               state->score_limit == 10U && state->team_headcount_type == 6U,
           "StateData player and mode identity must survive decoding");
    expect(state->team1_color == std::array<std::uint8_t, 3U>{11U, 22U, 33U} &&
               state->team2_color == std::array<std::uint8_t, 3U>{44U, 55U, 66U},
           "team colors must come from the server, not client constants");
    expect(state->fog_color == std::array<std::uint8_t, 3U>{56U, 34U, 12U},
           "StateData colors must reverse the server's BGR wire order exactly once");
    expect(state->gravity == 0.40625,
           "StateData must retain LunarBase's signed 1/64 gravity scalar");
    expect(state->light_color == std::array<std::uint8_t, 3U>{180U, 192U, 220U} &&
               state->light_direction == std::array<double, 3U>{0.203125, 0.796875, -0.25} &&
               state->back_light_color == std::array<std::uint8_t, 3U>{32U, 48U, 64U} &&
               state->back_light_direction ==
                   std::array<double, 3U>{-0.078125, -0.578125, 0.296875} &&
               state->ambient_light_color ==
                   std::array<std::uint8_t, 3U>{52U, 56U, 64U} &&
               state->ambient_light_intensity == 0.203125 &&
               state->time_scale == 1.5,
           "StateData lighting must decode BGR colors, ZYX directions and fixed scalars");
    expect(state->team1_locked && state->team1_locked_class &&
                state->team1_show_score && state->team1_show_max_score &&
                !state->team1_can_see_team2 && !state->team1_infinite_blocks &&
                !state->team1_locked_score &&
                !state->team2_locked && !state->team2_locked_class &&
                state->team2_show_score && state->team2_show_max_score &&
                !state->team2_can_see_team1 && !state->team2_infinite_blocks &&
                !state->team2_locked_score &&
                state->lock_team_swap && state->lock_spectator_swap &&
                state->has_map_ended &&
                state->screenshot_camera_points ==
                    std::vector<std::array<double, 3U>>{
                        {10.0, 20.0, 5.0}, {30.0, 40.0, -3.0}} &&
                state->screenshot_camera_rotations ==
                    std::vector<std::array<double, 3U>>{
                        {0.125, -0.25, 0.5}, {-0.5, 1.5, 1.0}} &&
                state->prefabs == std::vector<std::string>{"Bunker"},
            "pause locks, class locks, camera arrays, and prefab catalog must remain atomic");
}

void skybox_data_retains_only_a_safe_retail_definition() {
    using namespace battlespades::network;
    std::vector<std::byte> tokyo{std::byte{51U}};
    string(tokyo, "Tokyo.txt");
    std::string error;
    const auto decoded = decode_protocol168_skybox_info(tokyo, error);
    expect(decoded.has_value() && decoded->definition_name == "Tokyo.txt" &&
               error.empty(),
           "SkyboxData must retain the server-selected retail definition");

    std::vector<std::byte> traversal{std::byte{51U}};
    string(traversal, "../Tokyo.txt");
    expect(!decode_protocol168_skybox_info(traversal, error).has_value() &&
               !error.empty(),
           "SkyboxData must not escape the stock mesh directory");
}

void malformed_phase_packets_fail_closed() {
    using namespace battlespades::network;
    Protocol168Session session;
    static_cast<void>(session.connected());
    const std::vector<std::byte> premature_end{std::byte{59U}};
    const auto result = session.ingest(server_datagram(premature_end));
    expect(!result.accepted && session.phase() == Protocol168SessionPhase::failed &&
               session.map() == nullptr && session.malformed_packets() == 1U,
           "out-of-phase MapSyncEnd must fail without publishing a map");

    std::string error;
    const std::vector<std::byte> invalid_backref{
        std::byte{0x31U}, std::byte{0x20U}, std::byte{0U}};
    expect(!decode_protocol168_server_datagram(invalid_backref, error).has_value() &&
               !error.empty(),
           "invalid LZF references must be rejected without an out-of-bounds read");
}

void ugc_source_stream_is_framed_before_map_sync() {
    using namespace battlespades::network;
    Protocol168Session session;
    static_cast<void>(session.connected());
    expect(session.ingest(server_datagram(initial_info())).accepted,
           "UGC source test must reach the validation phase");
    expect(session.ingest(
               server_datagram(std::array{std::byte{54U}}))
               .accepted,
           "MapDataStart(54) must open the pre-validation UGC stream");
    const std::array chunk{
        std::byte{56U}, std::byte{50U}, std::byte{1U},
        std::byte{0U}, std::byte{0x78U}};
    expect(session.ingest(server_datagram(chunk)).accepted,
           "MapDataChunk(56) must retain its bounded zlib bytes");

    Protocol168Session large_chunk;
    static_cast<void>(large_chunk.connected());
    static_cast<void>(large_chunk.ingest(server_datagram(initial_info())));
    static_cast<void>(large_chunk.ingest(server_datagram(std::array{std::byte{54U}})));
    constexpr std::uint16_t recovered_flush_size{34'677U};
    std::vector<std::byte> persistent_zlib_flush(
        static_cast<std::size_t>(recovered_flush_size) + 4U, std::byte{0x55U});
    persistent_zlib_flush[0U] = std::byte{56U};
    persistent_zlib_flush[1U] = std::byte{13U};
    persistent_zlib_flush[2U] =
        static_cast<std::byte>(recovered_flush_size & 0xFFU);
    persistent_zlib_flush[3U] =
        static_cast<std::byte>((recovered_flush_size >> 8U) & 0xFFU);
    expect(large_chunk.ingest(server_datagram(persistent_zlib_flush)).accepted,
           "MapDataChunk length is an unsigned payload count at zlib flush boundaries");
    const auto early_sync =
        session.ingest(server_datagram(std::array{std::byte{55U}}));
    expect(!early_sync.accepted &&
               session.phase() == Protocol168SessionPhase::failed,
           "MapSync cannot splice into an unfinished UGC source stream");

    Protocol168Session malformed;
    static_cast<void>(malformed.connected());
    static_cast<void>(malformed.ingest(server_datagram(initial_info())));
    static_cast<void>(malformed.ingest(
        server_datagram(std::array{std::byte{54U}})));
    const auto empty_end = malformed.ingest(
        server_datagram(std::array{std::byte{58U}}));
    expect(!empty_end.accepted &&
               malformed.phase() == Protocol168SessionPhase::failed,
           "MapDataEnd must reject an empty/non-zlib UGC source");
}

void audio_arriving_during_join_is_deferred_in_order() {
    using namespace battlespades::network;
    Protocol168Session session;
    static_cast<void>(session.connected());
    expect(session.ingest(server_datagram(initial_info())).accepted,
           "audio deferral test must pass InitialInfo");
    std::vector<std::byte> music{std::byte{26U}};
    string(music, "last_man_standing_003");
    integer<std::int16_t>(music, 96); // 1.5 seconds
    expect(session.ingest(server_datagram(music)).accepted,
           "typed audio must not be reported as an unsupported join packet");
    const auto deferred = session.take_deferred_runtime_packets();
    expect(deferred.size() == 1U && deferred.front() == music,
           "join-time audio must retain its exact packet and arrival order");
    expect(session.take_deferred_runtime_packets().empty(),
           "deferred audio transfer must consume the bounded queue");
}

} // namespace

int main() {
    try {
        offline_ticket_and_initial_sequence_are_exact();
        native_steam_ticket_and_xor_sequence_are_exact();
        initial_info_retains_disabled_hud_presentation_flags();
        initial_info_preserves_host_and_client_ugc_roles();
        new_player_announcement_is_explicit_and_exact();
        state_data_retains_server_owned_team_and_menu_state();
        skybox_data_retains_only_a_safe_retail_definition();
        malformed_phase_packets_fail_closed();
        ugc_source_stream_is_framed_before_map_sync();
        audio_arriving_during_join_is_deferred_in_order();
        std::cout << "Protocol 168 session tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
