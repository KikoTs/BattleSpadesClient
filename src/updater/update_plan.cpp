#include "battlespades/updater/update_plan.hpp"

#include "battlespades/updater/file_util.hpp"
#include "battlespades/updater/semver.hpp"
#include "battlespades/updater/sha256.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace battlespades::updater {
namespace {

[[nodiscard]] std::string installed_of(const InstalledVersions& installed, std::string_view name) {
    const auto found = installed.find(std::string{name});
    return found == installed.end() ? std::string{} : found->second;
}

[[nodiscard]] bool newer_than_installed(const ComponentRelease& release, const std::string& installed) {
    if (installed.empty() || !parse_version(installed).has_value()) return true;
    return is_newer_version(release.version, installed);
}

} // namespace

std::string deferral_key(const ComponentRelease& release) { return release.name + "@" + release.version; }

UpdatePlan plan_updates(const UpdateManifest& manifest, const InstalledVersions& installed, const UpdaterState& state,
                        const PlanPolicy& policy) {
    UpdatePlan plan;
    for (const auto& release : manifest.components) {
        const auto current = installed_of(installed, release.name);
        if (current.empty()) {
            plan.notes.push_back(release.name + " is not installed; offered, not forced");
            continue;
        }
        if (!newer_than_installed(release, current)) continue;
        const bool required = manifest.required || release.required;
        const auto skipped = state.skipped_versions.find(release.name);
        if (!required && skipped != state.skipped_versions.end() && skipped->second == release.version) {
            plan.notes.push_back(release.name + " " + release.version + " was rolled back before; not reinstalling");
            continue;
        }
        PlannedUpdate update;
        update.release = release;
        update.installed = current;
        update.large = release.size > policy.large_update_bytes;
        const auto counted = state.deferrals.find(deferral_key(release));
        update.deferrals = counted == state.deferrals.end() ? 0U : counted->second;
        if (required) {
            update.prompt = UpdatePrompt::mandatory;
        } else if (update.large) {
            update.prompt = update.deferrals >= policy.max_deferrals ? UpdatePrompt::mandatory : UpdatePrompt::ask;
        } else {
            update.prompt = UpdatePrompt::automatic;
        }
        plan.updates.push_back(std::move(update));
    }
    return plan;
}

std::vector<PlannedUpdate> plan_install(const UpdateManifest& manifest, const InstalledVersions& installed,
                                        const std::vector<std::string>& names) {
    std::vector<PlannedUpdate> updates;
    for (const auto& name : names) {
        const auto* release = manifest.component(name);
        if (release == nullptr) continue;
        const auto current = installed_of(installed, name);
        if (!current.empty() && !newer_than_installed(*release, current)) continue;
        PlannedUpdate update;
        update.release = *release;
        update.installed = current;
        update.prompt = UpdatePrompt::automatic;
        updates.push_back(std::move(update));
    }
    return updates;
}

std::vector<PlannedUpdate> without_declined(std::vector<PlannedUpdate> updates, const std::vector<std::string>& declined) {
    std::erase_if(updates, [&](const PlannedUpdate& update) {
        return update.prompt != UpdatePrompt::mandatory &&
               std::ranges::find(declined, update.release.name) != declined.end();
    });
    return updates;
}

bool blocks_play(const PlannedUpdate& update) noexcept {
    return update.prompt == UpdatePrompt::mandatory && update.release.name != component_server;
}

const char* to_string(HostingState state) noexcept {
    switch (state) {
    case HostingState::ready: return "ready";
    case HostingState::not_installed: return "not_installed";
    case HostingState::update_required: return "update_required";
    }
    return "unknown";
}

