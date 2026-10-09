#pragma once

#include "battlespades/world/player_movement.hpp"
#include "battlespades/world/vxl_map.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace battlespades::world {

namespace corpse_detail {

struct Quaternion final {
    double w{1}, x{}, y{}, z{};
};

/** One rigid part: centre of mass, orientation and their rates. */
struct RigidBody final {
    Vec3 position{}, velocity{}, angular_velocity{};
    Quaternion orientation{};
    Vec3 position_before{};
    Quaternion orientation_before{};
    double inverse_mass{};
    /** Diagonal of the inverse inertia tensor in the part's own frame. */
    Vec3 inverse_inertia{};
    /** Where the part sits in the model's authored (rest) space. */
    Vec3 rest_position{};
    Quaternion rest_orientation{};
};

/** A collision ball fixed to one part. */
struct Ball final {
    std::uint8_t part{};
    Vec3 local{};
    double radius{};
};

} // namespace corpse_detail

/**
 * A dead player's body: local presentation only. No player collision,
 * hitboxes or network state.
 *
 * The body is ten rigid parts (torso, head, two-piece arms and legs) joined at
 * the neck, shoulders, elbows, hips and knees. Each part has its own mass in
 * kilograms, so a hit to the head whips the head and only nudges the torso,
 * and the whole body carries the momentum it died with. Joints have
 * anatomical limits; the terrain is solid to every part.
 *
 * It does not go limp in the instant it dies. For up to a second the joints
 * keep some of their strength: the legs go on stepping the way the body is
 * moving, whichever way that is, the trunk is still held up and takes the
 * shot as a recoil, and then the strength drains and the body goes down with
 * the momentum it has.
 */
class ClassicCorpse final {
public:
    enum Joint : std::size_t {
        pelvis,
        neck,
        head,
        shoulder_left,
        shoulder_right,
        elbow_left,
        elbow_right,
        hand_left,
        hand_right,
        foot_left,
        foot_right,
        hip_left,
        hip_right,
        knee_left,
        knee_right,
        joint_count
    };
    enum Part : std::size_t {
        torso,
        skull,
        upper_arm_left,
        upper_arm_right,
        forearm_left,
        forearm_right,
        thigh_left,
        thigh_right,
        shin_left,
        shin_right,
        part_count
    };
    using Matrix = std::array<float, 16>;

    /**
     * `position` is the player's eye, `velocity` blocks per second. A ragdoll
     * is the player's own model, let go where it stood; without `ragdoll` the
     * body is one falling point for the retail death model.
     */
    ClassicCorpse(Vec3 position,
                  double yaw_degrees,
                  Vec3 velocity,
                  std::uint32_t seed,
                  bool ragdoll,
                  bool crouched = false,
                  std::uint64_t animation_timer_ms = 0);

    void tick(double dt, const VxlMap& map, double gravity = 1.0);
    /** Start from the live weapon animation's world-space arm directions. */
    void set_arm_pose(bool left, Vec3 upper_direction, Vec3 lower_direction);

    struct Hit {
        Vec3 position;
        double distance{}, fraction{};
        Joint from{}, to{};
    };
    /** Cosmetic capsule query against the current articulated pose. */
    [[nodiscard]] std::optional<Hit> trace(Vec3 origin, Vec3 direction, double range) const;
    /**
     * A blow at `hit` with `velocity` in blocks per second. The momentum it
     * delivers is fixed, so a light part is thrown and a heavy one barely
     * moves; no part leaves much faster than the blow came. Wakes a sleeping
     * body.
     */
    void impulse(const Hit& hit, Vec3 velocity);
    void impulse(Vec3 position, Vec3 velocity);

    [[nodiscard]] bool ragdoll() const noexcept {
        return ragdoll_;
    }
    /** True once the body lies still; it wakes when hit or when the terrain changes. */
    [[nodiscard]] bool resting() const noexcept {
        return quiet_steps_ >= sleep_steps;
    }
    [[nodiscard]] Vec3 position() const noexcept;
    [[nodiscard]] Vec3 joint(Joint id) const noexcept;
    /** Kilograms. */
    [[nodiscard]] static double part_mass(Part part) noexcept;

