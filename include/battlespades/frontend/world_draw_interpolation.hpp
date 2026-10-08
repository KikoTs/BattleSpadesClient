#pragma once

#include "battlespades/render/camera_basis.hpp"
#include "battlespades/render/world_renderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>
#include <utility>
#include <vector>

namespace battlespades::frontend {

/**
 * Render-rate interpolation of everything that moves in the world pass.
 *
 * The simulation, prediction, hit tests and ClientData cadence stay on the
 * fixed 60 Hz step and read simulation state only. On a high-refresh display
 * the frontend presents extra frames between two ticks; each frame draws every
 * moving part `alpha` of the way from the previous tick's pose to the current
 * one, exactly as CameraEyeInterpolator already did for the eye. Nothing here
 * feeds back into gameplay, and a frame drawn at alpha 1 is the recorded tick
 * state bit for bit.
 *
 * Identity comes from WorldModelDraw::motion_key. Parts of one object share a
 * key and are paired by mesh slot, in order. A part with no partner (the slot
 * changed: crouch, spawn flash, tool swap) follows its object's motion rigidly
 * instead of jumping ahead of the parts that do interpolate. An object that
 * moved further than `snap_distance` in one tick (spawn, teleport) is never
 * slid across the map.
 */
enum class MotionCategory : std::uint8_t {
    anonymous = 0U,
    player = 1U,
    projectile = 2U,
    entity = 3U,
    terrain_effect = 4U,
    tracer = 5U,
    local_parachute = 6U,
    crate_parachute = 7U,
    /** Keyed by the tick itself, so it never has a partner: drawn as recorded. */
    unblended = 8U,
};

[[nodiscard]] constexpr std::uint32_t motion_key(MotionCategory category,
                                                 std::uint64_t identity) noexcept {
    return (static_cast<std::uint32_t>(category) << 24U) |
           static_cast<std::uint32_t>(identity & 0x00FFFFFFU);
}

/**
 * For parts that must not slide: a muzzle flash stays where it was fired and
 * a placement ghost steps from cell to cell. Consecutive ticks get different
 * keys, so such a part is never paired with anything.
 */
[[nodiscard]] constexpr std::uint32_t unblended_motion_key(std::uint64_t tick) noexcept {
    return motion_key(MotionCategory::unblended, tick);
}

/** Gives every draw appended during its lifetime one object's motion key. */
class MotionScope final {
public:
    MotionScope(std::vector<render::WorldModelDraw>& draws, std::uint32_t key) noexcept
        : draws_{draws}, first_{draws.size()}, key_{key} {}

    ~MotionScope() {
        for (std::size_t index = first_; index < draws_.size(); ++index) {
            draws_[index].motion_key = key_;
        }
    }

