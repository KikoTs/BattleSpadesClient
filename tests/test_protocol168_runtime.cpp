#include "battlespades/network/protocol168_runtime.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <variant>
#include <vector>

namespace {

using namespace battlespades::network;

void expect(bool value, const char* message) {
    if (!value)
        throw std::runtime_error{message};
}

[[nodiscard]] std::vector<std::byte> bytes(std::string_view hex) {
    const auto nibble = [](char value) -> std::uint8_t {
        if (value >= '0' && value <= '9')
            return static_cast<std::uint8_t>(value - '0');
        if (value >= 'a' && value <= 'f') {
            return static_cast<std::uint8_t>(value - 'a' + 10);
        }
        throw std::runtime_error{"invalid test hex"};
    };
    expect(hex.size() % 2U == 0U, "hex fixture must contain whole bytes");
    std::vector<std::byte> result;
    result.reserve(hex.size() / 2U);
    for (std::size_t index{}; index < hex.size(); index += 2U) {
        result.push_back(static_cast<std::byte>(
            static_cast<std::uint8_t>((nibble(hex[index]) << 4U) | nibble(hex[index + 1U]))));
    }
    return result;
}

void append_u16(std::vector<std::byte>& output, std::uint16_t value) {
    output.push_back(static_cast<std::byte>(value));
    output.push_back(static_cast<std::byte>(value >> 8U));
}

void append_i32(std::vector<std::byte>& output, std::int32_t value) {
    const auto raw = static_cast<std::uint32_t>(value);
    for (std::size_t index{}; index < 4U; ++index) {
        output.push_back(static_cast<std::byte>(raw >> (index * 8U)));
    }
}

void append_string(std::vector<std::byte>& output, std::string_view value) {
    for (const auto character : value) {
        output.push_back(static_cast<std::byte>(static_cast<std::uint8_t>(character)));
    }
    output.push_back(std::byte{});
}

void health_and_countdown_match_shared_packet() {
    auto decoded = decode_runtime_packet(bytes("05071960008f80c000"));
    expect(static_cast<bool>(decoded), decoded.error.c_str());
    const auto& hp = std::get<SetHpPacket>(*decoded.packet);
    expect(hp.health == 7U && hp.damage_type == 25U && hp.source[0U] == 1.5F &&
               hp.source[1U] == -143.0F / 64.0F && hp.source[2U] == 3.0F,
           "SetHP must retain health, damage type and fixed source");
    expect(!hp.has_directional_damage_source(),
           "only damage type 1 may create a directional HUD indicator");
    SetHpPacket attacker;
    attacker.damage_type = 1U;
    expect(attacker.has_directional_damage_source(),
           "attacker damage must retain its directional source");
    attacker.damage_type = 0U;
    expect(!attacker.has_directional_damage_source(),
           "self and fall damage must not invent a direction");
    attacker.damage_type = 2U;
    expect(!attacker.has_directional_damage_source(),
           "spawn/heal SetHP must not invent a direction");

    for (const auto damage_type : {0U, 1U, 2U, 3U, 4U}) {
        attacker.damage_type = static_cast<std::uint8_t>(damage_type);
        const bool expected = damage_type == 1U || damage_type == 3U || damage_type == 4U;
        expect(attacker.plays_local_hit_sound() == expected,
               "SetHP local impact audio must match retail damage-type routing");
    }

    decoded = decode_runtime_packet(bytes("540000f040"));
    expect(static_cast<bool>(decoded) &&
               std::get<DisplayCountdownPacket>(*decoded.packet).timer == 7.5F,
           "DisplayCountdown must decode the raw little-endian float");

    decoded = decode_runtime_packet(bytes("550001032a000000"));
    expect(static_cast<bool>(decoded), decoded.error.c_str());
    const auto& score = std::get<SetScorePacket>(*decoded.packet);
    expect(score.type == 0U && score.reason == 1U && score.specifier == 3U && score.value == 42,
           "SetScore must preserve type, reason, wire team/player id and value");

    decoded = decode_runtime_packet(bytes("2e03040105060100"));
    expect(static_cast<bool>(decoded), decoded.error.c_str());
    const auto& kill = std::get<KillActionPacket>(*decoded.packet);
    expect(kill.player_id == 3U && kill.killer_id == 4U && kill.kill_type == 1U &&
               kill.respawn_time == 5U && kill.kill_count == 6U && kill.domination && !kill.revenge,
           "KillAction must preserve the complete seven-byte retail body");

    decoded = decode_runtime_packet(bytes("31ff03426c756520746f6f6b2074686520696e74656c00"));
    expect(static_cast<bool>(decoded), decoded.error.c_str());
    const auto& chat = std::get<ChatMessagePacket>(*decoded.packet);
    expect(chat.player_id == 0xFFU && chat.chat_type == 3U && chat.value == "Blue took the intel",
           "ChatMessage must preserve the free-form CHAT_BIG body");

    decoded = decode_runtime_packet(
        bytes("32030143415054555245445f494e54454c00025445414d315f434f4c4f52004b696b6f0001"));
    expect(static_cast<bool>(decoded), decoded.error.c_str());
    const auto& localized = std::get<LocalisedMessagePacket>(*decoded.packet);
    expect(localized.chat_type == 3U && localized.localise_parameters &&
               localized.string_id == "CAPTURED_INTEL" &&
               localized.parameters == std::vector<std::string>{"TEAM1_COLOR", "Kiko"} &&
               localized.override_previous_message,
           "LocalisedMessage must preserve its bounded parameters and override flag");
}

void clock_entity_and_editor_packets_match_shared_packet() {
    auto decoded = decode_runtime_packet(bytes("00785634122a000000"));
    expect(static_cast<bool>(decoded), decoded.error.c_str());
    const auto& clock = std::get<ClockSyncPacket>(*decoded.packet);
    expect(clock.client_time == 0x12345678 && clock.server_loop_count == 42 &&
               encode_packet(clock) == bytes("00785634122a000000"),
           "ClockSync must round-trip both signed little-endian loop fields");

    decoded = decode_runtime_packet(bytes("0c09"));
    expect(static_cast<bool>(decoded) &&
               std::get<SetUgcEditModePacket>(*decoded.packet).mode == 9U,
           "SetUGCEditMode must be retained by the runtime dispatcher");
    decoded = decode_runtime_packet(bytes("6404"));
    expect(static_cast<bool>(decoded) &&
               std::get<UgcMessagePacket>(*decoded.packet).message ==
                   UgcMessageCode::request_map_info,
           "UGCMessage must be retained by the runtime dispatcher");
    decoded = decode_runtime_packet(bytes("654b"));
    expect(static_cast<bool>(decoded) &&
               std::get<UgcMapLoadingFromHostPacket>(*decoded.packet).percent == 75U,
           "UGCMapLoadingFromHost must be retained by the runtime dispatcher");

    decoded = decode_runtime_packet(bytes("1034120140008000c080"));
    expect(static_cast<bool>(decoded), decoded.error.c_str());
    const auto& moved = std::get<ChangeEntityPacket>(*decoded.packet);
    expect(moved.entity_id == 0x1234U && moved.action == ChangeEntityPacket::set_position &&
               moved.position == std::array<float, 3U>{1.0F, 2.0F, -3.0F},
           "ChangeEntity position must retain the fixed vector");

    decoded = decode_runtime_packet(bytes("10341205ff"));
    expect(static_cast<bool>(decoded) &&
               std::get<ChangeEntityPacket>(*decoded.packet).target_id == -1,
           "ChangeEntity target 0xff must remain the no-target sentinel");

    decoded = decode_runtime_packet(bytes("14341240008000c08007"));
    expect(static_cast<bool>(decoded), decoded.error.c_str());
    const auto& hit = std::get<HitEntityPacket>(*decoded.packet);
    expect(hit.entity_id == 0x1234U && hit.position == std::array<float, 3U>{1.0F, 2.0F, -3.0F} &&
               hit.type == 7U,
           "HitEntity must retain its effect position and part type");

    expect(std::holds_alternative<PrefabCompletePacket>(*decode_runtime_packet(bytes("1d")).packet),
           "PrefabComplete must be the exact bare id");
    decoded = decode_runtime_packet(bytes("240301"));
    expect(static_cast<bool>(decoded) &&
               std::get<ExplodeCorpsePacket>(*decoded.packet).show_explosion_effect,
           "ExplodeCorpse must preserve its canonical effect flag");

    decoded = decode_runtime_packet(bytes("4a0026160c"));
    expect(static_cast<bool>(decoded) && std::get<FogColorPacket>(*decoded.packet).color ==
                                             std::array<std::uint8_t, 3U>{12U, 22U, 38U},
           "FogColor must decode the legacy 00,B,G,R integer");

    decoded = decode_runtime_packet(bytes("440c01000000424c55455f424153450002000000"));
    expect(static_cast<bool>(decoded), decoded.error.c_str());
    const auto& objectives = std::get<UgcObjectivesPacket>(*decoded.packet);
    expect(objectives.mode == 12U && objectives.objectives.size() == 1U &&
               objectives.objectives.front().id == "BLUE_BASE" &&
               objectives.objectives.front().value == 2,
           "UGCObjectives must retain bounded string/value rows");

    decoded = decode_runtime_packet(bytes("6201000000020100feff03000f"));
    expect(static_cast<bool>(decoded), decoded.error.c_str());
    const auto& batch = std::get<InitialUgcBatchPacket>(*decoded.packet);
    expect(batch.items.size() == 1U && batch.items.front().mode == 2U &&
               batch.items.front().position == std::array<std::int16_t, 3U>{1, -2, 3} &&
               batch.items.front().item_id == 15U,
           "InitialUGCBatch must retain its exact eight-byte records");

    decoded = decode_runtime_packet(bytes("660800000089504e470d0a1a0a"));
    expect(static_cast<bool>(decoded) &&
               std::get<UgcMapInfoPacket>(*decoded.packet).png_data.size() == 8U,
           "UGCMapInfo must accept a bounded PNG payload");

    decoded = decode_runtime_packet(bytes("6d3f00000002494e54524f0057414c4b00"));
    expect(static_cast<bool>(decoded), decoded.error.c_str());
    const auto& help = std::get<HelpMessagePacket>(*decoded.packet);
    expect(help.delay == 0.5F && help.message_ids == std::vector<std::string>{"INTRO", "WALK"},
           "HelpMessage must honor its exceptional big-endian delay");

    decoded = decode_runtime_packet(bytes("76020c1626ff01020304"));
    expect(static_cast<bool>(decoded), decoded.error.c_str());
    const auto& ground = std::get<SetGroundColorsPacket>(*decoded.packet);
    expect(ground.colors.size() == 2U &&
               ground.colors[0U] == std::array<std::uint8_t, 4U>{12U, 22U, 38U, 255U} &&
               ground.colors[1U] == std::array<std::uint8_t, 4U>{1U, 2U, 3U, 4U},
           "SetGroundColors must preserve authored RGB/Z-threshold order");
    expect(encode_packet(ground) == bytes("76020c1626ff01020304"),
           "SetGroundColors must round-trip the complete authored palette");

    decoded = decode_runtime_packet(bytes("33557365725f47726173736c616e642e74787400"));
    expect(static_cast<bool>(decoded), decoded.error.c_str());
    const auto& sky = std::get<SkyboxDataPacket>(*decoded.packet);
    expect(sky.definition_name == "User_Grassland.txt" &&
               encode_packet(sky) == bytes("33557365725f47726173736c616e642e74787400"),
           "SkyboxData must round-trip a safe stock definition");
    expect(encode_packet(SkyboxDataPacket{"../Tokyo.txt"}).empty(),
           "SkyboxData encoder must reject path traversal");
}

void pickup_burden_is_strictly_decoded() {
    auto decoded = decode_runtime_packet(bytes("46030e01"));
    expect(static_cast<bool>(decoded), decoded.error.c_str());
    const auto& pickup = std::get<PickPickupPacket>(*decoded.packet);
    expect(pickup.player_id == 3U && pickup.pickup_id == 14U && pickup.burdensome,
           "PickPickup must preserve owner, pickup id and burdensome state");
    expect(!decode_runtime_packet(bytes("46030e02")),
           "PickPickup burdensome must be a canonical boolean");
}

void change_player_variable_tail_is_strictly_decoded() {
    auto decoded = decode_runtime_packet(bytes("1103000801"));
    expect(static_cast<bool>(decoded), decoded.error.c_str());
    const auto& visible = std::get<ChangePlayerPacket>(*decoded.packet);
    expect(visible.player_id == 3U &&
               visible.type == ChangePlayerPacket::set_high_minimap_visibility &&
               visible.high_minimap_visibility,
           "ChangePlayer action 8 must retain the extra visibility byte");

    decoded = decode_runtime_packet(bytes("1103000900"));
    expect(static_cast<bool>(decoded), decoded.error.c_str());
    const auto& chase = std::get<ChangePlayerPacket>(*decoded.packet);
    expect(chase.type == ChangePlayerPacket::set_chase_cam && !chase.chase_cam,
           "ChangePlayer action 9 must retain the extra chase-camera byte");

    decoded = decode_runtime_packet(bytes("11030007"));
    expect(static_cast<bool>(decoded) && std::get<ChangePlayerPacket>(*decoded.packet).type == 7U,
           "older ChangePlayer actions keep the four-byte base framing");
    expect(!decode_runtime_packet(bytes("1103000802")),
           "ChangePlayer mode flags must be canonical booleans");
    expect(!decode_runtime_packet(bytes("1103000700")),
           "ChangePlayer actions below eight must reject trailing bytes");
}

void entity_and_audio_records_are_framed_exactly() {
    // id=21, entity=0x1234, type/state/player=33/2/7, twelve fixed shorts,
    // face=4, int/float property counts=1/1, ugc=9.
    auto entity = bytes("153412210207");
    entity.insert(entity.end(), 22U, std::byte{});
    entity.push_back(std::byte{4U});
    entity.push_back(std::byte{0x40U});
    entity.push_back(std::byte{0U});
    entity.push_back(std::byte{1U});
    entity.push_back(std::byte{1U});
    entity.push_back(std::byte{9U});
    entity.insert(entity.end(),
                  {std::byte{0x78U},
                   std::byte{0x56U},
                   std::byte{0x34U},
                   std::byte{0x12U},
                   std::byte{0x20U},
                   std::byte{0U}});
    auto decoded = decode_runtime_packet(entity);
    expect(static_cast<bool>(decoded), decoded.error.c_str());
    const auto& created = std::get<CreateEntityPacket>(*decoded.packet);
    expect(created.entity_id == 0x1234U && created.type == 33U && created.face == 4U &&
               created.fuse == 1.0F &&
               created.integer_properties == std::vector<std::int32_t>{0x12345678} &&
               created.float_properties == std::vector<float>{0.5F},
           "CreateEntity must stop exactly after its bounded property arrays");
    expect(!created.has_explicit_color(),
           "zero CreateEntity RGB must select the entity/team material");
    CreateEntityPacket authored_color;
    authored_color.color = {0.0F, 64.0F, 0.0F};
    expect(authored_color.has_explicit_color(),
           "a nonzero CreateEntity channel must remain an authored tint");

    decoded = decode_runtime_packet(bytes("17630340002000076000c0a3c0008000"));
    expect(static_cast<bool>(decoded), decoded.error.c_str());
    const auto& sound = std::get<PlaySoundPacket>(*decoded.packet);
    expect(sound.sound_id == 99U && sound.looping && sound.positioned && sound.loop_id == 7U &&
               sound.volume == 1.0F && sound.position[0U] == 1.5F && sound.attenuation == 2.0F,
           "PlaySound optional fields must follow its two flag bits");

    decoded = decode_runtime_packet(bytes("16616d625f727572616c000300"));
    expect(static_cast<bool>(decoded), decoded.error.c_str());
    const auto& ambient = std::get<CreateAmbientSoundPacket>(*decoded.packet);
    expect(ambient.name == "amb_rural" && ambient.loop_id == 3U && ambient.points.empty(),
           "CreateAmbientSound must retain its string and byte loop id");

    decoded = decode_runtime_packet(bytes("1907"));
    expect(static_cast<bool>(decoded) && std::get<StopSoundPacket>(*decoded.packet).loop_id == 7U,
           "StopSound must release the exact Protocol 168 loop id");
}

void minimap_and_team_mode_records_are_framed_exactly() {
    auto decoded = decode_runtime_packet(bytes("2934120211223340008080c0007669700001"));
    expect(static_cast<bool>(decoded), decoded.error.c_str());
    const auto& billboard = std::get<MinimapBillboardPacket>(*decoded.packet);
    expect(billboard.entity_id == 0x1234U && billboard.key == 2U &&
               billboard.color == std::array<std::uint8_t, 3U>{0x33U, 0x22U, 0x11U} &&
               billboard.position == std::array<float, 3U>{1.0F, -2.0F, 3.0F} &&
               billboard.icon_name == "vip" && billboard.tracking,
           "MinimapBillboard must convert wire BGR and retain fixed position");

    decoded = decode_runtime_packet(bytes("2b0311223301000200030004000500060020000501"));
    expect(static_cast<bool>(decoded), decoded.error.c_str());
    const auto& zone = std::get<MinimapZonePacket>(*decoded.packet);
    expect(zone.key == 3U && zone.color == std::array<std::uint8_t, 3U>{0x33U, 0x22U, 0x11U} &&
               zone.minimum == std::array<std::int16_t, 3U>{1, 2, 3} &&
               zone.maximum == std::array<std::int16_t, 3U>{4, 5, 6} && zone.icon_scale == 0.5F &&
               zone.icon_id == 5U && zone.locked_in_zone,
           "MinimapZone must preserve XYZ-grouped bounds and fixed scale");

    decoded = decode_runtime_packet(bytes("2c010002000300040005000600"));
    expect(static_cast<bool>(decoded), decoded.error.c_str());
    const auto& clear = std::get<MinimapZoneClearPacket>(*decoded.packet);
    expect(clear.minimum == std::array<std::int16_t, 3U>{1, 2, 3} &&
               clear.maximum == std::array<std::int16_t, 3U>{4, 5, 6},
           "MinimapZoneClear must identify the exact six-bound key");

    decoded = decode_runtime_packet(bytes("6c010002000300040005000600"));
    expect(static_cast<bool>(decoded), decoded.error.c_str());
    const auto& lock = std::get<LockToZonePacket>(*decoded.packet);
    expect(lock.minimum == std::array<std::int16_t, 3U>{1, 2, 3} &&
               lock.maximum == std::array<std::int16_t, 3U>{4, 5, 6},
           "LockToZone must preserve its authoritative XYZ volume");

    decoded = decode_runtime_packet(bytes("4f0001"));
    expect(static_cast<bool>(decoded), decoded.error.c_str());
    expect(std::get<LockTeamPacket>(*decoded.packet).team_id == 0U &&
               std::get<LockTeamPacket>(*decoded.packet).locked,
           "LockTeam must retain the spectator lock");

    decoded = decode_runtime_packet(bytes("500201"));
    expect(static_cast<bool>(decoded), decoded.error.c_str());
    expect(std::get<TeamLockClassPacket>(*decoded.packet).team_id == 2U &&
               std::get<TeamLockClassPacket>(*decoded.packet).locked,
           "TeamLockClass must retain the target team and boolean");

    decoded = decode_runtime_packet(bytes("510300"));
    expect(static_cast<bool>(decoded), decoded.error.c_str());
    expect(std::get<TeamLockScorePacket>(*decoded.packet).team_id == 3U &&
               !std::get<TeamLockScorePacket>(*decoded.packet).locked,
           "TeamLockScore must retain the target team and boolean");

    decoded = decode_runtime_packet(bytes("520201"));
    expect(static_cast<bool>(decoded), decoded.error.c_str());
    expect(std::get<TeamInfiniteBlocksPacket>(*decoded.packet).team_id == 2U &&
               std::get<TeamInfiniteBlocksPacket>(*decoded.packet).infinite,
           "TeamInfiniteBlocks must retain the server gameplay override");

    decoded = decode_runtime_packet(bytes("530201"));
    expect(static_cast<bool>(decoded), decoded.error.c_str());
    const auto& visibility = std::get<TeamMapVisibilityPacket>(*decoded.packet);
    expect(visibility.team_id == 2U && visibility.visible,
           "TeamMapVisibility must retain its canonical boolean");

    decoded = decode_runtime_packet(bytes("75020f200001"));
    expect(static_cast<bool>(decoded), decoded.error.c_str());
    const auto& percent = std::get<TeamProgressPacket>(*decoded.packet);
    expect(percent.team_id == 2U && percent.visible && percent.show_particle &&
               percent.show_previous && percent.show_as_percent && percent.percent == 0.5F &&
               percent.icon_id == 1U,
           "TeamProgress percent form must follow its four flag bits");

    decoded = decode_runtime_packet(bytes("7503050a0000003200000000"));
    expect(static_cast<bool>(decoded), decoded.error.c_str());
    const auto& fraction = std::get<TeamProgressPacket>(*decoded.packet);
    expect(fraction.team_id == 3U && fraction.visible && !fraction.show_particle &&
               fraction.show_previous && !fraction.show_as_percent && fraction.numerator == 10 &&
               fraction.denominator == 50 && fraction.icon_id == 0U,
           "TeamProgress fraction form must retain signed 32-bit values");

    decoded = decode_runtime_packet(bytes("6a02050203800c"));
    expect(static_cast<bool>(decoded), decoded.error.c_str());
    const auto& territory = std::get<TerritoryBaseStatePacket>(*decoded.packet);
    expect(territory.base_index == 2U && territory.action == 5U && territory.controlled_by == 2U &&
               territory.attacked_by == 3U && territory.capture_amount == 50.0F,
           "TerritoryBaseState must preserve action, teams, and fixed capture");

    // TC_DETAIL_NOT_REQUIRED actions (3 entering, 4 leaving, 6 contended,
    // 7 uncontended) are the short base_index + action form.
    for (const auto* hex : {"6a0203", "6a0204", "6a0206", "6a0207"}) {
        decoded = decode_runtime_packet(bytes(hex));
        expect(static_cast<bool>(decoded), decoded.error.c_str());
        const auto& short_form = std::get<TerritoryBaseStatePacket>(*decoded.packet);
        expect(short_form.base_index == 2U && short_form.capture_amount == 0.5F,
               "TerritoryBaseState short form must decode base and action only");
    }
    expect(!decode_runtime_packet(bytes("6a0206020300")),
           "the short TerritoryBaseState form must reject trailing bytes");
}

void chat_vote_and_end_map_records_are_framed_exactly() {
    ChatMessagePacket outgoing_chat{
        .player_id = 7U,
        .chat_type = 1U,
        .value = "hold the base",
    };
    const auto encoded_chat = encode_packet(outgoing_chat);
    auto decoded = decode_runtime_packet(encoded_chat);
    expect(static_cast<bool>(decoded), decoded.error.c_str());
    const auto& chat = std::get<ChatMessagePacket>(*decoded.packet);
    expect(chat.player_id == 7U && chat.chat_type == 1U && chat.value == "hold the base",
           "client chat must round-trip through the retail packet");

    std::vector<std::byte> vote{std::byte{GenericVoteMessagePacket::id},
                                std::byte{0xFFU},
                                std::byte{GenericVoteMessagePacket::start}};
    append_u16(vote, 3U);
    for (std::int32_t index{}; index < 3; ++index) {
        append_string(vote, "('Map " + std::to_string(index + 1) + "', ())");
        append_i32(vote, index * 2);
    }
    append_string(vote, "('VOTE_MAP_TITLE', ())");
    append_string(vote, "('VOTE_MAP_DESCRIPTION', ())");
    vote.push_back(std::byte{0x05U});
    decoded = decode_runtime_packet(vote);
    expect(static_cast<bool>(decoded), decoded.error.c_str());
    const auto& started = std::get<GenericVoteMessagePacket>(*decoded.packet);
    expect(started.candidates.size() == 3U && started.candidates[2U].votes == 4 &&
               started.allow_revote && started.can_vote && !started.hide_after_vote,
           "GenericVote must retain three bounded candidates and flag bits");

    const auto selected_wire_name = started.candidates[1U].name;
    const auto cast = encode_generic_vote_cast(7U, selected_wire_name);
    decoded = decode_runtime_packet(cast);
    expect(static_cast<bool>(decoded), decoded.error.c_str());
    const auto& cast_packet = std::get<GenericVoteMessagePacket>(*decoded.packet);
    expect(cast_packet.message_type == GenericVoteMessagePacket::cast &&
               cast_packet.candidates.size() == 1U &&
               cast_packet.candidates.front().name == selected_wire_name,
           "vote CAST must echo the exact opaque candidate token");

    std::vector<std::byte> statistics{std::byte{GameStatsPacket::id}};
    append_i32(statistics, 2);
    append_i32(statistics, 2);
    append_i32(statistics, 7);
    append_i32(statistics, 5);
    append_i32(statistics, 8);
    append_i32(statistics, 11);
    decoded = decode_runtime_packet(statistics);
    expect(static_cast<bool>(decoded), decoded.error.c_str());
    const auto& stats = std::get<GameStatsPacket>(*decoded.packet);
    expect(stats.team_id == 2 && stats.entries.size() == 2U && stats.entries[1U].player_id == 8 &&
               stats.entries[1U].stat_type == 11,
           "GameStats must preserve the per-team award list");

    std::vector<std::byte> rank_ups{std::byte{RankUpsPacket::id}};
    append_i32(rank_ups, 1);
    append_i32(rank_ups, 1);
    append_string(rank_ups, "20");
    append_string(rank_ups, "21");
    decoded = decode_runtime_packet(rank_ups);
    expect(static_cast<bool>(decoded), decoded.error.c_str());
    const auto& rank = std::get<RankUpsPacket>(*decoded.packet);
    expect(rank.entries.size() == 1U && rank.entries.front().score_reason == 1 &&
               rank.entries.front().old_score == 20 && rank.entries.front().new_score == 21,
           "RankUps must decode reason and numeric C-string scores");

    expect(std::holds_alternative<ShowGameStatsPacket>(*decode_runtime_packet(bytes("35")).packet),
           "ShowGameStats is a one-byte activation packet");
    decoded = decode_runtime_packet(bytes("4801"));
    expect(static_cast<bool>(decoded) &&
               std::get<ForceShowScoresPacket>(*decoded.packet).forced,
           "ForceShowScores must retain the authoritative forced-open flag");
    decoded = decode_runtime_packet(bytes("12004040800008"));
    expect(static_cast<bool>(decoded) &&
               std::get<PoiFocusPacket>(*decoded.packet).target ==
                   std::array<float, 3U>{256.0F, -1.0F, 32.0F},
           "POIFocus(18) must decode three fixed16 target coordinates");
    expect(!decode_runtime_packet(bytes("1200404080")),
           "a truncated POIFocus(18) must be rejected");
    decoded = decode_runtime_packet(bytes("49068001"));
    expect(static_cast<bool>(decoded), decoded.error.c_str());
    const auto& result_message =
        std::get<ShowTextMessagePacket>(*decoded.packet);
    expect(result_message.message_id == 6U && result_message.duration == 6.0F,
           "ShowTextMessage must retain its selector and signed fixed duration");
    decoded = decode_runtime_packet(bytes("49034080"));
    expect(static_cast<bool>(decoded) &&
               std::get<ShowTextMessagePacket>(*decoded.packet).duration == -1.0F,
           "ShowTextMessage must preserve retail sign-magnitude fixed values");
    expect(std::holds_alternative<MapEndedPacket>(*decode_runtime_packet(bytes("34")).packet),
           "MapEnded is a one-byte loader boundary");
}

void malformed_or_unknown_records_fail_closed() {
    expect(!decode_runtime_packet(bytes("00785634")), "truncated ClockSync must fail");
    expect(!decode_runtime_packet(bytes("10341208")), "unknown ChangeEntity action must fail");
    expect(!decode_runtime_packet(bytes("240302")),
           "ExplodeCorpse effect must be a canonical boolean");
    expect(!decode_runtime_packet(bytes("4a0126160c")), "FogColor low padding byte must stay zero");
    expect(!decode_runtime_packet(bytes("62ffffffff")), "negative InitialUGCBatch count must fail");
    expect(!decode_runtime_packet(bytes("66080000004e4f54504e472121")),
           "UGCMapInfo must reject a non-PNG payload");
    expect(!decode_runtime_packet(bytes("6d7fc0000000")), "HelpMessage non-finite delay must fail");
    expect(!decode_runtime_packet(bytes("05")), "truncated SetHP must fail");
    expect(!decode_runtime_packet(bytes("540000c07f")), "non-finite countdown must fail");
    expect(!decode_runtime_packet(bytes("5502010300000000")),
           "SetScore type must be TEAM(0) or PLAYER(1)");
    expect(!decode_runtime_packet(bytes("2e03040105060200")),
           "KillAction domination/revenge bytes must be canonical booleans");
    expect(!decode_runtime_packet(bytes("320302580000")),
           "LocalisedMessage localization flag must be canonical");
    expect(!decode_runtime_packet(bytes("320300580011")),
           "LocalisedMessage parameter count must remain bounded");
    expect(!decode_runtime_packet(bytes("2934120211223340008080c0007669700002")),
           "MinimapBillboard tracking must be a canonical boolean");
    expect(!decode_runtime_packet(bytes("2b0311223301000200030004000500060020000502")),
           "MinimapZone locked flag must be canonical");
    expect(!decode_runtime_packet(bytes("530202")),
           "TeamMapVisibility must reject noncanonical booleans");
    expect(!decode_runtime_packet(bytes("4f0101")),
           "LockTeam must reject the neutral pseudo-team");
    expect(!decode_runtime_packet(bytes("500001")),
           "TeamLockClass must reject the spectator team");
    expect(!decode_runtime_packet(bytes("51020200")),
           "team boolean packets must reject trailing bytes");
    expect(!decode_runtime_packet(bytes("520302")),
           "TeamInfiniteBlocks must reject noncanonical booleans");
    expect(!decode_runtime_packet(bytes("530001")),
           "TeamMapVisibility must reject the spectator team");
    expect(!decode_runtime_packet(bytes("7502f1200001")),
           "TeamProgress must reject unknown high flag bits");
    expect(!decode_runtime_packet(bytes("6a02080203800c")),
           "TerritoryBaseState must reject actions outside the retail enum");
    expect(!decode_runtime_packet(bytes("6a02050204800c")),
           "TerritoryBaseState must reject teams absent from TEAM_COLOURS");
    expect(!decode_runtime_packet(bytes("6a0205020380")),
           "TerritoryBaseState must reject a truncated fixed capture value");
    expect(!decode_runtime_packet(bytes("2fff000400000000")),
           "GenericVote must reject more rows than the three-key HUD");
    expect(!decode_runtime_packet(bytes("2fff000000000008")),
           "GenericVote must reject unknown flag bits");
    expect(!decode_runtime_packet(bytes("438100000002000000")),
           "GameStats must reject an unbounded award count");
    expect(!decode_runtime_packet(bytes("420100000001000000323100323000")),
           "RankUps must reject a decreasing persistent score");
    expect(!decode_runtime_packet(bytes("4802")),
           "ForceShowScores must reject noncanonical booleans");
    expect(!decode_runtime_packet(bytes("4909c000")),
           "ShowTextMessage must reject selectors outside the retail enum");
    expect(!decode_runtime_packet(bytes("4901c0")),
           "ShowTextMessage must reject a truncated fixed duration");
    expect(!decode_runtime_packet(bytes("4901c00000")),
           "ShowTextMessage must reject trailing bytes");
    expect(encode_packet(
               ChatMessagePacket{.player_id = 1U, .chat_type = 0U, .value = std::string(201U, 'x')})
               .empty(),
           "client chat must reject payloads above the retail wire limit");
    expect(!decode_runtime_packet(bytes("ff")), "unknown runtime id must fail");
}

} // namespace

int main() {
    try {
        health_and_countdown_match_shared_packet();
        clock_entity_and_editor_packets_match_shared_packet();
        pickup_burden_is_strictly_decoded();
        change_player_variable_tail_is_strictly_decoded();
        entity_and_audio_records_are_framed_exactly();
        minimap_and_team_mode_records_are_framed_exactly();
        chat_vote_and_end_map_records_are_framed_exactly();
        malformed_or_unknown_records_fail_closed();
        std::cout << "Protocol 168 runtime packet tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
