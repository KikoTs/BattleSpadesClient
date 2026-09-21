#include "battlespades/world/jetpack_death.hpp"

#include <algorithm>
#include <cmath>

namespace battlespades::world {
namespace {

[[nodiscard]] constexpr bool is_retail_jetpack(std::uint8_t item) noexcept {
    return item >= 66U && item <= 69U;
}

[[nodiscard]] std::uint32_t mix(std::uint32_t value) noexcept {
    value ^= value >> 16U;
    value *= 0x7FEB352DU;
    value ^= value >> 15U;
    value *= 0x846CA68BU;
    return value ^ (value >> 16U);
}

[[nodiscard]] double unit_component(std::uint32_t seed) noexcept {
    return static_cast<double>(mix(seed) & 0x00FFFFFFU) / 16777215.0;
}

[[nodiscard]] Vec3 deterministic_rotation_axis(std::uint8_t player_id,
                                               std::uint32_t generation) noexcept {
    const std::uint32_t seed =
        mix(generation ^ (static_cast<std::uint32_t>(player_id) + 1U) * 0x9E3779B9U);
    // Exact get_random_vector construction recovered from world.pyx: choose Z
    // uniformly in [-1,1], choose theta uniformly around the circle, then
    // project the remaining radius. A normalized random cube vector biases
    // deaths toward its corners and visibly repeats diagonal spin axes.
    const double z = unit_component(seed ^ 0xA341316CU) * 2.0 - 1.0;
    constexpr double tau{6.28318530717958647692};
    const double theta = unit_component(seed ^ 0xC8013EA4U) * tau;
    const double radius = std::sqrt(std::max(0.0, 1.0 - z * z));
    return {std::cos(theta) * radius, std::sin(theta) * radius, z};
}

} // namespace

bool has_retail_jetpack(std::span<const std::uint8_t> loadout,
                        std::span<const std::uint8_t> ugc_tools) noexcept {
    return retail_jetpack_id(loadout, ugc_tools).has_value();
}

std::optional<std::uint8_t> retail_jetpack_id(
    std::span<const std::uint8_t> loadout,
    std::span<const std::uint8_t> ugc_tools) noexcept {
    if (const auto found = std::ranges::find_if(loadout, is_retail_jetpack);
        found != loadout.end()) {
        return *found;
    }
    if (const auto found = std::ranges::find_if(ugc_tools, is_retail_jetpack);
        found != ugc_tools.end()) {
        return *found;
    }
    return std::nullopt;
}

std::string_view retail_jetpack_model(std::uint8_t jetpack_id) noexcept {
    switch (jetpack_id) {
    case 66U:
        return "jetpack";
    case 67U:
        return "Jetpack2";
    case 68U:
        return "JetpackEngineer";
    case 69U:
        return "JetpackUGCBuilder";
    default:
        return {};
    }
}

bool JetpackDeathPresentation::begin(std::uint8_t player_id,
                                     std::uint32_t generation,
                                     bool has_jetpack,
                                     Vec3 position) noexcept {
    return has_jetpack && begin(player_id, generation, std::uint8_t{66U}, position);
}

bool JetpackDeathPresentation::begin(std::uint8_t player_id,
                                     std::uint32_t generation,
                                     std::uint8_t jetpack_id,
                                     Vec3 position) noexcept {
    if (!is_retail_jetpack(jetpack_id) || player_id >= states_.size() ||
        generation == 0U) {
        return false;
    }
    auto& state = states_[player_id];
    if (state.has_value() && state->generation == generation) {
        return update_authority(player_id, generation, position);
    }
    state = JetpackDeathSnapshot{player_id,
                                 generation,
                                 jetpack_id,
                                 position,
                                 position,
                                 position,
                                 0.0,
                                 deterministic_rotation_axis(player_id, generation),
                                 {}};
    return true;
}

bool JetpackDeathPresentation::update_authority(std::uint8_t player_id,
                                                std::uint32_t generation,
                                                Vec3 position) noexcept {
    if (player_id >= states_.size()) {
        return false;
    }
    auto& state = states_[player_id];
    if (!state.has_value() || state->generation != generation) {
        return false;
    }
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
        return false;
    if (position.x == state->authoritative_position.x &&
        position.y == state->authoritative_position.y &&
        position.z == state->authoritative_position.z) return true;
    const Vec3 delta{position.x - state->position.x, position.y - state->position.y,
                     position.z - state->position.z};
    // Do not slide a teleport through geometry. Ordinary 30 Hz corpse rows
    // become continuous 60 Hz motion without extrapolating beyond authority.
    if (delta.x * delta.x + delta.y * delta.y + delta.z * delta.z > 16.0)
        state->position = position;
    state->interpolation_start = state->position;
    state->authoritative_position = position;
    state->interpolation_elapsed = 0.0;
    return true;
}

void JetpackDeathPresentation::tick(double dt) noexcept {
    if (!std::isfinite(dt) || dt <= 0.0) {
        return;
    }
    // Character.update_dead adds its once-selected vector every retail update.
    // The native game loop is 60 Hz, so scale by equivalent retail frames.
    const double retail_frames = std::min(dt, 0.25) * 60.0;
    for (auto& state : states_) {
        if (!state.has_value()) {
            continue;
        }
        state->interpolation_elapsed = std::min(1.0 / 30.0, state->interpolation_elapsed + dt);
        const double blend = state->interpolation_elapsed * 30.0;
        const auto& start = state->interpolation_start;
        const auto& target = state->authoritative_position;
        state->position = {start.x + (target.x - start.x) * blend,
                           start.y + (target.y - start.y) * blend,
                           start.z + (target.z - start.z) * blend};
        state->rotation_degrees.x += state->rotation_axis.x * retail_frames;
        state->rotation_degrees.y += state->rotation_axis.y * retail_frames;
        state->rotation_degrees.z += state->rotation_axis.z * retail_frames;
    }
}

const JetpackDeathSnapshot* JetpackDeathPresentation::state(
    std::uint8_t player_id) const noexcept {
    if (player_id >= states_.size()) {
        return nullptr;
    }
    const auto& state = states_[player_id];
    return state.has_value() ? &*state : nullptr;
}

bool JetpackDeathPresentation::active(std::uint8_t player_id,
                                      std::uint32_t generation) const noexcept {
    const auto* found = state(player_id);
    return found != nullptr && found->generation == generation;
}

std::optional<JetpackDeathSnapshot> JetpackDeathPresentation::finish(
    std::uint8_t player_id) noexcept {
    if (player_id >= states_.size()) {
        return std::nullopt;
    }
    auto result = states_[player_id];
    if (result.has_value()) result->position = result->authoritative_position;
    states_[player_id].reset();
    return result;
}

void JetpackDeathPresentation::clear(std::uint8_t player_id) noexcept {
    if (player_id < states_.size()) {
        states_[player_id].reset();
    }
}

void JetpackDeathPresentation::clear() noexcept {
    states_.fill(std::nullopt);
}

} // namespace battlespades::world
