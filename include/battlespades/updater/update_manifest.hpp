#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace battlespades::updater {

inline constexpr std::string_view default_manifest_url = "https://www.aosplay.net/updates/stable.json";
/**
 * Copy of stable.json on the client repository's fixed "update-manifest"
 * release, tried when aosplay.net cannot be reached (for example where the
 * site's host is blocked). Every publish uploads the same file to both.
 */
inline constexpr std::string_view default_manifest_mirror_url =
    "https://github.com/KikoTs/BattleSpadesClient/releases/download/update-manifest/stable.json";
inline constexpr std::string_view component_client = "client";
inline constexpr std::string_view component_server = "server";
inline constexpr std::string_view component_assets = "assets";
inline constexpr std::string_view component_retail_assets = "retail_assets";

/**
 * One independently versioned part of an installation (stable.json schema 2).
 *
 *   client  BattleSpadesClient.exe, DLLs, launcher, our shaders/UI/localization
 *   server  the bundled BattleSpades dedicated server (install\server)
 *   assets  our own redistributable asset packs (install\assets\client)
 *   retail_assets  optional download of the original game files, hosted by
 *           Kiril himself; installed through BattleSpadesAssetInstaller into
 *           install\assets\original exactly like a folder import. Our build
 *           and publish scripts never package or reference retail files.
 *
 * Components roll out independently: client 0.2 with server 0.1 is fine.
 * Optional components that are not installed are offered, never forced.
 */
struct ComponentRelease {
    std::string name;
    std::string version;
    std::string package;                 ///< file name (also the download cache name)
    std::vector<std::string> urls;       ///< mirrors, tried in order
    std::uint64_t size{};
    std::string sha256;                  ///< lower-case hex
    std::string root;                    ///< folder inside the ZIP mapped onto `target`
    std::string target;                  ///< install-relative folder ("" = install root)
    std::vector<std::string> preserve;
    std::vector<std::string> mirror_directories;
    std::vector<std::string> remove;
    bool required{};                     ///< no "Play without updating"
    std::optional<std::uint32_t> protocol;
    /// Client only: version constraint the bundled server must meet for
    /// hosting (Create Match / Map Creator), e.g. ">=0.1.0-beta.3". Never
    /// blocks playing or joining remote servers.
    std::string hosting_server;
};

struct UpdateManifest {
    int schema{2};
    std::string product;
    std::string channel;
    std::string published;
    std::string notes_url;
    bool required{};                     ///< every update in this release is mandatory
    std::vector<ComponentRelease> components;

    [[nodiscard]] const ComponentRelease* component(std::string_view name) const noexcept;
};

[[nodiscard]] std::optional<UpdateManifest> parse_update_manifest(std::string_view json, std::string& error);

/// Default install-relative folder of a known component.
[[nodiscard]] std::string default_component_target(std::string_view component);

/**
 * Space- or comma-separated comparators, all of which must hold:
 * ">=1.2.0", ">1.2.0", "<=1.2.0", "<1.2.0", "=1.2.0", "==1.2.0" or a bare
 * version (exact). An empty constraint always holds.
 */
[[nodiscard]] bool satisfies_constraint(std::string_view version, std::string_view constraint);
[[nodiscard]] bool valid_constraint(std::string_view constraint);

/// https, or http only to 127.0.0.1/localhost (test mirrors).
[[nodiscard]] bool acceptable_url(std::string_view url) noexcept;

} // namespace battlespades::updater
