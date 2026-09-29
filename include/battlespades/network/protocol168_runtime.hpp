#pragma once

#include "battlespades/network/protocol168_ugc.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace battlespades::network {

/**
 * Bidirectional packet 0. The client echoes a monotonic millisecond token and
 * the server returns its current simulation loop in the second field.
 */
struct ClockSyncPacket final {
    static constexpr std::uint8_t id{0U};
    std::int32_t client_time{};
    std::int32_t server_loop_count{};
};

struct SetHpPacket final {
    static constexpr std::uint8_t id{5U};
    std::uint8_t health{};
    std::uint8_t damage_type{};
    std::array<float, 3U> source{};

    /** Retail type 1 carries an attacker direction; 0 is self/fall, 2 heal. */
    [[nodiscard]] bool has_directional_damage_source() const noexcept {
        return damage_type == 1U;
    }

    /**
     * Whether retail plays its local ``hitplayer`` impact cue for this row.
     *
     * Recovered from GameScene.process_packet_set_hp in the symbol-rich macOS
     * binary: attacker, burn and the fourth damage class each play the cue,
     * while self/fall (0) and heal/spawn (2) deliberately stay silent.
     */
    [[nodiscard]] bool plays_local_hit_sound() const noexcept {
        return damage_type == 1U || damage_type == 3U || damage_type == 4U;
    }
};

/** Packet 17's two variable-width HUD/camera actions used by retail modes. */
struct ChangePlayerPacket final {
    static constexpr std::uint8_t id{17U};
    static constexpr std::uint8_t set_high_minimap_visibility{8U};
    static constexpr std::uint8_t set_chase_cam{9U};
    std::uint16_t player_id{};
    std::uint8_t type{};
    bool high_minimap_visibility{};
    bool chase_cam{};
};

/** One field mutation for an already-created packet-21 entity. */
struct ChangeEntityPacket final {
    static constexpr std::uint8_t id{16U};
    enum Action : std::uint8_t {
        set_state = 0U,
        set_position = 1U,
        set_velocity = 2U,
        set_player = 3U,
        set_forward_vector = 4U,
        set_target = 5U,
        set_fuse = 6U,
        set_ammo = 7U,
    };

    std::uint16_t entity_id{};
    std::uint8_t action{};
    std::uint8_t state{};
    std::array<float, 3U> position{};
    std::array<float, 3U> velocity{};
    std::uint8_t player_id{};
    std::array<float, 3U> forward{};
    /** -1 is the retail sentinel for no target. */
    std::int16_t target_id{-1};
    float fuse{};
    float ammo{};
};

/** Server-confirmed impact on a deployable/entity; health stays server-only. */
struct HitEntityPacket final {
    static constexpr std::uint8_t id{20U};
    std::uint16_t entity_id{};
    std::array<float, 3U> position{};
    std::uint8_t type{};
};

/** Full packet-21 entity record used by pickups, projectiles and deployables. */
struct CreateEntityPacket final {
    static constexpr std::uint8_t id{21U};
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
     * Entity.color is mandatory on the wire, but zero RGB is retail's
     * "use the entity/team material" sentinel. Treating it as authored black
     * hides the mask pixels that identify friendly mines and turrets.
     */
    [[nodiscard]] bool has_explicit_color() const noexcept {
        return color[0U] != 0.0F || color[1U] != 0.0F || color[2U] != 0.0F;
    }
};

struct DestroyEntityPacket final {
    static constexpr std::uint8_t id{19U};
    std::uint16_t entity_id{};
};

/** Acknowledges completion of the owner's deferred prefab build/erase. */
struct PrefabCompletePacket final {
    static constexpr std::uint8_t id{29U};
};

/** Removes a retained grave/corpse and optionally emits its explosion effect. */
struct ExplodeCorpsePacket final {
    static constexpr std::uint8_t id{36U};
    std::uint8_t player_id{};
    bool show_explosion_effect{};
};

/** Server-authored 3D objective marker mirrored by retail onto the minimap. */
struct MinimapBillboardPacket final {
    static constexpr std::uint8_t id{41U};
    std::uint16_t entity_id{};
    std::uint8_t key{};
    std::array<std::uint8_t, 3U> color{};
    std::array<float, 3U> position{};
    std::string icon_name;
    bool tracking{};
};

struct MinimapBillboardClearPacket final {
    static constexpr std::uint8_t id{42U};
    std::uint16_t entity_id{};
};

