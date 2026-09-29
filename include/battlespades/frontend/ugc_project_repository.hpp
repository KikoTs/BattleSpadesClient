#pragma once

#include "battlespades/frontend/ugc_publish_menu.hpp"

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace battlespades::frontend {

/** Result of one bounded scan of the client-owned hosted_ugc/maps catalog. */
struct UgcProjectScanResult final {
    std::vector<UgcLocalMapRecord> maps;
    std::vector<std::string> warnings;
};

/**
 * Read the local Map Creator triplets without trusting paths stored in JSON.
 *
 * The repository accepts only regular `.ugc` files directly below `maps_root`,
 * bounds every sidecar to 1 MiB, and derives sibling VXL/TXT/PNG paths from the
 * sidecar stem.  Malformed projects are reported but never abort discovery of
 * the remaining catalog.
 */
[[nodiscard]] UgcProjectScanResult
scan_ugc_projects(const std::filesystem::path& maps_root);

/**
 * Delete one opaque UID previously returned by scan_ugc_projects.
 *
 * `uid` must be a single `.ugc` filename.  The four project siblings are
 * removed from `maps_root`; path traversal and symlink redirection fail closed.
 */
/**
 * Every distinct file stem directly below `maps_root` (any extension), used
 * to keep generated `Custommap_N` names clear of partial projects.
 */
[[nodiscard]] std::vector<std::string>
list_ugc_map_stems(const std::filesystem::path& maps_root);

/**
 * matchSettings.generate_ugc_map_title: `base` unchanged when free, else the
 * first "<base>-N" (N = 1, 2, ...) that no existing title uses.
 */
[[nodiscard]] std::string generate_ugc_map_title(std::string_view base,
                                                 std::span<const std::string> existing_titles);

/**
 * matchSettings.generate_ugc_map_filename: "Custommap_1" when no stem starts
 * with "Custommap"; otherwise count those stems and step "Custommap_<count+1>"
 * upward until the stem is unused (case-insensitive, like the Windows catalog).
 */
[[nodiscard]] std::string generate_ugc_map_filename(std::span<const std::string> existing_stems);

[[nodiscard]] bool delete_ugc_project(const std::filesystem::path& maps_root,
                                      std::string_view uid,
                                      std::string& error) noexcept;

} // namespace battlespades::frontend
