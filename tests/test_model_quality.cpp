#include "battlespades/world/kv6_model.hpp"
#include "battlespades/world/model_quality.hpp"
#include "battlespades/world/weapon_models.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using namespace battlespades::world;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error{message};
    }
}

[[nodiscard]] bool is_team_marker(const VxlColor& color) noexcept {
    return color.green == 0U && color.red == color.blue &&
           (color.red == 0U || color.red == 64U || color.red == 128U ||
            color.red == 192U);
}

void retail_quality_mapping_is_exact() {
    expect(kv6_inverse_scale(ModelQualityTier::low) == 3U,
           "low model quality must use invscale 3");
    expect(kv6_inverse_scale(ModelQualityTier::medium) == 2U,
           "medium model quality must use invscale 2");
    expect(kv6_inverse_scale(ModelQualityTier::high) == 1U,
           "high model quality must use invscale 1");
    expect(kv6_inverse_scale(ModelQualityTier::low, ModelQualityTier::high) == 1U,
           "a retail high minimum detail must force full-resolution geometry");
    expect(kv6_inverse_scale(ModelQualityTier::low, ModelQualityTier::low, true) == 1U,
           "prefabs must ignore global model quality");
}

void retail_normals_survive_loading_and_all_six_faces() {
    for (const auto index : {std::uint8_t{0},std::uint8_t{254},std::uint8_t{255}}) {
        std::vector<std::byte> bytes;
        const auto u32 = [&](std::uint32_t value) {
            for (int byte=0;byte<4;++byte) bytes.push_back(static_cast<std::byte>(value>>(byte*8)));
        };
        u32(0x6C78764B); u32(1); u32(1); u32(1); // Kvxl, 1x1x1
        u32(0); u32(0); u32(0); u32(1); // pivot and voxel count
        u32(0x00808080); u32((static_cast<std::uint32_t>(index)<<24)|0x003F0000);
        u32(1); bytes.push_back(std::byte{1}); bytes.push_back(std::byte{0});
        const auto model=Kv6Model::load(bytes);
        expect(model && model->voxels().front().normal_index==index,"KV6 normal byte was discarded");
        const auto mesh=model->mesh();
        expect(mesh.vertices.size()==24,"one voxel must expose six faces");
        for (const auto& v:mesh.vertices) {
            expect(v.static_light==0x40000000U,"KV6 material must stay distinct from terrain/effect cubes");
            expect(v.ao_u==mesh.vertices.front().ao_u && v.ao_v==mesh.vertices.front().ao_v &&
                v.edge_u==mesh.vertices.front().edge_u,"authored normal must be shared by all faces");
            if (index==0) expect(std::abs(v.ao_u+0.08847556F)<0.00001F &&
                std::abs(v.ao_v+0.99607843F)<0.000001F && v.edge_u==0,
                "normal zero must match the recovered table and coordinate conversion");
            if (index==255) expect(v.ao_u==2 && v.ao_v==0 && v.edge_u==0,
                "normal 255 must retain retail's special vector");
        }
        expect(model->inverse_scaled(2).voxels().front().normal_index==1,
               "reduced-detail models must use retail's rebuilt normal index");
    }
}

void inverse_scaling_preserves_size_pivot_and_team_material() {
    const std::filesystem::path root{AOS_TEST_ASSET_ROOT};
    std::string error;
    const auto original =
        Kv6Model::load_file(root / "kv6/Character_Soldier_Body.kv6", &error);
    expect(original.has_value(), "soldier body failed to load: " + error);

    auto low = original->inverse_scaled(3U);
    expect(low.voxel_scale() == 3.0F, "low detail voxels must render at authored size 3");
    expect(low.size_x() == (original->size_x() + 2U) / 3U &&
               low.size_y() == (original->size_y() + 2U) / 3U &&
               low.size_z() == (original->size_z() + 2U) / 3U,
           "scaled KV6 dimensions must use ceiling division");
    for (std::size_t axis{}; axis < 3U; ++axis) {
        expect(std::abs(low.pivot()[axis] - original->pivot()[axis] / 3.0F) < 1.0e-5F,
               "scaled KV6 pivot must be divided by invscale");
    }
    expect(low.voxels().size() < original->voxels().size(),
           "low detail must reduce stored model voxels");

    const bool original_has_marker =
        std::ranges::any_of(original->voxels(), [](const Kv6Model::Voxel& voxel) {
            return is_team_marker(voxel.color);
        });
    expect(original_has_marker, "soldier body fixture must contain team-colour markers");
    expect(std::ranges::any_of(low.voxels(), [](const Kv6Model::Voxel& voxel) {
               return voxel.color.red == 0U && voxel.color.green == 0U &&
                      voxel.color.blue == 0U;
           }),
           "a grouped team-colour marker must remain the retail black base band");

    const auto high_mesh = original->mesh();
    const auto low_mesh = low.mesh();
    expect(low_mesh.vertices.size() < high_mesh.vertices.size(),
           "low detail must reduce uploaded KV6 vertices");
    for (std::size_t axis{}; axis < 3U; ++axis) {
        const float high_extent = high_mesh.maximum[axis] - high_mesh.minimum[axis];
        const float low_extent = low_mesh.maximum[axis] - low_mesh.minimum[axis];
        expect(std::abs(low_extent - high_extent) <= 3.0F,
               "inverse-scaled mesh must retain the authored world-space extent");
    }

    const auto before_offset = low.pivot();
    low.offset_pivots({3.0F, 6.0F, 9.0F});
    expect(std::abs(low.pivot()[0U] - before_offset[0U] - 1.0F) < 1.0e-5F &&
               std::abs(low.pivot()[1U] - before_offset[1U] - 2.0F) < 1.0e-5F &&
               std::abs(low.pivot()[2U] - before_offset[2U] - 3.0F) < 1.0e-5F,
           "retail offset_pivots must divide authored offsets by invscale");
}

void sights_ignore_low_model_quality() {
    const std::filesystem::path root{AOS_TEST_ASSET_ROOT};
    constexpr std::uint8_t rifle_tool_id{6U};
    const auto high = load_weapon_models(root, rifle_tool_id);
    const auto low = load_weapon_models(root, rifle_tool_id, {1.0F, 1.0F, 1.0F},
                                        std::nullopt, 3U);
    expect(static_cast<bool>(high), "high-detail rifle failed: " + high.error);
    expect(static_cast<bool>(low), "low-detail rifle failed: " + low.error);
    expect(high.models->sight.has_value() && low.models->sight.has_value(),
           "rifle fixture must expose its sight mesh");
    expect(high.models->sight->vertices.size() == low.models->sight->vertices.size() &&
               high.models->sight->indices.size() == low.models->sight->indices.size(),
           "sight geometry must stay full resolution at low model quality");
    expect(!high.models->first_person_parts.empty() &&
               !low.models->first_person_parts.empty() &&
               low.models->first_person_parts.front().vertices.size() <
                   high.models->first_person_parts.front().vertices.size(),
           "ordinary held-weapon geometry must obey low model quality");
}

} // namespace

int main() {
    try {
        retail_quality_mapping_is_exact();
        retail_normals_survive_loading_and_all_six_faces();
        inverse_scaling_preserves_size_pivot_and_team_material();
        sights_ignore_low_model_quality();
        std::cout << "model quality tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
