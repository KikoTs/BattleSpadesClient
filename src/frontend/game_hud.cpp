#include "battlespades/frontend/game_hud.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>

namespace battlespades::frontend {

double retail_jetpack_fuel_fraction(double fuel) noexcept {
    if (!std::isfinite(fuel)) {
        return 0.0;
    }
    return std::clamp(fuel / game_hud_assets::retail_jetpack_max_fuel, 0.0, 1.0);
}

GameHudHeadCountValue resolve_game_hud_head_count(
    std::uint8_t raw_type, std::int32_t score,
    std::int32_t player_count) noexcept {
    return GameHudHeadCountValue{
        raw_type == static_cast<std::uint8_t>(GameHudHeadCountType::player_count)
            ? player_count
            : score,
        raw_type != static_cast<std::uint8_t>(GameHudHeadCountType::inactive)};
}

namespace {

constexpr double crosshair_size{16.0};
constexpr double hit_crosshair_time{0.25};
constexpr ui::ColorRgba8 normal_crosshair_color{255U, 255U, 255U, 255U};
constexpr ui::ColorRgba8 hit_crosshair_color{230U, 40U, 79U, 255U};
// Captured retail toolbar at 1920x1080 plus hud.pyd draw_loadout_item_hud:
// entries are centered 80 px apart; the baseline is 30% of window height
// from the bottom. Retail draws Tool.image here (the authored 330px weapon
// portrait), not Tool.icon's unrelated 32px menu glyph. Frame scale is
// independent of the branch's label scale: the normal frame uses 0.25 and
// the selected frame keeps its authored 0.7. Weapon portraits are drawn
// inside the recovered half-scale transform after their 0.4/0.9 item scale.
constexpr double inventory_slot_stride{80.0};
constexpr double inventory_frame_normal{195.0 * 0.25};
constexpr double inventory_frame_selected{195.0 * 0.70};
constexpr double inventory_icon_normal{330.0 * 0.40 * 0.5};
constexpr double inventory_icon_selected{330.0 * 0.90 * 0.5};
constexpr double inventory_label_offset_normal{1.3 * 38.0 * 0.5};
constexpr double inventory_label_offset_selected{2.0 * 38.0 * 0.5};
constexpr double palette_cell_size{14.0};
constexpr double palette_cell_gap{2.0};
constexpr double palette_border{2.0};
constexpr double palette_right_padding{170.0};
constexpr double palette_bottom_padding{5.0};
constexpr double palette_ugc_padding{30.0};

// hud.pyd draw_ammo_hud/draw_player_score and images.py: these authored
// frames are 115x40 and 200x40. score_frame is authored top-left and retail
// draws it at (0, window.height) in pyglet's bottom-origin coordinates: (0,0)
// in our top-origin draw list. Ammo remains lower-right. The ammo portrait
// deliberately reuses Tool.ammo_image (normally Tool.image), scaled to the
// normal strip size.
constexpr double ammo_frame_width{115.0};
constexpr double ammo_frame_height{40.0};
constexpr double score_frame_width{200.0};
constexpr double score_frame_height{40.0};
constexpr double hud_right_inset{8.0};
constexpr double hud_bottom_inset{8.0};
constexpr double ammo_portrait_size{330.0 * 0.40};
// draw_ammo_hud defaults image_scale to 0.1. TOOL_IMAGES are authored 330px
// textures loaded at scale 1.0, so the lower-right weapon and block portraits
// occupy a 33px square before the authored transparent margins.
constexpr double ammo_icon_size{330.0 * 0.1};
constexpr double kill_feed_font_size{14.0};
constexpr double kill_feed_ttl{5.0};
constexpr double kill_feed_anchor_top{75.0};
constexpr double kill_feed_name_padding{5.0};
constexpr double kill_feed_icon_scale{0.1};
constexpr double kill_feed_icon_lift{6.0};
// ChatLine.__init__ stores chat_font.get_line_height(), not the nominal 12px
// request. The bundled retail font wrapper returns 13 for this exact face/size.
constexpr double retail_chat_line_height{13.0};
// hud.pyd ScoreLine/Font values, confirmed against the shipped Spades.ttf:
// the title uses 20 px (22 px line height, 15 px ascender), reason rows use
// 16 px (17/12), and HUD.draw translates by content_height + MSG_PAD * 2.
constexpr double score_title_font_size{20.0};
constexpr double score_title_line_height{22.0};
constexpr double score_item_font_size{16.0};
constexpr double score_item_line_height{17.0};
constexpr double score_message_ttl{1.5};
constexpr double score_line_delay{0.25};
constexpr double score_fade_time{0.2};
constexpr double score_message_pad{5.0};
constexpr std::uint8_t score_full_alpha{127U};
// One numeric title plus four reason rows. A new award resets a full stack.
constexpr std::size_t maximum_score_lines{5U};
constexpr ui::ColorRgba8 score_title_color{255U, 255U, 255U, 255U};
constexpr ui::ColorRgba8 score_stroke_color{0U, 0U, 0U, 255U};
constexpr double big_text_duration{4.0};
constexpr double big_text_font_size{30.0};
// Font.get_line_height() from the shipped x86 font module. These are layout
// spans, not nominal point sizes. Label(anchor_y='center') subtracts half of
// this value from its bottom-origin baseline.
constexpr double big_text_line_advance{37.0};
constexpr double big_text_frame_height{58.0};
constexpr double big_text_frame_padding{40.0};
constexpr ui::ColorRgba8 big_text_color{231U, 74U, 25U, 255U};
constexpr double respawn_font_size{40.0};
constexpr double respawn_line_height{44.0};
constexpr double health_line_height{27.0};
constexpr double team_progress_line_height{22.0};
constexpr ui::ColorRgba8 respawn_shadow_color{64U, 64U, 64U, 255U};
constexpr double damage_indicator_ttl{1.2};
constexpr double damage_indicator_scale{0.5};
// png/ui/indicator.png is a transparent, centre-anchored 658x952 canvas.
constexpr double damage_indicator_width{658.0 * damage_indicator_scale};
constexpr double damage_indicator_height{952.0 * damage_indicator_scale};
constexpr double minimap_size{128.0};
constexpr double minimap_half{64.0};
constexpr double minimap_edge{512.0};
constexpr double minimap_margin{15.0};
constexpr double minimap_frame_border{7.0};
constexpr double minimap_frame_size{142.0};
constexpr double full_map_size{512.0};
constexpr double full_map_frame_size{552.0};
// minimap.py:635-646 draws a 1px white, 0.5-alpha grid after the full
// 512x512 map sprite.  The two recovered xrange tuples are (0, 576, 64)
// for vertical lines and (512, -64, -64) for horizontal lines.  Their final
// x=512/y=0 strips deliberately extend one pixel toward the frame, matching
// the retail immediate-mode GL_QUADS rather than clipping them back inside.
constexpr double full_map_grid_step{64.0};
constexpr std::uint16_t full_map_grid_opacity{500U};
constexpr double team_progress_font_size{20.0};
constexpr double team_progress_particle_source_size{128.0};
constexpr std::size_t maximum_hud_particles{100U};
constexpr double team_progress_label_width{160.0};
constexpr ui::ColorRgba8 team_progress_shadow_color{64U, 64U, 64U, 255U};
constexpr ui::ColorRgba8 territory_contested_color{255U, 100U, 0U, 255U};
constexpr double territory_pulse_time{std::numbers::pi / 3.0};

std::string_view score_reason_label_impl(std::uint8_t reason) noexcept {
    // shared.constants.SCORE_REASON_CODES plus strings/english.py. Ordinals
    // that are profile totals deliberately remain blank: they update stats,
    // but retail does not put an internal identifier on the gameplay HUD.
    switch (reason) {
    case 1U: return "Kill";
    case 2U: return "Suicide";
    case 3U: return "Headshot";
    case 4U: return "Melee";
    case 5U: return "Assist";
    case 6U: return "Team Kill";
    case 7U: return "Death Revenge";
    case 8U: return "Distraction";
    case 9U: return "Payback";
    case 10U: return "Reloading Kill";
    case 11U: return "Defend";
    case 12U: return "VIP Survive";
    case 13U: return "VIP Escort";
    case 14U: return "Kill Enemy VIP";
    case 15U: return "VIP Distraction";
    case 16U: return "VIP Kill";
    case 17U: return "Close to VIP";
    case 18U: return "VIP Assault";
    case 19U: return "VIP Defend";
    case 20U: return "Controlled Territories";
    case 21U: return "Claim Territory";
    case 22U: return "Control Territory";
    case 23U: return "Defend Territory";
    case 24U: return "Assault Territory";
    case 25U: return "Contend Territory";
    case 26U: return "Occupy";
    case 27U: return "Carry Bomb";
    case 28U: return "BOOM!";
    case 29U: return "Bomb Distraction";
    case 30U: return "Carrier Defend";
    case 31U: return "Bomb Defend";
    case 32U: return "Close to Bomb";
    case 33U: return "Survive Blast";
    case 34U: return "Intercept Carrier";
    case 36U: return "Bomb Disposal";
    case 37U: return "Intercept Disposal";
    case 38U: return "Capture";
    case 39U: return "Uncover Diamond";
    case 40U: return "Carry Diamond";
    case 41U: return "Diamond Escort";
    case 42U: return "Diamond Distraction";
    case 43U: return "Carrier Defend";
    case 44U: return "Diamond Defend";
    case 45U: return "Diamond Assault";
    case 46U: return "Intercept Carrier";
    case 49U: return "Capture Flag";
    case 50U: return "Carry Flag";
    case 51U: return "Flag Escort";
    case 52U: return "First to Claim Flag";
    case 53U: return "Flag Distraction";
    case 54U: return "Flag Defend";
    case 55U: return "Close to Flag";
    case 56U: return "Flag Assault";
    case 57U: return "Flag Carrier Defend";
    case 58U: return "Flag Intercept";
    case 59U: return "Survive";
    case 60U: return "Last Man Standing";
    case 61U: return "Kill Survivor";
    case 62U: return "LMS Zombie Kill";
    case 69U: return "Destroy Base";
    case 70U: return "Repair Base";
    case 71U: return "Defend Base";
    case 72U: return "Assault Base";
    case 76U: return "Occupy";
    case 77U: return "First to Hill";
    case 78U: return "Claim Hill";
    case 79U: return "Control Hill";
    case 80U: return "Defend Hill";
    case 81U: return "Assault Hill";
    case 82U: return "Contest Hill";
    default: return {};
    }
}

[[nodiscard]] std::int32_t saturating_score_add(std::int32_t current,
                                                std::int32_t delta) noexcept {
    const auto total = static_cast<std::int64_t>(current) + delta;
    return static_cast<std::int32_t>(std::clamp(
        total,
        static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min()),
        static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max())));
}

[[nodiscard]] std::string format_score_delta(std::int32_t delta) {
    return (delta > 0 ? "+" : "") + std::to_string(delta);
}

// Recovered HelpPanel layout in live window pixels: max width 30% of the
// window, padding 20, ALDO/Spades fixed 20px, backing (0,0,0,150), text
// MENU_FONT_COLOR (244,236,187). Line metrics follow the Spades.ttf face
// (em 2048, ascent 1930, descent 647): sizing row = (asc+desc+5)*scale and
// the per-line draw advance is (asc+desc)*scale + 4.
constexpr double help_padding{20.0};
constexpr double help_font_size{20.0};
constexpr double help_ascender{1930.0 / 2048.0 * help_font_size};
constexpr double help_descender{647.0 / 2048.0 * help_font_size};
constexpr ui::ColorRgba8 menu_font_color{244U, 236U, 187U, 255U};

// Recovered draw_healthbar layout (hud.pyd @0x1009b910): the center-anchored
// 239x34 frame blits at (window.width*0.5, 30) bottom-origin, i.e. top-left
// (W*0.5 - 119.5, H - 47) in window pixels. The fill scales horizontally by
// hp/100 about the retail 35px anchor (glScalef, not clip) tinted with the
// team color; the Tutorial player is Blue team 1 and pins 100.
constexpr double health_bar_width{239.0};
constexpr double health_bar_height{34.0};
constexpr ui::ColorRgba8 team1_color{44U, 117U, 179U, 255U};

[[nodiscard]] ui::SpriteDrawCommand window_sprite(std::string_view asset, double left,
                                                  double top, double width, double height,
                                                  ui::ColorModulation modulation = {},
                                                  ui::TextureFilter filter =
                                                      ui::TextureFilter::linear,
                                                  double rotation_degrees = 0.0) {
    return ui::SpriteDrawCommand{
        std::string{asset},
        ui::DrawRect{left, top, width, height},
        ui::DrawSpace::window_pixels,
        filter,
        ui::TextureAnchor::top_left,
        1.0,
        ui::SpriteSizing::stretch,
        modulation,
        rotation_degrees,
    };
}

[[nodiscard]] GameHudTeamProgressEntry*
team_progress_entry(GameHudTeamProgressState& state,
                    hud_layout::Team team) noexcept {
    const auto found = std::ranges::find_if(
        state.entries,
        [team](const GameHudTeamProgressEntry& entry) {
            return entry.team == team;
        });
    return found == state.entries.end() ? nullptr : &*found;
}

[[nodiscard]] ui::ColorRgba8 blend_territory_color(
    ui::ColorRgba8 controlled, double contested_alpha) noexcept {
    const auto alpha = std::clamp(contested_alpha, 0.0, 1.0);
    const auto blend_channel = [alpha](std::uint8_t from,
                                       std::uint8_t to) {
        return static_cast<std::uint8_t>(std::clamp(
            std::lround(static_cast<double>(from) * (1.0 - alpha) +
                        static_cast<double>(to) * alpha),
            0L, 255L));
    };
    return {
        blend_channel(controlled.red, territory_contested_color.red),
        blend_channel(controlled.green, territory_contested_color.green),
        blend_channel(controlled.blue, territory_contested_color.blue),
        controlled.alpha,
    };
}

} // namespace

