#include "battlespades/frontend/match_overlays.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

using namespace battlespades;
using namespace battlespades::frontend;

void expect(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error{std::string{message}};
}

void chat_is_bounded_and_utf8_safe() {
    GameChatModel chat;
    for (std::size_t index{}; index < 55U; ++index) {
        chat.add("message " + std::to_string(index));
    }
    expect(chat.entries().size() == GameChatModel::maximum_entries,
           "chat feed must evict its oldest retained line");
    expect(chat.entries().front().text == "message 54" &&
               chat.entries().back().text == "message 5",
           "retail chat must retain its newest line at index zero");

    chat.begin(ChatChannel::team);
    expect(chat.append_text("hello \xE2\x98\x83"),
           "chat input must accept committed UTF-8");
    expect(chat.erase_code_point(), "backspace must remove one code point");
    expect(chat.input() == "hello ",
           "backspace must not leave a partial UTF-8 sequence");
    const auto submitted = chat.submit();
    expect(submitted.has_value() &&
               submitted->first == ChatChannel::team &&
               submitted->second == "hello ",
           "chat submit must retain its selected wire lane");

    chat.tick(GameChatModel::entry_lifetime_seconds + 1.0);
    const auto draw =
        GameChatPresentation{}.build(chat, ui::PixelExtent{1280, 720});
    expect(draw.empty(), "expired chat must disappear while input is closed");
}

void chat_presentation_matches_retail_geometry_stroke_and_fade() {
    GameChatModel chat;
    chat.add("older", {30U, 140U, 220U, 255U});
    chat.add("newest", {240U, 180U, 40U, 255U});
    chat.tick(3.75);
    const auto draw =
        GameChatPresentation{}.build(chat, ui::PixelExtent{1280, 720});
    expect(draw.size() == 18U,
           "each visible retail chat line must use eight stroke draws plus text");
    const auto* first_shadow =
        std::get_if<ui::TextDrawCommand>(&draw.commands()[0U]);
    const auto* newest =
        std::get_if<ui::TextDrawCommand>(&draw.commands()[8U]);
    const auto* older =
        std::get_if<ui::TextDrawCommand>(&draw.commands()[17U]);
    expect(first_shadow != nullptr && newest != nullptr && older != nullptr,
           "chat stroke and foreground commands must remain text draws");
    expect(first_shadow->destination ==
                   ui::DrawRect{13.0, 656.0, 1256.0, 13.0} &&
               first_shadow->modulation.color ==
                   ui::ColorRgba8{64U, 64U, 64U, 127U},
           "retail first stroke must preserve bottom-left offset conversion");
    expect(newest->localization_key == "newest" &&
               newest->destination ==
                   ui::DrawRect{12.0, 655.0, 1256.0, 13.0} &&
               newest->preferred_font_asset ==
                   "fonts/A750-Sans-Medium.ttf" &&
               newest->requested_font_size_pixels == 12.0 &&
               newest->vertical_alignment ==
                   ui::VerticalTextAlignment::baseline &&
               newest->modulation.color.alpha == 127U,
           "newest chat line must use retail's 65px bottom-origin baseline and fade");
    expect(older->localization_key == "older" &&
               older->destination.y == 632.0,
           "older chat lines must rise by the measured 23px pitch");

    GameChatModel input;
    input.begin(ChatChannel::team);
    expect(input.append_text("hello"), "chat fixture must accept text");
    const auto input_draw =
        GameChatPresentation{}.build(input, ui::PixelExtent{1280, 720});
    expect(input_draw.size() == 18U,
           "retail input and channel labels must each be stroked independently");
    const auto* input_text =
        std::get_if<ui::TextDrawCommand>(&input_draw.commands()[8U]);
    const auto* channel_text =
        std::get_if<ui::TextDrawCommand>(&input_draw.commands()[17U]);
    expect(input_text != nullptr && channel_text != nullptr &&
               input_text->localization_key == "hello" &&
               input_text->destination.y == 705.0 &&
               input_text->vertical_alignment ==
                   ui::VerticalTextAlignment::baseline &&
               channel_text->localization_key == "Team chat:" &&
               channel_text->destination.y == 680.0 &&
               channel_text->vertical_alignment ==
                   ui::VerticalTextAlignment::baseline,
           "input must not fabricate a panel, uppercase prefix, or underscore cursor");
    expect(std::ranges::none_of(
               input_draw.commands(), [](const ui::DrawCommand& command) {
                   return std::holds_alternative<ui::SpriteDrawCommand>(command);
               }),
           "retail chat input has no black background quad");
}

void player_chat_preserves_retail_sender_and_body_labels() {
    GameChatModel chat;
    chat.add_player_message("Builder", {44U, 117U, 179U, 255U},
                            "hello", false);
    expect(chat.entries().front().text == "Builder: hello" &&
               chat.entries().front().runs.size() == 2U,
           "player chat must retain a compatibility line and two retail labels");
    const auto draw = GameChatPresentation{}.build(
        chat, ui::PixelExtent{1280, 720},
        [](std::string_view value, double, std::string_view) {
            return value == "Builder: " ? 49.0 : 30.0;
        });
    expect(draw.size() == 18U,
           "sender prefix and body must each use retail eight-neighbour stroke");
    const auto* prefix =
        std::get_if<ui::TextDrawCommand>(&draw.commands()[8U]);
    const auto* body =
        std::get_if<ui::TextDrawCommand>(&draw.commands()[17U]);
    expect(prefix != nullptr && body != nullptr &&
               prefix->localization_key == "Builder: " &&
               prefix->modulation.color ==
                   ui::ColorRgba8{44U, 117U, 179U, 255U} &&
               body->localization_key == "hello" &&
               body->destination.x == 61.0 &&
               body->modulation.color ==
                   ui::ColorRgba8{255U, 255U, 255U, 255U},
           "normal chat body must start after the measured team-coloured prefix and stay white");

    GameChatModel team_chat;
    team_chat.add_player_message("Builder", {44U, 117U, 179U, 255U},
                                 "team", true);
    const auto team_draw = GameChatPresentation{}.build(
        team_chat, ui::PixelExtent{1280, 720},
        [](std::string_view value, double, std::string_view) {
            return static_cast<double>(value.size()) * 6.0;
        });
    const auto* team_body =
        std::get_if<ui::TextDrawCommand>(&team_draw.commands()[17U]);
    expect(team_body != nullptr && team_body->modulation.color ==
                                       ui::ColorRgba8{128U, 172U, 209U, 255U},
           "team body must use shared.common.blend_color(team, white, 0.4)");

    GameChatModel localized_chat;
    localized_chat.add_player_message("Builder", {44U, 117U, 179U, 255U},
                                      "privet", false, 6U);
    std::vector<std::string> measured_fonts;
    const auto localized_draw = GameChatPresentation{}.build(
        localized_chat, ui::PixelExtent{1280, 720},
        [&measured_fonts](std::string_view value, double,
                          std::string_view font_asset) {
            measured_fonts.emplace_back(font_asset);
            return value == "Builder: " ? 53.0 : 36.0;
        });
    const auto* localized_prefix =
        std::get_if<ui::TextDrawCommand>(&localized_draw.commands()[8U]);
    const auto* localized_body =
        std::get_if<ui::TextDrawCommand>(&localized_draw.commands()[17U]);
    expect(localized_prefix != nullptr && localized_body != nullptr &&
               localized_prefix->preferred_font_asset ==
                   "fonts/Tuffy_Bold.ttf" &&
               localized_body->preferred_font_asset ==
                   "fonts/Tuffy_Bold.ttf" &&
               localized_body->destination.x == 65.0 &&
               measured_fonts == std::vector<std::string>{
                                     "fonts/Tuffy_Bold.ttf",
                                     "fonts/Tuffy_Bold.ttf"},
           "Russian, Polish and Turkish player chat must measure and draw both runs with ChatLine.tuffy_font");
}

