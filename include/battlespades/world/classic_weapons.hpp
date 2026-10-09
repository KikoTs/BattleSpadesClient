#pragma once
#include <cstdint>
#include <numbers>
#include <optional>
#include <span>

namespace battlespades::world {
// Original AoS wire versions, independent of the Classic+ retail catalog.
struct ClassicWeaponRules {
    std::uint16_t magazine, reserve;
    double interval, reload, spread;
    std::uint8_t pellets, block_damage;
    double recoil_side, recoil_up;
};
[[nodiscard]] constexpr std::optional<ClassicWeaponRules>
classic_weapon_rules(std::uint8_t protocol, std::uint8_t tool) noexcept {
    if (protocol != 3 && protocol != 4)
        return {};
    const bool v76 = protocol == 4;
    switch (tool) {
    case 6:
        return v76 ? ClassicWeaponRules{8, 48, 0.6, 2.5, 0.004, 1, 50, 0.0002, 0.075}
                   : ClassicWeaponRules{10, 50, 0.5, 2.5, 0.006, 1, 50, 0.0001, 0.05};
    case 38:
        return v76 ? ClassicWeaponRules{30, 150, 0.1, 2.5, 0.012, 1, 26, 0.00005, 0.0125}
                   : ClassicWeaponRules{30, 120, 0.1, 2.5, 0.012, 1, 34, 0.00005, 0.0125};
    case 37:
        return v76 ? ClassicWeaponRules{8, 48, 0.8, 0.4, 0.036, 8, 34, 0.0002, 0.075}
                   : ClassicWeaponRules{6, 48, 1.0, 0.5, 0.024, 8, 34, 0.0002, 0.1};
    default:
        return {};
    }
}

struct ClassicRecoilKick {
    double pitch_degrees{}, yaw_degrees{};
};

/** A kill's cause survives switching to the spade before its packet arrives. */
[[nodiscard]] inline std::optional<std::uint8_t>
classic_kill_tool(std::uint8_t kill_type, std::span<const std::uint8_t> loadout) noexcept {
    if (kill_type == 2)
        return 4;
    if (kill_type == 0)
        for (auto tool : loadout)
            if (classic_weapon_rules(3, tool))
                return tool;
    return {};
}

// Adapted from OpenSpades/ZeroSpades Player::FireWeapon and Player::Turn.
// Copyright (c) 2013 yvt. GPL-3.0-or-later. See docs/CLASSIC_PROTOCOL.md.
// Original recoil is in radians, with a 1024 ms triangular horizontal wave.
[[nodiscard]] constexpr ClassicRecoilKick classic_recoil_kick(const ClassicWeaponRules& weapon,
                                                              std::uint64_t timer_ms,
                                                              bool walking,
                                                              bool aiming,
                                                              bool crouching,
                                                              bool airborne) noexcept {
    const double phase = static_cast<double>(timer_ms % 512U) - 255.5;
    const double side = weapon.recoil_side * (timer_ms % 1024U < 512U ? phase : -phase);
    double scale = 180.0 / std::numbers::pi;
    if (walking && !aiming)
        scale *= 2;
    if (airborne)
        scale *= 2;
    else if (crouching)
        scale *= 0.5;
    return {-weapon.recoil_up * scale, side * scale};
}
} // namespace battlespades::world
