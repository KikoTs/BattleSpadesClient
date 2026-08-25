#include "battlespades/frontend/loading_screen.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace battlespades::frontend {
namespace {

struct NameAsset final {
    std::string_view name;
    std::string_view asset;
};

constexpr std::array map_images{
    NameAsset{"Alcatraz", "loading_mapimage_alcatraz"},
    NameAsset{"Ancient Egypt", "loading_mapimage_ancientegypt"},
    NameAsset{"Arctic Base", "loading_mapimage_arcticbase"},
    NameAsset{"Atlantis", "loading_mapimage_atlantis"},
    NameAsset{"Block Ness", "loading_mapimage_blockness"},
    NameAsset{"Bran Castle", "loading_mapimage_brancastle"},
    NameAsset{"Castle Wars", "loading_mapimage_castlewars"},
    NameAsset{"City Of Chicago", "loading_mapimage_cityofchicago"},
    NameAsset{"City of Chicago", "loading_mapimage_cityofchicago"},
    NameAsset{"Classic", "loading_mapimage_classic"},
    NameAsset{"Crossroads", "loading_mapimage_crossroads"},
    NameAsset{"Double Dragon", "loading_mapimage_doubledragon"},
    NameAsset{"Dragon Island", "loading_mapimage_dragonisland"},
    NameAsset{"Frontier", "loading_mapimage_frontier"},
    NameAsset{"Great Wall", "loading_mapimage_greatwall"},
    NameAsset{"Hiesville", "loading_mapimage_hiesville"},
    NameAsset{"Ice Train", "loading_mapimage_icetrain"},
    NameAsset{"Invasion", "loading_mapimage_invasion"},
    NameAsset{"London", "loading_mapimage_london"},
    NameAsset{"Lunar Base", "loading_mapimage_lunarbase"},
    NameAsset{"Mall Of War", "loading_mapimage_mallowar"},
    NameAsset{"Mayan Jungle", "loading_mapimage_mayanjungle"},
    // Retail intentionally aliases this legacy spelling to Mayan Jungle art.
    NameAsset{"Mount Rushmoore", "loading_mapimage_mayanjungle"},
    NameAsset{"Orc Fortress", "loading_mapimage_orcfortress"},
    NameAsset{"Pool Table", "loading_mapimage_pooltable"},
    NameAsset{"Spooky Mansion", "loading_mapimage_spookymansion"},
    NameAsset{"The Colosseum", "loading_mapimage_thecolosseum"},
    NameAsset{"Tokyo Neon", "loading_mapimage_tokyoneon"},
    NameAsset{"To The Bridge", "loading_mapimage_tothebridge"},
    NameAsset{"Trenches", "loading_mapimage_trenches"},
    NameAsset{"Winter Valley", "loading_mapimage_wintervalley"},
    NameAsset{"WW1", "loading_mapimage_ww1"},
    NameAsset{"Training", "loading_mapimage_tut"},
};

[[nodiscard]] std::string_view
infographic_name(std::string_view mode, bool classic, std::string_view skin) noexcept {
    const auto is = [&](std::string_view short_name, std::string_view title_name) {
        return mode == short_name || mode == title_name;
    };
    if (is("VIP", "VIP_MODE_TITLE") && skin == "mafia") {
        return "infographic_mafia_vip";
    }
    if ((is("TC", "TC_TITLE") || mode == "TERRITORY_CONTROL") && skin == "mafia") {
        return "infographic_mafia_tc";
    }
    if (is("CTF", "CTF_TITLE") && classic) {
        return "infographic_classic_ctf";
    }
    if (is("DEM", "DEMOLITION_TITLE")) {
        return "infographic_dem";
    }
    if (is("ZOM", "ZOMBIE_MODE_TITLE")) {
        return "infographic_zom";
    }
    if (is("MH", "MULTIHILL_TITLE")) {
        return "infographic_mh";
    }
    if (is("OCC", "OCCUPATION_MODE_TITLE")) {
        return "infographic_oc";
    }
    if (is("DIA", "DIAMOND_MINE_TITLE")) {
        return "infographic_dia";
    }
    if (is("VIP", "VIP_MODE_TITLE")) {
        return "infographic_vip";
    }
    if (is("TC", "TC_TITLE") || mode == "TERRITORY_CONTROL") {
        return "infographic_tc";
    }
    if (is("CTF", "CTF_TITLE")) {
        return "infographic_ctf";
    }
    if (is("TUTORIAL", "TUTORIAL_MODE_TITLE")) {
        return "infographic_tut";
    }
    if (is("MAP_CREATOR", "UGC")) {
        return "infographic_ugc";
    }
    return "infographic_tdm";
}

[[nodiscard]] double clamp_progress(double value) noexcept {
    return std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : 0.0;
}

[[nodiscard]] std::string normalized_identity(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (std::isalnum(byte) != 0) {
            result.push_back(static_cast<char>(std::tolower(byte)));
        }
    }
    return result;
}

