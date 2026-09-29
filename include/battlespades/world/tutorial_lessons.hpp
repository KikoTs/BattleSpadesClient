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
 * standing on top of the lane's tower (CLIMB1: "Dig and build your way to the
 * top of the tower!"); both arrive through advance_external().
 *
 * The retail lesson logic ran on an unshipped dedicated server, so these
 * gates are a reconstruction shared value-for-value with the server and
 * pinned by tests/data/tutorial_script.json (a copy of the server fixture).
 */
class TutorialLessons final {
public:
    /** Recovered pacing constants. */
    static constexpr double intro_seconds{3.0};
    static constexpr double help_transition_delay{0.35};
    /** COMPLETE_3 "Training will exit when the timer reaches zero". */
    static constexpr double completion_seconds{10.0};
    /** Movement gates on the minimum lane-local x reached. */
    static constexpr double basic_controls_max_local_x{135.0};
    static constexpr double jump_with_input_max_local_x{128.0};
    static constexpr double jump_fallback_max_local_x{119.0};
    static constexpr double crouch_with_input_max_local_x{108.0};
    static constexpr double crouch_fallback_max_local_x{99.0};
    /** Five bullseyes per lane. */
    static constexpr int target_count{5};
    /**
     * Tower top (Training.vxl, identical in all twelve lanes): dome centre at
     * lane-local (118.5, 51.5), top voxel z 193, dome surface z <= 197 within
     * r ~8.5, a ledge ring at z 207 and the ground near z 238. Standing on
     * the dome puts the player position at z <= 200 (z grows downward).
     */
    static constexpr double tower_center_local_x{118.5};
    static constexpr double tower_center_local_y{51.5};
    static constexpr double tower_radius{9.5};
    static constexpr double tower_max_player_z{200.0};

    /** CLIMB gate: the player stands on the tower top. */
    [[nodiscard]] static bool on_tower_top(double local_x, double local_y,
                                           double z) noexcept;

    /**
     * Retail tool ids granted at `stage` in inventory order: nothing before
     * SHOOTING, the pistol (17) at SHOOTING, then pistol, block tool (5) and
     * spade (2). SetClassLoadout instant=1 equips the final list item.
     */
    [[nodiscard]] static std::span<const std::uint8_t>
    loadout(TutorialLessonStage stage) noexcept;

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