    MotionScope(const MotionScope&) = delete;
    MotionScope& operator=(const MotionScope&) = delete;
    MotionScope(MotionScope&&) = delete;
    MotionScope& operator=(MotionScope&&) = delete;

private:
    std::vector<render::WorldModelDraw>& draws_;
    std::size_t first_;
    std::uint32_t key_;
};

using DrawTransform = std::array<float, 16U>;

/**
 * A model matrix split into scale, rotation and translation.
 *
 * Matrices here use the row-vector convention of the frontend's mat_* helpers:
 * rows 0..2 are the images of the model axes and the translation sits at
 * [12..14]. Blending the sixteen numbers directly shrinks a part whenever it
 * turns between two ticks, so rotation is blended as a quaternion.
 */
struct DecomposedTransform final {
    std::array<float, 3U> translation{};
    std::array<float, 3U> scale{1.0F, 1.0F, 1.0F};
    /** x, y, z, w. */
    std::array<float, 4U> rotation{0.0F, 0.0F, 0.0F, 1.0F};
    /** False for a projective, sheared or collapsed matrix. */
    bool valid{};
};

[[nodiscard]] inline DecomposedTransform decompose_transform(const DrawTransform& m) noexcept {
    DecomposedTransform result;
    result.translation = {m[12U], m[13U], m[14U]};
    if (m[3U] != 0.0F || m[7U] != 0.0F || m[11U] != 0.0F || m[15U] != 1.0F) {
        return result;
    }
    std::array<std::array<float, 3U>, 3U> rows{{{m[0U], m[1U], m[2U]},
                                                {m[4U], m[5U], m[6U]},
                                                {m[8U], m[9U], m[10U]}}};
    for (std::size_t axis{}; axis < 3U; ++axis) {
        const float length = std::sqrt(rows[axis][0U] * rows[axis][0U] +
                                       rows[axis][1U] * rows[axis][1U] +
                                       rows[axis][2U] * rows[axis][2U]);
        if (!(length > 1.0e-6F) || !std::isfinite(length)) {
            return result;
        }
        result.scale[axis] = length;
        for (float& value : rows[axis]) {
            value /= length;
        }
    }
    const auto dot = [&](std::size_t a, std::size_t b) {
        return rows[a][0U] * rows[b][0U] + rows[a][1U] * rows[b][1U] +
               rows[a][2U] * rows[b][2U];
    };
    if (std::fabs(dot(0U, 1U)) > 1.0e-3F || std::fabs(dot(0U, 2U)) > 1.0e-3F ||
        std::fabs(dot(1U, 2U)) > 1.0e-3F) {
        return result;
    }
    const float determinant =
        rows[0U][0U] * (rows[1U][1U] * rows[2U][2U] - rows[1U][2U] * rows[2U][1U]) -
        rows[0U][1U] * (rows[1U][0U] * rows[2U][2U] - rows[1U][2U] * rows[2U][0U]) +
        rows[0U][2U] * (rows[1U][0U] * rows[2U][1U] - rows[1U][1U] * rows[2U][0U]);
    if (determinant < 0.0F) {
        // A mirrored part: keep the mirror in the scale so the rest is a
        // proper rotation.
        result.scale[2U] = -result.scale[2U];
        for (float& value : rows[2U]) {
            value = -value;
        }
    }
    // Column-vector rotation C is the transpose of the row images.
    const float c00 = rows[0U][0U];
    const float c01 = rows[1U][0U];
    const float c02 = rows[2U][0U];
    const float c10 = rows[0U][1U];
    const float c11 = rows[1U][1U];
    const float c12 = rows[2U][1U];
    const float c20 = rows[0U][2U];
    const float c21 = rows[1U][2U];
    const float c22 = rows[2U][2U];
    const float trace = c00 + c11 + c22;
    float x{};
    float y{};
    float z{};
    float w{};
    if (trace > 0.0F) {
        const float s = std::sqrt(trace + 1.0F) * 2.0F;
        w = 0.25F * s;
        x = (c21 - c12) / s;
        y = (c02 - c20) / s;
        z = (c10 - c01) / s;
    } else if (c00 > c11 && c00 > c22) {
        const float s = std::sqrt(1.0F + c00 - c11 - c22) * 2.0F;
        w = (c21 - c12) / s;
        x = 0.25F * s;
        y = (c01 + c10) / s;
        z = (c02 + c20) / s;
    } else if (c11 > c22) {
        const float s = std::sqrt(1.0F + c11 - c00 - c22) * 2.0F;
        w = (c02 - c20) / s;
        x = (c01 + c10) / s;
        y = 0.25F * s;
        z = (c12 + c21) / s;
    } else {
        const float s = std::sqrt(1.0F + c22 - c00 - c11) * 2.0F;
        w = (c10 - c01) / s;
        x = (c02 + c20) / s;
        y = (c12 + c21) / s;
        z = 0.25F * s;
    }
    const float norm = std::sqrt(x * x + y * y + z * z + w * w);
    if (!(norm > 1.0e-6F) || !std::isfinite(norm)) {
        return result;
    }
    result.rotation = {x / norm, y / norm, z / norm, w / norm};
    result.valid = true;
    return result;
}

[[nodiscard]] inline DrawTransform compose_transform(const DecomposedTransform& parts) noexcept {
    const float x = parts.rotation[0U];
    const float y = parts.rotation[1U];
    const float z = parts.rotation[2U];
    const float w = parts.rotation[3U];
    const float c00 = 1.0F - 2.0F * (y * y + z * z);
    const float c01 = 2.0F * (x * y - w * z);
    const float c02 = 2.0F * (x * z + w * y);
    const float c10 = 2.0F * (x * y + w * z);
    const float c11 = 1.0F - 2.0F * (x * x + z * z);
    const float c12 = 2.0F * (y * z - w * x);
    const float c20 = 2.0F * (x * z - w * y);
    const float c21 = 2.0F * (y * z + w * x);
    const float c22 = 1.0F - 2.0F * (x * x + y * y);
    const auto& s = parts.scale;
    return {c00 * s[0U], c10 * s[0U], c20 * s[0U], 0.0F,
            c01 * s[1U], c11 * s[1U], c21 * s[1U], 0.0F,
            c02 * s[2U], c12 * s[2U], c22 * s[2U], 0.0F,
            parts.translation[0U], parts.translation[1U], parts.translation[2U], 1.0F};
}

/** `t` of the way from `from` to `to`; `t` is clamped to [0, 1]. */
[[nodiscard]] inline DecomposedTransform blend_decomposed(const DecomposedTransform& from,
                                                          const DecomposedTransform& to,
                                                          float t) noexcept {
    t = std::isfinite(t) ? std::clamp(t, 0.0F, 1.0F) : 1.0F;
    DecomposedTransform result;
    result.valid = true;
    for (std::size_t axis{}; axis < 3U; ++axis) {
        result.translation[axis] =
            from.translation[axis] + (to.translation[axis] - from.translation[axis]) * t;
        result.scale[axis] = from.scale[axis] + (to.scale[axis] - from.scale[axis]) * t;
    }
    auto target = to.rotation;
    float cosine = from.rotation[0U] * target[0U] + from.rotation[1U] * target[1U] +
                   from.rotation[2U] * target[2U] + from.rotation[3U] * target[3U];
    if (cosine < 0.0F) {
        // The same orientation the short way round.
        cosine = -cosine;
        for (float& value : target) {
            value = -value;
        }
    }
    float weight_from = 1.0F - t;
    float weight_to = t;
    if (cosine < 0.9995F) {
        const float angle = std::acos(std::clamp(cosine, -1.0F, 1.0F));
        const float sine = std::sin(angle);
        weight_from = std::sin((1.0F - t) * angle) / sine;
        weight_to = std::sin(t * angle) / sine;
    }
    float norm{};
    for (std::size_t index{}; index < 4U; ++index) {
        result.rotation[index] = from.rotation[index] * weight_from + target[index] * weight_to;
        norm += result.rotation[index] * result.rotation[index];
    }
    norm = std::sqrt(norm);
    if (norm > 1.0e-6F) {
        for (float& value : result.rotation) {
            value /= norm;
        }
    } else {
        result.rotation = to.rotation;
    }
    return result;
}

/**
 * `t` of the way from `from` to `to`. Exactly `to` at t >= 1 and exactly
 * `from` at t <= 0, so a frame on a tick boundary reproduces that tick.
 */
[[nodiscard]] inline DrawTransform blend_transform(const DrawTransform& from,
                                                   const DrawTransform& to,
                                                   float t) noexcept {
    if (!(t < 1.0F) || from == to) {
        return to;
    }
    if (!(t > 0.0F)) {
        return from;
    }
    const auto a = decompose_transform(from);
    const auto b = decompose_transform(to);
    if (!a.valid || !b.valid || (a.scale[2U] < 0.0F) != (b.scale[2U] < 0.0F)) {
        // Not a rigid placement: blend the numbers. Never reached by the
        // frontend's scale/rotate/translate products; kept so an unusual
        // matrix degrades to a small error instead of a jump.
        DrawTransform result{};
        for (std::size_t index{}; index < result.size(); ++index) {
            result[index] = from[index] + (to[index] - from[index]) * t;
        }
        return result;
    }
    return compose_transform(blend_decomposed(a, b, t));
}

[[nodiscard]] constexpr std::uint32_t motion_key_of(const render::WorldModelDraw& draw) noexcept {
    return draw.motion_key;
}
/** View-model parts are one object: the local player's hands and tool. */
[[nodiscard]] constexpr std::uint32_t motion_key_of(const render::ViewModelDraw&) noexcept {
    return 0U;
}

inline void blend_appearance(const render::WorldModelDraw& from, render::WorldModelDraw& result,
                             float t) noexcept {
    result.opacity = from.opacity + (result.opacity - from.opacity) * t;
}
inline void blend_appearance(const render::ViewModelDraw&, render::ViewModelDraw&,
                             float) noexcept {}

/**
 * Pairs one tick's draw list with the previous tick's and blends between them.
 *
 * record() once per presented tick, sample() for every frame of that tick.
 * sample(1.0) returns the recorded list unchanged.
 */
template <typename Draw>
class DrawInterpolator final {
public:
    explicit DrawInterpolator(float snap_distance = 4.0F) noexcept
        : snap_distance_{snap_distance} {}

