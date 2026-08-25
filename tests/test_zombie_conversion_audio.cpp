#include "battlespades/frontend/zombie_conversion_audio.hpp"

#include <chrono>
#include <iostream>
#include <stdexcept>

namespace {

void expect(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error{message};
    }
}

} // namespace

int main() {
    try {
        using Gate = battlespades::frontend::ZombieConversionAudioGate;
        using namespace std::chrono_literals;
        Gate gate;
        const Gate::time_point start{};
        constexpr std::uint8_t soldier{1U};
        constexpr std::uint8_t rocketeer{2U};
        constexpr std::uint8_t engineer{3U};
        constexpr std::uint8_t zombie{4U};
        constexpr std::uint8_t legacy_zombie{14U};
        constexpr std::uint8_t fast_zombie{15U};

        expect(!gate.observe_conversion(std::nullopt, fast_zombie, start),
               "initial Zombie roster creation must not synthesize a conversion cue");
        expect(gate.observe_conversion(soldier, fast_zombie, start + 1s),
               "human-to-Zombie CreatePlayer must emit the global cue");
        expect(!gate.observe_explicit(start + 1100ms),
               "the matching explicit server sound must be deduplicated");
        expect(!gate.observe_conversion(fast_zombie, fast_zombie, start + 2s) &&
                   !gate.observe_conversion(soldier, engineer, start + 2s),
               "Zombie respawn and ordinary class changes must remain silent");
        expect(gate.observe_explicit(start + 3s) &&
                   !gate.observe_conversion(soldier, legacy_zombie, start + 3100ms),
               "explicit-first outbreak ordering must also produce one cue");
        expect(gate.observe_conversion(rocketeer, zombie, start + 4s),
               "a later victim conversion must play its own global cue");
        gate.reset();
        expect(gate.observe_explicit(start),
               "session reset must clear the conversion deduplication window");

        std::cout << "zombie conversion audio tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