void vote_decoding_and_cast_are_crash_safe() {
    expect(GenericVotingModel::decode_retail_literal(
               "('VOTE_MAP_TITLE', ())") == "VOTE MAP",
           "retail vote tuple must resolve its source English string");
    expect(GenericVotingModel::decode_retail_literal(
               "('VOTE_TO_KICK_DESCRIPTION', ('Builder', 'Griefing', 'Admin'))") ==
               "Vote to Kick Builder for Griefing? Vote initiated by Admin",
           "retail vote tuple must preserve and format dynamic arguments");
    expect(GenericVotingModel::decode_retail_literal(
               "(('VOTE_TO_KICK_DESCRIPTION', (1,)), "
               "('Builder', 'KICK_REASON_GRIEFING', 'Admin'))") ==
               "Vote to Kick Builder for Griefing? Vote initiated by Admin",
           "retail nested identifier form must localize selected arguments");
    expect(GenericVotingModel::decode_retail_literal(
               "('Maya \\u2605 {{Night}}', ())") ==
               "Maya \xE2\x98\x85 {Night}",
           "safe literal decoding must reproduce Python escapes and format braces");
    expect(GenericVotingModel::decode_retail_literal(
               "__import__('os').system('bad')") ==
               "__import__('os').system('bad')",
           "malformed-but-printable vote text must remain inert plain text");
    expect(GenericVotingModel::decode_retail_literal("('ONE_ITEM',)") ==
               "('ONE_ITEM',)",
           "a malformed one-item tuple must remain inert instead of indexing past it");

    network::GenericVoteMessagePacket packet;
    packet.message_type = network::GenericVoteMessagePacket::start;
    packet.allow_revote = false;
    packet.can_vote = true;
    packet.hide_after_vote = true;
    packet.title = "('VOTE_MAP_TITLE', ())";
    packet.description = "('VOTE_MAP_DESCRIPTION', ())";
    packet.candidates = {
        {"('City of Chicago', ())", 4},
        {"('Castle Wars', ())", 2},
        {"('Maya Jungle', ())", 1},
    };
    GenericVotingModel vote;
    vote.apply(packet);
    const auto normal_draw = GenericVotingPresentation{}.build(
        vote, ui::PixelExtent{1280, 720}, {"F1", "F2", "F3"});
    const auto* panel =
        std::get_if<ui::SpriteDrawCommand>(&normal_draw.commands().front());
    expect(panel != nullptr &&
               panel->destination == ui::DrawRect{10.0, 260.0, 290.0, 200.0} &&
               panel->modulation.color ==
                   ui::ColorRgba8{0U, 0U, 0U, 100U},
           "vote panel must use retail dimensions, vertical centering and alpha");
    const auto has_title_face = std::ranges::any_of(
        normal_draw.commands(), [](const ui::DrawCommand& command) {
            const auto* value = std::get_if<ui::TextDrawCommand>(&command);
            return value != nullptr && value->localization_key == "VOTE MAP" &&
                   value->preferred_font_asset == "fonts/Spades.ttf" &&
                   value->requested_font_size_pixels == 26.0;
        });
    const auto has_retail_key = std::ranges::any_of(
        normal_draw.commands(), [](const ui::DrawCommand& command) {
            const auto* value = std::get_if<ui::TextDrawCommand>(&command);
            return value != nullptr && value->localization_key == "[F1] ";
        });
    const auto has_retail_votes = std::ranges::any_of(
        normal_draw.commands(), [](const ui::DrawCommand& command) {
            const auto* value = std::get_if<ui::TextDrawCommand>(&command);
            return value != nullptr && value->localization_key == "[4]";
        });
    expect(has_title_face && has_retail_key && has_retail_votes,
           "vote text must use hc_font and bracketed retail key/vote labels");
    const auto* title =
        std::get_if<ui::TextDrawCommand>(&normal_draw.commands()[1U]);
    const auto* description =
        std::get_if<ui::TextDrawCommand>(&normal_draw.commands()[2U]);
    const auto* first_key =
        std::get_if<ui::TextDrawCommand>(&normal_draw.commands()[3U]);
    const auto* first_candidate =
        std::get_if<ui::TextDrawCommand>(&normal_draw.commands()[4U]);
    const auto* first_count =
        std::get_if<ui::TextDrawCommand>(&normal_draw.commands()[5U]);
    const auto* last_key =
        std::get_if<ui::TextDrawCommand>(&normal_draw.commands()[9U]);
    expect(title != nullptr && description != nullptr && first_key != nullptr &&
               first_candidate != nullptr && first_count != nullptr &&
               last_key != nullptr,
           "retail vote panel must emit its recovered title, description and row fields");
    expect(std::abs(title->destination.y - (720.0 - (400.0 + 70.0 / 3.0))) <
                   1.0e-9 &&
               title->destination.x == 10.0 &&
               title->destination.width == 290.0 &&
               title->vertical_alignment == ui::VerticalTextAlignment::baseline &&
               title->fit == ui::TextFit::retail_width_scale,
           "vote title must use draw_text_with_size_validation's exact baseline and FTGL width scale");
    expect(description->destination ==
                   ui::DrawRect{15.0, 330.0, 295.0, 80.0} &&
               description->layout == ui::TextLayout::retail_wrapped_lines &&
               description->vertical_alignment ==
                   ui::VerticalTextAlignment::baseline &&
               description->line_spacing_pixels == 4.0 &&
               description->maximum_lines == 0U,
           "vote description must reproduce the source text.py wrap/resize helper contract");
    expect(first_key->destination ==
                   ui::DrawRect{40.0, 385.0, 25.0, 30.0} &&
               first_candidate->destination ==
                   ui::DrawRect{90.0, 385.0, 150.0, 30.0} &&
               first_count->destination ==
                   ui::DrawRect{250.0, 385.0, 25.0, 30.0} &&
               last_key->destination ==
                   ui::DrawRect{40.0, 445.0, 25.0, 30.0},
           "vote rows must use the recovered 40/90/250 columns and bottom-origin 30px pitch");
    expect(first_key->vertical_alignment ==
                   ui::VerticalTextAlignment::baseline &&
               first_candidate->fit == ui::TextFit::retail_width_scale &&
               first_count->fit == ui::TextFit::retail_width_scale,
           "vote row helpers must preserve center_text=False baselines and cached-glyph fitting");
    expect(std::ranges::count_if(
               normal_draw.commands(), [](const ui::DrawCommand& command) {
                   return std::holds_alternative<ui::SpriteDrawCommand>(command);
               }) == 1,
           "retail vote candidates must not receive fabricated row backgrounds");

    const auto token = vote.cast(1U);
    expect(token.has_value() &&
               *token == "('Castle Wars', ())",
           "vote CAST must return the exact original candidate token");
    expect(vote.visible() && !vote.showing_result() && !vote.can_vote() &&
               vote.voted_index() == 1U,
           "local CAST must mark the row, disable non-revote input, and keep the list visible");
    expect(!vote.cast(2U).has_value(),
           "GameScene can_vote must prevent a second cast when revoting is disabled");

    const auto after_cast_draw = GenericVotingPresentation{}.build(
        vote, ui::PixelExtent{1280, 720}, {"F1", "F2", "F3"});
    expect(vote.visible() && !vote.showing_result() &&
               after_cast_draw.size() == 9U,
           "local CAST must hide retail key hints without fabricating a result panel");
    expect(std::ranges::none_of(
               after_cast_draw.commands(), [](const ui::DrawCommand& command) {
                   const auto* value = std::get_if<ui::TextDrawCommand>(&command);
                   return value != nullptr && value->localization_key.starts_with("[F");
               }),
           "GenericVotingHUD must not draw key hints while can_vote is false");
    const auto* cast_first_candidate =
        std::get_if<ui::TextDrawCommand>(&after_cast_draw.commands()[3U]);
    const auto* selected_count =
        std::get_if<ui::TextDrawCommand>(&after_cast_draw.commands()[6U]);
    expect(cast_first_candidate != nullptr && selected_count != nullptr &&
               cast_first_candidate->destination.x == 40.0 &&
               selected_count->localization_key == "[2]" &&
               selected_count->modulation.color ==
                   ui::ColorRgba8{0U, 255U, 0U, 255U},
           "cast rows must reclaim the key column and tint only the selected vote count green");
    vote.tick(GenericVotingModel::hide_after_cast_seconds);
    expect(vote.visible(),
           "retail hide-after-vote comparison is strict and must remain visible at exactly five seconds");
    vote.tick(0.01);
    expect(!vote.visible(),
           "hide-after-vote must dismiss the normal list after five seconds");

    network::GenericVoteMessagePacket update = packet;
    update.message_type = network::GenericVoteMessagePacket::update;
    update.can_vote = true;
    update.allow_revote = true;
    update.hide_after_vote = false;
    update.title = "('KICK_PLAYER', ())";
    update.description = "('KICK_PLAYER', ())";
    update.candidates = {{"('Updated', ())", 9}};
    vote.apply(update);
    expect(!vote.visible() && !vote.can_vote() && !vote.allow_revote() &&
               !vote.voted_index().has_value() && vote.title() == "VOTE MAP" &&
               vote.description() ==
                   "Please select the map you would like to play next:" &&
               vote.choices().size() == 1U &&
               vote.choices()[0U].display_text == "Updated" &&
               vote.choices()[0U].votes == 9,
           "UPDATE must replace candidates and reset the voted row without replaying START fields");

    packet.allow_revote = true;
    packet.hide_after_vote = false;
    vote.apply(packet);
    expect(vote.cast(0U).has_value() && vote.can_vote() &&
               vote.cast(2U).has_value() && vote.can_vote() &&
               vote.voted_index() == 2U,
           "allow_vote_changing must retain input and permit a later valid candidate");

    network::GenericVoteMessagePacket closed;
    closed.message_type = network::GenericVoteMessagePacket::closed;
    closed.title =
        "('VOTE_TO_KICK_DESCRIPTION', ('Builder', 'Griefing', 'Admin'))";
    vote.apply(closed);
    const auto result_draw = GenericVotingPresentation{}.build(
        vote, ui::PixelExtent{1280, 720}, {"F1", "F2", "F3"});
    expect(vote.visible() && vote.showing_result() && !vote.can_vote() &&
               vote.choices().size() == 3U && result_draw.size() == 2U,
           "CLOSED must preserve rows and show the packet title as a six-second result");
    const auto* result_panel =
        std::get_if<ui::SpriteDrawCommand>(&result_draw.commands()[0U]);
    const auto* result_text =
        std::get_if<ui::TextDrawCommand>(&result_draw.commands()[1U]);
    expect(result_panel != nullptr && result_text != nullptr &&
           result_panel->destination ==
                   ui::DrawRect{15.0, 325.0, 290.0, 80.0} &&
               result_text->localization_key ==
                   "Vote to Kick Builder for Griefing? Vote initiated by Admin" &&
               result_text->destination ==
                   ui::DrawRect{15.0, 405.0, 295.0, 80.0} &&
               result_text->line_spacing_pixels == 4.0 &&
               result_text->layout == ui::TextLayout::retail_wrapped_lines &&
               result_text->vertical_alignment ==
                   ui::VerticalTextAlignment::baseline,
           "vote result must use the recovered 290x80 panel and 295px source text helper");
    vote.tick(GenericVotingModel::closed_result_seconds);
    expect(vote.showing_result(),
           "retail voted-result comparison is strict at exactly six seconds");
    vote.tick(0.01);
    expect(!vote.visible() && !vote.showing_result() &&
               GenericVotingPresentation{}
                   .build(vote, ui::PixelExtent{1280, 720},
                          {"F1", "F2", "F3"})
                   .empty(),
           "server-authored result must hide after its six-second retail lifetime");
}