    void reset() noexcept {
        valid_ = false;
        previous_.clear();
        current_.clear();
        plan_.clear();
    }

    [[nodiscard]] bool valid() const noexcept {
        return valid_;
    }

    [[nodiscard]] std::span<const Draw> current() const noexcept {
        return current_;
    }

    /** Parts of the recorded tick that blend or follow; the rest are drawn as recorded. */
    [[nodiscard]] std::size_t moving_parts() const noexcept {
        return static_cast<std::size_t>(std::ranges::count_if(
            plan_, [](const Plan& plan) { return plan.link != Link::none; }));
    }

    void record(std::span<const Draw> draws, std::uint64_t tick) {
        if (!valid_) {
            previous_.assign(draws.begin(), draws.end());
            current_.assign(draws.begin(), draws.end());
            tick_ = tick;
            valid_ = true;
        } else {
            if (tick != tick_) {
                // A skipped presentation leaves a gap of several ticks; the
                // last recorded pose is then too old to blend from.
                const bool consecutive = tick == tick_ + 1U;
                previous_.swap(current_);
                if (!consecutive) {
                    previous_.clear();
                }
                tick_ = tick;
            }
            current_.assign(draws.begin(), draws.end());
        }
        build_plan();
    }

    /** The recorded tick `alpha` of the way from the tick before it. */
    void sample(double alpha, std::vector<Draw>& result) const {
        result.assign(current_.begin(), current_.end());
        if (!valid_ || !(alpha < 1.0)) {
            return;
        }
        const float t = std::isfinite(alpha) ? static_cast<float>(std::max(alpha, 0.0)) : 1.0F;
        for (std::size_t index{}; index < result.size(); ++index) {
            const auto& plan = plan_[index];
            if (plan.link == Link::blend) {
                const auto& from = previous_[plan.previous];
                if (plan.rigid) {
                    result[index].transform =
                        t > 0.0F ? compose_transform(blend_decomposed(plan.from, plan.to, t))
                                 : from.transform;
                } else {
                    result[index].transform =
                        blend_transform(from.transform, result[index].transform, t);
                }
                blend_appearance(from, result[index], t);
            } else if (plan.link == Link::follow) {
                for (std::size_t axis{}; axis < 3U; ++axis) {
                    result[index].transform[12U + axis] -= plan.offset[axis] * (1.0F - t);
                }
            }
        }
    }

private:
    enum class Link : std::uint8_t {
        /** Drawn as recorded. */
        none,
        /** Blended from its partner in the previous tick. */
        blend,
        /** No partner: carried by its object's translation. */
        follow,
    };

