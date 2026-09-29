#include "battlespades/world/weapon_models.hpp"

#include "battlespades/world/kv6_model.hpp"
#include "battlespades/world/weapon_catalog.hpp"
#include "battlespades/world/retail_view_model.hpp"

#include <span>
#include <algorithm>
#include <string_view>
#include <utility>

namespace battlespades::world {
namespace {

[[nodiscard]] std::optional<ChunkMesh> load_part(
    const std::filesystem::path& asset_root, const WeaponModelPartDefinition& part,
    std::array<float, 3U> tint, std::optional<VxlColor> team_color,
    std::uint8_t inverse_scale, std::string& error, const WeaponCosmeticFinish* finish = nullptr) {
    std::string detail;
    const bool replacement = finish && finish->replacement && finish->asset == part.asset;
    auto model = replacement ? std::optional<Kv6Model>{*finish->replacement}
                             : Kv6Model::load_file(asset_root / part.asset, &detail);
    if (!model.has_value()) {
        error = "failed to load " + std::string{part.asset} + ": " + detail;
        return std::nullopt;
    }
    *model = model->inverse_scaled(inverse_scale);
    if (finish && !replacement && finish->asset==part.asset) model->apply_cosmetic_palette(finish->palette);
    if (team_color.has_value()) {
        model->apply_default_color(*team_color);
    }
    auto mesh = model->mesh({}, tint);
    if (replacement) {
        const auto& pivot = finish->replacement->pivot();
        const std::array<float,3U> delta{pivot[0]-finish->pivot[0],
                                       -pivot[2]+finish->pivot[2],pivot[1]-finish->pivot[1]};
        for (auto& vertex : mesh.vertices) {
            vertex.x = (vertex.x+delta[0])*finish->scale;
            vertex.y = (vertex.y+delta[1])*finish->scale;
            vertex.z = (vertex.z+delta[2])*finish->scale;
        }
        for (std::size_t axis{}; axis<3U; ++axis) {
            mesh.minimum[axis]=(mesh.minimum[axis]+delta[axis])*finish->scale;
            mesh.maximum[axis]=(mesh.maximum[axis]+delta[axis])*finish->scale;
        }
    }
    if (mesh.vertices.empty() || mesh.indices.empty()) {
        error = "weapon model produced an empty mesh: " + std::string{part.asset};
        return std::nullopt;
    }
    // KV6.offset_pivots adds the load_model offset to the authored pivot.
    // kv6.pyd then emits authored XYZ as render-space (X, -Z, Y), so the
    // resulting geometry delta is (-offset.x, +offset.z, -offset.y).
    const std::array<float, 3U> render_offset{
        -part.authored_offset[0U], part.authored_offset[2U],
        -part.authored_offset[1U]};
    for (auto& vertex : mesh.vertices) {
        vertex.x += render_offset[0U];
        vertex.y += render_offset[1U];
        vertex.z += render_offset[2U];
    }
    for (std::size_t axis{}; axis < render_offset.size(); ++axis) {
        mesh.minimum[axis] += render_offset[axis];
        mesh.maximum[axis] += render_offset[axis];
    }
    return mesh;
}

[[nodiscard]] bool load_parts(const std::filesystem::path& asset_root,
                              std::span<const WeaponModelPartDefinition> parts,
                              std::array<float, 3U> tint,
                              std::optional<VxlColor> team_color,
                              std::uint8_t inverse_scale,
                              std::vector<ChunkMesh>& output,
                              std::string& error,
                              std::optional<std::size_t> tint_only_part = std::nullopt,
                              const WeaponCosmeticFinish* finish = nullptr) {
    output.reserve(parts.size());
    for (std::size_t index{}; index < parts.size(); ++index) {
        // A complete replacement has one assembled model. Do not retain the
        // parent's rotating barrel or other independently animated geometry.
        if (index > 0U && finish && finish->replacement) break;
        // ZombiePrefabTool is the only retail composite mixing character art
        // with a colourable block. Character.draw changes the KV6 default
        // colour for the block marker; it does not wash both Zombie hands in
        // the palette colour. Keep tinting scoped to the middle BLOCK_MODEL.
        const auto part_tint = tint_only_part.has_value() && index != *tint_only_part
                                   ? std::array<float, 3U>{1.0F, 1.0F, 1.0F}
                                   : tint;
        auto mesh = load_part(asset_root, parts[index], part_tint, team_color,
                              inverse_scale, error, finish);
        if (!mesh.has_value()) {
            return false;
        }
        output.push_back(std::move(*mesh));
    }
    return true;
}

[[nodiscard]] bool load_optional(const std::filesystem::path& asset_root,
                                 const WeaponModelPartDefinition& part,
                                 std::array<float, 3U> tint,
                                 std::optional<ChunkMesh>& output,
                                 std::uint8_t inverse_scale,
                                 std::string& error) {
    if (part.asset.empty()) {
        return true;
    }
    output = load_part(asset_root, part, tint, std::nullopt, inverse_scale, error);
    return output.has_value();
}

} // namespace

WeaponModelLoadResult load_weapon_models(
    const std::filesystem::path& asset_root, std::uint8_t tool_id,
    std::array<float, 3U> tint, std::optional<VxlColor> team_color,
    std::uint8_t inverse_scale, const WeaponCosmeticFinish* finish,
    bool force_default_color) {
    const auto* definition = find_weapon_definition(tool_id);
    if (definition == nullptr) {
        return {std::nullopt, "tool id is outside the selectable catalog"};
    }
    WeaponModelSet result;
    result.tool_id = tool_id;
    std::string error;
    // force_default_color: Character.draw's use_color and
    // use_other_team_color paths also call set_kv6_default_color.
    const auto model_team_color = definition->retail.use.use_team_color || force_default_color
                                      ? team_color
                                      : std::nullopt;
    constexpr std::uint8_t zombie_prefab_tool_id{28U};
    const auto tint_only_part = tool_id == zombie_prefab_tool_id
                                    ? std::optional<std::size_t>{1U}
                                    : std::nullopt;
    if (!load_parts(asset_root, definition->third_person_models, tint,
                    model_team_color, inverse_scale,
                    result.third_person_parts, error, tint_only_part, finish) ||
        !load_parts(asset_root, definition->first_person_models, tint,
                    model_team_color, inverse_scale,
                    result.first_person_parts, error, tint_only_part, finish) ||
        // models.py passes min_model_detail=2 for sight and pin so aiming
        // geometry never loses alignment at low global model quality.
        !load_optional(asset_root, {definition->sight_model_asset, {}}, tint,
                       result.sight, 1U, error) ||
        // The pin keeps its authored (0, 0, -0.5) load offset, so it goes
        // through the same part path as any other model rather than the
        // zero-offset shorthand the sight/casing/tracer can afford.
        !load_optional(asset_root, definition->pin_model, tint, result.pin, 1U,
                       error) ||
        !load_optional(asset_root, {definition->casing_model_asset, {}}, tint,
                       result.casing, inverse_scale, error) ||
        !load_optional(asset_root, {definition->tracer_model_asset, {}}, tint,
                       result.tracer, inverse_scale, error)) {
        return {std::nullopt, std::move(error)};
    }
    if(finish&&finish->sight_replacement){result.sight=finish->sight_replacement->mesh({},tint);result.pin.reset();}
    // Preserve older one-point fits only when no two-point calibration exists.
    // Untagged skins keep the parent's authored ADS/optic and pin.
    if (finish && finish->replacement && !finish->sight_tags && finish->sight_pivot && !definition->first_person_models.empty()) {
        auto aimed=*finish;
        aimed.pivot=*finish->sight_pivot;
        result.sight=load_part(asset_root,{definition->first_person_models.front().asset,{}},
            tint,std::nullopt,1U,error,&aimed);
        if (!result.sight) return {std::nullopt,std::move(error)};
        result.pin.reset();
    }
    if (finish && finish->replacement && finish->sight_tags && result.sight) {
        auto mesh = finish->replacement->mesh({}, tint);
        const auto& pivot = finish->replacement->pivot();
        const auto pose = evaluate_weapon_sight(tool_id);
        mesh.minimum = {1e9F, 1e9F, 1e9F};
        mesh.maximum = {-1e9F, -1e9F, -1e9F};
        for (auto& vertex : mesh.vertices) {
            const auto point = sight_tag_position(*finish->sight_tags,
                {vertex.x+pivot[0],vertex.z+pivot[1],pivot[2]-vertex.y},
                finish->scale*static_cast<float>(pose.model_scale));
            vertex.x = (point[0]-static_cast<float>(pose.position.x))/static_cast<float>(pose.model_scale);
            vertex.y = (point[1]-static_cast<float>(pose.position.y))/static_cast<float>(pose.model_scale);
            vertex.z = (point[2]-static_cast<float>(pose.position.z))/static_cast<float>(pose.model_scale);
            const std::array values{vertex.x,vertex.y,vertex.z};
            for (std::size_t axis{};axis<3U;++axis) {
                mesh.minimum[axis]=std::min(mesh.minimum[axis],values[axis]);
                mesh.maximum[axis]=std::max(mesh.maximum[axis],values[axis]);
            }
        }
        result.sight = std::move(mesh);
        result.pin.reset();
    }
    return {std::move(result), {}};
}

namespace {

/** Kv6Model::mesh's per-channel tint on an unshaded, untinted colour. */
void tint_mesh_colors(ChunkMesh& mesh, std::array<float, 3U> tint) {
    const auto channel = [](std::uint32_t abgr, unsigned int shift, float channel_tint) {
        const auto value = static_cast<float>((abgr >> shift) & 0xFFU);
        return static_cast<std::uint32_t>(std::min(255.0F, value * channel_tint + 0.5F))
               << shift;
    };
    for (auto& vertex : mesh.vertices) {
        vertex.abgr = (vertex.abgr & 0xFF000000U) | channel(vertex.abgr, 0U, tint[0U]) |
                      channel(vertex.abgr, 8U, tint[1U]) | channel(vertex.abgr, 16U, tint[2U]);
    }
}

} // namespace

WeaponModelSet tinted_weapon_models(WeaponModelSet untinted, std::array<float, 3U> tint) {
    if (tint == std::array<float, 3U>{1.0F, 1.0F, 1.0F}) {
        return untinted;
    }
    // Same rule as load_weapon_models: ZombiePrefabTool tints only part 1.
    constexpr std::uint8_t zombie_prefab_tool_id{28U};
    const auto tint_parts = [&](std::vector<ChunkMesh>& parts) {
        for (std::size_t index{}; index < parts.size(); ++index) {
            if (untinted.tool_id == zombie_prefab_tool_id && index != 1U) {
                continue;
            }
            tint_mesh_colors(parts[index], tint);
        }
    };
    tint_parts(untinted.third_person_parts);
    tint_parts(untinted.first_person_parts);
    for (auto* optional : {&untinted.sight, &untinted.pin, &untinted.casing, &untinted.tracer}) {
        if (optional->has_value()) {
            tint_mesh_colors(**optional, tint);
        }
    }
    return untinted;
}

} // namespace battlespades::world