/**
 * Server-authored objective volume. Bounds are integral voxel coordinates;
 * packet order is x1,y1,z1,x2,y2,z2 despite the opaque retail field names.
 */
struct MinimapZonePacket final {
    static constexpr std::uint8_t id{43U};
    std::uint8_t key{};
    std::array<std::uint8_t, 3U> color{};
    std::array<std::int16_t, 3U> minimum{};
    std::array<std::int16_t, 3U> maximum{};
    float icon_scale{};
    std::uint8_t icon_id{};
    bool locked_in_zone{};
};

struct MinimapZoneClearPacket final {
    static constexpr std::uint8_t id{44U};
    std::array<std::int16_t, 3U> minimum{};
    std::array<std::int16_t, 3U> maximum{};
};

/** Server-owned local movement volume used by Demolition's build phase. */
struct LockToZonePacket final {
    static constexpr std::uint8_t id{108U};
    std::array<std::int16_t, 3U> minimum{};
    std::array<std::int16_t, 3U> maximum{};
};

struct CreateAmbientSoundPacket final {
    static constexpr std::uint8_t id{22U};
    std::string name;
    std::uint8_t loop_id{};
    std::vector<std::array<std::int16_t, 3U>> points;
};

struct PlaySoundPacket final {
    static constexpr std::uint8_t id{23U};
    std::uint8_t sound_id{};
    bool looping{};
    bool positioned{};
    float volume{};
    float time{};
    std::uint8_t loop_id{};
    std::array<float, 3U> position{};
    float attenuation{};
};

struct PlayAmbientSoundPacket final {
    static constexpr std::uint8_t id{24U};
    std::string name;
    bool looping{};
    bool positioned{};
    float volume{};
    float time{};
    std::uint8_t loop_id{};
    std::array<float, 3U> position{};
    float attenuation{};
};

struct StopSoundPacket final {
    static constexpr std::uint8_t id{25U};
    std::uint8_t loop_id{};
};

struct PlayMusicPacket final {
    static constexpr std::uint8_t id{26U};
    std::string name;
    float seconds_played{};
};

struct StopMusicPacket final {
    static constexpr std::uint8_t id{27U};
};

struct PlayerLeftPacket final {
    static constexpr std::uint8_t id{64U};
    std::uint8_t player_id{};
};

struct UgcObjective final {
    std::string id;
    std::int32_t value{};
};

/** Retained UGC validation rows shown by the editor HUD. */
struct UgcObjectivesPacket final {
    static constexpr std::uint8_t id{68U};
    std::uint8_t mode{};
    std::vector<UgcObjective> objectives;
};

/** Server confirmation that a player now carries pickup/objective state. */
struct PickPickupPacket final {
    static constexpr std::uint8_t id{70U};
    std::uint8_t player_id{};
    std::uint8_t pickup_id{};
    bool burdensome{};
};

/** Runtime `/fog` override. Wire bytes are 00,B,G,R inside one little int. */
struct FogColorPacket final {
    static constexpr std::uint8_t id{74U};
    std::array<std::uint8_t, 3U> color{};
};

/** Packet 98's exact eight-byte UGC placement record. */
struct InitialUgcItem final {
    std::uint8_t mode{};
    std::array<std::int16_t, 3U> position{};
    std::uint8_t item_id{};
};

struct InitialUgcBatchPacket final {
    static constexpr std::uint8_t id{98U};
    std::vector<InitialUgcItem> items;
};

/** Optional bounded PNG preview authored by the UGC host. */
struct UgcMapInfoPacket final {
    static constexpr std::uint8_t id{102U};
    std::vector<std::byte> png_data;
};

/**
 * Tutorial help uses one exceptional big-endian float followed by string ids.
 */
struct HelpMessagePacket final {
    static constexpr std::uint8_t id{109U};
    float delay{};
    std::vector<std::string> message_ids;
};

/** Complete editor palette; every record is red, green, blue, then Z threshold. */
struct SetGroundColorsPacket final {
    static constexpr std::uint8_t id{118U};
    std::vector<std::array<std::uint8_t, 4U>> colors;
};

struct DisplayCountdownPacket final {
    static constexpr std::uint8_t id{84U};
    float timer{};
};

/** Live team lock used by ChangeTeam/SelectTeam. Team zero is spectator. */
struct LockTeamPacket final {
    static constexpr std::uint8_t id{79U};
    std::uint8_t team_id{};
    bool locked{};
};

struct TeamLockClassPacket final {
    static constexpr std::uint8_t id{80U};
    std::uint8_t team_id{};
    bool locked{};
};

