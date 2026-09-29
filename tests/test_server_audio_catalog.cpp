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

void weapon_reports_use_retail_tool_volume_and_falloff() {
    // Tool.play_sound plays every shot, loop and tail at 0.5 for the shooter
    // and for observers alike; observers get DEFAULT_ATTENUATION 0.15.
    expect(local_weapon_report_gain() == 0.5F,
           "the local report must use Tool.play_sound volume 0.5");
    expect(remote_weapon_report_gain() == 0.5F,
           "observer reports must use Tool.play_sound volume 0.5");
    expect(spatial_rolloff(SpatialSoundProfile::weapon_report) == 0.15F &&
               spatial_rolloff(SpatialSoundProfile::ordinary) == 0.15F,
           "every world cue uses retail DEFAULT_ATTENUATION 0.15");
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
        weapon_reports_use_retail_tool_volume_and_falloff();
        every_mapped_server_sound_exists();
        std::cout << "server audio catalog tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
