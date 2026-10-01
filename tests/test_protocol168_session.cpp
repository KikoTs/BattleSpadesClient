#include "battlespades/network/protocol168_session.hpp"
#include "battlespades/network/live_protocol168_connection.hpp"

#include <enet/enet.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <string>
#include <type_traits>
#include <utility>
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
    bool enable_numeric_hp = true, bool enable_player_score = true,
    bool friendly_fire = false,
    std::array<std::string_view, 5U> mode_strings = {
        "TDM", "Description", "One", "Two", "Three"},
    std::string_view map_name = "Training") {
    std::vector<std::byte> packet{std::byte{114U}};
    integer<std::uint64_t>(packet, 0U);
    integer<std::uint32_t>(packet, 0U);
    integer<std::uint32_t>(packet, 32887U);
    for (const auto value : mode_strings) {
        string(packet, value);
    }
    string(packet, map_name);
    string(packet, std::string{map_name} + ".vxl");
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
    packet.push_back(friendly_fire ? std::byte{1U} : std::byte{});
    packet.push_back(std::byte{}); // padding
    packet.push_back(std::byte{}); // corpse explosions
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
    expect(info->mode_name == "TDM" && info->mode_description == "Description" &&
               info->mode_infographic_text ==
                   std::array<std::string, 3U>{"One", "Two", "Three"} &&
               !info->friendly_fire,
           "InitialInfo must retain literal loading text and disabled friendly fire");
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
    expect(!info->beach_z_modifiable,
           "InitialInfo beach_z_modifiable is decoded (fixture byte 0 -> max z 237)");
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

void flight_profile_is_negotiated_bounded_and_separate_from_the_ticket() {
    using namespace battlespades::network;
    Protocol168SessionConfig config;
    config.negotiate_flight_profile = true;
    config.steam_ticket = {std::byte{'k'}, std::byte{'e'}, std::byte{'y'}};
    Protocol168Session session{config};
    const auto ticket = session.connected();
    expect(ticket.size() == 14U && ticket[2U] == std::byte{3U} &&
               ticket[6U] == std::byte{'k'} && ticket[9U] == std::byte{'B'} &&
               ticket.back() == std::byte{2U},
           "flight support (BSCF v2) must be explicit and outside the length-delimited auth key");
    // Exact server.flight_profile.BALANCED_FLIGHT.encode() bytes.
    const std::array<unsigned char, 20U> trailer{
        0x42,0x53,0x46,0x50,0x01,0x03,0x40,0x00,0x80,0x07,
        0x40,0x02,0xE0,0x01,0x00,0x05,0x00,0x05,0x00,0x05};
    auto packet = initial_info();
    const auto original_size = packet.size();
    for (const auto value : trailer) packet.push_back(static_cast<std::byte>(value));
    const auto result = session.ingest(server_datagram(packet));
    expect(result.accepted && session.initial_info(), "negotiated flight profile must survive handshake");
    const auto& profile = session.initial_info()->flight_profile;
    expect(profile.drain[1U] == 30.0 && profile.drain[2U] == 9.0 && profile.drain[3U] == 7.5 &&
               profile.refill[1U] == 20.0 && profile.refill[3U] == 20.0 &&
               profile.grounded_refill_only && profile.refill_idle_seconds == 1.0 &&
               profile.descending_parachute_only,
           "authoritative profile must reach prediction without rounded or guessed rates");
    const std::array<std::byte, 5U> validation{std::byte{60U}, std::byte{}, std::byte{}, std::byte{}, std::byte{}};
    expect(result.outbound_datagrams.front() == encode_protocol168_client_datagram(validation, config.steam_ticket),
           "capability trailer must not contaminate the XOR key");
    std::string error;
    const auto stock = decode_protocol168_initial_info(initial_info(), error);
    expect(stock && stock->flight_profile.drain[1U] == 75.0 && !stock->flight_profile.grounded_refill_only,
           "servers without a negotiated profile retain recovered defaults");
    for (std::size_t size{1U}; size < trailer.size(); ++size) {
        expect(!decode_protocol168_initial_info(std::span{packet}.first(original_size + size), error),
               "every truncated profile must fail closed");
    }
    for (const auto offset : {4U, 5U, 7U, 9U, 15U}) {
        auto invalid = packet;
        invalid[original_size + offset] = std::byte{0xFFU};
        expect(!decode_protocol168_initial_info(invalid, error),
               "unknown versions, flags, excessive delays and rates must be rejected");
    }
    packet.push_back(std::byte{});
    expect(!decode_protocol168_initial_info(packet, error), "extra profile bytes must not be ignored");
    expect(profile.engineer_flight_accel == 0.1F && profile.canopy_gravity_scale == 0.05F &&
               !profile.canopy_free_fall_floor,
           "a v1 profile keeps the stock Engineer flight and canopy literals");

    // Exact server.flight_profile.BALANCED_FLIGHT_V2.encode() bytes
    // (tests/test_flight_balance.py::test_v2_profile_wire_golden).
    const std::array<unsigned char, 24U> trailer_v2{
        0x42,0x53,0x46,0x50,0x02,0x07,0x40,0x00,0x80,0x07,0x40,0x02,
        0xE0,0x01,0x00,0x05,0x00,0x05,0x00,0x05,0x00,0x01,0xA0,0x00};
    auto packet_v2 = initial_info();
    for (const auto value : trailer_v2) packet_v2.push_back(static_cast<std::byte>(value));
    const auto v2 = decode_protocol168_initial_info(packet_v2, error);
    expect(v2 && v2->flight_profile.drain[3U] == 7.5 && v2->flight_profile.grounded_refill_only &&
               v2->flight_profile.descending_parachute_only &&
               v2->flight_profile.engineer_flight_accel == 0.25F &&
               v2->flight_profile.canopy_gravity_scale == 0.15625F &&
               v2->flight_profile.canopy_free_fall_floor,
           "BSFP v2 must deliver the exact Engineer flight and canopy tuning");
    const auto local = battlespades::world::balanced_flight_profile();
    expect(local.engineer_flight_accel == v2->flight_profile.engineer_flight_accel &&
               local.canopy_gravity_scale == v2->flight_profile.canopy_gravity_scale &&
               local.canopy_free_fall_floor == v2->flight_profile.canopy_free_fall_floor,
           "offline balance must equal the server's BALANCED_FLIGHT_V2");
    for (std::size_t size{1U}; size < trailer_v2.size(); ++size) {
        expect(!decode_protocol168_initial_info(std::span{packet_v2}.first(original_size + size), error),
               "every truncated v2 profile must fail closed");
    }
    for (const auto& [offset, value] : std::array<std::pair<std::size_t, unsigned>, 6U>{{
             {4U, 3U}, {5U, 8U}, {20U, 0U}, {21U, 0U}, {21U, 0x05U}, {23U, 0x05U}}}) {
        auto invalid = packet_v2;
        if (offset == 20U) invalid[original_size + 21U] = std::byte{0U};  // accel = 0
        invalid[original_size + offset] = static_cast<std::byte>(value);
        expect(!decode_protocol168_initial_info(invalid, error),
               "unknown versions/flags and zero or >1 mover tunings must be rejected");
    }
}

void initial_info_retains_bounded_loading_metadata() {
    using namespace battlespades::network;
    std::string error;
    const std::array<std::string_view, 5U> keys{
        "ZOM_TITLE", "ZOM_DESCRIPTION", "ZOM_INFOGRAPHIC_TEXT1",
        "ZOM_INFOGRAPHIC_TEXT2", "ZOM_INFOGRAPHIC_TEXT3"};
    const auto info = decode_protocol168_initial_info(
        initial_info(UgcRole::none, true, true, true, keys), error);
    expect(info.has_value() && error.empty() && info->friendly_fire &&
               info->mode_name == keys[0U] && info->mode_description == keys[1U] &&
               info->mode_infographic_text[0U] == keys[2U] &&
               info->mode_infographic_text[1U] == keys[3U] &&
               info->mode_infographic_text[2U] == keys[4U],
           "Loading localization keys and enabled friendly fire must survive decoding");

    std::string long_text(4096U, 'A');
    for (std::size_t index{}; index < keys.size(); ++index) {
        auto fields = keys;
        fields[index] = long_text;
        expect(decode_protocol168_initial_info(
                   initial_info(UgcRole::none, true, true, false, fields), error)
                   .has_value(),
               "Loading strings at the existing wire bound must still decode");
        const std::string oversized(4097U, 'A');
        fields[index] = oversized;
        expect(!decode_protocol168_initial_info(
                    initial_info(UgcRole::none, true, true, false, fields), error)
                    .has_value(),
               "Retaining loading metadata must not relax existing wire bounds");
    }
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

/** A flat, full-height 512x512 VXL: every column one solid voxel at z=239. */
std::vector<std::byte> flat_stock_map() {
    std::vector<std::byte> raw;
    raw.reserve(512U * 512U * 8U);
    for (std::size_t column{}; column < 512U * 512U; ++column) {
        for (const auto value : {0U, 239U, 239U, 0U, 0x40U, 0x50U, 0x60U, 0x7FU}) {
            raw.push_back(static_cast<std::byte>(value));
        }
    }
    return raw;
}

void local_map_crc_answers_validation_and_becomes_the_world_base() {
    using namespace battlespades::network;
    const auto directory =
        std::filesystem::temp_directory_path() / "aos_protocol168_session_crc_test";
    std::filesystem::create_directories(directory);
    const auto raw = flat_stock_map();
    {
        std::ofstream file{directory / "Training.vxl", std::ios::binary | std::ios::trunc};
        file.write(reinterpret_cast<const char*>(raw.data()),
                   static_cast<std::streamsize>(raw.size()));
    }
    const auto crc = protocol168_map_crc32(raw);
    expect(protocol168_map_crc32(std::as_bytes(std::span{"123456789", 9U})) == 0xCBF43926U,
           "crc32 is the zlib/IEEE polynomial");

    Protocol168SessionConfig config;
    config.local_map_directory = directory;
    Protocol168Session session{config};
    static_cast<void>(session.connected());
    const auto info = session.ingest(server_datagram(initial_info()));
    expect(info.accepted && info.outbound_datagrams.size() == 1U && session.sent_map_crc() == crc,
           "InitialInfo answers with crc32 of the local <filename>.vxl");
    const auto& validation = info.outbound_datagrams.front();
    std::uint32_t wire{};
    for (std::size_t index{}; index < 4U; ++index) {
        wire |= std::to_integer<std::uint32_t>(validation[2U + index]) << (index * 8U);
    }
    expect(validation[1U] == std::byte{60U} && wire == crc, "packet 60 carries the local CRC");
    expect(session.loading_progress().initial_info, "InitialInfo is a loader milestone");

    std::vector<std::byte> server_validation{std::byte{60U}};
    integer<std::uint32_t>(server_validation, crc);
    expect(session.ingest(server_datagram(server_validation)).accepted &&
               session.loading_progress().map_validated,
           "MapDataValidation is recorded (LOADING_MAP)");
    expect(session.ingest(server_datagram(std::array{std::byte{55U}})).accepted &&
               session.loading_progress().sync_started,
           "MapSyncStart opens the second loader third");
    expect(session.ingest(server_datagram(std::array{std::byte{59U}})).accepted,
           "a CRC match accepts an empty dirty-column delta");
    const auto* map = session.map();
    expect(map != nullptr && map->solid(10U, 20U, 239U) && !map->solid(10U, 20U, 238U) &&
               session.loading_progress().local_map_base &&
               session.loading_progress().sync_finished,
           "the local stock map became the world base");

    // A delta record replaces exactly the column it names.
    std::vector<std::byte> records;
    integer<std::uint32_t>(records, 5U);
    integer<std::uint32_t>(records, 6U);
    for (const auto value : {0U, 200U, 200U, 0U, 1U, 2U, 3U, 0x7FU}) {
        records.push_back(static_cast<std::byte>(value));
    }
    std::string error;
    const auto overlaid = protocol168_apply_map_records(raw, records, error);
    expect(overlaid.has_value() && overlaid->solid(5U, 6U, 200U) &&
               overlaid->solid(5U, 6U, 239U) && !overlaid->solid(5U, 6U, 199U) &&
               !overlaid->solid(6U, 6U, 200U),
           "an overlay record rebuilds only its own column");
    records.resize(records.size() - 3U);
    expect(!protocol168_apply_map_records(raw, records, error).has_value(),
           "a truncated record fails closed");

    Protocol168Session mismatch{config};
    static_cast<void>(mismatch.connected());
    static_cast<void>(mismatch.ingest(server_datagram(initial_info())));
    std::vector<std::byte> other_crc{std::byte{60U}};
    integer<std::uint32_t>(other_crc, crc ^ 1U);
    static_cast<void>(mismatch.ingest(server_datagram(other_crc)));
    static_cast<void>(mismatch.ingest(server_datagram(std::array{std::byte{55U}})));
    expect(!mismatch.ingest(server_datagram(std::array{std::byte{59U}})).accepted &&
               mismatch.phase() == Protocol168SessionPhase::failed,
           "a CRC mismatch still requires a complete snapshot");

    Protocol168SessionConfig missing = config;
    missing.local_map_directory = directory / "absent";
    Protocol168Session absent{missing};
    static_cast<void>(absent.connected());
    static_cast<void>(absent.ingest(server_datagram(initial_info())));
    expect(absent.sent_map_crc() == 0U, "a missing local map answers CRC 0");

    Protocol168Session ugc{config};
    static_cast<void>(ugc.connected());
    static_cast<void>(ugc.ingest(server_datagram(initial_info(UgcRole::host))));
    expect(ugc.sent_map_crc() == 0U, "UGC worlds always request a full sync");
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
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

void replacement_initial_info_discards_an_unfinished_map() {
    using namespace battlespades::network;
    Protocol168Session session;
    static_cast<void>(session.connected());
    expect(session.ingest(server_datagram(initial_info())).accepted, "begin interrupted transfer");
    expect(session.ingest(server_datagram(std::array{std::byte{27U}})).accepted,
           "retain deferred old-map audio");
    expect(session.ingest(server_datagram(std::array{std::byte{55U}})).accepted,
           "start interrupted MapSync");
    std::vector<std::byte> chunk{std::byte{57U}, std::byte{1U}, std::byte{3U}, std::byte{0U},
                                 std::byte{1U}, std::byte{2U}, std::byte{3U}};
    expect(session.ingest(server_datagram(chunk)).accepted && session.compressed_map_bytes() == 3U,
           "accumulate part of old map stream");
    const auto replacement = session.ingest(server_datagram(initial_info(
        UgcRole::none, true, true, false, {"TDM", "Description", "One", "Two", "Three"}, "NextMap")));
    expect(replacement.accepted && replacement.outbound_datagrams.size() == 1U &&
               replacement.outbound_datagrams.front()[1U] == std::byte{60U},
           "replacement restarts validation without another authentication");
    expect(session.map_generation() == 1U && session.compressed_map_bytes() == 0U &&
               session.initial_info()->map_name == "NextMap" && session.state_info() == nullptr &&
               !session.local_player_id().has_value() && session.take_deferred_runtime_packets().empty() &&
               !session.loading_progress().sync_started && !session.bootstrap_ready(),
           "all partial-map state is discarded at InitialInfo");
    expect(!session.ingest(server_datagram(std::array{std::byte{114U}})).accepted &&
               session.phase() == Protocol168SessionPhase::failed,
           "malformed replacement InitialInfo still fails closed");
}

void live_connection_loads_two_maps_on_one_authenticated_peer() {
    using namespace battlespades::network;
    expect(enet_initialize() == 0, "initialize rollover fixture ENet");
    struct EnetGuard { ~EnetGuard() { enet_deinitialize(); } } enet_guard;
    const auto directory = std::filesystem::temp_directory_path() /
                           "aos_protocol168_same_peer_rollover";
    std::filesystem::create_directories(directory);
    struct FilesGuard {
        std::filesystem::path path;
        ~FilesGuard() { std::error_code error; std::filesystem::remove_all(path, error); }
    } files_guard{directory};
    std::array<std::uint32_t, 2U> crcs{};
    for (std::size_t index{}; index < crcs.size(); ++index) {
        auto raw = flat_stock_map();
        if (index == 1U) {
            // Change one column while retaining the stock 240-high extent;
            // shorter worlds intentionally cannot use the local-CRC path.
            const auto column = (5U * 512U + 5U) * 8U;
            raw[column + 1U] = std::byte{220U};
            raw[column + 2U] = std::byte{220U};
        }
        crcs[index] = protocol168_map_crc32(raw);
        std::ofstream file{directory / (index == 0U ? "Training.vxl" : "NextMap.vxl"),
                           std::ios::binary | std::ios::trunc};
        file.write(reinterpret_cast<const char*>(raw.data()),
                   static_cast<std::streamsize>(raw.size()));
    }
    ENetAddress address{};
    expect(enet_address_set_host_ip(&address, "127.0.0.1") == 0, "rollover loopback");
    const std::unique_ptr<ENetHost, decltype(&enet_host_destroy)> server{
        enet_host_create(&address, 1U, 1U, 0U, 0U), &enet_host_destroy};
    expect(server != nullptr && enet_socket_get_address(server->socket, &address) == 0,
           "bind rollover server on an ephemeral port");
    expect(enet_host_compress_with_range_coder(server.get()) == 0, "rollover compression");
    LiveProtocol168Connection connection;
    Protocol168SessionConfig config;
    config.auto_join = false;
    config.local_map_directory = directory;
    config.steam_ticket = {std::byte{'a'}, std::byte{'1'}, std::byte{'B'}};
    expect(connection.start({"127.0.0.1", address.port, 5'000U}, config), "start rollover client");
    ENetPeer* peer{};
    unsigned connects{}, tickets{}, validations{};
    const auto send = [&](std::span<const std::byte> plain) {
        const auto bytes = server_datagram(plain);
        auto* packet = enet_packet_create(bytes.data(), bytes.size(), ENET_PACKET_FLAG_RELIABLE);
        expect(packet != nullptr, "allocate rollover packet");
        if (enet_peer_send(peer, 0U, packet) != 0) {
            enet_packet_destroy(packet);
            throw std::runtime_error{"send rollover packet"};
        }
        enet_host_flush(server.get());
    };
    const auto service = [&] {
        ENetEvent event{};
        expect(enet_host_service(server.get(), &event, 2U) >= 0, "service rollover fixture");
        if (event.type == ENET_EVENT_TYPE_CONNECT) {
            ++connects;
            peer = event.peer;
        } else if (event.type == ENET_EVENT_TYPE_DISCONNECT) {
            throw std::runtime_error{"same-peer rollover must not disconnect"};
        } else if (event.type == ENET_EVENT_TYPE_RECEIVE) {
            std::vector<std::byte> bytes{
                reinterpret_cast<const std::byte*>(event.packet->data),
                reinterpret_cast<const std::byte*>(event.packet->data + event.packet->dataLength)};
            enet_packet_destroy(event.packet);
            expect(bytes.size() >= 2U && bytes.front() == std::byte{0x30U}, "client packet framing");
            if (tickets == 0U) {
                expect(bytes[1U] == std::byte{105U}, "authenticate exactly once");
                ++tickets;
                send(initial_info());
                return;
            }
            for (std::size_t index{1U}; index < bytes.size(); ++index)
                bytes[index] ^= config.steam_ticket[(index - 1U) % config.steam_ticket.size()];
            expect(bytes[1U] == std::byte{60U}, "rollover keeps the same ticket key and sends validation, not reauthentication");
            expect(validations < 2U && bytes.size() == 6U, "one validation per map");
            std::uint32_t crc{};
            for (std::size_t index{}; index < 4U; ++index)
                crc |= std::to_integer<std::uint32_t>(bytes[2U + index]) << (index * 8U);
            expect(crc == crcs[validations], "new map validates its own local VXL");
            std::vector<std::byte> validation{std::byte{60U}};
            integer(validation, crc);
            send(validation);
            send(std::array{std::byte{55U}});
            send(std::array{std::byte{59U}});
            auto state = state_data();
            state[1U] = static_cast<std::byte>(17U + validations);
            send(state);
            if (validations == 1U)
                send(std::array{std::byte{27U}, std::byte{42U}});
            ++validations;
        }
    };
    const auto wait_bootstrap = [&] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
        std::unique_ptr<Protocol168WorldBootstrap> bootstrap;
        while (std::chrono::steady_clock::now() < deadline && !bootstrap) {
            service();
            bootstrap = connection.take_bootstrap();
        }
        expect(bootstrap != nullptr, "InitialInfo on an existing peer must complete another map bootstrap");
        return bootstrap;
    };
    auto first = wait_bootstrap();
    expect(first->map_generation == 0U && first->initial_info.map_name == "Training" && first->local_player_id == 17U &&
               first->map->solid(5U, 5U, 239U) && !first->map->solid(5U, 5U, 220U),
           "first bootstrap is the old map");
    first.reset();
    send(std::array{std::byte{27U}}); // old-map runtime packet intentionally left queued
    const auto queued_deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    while (connection.status().queued_inbound == 0U && std::chrono::steady_clock::now() < queued_deadline)
        service();
    expect(connection.status().queued_inbound != 0U, "old-map packet was queued before rollover");
    send(std::array{std::byte{52U}});
    send(initial_info(UgcRole::none, true, true, false,
                      {"TDM", "Description", "One", "Two", "Three"}, "NextMap"));
    const auto rollover_deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    bool ready{};
    while (std::chrono::steady_clock::now() < rollover_deadline && !ready) {
        service();
        const auto status = connection.status();
        ready = status.map_generation == 1U && status.phase == LiveProtocol168Phase::ready &&
                status.queued_inbound != 0U;
    }
    expect(ready, "InitialInfo on an existing peer must complete another map bootstrap");
    expect(connection.take_inbound().empty() && !connection.send(std::array{std::byte{27U}}),
           "pending bootstrap quarantines new-map packets and old-scene sends");
    auto second = connection.take_bootstrap();
    expect(second != nullptr && second->map_generation == 1U &&
               second->initial_info.map_name == "NextMap" && second->local_player_id == 18U &&
               second->next_client_loop_count == 0U && second->map->solid(5U, 5U, 220U),
           "second bootstrap resets identity labels and loads the new VXL");
    expect(connects == 1U && tickets == 1U && validations == 2U,
           "rotation uses one peer, one authentication, two map validations");
    expect(connection.take_inbound(64U, 0U).empty(),
           "a stale render tick cannot drain replacement-map packets");
    const auto inbound = connection.take_inbound(64U, second->map_generation);
    expect(inbound == std::vector<std::vector<std::byte>>{{std::byte{27U}, std::byte{42U}}},
           "new scene receives only its own queued runtime packets");
    expect(connection.status().phase == LiveProtocol168Phase::ready,
           "same transport is playable after the second bootstrap");
}

void deferred_runtime_memory_is_bounded_independently_of_map_transfer() {
    using namespace battlespades::network;
    Protocol168Session session;
    static_cast<void>(session.connected());
    expect(session.ingest(server_datagram(initial_info())).accepted, "reach deferred packet phase");
    // These complete, untrusted runtime bodies used to retain 64 MiB before
    // typed audio validation. Reject overload at admission, before a UI exists.
    std::vector<std::byte> large(detail::deferred_runtime_byte_limit / 4U, std::byte{});
    large.front() = std::byte{26U};
    const auto wire = server_datagram(large);
    for (unsigned index{}; index < 4U; ++index)
        expect(session.ingest(wire).accepted, "deferred burst must fit its byte budget");

    expect(session.ingest(server_datagram(std::array{std::byte{55U}})).accepted,
           "a full audio budget must not block the independent map stream");
    std::vector<std::byte> map_chunk{std::byte{57U}, std::byte{1U}, std::byte{}, std::byte{4U}};
    map_chunk.resize(1'028U, std::byte{0x55U});
    expect(session.ingest(server_datagram(map_chunk)).accepted && session.compressed_map_bytes() == 1'024U,
           "ordinary MapSync chunks retain their independent map byte limit");
    const auto drained = session.take_deferred_runtime_packets();
    expect(drained.size() == 4U && drained.front() == large && drained.back() == large,
           "bounded deferral must preserve complete packet bodies");
    for (unsigned index{}; index < 4U; ++index)
        expect(session.ingest(wire).accepted, "draining deferred audio must restore its byte budget");
    expect(!session.ingest(server_datagram(std::array{std::byte{27U}})).accepted &&
               session.phase() == Protocol168SessionPhase::failed &&
               session.last_error().find("queue overflow") != std::string_view::npos,
           "deferred byte overflow must fail closed before the 64-packet limit");
    expect(!session.ingest(wire).accepted && session.take_deferred_runtime_packets().size() == 4U,
           "a failed session must not continue accumulating untrusted audio");

    Protocol168Session replacement;
    static_cast<void>(replacement.connected());
    static_cast<void>(replacement.ingest(server_datagram(initial_info())));
    const auto small_packet = server_datagram(std::array{std::byte{27U}});
    for (std::size_t index{}; index < detail::deferred_runtime_packet_limit; ++index)
        expect(replacement.ingest(small_packet).accepted, "fresh session must retain its normal packet capacity");
    expect(!replacement.ingest(small_packet).accepted && replacement.phase() == Protocol168SessionPhase::failed,
           "the existing deferred packet-count bound must remain enforced");
}

} // namespace

int main() {
    try {
        offline_ticket_and_initial_sequence_are_exact();
        native_steam_ticket_and_xor_sequence_are_exact();
        flight_profile_is_negotiated_bounded_and_separate_from_the_ticket();
        initial_info_retains_disabled_hud_presentation_flags();
        initial_info_retains_bounded_loading_metadata();
        initial_info_preserves_host_and_client_ugc_roles();
        new_player_announcement_is_explicit_and_exact();
        state_data_retains_server_owned_team_and_menu_state();
        skybox_data_retains_only_a_safe_retail_definition();
        malformed_phase_packets_fail_closed();
        ugc_source_stream_is_framed_before_map_sync();
        local_map_crc_answers_validation_and_becomes_the_world_base();
        audio_arriving_during_join_is_deferred_in_order();
        replacement_initial_info_discards_an_unfinished_map();
        live_connection_loads_two_maps_on_one_authenticated_peer();
        deferred_runtime_memory_is_bounded_independently_of_map_transfer();
        std::cout << "Protocol 168 session tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
