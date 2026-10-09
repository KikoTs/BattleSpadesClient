/* Movement adapted from OpenSpades/ZeroSpades Player.cpp (Copyright 2013 yvt)
 * and pyspades world_c.cpp (Copyright 2011-2012 Mathias Kaerlev).
 * GPL-3.0-or-later; distributed as part of the combined AGPL-3.0 project.
 * See docs/CLASSIC_PROTOCOL.md and LICENSES/GPL-3.0.txt. */
#include "battlespades/world/classic_movement.hpp"
#include <algorithm>
#include <cmath>

namespace battlespades::world {
double
step_classic_grenade(Vec3& position, Vec3& velocity, const VxlMap& map, double timestep) noexcept {
    // pyspades move_grenade / ZeroSpades Grenade::MoveGrenade. Do arithmetic
    // in original float32 coordinates, then translate for the shared renderer.
    const float dt = static_cast<float>(timestep);
    std::array<float, 3> old{static_cast<float>(position.x),
                             static_cast<float>(position.y),
                             static_cast<float>(position.z - 176.0)};
    std::array<float, 3> v{static_cast<float>(velocity.x / 32.0),
                           static_cast<float>(velocity.y / 32.0),
                           static_cast<float>(velocity.z / 32.0)};
    v[2] += dt;
    auto next = old;
    std::array<int, 3> cell{}, prior{};
    for (std::size_t i = 0; i < 3; ++i) {
        next[i] += v[i] * (dt * 32.0F);
        cell[i] = static_cast<int>(std::floor(next[i]));
        prior[i] = static_cast<int>(std::floor(old[i]));
    }
    const auto clip = [&](int x, int y, int z) {
        if (x < 0 || x >= 512 || y < 0 || y >= 512 || z < 0)
            return false;
        if (z >= 64)
            return true;
        return map.solid(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y),
                         static_cast<std::uint32_t>((z == 63 ? 62 : z) + 176));
    };
    double impact{};
    if (clip(cell[0], cell[1], cell[2])) {
        impact = std::max({std::abs(v[0]), std::abs(v[1]), std::abs(v[2])});
        if (cell[2] != prior[2] &&
            ((cell[0] == prior[0] && cell[1] == prior[1]) || !clip(cell[0], cell[1], prior[2])))
            v[2] = -v[2];
        else if (cell[0] != prior[0] && ((cell[1] == prior[1] && cell[2] == prior[2]) ||
                                         !clip(prior[0], cell[1], cell[2])))
            v[0] = -v[0];
        else if (cell[1] != prior[1] && ((cell[0] == prior[0] && cell[2] == prior[2]) ||
                                         !clip(cell[0], prior[1], cell[2])))
            v[1] = -v[1];
        next = old;
        for (auto& component : v)
            component *= 0.36F;
    }
    position = {next[0], next[1], static_cast<double>(next[2]) + 176.0};
    velocity = {v[0] * 32.0F, v[1] * 32.0F, v[2] * 32.0F};
    return impact;
}

