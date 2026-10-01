#include "battlespades/network/protocol168_session.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <type_traits>
#include <utility>

#include <zlib.h>

namespace battlespades::network {
namespace {

constexpr std::size_t maximum_packet_bytes{1U << 20U};
constexpr std::size_t maximum_compressed_map_bytes{64U << 20U};
constexpr std::size_t maximum_inflated_map_bytes{64U << 20U};

class Reader final {
public:
    explicit Reader(std::span<const std::byte> bytes) : bytes_{bytes} {}
    [[nodiscard]] std::size_t remaining() const noexcept {
        return bytes_.size() - offset_;
    }
    [[nodiscard]] bool done() const noexcept { return remaining() == 0U; }
    [[nodiscard]] std::optional<std::uint8_t> u8() noexcept {
        if (remaining() < 1U) return std::nullopt;
        return std::to_integer<std::uint8_t>(bytes_[offset_++]);
    }
    template <typename Integer>
    [[nodiscard]] std::optional<Integer> integer() noexcept {
        static_assert(std::is_integral_v<Integer>);
        if (remaining() < sizeof(Integer)) return std::nullopt;
        using Unsigned = std::make_unsigned_t<Integer>;
        Unsigned value{};
        for (std::size_t index{}; index < sizeof(Integer); ++index) {
            value = static_cast<Unsigned>(
                value | (static_cast<Unsigned>(std::to_integer<std::uint8_t>(bytes_[offset_ + index]))
                         << (index * 8U)));
        }
        offset_ += sizeof(Integer);
        return static_cast<Integer>(value);
    }
    [[nodiscard]] std::optional<std::string> string(std::size_t maximum) {
        const auto begin = offset_;
        while (offset_ < bytes_.size() && bytes_[offset_] != std::byte{0U}) {
            if (offset_ - begin >= maximum) return std::nullopt;
            ++offset_;
        }
        if (offset_ == bytes_.size()) return std::nullopt;
        std::string result;
        result.reserve(offset_ - begin);
        while (offset_ > begin && bytes_[offset_ - 1U] == std::byte{0U}) --offset_;
        for (auto index = begin; index < offset_; ++index) {
            result.push_back(static_cast<char>(
                std::to_integer<std::uint8_t>(bytes_[index])));
        }
        // Skip the terminator (the loop stopped on it).
        ++offset_;
        return result;
    }
    [[nodiscard]] bool skip(std::size_t count) noexcept {
        if (remaining() < count) return false;
        offset_ += count;
        return true;
    }

private:
    std::span<const std::byte> bytes_;
    std::size_t offset_{};
};

class Writer final {
public:
    void u8(std::uint8_t value) { bytes_.push_back(static_cast<std::byte>(value)); }
    template <typename Integer>
    void integer(Integer value) {
        static_assert(std::is_integral_v<Integer>);
        using Unsigned = std::make_unsigned_t<Integer>;
        const auto raw = static_cast<Unsigned>(value);
        for (std::size_t index{}; index < sizeof(Integer); ++index) {
            u8(static_cast<std::uint8_t>(raw >> (index * 8U)));
        }
    }
    void string(std::string_view value) {
        for (const char character : value) {
            u8(static_cast<std::uint8_t>(character));
        }
        u8(0U);
    }
    [[nodiscard]] std::vector<std::byte> take() && { return std::move(bytes_); }

private:
    std::vector<std::byte> bytes_;
};

template <typename Value>
[[nodiscard]] bool read_required(Reader& reader, Value& output) {
    auto value = reader.integer<Value>();
    if (!value.has_value()) return false;
    output = *value;
    return true;
}

[[nodiscard]] bool skip_counted_bytes(Reader& reader) {
    const auto count = reader.u8();
    return count.has_value() && reader.skip(*count);
}

[[nodiscard]] float from_fixed(std::int16_t raw) noexcept {
    const auto bits = static_cast<std::uint16_t>(raw);
    const float magnitude = static_cast<float>(bits & 0x7FFFU) / 64.0F;
    return (bits & 0x8000U) != 0U ? -magnitude : magnitude;
}

[[nodiscard]] bool is_safe_skybox_name(std::string_view name) noexcept {
    if (name.empty() || name.size() > 63U || !name.ends_with(".txt")) {
        return false;
    }
    return std::ranges::all_of(name, [](unsigned char character) {
        return std::isalnum(character) != 0 || character == ' ' ||
               character == '_' || character == '-' || character == '.';
    });
}

[[nodiscard]] std::optional<Protocol168SkyboxInfo>
decode_skybox_info(std::span<const std::byte> packet, std::string& error) {
    Reader reader{packet};
    const auto id = reader.u8();
    auto name = reader.string(63U);
    if (!id.has_value() || *id != 51U || !name.has_value() || !reader.done() ||
        !is_safe_skybox_name(*name)) {
        error = "malformed or unsafe SkyboxData definition name";
        return std::nullopt;
    }
    error.clear();
    return Protocol168SkyboxInfo{std::move(*name)};
}

[[nodiscard]] std::optional<Protocol168InitialInfo>
decode_initial_info(std::span<const std::byte> packet, std::string& error) {
    Reader reader{packet};
    const auto id = reader.u8();
    if (!id.has_value() || *id != 114U || !reader.skip(16U)) {
        error = "malformed InitialInfo fixed header";
        return std::nullopt;
    }
    Protocol168InitialInfo info;
    // Retain the same bounded strings the server supplies for Loading/Mode.
    const std::array<std::string*, 5U> mode_strings{
        &info.mode_name, &info.mode_description,
        &info.mode_infographic_text[0U], &info.mode_infographic_text[1U],
        &info.mode_infographic_text[2U]};
    for (auto* destination : mode_strings) {
        auto value = reader.string(4096U);
        if (!value.has_value()) {
            error = "malformed InitialInfo mode strings";
            return std::nullopt;
        }
        *destination = std::move(*value);
    }
    auto map_name = reader.string(255U);
    auto filename = reader.string(1024U);
    std::int32_t checksum{};
    if (!map_name.has_value() || !filename.has_value() ||
        !read_required(reader, checksum)) {
        error = "malformed InitialInfo map identity";
        return std::nullopt;
    }
    const auto mode_key = reader.u8();
    if (!mode_key.has_value()) {
        error = "malformed InitialInfo mode key";
        return std::nullopt;
    }
    info.map_name = std::move(*map_name);
    info.filename = std::move(*filename);
    info.checksum = static_cast<std::uint32_t>(checksum);
    info.mode_key = *mode_key;
    const auto map_is_ugc = reader.u8();
    std::uint16_t query_port{};
    // InitialInfo writes query_port immediately after map_is_ugc. Reading it
    // after the flag run shifted every presentation rule by two bytes while
    // leaving the later string boundary deceptively aligned.
    if (!map_is_ugc.has_value() || !read_required(reader, query_port)) {
        error = "truncated InitialInfo role/query port";
        return std::nullopt;
    }
    const auto classic = reader.u8();
    const auto enable_minimap = reader.u8();
    const auto same_team_collision = reader.u8();
    const auto max_draw_distance = reader.u8();
    const auto enable_colour_picker = reader.u8();
    const auto enable_colour_palette = reader.u8();
    const auto enable_deathcam = reader.u8();
    const auto enable_sniper_beam = reader.u8();
    const auto enable_spectator = reader.u8();
    const auto exposed_teams = reader.u8();
    const auto enable_numeric_hp = reader.u8();
    static_cast<void>(max_draw_distance);
    if (!classic.has_value() || !enable_minimap.has_value() ||
        !same_team_collision.has_value() || !max_draw_distance.has_value() ||
        !enable_colour_picker.has_value() || !enable_colour_palette.has_value() ||
        !enable_deathcam.has_value() || !enable_sniper_beam.has_value() ||
        !enable_spectator.has_value() || !exposed_teams.has_value() ||
        !enable_numeric_hp.has_value()) {
        error = "truncated InitialInfo flags";
        return std::nullopt;
    }
    info.query_port = query_port;
    if (*map_is_ugc > static_cast<std::uint8_t>(UgcRole::client)) {
        error = "InitialInfo carries an unknown UGC role";
        return std::nullopt;
    }
    info.ugc_role = static_cast<UgcRole>(*map_is_ugc);
    info.classic = *classic != 0U;
    info.enable_minimap = *enable_minimap != 0U;
    info.exposed_teams_always_on_minimap =
        exposed_teams.value_or(0U) != 0U;
    info.same_team_collision = same_team_collision.value_or(0U) != 0U;
    info.enable_colour_picker = *enable_colour_picker != 0U;
    info.enable_colour_palette = *enable_colour_palette != 0U;
    info.enable_numeric_hp = *enable_numeric_hp != 0U;
    info.enable_deathcam = enable_deathcam.value_or(0U) != 0U;
    info.enable_sniper_beam = *enable_sniper_beam != 0U;
    info.enable_spectator = *enable_spectator != 0U;
    const auto texture_skin = reader.string(255U);
    const auto beach_z_modifiable = reader.u8();
    const auto enable_minimap_height_icons = reader.u8();
    const auto enable_fall_on_water_damage = reader.u8();
    // GameScene.on_connect: max_modifiable_z = 238 if beach_z_modifiable
    // else 237 (the BattleSpades server always sends 1).
    info.beach_z_modifiable = beach_z_modifiable.value_or(1U) != 0U;
    // block_wallet_multiplier (SelectClass block counts) then
    // block_health_multiplier, both 1/64 fixed16; the latter scales every
    // retail get_initial_health. Then the disabled tool / class byte lists.
    std::int16_t block_wallet_raw{};
    std::int16_t block_health_raw{};
    const auto read_counted = [&reader](std::vector<std::uint8_t>& output) {
        const auto count = reader.u8();
        if (!count.has_value() || reader.remaining() < *count) return false;
        output.clear();
        output.reserve(*count);
        for (std::size_t index{}; index < *count; ++index) {
            const auto value = reader.u8();
            if (!value.has_value()) return false;
            output.push_back(*value);
        }
        return true;
    };
    if (!texture_skin.has_value() || !beach_z_modifiable.has_value() ||
        !enable_minimap_height_icons.has_value() ||
        !enable_fall_on_water_damage.has_value() ||
        !read_required(reader, block_wallet_raw) ||
        !read_required(reader, block_health_raw) ||
        !read_counted(info.disabled_tools) || !read_counted(info.disabled_classes)) {
        error = "malformed InitialInfo rule collections";
        return std::nullopt;
    }
    info.enable_minimap_height_icons =
        *enable_minimap_height_icons != 0U;
    info.enable_fall_on_water_damage = *enable_fall_on_water_damage != 0U;
    info.block_wallet_multiplier = from_fixed(block_wallet_raw);
    info.block_health_multiplier = from_fixed(block_health_raw);
    info.texture_skin = std::move(*texture_skin);
    const auto multiplier_count = reader.u8();
    if (!multiplier_count.has_value() ||
        reader.remaining() < static_cast<std::size_t>(*multiplier_count) * 2U) {
        error = "malformed InitialInfo movement multipliers";
        return std::nullopt;
    }
    info.movement_speed_multipliers.reserve(*multiplier_count);
    for (std::size_t index{}; index < *multiplier_count; ++index) {
        std::int16_t raw{};
        if (!read_required(reader, raw)) {
            error = "truncated InitialInfo movement multiplier";
            return std::nullopt;
        }
        info.movement_speed_multipliers.push_back(from_fixed(raw));
    }
    if (!skip_counted_bytes(reader)) {
        error = "malformed InitialInfo prefab sets";
        return std::nullopt;
    }
    const auto enable_player_score = reader.u8();
    if (!enable_player_score.has_value()) {
        error = "truncated InitialInfo player-score flag";
        return std::nullopt;
    }
    info.enable_player_score = *enable_player_score != 0U;
    auto server_name = reader.string(255U);
    const auto ground_count = reader.u8();
    if (!server_name.has_value() || !ground_count.has_value()) {
        error = "malformed InitialInfo tail";
        return std::nullopt;
    }
    info.ground_colors.reserve(*ground_count);
    for (std::size_t index{}; index < *ground_count; ++index) {
        std::array<std::uint8_t, 4U> row{};
        for (auto& value : row) {
            if (!read_required(reader, value)) {
                error = "truncated InitialInfo ground-color row";
                return std::nullopt;
            }
        }
        info.ground_colors.push_back(row);
    }
    // Shared.packet appends one list terminator after the counted rows.
    if (!reader.skip(1U)) {
        error = "truncated InitialInfo ground-color terminator";
        return std::nullopt;
    }
    const auto allow_shooting_holding_intel = reader.u8();
    const auto friendly_fire = reader.u8();
    const auto padding = reader.u8();
    const auto enable_corpse_explosion = reader.u8();
    static_cast<void>(padding);
    static_cast<void>(enable_corpse_explosion);
    if (!allow_shooting_holding_intel.has_value() ||
        !friendly_fire.has_value() || !padding.has_value() ||
        !enable_corpse_explosion.has_value()) {
        error = "truncated InitialInfo combat flags";
        return std::nullopt;
    }
    info.allow_shooting_holding_intel =
        *allow_shooting_holding_intel != 0U;
    info.friendly_fire = *friendly_fire != 0U;
    const auto ugc_mode = reader.u8();
    if (!ugc_mode.has_value()) {
        error = "malformed InitialInfo UGC mode";
        return std::nullopt;
    }
    if (!reader.done()) {
        // Only our explicitly negotiated extension may follow retail fields.
        for (const auto expected : {'B', 'S', 'F', 'P'}) {
            if (reader.u8() != static_cast<std::uint8_t>(expected)) {
                error = "unknown InitialInfo flight profile";
                return std::nullopt;
            }
        }
        const auto version = reader.u8();
        if (!version || (*version != 1U && *version != 2U)) {
            error = "unknown InitialInfo flight profile";
            return std::nullopt;
        }
        const auto flags = reader.u8();
        const auto idle = reader.integer<std::uint16_t>();
        const std::uint8_t maximum_flags = *version == 1U ? 3U : 7U;
        if (!flags || *flags > maximum_flags || !idle || *idle > 640U) {
            error = "invalid InitialInfo flight refill policy";
            return std::nullopt;
        }
        info.flight_profile.grounded_refill_only = (*flags & 1U) != 0U;
        info.flight_profile.descending_parachute_only = (*flags & 2U) != 0U;
        info.flight_profile.canopy_free_fall_floor = (*flags & 4U) != 0U;
        info.flight_profile.refill_idle_seconds = *idle / 64.0;
        for (auto* values : {&info.flight_profile.drain, &info.flight_profile.refill}) {
            for (std::size_t pack{1U}; pack <= 3U; ++pack) {
                const auto raw = reader.integer<std::uint16_t>();
                if (!raw || *raw == 0U || *raw > 6400U) {
                    error = "invalid InitialInfo flight resource rate";
                    return std::nullopt;
                }
                (*values)[pack] = *raw / 64.0;
            }
        }
        if (*version == 2U) {
            // Mover tunings in exact 1/1024 units, each in (0, 1].
            for (auto* value : {&info.flight_profile.engineer_flight_accel,
                                &info.flight_profile.canopy_gravity_scale}) {
                const auto raw = reader.integer<std::uint16_t>();
                if (!raw || *raw == 0U || *raw > 1024U) {
                    error = "invalid InitialInfo flight mover tuning";
                    return std::nullopt;
                }
                *value = static_cast<float>(*raw) / 1024.0F;
            }
        }
        if (!reader.done()) {
            error = "trailing InitialInfo flight data";
            return std::nullopt;
        }
    }
    info.ugc_mode = *ugc_mode;
    info.server_name = std::move(*server_name);
    return info;
}

[[nodiscard]] std::optional<Protocol168StateInfo>
decode_state_info(std::span<const std::byte> packet, std::string& error) {
    Reader reader{packet};
    const auto id = reader.u8();
    const auto player_id = reader.u8();
    Protocol168StateInfo result;
    if (!id.has_value() || *id != 45U || !player_id.has_value()) {
        error = "malformed StateData environment prefix";
        return std::nullopt;
    }
    result.player_id = *player_id;
    const auto read_wire_rgb = [&reader](std::array<std::uint8_t, 3U>& color) {
        const auto blue = reader.u8();
        const auto green = reader.u8();
        const auto red = reader.u8();
        if (!blue.has_value() || !green.has_value() || !red.has_value()) {
            return false;
        }
        // shared.packet.write_color is a historical Python-2 compatibility
        // boundary and writes B,G,R. Keep that reversal here, once, instead
        // of leaking wire order into renderer-owned RGB values.
        color = {*red, *green, *blue};
        return true;
    };
    if (!read_wire_rgb(result.fog_color)) {
        error = "truncated StateData fog color";
        return std::nullopt;
    }
    std::int16_t gravity_raw{};
    if (!read_required(reader, gravity_raw)) {
        error = "truncated StateData gravity";
        return std::nullopt;
    }
    result.gravity = from_fixed(gravity_raw);
    if (!std::isfinite(result.gravity) || result.gravity <= 0.0 ||
        result.gravity > 8.0) {
        error = "invalid StateData gravity";
        return std::nullopt;
    }
    const auto read_wire_triplet = [&reader](std::array<double, 3U>& value,
                                             double maximum_magnitude) {
        std::int16_t z{};
        std::int16_t y{};
        std::int16_t x{};
        if (!read_required(reader, z) || !read_required(reader, y) ||
            !read_required(reader, x)) {
            return false;
        }
        value = {from_fixed(x), from_fixed(y), from_fixed(z)};
        return std::ranges::all_of(value, [maximum_magnitude](double component) {
            return std::isfinite(component) &&
                   std::abs(component) <= maximum_magnitude;
        });
    };
    const auto read_wire_direction = [&read_wire_triplet](
                                         std::array<double, 3U>& direction) {
        return read_wire_triplet(direction, 8.0);
    };
    std::int16_t ambient_intensity_raw{};
    std::int16_t time_scale_raw{};
    // StateData is the lighting authority. The previous implementation skipped
    // these 25 bytes, then tried to reconstruct Legacy lighting from the local
    // skybox; that can never match a server-authored UGC atmosphere.
    if (!read_wire_rgb(result.light_color) ||
        !read_wire_direction(result.light_direction) ||
        !read_wire_rgb(result.back_light_color) ||
        !read_wire_direction(result.back_light_direction) ||
        !read_wire_rgb(result.ambient_light_color) ||
        !read_required(reader, ambient_intensity_raw) ||
        !read_required(reader, time_scale_raw)) {
        error = "malformed StateData lighting prefix";
        return std::nullopt;
    }
    result.ambient_light_intensity = from_fixed(ambient_intensity_raw);
    result.time_scale = from_fixed(time_scale_raw);
    if (!std::isfinite(result.ambient_light_intensity) ||
        result.ambient_light_intensity < 0.0 || result.ambient_light_intensity > 8.0 ||
        !std::isfinite(result.time_scale) || result.time_scale < 0.0 ||
        result.time_scale > 8.0) {
        error = "invalid StateData lighting scalar";
        return std::nullopt;
    }
    const auto score_limit = reader.u8();
    const auto mode_type = reader.u8();
    const auto headcount_type = reader.u8();
    if (!score_limit.has_value() || !mode_type.has_value() ||
        !headcount_type.has_value()) {
        error = "truncated StateData mode fields";
        return std::nullopt;
    }
    result.score_limit = *score_limit;
    result.mode_type = *mode_type;
    result.team_headcount_type = *headcount_type;
    auto read_team = [&](std::string& name, bool& locked_class,
                         bool& locked, bool& can_see_other, bool& show_score,
                         bool& show_max_score, bool& infinite_blocks,
                         bool& locked_score,
                         std::array<std::uint8_t, 3U>& color,
                         std::int32_t& team_score,
                         std::vector<std::uint8_t>& classes) {
        auto decoded_name = reader.string(255U);
        std::int32_t score{};
        if (!decoded_name.has_value()) {
            return false;
        }
        if (!read_wire_rgb(color)) return false;
        if (!read_required(reader, score)) return false;
        const auto flags = reader.u8();
        const auto class_count = reader.u8();
        if (!flags.has_value() || !class_count.has_value() ||
            reader.remaining() < *class_count) {
            return false;
        }
        name = std::move(*decoded_name);
        team_score = score;
        locked = (*flags & 0x01U) != 0U;
        can_see_other = (*flags & 0x02U) != 0U;
        show_score = (*flags & 0x04U) != 0U;
        show_max_score = (*flags & 0x08U) != 0U;
        infinite_blocks = (*flags & 0x10U) != 0U;
        locked_class = (*flags & 0x20U) != 0U;
        locked_score = (*flags & 0x40U) != 0U;
        classes.reserve(*class_count);
        for (std::size_t index{}; index < *class_count; ++index) {
            classes.push_back(*reader.u8());
        }
        return true;
    };
    if (!read_team(result.team1_name, result.team1_locked_class,
                   result.team1_locked, result.team1_can_see_team2,
                   result.team1_show_score, result.team1_show_max_score,
                   result.team1_infinite_blocks, result.team1_locked_score,
                   result.team1_color, result.team1_score,
                   result.team1_classes) ||
        !read_team(result.team2_name, result.team2_locked_class,
                   result.team2_locked, result.team2_can_see_team1,
                   result.team2_show_score, result.team2_show_max_score,
                   result.team2_infinite_blocks, result.team2_locked_score,
                   result.team2_color, result.team2_score,
                   result.team2_classes) ||
        reader.remaining() < 1U) {
        error = "malformed StateData team catalog";
        return std::nullopt;
    }
    const auto lock_flags = *reader.u8();
    result.lock_team_swap = (lock_flags & 0x01U) != 0U;
    result.lock_spectator_swap = (lock_flags & 0x02U) != 0U;
    const auto prefab_count = reader.integer<std::int16_t>();
    if (!prefab_count.has_value() || *prefab_count < 0 || *prefab_count > 4096) {
        error = "invalid StateData prefab count";
        return std::nullopt;
    }
    const auto prefab_total = static_cast<std::size_t>(*prefab_count);
    result.prefabs.reserve(prefab_total);
    for (std::size_t index{}; index < prefab_total; ++index) {
        auto name = reader.string(255U);
        if (!name.has_value()) {
            error = "malformed StateData prefab name";
            return std::nullopt;
        }
        result.prefabs.push_back(std::move(*name));
    }

    // StateData does not end at the prefab catalog. Retail then reads a
    // signed entity count, variable Entity records, two screenshot-camera
    // arrays and finally has_map_ended. Losing this tail left EscapeMenu
    // class/team controls active during the server-owned score state.
    const auto entity_count = reader.integer<std::int16_t>();
    if (!entity_count.has_value() || *entity_count < 0 || *entity_count > 8192) {
        error = "invalid StateData entity count";
        return std::nullopt;
    }
    for (std::int32_t index{}; index < *entity_count; ++index) {
        // Entity fixed prefix through fuse: id(2), type/state/player(3),
        // eleven fixed shorts, face(1), fuse(2).
        if (!reader.skip(30U)) {
            error = "truncated StateData entity prefix";
            return std::nullopt;
        }
        const auto integer_properties = reader.u8();
        const auto float_properties = reader.u8();
        const auto ugc_mode = reader.u8();
        if (!integer_properties.has_value() || !float_properties.has_value() ||
            !ugc_mode.has_value() ||
            !reader.skip(static_cast<std::size_t>(*integer_properties) * 4U +
                         static_cast<std::size_t>(*float_properties) * 2U)) {
            error = "malformed StateData entity properties";
            return std::nullopt;
        }
    }
    const auto camera_points = reader.u8();
    if (!camera_points.has_value()) {
        error = "malformed StateData screenshot points";
        return std::nullopt;
    }
    result.screenshot_camera_points.reserve(*camera_points);
    for (std::size_t index{}; index < *camera_points; ++index) {
        std::array<double, 3U> point{};
        // Camera positions span the complete 512x512x256 VXL world. They use
        // the same signed 1/64 triplet and historical Z,Y,X wire order as the
        // lighting vectors, but must not inherit the latter's unit-vector cap.
        if (!read_wire_triplet(point, 512.0)) {
            error = "malformed StateData screenshot points";
            return std::nullopt;
        }
        result.screenshot_camera_points.push_back(point);
    }
    const auto camera_rotations = reader.u8();
    if (!camera_rotations.has_value()) {
        error = "malformed StateData screenshot rotations";
        return std::nullopt;
    }
    result.screenshot_camera_rotations.reserve(*camera_rotations);
    for (std::size_t index{}; index < *camera_rotations; ++index) {
        std::array<double, 3U> rotation{};
        if (!read_wire_triplet(rotation, 512.0)) {
            error = "malformed StateData screenshot rotations";
            return std::nullopt;
        }
        result.screenshot_camera_rotations.push_back(rotation);
    }
    const auto has_map_ended = reader.u8();
    if (!has_map_ended.has_value() || !reader.done()) {
        error = "malformed StateData map-ended tail";
        return std::nullopt;
    }
    result.has_map_ended = *has_map_ended != 0U;
    return result;
}

[[nodiscard]] std::optional<std::vector<std::byte>>
inflate_map(std::span<const std::byte> compressed, std::string& error) {
    if (compressed.empty()) {
        error = "empty zlib MapSync stream";
        return std::nullopt;
    }
    z_stream stream{};
    stream.next_in = reinterpret_cast<Bytef*>(
        const_cast<std::byte*>(compressed.data()));
    stream.avail_in = static_cast<uInt>(compressed.size());
    if (inflateInit(&stream) != Z_OK) {
        error = "cannot initialize zlib MapSync decoder";
        return std::nullopt;
    }
    std::vector<std::byte> output;
    output.reserve(8U << 20U);
    std::array<std::byte, 64U << 10U> chunk{};
    int status{Z_OK};
    while (status == Z_OK) {
        stream.next_out = reinterpret_cast<Bytef*>(chunk.data());
        stream.avail_out = static_cast<uInt>(chunk.size());
        status = inflate(&stream, Z_NO_FLUSH);
        const auto produced = chunk.size() - stream.avail_out;
        if (output.size() + produced > maximum_inflated_map_bytes) {
            inflateEnd(&stream);
            error = "inflated MapSync exceeds 64 MiB";
            return std::nullopt;
        }
        output.insert(output.end(), chunk.begin(), chunk.begin() +
                      static_cast<std::ptrdiff_t>(produced));
    }
    const bool clean = status == Z_STREAM_END && stream.avail_in == 0U;
    inflateEnd(&stream);
    if (!clean) {
        error = "malformed or trailing zlib MapSync stream";
        return std::nullopt;
    }
    return output;
}

[[nodiscard]] std::optional<world::VxlMap>
decode_full_map_records(std::span<const std::byte> records, std::string& error) {
    Reader reader{records};
    std::vector<std::byte> raw;
    raw.reserve(records.size());
    for (std::uint32_t y{}; y < world::VxlMap::depth; ++y) {
        for (std::uint32_t x{}; x < world::VxlMap::width; ++x) {
            std::uint32_t wire_x{};
            std::uint32_t wire_y{};
            if (!read_required(reader, wire_x) || !read_required(reader, wire_y) ||
                wire_x != x || wire_y != y) {
                error = "MapSync is not a complete ordered 512x512 snapshot";
                return std::nullopt;
            }
            bool terminal{};
            while (!terminal) {
                const auto span_words = reader.u8();
                const auto top_start = reader.u8();
                const auto top_end = reader.u8();
                const auto previous_air = reader.u8();
                if (!span_words.has_value() || !top_start.has_value() ||
                    !top_end.has_value() || !previous_air.has_value()) {
                    error = "truncated MapSync VXL span header";
                    return std::nullopt;
                }
                raw.push_back(static_cast<std::byte>(*span_words));
                raw.push_back(static_cast<std::byte>(*top_start));
                raw.push_back(static_cast<std::byte>(*top_end));
                raw.push_back(static_cast<std::byte>(*previous_air));
                const std::size_t top_colors =
                    *top_end >= *top_start
                        ? static_cast<std::size_t>(*top_end - *top_start + 1U) * 4U
                        : 0U;
                const std::size_t span_size =
                    *span_words == 0U
                        ? 4U + top_colors
                        : static_cast<std::size_t>(*span_words) * 4U;
                if (span_size < 4U || reader.remaining() < span_size - 4U) {
                    error = "invalid MapSync VXL span length";
                    return std::nullopt;
                }
                const auto payload_size = span_size - 4U;
                const auto old_size = raw.size();
                raw.resize(old_size + payload_size);
                for (std::size_t index{}; index < payload_size; ++index) {
                    const auto byte = reader.u8();
                    if (!byte.has_value()) {
                        error = "truncated MapSync VXL span payload";
                        return std::nullopt;
                    }
                    raw[old_size + index] = static_cast<std::byte>(*byte);
                }
                terminal = *span_words == 0U;
            }
        }
    }
    if (!reader.done()) {
        error = "MapSync full snapshot has trailing records";
        return std::nullopt;
    }
    auto loaded = world::VxlMap::load(raw);
    if (!loaded) {
        error = "MapSync reconstructed invalid VXL: " + loaded.error;
        return std::nullopt;
    }
    return std::move(*loaded.map);
}

[[nodiscard]] std::vector<std::byte>
steam_ticket_packet(std::span<const std::byte> ticket, bool flight_profile) {
    Writer writer;
    writer.u8(105U);
    writer.integer<std::int32_t>(static_cast<std::int32_t>(ticket.size()));
    for (const auto value : ticket) {
        writer.u8(std::to_integer<std::uint8_t>(value));
    }
    if (flight_profile) {
        // BSCF v2: also accept the Engineer/canopy mover tuning (BSFP v2).
        // A v1-only server ignores the unknown trailer and stays stock.
        for (const auto value : {'B', 'S', 'C', 'F', '\x02'})
            writer.u8(static_cast<std::uint8_t>(value));
    }
    return std::move(writer).take();
}

/** End offset of the VXL column starting at `position`, tracking its max z. */
[[nodiscard]] std::optional<std::size_t>
vxl_column_end(std::span<const std::byte> bytes, std::size_t position,
               std::uint32_t& maximum_z) noexcept {
    for (;;) {
        if (bytes.size() < position || bytes.size() - position < 4U) return std::nullopt;
        const auto words = std::to_integer<std::uint8_t>(bytes[position]);
        const auto top_start = std::to_integer<std::uint8_t>(bytes[position + 1U]);
        const auto top_end = std::to_integer<std::uint8_t>(bytes[position + 2U]);
        maximum_z = std::max({maximum_z, static_cast<std::uint32_t>(top_start),
                              static_cast<std::uint32_t>(top_end),
                              std::to_integer<std::uint32_t>(bytes[position + 3U])});
        if (words == 0U) {
            const std::size_t top_words =
                top_end >= top_start ? static_cast<std::size_t>(top_end - top_start + 1U) : 0U;
            const auto advance = 4U * (1U + top_words);
            if (advance > bytes.size() - position) return std::nullopt;
            return position + advance;
        }
        const auto advance = static_cast<std::size_t>(words) * 4U;
        if (advance > bytes.size() - position) return std::nullopt;
        position += advance;
    }
}

struct VxlColumns final {
    std::vector<std::pair<std::size_t, std::size_t>> slices;
    std::uint32_t maximum_z{};
};

/** Split a raw VXL into its columns; exactly 512x512 or nothing. */
[[nodiscard]] std::optional<VxlColumns> split_full_vxl(std::span<const std::byte> raw) {
    constexpr std::size_t columns = std::size_t{world::VxlMap::width} * world::VxlMap::depth;
    VxlColumns result;
    result.slices.reserve(columns);
    std::size_t position{};
    while (position < raw.size()) {
        if (result.slices.size() == columns) return std::nullopt;
        const auto end = vxl_column_end(raw, position, result.maximum_z);
        if (!end.has_value()) return std::nullopt;
        result.slices.emplace_back(position, *end);
        position = *end;
    }
    if (result.slices.size() != columns) return std::nullopt;
    return result;
}

/**
 * GameClient.attempt_local_map_open(filename): the raw local stock map, only
 * when its records share the wire's coordinates (a full 512x512, 240-high
 * map needs no z normalisation). Anything else answers CRC 0.
 */
[[nodiscard]] std::optional<std::vector<std::byte>>
read_local_stock_map(const std::filesystem::path& directory, std::string_view filename) {
    if (directory.empty() || filename.empty() || filename.size() > 64U) return std::nullopt;
    std::string stem{filename};
    if (stem.size() > 4U && stem.ends_with(".vxl")) stem.resize(stem.size() - 4U);
    if (stem.empty() || !std::ranges::all_of(stem, [](unsigned char character) {
            return std::isalnum(character) != 0 || character == '_' || character == '-';
        })) {
        return std::nullopt;
    }
    std::ifstream input{directory / (stem + ".vxl"), std::ios::binary};
    if (!input) return std::nullopt;
    std::vector<char> raw{std::istreambuf_iterator<char>{input}, {}};
    if (raw.empty() || raw.size() > maximum_inflated_map_bytes) return std::nullopt;
    std::vector<std::byte> bytes(raw.size());
    std::memcpy(bytes.data(), raw.data(), raw.size());
    const auto columns = split_full_vxl(bytes);
    if (!columns.has_value() || columns->maximum_z < world::VxlMap::height - 1U) {
        return std::nullopt;
    }
    return bytes;
}

[[nodiscard]] std::vector<std::byte> validation_packet(std::uint32_t crc) {
    Writer writer;
    writer.u8(60U);
    writer.integer<std::uint32_t>(crc);
    return std::move(writer).take();
}

[[nodiscard]] std::vector<std::byte>
new_player_packet(const Protocol168SessionConfig& config) {
    Writer writer;
    writer.u8(15U);
    writer.u8(config.team);
    writer.u8(config.class_id);
    writer.u8(0U); // forced_team is server output, never requested by clients
    writer.u8(config.local_language);
    writer.string(config.player_name);
    return std::move(writer).take();
}

[[nodiscard]] std::vector<std::byte>
first_client_data(std::uint8_t player_id, std::uint8_t tool_id,
                  std::uint32_t loop_count) {
    Writer writer;
    writer.u8(4U);
    writer.integer(loop_count);
    writer.u8(player_id & 0x7FU);
    writer.u8(tool_id);
    writer.integer<std::int16_t>(-8192); // normalized -X view direction
    writer.integer<std::int16_t>(0);
    writer.integer<std::int16_t>(0);
    writer.u8(protocol168_client_data_opaque_state(
        static_cast<std::int32_t>(loop_count)));
    writer.u8(0U); // movement flags
    writer.u8(0x10U); // can_display_weapon
    writer.integer<std::int16_t>(0); // deployment yaw
    return std::move(writer).take();
}

} // namespace

std::optional<Protocol168InitialInfo>
decode_protocol168_initial_info(std::span<const std::byte> packet,
                                std::string& error) {
    return decode_initial_info(packet, error);
}

double protocol168_movement_scale(const Protocol168InitialInfo& info,
                                  std::uint8_t class_id) noexcept {
    if (class_id >= info.movement_speed_multipliers.size()) return 1.0;
    const auto scale = static_cast<double>(
        info.movement_speed_multipliers[class_id]);
    return std::isfinite(scale) && scale > 0.0 ? scale : 1.0;
}

std::optional<Protocol168StateInfo>
decode_protocol168_state_info(std::span<const std::byte> packet,
                              std::string& error) {
    return decode_state_info(packet, error);
}

std::optional<Protocol168SkyboxInfo>
decode_protocol168_skybox_info(std::span<const std::byte> packet,
                               std::string& error) {
    return decode_skybox_info(packet, error);
}

std::optional<std::vector<std::byte>> decode_protocol168_server_datagram(
    std::span<const std::byte> datagram, std::string& error) {
    if (datagram.size() < 2U) {
        error = "Protocol 168 datagram is missing prefix/body";
        return std::nullopt;
    }
    const auto prefix = std::to_integer<std::uint8_t>(datagram.front());
    if (prefix != 0x30U && prefix != 0x31U && prefix != 0x32U) {
        error = "Protocol 168 datagram has an unknown outer prefix";
        return std::nullopt;
    }
    // BattleSpades' server literal-LZF encodes every outbound body, including
    // the 0x32 MapSyncStart marker. Decode the complete bounded stream.
    std::vector<std::byte> output;
    output.reserve(std::min(datagram.size() * 2U, maximum_packet_bytes));
    std::size_t input{1U};
    while (input < datagram.size()) {
        const auto control = std::to_integer<std::uint8_t>(datagram[input++]);
        const std::size_t length_code = control >> 5U;
        std::size_t distance = control & 0x1FU;
        if (length_code == 0U) {
            const std::size_t count = distance + 1U;
            if (input + count > datagram.size() ||
                output.size() + count > maximum_packet_bytes) {
                error = "truncated or oversized LZF literal";
                return std::nullopt;
            }
            output.insert(output.end(), datagram.begin() +
                          static_cast<std::ptrdiff_t>(input),
                          datagram.begin() +
                          static_cast<std::ptrdiff_t>(input + count));
            input += count;
            continue;
        }
        std::size_t count = length_code;
        if (count == 7U) {
            if (input >= datagram.size()) {
                error = "truncated LZF extended length";
                return std::nullopt;
            }
            count += std::to_integer<std::uint8_t>(datagram[input++]);
        }
        if (input >= datagram.size()) {
            error = "truncated LZF back-reference";
            return std::nullopt;
        }
        distance = (distance << 8U) +
                   std::to_integer<std::uint8_t>(datagram[input++]) + 1U;
        count += 2U;
        if (distance > output.size() || output.size() + count > maximum_packet_bytes) {
            error = "invalid or oversized LZF back-reference";
            return std::nullopt;
        }
        for (std::size_t index{}; index < count; ++index) {
            output.push_back(output[output.size() - distance]);
        }
    }
    if (output.empty()) {
        error = "Protocol 168 datagram decoded to an empty packet";
        return std::nullopt;
    }
    error.clear();
    return output;
}

std::vector<std::byte> encode_protocol168_client_datagram(
    std::span<const std::byte> packet, std::span<const std::byte> ticket) {
    std::vector<std::byte> result;
    result.reserve(packet.size() + 1U);
    result.push_back(std::byte{0x30U});
    for (std::size_t index{}; index < packet.size(); ++index) {
        const auto key = ticket.empty() ? std::byte{} : ticket[index % ticket.size()];
        result.push_back(packet[index] ^ key);
    }
    return result;
}

std::vector<std::byte> encode_protocol168_new_player_connection(
    const Protocol168SessionConfig& config) {
    return new_player_packet(config);
}

std::uint32_t protocol168_map_crc32(std::span<const std::byte> bytes) noexcept {
    auto crc = crc32(0L, Z_NULL, 0U);
    std::size_t offset{};
    while (offset < bytes.size()) {
        const auto count = static_cast<uInt>(
            std::min<std::size_t>(bytes.size() - offset, std::numeric_limits<uInt>::max()));
        crc = crc32(crc, reinterpret_cast<const Bytef*>(bytes.data() + offset), count);
        offset += count;
    }
    return static_cast<std::uint32_t>(crc);
}

std::optional<world::VxlMap>
protocol168_apply_map_records(std::span<const std::byte> base_raw,
                              std::span<const std::byte> records,
                              std::string& error) {
    const auto base = split_full_vxl(base_raw);
    if (!base.has_value()) {
        error = "local map base is not a 512x512 VXL";
        return std::nullopt;
    }
    constexpr std::size_t columns = std::size_t{world::VxlMap::width} * world::VxlMap::depth;
    std::vector<std::optional<std::pair<std::size_t, std::size_t>>> overrides(columns);
    Reader reader{records};
    std::size_t position{};
    while (!reader.done()) {
        const auto x = reader.integer<std::uint32_t>();
        const auto y = reader.integer<std::uint32_t>();
        if (!x.has_value() || !y.has_value() || *x >= world::VxlMap::width ||
            *y >= world::VxlMap::depth) {
            error = "MapSync column record has an invalid coordinate";
            return std::nullopt;
        }
        position += 8U;
        std::uint32_t ignored_z{};
        const auto end = vxl_column_end(records, position, ignored_z);
        if (!end.has_value()) {
            error = "truncated MapSync column record";
            return std::nullopt;
        }
        overrides[std::size_t{*y} * world::VxlMap::width + *x] =
            std::pair{position, *end};
        for (std::size_t skip = position; skip < *end; ++skip) static_cast<void>(reader.u8());
        position = *end;
    }
    std::vector<std::byte> raw;
    raw.reserve(base_raw.size() + records.size());
    for (std::size_t column{}; column < columns; ++column) {
        const auto source = overrides[column].has_value() ? records : base_raw;
        const auto [begin, end] = overrides[column].value_or(base->slices[column]);
        raw.insert(raw.end(), source.begin() + static_cast<std::ptrdiff_t>(begin),
                   source.begin() + static_cast<std::ptrdiff_t>(end));
    }
    auto loaded = world::VxlMap::load(raw);
    if (!loaded) {
        error = "MapSync overlay produced an invalid VXL: " + loaded.error;
        return std::nullopt;
    }
    return std::move(*loaded.map);
}

Protocol168Session::Protocol168Session(Protocol168SessionConfig config)
    : config_{std::move(config)} {
    if (config_.player_name.empty() || config_.player_name.size() > 31U ||
        config_.team > 3U || config_.class_id > 17U ||
        config_.steam_ticket.size() > 2048U) {
        phase_ = Protocol168SessionPhase::failed;
        last_error_ = "invalid Protocol 168 session configuration";
    }
}

std::vector<std::byte> Protocol168Session::connected() {
    if (phase_ != Protocol168SessionPhase::disconnected) return {};
    phase_ = Protocol168SessionPhase::awaiting_initial_info;
    // Packet 105 itself is plain. Its payload becomes the key only after the
    // server has consumed it, matching retail's SteamSendSessionTicket order.
    return encode_protocol168_client_datagram(
        steam_ticket_packet(config_.steam_ticket, config_.negotiate_flight_profile));
}

Protocol168IngestResult Protocol168Session::ingest(
    std::span<const std::byte> datagram) {
    if (phase_ == Protocol168SessionPhase::failed) {
        return {{}, false, last_error_};
    }
    std::string error;
    auto decoded = decode_protocol168_server_datagram(datagram, error);
    if (!decoded.has_value()) {
        Protocol168IngestResult result;
        static_cast<void>(note_malformed(std::move(error), result, false));
        return result;
    }
    return ingest_packet(*decoded);
}

Protocol168IngestResult Protocol168Session::ingest_packet(
    std::span<const std::byte> packet) {
    Protocol168IngestResult result;
    if (packet.empty()) {
        static_cast<void>(
            note_malformed("empty Protocol 168 packet", result, true));
        return result;
    }
    const auto id = std::to_integer<std::uint8_t>(packet.front());
    if (id == 112U) {
        // PasswordNeeded: only before InitialInfo, only the id byte. A
        // repeated request is the server's "wrong password".
        if (phase_ != Protocol168SessionPhase::awaiting_initial_info || packet.size() != 1U) {
            static_cast<void>(note_malformed(
                "malformed or out-of-phase PasswordNeeded", result, true));
            return result;
        }
        if (password_prompt_.requests < 255U) ++password_prompt_.requests;
        password_prompt_.rejected = password_prompt_.requests > 1U;
        const bool configured = !configured_password_sent_ &&
                                !encode_password_provided_packet(config_.server_password).empty();
        if (configured) {
            configured_password_sent_ = true;
            password_prompt_.pending = false;
            result.outbound_datagrams.push_back(encode_protocol168_client_datagram(
                encode_password_provided_packet(config_.server_password),
                config_.steam_ticket));
        } else {
            password_prompt_.pending = true;
        }
        result.accepted = true;
        return result;
    }
    if (id == 114U) {
        if (phase_ == Protocol168SessionPhase::disconnected) {
            static_cast<void>(note_malformed(
                "InitialInfo arrived outside its handshake phase", result, true));
            return result;
        }
        std::string error;
        auto info = decode_protocol168_initial_info(packet, error);
        if (!info.has_value()) {
            static_cast<void>(note_malformed(std::move(error), result, true));
            return result;
        }
        if (initial_info_.has_value()) {
            // Stock GameClient handles 114 by destroying the old map and
            // opening LoadingMenu on the existing authenticated connection.
            // Reset every map-owned stream/roster/label, retaining the ticket
            // and local-map configuration. No second packet 105 is sent.
            const auto next_generation = map_generation_ + 1U;
            auto config = config_;
            *this = Protocol168Session{std::move(config)};
            map_generation_ = next_generation;
        }
        initial_info_ = std::move(info);
        // InitialInfo is the only "password accepted" signal there is.
        password_prompt_.pending = false;
        password_prompt_.rejected = false;
        phase_ = Protocol168SessionPhase::awaiting_map_validation;
        // GameClient.packet_received (network.pyd 0x1000d900) opens the local
        // map on InitialInfo and send_map_validation replies crc32(file), or
        // 0 when it is missing. UGC worlds never have a stock local file.
        sent_map_crc_ = config_.local_map_crc;
        local_map_raw_.clear();
        if (!config_.local_map_directory.empty()) {
            sent_map_crc_ = 0U;
            if (!initial_info_->map_is_ugc()) {
                if (auto raw = read_local_stock_map(config_.local_map_directory,
                                                    initial_info_->filename);
                    raw.has_value()) {
                    sent_map_crc_ = protocol168_map_crc32(*raw);
                    local_map_raw_ = std::move(*raw);
                }
            }
        }
        loading_.initial_info = true;
        result.outbound_datagrams.push_back(encode_protocol168_client_datagram(
            validation_packet(sent_map_crc_), config_.steam_ticket));
        phase_ = Protocol168SessionPhase::awaiting_map_start;
        result.accepted = true;
        return result;
    }
    if (id == 54U) {
        if (phase_ != Protocol168SessionPhase::awaiting_map_start ||
            packet.size() != 1U || receiving_ugc_source_) {
            static_cast<void>(note_malformed(
                "malformed or out-of-phase MapDataStart", result, true));
            return result;
        }
        ugc_source_stream_.clear();
        receiving_ugc_source_ = true;
        loading_.receiving_map_data = true;
        loading_.map_data_percent = 0U;
        result.accepted = true;
        return result;
    }
    if (id == 56U) {
        Reader reader{packet};
        std::uint16_t size{};
        const auto packet_id = reader.u8();
        const auto percent = reader.u8();
        // ByteWriter calls this field `short`, but its two bytes are a payload
        // count. A persistent zlib encoder may hold many 1,048-byte source
        // slices and emit more than 32,767 bytes at a flush boundary. Treating
        // bit 15 as a sign rejected a valid 34,677-byte editor source chunk.
        if (phase_ != Protocol168SessionPhase::awaiting_map_start ||
            !receiving_ugc_source_ || !packet_id.has_value() ||
            !percent.has_value() || *percent > 100U ||
            !read_required(reader, size) ||
            reader.remaining() != static_cast<std::size_t>(size) ||
            ugc_source_stream_.size() + reader.remaining() >
                maximum_compressed_map_bytes) {
            static_cast<void>(note_malformed(
                "malformed or oversized MapDataChunk (phase=" +
                    std::to_string(static_cast<unsigned int>(phase_)) +
                    ", receiving=" + (receiving_ugc_source_ ? "1" : "0") +
                    ", id=" +
                    std::to_string(packet_id.has_value() ? *packet_id : 999U) +
                    ", percent=" +
                    std::to_string(percent.has_value() ? *percent : 999U) +
                    ", size=" + std::to_string(size) +
                    ", remaining=" + std::to_string(reader.remaining()) + ")",
                result,
                true));
            return result;
        }
        while (!reader.done()) {
            ugc_source_stream_.push_back(
                static_cast<std::byte>(*reader.u8()));
        }
        loading_.map_data_percent = std::max(loading_.map_data_percent, *percent);
        result.accepted = true;
        return result;
    }
    if (id == 58U) {
        if (phase_ != Protocol168SessionPhase::awaiting_map_start ||
            !receiving_ugc_source_ || packet.size() != 1U) {
            static_cast<void>(note_malformed(
                "malformed or out-of-phase MapDataEnd", result, true));
            return result;
        }
        std::string error;
        auto raw = inflate_map(ugc_source_stream_, error);
        if (!raw.has_value()) {
            static_cast<void>(note_malformed(
                "invalid UGC source VXL: " + error, result, true));
            return result;
        }
        const auto loaded = world::VxlMap::load(*raw);
        if (!loaded) {
            static_cast<void>(note_malformed(
                "invalid UGC source VXL: " + loaded.error, result, true));
            return result;
        }
        // The client deliberately requested a full MapSync with CRC zero. The
        // source validates the UGC-host stream and is then superseded by that
        // authoritative current-world snapshot, including edits made since
        // the source file was loaded.
        receiving_ugc_source_ = false;
        ugc_source_stream_.clear();
        result.accepted = true;
        return result;
    }
    if (id == 60U) {
        if (phase_ != Protocol168SessionPhase::awaiting_map_start ||
            receiving_ugc_source_ || packet.size() != 5U) {
            static_cast<void>(note_malformed(
                "malformed or out-of-phase MapDataValidation", result, true));
            return result;
        }
        // The server's own file CRC (never an echo of ours): on a match the
        // local stock map becomes the world base (BS connection.send_map_data).
        Reader validation{packet.subspan(1U)};
        server_map_crc_ = validation.integer<std::uint32_t>();
        loading_.map_validated = true;
        result.accepted = true;
        return result;
    }
    if (id == 55U) {
        if (phase_ != Protocol168SessionPhase::awaiting_map_start ||
            receiving_ugc_source_ || packet.size() != 1U) {
            static_cast<void>(note_malformed(
                "MapSyncStart must be one bare id in validation phase", result, true));
            return result;
        }
        map_stream_.clear();
        phase_ = Protocol168SessionPhase::receiving_map;
        loading_.sync_started = true;
        loading_.sync_percent = 0U;
        result.accepted = true;
        return result;
    }
    if (id == 57U) {
        Reader reader{packet};
        std::int16_t size{};
        const auto packet_id = reader.u8();
        const auto percent = reader.u8();
        if (phase_ != Protocol168SessionPhase::receiving_map ||
            !packet_id.has_value() || !percent.has_value() ||
            !read_required(reader, size) || size < 0 || size > 1024 ||
            reader.remaining() != static_cast<std::size_t>(size) ||
            *percent > 101U || map_stream_.size() + reader.remaining() >
                                  maximum_compressed_map_bytes) {
            static_cast<void>(note_malformed(
                "malformed, oversized, or out-of-phase MapSyncChunk", result,
                true));
            return result;
        }
        while (!reader.done()) {
            map_stream_.push_back(static_cast<std::byte>(*reader.u8()));
        }
        // percent_complete = int(index / total * 100) + 1, so up to 101.
        loading_.sync_percent = std::max<std::uint8_t>(
            loading_.sync_percent, std::min<std::uint8_t>(*percent, 100U));
        result.accepted = true;
        return result;
    }
    if (id == 59U) {
        if (phase_ != Protocol168SessionPhase::receiving_map || packet.size() != 1U) {
            static_cast<void>(note_malformed(
                "malformed or out-of-phase MapSyncEnd", result, true));
            return result;
        }
        std::string error;
        const bool local_base = !local_map_raw_.empty() && sent_map_crc_ != 0U &&
                                server_map_crc_.has_value() &&
                                *server_map_crc_ == sent_map_crc_;
        // A CRC match lets the server send only its dirty columns, which may
        // be none at all (an empty stream).
        std::optional<std::vector<std::byte>> records;
        if (local_base && map_stream_.empty()) {
            records.emplace();
        } else {
            records = inflate_map(map_stream_, error);
        }
        if (!records.has_value()) {
            static_cast<void>(note_malformed(std::move(error), result, true));
            return result;
        }
        auto map = local_base
                       ? protocol168_apply_map_records(local_map_raw_, *records, error)
                       : decode_full_map_records(*records, error);
        if (!map.has_value()) {
            static_cast<void>(note_malformed(std::move(error), result, true));
            return result;
        }
        map_ = std::move(map);
        local_map_raw_.clear();
        local_map_raw_.shrink_to_fit();
        loading_.sync_finished = true;
        loading_.local_map_base = local_base;
        phase_ = Protocol168SessionPhase::awaiting_state;
        result.accepted = true;
        return result;
    }
    if (id == 45U) {
        if (phase_ != Protocol168SessionPhase::awaiting_state || packet.size() < 2U) {
            static_cast<void>(note_malformed(
                "malformed or out-of-phase StateData", result, true));
            return result;
        }
        const auto player_id = std::to_integer<std::uint8_t>(packet[1U]);
        if (player_id >= 128U) {
            static_cast<void>(note_malformed(
                "StateData assigned an invalid player id", result, true));
            return result;
        }
        local_player_id_ = player_id;
        std::string state_error;
        state_info_ = decode_protocol168_state_info(packet, state_error);
        if (!state_info_.has_value()) {
            static_cast<void>(note_malformed(std::move(state_error), result, true));
            return result;
        }
        phase_ = Protocol168SessionPhase::awaiting_own_player;
        // Retail opens SelectTeam here. Only non-interactive probes opt into
        // the legacy one-shot path; the real frontend sends packet 15 after
        // both team and class are explicitly confirmed.
        if (config_.auto_join) {
            result.outbound_datagrams.push_back(
                encode_protocol168_client_datagram(new_player_packet(config_),
                                                   config_.steam_ticket));
        }
        result.accepted = true;
        return result;
    }
    if (id == 51U) {
        if (phase_ != Protocol168SessionPhase::awaiting_own_player &&
            phase_ != Protocol168SessionPhase::ready) {
            static_cast<void>(note_malformed(
                "SkyboxData arrived before StateData", result, false));
            return result;
        }
        std::string error;
        auto info = decode_protocol168_skybox_info(packet, error);
        if (!info.has_value()) {
            static_cast<void>(note_malformed(std::move(error), result, false));
            return result;
        }
        skybox_info_ = std::move(info);
        result.accepted = true;
        return result;
    }
    if (id == CreatePlayerPacket::id) {
        if (phase_ != Protocol168SessionPhase::awaiting_own_player &&
            phase_ != Protocol168SessionPhase::ready &&
            phase_ != Protocol168SessionPhase::awaiting_state) {
            static_cast<void>(note_malformed(
                "CreatePlayer arrived before map synchronization", result, true));
            return result;
        }
        std::string error;
        if (!roster_.apply(packet, &error)) {
            static_cast<void>(note_malformed(std::move(error), result, false));
            return result;
        }
        const auto decoded = decode_create_player(packet);
        if (decoded && local_player_id_.has_value() &&
            decoded.packet->player_id == *local_player_id_) {
            const auto tool = decoded.packet->loadout.empty()
                                  ? std::uint8_t{2U}
                                  : decoded.packet->loadout.front();
            result.outbound_datagrams.push_back(encode_protocol168_client_datagram(
                first_client_data(*local_player_id_, tool, client_loop_count_++),
                config_.steam_ticket));
            phase_ = Protocol168SessionPhase::ready;
        }
        result.accepted = true;
        return result;
    }

    // The server starts map ambience/music immediately after StateData on
    // some join paths, before our own CreatePlayer flips the connection into
    // its live-packet loop. These are complete ENet datagrams and already have
    // typed runtime decoders; retain them in arrival order instead of silently
    // losing the soundscape during the handshake.
    if (phase_ != Protocol168SessionPhase::disconnected &&
        phase_ != Protocol168SessionPhase::awaiting_initial_info &&
        phase_ != Protocol168SessionPhase::ready &&
        id >= 22U && id <= 27U) {
        if (!deferred_runtime_packets_.push(packet)) {
            return fail("Protocol 168 deferred audio queue overflow");
        }
        result.accepted = true;
        return result;
    }

    // SetColor(1), HP(17), Restock(69), roster auxiliaries,
    // and mode packets may legally interleave after StateData. They remain
    // quarantined until their typed adapters exist, but never desynchronize
    // framing because ENet delivers one complete packet per datagram.
    ++unknown_packets_;
    result.accepted = false;
    result.diagnostic = "ignored unsupported Protocol 168 packet " +
                        std::to_string(id);
    return result;
}

Protocol168IngestResult Protocol168Session::fail(std::string message) {
    phase_ = Protocol168SessionPhase::failed;
    last_error_ = std::move(message);
    return {{}, false, last_error_};
}

bool Protocol168Session::note_malformed(std::string message,
                                        Protocol168IngestResult& result,
                                        bool critical) {
    ++malformed_packets_;
    if (critical || malformed_packets_ >= 3U) {
        result = fail(std::move(message));
        return false;
    }
    result.accepted = false;
    result.diagnostic = std::move(message);
    return true;
}

void Protocol168Session::disconnected() noexcept {
    if (phase_ != Protocol168SessionPhase::failed) {
        phase_ = Protocol168SessionPhase::disconnected;
    }
}

std::vector<std::byte> encode_password_provided_packet(std::string_view password) {
    if (password.empty() || password.size() > maximum_server_password_bytes ||
        password.find('\0') != std::string_view::npos) {
        return {};
    }
    Writer writer;
    writer.u8(113U);
    writer.string(password);
    return std::move(writer).take();
}

Protocol168PasswordPrompt Protocol168Session::password_prompt() const noexcept {
    return password_prompt_;
}

std::vector<std::byte> Protocol168Session::provide_password(std::string_view password) {
    if (!password_prompt_.pending ||
        phase_ != Protocol168SessionPhase::awaiting_initial_info) {
        return {};
    }
    const auto packet = encode_password_provided_packet(password);
    if (packet.empty()) return {};
    password_prompt_.pending = false;
    return encode_protocol168_client_datagram(packet, config_.steam_ticket);
}

Protocol168SessionPhase Protocol168Session::phase() const noexcept { return phase_; }
std::uint64_t Protocol168Session::map_generation() const noexcept { return map_generation_; }
bool Protocol168Session::bootstrap_ready() const noexcept {
    if (!map_.has_value() || !local_player_id_.has_value() ||
        !initial_info_.has_value() || !state_info_.has_value()) {
        return false;
    }
    return config_.auto_join ? ready()
                             : phase_ == Protocol168SessionPhase::awaiting_own_player;
}
bool Protocol168Session::ready() const noexcept {
    return phase_ == Protocol168SessionPhase::ready;
}
std::string_view Protocol168Session::last_error() const noexcept { return last_error_; }
const Protocol168InitialInfo* Protocol168Session::initial_info() const noexcept {
    return initial_info_.has_value() ? &*initial_info_ : nullptr;
}

const Protocol168StateInfo* Protocol168Session::state_info() const noexcept {
    return state_info_.has_value() ? &*state_info_ : nullptr;
}
const Protocol168SkyboxInfo* Protocol168Session::skybox_info() const noexcept {
    return skybox_info_.has_value() ? &*skybox_info_ : nullptr;
}
const world::VxlMap* Protocol168Session::map() const noexcept {
    return map_.has_value() ? &*map_ : nullptr;
}

std::optional<world::VxlMap> Protocol168Session::take_map() noexcept {
    if (!bootstrap_ready() || !map_.has_value()) {
        return std::nullopt;
    }
    auto map = std::move(map_);
    map_.reset();
    return map;
}
const Protocol168Roster& Protocol168Session::roster() const noexcept { return roster_; }
std::optional<std::uint8_t> Protocol168Session::local_player_id() const noexcept {
    return local_player_id_;
}
std::size_t Protocol168Session::compressed_map_bytes() const noexcept {
    return map_stream_.size();
}
std::size_t Protocol168Session::unknown_packets() const noexcept {
    return unknown_packets_;
}
std::size_t Protocol168Session::malformed_packets() const noexcept {
    return malformed_packets_;
}

std::vector<std::vector<std::byte>>
Protocol168Session::take_deferred_runtime_packets() {
    return deferred_runtime_packets_.take(deferred_runtime_packets_.size());
}

std::uint32_t Protocol168Session::next_client_loop_count() const noexcept {
    return client_loop_count_;
}

Protocol168LoadingProgress Protocol168Session::loading_progress() const noexcept {
    return loading_;
}

std::uint32_t Protocol168Session::sent_map_crc() const noexcept { return sent_map_crc_; }

} // namespace battlespades::network
