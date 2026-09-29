#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>

namespace battlespades::audio {

/**
 * Retail client-local event cues that the server never sends.
 *
 * Each entry names the `shared/constants_audio.py` row it comes from; the
 * pitch bounds are that row's semitone range (`media.play_pitched`).
 * The frontend decides *when* an event happened; these helpers pin *what*
 * retail played for it so the mapping can be unit tested.
 */
struct RetailCue final {
    std::string_view group;
    std::array<float, 2U> pitch_semitones{};
};

/**
 * Player pickup setter (player.pyd 0x1000F150): BOMB_PICKUP_SOUND /
 * DIAMOND_PICKUP_SOUND at the carrier. Intel (16) is announced by the server's
 * classic_pickup PlaySound instead.
 */
[[nodiscard]] constexpr std::optional<RetailCue> pickup_carry_cue(std::uint8_t pickup_id) noexcept {
    switch (pickup_id) {
    case 14U:
        return RetailCue{"bomb_pickup", {-0.4F, 0.4F}};
    case 15U:
        return RetailCue{"diamond_pickup", {-0.4F, 0.4F}};
    default:
        return std::nullopt;
    }
}

/** AIRSTRIKE_EXPLODE_SOUND / AIRSTRIKE_EXPLODE_WATER_SOUND (A2779/A2780). */
[[nodiscard]] constexpr RetailCue airstrike_explode_cue(bool submerged) noexcept {
    return {submerged ? "airstrike_explode_water" : "airstrike_explode", {-0.8F, 0.8F}};
}
/** Airstrike.delete plays the explode cue with play_pitched(volume=1.0). */
inline constexpr float airstrike_explosion_volume{1.0F};

/** BUILD_ERROR_SOUND: 2D, fixed pitch, on every rejected local placement. */
inline constexpr RetailCue build_error_cue{"build_error", {0.0F, 0.0F}};
/**
 * BUILD_LIGHT_SOUND: flareBlockTool.py:47 on a successful flare placement,
 * positioned at the cube. It uses media.play, not play_pitched, so the
 * row's +-0.8 semitone range is ignored.
 */
inline constexpr RetailCue build_light_cue{"build_light", {0.0F, 0.0F}};
/** GRENADE_BOUNCE_SOUND. */
inline constexpr RetailCue grenade_bounce_cue{"grenadebounce", {-0.8F, 0.8F}};
/**
 * Grenade.update plays the bounce only when the world object reports a
 * "sound" bounce (return value 2). The AoS lineage (OpenSpades
 * Grenade.cpp:95-97) returns 2 when any velocity component exceeds
 * BOUNCE_SOUND_THRESHOLD = 0.1 in 1/32-second units, i.e. 3.2 blocks/s.
 * Inferred: the check lives inside compiled world.pyd.
 */
[[nodiscard]] constexpr bool grenade_bounce_audible(double largest_velocity_component) noexcept {
    return largest_velocity_component > 0.1 * 32.0;
}
/** ROCKET_TURRET_SHOOT_SOUND (rocket.py:71). */
inline constexpr RetailCue turret_rocket_shoot_cue{"turr_rocketshoot", {-0.8F, 0.8F}};

/** Which retail projectile class owns a flight loop (rocket.py, rocket2.py). */
enum class RocketFlightKind : std::uint8_t { none, rpg, rocket2, turret };

/**
 * Rocket.__init__: entity 21 is an RPG rocket when its owner currently holds
 * RPG_TOOL (12), otherwise a rocket-turret rocket. Entity 22 is Rocket2.
 */
[[nodiscard]] constexpr RocketFlightKind
rocket_flight_kind(std::uint8_t entity_type,
                   std::optional<std::uint8_t> owner_tool) noexcept {
    if (entity_type == 22U) {
        return RocketFlightKind::rocket2;
    }
    if (entity_type != 21U) {
        return RocketFlightKind::none;
    }
    return owner_tool == std::uint8_t{12U} ? RocketFlightKind::rpg : RocketFlightKind::turret;
}

/** The loops=0 flight cue each rocket class starts in __init__. */
[[nodiscard]] constexpr std::string_view rocket_flight_loop(RocketFlightKind kind) noexcept {
    switch (kind) {
    case RocketFlightKind::rpg:
        return "rocket_projectile";
    case RocketFlightKind::rocket2:
        return "rocket_trip_projectile";
    case RocketFlightKind::turret:
        return "turr_rocket_projectile";
    case RocketFlightKind::none:
        break;
    }
    return {};
}

/**
 * Rocket.delete explosion bank: the turret rocket uses turr_rocketexplode, the
 * RPG rocketexplode. Returns the weapon-tool id retail_explosion_sound keys on
 * (20 = rocket turret, 12 = RPG, 13 = RPG2).
 */
[[nodiscard]] constexpr std::uint8_t rocket_explosion_tool(RocketFlightKind kind) noexcept {
    switch (kind) {
    case RocketFlightKind::turret:
        return 20U;
    case RocketFlightKind::rocket2:
        return 13U;
    case RocketFlightKind::rpg:
    case RocketFlightKind::none:
        break;
    }
    return 12U;
}

/**
 * RocketTurret.update aim detector (rocketTurret.py:59-79).
 *
 * Aiming when |d(pitch)|/dt or |d(yaw)|/dt exceeds A1607 * 10 = 1 degree/s;
 * it stays aiming for a 0.2 s tolerance after the motion stops. The frontend
 * plays turret_aim_start + the turret_aiming_lp loop on the rising edge and
 * turret_aim_stop on the falling edge.
 */
class TurretAimDetector final {
public:
    enum class Edge : std::uint8_t { none, started, stopped };

