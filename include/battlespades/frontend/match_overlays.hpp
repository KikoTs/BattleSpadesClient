#pragma once

#include "battlespades/frontend/change_team_menu.hpp"
#include "battlespades/network/protocol168_players.hpp"
#include "battlespades/network/protocol168_runtime.hpp"
#include "battlespades/ui/draw_list.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace battlespades::frontend {

enum class ChatChannel : std::uint8_t {
    global = 0U,
    team = 1U,
};

/** One independently coloured label inside a retail ChatLine. */
struct ChatTextRun final {
    std::string text;
    ui::ColorRgba8 color{};
};

struct ChatFeedEntry final {
    /** Compatibility/plain-text form used for retention and diagnostics. */
    std::string text;
    ui::ColorRgba8 color{};
    double time_to_live_seconds{};
    /** Retail stores the sender prefix and message body as separate labels. */
    std::vector<ChatTextRun> runs;
    /** Russian, Polish and Turkish CreatePlayer rows select ChatLine.tuffy_font. */
    bool use_tuffy_font{};
};

/**
 * Retained retail chat state.
 *
 * Network packets and SDL text events are consumed on the main tick thread.
 * The model owns no transport: submit() returns a bounded message which the
 * frontend may send or discard if the connection has already closed.
 */
class GameChatModel final {
public:
    static constexpr std::size_t maximum_entries{50U};
    static constexpr std::size_t shown_lines{10U};
    /** Counted in characters so Cyrillic input is not cut to half the length. */
    static constexpr std::size_t maximum_input_code_points{90U};
    /** ChatMessage(49) wire ceiling in UTF-8 bytes. */
    static constexpr std::size_t maximum_input_bytes{200U};
    static constexpr double entry_lifetime_seconds{5.0};

    void add(std::string text, ui::ColorRgba8 color = {});
    void add_player_message(std::string sender, ui::ColorRgba8 sender_color,
                            std::string message, bool team_message,
                            std::uint8_t local_language = 0U);
    void tick(double elapsed_seconds) noexcept;

    void begin(ChatChannel channel);
    void cancel() noexcept;
    [[nodiscard]] bool append_text(std::string_view utf8);
    [[nodiscard]] bool erase_code_point() noexcept;
    [[nodiscard]] std::optional<std::pair<ChatChannel, std::string>> submit();

    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] ChatChannel channel() const noexcept { return channel_; }
    [[nodiscard]] std::string_view input() const noexcept { return input_; }
    [[nodiscard]] std::span<const ChatFeedEntry> entries() const noexcept {
        return entries_;
    }

private:
    std::vector<ChatFeedEntry> entries_;
    std::string input_;
    ChatChannel channel_{ChatChannel::global};
    bool active_{};
};

class GameChatPresentation final {
public:
    [[nodiscard]] ui::DrawList build(const GameChatModel& model,
                                     ui::PixelExtent window,
                                     std::function<double(std::string_view,
                                                          double,
                                                          std::string_view)> measure_text = {}) const;
};

struct VoteChoice final {
    /** Exact server token; never replace this with display_text on CAST. */
    std::string wire_name;
    std::string display_text;
    std::int32_t votes{};
};

/**
 * Source-backed, crash-safe equivalent of retail GenericVotingHUD.
 *
 * Packet 47 and local F1/F2/F3 input are consumed on the main tick thread.
 * The model deliberately reproduces retail's two independent timers: a
 * server-authored result hides after its requested lifetime, while
 * hide_after_vote delays list dismissal for five seconds. It owns no network
 * transport; cast() returns the untouched candidate token for the caller.
 *
 * The shipped HUD used ast.literal_eval on every label. This implementation
 * accepts only the bounded tuple/list/string/scalar grammar used on the wire,
 * formats localized arguments, retains at most three rows, and never executes
 * packet text.
 */
class GenericVotingModel final {
public:
    static constexpr double hide_after_cast_seconds{5.0};
    static constexpr double closed_result_seconds{6.0};

    void apply(const network::GenericVoteMessagePacket& packet);
    void tick(double elapsed_seconds) noexcept;
    void clear() noexcept;

    [[nodiscard]] std::optional<std::string_view>
    cast(std::size_t candidate_index);

    [[nodiscard]] bool visible() const noexcept { return visible_; }
    [[nodiscard]] bool can_vote() const noexcept { return can_vote_; }
    [[nodiscard]] bool allow_revote() const noexcept { return allow_revote_; }
    [[nodiscard]] std::string_view title() const noexcept { return title_; }
    [[nodiscard]] std::string_view description() const noexcept {
        return description_;
    }
    [[nodiscard]] std::span<const VoteChoice> choices() const noexcept {
        return choices_;
    }
    [[nodiscard]] std::optional<std::size_t> voted_index() const noexcept {
        return voted_index_;
    }
    [[nodiscard]] bool showing_result() const noexcept {
        return visible_ && !result_text_.empty();
    }
    [[nodiscard]] std::string_view result_text() const noexcept {
        return result_text_;
    }

