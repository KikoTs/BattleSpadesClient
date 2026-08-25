#include "battlespades/network/protocol168_reconciliation.hpp"

#include <algorithm>
#include <cmath>

namespace battlespades::network {
namespace {

// Character.apply_player_network_correction ignores sub-0.1 displacement.
// Applying every fixed-point wobble made sloped voxel edges feel like network
// jitter even though retail deliberately leaves those predictions untouched.
constexpr double retail_adjust_threshold{0.1};

void add(world::Vec3& target, const world::Vec3& delta) noexcept {
    target.x += delta.x;
    target.y += delta.y;
    target.z += delta.z;
}

} // namespace

Protocol168PredictionHistory::Protocol168PredictionHistory(std::size_t capacity)
    : capacity_{std::max<std::size_t>(1U, capacity)} {}

void Protocol168PredictionHistory::record(
    std::int32_t loop, const world::PlayerMovementState& state) {
    if (!samples_.empty() && samples_.back().loop == loop) {
        samples_.back() = Sample{loop, state.position, state.velocity};
        return;
    }
    samples_.push_back(Sample{loop, state.position, state.velocity});
    while (samples_.size() > capacity_) samples_.pop_front();
}

std::optional<PredictionCorrection> Protocol168PredictionHistory::reconcile(
    std::int32_t acknowledged_loop, world::Vec3 authoritative_position,
    world::Vec3 authoritative_velocity) {
    if (acknowledged_loop < last_reconciled_loop_) return std::nullopt;
    if (acknowledged_loop == last_reconciled_loop_) {
        // The row is recognized but already consumed. Preserve the public
        // contract (a retained ACK returns a correction) without mutating any
        // state or compounding its first correction.
        return PredictionCorrection{acknowledged_loop, {}, {}};
    }
    const auto found = std::find_if(
        samples_.begin(), samples_.end(), [acknowledged_loop](const Sample& sample) {
            return sample.loop == acknowledged_loop;
        });
    if (found == samples_.end()) return std::nullopt;
    last_reconciled_loop_ = acknowledged_loop;

    PredictionCorrection correction;
    correction.acknowledged_loop = acknowledged_loop;
    const world::Vec3 position_delta{
        authoritative_position.x - found->position.x,
        authoritative_position.y - found->position.y,
        authoritative_position.z - found->position.z};
    // Retail gates the complete correction on position distance squared. It
    // neither drops individual accepted components nor applies velocity-only
    // noise while position remains inside the 0.1-block tolerance.
    if (std::hypot(position_delta.x, position_delta.y, position_delta.z) >
        retail_adjust_threshold) {
        correction.position_delta = position_delta;
        correction.velocity_delta = {
            authoritative_velocity.x - found->velocity.x,
            authoritative_velocity.y - found->velocity.y,
            authoritative_velocity.z - found->velocity.z};
    }

    // Rebase the acknowledged sample and every later prediction. This makes
    // repeated acknowledgements idempotent instead of applying drift twice.
    for (auto iterator = found; iterator != samples_.end(); ++iterator) {
        add(iterator->position, correction.position_delta);
        add(iterator->velocity, correction.velocity_delta);
    }
    while (!samples_.empty() && samples_.front().loop < acknowledged_loop) {
        samples_.pop_front();
    }
    return correction;
}

void Protocol168PredictionHistory::clear() noexcept {
    samples_.clear();
    last_reconciled_loop_ = std::numeric_limits<std::int32_t>::min();
}

std::size_t Protocol168PredictionHistory::size() const noexcept {
    return samples_.size();
}

} // namespace battlespades::network
