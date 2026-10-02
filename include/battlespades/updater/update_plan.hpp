#pragma once

#include "battlespades/updater/update_apply.hpp"
#include "battlespades/updater/update_manifest.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace battlespades::updater {

/// How the launcher presents one pending component update.
enum class UpdatePrompt {
    automatic,   ///< small: download right away, "Play without updating" allowed
    ask,         ///< large: "Update now / Later", Later allowed
    mandatory,   ///< `required`, or a large update after too many Laters
};

struct PlannedUpdate {
    ComponentRelease release;
    std::string installed;          ///< empty when the component is not installed yet
    UpdatePrompt prompt{UpdatePrompt::automatic};
    bool large{};
    std::uint32_t deferrals{};      ///< "Later" answers so far for this version
};

struct PlanPolicy {
    std::uint64_t large_update_bytes{200ULL * 1024ULL * 1024ULL};
    std::uint32_t max_deferrals{3U};
};

struct UpdatePlan {
    std::vector<PlannedUpdate> updates;
    std::vector<std::string> notes;   ///< why something was skipped, for the log
};

/**
 * Per-launch updates. Only components that are installed (have a known
 * version) and whose manifest version is newer are planned; optional
 * components that are not installed are never forced. Every component is
 * independent: there is no cross-component pairing here. A version the
 * player rolled back from is skipped unless it is required.
 */
[[nodiscard]] UpdatePlan plan_updates(const UpdateManifest& manifest, const InstalledVersions& installed,
                                      const UpdaterState& state, const PlanPolicy& policy);

/**
 * Explicit, player-chosen installs ("Download game assets", "Enable
 * hosting"): each named component the manifest offers, when it is missing
 * or older. Never asks (the player already chose).
 */
[[nodiscard]] std::vector<PlannedUpdate> plan_install(const UpdateManifest& manifest, const InstalledVersions& installed,
                                                      const std::vector<std::string>& names);

/// Drops non-mandatory updates the player declined.
[[nodiscard]] std::vector<PlannedUpdate> without_declined(std::vector<PlannedUpdate> updates,
                                                          const std::vector<std::string>& declined);

/// A failed mandatory update only stops the game for parts it needs to run;
/// the bundled server is only needed for hosting.
[[nodiscard]] bool blocks_play(const PlannedUpdate& update) noexcept;

[[nodiscard]] std::string deferral_key(const ComponentRelease& release);

// ---------------------------------------------------------------------------
// Hosting (Create Match / Map Creator) readiness. Never affects playing or
// joining remote servers: the protocol 168 handshake decides those.
// ---------------------------------------------------------------------------

enum class HostingState { ready, not_installed, update_required };

struct HostingStatus {
    HostingState state{HostingState::not_installed};
    std::string installed_server;
    std::string available_server;      ///< manifest server version, if any
    std::uint64_t download_size{};
    bool update_available{};           ///< the manifest server would make hosting work
    std::string reason;                ///< human readable when not ready
};

/**
 * Compatibility = network protocol (client vs bundled server) plus the
 * client's optional `hosting_server` minimum. Protocols come from the
 * arguments when known (battlespades-version.json for the client), else from
 * the manifest entry whose version is installed; unknown means compatible.
 */
[[nodiscard]] HostingStatus hosting_status(const UpdateManifest* manifest, const InstalledVersions& installed,
                                           std::optional<std::uint32_t> client_protocol,
                                           std::optional<std::uint32_t> server_protocol);

/// update/hosting.json, read by the client before Create Match / Map Creator.
[[nodiscard]] std::string hosting_status_json(const HostingStatus& status);
[[nodiscard]] const char* to_string(HostingState state) noexcept;

enum class MirrorOutcome { success, failed, cancelled };

struct MirrorResult {
    MirrorOutcome outcome{MirrorOutcome::failed};
    std::size_t used{};                 ///< index of the mirror that succeeded
    std::vector<std::string> errors;    ///< one entry per failed mirror, in order
};

/**
 * Tries each mirror in order until `attempt` succeeds. The attempt downloads
 * (resuming a partial file when the server supports ranges) and verifies
 * size + SHA-256; any failure - transport, HTTP status, mismatch - moves on
 * to the next mirror. Cancellation stops immediately.
 */
[[nodiscard]] MirrorResult try_mirrors(const std::vector<std::string>& urls,
                                       const std::function<MirrorOutcome(const std::string&, std::string&)>& attempt);

/**
 * The one check every downloaded package passes (launcher updates and the
 * asset installer's "Download game assets" alike): exact size, then SHA-256.
 * `error` says which check failed.
 */
[[nodiscard]] bool verify_package_file(const std::filesystem::path& file, std::uint64_t expected_size,
                                       std::string_view expected_sha256, std::string& error);

} // namespace battlespades::updater
