#pragma once
#include "battlespades/network/game_protocol.hpp"

#include "battlespades/assets/preload_service.hpp"

#include <cstddef>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace battlespades::frontend {

enum class LoadingTab : std::uint8_t {
    map,
    mode,
    scores,
};

enum class MatchLoadingState : std::uint8_t {
    connecting,
    checking_map,
    receiving_packs,
    receiving_map,
    syncing_map,
    preloading_assets,
    ready,
    timed_out,
    failed,
};

struct LoadingTextureSelection final {
    std::string map_image_asset;
    std::string infographic_asset;
};

struct LoadingScoreRow final {
    std::string label_key;
    std::string value;
    /** Category headings retain an index so drawing and input share one row list. */
    std::optional<std::size_t> section;
    bool expanded{};
};

/**
 * One row of loadingMenu's CUSTOM GAME RULES list: a category heading, or a
 * rule name (240 px column) and its value (60 px column).
 */
struct LoadingCustomRuleRow final {
    std::string label_key;
    std::string value;
    bool category{};
    /** Mode categories keep their title's case; the others are upper-cased. */
    bool uppercase{};
};

struct MatchLoadingSnapshot final {
    MatchLoadingState state{MatchLoadingState::connecting};
    std::string map_name;
    std::string mode_key;
    std::string status_key{"CONNECTING_TO_SERVER"};
    std::vector<LoadingTab> tabs{LoadingTab::map};
    std::size_t selected_tab{};
    LoadingTextureSelection textures;
    double network_progress{};
    double asset_progress{};
    double overall_progress{};
    double no_progress_seconds_remaining{30.0};
    bool start_enabled{};
    std::array<std::string, 3U> infographic_captions;
    std::vector<LoadingScoreRow> score_rows;
    std::size_t score_scroll{};
    /** map_previews[map][2], drawn 238x238 inside loading_map_frame. */
    std::string map_preview_asset;
    /** MAP_NAME_TAGLINES id; non-empty also selects the smaller title font. */
    std::string map_tagline_key;
    std::vector<LoadingCustomRuleRow> custom_rules;
    /**
     * loadingMenu.mode_text: strings.get_by_id(InitialInfo.mode_name, with a
     * CLASSIC_ prefix for classic servers).upper(), drawn over the MODE
     * infographic. Empty until InitialInfo named the mode.
     */
    std::string mode_title_key;
};

/** Retail boot splash projection: 36 bullets backed by real preload progress. */
struct BootLoadingSnapshot final {
    static constexpr std::size_t bullet_count{36U};
    std::size_t filled_bullets{};
    double progress{};
    assets::PreloadBatchState state{assets::PreloadBatchState::idle};
};

[[nodiscard]] BootLoadingSnapshot
boot_loading_snapshot(const assets::PreloadSnapshot& preload) noexcept;

/**
 * Packet-driven match loading state, including the retail 3-second tab cycle
 * and 30-second no-progress timeout.
 */
class MatchLoadingModel final {
public:
    static constexpr double no_progress_timeout_seconds{30.0};
    static constexpr double automatic_tab_interval_seconds{3.0};
    static constexpr std::size_t visible_score_rows{10U};