    [[nodiscard]] static std::string
    decode_retail_literal(std::string_view encoded);

private:
    std::vector<VoteChoice> choices_;
    std::string title_;
    std::string description_;
    std::string result_text_;
    std::optional<std::size_t> voted_index_;
    std::optional<double> voted_message_seconds_;
    std::optional<double> hide_menu_seconds_;
    bool visible_{};
    bool can_vote_{};
    bool allow_revote_{};
    bool hide_after_vote_{};
    /** Only START opens a ballot; late UPDATE packets cannot reopen CLOSED. */
    bool accepting_updates_{};
};

class GenericVotingPresentation final {
public:
    [[nodiscard]] ui::DrawList build(const GenericVotingModel& model,
                                     ui::PixelExtent window,
                                     const std::array<std::string, 3U>& keys) const;
};

struct MatchAward final {
    std::uint8_t player_id{};
    std::int32_t stat_type{};
    /** Packet 67's authoritative ViewGameStats column (wire team 2/3). */
    std::uint8_t team_id{};
    /** Retail GameStat retains its player object beyond roster removal. */
    std::optional<std::string> player_name;
    std::uint8_t player_team{};
};

/**
 * Resolve a GameStats(67) award ordinal through retail GAME_STAT_TYPES.
 *
 * The packet carries an index into a dedicated 30-entry award table, not a
 * score-reason identifier. Unknown values fail closed to the same inert label
 * used by the presentation instead of indexing past the retail table.
 */
[[nodiscard]] std::string_view
retail_game_stat_award_label(std::int32_t stat_type) noexcept;

/** Resolve constants_gamemode.MODE_TITLE / InitialInfo.classic exactly. */
[[nodiscard]] std::string_view
retail_scoreboard_mode_title(std::uint8_t mode_type,
                             bool classic) noexcept;

/** Retail result sting selected from the local and winning wire teams. */
[[nodiscard]] std::optional<std::string_view> match_result_audio_stem(
    std::int32_t winner_team, std::uint8_t local_team) noexcept;

/** Resolve the winning wire team from the final server-owned score snapshot. */
[[nodiscard]] std::int32_t match_result_winner_from_scores(
    const ChangeTeamServerState& state) noexcept;

struct MatchRankUp final {
    std::int32_t score_reason{};
    std::int64_t old_score{};
    std::int64_t new_score{};
};

/** One source-backed frame of retail ViewGameStats.draw_rank_ups. */
struct MatchRankUpFrame final {
    MatchRankUp rank_up{};
    double interpolated_score{};
    double next_level_min{};
    double next_level_max{};
    std::uint32_t level{1U};
    double opacity{};
    double level_up_timer{};
    bool level_up{};
    bool last_rank_up{};
};

/**
 * End-of-round retained state assembled from GameStats(67), RankUps(66), and
 * the authored ShowTextMessage(73) selector, then activated by
 * ShowGameStats(53). MapEnded(52) freezes/preserves it; the retiring scene
 * clears it only when the next loader transition actually takes ownership.
 */
class MatchResultsModel final {
public:
    static constexpr double rank_up_initial_delay_seconds{5.5};
    static constexpr double rank_up_before_delay_seconds{0.75};
    static constexpr double rank_up_interpolation_seconds{1.5};
    static constexpr double rank_up_continue_delay_seconds{2.0};
    static constexpr double rank_up_total_seconds{
        rank_up_before_delay_seconds + rank_up_interpolation_seconds +
        rank_up_continue_delay_seconds};
    static constexpr double rank_up_fade_seconds{0.5};

    void apply(const network::GameStatsPacket& packet);
    void apply(const network::GameStatsPacket& packet,
               const network::Protocol168Roster& roster);
    void apply(const network::RankUpsPacket& packet);
    void apply(const network::ShowTextMessagePacket& packet) noexcept;

    /**
     * Observe one authoritative team-score update while an unshown result
     * batch is retained.
     *
     * BattleSpades deliberately omits ShowGameStats/MapEnded when it restarts
     * the current map in-place.  Its round reset is instead marked by the two
     * SetScore(TEAM) zero packets.  Once both wire teams have reset, discard
     * the hidden GameStats batch so a later real map rollover cannot display
     * stale awards.  A visible retail ViewGameStats is never affected.
     */
    void observe_team_score(std::uint8_t wire_team,
                            std::int32_t score) noexcept;
    /**
     * Preserve or activate ViewGameStats at the terminal map boundary.
     *
     * Retail keeps the accumulated packet-67/66/73 state alive when
     * MapEnded(52) freezes the current GameScene.  The owning scene teardown,
     * not packet 52 itself, is responsible for calling clear().  This also
     * lets a client that receives only StateData.has_map_ended render the
     * terminal screen while it waits for the next scene handshake.
     */
    void on_map_ended(std::optional<std::int32_t> score_winner_team =
                          std::nullopt) noexcept;
    /**
     * Activate ViewGameStats after its packet-67 records have arrived.
     *
     * `score_winner_team` is derived from authoritative SetScore/StateData.
     * An explicit zero is an authoritative draw. Only an absent snapshot may
     * use a single packet-67 team as the old BattleSpades compatibility winner;
     * retail packet 67 normally selects an award list, not the winner.
     */
    void show(std::optional<std::int32_t> score_winner_team =
                  std::nullopt) noexcept;
    void tick(double elapsed_seconds) noexcept;
    void clear() noexcept;

