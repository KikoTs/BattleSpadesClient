#pragma once

#include "battlespades/frontend/hud_layout.hpp"
#include "battlespades/ui/design_canvas.hpp"
#include "battlespades/ui/draw_list.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace battlespades::frontend {

namespace game_hud_assets {

/** Five-image retail crosshair; all 16x16, anchored at their centers. */
inline constexpr std::string_view crosshair_centre{"png/ui/target_centre.png"};
inline constexpr std::string_view crosshair_top_left{"png/ui/target_top_left.png"};
inline constexpr std::string_view crosshair_top_right{"png/ui/target_top_right.png"};
inline constexpr std::string_view crosshair_bottom_left{"png/ui/target_bottom_left.png"};
inline constexpr std::string_view crosshair_bottom_right{"png/ui/target_bottom_right.png"};
inline constexpr std::string_view health_bar{"png/ui/health_bar/health_bar.png"};
inline constexpr std::string_view health_bar_frame{"png/ui/health_bar/health_bar_frame.png"};
/** Runtime-generated 1x1 white texture used for solid tinted quads. */
// The native texture cache intentionally accepts shipped PNG assets only.
// CTF/minimap panels reach this sprite during the first live frame, so a
// synthetic runtime id turns a valid mode packet into a fatal UI load error.
inline constexpr std::string_view white_pixel{"png/high/white.png"};
inline constexpr std::string_view help_font{"fonts/Spades.ttf"};
inline constexpr std::string_view weapon_frame{"png/ui/weapon_select/weapon_frame.png"};
inline constexpr std::string_view weapon_frame_selected{
    "png/ui/weapon_select/weapon_frame_selected.png"};
/** images.py blueprint_background: the schematic under prefab/UGC strip entries. */
inline constexpr std::string_view prefab_blueprint{
    "png/ui/in_game_menus/prefab_selection/blueprint.png"};
inline constexpr std::string_view ammo_frame{"png/ui/ammo/ammo_frame.png"};
inline constexpr std::string_view score_frame{"png/ui/score/score_frame.png"};
/** The blocks row's icon: TOOL_IMAGES[BLOCK_TOOL], the 330px block portrait. */
inline constexpr std::string_view block_icon{"png/ui/weapons/block.png"};
inline constexpr std::string_view head_count_frame{
    "png/ui/head_count/hc_frame_long.png"};
inline constexpr std::string_view head_count_team1_face{
    "png/ui/icons/deuce_head_2.png"};
inline constexpr std::string_view head_count_team1_tint{
    "png/ui/icons/deuce_head_colour_2.png"};
inline constexpr std::string_view head_count_team2_face{
    "png/ui/icons/deuce_head_1.png"};
inline constexpr std::string_view head_count_team2_tint{
    "png/ui/icons/deuce_head_colour_1.png"};
inline constexpr std::string_view timer_frame{"png/ui/timer/timer_frame.png"};
inline constexpr std::string_view timer_icon{"png/ui/timer/timer.png"};
inline constexpr std::string_view jetpack_fuel_bar{
    "png/ui/jetpack_fuel/jetpack_fuel_bar.png"};
inline constexpr std::string_view jetpack_fuel_frame{
    "png/ui/jetpack_fuel/jetpack_fuel_frame.png"};
/** Every shipping retail jetpack profile uses 100 resource units. */
inline constexpr double retail_jetpack_max_fuel{100.0};
inline constexpr std::string_view disguise_status{"png/ui/weapons/disguise.png"};
inline constexpr std::string_view parachute_status{"png/ui/weapons/parachute.png"};
inline constexpr std::string_view kill_feed_font{"fonts/A750-Sans-Medium.ttf"};
inline constexpr std::string_view big_text_font{"fonts/Edo.ttf"};
inline constexpr std::string_view big_text_frame{
    "png/ui/in_game_menus/big_text_frame.png"};
inline constexpr std::string_view damage_indicator{"png/ui/indicator.png"};
/** Minimap.check_player_inside_zone_and_tint_screen's full-window mask. */
inline constexpr std::string_view inside_zone_tint{
    "png/high/alpha_block_inside_zone.png"};
inline constexpr std::string_view minimap_texture{"runtime/minimap"};
inline constexpr std::string_view minimap_frame{
    "png/ui/mini_map/minimap_frame.png"};
inline constexpr std::string_view full_map_frame{"png/ui/map/map_frame.png"};
inline constexpr std::string_view minimap_player{"png/ui/map_player_16.png"};
inline constexpr std::string_view minimap_view_cone{"png/ui/map_view_cone.png"};
inline constexpr std::string_view minimap_vip_player{
    "png/ui/map_vip_player_16.png"};
/** Zombie viewer's pulsing survivor marker from Player.get_map_icon. */
inline constexpr std::string_view minimap_zombie_heart{
    "png/ui/heart_icon_256x256.png"};
inline constexpr std::string_view minimap_base{"png/ui/map_base_16.png"};
inline constexpr std::string_view minimap_grave{"png/ui/map_grave_16.png"};
inline constexpr std::string_view minimap_turret{
    "png/ui/marker_turret_16.png"};
inline constexpr std::string_view minimap_landmine{
    "png/ui/marker_land_mine_16.png"};
inline constexpr std::string_view minimap_dynamite{
    "png/ui/marker_dynamite_16.png"};
inline constexpr std::string_view minimap_medpack{
    "png/ui/marker_medpack_16.png"};
inline constexpr std::string_view minimap_radar{
    "png/ui/marker_radar_station_16.png"};
inline constexpr std::string_view minimap_c4{"png/ui/marker_c4_16.png"};
inline constexpr std::string_view minimap_bomb{"png/ui/minimap_bomb.png"};
inline constexpr std::string_view minimap_diamond{
    "png/ui/minimap_diamond.png"};
inline constexpr std::string_view minimap_intel{"png/ui/minimap_intel.png"};
inline constexpr std::string_view minimap_height{
    "png/ui/minimap_height_indicator.png"};
inline constexpr std::string_view minimap_zone_base{
    "png/ui/minimap_base.png"};
inline constexpr std::string_view minimap_zone_multihill{
    "png/ui/minimap_multihill.png"};
inline constexpr std::string_view minimap_zone_occupation{
    "png/ui/minimap_occupation_target.png"};
inline constexpr std::string_view minimap_zone_diamond{
    "png/ui/minimap_diamond_dropoff.png"};
inline constexpr std::string_view minimap_zone_vip{
    "png/ui/vip_icon_256x256.png"};
inline constexpr std::string_view minimap_zone_spawn{
    "png/ui/minimap_spawn.png"};
inline constexpr std::string_view minimap_zone_territory_a{
    "png/ui/tc_minimap_a.png"};
inline constexpr std::string_view minimap_zone_territory_b{
    "png/ui/tc_minimap_b.png"};
inline constexpr std::string_view minimap_zone_territory_c{
    "png/ui/tc_minimap_c.png"};
inline constexpr std::string_view minimap_zone_territory_d{
    "png/ui/tc_minimap_d.png"};
inline constexpr std::string_view minimap_zone_territory_e{
    "png/ui/tc_minimap_e.png"};
inline constexpr std::string_view minimap_zone_territory_f{
    "png/ui/tc_minimap_f.png"};
inline constexpr std::string_view minimap_zone_territory_g{
    "png/ui/tc_minimap_g.png"};
/** TeamProgressBar icon id 0. */
inline constexpr std::string_view team_progress_base{
    "png/ui/minimap_base.png"};
/** TeamProgressBar icon id 1. */
inline constexpr std::string_view team_progress_diamond{
    "png/ui/minimap_diamond.png"};
/** HudParticleManager image used by TeamProgressBar damage bursts. */
inline constexpr std::string_view team_progress_particle{
    "png/high/block128.png"};
inline constexpr std::string_view territory_backplate{
    "png/ui/modes/tc_backplate.png"};
inline constexpr std::string_view territory_frame{
    "png/ui/modes/tc_frame.png"};
inline constexpr std::array<std::string_view, 10U> territory_letters{
    "png/ui/modes/tc_text_a.png",
    "png/ui/modes/tc_text_b.png",
    "png/ui/modes/tc_text_c.png",
    "png/ui/modes/tc_text_d.png",
    "png/ui/modes/tc_text_e.png",
    "png/ui/modes/tc_text_f.png",
    "png/ui/modes/tc_text_g.png",
    "png/ui/modes/tc_text_g.png",
    "png/ui/modes/tc_text_g.png",
    "png/ui/modes/tc_text_g.png",
};
inline constexpr std::string_view intel_blue{
    "png/ui/icons/intel_blue_90.png"};
inline constexpr std::string_view intel_green{
    "png/ui/icons/intel_green_90.png"};

} // namespace game_hud_assets