[[nodiscard]] std::string normalized_map_identity(std::string_view value) {
    if (const auto separator = value.find_last_of("/\\"); separator != std::string_view::npos) {
        value.remove_prefix(separator + 1U);
    }
    if (value.size() >= 4U) {
        const auto suffix = normalized_identity(value.substr(value.size() - 4U));
        if (suffix == "vxl") {
            value.remove_suffix(4U);
        }
    }
    return normalized_identity(value);
}

} // namespace

BootLoadingSnapshot boot_loading_snapshot(const assets::PreloadSnapshot& preload) noexcept {
    const auto progress = clamp_progress(preload.progress);
    const auto bullets = static_cast<std::size_t>(
        std::floor(progress * static_cast<double>(BootLoadingSnapshot::bullet_count)));
    return {std::min(bullets, BootLoadingSnapshot::bullet_count), progress, preload.state};
}

void MatchLoadingModel::begin(std::string expected_map,
                              std::string expected_mode,
                              bool classic,
                              std::string texture_skin) {
    state_ = MatchLoadingState::connecting;
    map_name_ = std::move(expected_map);
    mode_key_ = std::move(expected_mode);
    classic_ = classic;
    texture_skin_ = std::move(texture_skin);
    status_key_ = "CONNECTING_TO_SERVER";
    tabs_.assign(1U, LoadingTab::map);
    selected_tab_ = 0U;
    tab_cycle_interrupted_ = false;
    tab_timer_ = 0.0;
    map_progress_ = 0.0;
    sync_progress_ = 0.0;
    preload_ = {};
    last_observed_progress_ = 0.0;
    no_progress_remaining_ = no_progress_timeout_seconds;
}

void MatchLoadingModel::initial_info(std::string map_name,
                                     std::string mode_key,
                                     bool classic,
                                     std::string texture_skin,
                                     bool map_creator) {
    map_name_ = std::move(map_name);
    mode_key_ = std::move(mode_key);
    classic_ = classic;
    texture_skin_ = std::move(texture_skin);
    state_ = MatchLoadingState::checking_map;
    status_key_ = "CHECKING_MAP";
    rebuild_tabs(map_creator);
    observe_progress();
}

void MatchLoadingModel::receiving_packs() noexcept {
    state_ = MatchLoadingState::receiving_packs;
    status_key_ = "RECEIVING_SERVER_PACKS";
}

void MatchLoadingModel::checking_map() noexcept {
    state_ = MatchLoadingState::checking_map;
    status_key_ = "CHECKING_MAP";
}

void MatchLoadingModel::receiving_map() noexcept {
    state_ = MatchLoadingState::receiving_map;
    status_key_ = "RECEIVING_MAP";
}

void MatchLoadingModel::map_progress(double progress) noexcept {
    map_progress_ = std::max(map_progress_, clamp_progress(progress));
    observe_progress();
}