void vote_updates_keep_confirmation_and_closed_results() {
    GenericVotingModel vote;
    network::GenericVoteMessagePacket packet;
    packet.message_type = network::GenericVoteMessagePacket::start;
    packet.can_vote = true;
    packet.hide_after_vote = true;
    packet.allow_revote = false;
    packet.title = "('VOTE_MAP_TITLE', ())";
    packet.candidates = {{"('Castle Wars', ())", 0},
                         {"('Maya Jungle', ())", 0}};
    vote.apply(packet);
    expect(!vote.cast(2U).has_value() && !vote.voted_index().has_value(),
           "F3 on a two-choice ballot must not select an absent candidate");
    vote.tick(10.0);
    expect(vote.visible() && vote.can_vote(),
           "an invalid key must not start the hide-after-vote timer");
    expect(vote.cast(1U).has_value(), "valid map vote must be accepted");
    packet.message_type = network::GenericVoteMessagePacket::update;
    packet.candidates = {{"('Maya Jungle', ())", 1},
                         {"('Castle Wars', ())", 4}};
    vote.apply(packet);
    expect(vote.voted_index() == 0U && !vote.can_vote() &&
               vote.choices()[0U].votes == 1,
           "server count updates must preserve the selected wire token across reordering");
    const auto selected = GenericVotingPresentation{}.build(
        vote, {1280, 720}, {"F1", "F2", "F3"});
    expect(std::ranges::any_of(selected.commands(), [](const auto& command) {
        const auto* text = std::get_if<ui::TextDrawCommand>(&command);
        return text != nullptr && text->localization_key == "[1]" &&
               text->modulation.color == ui::ColorRgba8{0U, 255U, 0U, 255U};
    }), "successful vote must retain its visible green count confirmation");
    network::GenericVoteMessagePacket closed;
    closed.message_type = network::GenericVoteMessagePacket::closed;
    closed.title = "('MAP_VOTED_MESSAGE', ('Maya Jungle',))";
    vote.apply(closed);
    const std::string result{vote.result_text()};
    vote.apply(packet);
    expect(vote.showing_result() && vote.result_text() == result &&
               !vote.can_vote(),
           "an UPDATE arriving after CLOSED must not erase the map result");
    vote.tick(GenericVotingModel::closed_result_seconds + 0.01);
    vote.apply(packet);
    expect(!vote.visible() && !vote.can_vote(),
           "an expired ballot must not be resurrected by count updates");
    vote.clear();
    vote.apply(packet);
    expect(vote.choices().empty() && !vote.visible(),
           "UPDATE before START must not seed a stale vote in the next scene");
}

void map_vote_closed_result_names_the_authoritative_next_map() {
    GenericVotingModel vote;
    network::GenericVoteMessagePacket packet;
    packet.message_type = network::GenericVoteMessagePacket::start;
    packet.title = "('VOTE_MAP_TITLE', ())";
    packet.can_vote = true;
    packet.candidates = {{"('London', ())", 0}, {"('CastleWars', ())", 0}};
    vote.apply(packet);
    packet.message_type = network::GenericVoteMessagePacket::closed;
    packet.title = "('MAP_VOTED_MESSAGE', (\"Map {1}'s Hill\",))";
    vote.apply(packet);
    expect(vote.showing_result() && !vote.can_vote() &&
               vote.result_text() == "Next map will be Map {1}'s Hill",
           "CLOSED map result must localize the winner once, preserving literal braces and quotes");
}

void end_results_reject_malformed_awards_and_preserve_draws() {
    MatchResultsModel model;
    network::GameStatsPacket packet;
    packet.team_id = 2;
    packet.entries = {{257, 5}, {-1, 5}, {1, -1}, {1, 30}, {1, 5}};
    model.apply(packet);
    expect(model.awards().size() == 1U &&
               model.awards().front().player_id == 1U &&
               model.awards().front().stat_type == 5,
           "malformed signed IDs and stat ordinals must never alias a real player's award");
    packet.team_id = 258;
    packet.entries = {{2, 8}};
    model.apply(packet);
    expect(model.awards().size() == 1U,
           "out-of-range packet team IDs must not wrap into the blue list");
    model.show(0);
    expect(model.winner_team() == 0,
           "an authoritative draw must not become a blue victory when only blue awards arrived");

    MatchResultsModel empty_green;
    packet.team_id = 2;
    packet.entries = {{1, 5}, {2, 8}};
    empty_green.apply(packet);
    packet.team_id = 3;
    packet.entries.clear();
    empty_green.apply(packet);
    empty_green.show(0);
    expect(empty_green.has_both_team_lists(),
           "an empty team packet still establishes official two-team authority");
    network::Protocol168Roster roster;
    network::CreatePlayerPacket player;
    player.player_id = 1U;
    player.team = 2U;
    player.name = "Blue award";
    player.orientation = {1.0F, 0.0F, 0.0F};
    expect(roster.apply(player), "blue award fixture must enter roster");
    player.player_id = 2U;
    player.team = 3U;
    player.name = "Changed team";
    expect(roster.apply(player), "changed team fixture must enter roster");
    const auto draw = MatchResultsPresentation{}.build(
        empty_green, {}, "Team Deathmatch!", roster, {1280, 720});
    expect(std::ranges::any_of(draw.commands(), [](const auto& command) {
        const auto* text = std::get_if<ui::TextDrawCommand>(&command);
        return text != nullptr && text->localization_key == "Changed team" &&
               text->destination.x == 91.14;
    }), "empty official green list must not reclassify blue awards through a later roster");
}

void end_results_retain_award_winners_after_disconnect() {
    network::Protocol168Roster roster;
    network::CreatePlayerPacket player;
    player.player_id = 1U;
    player.team = 2U;
    player.name = "Original winner";
    player.orientation = {1.0F, 0.0F, 0.0F};
    expect(roster.apply(player), "award winner fixture must enter roster");
    MatchResultsModel model;
    network::GameStatsPacket packet;
    packet.team_id = 2;
    packet.entries = {{1, 5}, {2, 8}};
    model.apply(packet, roster);
    expect(model.awards().size() == 1U,
           "an unresolved award ID must not wait for an unrelated future player");
    packet.team_id = 3;
    packet.entries.clear();
    model.apply(packet, roster);
    model.show(2);
    roster.remove(1U);
    for (const bool reuse_slot : {false, true}) {
        if (reuse_slot) {
            player.name = "New arrival";
            player.team = 3U;
            expect(roster.apply(player), "new player must be able to reuse the departed slot");
        }
        const auto draw = MatchResultsPresentation{}.build(
            model, {}, "Team Deathmatch!", roster, {1280, 720});
        expect(std::ranges::any_of(draw.commands(), [](const auto& command) {
            const auto* text = std::get_if<ui::TextDrawCommand>(&command);
            return text != nullptr && text->localization_key == "Original winner" &&
                   text->destination.x == 91.14 &&
                   text->modulation.color == ui::ColorRgba8{44U, 117U, 179U, 255U};
        }), "GameStats must retain the actual winner's name and team after disconnect or slot reuse");
        expect(std::ranges::none_of(draw.commands(), [](const auto& command) {
            const auto* text = std::get_if<ui::TextDrawCommand>(&command);
            return text != nullptr && text->localization_key == "New arrival";
        }), "a new player must never inherit an earlier occupant's award");
    }
}