/**
 * Convert Character.jetpack_fuel to the ratio consumed by draw_jetpack_hud.
 *
 * The protocol carries fixed-point resource units (0..100), not a normalized
 * float. Retail divides by the active jetpack's JETPACK_MAX_FUEL first.
 */
[[nodiscard]] double retail_jetpack_fuel_fraction(double fuel) noexcept;

/** One team's readout in the top-centre HeadCount bar. */
struct GameHudTeamScore final {
    hud_layout::Team team{hud_layout::Team::team1};
    std::int32_t score{};
    std::int32_t max_score{};
    /** Retail appends '/%s' % max_score only when the mode sets show_max_score. */
    bool show_max_score{};
    /** StateData may override the stock blue/green team color. */
    std::optional<ui::ColorRgba8> color{};
};

/** Retail StateData selector used by the top-centre HeadCount widget. */
enum class GameHudHeadCountType : std::uint8_t {
    player_count = 0U,
    score = 1U,
    four_digit_score = 2U,
    inactive = 3U,
};

struct GameHudHeadCountValue final {
    std::int32_t value{};
    bool visible{};
};

/**
 * Reproduce the stock client's two equality checks without trusting an enum
 * cast: only zero selects roster size and only three disables the widget.
 * Unknown server values remain visible and display score.
 */
[[nodiscard]] GameHudHeadCountValue resolve_game_hud_head_count(
    std::uint8_t raw_type, std::int32_t score,
    std::int32_t player_count) noexcept;

/** The two icon ordinals accepted by retail TeamProgressBar.icons. */
enum class GameHudTeamProgressIcon : std::uint8_t {
    base = 0U,
    diamond = 1U,
};

/** One retained team row inside the retail TeamProgressBar widget. */
struct GameHudTeamProgressEntry final {
    hud_layout::Team team{hud_layout::Team::neutral};
    bool visible{};
    bool show_previous{};
    double current{100.0};
    double previous_damage{100.0};
    GameHudTeamProgressIcon icon{GameHudTeamProgressIcon::base};
    std::optional<ui::ColorRgba8> color{};
};

/**
 * Raw packet-117 update after its wire team and icon ordinals are validated.
 *
 * `value` is packet.percent in percent mode and packet.numerator otherwise.
 * `maximum` is ignored in percent mode, matching the retail Python class.
 */
struct GameHudTeamProgressUpdate final {
    hud_layout::Team team{hud_layout::Team::neutral};
    bool visible{};
    bool show_particle{};
    bool show_previous{};
    bool show_as_percent{true};
    double value{};
    double maximum{100.0};
    /** Unknown wire icon ids arrive as nullopt and retain the prior icon. */
    std::optional<GameHudTeamProgressIcon> icon{};
};

