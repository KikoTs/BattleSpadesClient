// Character.draw_sight's second model: the classic rifle's front bead.
//
// character.pyx:2083-2089 draws `weapon_object.pin` at `weapon_object.
// pin_scale` from the ALREADY-OFFSET sight position. Exactly one tool in the
// game has one (classicRifleWeapon.py:33 `pin = SEMI_PIN`, the only override
// of weapon.py:39 `pin = None`), and its whole purpose is to be the aiming
// mark: classicRifleWeapon.py:27 hides the HUD crosshair while aimed, so the
// pin's single red voxel is the only thing left on the view axis.
//
// This test walks the real asset through the real loader and the real
// transform, so it fails if the catalog, the authored load offset, the KV6
// vertex convention or the recovered constants move.

#include "battlespades/world/retail_view_model.hpp"
#include "battlespades/world/weapon_catalog.hpp"
#include "battlespades/world/weapon_models.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

using battlespades::world::RetailSightPose;
using battlespades::world::evaluate_weapon_sight;
using battlespades::world::find_weapon_definition;
using battlespades::world::load_weapon_models;
using battlespades::world::weapon_catalog;

constexpr std::uint8_t rifle_tool_id{6U};

void expect(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error{message};
    }
}

void expect_near(double actual, double expected, const char* message) {
    if (std::fabs(actual - expected) > 1.0e-9) {
        throw std::runtime_error{std::string{message} + " (got " +
                                 std::to_string(actual) + ", wanted " +
                                 std::to_string(expected) + ")"};
    }
}

/**
 * One model-space point through the recovered pin chain.
 *
 * draw.pyx:449-451 translates before it scales and character.pyx:2074 rotates
 * 180 degrees around Y, which maps (x, y, z) to (-x, y, -z). GL looks down -Z,
 * so a correct pin has a small negative eye Z and no lateral component.
 */
[[nodiscard]] battlespades::world::ViewModelVector
pin_eye_space(const RetailSightPose& sight, double x, double y, double z) {
    return {-(x * sight.pin_scale + sight.pin_position.x),
            y * sight.pin_scale + sight.pin_position.y,
            -(z * sight.pin_scale + sight.pin_position.z)};
}

void only_the_classic_rifle_carries_a_pin() {
    const auto* rifle = find_weapon_definition(rifle_tool_id);
    expect(rifle != nullptr, "tool 6 must be a real catalog row");
    expect(rifle->pin_model.asset == "kv6/semi_sight_pin.kv6",
           "RIFLE must resolve classicRifleWeapon.py:33 pin = SEMI_PIN");
    expect(rifle->pin_model.authored_offset ==
               std::array<float, 3U>{0.0F, 0.0F, -0.5F},
           "the pin must keep its models.py:230 sight_extra_offset");
    expect_near(rifle->retail.use.pin_scale, 0.02,
                "Weapon.pin_scale must match weapon.py:37");

    // Sighted weapons that deliberately have no pin. weapon.py:39 leaves the
    // attribute at None for every class but the classic rifle, so a pin on any
    // of these would mean the generator resolved a stem instead of the value.
    for (const auto tool_id :
         std::array<std::uint8_t, 6U>{7U, 12U, 18U, 9U, 35U, 60U}) {
        const auto* weapon = find_weapon_definition(tool_id);
        expect(weapon != nullptr && !weapon->sight_model_asset.empty(),
               "the no-pin sample must be made of sighted weapons");
        expect(weapon->pin_model.asset.empty(),
               "only the classic rifle may carry a pin");
        expect(!evaluate_weapon_sight(tool_id).has_pin,
               "a weapon with no pin model must not pose one");
    }
    std::size_t pins{};
    for (const auto& weapon : weapon_catalog()) {
        pins += weapon.pin_model.asset.empty() ? 0U : 1U;
    }
    expect(pins == 1U, "the catalog must contain exactly one pin");
}

void pin_pose_hangs_off_the_already_offset_sight_position() {
    const auto sight = evaluate_weapon_sight(rifle_tool_id);
    expect(sight.has_pin, "the classic rifle must pose a pin");
    expect_near(sight.pin_scale, 0.02, "the pin must use Weapon.pin_scale");
    expect_near(sight.yaw_degrees, 180.0,
                "the pin shares the sight's single glRotatef(180, 0, 1, 0)");
    // character.pyx:2087-2089 subtracts/adds from X/Y/Z, which are already
    // sight_pos + (0.025, -0.35, 1.85) -- not from sight_pos itself.
    expect_near(sight.pin_position.x, sight.position.x - 0.015,
                "pin x must be the offset sight x minus 0.015");
    expect_near(sight.pin_position.y, sight.position.y + 0.3,
                "pin y must be the offset sight y plus 0.3");
    expect_near(sight.pin_position.z, sight.position.z + 2.1,
                "pin z must be the offset sight z plus 2.1");
    // RIFLE's sight_pos is (0, 0, 0), so the recovered constants land bare.
    expect_near(sight.pin_position.x, 0.01, "rifle pin x must be 0.025 - 0.015");
    expect_near(sight.pin_position.y, -0.05, "rifle pin y must be -0.35 + 0.3");
    expect_near(sight.pin_position.z, 3.95, "rifle pin z must be 1.85 + 2.1");
}

