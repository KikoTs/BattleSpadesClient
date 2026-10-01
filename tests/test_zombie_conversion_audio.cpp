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
        constexpr std::uint8_t zombie{4U};
        constexpr std::uint8_t legacy_zombie{14U};
        constexpr std::uint8_t fast_zombie{15U};

        // Retail: the client never synthesises SOUND_MAP 28; the server
        // sends it once per outbreak. An ordinary Zombie kill converts the
        // victim through CreatePlayer and must stay silent.
        expect(!Gate::plays_on_conversion(std::nullopt, fast_zombie) &&
                   !Gate::plays_on_conversion(soldier, fast_zombie) &&
                   !Gate::plays_on_conversion(rocketeer, zombie) &&
                   !Gate::plays_on_conversion(soldier, legacy_zombie) &&
                   !Gate::plays_on_conversion(zombie, zombie),
               "a human-to-Zombie CreatePlayer must never play zombie_become");

        expect(gate.observe_explicit(start),
               "the server's outbreak PlaySound(28) must play");
        expect(!gate.observe_explicit(start + 100ms),
               "a duplicate PlaySound(28) inside 300 ms must collapse");
        expect(gate.observe_explicit(start + 60s),
               "the next outbreak's PlaySound(28) must play again");
        gate.reset();
        expect(gate.observe_explicit(start + 60s),
               "session reset must clear the deduplication window");

        std::cout << "zombie conversion audio tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
