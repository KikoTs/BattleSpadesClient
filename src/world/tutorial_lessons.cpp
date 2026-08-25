#include "battlespades/world/tutorial_lessons.hpp"

#include <algorithm>
#include <array>

namespace battlespades::world {
namespace {

constexpr std::array<std::string_view, 1U> intro_keys{"TUTORIAL_INTRO"};
constexpr std::array<std::string_view, 3U> basic_controls_keys{
    "TUTORIAL_BASIC_CONTROLS_1",
    "TUTORIAL_BASIC_CONTROLS_2",
    "TUTORIAL_BASIC_CONTROLS_3",
};
constexpr std::array<std::string_view, 2U> jump_keys{"TUTORIAL_JUMP_1", "TUTORIAL_JUMP_2"};
constexpr std::array<std::string_view, 2U> crouch_keys{"TUTORIAL_CROUCH_1",
                                                       "TUTORIAL_CROUCH_2"};
constexpr std::array<std::string_view, 3U> shooting_keys{
    "TUTORIAL_SHOOTING_1",
    "TUTORIAL_SHOOTING_2",
    "TUTORIAL_SHOOTING_3",
};
constexpr std::array<std::string_view, 3U> climb_keys{
    "TUTORIAL_CLIMB1",
    "TUTORIAL_CLIMB2",
    "TUTORIAL_CLIMB3",
};
constexpr std::array<std::string_view, 3U> complete_keys{
    "TUTORIAL_COMPLETE_1",
    "TUTORIAL_COMPLETE_2",
    "TUTORIAL_COMPLETE_3",
};

} // namespace

void TutorialLessons::enter(TutorialLessonStage stage) {
    stage_ = stage;
    stage_elapsed_ = 0.0;
}

bool TutorialLessons::tick(double dt, double local_x, bool jump_input, bool crouch_input) {
    stage_elapsed_ += dt;
    minimum_local_x_ = std::min(minimum_local_x_, local_x);
    saw_jump_ = saw_jump_ || jump_input;
    saw_crouch_ = saw_crouch_ || crouch_input;

    switch (stage_) {
    case TutorialLessonStage::intro:
        if (stage_elapsed_ >= intro_seconds) {
            enter(TutorialLessonStage::basic_controls);
            return true;
        }
        break;
    case TutorialLessonStage::basic_controls:
        // The retail capsule collides at x=134.45 against the first authored
        // jump obstacle; the gate sits on the reachable approach side.
        if (minimum_local_x_ <= 135.0) {
            enter(TutorialLessonStage::jump);
            return true;
        }
        break;
    case TutorialLessonStage::jump:
        // Crossing x=119 proves the ledge was traversed even if a very
        // short jump pulse fell between samples.
        if ((saw_jump_ && minimum_local_x_ <= 128.0) || minimum_local_x_ <= 119.0) {
            enter(TutorialLessonStage::crouch);
            return true;
        }
        break;
    case TutorialLessonStage::crouch:
        // The corridor cannot be crossed standing; x=99 is the
        // geometry-backed fallback for a missed crouch sample.
        if ((saw_crouch_ && minimum_local_x_ <= 108.0) || minimum_local_x_ <= 99.0) {
            enter(TutorialLessonStage::shooting);
            return true;
        }
        break;
    case TutorialLessonStage::shooting:
    case TutorialLessonStage::climb:
        // Target destruction and building arrive with the weapon/tool
        // milestones through advance_external().
        break;
    case TutorialLessonStage::complete:
        break;
    }
    return false;
}

bool TutorialLessons::advance_external(TutorialLessonStage completed_gate) {
    if (completed_gate == TutorialLessonStage::shooting &&
        stage_ == TutorialLessonStage::shooting) {
        enter(TutorialLessonStage::climb);
        return true;
    }
    if (completed_gate == TutorialLessonStage::climb && stage_ == TutorialLessonStage::climb) {
        enter(TutorialLessonStage::complete);
        return true;
    }
    return false;
}

std::span<const std::string_view>
TutorialLessons::message_keys(TutorialLessonStage stage) noexcept {
    switch (stage) {
    case TutorialLessonStage::intro:
        return intro_keys;
    case TutorialLessonStage::basic_controls:
        return basic_controls_keys;
    case TutorialLessonStage::jump:
        return jump_keys;
    case TutorialLessonStage::crouch:
        return crouch_keys;
    case TutorialLessonStage::shooting:
        return shooting_keys;
    case TutorialLessonStage::climb:
        return climb_keys;
    case TutorialLessonStage::complete:
        return complete_keys;
    }
    return {};
}

} // namespace battlespades::world