void end_results_group_the_server_snapshot_by_roster_team() {
    static constexpr std::array<std::string_view, 30U> retail_awards{
        "Just out for a stroll",
        "Barely touched the ground",
        "Hypochondriac",
        "Pancho Villa Resupply Award",
        "I <3 Block Crates Award",
        "Most Kills Award",
        "Healthbar Schmealthbar Award",
        "Milk And Two Teabagging Award",
        "Right Between the Eyes Award",
        "Bricklayer Award",
        "Destroy All The Things Award",
        "Longest Streaker Award",
        "Timber! Award for Destruction",
        "Eat My Draw Distance Range Award",
        "Up Close And Personal Melee Award",
        "Scholar by Proxy Brain Eating Award",
        "Attention Seeker Award",
        "You Shoot Him I Shoot You Defence Award",
        "\"Sir I Believe This is Your Kill\" Assist Award",
        "Invisible Helmet Airstrike Survival Kit",
        "Why Did I Buy A Bullet Magnet Award",
        "King of the Castle Highest Block Award",
        "Right in the Face Headshot Receiver Award",
        "Snipe THIS Counter-Sniper Award",
        "I Might Need Them Later Bullet Miser Award",
        "Didn't Get It Right First Time Suicide Award",
        "YOINK! Most Kill-Steals Award",
        "AAAAAAAAAAAAAAAAAA! Time on Fire Award",
        "Banana! Banana! Most Dominated",
        "Safety Word is Banana Domination",
    };
    for (std::size_t index{}; index < retail_awards.size(); ++index) {
        expect(retail_game_stat_award_label(static_cast<std::int32_t>(index)) ==
                   retail_awards[index],
               "every GameStats ordinal must resolve through retail GAME_STAT_TYPES");
    }
    expect(retail_game_stat_award_label(-1) == "Match Award" &&
               retail_game_stat_award_label(30) == "Match Award",
           "malformed GameStats ordinals must fail closed without indexing retail data");
    expect(retail_level_screenshot_asset("City Of Chicago", 3U) ==
               "png/ui/level_screenshots/City of Chicago3.png" &&
               retail_level_screenshot_asset("CityOfChicago", 0U) ==
                   "png/ui/level_screenshots/City of Chicago0.png" &&
               retail_level_screenshot_asset("MayanJungle", 2U) ==
                   "png/ui/level_screenshots/Mayan Jungle2.png" &&
               retail_level_screenshot_asset("LunarBase", 1U) ==
                   "png/ui/level_screenshots/Lunar Base1.png" &&
               retail_level_screenshot_asset("TheColosseum", 0U) ==
                   "png/ui/level_screenshots/The Colosseum0.png" &&
               retail_level_screenshot_asset("20thCenturyTown", 3U) ==
                   "png/ui/level_screenshots/WW13.png" &&
               retail_level_screenshot_asset("20th Century Town", 0U) ==
                   "png/ui/level_screenshots/WW10.png" &&
               !retail_level_screenshot_asset("../escaped", 0U).has_value(),
           "result-camera assets must resolve stock stems, preserve retail spelling and reject path traversal");

    MatchResultsModel results;
    network::GameStatsPacket team1;
    team1.team_id = 2;
    team1.entries = {{1, 5}, {5, 8}, {3, 11}, {4, 13}};
    results.apply(team1);
    expect(results.awards().size() == 4U && results.winner_team() == 0,
           "packet-67 team id must select an award list, not invent a winner");
    expect(results.awards().front().team_id == 2U,
           "packet 67 must retain its authoritative blue result column");
    network::GameStatsPacket team2;
    team2.team_id = 3;
    team2.entries = {{2, 9}};
    results.apply(team2);
    expect(results.awards().size() == 5U,
           "official two-packet results must retain both team award lists");
    expect(results.awards().back().team_id == 3U,
           "packet 67 must retain its authoritative green result column");
    network::RankUpsPacket rank_ups;
    rank_ups.entries = {{1, 20, 21}};
    results.apply(rank_ups);
    rank_ups.entries = {{1, 40, 41}};
    results.apply(rank_ups);
    expect(results.rank_ups().size() == 2U &&
               results.rank_ups().front().new_score == 21 &&
               results.rank_ups().back().new_score == 41,
           "split RankUps packets must append before retail's LIFO display");
    results.show(2);

    network::Protocol168Roster roster;
    network::CreatePlayerPacket player;
    player.player_id = 1U;
    player.class_id = 0U;
    player.team = 2U;
    player.position = {1.0F, 2.0F, 3.0F};
    player.orientation = {1.0F, 0.0F, 0.0F};
    player.name = "Kiko";
    player.loadout = {17U};
    expect(roster.apply(player), "result fixture player must enter roster");
    player.player_id = 2U;
    player.team = 3U;
    player.name = "Juno";
    expect(roster.apply(player), "second result fixture player must enter roster");

    ChangeTeamServerState state;
    state.team1_name = "Blue";
    state.team2_name = "Green";
    // ViewGameStats deliberately uses UI_TEAM_COLOURS, not these live model
    // colors. Non-retail fixture colors catch accidental coupling.
    state.team1_color = {40U, 80U, 220U};
    state.team2_color = {60U, 190U, 70U};
    state.team1_score = 10;
    state.team2_score = 8;
    state.score_limit = 200;
    const auto draw = MatchResultsPresentation{}.build(
        results, state, "TEAM DEATHMATCH!", roster,
        ui::PixelExtent{1280, 720},
        MatchScreenshotPreview{"City Of Chicago", 3U, true});

    expect(draw.size() >= 3U,
           "end-screen fixture must include its retail camera preview");
    const auto* screenshot_frame =
        std::get_if<ui::SpriteDrawCommand>(&draw.commands()[0U]);
    const auto* screenshot =
        std::get_if<ui::SpriteDrawCommand>(&draw.commands()[1U]);
    expect(screenshot_frame != nullptr && screenshot != nullptr &&
               screenshot_frame->asset_id.ends_with(
                   "in_game_menus/screenshot_frame.png") &&
                screenshot_frame->destination ==
                    ui::DrawRect{10.0, 10.0, 197.0, 158.0} &&
                screenshot_frame->space == ui::DrawSpace::window_pixels &&
                screenshot_frame->retail_source_scale == 0.64 &&
               screenshot->asset_id.ends_with(
                   "level_screenshots/City of Chicago3.png") &&
                screenshot->destination ==
                    ui::DrawRect{20.0, 22.0, 176.0, 135.0} &&
                screenshot->space == ui::DrawSpace::window_pixels &&
                screenshot->retail_source_scale == 0.64,
           "ViewGameStats.draw_hud must keep the framed camera in raw window pixels and preserve the Chicago spelling fix");

    expect(std::ranges::none_of(
               draw.commands(), [](const ui::DrawCommand& command) {
                   const auto* sprite =
                       std::get_if<ui::SpriteDrawCommand>(&command);
                   return sprite != nullptr &&
                          sprite->asset_id.ends_with("high/white.png") &&
                          sprite->destination ==
                              ui::DrawRect{0.0, 0.0, 800.0, 600.0};
               }),
           "retail ViewGameStats must not fabricate a full-screen black veil");

    const auto frame = std::ranges::find_if(
        draw.commands(), [](const ui::DrawCommand& command) {
            const auto* sprite = std::get_if<ui::SpriteDrawCommand>(&command);
            return sprite != nullptr &&
                   sprite->asset_id.ends_with(
                       "view_game_stats_content_framesy.png");
        });
    expect(frame != draw.commands().end(),
           "end screen must use the authored retail result frame");
    const auto* frame_sprite =
        std::get_if<ui::SpriteDrawCommand>(&*frame);
    expect(frame_sprite != nullptr &&
               frame_sprite->destination ==
                   ui::DrawRect{49.0, 380.0, 702.0, 220.0} &&
               frame_sprite->retail_source_scale == 0.64 &&
               frame_sprite->retail_source_anchor == ui::TextureAnchor::center,
           "result frame must preserve 0.64 load truncation and center blit(400,110)");

    const auto result_message = std::ranges::find_if(
        draw.commands(), [](const ui::DrawCommand& command) {
            const auto* value = std::get_if<ui::TextDrawCommand>(&command);
            return value != nullptr && value->localization_key == "Blue wins!";
        });
    const auto mode_heading = std::ranges::find_if(
        draw.commands(), [](const ui::DrawCommand& command) {
            const auto* value = std::get_if<ui::TextDrawCommand>(&command);
            return value != nullptr &&
                   value->localization_key == "TEAM DEATHMATCH!";
        });
    expect(result_message != draw.commands().end() &&
               mode_heading != draw.commands().end() &&
               frame < result_message && frame < mode_heading,
           "authored frame must be painted before result and mode labels");

    const auto find_sprite = [&](std::string_view suffix) {
        return std::ranges::find_if(
            draw.commands(), [suffix](const ui::DrawCommand& command) {
                const auto* value =
                    std::get_if<ui::SpriteDrawCommand>(&command);
                return value != nullptr && value->asset_id.ends_with(suffix);
            });
    };
    const auto blue_head = find_sprite("deuce_head_colour_2.png");
    const auto green_head = find_sprite("deuce_head_colour_1.png");
    expect(blue_head != draw.commands().end() &&
               green_head != draw.commands().end(),
           "each result column must composite its recovered Deuce head pair");
    const auto* blue_head_sprite =
        std::get_if<ui::SpriteDrawCommand>(&*blue_head);
    const auto* green_head_sprite =
        std::get_if<ui::SpriteDrawCommand>(&*green_head);
    expect(blue_head_sprite != nullptr && green_head_sprite != nullptr &&
               blue_head_sprite->destination ==
                   ui::DrawRect{78.0, 445.0, 46.0, 44.0} &&
               green_head_sprite->destination ==
                   ui::DrawRect{678.0, 445.0, 46.0, 44.0} &&
               blue_head_sprite->modulation.color ==
                   ui::ColorRgba8{44U, 117U, 179U, 255U} &&
               green_head_sprite->modulation.color ==
                   ui::ColorRgba8{137U, 179U, 44U, 255U},
           "head overlays must use fixed retail UI team colors and exact anchors");

    const auto find_text = [&](std::string_view value) {
        return std::ranges::find_if(
            draw.commands(), [value](const ui::DrawCommand& command) {
                const auto* text = std::get_if<ui::TextDrawCommand>(&command);
                return text != nullptr && text->localization_key == value;
            });
    };
    const auto mode = find_text("TEAM DEATHMATCH!");
    const auto result = find_text("Blue wins!");
    const auto blue_score = find_text("10/200");
    const auto green_score = find_text("8/200");
    const auto footer = find_text("Press TAB to show scores");
    expect(mode != draw.commands().end() && result != draw.commands().end() &&
               blue_score != draw.commands().end() &&
               green_score != draw.commands().end() &&
               footer != draw.commands().end(),
           "result screen must expose retail mode, message, scores and instruction");
    const auto* mode_text = std::get_if<ui::TextDrawCommand>(&*mode);
    const auto* result_text = std::get_if<ui::TextDrawCommand>(&*result);
    const auto* blue_score_text =
        std::get_if<ui::TextDrawCommand>(&*blue_score);
    const auto* green_score_text =
        std::get_if<ui::TextDrawCommand>(&*green_score);
    const auto* footer_text = std::get_if<ui::TextDrawCommand>(&*footer);
    expect(mode_text != nullptr && result_text != nullptr &&
               blue_score_text != nullptr && green_score_text != nullptr &&
               footer_text != nullptr &&
               mode_text->destination ==
                   ui::DrawRect{74.0, 420.0, 652.0, 0.0} &&
               mode_text->requested_font_size_pixels == 46.0 &&
               mode_text->vertical_alignment ==
                   ui::VerticalTextAlignment::baseline &&
               mode_text->fit == ui::TextFit::retail_width_scale &&
               result_text->destination ==
                   ui::DrawRect{64.0, 435.0, 672.0, 0.0} &&
               result_text->requested_font_size_pixels == 14.0 &&
               result_text->vertical_alignment ==
                   ui::VerticalTextAlignment::baseline &&
               result_text->fit == ui::TextFit::retail_width_scale &&
               blue_score_text->destination ==
                   ui::DrawRect{285.0, 455.0, 100.0, 30.0} &&
                blue_score_text->horizontal_alignment ==
                    ui::HorizontalTextAlignment::right &&
                blue_score_text->fit == ui::TextFit::retail_width_scale &&
                blue_score_text->vertical_alignment ==
                    ui::VerticalTextAlignment::retail_center &&
                green_score_text->destination ==
                    ui::DrawRect{410.0, 455.0, 100.0, 30.0} &&
                green_score_text->horizontal_alignment ==
                    ui::HorizontalTextAlignment::left &&
                green_score_text->fit == ui::TextFit::retail_width_scale &&
                green_score_text->vertical_alignment ==
                    ui::VerticalTextAlignment::retail_center &&
                footer_text->destination ==
                    ui::DrawRect{300.0, 557.0, 200.0, 18.0} &&
                footer_text->requested_font_size_pixels == 18.0 &&
                footer_text->fit == ui::TextFit::retail_width_scale &&
                footer_text->vertical_alignment ==
                    ui::VerticalTextAlignment::retail_center,
           "result typography must retain recovered fonts, anchors and alignment");

    constexpr std::array<std::string_view, 9U> retail_result_messages{
        "End of map. Next map incoming...",
        "Blue wins!",
        "Blue destroyed Green base!",
        "Zombie virus has claimed all survivors!",
        "Zombie outbreak contained! Survivors receive a score bonus!",
        "Blue wins!",
        "Blue wins!",
        "Green wins!",
        "Draw!",
    };
    for (std::size_t message_id{}; message_id < retail_result_messages.size();
         ++message_id) {
        MatchResultsModel authored_result;
        authored_result.apply(network::ShowTextMessagePacket{
            .message_id = static_cast<std::uint8_t>(message_id),
            .duration = 6.0F});
        authored_result.show(2);
        const auto authored_draw = MatchResultsPresentation{}.build(
            authored_result, state, "TEAM DEATHMATCH!", roster,
            ui::PixelExtent{1280, 720});
        expect(std::ranges::any_of(
                   authored_draw.commands(), [&](const ui::DrawCommand& command) {
                       const auto* value =
                           std::get_if<ui::TextDrawCommand>(&command);
                       return value != nullptr &&
                              value->localization_key ==
                                  retail_result_messages[message_id];
                   }),
               "every ShowTextMessage selector must reach ViewGameStats");
        authored_result.clear();
        expect(!authored_result.message_id().has_value(),
               "scene teardown must discard the previous result selector");
    }

    const auto kiko = find_text("Kiko");
    const auto juno = find_text("Juno");
    expect(kiko != draw.commands().end() && juno != draw.commands().end(),
           "server awards must populate both recovered team columns");
    const auto* kiko_text = std::get_if<ui::TextDrawCommand>(&*kiko);
    const auto* juno_text = std::get_if<ui::TextDrawCommand>(&*juno);
    expect(kiko_text != nullptr && juno_text != nullptr &&
               kiko_text->destination ==
                   ui::DrawRect{91.14, 506.0, 86.06, 0.0} &&
               juno_text->destination ==
                   ui::DrawRect{423.14, 506.0, 86.06, 0.0} &&
               kiko_text->requested_font_size_pixels == 11.0 &&
               kiko_text->preferred_font_asset ==
                   "fonts/A750-Sans-Medium.ttf" &&
               kiko_text->vertical_alignment ==
                   ui::VerticalTextAlignment::baseline &&
               kiko_text->fit == ui::TextFit::retail_width_scale &&
               kiko_text->modulation.color ==
                   ui::ColorRgba8{44U, 117U, 179U, 255U} &&
               juno_text->modulation.color ==
                   ui::ColorRgba8{137U, 179U, 44U, 255U},
           "award rows must preserve retail 16px pitch, columns and text colors");

    const auto blue_row = std::ranges::find_if(
        draw.commands(), [](const ui::DrawCommand& command) {
            const auto* sprite = std::get_if<ui::SpriteDrawCommand>(&command);
            return sprite != nullptr && sprite->asset_id.ends_with("high/white.png") &&
                   sprite->destination ==
                       ui::DrawRect{78.0, 494.0, 314.0, 16.0};
        });
    const auto green_row = std::ranges::find_if(
        draw.commands(), [](const ui::DrawCommand& command) {
            const auto* sprite = std::get_if<ui::SpriteDrawCommand>(&command);
            return sprite != nullptr && sprite->asset_id.ends_with("high/white.png") &&
                   sprite->destination ==
                       ui::DrawRect{410.0, 494.0, 314.0, 16.0};
        });
    expect(blue_row != draw.commands().end() &&
               green_row != draw.commands().end() &&
               std::get<ui::SpriteDrawCommand>(*blue_row).modulation.color ==
                   ui::ColorRgba8{36U, 51U, 63U, 255U} &&
               std::get<ui::SpriteDrawCommand>(*green_row).modulation.color ==
                   ui::ColorRgba8{55U, 63U, 36U, 255U},
           "award backgrounds must use shared.common's truncated 0.2 blend");

    MatchResultsModel mixed_compatibility_results;
    network::GameStatsPacket mixed_packet;
    mixed_packet.team_id = 2;
    mixed_packet.entries = {{1, 5}, {2, 9}};
    mixed_compatibility_results.apply(mixed_packet);
    mixed_compatibility_results.show(2);
    const auto mixed_draw = MatchResultsPresentation{}.build(
        mixed_compatibility_results, state, "TEAM DEATHMATCH!", roster,
        ui::PixelExtent{1280, 720});
    expect(std::ranges::any_of(
               mixed_draw.commands(), [](const ui::DrawCommand& command) {
                   const auto* value = std::get_if<ui::TextDrawCommand>(&command);
                   return value != nullptr && value->localization_key == "Kiko" &&
                          value->destination.x == 91.14;
               }) &&
               std::ranges::any_of(
                   mixed_draw.commands(), [](const ui::DrawCommand& command) {
                       const auto* value = std::get_if<ui::TextDrawCommand>(&command);
                       return value != nullptr && value->localization_key == "Juno" &&
                              value->destination.x == 423.14;
                   }),
           "one mixed BattleSpades GameStats packet must split rows by the authoritative roster");

    MatchResultsModel draw_compatibility_results;
    mixed_packet.team_id = 0;
    draw_compatibility_results.apply(mixed_packet);
    draw_compatibility_results.show(0);
    const auto draw_compatibility = MatchResultsPresentation{}.build(
        draw_compatibility_results, state, "TEAM DEATHMATCH!", roster,
        ui::PixelExtent{1280, 720});
    expect(std::ranges::any_of(
               draw_compatibility.commands(), [](const ui::DrawCommand& command) {
                   const auto* value = std::get_if<ui::TextDrawCommand>(&command);
                   return value != nullptr && value->localization_key == "Kiko";
               }) &&
               std::ranges::any_of(
                   draw_compatibility.commands(), [](const ui::DrawCommand& command) {
                       const auto* value = std::get_if<ui::TextDrawCommand>(&command);
                       return value != nullptr && value->localization_key == "Juno";
                   }),
           "draw-result team 0 must retain both roster-owned award columns");

    expect(std::ranges::none_of(
               draw.commands(), [](const ui::DrawCommand& command) {
                   const auto* sprite =
                       std::get_if<ui::SpriteDrawCommand>(&command);
                   return sprite != nullptr &&
                          sprite->asset_id.ends_with(
                              "view_game_stats_rankup_bar_stroke.png");
               }),
           "the removed five-at-once rank bars must not masquerade as retail timing");

    results.tick(MatchResultsModel::rank_up_initial_delay_seconds);
    expect(!results.rank_up_frame().has_value() &&
               results.rank_ups().size() == 2U,
           "retail must hold the footer for 5.5 seconds before activating a rank-up");
    results.tick(1.0 / 60.0);
    auto frame_state = results.rank_up_frame();
    expect(frame_state.has_value() &&
               frame_state->rank_up.old_score == 40 &&
               frame_state->rank_up.new_score == 41 &&
               frame_state->interpolated_score == 40.0 &&
               frame_state->opacity == 0.0 &&
               !frame_state->last_rank_up &&
               results.rank_ups().size() == 1U,
           "rank-up activation must pop the last queued entry at zero opacity");

    const auto rank_draw = MatchResultsPresentation{}.build(
        results, state, "TEAM DEATHMATCH!", roster,
        ui::PixelExtent{1280, 720});
    const auto rank_stroke = std::ranges::find_if(
        rank_draw.commands(), [](const ui::DrawCommand& command) {
            const auto* value = std::get_if<ui::SpriteDrawCommand>(&command);
            return value != nullptr && value->asset_id.ends_with(
                                           "view_game_stats_rankup_bar_stroke.png");
        });
    expect(rank_stroke != rank_draw.commands().end(),
           "the active one-at-a-time state must restore the authored rank stroke");
    const auto* rank_stroke_sprite =
        std::get_if<ui::SpriteDrawCommand>(&*rank_stroke);
    expect(rank_stroke_sprite != nullptr &&
               rank_stroke_sprite->destination ==
                   ui::DrawRect{71.0, 556.0, 658.0, 24.0} &&
               rank_stroke_sprite->retail_source_scale == 0.64 &&
               rank_stroke_sprite->retail_source_anchor ==
                   ui::TextureAnchor::top_left &&
               rank_stroke_sprite->modulation.opacity_per_mille == 0U,
           "rank stroke must preserve the recovered .64 load and blit(71,20)");
    const auto rank_label = std::ranges::find_if(
        rank_draw.commands(), [](const ui::DrawCommand& command) {
            const auto* value = std::get_if<ui::TextDrawCommand>(&command);
            return value != nullptr && value->localization_key == "TDM_Kill";
        });
    const auto rank_score = std::ranges::find_if(
        rank_draw.commands(), [](const ui::DrawCommand& command) {
            const auto* value = std::get_if<ui::TextDrawCommand>(&command);
            return value != nullptr && value->localization_key == "40 / 50";
        });
    const auto rank_level = std::ranges::find_if(
        rank_draw.commands(), [](const ui::DrawCommand& command) {
            const auto* value = std::get_if<ui::TextDrawCommand>(&command);
            return value != nullptr && value->localization_key == "Level 1";
        });
    expect(rank_label != rank_draw.commands().end() &&
               rank_score != rank_draw.commands().end() &&
               rank_level != rank_draw.commands().end() &&
               std::get<ui::TextDrawCommand>(*rank_label).destination ==
                   ui::DrawRect{300.0, 557.0, 200.0, 18.0} &&
               std::get<ui::TextDrawCommand>(*rank_label)
                       .requested_font_size_pixels == 14.0 &&
                std::get<ui::TextDrawCommand>(*rank_label)
                        .preferred_font_asset == "fonts/Spades.ttf" &&
                std::get<ui::TextDrawCommand>(*rank_label).fit ==
                    ui::TextFit::retail_width_scale &&
                std::get<ui::TextDrawCommand>(*rank_label).vertical_alignment ==
                    ui::VerticalTextAlignment::retail_center &&
                std::get<ui::TextDrawCommand>(*rank_score).destination ==
                    ui::DrawRect{81.0, 557.0, 98.0, 18.0} &&
                std::get<ui::TextDrawCommand>(*rank_score).fit ==
                    ui::TextFit::retail_width_scale &&
                std::get<ui::TextDrawCommand>(*rank_score).vertical_alignment ==
                    ui::VerticalTextAlignment::retail_center &&
                std::get<ui::TextDrawCommand>(*rank_level).destination ==
                    ui::DrawRect{567.0, 553.0, 150.0, 20.0} &&
                std::get<ui::TextDrawCommand>(*rank_level)
                        .horizontal_alignment ==
                    ui::HorizontalTextAlignment::right &&
                std::get<ui::TextDrawCommand>(*rank_level).fit ==
                    ui::TextFit::retail_width_scale &&
                std::get<ui::TextDrawCommand>(*rank_level).vertical_alignment ==
                    ui::VerticalTextAlignment::retail_center &&
               std::ranges::none_of(
                   rank_draw.commands(), [](const ui::DrawCommand& command) {
                       const auto* value =
                           std::get_if<ui::TextDrawCommand>(&command);
                       return value != nullptr &&
                              value->localization_key ==
                                  "Press TAB to show scores";
                   }),
           "active rank-up must replace the footer with exact label/score geometry");

    results.tick(0.25);
    frame_state = results.rank_up_frame();
    expect(frame_state.has_value() && frame_state->opacity == 0.5 &&
               frame_state->interpolated_score == 40.0,
           "rank bar must fade in while the score remains in its .75-second hold");
    results.tick(0.5);
    frame_state = results.rank_up_frame();
    expect(frame_state.has_value() && frame_state->opacity == 1.0 &&
               frame_state->interpolated_score == 40.0,
           "the interpolation must begin only after the recovered pre-delay");
    results.tick(0.75);
    frame_state = results.rank_up_frame();
    expect(frame_state.has_value() &&
               frame_state->interpolated_score == 40.5,
           "score interpolation must be linear across exactly 1.5 seconds");
    results.tick(0.75);
    frame_state = results.rank_up_frame();
    expect(frame_state.has_value() &&
               frame_state->interpolated_score == 41.0,
           "rank score must settle on the authoritative new value");
    results.tick(2.0);
    expect(!results.rank_up_frame().has_value(),
           "the completed bar must disappear after the 4.25-second total");
    results.tick(1.0 / 60.0);
    frame_state = results.rank_up_frame();
    expect(frame_state.has_value() && frame_state->rank_up.old_score == 20 &&
               frame_state->last_rank_up && results.rank_ups().empty(),
           "the next update must pop the final queued rank-up and mark it last");

    MatchResultsModel level_up_results;
    network::RankUpsPacket level_up_packet;
    level_up_packet.entries = {{1, 49, 51}};
    level_up_results.apply(level_up_packet);
    level_up_results.show();
    level_up_results.tick(MatchResultsModel::rank_up_initial_delay_seconds);
    level_up_results.tick(1.0 / 60.0);
    level_up_results.tick(2.25);
    auto level_up_frame = level_up_results.rank_up_frame();
    expect(level_up_frame.has_value() && level_up_frame->level == 2U &&
               level_up_frame->level_up &&
               level_up_frame->level_up_timer == 0.0,
           "crossing a retail progression boundary must start the level-up pulse");
    level_up_results.tick(0.15);
    const auto level_up_draw = MatchResultsPresentation{}.build(
        level_up_results, state, "TEAM DEATHMATCH!", roster,
        ui::PixelExtent{1280, 720});
    const auto glow = std::ranges::find_if(
        level_up_draw.commands(), [](const ui::DrawCommand& command) {
            const auto* value = std::get_if<ui::SpriteDrawCommand>(&command);
            return value != nullptr && value->asset_id.ends_with(
                                           "view_game_stats_content_stroke_glow.png");
        });
    expect(glow != level_up_draw.commands().end() &&
               std::get<ui::SpriteDrawCommand>(*glow).destination ==
                   ui::DrawRect{68.0, 553.0, 664.0, 30.0} &&
               std::get<ui::SpriteDrawCommand>(*glow)
                       .modulation.opacity_per_mille == 500U,
           "level-up must pulse the authored glow at recovered blit(68,17)");

    const auto level_number = std::ranges::find_if(
        level_up_draw.commands(), [](const ui::DrawCommand& command) {
            const auto* value = std::get_if<ui::TextDrawCommand>(&command);
            return value != nullptr && value->requested_font_size_pixels == 72.0;
        });
    expect(level_number != level_up_draw.commands().end(),
           "a level transition must draw retail's separate 72px level number");
    const auto& level_number_command =
        std::get<ui::TextDrawCommand>(*level_number);
    expect(level_number_command.localization_key == "2" &&
               level_number_command.preferred_font_asset ==
                   "fonts/Spades.ttf" &&
               level_number_command.destination ==
                   ui::DrawRect{714.0, 574.0, 0.0, 0.0} &&
               level_number_command.horizontal_alignment ==
                   ui::HorizontalTextAlignment::left &&
               level_number_command.vertical_alignment ==
                   ui::VerticalTextAlignment::baseline &&
               level_number_command.fit == ui::TextFit::none &&
               level_number_command.modulation.color ==
                   ui::ColorRgba8{104U, 173U, 87U, 255U} &&
               std::abs(level_number_command.geometric_scale - 2.175) <
                   0.000001,
           "level text must retain retail's baseline anchor, colour and cached-glyph scale");

    MatchResultsModel initial_level_scale;
    initial_level_scale.apply(level_up_packet);
    initial_level_scale.show();
    initial_level_scale.tick(MatchResultsModel::rank_up_initial_delay_seconds);
    initial_level_scale.tick(1.0 / 60.0);
    initial_level_scale.tick(2.25);
    const auto initial_level_draw = MatchResultsPresentation{}.build(
        initial_level_scale, state, "TEAM DEATHMATCH!", roster,
        ui::PixelExtent{800, 600});
    const auto initial_level_number = std::ranges::find_if(
        initial_level_draw.commands(), [](const ui::DrawCommand& command) {
            const auto* value = std::get_if<ui::TextDrawCommand>(&command);
            return value != nullptr && value->requested_font_size_pixels == 72.0;
        });
    expect(initial_level_number != initial_level_draw.commands().end() &&
               std::get<ui::TextDrawCommand>(*initial_level_number)
                       .geometric_scale == 0.3,
           "the exact transition frame must begin at retail's discontinuous 0.3 scale");

    initial_level_scale.tick(0.01);
    const auto clamped_level_draw = MatchResultsPresentation{}.build(
        initial_level_scale, state, "TEAM DEATHMATCH!", roster,
        ui::PixelExtent{800, 600});
    const auto clamped_level_number = std::ranges::find_if(
        clamped_level_draw.commands(), [](const ui::DrawCommand& command) {
            const auto* value = std::get_if<ui::TextDrawCommand>(&command);
            return value != nullptr && value->requested_font_size_pixels == 72.0;
        });
    expect(clamped_level_number != clamped_level_draw.commands().end() &&
               std::get<ui::TextDrawCommand>(*clamped_level_number)
                       .geometric_scale == 3.0,
           "the first positive timer frame must clamp the retail scale to 3.0");

    initial_level_scale.tick(0.19);
    const auto midpoint_level_draw = MatchResultsPresentation{}.build(
        initial_level_scale, state, "TEAM DEATHMATCH!", roster,
        ui::PixelExtent{800, 600});
    const auto midpoint_level_number = std::ranges::find_if(
        midpoint_level_draw.commands(), [](const ui::DrawCommand& command) {
            const auto* value = std::get_if<ui::TextDrawCommand>(&command);
            return value != nullptr && value->requested_font_size_pixels == 72.0;
        });
    expect(midpoint_level_number != midpoint_level_draw.commands().end() &&
               std::abs(std::get<ui::TextDrawCommand>(*midpoint_level_number)
                                .geometric_scale -
                        1.8) < 0.000001,
           "the 0.2-second level frame must use retail's exact 1.8 scale");

    initial_level_scale.tick(0.2);
    const auto ending_level_draw = MatchResultsPresentation{}.build(
        initial_level_scale, state, "TEAM DEATHMATCH!", roster,
        ui::PixelExtent{800, 600});
    const auto ending_level_number = std::ranges::find_if(
        ending_level_draw.commands(), [](const ui::DrawCommand& command) {
            const auto* value = std::get_if<ui::TextDrawCommand>(&command);
            return value != nullptr && value->requested_font_size_pixels == 72.0;
        });
    expect(ending_level_number != ending_level_draw.commands().end() &&
               std::abs(std::get<ui::TextDrawCommand>(*ending_level_number)
                                .geometric_scale -
                        0.3) < 0.000001,
           "the 0.4-second level frame must settle back to retail's 0.3 scale");

    initial_level_scale.tick(0.001);
    const auto expired_level_draw = MatchResultsPresentation{}.build(
        initial_level_scale, state, "TEAM DEATHMATCH!", roster,
        ui::PixelExtent{800, 600});
    expect(std::ranges::none_of(
               expired_level_draw.commands(), [](const ui::DrawCommand& command) {
                   const auto* value =
                       std::get_if<ui::TextDrawCommand>(&command);
                   return value != nullptr &&
                          value->requested_font_size_pixels == 72.0;
               }),
           "retail must clear level_up after the timer exceeds 0.4 seconds");

    results.clear();
    expect(!results.visible() && results.awards().empty() &&
               results.rank_ups().empty() &&
               !results.rank_up_frame().has_value(),
           "scene teardown must remove prior-map awards");
}