/** Shared state: retail stores maximum/show mode once, not once per team. */
struct GameHudTeamProgressState final {
    std::array<GameHudTeamProgressEntry, 3U> entries{
        GameHudTeamProgressEntry{hud_layout::Team::team1},
        GameHudTeamProgressEntry{hud_layout::Team::team2},
        GameHudTeamProgressEntry{hud_layout::Team::neutral}};
    double maximum{100.0};
    bool show_as_percent{true};
};

/**
 * One live HudParticle spawned by TeamProgressBar.
 *
 * Position remains relative to the widget's centre/baseline anchor. Retail's
 * HudParticle stores bottom-left coordinates, advances them at 200 px/s, grows
 * the 128 px block image by 0.8 scale/s, and fades it by 2 alpha/s.
 */
struct GameHudTeamProgressParticle final {
    double offset_x{};
    double offset_y{};
    double direction_x{};
    double direction_y{-1.0};
    double remaining_seconds{0.5};
    double speed_pixels_per_second{200.0};
    double scale{0.5};
    double scale_rate_per_second{0.8};
    double alpha{1.0};
    double alpha_rate_per_second{-2.0};
    ui::ColorRgba8 color{};
};

/** Converts only retail's two legal TeamProgress icon ids. */
[[nodiscard]] std::optional<GameHudTeamProgressIcon>
team_progress_icon_from_wire(std::uint8_t icon_id) noexcept;

/** Retail sub-actions carried by packet 106. */
enum class GameHudTerritoryBaseAction : std::uint8_t {
    initial_info = 0U,
    activate = 1U,
    deactivate = 2U,
    entering = 3U,
    leaving = 4U,
    capture_update = 5U,
    contended = 6U,
    uncontended = 7U,
};

/** One normalized packet-106 mutation after wire team validation. */
struct GameHudTerritoryBaseUpdate final {
    std::uint8_t base_index{};
    GameHudTerritoryBaseAction action{
        GameHudTerritoryBaseAction::initial_info};
    hud_layout::Team controlled_by{hud_layout::Team::neutral};
    hud_layout::Team attacked_by{hud_layout::Team::neutral};
    double capture_amount{};
};

/** Retained state owned by one retail TerritoryBaseInfo instance. */
struct GameHudTerritoryBaseState final {
    std::uint8_t base_index{};
    hud_layout::Team controlled_by{hud_layout::Team::neutral};
    hud_layout::Team attacked_by{hud_layout::Team::neutral};
    double capture_amount{};
    bool contains_player{};
    bool visible{true};
    bool contended{};
    double contend_time{};
    double contend_alpha{};
};

/** Ten authored slots A-J; H/I/J intentionally reuse the G letter sprite. */
struct GameHudTerritoryBasesState final {
    std::array<std::optional<GameHudTerritoryBaseState>, 10U> bases{};
};

/** Bottom-right block counter; shares draw_ammo_hud with the weapon panel. */
struct GameHudBlockState final {
    std::string icon_asset;
    std::int32_t count{};
    std::int32_t reserve{};
    bool visible{};
};

/** One-shot audio cues drained by the frontend each tick. */
struct GameHudSounds final {
    bool play_disappear{};
    bool play_appear{};
    /** Retail Character.update_respawn_time final ("1") countdown tone. */
    bool play_respawn_beep1{};
    /** Retail Character.update_respawn_time earlier ("3", "2") tone. */
    bool play_respawn_beep2{};
};

/** Renderer-facing metadata for one entry in the retail tool strip. */
struct GameHudInventorySlot final {
    std::string icon_asset;
    std::string hotkey_label;
    /**
     * draw_tool_loadout_hud passes draw_background=True, max_scale=0.8 for
     * prefab and UGC entries: a blueprint plate replaces the green frame.
     */
    bool blueprint_background{};
    /**
     * Retail texture width of icon_asset after its load scale: TOOL_IMAGES
     * load at 1.0 (330), prefab palette / UGC tool images at 0.64 (211).
     */
    double icon_texture_pixels{330.0};
};

/** Current equipped-tool ammunition rendered by the retail lower-right panel. */
struct GameHudAmmoState final {
    std::string image_asset;
    std::int32_t current{};
    std::optional<std::int32_t> reserve{};
    /** Retail draw_ammo_hud image_scale (normal tools 0.1, prefabs 0.25). */
    double image_scale{0.1};
    /** Optional glColor tint used by the prefab preview and block portrait. */
    std::optional<ui::ColorRgba8> image_color;
    /** Cost/ammo threshold selected before rendering the numeric value. */
    bool enough{};
    bool visible{};
};

/** Bottom-right server-enabled block colour grid and its current selector. */
struct GameHudPaletteState final {
    std::vector<ui::ColorRgba8> colors;
    std::size_t columns{8U};
    std::optional<std::size_t> selected;
    bool visible{};
    /** UGC editor uses the recovered 30px insets instead of 170/5. */
    bool ugc_layout{};
};

/** Stable retail-compatible swatches shared by input and HUD presentation. */
[[nodiscard]] std::span<const ui::ColorRgba8> retail_block_palette() noexcept;

/** One retail icon-based kill row, newest entries stored first. */
struct GameHudKillEntry final {
    std::optional<std::string> killer_name;
    ui::ColorRgba8 killer_color{};
    std::string icon_asset;
    /** Authored PNG width; KillLine draws it at exactly 0.1 scale. */
    double icon_source_pixels{330.0};
    std::string victim_name;
    ui::ColorRgba8 victim_color{};
    double ttl_seconds{5.0};
};

/**
 * One source-derived `ScoreLine` row beside the crosshair.
 *
 * Retail keeps the numeric title and each localized reason as separate rows.
 * `pending_delta` belongs only to delayed reason rows: HUD.update transfers it
 * into the numeric title when that row becomes drawable, then clears it.
 */