std::string_view retail_score_reason_label(std::uint8_t reason) noexcept {
    return score_reason_label_impl(reason);
}

std::span<const ui::ColorRgba8> retail_block_palette() noexcept {
    // Four value bands by eight hue columns. Retail's palette is generated
    // from colours rather than an image; keeping this table stable makes arrow
    // selection deterministic and, crucially, sends the same RGB that is shown.
    static constexpr std::array<ui::ColorRgba8, 32U> colors{{
        {255U, 255U, 255U, 255U}, {255U, 204U, 204U, 255U},
        {255U, 222U, 153U, 255U}, {255U, 255U, 153U, 255U},
        {153U, 255U, 153U, 255U}, {153U, 255U, 255U, 255U},
        {153U, 187U, 255U, 255U}, {221U, 153U, 255U, 255U},
        {190U, 190U, 190U, 255U}, {221U, 102U, 102U, 255U},
        {221U, 153U, 68U, 255U},  {221U, 221U, 68U, 255U},
        {68U, 187U, 68U, 255U},   {68U, 187U, 187U, 255U},
        {68U, 102U, 221U, 255U},  {153U, 68U, 221U, 255U},
        {110U, 110U, 110U, 255U}, {159U, 0U, 0U, 255U},
        {159U, 85U, 0U, 255U},    {159U, 159U, 0U, 255U},
        {0U, 127U, 0U, 255U},     {0U, 127U, 127U, 255U},
        {0U, 51U, 159U, 255U},    {85U, 0U, 159U, 255U},
        {35U, 35U, 35U, 255U},    {96U, 0U, 0U, 255U},
        {96U, 48U, 0U, 255U},     {96U, 96U, 0U, 255U},
        {0U, 72U, 0U, 255U},      {0U, 72U, 72U, 255U},
        {0U, 32U, 96U, 255U},     {48U, 0U, 96U, 255U},
    }};
    return colors;
}

std::optional<GameHudTeamProgressIcon>
team_progress_icon_from_wire(std::uint8_t icon_id) noexcept {
    switch (icon_id) {
    case 0U:
        return GameHudTeamProgressIcon::base;
    case 1U:
        return GameHudTeamProgressIcon::diamond;
    default:
        return std::nullopt;
    }
}

double damage_indicator_angle_degrees(double source_delta_x,
                                      double source_delta_y,
                                      double view_x,
                                      double view_y) noexcept {
    constexpr double radians_to_degrees{180.0 / std::numbers::pi};
    return (std::atan2(source_delta_y, source_delta_x) -
            std::atan2(view_y, view_x)) *
           radians_to_degrees;
}

double minimap_marker_rotation_degrees(double orientation_x,
                                       double orientation_y) noexcept {
    if (!std::isfinite(orientation_x) || !std::isfinite(orientation_y) ||
        std::hypot(orientation_x, orientation_y) <= 1.0e-12) {
        return 0.0;
    }

    // Retail writes Sprite.rotation = Player.yaw - 180. Player.yaw uses the
    // character-root basis atan2(-orientation.x, orientation.y), unlike the
    // native camera yaw. This equivalent form returns a stable [-180, 180]
    // clockwise angle directly in the y-down minimap coordinate system.
    return std::atan2(orientation_x, -orientation_y) * 180.0 /
           std::numbers::pi;
}

double zombie_heartbeat_marker_size(double elapsed_seconds) noexcept {
    constexpr double off_time{1.0};
    constexpr double on_time{0.1};
    constexpr double contract_time{1.0};
    constexpr double minimum_size{0.05};
    constexpr double maximum_size{0.07};
    constexpr double base_scale{0.05};
    constexpr double source_pixels{256.0};
    constexpr double cycle = off_time + on_time + contract_time;
    if (!std::isfinite(elapsed_seconds) || elapsed_seconds < 0.0) {
        return 0.0;
    }
    const double phase = std::fmod(elapsed_seconds, cycle);
    if (phase < off_time) {
        return 0.0;
    }
    double animated_size{};
    if (phase < off_time + on_time) {
        const double progress = (phase - off_time) / on_time;
        animated_size = minimum_size +
                        (maximum_size - minimum_size) * progress;
    } else {
        const double progress =
            (phase - off_time - on_time) / contract_time;
        animated_size = maximum_size -
                        (maximum_size - minimum_size) * progress;
    }
    return (base_scale + animated_size) * source_pixels;
}

std::optional<GameHudMinimapEntityStyle>
minimap_entity_style(std::uint8_t entity_type) noexcept {
    using namespace game_hud_assets;
    switch (entity_type) {
    case 8U:
        return GameHudMinimapEntityStyle{minimap_turret, true, false};
    case 9U:
        return GameHudMinimapEntityStyle{minimap_landmine, false, false};
    case 10U:
        return GameHudMinimapEntityStyle{minimap_dynamite, false, false};
    case 11U:
        return GameHudMinimapEntityStyle{minimap_grave, false, false};
    case 14U:
        return GameHudMinimapEntityStyle{minimap_bomb, false, false};
    case 15U:
        return GameHudMinimapEntityStyle{minimap_diamond, false, false};
    case 16U:
        return GameHudMinimapEntityStyle{minimap_intel, false, true};
    case 25U:
        return GameHudMinimapEntityStyle{minimap_base, false, true};
    case 30U:
        return GameHudMinimapEntityStyle{minimap_medpack, false, false};
    case 36U:
        return GameHudMinimapEntityStyle{minimap_radar, false, false};
    case 38U:
        return GameHudMinimapEntityStyle{minimap_c4, false, false};
    default:
        return std::nullopt;
    }
}

std::optional<GameHudMinimapZoneIconStyle>
minimap_zone_icon_style(std::uint8_t icon_id) noexcept {
    using namespace game_hud_assets;
    // MINIMAP_ZONE_ICON in constants_gamemode.py. H/I/J intentionally reuse
    // the shipped G art; there are no authored H/I/J minimap textures.
    switch (icon_id) {
    case 1U:
    case 6U:
        return GameHudMinimapZoneIconStyle{minimap_zone_base, 16.0};
    case 2U:
        return GameHudMinimapZoneIconStyle{minimap_zone_multihill, 128.0};
    case 3U:
        return GameHudMinimapZoneIconStyle{minimap_zone_occupation, 128.0};
    case 4U:
        return GameHudMinimapZoneIconStyle{minimap_zone_diamond, 128.0};
    case 5U:
        return GameHudMinimapZoneIconStyle{minimap_zone_vip, 256.0};
    case 7U:
        return GameHudMinimapZoneIconStyle{minimap_zone_territory_a, 64.0};
    case 8U:
        return GameHudMinimapZoneIconStyle{minimap_zone_territory_b, 64.0};
    case 9U:
        return GameHudMinimapZoneIconStyle{minimap_zone_territory_c, 64.0};
    case 10U:
        return GameHudMinimapZoneIconStyle{minimap_zone_territory_d, 64.0};
    case 11U:
        return GameHudMinimapZoneIconStyle{minimap_zone_territory_e, 64.0};
    case 12U:
        return GameHudMinimapZoneIconStyle{minimap_zone_territory_f, 64.0};
    case 13U:
    case 14U:
    case 15U:
    case 16U:
        return GameHudMinimapZoneIconStyle{minimap_zone_territory_g, 64.0};
    case 17U:
        return GameHudMinimapZoneIconStyle{minimap_zone_spawn, 16.0};
    default:
        return std::nullopt;
    }
}

std::string_view ControlKeyNames::lookup(std::string_view placeholder) const noexcept {
    if (placeholder == "key_forward") {
        return forward;
    }
    if (placeholder == "key_backward") {
        return backward;
    }
    if (placeholder == "key_left") {
        return left;
    }
    if (placeholder == "key_right") {
        return right;
    }
    if (placeholder == "key_jump") {
        return jump;
    }
    if (placeholder == "key_crouch") {
        return crouch;
    }
    if (placeholder == "key_tool_help") {
        return tool_help;
    }
    return {};
}

std::string resolve_control_placeholders(std::string_view text,
                                         const ControlKeyNames& names) {
    std::string result;
    result.reserve(text.size() + 8U);
    std::size_t position{};
    while (position < text.size()) {
        const auto open = text.find('{', position);
        if (open == std::string_view::npos) {
            result.append(text.substr(position));
            break;
        }
        const auto close = text.find('}', open + 1U);
        if (close == std::string_view::npos) {
            result.append(text.substr(position));
            break;
        }
        result.append(text.substr(position, open - position));
        const auto placeholder = text.substr(open + 1U, close - open - 1U);
        const auto name = names.lookup(placeholder);
        // Retail translate_controls_in_message renders "[<key>]".
        result.push_back('[');
        result.append(name.empty() ? std::string_view{"?"} : name);
        result.push_back(']');
        position = close + 1U;
    }
    return result;
}

std::string_view tutorial_string(std::string_view message_key) noexcept {
    // Verbatim recovered retail strings (aoslib/strings/english.py).
    static constexpr std::array<std::pair<std::string_view, std::string_view>, 22U> table{{
        {"TUTORIAL_INTRO", "Welcome to the Ace of Spades training level."},
        {"TUTORIAL_BASIC_CONTROLS_1",
         "Use {key_forward}, {key_left}, {key_backward} and {key_right} to move."},
        {"TUTORIAL_BASIC_CONTROLS_2", "Use the mouse to look around."},
        {"TUTORIAL_BASIC_CONTROLS_3", "Head along the corridor!"},
        {"TUTORIAL_JUMP_1", "Use {key_jump} to jump."},
        {"TUTORIAL_JUMP_2", "Jump and move forward to climb the ledge."},
        {"TUTORIAL_CROUCH_1", "Hold {key_crouch} to crouch."},
        {"TUTORIAL_CROUCH_2", "Head to the shooting gallery!"},
        {"TUTORIAL_SHOOTING_1", "You now have a pistol!"},
        {"TUTORIAL_SHOOTING_2", "Use Left Mouse to fire at the targets."},
        {"TUTORIAL_SHOOTING_3",
         "Use Right Mouse to enter Iron Sights for higher accuracy."},
        {"TUTORIAL_CLIMB1",
         "You now have the block tool and spade! Dig and build your way to the top of "
         "the tower!"},
        {"TUTORIAL_CLIMB2", "Use Mouse Wheel or number keys to cycle weapons."},
        {"TUTORIAL_CLIMB3", "Hold Left Mouse Button to draw lines of blocks."},
        {"TUTORIAL_COMPLETE_1", "You have completed the Ace of Spades boot camp!"},
        {"TUTORIAL_COMPLETE_2", "You are now ready to build and destroy the battlefield!"},
        {"TUTORIAL_COMPLETE_3", "Training will exit when the timer reaches zero."},
        {"TUTORIAL_DESTROY_TARGET", "Target destroyed. {0} left."},
        {"TOOL_HELP_PANEL_CLOSE", "{key_tool_help} Close"},
        {"TUTORIAL_MODE_TITLE", "Tutorial"},
        {"LOAD_FAILED", "LOAD_FAILED"},
        {"", ""},
    }};
    for (const auto& [key, value] : table) {
        if (key == message_key) {
            return value;
        }
    }
    return message_key;
}

void GameHudModel::set_help_messages(std::vector<std::string> lines, std::string close_hint,
                                     double delay_seconds) {
    help_lines_ = std::move(lines);
    help_close_hint_ = std::move(close_hint);
    help_delay_ = delay_seconds;
    help_open_ = true;
    // Retail plays the swap-out cue immediately when new text is set.
    if (!help_lines_.empty()) {
        pending_sounds_.play_disappear = true;
    }
}

void GameHudModel::toggle_help() noexcept {
    help_open_ = !help_open_;
}

void GameHudModel::toggle_hud() noexcept {
    hud_visible_ = !hud_visible_;
}

void GameHudModel::set_health(std::int32_t health) noexcept {
    health_ = std::max(health, 0);
}

void GameHudModel::set_numeric_health_visible(bool visible) noexcept {
    numeric_health_visible_ = visible;
}

void GameHudModel::set_health_damage_multiplier(double multiplier) noexcept {
    health_damage_multiplier_ =
        std::isfinite(multiplier) && multiplier > 0.0 ? multiplier : 0.0;
}

std::int32_t GameHudModel::displayed_health() const noexcept {
    // hud.pyx:1068-1085: classes with a non-positive multiplier display zero;
    // every other class divides the server's raw pool before rounding up.
    if (health_damage_multiplier_ <= 0.0) return 0;
    const auto value = std::ceil(static_cast<double>(health_) /
                                 health_damage_multiplier_);
    return static_cast<std::int32_t>(std::clamp(
        value, 0.0,
        static_cast<double>(std::numeric_limits<std::int32_t>::max())));
}

ui::ColorRgba8 GameHudModel::health_text_color() const noexcept {
    constexpr double low_health_threshold{20.0};
    // Retail blends with the unrounded effective value, then separately uses
    // ceil() for the text. Reusing displayed_health() here makes 19.1 appear
    // fully white because its label rounds to 20.
    const auto hp = health_damage_multiplier_ > 0.0
                        ? static_cast<double>(health_) /
                              health_damage_multiplier_
                        : 0.0;
    if (hp >= low_health_threshold) {
        return {255U, 255U, 255U, 255U};
    }
    const auto ratio = std::clamp(hp / low_health_threshold, 0.0, 1.0);
    const auto channel = static_cast<std::uint8_t>(
        std::lround(255.0 * ratio));
    return {255U, channel, channel, 255U};
}

void GameHudModel::set_crosshair_visible(bool visible) noexcept {
    crosshair_visible_ = visible;
}

