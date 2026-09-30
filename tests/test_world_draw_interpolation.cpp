#include "battlespades/core/frame_pacing.hpp"
#include "battlespades/frontend/render_interpolation.hpp"
#include "battlespades/frontend/world_draw_interpolation.hpp"
#include "battlespades/world/particle_system.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using battlespades::core::IntermediateFramePacer;
using battlespades::core::SleepBudget;
using battlespades::frontend::blend_transform;
using battlespades::frontend::compose_transform;
using battlespades::frontend::decompose_transform;
using battlespades::frontend::DrawTransform;
using battlespades::frontend::motion_key;
using battlespades::frontend::MotionCategory;
using battlespades::frontend::SequenceInterpolator;
using battlespades::frontend::ViewModelInterpolator;
using battlespades::frontend::WorldDrawInterpolator;
using battlespades::render::SpotShadowDraw;
using battlespades::render::ViewModelDraw;
using battlespades::render::WorldModelDraw;

void expect(bool value, const std::string& message) {
    if (!value) {
        throw std::runtime_error{message};
    }
}

[[nodiscard]] bool near(float a, float b, float tolerance = 1.0e-4F) {
    return std::fabs(a - b) <= tolerance;
}

[[nodiscard]] bool near(const DrawTransform& a, const DrawTransform& b,
                        float tolerance = 1.0e-4F) {
    for (std::size_t index{}; index < a.size(); ++index) {
        if (!near(a[index], b[index], tolerance)) {
            return false;
        }
    }
    return true;
}

// The frontend's own row-vector helpers, restated so this test pins the
// convention the interpolator has to agree with.
[[nodiscard]] DrawTransform identity() {
    return {1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F,
            0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F};
}

[[nodiscard]] DrawTransform multiply(const DrawTransform& a, const DrawTransform& b) {
    DrawTransform result{};
    for (std::size_t row{}; row < 4U; ++row) {
        for (std::size_t column{}; column < 4U; ++column) {
            float sum{};
            for (std::size_t k{}; k < 4U; ++k) {
                sum += a[row * 4U + k] * b[k * 4U + column];
            }
            result[row * 4U + column] = sum;
        }
    }
    return result;
}

[[nodiscard]] DrawTransform translate(float x, float y, float z) {
    auto result = identity();
    result[12U] = x;
    result[13U] = y;
    result[14U] = z;
    return result;
}

[[nodiscard]] DrawTransform scale(float value) {
    auto result = identity();
    result[0U] = value;
    result[5U] = value;
    result[10U] = value;
    return result;
}

[[nodiscard]] DrawTransform rotate_x(float degrees) {
    const float radians = degrees * std::numbers::pi_v<float> / 180.0F;
    auto result = identity();
    result[5U] = std::cos(radians);
    result[6U] = std::sin(radians);
    result[9U] = -std::sin(radians);
    result[10U] = std::cos(radians);
    return result;
}

[[nodiscard]] DrawTransform rotate_z(float degrees) {
    const float radians = degrees * std::numbers::pi_v<float> / 180.0F;
    auto result = identity();
    result[0U] = std::cos(radians);
    result[1U] = std::sin(radians);
    result[4U] = -std::sin(radians);
    result[5U] = std::cos(radians);
    return result;
}

/** scale, pitch, yaw, then place: the shape of every character part matrix. */
[[nodiscard]] DrawTransform part(float size, float pitch, float yaw, float x, float y, float z) {
    auto result = multiply(scale(size), rotate_x(pitch));
    result = multiply(result, rotate_z(yaw));
    return multiply(result, translate(x, y, z));
}

[[nodiscard]] WorldModelDraw draw(std::uint32_t slot, const DrawTransform& transform,
                                  std::uint32_t key = 0U) {
    WorldModelDraw result;
    result.slot = slot;
    result.transform = transform;
    result.motion_key = key;
    return result;
}

void transforms_survive_decomposition() {
    for (const auto& matrix :
         {part(0.5F, 20.0F, 135.0F, 10.0F, -4.0F, 61.5F), part(1.0F, -80.0F, -170.0F, 0.0F, 0.0F, 0.0F),
          part(0.065F, 0.0F, 0.0F, 511.0F, 511.0F, 239.0F), identity()}) {
        const auto parts = decompose_transform(matrix);
        expect(parts.valid, "a scale/rotate/translate product must decompose");
        expect(near(compose_transform(parts), matrix, 2.0e-4F),
               "decompose then compose must reproduce the matrix");
    }
    auto projective = identity();
    projective[3U] = 0.5F;
    expect(!decompose_transform(projective).valid, "a projective matrix is not a placement");
    expect(!decompose_transform(scale(0.0F)).valid, "a collapsed matrix is not a placement");
}