struct GameHudScoreLineState final {
    std::string text;
    std::int32_t pending_delta{};
    double ttl_seconds{1.5};
    double alive_seconds{};
    double show_after_seconds{};
    bool title{};
    /** SCORE_REASON_CODES identifier; drawn through the retail strings. */
    std::string localization_key{};

    [[nodiscard]] bool can_draw() const noexcept {
        return ttl_seconds > 0.0 && alive_seconds >= show_after_seconds;
    }
};

/**
 * Retail's short-lived score-reason stack.
 *
 * `HUD.add_score_reason` creates a title plus the first reason, then appends up
 * to three delayed reasons. `HUD.update` folds a reason's score into the title
 * only when its `ScoreLine.can_draw()` delay expires. A fifth retained row or
 * an almost-faded title starts a fresh stack instead of extending stale UI.
 */
struct GameHudScoreAwardState final {
    std::int32_t displayed_delta{};
    std::vector<GameHudScoreLineState> lines;
};

/** Player-facing English label for a retail SCORE_REASON ordinal. */
[[nodiscard]] std::string_view retail_score_reason_label(
    std::uint8_t reason) noexcept;

struct GameHudPendingBigMessage final {
    std::string text;
    double duration_seconds{};
};

/** Retail CHAT_BIG lane and its bounded parallel text/time lists. */
struct GameHudBigMessage final {
    std::string text;
    double remaining_seconds{};
    double age_seconds{};
    /** big_text_duration of the line on screen. */
    double duration_seconds{};
    /** min(duration, BIG_TEXT_MIN_DURATION=1.5): dwell once a line is queued. */
    double min_duration_seconds{};
    /**
     * hud.pyd caps bigMsgList_text/time at six deferred rows. Retail takes
     * rows with list.pop(), so back() is the next line to show.
     */
    std::vector<GameHudPendingBigMessage> pending;
};

/** Centre-screen timed/no-respawn state driven by KillAction.respawn_time. */
struct GameHudRespawnState final {
    double remaining_seconds{};
    bool visible{};
    bool never_respawn{};
};

/** One retail directional-damage flash attached to the local character. */
struct GameHudDamageIndicator final {
    /** Pyglet-compatible clockwise degrees relative to the current view. */
    double rotation_degrees{};
    double remaining_seconds{1.2};
};

/** One world-space icon rendered over either retail map view. */
struct GameHudMinimapMarker final {
    std::string icon_asset;
    double world_x{};
    double world_y{};
    double size_pixels{16.0};
    double rotation_degrees{};
    ui::ColorRgba8 color{255U, 255U, 255U, 255U};
    /** Privileged objectives/carriers pin to the crop edge; ordinary players cull. */
    bool clamp_out_of_bounds{};
};

/** Retail entity-class to minimap-art binding recovered from gameScene.pyd. */
struct GameHudMinimapEntityStyle final {
    std::string_view icon_asset;
    /** RocketTurret.draw_on_minimap is the one proven friendly-only override. */
    bool friendly_only{};
    /** Only capture points and ground intel may receive the 8px height arrow. */
    bool supports_height_indicator{};
};

[[nodiscard]] std::optional<GameHudMinimapEntityStyle>
minimap_entity_style(std::uint8_t entity_type) noexcept;

/** Retail MINIMAP_ZONE_ICON ordinal to its authored texture dimensions. */
struct GameHudMinimapZoneIconStyle final {
    std::string_view icon_asset;
    double authored_size_pixels{};
};

[[nodiscard]] std::optional<GameHudMinimapZoneIconStyle>
minimap_zone_icon_style(std::uint8_t icon_id) noexcept;

/** One packet-43 objective area, rendered beneath players and entities. */
struct GameHudMinimapZone final {
    double world_x1{};
    double world_y1{};
    double world_x2{};
    double world_y2{};
    ui::ColorRgba8 color{255U, 255U, 255U, 255U};
    std::string icon_asset;
    double icon_source_pixels{};
    double icon_scale{};
};

/** Renderer-neutral state for the 512x512 north-up retail minimap. */
struct GameHudMinimapState final {
    std::string texture_asset{std::string{game_hud_assets::minimap_texture}};
    double focus_x{};
    double focus_y{};
    bool visible{};
    bool full_map_visible{};
    /** Retail advances this shared pulse at six radians per second. */
    double zone_phase_radians{};
    std::vector<GameHudMinimapZone> zones;
    std::optional<GameHudMinimapMarker> view_cone;
    std::vector<GameHudMinimapMarker> markers;
};

/**
 * Convert a horizontal Protocol 168 orientation into pyglet-compatible
 * clockwise minimap degrees.
 *
 * The authored player/cone art points toward the top of the north-up map.
 * World +X is screen-right and world +Y is screen-down, so this conversion
 * deliberately does not reuse the camera yaw convention. Invalid or
 * vertical-only orientations fail to an upright marker.
 */
[[nodiscard]] double minimap_marker_rotation_degrees(
    double orientation_x, double orientation_y) noexcept;

/**
 * Visible size of retail's Zombie survivor-heart pulse, or zero while off.
 *
 * Player.update_heartbeat waits 1.0 s, expands 0.05 -> 0.07 in 0.1 s,
 * contracts back over 1.0 s, then repeats. Player.get_map_icon adds the
 * separate 0.05 base scale to the live size before scaling the 256px art.
 */
[[nodiscard]] double zombie_heartbeat_marker_size(
    double elapsed_seconds) noexcept;

/** Inputs of retail Player.display_map_icon_out_of_bounds(viewer). */
struct PlayerMarkerEdgePinInputs {
    bool high_minimap_visibility{};
    bool carries_pickup{};
    bool is_viewer{};
    bool viewer_is_zombie{};
    bool exposed_teams_always_on_minimap{};
    /** `player.team.other.can_see_other_team` (the team opposite the player's). */
    bool opposite_team_can_see_other{};
};