void end_results_select_the_local_win_or_lose_sting() {
    using battlespades::frontend::match_result_audio_stem;
    expect(match_result_audio_stem(2U, 2U) ==
               std::optional<std::string_view>{"mu_win_game"} &&
               match_result_audio_stem(2U, 3U) ==
                   std::optional<std::string_view>{"mu_lose_game"},
           "winner team must select the local retail win/lose sting");
    expect(!match_result_audio_stem(0U, 2U).has_value() &&
               !match_result_audio_stem(2U, 0U).has_value(),
           "draws and spectators must not receive a false win/lose sting");
}

void end_results_derive_winner_from_authoritative_scores() {
    ChangeTeamServerState scores;
    scores.team1_score = 21;
    scores.team2_score = 17;
    expect(match_result_winner_from_scores(scores) == 2,
           "higher blue score must select wire team 2");
    scores.team1_score = 4;
    scores.team2_score = 9;
    expect(match_result_winner_from_scores(scores) == 3,
           "higher green score must select wire team 3");
    scores.team1_score = 12;
    scores.team2_score = 12;
    expect(match_result_winner_from_scores(scores) == 0,
           "equal final scores must remain a draw");

    MatchResultsModel retail_results;
    network::GameStatsPacket team1;
    team1.team_id = 2;
    team1.entries = {{1, 5}};
    retail_results.apply(team1);
    network::GameStatsPacket team2;
    team2.team_id = 3;
    team2.entries = {{2, 8}};
    retail_results.apply(team2);
    retail_results.show();
    expect(retail_results.winner_team() == 0,
           "two retail team-stat packets must never invent a winner");

    MatchResultsModel compatibility_results;
    compatibility_results.apply(team1);
    compatibility_results.show();
    expect(compatibility_results.winner_team() == 2,
           "one legacy mixed-stat packet may use its team as a compatibility winner");
}

