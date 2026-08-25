#include "battlespades/world/class_voice.hpp"

#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using namespace battlespades::world;

void expect(bool value, const std::string& message) {
    if (!value) {
        throw std::runtime_error{message};
    }
}

/** Every stem the table names must ship, or the line silently never plays. */
void every_named_line_ships() {
    const std::filesystem::path sounds =
        std::filesystem::path{AOS_TEST_ASSET_ROOT} / "sounds";
    if (!std::filesystem::is_directory(sounds)) {
        return;
    }
    std::size_t checked{};
    for (const auto& set : class_voice_table()) {
        for (std::uint8_t slot{}; slot < static_cast<std::uint8_t>(ClassVoice::count);
             ++slot) {
            for (const auto stem : set.bank(static_cast<ClassVoice>(slot)).stems) {
                const auto path = sounds / (std::string{stem} + ".ogg");
                expect(std::filesystem::is_regular_file(path),
                       "missing voice sample: " + std::string{stem});
                ++checked;
            }
        }
    }
    expect(checked > 300U, "the voice table looks suspiciously small");
}

/** Silence is authored. Some classes genuinely have no speech. */
void deliberate_silences_are_preserved() {
    std::size_t voiced_spawn{};
    std::size_t silent_spawn{};
    for (const auto& set : class_voice_table()) {
        (set.spawn.stems.empty() ? silent_spawn : voiced_spawn) += 1U;
    }
    expect(voiced_spawn > 0U, "no class has a spawn line at all");
    expect(silent_spawn > 0U,
           "every class got a spawn line; the deliberate silences were lost");
}

/** Retail's nested PERIODIC_SOUND tuple belongs to all three Zombie classes. */
void zombie_idle_groans_survive_generation() {
    constexpr std::array<std::uint8_t, 3U> zombie_classes{4U, 14U, 15U};
    for (const auto class_id : zombie_classes) {
        const auto* zombie = find_class_voice(class_id);
        expect(zombie != nullptr, "Zombie voice class must exist");
        expect(zombie->periodic.stems.size() == 16U,
               "Zombie periodic bank must contain all sixteen groans");
        expect(zombie->periodic.chance == 100U,
               "Zombie periodic groans must retain their authored chance");
        expect(zombie->periodic.no_consecutive_repeat,
               "Zombie periodic groans must not repeat the same take");
    }
    const auto* soldier = find_class_voice(0U);
    expect(soldier != nullptr && soldier->periodic.stems.empty(),
           "ordinary classes must not inherit the Zombie idle loop");
}

/** A negative chance is a no-repeat flag, not a negative probability. */
void the_no_repeat_flag_is_honoured() {
    const auto* soldier = find_class_voice(0U);
    expect(soldier != nullptr, "class 0 must exist");
    const auto& bank = soldier->jump;
    expect(!bank.stems.empty(), "the soldier must have a jump vocal");
    expect(bank.chance > 0U && bank.chance <= 100U,
           "a negative chance leaked through as a probability");
    if (!bank.no_consecutive_repeat) {
        return;
    }
    VoiceSelectionState state;
    const auto first = choose_voice_line(bank, 0U, 3U, state);
    expect(!first.empty(), "the first no-repeat line must play");
    expect(choose_voice_line(bank, 0U, 3U, state).empty(),
           "retail must suppress the trigger immediately after a no-repeat line");
    const auto third = choose_voice_line(bank, 0U, 3U, state);
    expect(!third.empty() && third != first,
           "the next allowed trigger must not reuse the prior take");
}

/** A zero chance or an empty bank must never produce a line. */
void silent_banks_stay_silent() {
    ClassVoiceBank empty;
    VoiceSelectionState state;
    expect(choose_voice_line(empty, 0U, 0U, state).empty(),
           "an empty bank produced a line");
}

/** Retail randint(0, 100) is inclusive at both ends. */
void voice_chance_uses_101_outcomes() {
    constexpr std::array<std::string_view, 1U> stems{"line"};
    ClassVoiceBank bank{stems, 25U, false};
    VoiceSelectionState state;
    expect(!choose_voice_line(bank, 25U, 0U, state).empty(),
           "chance 25 must accept retail roll 25");
    expect(choose_voice_line(bank, 26U, 0U, state).empty(),
           "chance 25 must reject retail roll 26");
}

/** Local respawn VO is delayed and only class changes override its 25% bank. */
void local_spawn_voice_matches_retail_lifecycle() {
    expect(local_spawn_voice_delay(0U) == 1.0 &&
               local_spawn_voice_delay(1000U) == 2.0,
           "local spawn voice delay must cover retail's exact 1-2 second range");

    LocalSpawnVoiceState state;
    begin_local_spawn_voice(state, 0U, 1.25);
    expect(!step_local_spawn_voice(state, 1.24).has_value(),
           "the local spawn line fired before its Character timer expired");
    const auto first = step_local_spawn_voice(state, 0.02);
    expect(first.has_value() && first->class_id == 0U && first->force,
           "the first local life must force its class spawn bank");
    expect(!step_local_spawn_voice(state, 10.0).has_value(),
           "a local spawn latch emitted more than once for one life");

    begin_local_spawn_voice(state, 0U, 1.0);
    const auto same_class = step_local_spawn_voice(state, 1.0);
    expect(same_class.has_value() && !same_class->force,
           "same-class respawn must retain the authored 25/101 chance");

    begin_local_spawn_voice(state, 16U, 2.0);
    expect(!step_local_spawn_voice(state, 1.99).has_value(),
           "class-change spawn line ignored its randomized delay");
    const auto changed = step_local_spawn_voice(state, 0.02);
    expect(changed.has_value() && changed->class_id == 16U && changed->force,
           "class-change respawn must force the new class spawn bank");
}

/** The idle vocal waits before its first line and then re-arms. */
void the_idle_voice_repeats() {
    PeriodicVoiceState state;
    expect(!step_periodic_voice(state, 1.0 / 60.0, 0U),
           "the idle voice fired on its very first tick");
    std::size_t fired{};
    for (int tick{}; tick < 60 * 30; ++tick) {
        if (step_periodic_voice(state, 1.0 / 60.0, static_cast<std::uint32_t>(tick))) {
            ++fired;
        }
    }
    // 30 s at a 3-6 s interval is between 5 and 10 lines.
    expect(fired >= 4U && fired <= 11U,
           "idle vocal cadence is wrong: " + std::to_string(fired) + " in 30 s");
}

} // namespace

int main() {
    try {
        every_named_line_ships();
        deliberate_silences_are_preserved();
        zombie_idle_groans_survive_generation();
        the_no_repeat_flag_is_honoured();
        silent_banks_stay_silent();
        voice_chance_uses_101_outcomes();
        local_spawn_voice_matches_retail_lifecycle();
        the_idle_voice_repeats();
        std::cout << "class voice tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