void MatchLoadingModel::syncing_map() noexcept {
    map_progress_ = 1.0;
    sync_progress_ = 1.0;
    state_ = MatchLoadingState::syncing_map;
    status_key_ = "SYNCING_MAP";
    observe_progress();
}

void MatchLoadingModel::begin_asset_preload() noexcept {
    map_progress_ = 1.0;
    sync_progress_ = 1.0;
    state_ = MatchLoadingState::preloading_assets;
    status_key_ = "INITIALISING_MAP";
    observe_progress();
}

void MatchLoadingModel::set_preload_snapshot(assets::PreloadSnapshot snapshot) noexcept {
    preload_ = snapshot;
    if (preload_.state == assets::PreloadBatchState::failed ||
        preload_.state == assets::PreloadBatchState::cancelled) {
        state_ = MatchLoadingState::failed;
        status_key_ = "ASSET_PRELOAD_FAILED";
    } else if (preload_.state == assets::PreloadBatchState::ready ||
               preload_.state == assets::PreloadBatchState::ready_with_warnings) {
        map_progress_ = 1.0;
        sync_progress_ = 1.0;
        state_ = MatchLoadingState::ready;
        status_key_ = "MAP_READY";
    } else if (state_ != MatchLoadingState::failed) {
        state_ = MatchLoadingState::preloading_assets;
        status_key_ = "INITIALISING_MAP";
    }
    observe_progress();
}

void MatchLoadingModel::fail(std::string status_key) {
    state_ = MatchLoadingState::failed;
    status_key_ = std::move(status_key);
}

void MatchLoadingModel::tick(double delta_seconds) noexcept {
    if (!std::isfinite(delta_seconds) || delta_seconds <= 0.0 ||
        state_ == MatchLoadingState::ready || state_ == MatchLoadingState::failed ||
        state_ == MatchLoadingState::timed_out) {
        return;
    }
    const auto progress = snapshot().overall_progress;
    if (progress > last_observed_progress_ + 1.0e-9) {
        last_observed_progress_ = progress;
        no_progress_remaining_ = no_progress_timeout_seconds;
    } else {
        no_progress_remaining_ = std::max(0.0, no_progress_remaining_ - delta_seconds);
        if (no_progress_remaining_ <= 0.0) {
            state_ = MatchLoadingState::timed_out;
            status_key_ = "ERROR_TIMEOUT";
            return;
        }
    }

    if (!tab_cycle_interrupted_ && tabs_.size() > 1U) {
        tab_timer_ += delta_seconds;
        while (tab_timer_ >= automatic_tab_interval_seconds) {
            tab_timer_ -= automatic_tab_interval_seconds;
            selected_tab_ = (selected_tab_ + 1U) % tabs_.size();
        }
    }
}

bool MatchLoadingModel::select_tab(std::size_t selected) noexcept {
    if (selected >= tabs_.size()) {
        return false;
    }
    selected_tab_ = selected;
    tab_cycle_interrupted_ = true;
    return true;
}

MatchLoadingSnapshot MatchLoadingModel::snapshot() const {
    const auto network = clamp_progress((map_progress_ + sync_progress_) / 3.0);
    const auto assets =
        state_ == MatchLoadingState::ready ? 1.0 : clamp_progress(preload_.progress);
    const auto overall = state_ == MatchLoadingState::ready
                             ? 1.0
                             : clamp_progress((map_progress_ + sync_progress_ + assets) / 3.0);
    return MatchLoadingSnapshot{
        state_,
        map_name_,
        mode_key_,
        status_key_,
        tabs_,
        selected_tab_,
        select_loading_textures(map_name_, mode_key_, classic_, texture_skin_),
        network,
        assets,
        overall,
        no_progress_remaining_,
        state_ == MatchLoadingState::ready};
}