/**
 * Whether a visible player's minimap marker is pinned to the minimap edge
 * instead of culled once it leaves the 128 px window.
 *
 * player.pyd 0x100153E0 (player.pyx:529-537): true for a high-visibility
 * player, a pickup carrier, any other player seen by a Zombie-class viewer
 * (`self is not viewer and viewer.current_class.id == CLASS_ZOMBIE`), or
 * when exposed_teams_always_on_minimap is set and the player's opposite
 * team can see the other team.
 */
[[nodiscard]] bool player_marker_pins_to_minimap_edge(
    const PlayerMarkerEdgePinInputs& inputs) noexcept;

/**
 * Retail's exact common.vector_angle_2d conversion:
 * degrees(atan2(source_delta_y, source_delta_x) - atan2(view_y, view_x)).
 */
[[nodiscard]] double damage_indicator_angle_degrees(
    double source_delta_x, double source_delta_y,
    double view_x, double view_y) noexcept;

/**
 * Renderer-neutral in-game HUD state for the playable world.
 *
 * The help panel reproduces the recovered retail HelpPanel behavior: setting
 * text plays the swap-out cue immediately, the appear cue fires when the
 * 0.35 s delay expires, and the panel slides from the top with the retail
 * sine easing at 8%% per 60 Hz frame. Health is display state only: offline
 * Tutorial pins its pool at 100, while live matches retain server HP and use
 * the acknowledged class durability profile for the numeric readout.
 */
class GameHudModel final {
public:
    static constexpr double transition_speed_percent{8.0};
    static constexpr double fixed_dt{1.0 / 60.0};

