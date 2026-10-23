#pragma once

#include <array>
#include <string_view>

namespace battlespades::settings {
struct HostedMode final {
    std::string_view code;
    std::string_view title;
    // Base mode for Hardcore; arena stem for community modes.
    std::string_view source;
};
inline constexpr std::array<HostedMode, 10> hardcore_hosted_modes{{
    {"hc_ctf", "Hardcore CTF", "ctf"},
    {"hc_cctf", "Hardcore Classic CTF", "cctf"},
    {"hc_dem", "Hardcore Demolition", "dem"},
    {"hc_dia", "Hardcore Diamond Mine", "dia"},
    {"hc_mh", "Hardcore Multi-Hill", "mh"},
    {"hc_oc", "Hardcore Occupation", "oc"},
    {"hc_tdm", "Hardcore Team Deathmatch", "tdm"},
    {"hc_tc", "Hardcore Territory Control", "tc"},
    {"hc_vip", "Hardcore VIP", "vip"},
    {"hc_zom", "Hardcore Zombie", "zom"},
}};
// Server's curated SPECS playlist, excluding retired prototypes.
inline constexpr std::array<HostedMode, 13> community_hosted_modes{{
    {"block_hunt", "Prop Hunt", "LanternQuarter"},
    {"crumbling_run", "Crumbling Run", "FoundryRun"},
    {"last_exit", "Extraction", "DuskwaterDocks"},
    {"night_shift", "Survival", "BlackPineOutpost"},
    {"bridge_chicken", "Bridge Race", "RelayViaduct"},
    {"towerfall", "Tower Wars", "TwinBellFoundry"},
    {"floor_tax", "Spleef", "Glassworks"},
    {"hot_potato", "Hot Potato", "FuseCourtyard"},
    {"one_in_the_spade", "One in the Chamber", "TheKiln"},
    {"borrowed_arsenal", "Gun Game", "ArsenalArcade"},
    {"kingmaker", "King of the Hill", "CrownGardens"},
    {"red_light_raid", "Red Light, Green Light", "SignalSprint"},
    {"spadeball", "Spadeball", "SpadeStadium"},
}};
[[nodiscard]] constexpr bool is_community_mode(std::string_view code) noexcept {
    for (const auto& mode : community_hosted_modes) if (mode.code == code) return true;
    return false;
}
[[nodiscard]] constexpr bool is_hardcore_mode(std::string_view code) noexcept {
    for (const auto& mode : hardcore_hosted_modes) if (mode.code == code) return true;
    return false;
}
[[nodiscard]] constexpr std::string_view hosted_base_mode(std::string_view code) noexcept {
    for (const auto& mode : hardcore_hosted_modes) if (mode.code == code) return mode.source;
    return code;
}
} // namespace battlespades::settings
