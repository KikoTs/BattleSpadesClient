#include "battlespades/world/class_catalog.hpp"
#include "battlespades/world/class_models.hpp"
#include "battlespades/world/kv6_model.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error{message};
}

} // namespace

int main() {
    using namespace battlespades::world;
    try {
        const auto classes = class_catalog();
        expect(classes.size() == retail_class_count, "all 18 playable classes must be ported");
        expect(find_class_definition(18U) == nullptr, "CLASS_NOOF is a sentinel, not a class");
        expect(find_class_definition(12U)->display_name == "Engineer",
               "new Engineer id/name mapping must not alias legacy Rocketeer");
        expect(find_class_definition(2U)->display_name == "Rocketeer",
               "legacy Engineer must retain its recovered identity");
        expect(find_class_definition(17U)->body_parts[4U].model_asset.ends_with(
                   "Character_Medic_Leg_R.kv6"),
               "Medic must retain its distinct right-leg skin");
        expect(find_class_definition(4U)->first_person_arm_assets[0U].empty(),
               "zombies intentionally use weapon-hand models instead of class FPS arms");
        expect(find_class_definition(4U)->damage_multiplier == 0.6 &&
                   find_class_definition(14U)->damage_multiplier == 0.5 &&
                   find_class_definition(1U)->damage_multiplier == 1.43,
               "class durability must retain the retail numeric-HP multipliers");
        for (std::uint8_t class_id{6U}; class_id <= 11U; ++class_id) {
            const auto* gangster = find_class_definition(class_id);
            expect(gangster != nullptr &&
                       gangster->team_portrait_assets[0U].ends_with(
                           "soldier_character_team1.png") &&
                       gangster->team_portrait_assets[1U].ends_with(
                           "soldier_character_team2.png"),
                   "retail Gangster/VIP class cards reuse the Soldier portraits");
        }
        const auto soldier_default = default_class_items(*find_class_definition(0U));
        expect(std::ranges::find(soldier_default, 23U) != soldier_default.end() &&
                   std::ranges::find(soldier_default, 22U) == soldier_default.end(),
               "default loadouts must use prefab tool 23, not flare tool 22");
        const std::array<std::uint8_t, 6U> scout_rifle{
            5U, 0U, 18U, 17U, 20U, 23U};
        const std::array<std::uint8_t, 5U> scout_smg{
            5U, 0U, 19U, 17U, 20U};
        const std::array<std::uint8_t, 2U> incomplete_scout{5U, 2U};
        expect(preferred_spawn_tool(*find_class_definition(1U), scout_rifle) ==
                   std::optional<std::uint8_t>{std::uint8_t{18U}} &&
                   preferred_spawn_tool(*find_class_definition(1U), scout_smg) ==
                       std::optional<std::uint8_t>{std::uint8_t{19U}},
               "fresh Scout lives must equip the selected primary, not BLOCK_TOOL");
        expect(preferred_spawn_tool(*find_class_definition(1U), incomplete_scout) ==
                   std::optional<std::uint8_t>{std::uint8_t{5U}},
               "an incomplete selection must fall back to its first valid acknowledged tool");
        expect(find_ui_skin("default") != nullptr && find_ui_skin("mafia") != nullptr,
               "both shipped UI texture skins must be catalogued");
        const auto& soldier_parts = find_class_definition(0U)->body_parts;
        const auto head_origin = class_part_preview_origin(soldier_parts[0U]);
        const auto torso_origin = class_part_preview_origin(soldier_parts[1U]);
        const auto leg_origin = class_part_preview_origin(soldier_parts[3U]);
        expect(head_origin[2U] < torso_origin[2U] && torso_origin[2U] < leg_origin[2U],
               "KV6 pivot offsets must stack head, torso, then legs in z-down space");

        const std::filesystem::path assets{AOS_TEST_ASSET_ROOT};
        {
            std::string error;
            auto soldier = Kv6Model::load_file(
                assets / "kv6/Character_Soldier_Body.kv6", &error);
            expect(soldier.has_value(), error.c_str());
            constexpr VxlColor blue_team{44U, 117U, 179U, 255U};
            soldier->apply_default_color(blue_team);
            bool base{};
            bool shadow{};
            bool highlight{};
            bool marker_left{};
            for (const auto& voxel : soldier->voxels()) {
                base |= voxel.color == VxlColor{44U, 117U, 179U, 255U};
                shadow |= voxel.color == VxlColor{31U, 82U, 125U, 255U};
                highlight |= voxel.color == VxlColor{57U, 152U, 233U, 255U};
                marker_left |= voxel.color.green == 0U &&
                               voxel.color.red == voxel.color.blue &&
                               (voxel.color.red == 64U || voxel.color.red == 128U ||
                                voxel.color.red == 192U);
            }
            expect(base && shadow && highlight && !marker_left,
                   "retail team RGB must resolve all three magenta material bands");
            const auto mesh = soldier->mesh();
            const auto packed_blue = std::ranges::any_of(
                mesh.vertices, [](const ChunkVertex& vertex) {
                    // One exposed, unshaded base-band face must survive the
                    // complete KV6 -> ChunkVertex -> bgfx ABGR boundary.
                    //
                    // Alpha MUST be 0: that byte is self-illumination, not
                    // opacity. At 255 every model added an unshaded copy of its
                    // own albedo over its lit result and rendered as bright
                    // plastic. Pinned here so restoring it fails loudly.
                    return vertex.abgr == 0x00B3752CU;
                });
            expect(packed_blue,
                   "Soldier team RGB must survive into renderer vertex colors");
        }
        {
            constexpr VxlColor test_team{17U, 83U, 201U, 255U};
            const auto verify_late_class_material =
                [&](std::string_view asset) {
                    std::string error;
                    auto model = Kv6Model::load_file(assets / asset, &error);
                    expect(model.has_value(), error.c_str());
                    const bool had_black = std::ranges::any_of(
                        model->voxels(), [](const Kv6Model::Voxel& voxel) {
                            return voxel.color == VxlColor{0U, 0U, 0U, 255U};
                        });
                    expect(had_black,
                           "late class fixture must contain its exact-black team marker");
                    model->apply_default_color(test_team);
                    const bool resolved = std::ranges::any_of(
                        model->voxels(), [](const Kv6Model::Voxel& voxel) {
                            return voxel.color == test_team;
                        });
                    expect(resolved,
                           "Specialist/Medic black material must resolve to team RGB");
                };
            verify_late_class_material("kv6/Character_Specialist_Body.kv6");
            verify_late_class_material("kv6/Character_Specialist_Arms_Upper.kv6");
            verify_late_class_material("kv6/Character_Medic_Body.kv6");
            verify_late_class_material("kv6/Character_Medic_Arms_Upper.kv6");

            std::string error;
            auto generic =
                Kv6Model::load_file(assets / "kv6/playertorsoc.kv6", &error);
            expect(generic.has_value(), error.c_str());
            generic->apply_default_color(test_team);
            expect(std::ranges::any_of(
                       generic->voxels(), [](const Kv6Model::Voxel& voxel) {
                           return voxel.color == test_team;
                       }),
                   "retail global black material must tint the generic crouch mesh");

            for (const auto fixture : {
                     std::string_view{"kv6/Character_Rocketeer_Body.kv6"},
                     std::string_view{"kv6/Character_Deuce_Body.kv6"},
                     std::string_view{"kv6/Character_UGCBuilder_Body.kv6"}}) {
                auto model = Kv6Model::load_file(assets / fixture, &error);
                expect(model.has_value(), error.c_str());
                model->apply_default_color(test_team);
                expect(std::ranges::any_of(
                           model->voxels(), [](const Kv6Model::Voxel& voxel) {
                               return voxel.color == test_team;
                           }),
                       "every black-authored retail class must resolve team RGB");
            }
        }
        for (const auto& definition : classes) {
            const auto blue = load_class_models(assets, definition.class_id);
            expect(static_cast<bool>(blue), blue.error.c_str());
            expect(blue.models->body_parts.size() == retail_body_part_count,
                   "every class must load all seven body/collision poses");
            expect(!blue.models->standing_preview.empty(),
                   "every class must produce a standing debug mannequin");
            expect(!blue.models->standing_body_preview.empty(),
                   "every class must expose a separately animated standing body");
            expect(!blue.models->standing_torso_preview.empty() &&
                       !blue.models->head_preview.empty(),
                   "live characters must expose independent torso and head meshes");
            expect(!blue.models->left_leg_preview.empty() &&
                       !blue.models->right_leg_preview.empty(),
                   "every class must expose both articulated standing legs");
            expect(!blue.models->crouching_torso_preview.empty() &&
                       !blue.models->crouching_left_leg_preview.empty() &&
                       !blue.models->crouching_right_leg_preview.empty(),
                   "retail crouch pose must retain independently animated parts");
            const auto crouched_leg_index =
                static_cast<std::size_t>(BodyPart::crouched_leg);
            const auto expected_crouching_vertices =
                blue.models->body_parts[static_cast<std::size_t>(BodyPart::head)]
                    .mesh.vertices.size() +
                blue.models->body_parts[static_cast<std::size_t>(BodyPart::crouched_torso)]
                    .mesh.vertices.size() +
                2U * blue.models->body_parts[crouched_leg_index].mesh.vertices.size();
            expect(blue.models->crouching_preview.vertices.size() ==
                       expected_crouching_vertices,
                   "retail crouch pose must draw the shared leg mesh twice");
            for (const auto portrait : definition.team_portrait_assets) {
                if (!portrait.empty()) expect(std::filesystem::exists(assets / portrait),
                                              "declared class portrait must exist");
            }
            for (const auto icon : definition.team_icon_assets) {
                if (!icon.empty()) expect(std::filesystem::exists(assets / icon),
                                          "declared class icon must exist");
            }
        }
        {
            const auto soldier = load_class_models(assets, 0U);
            expect(static_cast<bool>(soldier), soldier.error.c_str());
            const auto leg_top = std::min(soldier.models->left_leg_preview.minimum[2U],
                                          soldier.models->right_leg_preview.minimum[2U]);
            const auto seam_overlap =
                soldier.models->standing_body_preview.maximum[2U] - leg_top;
            expect(seam_overlap >= 0.0F && seam_overlap <= 0.04F,
                   "standing upper body must clear the legs with only a hidden hip seam");
        }
        expect(!class_catalog_contract_sha256().empty(), "catalog must carry its source digest");
        std::cout << "class catalog: 18 skins, body parts, arms and loadouts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "class catalog failure: " << error.what() << '\n';
        return 1;
    }
}
