#include "battlespades/audio/openal_frontend_audio.hpp"
#include "battlespades/audio/retail_event_cues.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using namespace battlespades::audio;

void expect(bool value, const std::string& message) {
    if (!value) {
        throw std::runtime_error{message};
    }
}

void pickup_cues_cover_bomb_and_diamond_only() {
    expect(pickup_carry_cue(14U).has_value() && pickup_carry_cue(14U)->group == "bomb_pickup",
           "bomb carry plays bomb_pickup");
    expect(pickup_carry_cue(15U).has_value() &&
               pickup_carry_cue(15U)->group == "diamond_pickup" &&
               pickup_carry_cue(15U)->pitch_semitones[0U] == -0.4F,
           "diamond carry plays diamond_pickup at +-0.4 semitones");
    expect(!pickup_carry_cue(16U).has_value() && !pickup_carry_cue(0xFFU).has_value(),
           "intel and no-pickup are server-announced or silent");
}

void airstrike_uses_its_own_bank() {
    expect(airstrike_explode_cue(false).group == "airstrike_explode" &&
               airstrike_explode_cue(true).group == "airstrike_explode_water",
           "entity 17 must use the airstrike blast, not the generic explode");
}

void rockets_pick_their_flight_loop_from_the_owner_tool() {
    expect(rocket_flight_kind(21U, std::uint8_t{12U}) == RocketFlightKind::rpg,
           "an RPG holder's rocket is an RPG rocket");
    expect(rocket_flight_kind(21U, std::uint8_t{3U}) == RocketFlightKind::turret &&
               rocket_flight_kind(21U, std::nullopt) == RocketFlightKind::turret,
           "any other owner tool means a turret rocket");
    expect(rocket_flight_kind(22U, std::nullopt) == RocketFlightKind::rocket2,
           "entity 22 is Rocket2");
    expect(rocket_flight_kind(23U, std::nullopt) == RocketFlightKind::none,
           "the drill keeps its own path");
    expect(rocket_flight_loop(RocketFlightKind::rpg) == "rocket_projectile" &&
               rocket_flight_loop(RocketFlightKind::rocket2) == "rocket_trip_projectile" &&
               rocket_flight_loop(RocketFlightKind::turret) == "turr_rocket_projectile",
           "rocket.py / rocket2.py loop stems");
    expect(rocket_explosion_tool(RocketFlightKind::turret) == 20U &&
               rocket_explosion_tool(RocketFlightKind::rpg) == 12U,
           "turret rockets use the turr_rocketexplode bank");
    expect(airstrike_explosion_volume == 1.0F,
           "Airstrike.delete plays its explosion at volume 1.0 (no x1.6 boost)");
}

void turret_aim_detector_matches_rocket_turret_update() {
    TurretAimDetector detector;
    const double dt = 1.0 / 60.0;
    expect(detector.update(0.0, 0.0, dt) == TurretAimDetector::Edge::none,
           "the first sample only seeds the detector");
    expect(detector.update(0.0, 0.0, dt) == TurretAimDetector::Edge::none,
           "a still turret is not aiming");
    expect(detector.update(0.0, 1.0, dt) == TurretAimDetector::Edge::started,
           "turning faster than 1 degree/s starts aiming");
    expect(detector.update(0.0, 2.0, dt) == TurretAimDetector::Edge::none,
           "continuous motion keeps the loop");
    int frames = 0;
    auto edge = TurretAimDetector::Edge::none;
    while (edge == TurretAimDetector::Edge::none && frames < 120) {
        edge = detector.update(0.0, 2.0, dt);
        ++frames;
    }
    expect(edge == TurretAimDetector::Edge::stopped && frames >= 12 && frames <= 14,
           "aiming stops after the 0.2 s tolerance");
}

void double_shotgun_alternates_barrels() {
    expect(double_shotgun_barrel_stem(false) == "shotgun_double_fire01" &&
               double_shotgun_barrel_stem(true) == "shotgun_double_fire02",
           "Shotgun2Weapon alternates its two barrel samples");
    expect(!double_shotgun_second_barrel(1U) && double_shotgun_second_barrel(0U),
           "two shells -> first barrel, the last shell -> second barrel");
}

void bounce_and_build_cues_follow_retail() {
    expect(!grenade_bounce_audible(3.2) && grenade_bounce_audible(3.3),
           "a grenade at rest must not click; a real bounce above 0.1/tick sounds");
    expect(grenade_bounce_cue.group == "grenadebounce", "GRENADE_BOUNCE_SOUND");
    expect(build_error_cue.group == "build_error" &&
               build_error_cue.pitch_semitones[0U] == 0.0F,
           "BUILD_ERROR_SOUND is unpitched");
    expect(build_light_cue.group == "build_light" &&
               build_light_cue.pitch_semitones[1U] == 0.0F,
           "build_light uses media.play, so no pitch variation");
}

void voice_pool_follows_decision_d5() {
    const OpenAlFrontendAudioConfig config;
    expect(config.max_one_shot_voices == 128U, "retail opened 128 one-shot sources");
    expect(valid_openal_frontend_audio_config(config), "the retail default must validate");
    OpenAlFrontendAudioConfig too_many;
    too_many.max_one_shot_voices = 129U;
    expect(!valid_openal_frontend_audio_config(too_many), "more than 128 is rejected");
    expect(minimum_one_shot_voices == 64U, "a short device keeps a 64-voice floor");
}

} // namespace

int main() {
    try {
        pickup_cues_cover_bomb_and_diamond_only();
        airstrike_uses_its_own_bank();
        rockets_pick_their_flight_loop_from_the_owner_tool();
        turret_aim_detector_matches_rocket_turret_update();
        double_shotgun_alternates_barrels();
        bounce_and_build_cues_follow_retail();
        voice_pool_follows_decision_d5();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