struct TeamLockScorePacket final {
    static constexpr std::uint8_t id{81U};
    std::uint8_t team_id{};
    bool locked{};
};

struct TeamInfiniteBlocksPacket final {
    static constexpr std::uint8_t id{82U};
    std::uint8_t team_id{};
    bool infinite{};
};

/** Dynamically exposes one team's players to the opposing minimap. */
struct TeamMapVisibilityPacket final {
    static constexpr std::uint8_t id{83U};
    std::uint8_t team_id{};
    bool visible{};
};

/** Incremental HUD score update: 0=team, 1=player. */
struct SetScorePacket final {
    static constexpr std::uint8_t id{85U};
    std::uint8_t type{};
    std::uint8_t reason{};
    std::uint8_t specifier{};
    std::int32_t value{};
};

struct KillActionPacket final {
    static constexpr std::uint8_t id{46U};
    std::uint8_t player_id{};
    std::uint8_t killer_id{};
    std::uint8_t kill_type{};
    std::uint8_t respawn_time{};
    std::uint8_t kill_count{};
    bool domination{};
    bool revenge{};
};

/** One candidate retained by GenericVotingHUD. Names are opaque wire tokens. */
struct GenericVoteCandidate final {
    std::string name;
    std::int32_t votes{};
};

/**
 * Bidirectional retail generic-vote packet.
 *
 * The stock HUD only has three vote keys. Keeping that bound in the decoder is
 * important: malformed candidate arrays used to index past the native HUD's
 * fixed tuple and crash the client.
 */
struct GenericVoteMessagePacket final {
    static constexpr std::uint8_t id{47U};
    enum MessageType : std::uint8_t {
        start = 0U,
        cast = 1U,
        update = 2U,
        closed = 3U,
    };

    std::uint8_t player_id{};
    std::uint8_t message_type{};
    std::vector<GenericVoteCandidate> candidates;
    std::string title;
    std::string description;
    bool allow_revote{};
    bool hide_after_vote{};
    bool can_vote{};
};

/** Free-form server/chat message; chat_type 3 is retail's top-screen lane. */
struct ChatMessagePacket final {
    static constexpr std::uint8_t id{49U};
    std::uint8_t player_id{};
    std::uint8_t chat_type{};
    std::string value;
};

/** Localized server message with bounded positional string parameters. */
struct LocalisedMessagePacket final {
    static constexpr std::uint8_t id{50U};
    std::uint8_t chat_type{};
    bool localise_parameters{};
    std::string string_id;
    std::vector<std::string> parameters;
    bool override_previous_message{};
};

/** Live UGC host sky selection; the same packet also appears during bootstrap. */
struct SkyboxDataPacket final {
    static constexpr std::uint8_t id{51U};
    std::string definition_name;
};

/** The end-of-map statistics for one team, shown before the map transition. */
struct GameStatEntry final {
    std::int32_t player_id{};
    std::int32_t stat_type{};
};

struct GameStatsPacket final {
    static constexpr std::uint8_t id{67U};
    /** Retail uses 2/3 to select the blue/green award list, not the winner. */
    std::int32_t team_id{};
    std::vector<GameStatEntry> entries;
};

/** Activates ViewGameStats after the server has sent both team records. */
struct ShowGameStatsPacket final {
    static constexpr std::uint8_t id{53U};
};

/** Server-owned hold/release state for retail's full ViewScores overlay. */
struct ForceShowScoresPacket final {
    static constexpr std::uint8_t id{72U};
    bool forced{};
};

/**
 * Selects one of retail's nine authored end-of-round messages.
 *
 * The fixed-point duration is present on the wire, but the shipped
 * GameScene.show_text_message implementation does not consume it. Keep the
 * value for protocol diagnostics without imposing a native-only timeout.
 */
struct ShowTextMessagePacket final {
    static constexpr std::uint8_t id{73U};
    std::uint8_t message_id{};
    float duration{};
};

/** One persistent statistic delta displayed on retail's end-match rank bar. */
struct RankUpEntry final {
    std::int32_t score_reason{};
    std::int64_t old_score{};
    std::int64_t new_score{};
};

struct RankUpsPacket final {
    static constexpr std::uint8_t id{66U};
    std::vector<RankUpEntry> entries;
};

/** Loader boundary sent after the results/vote dwell has completed. */
struct MapEndedPacket final {
    static constexpr std::uint8_t id{52U};
};

