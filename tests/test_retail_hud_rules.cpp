#include "battlespades/frontend/retail_hud_rules.hpp"

#include "battlespades/frontend/game_hud.hpp"

#include <cmath>
#include <variant>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using namespace battlespades::frontend;

void expect(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error{std::string{message}};
}

void disconnect_reasons_follow_retail_strings() {
    expect(retail_disconnect_status_key(3U) == "SERVER_OUT_OF_DATE",
           "3 is ERROR_SERVER_OUT_OF_DATE, not a full server");
    expect(retail_disconnect_status_key(4U) == "SERVERFULL_ERROR", "4 is ERROR_FULL");
    expect(retail_disconnect_status_key(16U) == "A955", "16 is AFK: Kicked due to being idle");
    expect(retail_disconnect_status_key(22U) == "A961", "22 is Host has left");
    expect(retail_disconnect_status_key(9U) == "YOU_HAVE_BEEN_VAC_BANNED", "9 is VAC banned");
    expect(retail_disconnect_status_key(18U).empty(),
           "18 is the match-ended rollover, never a failure string");
    for (const auto reason : {23U, 24U, 25U}) {
        expect(retail_disconnect_status_key(reason) == "A958",
               "vote/conduct kicks use the kicked-for-reason template");
    }
    expect(retail_disconnect_kick_reason_key(23U) == "KICK_REASON_GRIEFING" &&
               retail_disconnect_kick_reason_key(24U) == "KICK_REASON_HACKING" &&
               retail_disconnect_kick_reason_key(25U) == "KICK_REASON_ABUSE" &&
               retail_disconnect_kick_reason_key(2U).empty(),
           "23/24/25 carry the retail kick reason");
    expect(format_retail_template("You have been kicked for {0} until the end of the current match.",
                                  {"Griefing"}) ==
               "You have been kicked for Griefing until the end of the current match.",
           "A958 formats its reason");
    expect(format_retail_template("{0} x {1} {0}", {"a", "b"}) == "a x b a",
           "every placeholder occurrence is replaced");
    expect(retail_disconnect_status_key(0U) == "CONNECTION_CLOSED" &&
               retail_disconnect_status_key(20U) == "CONNECTION_CLOSED" &&
               retail_disconnect_status_key(31U) == "CONNECTION_CLOSED" &&
               retail_disconnect_status_key(200U) == "CONNECTION_CLOSED",
           "set_big_text_message falls back to strings.CONNECTION_CLOSED");
    expect(retail_disconnect_status_key(11U) == "UNABLE_TO_CONNECT_TO_SERVER",
           "11 ERROR_TIMEOUT reads Unable to connect to server");
    expect(retail_disconnect_status_key(19U) == "A958" &&
               retail_disconnect_kick_reason_key(19U).empty(),
           "a temp ban shows the unformatted A958 template, as retail does");
    expect(retail_disconnect_status_key(21U) == "INVALID_SESSION_TICKET",
           "21 is INVALID_DEMO_CONTENT only in a Steam demo build");
}

void radar_uses_three_dimensional_250_block_range() {
    expect(retail_radar_detects(0.0, 0.0, 0.0, 249.9, 0.0, 0.0), "inside 250 is detected");
    expect(!retail_radar_detects(0.0, 0.0, 0.0, 250.0, 0.0, 0.0),
           "can_detect_player compares strictly below 250 squared");
    expect(!retail_radar_detects(0.0, 0.0, 0.0, 200.0, 0.0, 160.0),
           "sq_distance is three-dimensional");
    expect(!retail_radar_detects(0.0, 0.0, 0.0, NAN, 0.0, 0.0), "non-finite never detects");
}

void look_at_angles_match_session_convention() {
    const auto ahead = retail_look_at_angles(10.0, 10.0, 10.0, 0.0, 10.0, 10.0);
    expect(ahead && std::abs(ahead->yaw_degrees) < 1.0e-9 && std::abs(ahead->pitch_degrees) < 1.0e-9,
           "yaw 0 faces -x");
    const auto below = retail_look_at_angles(0.0, 0.0, 0.0, -10.0, 0.0, 10.0);
    expect(below && std::abs(below->pitch_degrees - 45.0) < 1.0e-9,
           "a lower target (larger z) looks down with positive pitch");
    const auto side = retail_look_at_angles(0.0, 0.0, 0.0, 0.0, -5.0, 0.0);
    expect(side && std::abs(side->yaw_degrees - 90.0) < 1.0e-9, "-y is yaw +90");
    expect(!retail_look_at_angles(1.0, 2.0, 3.0, 1.0, 2.0, 3.0).has_value(),
           "coincident points have no direction");
}