    void set_help_messages(std::vector<std::string> lines, std::string close_hint,
                           double delay_seconds);
    void toggle_help() noexcept;
    /** HelpPanel.force_open / force_close (ToolsHelpPanel temp_close). */
    void set_help_open(bool open) noexcept;
    void toggle_hud() noexcept;
    /** Preserve the authoritative raw HP value; the wire is not capped at 100. */
    void set_health(std::int32_t health) noexcept;
    /** InitialInfo gate for the numeric label; the bar itself always survives. */
    void set_numeric_health_visible(bool visible) noexcept;
    /**
     * Set the active server-selected class durability profile.
     * Retail displays ceil(raw HP / damage multiplier).
     */
    void set_health_damage_multiplier(double multiplier) noexcept;
    /** Follows the equipped tool's recovered visibility enum; no tool = none. */
    void set_crosshair_visible(bool visible) noexcept;
    /**
     * Positions retail's four 16x16 corner sprites around the fixed centre.
     * `radius_pixels` is the equipped weapon's live accuracy result.
     */
    void set_crosshair_geometry(double radius_pixels,
                                bool draw_center = true) noexcept;
    /**
     * Starts retail's 0.25-second server-confirmed hit tint.
     *
     * This must be called only for ShootResponse(9), never from a local
     * raycast, because the server can reject protected or obstructed hits.
     */
    void confirm_hit() noexcept;
    /** Copies the renderer-neutral inventory presentation for this frame. */
    void set_inventory_state(std::vector<GameHudInventorySlot> slots,
                             std::optional<std::size_t> selected, bool visible) noexcept;
    /** Updates the original framed ammunition/tool-count widget. */
    void set_ammo_state(std::string image_asset, std::int32_t current,
                        std::optional<std::int32_t> reserve, bool visible) noexcept;
    /** Block/drag-line cost above stock/capacity; red when placement is invalid. */
    void set_block_cost_state(std::int32_t cost, bool can_place,
                              ui::ColorRgba8 tint, bool visible) noexcept;
    /**
     * Flare Block row: the tool's own image over FLAREBLOCK_COST. HUD draw
     * (hud.pyd 0x100936E0) takes this branch for tool_id == FLAREBLOCK_TOOL
     * and, unlike the block row, never asks is_placement_valid.
     */
    void set_flare_cost_state(std::string image_asset, std::int32_t cost,
                              ui::ColorRgba8 tint, bool visible) noexcept;
    /**
     * Replaces the weapon-ammo row with retail's selected-prefab preview.
     *
     * `cost` is the KV6 voxel count and `affordable` compares it with the
     * player's current block stock. The independent blocks row remains
     * visible below this row, matching HUD.draw_tools_hud.
     */
    void set_prefab_cost_state(std::string preview_asset, std::int32_t cost,
                               bool affordable, ui::ColorRgba8 tint,
                               bool visible) noexcept;
    void set_palette_state(std::vector<ui::ColorRgba8> colors,
                           std::size_t columns,
                           std::optional<std::size_t> selected,
                           bool visible, bool ugc_layout = false) noexcept;
    /** Updates the original personal-score widget anchored at the top-left. */
    void set_player_score(std::int32_t score, bool visible) noexcept;
    /** Owning team; drives the health-bar tint per draw_healthbar hud.pyx:1027. */
    void set_team(hud_layout::Team team) noexcept;
    /** Exact StateData team color; call after set_team for custom servers. */
    void set_team_color(ui::ColorRgba8 color) noexcept;
    /**
     * The per-class head drawn beside the health bar. Retail selects it as
     * class_icons[class.id][team.id] (images.py:485), which our class catalog
     * already carries as ClassDefinition::team_icon_assets.
     */
    void set_class_portrait(std::string icon_asset, bool high_minimap_visibility);
    /** Bottom-right block counter, drawn directly beneath the ammo panel. */
    void set_block_state(std::string icon_asset, std::int32_t count,
                         std::int32_t reserve, bool visible);
    /** Jetpack fuel in [0,1]; hidden entirely when the class has no jetpack. */
    void set_jetpack_fuel(double fraction, bool visible) noexcept;
    /** WorldUpdate state bit 0x02, drawn with TOOL_IMAGES[DISGUISE_TOOL]. */
    void set_disguise_active(bool active) noexcept;
    /** WorldUpdate state bit 0x01, drawn with TOOL_IMAGES[PARACHUTE_TOOL]. */
    void set_parachute_active(bool active) noexcept;
    void set_ability_hint(std::string text) { ability_hint_ = std::move(text); }
    /**
     * Dead-screen hints: DEATH_CLASS_CHANGE_HINT (already resolved, with the
     * change-class key) while a respawn is pending, and
     * VIP_DEAD_CAM_INSTRUCTION in the VIP dead camera.
     */
    void set_death_hints(std::string class_change_hint, bool vip_dead_camera) {
        death_class_hint_ = std::move(class_change_hint);
        vip_dead_camera_ = vip_dead_camera;
    }
    /** Top-centre HeadCount readouts and the countdown between them. */
    void set_team_scores(GameHudTeamScore left, GameHudTeamScore right,
                         bool visible) noexcept;
    /**
     * Applies one validated packet-117 update with the retail widget's shared
     * denominator and inverted `maximum - value` semantics.
     */
    void update_team_progress(const GameHudTeamProgressUpdate& update) noexcept;
    /** Clears all mode-owned rows at a map/session boundary. */
    void reset_team_progress() noexcept;
    /** Applies one validated packet-106 mutation to retained retail state. */
    void update_territory_base(
        const GameHudTerritoryBaseUpdate& update) noexcept;
    /** Clears all A-J base slots at a map/session boundary. */
    void reset_territory_bases() noexcept;
    void set_match_clock(
        double remaining_seconds, bool visible,
        hud_layout::TimerPlacement placement =
            hud_layout::TimerPlacement::head_count,
        bool minimap_enabled = false) noexcept;
    /**
     * Inserts one kill at index zero and caps the feed at five, matching
     * HUD.add_kill_feed. A suicide has no killer_name and starts with the icon.
     */
    void add_kill(std::optional<std::string> killer_name,
                  ui::ColorRgba8 killer_color, std::string icon_asset,
                  double icon_source_pixels, std::string victim_name,
                  ui::ColorRgba8 victim_color);
    /** Merge one authoritative local SetScore delta into the bounded HUD stack. */
    void add_score_award(std::int32_t delta, std::uint8_t reason);
    /**
     * Show one top-screen message. Non-overriding arrivals enter retail's
     * bounded six-row queue; while a row is queued the current line yields
     * after min(duration, 1.5 s) and the newest queued row shows next.
     */
    void set_big_message(std::string text, bool override_previous = false,
                         double duration_seconds = 4.0);
    /**
     * HUD.add_big_messageBackGround(text, duration=BIG_TEXT_TIME): shows the
     * line now when the big-text lane is idle, and remembers it so HUD.update
     * re-shows it once whenever the lane empties before `duration` expires.
     * Character.update_respawn_time posts VIP_DEAD_CAM_INSTRUCTION this way
     * every frame while never_respawn is set.
     */
    void set_background_big_message(std::string text,
                                    double duration_seconds = 4.0);
    /**
     * GameScene hides big_text when it still shows `text` (the VIP dead-cam
     * instruction on respawn): big_text_time = None.
     */
    void hide_big_message_if(std::string_view text) noexcept;
    /** Zero is an immediate/mode transition; 255 is NEVER_RESPAWN_TIME. */
    void set_respawn_time(std::uint8_t seconds) noexcept;
    /** Clear only at the authoritative local CreatePlayer life boundary. */
    void clear_respawn() noexcept;
    /** Adds a 1.2-second retail hit-direction flash; bounded under packet bursts. */
    void add_damage_indicator(double rotation_degrees) noexcept;
    /** Sets the north-up map crop and its already-authorized marker set. */
    void set_minimap(GameHudMinimapState state) noexcept;
    /** Held VIEW_MAP binding; does not change server-owned minimap permission. */
    void set_full_map_visible(bool visible) noexcept;
    /** Retail packet-43 full-screen tint, absent outside visible objective zones. */
    void set_inside_zone_tint(std::optional<ui::ColorRgba8> tint) noexcept;
    /**
     * SetHP burn (type 3) and sudden-death (type 4) indicators: HUD.draw's
     * draw_fsquad_tex(inside_zone_texture, colour) quads, fading out over
     * BURN/SUDDEN_DEATH_INDICATOR_TIME. Burn is red, sudden death the local
     * team colour; both draw when both run (burn first).
     */
    void set_status_tints(std::optional<ui::ColorRgba8> burn,
                          std::optional<ui::ColorRgba8> sudden_death) noexcept {
        burn_tint_ = burn;
        sudden_death_tint_ = sudden_death;
    }
    [[nodiscard]] std::optional<ui::ColorRgba8> burn_tint() const noexcept { return burn_tint_; }
    [[nodiscard]] std::optional<ui::ColorRgba8> sudden_death_tint() const noexcept {
        return sudden_death_tint_;
    }
    /**
     * Additional CTF-only corner icon. A carrier shows the opposing team's
     * intel colour; team ids are retail wire ids 2=Blue and 3=Green.
     */
    void set_intel_carrier(std::uint8_t carrier_team, bool visible) noexcept;

    /** Advances one fixed 60 Hz step of panel delay/transition animation. */
    void tick() noexcept;

    [[nodiscard]] GameHudSounds take_sounds() noexcept;