    struct Plan final {
        Link link{Link::none};
        bool rigid{};
        std::uint32_t previous{};
        /** This tick's translation minus the previous tick's, for `follow`. */
        std::array<float, 3U> offset{};
        DecomposedTransform from{};
        DecomposedTransform to{};
    };

    struct Sorted final {
        std::uint64_t key{};
        std::uint32_t index{};
    };

    [[nodiscard]] static std::uint64_t pairing_key(const Draw& draw) noexcept {
        return (static_cast<std::uint64_t>(motion_key_of(draw)) << 32U) | draw.slot;
    }

    static void sort_by_key(std::span<const Draw> draws, std::vector<Sorted>& sorted) {
        sorted.clear();
        sorted.reserve(draws.size());
        for (std::size_t index{}; index < draws.size(); ++index) {
            sorted.push_back({pairing_key(draws[index]), static_cast<std::uint32_t>(index)});
        }
        std::ranges::sort(sorted, [](const Sorted& a, const Sorted& b) {
            return a.key != b.key ? a.key < b.key : a.index < b.index;
        });
    }

    void build_plan() {
        plan_.assign(current_.size(), Plan{});
        if (previous_.empty() || current_.empty()) {
            return;
        }
        sort_by_key(previous_, sorted_previous_);
        sort_by_key(current_, sorted_current_);

        // Pair equal (object, slot) runs of equal length, in draw order.
        std::size_t p{};
        std::size_t c{};
        while (c < sorted_current_.size()) {
            const auto key = sorted_current_[c].key;
            std::size_t c_end = c;
            while (c_end < sorted_current_.size() && sorted_current_[c_end].key == key) {
                ++c_end;
            }
            while (p < sorted_previous_.size() && sorted_previous_[p].key < key) {
                ++p;
            }
            std::size_t p_end = p;
            while (p_end < sorted_previous_.size() && sorted_previous_[p_end].key == key) {
                ++p_end;
            }
            if (c_end - c == p_end - p) {
                for (std::size_t offset{}; offset < c_end - c; ++offset) {
                    pair(sorted_current_[c + offset].index, sorted_previous_[p + offset].index);
                }
            }
            c = c_end;
            p = p_end;
        }

        // A part without a partner rides on the first paired part of its
        // object, in draw order (the body root for a character).
        for (std::size_t index{}; index < current_.size(); ++index) {
            const auto object = motion_key_of(current_[index]);
            if (plan_[index].link != Link::none || object == 0U) {
                continue;
            }
            for (std::size_t other{}; other < current_.size(); ++other) {
                if (plan_[other].link == Link::blend &&
                    motion_key_of(current_[other]) == object) {
                    plan_[index].link = Link::follow;
                    plan_[index].offset = plan_[other].offset;
                    break;
                }
            }
        }
    }

