#pragma once

#include "battlespades/updater/update_apply.hpp"
#include "battlespades/updater/update_manifest.hpp"
#include "battlespades/updater/update_plan.hpp"
#include "battlespades/updater/updater_config.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace battlespades::updater::win {

struct SessionCallbacks {
    std::function<void(std::string_view)> status;                 ///< one-line UI status
    std::function<bool(std::uint64_t, std::uint64_t)> progress;   ///< false = cancel
    std::function<void(std::string_view)> log;
};

enum class DiscoverySource { manifest, github_fallback, none };

struct Discovery {
    DiscoverySource source{DiscoverySource::none};
    std::optional<UpdateManifest> manifest;
    std::string error;
};

/**
 * Fetches stable.json (or the configured channel/URL). When it is
 * unreachable and updater.json enables github_fallback, the old GitHub
 * latest-release check is turned into a one-component ("client") manifest.
 */
[[nodiscard]] Discovery discover_updates(const UpdaterConfig& config, const SessionCallbacks& callbacks);

enum class SessionOutcome { applied, partial, nothing_to_do, cancelled, failed };

struct SessionResult {
    SessionOutcome outcome{SessionOutcome::nothing_to_do};
    std::string error;
    std::vector<std::string> applied;   ///< "component version"
    std::vector<std::string> failed;    ///< component names that could not be installed
};

/**
 * Downloads (resumable, mirror by mirror, size + SHA-256 verified), stages
 * and applies each update independently: a failing component is restored on
 * its own and never blocks or rolls back another. retail_assets is installed
 * through BattleSpadesAssetInstaller (same validation as a folder import).
 * Partial and verified archives stay in update/download for the next try.
 */
[[nodiscard]] SessionResult run_update_session(const UpdateLayout& layout, const UpdateManifest& manifest,
                                               const std::vector<PlannedUpdate>& updates, const UpdaterConfig& config,
                                               const SessionCallbacks& callbacks);

/// Rolls back every component changed by the most recent update session.
[[nodiscard]] SessionResult rollback_last_session(const UpdateLayout& layout);

} // namespace battlespades::updater::win
