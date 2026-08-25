#pragma once

#include "battlespades/frontend/ugc_publish_menu.hpp"

#include <filesystem>
#include <string>
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
[[nodiscard]] bool delete_ugc_project(const std::filesystem::path& maps_root,
                                      std::string_view uid,
                                      std::string& error) noexcept;

} // namespace battlespades::frontend
