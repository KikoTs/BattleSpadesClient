#include "battlespades/world/class_models.hpp"

#include "battlespades/world/class_catalog.hpp"
#include "battlespades/world/kv6_model.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace battlespades::world {
namespace {

constexpr float body_scale{0.05F};

// The raw standing body and leg volumes overlap by 0.125 world units after
// applying the recovered BODY_PARTS anchors. Retail hides most of that inside
// the hip seam; our centred KV6 cubes expose it as the torso sitting in the
// thighs. Lift the complete upper body by two KV6 voxels, retaining a small
// 0.025-unit seam so movement never opens a visible gap.
constexpr float standing_upper_body_lift{-2.0F * body_scale};

// Retail has one shared crouched-leg asset but draws it at both leg anchors.
// BODY_PART_LEG_CROUCH_Y is -0.3 in shared.constants; treating the catalog row
// as one visible part produced the single centred leg reported in gameplay.
constexpr std::array<float, 3U> crouched_left_leg_adjust{0.25F, -0.3F, 0.0F};
constexpr std::array<float, 3U> crouched_right_leg_adjust{-0.25F, -0.3F, 0.0F};

[[nodiscard]] std::optional<ChunkMesh>
load_mesh(const std::filesystem::path& root, std::string_view path,
          VxlColor team_color, std::uint8_t inverse_scale, std::string& error) {
    std::string detail;
    auto model = Kv6Model::load_file(root / path, &detail);
    if (!model.has_value()) {
        error = "failed to load " + std::string{path} + ": " + detail;
        return std::nullopt;
    }
    *model = model->inverse_scaled(inverse_scale);
    model->apply_default_color(team_color);
    auto mesh = model->mesh();
    if (mesh.empty()) {
        error = "class model produced an empty mesh: " + std::string{path};
        return std::nullopt;
    }
    return mesh;
}

void append_preview(ChunkMesh& destination, const ChunkMesh& source,
                    const ClassBodyPartDefinition& part,
                    std::array<float, 3U> position_adjust = {}) {
    const auto first_vertex = destination.vertices.size();
    const auto origin = class_part_preview_origin(part);
    for (auto vertex : source.vertices) {
        // The standing catalog preview is assembled in its historical
        // authored XYZ/z-down layout. Kv6Model meshes are correctly stored in
        // retail OpenGL (X,-Z,Y), so undo that one asset-boundary conversion
        // only for this pre-combined mannequin. Runtime body/arm draws keep
        // the native retail basis.
        const auto retail_y = vertex.y;
        const auto retail_z = vertex.z;
        vertex.x = vertex.x * body_scale + origin[0U] + position_adjust[0U];
        vertex.y = retail_z * body_scale + origin[1U] + position_adjust[1U];
        vertex.z = -retail_y * body_scale + origin[2U] + position_adjust[2U];
        destination.vertices.push_back(vertex);
        destination.minimum[0U] = std::min(destination.minimum[0U], vertex.x);
        destination.minimum[1U] = std::min(destination.minimum[1U], vertex.y);
        destination.minimum[2U] = std::min(destination.minimum[2U], vertex.z);
        destination.maximum[0U] = std::max(destination.maximum[0U], vertex.x);
        destination.maximum[1U] = std::max(destination.maximum[1U], vertex.y);
        destination.maximum[2U] = std::max(destination.maximum[2U], vertex.z);
    }
    for (const auto index : source.indices) {
        destination.indices.push_back(static_cast<std::uint32_t>(first_vertex) + index);
    }
}

} // namespace

std::array<float, 3U>
class_part_preview_origin(const ClassBodyPartDefinition& part) noexcept {
    return {
        part.body_anchor[0U] - part.authored_offset[0U] * body_scale,
        part.body_anchor[1U] - part.authored_offset[1U] * body_scale,
        part.body_anchor[2U] - part.authored_offset[2U] * body_scale,
    };
}