void MatchLoadingModel::rebuild_tabs(bool map_creator) {
    tabs_.clear();
    tabs_.push_back(LoadingTab::map);
    if (map_name_ != "Training" && !map_name_.empty()) {
        tabs_.push_back(LoadingTab::mode);
        if (!map_creator && mode_key_ != "MAP_CREATOR") {
            tabs_.push_back(LoadingTab::scores);
        }
    }
    selected_tab_ = std::min(selected_tab_, tabs_.size() - 1U);
}

void MatchLoadingModel::observe_progress() noexcept {
    const auto progress = snapshot().overall_progress;
    if (progress > last_observed_progress_ + 1.0e-9) {
        last_observed_progress_ = progress;
        no_progress_remaining_ = no_progress_timeout_seconds;
    }
}

LoadingTextureSelection select_loading_textures(std::string_view map_name,
                                                std::string_view mode_key,
                                                bool classic,
                                                std::string_view texture_skin) {
    auto map_asset = std::string{"loading_mapimage_default"};
    // InitialInfo carries a server-authored display label, not a guaranteed
    // retail catalog spelling. BattleSpades commonly sends compact stems such
    // as `MayanJungle`, while the preserved loading table calls the same map
    // `Mayan Jungle`. Exact matching made a second connection keep presenting
    // the fallback/previous-looking image even though the new VXL was already
    // authoritative. Use the same harmless case/spacing/punctuation
    // normalization as the server browser and official atmosphere resolver.
    const auto identity = normalized_map_identity(map_name);
    const auto found = std::ranges::find_if(map_images, [&identity](const NameAsset& row) {
        return normalized_map_identity(row.name) == identity;
    });
    if (found != map_images.end() && !identity.empty()) {
        map_asset = found->asset;
    }
    return LoadingTextureSelection{
        "png/ui/game_loading/map_images/" + map_asset + ".png",
        "png/ui/game_loading/mode_infographics/" +
            std::string{infographic_name(mode_key, classic, texture_skin)} + ".png",
    };
}

std::string resolve_server_map_preview_asset(std::string_view map_name) {
    constexpr std::array previews{
        NameAsset{"alcatraz", "Blocatraz.png"},
        NameAsset{"blocatraz", "Blocatraz.png"},
        NameAsset{"ancientegypt", "ancientegypt.png"},
        NameAsset{"arcticbase", "arcticbase.png"},
        NameAsset{"atlantis", "Atlantis.png"},
        NameAsset{"blockness", "blockness.png"},
        NameAsset{"brancastle", "brancastle.png"},
        NameAsset{"castlewars", "castlewars.png"},
        NameAsset{"classic", "Classic.png"},
        NameAsset{"thecolosseum", "colosseum.png"},
        NameAsset{"colosseum", "colosseum.png"},
        NameAsset{"crossroads", "crossroadcarnage.png"},
        NameAsset{"crossroadcarnage", "crossroadcarnage.png"},
        NameAsset{"doubledragon", "doubledragon.png"},
        NameAsset{"dragonisland", "dragonisland.png"},
        NameAsset{"frontier", "frontier.png"},
        NameAsset{"greatwall", "greatwall.png"},
        NameAsset{"hiesville", "hiesville.png"},
        NameAsset{"invasion", "Invasion.png"},
        NameAsset{"london", "london.png"},
        NameAsset{"lunarbase", "lunarbase.png"},
        NameAsset{"mayanjungle", "mayanjungle.png"},
        NameAsset{"spookymansion", "spookymansion.png"},
        NameAsset{"tokyoneon", "tokyoneon.png"},
        NameAsset{"tothebridge", "tothebridge.png"},
        NameAsset{"trenches", "Trenches.png"},
        NameAsset{"wintervalley", "wintervalley.png"},
        NameAsset{"ww1", "ww1.png"},
        NameAsset{"desertbaseplate", "DesertBaseplate.png"},
        NameAsset{"grasslandbaseplate", "GrasslandBaseplate.png"},
        NameAsset{"lunarbaseplate", "LunarBaseplate.png"},
        NameAsset{"mountainbaseplate", "MountainBaseplate.png"},
        NameAsset{"templebaseplate", "TempleBaseplate.png"},
        NameAsset{"urbanbaseplate", "UrbanBaseplate.png"},
    };
    const auto identity = normalized_map_identity(map_name);
    const auto found = std::ranges::find(previews, identity, &NameAsset::name);
    if (found == previews.end()) return {};
    return "png/ui/game_loading/map_previews/" + std::string{found->asset};
}