    void begin(std::string expected_map = {},
               std::string expected_mode = {},
               bool classic = false,
               std::string texture_skin = {});
    void initial_info(std::string map_name,
                      std::string mode_key,
                      bool classic,
                      std::string texture_skin,
                      bool map_creator = false,
                      bool friendly_fire = false);
    /** Explain real host/authentication stages without changing readiness. */
    void set_status(std::string status);
    void set_infographic_captions(std::array<std::string, 3U> captions);
    /** Original-protocol scoring; Classic+ retains its authored score table. */
    void set_classic_scoring(bool territory_mode) noexcept;
    void receiving_packs() noexcept;
    void checking_map() noexcept;
    void receiving_map() noexcept;
    void map_progress(double progress) noexcept;
    /** MapDataValidation(60): LOADING_MAP. */
    void loading_map() noexcept;
    /** MapSyncStart(55): the second third of the bar, SYNCING_MAP. */
    void map_sync_started() noexcept;
    /** MapSyncChunk percent_complete / 100 within the second third. */
    void map_sync_progress(double progress) noexcept;
    /** MapSyncEnd(59): the last third, INITIALISING_MAP. */
    void map_sync_finished() noexcept;
    /** Local world build (mesh + upload) inside the last third. */
    void world_build_progress(double progress) noexcept;
    void syncing_map() noexcept;
    void begin_asset_preload() noexcept;
    void set_preload_snapshot(assets::PreloadSnapshot snapshot) noexcept;
    void fail(std::string status_key = "LOAD_FAILED");
    /**
     * InitialInfo.custom_game_rules as (rule id, value) pairs. Grouped by
     * GAME_RULES_NAMES category; ignored in the tutorial (loadingMenu.py).
     */
    void set_custom_game_rules(std::vector<std::pair<std::string, std::string>> rules);
    /**
     * LoadingMenu.draw_map_tab for a Map Creator project: the project's own
     * preview png (ugc_data.local_png_data) replaces the stock map preview.
     * Empty restores the stock art; begin() clears it.
     */
    void set_map_preview_override(std::string asset) { map_preview_override_ = std::move(asset); }
    /** loadingMenu: any mouse press or key sets tab_timer_interupted. */
    void interrupt_tab_cycle() noexcept { tab_cycle_interrupted_ = true; }
    /**
     * The server asked for its password and sends nothing until it has one:
     * the no-progress timeout waits with it. Releasing restarts the 30 s.
     */
    void set_waiting_for_player(bool waiting) noexcept;
    void tick(double delta_seconds) noexcept;
    [[nodiscard]] bool select_tab(std::size_t index) noexcept;
    [[nodiscard]] bool handle_score_click(double design_x, double design_y);
    [[nodiscard]] bool scroll_scores(int rows);
    [[nodiscard]] bool set_score_scroll(double fraction);
    [[nodiscard]] MatchLoadingSnapshot snapshot() const;

private:
    void rebuild_tabs(bool map_creator);
    void observe_progress() noexcept;
    [[nodiscard]] double initialising_progress() const noexcept;

    MatchLoadingState state_{MatchLoadingState::connecting};
    std::string map_name_;
    std::string map_preview_override_;
    std::string mode_key_;
    std::string mode_title_key_;
    std::string texture_skin_;
    std::string status_key_{"CONNECTING_TO_SERVER"};
    bool classic_{};
    std::vector<LoadingTab> tabs_{LoadingTab::map};
    std::size_t selected_tab_{};
    bool tab_cycle_interrupted_{};
    double tab_timer_{};
    double map_progress_{};
    double sync_progress_{};
    double world_build_progress_{};
    assets::PreloadSnapshot preload_{};
    double last_observed_progress_{};
    double no_progress_remaining_{no_progress_timeout_seconds};
    bool waiting_for_player_{};
    std::array<bool, 2U> score_expanded_{true, true};
    std::size_t score_scroll_{};
    bool friendly_fire_{};
    std::optional<bool> classic_territory_scoring_;
    std::array<std::string, 3U> infographic_captions_;
    std::vector<LoadingCustomRuleRow> custom_rules_;
};

[[nodiscard]] LoadingTextureSelection select_loading_textures(std::string_view map_name,
                                                              std::string_view mode_key,
                                                              bool classic,
                                                              std::string_view texture_skin);

/** Retail server-browser preview art, normalized for harmless map-name spacing/case drift. */
[[nodiscard]] std::string resolve_server_map_preview_asset(std::string_view map_name);

/** Retail localized title/description identities for one master-server mode code. */
struct ServerModePresentation final {
    std::string code;
    std::string title_key;
    std::string description_key;
    bool classic{};
};

[[nodiscard]] ServerModePresentation resolve_server_mode(std::string_view mode_code,
                                                         bool classic = false);

/** Resolve InitialInfo.mode_key using retail's MODE_* ordinal table. */
[[nodiscard]] ServerModePresentation resolve_protocol168_mode(std::uint8_t mode_id,
                                                              bool classic = false,
                                                              network::GameProtocol protocol = network::GameProtocol::retail168);

namespace loading_screen_assets {

inline constexpr std::string_view splash{"png/ui/ugc_splash.png"};
inline constexpr std::string_view boot_title{"png/ui/title_and_copyright.png"};
inline constexpr std::string_view boot_bullet{"png/ui/progress_bullet_large.png"};
inline constexpr std::string_view boot_bullet_dark{"png/ui/progress_bullet_dark_large.png"};
inline constexpr std::string_view match_frame{"png/ui/common_elements/frames/ui_frame_large.png"};
inline constexpr std::string_view loading_bar{"png/ui/game_loading/game_loading_bar_bg.png"};
inline constexpr std::string_view loading_tab_background{
    "png/ui/game_loading/game_loading_tab_bg.png"};
inline constexpr std::string_view loading_bullet{"png/ui/game_loading/loading_bar_bullet.png"};

} // namespace loading_screen_assets

} // namespace battlespades::frontend