HostingStatus hosting_status(const UpdateManifest* manifest, const InstalledVersions& installed,
                             std::optional<std::uint32_t> client_protocol, std::optional<std::uint32_t> server_protocol) {
    HostingStatus status;
    status.installed_server = installed_of(installed, component_server);
    const auto installed_client = installed_of(installed, component_client);
    const ComponentRelease* offered = manifest != nullptr ? manifest->component(component_server) : nullptr;
    const ComponentRelease* client = manifest != nullptr ? manifest->component(component_client) : nullptr;
    const bool client_is_manifest = client != nullptr && client->version == installed_client;
    if (!client_protocol.has_value() && client_is_manifest) client_protocol = client->protocol;
    if (!server_protocol.has_value() && offered != nullptr && offered->version == status.installed_server) {
        server_protocol = offered->protocol;
    }
    // The minimum is a property of this client build; only trust it when the
    // manifest entry describes the installed client.
    const std::string hosting_rule = client_is_manifest ? client->hosting_server : std::string{};

    const auto compatible = [&](const ComponentRelease& release) {
        if (client_protocol.has_value() && release.protocol.has_value() && *release.protocol != *client_protocol) {
            return false;
        }
        return hosting_rule.empty() || satisfies_constraint(release.version, hosting_rule);
    };
    if (offered != nullptr) {
        status.available_server = offered->version;
        status.download_size = offered->size;
    }

    if (status.installed_server.empty()) {
        status.state = HostingState::not_installed;
        status.update_available = offered != nullptr && compatible(*offered);
        status.reason = "the BattleSpades server for hosting is not installed";
        return status;
    }
    std::string problem;
    if (client_protocol.has_value() && server_protocol.has_value() && *client_protocol != *server_protocol) {
        problem = "the installed server " + status.installed_server + " speaks protocol " +
                  std::to_string(*server_protocol) + ", this client " + std::to_string(*client_protocol);
    } else if (!hosting_rule.empty() && !satisfies_constraint(status.installed_server, hosting_rule)) {
        problem = "hosting needs server " + hosting_rule + " (installed " + status.installed_server + ")";
    }
    const bool newer = offered != nullptr && is_newer_version(offered->version, status.installed_server);
    if (problem.empty()) {
        status.state = HostingState::ready;
        status.update_available = newer && compatible(*offered);
        return status;
    }
    status.state = HostingState::update_required;
    status.reason = problem;
    status.update_available = newer && compatible(*offered);
    return status;
}

std::string hosting_status_json(const HostingStatus& status) {
    nlohmann::json json;
    json["schema"] = 1;
    json["state"] = to_string(status.state);
    json["installed_server"] = status.installed_server;
    json["available_server"] = status.available_server;
    json["download_size"] = status.download_size;
    json["update_available"] = status.update_available;
    json["reason"] = status.reason;
    return json.dump(2);
}

MirrorResult try_mirrors(const std::vector<std::string>& urls,
                         const std::function<MirrorOutcome(const std::string&, std::string&)>& attempt) {
    MirrorResult result;
    for (std::size_t index = 0; index < urls.size(); ++index) {
        std::string error;
        const auto outcome = attempt(urls[index], error);
        if (outcome == MirrorOutcome::success) {
            result.outcome = MirrorOutcome::success;
            result.used = index;
            return result;
        }
        if (outcome == MirrorOutcome::cancelled) {
            result.outcome = MirrorOutcome::cancelled;
            return result;
        }
        result.errors.push_back(urls[index] + ": " + (error.empty() ? "failed" : error));
    }
    result.outcome = MirrorOutcome::failed;
    return result;
}

bool verify_package_file(const std::filesystem::path& file, std::uint64_t expected_size,
                         std::string_view expected_sha256, std::string& error) {
    std::error_code code;
    if (!std::filesystem::is_regular_file(file, code) || code) {
        error = "the downloaded file is missing";
        return false;
    }
    const auto size = std::filesystem::file_size(file, code);
    if (code || size != expected_size) {
        error = "size " + std::to_string(code ? 0U : size) + " instead of " + std::to_string(expected_size);
        return false;
    }
    std::string hash_error;
    const auto digest = sha256_hex_file(file, hash_error);
    if (!digest.has_value()) {
        error = hash_error;
        return false;
    }
    if (!same_sha256(*digest, expected_sha256)) {
        error = "SHA-256 mismatch (" + *digest + ")";
        return false;
    }
    error.clear();
    return true;
}

std::optional<std::uint64_t> prepare_package_partial(const std::filesystem::path& partial,
                                                    std::uint64_t expected_size,
                                                    std::string_view expected_sha256,
                                                    std::string& error) {
    namespace fs = std::filesystem;
    auto identity = partial;
    identity += ".identity";
    const auto wanted = std::to_string(expected_size) + "\n" + std::string{expected_sha256} + "\n";
    std::error_code code;
    std::uint64_t size{};
    if (fs::is_regular_file(partial, code)) {
        size = fs::file_size(partial, code);
        if (code) {
            error = "cannot inspect partial download: " + code.message();
            return std::nullopt;
        }
        // A previous process may have finished receiving just before it was
        // stopped. Verify locally, without asking a mirror for an empty range.
        if (size == expected_size && verify_package_file(partial, expected_size, expected_sha256, error)) {
            return size;
        }
    }
    std::string ignored;
    const auto saved = read_text_file(identity, ignored);
    if (size >= expected_size || (saved.has_value() && *saved != wanted)) {
        fs::remove(partial, code);
        if (code) {
            error = "cannot discard stale partial download: " + code.message();
            return std::nullopt;
        }
        size = 0U;
    }
    if ((!saved.has_value() || *saved != wanted) && !write_file_atomic(identity, wanted, error)) return std::nullopt;
    error.clear();
    return size;
}

} // namespace battlespades::updater
