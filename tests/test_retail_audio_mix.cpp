#include "battlespades/audio/openal_frontend_audio.hpp"
#include "battlespades/audio/retail_mix.hpp"
#include "battlespades/audio/sound_groups.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace battlespades::audio;

void expect(bool value, const std::string& message) {
    if (!value) {
        throw std::runtime_error{message};
    }
}

[[nodiscard]] bool approx(float left, float right, float tolerance = 1.0e-4F) {
    return std::fabs(left - right) <= tolerance;
}

using Direction = std::array<float, 3U>;

/** A probe answering from a fixed ceiling distance and one wall distance per ray. */
struct RoomProbe final {
    std::optional<float> ceiling;
    std::array<std::optional<float>, retail_reverb_wall_rays> walls{};

    [[nodiscard]] std::optional<float> operator()(const Direction& direction) const {
        for (const auto& up : retail_reverb_ceiling_directions) {
            if (up == direction) {
                return ceiling;
            }
        }
        for (std::size_t index{}; index < retail_reverb_wall_directions.size(); ++index) {
            if (retail_reverb_wall_directions[index] == direction) {
                return walls[index];
            }
        }
        throw std::runtime_error{"probe asked an unknown retail direction"};
    }
};

void reverb_is_dry_outdoors() {
    RoomProbe open_sky;
    const auto targets = probe_retail_reverb(open_sky);
    expect(targets.amount == 0.0F && targets.size == 1.0F,
           "open terrain must have no reverb (size 1, amount 0)");

    // A ceiling but open sides: fewer than REVERB_MIN_WALLS walls.
    RoomProbe porch;
    porch.ceiling = 3.0F;
    for (std::size_t index{}; index < 5U; ++index) {
        porch.walls[index] = 4.0F;
    }
    expect(probe_retail_reverb(porch).amount == 0.0F,
           "five walls within 40 is still outdoors");

    // Walls but no ceiling: the lateral probe never runs.
    RoomProbe courtyard;
    courtyard.walls.fill(5.0F);
    expect(probe_retail_reverb(courtyard).amount == 0.0F,
           "a roofless courtyard must stay dry");

    // Walls past REVERB_MAX_WALL_DISTANCE do not count.
    RoomProbe hall;
    hall.ceiling = 10.0F;
    hall.walls.fill(40.0F);
    expect(probe_retail_reverb(hall).amount == 0.0F, "a wall at exactly 40 does not count");
}

void reverb_indoor_targets_match_retail() {
    RoomProbe room;
    room.ceiling = 2.0F;
    room.walls.fill(5.0F);
    auto targets = probe_retail_reverb(room);
    expect(approx(targets.amount, 0.1F), "8 walls give REVERB_MAX_GAIN 0.1");
    expect(approx(targets.size, 0.5F), "a 5-block room decays in 0.5 s");

    room.walls[7U].reset();
    targets = probe_retail_reverb(room);
    expect(approx(targets.amount, 0.1F * 2.0F / 3.0F), "7 walls give 0.067");

    room.walls[6U].reset();
    targets = probe_retail_reverb(room);
    expect(approx(targets.amount, 0.1F / 3.0F), "6 walls give 0.033");

    RoomProbe cavern;
    cavern.ceiling = 30.0F;
    cavern.walls.fill(35.0F);
    expect(approx(probe_retail_reverb(cavern).size, 2.2F),
           "decay is capped at REVERB_MAX_DECAY_TIME 2.2 s");

    // Four of five ceiling rays is enough.
    int calls{};
    const auto four_of_five = [&](const Direction& direction) -> std::optional<float> {
        if (direction == retail_reverb_ceiling_directions.front()) {
            ++calls;
            return std::nullopt;
        }
        return 3.0F;
    };
    expect(approx(probe_retail_reverb(four_of_five).amount, 0.1F),
           "4/5 ceiling hits count as indoors");
    expect(calls == 1, "the straight-up ray is probed once");
}