ServerModePresentation resolve_server_mode(std::string_view mode_code, bool classic) {
    auto code = normalized_identity(mode_code);
    if (code.empty()) code = "tdm";
    if (code == "cctf") classic = true;
    struct Mode final {
        std::string_view code;
        std::string_view title;
        std::string_view description;
    };
    constexpr std::array modes{
        // MODE_NORMAL has no stock retail title. BattleSpades uses that spare
        // wire row for its non-retail Arena extension, while A2S advertises
        // the explicit "arena" code.
        Mode{"nor", "ARENA", "ARENA_DESCRIPTION"},
        Mode{"arena", "ARENA", "ARENA_DESCRIPTION"},
        Mode{"zom", "ZOMBIE_MODE_TITLE", "ZOMBIE_MODE_DESCRIPTION"},
        Mode{"tdm", "TDM_TITLE", "TDM_DESCRIPTION"},
        Mode{"dia", "DIAMOND_MINE_TITLE", "DIAMOND_MINE_DESCRIPTION"},
        Mode{"oc", "OCCUPATION_MODE_TITLE", "OCCUPATION_MODE_DESCRIPTION"},
        Mode{"occ", "OCCUPATION_MODE_TITLE", "OCCUPATION_MODE_DESCRIPTION"},
        Mode{"dem", "DEMOLITION_TITLE", "DEMOLITION_DESCRIPTION"},
        Mode{"mh", "MULTIHILL_TITLE", "MULTIHILL_DESCRIPTION"},
        Mode{"vip", "VIP_MODE_TITLE", "VIP_MODE_DESCRIPTION"},
        Mode{"ctf", "CTF_TITLE", "CTF_DESCRIPTION"},
        Mode{"cctf", "CLASSIC_CTF_TITLE", "CTF_DESCRIPTION"},
        Mode{"tc", "TC_TITLE", "TC_DESCRIPTION"},
        Mode{"tut", "TUTORIAL_MODE_TITLE", "TUTORIAL_DESCRIPTION"},
        Mode{"ugc", "MAP_CREATOR", "UGC_DESCRIPTION"},
    };
    const auto found = std::ranges::find(modes, code, &Mode::code);
    if (found == modes.end()) {
        return ServerModePresentation{"tdm", "TDM_TITLE", "TDM_DESCRIPTION", false};
    }
    return ServerModePresentation{std::string{found->code}, std::string{found->title},
                                  std::string{found->description}, classic};
}

ServerModePresentation resolve_protocol168_mode(std::uint8_t mode_id, bool classic) {
    // shared.constants_gamemode.MODE_* is a dense 0..12 ordinal table. The
    // classic server deliberately sends MODE_CTF plus InitialInfo.classic,
    // so that feature bit wins over the otherwise unused MODE_CCTF ordinal.
    constexpr std::array<std::string_view, 13U> mode_codes{
        "nor", "dem", "zom", "mh", "oc", "dia", "tdm",
        "vip", "ctf", "tc", "tut", "cctf", "ugc"};
    if (mode_id >= mode_codes.size()) return resolve_server_mode("tdm", false);
    if (mode_id == 8U && classic) return resolve_server_mode("cctf", true);
    return resolve_server_mode(mode_codes[mode_id], classic);
}

} // namespace battlespades::frontend
