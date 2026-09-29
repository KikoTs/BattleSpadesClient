#pragma once

#include "battlespades/world/player_movement.hpp"
#include "battlespades/world/vxl_map.hpp"

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

/** Retail NO_JETPACK sentinel (A363). */
inline constexpr std::uint8_t retail_no_jetpack{65U};

/**
 * Character.__init__ pyx 176-179 / set_crouch pyx 812-849: the Classic CTF
 * intel carried on the back (INTEL_ENTITY_MODEL, z_offset 6, size 0.1).
 *
 * Standing: z 0.9; y -0.38 without a pack, -1.1 for JETPACK_NORMAL (66) and
 * -1.0 for packs 67..69. Crouched: y -0.65 / z 0.5 without a pack, otherwise
 * y -1.15 and z 1.0 (every pack branch loads fld1 for z).
 */
[[nodiscard]] RetailJetpackAttachment retail_back_intel_attachment(
    bool crouched, std::optional<std::uint8_t> jetpack_id) noexcept;

/** Character.__init__ pyx 172-174: CLASSIC_CORPSE_MODEL, BODY_PARTS_SIZE, z 2.0. */
[[nodiscard]] constexpr RetailJetpackAttachment retail_classic_corpse_attachment() noexcept {
    return {6, 0.0, 2.0, 0.05};
}
inline constexpr std::string_view retail_back_intel_model{"intel"};
inline constexpr std::string_view retail_classic_corpse_model{"ClassicCorpse"};

/**
 * Character.set_team pyx 667-672: a character's KV6 default colour is the
 * team colour at half intensity (color and other_color = team * 0.5).
 * kv6.pyd then draws the three default bands at x1.0/x0.7/x1.3 of it.
 * Entities keep the full team colour; only characters, their held tools, the
 * pack, the classic corpse and the back intel use this.
 */
[[nodiscard]] VxlColor retail_character_color(VxlColor team_color) noexcept;

/** SPAWN_COLOR_MULTIPLIER (A2208) applied to the character colour. */
inline constexpr double retail_spawn_color_multiplier{2.0};
[[nodiscard]] VxlColor retail_spawn_flash_color(VxlColor character_color) noexcept;

/** Character.__init__ pyx 167: spawn_protection_time. */
inline constexpr double retail_spawn_protection_time{3.0};

/**
 * Character.spawn_color_blink_timer (pyx 319-320, 1105-1106, 1873-1877).
 *
 * While protected, Character.draw doubles the default colour every frame
 * except the one on which the timer has run out; that frame draws the plain
 * half colour and re-arms the timer with protection_remaining / 3.0, so the
 * normal-colour blink accelerates as protection expires.
 */
struct RetailSpawnBlink final {
    double timer{1.0};
};
/** Character.spawn: spawn_color_blink_timer = 1. */
void retail_spawn_blink_reset(RetailSpawnBlink& blink) noexcept;
/** update_alive: decrement while above zero. */
void retail_spawn_blink_update(RetailSpawnBlink& blink, double dt) noexcept;
/** One Character.draw; true when the default colour is doubled this frame. */
[[nodiscard]] bool retail_spawn_blink_draw(RetailSpawnBlink& blink,
                                           double protection_remaining) noexcept;

/** Tool.use_color / use_team_color / use_other_team_color (Character.draw 2020/2052). */
enum class RetailToolColorPath : std::uint8_t {
    /** Plain KV6 colours. */
    none,
    /** MODEL_SHADER blend_color = holder's block colour over the whole model. */
    block_color,
    /** set_kv6_default_color(character.color). */
    team_color,
    /** set_kv6_default_color(character.other_color). */
    other_team_color,
};
/** `use_team_color` is the catalog's recovered Tool.use_team_color flag. */
[[nodiscard]] RetailToolColorPath retail_tool_color_path(std::uint8_t tool_id,
                                                          bool use_team_color) noexcept;
/** Tool.flash_with_spawn_protection: only ZombieHandTool (24) sets it. */
[[nodiscard]] bool retail_tool_flashes_with_spawn_protection(std::uint8_t tool_id) noexcept;

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