void same_map_score_reset_discards_only_unshown_results() {
    MatchResultsModel hidden;
    network::GameStatsPacket round_one;
    round_one.team_id = 2U;
    round_one.entries = {{1U, 5}};
    hidden.apply(round_one);

    hidden.observe_team_score(2U, 0);
    expect(hidden.awards().size() == 1U && !hidden.visible(),
           "one zero team score must not guess a same-map round boundary");
    hidden.observe_team_score(3U, 4);
    hidden.observe_team_score(3U, 0);
    expect(hidden.awards().size() == 1U,
           "a nonzero score update must invalidate an incomplete reset pair");
    hidden.observe_team_score(2U, 0);
    expect(hidden.awards().empty() && !hidden.visible(),
           "paired team-zero SetScore packets must discard hidden same-map stats");

    network::GameStatsPacket next_round;
    next_round.team_id = 3U;
    next_round.entries = {{2U, 9}};
    hidden.apply(next_round);
    hidden.show(3);
    hidden.observe_team_score(2U, 0);
    hidden.observe_team_score(3U, 0);
    expect(hidden.visible() && hidden.awards().size() == 1U,
           "a visible retail end screen must survive later score traffic");
    hidden.on_map_ended(2);
    expect(hidden.visible() && hidden.awards().size() == 1U &&
               hidden.winner_team() == 3,
           "MapEnded must preserve an already-visible result and its resolved winner");
    hidden.clear();
    expect(!hidden.visible() && hidden.awards().empty(),
           "only scene teardown may clear a visible result screen");

    MatchResultsModel terminal_without_packet53;
    terminal_without_packet53.apply(round_one);
    terminal_without_packet53.on_map_ended(2);
    expect(terminal_without_packet53.visible() &&
               terminal_without_packet53.winner_team() == 2 &&
               terminal_without_packet53.awards().size() == 1U,
           "MapEnded/StateData must activate retained results when packet 53 was missed");
}