ClassModelLoadResult load_class_models(const std::filesystem::path& asset_root,
                                       std::uint8_t class_id,
                                       VxlColor team_color,
                                       std::uint8_t inverse_scale) {
    const auto* definition = find_class_definition(class_id);
    if (definition == nullptr) {
        return {std::nullopt, "class id is outside the retail catalog"};
    }

    ClassModelSet result;
    result.class_id = class_id;
    result.body_parts.reserve(definition->body_parts.size());
    const auto initialize_bounds = [](ChunkMesh& mesh) {
        mesh.minimum.fill(std::numeric_limits<float>::max());
        mesh.maximum.fill(std::numeric_limits<float>::lowest());
    };
    initialize_bounds(result.standing_preview);
    initialize_bounds(result.standing_body_preview);
    initialize_bounds(result.standing_torso_preview);
    initialize_bounds(result.head_preview);
    initialize_bounds(result.left_leg_preview);
    initialize_bounds(result.right_leg_preview);
    initialize_bounds(result.crouching_preview);
    initialize_bounds(result.crouching_torso_preview);
    initialize_bounds(result.crouching_left_leg_preview);
    initialize_bounds(result.crouching_right_leg_preview);
    std::string error;
    for (const auto& part : definition->body_parts) {
        auto mesh = load_mesh(asset_root, part.model_asset, team_color, inverse_scale, error);
        if (!mesh.has_value()) {
            return {std::nullopt, std::move(error)};
        }
        // Arms_Collision is a physics hull, not visible character geometry.
        // Retail builds the visible upper/lower arms from the class arm KV6s.
        if (part.part == BodyPart::head || part.part == BodyPart::torso ||
            part.part == BodyPart::left_leg || part.part == BodyPart::right_leg) {
            const std::array<float, 3U> adjustment =
                part.part == BodyPart::head || part.part == BodyPart::torso
                    ? std::array<float, 3U>{0.0F, 0.0F, standing_upper_body_lift}
                    : std::array<float, 3U>{};
            append_preview(result.standing_preview, *mesh, part, adjustment);
        }
        if (part.part == BodyPart::head || part.part == BodyPart::torso) {
            append_preview(result.standing_body_preview, *mesh, part,
                           {0.0F, 0.0F, standing_upper_body_lift});
            if (part.part == BodyPart::head) {
                append_preview(result.head_preview, *mesh, part,
                               {0.0F, 0.0F, standing_upper_body_lift});
            } else {
                append_preview(result.standing_torso_preview, *mesh, part,
                               {0.0F, 0.0F, standing_upper_body_lift});
            }
        } else if (part.part == BodyPart::left_leg) {
            append_preview(result.left_leg_preview, *mesh, part);
        } else if (part.part == BodyPart::right_leg) {
            append_preview(result.right_leg_preview, *mesh, part);
        }
        if (part.part == BodyPart::head || part.part == BodyPart::crouched_torso) {
            append_preview(result.crouching_preview, *mesh, part);
            if (part.part == BodyPart::crouched_torso) {
                append_preview(result.crouching_torso_preview, *mesh, part);
            }
        } else if (part.part == BodyPart::crouched_leg) {
            // The catalog contains one shared model definition, while the
            // character pose contains two independently anchored legs.
            append_preview(result.crouching_preview, *mesh, part,
                           crouched_left_leg_adjust);
            append_preview(result.crouching_preview, *mesh, part,
                           crouched_right_leg_adjust);
            append_preview(result.crouching_left_leg_preview, *mesh, part,
                           crouched_left_leg_adjust);
            append_preview(result.crouching_right_leg_preview, *mesh, part,
                           crouched_right_leg_adjust);
        }
        result.body_parts.push_back(
            ClassModelPart{std::move(*mesh), part.authored_offset, part.body_anchor});
    }
    for (const auto path : definition->first_person_arm_assets) {
        if (path.empty()) {
            continue; // zombies intentionally render hands from the equipped tool.
        }
        auto mesh = load_mesh(asset_root, path, team_color, inverse_scale, error);
        if (!mesh.has_value()) {
            return {std::nullopt, std::move(error)};
        }
        result.first_person_arms.push_back(std::move(*mesh));
    }
    if (result.standing_preview.empty()) {
        return {std::nullopt, "class standing preview has no geometry"};
    }
    return {std::move(result), {}};
}

} // namespace battlespades::world
