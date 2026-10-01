#pragma once

#include <filesystem>

namespace battlespades::world {

/**
 * Resolve a file shipped under `<assets>/client/cosmetics/`.
 *
 * Some community packs reused files that are byte-identical to retail Ace of
 * Spades content (tracers, casings, stock body parts, reload sounds). Those
 * files are not redistributed by this repository. Instead,
 * `client/cosmetics/retail-substitutes.json` maps each such path to the
 * retail file the player imported into `<assets>/original/`.
 *
 * Returns `file` unchanged when it exists or when no substitute is recorded;
 * otherwise returns the imported retail file. Callers keep verifying hashes
 * exactly as before, so a substitute is only accepted when its bytes match.
 */
[[nodiscard]] std::filesystem::path resolve_cosmetic_file(const std::filesystem::path& file);

} // namespace battlespades::world