    void pair(std::uint32_t current, std::uint32_t previous) {
        const auto& from = previous_[previous].transform;
        const auto& to = current_[current].transform;
        const std::array<float, 3U> offset{to[12U] - from[12U], to[13U] - from[13U],
                                           to[14U] - from[14U]};
        const float distance_squared =
            offset[0U] * offset[0U] + offset[1U] * offset[1U] + offset[2U] * offset[2U];
        if (!(distance_squared <= snap_distance_ * snap_distance_)) {
            return;
        }
        auto& plan = plan_[current];
        plan.link = Link::blend;
        plan.previous = previous;
        plan.offset = offset;
        if (from == to) {
            // Most entities do not move: nothing to decompose.
            plan.rigid = false;
            return;
        }
        plan.from = decompose_transform(from);
        plan.to = decompose_transform(to);
        plan.rigid = plan.from.valid && plan.to.valid &&
                     (plan.from.scale[2U] < 0.0F) == (plan.to.scale[2U] < 0.0F);
    }

    float snap_distance_{4.0F};
    bool valid_{};
    std::uint64_t tick_{};
    std::vector<Draw> previous_;
    std::vector<Draw> current_;
    std::vector<Plan> plan_;
    std::vector<Sorted> sorted_previous_;
    std::vector<Sorted> sorted_current_;
};

using WorldDrawInterpolator = DrawInterpolator<render::WorldModelDraw>;
using ViewModelInterpolator = DrawInterpolator<render::ViewModelDraw>;

/**
 * The same, for lists without object identity (contact shadows, laser beams,
 * objective boundaries): paired by position in the list, and only while the
 * list keeps its length. `Blend(from, to, t)` returns false to refuse a pair,
 * which is then drawn as recorded.
 */
template <typename Item>
class SequenceInterpolator final {
public:
    void reset() noexcept {
        valid_ = false;
        previous_.clear();
        current_.clear();
    }

    void record(std::span<const Item> items, std::uint64_t tick) {
        if (!valid_) {
            previous_.assign(items.begin(), items.end());
            tick_ = tick;
            valid_ = true;
        } else if (tick != tick_) {
            const bool consecutive = tick == tick_ + 1U;
            previous_.swap(current_);
            if (!consecutive) {
                previous_.clear();
            }
            tick_ = tick;
        }
        current_.assign(items.begin(), items.end());
    }

