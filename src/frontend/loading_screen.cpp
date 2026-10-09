#include "battlespades/frontend/loading_screen.hpp"
#include "battlespades/core/utf8.hpp"
#include "battlespades/frontend/create_match_menu.hpp"
#include "battlespades/frontend/settings_menu.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
    if (mode == "CLASSIC_CTF_TITLE") return "infographic_classic_ctf";
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

/** shared.constants.MAP_NAME_TAGLINES: only these two stock maps carry one. */
[[nodiscard]] std::string map_tagline_key(std::string_view map_name) {
    if (map_name == "Hiesville") return "Hiesville_TagLine";
    if (map_name == "Trenches") return "Trenches_TagLine";
    return {};
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

[[nodiscard]] std::string canonical_mode(std::string_view value) {
    return resolve_server_mode(value).code;
}

[[nodiscard]] std::array<std::string, 3U> mode_captions(std::string_view mode) {
    const auto code = canonical_mode(mode);
    std::string prefix{"TDM"};
    if (code == "zom") prefix = "ZOM";
    else if (code == "ctf" || code == "cctf") prefix = "CTF";
    else if (code == "dem") prefix = "DEM";
    else if (code == "dia") prefix = "DIA";
    else if (code == "oc" || code == "occ") prefix = "OCC";
    else if (code == "vip") prefix = "VIP";
    else if (code == "tc") prefix = "TC";
    else if (code == "mh") prefix = "MH";
    else if (code == "ugc") prefix = "UGC";
    return {prefix + "_INFOGRAPHIC_TEXT1", prefix + "_INFOGRAPHIC_TEXT2",
            prefix + "_INFOGRAPHIC_TEXT3"};
}

/** Recovered from retail scoreTypesDisplay.py and constants_gamemode.py.
 * The protocol transmits the mode/friendly-fire filter, not score values. */
[[nodiscard]] std::vector<LoadingScoreRow> score_rows(
    std::string_view mode, const std::array<bool, 2U>& expanded, bool friendly_fire,
    std::optional<bool> classic_territory = std::nullopt) {
    if (classic_territory.has_value()) {
        std::vector<LoadingScoreRow> rows;
        if (!*classic_territory) {
            rows.push_back({"MODE_SPECIFIC_SCORE_TYPES", {}, 0U, expanded[0]});
            if (expanded[0]) rows.push_back({"Capture Flag", "+10", std::nullopt, false});
        }
        rows.push_back({"GENERIC_SCORE_TYPES", {}, 1U, expanded[1]});
        if (expanded[1]) rows.push_back({"Kill", "+1", std::nullopt, false});
        return rows;
    }
    struct Score final { std::string_view key; std::string_view value; };
    static constexpr std::array generic{
        Score{"Headshot", "+150"}, Score{"Melee", "+150"},
        Score{"Kill", "+100"}, Score{"Assist", "+50"},
        Score{"Death Revenge", "+50"}, Score{"Payback", "+50"},
        Score{"Reloading Kill", "+50"}, Score{"Defend", "+50"},
        Score{"Suicide", "-100"}};
    static constexpr std::array tdm{Score{"Distraction", "+50"}};
    static constexpr std::array zom{
        Score{"Survive", "+50 every 10 seconds"},
        Score{"Last Man Standing", "+150 every 5 seconds"},
        Score{"Kill Survivor", "+100"}, Score{"LMS Zombie Kill", "+50"}};
    static constexpr std::array tc{
        Score{"Controlled Territories", "+35 every 5 seconds"}, Score{"Contest hill", "+25 every 5 seconds"},
        Score{"Claim Territory", "+150"}, Score{"Control Territory", "+100"},
        Score{"Defend Territory", "+100"}, Score{"Assault Territory", "+50"}};
    static constexpr std::array dia{
        Score{"Carry diamond", "+50 every 5 seconds"}, Score{"Diamond Escort", "+10 every 5 seconds"},
        Score{"Capture", "+100"}, Score{"Uncover Diamond", "+10"},
        Score{"Diamond Distraction", "+100"}, Score{"Carrier Defend", "+100"},
        Score{"Diamond Defend", "+50"}, Score{"Diamond Assault", "+50"},
        Score{"Intercept Carrier", "+50"}};
    static constexpr std::array vip{
        Score{"VIP Survive", "+50 every 10 seconds"}, Score{"VIP Escort", "+10 every 5 seconds"},
        Score{"Kill Enemy VIP", "10% of VIP Score"}, Score{"VIP Distraction", "+50"},
        Score{"VIP Kill", "+100"}, Score{"VIP Defend", "+150"}};
    static constexpr std::array dem{
        Score{"Destroy Base", "+25 every 50 blocks"}, Score{"Repair Base", "+50 every 50 blocks"},
        Score{"Defend Base", "+100"}, Score{"Assault Base", "+50"}};
    static constexpr std::array ctf{
        Score{"Carry Flag", "+50 every 5 seconds"}, Score{"Flag Escort", "+10 every 5 seconds"},
        Score{"Capture Flag", "+10"}, Score{"First to Claim Flag", "+100"},
        Score{"Flag Distraction", "+100"}, Score{"Flag Defend", "+50"},
        Score{"Close to Flag", "+50"}, Score{"Flag Assault", "+50"},
        Score{"Flag Carrier Defend", "+100"}, Score{"Flag Intercept", "+50"}};
    static constexpr std::array occ{
        Score{"Occupy", "+50 every 5 seconds"}, Score{"Carry Bomb", "+50 every 5 seconds"},
        Score{"BOOM!", "+50"}, Score{"Bomb Distraction", "+100"},
        Score{"Carrier Defend", "+100"}, Score{"Bomb Defend", "+50"},
        Score{"Close to Bomb", "+100"}, Score{"Survive Blast", "+50"},
        Score{"Intercept Carrier", "+50"}};
    static constexpr std::array mh{
        Score{"Occupy", "+150 every 5 seconds"}, Score{"First to Hill", "+250"},
        Score{"Claim Hill", "+150"}, Score{"Control Hill", "+100"},
        Score{"Defend Hill", "+100"}, Score{"Assault Hill", "+50"}, Score{"Contest Hill", "+50"}};
    const auto code = canonical_mode(mode);
    std::span<const Score> specific;
    if (code == "tdm") specific = tdm;
    else if (code == "zom") specific = zom;
    else if (code == "tc") specific = tc;
    else if (code == "dia") specific = dia;
    else if (code == "vip") specific = vip;
    else if (code == "dem") specific = dem;
    else if (code == "ctf" || code == "cctf") specific = ctf;
    else if (code == "oc" || code == "occ") specific = occ;
    else if (code == "mh") specific = mh;
    std::vector<LoadingScoreRow> rows;
    const auto append = [&](std::span<const Score> values) {
        for (const auto& score : values) rows.push_back(
            {std::string{score.key}, std::string{score.value}, std::nullopt, false});
    };
    if (!specific.empty()) {
        rows.push_back({"MODE_SPECIFIC_SCORE_TYPES", {}, 0U, expanded[0]});
        if (expanded[0]) append(specific);
    }
    rows.push_back({"GENERIC_SCORE_TYPES", {}, 1U, expanded[1]});
    if (expanded[1]) {
        append(generic);
        if (friendly_fire) rows.push_back({"Team Kill", "-100", std::nullopt, false});
    }
    return rows;
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
    map_preview_override_.clear();
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
    world_build_progress_ = 0.0;
    preload_ = {};
    last_observed_progress_ = 0.0;
    no_progress_remaining_ = no_progress_timeout_seconds;
    waiting_for_player_ = false;
    score_expanded_ = {true, true};
    score_scroll_ = 0U;
    friendly_fire_ = false;
    classic_territory_scoring_.reset();
    custom_rules_.clear();
    infographic_captions_ = mode_captions(mode_key_);
    // The advertised/detected mode is useful immediately; InitialInfo replaces
    // it with the server's authoritative mode once the handshake completes.
    mode_title_key_ = mode_key_.empty() ? std::string{} : resolve_server_mode(mode_key_, classic_).title_key;
    if (!mode_key_.empty()) rebuild_tabs(canonical_mode(mode_key_) == "ugc");
}

void MatchLoadingModel::initial_info(std::string map_name,
                                     std::string mode_key,
                                     bool classic,
                                     std::string texture_skin,
                                     bool map_creator,
                                     bool friendly_fire) {
    map_name_ = std::move(map_name);
    mode_key_ = std::move(mode_key);
    classic_ = classic;
    texture_skin_ = std::move(texture_skin);
    friendly_fire_ = friendly_fire;
    classic_territory_scoring_.reset();
    custom_rules_.clear();
    infographic_captions_ = mode_captions(mode_key_);
    // loadingMenu: mode_name = packet.mode_name, 'CLASSIC_' + it when the
    // server is classic, then strings.get_by_id(mode_name).upper().
    {
        const auto resolved = resolve_server_mode(mode_key_, classic_);
        mode_title_key_ = resolved.title_key;
        // CLASSIC_CTF_TITLE is the only CLASSIC_ title in the string table.
        if (resolved.classic && mode_title_key_ == "CTF_TITLE") {
            mode_title_key_ = "CLASSIC_CTF_TITLE";
        }
    }
    score_expanded_ = {true, true};
    score_scroll_ = 0U;
    state_ = MatchLoadingState::checking_map;
    status_key_ = "CHECKING_MAP";
    rebuild_tabs(map_creator);
    observe_progress();
}

void MatchLoadingModel::set_status(std::string status) {
    status_key_ = std::move(status);
}

void MatchLoadingModel::set_classic_scoring(bool territory_mode) noexcept {
    classic_territory_scoring_ = territory_mode;
    score_scroll_ = 0;
}

void MatchLoadingModel::set_infographic_captions(std::array<std::string, 3U> captions) {
    for (std::size_t index{}; index < captions.size(); ++index) {
        // BattleSpades sends its short wire code; these three stock aliases
        // use different localization prefixes in the retail string catalog.
        for (const auto& [wire, catalog] : {std::pair{"OC_INFOGRAPHIC_", "OCC_INFOGRAPHIC_"},
                                         std::pair{"CCTF_INFOGRAPHIC_", "CTF_INFOGRAPHIC_"},
                                         std::pair{"NOR_INFOGRAPHIC_", "TDM_INFOGRAPHIC_"}}) {
            if (captions[index].starts_with(wire))
                captions[index].replace(0U, std::string_view{wire}.size(), catalog);
        }
        if (!captions[index].empty()) infographic_captions_[index] =
            core::utf8_code_point_prefix(captions[index], 256U);
    }
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

void MatchLoadingModel::loading_map() noexcept {
    state_ = MatchLoadingState::checking_map;
    status_key_ = "LOADING_MAP";
}

void MatchLoadingModel::map_sync_started() noexcept {
    // loadingMenu MapSyncStart: current_part += 1, SYNCING_MAP.
    map_progress_ = 1.0;
    state_ = MatchLoadingState::syncing_map;
    status_key_ = "SYNCING_MAP";
    observe_progress();
}

void MatchLoadingModel::map_sync_progress(double progress) noexcept {
    sync_progress_ = std::max(sync_progress_, clamp_progress(progress));
    observe_progress();
}

void MatchLoadingModel::map_sync_finished() noexcept {
    // loadingMenu MapSyncEnd: current_part += 1, INITIALISING_MAP.
    map_progress_ = 1.0;
    sync_progress_ = 1.0;
    state_ = MatchLoadingState::preloading_assets;
    status_key_ = "INITIALISING_MAP";
    observe_progress();
}

void MatchLoadingModel::world_build_progress(double progress) noexcept {
    world_build_progress_ = std::max(world_build_progress_, clamp_progress(progress));
    observe_progress();
}

double MatchLoadingModel::initialising_progress() const noexcept {
    return std::max(clamp_progress(preload_.progress), world_build_progress_);
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

void MatchLoadingModel::set_waiting_for_player(bool waiting) noexcept {
    if (waiting_for_player_ && !waiting) {
        no_progress_remaining_ = no_progress_timeout_seconds;
    }
    waiting_for_player_ = waiting;
}

void MatchLoadingModel::tick(double delta_seconds) noexcept {
    if (!std::isfinite(delta_seconds) || delta_seconds <= 0.0 ||
        state_ == MatchLoadingState::failed || state_ == MatchLoadingState::timed_out) {
        return;
    }
    const auto progress = clamp_progress((map_progress_ + sync_progress_ +
        initialising_progress()) / 3.0);
    if (state_ == MatchLoadingState::ready || waiting_for_player_) {
        // A finished load waits for START without a timeout, and the tabs
        // keep cycling until then (loadingMenu.update). A password prompt
        // waits on the server's own clock.
    } else if (progress > last_observed_progress_ + 1.0e-9) {
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
    score_scroll_ = 0U;
    return true;
}

bool MatchLoadingModel::scroll_scores(int rows) {
    if (tabs_[selected_tab_] != LoadingTab::scores) return false;
    const auto count = score_rows(mode_key_, score_expanded_, friendly_fire_, classic_territory_scoring_).size();
    const auto max_scroll = count > visible_score_rows ? count - visible_score_rows : 0U;
    const auto next = static_cast<std::size_t>(std::clamp(
        static_cast<long long>(score_scroll_) + rows, 0LL, static_cast<long long>(max_scroll)));
    tab_cycle_interrupted_ = true;
    const auto changed = next != score_scroll_;
    score_scroll_ = next;
    return changed;
}

bool MatchLoadingModel::set_score_scroll(double fraction) {
    if (tabs_[selected_tab_] != LoadingTab::scores || !std::isfinite(fraction)) return false;
    const auto count = score_rows(mode_key_, score_expanded_, friendly_fire_, classic_territory_scoring_).size();
    const auto maximum = count > visible_score_rows ? count - visible_score_rows : 0U;
    const auto next = static_cast<std::size_t>(std::round(clamp_progress(fraction) * static_cast<double>(maximum)));
    tab_cycle_interrupted_ = true;
    const auto changed = score_scroll_ != next;
    score_scroll_ = next;
    return changed;
}

MatchLoadingSnapshot MatchLoadingModel::snapshot() const {
    const auto network = clamp_progress((map_progress_ + sync_progress_) / 3.0);
    const auto assets =
        state_ == MatchLoadingState::ready ? 1.0 : initialising_progress();
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
        state_ == MatchLoadingState::ready,
        infographic_captions_, score_rows(mode_key_, score_expanded_, friendly_fire_, classic_territory_scoring_), score_scroll_,
        map_preview_override_.empty() ? resolve_server_map_preview_asset(map_name_)
                                      : map_preview_override_,
        map_tagline_key(map_name_), custom_rules_,
        mode_title_key_};
}

void MatchLoadingModel::set_custom_game_rules(
    std::vector<std::pair<std::string, std::string>> rules) {
    custom_rules_.clear();
    const auto code = canonical_mode(mode_key_);
    // `if packet.mode_key != A2445`: the tutorial never lists rules, and the
    // Map Creator loader has no rules panel either.
    if (rules.empty() || code == "tut" || code == "ugc" || map_name_ == "Training") return;
    struct Group final {
        std::string key;
        bool uppercase{};
        std::vector<LoadingCustomRuleRow> rows;
    };
    std::vector<Group> groups;
    for (auto& [rule, value] : rules) {
        // get_game_rule_catagory: the GAME_RULES_NAMES bucket owning this id.
        std::string_view category{"GENERAL"};
        for (const auto& definition : retail_create_match_rules()) {
            if (definition.rule_key == rule) {
                category = definition.category_key;
                break;
            }
        }
        // A2448 (MODE_MAP_TITLES) names the per-mode buckets by their title;
        // the shared buckets are upper-cased catalogue ids.
        const auto mode = resolve_server_mode(category);
        const bool mode_category = mode.code == category;
        auto key = mode_category ? mode.title_key : std::string{category};
        auto found = std::ranges::find(groups, key, &Group::key);
        if (found == groups.end()) {
            groups.push_back({key, !mode_category, {}});
            found = std::prev(groups.end());
        }
        // Values are drawn verbatim, as retail printed custom_rule[1].
        found->rows.push_back(
            {std::move(rule), std::string{literal_text_prefix} + value, false, false});
    }
    // setup_custom_game_rules walks sorted(category_list).
    std::ranges::sort(groups, {}, &Group::key);
    for (auto& group : groups) {
        custom_rules_.push_back({group.key, {}, true, group.uppercase});
        for (auto& row : group.rows) custom_rules_.push_back(std::move(row));
    }
}

void MatchLoadingModel::rebuild_tabs(bool map_creator) {
    tabs_.clear();
    tabs_.push_back(LoadingTab::map);
    if (map_name_ != "Training" && !map_name_.empty()) {
        tabs_.push_back(LoadingTab::mode);
        if (!map_creator && canonical_mode(mode_key_) != "ugc") {
            tabs_.push_back(LoadingTab::scores);
        }
    }
    selected_tab_ = std::min(selected_tab_, tabs_.size() - 1U);
}

void MatchLoadingModel::observe_progress() noexcept {
    const auto progress = state_ == MatchLoadingState::ready ? 1.0 :
        clamp_progress((map_progress_ + sync_progress_ + initialising_progress()) / 3.0);
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
    const auto resolved_mode = resolve_server_mode(mode_key, classic);
    return LoadingTextureSelection{
        "png/ui/game_loading/map_images/" + map_asset + ".png",
        "png/ui/game_loading/mode_infographics/" +
            std::string{infographic_name(resolved_mode.title_key, resolved_mode.classic, texture_skin)} + ".png",
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
        // images.reset_map_previews: "City Of Chicago" -> midtownmassacre.
        NameAsset{"cityofchicago", "midtownmassacre.png"},
        NameAsset{"midtownmassacre", "midtownmassacre.png"},
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
        NameAsset{"desert", "DesertBaseplate.png"},
        NameAsset{"grassland", "GrasslandBaseplate.png"},
        NameAsset{"lunar", "LunarBaseplate.png"},
        NameAsset{"mountain", "MountainBaseplate.png"},
        NameAsset{"temple", "TempleBaseplate.png"},
        NameAsset{"urban", "UrbanBaseplate.png"},
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
    if (code == "territorycontrol") code = "tc";
    if (code == "tutorial") code = "tut";
    if (code == "zombie") code = "zom";
    if (code == "classic075title" || code == "classic076title") {
        return {"ctf", code == "classic075title" ? "CLASSIC_075_TITLE" : "CLASSIC_076_TITLE",
                "CTF_DESCRIPTION", true};
    }
    if (code == "cctf" || code == "classicctftitle") classic = true;
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
    // Exact advertised codes win over title aliases. NOR shares Arena's title,
    // but must not intercept the explicit "arena" code before its own row.
    auto found = std::ranges::find(modes, code, &Mode::code);
    if (found == modes.end()) {
        found = std::ranges::find_if(modes, [&code](const Mode& mode) {
            return normalized_identity(mode.title) == code;
        });
    }
    if (found == modes.end()) {
        return ServerModePresentation{"tdm", "TDM_TITLE", "TDM_DESCRIPTION", false};
    }
    return ServerModePresentation{std::string{found->code}, std::string{found->title},
                                  std::string{found->description}, classic};
}

ServerModePresentation resolve_protocol168_mode(std::uint8_t mode_id, bool classic, network::GameProtocol protocol) {
    if (network::is_classic_protocol(protocol)) {
        return {mode_id == 9 ? "tc" : "ctf",
                protocol == network::GameProtocol::classic075 ? "CLASSIC_075_TITLE" : "CLASSIC_076_TITLE",
                mode_id == 9 ? "TC_DESCRIPTION" : "CTF_DESCRIPTION", true};
    }
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