void GameHudModel::set_crosshair_geometry(double radius_pixels,
                                          bool draw_center) noexcept {
    crosshair_radius_pixels_ =
        std::isfinite(radius_pixels) ? std::max(0.0, radius_pixels) : 6.0;
    crosshair_center_visible_ = draw_center;
}

void GameHudModel::confirm_hit() noexcept {
    hit_crosshair_seconds_remaining_ = hit_crosshair_time;
}

ui::ColorRgba8 GameHudModel::crosshair_color() const noexcept {
    return hit_crosshair_seconds_remaining_ > 0.0 ? hit_crosshair_color
                                                   : normal_crosshair_color;
}

void GameHudModel::set_inventory_state(std::vector<GameHudInventorySlot> slots,
                                       std::optional<std::size_t> selected,
                                       bool visible) noexcept {
    inventory_slots_ = std::move(slots);
    selected_inventory_slot_ = selected.has_value() && *selected < inventory_slots_.size()
                                   ? selected
                                   : std::nullopt;
    inventory_visible_ = visible && !inventory_slots_.empty();
}

void GameHudModel::set_ammo_state(std::string image_asset, std::int32_t current,
                                  std::optional<std::int32_t> reserve,
                                  bool visible) noexcept {
    ammo_.image_asset = std::move(image_asset);
    ammo_.current = std::max(current, 0);
    ammo_.reserve = reserve.has_value()
                        ? std::optional<std::int32_t>{std::max(*reserve, 0)}
                        : std::nullopt;
    ammo_.image_scale = 0.1;
    ammo_.image_color.reset();
    ammo_.enough = ammo_.current > 0;
    ammo_.visible = visible && !ammo_.image_asset.empty();
}

void GameHudModel::set_prefab_cost_state(std::string preview_asset,
                                         std::int32_t cost,
                                         bool affordable,
                                         ui::ColorRgba8 tint,
                                         bool visible) noexcept {
    ammo_.image_asset = std::move(preview_asset);
    ammo_.current = std::max(cost, 0);
    ammo_.reserve.reset();
    // hud.pyd draw_tools_hud passes image_scale=.25 specifically for
    // weapon_object.prefab_cost_icon. Every shipped prefab portrait is 330px.
    ammo_.image_scale = 0.25;
    ammo_.image_color = tint;
    ammo_.enough = affordable;
    ammo_.visible = visible && !ammo_.image_asset.empty();
}

void GameHudModel::set_player_score(std::int32_t score, bool visible) noexcept {
    player_score_ = std::max(score, 0);
    player_score_visible_ = visible;
}

void GameHudModel::set_team(hud_layout::Team team) noexcept {
    team_ = team;
    team_color_ = hud_layout::team_color(team);
}

void GameHudModel::set_palette_state(
    std::vector<ui::ColorRgba8> colors, std::size_t columns,
    std::optional<std::size_t> selected, bool visible,
    bool ugc_layout) noexcept {
    palette_.colors = std::move(colors);
    palette_.columns = std::max<std::size_t>(1U, columns);
    palette_.selected =
        selected.has_value() && *selected < palette_.colors.size()
            ? selected
            : std::nullopt;
    palette_.visible = visible && !palette_.colors.empty();
    palette_.ugc_layout = ugc_layout;
}

void GameHudModel::set_team_color(ui::ColorRgba8 color) noexcept {
    team_color_ = color;
}

void GameHudModel::set_class_portrait(std::string icon_asset, bool high_minimap_visibility) {
    class_portrait_asset_ = std::move(icon_asset);
    class_portrait_visible_ = high_minimap_visibility;
}

void GameHudModel::set_block_state(std::string icon_asset, std::int32_t count,
                                   std::int32_t reserve, bool visible) {
    blocks_.icon_asset = std::move(icon_asset);
    blocks_.count = std::max(count, 0);
    blocks_.reserve = std::max(reserve, 0);
    blocks_.visible = visible;
}

void GameHudModel::set_jetpack_fuel(double fraction, bool visible) noexcept {
    jetpack_fuel_ = std::clamp(fraction, 0.0, 1.0);
    jetpack_visible_ = visible;
}

void GameHudModel::set_disguise_active(bool active) noexcept {
    disguise_active_ = active;
}

void GameHudModel::set_parachute_active(bool active) noexcept {
    parachute_active_ = active;
}

void GameHudModel::set_team_scores(GameHudTeamScore left, GameHudTeamScore right,
                                   bool visible) noexcept {
    left_team_score_ = left;
    right_team_score_ = right;
    team_scores_visible_ = visible;
    if (auto* entry = team_progress_entry(team_progress_, left.team);
        entry != nullptr) {
        entry->color = left.color;
    }
    if (auto* entry = team_progress_entry(team_progress_, right.team);
        entry != nullptr) {
        entry->color = right.color;
    }
}

void GameHudModel::update_team_progress(
    const GameHudTeamProgressUpdate& update) noexcept {
    auto* entry = team_progress_entry(team_progress_, update.team);
    if (entry == nullptr) {
        // Retail's dict would raise on spectator/invalid teams. The native
        // client fails closed instead of letting a malformed server packet
        // tear down the render loop.
        return;
    }

    if (!update.show_as_percent) {
        team_progress_.maximum = update.maximum;
    }
    team_progress_.show_as_percent = update.show_as_percent;
    entry->visible = update.visible;
    entry->show_previous = update.show_previous;
    if (update.icon.has_value()) {
        entry->icon = *update.icon;
    }

    if (update.value == 0.0) {
        entry->previous_damage = 0.0;
        entry->current = team_progress_.maximum;
        return;
    }

    const auto converted = team_progress_.maximum - update.value;
    entry->previous_damage =
        update.show_previous ? entry->current : converted;
    entry->current = converted;

    // TeamProgressBar only exposes a particle for damage/progress loss. Its
    // healing branch computes alternate geometry but deliberately clears the
    // packet flag before reaching HudParticleManager.start_particle.
    if (!update.show_particle || entry->previous_damage < entry->current ||
        team_progress_particles_.size() >= maximum_hud_particles) {
        return;
    }

    const double delta = entry->previous_damage - entry->current;
    double scale = 0.5;
    if (delta * 100.0 < 10.0) {
        scale = delta * 0.4;
    }
    const double direction =
        update.team == hud_layout::Team::team1 ? -1.0 : 1.0;
    team_progress_particles_.push_back(GameHudTeamProgressParticle{
        direction * 67.0 - 10.0,
        0.0,
        direction,
        -1.0,
        0.5,
        200.0,
        scale,
        0.8,
        1.0,
        -2.0,
        entry->color.value_or(hud_layout::team_color(update.team)),
    });
}

void GameHudModel::reset_team_progress() noexcept {
    team_progress_ = GameHudTeamProgressState{};
    team_progress_particles_.clear();
}

void GameHudModel::update_territory_base(
    const GameHudTerritoryBaseUpdate& update) noexcept {
    if (update.base_index >= territory_bases_.bases.size()) {
        // Retail has ten letter slots and would raise while indexing past J.
        // Fail closed so malformed mode state cannot tear down the render loop.
        return;
    }

    auto& retained = territory_bases_.bases[update.base_index];
    if (!retained.has_value()) {
        retained = GameHudTerritoryBaseState{update.base_index};
    }
    auto& base = *retained;

    const bool detail_not_required =
        update.action == GameHudTerritoryBaseAction::entering ||
        update.action == GameHudTerritoryBaseAction::leaving ||
        update.action == GameHudTerritoryBaseAction::contended ||
        update.action == GameHudTerritoryBaseAction::uncontended;
    if (!detail_not_required) {
        base.controlled_by = update.controlled_by;
        base.attacked_by = update.attacked_by;
        base.capture_amount = update.capture_amount;
    }

    switch (update.action) {
    case GameHudTerritoryBaseAction::activate:
        base.visible = true;
        break;
    case GameHudTerritoryBaseAction::deactivate:
        base.visible = false;
        break;
    case GameHudTerritoryBaseAction::entering:
        base.contains_player = true;
        break;
    case GameHudTerritoryBaseAction::leaving:
        base.contains_player = false;
        break;
    case GameHudTerritoryBaseAction::contended:
        base.contended = true;
        break;
    case GameHudTerritoryBaseAction::uncontended:
        base.contended = false;
        break;
    case GameHudTerritoryBaseAction::initial_info:
    case GameHudTerritoryBaseAction::capture_update:
        break;
    }
}

void GameHudModel::reset_territory_bases() noexcept {
    territory_bases_ = GameHudTerritoryBasesState{};
}

void GameHudModel::set_match_clock(
    double remaining_seconds, bool visible,
    hud_layout::TimerPlacement placement, bool minimap_enabled) noexcept {
    match_clock_seconds_ = std::max(remaining_seconds, 0.0);
    match_clock_visible_ = visible;
    match_clock_placement_ = placement;
    match_clock_minimap_enabled_ = minimap_enabled;
}

void GameHudModel::add_kill(std::optional<std::string> killer_name,
                            ui::ColorRgba8 killer_color,
                            std::string icon_asset,
                            double icon_source_pixels,
                            std::string victim_name,
                            ui::ColorRgba8 victim_color) {
    if (victim_name.empty()) {
        return;
    }
    GameHudKillEntry entry;
    entry.killer_name = std::move(killer_name);
    entry.killer_color = killer_color;
    entry.icon_asset = std::move(icon_asset);
    entry.icon_source_pixels = std::max(icon_source_pixels, 0.0);
    entry.victim_name = std::move(victim_name);
    entry.victim_color = victim_color;
    entry.ttl_seconds = kill_feed_ttl;
    kill_feed_.insert(kill_feed_.begin(), std::move(entry));
    constexpr std::size_t maximum_feed_entries{5U};
    if (kill_feed_.size() > maximum_feed_entries) {
        kill_feed_.resize(maximum_feed_entries);
    }
}

void GameHudModel::add_score_award(std::int32_t delta,
                                   std::uint8_t reason) {
    const auto label = retail_score_reason_label(reason);
    if (delta == 0 || label.empty()) {
        return;
    }

    auto& lines = score_award_.lines;
    const bool reset_stack =
        lines.empty() || lines.size() >= maximum_score_lines ||
        lines.front().ttl_seconds < score_fade_time;
    if (reset_stack) {
        // The reset path seeds the visible title with this amount. Retail gives
        // its first reason a zero score, otherwise HUD.update would count the
        // same award again as soon as the row became drawable.
        score_award_ = {};
        score_award_.displayed_delta = delta;
        score_award_.lines.push_back(GameHudScoreLineState{
            format_score_delta(delta), 0, score_message_ttl, 0.0, 0.0, true});
        score_award_.lines.push_back(GameHudScoreLineState{
            std::string{label}, 0, score_message_ttl, 0.0, 0.0, false});
        return;
    }

    // The constructor receives the current retained line count, so the second
    // reason waits 0.5 s (title + first row), the third waits 0.75 s, etc.
    const auto retained_lines = lines.size();
    lines.push_back(GameHudScoreLineState{
        std::string{label}, delta, score_message_ttl, 0.0,
        score_line_delay * static_cast<double>(retained_lines), false});

    // add_score_reason computes alive_before_show + start_ttl (1.75 s) and
    // calls ScoreLine.set_ttl on every retained line when the title has less.
    // This keeps the complete delayed stack alive long enough to appear.
    constexpr double extended_ttl{score_line_delay + score_message_ttl};
    if (lines.front().ttl_seconds < extended_ttl) {
        for (auto& line : lines) {
            line.ttl_seconds = extended_ttl;
        }
    }
}

void GameHudModel::set_big_message(std::string text, bool override_previous,
                                   double duration_seconds) {
    if (text.empty()) {
        return;
    }
    const auto duration = duration_seconds > 0.0
                              ? duration_seconds
                              : big_text_duration;
    if (override_previous) {
        // LocalisedMessage.override_previous_message clears both recovered
        // parallel lists before forcing the replacement into big_text.
        big_message_.pending.clear();
    } else if ((!big_message_.text.empty() &&
                big_message_.remaining_seconds > 0.0) ||
               !big_message_.pending.empty()) {
        // hud.pyd checks `len(bigMsgList_text) > 5` before appending, so six
        // deferred messages survive and the oldest is discarded on overflow.
        constexpr std::size_t maximum_pending{6U};
        if (big_message_.pending.size() == maximum_pending) {
            big_message_.pending.erase(big_message_.pending.begin());
        }
        big_message_.pending.push_back({std::move(text), duration});
        return;
    }
    big_message_.text = std::move(text);
    big_message_.remaining_seconds = duration;
    big_message_.age_seconds = 0.0;
}

void GameHudModel::set_respawn_time(std::uint8_t seconds) noexcept {
    // A zero timer is a mode transition, not a one-frame countdown. Zombie
    // immediately replaces the life with a zombie CreatePlayer; VIP can leave
    // the client in its server-owned spectator state. Only a positive delay or
    // the explicit NEVER_RESPAWN_TIME sentinel owns centre-screen text.
    respawn_.visible = seconds != 0U;
    respawn_.never_respawn = seconds == 0xFFU;
    respawn_.remaining_seconds =
        respawn_.never_respawn ? 0.0 : static_cast<double>(seconds);
    // A replacement KillAction owns a fresh countdown. Do not let an audio
    // edge queued by the preceding life escape on the next frontend drain.
    pending_sounds_.play_respawn_beep1 = false;
    pending_sounds_.play_respawn_beep2 = false;
}

void GameHudModel::clear_respawn() noexcept {
    respawn_ = {};
    pending_sounds_.play_respawn_beep1 = false;
    pending_sounds_.play_respawn_beep2 = false;
}