void environment_smoothing_matches_update_audio_effects() {
    EnvironmentAudioState state{};
    const ReverbTargets indoor{0.5F, 0.1F};
    state = step_environment_audio(state, indoor);
    expect(approx(state.reverb_amount, 0.003F), "amount blends 3 % per update");
    expect(approx(state.reverb_size, 0.985F), "size blends 3 % per update");
    expect(approx(state.ambience_ducking, 0.03F), "ducking tracks amount / REVERB_MAX_GAIN");
    for (int update{}; update < 600; ++update) {
        state = step_environment_audio(state, indoor);
    }
    expect(approx(state.reverb_amount, 0.1F, 1.0e-3F) && approx(state.ambience_ducking, 1.0F, 1.0e-2F),
           "ten seconds indoors converges to the room targets");
    expect(approx(ducked_ambience_volume(1.0F, 1.0F), 0.2F),
           "full ducking leaves 20 % ambience (REVERB_AMBIENCE_DUCKING_RANGE 0.8)");
    expect(ducked_ambience_volume(0.7F, 0.0F) == 0.7F, "outdoors the bed is untouched");
}

void ambient_emitters_follow_ambient_sound_update() {
    const SoundPosition listener{10.0F, 5.0F, 0.0F};
    const std::vector<SoundPosition> none;
    auto update = retail_ambient_emitter_update(none, listener, 0.6F, 0.15F);
    expect(!update.position && update.volume && *update.volume == 0.6F,
           "no points is a global bed at its own volume");

    const std::vector<SoundPosition> segment{{0.0F, 0.0F, 0.0F}, {20.0F, 0.0F, 0.0F}};
    update = retail_ambient_emitter_update(segment, listener, 0.6F, 0.15F);
    expect(update.position && !update.volume, "two points are positional only");
    expect(approx(update.position->x, 10.0F) && approx(update.position->y, 0.0F),
           "the emitter snaps to the closest point on the segment, not a vertex");

    const std::vector<SoundPosition> river{
        {0.0F, 0.0F, 0.0F}, {20.0F, 0.0F, 0.0F}, {20.0F, 20.0F, 0.0F}};
    update = retail_ambient_emitter_update(river, listener, 0.6F, 0.15F);
    expect(!update.position && update.volume, ">= 3 points are non-positional");
    expect(approx(*update.volume, 0.6F / (1.0F + 0.15F * 4.0F)),
           "scripted inverse falloff over the polyline distance");

    update = retail_ambient_emitter_update(river, {10.0F, 0.5F, 0.0F}, 0.6F, 0.15F);
    expect(approx(*update.volume, 0.6F), "inside one block the scripted volume is unattenuated");

    const auto clamped = closest_point_on_segment({0.0F, 0.0F, 0.0F}, {10.0F, 0.0F, 0.0F}, {-5.0F, 3.0F, 0.0F});
    expect(clamped.x == 0.0F && clamped.y == 0.0F, "projection clamps to the segment end");
}

void positional_mix_uses_retail_openal_defaults() {
    expect(retail_reference_distance == 1.0F, "retail never sets AL_REFERENCE_DISTANCE");
    expect(approx(retail_positional_gain(0.5F, 5.0F), 0.5F / 1.6F),
           "an observer shot at 5 blocks is 0.31");
    expect(approx(retail_positional_gain(0.5F, 20.0F), 0.5F / 3.85F), "0.130 at 20 blocks");
    expect(approx(retail_positional_gain(0.5F, 50.0F), 0.5F / 8.35F), "0.060 at 50 blocks");
    expect(retail_positional_gain(1.0F, 0.2F) == 1.0F, "inside the reference distance is full");
    const auto offset = retail_source_position({10.0F, 20.0F, 30.0F});
    expect(offset.x == 10.5F && offset.y == 20.5F && offset.z == 29.5F,
           "GameSound.set_position: +0.5 x/y and half a block up");
    // Measured by rendering a 1 kHz tone through retail's own OpenAL Soft
    // 1.13 DLL (wave backend): a source 11 units away with rolloff 0.15 comes
    // out at exactly 1/2.5 of the same source at 1 unit.
    expect(approx(retail_positional_gain(1.0F, 11.0F), 1.0F / 2.5F),
           "retail OpenAL 1.13 measured 0.4 at 11 blocks");
    expect(!retail_context_hrtf, "retail's OpenAL Soft 1.13 has no HRTF");
}

