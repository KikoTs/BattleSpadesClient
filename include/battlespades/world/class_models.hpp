#pragma once

#include "battlespades/world/chunk_mesh.hpp"
#include "battlespades/world/class_catalog.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace battlespades::world {

class Kv6Model;

struct ClassModelPart final {
    ChunkMesh mesh;
    std::array<float, 3U> authored_offset{};
    std::array<float, 3U> body_anchor{};
};

struct ClassModelSet final {
    std::uint8_t class_id{};
    std::vector<ClassModelPart> body_parts;
    std::vector<ChunkMesh> first_person_arms;
    /** Debug/selection-screen standing composite in local world units. */
    ChunkMesh standing_preview;
    /** Compatibility standing head/torso composite used by static previews. */
    ChunkMesh standing_body_preview;
    /** Standing torso only; live characters pitch the head independently. */
    ChunkMesh standing_torso_preview;
    /** Head at its standing retail anchor. Crouching adds the recovered lift. */
    ChunkMesh head_preview;
    /** Left leg pre-positioned at the retail hip anchor. */
    ChunkMesh left_leg_preview;
    /** Right leg pre-positioned at the retail hip anchor. */
    ChunkMesh right_leg_preview;
    /** Compatibility complete crouch pose used by static previews. */
    ChunkMesh crouching_preview;
    /** Crouched torso only; the head and legs remain articulated at runtime. */
    ChunkMesh crouching_torso_preview;
    /** Shared crouch-leg asset pre-positioned at the left retail hip anchor. */
    ChunkMesh crouching_left_leg_preview;
    /** Shared crouch-leg asset pre-positioned at the right retail hip anchor. */
    ChunkMesh crouching_right_leg_preview;
};

struct ClassModelLoadResult final {
    std::optional<ClassModelSet> models;
    std::string error;
    [[nodiscard]] explicit operator bool() const noexcept { return models.has_value(); }
};

/** Retail load_model offsets shift the KV6 pivot, not the rendered object. */
[[nodiscard]] std::array<float, 3U>
class_part_preview_origin(const ClassBodyPartDefinition& part) noexcept;

/**
 * Loads every body and first-person arm model declared by one retail class.
 * Retail's global KV6 team-material convention resolves against the requested
 * team color for class-specific and shared body meshes alike.
 */
[[nodiscard]] ClassModelLoadResult
load_class_models(const std::filesystem::path& asset_root, std::uint8_t class_id,
                  VxlColor team_color = {44U, 117U, 179U, 255U},
                  std::uint8_t inverse_scale = 1U);

} // namespace battlespades::world
