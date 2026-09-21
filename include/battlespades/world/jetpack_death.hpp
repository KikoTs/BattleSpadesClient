#pragma once

#include "battlespades/world/player_movement.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace battlespades::world {

/**
 * One retained jetpack corpse between KillAction(25) and ExplodeCorpse(36).
 *
 * Position interpolates the authoritative WorldUpdate stream. Presentation
 * and retail visual rotation cannot affect collision,
 * damage, respawn timing, or the eventual grave entity.
 */
struct JetpackDeathSnapshot final {
    std::uint8_t player_id{};
    std::uint32_t generation{};
    /** Exact equipped row, retained so the corpse draws the correct pack. */
    std::uint8_t jetpack_id{};
    Vec3 position{};
    Vec3 authoritative_position{};
    Vec3 interpolation_start{};
    double interpolation_elapsed{};
    /** Random vector chosen once by Character.set_dead in the retail client. */
    Vec3 rotation_axis{};
    /** Accumulated Character yaw, pitch and roll offsets, in degrees. */
    Vec3 rotation_degrees{};
};

/** True when a normalized equipment list selects retail pack id 66..69. */
[[nodiscard]] bool has_retail_jetpack(std::span<const std::uint8_t> loadout,
                                     std::span<const std::uint8_t> ugc_tools = {}) noexcept;

/** First selected retail pack id (66..69), preserving normalized slot order. */
[[nodiscard]] std::optional<std::uint8_t> retail_jetpack_id(
    std::span<const std::uint8_t> loadout,
    std::span<const std::uint8_t> ugc_tools = {}) noexcept;

/** Exact KV6 stem used by the four recovered pack rows. */
[[nodiscard]] std::string_view retail_jetpack_model(std::uint8_t jetpack_id) noexcept;

/**
 * Retail Character.set_jetpack_model DisplayList parameters.
 *
 * These are model-space values, before the owning character's ordinary or
 * dead world transform. The dead path keeps this same child attachment while
 * rotating the whole character; it does not introduce a second pack offset.
 */
struct RetailJetpackAttachment final {
    int z_offset{};
    double y{};
    double z{};
    double size{};
};

[[nodiscard]] constexpr RetailJetpackAttachment retail_jetpack_attachment() noexcept {
    return {6, -0.6, 0.8, 0.075};
}

/**
 * Generation-safe client presentation for the one-second jetpack death fuse.
 *
 * The fixed gameplay thread owns this object. Packet handlers begin/update/end
 * entries and the renderer reads immutable snapshots. No I/O, networking, or
 * gameplay mutation occurs here.
 */
class JetpackDeathPresentation final {
public:
    static constexpr std::size_t maximum_players{128U};

    /**
     * Retain a corpse when the selected equipment really contains a jetpack.
     * Repeating the same life only refreshes position and never rerolls spin.
     */
    [[nodiscard]] bool begin(std::uint8_t player_id,
                             std::uint32_t generation,
                             bool has_jetpack,
                             Vec3 position) noexcept;

    /** Retain the exact equipped pack so engineer/glider corpses keep their art. */
    [[nodiscard]] bool begin(std::uint8_t player_id,
                             std::uint32_t generation,
                             std::uint8_t jetpack_id,
                             Vec3 position) noexcept;

    /** Apply a dead WorldUpdate only when it belongs to the retained life. */
    [[nodiscard]] bool update_authority(std::uint8_t player_id,
                                        std::uint32_t generation,
                                        Vec3 position) noexcept;

    /** Smooth network positions and advance the visual yaw/pitch/roll accumulator. */
    void tick(double dt) noexcept;

    [[nodiscard]] const JetpackDeathSnapshot* state(std::uint8_t player_id) const noexcept;
    [[nodiscard]] bool active(std::uint8_t player_id,
                              std::uint32_t generation) const noexcept;

    /** Consume packet 36; returned position is the exact latest authority point. */
    [[nodiscard]] std::optional<JetpackDeathSnapshot> finish(
        std::uint8_t player_id) noexcept;

    void clear(std::uint8_t player_id) noexcept;
    void clear() noexcept;

private:
    std::array<std::optional<JetpackDeathSnapshot>, maximum_players> states_{};
};

} // namespace battlespades::world