    /** Rest space to world for one rigid part: the transform its mesh is drawn with. */
    [[nodiscard]] Matrix part_transform(Part part) const noexcept;
    [[nodiscard]] Matrix body_transform() const noexcept;
    [[nodiscard]] Matrix head_transform() const noexcept;
    /** The thigh. */
    [[nodiscard]] Matrix leg_transform(bool left) const noexcept;
    [[nodiscard]] Matrix shin_transform(bool left) const noexcept;
    /** Map a living arm mesh's long axis onto the simulated limb. */
    [[nodiscard]] Matrix
    arm_transform(bool left, bool upper, Vec3 minimum, Vec3 maximum) const noexcept;

private:
    using Body = corpse_detail::RigidBody;
    using Ball = corpse_detail::Ball;
    struct Link final {
        std::uint8_t parent{}, child{};
        Vec3 parent_anchor{}, child_anchor{};
        bool hinge{};
        /** Hinge: flexion about the shared local x axis, radians. */
        double flex_minimum{}, flex_maximum{};
        /** Ball joint: the child's bone in the parent's neutral frame. */
        Vec3 axis{}, front{}, side{}, neutral_hinge{};
        Vec3 child_bone{0, 0, 1};
        double pitch_minimum{}, pitch_maximum{}, side_minimum{}, side_maximum{}, twist{};
    };
    static constexpr unsigned sleep_steps{72};
    static constexpr std::size_t link_count{9};
    static constexpr std::size_t maximum_balls{40};

    void step(const VxlMap& map, double gravity);
    void substep(const VxlMap& map, double gravity, double h);
    void solve_links(double h);
    void solve_self();
    /** This step's muscle: what each joint is pulled toward, and how hard. */
    void plan_drives();
    /** Remember the pose the muscles held at death. */
    void capture_pose();
    [[nodiscard]] double flex_of(const Link& link) const noexcept;
    /** Set each child at its joint, parents first. */
    void place_chain();
    void lift_out_of_terrain(const VxlMap& map);
    /** Point a two-part limb along the given world directions. */
    void pose_limb(Part upper, Part lower, Vec3 upper_direction, Vec3 lower_direction, Vec3 bend);
    [[nodiscard]] Vec3 joint_rest_to_world(Part part, Vec3 rest_point) const noexcept;

    std::array<Body, part_count> bodies_{};
    std::array<Link, link_count> links_{};
    std::array<Ball, maximum_balls> balls_{};
    std::size_t ball_count_{};
    /** Each skeleton joint as a rest-space point carried by one part. */
    std::array<Vec3, joint_count> joint_rest_{};
    Vec3 right_{1, 0, 0};
    Vec3 origin_rest_{};
    double remainder_{}, age_{};
    bool ragdoll_{};
    bool placed_{};
    std::uint64_t map_revision_{};
    unsigned quiet_steps_{};
    /** Where each part was when the body last went anywhere, and for how long. */
    struct Still final {
        Vec3 position{}, bone{}, across{};
    };
    std::array<Still, part_count> still_{};
    unsigned still_steps_{};
    /** Two parts found touching in this substep. */
    struct Touch final {
        std::uint8_t one{}, other{};
        Vec3 one_offset{}, other_offset{}, normal{};
    };
    std::array<Touch, 16> touches_{};
    std::size_t touch_count_{};
    /** One joint's muscle: the angles it pulls toward and its stiffness. */
    struct Drive final {
        double pitch{}, lateral{}, flex{}, stiffness{};
        /** Hips: the target is a direction in the world, not against the trunk. */
        bool world{};
    };
    std::array<Drive, link_count> held_{}, drives_{};
    /** Seconds the body keeps its feet, the gait it keeps them with, and how
     * hard the trunk is still held upright this step. */
    double stagger_{}, gait_phase_{}, upright_{}, tone_{};
    /** What its legs still carry: the share of its weight and the height of
     * the hips they carry it at. */
    double carried_{}, carried_height_{}, standing_height_{};
    /** What is left of the spring in its limp joints: gone once it is down. */
    double slack_{1};
    Vec3 gait_velocity_{}, gait_forward_{0, 1, 0}, gait_left_{1, 0, 0};
    bool crouched_{}, grounded_{true}, touched_ground_{};
    /** A body left kneeling keels over: the pull on its hips, and for how long. */
    Vec3 keel_push_{};
    unsigned keel_steps_{}, keel_spent_{};
    bool keel_left_{}, keel_pulling_{}, keel_at_neck_{};
};

/** Nearest corpse only, clipped by terrain and the caller's live-player contact.
 * Never consumes a gameplay shot or changes terrain/player/network state. */
[[nodiscard]] std::optional<Vec3> shoot_classic_corpses(std::span<ClassicCorpse* const> corpses,
                                                        const VxlMap& map,
                                                        Vec3 origin,
                                                        Vec3 direction,
                                                        double range,
                                                        double strength);

/** A released gun is one rigid collision body, never a server pickup. */
class ClassicDroppedWeapon final {
public:
    ClassicDroppedWeapon(Vec3 center, Vec3 direction, Vec3 velocity, double half_length);
    void tick(double dt, const VxlMap& map, double gravity);
    /** The motion since release: multiply the release matrix by it. */
    [[nodiscard]] ClassicCorpse::Matrix transform() const noexcept;
    [[nodiscard]] Vec3 position() const noexcept;

private:
    corpse_detail::RigidBody body_{};
    std::array<corpse_detail::Ball, 3> balls_{};
    double remainder_{};
    unsigned quiet_steps_{};
    std::uint64_t revision_{};
};
} // namespace battlespades::world
