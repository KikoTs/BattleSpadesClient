// Pins the offline TutorialLessons reconstruction to the fixture shared with
// the BattleSpades tutorial server (tests/data/tutorial_script.json is a copy
// of the server's tutorial_script.json fixture).
#include "battlespades/world/tutorial_lessons.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using battlespades::world::TutorialLessons;
using battlespades::world::TutorialLessonStage;
using Json = nlohmann::json;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error{message};
    }
}

bool same(double actual, double expected) {
    return std::abs(actual - expected) < 1e-9;
}

Json read_fixture(const char* path) {
    std::ifstream stream{path};
    if (!stream) {
        throw std::runtime_error{std::string{"Cannot open tutorial fixture: "} + path};
    }
    return Json::parse(stream);
}

constexpr std::array<TutorialLessonStage, 7U> stage_order{
    TutorialLessonStage::intro,    TutorialLessonStage::basic_controls,
    TutorialLessonStage::jump,     TutorialLessonStage::crouch,
    TutorialLessonStage::shooting, TutorialLessonStage::climb,
    TutorialLessonStage::complete,
};

std::uint8_t tool_id(const std::string& name) {
    static const std::map<std::string, std::uint8_t> ids{
        {"pistol", std::uint8_t{17U}},
        {"block", std::uint8_t{5U}},
        {"spade", std::uint8_t{2U}}};
    const auto found = ids.find(name);
    expect(found != ids.end(), "unknown fixture tool " + name);
    return found->second;
}

void check_messages(const Json& fixture) {
    const auto& stages = fixture.at("stages");
    expect(stages.size() == stage_order.size(), "stage count drifted");
    for (std::size_t index{}; index < stage_order.size(); ++index) {
        const auto name = stages.at(index).get<std::string>();
        const auto& expected = fixture.at("messages").at(name);
        const auto actual = TutorialLessons::message_keys(stage_order[index]);
        expect(actual.size() == expected.size(), "message count drifted for " + name);
        for (std::size_t key{}; key < actual.size(); ++key) {
            expect(std::string{actual[key]} == expected.at(key).get<std::string>(),
                   "message id drifted for " + name);
        }
    }
}

void check_constants(const Json& fixture) {
    expect(same(TutorialLessons::intro_seconds, fixture.at("intro_seconds").get<double>()),
           "intro seconds drifted");
    expect(same(TutorialLessons::help_transition_delay,
                fixture.at("help_transition_delay").get<double>()),
           "help delay drifted");
    expect(same(TutorialLessons::completion_seconds,
                fixture.at("completion_seconds").get<double>()),
           "completion countdown drifted");
    const auto& gates = fixture.at("gates");
    expect(same(TutorialLessons::basic_controls_max_local_x,
                gates.at("basic_controls_max_local_x").get<double>()) &&
               same(TutorialLessons::jump_with_input_max_local_x,
                    gates.at("jump_with_input_max_local_x").get<double>()) &&
               same(TutorialLessons::jump_fallback_max_local_x,
                    gates.at("jump_fallback_max_local_x").get<double>()) &&
               same(TutorialLessons::crouch_with_input_max_local_x,
                    gates.at("crouch_with_input_max_local_x").get<double>()) &&
               same(TutorialLessons::crouch_fallback_max_local_x,
                    gates.at("crouch_fallback_max_local_x").get<double>()),
           "movement gates drifted");
    const auto& tower = fixture.at("tower");
    expect(same(TutorialLessons::tower_center_local_x,
                tower.at("center_local").at(0).get<double>()) &&
               same(TutorialLessons::tower_center_local_y,
                    tower.at("center_local").at(1).get<double>()) &&
               same(TutorialLessons::tower_radius, tower.at("radius").get<double>()) &&
               same(TutorialLessons::tower_max_player_z, tower.at("max_z").get<double>()),
           "tower gate drifted");
    expect(TutorialLessons::target_count == fixture.at("targets").at("count").get<int>(),
           "target count drifted");
    expect(!fixture.at("colour_picker").get<bool>(),
           "Training keeps RULE_ENABLE_COLOUR_PICKER OFF");
}

void compare_loadout(std::string_view label, const Json& expected, TutorialLessonStage stage) {
    const auto actual = TutorialLessons::loadout(stage);
    const std::string name{label};
    expect(actual.size() == expected.size(), name + " loadout size drifted");
    for (std::size_t index{}; index < actual.size(); ++index) {
        expect(actual[index] == tool_id(expected.at(index).get<std::string>()),
               name + " loadout order drifted");
    }
}

void check_loadouts(const Json& fixture) {
    const auto& loadouts = fixture.at("loadouts");
    for (const auto stage : {TutorialLessonStage::intro, TutorialLessonStage::basic_controls,
                             TutorialLessonStage::jump, TutorialLessonStage::crouch}) {
        compare_loadout("movement", loadouts.at("movement"), stage);
    }
    compare_loadout("shooting", loadouts.at("shooting"), TutorialLessonStage::shooting);
    compare_loadout("climb", loadouts.at("climb"), TutorialLessonStage::climb);
    const auto climb = TutorialLessons::loadout(TutorialLessonStage::climb);
    expect(!climb.empty() &&
               climb.back() == tool_id(loadouts.at("climb_equips").get<std::string>()),
           "the climb grant must equip its final item (the spade)");
}

void check_tower_gate() {
    expect(TutorialLessons::on_tower_top(118.5, 51.5, 193.75), "dome centre is the tower top");
    expect(TutorialLessons::on_tower_top(126.0, 51.5, 194.75), "the dome rim counts");
    expect(!TutorialLessons::on_tower_top(118.5, 51.5, 204.75),
           "the z 207 ledge ring is not the top");
    expect(!TutorialLessons::on_tower_top(130.0, 51.5, 190.0),
           "a pillar beside the tower is not the tower");
    expect(!TutorialLessons::on_tower_top(140.5, 76.5, 230.75), "spawn is not the tower");
}

void check_stage_walk() {
    TutorialLessons lessons;
    for (int tick{}; tick < 400; ++tick) {
        static_cast<void>(lessons.tick(1.0 / 60.0, 90.0, true, true));
    }
    expect(lessons.stage() == TutorialLessonStage::shooting,
           "movement gates must reach SHOOTING");
    expect(lessons.advance_external(TutorialLessonStage::shooting) &&
               lessons.stage() == TutorialLessonStage::climb,
           "five targets advance to CLIMB");
    expect(lessons.advance_external(TutorialLessonStage::climb) &&
               lessons.stage() == TutorialLessonStage::complete,
           "the tower-top gate advances to COMPLETE");
}

} // namespace

int main() {
    try {
        const auto fixture = read_fixture(AOS_TUTORIAL_SCRIPT_FIXTURE);
        check_messages(fixture);
        check_constants(fixture);
        check_loadouts(fixture);
        check_tower_gate();
        check_stage_walk();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