    [[nodiscard]] bool hud_visible() const noexcept { return hud_visible_; }
    [[nodiscard]] bool crosshair_visible() const noexcept { return crosshair_visible_; }
    [[nodiscard]] double crosshair_radius_pixels() const noexcept {
        return crosshair_radius_pixels_;
    }
    [[nodiscard]] bool crosshair_center_visible() const noexcept {
        return crosshair_center_visible_;
    }
    [[nodiscard]] ui::ColorRgba8 crosshair_color() const noexcept;
    [[nodiscard]] double hit_crosshair_seconds_remaining() const noexcept {
        return hit_crosshair_seconds_remaining_;
    }
    [[nodiscard]] bool help_open() const noexcept { return help_open_; }
    [[nodiscard]] double help_transition_percentage() const noexcept {
        return help_transition_;
    }
    [[nodiscard]] double help_delay_remaining() const noexcept { return help_delay_; }
    [[nodiscard]] const std::vector<std::string>& help_lines() const noexcept {
        return help_lines_;
    }
    [[nodiscard]] const std::string& help_close_hint() const noexcept {
        return help_close_hint_;
    }
    [[nodiscard]] std::int32_t health() const noexcept { return health_; }
    [[nodiscard]] bool numeric_health_visible() const noexcept {
        return numeric_health_visible_;
    }
    [[nodiscard]] std::int32_t displayed_health() const noexcept;
    [[nodiscard]] ui::ColorRgba8 health_text_color() const noexcept;
    [[nodiscard]] const std::vector<GameHudInventorySlot>& inventory_slots() const noexcept {
        return inventory_slots_;
    }
    [[nodiscard]] std::optional<std::size_t> selected_inventory_slot() const noexcept {
        return selected_inventory_slot_;
    }
    [[nodiscard]] bool inventory_visible() const noexcept { return inventory_visible_; }
    [[nodiscard]] const GameHudAmmoState& ammo() const noexcept { return ammo_; }
    [[nodiscard]] const GameHudPaletteState& palette() const noexcept {
        return palette_;
    }
    [[nodiscard]] std::int32_t player_score() const noexcept { return player_score_; }
    [[nodiscard]] bool player_score_visible() const noexcept {
        return player_score_visible_;
    }
    [[nodiscard]] hud_layout::Team team() const noexcept { return team_; }
    [[nodiscard]] ui::ColorRgba8 team_color() const noexcept { return team_color_; }
    [[nodiscard]] const std::string& class_portrait_asset() const noexcept {
        return class_portrait_asset_;
    }
    [[nodiscard]] bool class_portrait_highly_visible() const noexcept {
        return class_portrait_visible_;
    }
    [[nodiscard]] const GameHudBlockState& blocks() const noexcept { return blocks_; }
    [[nodiscard]] double jetpack_fuel() const noexcept { return jetpack_fuel_; }
    [[nodiscard]] bool jetpack_visible() const noexcept { return jetpack_visible_; }
    [[nodiscard]] bool disguise_active() const noexcept { return disguise_active_; }
    [[nodiscard]] bool parachute_active() const noexcept { return parachute_active_; }
    [[nodiscard]] const std::string& ability_hint() const noexcept { return ability_hint_; }
    [[nodiscard]] const std::string& death_class_hint() const noexcept { return death_class_hint_; }
    [[nodiscard]] bool vip_dead_camera() const noexcept { return vip_dead_camera_; }
    [[nodiscard]] const GameHudTeamScore& left_team_score() const noexcept {
        return left_team_score_;
    }
    [[nodiscard]] const GameHudTeamScore& right_team_score() const noexcept {
        return right_team_score_;
    }
    [[nodiscard]] bool team_scores_visible() const noexcept {
        return team_scores_visible_;
    }
    [[nodiscard]] const GameHudTeamProgressState& team_progress() const noexcept {
        return team_progress_;
    }
    [[nodiscard]] const std::vector<GameHudTeamProgressParticle>&
    team_progress_particles() const noexcept {
        return team_progress_particles_;
    }
    [[nodiscard]] const GameHudTerritoryBasesState&
    territory_bases() const noexcept {
        return territory_bases_;
    }
    [[nodiscard]] double match_clock_seconds() const noexcept {
        return match_clock_seconds_;
    }
    [[nodiscard]] bool match_clock_visible() const noexcept {
        return match_clock_visible_;
    }
    [[nodiscard]] hud_layout::TimerPlacement match_clock_placement() const noexcept {
        return match_clock_placement_;
    }
    [[nodiscard]] bool match_clock_minimap_enabled() const noexcept {
        return match_clock_minimap_enabled_;
    }
    [[nodiscard]] const std::vector<GameHudKillEntry>& kill_feed() const noexcept {
        return kill_feed_;
    }
    [[nodiscard]] const GameHudScoreAwardState& score_award() const noexcept {
        return score_award_;
    }
    [[nodiscard]] const GameHudBigMessage& big_message() const noexcept {
        return big_message_;
    }
    [[nodiscard]] const GameHudRespawnState& respawn() const noexcept {
        return respawn_;
    }
    [[nodiscard]] const std::vector<GameHudDamageIndicator>&
    damage_indicators() const noexcept {
        return damage_indicators_;
    }
    [[nodiscard]] const GameHudMinimapState& minimap() const noexcept {
        return minimap_;
    }
    [[nodiscard]] std::optional<ui::ColorRgba8> inside_zone_tint() const noexcept {
        return inside_zone_tint_;
    }
    [[nodiscard]] bool intel_carried() const noexcept {
        return intel_carried_;
    }
    [[nodiscard]] std::uint8_t intel_carrier_team() const noexcept {
        return intel_carrier_team_;
    }

private:
    void start_big_message(std::string text, double duration);