    template <typename Blend>
    void sample(double alpha, std::vector<Item>& result, Blend&& blend) const {
        result.assign(current_.begin(), current_.end());
        if (!valid_ || !(alpha < 1.0) || previous_.size() != current_.size()) {
            return;
        }
        const float t = std::isfinite(alpha) ? static_cast<float>(std::max(alpha, 0.0)) : 1.0F;
        for (std::size_t index{}; index < result.size(); ++index) {
            Item blended = current_[index];
            if (blend(previous_[index], blended, t)) {
                result[index] = blended;
            }
        }
    }

private:
    bool valid_{};
    std::uint64_t tick_{};
    std::vector<Item> previous_;
    std::vector<Item> current_;
};

namespace interpolation_detail {

[[nodiscard]] inline bool blend_point(const std::array<float, 3U>& from,
                                      std::array<float, 3U>& to, float t,
                                      float snap_distance) noexcept {
    const float dx = to[0U] - from[0U];
    const float dy = to[1U] - from[1U];
    const float dz = to[2U] - from[2U];
    if (!(dx * dx + dy * dy + dz * dz <= snap_distance * snap_distance)) {
        return false;
    }
    to = {from[0U] + dx * t, from[1U] + dy * t, from[2U] + dz * t};
    return true;
}

} // namespace interpolation_detail

[[nodiscard]] inline bool blend_spot_shadow(const render::SpotShadowDraw& from,
                                            render::SpotShadowDraw& to, float t) noexcept {
    if (!interpolation_detail::blend_point(from.position, to.position, t, 4.0F)) {
        return false;
    }
    to.size = from.size + (to.size - from.size) * t;
    to.opacity = from.opacity + (to.opacity - from.opacity) * t;
    return true;
}

[[nodiscard]] inline bool blend_zone_volume(const render::ZoneVolumeDraw& from,
                                            render::ZoneVolumeDraw& to, float t) noexcept {
    if (from.solid != to.solid || from.color != to.color) {
        return false;
    }
    if (!interpolation_detail::blend_point(from.minimum, to.minimum, t, 4.0F) ||
        !interpolation_detail::blend_point(from.maximum, to.maximum, t, 4.0F)) {
        return false;
    }
    to.opacity = from.opacity + (to.opacity - from.opacity) * t;
    return true;
}

[[nodiscard]] inline bool blend_laser_beam(const render::LaserBeamDraw& from,
                                           render::LaserBeamDraw& to, float t) noexcept {
    if (from.pose.visible != to.pose.visible || from.pose.color != to.pose.color ||
        from.pose.player_hit != to.pose.player_hit) {
        return false;
    }
    auto direction = to.pose.direction;
    float length{};
    for (std::size_t axis{}; axis < 3U; ++axis) {
        direction[axis] =
            from.pose.direction[axis] + (to.pose.direction[axis] - from.pose.direction[axis]) * t;
        length += direction[axis] * direction[axis];
    }
    length = std::sqrt(length);
    // A beam that swung more than about 60 degrees in a tick is a new aim.
    if (!(length > 0.5F) ||
        !interpolation_detail::blend_point(from.pose.origin, to.pose.origin, t, 4.0F)) {
        return false;
    }
    to.pose.direction = {direction[0U] / length, direction[1U] / length, direction[2U] / length};
    to.pose.distance = from.pose.distance + (to.pose.distance - from.pose.distance) * t;
    to.pose.alpha = from.pose.alpha + (to.pose.alpha - from.pose.alpha) * t;
    return true;
}

/**
 * HUD elements that sit on a world point: objective icons, fuse and ammo
 * numbers, names above players.
 *
 * They are projected once per tick, from that tick's eye. Between ticks the
 * eye and the point both move, so a label replayed unchanged judders against
 * the smoothly moving world behind it. The track remembers each label's world
 * point and the view it was projected through, and answers how far to move
 * the label for a frame at `alpha`.
 */
class WorldAnchorTrack final {
public:
    struct View final {
        std::array<double, 3U> eye{};
        double yaw_degrees{};
        double pitch_degrees{};
        double fov_y_degrees{75.0};
        double width{};
        double height{};
    };

