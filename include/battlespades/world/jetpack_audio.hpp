#pragma once

#include <cstdint>

namespace battlespades::world {

/** Which of retail's two jetpack loops a character holds. */
enum class JetpackLoop : std::uint8_t {
    none,
    /** `JP_flight_lp`: the pack is thrusting (Player.jetpack_active). */
    flight,
    /** `JP_lowthrust_lp`: the pack is engaged but not thrusting. */
    low_thrust,
};

/** CLASS_UGCBUILDER: no low-thrust loop and a blank JETPACK_LAND_SOUND. */
inline constexpr std::uint8_t jetpack_silent_glide_class{13U};

/**
 * How long after the thrust stops a grounded reading is not yet trusted. A
 * hovering glider has no vertical speed, and an observer's grounded flag is
 * derived from exactly that speed.
 */
inline constexpr double jetpack_descent_grace_seconds{0.2};

/** One character's jetpack state for one frame. */
struct JetpackAudioInput final {
    /** parent.jetpack != NO_JETPACK. */
    bool has_pack{};
    bool alive{};
    /** Player.jetpack_active: WorldUpdate action bit 0x04. */
    bool active{};
    /** Player.jetpack_passive: WorldUpdate action bit 0x08. */
    bool passive{};
    bool airborne{};
    std::uint8_t class_id{};
    /** The main character is steering a UGC prefab (is_controlling_prefab). */
    bool controlling_prefab{};
};

/** Edge memory for one character; reset it with every new life. */
struct JetpackAudioState final {
    bool initialized{};
    bool active{};
    /** The passive flag the cues below were derived from. */
    bool passive{};
    /** The pack thrust since the character last stood on the ground. */
    bool thrust_this_flight{};
    double seconds_since_thrust{};
};

struct JetpackAudioCues final {
    JetpackLoop loop{JetpackLoop::none};
    /** Player.set_jetpack_active(True): `JP_ignite` and the class JUMP_VO. */
    bool ignite{};
    /** Player.set_jetpack_active(False): `JP_release`. */
    bool release{};
    /**
     * Player.set_jetpack_passive(False): CLASS_SOUNDS[class][JETPACK_LAND_SOUND]
     * (`JP_lowthrust_rel`, blank for the UGC builder).
     */
    bool land{};
};

/**
 * Character.update_jetpack_sound (character.pyd 0x10038480, lines 1245-1285):
 *
 *   no pack                      -> both loops closed
 *   jetpack_active               -> passive loop closed, JP_flight_lp looping
 *   jetpack_passive, and not (main and is_controlling_prefab()),
 *   and class != CLASS_UGCBUILDER -> JP_lowthrust_lp looping
 *   otherwise                    -> both loops closed
 *
 * A dead character has both flags cleared by Character.set_dead.
 */
[[nodiscard]] JetpackLoop retail_jetpack_loop(const JetpackAudioInput& input) noexcept;

/**
 * Advance one character by one frame and report the loop to hold and the
 * one-shots to play.
 *
 * The loop and the three one-shots are retail's. The passive flag is the
 * server's in retail, and the retail server's rule for it is not recoverable.
 * The BattleSpades server raises it only together with the active flag (the
 * glide pack), so on its own the wire would never produce "passive and not
 * active". This function therefore also holds the passive state from the first
 * thrust of a flight until the character stands on the ground again. That rule
 * is inferred from the client, which glides a passive character (0.75 gravity,
 * no air friction, fall damage x0.75) and names the cue played when the flag
 * clears JETPACK_LAND_SOUND. A server that sends the flag itself needs no
 * change here: the wire flag is honoured as it is.
 */
[[nodiscard]] JetpackAudioCues step_jetpack_audio(JetpackAudioState& state,
                                                  const JetpackAudioInput& input,
                                                  double dt) noexcept;

} // namespace battlespades::world
