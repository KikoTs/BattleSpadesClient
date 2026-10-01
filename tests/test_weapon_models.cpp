#include "battlespades/world/weapon_catalog.hpp"
#include "battlespades/world/weapon_models.hpp"
#include "battlespades/world/kv6_model.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <vector>
#include <cmath>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace {

void expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main() {
    const std::filesystem::path root{AOS_TEST_ASSET_ROOT};
    // Recolouring a cached untinted set must equal a fresh tinted load, so
    // remote block-colour tools never touch the disk per palette pick.
    for (const std::uint8_t tool : {std::uint8_t{5U}, std::uint8_t{22U}, std::uint8_t{23U},
                                    std::uint8_t{27U}, std::uint8_t{28U}}) {
        const std::array<float, 3U> tint{0.25F, 0.8F, 0.55F};
        const auto direct = battlespades::world::load_weapon_models(root, tool, tint);
        const auto base = battlespades::world::load_weapon_models(root, tool);
        expect(direct && base, "block-colour tool models must load");
        const auto recoloured = battlespades::world::tinted_weapon_models(*base.models, tint);
        const auto same = [](const std::vector<battlespades::world::ChunkMesh>& left,
                             const std::vector<battlespades::world::ChunkMesh>& right) {
            if (left.size() != right.size()) {
                return false;
            }
            for (std::size_t part{}; part < left.size(); ++part) {
                if (left[part].vertices.size() != right[part].vertices.size()) {
                    return false;
                }
                for (std::size_t vertex{}; vertex < left[part].vertices.size(); ++vertex) {
                    if (left[part].vertices[vertex].abgr != right[part].vertices[vertex].abgr) {
                        return false;
                    }
                }
            }
            return true;
        };
        expect(same(direct.models->third_person_parts, recoloured.third_person_parts) &&
                   same(direct.models->first_person_parts, recoloured.first_person_parts),
               "tinted_weapon_models must be bit-identical to a tinted load");
    }
    for (const auto& definition : battlespades::world::weapon_catalog()) {
        const auto loaded = battlespades::world::load_weapon_models(
            root, definition.tool_id);
        if (!loaded) {
            std::cerr << "tool " << static_cast<unsigned int>(definition.tool_id)
                      << ": " << loaded.error << '\n';
            return EXIT_FAILURE;
        }
        expect(loaded.models->third_person_parts.size() ==
                   definition.third_person_models.size(),
               "third-person composite must load every declared part");
        expect(loaded.models->first_person_parts.size() ==
                   definition.first_person_models.size(),
               "first-person composite must load every declared part");
        expect(loaded.models->sight.has_value() ==
                   !definition.sight_model_asset.empty(),
               "sight mesh presence must follow the recovered catalog");
        expect(loaded.models->pin.has_value() ==
                   !definition.pin_model.asset.empty(),
               "pin mesh presence must follow the recovered catalog");
        expect(loaded.models->casing.has_value() ==
                   !definition.casing_model_asset.empty(),
               "casing mesh presence must follow the recovered catalog");
        expect(loaded.models->tracer.has_value() ==
                   !definition.tracer_model_asset.empty(),
               "tracer mesh presence must follow the recovered catalog");
    }
    expect(!battlespades::world::load_weapon_models(root, 65U),
           "protocol tool sentinel must not load a model set");
    {
        std::string error;
        const auto upper = battlespades::world::Kv6Model::load_file(
            root / "kv6/Character_Soldier_Arms_Upper.kv6", &error);
        expect(upper.has_value(), error);
        const auto mesh = upper->mesh();
        expect((mesh.maximum[2U] - mesh.minimum[2U]) >
                   (mesh.maximum[1U] - mesh.minimum[1U]),
               "retail KV6 VBO conversion must map authored Y arm length onto render Z");
    }
    {
        std::string error;
        const auto raw = battlespades::world::Kv6Model::load_file(
            root / "kv6/pistol.kv6", &error);
        expect(raw.has_value(), error);
        const auto raw_mesh = raw->mesh();
        const auto pistol = battlespades::world::load_weapon_models(root, 17U);
        expect(static_cast<bool>(pistol), pistol.error);
        const auto& placed = pistol.models->third_person_parts.front();
        expect(std::abs(placed.minimum[0U] - (raw_mesh.minimum[0U] - 6.0F)) < 0.001F &&
                   std::abs(placed.minimum[1U] - raw_mesh.minimum[1U]) < 0.001F &&
                   std::abs(placed.minimum[2U] - (raw_mesh.minimum[2U] + 18.0F)) < 0.001F,
               "load_model offsets must follow retail KV6 (X,-Z,Y) pivot conversion");
    }
    {
        const auto* hands = battlespades::world::find_weapon_definition(24U);
        expect(hands != nullptr && hands->retail.use.use_team_color,
               "Zombie hands must retain retail use_team_color metadata");
        const auto colored = battlespades::world::load_weapon_models(
            root, 24U, {1.0F, 1.0F, 1.0F},
            battlespades::world::VxlColor{44U, 117U, 179U, 255U});
        expect(static_cast<bool>(colored), colored.error);
    }
    {
        // Zombie hand KV6s carry retail team markers (128,0,128) and
        // (64,0,64). Both ZombieHandTool (24, use_team_color) and
        // ZombiePrefabTool (28, class-special-cased in Character.draw /
        // draw_fps: set_kv6_default_color(*self.color) before each hand) must
        // resolve them, or the sleeves render purple.
        using battlespades::world::Kv6Model;
        using battlespades::world::VxlColor;
        const VxlColor team{22U, 58U, 90U, 255U}; // retail_character_color(blue)
        const auto hand_colors = [&](std::string_view asset, std::optional<VxlColor> color) {
            std::string error;
            auto model = Kv6Model::load_file(root / asset, &error);
            expect(model.has_value(), error);
            bool has_marker{};
            for (const auto& voxel : model->voxels()) {
                has_marker = has_marker || (voxel.color.green == 0U &&
                                            voxel.color.red == voxel.color.blue &&
                                            (voxel.color.red == 64U || voxel.color.red == 128U));
            }
            expect(has_marker, "Zombie hand KV6 must carry retail team-colour markers");
            if (color.has_value()) {
                model->apply_default_color(*color);
            }
            std::vector<std::uint32_t> colors;
            for (const auto& vertex : model->mesh().vertices) {
                colors.push_back(vertex.abgr);
            }
            return colors;
        };
        const auto part_colors = [](const battlespades::world::ChunkMesh& mesh) {
            std::vector<std::uint32_t> colors;
            for (const auto& vertex : mesh.vertices) {
                colors.push_back(vertex.abgr);
            }
            return colors;
        };
        const auto right = hand_colors("kv6/ZombieHand.kv6", team);
        const auto left = hand_colors("kv6/ZombieHandLeft.kv6", team);
        expect(right != hand_colors("kv6/ZombieHand.kv6", std::nullopt),
               "team colour must change the Zombie hand marker voxels");
        for (const std::uint8_t tool : {std::uint8_t{24U}, std::uint8_t{28U}}) {
            const auto loaded = battlespades::world::load_weapon_models(
                root, tool, {1.0F, 1.0F, 1.0F}, team);
            expect(static_cast<bool>(loaded), loaded.error);
            const auto& third = loaded.models->third_person_parts;
            const auto& first = loaded.models->first_person_parts;
            expect(part_colors(first.front()) == right && part_colors(third.front()) == right,
                   "right Zombie hand must resolve its markers to the team colour");
            expect(part_colors(third.back()) == left,
                   "left Zombie hand must resolve its markers to the team colour");
            if (tool == 24U) {
                expect(part_colors(first.back()) == left,
                       "first-person left Zombie hand must resolve its markers");
            }
        }
        // The block palette still tints only BLOCK_MODEL, after the hands
        // took the team colour.
        const auto tinted = battlespades::world::load_weapon_models(
            root, 28U, {1.0F, 0.25F, 0.25F}, team);
        expect(static_cast<bool>(tinted), tinted.error);
        expect(part_colors(tinted.models->first_person_parts[0U]) == right,
               "block palette must not tint the team-coloured Zombie hand");
    }
    {
        const auto* prefab = battlespades::world::find_weapon_definition(28U);
        expect(prefab != nullptr && prefab->third_person_models.size() == 3U &&
                   prefab->first_person_models.size() == 2U,
               "Zombie prefab must preserve retail's intentionally asymmetric composites");
        expect(prefab->third_person_models[0U].asset == "kv6/ZombieHand.kv6" &&
                   prefab->third_person_models[1U].asset == "kv6/block.kv6" &&
                   prefab->third_person_models[2U].asset == "kv6/ZombieHandLeft.kv6",
               "Zombie prefab third-person model order must be hand/block/hand");

        const auto neutral = battlespades::world::load_weapon_models(root, 28U);
        const auto tinted = battlespades::world::load_weapon_models(
            root, 28U, {1.0F, 0.25F, 0.25F});
        expect(static_cast<bool>(neutral), neutral.error);
        expect(static_cast<bool>(tinted), tinted.error);
        expect(neutral.models->first_person_parts[0U].vertices.front().abgr ==
                   tinted.models->first_person_parts[0U].vertices.front().abgr,
               "block palette tint must not recolour the Zombie hand");
        expect(neutral.models->first_person_parts[1U].vertices.front().abgr !=
                   tinted.models->first_person_parts[1U].vertices.front().abgr,
               "Zombie prefab block must consume the selected block palette colour");
    }
    std::cout << "All retail weapon KV6 composites loaded successfully\n";
    return EXIT_SUCCESS;
}