    std::vector<std::string> help_lines_;
    std::string help_close_hint_;
    double help_transition_{};
    double help_delay_{};
    bool help_open_{};
    bool hud_visible_{true};
    bool crosshair_visible_{};
    double crosshair_radius_pixels_{6.0};
    bool crosshair_center_visible_{true};
    double hit_crosshair_seconds_remaining_{};
    std::int32_t health_{100};
    bool numeric_health_visible_{true};
    double health_damage_multiplier_{1.0};
    std::vector<GameHudInventorySlot> inventory_slots_;
    std::optional<std::size_t> selected_inventory_slot_{};
    bool inventory_visible_{};
    GameHudAmmoState ammo_{};
    GameHudPaletteState palette_{};
    std::int32_t player_score_{};
    bool player_score_visible_{};
    hud_layout::Team team_{hud_layout::Team::team1};
    ui::ColorRgba8 team_color_{44U, 117U, 179U, 255U};
    std::string class_portrait_asset_;
    bool class_portrait_visible_{};
    GameHudBlockState blocks_{};
    double jetpack_fuel_{};
    bool jetpack_visible_{};
    bool disguise_active_{};
    bool parachute_active_{};
    std::string ability_hint_;
    std::string death_class_hint_;
    bool vip_dead_camera_{};
    GameHudTeamScore left_team_score_{};
    GameHudTeamScore right_team_score_{hud_layout::Team::team2, 0, 0, false};
    bool team_scores_visible_{};
    GameHudTeamProgressState team_progress_{};
    std::vector<GameHudTeamProgressParticle> team_progress_particles_{};
    GameHudTerritoryBasesState territory_bases_{};
    double match_clock_seconds_{};
    bool match_clock_visible_{};
    hud_layout::TimerPlacement match_clock_placement_{
        hud_layout::TimerPlacement::head_count};
    bool match_clock_minimap_enabled_{};
    std::vector<GameHudKillEntry> kill_feed_;
    GameHudScoreAwardState score_award_{};
    GameHudBigMessage big_message_{};
    std::string big_message_background_text_{};
    std::optional<double> big_message_background_seconds_{};
    GameHudRespawnState respawn_{};
    std::vector<GameHudDamageIndicator> damage_indicators_;
    GameHudMinimapState minimap_{};
    std::optional<ui::ColorRgba8> inside_zone_tint_{};
    std::optional<ui::ColorRgba8> burn_tint_{};
    std::optional<ui::ColorRgba8> sudden_death_tint_{};
    std::uint8_t intel_carrier_team_{};
    bool intel_carried_{};
    GameHudSounds pending_sounds_{};
};

struct GameHudPresentationContext final {
    /** Live window extent; the retail HUD is window-relative, never canvas. */
    ui::PixelExtent window{};
    std::uint32_t opacity_per_mille{1'000U};
    /**
     * Shaped line width in pixels for the help font at the given pixel size.
     * The retail panel backing hugs the widest shaped line; without a
     * measurer the presentation falls back to the max panel width.
     */
    std::function<double(std::string_view text, double font_size_pixels)> measure_text{};
    /**
     * Shaped A750 width used by KillLine. Keeping this separate is important:
     * retail's help panel uses Spades.ttf, while the kill feed uses
     * A750-Sans-Medium.ttf and advances the icon by that font's true width.
     */
    std::function<double(std::string_view text, double font_size_pixels)>
        measure_kill_feed_text{};
    /** Shaped Edo width used for CHAT_BIG line splitting and frame sizing. */
    std::function<double(std::string_view text, double font_size_pixels)>
        measure_big_text{};
    /** Spectators have no local health, inventory, or equipment to display. */
    bool player_widgets_visible{true};
    /**
     * False while the local character is dead: retail's character-driven
     * widgets (health bar, tool/ammo panels, intel icon, crosshair, tool
     * strip, block palette) draw nothing on the death screen, while the
     * score, HeadCount, minimap and feeds stay (live A/B, 2026-09-29).
     */
    bool character_widgets_visible{true};
    /**
     * Retail `strings` lookup for HUD labels (score reasons, respawn text,
     * VIP banner, dead-camera hints). Unset keeps the built-in English.
     */
    std::function<std::string(std::string_view key)> localize{};
};

/**
 * Builds the HUD draw list in live window pixels, exactly like the retail
 * hud.pyd: crosshair and health bar anchored to the window, then the sliding
 * help panel. The 800x600 design canvas applies only to 2D menus.
 */
class GameHudPresentation final {
public:
    [[nodiscard]] ui::DrawList build(const GameHudModel& model,
                                     const GameHudPresentationContext& context) const;
};

/**
 * Key names for the 20 controls retail's translate_controls_in_message
 * substitutes (aoslib/text.py:649-661). Defaults are the English
 * translate_key names of the retail default bindings; live values come from
 * retail_control_key_names (retail_input_rules.hpp).
 */
struct ControlKeyNames final {
    std::string forward{"W"};
    std::string backward{"S"};
    std::string left{"A"};
    std::string right{"D"};
    std::string jump{"SPACE"};
    std::string crouch{"CTRL"};
    std::string change_class{"COMMA"};
    std::string view_scores{"TAB"};
    std::string palette_up{"UP"};
    std::string palette_down{"DOWN"};
    std::string palette_left{"Left"};
    std::string palette_right{"Right"};
    std::string weapon_custom{"E"};
    std::string cancel_prefab_placement{"Q"};
    std::string carve_prefab{"C"};
    std::string tool_help{"H"};
    std::string hover{"Z"};
    std::string sprint{"SHIFT"};
    std::string ugc_settings{"X"};
    std::string menu{"ESCAPE"};

    /** The name for `key_<control>`, or nullopt for a non-retail placeholder. */
    [[nodiscard]] std::optional<std::string_view>
    lookup(std::string_view placeholder) const noexcept;
};

/**
 * Resolves the retail `{key_*}` control placeholders into bracketed key
 * names, e.g. "Use {key_forward} to move." -> "Use [W] to move.". Exactly
 * like retail, only the 20 known `{key_<control>}` tokens are replaced; any
 * other brace text (`{0}`, unknown keys) is left untouched.
 */
[[nodiscard]] std::string resolve_control_placeholders(std::string_view text,
                                                       const ControlKeyNames& names);

/** Verbatim recovered retail tutorial string table (english.py). */
[[nodiscard]] std::string_view tutorial_string(std::string_view message_key) noexcept;

} // namespace battlespades::frontend