    [[nodiscard]] bool visible() const noexcept { return visible_; }
    [[nodiscard]] std::span<const MatchAward> awards() const noexcept {
        return awards_;
    }
    [[nodiscard]] std::span<const MatchRankUp> rank_ups() const noexcept {
        return rank_ups_;
    }
    [[nodiscard]] std::int32_t winner_team() const noexcept {
        return winner_team_;
    }
    /** Empty team packets still establish authoritative two-column results. */
    [[nodiscard]] bool has_both_team_lists() const noexcept {
        return observed_stats_teams_ == 0x03U;
    }
    [[nodiscard]] std::optional<std::uint8_t> message_id() const noexcept {
        return message_id_;
    }
    [[nodiscard]] std::optional<MatchRankUpFrame>
    rank_up_frame() const noexcept;

private:
    std::vector<MatchAward> awards_;
    std::vector<MatchRankUp> rank_ups_;
    std::optional<MatchRankUp> current_rank_up_;
    double rank_up_timer_seconds_{
        rank_up_total_seconds - rank_up_initial_delay_seconds};
    double level_up_timer_seconds_{};
    std::uint32_t displayed_rank_level_{1U};
    std::optional<std::uint8_t> message_id_;
    std::int32_t winner_team_{};
    std::uint8_t observed_stats_teams_{};
    bool display_rank_ups_{};
    bool level_up_{};
    bool last_rank_up_{};
    bool visible_{};
    std::uint8_t unshown_score_reset_mask_{};
};

/**
 * Apply retail's terminal MapEnded overlay boundary.
 *
 * Packet 52 freezes the current GameScene; it does not tear its HUD models
 * down. ViewGameStats is activated/preserved, an unfinished chat edit is
 * cancelled, and retained chat lines plus GenericVotingHUD remain visible
 * until their own timers or scene teardown retire them.
 */
void apply_map_ended_overlay_boundary(MatchResultsModel& results,
                                      GameChatModel& chat,
                                      GenericVotingModel& vote,
                                      std::int32_t score_winner_team = 0) noexcept;

/**
 * Retail ViewGameStats' separately framed authored map-camera preview.
 *
 * The preview is available only when StateData supplied screenshot cameras;
 * the selected image follows GameScene.current_screenshot_camera.
 */
struct MatchScreenshotPreview final {
    std::string map_name;
    std::size_t camera_index{};
    bool visible{};
};

/** One frame of retail's authored five-second end-of-match camera pan. */
struct MatchResultCameraPose final {
    std::array<double, 3U> eye{};
    double yaw_degrees{};
    double pitch_degrees{};
    double roll_degrees{};
};

/**
 * Resolve GameScene.set_screenshot_camera without depending on the renderer.
 *
 * StateData stores each camera as a VXL position plus retail
 * `(pitch, yaw, roll)` degrees. Retail starts at that position, derives the
 * look vector with `pitch_yaw_to_direction_vector`, and linearly pans five
 * blocks backwards for SCREENSHOT_CAMERA_PAN_TIME (five seconds). Malformed,
 * non-finite, unpaired, and the revival server's historical all-zero
 * placeholder cameras fail closed so they cannot pull the live view to the
 * corner of the map.
 */
[[nodiscard]] std::optional<MatchResultCameraPose>
resolve_match_result_camera(
    std::span<const std::array<double, 3U>> points,
    std::span<const std::array<double, 3U>> rotations,
    std::size_t camera_index, double pan_elapsed_seconds) noexcept;

/**
 * Resolve retail's authored end-screen camera image without touching disk.
 *
 * The caller remains responsible for checking that the returned asset is
 * installed.  That distinction lets the presentation stay renderer-neutral
 * while the native frontend can fail soft for UGC maps that legitimately have
 * StateData camera points but no stock level screenshot.
 */
[[nodiscard]] std::optional<std::string>
retail_level_screenshot_asset(std::string map_name,
                              std::size_t camera_index);

class MatchResultsPresentation final {
public:
    [[nodiscard]] ui::DrawList
    build(const MatchResultsModel& model, const ChangeTeamServerState& state,
          std::string mode_title, const network::Protocol168Roster& roster,
          ui::PixelExtent window,
          MatchScreenshotPreview preview = {}) const;
};

} // namespace battlespades::frontend
