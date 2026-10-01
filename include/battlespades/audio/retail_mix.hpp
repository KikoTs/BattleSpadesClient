#pragma once

#include "battlespades/audio/audio_port.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>

namespace battlespades::audio {

/**
 * Retail mix model recovered from `aoslib/media.py`, `aoslib/audio.py`,
 * `weapons/tool.py`, `scenes/main/entity.py` and the stock gameScene /
 * character pyds (audit3 audio.md). Header-only and OpenAL-free so the
 * parity numbers are unit tested without an audio device.
 */

/** `Tool.play_sound` (tool.py:99-100): shots, loops, tails, throws, pins, swings. */
inline constexpr float retail_tool_sound_volume{0.5F};
/** `Character.play_sound` default: reload, foley, VO, zoom, heal, death. */
inline constexpr float retail_character_sound_volume{1.0F};
/** `Entity.play_sound` default (entity.py:160). */
inline constexpr float retail_entity_sound_volume{0.75F};
/** `Crate.update` chute-open cue (gameScene 0x1009fa40, `I4 volume`). */
inline constexpr float retail_crate_chute_open_volume{4.0F};
/**
 * Retail never sets AL_REFERENCE_DISTANCE, so OpenAL's default of 1 applies;
 * positional gain is `G / (1 + r * (d - 1))`.
 */
inline constexpr float retail_reference_distance{1.0F};
/** `DEFAULT_ATTENUATION` (constants.py:5056): the rolloff of every world cue. */
inline constexpr float retail_default_attenuation{0.15F};

/**
 * Retail's OpenAL32.dll is OpenAL Soft 1.13 ("1.1 ALSOFT 1.13"), which has
 * no HRTF (and no AL_SOFT_direct_channels). Modern OpenAL Soft enables HRTF
 * automatically on headphone outputs; the context requests it off.
 */
inline constexpr bool retail_context_hrtf{false};

/** `DEFAULT_MUSIC_FADE_TIME`: stop_music hands the track to a 6.5 s fade. */
inline constexpr float retail_music_fade_seconds{6.5F};
/** `SECONDARY_MUSIC_BED_FADE_TIME` for the class/team/UGC select bed. */
inline constexpr float retail_secondary_bed_fade_seconds{1.5F};

/** Semitone bounds stored at retail cue-list slots 3/4. */
struct PitchBounds final {
    float minimum{};
    float maximum{};
};

/** GENERIC/ZOMBIE_FOOTSTEP_SOUND and _WADE_SOUND (constants_audio.py:50-74). */
inline constexpr PitchBounds retail_footstep_pitch{-1.5F, 1.5F};
/** Jump, water jump, land, water land and fall-hurt (constants_audio.py:50-74). */
inline constexpr PitchBounds retail_movement_impact_pitch{-0.8F, 0.8F};
/** BULLET_HIT_SCENERY_SOUND (constants_audio.py:222). */
inline constexpr PitchBounds retail_bullet_hit_pitch{-1.2F, 1.2F};
/** DEATH_EXPLODE_SOUND / DEATH_EXPLODE_WATER_SOUND (constants_audio.py:132-133). */
inline constexpr PitchBounds retail_death_explode_pitch{-0.8F, 0.8F};

/**
 * Per-group pitch for the tool cue roles whose rows are known.
 *
 * `woosh` is DIG_MISS_SOUND (+-0.4) as a melee swing but GRENADE_THROW_SOUND
 * (+-0.8) as a throw; unknown groups stay at the fixed retail pitch.
 */
[[nodiscard]] constexpr PitchBounds retail_cue_group_pitch(std::string_view group,
                                                           bool thrown) noexcept {
    if (group == "woosh") {
        return thrown ? PitchBounds{-0.8F, 0.8F} : PitchBounds{-0.4F, 0.4F};
    }
    if (group == "molotov_throw" || group == "whack" || group == "grenadebounce") {
        return {-0.8F, 0.8F};
    }
    if (group == "hitground" || group == "hitground_super" || group == "hitground_zombie" ||
        group == "zombiehand_hit") {
        return {-0.4F, 0.4F};
    }
    return {};
}

/**
 * `GameSound.set_position` (media.py:15-17) stores (x+0.5, -z+0.5, y+0.5) in
 * OpenAL space while the listener gets no offset. In AoS coordinates (z grows
 * downward) every source therefore sits +0.5 x, +0.5 y and half a block up.
 */
[[nodiscard]] constexpr SoundPosition retail_source_position(SoundPosition nominal) noexcept {
    return {nominal.x + 0.5F, nominal.y + 0.5F, nominal.z - 0.5F};
}

/**
 * `MediaManager.get_sound_name`: a numbered family picks uniformly at random
 * but never the member it played last. `last` >= `count` means none yet.
 */
[[nodiscard]] constexpr std::uint32_t
pick_non_repeating_variant(std::uint32_t count, std::uint32_t last, std::uint32_t draw) noexcept {
    if (count <= 1U) {
        return 0U;
    }
    if (last >= count) {
        return draw % count;
    }
    const std::uint32_t pick = draw % (count - 1U);
    return pick >= last ? pick + 1U : pick;
}

// ---------------------------------------------------------------------------
// GameScene.update_audio_effects (gameScene 0x101446f0)
// ---------------------------------------------------------------------------

inline constexpr std::size_t retail_reverb_ceiling_rays{5U};
inline constexpr std::size_t retail_reverb_ceiling_hits_required{4U};
inline constexpr std::size_t retail_reverb_wall_rays{8U};
/** REVERB_MAX_WALL_DISTANCE / REVERB_MIN_WALLS / REVERB_MAX_GAIN. */
inline constexpr float retail_reverb_max_wall_distance{40.0F};
inline constexpr std::size_t retail_reverb_min_walls{6U};
inline constexpr float retail_reverb_max_gain{0.1F};
/** REVERB_DECAY_TIME_SCALE / REVERB_MAX_DECAY_TIME / REVERB_GAINHF. */
inline constexpr float retail_reverb_decay_scale{0.1F};
inline constexpr float retail_reverb_max_decay{2.2F};
inline constexpr float retail_reverb_gain_hf{0.89F};
/** REVERB_FADE_AMOUNT and AMBIENCE_FADE_AMOUNT: per-update smoothing. */
inline constexpr float retail_reverb_fade_amount{0.03F};
inline constexpr float retail_ambience_fade_amount{0.03F};
/** REVERB_AMBIENCE_DUCKING_RANGE. */
inline constexpr float retail_ambience_ducking_range{0.8F};

/** Upward probes in AoS space (z down): straight up and four 0.4 tilts. */
inline constexpr std::array<std::array<float, 3U>, retail_reverb_ceiling_rays>
    retail_reverb_ceiling_directions{{
        {0.0F, 0.0F, -1.0F},
        {0.4F, 0.0F, -0.95F},
        {-0.4F, 0.0F, -0.95F},
        {0.0F, 0.4F, -0.95F},
        {0.0F, -0.4F, -0.95F},
    }};

/** Eight lateral probes, each tilted 0.4 upward. */
inline constexpr std::array<std::array<float, 3U>, retail_reverb_wall_rays>
    retail_reverb_wall_directions{{
        {-0.95F, 0.0F, -0.4F},
        {0.95F, 0.0F, -0.4F},
        {0.0F, 0.95F, -0.4F},
        {0.0F, -0.95F, -0.4F},
        {-0.95F, -0.95F, -0.4F},
        {-0.95F, 0.95F, -0.4F},
        {0.95F, -0.95F, -0.4F},
        {0.95F, 0.95F, -0.4F},
    }};

/** Targets `update_audio_effects` smooths toward: decay seconds and wet gain. */
struct ReverbTargets final {
    float size{1.0F};
    float amount{0.0F};
};

/**
 * Evaluate the retail room probe.
 *
 * `hitscan(direction)` returns the distance from the listener to the solid
 * cell a `world.hitscan` ray reports, or nothing on a miss. Outdoors -- fewer
 * than 4/5 ceiling hits or fewer than 6/8 walls within 40 blocks -- the
 * targets are size 1.0 and amount 0, i.e. no reverb at all.
 */
template <class Hitscan>
[[nodiscard]] ReverbTargets probe_retail_reverb(Hitscan&& hitscan) {
    std::size_t ceiling_hits{};
    for (const auto& direction : retail_reverb_ceiling_directions) {
        const std::optional<float> hit = hitscan(direction);
        if (hit.has_value()) {
            ++ceiling_hits;
        }
    }
    if (ceiling_hits < retail_reverb_ceiling_hits_required) {
        return {};
    }
    std::size_t walls{};
    float total{};
    for (const auto& direction : retail_reverb_wall_directions) {
        const std::optional<float> hit = hitscan(direction);
        if (hit.has_value() && std::isfinite(*hit) && *hit < retail_reverb_max_wall_distance) {
            ++walls;
            total += *hit;
        }
    }
    if (walls < retail_reverb_min_walls) {
        return {};
    }
    const float mean = total / static_cast<float>(walls);
    const float size = std::min(retail_reverb_decay_scale * mean, retail_reverb_max_decay);
    const float amount = retail_reverb_max_gain *
                         static_cast<float>(walls - (retail_reverb_min_walls - 1U)) /
                         static_cast<float>(retail_reverb_wall_rays - (retail_reverb_min_walls - 1U));
    return {size, amount};
}

/** Smoothed `global_reverb_size`, `global_reverb_amount`, `global_ambience_ducking`. */
struct EnvironmentAudioState final {
    float reverb_size{1.0F};
    float reverb_amount{0.0F};
    float ambience_ducking{0.0F};
};

/** One `GameScene.update` smoothing step (0.03 blend per update). */
[[nodiscard]] constexpr EnvironmentAudioState step_environment_audio(EnvironmentAudioState state,
                                                                     ReverbTargets targets) noexcept {
    state.reverb_size = state.reverb_size * (1.0F - retail_reverb_fade_amount) +
                        targets.size * retail_reverb_fade_amount;
    state.reverb_amount = state.reverb_amount * (1.0F - retail_reverb_fade_amount) +
                          targets.amount * retail_reverb_fade_amount;
    state.ambience_ducking = state.ambience_ducking * (1.0F - retail_ambience_fade_amount) +
                             (targets.amount / retail_reverb_max_gain) * retail_ambience_fade_amount;
    return state;
}

/** Ambience loop volume after `GameScene.update`'s indoor ducking. */
[[nodiscard]] constexpr float ducked_ambience_volume(float volume, float ducking) noexcept {
    return volume * (1.0F - ducking * retail_ambience_ducking_range);
}

// ---------------------------------------------------------------------------
// AmbientSound.update (gameScene 0x1010b330)
// ---------------------------------------------------------------------------

/** `AmbientSound.closest_point`: projection onto one clamped segment. */
[[nodiscard]] constexpr SoundPosition
closest_point_on_segment(SoundPosition a, SoundPosition b, SoundPosition point) noexcept {
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float dz = b.z - a.z;
    const float length_squared = dx * dx + dy * dy + dz * dz;
    if (!(length_squared > 0.0F)) {
        return a;
    }
    float t = ((point.x - a.x) * dx + (point.y - a.y) * dy + (point.z - a.z) * dz) / length_squared;
    t = t < 0.0F ? 0.0F : (t > 1.0F ? 1.0F : t);
    return {a.x + dx * t, a.y + dy * t, a.z + dz * t};
}

/** What one emitter update changes: a new position, a new volume, or both absent. */
struct AmbientEmitterUpdate final {
    std::optional<SoundPosition> position;
    std::optional<float> volume;
};

/**
 * Port of `AmbientSound.update`:
 *
 * - <= 1 point: a global bed, `(None, volume)`.
 * - exactly 2 points: positional at the closest point of the segment,
 *   `(closest_point, None)` -- the loop keeps its own OpenAL falloff.
 * - >= 3 points: non-positional with the scripted inverse falloff
 *   `volume / (1 + attenuation * (max(d, 1) - 1))` to the nearest point on
 *   the polyline.
 */
[[nodiscard]] inline AmbientEmitterUpdate
retail_ambient_emitter_update(std::span<const SoundPosition> points,
                              SoundPosition listener,
                              float volume,
                              float attenuation) noexcept {
    if (points.size() <= 1U) {
        return {std::nullopt, volume};
    }
    SoundPosition best = points.front();
    float best_squared = std::numeric_limits<float>::max();
    for (std::size_t index{}; index + 1U < points.size(); ++index) {
        const auto candidate = closest_point_on_segment(points[index], points[index + 1U], listener);
        const float x = candidate.x - listener.x;
        const float y = candidate.y - listener.y;
        const float z = candidate.z - listener.z;
        const float squared = x * x + y * y + z * z;
        if (squared < best_squared) {
            best_squared = squared;
            best = candidate;
        }
    }
    if (points.size() == 2U) {
        return {best, std::nullopt};
    }
    const float distance = std::max(std::sqrt(best_squared), 1.0F);
    return {std::nullopt, volume / (1.0F + attenuation * (distance - 1.0F))};
}

/** Inverse-distance-clamped gain OpenAL applies with retail parameters. */
[[nodiscard]] inline float retail_positional_gain(float gain, float distance,
                                                  float rolloff = retail_default_attenuation) noexcept {
    const float clamped = std::max(distance, retail_reference_distance);
    return gain * retail_reference_distance /
           (retail_reference_distance + rolloff * (clamped - retail_reference_distance));
}

} // namespace battlespades::audio
