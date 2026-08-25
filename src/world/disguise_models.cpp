#include "battlespades/world/disguise_models.hpp"

#include "battlespades/world/kv6_model.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <string_view>
#include <utility>

namespace battlespades::world {
namespace {

constexpr float disguise_scale{0.1F};

[[nodiscard]] std::optional<ChunkMesh>
load_mesh(const std::filesystem::path& asset_root, std::string_view asset,
          VxlColor color, std::uint8_t inverse_scale, std::string& error) {
    std::string detail;
    auto model = Kv6Model::load_file(asset_root / asset, &detail);
    if (!model.has_value()) {
        error = "failed to load " + std::string{asset} + ": " + detail;
        return std::nullopt;
    }
    *model = model->inverse_scaled(inverse_scale);
    auto mesh = model->mesh();
    if (mesh.empty()) {
        error = "disguise model produced an empty mesh: " + std::string{asset};
        return std::nullopt;
    }

    mesh.minimum.fill(std::numeric_limits<float>::max());
    mesh.maximum.fill(std::numeric_limits<float>::lowest());
    const auto environment_abgr =
        (static_cast<std::uint32_t>(color.blue) << 16U) |
        (static_cast<std::uint32_t>(color.green) << 8U) |
        static_cast<std::uint32_t>(color.red);
    for (auto& vertex : mesh.vertices) {
        // Both retail disguise assets are authored entirely with the opaque
        // FCFCFC80 tint marker, not the black/purple marker used by character
        // team colours. Retail applies the player's selected block colour at
        // draw time. Bake that tint here because our world renderer has no
        // per-model blend-colour uniform.
        vertex.abgr = environment_abgr;
        // Kv6Model meshes are in DisplayList GL axes (X,-Z,Y). Remote player
        // roots use canonical world XYZ; this is the same conversion used by
        // ClassModelSet's combined standing/crouching previews.
        const float display_y = vertex.y;
        const float display_z = vertex.z;
        vertex.x *= disguise_scale;
        vertex.y = display_z * disguise_scale;
        vertex.z = -display_y * disguise_scale;
        mesh.minimum[0U] = std::min(mesh.minimum[0U], vertex.x);
        mesh.minimum[1U] = std::min(mesh.minimum[1U], vertex.y);
        mesh.minimum[2U] = std::min(mesh.minimum[2U], vertex.z);
        mesh.maximum[0U] = std::max(mesh.maximum[0U], vertex.x);
        mesh.maximum[1U] = std::max(mesh.maximum[1U], vertex.y);
        mesh.maximum[2U] = std::max(mesh.maximum[2U], vertex.z);
    }
    return mesh;
}

} // namespace

DisguiseModelLoadResult
load_disguise_models(const std::filesystem::path& asset_root,
                     VxlColor environment_color,
                     std::uint8_t inverse_scale) {
    environment_color.alpha = 255U;
    std::string error;
    auto standing = load_mesh(
        asset_root, "kv6/Character_Disguise_Blocks_Standing.kv6",
        environment_color, inverse_scale, error);
    if (!standing.has_value()) {
        return {std::nullopt, std::move(error)};
    }
    auto crouching = load_mesh(
        asset_root, "kv6/Character_Disguise_Blocks_Crouched.kv6",
        environment_color, inverse_scale, error);
    if (!crouching.has_value()) {
        return {std::nullopt, std::move(error)};
    }
    return {DisguiseModelSet{std::move(*standing), std::move(*crouching)}, {}};
}

float disguise_ground_offset(bool crouching) noexcept {
    return crouching ? 0.4F : 0.8F;
}

} // namespace battlespades::world
