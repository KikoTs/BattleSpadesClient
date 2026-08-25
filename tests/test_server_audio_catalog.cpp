#include "battlespades/audio/server_audio_catalog.hpp"
#include "battlespades/audio/sound_groups.hpp"

#include <cstdint>
#include <filesystem>
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

void retail_distance_is_a_hard_sphere() {
    const SoundPosition listener{10.0F, 20.0F, 30.0F};
    expect(within_retail_hearing_distance(
               listener, {60.0F, 20.0F, 30.0F}),
           "a cue exactly 50 blocks away must remain audible");
    expect(!within_retail_hearing_distance(
               listener, {60.01F, 20.0F, 30.0F}),
           "a cue beyond retail HEARING_DISTANCE must be rejected");
    expect(!within_retail_hearing_distance(
               listener, {1000.0F, 1000.0F, 1000.0F}),
           "map-wide distant sounds must never leak through OpenAL clamping");
}

void weapon_reports_survive_full_cover_without_leaking_map_audio() {
    expect(profiled_spatial_transmission(SpatialSoundProfile::ordinary, 0.18F) ==
               0.35F,
           "ordinary terrain audio must retain an audible covered-path floor");
    expect(profiled_spatial_transmission(SpatialSoundProfile::weapon_report, 0.18F) ==
               0.70F,
           "a weapon report behind full cover must remain audible");
    expect(profiled_spatial_transmission(SpatialSoundProfile::weapon_report, 0.0F) ==
               0.0F,
           "an explicitly rejected sound path must remain silent");
    expect(spatial_rolloff(SpatialSoundProfile::weapon_report) <
               spatial_rolloff(SpatialSoundProfile::ordinary),
           "weapon reports must carry farther inside retail's hard range");
    expect(local_weapon_report_gain() < 1.0F,
           "a head-relative local gun must leave headroom for damage and movement cues");
    expect(remote_weapon_report_gain() > 1.0F,
           "a positional observer gun must compensate before distance attenuation");
    expect(remote_weapon_report_gain() > local_weapon_report_gain(),
           "local and remote firearm paths must not collapse to the same flat mix");
}

void every_mapped_server_sound_exists() {
    const std::filesystem::path root{AOS_TEST_ASSET_ROOT};
    const auto sounds = root / "sounds";
    expect(std::filesystem::is_directory(sounds),
           "the original sound tree must be available");
    for (std::uint16_t id{}; id <= 60U; ++id) {
        const auto group =
            server_sound_group(static_cast<std::uint8_t>(id));
        if (group.empty()) {
            continue; // Deliberate holes in retail SOUND_MAP.
        }
        const auto stems = sound_group_stems(group);
        expect(!stems.empty(), "mapped server sound has no variants");
        for (const auto& stem : stems) {
            expect(std::filesystem::is_regular_file(
                       sounds / (stem + ".ogg")),
                   "PlaySound(" + std::to_string(id) +
                       ") references missing " + stem + ".ogg");
        }
    }
    expect(server_sound_group(61U).empty() &&
               server_sound_group(255U).empty(),
           "out-of-range sound ids must fail silent");
}

} // namespace

int main() {
    try {
        retail_distance_is_a_hard_sphere();
        weapon_reports_survive_full_cover_without_leaking_map_audio();
        every_mapped_server_sound_exists();
        std::cout << "server audio catalog tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