void blends_are_exact_at_both_ends_and_rigid_between() {
    const auto from = part(0.5F, 0.0F, 0.0F, 10.0F, 20.0F, 30.0F);
    const auto to = part(0.5F, 0.0F, 90.0F, 12.0F, 20.0F, 30.0F);
    expect(blend_transform(from, to, 0.0F) == from, "alpha 0 is the previous tick, bit for bit");
    expect(blend_transform(from, to, 1.0F) == to, "alpha 1 is the current tick, bit for bit");
    expect(blend_transform(from, to, 2.0F) == to, "alpha past 1 is clamped to the current tick");
    const auto half = blend_transform(from, to, 0.5F);
    expect(near(half, part(0.5F, 0.0F, 45.0F, 11.0F, 20.0F, 30.0F), 2.0e-4F),
           "halfway through a 90 degree turn is 45 degrees at full size");
    // Blending the sixteen numbers instead would have shrunk the part to 71%.
    const float axis_length =
        std::sqrt(half[0U] * half[0U] + half[1U] * half[1U] + half[2U] * half[2U]);
    expect(near(axis_length, 0.5F), "a turning part must keep its size");

    // The short way round: 170 -> -170 degrees passes through 180, not 0.
    const auto wrapped = blend_transform(part(1.0F, 0.0F, 170.0F, 0.0F, 0.0F, 0.0F),
                                         part(1.0F, 0.0F, -170.0F, 0.0F, 0.0F, 0.0F), 0.5F);
    expect(near(wrapped, part(1.0F, 0.0F, 180.0F, 0.0F, 0.0F, 0.0F), 2.0e-4F),
           "yaw must blend across the +-180 degree seam");
}

void character_parts_blend_between_ticks() {
    const auto player = motion_key(MotionCategory::player, 7U);
    WorldDrawInterpolator interpolator;
    const std::vector<WorldModelDraw> first{
        draw(96U, part(1.0F, 0.0F, 0.0F, 100.0F, 100.0F, 50.0F), player),
        draw(97U, part(1.0F, 10.0F, 0.0F, 100.0F, 100.0F, 49.0F), player),
        // Both arms share one mesh slot and are paired in draw order.
        draw(98U, part(1.0F, 0.0F, 0.0F, 99.5F, 100.0F, 49.5F), player),
        draw(98U, part(1.0F, 0.0F, 0.0F, 100.5F, 100.0F, 49.5F), player),
    };
    auto second = first;
    for (auto& entry : second) {
        entry.transform[12U] += 0.2F;
    }
    interpolator.record(first, 10U);
    std::vector<WorldModelDraw> frame;
    interpolator.sample(0.5, frame);
    expect(frame.size() == first.size() && frame[0U].transform == first[0U].transform,
           "the first recorded tick has nothing to blend from");

    interpolator.record(second, 11U);
    expect(interpolator.moving_parts() == 4U, "every part of a known player must be paired");
    interpolator.sample(1.0, frame);
    for (std::size_t index{}; index < frame.size(); ++index) {
        expect(frame[index].transform == second[index].transform,
               "alpha 1 must reproduce the recorded tick exactly");
    }
    interpolator.sample(0.0, frame);
    for (std::size_t index{}; index < frame.size(); ++index) {
        expect(near(frame[index].transform, first[index].transform),
               "alpha 0 must show the previous tick");
    }
    interpolator.sample(0.25, frame);
    expect(near(frame[0U].transform[12U], 100.05F) && near(frame[2U].transform[12U], 99.55F) &&
               near(frame[3U].transform[12U], 100.55F),
           "a quarter of the way is a quarter of the step, arm by arm");
}

void a_part_that_changed_slot_follows_its_object() {
    const auto player = motion_key(MotionCategory::player, 3U);
    WorldDrawInterpolator interpolator;
    interpolator.record(std::vector<WorldModelDraw>{
                            draw(120U, translate(50.0F, 50.0F, 40.0F), player),
                            draw(121U, translate(50.0F, 50.0F, 39.0F), player)},
                        1U);
    // Crouching swaps the torso mesh: slot 120 becomes 125.
    interpolator.record(std::vector<WorldModelDraw>{
                            draw(125U, translate(50.3F, 50.0F, 40.4F), player),
                            draw(121U, translate(50.3F, 50.0F, 39.0F), player)},
                        2U);
    std::vector<WorldModelDraw> frame;
    interpolator.sample(0.0, frame);
    expect(near(frame[1U].transform[12U], 50.0F), "the head blends from its own last pose");
    expect(near(frame[0U].transform[12U], 50.0F) && near(frame[0U].transform[14U], 40.4F),
           "the new torso rides on the player's motion instead of jumping ahead of the head");
    interpolator.sample(0.5, frame);
    expect(near(frame[0U].transform[12U], 50.15F) && near(frame[1U].transform[12U], 50.15F),
           "both parts stay together between ticks");
}