    Edge update(double pitch, double yaw, double dt) noexcept {
        if (!(dt > 0.0) || !std::isfinite(pitch) || !std::isfinite(yaw)) {
            return Edge::none;
        }
        if (!seeded_) {
            seeded_ = true;
            old_pitch_ = pitch;
            old_yaw_ = yaw;
            return Edge::none;
        }
        constexpr double threshold{0.1 * 10.0};
        constexpr double tolerance{0.2};
        const bool was_aiming = aiming_;
        if (std::abs(pitch - old_pitch_) / dt > threshold ||
            std::abs(yaw - old_yaw_) / dt > threshold) {
            aiming_ = true;
            tolerance_timer_ = tolerance;
        } else if (tolerance_timer_ > 0.0) {
            tolerance_timer_ -= dt;
        } else {
            aiming_ = false;
        }
        old_pitch_ = pitch;
        old_yaw_ = yaw;
        if (aiming_ && !was_aiming) {
            return Edge::started;
        }
        if (!aiming_ && was_aiming) {
            return Edge::stopped;
        }
        return Edge::none;
    }
    [[nodiscard]] bool aiming() const noexcept { return aiming_; }

private:
    double old_pitch_{};
    double old_yaw_{};
    double tolerance_timer_{};
    bool aiming_{};
    bool seeded_{};
};

inline constexpr std::uint8_t double_shotgun_tool_id{10U};

/**
 * Shotgun2Weapon (shotgun2Weapon.py:14-16, 60-66): Weapon.shoot plays
 * shoot_sound, then use_an_ammo + update_ammo pick the NEXT sound: more than
 * one shell left -> SHOTGUN2_SHOOT_SOUND, otherwise SHOTGUN2_SECOND_SHOOT_SOUND.
 * Only the main character decrements its ammo, so observers always hear the
 * first barrel (a retail quirk kept 1:1). `shells_after_shot` is the local
 * magazine after this shot was taken.
 */
[[nodiscard]] constexpr bool double_shotgun_second_barrel(std::uint16_t shells_after_shot) noexcept {
    // Shells before the shot = after + 1; first barrel iff that was > 1.
    return shells_after_shot == 0U;
}

[[nodiscard]] constexpr std::string_view double_shotgun_barrel_stem(bool second_barrel) noexcept {
    return second_barrel ? "shotgun_double_fire02" : "shotgun_double_fire01";
}

} // namespace battlespades::audio
