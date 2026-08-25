#pragma once

#include "battlespades/world/chunk_mesh.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace battlespades::world {

/** Render-ready KV6 parts for one exact retail tool definition. */
struct WeaponModelSet final {
    std::uint8_t tool_id{};
    std::vector<ChunkMesh> third_person_parts;
    std::vector<ChunkMesh> first_person_parts;
    std::optional<ChunkMesh> sight;
    /** Character.draw_sight's second model; only the classic rifle has one. */
    std::optional<ChunkMesh> pin;
    std::optional<ChunkMesh> casing;
    std::optional<ChunkMesh> tracer;
};

struct WeaponModelLoadResult final {
    std::optional<WeaponModelSet> models;
    std::string error;
    [[nodiscard]] explicit operator bool() const noexcept {
        return models.has_value();
    }
};

/**
 * Load every authored KV6 part required by one tool.
 *
 * Empty model arrays are valid for contextual tools such as the mounted MG.
 * A declared but missing/corrupt model fails the entire load so a client can
 * reject incomplete data instead of rendering a misleading fallback weapon.
 */
[[nodiscard]] WeaponModelLoadResult load_weapon_models(
    const std::filesystem::path& asset_root, std::uint8_t tool_id,
    std::array<float, 3U> tint = {1.0F, 1.0F, 1.0F},
    std::optional<VxlColor> team_color = std::nullopt,
    std::uint8_t inverse_scale = 1U);

} // namespace battlespades::world