void spawns_teleports_and_gaps_snap() {
    const auto player = motion_key(MotionCategory::player, 1U);
    const auto rocket = motion_key(MotionCategory::projectile, 9U);
    WorldDrawInterpolator interpolator;
    interpolator.record(
        std::vector<WorldModelDraw>{draw(96U, translate(10.0F, 10.0F, 10.0F), player)}, 1U);
    interpolator.record(
        std::vector<WorldModelDraw>{draw(96U, translate(200.0F, 10.0F, 10.0F), player),
                                    draw(64U, translate(30.0F, 30.0F, 30.0F), rocket)},
        2U);
    std::vector<WorldModelDraw> frame;
    interpolator.sample(0.3, frame);
    expect(frame[0U].transform[12U] == 200.0F, "a respawn must never slide across the map");
    expect(frame[1U].transform[12U] == 30.0F, "a new projectile appears where it is");
    expect(interpolator.moving_parts() == 0U, "neither may be blended");

    // A presentation was skipped: tick 2 is too old to blend tick 4 from.
    interpolator.record(
        std::vector<WorldModelDraw>{draw(96U, translate(200.5F, 10.0F, 10.0F), player)}, 4U);
    interpolator.sample(0.3, frame);
    expect(frame[0U].transform[12U] == 200.5F, "a gap in the ticks must not be blended over");

    interpolator.reset();
    interpolator.sample(0.3, frame);
    expect(frame.empty() && !interpolator.valid(), "reset forgets both ticks");
}

void anonymous_parts_need_an_unambiguous_slot() {
    WorldDrawInterpolator interpolator;
    interpolator.record(std::vector<WorldModelDraw>{draw(1920U, translate(1.0F, 0.0F, 0.0F)),
                                                    draw(1920U, translate(5.0F, 0.0F, 0.0F)),
                                                    draw(30U, translate(9.0F, 0.0F, 0.0F))},
                        1U);
    // One of the two shared-slot effects ended; which is which is unknown.
    interpolator.record(std::vector<WorldModelDraw>{draw(1920U, translate(5.5F, 0.0F, 0.0F)),
                                                    draw(30U, translate(9.5F, 0.0F, 0.0F))},
                        2U);
    std::vector<WorldModelDraw> frame;
    interpolator.sample(0.0, frame);
    expect(frame[0U].transform[12U] == 5.5F,
           "a shared slot whose count changed must not be paired by guesswork");
    expect(near(frame[1U].transform[12U], 9.0F), "a slot drawn once in both ticks is paired");
}

void opacity_blends_and_view_model_parts_pair_by_slot() {
    WorldDrawInterpolator interpolator;
    auto ghost = draw(12U, translate(1.0F, 1.0F, 1.0F));
    ghost.opacity = 0.2F;
    interpolator.record(std::vector<WorldModelDraw>{ghost}, 1U);
    ghost.opacity = 0.6F;
    interpolator.record(std::vector<WorldModelDraw>{ghost}, 2U);
    std::vector<WorldModelDraw> frame;
    interpolator.sample(0.5, frame);
    expect(near(frame[0U].opacity, 0.4F), "a fading ghost fades at the render rate");

    // View space: a swaying weapon moves a few hundredths per tick and a tool
    // swap moves it by whole units.
    ViewModelInterpolator hands{0.75F};
    ViewModelDraw tool;
    tool.slot = 3U;
    tool.transform = translate(0.30F, -0.20F, -0.80F);
    hands.record(std::vector<ViewModelDraw>{tool}, 1U);
    tool.transform = translate(0.34F, -0.20F, -0.80F);
    tool.unlit = true;
    hands.record(std::vector<ViewModelDraw>{tool}, 2U);
    std::vector<ViewModelDraw> view;
    hands.sample(0.5, view);
    expect(near(view[0U].transform[12U], 0.32F) && view[0U].unlit,
           "sway blends; flags come from the current tick");
    tool.transform = translate(0.34F, -2.20F, -0.80F);
    hands.record(std::vector<ViewModelDraw>{tool}, 3U);
    hands.sample(0.5, view);
    expect(view[0U].transform[13U] == -2.20F, "a raise/lower jump is not smeared");
}

