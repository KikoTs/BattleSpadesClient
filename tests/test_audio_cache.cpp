#include "battlespades/audio/openal_frontend_audio.hpp"
#include "battlespades/core/runtime_module.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

void expect(bool value, const std::string& message) {
    if (!value) {
        throw std::runtime_error{message};
    }
}

/** Pumps tick() until every worker decode has been uploaded (or times out). */
void drain(battlespades::audio::OpenAlFrontendAudio& audio) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{120};
    battlespades::core::TickContext context;
    context.fixed_delta = std::chrono::nanoseconds{16'666'666};
    while (audio.pending_named_decodes() != 0U) {
        expect(std::chrono::steady_clock::now() < deadline, "worker decodes never completed");
        static_cast<void>(audio.tick(context));
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
        ++context.index;
    }
    static_cast<void>(audio.tick(context));
}

} // namespace

int main() {
    try {
        namespace audio = battlespades::audio;
        const std::filesystem::path asset_root{AOS_TEST_ASSET_ROOT};
        audio::OpenAlFrontendAudio sound{{asset_root, 48U, 0.0F, 1.0F, false}};
        if (!sound.start()) {
            std::cout << "SKIP: no OpenAL playback device (" << sound.last_error() << ")\n";
            return 0;
        }
        expect(sound.set_master_volume(0.0F), "could not mute the audio cache probe");

        // Cold named one-shots resolve (true) without decoding on this
        // thread; a missing asset is still reported as missing.
        expect(sound.play_named_one_shot("build", {}, 1.0F, true),
               "an existing named sound must be accepted on first use");
        expect(!sound.play_named_one_shot("definitely_missing_stem_r5", {}, 1.0F, true),
               "a missing named sound must be rejected");
        drain(sound);
        expect(sound.play_named_one_shot("build", {}, 1.0F, true),
               "a cached named sound must play from the stem cache");

        // Touch every ambient bed: the resident decoded PCM must stay within
        // the LRU budget instead of growing with every map in a rotation.
        std::vector<std::string> stems;
        for (const auto& entry : std::filesystem::directory_iterator(asset_root / "ambients")) {
            if (entry.path().extension() == ".ogg") {
                stems.push_back(entry.path().stem().string());
            }
        }
        std::ranges::sort(stems);
        expect(stems.size() >= 8U, "the retail ambient beds must be present");
        std::size_t peak{};
        for (const auto& stem : stems) {
            expect(sound.preload_named_ambience(stem), "ambient bed must exist: " + stem);
            drain(sound);
            peak = std::max(peak, sound.resident_music_ambience_bytes());
            expect(sound.resident_music_ambience_bytes() <=
                       audio::OpenAlFrontendAudio::music_ambience_pcm_budget_bytes,
                   "resident music/ambience PCM exceeded its budget after " + stem);
        }
        // A bed that was evicted decodes again (off-thread) and still plays.
        expect(sound.play_named_ambience(stems.front(), 1.0F), "evicted bed must replay");
        drain(sound);
        expect(sound.resident_music_ambience_bytes() <=
                   audio::OpenAlFrontendAudio::music_ambience_pcm_budget_bytes,
               "a playing bed must not push the cache past its budget");
        sound.stop();
        std::cout << "audio cache: " << stems.size() << " beds, peak resident "
                  << peak / (1024U * 1024U) << " MiB (budget "
                  << audio::OpenAlFrontendAudio::music_ambience_pcm_budget_bytes /
                         (1024U * 1024U)
                  << " MiB)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
