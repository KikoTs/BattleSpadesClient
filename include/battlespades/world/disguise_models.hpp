#pragma once

#include "battlespades/world/chunk_mesh.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace battlespades::world {

/** The two retail environment-coloured meshes used while a player is disguised. */
struct DisguiseModelSet final {
    ChunkMesh standing;
    ChunkMesh crouching;
};

struct DisguiseModelLoadResult final {
    std::optional<DisguiseModelSet> models;
    std::string error;

    [[nodiscard]] explicit operator bool() const noexcept {
        return models.has_value();
    }
};

/**
 * Load and colour Character_Disguise_Blocks_Standing/Crouched.kv6.
 *
 * Retail DisguiseBlocks uses model scale 0.1 and the current selected block
 * colour. This loader bakes that scale and the KV6 display-axis conversion
 * into ordinary world meshes so the remote renderer can draw them without a
 * separate character skeleton.
 */
[[nodiscard]] DisguiseModelLoadResult
load_disguise_models(const std::filesystem::path& asset_root,
                     VxlColor environment_color,
                     std::uint8_t inverse_scale = 1U);

/**
 * Vertical root offset for the retail standing/crouched disguise mesh.
 *
 * Retail's -0.8/-0.4 values are expressed on its upward character axis. Our
 * canonical map Z grows downward, so presentation must use the opposite sign
 * or the two disguise blocks visibly hover above their player's feet.
 */
[[nodiscard]] float disguise_ground_offset(bool crouching) noexcept;

} // namespace battlespades::world