void sequences_pair_by_position_only_while_their_length_holds() {
    SequenceInterpolator<SpotShadowDraw> shadows;
    shadows.record(std::vector<SpotShadowDraw>{{{10.0F, 10.0F, 60.0F}, 1.0F, 0.8F},
                                               {{20.0F, 10.0F, 60.0F}, 1.0F, 0.8F}},
                   1U);
    shadows.record(std::vector<SpotShadowDraw>{{{10.4F, 10.0F, 60.0F}, 1.0F, 0.4F},
                                               {{90.0F, 10.0F, 60.0F}, 1.0F, 0.8F}},
                   2U);
    std::vector<SpotShadowDraw> frame;
    shadows.sample(0.5, frame, battlespades::frontend::blend_spot_shadow);
    expect(near(frame[0U].position[0U], 10.2F) && near(frame[0U].opacity, 0.6F),
           "a contact shadow follows its character between ticks");
    expect(frame[1U].position[0U] == 90.0F, "a shadow that jumped is drawn where it is");
    shadows.record(std::vector<SpotShadowDraw>{{{10.8F, 10.0F, 60.0F}, 1.0F, 0.4F}}, 3U);
    shadows.sample(0.5, frame, battlespades::frontend::blend_spot_shadow);
    expect(frame.size() == 1U && frame[0U].position[0U] == 10.8F,
           "a list that changed length is drawn as recorded");
}

void particles_blend_without_touching_the_simulation() {
    using battlespades::world::ParticleSpawn;
    using battlespades::world::ParticleSystem;
    ParticleSystem particles;
    ParticleSpawn spawn;
    spawn.position = {100.0F, 100.0F, 50.0F};
    spawn.velocity = {1.0F, 0.0F, 0.0F};
    spawn.gravity_scale = 0.0F;
    spawn.collide = false;
    spawn.lifetime = 10.0F;
    spawn.size_begin = 1.0F;
    spawn.size_end = 0.0F;
    spawn.alpha_begin = 1.0F;
    spawn.alpha_end = 1.0F;
    spawn.framerate = 0U;
    particles.emit(spawn);
    particles.tick_unbounded(1.0 / 60.0);
    particles.tick_unbounded(1.0 / 60.0);

    const std::array<float, 3U> eye{100.0F, 90.0F, 50.0F};
    particles.build_draw_list(eye, 192.0F);
    expect(particles.instances().size() == 1U, "one live particle");
    const auto recorded = particles.instances()[0U];

    particles.build_draw_list(eye, 192.0F, 1.0);
    expect(particles.instances()[0U].position == recorded.position &&
               particles.instances()[0U].size == recorded.size &&
               particles.instances()[0U].life01 == recorded.life01,
           "alpha 1 is the simulated state, bit for bit");

    particles.build_draw_list(eye, 192.0F, 0.0);
    const auto earlier = particles.instances()[0U];
    particles.build_draw_list(eye, 192.0F, 0.5);
    const auto halfway = particles.instances()[0U];
    expect(earlier.position[0U] < halfway.position[0U] &&
               halfway.position[0U] < recorded.position[0U],
           "a particle moves between its last two simulated positions");
    expect(near(halfway.position[0U],
                (earlier.position[0U] + recorded.position[0U]) * 0.5F, 1.0e-5F),
           "halfway is halfway");
    expect(earlier.size > halfway.size && halfway.size > recorded.size,
           "size follows the blended age");

    particles.build_draw_list(eye, 192.0F);
    expect(particles.instances()[0U].position == recorded.position,
           "building a blended frame must not move the particle");
}

