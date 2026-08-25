#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace battlespades::world {

/** Ordered lesson phases recovered from the retail string table. */
enum class TutorialLessonStage : std::uint8_t {
    intro,
    basic_controls,
    jump,
    crouch,
    shooting,
    climb,
    complete,
};

/**
 * Client-side port of the recovered Training lesson state machine.
 *
 * Gates mirror `BattleSpades/modes/tutorial.py` exactly: stages advance on
 * the MINIMUM lane-local x ever reached (so backtracking never regresses a
 * lesson), jump/crouch sightings are sticky, and each movement gate keeps the
 * recovered geometry-backed fallback threshold for missed input samples.
 * SHOOTING requires destroying the five gallery targets and CLIMB requires
 * building, so an offline session without weapons stops at SHOOTING —
 * advance_external() exists for those future systems.
 */
class TutorialLessons final {
public:
    /** Recovered pacing constants. */
    static constexpr double intro_seconds{3.0};
    static constexpr double help_transition_delay{0.35};

    /**
     * Advances one fixed step. Returns true when a new stage was entered
     * this tick (the caller shows that stage's help messages).
     */
    [[nodiscard]] bool tick(double dt, double local_x, bool jump_input, bool crouch_input);

    /** Future weapon/building systems report their gate completions here. */
    [[nodiscard]] bool advance_external(TutorialLessonStage completed_gate);

    [[nodiscard]] TutorialLessonStage stage() const noexcept { return stage_; }
    [[nodiscard]] double minimum_local_x() const noexcept { return minimum_local_x_; }
    [[nodiscard]] bool saw_jump() const noexcept { return saw_jump_; }
    [[nodiscard]] bool saw_crouch() const noexcept { return saw_crouch_; }

    /** Recovered HELP_BY_STAGE message identifiers, in retail order. */
    [[nodiscard]] static std::span<const std::string_view>
    message_keys(TutorialLessonStage stage) noexcept;

private:
    void enter(TutorialLessonStage stage);

    TutorialLessonStage stage_{TutorialLessonStage::intro};
    double stage_elapsed_{};
    double minimum_local_x_{1'000.0};
    bool saw_jump_{};
    bool saw_crouch_{};
};

} // namespace battlespades::world
