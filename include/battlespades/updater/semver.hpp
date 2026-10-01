#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace battlespades::updater {

/**
 * A Semantic Versioning 2.0 version.
 *
 * Release labels look like "0.2.0-beta.1" (AOS_RELEASE_VERSION) and GitHub
 * tags like "v0.2.0-beta.1". Precedence follows semver.org section 11:
 * a pre-release sorts below its release, numeric identifiers compare
 * numerically, and build metadata ("+abc") never affects ordering.
 */
struct Version {
    std::uint64_t major{};
    std::uint64_t minor{};
    std::uint64_t patch{};
    std::vector<std::string> prerelease;
    std::string build;
};

/// Accepts an optional leading 'v'/'V' and a missing minor/patch ("1.2").
[[nodiscard]] std::optional<Version> parse_version(std::string_view text);

/// Negative, zero or positive, exactly like strcmp, by semver precedence.
[[nodiscard]] int compare_versions(const Version& left, const Version& right) noexcept;

[[nodiscard]] std::string to_string(const Version& version);

/// True only when both parse and `candidate` has strictly higher precedence.
[[nodiscard]] bool is_newer_version(std::string_view candidate, std::string_view installed);

} // namespace battlespades::updater