void hud_labels_stay_on_their_world_point() {
    using battlespades::frontend::WorldAnchorTrack;
    // Yaw 0 looks along -x; the point is 20 blocks ahead, on the axis.
    const WorldAnchorTrack::View view{{100.0, 100.0, 50.0}, 0.0, 0.0, 75.0, 1920.0, 1080.0};
    WorldAnchorTrack track;
    track.begin_tick(1U);
    const auto first = track.add({80.0, 100.0, 50.0}, 7U, view);
    expect(first == 1U, "the first anchor is tag one");
    const auto still = track.shift(first, 0.5, {});
    expect(still[0U] == 0.0 && still[1U] == 0.0, "nothing to blend from on the first tick");

    track.begin_tick(2U);
    // The point moved one block to the viewer's right between the ticks.
    const auto basis = battlespades::render::world_camera_basis(0.0, 0.0);
    const auto tag = track.add({80.0 + basis.right[0U], 100.0 + basis.right[1U], 50.0}, 7U, view);
    const auto at_tick = track.shift(tag, 1.0, {});
    expect(at_tick[0U] == 0.0 && at_tick[1U] == 0.0,
           "a frame on the tick draws the label where it was projected");
    const auto start = track.shift(tag, 0.0, {});
    const auto middle = track.shift(tag, 0.5, {});
    expect(start[0U] < middle[0U] && middle[0U] < 0.0,
           "earlier in the tick the label is further left, where the point was");
    expect(near(static_cast<float>(middle[0U]), static_cast<float>(start[0U] * 0.5), 0.05F) &&
               near(static_cast<float>(middle[1U]), 0.0F, 1.0e-3F),
           "and it crosses the step evenly");

    // A static point still moves on screen while the eye does: the eye half a
    // block to its own left shows the point further right.
    track.begin_tick(3U);
    const auto fixed = track.add({80.0, 100.0, 50.0}, 9U, view);
    const auto parallax = track.shift(
        fixed, 0.5, {-0.5 * basis.right[0U], -0.5 * basis.right[1U], 0.0});
    expect(parallax[0U] > 0.0, "the label follows the eye's own interpolation");

    expect(track.shift(0U, 0.5, {})[0U] == 0.0 && track.shift(99U, 0.5, {})[0U] == 0.0,
           "untagged and unknown commands are never moved");
    // Behind the eye: not followed.
    const auto behind = track.add({120.0, 100.0, 50.0}, 11U, view);
    expect(track.shift(behind, 0.5, {})[0U] == 0.0, "a point behind the eye is left alone");
}

void frames_are_spaced_evenly_across_the_tick() {
    using namespace std::chrono_literals;
    using Clock = IntermediateFramePacer::Clock;
    constexpr auto tick = 16'666'666ns;
    IntermediateFramePacer pacer;
    expect(pacer.plan(tick, 0ns).frames_per_tick == 1U, "no request adds no frame");
    expect(pacer.plan(tick, tick).frames_per_tick == 1U, "a 60 Hz display adds no frame");
    expect(pacer.plan(tick, 8'333'333ns).frames_per_tick == 2U, "120 Hz is two frames a tick");
    const auto hz144 = pacer.plan(tick, 6'944'444ns);
    expect(hz144.frames_per_tick == 3U && hz144.spacing == tick / 3,
           "144 Hz is three evenly spaced frames a tick");
    expect(pacer.plan(tick, 4'166'666ns).frames_per_tick == 4U, "240 Hz is four");
    expect(pacer.plan(tick, 1ns).frames_per_tick == IntermediateFramePacer::maximum_frames_per_tick,
           "the frame count is bounded");

    // Every frame, the next tick's included, is one spacing after the last.
    const Clock::time_point tick_time{};
    const auto next_tick = tick_time + tick;
    const auto tick_presented = tick_time + 1ms;
    pacer.record_tick_work(1ms);
    pacer.record_frame_work(500us);
    const auto plan = pacer.plan(tick, 6'944'444ns);
    expect(plan.frames_per_tick == 3U, "a light tick keeps all three frames");
    const auto first = pacer.start_of(1U, plan, tick_presented, tick_presented, next_tick);
    const auto second = pacer.start_of(2U, plan, tick_presented, *first + 500us, next_tick);
    expect(first.has_value() && second.has_value(), "both render-only frames fit");
    expect(*first + pacer.frame_work() == tick_presented + plan.spacing &&
               *second + pacer.frame_work() == tick_presented + plan.spacing * 2,
           "a frame starts early by its own cost, so it is on screen on the slot");
    expect(*second + pacer.frame_work() < next_tick, "no frame may delay the fixed step");
    expect(!pacer.start_of(3U, plan, tick_presented, tick_presented, next_tick).has_value() &&
               !pacer.start_of(0U, plan, tick_presented, tick_presented, next_tick).has_value(),
           "only the planned frames exist");

    // A tick that needs 6 ms cannot share a 5.6 ms slot: drop to two frames,
    // still evenly spaced, rather than squeeze one in front of the tick.
    IntermediateFramePacer busy;
    busy.record_tick_work(6ms);
    const auto reduced = busy.plan(tick, 6'944'444ns);
    expect(reduced.frames_per_tick == 2U && reduced.spacing == tick / 2,
           "a heavy tick lowers the frame count, not the evenness");
    busy.record_tick_work(15ms);
    expect(busy.plan(tick, 6'944'444ns).frames_per_tick == 1U,
           "a tick that fills the step leaves no room for extra frames");
    // A frame woken late still never crosses the tick.
    expect(!pacer.start_of(2U, plan, tick_presented, next_tick - 1ms, next_tick).has_value(),
           "a late frame is dropped, not squeezed in");
}