void red_bead_lands_on_the_view_axis() {
    const std::filesystem::path root{AOS_TEST_ASSET_ROOT};
    const auto loaded = load_weapon_models(root, rifle_tool_id);
    expect(static_cast<bool>(loaded), loaded.error.c_str());
    expect(loaded.models->pin.has_value(), "the rifle pin mesh must load");
    const auto& mesh = *loaded.models->pin;
    const auto sight = evaluate_weapon_sight(rifle_tool_id);

    // semi_sight_pin.kv6 is a 1x1x5 post whose z = 0 tip voxel is #78181C.
    // Kv6Model packs colour as 0x00BBGGRR with no shade baked in.
    constexpr std::uint32_t red_tip{0x1C1878U};
    // Kv6Model CENTRES each cube on `voxel - pivot`, matching kv6.pyd's own
    // vertex writer (src/world/kv6_model.cpp), so the authored coordinate is
    // the MIDPOINT of the drawn cube on every axis and the two corners sit half
    // a voxel either side of it.
    float minimum_x{std::numeric_limits<float>::max()};
    float maximum_x{std::numeric_limits<float>::lowest()};
    float minimum_y{std::numeric_limits<float>::max()};
    float maximum_y{std::numeric_limits<float>::lowest()};
    float minimum_z{std::numeric_limits<float>::max()};
    float maximum_z{std::numeric_limits<float>::lowest()};
    std::size_t red_vertices{};
    for (const auto& vertex : mesh.vertices) {
        if (vertex.abgr != red_tip) {
            continue;
        }
        ++red_vertices;
        minimum_x = std::min(minimum_x, vertex.x);
        maximum_x = std::max(maximum_x, vertex.x);
        minimum_y = std::min(minimum_y, vertex.y);
        maximum_y = std::max(maximum_y, vertex.y);
        minimum_z = std::min(minimum_z, vertex.z);
        maximum_z = std::max(maximum_z, vertex.z);
    }
    expect(red_vertices > 0U, "the pin must retain its red tip voxel");
    const auto voxel_x = static_cast<double>(minimum_x + maximum_x) * 0.5;
    const auto voxel_y = static_cast<double>(minimum_y + maximum_y) * 0.5;
    const auto voxel_z = static_cast<double>(minimum_z + maximum_z) * 0.5;
    // One voxel across on every axis, centred on its authored coordinate.
    expect_near(static_cast<double>(maximum_x - minimum_x), 1.0,
                "the tip cube must be one voxel wide");
    expect_near(static_cast<double>(maximum_y - minimum_y), 1.0,
                "the tip cube must be one voxel tall");
    expect_near(static_cast<double>(maximum_z - minimum_z), 1.0,
                "the tip cube must be one voxel deep");
    // The authored (0, 0, -0.5) load offset became a -0.5 render Y shift, so
    // the tip's pivot-relative coordinate is (-0.5, 2.0, -0.5), not 2.5.
    expect_near(voxel_x, -0.5, "pin tip x must be one half-width off the pivot");
    expect_near(voxel_y, 2.0, "models.py:230 must move the pin pivot to z = 2.0");
    expect_near(voxel_z, -0.5, "pin tip z must be one half-depth off the pivot");

    const auto tip = pin_eye_space(sight, voxel_x, voxel_y, voxel_z);
    // THE measurement this whole edit exists for. -0.015 is not arbitrary:
    // 0.025 - 0.015 = 0.010 = 0.5 * pin_scale, exactly cancelling the pin's
    // half-width pivot bias, the same cancellation 0.025 = 0.5 * 0.05 performs
    // for every sight. The bead is the aim mark, so this must be zero.
    expect_near(tip.x, 0.0, "the pin tip must sit exactly on the view axis");
    expect_near(tip.y, -0.01, "the pin tip must sit just under the axis");
    expect_near(tip.z, -3.94, "the pin tip must sit 3.94 units in front");

    // The DRAWN bead, not just its centre coordinate. Our KV6 mesher centres
    // each cube on its voxel coordinate, exactly as kv6.pyd does, so the bead
    // straddles the view axis symmetrically: eye X in [-0.01, +0.01] at the
    // pin scale of 0.02. Pinned here because the alternative convention --
    // spanning [coord, coord + 1] -- puts the whole bead on one side of the
    // axis and moves EVERY model in the game half a voxel with it.
    // See docs/ADS_SIGHT_PLACEMENT.md section 2.
    const auto low = pin_eye_space(sight, mesh.maximum[0U], 0.0, 0.0);
    const auto high = pin_eye_space(sight, mesh.minimum[0U], 0.0, 0.0);
    expect_near(low.x, -0.01, "the drawn bead must reach half a voxel left of the axis");
    expect_near(high.x, +0.01, "the drawn bead must reach half a voxel right of the axis");
}

} // namespace

int main() {
    try {
        only_the_classic_rifle_carries_a_pin();
        pin_pose_hangs_off_the_already_offset_sight_position();
        red_bead_lands_on_the_view_axis();
        std::cout << "ADS sight pin parity tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