void score_and_award_keys_match_retail_tables() {
    expect(retail_score_reason_key(1U) == "TDM_Kill" && retail_score_reason_key(25U) == "TC_Contend" &&
               retail_score_reason_key(36U) == "Occ_Disposal" &&
               retail_score_reason_key(40U) == "DIA_Carry" &&
               retail_score_reason_key(49U) == "CTF_Capture" &&
               retail_score_reason_key(62U) == "ZOM_LastManZombieKill" &&
               retail_score_reason_key(69U) == "DEM_Destroy" &&
               retail_score_reason_key(76U) == "MH_Occupy" &&
               retail_score_reason_key(82U) == "MH_Contest",
           "SCORE_REASON_CODES ordinals must stay aligned");
    for (const auto total : {0U, 35U, 47U, 48U, 63U, 68U, 73U, 75U, 83U, 221U}) {
        expect(retail_score_reason_key(static_cast<std::uint8_t>(total)).empty(),
               "profile totals and the empty teabag code show no popup");
    }
    expect(retail_game_stat_award_key(0) == "MOST_DistanceTravelled" &&
               retail_game_stat_award_key(13) == "BIGGEST_RangedKill" &&
               retail_game_stat_award_key(29) == "MOST_Dominations" &&
               retail_game_stat_award_key(30).empty() && retail_game_stat_award_key(-1).empty(),
           "GAME_STAT_TYPES ordinals must stay aligned");
}

void kick_vote_denials_and_packet() {
    KickVoteContext context;
    context.local_is_spectator = true;
    expect(evaluate_kick_vote(context) == KickVoteDenial::spectator, "spectators cannot kick");
    context.local_is_spectator = false;
    context.local_team_players = 2U;
    expect(evaluate_kick_vote(context) == KickVoteDenial::not_enough_players,
           "fewer than 3 team players is denied");
    context.local_team_players = 3U;
    context.vote_in_progress = true;
    expect(evaluate_kick_vote(context) == KickVoteDenial::vote_in_progress, "one ballot at a time");
    context.vote_in_progress = false;
    context.seconds_since_last_start = 10.0;
    expect(evaluate_kick_vote(context) == KickVoteDenial::vote_too_soon, "60 s cooldown");
    expect(kick_vote_wait_seconds(10.0) == 50 && kick_vote_wait_seconds(59.9) == 1,
           "wait seconds round up");
    context.seconds_since_last_start = 61.0;
    expect(evaluate_kick_vote(context) == KickVoteDenial::none, "allowed after the cooldown");
    expect(retail_kick_denial_key(KickVoteDenial::vote_too_soon) ==
               "KICK_DENIED_REASON_VOTE_TOO_SOON",
           "denial strings are retail keys");

    const auto bytes = encode_initiate_kick(4U, 9U, KickVoteReason::hacking);
    expect(bytes[0] == std::byte{48U} && bytes[1] == std::byte{4U} && bytes[2] == std::byte{9U} &&
               bytes[3] == std::byte{1U},
           "InitiateKickMessage(48) is id, player, target, reason bytes");
}

void kick_vote_select_flow() {
    KickVoteSelectModel model;
    expect(!model.visible(), "closed by default");
    model.open({{3U, "alpha"}, {7U, "bravo"}});
    expect(model.stage() == KickVoteSelectModel::Stage::player && model.row_count() == 2U,
           "player stage lists candidates");
    model.move(-1);
    expect(model.highlighted() == 1U, "moving up wraps");
    expect(!model.confirm().has_value() && model.stage() == KickVoteSelectModel::Stage::reason &&
               model.chosen_target() == 7U,
           "choosing a player advances to the reason list");
    model.back();
    expect(model.stage() == KickVoteSelectModel::Stage::player, "escape backs out one step");
    static_cast<void>(model.choose(0U));
    const auto selection = model.choose(2U);
    expect(selection && selection->target_id == 3U && selection->reason == KickVoteReason::abuse &&
               !model.visible(),
           "choosing a reason finishes and closes");
    expect(!model.choose(0U).has_value(), "a closed model ignores input");
}