void volumes_and_pitches_match_the_retail_rows() {
    expect(retail_tool_sound_volume == 0.5F, "Tool.play_sound is 0.5");
    expect(retail_character_sound_volume == 1.0F, "Character.play_sound defaults to 1.0");
    expect(retail_entity_sound_volume == 0.75F, "Entity.play_sound defaults to 0.75");
    expect(retail_crate_chute_open_volume == 4.0F, "the crate chute opens at volume 4");
    expect(retail_footstep_pitch.maximum == 1.5F && retail_movement_impact_pitch.maximum == 0.8F,
           "footsteps +-1.5, jump/land/fall-hurt +-0.8 semitones");
    expect(retail_bullet_hit_pitch.maximum == 1.2F, "bullet scenery hits +-1.2");
    expect(retail_cue_group_pitch("woosh", false).maximum == 0.4F, "DIG_MISS woosh +-0.4");
    expect(retail_cue_group_pitch("woosh", true).maximum == 0.8F, "GRENADE_THROW woosh +-0.8");
    expect(retail_cue_group_pitch("pin", true).maximum == 0.0F, "the pin is unpitched");
    expect(retail_cue_group_pitch("molotov_throw", true).minimum == -0.8F, "molotov throw +-0.8");

    float lowest{2.0F};
    float highest{0.0F};
    for (std::uint32_t draw{}; draw < 400U; ++draw) {
        const float ratio = retail_sound_pitch_ratio(
            retail_footstep_pitch.minimum, retail_footstep_pitch.maximum, draw);
        lowest = std::min(lowest, ratio);
        highest = std::max(highest, ratio);
    }
    expect(lowest < 0.95F && highest > 1.05F, "footsteps are audibly pitched");
    expect(lowest >= std::pow(2.0F, -1.5F / 12.0F) - 1.0e-4F &&
               highest <= std::pow(2.0F, 1.5F / 12.0F) + 1.0e-4F,
           "footstep pitch stays inside +-1.5 semitones");
}

void variants_never_repeat_the_last_pick() {
    std::set<std::uint32_t> seen;
    std::uint32_t last{4U};
    for (std::uint32_t draw{}; draw < 64U; ++draw) {
        const auto pick = pick_non_repeating_variant(4U, last, draw * 2654435761U);
        expect(pick < 4U, "variant index in range");
        expect(pick != last, "get_sound_name never replays the previous member");
        seen.insert(pick);
        last = pick;
    }
    expect(seen.size() == 4U, "every member is reachable");
    expect(pick_non_repeating_variant(1U, 0U, 7U) == 0U, "a single file always plays");
}

void retail_voice_budget_and_fades() {
    expect(retail_one_shot_voices == 128U, "retail one-shot budget is 128 voices");
    const OpenAlFrontendAudioConfig config;
    expect(config.max_one_shot_voices == 128U, "the default config carries 128 voices");
    expect(retail_music_fade_seconds == 6.5F, "stop_music fades over 6.5 s");
    expect(retail_secondary_bed_fade_seconds == 1.5F, "the select bed fades over 1.5 s");
}

} // namespace

void local_tool_cues_are_dry_like_retail_hud_zone() {
    // Tool.play_sound(pos=None) for the main player -> HUD zone, no reverb.
    for (const auto cue : {WeaponCue::fire_loop, WeaponCue::fire_tail, WeaponCue::spin_loop,
                           WeaponCue::melee_miss, WeaponCue::throw_release, WeaponCue::pin,
                           WeaponCue::tool_loop, WeaponCue::tool_loop_start}) {
        expect(!tool_cue_reverb_send(true, cue), "the local player's own tool cues are dry");
        expect(tool_cue_reverb_send(false, cue), "observed tool cues stay IN_WORLD (wet)");
    }
    expect(!tool_cue_reverb_send(true), "local fire/reload one-shots are dry");
    expect(tool_cue_reverb_send(true, WeaponCue::empty_fire),
           "dry-fire stays IN_WORLD even for the local player");
}

int main() {
    try {
        local_tool_cues_are_dry_like_retail_hud_zone();
        reverb_is_dry_outdoors();
        reverb_indoor_targets_match_retail();
        environment_smoothing_matches_update_audio_effects();
        ambient_emitters_follow_ambient_sound_update();
        positional_mix_uses_retail_openal_defaults();
        volumes_and_pitches_match_the_retail_rows();
        variants_never_repeat_the_last_pick();
        retail_voice_budget_and_fades();
        std::cout << "retail audio mix tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