void GameHudModel::add_damage_indicator(double rotation_degrees) noexcept {
    if (!std::isfinite(rotation_degrees)) {
        return;
    }
    constexpr std::size_t maximum_indicators{8U};
    if (damage_indicators_.size() == maximum_indicators) {
        damage_indicators_.erase(damage_indicators_.begin());
    }
    damage_indicators_.push_back(
        GameHudDamageIndicator{rotation_degrees, damage_indicator_ttl});
}

void GameHudModel::set_minimap(GameHudMinimapState state) noexcept {
    if (!std::isfinite(state.focus_x) || !std::isfinite(state.focus_y) ||
        !std::isfinite(state.zone_phase_radians)) {
        state.visible = false;
    }
    constexpr std::size_t maximum_zones{64U};
    constexpr std::size_t maximum_markers{128U};
    if (state.zones.size() > maximum_zones) {
        state.zones.resize(maximum_zones);
    }
    if (state.markers.size() > maximum_markers) {
        state.markers.resize(maximum_markers);
    }
    minimap_ = std::move(state);
}

void GameHudModel::set_full_map_visible(bool visible) noexcept {
    minimap_.full_map_visible = visible;
}

void GameHudModel::set_inside_zone_tint(
    std::optional<ui::ColorRgba8> tint) noexcept {
    inside_zone_tint_ = tint;
}

void GameHudModel::set_intel_carrier(std::uint8_t carrier_team,
                                     bool visible) noexcept {
    intel_carrier_team_ = carrier_team;
    intel_carried_ =
        visible && (carrier_team == 2U || carrier_team == 3U);
}

void GameHudModel::tick() noexcept {
    hit_crosshair_seconds_remaining_ =
        std::max(0.0, hit_crosshair_seconds_remaining_ - fixed_dt);
    for (auto& particle : team_progress_particles_) {
        particle.remaining_seconds -= fixed_dt;
        particle.offset_x += fixed_dt * particle.speed_pixels_per_second *
                             particle.direction_x;
        particle.offset_y += fixed_dt * particle.speed_pixels_per_second *
                             particle.direction_y;
        particle.scale += fixed_dt * particle.scale_rate_per_second;
        particle.alpha =
            std::min(1.0, particle.alpha +
                              fixed_dt * particle.alpha_rate_per_second);
    }
    std::erase_if(team_progress_particles_,
                  [](const GameHudTeamProgressParticle& particle) {
                      return particle.remaining_seconds <= 0.0;
                  });
    minimap_.zone_phase_radians =
        std::fmod(minimap_.zone_phase_radians + fixed_dt * 6.0,
                  std::numbers::pi * 2.0);
    for (auto& retained : territory_bases_.bases) {
        if (!retained.has_value() || !retained->contended) {
            continue;
        }
        auto& base = *retained;
        base.contend_time += fixed_dt;
        if (base.contend_time > territory_pulse_time) {
            // TerritoryBaseInfo.update subtracts exactly once rather than
            // using fmod. Fixed 60 Hz updates keep this in one pulse period.
            base.contend_time -= territory_pulse_time;
        }
        const auto phase = base.contend_time / territory_pulse_time;
        base.contend_alpha =
            phase < 0.8 ? 1.0 - phase / 0.8 : 0.0;
    }
    if (match_clock_visible_) {
        match_clock_seconds_ = std::max(0.0, match_clock_seconds_ - fixed_dt);
    }
    if (respawn_.visible && !respawn_.never_respawn) {
        const auto previous_second =
            static_cast<std::int32_t>(respawn_.remaining_seconds);
        respawn_.remaining_seconds =
            std::max(0.0, respawn_.remaining_seconds - fixed_dt);
        const auto current_second =
            static_cast<std::int32_t>(respawn_.remaining_seconds);

        // character.pyx:1689-1694 compares int(previous) with int(current),
        // then uses beep2 while the live timer is <=4 and beep1 while it is
        // <=2. The <=1 branch performs the respawn before this code, producing
        // the familiar three beats: beep2, beep2, beep1 for 3, 2, 1. This is a
        // local presentation clock seeded by KillAction(46), not PlaySound(23)
        // and never Zombie's spoken outbreak countdown.
        if (previous_second != current_second &&
            respawn_.remaining_seconds > 1.0) {
            if (respawn_.remaining_seconds <= 2.0) {
                pending_sounds_.play_respawn_beep1 = true;
            } else if (respawn_.remaining_seconds <= 4.0) {
                pending_sounds_.play_respawn_beep2 = true;
            }
        }
    }
    for (auto& indicator : damage_indicators_) {
        indicator.remaining_seconds -= fixed_dt;
    }
    std::erase_if(damage_indicators_,
                  [](const GameHudDamageIndicator& indicator) {
                      return indicator.remaining_seconds <= 0.0;
                  });
    for (auto& entry : kill_feed_) {
        entry.ttl_seconds -= fixed_dt;
    }
    std::erase_if(kill_feed_, [](const GameHudKillEntry& entry) {
        return entry.ttl_seconds <= 0.0;
    });
    if (!score_award_.lines.empty()) {
        // ScoreLine.update runs before HUD.update. Positive scores are folded
        // into the numeric title only on the tick their delayed row first
        // becomes drawable; the line then clears its score to prevent repeats.
        for (auto& line : score_award_.lines) {
            line.ttl_seconds -= fixed_dt;
            line.alive_seconds += fixed_dt;
        }
        for (auto& line : score_award_.lines) {
            if (!line.title && line.pending_delta > 0 && line.can_draw()) {
                score_award_.displayed_delta = saturating_score_add(
                    score_award_.displayed_delta, line.pending_delta);
                line.pending_delta = 0;
            }
        }
        if (score_award_.lines.front().ttl_seconds <= 0.0) {
            score_award_ = {};
        }
    }
    if (!big_message_.text.empty()) {
        big_message_.remaining_seconds =
            std::max(0.0, big_message_.remaining_seconds - fixed_dt);
        big_message_.age_seconds += fixed_dt;
        if (big_message_.remaining_seconds <= 1.0e-9 &&
            !big_message_.pending.empty()) {
            auto pending = std::move(big_message_.pending.front());
            big_message_.pending.erase(big_message_.pending.begin());
            big_message_.text = std::move(pending.text);
            big_message_.remaining_seconds =
                pending.duration_seconds > 0.0
                    ? pending.duration_seconds
                    : big_text_duration;
            big_message_.age_seconds = 0.0;
        } else if (big_message_.remaining_seconds <= 1.0e-9) {
            big_message_.text.clear();
            big_message_.age_seconds = 0.0;
        }
    }
    if (!help_lines_.empty() && help_open_) {
        if (help_delay_ > 0.0) {
            help_delay_ -= fixed_dt;
            if (help_delay_ <= 0.0) {
                pending_sounds_.play_appear = true;
            }
        } else {
            help_transition_ = std::min(help_transition_ + transition_speed_percent, 100.0);
        }
    } else {
        help_transition_ = std::max(help_transition_ - transition_speed_percent, 0.0);
    }
}

GameHudSounds GameHudModel::take_sounds() noexcept {
    const auto sounds = pending_sounds_;
    pending_sounds_ = {};
    return sounds;
}