/** Per-team objective progress selected by the server's mode implementation. */
struct TeamProgressPacket final {
    static constexpr std::uint8_t id{117U};
    std::uint8_t team_id{};
    bool visible{};
    bool show_particle{};
    bool show_previous{};
    bool show_as_percent{};
    float percent{};
    std::int32_t numerator{};
    std::int32_t denominator{};
    std::uint8_t icon_id{};
};

/** One retained base-state mutation for the retail Territory Control HUD. */
struct TerritoryBaseStatePacket final {
    static constexpr std::uint8_t id{106U};
    std::uint8_t base_index{};
    std::uint8_t action{};
    std::uint8_t controlled_by{};
    std::uint8_t attacked_by{};
    float capture_amount{};
};

/**
 * Server camera focus (Demolition airstrike). GameScene.process_packet_poi_focus
 * reads only the three fixed16 coordinates and starts a LookAtController on
 * that point; there is no timeout and no release packet (BS docs/PROTOCOL.md
 * row 18, live 2026-09-26).
 */
struct PoiFocusPacket final {
    static constexpr std::uint8_t id{18U};
    std::array<float, 3U> target{};
};

using RuntimePacket = std::variant<ClockSyncPacket,
                                   SetUgcEditModePacket,
                                   SetHpPacket,
                                   ChangeEntityPacket,
                                   ChangePlayerPacket,
                                   HitEntityPacket,
                                   CreateEntityPacket,
                                   DestroyEntityPacket,
                                   PrefabCompletePacket,
                                   ExplodeCorpsePacket,
                                   MinimapBillboardPacket,
                                   MinimapBillboardClearPacket,
                                   MinimapZonePacket,
                                   MinimapZoneClearPacket,
                                   LockToZonePacket,
                                   CreateAmbientSoundPacket,
                                   PlaySoundPacket,
                                   PlayAmbientSoundPacket,
                                   StopSoundPacket,
                                   PlayMusicPacket,
                                   StopMusicPacket,
                                   PlayerLeftPacket,
                                   UgcObjectivesPacket,
                                   PickPickupPacket,
                                   FogColorPacket,
                                   InitialUgcBatchPacket,
                                   UgcMapInfoPacket,
                                   UgcMessagePacket,
                                   UgcMapLoadingFromHostPacket,
                                   HelpMessagePacket,
                                   SetGroundColorsPacket,
                                   DisplayCountdownPacket,
                                   LockTeamPacket,
                                   TeamLockClassPacket,
                                   TeamLockScorePacket,
                                   TeamInfiniteBlocksPacket,
                                   TeamMapVisibilityPacket,
                                   SetScorePacket,
                                   KillActionPacket,
                                   GenericVoteMessagePacket,
                                   ChatMessagePacket,
                                   LocalisedMessagePacket,
                                   SkyboxDataPacket,
                                   GameStatsPacket,
                                   ShowGameStatsPacket,
                                   ForceShowScoresPacket,
                                   ShowTextMessagePacket,
                                   RankUpsPacket,
                                   MapEndedPacket,
                                   TeamProgressPacket,
                                   TerritoryBaseStatePacket,
                                   PoiFocusPacket>;

struct RuntimeDecodeResult final {
    std::optional<RuntimePacket> packet;
    std::string error;

    [[nodiscard]] explicit operator bool() const noexcept {
        return packet.has_value();
    }
};

/** Strictly decode one supported server-to-client runtime packet. */
[[nodiscard]] RuntimeDecodeResult decode_runtime_packet(std::span<const std::byte> payload);

/** Encode one clock request/response without changing its echoed token. */
[[nodiscard]] std::vector<std::byte> encode_packet(const ClockSyncPacket& packet);

/** Encode one bounded client chat submission (global/team/system/big lane). */
[[nodiscard]] std::vector<std::byte> encode_packet(const ChatMessagePacket& packet);

/** Encode a safe stock sky definition selected by the UGC host. */
[[nodiscard]] std::vector<std::byte> encode_packet(const SkyboxDataPacket& packet);

/** Encode the UGC host's bounded PNG map preview (packet 102); empty if not a PNG. */
[[nodiscard]] std::vector<std::byte> encode_packet(const UgcMapInfoPacket& packet);

/** Encode the complete UGC RGB/Z-threshold terrain and water palette. */
[[nodiscard]] std::vector<std::byte> encode_packet(const SetGroundColorsPacket& packet);

/** Encode the one-candidate CAST form accepted by the retail vote manager. */
[[nodiscard]] std::vector<std::byte> encode_generic_vote_cast(std::uint8_t player_id,
                                                              std::string_view candidate);

} // namespace battlespades::network