void vertical_sync_never_asks_for_more_than_the_display_shows() {
    using namespace std::chrono_literals;
    using battlespades::frontend::render_interpolation_paced_period;
    constexpr auto tick = 16'666'666ns;
    IntermediateFramePacer pacer;
    const auto frames = [&](std::uint32_t millihertz, bool vertical_sync) {
        return pacer.plan(tick, render_interpolation_paced_period(true, millihertz,
                                                                   vertical_sync, tick))
            .frames_per_tick;
    };
    expect(frames(144'000U, false) == 3U && frames(144'000U, true) == 2U,
           "144 Hz: 180 fps free-running, 120 fps under VSync");
    expect(frames(120'000U, false) == 2U && frames(120'000U, true) == 2U, "120 Hz is 120 fps");
    expect(frames(240'000U, false) == 4U && frames(240'000U, true) == 4U, "240 Hz is 240 fps");
    expect(frames(165'000U, true) == 2U, "165 Hz under VSync is 120 fps");
    expect(frames(75'000U, true) == 1U, "75 Hz under VSync keeps one frame a tick");
    expect(frames(60'000U, false) == 1U && frames(60'000U, true) == 1U,
           "a 60 Hz display never gets render-only frames");
    expect(render_interpolation_paced_period(false, 144'000U, false, tick) == 0ns,
           "the setting switches all of it off");
}

void sleep_margin_tracks_the_timer() {
    using namespace std::chrono_literals;
    using Clock = battlespades::core::FixedStepPacer::Clock;
    SleepBudget budget;
    const Clock::time_point wanted{};
    budget.record(wanted, wanted + 1'200us);
    expect(budget.margin() >= 1'200us, "one late wake raises the margin at once");
    for (int repeat = 0; repeat < 200; ++repeat) {
        budget.record(wanted, wanted + 50us);
    }
    expect(budget.margin() < 400us && budget.margin() >= SleepBudget::minimum_margin,
           "a precise timer earns a small margin, so little time is spun");
    budget.record(wanted, wanted + 50ms);
    expect(budget.margin() == SleepBudget::maximum_margin, "the spin is bounded");
}

struct TestCase final {
    std::string_view name;
    std::function<void()> body;
};

} // namespace

int main() {
    const std::vector<TestCase> tests{
        {"transforms_survive_decomposition", transforms_survive_decomposition},
        {"blends_are_exact_at_both_ends_and_rigid_between",
         blends_are_exact_at_both_ends_and_rigid_between},
        {"character_parts_blend_between_ticks", character_parts_blend_between_ticks},
        {"a_part_that_changed_slot_follows_its_object",
         a_part_that_changed_slot_follows_its_object},
        {"spawns_teleports_and_gaps_snap", spawns_teleports_and_gaps_snap},
        {"anonymous_parts_need_an_unambiguous_slot", anonymous_parts_need_an_unambiguous_slot},
        {"opacity_blends_and_view_model_parts_pair_by_slot",
         opacity_blends_and_view_model_parts_pair_by_slot},
        {"sequences_pair_by_position_only_while_their_length_holds",
         sequences_pair_by_position_only_while_their_length_holds},
        {"particles_blend_without_touching_the_simulation",
         particles_blend_without_touching_the_simulation},
        {"hud_labels_stay_on_their_world_point", hud_labels_stay_on_their_world_point},
        {"frames_are_spaced_evenly_across_the_tick", frames_are_spaced_evenly_across_the_tick},
        {"vertical_sync_never_asks_for_more_than_the_display_shows",
         vertical_sync_never_asks_for_more_than_the_display_shows},
        {"sleep_margin_tracks_the_timer", sleep_margin_tracks_the_timer},
    };
    std::size_t failures{};
    for (const auto& test : tests) {
        try {
            test.body();
            std::cout << "ok   " << test.name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cout << "FAIL " << test.name << ": " << error.what() << '\n';
        }
    }
    return failures == 0U ? 0 : 1;
}