MovementStepResult step_classic_player(PlayerMovementState& s,
                                       PlayerInputState input,
                                       const VxlMap* map,
                                       double timestep,
                                       bool aiming,
                                       bool& jump_held) noexcept {
    MovementStepResult result;
    if (!std::isfinite(timestep) || timestep <= 0 || timestep > 0.1 || !map)
        return result;
    float x = static_cast<float>(s.position.x), y = static_cast<float>(s.position.y),
          z = static_cast<float>(s.position.z - 176.0);
    float vx = static_cast<float>(s.velocity.x), vy = static_cast<float>(s.velocity.y),
          vz = static_cast<float>(s.velocity.z);
    const float dt = static_cast<float>(timestep);
    const auto clip = [&](float px, float py, float pz) {
        if (px < 0 || px >= 512 || py < 0 || py >= 512)
            return true;
        if (pz < 0)
            return false;
        auto iz = static_cast<int>(pz);
        if (iz >= 64)
            return true;
        if (iz == 63)
            iz = 62;
        return map->solid(static_cast<std::uint32_t>(px),
                          static_cast<std::uint32_t>(py),
                          static_cast<std::uint32_t>(iz + 176));
    };
    const auto plane = [&](float pz) {
        return clip(x - 0.45F, y - 0.45F, pz) || clip(x - 0.45F, y + 0.45F, pz) ||
               clip(x + 0.45F, y - 0.45F, pz) || clip(x + 0.45F, y + 0.45F, pz);
    };
    if (input.crouch != s.crouch) {
        if (input.crouch) {
            if (!s.airborne)
                z += 0.9F;
            s.crouch = true;
        } else if (s.airborne && !plane(z + 2.25F))
            s.crouch = false;
        else if (!plane(z - 1.35F)) {
            z -= 0.9F;
            s.crouch = false;
        }
    }
    input.crouch = s.crouch;
    // Wade remains latched in the air; it is friction state, not jump permission.
    if (input.jump && !jump_held && !s.airborne && vz >= 0 && vz < 0.017F) {
        vz = -0.36F;
        result.jumped = true;
    }
    jump_held = input.jump;
    float acceleration = dt;
    if (s.airborne)
        acceleration *= 0.1F;
    else if (input.crouch)
        acceleration *= 0.3F;
    else if (aiming || input.sneak)
        acceleration *= 0.5F;
    else if (input.sprint)
        acceleration *= 1.3F;
    if ((input.forward || input.backward) && (input.left || input.right))
        acceleration *= 0.7071067811865476F;
    const float fx = static_cast<float>(s.orientation.x), fy = static_cast<float>(s.orientation.y);
    const float length = std::sqrt(fx * fx + fy * fy);
    const float rx = length > 0 ? -fy / length : 0, ry = length > 0 ? fx / length : 1;
    // Classic forward acceleration retains pitch; sideways uses the horizontal unit basis.
    if (input.forward) {
        vx += fx * acceleration;
        vy += fy * acceleration;
    } else if (input.backward) {
        vx -= fx * acceleration;
        vy -= fy * acceleration;
    }
    if (input.left) {
        vx -= rx * acceleration;
        vy -= ry * acceleration;
    } else if (input.right) {
        vx += rx * acceleration;
        vy += ry * acceleration;
    }
    float friction = dt + 1;
    vz = (vz + dt) / friction;
    if (s.wade)
        friction = dt * 6 + 1;
    else if (!s.airborne)
        friction = dt * 4 + 1;
    vx /= friction;
    vy /= friction;
    const float landing_speed = vz;
    const float offset = input.crouch ? 0.45F : 0.9F;
    float extent = input.crouch ? 0.9F : 1.35F;
    const float scale = dt * 32;
    const float nx = x + vx * scale, ny = y + vy * scale;
    float nz = z + offset;
    bool climbed{};
    float probe = extent;
    const float bx = nx + (vx < 0 ? -0.45F : 0.45F);
    while (probe >= -1.36F && !clip(bx, y - 0.45F, nz + probe) && !clip(bx, y + 0.45F, nz + probe))
        probe -= 0.9F;
    if (probe < -1.36F)
        x = nx;
    else if (!input.crouch && s.orientation.z < 0.5 && !input.sprint) {
        probe = 0.35F;
        while (probe >= -2.36F && !clip(bx, y - 0.45F, nz + probe) &&
               !clip(bx, y + 0.45F, nz + probe))
            probe -= 0.9F;
        if (probe < -2.36F) {
            x = nx;
            climbed = true;
        } else
            vx = 0;
    } else
        vx = 0;
    probe = extent;
    const float by = ny + (vy < 0 ? -0.45F : 0.45F);
    while (probe >= -1.36F && !clip(x - 0.45F, by, nz + probe) && !clip(x + 0.45F, by, nz + probe))
        probe -= 0.9F;
    if (probe < -1.36F)
        y = ny;
    else if (!input.crouch && s.orientation.z < 0.5 && !input.sprint && !climbed) {
        probe = 0.35F;
        while (probe >= -2.36F && !clip(x - 0.45F, by, nz + probe) &&
               !clip(x + 0.45F, by, nz + probe))
            probe -= 0.9F;
        if (probe < -2.36F) {
            y = ny;
            climbed = true;
        } else
            vy = 0;
    } else if (!climbed)
        vy = 0;
    if (climbed) {
        vx *= 0.5F;
        vy *= 0.5F;
        nz -= 1;
        extent = -1.35F;
    } else {
        if (vz < 0)
            extent = -extent;
        nz += vz * scale;
    }
    const bool was_airborne = s.airborne;
    s.airborne = true;
    if (plane(nz + extent)) {
        if (vz >= 0) {
            s.wade = z > 61;
            s.airborne = false;
        }
        vz = 0;
    } else
        z = nz - offset;
    result.climbed = climbed;
    result.landed = was_airborne && !s.airborne;
    if (vz == 0 && landing_speed > 0.24F) {
        vx *= 0.5F;
        vy *= 0.5F;
        result.hard_landing = true;
        result.landing_damage =
            landing_speed > 0.58F
                ? static_cast<int>((landing_speed - 0.58F) * (landing_speed - 0.58F) * 4096.0F)
                : -1;
    }
    s.position = {x, y, static_cast<double>(z) + 176.0};
    s.velocity = {vx, vy, vz};
    s.jetpack = 0;
    s.jetpack_active = false;
    s.parachute_active = false;
    s.climb_timer = climbed ? 0.25 : std::max(0.0, s.climb_timer - timestep);
    return result;
}
} // namespace battlespades::world
