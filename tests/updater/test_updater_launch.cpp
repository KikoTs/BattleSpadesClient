// Steam Play chooser (with / without %command%, remembered choice, reset),
// reconstruction of the original game's command line, and the first-run
// decision table (Steam copy found or not, import failed, retail_assets
// published or not, offline).

#include "updater_test_support.hpp"

#include "battlespades/updater/launch_flow.hpp"
#include "battlespades/updater/launcher_args.hpp"

#include <algorithm>
#include <vector>

namespace up = battlespades::updater;
using updater_test::expect;

namespace {

void test_arguments_with_and_without_command() {
    // Steam: "<launcher>" %command%  ->  launcher "<aos.exe>" [steam args]
    const auto steam = up::parse_launcher_arguments(
        {"C:\\Program Files (x86)\\Steam\\steamapps\\common\\aceofspades\\aos.exe", "+connect_lobby", "1234",
         "--no-update"});
    expect(steam.dropped_command == "C:\\Program Files (x86)\\Steam\\steamapps\\common\\aceofspades\\aos.exe",
           "the retail exe is recognised");
    expect((steam.original_command ==
            std::vector<std::string>{"C:\\Program Files (x86)\\Steam\\steamapps\\common\\aceofspades\\aos.exe",
                                     "+connect_lobby", "1234", "--no-update"}),
           "the original command is kept verbatim, including arguments after it");
    expect((steam.forwarded == std::vector<std::string>{"+connect_lobby", "1234"}),
           "Steam's arguments are still forwarded to BattleSpades");

    // Start menu / desktop / non-Steam shortcut: no %command%.
    const auto start_menu = up::parse_launcher_arguments({});
    expect(start_menu.original_command.empty() && start_menu.dropped_command.empty(), "no command without Steam");

    const auto switches = up::parse_launcher_arguments({"--choose", "--reset-launch-choice"});
    expect(switches.choose && switches.reset_launch_choice && switches.forwarded.empty(), "chooser switches parsed");

    // Launcher switches before %command% are consumed, not part of the original command.
    const auto mixed = up::parse_launcher_arguments({"--choose", "D:\\Games\\AOS.EXE", "-windowed"});
    expect(mixed.choose && (mixed.original_command == std::vector<std::string>{"D:\\Games\\AOS.EXE", "-windowed"}),
           "switches before %command% are not part of it");
}

void test_decision() {
    using up::LaunchDecision;
    using up::LaunchTarget;
    up::LaunchChoiceInputs inputs;

    // Without %command% there is never a chooser, whatever is remembered.
    inputs.remembered = LaunchTarget::original;
    inputs.shift_held = true;
    inputs.force_chooser = true;
    expect(up::decide_launch(inputs) == LaunchDecision::battlespades, "no %command%: always BattleSpades");

    inputs = {};
    inputs.has_original_command = true;
    expect(up::decide_launch(inputs) == LaunchDecision::ask, "first Steam launch asks");

    inputs.remembered = LaunchTarget::original;
    expect(up::decide_launch(inputs) == LaunchDecision::original, "remembered original");
    inputs.remembered = LaunchTarget::battlespades;
    expect(up::decide_launch(inputs) == LaunchDecision::battlespades, "remembered BattleSpades");

    inputs.shift_held = true;
    expect(up::decide_launch(inputs) == LaunchDecision::ask, "Shift shows the chooser again");
    inputs.shift_held = false;
    inputs.force_chooser = true;
    expect(up::decide_launch(inputs) == LaunchDecision::ask, "--choose shows the chooser again");
}

void test_remembered_choice_and_reset() {
    updater_test::TempDir temp{"launch-choice"};
    const auto file = temp.path() / "update" / "launch-choice.json";
    std::string error;
    expect(!up::load_launch_choice(file).has_value(), "nothing remembered at first");

    expect(up::save_launch_choice(file, up::LaunchTarget::original, error), "remember original: " + error);
    expect(up::load_launch_choice(file) == up::LaunchTarget::original, "original read back");
    expect(up::save_launch_choice(file, up::LaunchTarget::battlespades, error), "remember BattleSpades: " + error);
    expect(up::load_launch_choice(file) == up::LaunchTarget::battlespades, "BattleSpades read back");

    // Reset (--reset-launch-choice): forgotten, and resetting twice is fine.
    expect(up::save_launch_choice(file, std::nullopt, error), "reset: " + error);
    expect(!up::load_launch_choice(file).has_value(), "forgotten after reset");
    expect(up::save_launch_choice(file, std::nullopt, error), "reset without a file: " + error);

    // A damaged or foreign file is "not remembered", never a crash.
    updater_test::write_file(file, "{not json");
    expect(!up::load_launch_choice(file).has_value(), "damaged file ignored");
    updater_test::write_file(file, R"({"remember": "something else"})");
    expect(!up::load_launch_choice(file).has_value(), "unknown value ignored");
}

void test_original_command_line() {
    const std::vector<std::string> command{"C:\\Program Files (x86)\\Steam\\steamapps\\common\\aceofspades\\aos.exe",
                                           "+connect_lobby", "1234"};
    const std::string plain =
        "\"C:\\Program Files (x86)\\Steam\\steamapps\\common\\aceofspades\\aos.exe\" +connect_lobby 1234";
    expect(up::build_original_command_line(command, "") == plain, "unchanged %command%");

    // Options the player had before BattleSpades: appended, as Steam does.
    expect(up::build_original_command_line(command, "blitzdev +connect 127.0.0.1:28630") ==
               plain + " blitzdev +connect 127.0.0.1:28630",
           "previous options appended");
    // A wrapper template: %command% substituted.
    expect(up::build_original_command_line(command, "\"C:\\Tools\\wrap.exe\" --log %command% -x") ==
               "\"C:\\Tools\\wrap.exe\" --log " + plain + " -x",
           "%command% template substituted");

    up::RegistrationState state;
    up::LaunchOptionRecord ours;
    ours.app_id = up::ace_of_spades_app_id;
    ours.had_previous = true;
    ours.previous = "\"D:\\BattleSpades\\BattleSpadesLauncher.exe\" %command%";
    up::LaunchOptionRecord other_game;
    other_game.app_id = 440;
    other_game.had_previous = true;
    other_game.previous = "-novid";
    up::LaunchOptionRecord none;
    none.app_id = up::ace_of_spades_app_id;
    state.launch_options = {ours, other_game, none};
    expect(up::previous_launch_options(state, up::ace_of_spades_app_id).empty(),
           "our own launcher line and other games are never reused");
    up::LaunchOptionRecord real;
    real.app_id = up::ace_of_spades_app_id;
    real.had_previous = true;
    real.previous = "  blitzdev +connect 127.0.0.1:28630 ";
    state.launch_options.push_back(real);
    expect(up::previous_launch_options(state, up::ace_of_spades_app_id) == "blitzdev +connect 127.0.0.1:28630",
           "the player's previous options are found and trimmed");
}

void test_first_run_table() {
    using A = up::FirstRunAction;
    const auto plan = [](bool found, bool import_failed, bool offered, bool reachable, bool download_failed = false) {
        up::FirstRunInputs inputs;
        inputs.game_folder_found = found;
        inputs.import_failed = import_failed;
        inputs.download_offered = offered;
        inputs.manifest_reachable = reachable;
        inputs.download_failed = download_failed;
        return up::plan_first_run(inputs);
    };

    // Steam copy found, download published: import preselected, download offered too.
    auto screen = plan(true, false, true, true);
    expect((screen.actions == std::vector<A>{A::import_detected, A::download, A::choose_folder}), "found+offered");
    expect(screen.preselected == A::import_detected && screen.note == up::FirstRunNote::none, "import preselected");

    // The import failed: the download becomes the preselected fallback.
    screen = plan(true, true, true, true);
    expect(screen.actions.front() == A::download && screen.preselected == A::download, "import failed -> download");
    expect(std::ranges::find(screen.actions, A::import_detected) != screen.actions.end(), "retry import still offered");

    // No Steam copy, download published: download preselected.
    screen = plan(false, false, true, true);
    expect((screen.actions == std::vector<A>{A::download, A::choose_folder}) && screen.preselected == A::download,
           "not found+offered");

    // A failed download is retried (resume) and stays preselected.
    screen = plan(false, false, true, true, true);
    expect(screen.preselected == A::download && screen.retry_download, "download retry");
    screen = plan(true, false, true, true, true);
    expect(screen.preselected == A::download && screen.retry_download, "download retry even with a Steam copy");

    // No retail_assets in the manifest: say so and link the download page.
    screen = plan(false, false, false, true);
    expect((screen.actions == std::vector<A>{A::choose_folder, A::open_download_page}) &&
               screen.preselected == A::choose_folder && screen.note == up::FirstRunNote::download_unavailable,
           "not found, no retail_assets");
    screen = plan(true, false, false, true);
    expect(screen.preselected == A::import_detected && screen.note == up::FirstRunNote::download_unavailable &&
               std::ranges::find(screen.actions, A::download) == screen.actions.end() &&
               screen.actions.back() == A::open_download_page,
           "found, no retail_assets");
    screen = plan(true, true, false, true);
    expect(screen.preselected == A::choose_folder && screen.actions.back() == A::open_download_page,
           "found, import failed, no retail_assets");

    // Offline: a published retail_assets cannot be used; the note says why.
    screen = plan(false, false, true, false);
    expect(screen.note == up::FirstRunNote::offline &&
               std::ranges::find(screen.actions, A::download) == screen.actions.end() &&
               screen.preselected == A::choose_folder,
           "offline");

    // Every screen preselects one of its own actions.
    for (const bool found : {false, true}) {
        for (const bool failed : {false, true}) {
            for (const bool offered : {false, true}) {
                for (const bool reachable : {false, true}) {
                    const auto s = plan(found, failed, offered, reachable);
                    expect(std::ranges::find(s.actions, s.preselected) != s.actions.end(), "preselection is shown");
                }
            }
        }
    }
}

} // namespace

int main() {
    return updater_test::run("aos_updater_launch_tests", [] {
        test_arguments_with_and_without_command();
        test_decision();
        test_remembered_choice_and_reset();
        test_original_command_line();
        test_first_run_table();
    });
}