namespace {

/**
 * Converts a recovered retail rect into our window-pixel draw space.
 *
 * hud.pyd works in pyglet's bottom-left origin, so every recovered y counts up
 * from the window's bottom edge; our draw list counts down from the top.
 */
[[nodiscard]] ui::DrawRect to_window_rect(const hud_layout::RectF& rect,
                                           const ui::PixelExtent& window) noexcept {
    return ui::DrawRect{rect.x,
                        hud_layout::to_top_left_y(rect.y, rect.height, window),
                        rect.width, rect.height};
}

void append_score_line_text(ui::DrawList& list, std::string text,
                            double left, double baseline_y, double font_size,
                            double line_height, ui::ColorRgba8 color,
                            std::uint8_t text_alpha,
                            std::uint8_t stroke_alpha,
                            std::uint32_t context_opacity) {
    const auto opacity = static_cast<std::uint16_t>(
        std::min(context_opacity, 1'000U));
    const auto make_command = [&](ui::ColorRgba8 tint, bool outline) {
        auto command = ui::TextDrawCommand{
            text,
            std::string{game_hud_assets::help_font},
            ui::DrawRect{left, baseline_y, 320.0, line_height},
            ui::DrawSpace::window_pixels,
            font_size,
            0.0,
            1U,
            ui::HorizontalTextAlignment::left,
            ui::VerticalTextAlignment::baseline,
            ui::TextTransform::preserve,
            ui::TextFit::none,
            ui::ColorModulation{tint, 1'000U, opacity},
        };
        command.retail_outline_stroke = outline;
        return command;
    };

    // Font.draw_multi_stroke first renders the FTTextureFont created by
    // resize_font(stroke=True), then the normal fill at the same baseline.
    // This is the recovered 140/64px outside FreeType stroke path, not
    // text.py's separate eight-neighbour Label.draw_stroked helper.
    auto stroke = score_stroke_color;
    stroke.alpha = stroke_alpha;
    list.push(make_command(stroke, true));
    color.alpha = text_alpha;
    list.push(make_command(color, false));
}

} // namespace

ui::DrawList GameHudPresentation::build(const GameHudModel& model,
                                        const GameHudPresentationContext& context) const {
    const double window_width = static_cast<double>(context.window.width);
    const double window_height = static_cast<double>(context.window.height);
    ui::DrawList list;
    list.reserve(100U + model.help_lines().size() +
                 model.kill_feed().size() * 19U);
    if (window_width <= 0.0 || window_height <= 0.0) {
        return list;
    }

    // Retail draws this before every ordinary HUD component. The authored
    // alpha-block texture and recovered alpha=150 tint the 3D scene while the
    // crosshair, minimap and health remain crisp above it.
    if (const auto tint = model.inside_zone_tint(); tint.has_value()) {
        list.push(window_sprite(
            game_hud_assets::inside_zone_tint, 0.0, 0.0,
            window_width, window_height,
            ui::ColorModulation{*tint, 1'000U, 1'000U}));
    }

    if (model.hud_visible()) {
        // aoslib/text.py:draw_big_text is shared by CHAT_BIG and the local
        // VIP banner. It stretches the centre-anchored frame to the shaped
        // width, keeps its authored 58px height per source line, and then
        // calls Label.draw_shadowed through draw_offset. Keeping one native
        // path prevents the two source-identical widgets from drifting apart.
        const auto append_big_text =
            [&list, &context, window_width, window_height](
                std::string_view source_text, double anchor_x,
                double anchor_bottom, ui::ColorRgba8 foreground_color) {
                if (source_text.empty()) {
                    return;
                }
                const auto measure = [&context](std::string_view line) {
                    if (context.measure_big_text) {
                        return context.measure_big_text(line, big_text_font_size);
                    }
                    return static_cast<double>(line.size()) * 16.0;
                };
                std::vector<std::string> lines;
                const auto append_wrapped = [&lines, &measure, window_width](
                                                std::string_view source) {
                    if (measure(source) + 60.0 < window_width) {
                        lines.emplace_back(source);
                        return;
                    }
                    std::string current;
                    std::size_t start{};
                    while (start <= source.size()) {
                        const auto space = source.find(' ', start);
                        const auto word = source.substr(
                            start, space == std::string_view::npos
                                       ? source.size() - start
                                       : space - start);
                        const auto candidate =
                            current.empty() ? std::string{word}
                                            : current + ' ' + std::string{word};
                        if (!current.empty() &&
                            measure(candidate) + 60.0 >= window_width) {
                            lines.push_back(std::move(current));
                            current = std::string{word};
                        } else {
                            current = candidate;
                        }
                        if (space == std::string_view::npos) break;
                        start = space + 1U;
                    }
                    if (!current.empty()) lines.push_back(std::move(current));
                };
                std::size_t start{};
                while (start <= source_text.size()) {
                    const auto newline = source_text.find('\n', start);
                    append_wrapped(source_text.substr(
                        start, newline == std::string_view::npos
                                   ? source_text.size() - start
                                   : newline - start));
                    if (newline == std::string_view::npos) break;
                    start = newline + 1U;
                }
                if (lines.empty()) lines.emplace_back();
                double widest{};
                std::size_t non_empty_line_count{};
                for (std::size_t index{}; index < lines.size(); ++index) {
                    widest = std::max(widest, measure(lines[index]));
                    non_empty_line_count += lines[index].empty() ? 0U : 1U;
                }
                // Retail captures scale_y before it removes empty lines from
                // line_count. Preserve both counts: empty source lines still
                // stretch the authored frame, but do not shift frame_y.
                const auto scale_line_count = lines.size();
                const auto positioned_line_count =
                    static_cast<double>(non_empty_line_count);
                const auto frame_center_bottom =
                    anchor_bottom - 19.5 * positioned_line_count + 10.0;
                const auto frame_width = widest + big_text_frame_padding;
                const auto frame_height =
                    big_text_frame_height *
                    static_cast<double>(scale_line_count);
                list.push(window_sprite(
                    game_hud_assets::big_text_frame,
                    anchor_x - frame_width * 0.5,
                    window_height - frame_center_bottom - frame_height * 0.5,
                    frame_width, frame_height));

                const auto append_label =
                    [&list, &lines, anchor_x, anchor_bottom, window_height](
                        double x_offset, double y_offset, ui::ColorRgba8 color) {
                        // Label has anchor_x/anchor_y='center'. Its y is a
                        // baseline: the first line starts half a retail line
                        // below the bottom-origin anchor and subsequent lines
                        // descend by exactly Font.get_line_height(). Emit one
                        // command per line so neither the rasterizer nor a box
                        // alignment approximation can shift the announcement.
                        for (std::size_t index{}; index < lines.size(); ++index) {
                            if (lines[index].empty()) {
                                continue;
                            }
                            const auto baseline_top =
                                window_height - anchor_bottom +
                                big_text_line_advance * 0.5 +
                                static_cast<double>(index) * big_text_line_advance;
                            list.push(ui::TextDrawCommand{
                                lines[index],
                                std::string{game_hud_assets::big_text_font},
                                ui::DrawRect{anchor_x + x_offset,
                                             baseline_top + y_offset, 0.0, 0.0},
                                ui::DrawSpace::window_pixels,
                                big_text_font_size,
                                0.0,
                                1U,
                                ui::HorizontalTextAlignment::center,
                                ui::VerticalTextAlignment::baseline,
                                ui::TextTransform::preserve,
                                ui::TextFit::none,
                                ui::ColorModulation{color, 1'000U, 1'000U},
                            });
                        }
                    };
                // Label.draw_shadowed defaults to (64,64,64, foreground alpha)
                // at (+2,-2) in bottom-origin space, then draws foreground.
                append_label(
                    2.0, 2.0,
                    ui::ColorRgba8{64U, 64U, 64U, foreground_color.alpha});
                append_label(0.0, 0.0, foreground_color);
            };

        // Retail gates the crosshair on the equipped tool's visibility enum
        // and an empty loadout draws none, so the movement lessons run
        // crosshair-free; the weapon milestone flips this on and adds the
        // accuracy-driven corner spread. Every authored image remains 16x16:
        // the centre stays on the window centre while the four corner anchors
        // move by the weapon's live accuracy radius.
        if (model.crosshair_visible()) {
            const double center_left = window_width * 0.5 - crosshair_size * 0.5;
            const double center_top = window_height * 0.5 - crosshair_size * 0.5;
            const double radius = model.crosshair_radius_pixels();
            const ui::ColorModulation crosshair_modulation{
                model.crosshair_color(), 1'000U, 1'000U};
            list.push(window_sprite(game_hud_assets::crosshair_top_left,
                                    center_left - radius, center_top - radius,
                                    crosshair_size, crosshair_size,
                                    crosshair_modulation));
            list.push(window_sprite(game_hud_assets::crosshair_top_right,
                                    center_left + radius, center_top - radius,
                                    crosshair_size, crosshair_size,
                                    crosshair_modulation));
            list.push(window_sprite(game_hud_assets::crosshair_bottom_left,
                                    center_left - radius, center_top + radius,
                                    crosshair_size, crosshair_size,
                                    crosshair_modulation));
            list.push(window_sprite(game_hud_assets::crosshair_bottom_right,
                                    center_left + radius, center_top + radius,
                                    crosshair_size, crosshair_size,
                                    crosshair_modulation));
            if (model.crosshair_center_visible()) {
                list.push(window_sprite(game_hud_assets::crosshair_centre,
                                        center_left, center_top,
                                        crosshair_size, crosshair_size,
                                        crosshair_modulation));
            }
        }

        if (!model.score_award().lines.empty()) {
            // HUD.draw translates to (W/2 + 30, H/2 - 20) in pyglet's
            // bottom-origin space. ScoreLine.draw passes that exact point to
            // Font.draw_multi_stroke, so both its outline and fill must share
            // one top-origin native baseline.
            const double left = window_width * 0.5 + 30.0;
            double baseline_y_up = window_height * 0.5 - 20.0;
            for (const auto& line : model.score_award().lines) {
                const double font_size =
                    line.title ? score_title_font_size : score_item_font_size;
                const double line_height =
                    line.title ? score_title_line_height : score_item_line_height;
                if (line.can_draw()) {
                    const auto fade = std::clamp(
                        line.ttl_seconds / score_fade_time, 0.0, 1.0);
                    if (fade > 0.0) {
                        const auto text_alpha = static_cast<std::uint8_t>(
                            std::clamp(static_cast<int>(score_full_alpha * fade),
                                       0, static_cast<int>(score_full_alpha)));
                        const auto stroke_alpha = static_cast<std::uint8_t>(
                            std::clamp(static_cast<int>(score_full_alpha * fade * fade),
                                       0, static_cast<int>(score_full_alpha)));
                        const auto baseline_y_down =
                            window_height - baseline_y_up;
                        append_score_line_text(
                            list,
                            line.title
                                ? format_score_delta(model.score_award().displayed_delta)
                                : line.text,
                            left, baseline_y_down, font_size, line_height,
                            line.title ? score_title_color : model.team_color(),
                            text_alpha, stroke_alpha,
                            context.opacity_per_mille);
                    }
                }
                // The outer HUD loop advances even when ScoreLine.can_draw is
                // false, leaving delayed reasons in their final retail row.
                baseline_y_up -= line_height + score_message_pad * 2.0;
            }
        }

        // HUD.draw iterates the local character's recent hits, rotates the
        // centre-anchored 658x952 indicator by character.hit_direction, and
        // fades linearly over 1.2 seconds. Our y-down UI projection makes a
        // positive mathematical vertex rotation clockwise on screen, exactly
        // matching pyglet Sprite.rotation.
        for (const auto& indicator : model.damage_indicators()) {
            const auto fade =
                std::clamp(indicator.remaining_seconds / damage_indicator_ttl,
                           0.0, 1.0);
            if (fade <= 0.0) {
                continue;
            }
            const auto opacity = static_cast<std::uint16_t>(
                std::clamp(std::lround(fade * 1'000.0), 0L, 1'000L));
            list.push(window_sprite(
                game_hud_assets::damage_indicator,
                window_width * 0.5 - damage_indicator_width * 0.5,
                window_height * 0.5 - damage_indicator_height * 0.5,
                damage_indicator_width, damage_indicator_height,
                ui::ColorModulation{
                    ui::ColorRgba8{255U, 255U, 255U, 255U},
                    1'000U, opacity},
                ui::TextureFilter::linear, indicator.rotation_degrees));
        }

        if (model.minimap().visible &&
            !model.minimap().texture_asset.empty()) {
            const auto& minimap = model.minimap();
            const auto focus_x = std::clamp(
                std::trunc(minimap.focus_x), minimap_half,
                minimap_edge - minimap_half);
            const auto focus_y = std::clamp(
                std::trunc(minimap.focus_y), minimap_half,
                minimap_edge - minimap_half);
            const bool full = minimap.full_map_visible;
            const auto map_size = full ? full_map_size : minimap_size;
            const auto map_left =
                full ? window_width * 0.5 - full_map_size * 0.5
                     : window_width - minimap_size - minimap_margin;
            const auto map_top =
                full ? window_height * 0.5 - full_map_size * 0.5
                     : minimap_margin;
            const ui::DrawRect minimap_clip{
                map_left, map_top, map_size, map_size};
            if (full) {
                list.push(window_sprite(
                    game_hud_assets::full_map_frame,
                    window_width * 0.5 - full_map_frame_size * 0.5,
                    window_height * 0.5 - full_map_frame_size * 0.5,
                    full_map_frame_size, full_map_frame_size));
            } else {
                list.push(window_sprite(
                    game_hud_assets::minimap_frame,
                    map_left - minimap_frame_border,
                    map_top - minimap_frame_border,
                    minimap_frame_size, minimap_frame_size));
            }
            auto map_command = window_sprite(
                minimap.texture_asset, map_left, map_top, map_size, map_size);
            map_command.source_pixels =
                full ? std::optional<ui::DrawRect>{
                           ui::DrawRect{0.0, 0.0, minimap_edge, minimap_edge}}
                     : std::optional<ui::DrawRect>{
                           ui::DrawRect{focus_x - minimap_half,
                                        focus_y - minimap_half,
                                        minimap_size, minimap_size}};
            list.push(std::move(map_command));

            if (full) {
                const ui::ColorModulation grid_modulation{
                    ui::ColorRgba8{255U, 255U, 255U, 255U},
                    1'000U, full_map_grid_opacity};
                // Retail emits the vertical xrange first, including the
                // right-edge strip at x0+512.
                for (double offset{}; offset <= full_map_size;
                     offset += full_map_grid_step) {
                    list.push(window_sprite(
                        game_hud_assets::white_pixel,
                        map_left + offset, map_top, 1.0, full_map_size,
                        grid_modulation));
                }
                // Its bottom-origin loop is xrange(512, -64, -64) and uses
                // (y, y-1).  Converted to our top-left coordinates this is
                // the same 0..512 progression, with the last strip one pixel
                // below the map content exactly as shipped.
                for (double offset{}; offset <= full_map_size;
                     offset += full_map_grid_step) {
                    list.push(window_sprite(
                        game_hud_assets::white_pixel,
                        map_left, map_top + offset, full_map_size, 1.0,
                        grid_modulation));
                }
            }

            const auto view_left = full ? 0.0 : focus_x - minimap_half;
            const auto view_top = full ? 0.0 : focus_y - minimap_half;
            const auto view_right = view_left + map_size;
            const auto view_bottom = view_top + map_size;
            const auto zone_alpha =
                (std::sin(minimap.zone_phase_radians) * 0.4 + 0.6) * 0.5;
            const auto zone_icon_pulse =
                std::sin(minimap.zone_phase_radians * 1.5) * 0.4 + 0.6;
            for (const auto& zone : minimap.zones) {
                if (!std::isfinite(zone.world_x1) ||
                    !std::isfinite(zone.world_y1) ||
                    !std::isfinite(zone.world_x2) ||
                    !std::isfinite(zone.world_y2)) {
                    continue;
                }
                const auto left = std::min(zone.world_x1, zone.world_x2);
                const auto top = std::min(zone.world_y1, zone.world_y2);
                const auto right = std::max(zone.world_x1, zone.world_x2);
                const auto bottom = std::max(zone.world_y1, zone.world_y2);
                const auto clipped_left = std::max(left, view_left);
                const auto clipped_top = std::max(top, view_top);
                const auto clipped_right = std::min(right, view_right);
                const auto clipped_bottom = std::min(bottom, view_bottom);
                if (clipped_right > clipped_left &&
                    clipped_bottom > clipped_top) {
                    list.push(window_sprite(
                        game_hud_assets::white_pixel,
                        map_left + clipped_left - view_left,
                        map_top + clipped_top - view_top,
                        clipped_right - clipped_left,
                        clipped_bottom - clipped_top,
                        ui::ColorModulation{
                            zone.color, 1'000U,
                            static_cast<std::uint16_t>(std::clamp(
                                std::lround(zone_alpha * 1'000.0), 0L,
                                1'000L))}));
                }
                const auto centre_x = (left + right) * 0.5;
                const auto centre_y = (top + bottom) * 0.5;
                const auto icon_size = zone.icon_source_pixels *
                                       zone.icon_scale * zone_icon_pulse;
                if (zone.icon_asset.empty() || !std::isfinite(icon_size) ||
                    icon_size <= 0.0 || centre_x < view_left ||
                    centre_x > view_right || centre_y < view_top ||
                    centre_y > view_bottom) {
                    continue;
                }
                auto zone_icon = window_sprite(
                    zone.icon_asset,
                    map_left + centre_x - view_left - icon_size * 0.5,
                    map_top + centre_y - view_top - icon_size * 0.5,
                    icon_size, icon_size,
                    ui::ColorModulation{zone.color, 1'000U, 1'000U},
                    ui::TextureFilter::linear);
                zone_icon.clip_pixels = minimap_clip;
                list.push(std::move(zone_icon));
            }

            const auto append_marker =
                [&list, full, map_left, map_top, focus_x, focus_y,
                 minimap_clip](
                    const GameHudMinimapMarker& marker) {
                    if (marker.icon_asset.empty() ||
                        !std::isfinite(marker.world_x) ||
                        !std::isfinite(marker.world_y) ||
                        !std::isfinite(marker.size_pixels) ||
                        marker.size_pixels <= 0.0) {
                        return;
                    }
                    auto relative_x =
                        full ? marker.world_x
                             : marker.world_x - focus_x + minimap_half;
                    auto relative_y =
                        full ? marker.world_y
                             : marker.world_y - focus_y + minimap_half;
                    if (!full &&
                        (relative_x < 0.0 || relative_x > minimap_size ||
                         relative_y < 0.0 || relative_y > minimap_size)) {
                        if (!marker.clamp_out_of_bounds) {
                            return;
                        }
                        relative_x =
                            std::clamp(relative_x, 0.0, minimap_size);
                        relative_y =
                            std::clamp(relative_y, 0.0, minimap_size);
                    }
                    auto marker_command = window_sprite(
                        marker.icon_asset,
                        map_left + relative_x - marker.size_pixels * 0.5,
                        map_top + relative_y - marker.size_pixels * 0.5,
                        marker.size_pixels, marker.size_pixels,
                        ui::ColorModulation{marker.color, 1'000U, 1'000U},
                        ui::TextureFilter::linear,
                        marker.rotation_degrees);
                    marker_command.clip_pixels = minimap_clip;
                    list.push(std::move(marker_command));
                };
            // Retail draws the focus cone immediately before the player batch.
            if (minimap.view_cone.has_value()) {
                append_marker(*minimap.view_cone);
            }
            for (const auto& marker : minimap.markers) {
                append_marker(marker);
            }
        }

        if (model.intel_carried()) {
            // draw_intel_hud uses centre-anchored 90px art at
            // (window.width-80, window.height-250) with the minimap, or
            // (..., window.height-100) without it, in bottom-origin pyglet
            // coordinates. Green carries blue intel and vice versa.
            constexpr double intel_size{90.0};
            constexpr double intel_centre_right_inset{80.0};
            const auto top =
                (model.minimap().visible ? 250.0 : 100.0) -
                intel_size * 0.5;
            const auto asset = model.intel_carrier_team() == 3U
                                   ? game_hud_assets::intel_blue
                                   : game_hud_assets::intel_green;
            list.push(window_sprite(
                asset,
                window_width - intel_centre_right_inset -
                    intel_size * 0.5,
                top, intel_size, intel_size));
        }

        // The frame is a fully opaque dark backdrop, so it draws first and
        // the team-tinted fill sits on top; the fill's transparent 35px
        // leading inset leaves the frame border visible. hp<100 later scales
        // the fill about that retail 35px anchor.
        const auto health_frame = to_window_rect(
            hud_layout::health_bar_frame(context.window), context.window);
        const double health_left = health_frame.x;
        const double health_top = health_frame.y;
        list.push(window_sprite(game_hud_assets::health_bar_frame, health_left, health_top,
                                health_frame.width, health_frame.height));
        // draw_healthbar scales the fill about health_bar.anchor_x = 35 rather
        // than clipping it, so a wounded bar shrinks toward that inset and the
        // frame border stays visible on both sides.
        const auto fill = to_window_rect(
            hud_layout::health_bar_fill(context.window,
                                        static_cast<double>(model.health()) / 100.0),
            context.window);
        if (fill.width > 0.0) {
            list.push(window_sprite(
                game_hud_assets::health_bar, fill.x, fill.y, fill.width, fill.height,
                ui::ColorModulation{model.team_color(), 1'000U,
                                    1'000U}));
        }
        // The per-class head sits beside the bar, overlapping its left end.
        // Retail picks class_icons[player.get_class().id][player.team.id], and
        // our class catalog already carries both team variants. The whole block
        // is gated on scene.manager.enable_player_score (hud.pyx:1031) -- the
        // same switch as the SCORE box -- so the two appear together.
        if (model.player_score_visible() && !model.class_portrait_asset().empty()) {
            const auto portrait = to_window_rect(
                hud_layout::class_portrait(context.window,
                                           model.class_portrait_highly_visible()),
                context.window);
            list.push(window_sprite(model.class_portrait_asset(), portrait.x, portrait.y,
                                    portrait.width, portrait.height));
        }

        // HUD.draw_healthbar lines 1049-1054 render this independently from
        // enable_player_score. The server's high_minimap_visibility bit is the
        // predicate, and the active team's exact StateData colour replaces the
        // ordinary orange-red BIG_TEXT_COLOR. vip_text_offset is 60.0, making
        // draw_big_text's bottom-origin anchor y exactly 30 + 60 = 90.
        if (model.class_portrait_highly_visible()) {
            append_big_text("You are a V.I.P! Stay safe!",
                            window_width * 0.5, 90.0, model.team_color());
        }

        // InitialInfo.enable_numeric_hp gates this label only. Retail still
        // draws the frame, fill, and (independently gated) class portrait.
        if (model.numeric_health_visible()) {
            const auto passes = hud_layout::health_number_draw_passes(context.window);
            const auto foreground = model.health_text_color();
            const ui::ColorRgba8 shadow{64U, 64U, 64U, foreground.alpha};
            const auto append_health_number =
                [&list, &context, &model](const hud_layout::RectF& anchor,
                                          ui::ColorRgba8 color) {
                    // Retail Label(anchor_y='center') moves its *baseline* down
                    // by half Font.get_line_height(). A generic centred box
                    // instead centres the font's line rectangle and placed the
                    // visible number several pixels too high in the bar.
                    list.push(ui::TextDrawCommand{
                        std::to_string(model.displayed_health()),
                        std::string{game_hud_assets::help_font},
                        ui::DrawRect{
                            anchor.x,
                            hud_layout::to_top_left_y(anchor.y, 0.0,
                                                     context.window) +
                                health_line_height * 0.5,
                            0.0,
                            0.0,
                        },
                        ui::DrawSpace::window_pixels,
                        hud_layout::health_font_pixels,
                        0.0,
                        1U,
                        ui::HorizontalTextAlignment::center,
                        ui::VerticalTextAlignment::baseline,
                        ui::TextTransform::preserve,
                        ui::TextFit::none,
                        ui::ColorModulation{color, 1'000U, 1'000U},
                    });
                };
            // HUD.draw_healthbar calls draw(), then draw_offset(draw_shadowed).
            // draw_shadowed itself emits its shadow before its foreground.
            append_health_number(passes.foreground, foreground);
            append_health_number(passes.shadow, shadow);
            append_health_number(passes.offset_foreground, foreground);
        }

        // The two recovered lower-right widgets are permanent gameplay HUD,
        // independent of the transient mouse-wheel inventory strip.
        const auto ammo_rect =
            to_window_rect(hud_layout::ammo_panel(context.window), context.window);
        const double ammo_left = ammo_rect.x;
        const double ammo_top = ammo_rect.y;
        const auto panel_text_width =
            [&context](std::string_view text, double font_size) {
                if (context.measure_text) {
                    return context.measure_text(text, font_size);
                }
                return static_cast<double>(text.size()) * font_size * 0.55;
            };
        const auto append_panel_text =
            [&list, &context, &panel_text_width](
                std::string current, std::optional<std::string> reserve,
                double panel_top, ui::ColorRgba8 current_color) {
                constexpr double group_centre_offset{17.0};
                constexpr double pair_gap{2.0};
                constexpr double current_font_size{26.0};
                constexpr double reserve_font_size{18.0};

                const auto current_width =
                    panel_text_width(current, current_font_size);
                const auto reserve_width =
                    reserve.has_value()
                        ? panel_text_width(*reserve, reserve_font_size)
                        : 0.0;
                const auto group_width =
                    current_width +
                    (reserve.has_value() ? pair_gap + reserve_width : 0.0);
                const auto group_left =
                    hud_layout::ammo_anchor_x(context.window) +
                    group_centre_offset - group_width * 0.5;

                // Both fonts are drawn at the same bottom-origin y+11
                // baseline in HUD.draw_ammo_hud. The 1.3 transform applies to
                // x only, so do not vertically scale or independently centre
                // either value inside guessed rectangles.
                const auto baseline_top =
                    panel_top + ammo_frame_height - 11.0;
                list.push(ui::TextDrawCommand{
                    std::move(current),
                    std::string{game_hud_assets::help_font},
                    ui::DrawRect{group_left, baseline_top, current_width, 0.0},
                    ui::DrawSpace::window_pixels,
                    current_font_size,
                    0.0,
                    1U,
                    ui::HorizontalTextAlignment::left,
                    ui::VerticalTextAlignment::baseline,
                    ui::TextTransform::preserve,
                    ui::TextFit::none,
                    ui::ColorModulation{current_color, 1'000U, 1'000U},
                });
                if (reserve.has_value()) {
                    list.push(ui::TextDrawCommand{
                        std::move(reserve.value()),
                        std::string{game_hud_assets::help_font},
                        ui::DrawRect{group_left + current_width + pair_gap,
                                     baseline_top, reserve_width, 0.0},
                        ui::DrawSpace::window_pixels,
                        reserve_font_size,
                        0.0,
                        1U,
                        ui::HorizontalTextAlignment::left,
                        ui::VerticalTextAlignment::baseline,
                        ui::TextTransform::preserve,
                        ui::TextFit::none,
                        ui::ColorModulation{
                            ui::ColorRgba8{255U, 255U, 255U, 255U},
                            1'000U, 1'000U},
                    });
                }
            };
        if (model.ammo().visible) {
            const auto panel_icon_size = 330.0 * model.ammo().image_scale;
            list.push(window_sprite(game_hud_assets::ammo_frame, ammo_left, ammo_top,
                                    ammo_rect.width, ammo_rect.height));
            list.push(window_sprite(
                model.ammo().image_asset,
                hud_layout::ammo_anchor_x(context.window) - 50.0 -
                    panel_icon_size * 0.5,
                ammo_top + ammo_rect.height * 0.5 - panel_icon_size * 0.5,
                panel_icon_size, panel_icon_size,
                model.ammo().image_color.has_value()
                    ? ui::ColorModulation{*model.ammo().image_color, 1'000U, 1'000U}
                    : ui::ColorModulation{}));
            append_panel_text(
                std::to_string(model.ammo().current),
                model.ammo().reserve.has_value()
                    ? std::optional<std::string>{
                          "/ " + std::to_string(*model.ammo().reserve)}
                    : std::nullopt,
                ammo_top,
                model.ammo().enough
                    ? hud_layout::enough_ammo_color
                    : hud_layout::not_enough_ammo_color);
        }

        // The blocks readout is the same draw_ammo_hud panel at y = 12, so it
        // reads directly beneath the weapon ammo at the identical x. Its text
        // colour is the ammo threshold pair, NOT white: red at zero blocks and
        // yellow above it (draw_tools_hud hud.pyx:829/831).
        if (model.blocks().visible) {
            const auto blocks_rect =
                to_window_rect(hud_layout::blocks_panel(context.window), context.window);
            list.push(window_sprite(game_hud_assets::ammo_frame, blocks_rect.x,
                                    blocks_rect.y, blocks_rect.width,
                                    blocks_rect.height));
            if (!model.blocks().icon_asset.empty()) {
                list.push(window_sprite(
                    model.blocks().icon_asset,
                    hud_layout::ammo_anchor_x(context.window) - 50.0 -
                        ammo_icon_size * 0.5,
                    blocks_rect.y + blocks_rect.height * 0.5 - ammo_icon_size * 0.5,
                    ammo_icon_size, ammo_icon_size,
                    ui::ColorModulation{
                        model.team_color(), 1'000U, 1'000U}));
            }
            append_panel_text(
                std::to_string(model.blocks().count),
                "/ " + std::to_string(model.blocks().reserve),
                blocks_rect.y,
                hud_layout::block_count_color(model.blocks().count));
        }

        // The vertical yellow-to-red gauge above the lower-right tool icon is
        // the JETPACK FUEL bar, not health and not weapon heat:
        // draw_jetpack_hud returns immediately when the class has no jetpack,
        // which is why most loadouts never show it.
        if (model.jetpack_visible()) {
            const auto gauge =
                to_window_rect(hud_layout::jetpack_gauge(context.window), context.window);
            list.push(window_sprite(game_hud_assets::jetpack_fuel_frame, gauge.x, gauge.y,
                                    gauge.width, gauge.height));
            const auto fuel = to_window_rect(
                hud_layout::jetpack_fill(context.window, model.jetpack_fuel()),
                context.window);
            if (fuel.height > 0.0) {
                // jetpack_fuel_bar.anchor_y = 0, so the fill grows upward from
                // the gauge's base as fuel is regained.
                list.push(window_sprite(game_hud_assets::jetpack_fuel_bar, fuel.x, fuel.y,
                                        fuel.width, fuel.height));
            }
        }

        // draw_disgusie_hud (retail typo preserved in its symbol) and
        // draw_parachute_hud use identical centered TOOL_IMAGES geometry.
        // Keep retail order: parachute is submitted after disguise.
        const auto equipment = to_window_rect(
            hud_layout::active_equipment_icon(context.window), context.window);
        if (model.disguise_active()) {
            list.push(window_sprite(game_hud_assets::disguise_status,
                                    equipment.x, equipment.y,
                                    equipment.width, equipment.height));
        }
        if (model.parachute_active()) {
            list.push(window_sprite(game_hud_assets::parachute_status,
                                    equipment.x, equipment.y,
                                    equipment.width, equipment.height));
        }

        // TeamProgressBar is mode-owned and separate from HeadCount. Its
        // `draw_team_level_bar` helper exists in the retail binary but has no
        // runtime caller: the shipping draw path is frame + icons + labels.
        const auto& progress = model.team_progress();
        const auto team1_progress = std::ranges::find_if(
            progress.entries, [](const GameHudTeamProgressEntry& entry) {
                return entry.team == hud_layout::Team::team1;
            });
        const auto team2_progress = std::ranges::find_if(
            progress.entries, [](const GameHudTeamProgressEntry& entry) {
                return entry.team == hud_layout::Team::team2;
            });
        const auto neutral_progress = std::ranges::find_if(
            progress.entries, [](const GameHudTeamProgressEntry& entry) {
                return entry.team == hud_layout::Team::neutral;
            });
        const bool draw_progress_background =
            (team1_progress != progress.entries.end() &&
             team2_progress != progress.entries.end() &&
             team1_progress->visible && team2_progress->visible) ||
            (neutral_progress != progress.entries.end() &&
             neutral_progress->visible);
        if (draw_progress_background) {
            const auto metrics = hud_layout::team_progress(
                context.window, hud_layout::Team::team1,
                model.team_scores_visible());
            const auto frame = to_window_rect(metrics.background, context.window);
            list.push(window_sprite(game_hud_assets::head_count_frame,
                                    frame.x, frame.y, frame.width, frame.height));
        }

        for (const auto& entry : progress.entries) {
            if (!entry.visible) {
                continue;
            }
            const auto metrics = hud_layout::team_progress(
                context.window, entry.team, model.team_scores_visible());
            const auto icon = to_window_rect(metrics.icon, context.window);
            const auto icon_asset =
                entry.icon == GameHudTeamProgressIcon::diamond
                    ? game_hud_assets::team_progress_diamond
                    : game_hud_assets::team_progress_base;
            const auto color =
                entry.color.value_or(hud_layout::team_color(entry.team));
            list.push(window_sprite(
                icon_asset, icon.x, icon.y, icon.width, icon.height,
                ui::ColorModulation{color, 1'000U, 1'000U}));

            const auto displayed = static_cast<long long>(
                std::ceil(entry.current));
            auto text = std::to_string(displayed);
            if (progress.show_as_percent) {
                text += "%";
            } else {
                text += "/" + std::to_string(static_cast<long long>(
                                  progress.maximum));
            }

            const auto make_progress_text =
                [&text, &metrics, window_height](
                    double x_offset, double y_offset,
                    ui::ColorRgba8 text_color) {
                    // Label's centre anchor is applied to its baseline, not to
                    // a generic line box. Preserve that exact transform.
                    return ui::TextDrawCommand{
                        text,
                        std::string{game_hud_assets::help_font},
                        ui::DrawRect{
                            metrics.label_x + x_offset -
                                team_progress_label_width * 0.5,
                            window_height - (metrics.label_y + y_offset) +
                                team_progress_line_height * 0.5,
                            team_progress_label_width,
                            0.0},
                        ui::DrawSpace::window_pixels,
                        team_progress_font_size,
                        0.0,
                        1U,
                        ui::HorizontalTextAlignment::center,
                        ui::VerticalTextAlignment::baseline,
                        ui::TextTransform::preserve,
                        ui::TextFit::none,
                        ui::ColorModulation{text_color, 1'000U, 1'000U},
                    };
                };
            list.push(make_progress_text(0.0, 0.0, color));
            list.push(make_progress_text(
                2.2, -2.2,
                ui::ColorRgba8{team_progress_shadow_color.red,
                               team_progress_shadow_color.green,
                               team_progress_shadow_color.blue,
                               color.alpha}));
            list.push(make_progress_text(0.2, -0.2, color));
        }

        // TerritoryBasesHud follows TeamProgressBar in retail HUD.draw. The
        // shipped BASE_X_INTERVAL is zero, so its general arrangement formula
        // resolves to a right-edge vertical stack at window.width - 70.
        const auto territory_count = static_cast<std::size_t>(
            std::ranges::count_if(
                model.territory_bases().bases,
                [](const auto& base) { return base.has_value(); }));
        for (const auto& retained : model.territory_bases().bases) {
            if (!retained.has_value() || !retained->visible) {
                continue;
            }
            const auto& base = *retained;
            const auto metrics = hud_layout::territory_base(
                context.window, territory_count, base.base_index,
                base.capture_amount, base.contains_player);
            const auto controlled =
                hud_layout::team_color(base.controlled_by);
            const auto attacked =
                hud_layout::team_color(base.attacked_by);
            const auto frame_color =
                base.contended
                    ? blend_territory_color(controlled,
                                            base.contend_alpha)
                    : controlled;

            const auto plate =
                to_window_rect(metrics.plate, context.window);
            list.push(window_sprite(
                game_hud_assets::territory_backplate,
                plate.x, plate.y, plate.width, plate.height,
                ui::ColorModulation{controlled, 1'000U, 1'000U}));

            const auto overlay =
                to_window_rect(metrics.attacked_overlay, context.window);
            if (overlay.width > 0.0) {
                // pyglet AbstractImage.blit(width=...) scales the full plate
                // into this width; it does not crop source pixels.
                list.push(window_sprite(
                    game_hud_assets::territory_backplate,
                    overlay.x, overlay.y, overlay.width, overlay.height,
                    ui::ColorModulation{attacked, 1'000U, 1'000U}));
            }

            const auto frame =
                to_window_rect(metrics.frame, context.window);
            list.push(window_sprite(
                game_hud_assets::territory_frame,
                frame.x, frame.y, frame.width, frame.height,
                ui::ColorModulation{frame_color, 1'000U, 1'000U}));

            const auto letter =
                to_window_rect(metrics.letter, context.window);
            list.push(window_sprite(
                game_hud_assets::territory_letters[base.base_index],
                letter.x, letter.y, letter.width, letter.height,
                ui::ColorModulation{frame_color, 1'000U, 1'000U}));
        }

        // Top-centre HeadCount bar: each team's score flanking the countdown.
        if (model.team_scores_visible()) {
            const auto format_score = [](const GameHudTeamScore& entry) {
                auto text = std::to_string(entry.score);
                if (entry.show_max_score) {
                    text += "/" + std::to_string(entry.max_score);
                }
                return text;
            };
            // calculate_startx picks the per-team width from the score text,
            // then adds it to ITSELF plus the timer column -- retail's own loop
            // only keeps the last team's width, so both halves match. With a
            // 3-digit max_score ("4/200") that is 110 + 110 + 120 = 340, which
            // is why hc_frame_long's native 320 is never the resize target.
            const auto& last = model.right_team_score();
            const double bar_width =
                hud_layout::head_count_bar_width(last.show_max_score, last.max_score);
            const auto metrics = hud_layout::head_count(context.window, bar_width);
            const double bar_top = hud_layout::to_top_left_y(metrics.y, 40.0,
                                                             context.window);
            list.push(window_sprite(game_hud_assets::head_count_frame, metrics.start_x,
                                    bar_top, bar_width, 40.0));
            // images.py pairs TEAM1 with head_2/head_colour_2 and TEAM2 with
            // head_1/head_colour_1. image.py first truncates 72x69 at the 0.64
            // global scale to 46x44; HeadCount then draws those at 0.5, exactly
            // 23x22. Their centres are W/2 +/- 60 and bottom-left y+10.
            // The face stays neutral; only the cap overlay receives StateData's
            // custom team colour.
            const double left_head_center =
                static_cast<double>(context.window.width) * 0.5 -
                hud_layout::hc_head_x_offset;
            const double right_head_center =
                static_cast<double>(context.window.width) * 0.5 +
                hud_layout::hc_head_x_offset;
            const double head_count_frame_top =
                static_cast<double>(context.window.height) - metrics.y -
                hud_layout::hc_frame_height;
            const double head_top =
                head_count_frame_top +
                (hud_layout::hc_frame_height - hud_layout::hc_head_height) * 0.5;
            const auto append_head = [&](double center_x,
                                         std::string_view face,
                                         std::string_view tint,
                                         const GameHudTeamScore& score) {
                const double left = center_x - hud_layout::hc_head_width * 0.5;
                list.push(window_sprite(face, left, head_top,
                                        hud_layout::hc_head_width,
                                        hud_layout::hc_head_height));
                list.push(window_sprite(
                    tint, left, head_top, hud_layout::hc_head_width,
                    hud_layout::hc_head_height,
                    ui::ColorModulation{
                        score.color.value_or(
                            hud_layout::team_color(score.team)),
                        1'000U, 1'000U}));
            };
            append_head(left_head_center,
                        game_hud_assets::head_count_team1_face,
                        game_hud_assets::head_count_team1_tint,
                        model.left_team_score());
            append_head(right_head_center,
                        game_hud_assets::head_count_team2_face,
                        game_hud_assets::head_count_team2_tint,
                        model.right_team_score());
            // The Cython helper and FTGL express vertical placement through a
            // source baseline. Replaying that number as a native baseline or
            // a smaller 30px box visibly drops the scores. Centre the glyph
            // metric span in the actual 40px backing, matching the rendered
            // retail strip. The horizontal 80px cells remain byte-for-byte
            // faithful to HeadCount.draw's asymmetric right/left placement.
            list.push(ui::TextDrawCommand{
                format_score(model.left_team_score()),
                std::string{game_hud_assets::help_font},
                ui::DrawRect{metrics.left_text_x, head_count_frame_top,
                             metrics.text_width, hud_layout::hc_frame_height},
                ui::DrawSpace::window_pixels,
                hud_layout::head_count_font_pixels,
                0.0,
                1U,
                ui::HorizontalTextAlignment::right,
                ui::VerticalTextAlignment::retail_center,
                ui::TextTransform::preserve,
                ui::TextFit::none,
                ui::ColorModulation{
                                    model.left_team_score().color.value_or(
                                        hud_layout::team_color(model.left_team_score().team)),
                                    1'000U, 1'000U},
            });
            list.push(ui::TextDrawCommand{
                format_score(model.right_team_score()),
                std::string{game_hud_assets::help_font},
                ui::DrawRect{metrics.right_text_x, head_count_frame_top,
                             metrics.text_width, hud_layout::hc_frame_height},
                ui::DrawSpace::window_pixels,
                hud_layout::head_count_font_pixels,
                0.0,
                1U,
                ui::HorizontalTextAlignment::left,
                ui::VerticalTextAlignment::retail_center,
                ui::TextTransform::preserve,
                ui::TextFit::none,
                ui::ColorModulation{
                                    model.right_team_score().color.value_or(
                                        hud_layout::team_color(model.right_team_score().team)),
                                    1'000U, 1'000U},
            });
        }

        if (model.match_clock_visible()) {
            const auto metrics = hud_layout::timer_metrics(
                context.window, model.match_clock_placement(),
                model.match_clock_minimap_enabled());
            if (metrics.draw_background) {
                const auto frame = to_window_rect(metrics.frame, context.window);
                list.push(window_sprite(game_hud_assets::timer_frame,
                                        frame.x, frame.y,
                                        frame.width, frame.height));
            }
            const auto clock_text =
                hud_layout::format_clock(model.match_clock_seconds());
            const bool top_centre_clock =
                model.match_clock_placement() ==
                hud_layout::TimerPlacement::head_count;
            const double clock_frame_top =
                static_cast<double>(context.window.height) -
                (metrics.frame.y + metrics.frame.height);
            const double clock_text_top =
                top_centre_clock
                    ? clock_frame_top
                    : static_cast<double>(context.window.height) -
                          metrics.foreground_baseline_y;
            const double clock_text_height =
                top_centre_clock ? metrics.frame.height : 0.0;
            const auto clock_vertical_alignment =
                top_centre_clock ? ui::VerticalTextAlignment::retail_center
                                 : ui::VerticalTextAlignment::baseline;
            // Retail draws the two-pixel black shadow first, then the white
            // foreground. The gameplay HeadCount clock is centred in the same
            // 40px backing as the team scores; the separate tutorial caller
            // keeps its recovered direct-baseline contract.
            list.push(ui::TextDrawCommand{
                clock_text,
                std::string{game_hud_assets::help_font},
                ui::DrawRect{
                    metrics.shadow_baseline_x,
                    top_centre_clock
                        ? clock_text_top + 2.0
                        : static_cast<double>(context.window.height) -
                              metrics.shadow_baseline_y,
                    0.0, clock_text_height},
                ui::DrawSpace::window_pixels,
                hud_layout::ammo_font_pixels,
                0.0,
                1U,
                ui::HorizontalTextAlignment::left,
                clock_vertical_alignment,
                ui::TextTransform::preserve,
                ui::TextFit::none,
                ui::ColorModulation{ui::ColorRgba8{0U, 0U, 0U, 255U}, 1'000U,
                                    1'000U},
            });
            list.push(ui::TextDrawCommand{
                clock_text,
                std::string{game_hud_assets::help_font},
                ui::DrawRect{
                    metrics.foreground_baseline_x,
                    clock_text_top,
                    0.0, clock_text_height},
                ui::DrawSpace::window_pixels,
                hud_layout::ammo_font_pixels,
                0.0,
                1U,
                ui::HorizontalTextAlignment::left,
                clock_vertical_alignment,
                ui::TextTransform::preserve,
                ui::TextFit::none,
                ui::ColorModulation{ui::ColorRgba8{255U, 255U, 255U, 255U},
                                    1'000U, 1'000U},
            });
            list.push(window_sprite(
                game_hud_assets::timer_icon,
                metrics.icon_center_x - metrics.icon_width * 0.5,
                top_centre_clock
                    ? clock_frame_top +
                          (metrics.frame.height - metrics.icon_height) * 0.5
                    : static_cast<double>(context.window.height) -
                          (metrics.icon_center_y + metrics.icon_height * 0.5),
                metrics.icon_width, metrics.icon_height));
        }

        if (model.player_score_visible()) {
            // score_frame is the one HUD frame loaded with center=False.
            // Its recovered call site supplies the window's top-left, not an
            // offset relative to the lower-right ammunition panel.
            // HUD.draw calls draw_player_score(MSG_LEFT_MARGIN, height - 48),
            // so the box is inset 12 px from the left and its 40 px frame ends
            // 48 px below the top edge -- not flush in the corner.
            const auto score_rect =
                to_window_rect(hud_layout::score_box(context.window), context.window);
            const double score_left = score_rect.x;
            const double score_top = score_rect.y;
            list.push(window_sprite(game_hud_assets::score_frame, score_left, score_top,
                                    score_rect.width, score_rect.height));
            list.push(ui::TextDrawCommand{
                "SCORE: " + std::to_string(model.player_score()),
                std::string{game_hud_assets::help_font},
                ui::DrawRect{score_left + 10.0, score_top, score_rect.width,
                             score_rect.height},
                ui::DrawSpace::window_pixels,
                hud_layout::score_font_pixels,
                0.0,
                1U,
                ui::HorizontalTextAlignment::left,
                ui::VerticalTextAlignment::retail_center,
                ui::TextTransform::preserve,
                ui::TextFit::retail_width_scale,
                ui::ColorModulation{ui::ColorRgba8{255U, 255U, 255U, 255U},
                                    1'000U, 1'000U},
            });
        }

        // Retail stores newest at index 0 but iterates reversed(), so the
        // oldest surviving row starts at (12, H-75) and the newest is lowest.
        double kill_row_offset{};
        for (auto entry = model.kill_feed().rbegin();
             entry != model.kill_feed().rend(); ++entry) {
            const auto alpha = std::clamp(
                entry->ttl_seconds / kill_feed_ttl * 2.0, 0.0, 1.0);
            if (alpha <= 0.0) {
                continue;
            }
            const auto opacity = static_cast<std::uint16_t>(
                std::clamp(std::lround(alpha * 1'000.0), 0L, 1'000L));
            const auto shadow_opacity = static_cast<std::uint16_t>(
                std::clamp(std::lround(alpha * alpha * 1'000.0), 0L, 1'000L));
            const auto text_width = [&context](std::string_view text) {
                if (context.measure_kill_feed_text) {
                    return context.measure_kill_feed_text(text, kill_feed_font_size);
                }
                return static_cast<double>(text.size()) * 7.0;
            };
            const auto draw_name = [&list, opacity, shadow_opacity](
                                       std::string_view name,
                                       ui::ColorRgba8 color,
                                       double x, double baseline,
                                       double width) {
                auto outline = ui::TextDrawCommand{
                    std::string{name},
                    std::string{game_hud_assets::kill_feed_font},
                    ui::DrawRect{x, baseline, width + 2.0, 18.0},
                    ui::DrawSpace::window_pixels,
                    kill_feed_font_size,
                    0.0,
                    1U,
                    ui::HorizontalTextAlignment::left,
                    ui::VerticalTextAlignment::baseline,
                    ui::TextTransform::preserve,
                    ui::TextFit::none,
                    ui::ColorModulation{
                        ui::ColorRgba8{0U, 0U, 0U, 255U},
                        1'000U, shadow_opacity},
                };
                outline.retail_outline_stroke = true;
                list.push(std::move(outline));
                list.push(ui::TextDrawCommand{
                    std::string{name},
                    std::string{game_hud_assets::kill_feed_font},
                    ui::DrawRect{x, baseline, width + 2.0, 18.0},
                    ui::DrawSpace::window_pixels,
                    kill_feed_font_size,
                    0.0,
                    1U,
                    ui::HorizontalTextAlignment::left,
                    ui::VerticalTextAlignment::baseline,
                    ui::TextTransform::preserve,
                    ui::TextFit::none,
                    ui::ColorModulation{color, 1'000U, opacity},
                });
            };

            const auto icon_size =
                entry->icon_asset.empty()
                    ? 0.0
                    : entry->icon_source_pixels * kill_feed_icon_scale;
            const auto content_height =
                std::max(retail_chat_line_height, icon_size * 0.5);
            const auto row_anchor = kill_feed_anchor_top + kill_row_offset;
            double x = hud_layout::message_left_margin;
            if (entry->killer_name.has_value()) {
                const auto width = text_width(*entry->killer_name);
                draw_name(*entry->killer_name, entry->killer_color,
                          x, row_anchor, width);
                x += width + kill_feed_name_padding;
            }
            if (!entry->icon_asset.empty()) {
                // Retail's center-anchored image is translated to y=6 in the
                // row's bottom-left coordinates and drawn at 0.1 scale.
                const auto icon_top =
                    row_anchor - kill_feed_icon_lift - icon_size * 0.5;
                list.push(window_sprite(
                    entry->icon_asset, x, icon_top, icon_size, icon_size,
                    ui::ColorModulation{
                        ui::ColorRgba8{255U, 255U, 255U, 255U},
                        1'000U, opacity}));
                x += icon_size + kill_feed_name_padding;
            }
            const auto victim_width = text_width(entry->victim_name);
            draw_name(entry->victim_name, entry->victim_color,
                      x, row_anchor, victim_width);
            kill_row_offset += content_height + hud_layout::message_pad * 2.0;
        }

        if (!model.big_message().text.empty() &&
            model.big_message().remaining_seconds > 0.0) {
            append_big_text(model.big_message().text, window_width * 0.5,
                            window_height * 0.8, big_text_color);
        }

        if (model.respawn().visible) {
            // HUD.draw calls draw_offset(respawn.draw_shadowed, W/2, H/2).
            // The Label is Spades/HUD_FONT at 40 px with both anchors=center,
            // white foreground and Label's default (64,64,64) shadow at
            // (+2,-2) in bottom-origin coordinates.
            const auto respawn_text =
                model.respawn().never_respawn
                    ? std::string{"No respawns!"}
                    : std::string{"Respawning in "} +
                          std::to_string(static_cast<std::int32_t>(
                              model.respawn().remaining_seconds));
            const auto append_respawn_text =
                [&list, &respawn_text, window_width, window_height](
                    double x_offset, double y_offset,
                    ui::ColorRgba8 color) {
                    list.push(ui::TextDrawCommand{
                        respawn_text,
                        std::string{game_hud_assets::help_font},
                        ui::DrawRect{x_offset,
                                     window_height * 0.5 +
                                         respawn_line_height * 0.5 + y_offset,
                                     window_width, 0.0},
                        ui::DrawSpace::window_pixels,
                        respawn_font_size,
                        0.0,
                        1U,
                        ui::HorizontalTextAlignment::center,
                        ui::VerticalTextAlignment::baseline,
                        ui::TextTransform::preserve,
                        ui::TextFit::none,
                        ui::ColorModulation{color, 1'000U, 1'000U},
                    });
                };
            append_respawn_text(2.0, 2.0, respawn_shadow_color);
            append_respawn_text(0.0, 0.0,
                                ui::ColorRgba8{255U, 255U, 255U, 255U});
        }

        // GameScene.on_mouse_scroll asks HUD.set_show_tool_loadout(True, 1.0)
        // and shows the selected authored-scale entry. Hotkey selection changes the
        // tool without opening this strip, which is the important retail
        // wheel-vs-number distinction.
        if (model.inventory_visible()) {
            const auto& slots = model.inventory_slots();
            const double count = static_cast<double>(slots.size());
            const double start_x = window_width * 0.5 - count * inventory_slot_stride * 0.5 +
                                   inventory_slot_stride * 0.5;
            const double center_y = window_height * 0.70;
            for (std::size_t index{}; index < slots.size(); ++index) {
                const bool selected = model.selected_inventory_slot() == index;
                const double frame_size =
                    selected ? inventory_frame_selected : inventory_frame_normal;
                const double icon_size =
                    selected ? inventory_icon_selected : inventory_icon_normal;
                const double center_x = start_x +
                                        static_cast<double>(index) * inventory_slot_stride;
                const double left = center_x - frame_size * 0.5;
                const double top = center_y - frame_size * 0.5;
                list.push(window_sprite(selected ? game_hud_assets::weapon_frame_selected
                                                 : game_hud_assets::weapon_frame,
                                        left, top, frame_size, frame_size));
                // Some selectable retail tools intentionally have no HUD
                // image. Class loadouts normally hide that fact, but the F4
                // all-tools sandbox can expose them. An empty sprite asset is
                // invalid renderer input, so keep the selection frame and
                // omit only the absent icon.
                if (!slots[index].icon_asset.empty()) {
                    list.push(window_sprite(slots[index].icon_asset,
                                            center_x - icon_size * 0.5,
                                            center_y - icon_size * 0.5,
                                            icon_size, icon_size,
                                            {}, ui::TextureFilter::nearest));
                }
                if (!slots[index].hotkey_label.empty()) {
                    // Retail does not keep the digit at one shared inset:
                    // the selected 0.7-scale bracket pushes it to the far
                    // lower-right, while normal entries keep it tight to the
                    // 1.3x icon. These offsets reproduce the captured pixel
                    // centers at both 800x600 and 1920x1080.
                    const double label_offset = selected
                                                    ? inventory_label_offset_selected
                                                    : inventory_label_offset_normal;
                    const double label_x = center_x + label_offset;
                    const double label_y = center_y + label_offset;
                    list.push(ui::TextDrawCommand{
                        slots[index].hotkey_label,
                        std::string{game_hud_assets::help_font},
                        ui::DrawRect{label_x, label_y, 18.0, 18.0},
                        ui::DrawSpace::window_pixels,
                        selected ? 20.0 : 11.0,
                        0.0,
                        1U,
                        ui::HorizontalTextAlignment::left,
                        ui::VerticalTextAlignment::top,
                        ui::TextTransform::preserve,
                        ui::TextFit::none,
                        ui::ColorModulation{ui::ColorRgba8{255U, 239U, 0U, 255U},
                                            1'000U, 1'000U},
                    });
                }
            }
        }

        if (model.palette().visible) {
            const auto& palette = model.palette();
            const auto columns = std::max<std::size_t>(1U, palette.columns);
            const auto rows =
                (palette.colors.size() + columns - 1U) / columns;
            const double stride = palette_cell_size + palette_cell_gap;
            const double width =
                static_cast<double>(columns) * stride - palette_cell_gap;
            const double height =
                static_cast<double>(rows) * stride - palette_cell_gap;
            const double right =
                palette.ugc_layout ? palette_ugc_padding : palette_right_padding;
            const double bottom =
                palette.ugc_layout ? palette_ugc_padding : palette_bottom_padding;
            const double origin_x = window_width - right - width;
            const double origin_y = window_height - bottom - height;
            for (std::size_t index{}; index < palette.colors.size(); ++index) {
                const auto column = index % columns;
                const auto row = index / columns;
                const double x = origin_x + static_cast<double>(column) * stride;
                const double y = origin_y + static_cast<double>(row) * stride;
                if (palette.selected == index) {
                    list.push(window_sprite(
                        game_hud_assets::white_pixel, x - palette_border,
                        y - palette_border,
                        palette_cell_size + palette_border * 2.0,
                        palette_cell_size + palette_border * 2.0,
                        ui::ColorModulation{
                            ui::ColorRgba8{255U, 239U, 0U, 255U},
                            1'000U, 1'000U}));
                }
                list.push(window_sprite(
                    game_hud_assets::white_pixel, x, y,
                    palette_cell_size, palette_cell_size,
                    ui::ColorModulation{palette.colors[index], 1'000U, 1'000U}));
            }
        }

        // HUD.hud_particle_manager.draw runs after the ordinary HUD widgets.
        // HudParticle.blit uses the live bottom-left position directly (the
        // sprite is not centred), so only the y axis changes when emitted into
        // our top-left draw list.
        if (!model.team_progress_particles().empty()) {
            const auto metrics = hud_layout::team_progress(
                context.window, hud_layout::Team::team1,
                model.team_scores_visible());
            const double anchor_x = window_width * 0.5;
            const double anchor_y = metrics.background.y + 10.0;
            for (const auto& particle : model.team_progress_particles()) {
                const double size =
                    team_progress_particle_source_size * particle.scale;
                const double x = anchor_x + particle.offset_x;
                const double y = window_height -
                                 (anchor_y + particle.offset_y) - size;
                const auto opacity = static_cast<std::uint16_t>(std::clamp(
                    std::lround(particle.alpha * 1'000.0), 0L, 1'000L));
                list.push(window_sprite(
                    game_hud_assets::team_progress_particle,
                    x, y, size, size,
                    ui::ColorModulation{particle.color, 1'000U, opacity}));
            }
        }
    }

    // Help panel renders above everything else, matching retail draw order.
    const auto transition = model.help_transition_percentage();
    if (!model.help_lines().empty() && transition > 0.0) {
        const double max_width = window_width * 0.3;

        // Shared line scale: the widest shaped line shrinks every line
        // together, exactly like retail's force_line_scale.
        double line_scale = 1.0;
        double biggest_width = max_width;
        if (context.measure_text) {
            double widest = 0.0;
            for (const auto& line : model.help_lines()) {
                widest = std::max(widest, context.measure_text(line, help_font_size));
            }
            if (widest > 0.0) {
                line_scale = std::min(1.0, max_width / widest);
                biggest_width = widest * line_scale;
            }
        }

        const auto line_count = static_cast<double>(model.help_lines().size());
        const double line_height = (help_ascender + help_descender + 5.0) * line_scale;
        const double line_advance = (help_ascender + help_descender) * line_scale + 4.0;
        // Retail reserves two extra rows for the close-hint spacing.
        const double text_height = (line_count + 2.0) * line_height;
        const double eased = std::sin(transition * 0.01 * std::numbers::pi * 0.5);

        // Recovered top-origin geometry: the backing rests flush with the
        // top edge and slides in from text_height + 90 above it.
        const double panel_left = window_width * 0.5 - biggest_width * 0.5 - help_padding;
        const double panel_top =
            eased * (text_height + 90.0) - text_height - 60.0 - line_height;
        const double panel_width = biggest_width + help_padding * 2.0;
        const double panel_height = text_height + help_padding * 2.0;

        list.push(ui::SpriteDrawCommand{
            std::string{game_hud_assets::white_pixel},
            ui::DrawRect{panel_left, panel_top, panel_width, panel_height},
            ui::DrawSpace::window_pixels,
            ui::TextureFilter::nearest,
            ui::TextureAnchor::top_left,
            1.0,
            ui::SpriteSizing::stretch,
            // Retail backing quad rgba(0,0,0,150): 150/255 = 588 per mille.
            ui::ColorModulation{ui::ColorRgba8{0U, 0U, 0U, 255U}, 1'000U, 588U},
        });

        // Body: left-aligned at the backing's inner edge; first baseline
        // rests 50px from the window top.
        const double text_left = window_width * 0.5 - biggest_width * 0.5;
        const double first_baseline = eased * (text_height + 90.0) - text_height - 40.0;
        const double scaled_font = help_font_size * line_scale;
        double line_index = 0.0;
        for (const auto& line : model.help_lines()) {
            const double top = first_baseline - help_ascender * line_scale +
                               line_index * line_advance;
            list.push(ui::TextDrawCommand{
                line,
                std::string{game_hud_assets::help_font},
                ui::DrawRect{text_left, top, biggest_width, line_height},
                ui::DrawSpace::window_pixels,
                scaled_font,
                0.0,
                1U,
                ui::HorizontalTextAlignment::left,
                ui::VerticalTextAlignment::top,
                ui::TextTransform::preserve,
                ui::TextFit::none,
                ui::ColorModulation{menu_font_color, 1'000U, 1'000U},
            });
            line_index += 1.0;
        }
        if (!model.help_close_hint().empty()) {
            const double hint_top = first_baseline - help_ascender * line_scale +
                                    line_height * (line_count + 1.0);
            list.push(ui::TextDrawCommand{
                model.help_close_hint(),
                std::string{game_hud_assets::help_font},
                ui::DrawRect{text_left, hint_top, biggest_width, line_height},
                ui::DrawSpace::window_pixels,
                scaled_font,
                0.0,
                1U,
                ui::HorizontalTextAlignment::right,
                ui::VerticalTextAlignment::top,
                ui::TextTransform::preserve,
                ui::TextFit::none,
                ui::ColorModulation{menu_font_color, 1'000U, 1'000U},
            });
        }
    }
    return list;
}

} // namespace battlespades::frontend