void map_ended_preserves_vote_and_chat_layers() {
    MatchResultsModel results;
    network::GameStatsPacket statistics;
    statistics.team_id = 2U;
    statistics.entries = {{1U, 5}};
    results.apply(statistics);

    GameChatModel chat;
    chat.add("Next map ballot is closing");
    chat.begin(ChatChannel::global);
    expect(chat.append_text("unfinished"),
           "terminal-overlay fixture must begin with active chat input");

    GenericVotingModel vote;
    network::GenericVoteMessagePacket ballot;
    ballot.message_type = network::GenericVoteMessagePacket::start;
    ballot.can_vote = true;
    ballot.allow_revote = true;
    ballot.title = "('VOTE_MAP_TITLE', ())";
    ballot.description = "('VOTE_MAP_DESCRIPTION', ())";
    ballot.candidates = {{"('MAP_NAME', ('London',))", 2},
                         {"('MAP_NAME', ('Castle Wars',))", 1}};
    vote.apply(ballot);
    expect(vote.visible(),
           "terminal-overlay fixture must begin with a visible ballot");

    apply_map_ended_overlay_boundary(results, chat, vote, 2);
    expect(results.visible() && results.winner_team() == 2,
           "MapEnded must activate the retained ViewGameStats batch");
    expect(!chat.active() && chat.entries().size() == 1U,
           "MapEnded must close only the chat editor and preserve chat history");
    expect(vote.visible() && vote.choices().size() == 2U,
           "MapEnded must not erase GenericVotingHUD before scene teardown");

    const std::array<std::string, 3U> keys{"F1", "F2", "F3"};
    expect(!GameChatPresentation{}
                .build(chat, ui::PixelExtent{800, 600})
                .empty() &&
               !GenericVotingPresentation{}
                    .build(vote, ui::PixelExtent{800, 600}, keys)
                    .empty(),
           "chat and vote must remain drawable above terminal results");
}

