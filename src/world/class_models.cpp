#include "battlespades/world/class_models.hpp"

#include "battlespades/world/class_catalog.hpp"
#include "battlespades/world/kv6_model.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace battlespades::world {
namespace {

constexpr float body_scale{0.05F};

// The standing body and leg volumes overlap by 0.125 world units at the
// recovered BODY_PARTS anchors (head/torso z 0.3, legs 1.1). Retail keeps that
// overlap: Character.set_crouch (character.pyx 829-837) places the torso and
// head exactly at those anchors, so no upper-body lift is applied here.

// Retail has one shared crouched-leg asset but draws it at both leg anchors.
// BODY_PART_LEG_CROUCH_Y is -0.3 in shared.constants; treating the catalog row
// as one visible part produced the single centred leg reported in gameplay.
constexpr std::array<float, 3U> crouched_left_leg_adjust{0.25F, -0.3F, 0.0F};
constexpr std::array<float, 3U> crouched_right_leg_adjust{-0.25F, -0.3F, 0.0F};

[[nodiscard]] std::optional<ChunkMesh>
load_mesh(const std::filesystem::path& root, std::string_view path,
          VxlColor team_color, std::uint8_t inverse_scale, std::string& error,
          std::optional<std::array<std::uint8_t,3U>> palette = std::nullopt,
          const Kv6Model* replacement = nullptr,
          std::optional<bool> upper_half = std::nullopt) {
    std::string detail;
    auto model = replacement ? std::optional<Kv6Model>{*replacement} : Kv6Model::load_file(root / path, &detail);
    if (!model.has_value()) {
        error = "failed to load " + std::string{path} + ": " + detail;
        return std::nullopt;
    }
    *model = model->inverse_scaled(inverse_scale);
    if (palette) model->apply_cosmetic_palette(*palette);
    model->apply_default_color(team_color);
    if (upper_half) *model = model->split_at_middle_z(*upper_half);
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
                                       std::uint8_t inverse_scale,
                                       std::optional<std::array<std::uint8_t,3U>> palette,
                                       const Kv6Model* head_override,
                                       const ClassModelOverrides* body_override) {
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
    for (auto& half : result.leg_halves) initialize_bounds(half);
    initialize_bounds(result.crouching_preview);
    initialize_bounds(result.crouching_torso_preview);
    initialize_bounds(result.crouching_left_leg_preview);
    initialize_bounds(result.crouching_right_leg_preview);
    std::string error;
    const auto replacement=[&](std::string_view role)->const Kv6Model* {
        if(!body_override)return nullptr;
        const auto it=body_override->find(std::string{role});return it==body_override->end()?nullptr:&it->second;
    };
    const auto fit=[&](ChunkMesh& mesh,const Kv6Model* model,std::string_view parent,std::string_view role){
        if(!model)return;
        const auto original=Kv6Model::load_file(asset_root/parent);if(!original)return;
        float scale=static_cast<float>(original->size_z())/static_cast<float>(model->size_z());
        const auto a=original->pivot(),b=model->pivot();
        std::array<float,3> from{(static_cast<float>(model->size_x())-1)*.5F-b[0],-((static_cast<float>(model->size_z())-1)*.5F-b[2]),(static_cast<float>(model->size_y())-1)*.5F-b[1]};
        const std::array<float,3> to{(static_cast<float>(original->size_x())-1)*.5F-a[0],-((static_cast<float>(original->size_z())-1)*.5F-a[2]),(static_cast<float>(original->size_y())-1)*.5F-a[1]};
        if(body_override->open_spades){
            // Fit the original attachment frame, not the total bounding box:
            // antennas, helmets and backpacks must retain their authored extent.
            if(role.starts_with("arm_")){
                scale=static_cast<float>(original->size_y())/12.F;
                constexpr std::array<std::uint8_t,6> faces{0,1,5,4,2,3};
                for(auto& v:mesh.vertices){const auto y=v.y;v.y=v.z;v.z=-y;v.face=faces[v.face];}
                from={0.F,-.5F,5.5F};
            }else{
                const float height=role=="head"?6.F:role=="torso"?9.F:role=="torso_crouch"?7.F:role=="leg_crouch"?8.F:12.F;
                scale=static_cast<float>(original->size_z())/height;
                from=role=="head"?std::array<float,3>{0,3,0}:role=="torso"?std::array<float,3>{0,-4.5F,0}:role=="torso_crouch"?std::array<float,3>{0,-2.5F,-3}:role=="leg_crouch"?std::array<float,3>{0,-2.5F,1.5F}:std::array<float,3>{0,-5.5F,.5F};
            }
        }
        for(auto& v:mesh.vertices){v.x=(v.x-from[0])*scale+to[0];v.y=(v.y-from[1])*scale+to[1];v.z=(v.z-from[2])*scale+to[2];}
        mesh.minimum.fill(std::numeric_limits<float>::max());mesh.maximum.fill(std::numeric_limits<float>::lowest());
        for(const auto& v:mesh.vertices){const std::array xyz{v.x,v.y,v.z};for(std::size_t i=0;i<3;++i){mesh.minimum[i]=std::min(mesh.minimum[i],xyz[i]);mesh.maximum[i]=std::max(mesh.maximum[i],xyz[i]);}}
    };
    for (const auto& part : definition->body_parts) {
        const bool hat = part.part == BodyPart::head && head_override;
        const char* role=part.part==BodyPart::head?"head":part.part==BodyPart::torso?"torso":part.part==BodyPart::crouched_torso?"torso_crouch":part.part==BodyPart::crouched_leg?"leg_crouch":part.part==BodyPart::left_leg||part.part==BodyPart::right_leg?"leg":"";
        const auto* source=hat?head_override:replacement(role);
        auto mesh = load_mesh(asset_root, part.model_asset, team_color, inverse_scale, error,
                              source ? std::nullopt : palette, source);
        if (!mesh.has_value()) {
            return {std::nullopt, std::move(error)};
        }
        if(!hat)fit(*mesh,source,part.model_asset,role);
        // Arms_Collision is a physics hull, not visible character geometry.
        // Retail builds the visible upper/lower arms from the class arm KV6s.
        if (part.part == BodyPart::head || part.part == BodyPart::torso ||
            part.part == BodyPart::left_leg || part.part == BodyPart::right_leg) {
            append_preview(result.standing_preview, *mesh, part);
        }
        if (part.part == BodyPart::head || part.part == BodyPart::torso) {
            append_preview(result.standing_body_preview, *mesh, part);
            if (part.part == BodyPart::head) {
                append_preview(result.head_preview, *mesh, part);
            } else {
                append_preview(result.standing_torso_preview, *mesh, part);
            }
        } else if (part.part == BodyPart::left_leg) {
            append_preview(result.left_leg_preview, *mesh, part);
        } else if (part.part == BodyPart::right_leg) {
            append_preview(result.right_leg_preview, *mesh, part);
        }
        if (part.part == BodyPart::left_leg || part.part == BodyPart::right_leg) {
            for (const bool upper : {true, false}) {
                auto half = load_mesh(asset_root, part.model_asset, team_color, inverse_scale, error,
                                      source ? std::nullopt : palette, source, upper);
                if (!half.has_value()) {
                    return {std::nullopt, std::move(error)};
                }
                fit(*half, source, part.model_asset, role);
                append_preview(result.leg_halves[(part.part == BodyPart::left_leg ? 0U : 2U) +
                                                 (upper ? 0U : 1U)],
                               *half, part);
            }
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
    std::size_t arm_index=0;
    if(const auto* combined=replacement("arms");combined&&!replacement("arm_upper")&&!replacement("arm_lower")){
        auto model=*combined;model.apply_default_color(team_color);auto mesh=model.mesh();
        for(auto& v:mesh.vertices){const auto y=v.y;v.x=v.x*.1F+.05F;v.y=v.z*.1F-.075F;v.z=-y*.1F+.25F;}
        result.combined_arms=std::move(mesh);
    }
    for (const auto path : definition->first_person_arm_assets) {
        if(result.combined_arms)break;
        if (path.empty()) {
            continue; // zombies intentionally render hands from the equipped tool.
        }
        const auto role=arm_index++==0?"arm_upper":"arm_lower";
        const auto* source=replacement(role);
        auto mesh = load_mesh(asset_root, path, team_color, inverse_scale, error, source?std::nullopt:palette,source);
        if (!mesh.has_value()) {
            return {std::nullopt, std::move(error)};
        }
        fit(*mesh,source,path,role);
        result.first_person_arms.push_back(std::move(*mesh));
    }
    if (result.standing_preview.empty()) {
        return {std::nullopt, "class standing preview has no geometry"};
    }
    return {std::move(result), {}};
}

} // namespace battlespades::world