    static constexpr std::size_t maximum_anchors{1024U};
    static constexpr double snap_distance{4.0};

    void reset() noexcept {
        valid_ = false;
        previous_.clear();
        current_.clear();
    }

    /** Starts the anchors of tick `tick`; the last tick's become the blend source. */
    void begin_tick(std::uint64_t tick) {
        if (valid_ && tick == tick_) {
            current_.clear();
            return;
        }
        const bool consecutive = valid_ && tick == tick_ + 1U;
        previous_.swap(current_);
        if (!consecutive) {
            previous_.clear();
        }
        current_.clear();
        tick_ = tick;
        valid_ = true;
    }

    /** Registers one world point; returns its command tag, or zero when full. */
    [[nodiscard]] std::uint16_t add(std::array<double, 3U> position, std::uint32_t key,
                                    const View& view) {
        if (current_.size() >= maximum_anchors) {
            return 0U;
        }
        current_.push_back({position, key, view});
        return static_cast<std::uint16_t>(current_.size());
    }

    /**
     * Window-pixel offset for the command tagged `anchor` in a frame `alpha`
     * of the way through the tick, whose eye is `eye_offset` away from the
     * tick's own eye. Zero when the point cannot be followed.
     */
    [[nodiscard]] std::array<double, 2U> shift(std::uint16_t anchor, double alpha,
                                               std::array<double, 3U> eye_offset) const noexcept {
        if (anchor == 0U || anchor > current_.size() || !std::isfinite(alpha)) {
            return {};
        }
        const auto& current = current_[anchor - 1U];
        auto point = current.position;
        if (alpha < 1.0) {
            const double t = std::max(alpha, 0.0);
            for (const auto& earlier : previous_) {
                if (earlier.key != current.key) {
                    continue;
                }
                const double dx = current.position[0U] - earlier.position[0U];
                const double dy = current.position[1U] - earlier.position[1U];
                const double dz = current.position[2U] - earlier.position[2U];
                if (dx * dx + dy * dy + dz * dz <= snap_distance * snap_distance) {
                    point = {earlier.position[0U] + dx * t, earlier.position[1U] + dy * t,
                             earlier.position[2U] + dz * t};
                }
                break;
            }
        }
        const auto recorded = project(current.position, current.view, {});
        const auto moved = project(point, current.view, eye_offset);
        if (!recorded.valid || !moved.valid) {
            return {};
        }
        return {moved.x - recorded.x, moved.y - recorded.y};
    }

private:
    struct Anchor final {
        std::array<double, 3U> position{};
        std::uint32_t key{};
        View view{};
    };

    struct Projected final {
        double x{};
        double y{};
        bool valid{};
    };

    [[nodiscard]] static Projected project(const std::array<double, 3U>& point, const View& view,
                                           const std::array<double, 3U>& eye_offset) noexcept {
        if (!(view.height > 0.0) || !(view.fov_y_degrees > 1.0) ||
            !(view.fov_y_degrees < 179.0)) {
            return {};
        }
        const auto basis = render::world_camera_basis(view.yaw_degrees, view.pitch_degrees);
        const std::array<double, 3U> offset{point[0U] - view.eye[0U] - eye_offset[0U],
                                            point[1U] - view.eye[1U] - eye_offset[1U],
                                            point[2U] - view.eye[2U] - eye_offset[2U]};
        const auto along = [&](const std::array<double, 3U>& axis) {
            return offset[0U] * axis[0U] + offset[1U] * axis[1U] + offset[2U] * axis[2U];
        };
        const double depth = along(basis.forward);
        if (!(depth > 0.05) || !std::isfinite(depth)) {
            return {};
        }
        const double focal =
            view.height * 0.5 / std::tan(view.fov_y_degrees * std::numbers::pi / 360.0);
        return {view.width * 0.5 + along(basis.right) / depth * focal,
                view.height * 0.5 - along(basis.up) / depth * focal, true};
    }

    bool valid_{};
    std::uint64_t tick_{};
    std::vector<Anchor> previous_;
    std::vector<Anchor> current_;
};

} // namespace battlespades::frontend
