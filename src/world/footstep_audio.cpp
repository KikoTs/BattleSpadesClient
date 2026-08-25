#include "battlespades/world/footstep_audio.hpp"

namespace battlespades::world {

MovementSound footstep_family(const FootstepInput& input) noexcept {
    // Crouching and sneaking are silent by design -- that is what makes them
    // worth doing -- and a character in the air is not touching anything.
    if (!input.walking || input.airborne || input.crouch || input.sneak) {
        return MovementSound::none;
    }
    return input.wade ? MovementSound::wade : MovementSound::footstep;
}

MovementSound
step_footsteps(FootstepState& state, const FootstepInput& input, double now) noexcept {
    const auto family = footstep_family(input);
    if (family == MovementSound::none) {
        // Deliberately does NOT re-arm. Leaving the deadline stale is what makes
        // the first step after standing up land immediately instead of after a
        // silent half second; see stale_suppressed_timer for why this is a named
        // inference rather than a silent one.
        if (!stale_suppressed_timer) {
            state.armed = false;
        }
        return MovementSound::none;
    }
    if (!state.armed) {
        // First step of a walk fires at once, then the cadence takes over.
        state.armed = true;
        state.next_footstep = now;
    }
    if (now < state.next_footstep) {
        return MovementSound::none;
    }
    const double interval = input.sprint ? footstep_interval_sprint : footstep_interval_walk;
    // Advance from the deadline, not from `now`, so the cadence stays even
    // instead of drifting by however late this tick arrived. Catch up rather
    // than burst if the deadline is far behind, which happens after a stall.
    state.next_footstep += interval;
    if (state.next_footstep < now) {
        state.next_footstep = now + interval;
    }
    return family;
}

LandJumpSounds
step_land_jump(LandJumpState& state, const LandJumpInput& input, double dt) noexcept {
    if (state.repeat_timer > 0.0) {
        state.repeat_timer -= dt;
        if (state.repeat_timer < 0.0) {
            state.repeat_timer = 0.0;
        }
    }
    LandJumpSounds result;
    // Fall damage is not gated: it layers over the landing rather than
    // replacing it, so a painful drop is audibly worse than a safe one.
    result.fall_hurt = input.fall_damage;

    if (state.repeat_timer > 0.0) {
        return result;
    }
    // Landing wins over jumping when a frame somehow carries both, because the
    // landing is the one the player just felt.
    // Retail has two landing classes. Ordinary contact (including transient
    // stair/auto-climb contact) is silent, while a hard landing thuds even when
    // the fall-distance curve rounded its HP damage to zero.
    if (input.landed && input.hard_landing) {
        result.impact = input.wade ? MovementSound::water_land : MovementSound::land;
    } else if (input.jumped) {
        result.impact = input.wade ? MovementSound::water_jump : MovementSound::jump;
    }
    if (result.impact != MovementSound::none) {
        state.repeat_timer = jump_sound_repeat_delay;
    }
    return result;
}

std::uint32_t movement_sound_variants(MovementSound sound) noexcept {
    switch (sound) {
    case MovementSound::footstep:
    case MovementSound::wade:
        return 4U;
    case MovementSound::jump:
    case MovementSound::water_jump:
    case MovementSound::jetpack_land:
    case MovementSound::land:
    case MovementSound::water_land:
    case MovementSound::fall_hurt:
        return 1U;
    case MovementSound::none:
        break;
    }
    return 0U;
}

const char* movement_sound_stem(MovementSound sound, std::uint32_t variant) noexcept {
    // Every stem below was confirmed present in assets/original/sounds.
    switch (sound) {
    case MovementSound::footstep: {
        static constexpr const char* stems[4U]{
            "footstep_001", "footstep_002", "footstep_003", "footstep_004"};
        return stems[variant % 4U];
    }
    case MovementSound::wade: {
        static constexpr const char* stems[4U]{"wade_001", "wade_002", "wade_003", "wade_004"};
        return stems[variant % 4U];
    }
    case MovementSound::jump:
        return "jump";
    case MovementSound::water_jump:
        return "waterjump";
    case MovementSound::jetpack_land:
        return "JP_lowthrust_rel";
    case MovementSound::land:
        return "land";
    case MovementSound::water_land:
        return "waterland";
    case MovementSound::fall_hurt:
        return "fallhurt";
    case MovementSound::none:
        break;
    }
    return "";
}

const char* class_movement_sound_stem(std::uint8_t class_id,
                                      MovementSound sound,
                                      std::uint32_t variant) noexcept {
    const bool zombie = class_id == 4U || class_id == 14U || class_id == 15U;
    if (!zombie) {
        // CLASS_UGCBUILDER is the one authored blank in this slot.
        if (class_id == 13U && sound == MovementSound::jetpack_land) {
            return "";
        }
        return movement_sound_stem(sound, variant);
    }
    switch (sound) {
    case MovementSound::footstep: {
        static constexpr const char* stems[4U]{"zombie_footstep_001",
                                                "zombie_footstep_002",
                                                "zombie_footstep_003",
                                                "zombie_footstep_004"};
        return stems[variant % 4U];
    }
    case MovementSound::wade: {
        static constexpr const char* stems[4U]{"zombie_wade_001",
                                                "zombie_wade_002",
                                                "zombie_wade_003",
                                                "zombie_wade_004"};
        return stems[variant % 4U];
    }
    case MovementSound::jump:
        return "zombie_jump";
    case MovementSound::water_jump:
        return "zombie_jump_water";
    case MovementSound::land:
        return "zombie_land";
    case MovementSound::water_land:
        return "zombie_land_water";
    case MovementSound::fall_hurt:
        return "zombie_fallhurt";
    case MovementSound::jetpack_land:
        return "JP_lowthrust_rel";
    case MovementSound::none:
        break;
    }
    return "";
}

} // namespace battlespades::world
