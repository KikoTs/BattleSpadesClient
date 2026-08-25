#include "battlespades/render/camera_basis.hpp"
#include "battlespades/world/tutorial_session.hpp"
#include "battlespades/world/vxl_map.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

using battlespades::render::world_camera_basis;
using battlespades::world::TutorialWorldSession;
using battlespades::world::VxlMap;

void expect(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error{message};
    }
}

[[nodiscard]] double dot(const std::array<double, 3U>& a, const std::array<double, 3U>& b) {
    return a[0U] * b[0U] + a[1U] * b[1U] + a[2U] * b[2U];
}

[[nodiscard]] double length(const std::array<double, 3U>& a) {
    return std::sqrt(dot(a, a));
}

} // namespace

int main() {
    try {
        // Spawn framing: yaw 0 faces -x down the course, sky is -z, and
        // screen-right matches the retail strafe direction.
        {
            const auto basis = world_camera_basis(0.0, 0.0);
            expect(std::fabs(basis.forward[0U] + 1.0) < 1e-12 &&
                       std::fabs(basis.forward[1U]) < 1e-12 &&
                       std::fabs(basis.forward[2U]) < 1e-12,
                   "yaw zero must face -x");
            expect(std::fabs(basis.right[0U]) < 1e-12 &&
                       std::fabs(basis.right[1U] + 1.0) < 1e-12,
                   "screen right at spawn must be -y");
            expect(std::fabs(basis.up[2U] + 1.0) < 1e-12, "camera up must point at the sky");
        }

        // The one invariant that prevents a mirrored world: for any yaw, the
        // camera right vector equals the movement strafe vector
        // s = (-oy, ox) of the horizontal facing.
        for (const double yaw : {0.0, 37.0, 90.0, 145.0, -120.0, 180.0}) {
            const auto basis = world_camera_basis(yaw, 0.0);
            const auto ox = basis.forward[0U];
            const auto oy = basis.forward[1U];
            expect(std::fabs(basis.right[0U] - (-oy)) < 1e-12 &&
                       std::fabs(basis.right[1U] - ox) < 1e-12,
                   "camera right must equal the retail strafe vector");
        }

        // Orthonormality across pitch, and pitch positive looks down.
        for (const double pitch : {-89.9, -30.0, 0.0, 45.0, 89.9}) {
            const auto basis = world_camera_basis(25.0, pitch);
            expect(std::fabs(length(basis.forward) - 1.0) < 1e-12 &&
                       std::fabs(length(basis.right) - 1.0) < 1e-12 &&
                       std::fabs(length(basis.up) - 1.0) < 1e-12,
                   "camera basis must stay unit length");
            expect(std::fabs(dot(basis.forward, basis.right)) < 1e-12 &&
                       std::fabs(dot(basis.forward, basis.up)) < 1e-12 &&
                       std::fabs(dot(basis.right, basis.up)) < 1e-12,
                   "camera basis must stay orthogonal");
            expect(basis.up[2U] < 0.0, "camera up must never flip below the horizon");
        }
        expect(world_camera_basis(0.0, 45.0).forward[2U] > 0.0,
               "positive pitch must look down (+z)");

        // Mouse-right must turn the view toward screen-right through the
        // session's yaw integration.
        {
            std::vector<std::byte> bytes;
            bytes.reserve(static_cast<std::size_t>(VxlMap::width) * VxlMap::depth * 4U);
            for (std::size_t column{};
                 column < static_cast<std::size_t>(VxlMap::width) * VxlMap::depth; ++column) {
                bytes.push_back(std::byte{0U});
                bytes.push_back(std::byte{1U});
                bytes.push_back(std::byte{0U});
                bytes.push_back(std::byte{0U});
            }
            auto loaded = VxlMap::load(bytes);
            expect(static_cast<bool>(loaded), "synthetic world must parse");
            TutorialWorldSession session{
                std::make_shared<VxlMap>(std::move(*loaded.map))};

            const auto before = world_camera_basis(session.yaw(), session.pitch());
            session.apply_look_delta(120.0, 0.0);
            const auto after = world_camera_basis(session.yaw(), session.pitch());
            const std::array<double, 3U> swing{
                after.forward[0U] - before.forward[0U],
                after.forward[1U] - before.forward[1U],
                after.forward[2U] - before.forward[2U],
            };
            expect(dot(swing, before.right) > 0.0,
                   "moving the mouse right must turn the view toward screen right");

            session.apply_look_delta(0.0, 80.0);
            expect(world_camera_basis(session.yaw(), session.pitch()).forward[2U] > 0.0,
                   "pulling the mouse back must look down without inversion");
        }

        std::cout << "camera basis: orientation and strafe consistency checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