void end_results_camera_matches_retail_pan_controller() {
    const std::array<std::array<double, 3U>, 1U> points{{
        {10.0, 20.0, 30.0},
    }};
    const std::array<std::array<double, 3U>, 1U> rotations{{
        {0.0, 0.0, 0.0},
    }};

    const auto start = resolve_match_result_camera(points, rotations, 0U, 0.0);
    expect(start.has_value() && std::abs(start->eye[0U] - 10.0) < 0.000001 &&
               std::abs(start->eye[1U] - 20.0) < 0.000001 &&
               std::abs(start->eye[2U] - 30.0) < 0.000001 &&
               std::abs(start->yaw_degrees + 90.0) < 0.000001 &&
               std::abs(start->pitch_degrees) < 0.000001,
           "the first screenshot-camera frame must use the authored point and converted yaw");

    const auto midpoint =
        resolve_match_result_camera(points, rotations, 0U, 2.5);
    expect(midpoint.has_value() &&
               std::abs(midpoint->eye[0U] - 10.0) < 0.000001 &&
               std::abs(midpoint->eye[1U] - 17.5) < 0.000001 &&
               std::abs(midpoint->eye[2U] - 30.0) < 0.000001,
           "PanController must linearly move half of its five-block reverse track at 2.5 seconds");

    const std::array<std::array<double, 3U>, 1U> angled_rotations{{
        {30.0, 90.0, 15.0},
    }};
    const auto ending =
        resolve_match_result_camera(points, angled_rotations, 0U, 50.0);
    expect(ending.has_value() &&
               std::abs(ending->eye[0U] - (10.0 - 5.0 * std::cos(30.0 * 3.14159265358979323846 / 180.0))) <
                   0.000001 &&
               std::abs(ending->eye[1U] - 20.0) < 0.000001 &&
               std::abs(ending->eye[2U] - 27.5) < 0.000001 &&
               std::abs(ending->yaw_degrees + 180.0) < 0.000001 &&
               std::abs(ending->pitch_degrees - 30.0) < 0.000001 &&
               std::abs(ending->roll_degrees - 15.0) < 0.000001,
           "authored pitch/yaw/roll and the clamped five-second endpoint must survive conversion");

    const auto before_start =
        resolve_match_result_camera(points, rotations, 0U, -10.0);
    expect(before_start.has_value() &&
               std::abs(before_start->eye[1U] - 20.0) < 0.000001,
           "negative pan time must clamp to the authored start point");

    const std::array<std::array<double, 3U>, 1U> placeholder_points{{
        {0.0, 0.0, 0.0},
    }};
    const std::array<std::array<double, 3U>, 1U> placeholder_rotations{{
        {0.0, 0.0, 0.0},
    }};
    expect(!resolve_match_result_camera(
                placeholder_points, placeholder_rotations, 0U, 0.0)
                .has_value(),
           "the historical all-zero revival camera must never pull the world view to map origin");
    expect(!resolve_match_result_camera(
                points, std::span<const std::array<double, 3U>>{}, 0U, 0.0)
                .has_value() &&
               !resolve_match_result_camera(points, rotations, 1U, 0.0)
                    .has_value(),
           "unpaired and out-of-range screenshot cameras must fail closed");

    auto malformed_points = points;
    malformed_points[0U][1U] = std::numeric_limits<double>::quiet_NaN();
    expect(!resolve_match_result_camera(
                malformed_points, rotations, 0U, 0.0)
                .has_value(),
           "non-finite screenshot-camera input must not reach the renderer");
}

void scoreboard_mode_titles_follow_retail_mode_tables() {
    using battlespades::frontend::retail_scoreboard_mode_title;
    constexpr std::array expected{
        std::string_view{"Scores"},
        std::string_view{"Demolition!"},
        std::string_view{"Zombie!"},
        std::string_view{"Multi-Hill!"},
        std::string_view{"Occupation!"},
        std::string_view{"Diamond Mine!"},
        std::string_view{"Team Deathmatch!"},
        std::string_view{"VIP"},
        std::string_view{"Capture the Flag"},
        std::string_view{"Territory Control"},
        std::string_view{"Tutorial"},
        std::string_view{"Classic CTF"},
        std::string_view{"Map Creator"},
    };
    for (std::size_t index{}; index < expected.size(); ++index) {
        expect(retail_scoreboard_mode_title(static_cast<std::uint8_t>(index), false) ==
                   expected[index],
               "every retail MODE_TITLE ordinal must resolve to its authored English title");
    }
    expect(retail_scoreboard_mode_title(8U, true) == "Classic CTF" &&
               retail_scoreboard_mode_title(250U, false) == "Scores",
           "InitialInfo.classic must select CLASSIC_CTF_TITLE and malformed modes must fail closed");
}

} // namespace

int main() {
    try {
        chat_is_bounded_and_utf8_safe();
        chat_presentation_matches_retail_geometry_stroke_and_fade();
        player_chat_preserves_retail_sender_and_body_labels();
        vote_decoding_and_cast_are_crash_safe();
        vote_updates_keep_confirmation_and_closed_results();
        map_vote_closed_result_names_the_authoritative_next_map();
        end_results_reject_malformed_awards_and_preserve_draws();
        end_results_retain_award_winners_after_disconnect();
        end_results_group_the_server_snapshot_by_roster_team();
        end_results_select_the_local_win_or_lose_sting();
        end_results_derive_winner_from_authoritative_scores();
        same_map_score_reset_discards_only_unshown_results();
        map_ended_preserves_vote_and_chat_layers();
        end_results_camera_matches_retail_pan_controller();
        scoreboard_mode_titles_follow_retail_mode_tables();
        std::cout << "Match overlay tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