[[nodiscard]] bool draws_text(const battlespades::ui::DrawList& list, std::string_view needle) {
    for (const auto& command : list.commands()) {
        const auto* text = std::get_if<battlespades::ui::TextDrawCommand>(&command);
        if (text != nullptr && text->localization_key == needle) return true;
    }
    return false;
}

void hud_labels_resolve_through_retail_strings() {
    GameHudModel model;
    model.set_respawn_time(5U);
    model.add_score_award(100, 25U);
    // character.pyd posts DEATH_CLASS_CHANGE_HINT once through
    // hud.add_big_message(text, respawn_time - 2.0): it is big text, not a
    // separate dead-screen label.
    model.set_big_message("Press [COMMA] to change class", false, 3.0);
    GameHudPresentationContext context{{800, 600}, 1'000U, {}};
    const auto english = GameHudPresentation{}.build(model, context);
    expect(draws_text(english, "Respawning in 5") && draws_text(english, "Contest hill"),
           "without a localizer the HUD keeps the retail English, incl. TC_Contend");
    expect(draws_text(english, "Press [COMMA] to change class"),
           "the death class hint renders in the big-text lane");

    context.localize = [](std::string_view key) -> std::string {
        if (key == "RESPAWNING_IN") return "Wiederbelebung in {0}";
        if (key == "TC_Contend") return "Huegel beanspruchen";
        return std::string{key};
    };
    const auto german = GameHudPresentation{}.build(model, context);
    expect(draws_text(german, "Wiederbelebung in 5"), "RESPAWNING_IN formats its {0}");
    expect(draws_text(german, "Huegel beanspruchen"),
           "score reasons resolve through SCORE_REASON_CODES keys");

    model.set_respawn_time(0xFFU);
    GameHudModel vip_model;
    vip_model.set_respawn_time(0xFFU);
    vip_model.set_background_big_message("VIP_DEAD_CAM_INSTRUCTION");
    const auto vip = GameHudPresentation{}.build(vip_model, context);
    expect(draws_text(vip, "NEVER_RESPAWN") && draws_text(vip, "VIP_DEAD_CAM_INSTRUCTION"),
           "never-respawn and the VIP dead-camera instruction use retail keys");

    // add_big_messageBackGround: a queued line interrupts, then HUD.update
    // restarts the background line once while its time is left.
    GameHudModel background;
    background.set_background_big_message("dead-cam");
    background.set_big_message("announcement");
    for (int tick{}; tick < 90; ++tick) background.tick();
    expect(background.big_message().text == "announcement",
           "a queued announcement replaces the background line after 1.5 s");
    for (int tick{}; tick < 240; ++tick) background.tick();
    expect(background.big_message().text.empty(),
           "the background line does not return after its own duration lapsed");
    GameHudModel refreshed;
    refreshed.set_background_big_message("dead-cam");
    refreshed.set_big_message("announcement");
    for (int tick{}; tick < 100; ++tick) {
        refreshed.set_background_big_message("dead-cam");
        refreshed.tick();
    }
    for (int tick{}; tick < 240; ++tick) {
        refreshed.set_background_big_message("dead-cam");
        refreshed.tick();
    }
    expect(refreshed.big_message().text == "dead-cam",
           "a per-frame background line returns once the lane is idle");
    refreshed.hide_big_message_if("dead-cam");
    expect(refreshed.big_message().text.empty(),
           "GameScene hides the dead-cam line when it no longer applies");
}

} // namespace

int main() {
    try {
        disconnect_reasons_follow_retail_strings();
        radar_uses_three_dimensional_250_block_range();
        look_at_angles_match_session_convention();
        score_and_award_keys_match_retail_tables();
        kick_vote_denials_and_packet();
        kick_vote_select_flow();
        hud_labels_resolve_through_retail_strings();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
